#include "gvn/ime.h"
#include <algorithm>
#include <exception>
#include <stdexcept>

namespace gvn {
namespace {
constexpr auto marker = L"GVN.ViewerIme.1";
struct Changing {
    bool &flag;
    explicit Changing(bool &value) : flag(value) {
        flag = true;
    }
    ~Changing() {
        flag = false;
    }
};
bool text_input(HWND window) {
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    return _wcsicmp(name, L"Edit") == 0 || _wcsnicmp(name, L"RichEdit", 8) == 0 ||
           _wcsnicmp(name, L"ComboBox", 8) == 0;
}
} // namespace
bool ViewerIme::own(HWND window) const {
    DWORD process{};
    return window && IsWindow(window) && GetWindowThreadProcessId(window, &process) == thread_ &&
           process == GetCurrentProcessId();
}
bool ViewerIme::target(HWND window) const {
    if (!own(window) || !own(root_) || (window != root_ && !IsChild(root_, window)))
        return false;
    // Also spare an editable control's internal child windows.
    for (auto parent = window; parent && parent != root_; parent = GetParent(parent))
        if (text_input(parent))
            return false;
    return true;
}
void ViewerIme::restore_entry(const Entry &entry) noexcept {
    if (!own(entry.window) || GetPropW(entry.window, marker) != this)
        return; // HWND may have been destroyed and reused since the snapshot.
    RemovePropW(entry.window, marker);
    if (entry.previous)
        ImmAssociateContext(entry.window, entry.previous);
    else
        ImmAssociateContextEx(entry.window, nullptr, IACE_DEFAULT);
}
void ViewerIme::suppress(HWND window) {
    if (!target(window))
        return;
    auto found = std::find_if(entries_.begin(), entries_.end(), [&](const auto &entry) {
        return entry.window == window && GetPropW(window, marker) == this;
    });
    if (found == entries_.end()) {
        if (GetPropW(window, marker))
            return;
        // Allocate before changing the OS association so failure can leave it untouched.
        entries_.push_back({window});
        if (!SetPropW(window, marker, this)) {
            entries_.pop_back();
            throw std::runtime_error("Cannot track viewer IME association");
        }
        found = entries_.end() - 1;
    }
    if (const auto context = ImmAssociateContext(window, nullptr))
        found->previous = context; // Preserve a newer context after a language/profile change.
}
ViewerIme::~ViewerIme() {
    restore();
}
void ViewerIme::restore() noexcept {
    if (GetCurrentThreadId() != thread_ || changing_)
        return;
    Changing guard(changing_);
    for (const auto &entry : entries_)
        restore_entry(entry);
    entries_.clear();
    root_ = nullptr;
}
void ViewerIme::apply(HWND root, bool disabled) {
    if (GetCurrentThreadId() != thread_ || changing_)
        return;
    if (!disabled || root_ != root || !own(root))
        restore();
    if (!disabled || !own(root))
        return;
    Changing guard(changing_);
    root_ = root;
    try {
        std::erase_if(entries_, [&](const auto &entry) {
            if (target(entry.window) && GetPropW(entry.window, marker) == this)
                return false;
            restore_entry(entry);
            return true;
        });
        suppress(root);
        struct Visit {
            ViewerIme *policy;
            std::exception_ptr error;
        } visit{this};
        EnumChildWindows(
            root,
            [](HWND child, LPARAM value) -> BOOL {
                auto &visit = *reinterpret_cast<Visit *>(value);
                try {
                    visit.policy->suppress(child);
                    return TRUE;
                } catch (...) {
                    visit.error = std::current_exception();
                    return FALSE;
                }
            },
            reinterpret_cast<LPARAM>(&visit));
        if (visit.error)
            std::rethrow_exception(visit.error);
    } catch (...) {
        for (const auto &entry : entries_)
            restore_entry(entry);
        entries_.clear();
        root_ = nullptr;
        throw;
    }
}
} // namespace gvn
