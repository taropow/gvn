#pragma once
#include "gvn/preferences.h"
#include <array>
#include <fstream>

namespace gvn_test {
inline void background_preferences_contract(HWND owner) {
    using namespace gvn;
    using namespace preference_id;
    const auto check = [](bool ok, const char *reason) {
        if (!ok) throw std::runtime_error(reason);
    };
    const auto directory = std::filesystem::temp_directory_path() /
                           (L"GVN-background-ui-" + std::to_wstring(GetCurrentProcessId()));
    check(std::filesystem::create_directory(directory), "Background UI temporary identity collision");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{directory};
    const auto bytes = [](const auto &path) {
        std::ifstream stream(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(stream), {});
    };
    const auto same = [&](const Settings &a, const Settings &b) {
        a.save(directory / L"expected.ini"); b.save(directory / L"actual.ini");
        check(bytes(directory / L"expected.ini") == bytes(directory / L"actual.ini"),
              "Background UI changed another setting or lost an edited field");
    };
    Settings seed, applied;
    seed.grid = 17; seed.checker = false; seed.background = 0xabcdef;
    seed.checker_light = 0x12ef34; seed.checker_dark = 0xef1234; seed.checker_size = 13;
    seed.zoom_step = 65; seed.full_down = 8; seed.external[7] = L"Z:\\GVN-background-editor.exe";
    size_t commits{}, errors{}; bool fail_save{};
    auto apply = [&](const Settings &value) {
        if (fail_save) throw std::runtime_error("Owned save failure");
        value.save(directory / L"settings.ini"); applied = value; ++commits;
    };
    auto error = [&](HWND, const std::wstring &) { ++errors; };
    const auto notify = [](HWND control, int code) {
        SendMessageW(GetParent(control), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(control), code),
                     reinterpret_cast<LPARAM>(control));
    };
    const auto set_check = [&](PreferencesDialog &dialog, int id, bool value) {
        const auto control = dialog.control(id); check(control != nullptr, "Background checkbox missing");
        SendMessageW(control, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
        notify(control, BN_CLICKED);
    };
    const auto set_combo = [&](PreferencesDialog &dialog, int id, int value) {
        const auto control = dialog.control(id); check(control != nullptr, "Background size choice missing");
        check(SendMessageW(control, CB_SETCURSEL, value, 0) == value, "Background size selection failed");
        notify(control, CBN_SELCHANGE);
    };
    const auto set_text = [&](PreferencesDialog &dialog, int id, const wchar_t *value) {
        check(dialog.control(id) != nullptr && SetWindowTextW(dialog.control(id), value),
              "Background color control missing");
    };
    const auto palette = [&](PreferencesDialog &dialog, bool tiled, int preset, bool custom) {
        set_check(dialog, BackgroundTiled, tiled);
        set_combo(dialog, BackgroundPattern, preset);
        set_text(dialog, BackgroundFirst, custom ? L"172D43" : L"202020");
        set_text(dialog, BackgroundSecond, custom ? L"596571" : L"303030");
    };
    const auto preview = [&](PreferencesDialog &dialog, bool tiled, int preset, uint32_t first, uint32_t second) {
        const auto control = dialog.control(Preview); check(control != nullptr, "Background preview missing");
        RECT area{}; check(GetClientRect(control, &area), "Background preview has no client");
        const int width = area.right, height = area.bottom;
        check(width > 0 && height >= 50, "Background preview is too small");
        HDC dc = CreateCompatibleDC(nullptr); check(dc != nullptr, "Background preview DC failed");
        BITMAPINFO info{}; info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        void *pixels{}; const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        check(bitmap && pixels, "Background preview bitmap failed");
        const auto old = SelectObject(dc, bitmap);
        DRAWITEMSTRUCT draw{}; draw.CtlType = ODT_STATIC; draw.CtlID = Preview;
        draw.itemAction = ODA_DRAWENTIRE; draw.hwndItem = control; draw.hDC = dc; draw.rcItem = area;
        SendMessageW(GetParent(control), WM_DRAWITEM, Preview, reinterpret_cast<LPARAM>(&draw)); GdiFlush();
        bool matches = true;
        const int cell = std::array{8,16,32,64,128}[preset];
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const auto expected = tiled && ((x / cell + y / cell) & 1) ? second : first;
            matches &= (static_cast<const uint32_t *>(pixels)[y * width + x] & 0xffffff) == expected;
        }
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
        check(matches, "Background native preview differs from the actual reference cell/color model");
    };
    PreferencesDialog dialog(seed, apply, error);
    check(dialog.create(owner) != nullptr, "Cannot create Background contract dialog");
    set_text(dialog, preference_id::Grid, L"23"); // Keep a pending edit while another lazy tab is created.
    dialog.select_page(PreferencePage::Background);
    for (const int id : std::array<int,10>{BackgroundTiled, BackgroundPattern, BackgroundFirst, BackgroundSecond,
                         preference_id::Checker, preference_id::Background, Light, Dark, Cell, Preview})
        check(dialog.control(id) != nullptr, "Background page did not create every control");
    check(SendMessageW(dialog.control(BackgroundPattern), CB_GETCOUNT, 0, 0) == 5,
          "Background size selector must contain the five reference presets");
    RECT last{}, preview_box{};
    GetWindowRect(dialog.control(Cell), &last); GetWindowRect(dialog.control(Preview), &preview_box);
    check(preview_box.top >= last.bottom + 4, "Background preview overlaps the last setting row");
    auto expected = seed; expected.grid = 23;
    const auto gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (bool custom : {false,true}) for (int preset = 0; preset < 5; ++preset) for (bool tiled : {true,false}) {
        palette(dialog, tiled, preset, custom);
        check(bool(IsWindowEnabled(dialog.control(BackgroundPattern))) == tiled &&
              bool(IsWindowEnabled(dialog.control(BackgroundSecond))) == tiled &&
              bool(IsWindowEnabled(dialog.control(BackgroundSecond + 5000))) == tiled &&
              IsWindowEnabled(dialog.control(BackgroundFirst)), "Background tile dependencies differ");
        expected.background_tiled = tiled; expected.background_pattern = preset;
        expected.background_first = custom ? 0x172d43 : 0x202020;
        expected.background_second = custom ? 0x596571 : 0x303030;
        preview(dialog, tiled, preset, expected.background_first, expected.background_second);
        const auto previous = commits;
        SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
        check(commits == previous + 1 && errors == 0, "Background apply did not commit");
        same(expected, applied);
        Settings loaded; loaded.load(directory / L"settings.ini"); same(expected, loaded);
    }
    check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi_before + 2,
          "Background preview retained GDI objects across paints");
    // Disabled invalid text cannot block another option; last valid color is retained.
    set_text(dialog, BackgroundSecond, L"invalid");
    SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    same(expected, applied);
    set_check(dialog, BackgroundTiled, true);
    const auto before_failure = bytes(directory / L"settings.ini"); const auto saved_commits = commits;
    fail_save = true; SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    check(commits == saved_commits && errors == 1 && bytes(directory / L"settings.ini") == before_failure &&
          IsWindowEnabled(dialog.control(ApplyButton)), "Background save failure changed the file or lost the edit");
    fail_save = false; SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    expected.background_tiled = true; same(expected, applied);
    palette(dialog, false, 0, false);
    SendMessageW(dialog.control(IDCANCEL), BM_CLICK, 0, 0);
    check(!IsWindow(dialog.window()), "Background Cancel did not close"); same(expected, applied);
    PreferencesDialog reset(expected, apply, error, PreferencePage::Background);
    check(reset.create(owner) != nullptr, "Cannot reopen Background dialog");
    const auto after_apply = commits;
    SendMessageW(reset.control(Reset), BM_CLICK, 0, 0);
    preview(reset, true, 2, 0x202020, 0x303030);
    check(commits == after_apply && IsWindowEnabled(reset.control(BackgroundPattern)),
          "Background Reset committed before Apply or left options disabled");
    SendMessageW(reset.control(IDCANCEL), BM_CLICK, 0, 0); same(expected, applied);
    PreferencesDialog ok(expected, apply, error, PreferencePage::Background);
    check(ok.create(owner) != nullptr, "Cannot create Background OK dialog");
    SendMessageW(ok.control(Reset), BM_CLICK, 0, 0); SendMessageW(ok.control(IDOK), BM_CLICK, 0, 0);
    Settings defaults;
    expected.background_tiled = defaults.background_tiled; expected.background_pattern = defaults.background_pattern;
    expected.background_first = defaults.background_first; expected.background_second = defaults.background_second;
    expected.checker = defaults.checker; expected.background = defaults.background;
    expected.checker_light = defaults.checker_light; expected.checker_dark = defaults.checker_dark;
    expected.checker_size = defaults.checker_size;
    check(!IsWindow(ok.window()) && commits == after_apply + 1, "Background Reset/OK did not commit once");
    same(expected, applied);
}
} // namespace gvn_test
