#include "gvn/ime.h"
#include <string>
#include "test.h"
#include "test_desktop.h"
#include <future>

namespace {
struct Window {
    HWND value{};
    Window(const wchar_t *type = L"Static", HWND parent = nullptr, bool child = false) {
        value = CreateWindowExW(0, type, L"IME audit", child ? WS_CHILD : WS_OVERLAPPEDWINDOW, 0, 0, 200, 100,
                                parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        CHECK(value);
    }
    ~Window() {
        if (IsWindow(value))
            DestroyWindow(value);
    }
    void destroy() {
        CHECK(DestroyWindow(value));
        value = nullptr;
    }
};
HIMC current(HWND window) {
    const auto context = ImmGetContext(window);
    if (context)
        CHECK(ImmReleaseContext(window, context));
    return context;
}
struct Context {
    HWND window;
    HIMC value{ImmCreateContext()}, previous{};
    explicit Context(HWND target) : window(target) {
        CHECK(value);
        previous = ImmAssociateContext(window, value);
        CHECK(current(window) == value);
    }
    ~Context() {
        if (IsWindow(window))
            ImmAssociateContext(window, previous);
        ImmDestroyContext(value);
    }
};
} // namespace

TEST(readonly_descendants_disabled_but_owned_and_inline_edit_controls_keep_context) {
    Window root, bar(L"Static", root.value, true), nested(L"Static", bar.value, true),
        edit(L"Edit", root.value, true), popup(L"Static", root.value), popup_edit(L"Edit", popup.value, true);
    CHECK(!IsChild(root.value, popup.value) && IsChild(root.value, nested.value));
    Context root_context(root.value), bar_context(bar.value), nested_context(nested.value),
        edit_context(edit.value), popup_context(popup.value), popup_edit_context(popup_edit.value);
    gvn::ViewerIme policy;
    policy.apply(root.value, true);
    CHECK(!current(root.value) && !current(bar.value) && !current(nested.value));
    CHECK(current(edit.value) == edit_context.value);
    CHECK(current(popup.value) == popup_context.value &&
          current(popup_edit.value) == popup_edit_context.value);
    for (int i = 0; i < 8; ++i)
        policy.apply(root.value, true);
    policy.apply(root.value, false);
    CHECK(current(root.value) == root_context.value && current(bar.value) == bar_context.value &&
          current(nested.value) == nested_context.value);
    CHECK(current(edit.value) == edit_context.value && current(popup_edit.value) == popup_edit_context.value);
    policy.apply(root.value, true);
    policy.restore();
    CHECK(current(root.value) == root_context.value && current(bar.value) == bar_context.value);
}
TEST(new_child_and_destroyed_handles_restore_only_their_current_contexts) {
    Window root, old(L"Static", root.value, true), replacement(L"Static", root.value, true);
    Context root_context(root.value), old_context(old.value), replacement_context(replacement.value);
    gvn::ViewerIme policy;
    policy.apply(root.value, true);
    CHECK(!current(old.value));
    old.destroy();
    // Existing children and future children both need a fresh apply after creation/rebuild.
    policy.apply(root.value, true);
    policy.apply(root.value, false);
    CHECK(current(replacement.value) == replacement_context.value);
    {
        Window later(L"Static", root.value, true);
        Context later_context(later.value);
        policy.apply(root.value, true);
        CHECK(!current(later.value));
        policy.apply(root.value, false);
        CHECK(current(later.value) == later_context.value);
    }
    CHECK(current(root.value) == root_context.value);
}
TEST(language_profile_reattachment_is_disabled_and_latest_context_is_restored) {
    Window root;
    Context original(root.value), replacement(root.value);
    ImmAssociateContext(root.value, original.value);
    gvn::ViewerIme policy;
    policy.apply(root.value, true);
    CHECK(!current(root.value));
    ImmAssociateContext(root.value, replacement.value);
    policy.apply(root.value, true);
    CHECK(!current(root.value));
    policy.apply(root.value, false);
    CHECK(current(root.value) == replacement.value);
}
TEST(reparented_controls_are_restored_and_unrelated_windows_are_unchanged) {
    Window root, other, child(L"Static", root.value, true);
    Context root_context(root.value), other_context(other.value), child_context(child.value);
    gvn::ViewerIme policy;
    policy.apply(root.value, true);
    CHECK(!current(child.value) && current(other.value) == other_context.value);
    CHECK(SetParent(child.value, other.value) == root.value);
    policy.apply(root.value, true);
    CHECK(current(child.value) == child_context.value && current(other.value) == other_context.value);
    policy.restore();
    CHECK(current(root.value) == root_context.value);
}
TEST(policy_cannot_mutate_its_associations_from_another_thread) {
    Window root;
    Context context(root.value);
    gvn::ViewerIme policy;
    std::async(std::launch::async, [&] { policy.apply(root.value, true); }).get();
    CHECK(current(root.value) == context.value);
    policy.apply(root.value, true);
    CHECK(!current(root.value));
    std::async(std::launch::async, [&] { policy.restore(); }).get();
    CHECK(!current(root.value));
    policy.restore();
    CHECK(current(root.value) == context.value);
}
TEST(scope_exit_restores_associations_and_destroyed_root_is_not_reused) {
    Window old, next;
    Context old_context(old.value), next_context(next.value);
    {
        gvn::ViewerIme policy;
        policy.apply(old.value, true);
        CHECK(!current(old.value));
        old.destroy();
        policy.apply(next.value, true);
        CHECK(!current(next.value));
    }
    CHECK(current(next.value) == next_context.value);
}
int main() {
    gvn_test::ClipboardLock lock;
    if (!lock)
        return 2;
    const auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !gvn_test::noninteractive(station) || !SetProcessWindowStation(station))
        return 3;
    const auto desktop = CreateDesktopW(L"GVN-ImeAudit", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop))
        return 4;
    return run_tests();
}
