#pragma once
#include "gvn/preferences.h"
#include <fstream>

namespace gvn_test {
inline void zoom_preferences_contract(HWND owner) {
    using namespace gvn;
    using namespace preference_id;
    const auto check = [](bool ok, const char *reason) {
        if (!ok) throw std::runtime_error(reason);
    };
    const auto directory = std::filesystem::temp_directory_path() /
                           (L"GVN-zoom-ui-" + std::to_wstring(GetCurrentProcessId()));
    check(std::filesystem::create_directory(directory), "Zoom UI temporary identity collision");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{directory};
    const auto bytes = [](const auto &path) {
        std::ifstream stream(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(stream), {});
    };
    const auto same = [&](const Settings &expected, const Settings &actual) {
        expected.save(directory / L"expected.ini"); actual.save(directory / L"actual.ini");
        check(bytes(directory / L"expected.ini") == bytes(directory / L"actual.ini"),
              "Zoom UI changed another setting or lost an edited value");
    };
    Settings seed, applied;
    seed.zoom_mode = 0; seed.zoom_step = 65; seed.zoom_below = 7; seed.zoom_above = 4999;
    seed.zoom_reduce = seed.zoom_enlarge = false; seed.drag_simple = seed.keep_zoom = true;
    seed.zoom_simple = 750; seed.grid = 17; seed.background_first = 0x123456;
    seed.full_down = 8; seed.full_up = 10; seed.external[7] = L"Z:\\GVN-zoom-editor.exe";
    size_t commits{}, errors{};
    const auto apply = [&](const Settings &value) {
        value.save(directory / L"settings.ini"); applied = value; ++commits;
    };
    const auto error = [&](HWND, const std::wstring &) { ++errors; };
    const auto notify = [](HWND control, int code) {
        SendMessageW(GetParent(control), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(control), code),
                     reinterpret_cast<LPARAM>(control));
    };
    const auto combo = [&](PreferencesDialog &dialog, int value) {
        const auto control = dialog.control(ZoomMode);
        check(control && SendMessageW(control, CB_SETCURSEL, value, 0) == value, "Zoom mode missing");
        notify(control, CBN_SELCHANGE);
    };
    const auto text = [&](PreferencesDialog &dialog, int id, const wchar_t *value) {
        check(dialog.control(id) && SetWindowTextW(dialog.control(id), value), "Zoom input missing");
    };
    const auto click = [&](PreferencesDialog &dialog, int id) {
        check(dialog.control(id) != nullptr, "Zoom button missing");
        SendMessageW(dialog.control(id), BM_CLICK, 0, 0);
    };
    const auto enabled = [&](PreferencesDialog &dialog, int mode) {
        check((IsWindowEnabled(dialog.control(ZoomStep)) != FALSE) == (mode == 0) &&
                  (IsWindowEnabled(dialog.control(ZoomBelow)) != FALSE) == (mode == 1) &&
                  (IsWindowEnabled(dialog.control(ZoomAbove)) != FALSE) == (mode == 1),
              "Zoom mode dependencies differ from the selected mode");
    };
    PreferencesDialog dialog(seed, apply, error, PreferencePage::Zoom);
    check(dialog.create(owner) != nullptr, "Cannot create zoom settings");
    enabled(dialog, 0);
    combo(dialog, 1); enabled(dialog, 1);
    text(dialog, ZoomBelow, L"100"); text(dialog, ZoomAbove, L"5001");
    click(dialog, ApplyButton);
    check(commits == 0 && errors == 1 && IsWindow(dialog.window()), "Invalid zoom step was accepted");
    text(dialog, ZoomAbove, L"5000"); click(dialog, ApplyButton);
    auto expected = seed; expected.zoom_mode = 1; expected.zoom_below = 100; expected.zoom_above = 5000;
    check(commits == 1 && errors == 1, "5000-point zoom step was rejected"); same(expected, applied);
    Settings loaded; loaded.load(directory / L"settings.ini"); same(expected, loaded);
    // Disabled fields retain their applied values when the standard mode is selected.
    combo(dialog, 2); enabled(dialog, 2); click(dialog, ApplyButton); expected.zoom_mode = 2;
    check(commits == 2, "Standard zoom mode apply failed"); same(expected, applied);
    // A page reset keeps pending edits on another page and remains cancellable.
    dialog.select_page(PreferencePage::Background); text(dialog, Cell, L"13");
    dialog.select_page(PreferencePage::Zoom); click(dialog, Reset); enabled(dialog, 2);
    check(SendMessageW(dialog.control(DragSimple), BM_GETCHECK, 0, 0) == BST_UNCHECKED &&
              SendMessageW(dialog.control(preference_id::KeepZoom), BM_GETCHECK, 0, 0) == BST_UNCHECKED &&
              !dialog.control(ZoomReduce) && !dialog.control(ZoomEnlarge) && !dialog.control(ZoomSimple) &&
              SendMessageW(dialog.control(ZoomMode), CB_GETCURSEL, 0, 0) == 2,
          "Zoom reset defaults differ from the measured reference");
    click(dialog, IDCANCEL); check(commits == 2, "Cancel committed a zoom reset"); same(expected, applied);
    PreferencesDialog reset(applied, apply, error, PreferencePage::Zoom);
    check(reset.create(owner) != nullptr, "Cannot reopen zoom settings");
    reset.select_page(PreferencePage::Background); text(reset, Cell, L"13");
    reset.select_page(PreferencePage::Zoom); click(reset, Reset); click(reset, IDOK);
    const Settings defaults;
    expected.keep_zoom = defaults.keep_zoom; expected.zoom_step = defaults.zoom_step;
    expected.zoom_mode = defaults.zoom_mode; expected.zoom_below = defaults.zoom_below;
    expected.zoom_above = defaults.zoom_above;
    expected.drag_simple = defaults.drag_simple; expected.checker_size = 13;
    check(commits == 3 && errors == 1 && !IsWindow(reset.window()), "Zoom reset apply failed");
    same(expected, applied); loaded.load(directory / L"settings.ini"); same(expected, loaded);
}
} // namespace gvn_test
