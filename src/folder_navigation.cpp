#include "gvn/folder_navigation.h"
#include <windows.h>
#include <algorithm>
#include <stdexcept>

namespace gvn {
namespace {
namespace fs = std::filesystem;
bool equal(const fs::path &a, const fs::path &b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
bool contains(fs::path directory, const fs::path &root) {
    for (;;) {
        if (equal(directory, root))
            return true;
        const auto parent = directory.parent_path();
        if (parent.empty() || equal(parent, directory))
            return false;
        directory = parent;
    }
}
fs::path normalized(const fs::path &path) {
    auto value = fs::absolute(path).lexically_normal();
    while (value != value.root_path() && value.filename().empty())
        value = value.parent_path();
    return value;
}
fs::path volume_root(const fs::path &directory) {
    auto fallback = directory.root_path();
    // std::filesystem's UNC root_path ends at the server, before the share component.
    if (directory.root_name().native().starts_with(L"\\\\") && !directory.relative_path().empty())
        fallback /= *directory.relative_path().begin();
    fallback = normalized(fallback);
    std::wstring text(32768, L'\0');
    if (GetVolumePathNameW(directory.c_str(), text.data(), static_cast<DWORD>(text.size()))) {
        text.resize(wcslen(text.c_str()));
        const auto root = normalized(text);
        if (contains(directory, root)) {
            if (equal(root, fallback))
                return root;
            // DOS aliases can make GetVolumePathName report an ordinary folder.
            // Accept a deeper boundary only when it is a real volume mount point.
            auto mount = root.native();
            if (mount.back() != L'\\')
                mount += L'\\';
            wchar_t volume[MAX_PATH]{};
            if (GetVolumeNameForVolumeMountPointW(mount.c_str(), volume, MAX_PATH))
                return root;
        }
    }
    return fallback;
}
bool visible(DWORD attributes, FileVisibility policy) {
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return false;
    const bool hidden = (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    const bool system = (attributes & FILE_ATTRIBUTE_SYSTEM) != 0;
    return hidden && system ? policy.protected_system : hidden ? policy.hidden : !system || policy.system;
}
struct Frame {
    fs::path parent;
    std::vector<FileEntry> children;
    size_t index{};
};
class Search {
    const FolderQuery &query;
    const std::function<bool()> &cancel;
    FolderMatch result;
    fs::path current, root;
    std::vector<Frame> spine;
    bool stopped() {
        if (cancel && cancel())
            result.cancelled = true;
        return result.cancelled;
    }
    std::vector<FileEntry> children(const fs::path &parent, const fs::path &anchor = {},
                                    bool allow_reparse = false) {
        std::vector<FileEntry> list;
        std::error_code error;
        for (fs::directory_iterator it(parent, fs::directory_options::skip_permission_denied, error), end;
             !error && it != end; it.increment(error)) {
            if (stopped())
                break;
            const auto path = it->path();
            const auto attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            if ((!anchor.empty() && equal(path, anchor)) ||
                (visible(attributes, query.visibility) &&
                 (allow_reparse || !(attributes & FILE_ATTRIBUTE_REPARSE_POINT))))
                list.push_back({path});
        }
        sort_files(list, Sort::Name, false, query.natural);
        return list;
    }
    std::vector<FileEntry> collect(const fs::path &directory) {
        std::vector<FileEntry> files;
        std::error_code error;
        for (fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, error), end;
             !error && it != end; it.increment(error)) {
            if (stopped())
                return {};
            if (!supported(it->path()))
                continue;
            const auto attributes = GetFileAttributesW(it->path().c_str());
            if (!visible(attributes, query.visibility) || (attributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            const auto bytes = it->file_size(error);
            if (error) {
                error.clear();
                continue;
            }
            const auto time = it->last_write_time(error);
            if (error) {
                error.clear();
                continue;
            }
            files.push_back({it->path(), bytes, time.time_since_epoch().count()});
        }
        if (!stopped())
            sort_files(files, query.sort, query.reverse, query.natural, query.seed);
        return files;
    }
    bool has_image(const fs::path &directory) {
        ++result.examined;
        if (query.collect_files) {
            auto files = collect(directory);
            if (stopped() || files.empty())
                return false;
            result.files = std::move(files);
            return true;
        }
        std::error_code error;
        for (fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, error), end;
             !error && it != end; it.increment(error)) {
            if (stopped())
                return false;
            if (!supported(it->path()))
                continue;
            const auto attributes = GetFileAttributesW(it->path().c_str());
            if (visible(attributes, query.visibility) && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                return true;
        }
        return false;
    }
    bool next() {
        auto list = children(current);
        if (stopped())
            return false;
        if (!list.empty()) {
            spine.push_back({current, std::move(list), 0});
            current = spine.back().children.front().path;
            return true;
        }
        while (!spine.empty()) {
            auto &frame = spine.back();
            if (++frame.index < frame.children.size()) {
                current = frame.children[frame.index].path;
                return true;
            }
            spine.pop_back();
        }
        return false;
    }
    bool previous() {
        if (spine.empty())
            return false;
        auto &frame = spine.back();
        if (!frame.index) {
            current = frame.parent;
            spine.pop_back();
            return true;
        }
        current = frame.children[--frame.index].path;
        while (!stopped()) {
            auto list = children(current);
            if (list.empty())
                return !stopped();
            const auto last = list.size() - 1;
            spine.push_back({current, std::move(list), last});
            current = spine.back().children[last].path;
        }
        return false;
    }

  public:
    Search(const FolderQuery &q, const std::function<bool()> &stop) : query(q), cancel(stop) {}
    FolderMatch run() {
        if (query.move < FolderMove::Next || query.move > FolderMove::Parent)
            throw std::invalid_argument("Invalid folder movement");
        if (query.collect_files && (query.sort < Sort::Name || query.sort > Sort::Random))
            throw std::invalid_argument("Invalid file ordering for folder movement");
        if (stopped())
            return result;
        if (query.directory.empty())
            throw std::invalid_argument("An existing starting folder is required");
        current = normalized(query.directory);
        root = query.root.empty() ? volume_root(current) : normalized(query.root);
        if (!contains(current, root))
            throw std::invalid_argument("Starting folder is outside the navigation root");
        std::error_code error;
        if (!fs::is_directory(current, error) || !fs::is_directory(root, error))
            throw std::invalid_argument("Starting folder and root must exist");
        if (query.move == FolderMove::Parent) {
            if (!equal(current, root)) {
                result.directory = current.parent_path();
                result.examined = 1;
            }
            if (!result.directory.empty() && query.collect_files)
                result.files = collect(result.directory);
            if (stopped()) {
                result.directory.clear();
                result.files.reset();
            }
            return result;
        }
        const bool adjacent =
            query.move == FolderMove::AdjacentNext || query.move == FolderMove::AdjacentPrevious;
        std::vector<fs::path> ancestors;
        for (auto path = current; !equal(path, root); path = path.parent_path()) {
            ancestors.push_back(path);
            if (adjacent)
                break;
        }
        for (auto it = ancestors.rbegin(); it != ancestors.rend() && !stopped(); ++it) {
            auto list = children(it->parent_path(), *it, adjacent);
            const auto found = std::find_if(list.begin(), list.end(),
                                            [&](const auto &entry) { return equal(entry.path, *it); });
            if (found == list.end())
                return result; // The starting chain disappeared during discovery.
            const auto index = static_cast<size_t>(std::distance(list.begin(), found));
            spine.push_back({it->parent_path(), std::move(list), index});
        }
        if (adjacent) {
            if (!spine.empty() && !stopped()) {
                const auto &frame = spine.back();
                const auto index =
                    static_cast<int64_t>(frame.index) + (query.move == FolderMove::AdjacentNext ? 1 : -1);
                if (index >= 0 && index < static_cast<int64_t>(frame.children.size())) {
                    result.directory = frame.children[static_cast<size_t>(index)].path;
                    result.examined = 1;
                }
            }
            if (!result.directory.empty() && query.collect_files)
                result.files = collect(result.directory);
        } else {
            while (!stopped() && (query.move == FolderMove::Next ? next() : previous())) {
                if (has_image(current)) {
                    result.directory = current;
                    break;
                }
            }
            if (result.directory.empty() && !stopped() && query.wrap && query.move == FolderMove::Next) {
                // Reuse the same bounded search and cancellation checks. Include the root's
                // own images before descending, without an extra UI-thread directory scan.
                current = root;
                spine.clear();
                if (has_image(current))
                    result.directory = current;
                while (result.directory.empty() && !stopped() && next()) {
                    if (has_image(current))
                        result.directory = current;
                }
            }
        }
        if (stopped()) {
            result.directory.clear();
            result.files.reset();
        }
        return result;
    }
};
} // namespace
FolderMatch find_folder(const FolderQuery &query, const std::function<bool()> &cancel) {
    return Search(query, cancel).run();
}
} // namespace gvn
