#include "gvn/external.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace gvn {
namespace {
constexpr size_t max_text = 32767;
void text_valid(std::wstring_view text) {
    if (text.size() > max_text || text.find(L'\0') != std::wstring_view::npos)
        throw std::runtime_error("Invalid external application text");
}
void index_valid(size_t index) {
    if (index >= 9)
        throw std::out_of_range("External application index");
}
std::wstring_view trimmed(std::wstring_view value) {
    while (!value.empty() && iswspace(value.front())) value.remove_prefix(1);
    while (!value.empty() && iswspace(value.back())) value.remove_suffix(1);
    return value;
}
std::wstring replace_paths(std::wstring_view text, const std::wstring &file, const std::wstring &folder) {
    std::wstring result;
    for (size_t i = 0; i < text.size();) {
        if (text.substr(i, 4) == L"($f)") { result += file; i += 4; }
        else if (text.substr(i, 4) == L"($d)") { result += folder; i += 4; }
        else result += text[i++];
        if (result.size() > max_text)
            throw std::runtime_error("External application arguments are too long");
    }
    return result;
}
} // namespace

ExternalApplications default_external_applications() {
    ExternalApplications result;
    result[7].name = L"現在の ファイル を開く";
    result[7].verb = L"open";
    result[8].name = L"現在の フォルダ を開く";
    result[8].send = ExternalSend::Folder;
    return result;
}
std::wstring external_application_name(std::wstring_view text) {
    text_valid(text);
    auto length = std::min<size_t>(50, text.size());
    if (length < text.size() && length && text[length - 1] >= 0xd800 && text[length - 1] <= 0xdbff &&
        text[length] >= 0xdc00 && text[length] <= 0xdfff)
        --length;
    return std::wstring(text.substr(0, length));
}
ExternalApplication migrate_external_application(size_t index, const std::wstring &legacy_path) {
    index_valid(index);
    text_valid(legacy_path);
    auto entry = default_external_applications()[index];
    if (!legacy_path.empty()) {
        entry = {};
        entry.name = external_application_name(std::filesystem::path(legacy_path).stem().wstring());
        entry.executable = legacy_path;
        entry.send = ExternalSend::DisplayedPng;
    }
    return entry;
}
size_t move_external_application(ExternalApplications &entries, size_t selected, int direction) {
    index_valid(selected);
    if (direction != -1 && direction != 1)
        throw std::invalid_argument("External application move direction");
    if ((direction == -1 && selected == 0) || (direction == 1 && selected == entries.size() - 1))
        return selected;
    auto target = direction == -1 ? selected - 1 : selected + 1;
    std::swap(entries[selected], entries[target]);
    return target;
}
void reset_external_application(ExternalApplications &entries, size_t selected) {
    index_valid(selected);
    entries[selected] = default_external_applications()[selected];
}
std::wstring quote_external_argument(std::wstring_view text) {
    text_valid(text);
    std::wstring result = L"\"";
    size_t slashes{};
    for (const auto ch : text) {
        if (ch == L'\\') ++slashes;
        else {
            result.append(slashes * (ch == L'"' ? 2 : 1) + (ch == L'"' ? 1 : 0), L'\\');
            result += ch;
            slashes = 0;
        }
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}
ExternalLaunch plan_external_application(const ExternalApplication &entry, const ExternalContext &context) {
    for (const auto *text : {&entry.name, &entry.executable, &entry.verb, &context.file_path, &context.folder_path})
        text_valid(*text);
    text_valid(context.viewer_directory.native());
    if (!context.viewer_directory.is_absolute())
        throw std::runtime_error("External application base directory must be absolute");
    if (static_cast<unsigned>(entry.send) > static_cast<unsigned>(ExternalSend::DisplayedPng) ||
        static_cast<unsigned>(entry.show) > static_cast<unsigned>(ExternalShow::Maximized))
        throw std::runtime_error("Invalid external application option");
    ExternalLaunch result;
    result.directory = context.viewer_directory.lexically_normal();
    result.verb = entry.verb;
    result.show = entry.show == ExternalShow::Minimized ? SW_SHOWMINIMIZED :
                  entry.show == ExternalShow::Maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    result.copy_image = entry.send == ExternalSend::Clipboard && context.image_available;
    result.quit_after_launch = entry.quit_after_launch;
    const auto folder = context.folder_path.empty() ? result.directory.wstring() : context.folder_path;
    std::wstring_view executable = trimmed(entry.executable), arguments;
    if (!executable.empty() && executable.front() == L'"') {
        const auto end = executable.find(L'"', 1);
        if (end == std::wstring_view::npos)
            throw std::runtime_error("Unclosed external executable quote");
        arguments = trimmed(executable.substr(end + 1));
        executable = executable.substr(1, end - 1);
        if (executable.empty())
            throw std::runtime_error("Empty quoted external executable");
        result.explicit_arguments = !arguments.empty();
    }
    if (executable.find(L'"') != std::wstring_view::npos)
        throw std::runtime_error("Quote inside external executable path");
    if (!executable.empty()) {
        auto path = std::filesystem::path(executable);
        if (path.has_root_name() && !path.has_root_directory())
            throw std::runtime_error("Drive-relative external executable is ambiguous");
        result.file = (path.is_absolute() ? path : result.directory / path).lexically_normal().wstring();
    }
    if (result.explicit_arguments)
        result.parameters = replace_paths(arguments, context.file_path, folder);
    else {
        std::wstring sent;
        if (entry.send == ExternalSend::File) sent = context.file_path;
        else if (entry.send == ExternalSend::Folder) sent = folder;
        else if (entry.send == ExternalSend::DisplayedPng && context.image_available) {
            if (!context.displayed_png || !context.displayed_png->is_absolute())
                throw std::runtime_error("Displayed image PNG has not been prepared");
            text_valid(context.displayed_png->native());
            sent = context.displayed_png->wstring();
        }
        if (!sent.empty()) {
            if (result.file.empty()) result.file = std::move(sent);
            else result.parameters = quote_external_argument(sent);
        }
    }
    if (result.file.size() + result.parameters.size() + 4 > max_text)
        throw std::runtime_error("External application command line is too long");
    return result;
}
} // namespace gvn
