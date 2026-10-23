#include "gvn/open_source.h"
#include <windows.h>

namespace gvn {
namespace {
std::vector<FileEntry> collect(const std::filesystem::path &directory, FileVisibility visibility,
                               const std::function<bool()> &cancel) {
    std::vector<FileEntry> files;
    std::error_code error;
    for (std::filesystem::directory_iterator
             it(directory, std::filesystem::directory_options::skip_permission_denied, error),
         end;
         !error && it != end; it.increment(error)) {
        if (cancel && cancel())
            return {};
        if (!supported(it->path()))
            continue;
        const auto attributes = GetFileAttributesW(it->path().c_str());
        const bool hidden = (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        const bool system = (attributes & FILE_ATTRIBUTE_SYSTEM) != 0;
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (hidden && system ? !visibility.protected_system
             : hidden         ? !visibility.hidden
                              : system && !visibility.system))
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
    return files;
}
bool equal(const std::filesystem::path &a, const std::filesystem::path &b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
}
} // namespace
OpenMatch prepare_open(const OpenQuery &query, const std::function<bool()> &cancel,
                       const OpenLister &lister) {
    OpenMatch result;
    const auto stopped = [&] {
        if (cancel && cancel()) {
            result.cancelled = true;
            result.path.clear();
            result.files.clear();
            result.entries.clear();
        }
        return result.cancelled;
    };
    if (stopped())
        return result;
    auto absolute = std::filesystem::absolute(query.path).lexically_normal();
    if (query.missing_fallback) {
        const auto source = source_file(absolute);
        std::error_code error;
        const bool exists = std::filesystem::exists(source, error);
        if (error && error != std::errc::no_such_file_or_directory && error != std::errc::not_a_directory)
            throw std::filesystem::filesystem_error("Cannot inspect previous image", source, error);
        if (!exists) {
            absolute = source.parent_path();
            result.folder = absolute;
            error.clear();
            if (!std::filesystem::is_directory(absolute, error)) {
                if (error && error != std::errc::no_such_file_or_directory &&
                    error != std::errc::not_a_directory)
                    throw std::filesystem::filesystem_error("Cannot inspect previous image folder", absolute,
                                                            error);
                return result;
            }
        }
    }
    const auto text = absolute.wstring();
    const bool member = text.find(L'|') != std::wstring::npos;
    const bool directory = !member && std::filesystem::is_directory(absolute);
    if (stopped())
        return result;
    if (!directory && (member || _wcsicmp(absolute.extension().c_str(), L".zip") == 0)) {
        result.archive = source_file(absolute);
        Archive zip(read_file(result.archive));
        if (stopped())
            return result;
        result.entries = zip.entries(cancel);
        if (stopped())
            return result;
        result.folder = result.archive.parent_path();
        if (result.entries.empty()) {
            if (query.missing_fallback)
                return result;
            throw std::runtime_error("ZIP contains no supported images");
        }
        for (const auto &entry : result.entries) {
            if (stopped())
                return result;
            result.files.push_back(
                {std::filesystem::path(result.archive.wstring() + L"|" + std::to_wstring(entry.index)),
                 entry.bytes, entry.time, entry.name});
        }
    } else {
        result.folder = directory ? absolute : absolute.parent_path();
        result.files = lister ? lister(result.folder, query.visibility, cancel)
                              : collect(result.folder, query.visibility, cancel);
    }
    if (stopped())
        return result;
    sort_files(result.files, query.sort, query.reverse, query.natural, query.seed);
    if (stopped())
        return result;
    if (directory || (!result.archive.empty() && !member)) {
        if (!result.files.empty()) {
            result.index = query.last ? result.files.size() - 1 : 0;
            result.path = result.files[result.index].path;
        }
    } else {
        result.path = absolute;
        bool found{};
        for (size_t i = 0; i < result.files.size(); ++i)
            if (equal(result.files[i].path, absolute)) {
                result.index = i;
                found = true;
                break;
            }
        if (member && query.missing_fallback) {
            if (!query.archive_entry.empty()) {
                found = false;
                for (size_t i = 0; i < result.files.size(); ++i)
                    if (result.files[i].display_name == query.archive_entry) {
                        result.index = i;
                        found = true;
                        break;
                    }
            }
            if (!found)
                result.index = 0;
            result.path = result.files[result.index].path;
        } else if (member && !found)
            throw std::runtime_error("ZIP entry not found in supported image list");
    }
    return result;
}
} // namespace gvn
