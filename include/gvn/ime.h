#pragma once
#include <windows.h>
#include <imm.h>
#include <vector>

namespace gvn {
// Non-owning associations for one viewer and its readonly controls, on its UI thread.
// Owned dialogs and their text controls keep their own/default input context.
class ViewerIme {
    struct Entry {
        HWND window{};
        HIMC previous{};
    };
    DWORD thread_{GetCurrentThreadId()};
    HWND root_{};
    bool changing_{};
    std::vector<Entry> entries_;
    bool own(HWND) const;
    bool target(HWND) const;
    void suppress(HWND);
    void restore_entry(const Entry &) noexcept;

  public:
    ViewerIme() = default;
    ViewerIme(const ViewerIme &) = delete;
    ViewerIme &operator=(const ViewerIme &) = delete;
    ~ViewerIme();
    void apply(HWND root, bool disabled);
    void restore() noexcept;
};
} // namespace gvn
