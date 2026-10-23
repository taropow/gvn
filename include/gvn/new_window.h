#pragma once
#include "core.h"
#include <windows.h>
#include <optional>
#include <span>
#include <variant>

namespace gvn {
// Empty window, original source file, or a detached single clipboard frame.
// A defined X/Y resolution accompanies the detached frame without changing its RGBA.
using NewWindowContent = std::variant<std::monostate, std::filesystem::path, Image>;
std::vector<uint8_t> encode_new_window(const NewWindowContent &content);
NewWindowContent decode_new_window(std::span<const uint8_t> bytes);
struct NewWindowStartup {
    std::filesystem::path settings;
    NewWindowContent content;
};
// Only the internal --new-window form is consumed. Its mapping handle is closed on receipt.
std::optional<NewWindowStartup> read_new_window_arguments(int count, wchar_t *const *arguments);
std::filesystem::path viewer_executable();
class ViewerProcess {
    HANDLE process_{}, thread_{};

  public:
    explicit ViewerProcess(PROCESS_INFORMATION info) : process_(info.hProcess), thread_(info.hThread) {}
    ~ViewerProcess();
    ViewerProcess(ViewerProcess &&other) noexcept;
    ViewerProcess(const ViewerProcess &) = delete;
    ViewerProcess &operator=(const ViewerProcess &) = delete;
    HANDLE handle() const { return process_; }
    DWORD id() const { return GetProcessId(process_); }
};
// Inherits only a read-only anonymous snapshot mapping, on the calling thread's desktop.
// Releasing the returned process handle leaves the independent viewer running.
ViewerProcess launch_new_viewer(const std::filesystem::path &executable,
                                const std::filesystem::path &settings,
                                const NewWindowContent &content);
} // namespace gvn
