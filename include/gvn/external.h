#pragma once
#include <windows.h>
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace gvn {
enum class ExternalSend { None, File, Folder, Clipboard, DisplayedPng };
enum class ExternalShow { Normal, Minimized, Maximized };
struct ExternalApplication {
    std::wstring name, executable, verb;
    ExternalSend send{ExternalSend::File};
    ExternalShow show{ExternalShow::Normal};
    bool quit_after_launch{}, separator{}, include_archive_member{}, archive_file_for_folder{};
    bool operator==(const ExternalApplication &) const = default;
};
using ExternalApplications = std::array<ExternalApplication, 9>;
ExternalApplications default_external_applications();
std::wstring external_application_name(std::wstring_view text);
ExternalApplication migrate_external_application(size_t index, const std::wstring &legacy_path);
size_t move_external_application(ExternalApplications &entries, size_t selected, int direction);
void reset_external_application(ExternalApplications &entries, size_t selected);

// The caller supplies paths for the displayed source, with the selected archive policy applied.
// An edited image's original path remains its source; DisplayedPng is the explicit legacy extension.
struct ExternalContext {
    std::filesystem::path viewer_directory;
    std::wstring file_path, folder_path;
    bool image_available{};
    std::optional<std::filesystem::path> displayed_png;
};
struct ExternalLaunch {
    std::wstring file, parameters, verb;
    std::filesystem::path directory;
    int show{};
    bool copy_image{}, quit_after_launch{}, explicit_arguments{};
};
std::wstring quote_external_argument(std::wstring_view text);
ExternalLaunch plan_external_application(const ExternalApplication &entry, const ExternalContext &context);
// Returns false for a copy-only/empty request; throws when Windows rejects a nonempty target.
bool launch_external_application(HWND owner, const ExternalLaunch &request);
} // namespace gvn
