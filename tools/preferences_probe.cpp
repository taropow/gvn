// Measure GVN's real settings controls on a noninteractive desktop.
#include "gvn/preferences.h"
#include "gvn/ui.h"
#include "gvn/display_quality.h"
#include "../tests/test_desktop.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include "../tests/background_preferences_contract.h"
#include "../tests/zoom_preferences_contract.h"
#include "../tests/mask_preferences_contract.h"

namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void preservation_contract(HWND owner, const gvn::Settings &source) {
    using namespace gvn;
    using namespace preference_id;
    Settings expected = source, applied;
    expected.full_down = 8;
    expected.full_up = 10;
    expected.toolbar_large = expected.overlay_animation = true;
    expected.overlay_family = L"MS UI Gothic";
    expected.copy_destinations[4] = L"Z:\\GVN-probe-copy";
    expected.move_destinations[8] = L"Z:\\GVN-probe-move";
    expected.copy_basic_path = L"Z:\\GVN-probe-basic-copy";
    expected.move_basic_path = L"Z:\\GVN-probe-basic-move";
    expected.copy_registered = true;
    expected.set_end_action(3);
    expected.folder_history = expected.previous_folder_first = expected.adjacent_previous_first =
        expected.parent_first = expected.boundary_sound = true;
    expected.comic_toolbar_reverse = false;
    expected.comic_jump_reverse = expected.jump_preview = false;
    expected.jump_quality = 2;
    expected.comic_mode = 2;
    expected.comic_fill = 0x123456;
    expected.comic_down = 10;
    expected.comic_up = 9;
    expected.external[7] = L"Z:\\GVN-probe-editor.exe";
    size_t commits{};
    PreferencesDialog dialog(expected, [&](const Settings &value) {
        applied = value;
        ++commits;
    });
    require(dialog.create(owner) != nullptr, "Cannot create contract dialog");
    require(!dialog.control(KeyList) && !dialog.control(DisplayDown) && !dialog.control(FullDown),
            "Unvisited pages were built eagerly");
    require(!dialog.control(PageBase + static_cast<int>(PreferencePage::Display)),
            "Unvisited page containers were created eagerly");
    expected.grid = source.grid == 11 ? 7 : 11;
    SetWindowTextW(dialog.control(preference_id::Grid), std::to_wstring(expected.grid).c_str());
    SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    require(commits == 1, "General-page apply failed");
    GUID id{};
    require(SUCCEEDED(CoCreateGuid(&id)), "Cannot create temporary identity");
    wchar_t token[40]{};
    StringFromGUID2(id, token, 40);
    const auto directory = std::filesystem::temp_directory_path() / (std::wstring(L"GVN-prefs-") + token);
    std::filesystem::create_directory(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{directory};
    const auto equal_saved = [&] {
        expected.save(directory / L"expected.ini");
        applied.save(directory / L"applied.ini");
        const auto bytes = [](const auto &path) {
            std::ifstream input(path, std::ios::binary);
            return std::vector<char>(std::istreambuf_iterator<char>(input), {});
        };
        require(bytes(directory / L"expected.ini") == bytes(directory / L"applied.ini"),
                "Applying a page changed other settings");
    };
    equal_saved();
    dialog.select_page(PreferencePage::Display);
    const auto down = dialog.control(DisplayDown);
    require(down != nullptr && !dialog.control(FullDown), "Independent lazy pages failed");
    SendMessageW(down, CB_SETCURSEL, 2, 0);
    SendMessageW(GetParent(down), WM_COMMAND, MAKEWPARAM(DisplayDown, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(down));
    dialog.select_page(PreferencePage::General);
    dialog.select_page(PreferencePage::Display);
    require(dialog.control(DisplayDown) == down && SendMessageW(down, CB_GETCURSEL, 0, 0) == 2,
            "Switching pages lost an edit or rebuilt controls");
    expected.display_down = 10;
    expected.comic_up = 10; // Visiting the quality controls canonicalizes the former high-quality choice.
    SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    require(commits == 2, "Display-page apply failed");
    equal_saved();
    SendMessageW(down, CB_SETCURSEL, 3, 0);
    SendMessageW(dialog.control(IDCANCEL), BM_CLICK, 0, 0);
    require(commits == 2 && !IsWindow(dialog.window()), "Cancel applied pending edits");
}
void general_reset_contract(HWND owner) {
    using namespace gvn;
    using namespace preference_id;
    struct Flag {
        int id;
        bool Settings::*member;
    };
    const Flag flags[]{{AllowMultiple, &Settings::allow_multiple},
                       {ActivateReused, &Settings::activate_reused},
                       {ActivateDropped, &Settings::activate_dropped},
                       {AllowPhysicalOverflow, &Settings::allow_physical_overflow},
                       {preference_id::Undo, &Settings::undo_enabled},
                       {CutTransparent, &Settings::cut_transparent},
                       {preference_id::History, &Settings::remember_history},
                       {PasteNew, &Settings::paste_new},
                       {ReopenLast, &Settings::reopen_last},
                       {DisableIme, &Settings::disable_ime}};
    Settings defaults, seed;
    for (const auto &flag : flags)
        seed.*flag.member = !(defaults.*flag.member);
    seed.cut_color = 0x123456;
    seed.grid = 23;
    seed.checker_size = 19;
    seed.zoom_step = 65;
    seed.full_down = 8;
    seed.full_up = 10;
    seed.toolbar_large = seed.overlay_animation = true;
    seed.copy_destinations[4] = L"Z:\\GVN-reset-copy";
    seed.external[7] = L"Z:\\GVN-reset-editor.exe";
    seed.last_file = L"Z:\\GVN-reset-日本語.png";
    seed.last_archive_entry = L"folder/日本語\nframe.png";
    seed.history = {L"Z:\\GVN-reset-history"};
    GUID id{};
    require(SUCCEEDED(CoCreateGuid(&id)), "Cannot create General-reset identity");
    wchar_t token[40]{};
    StringFromGUID2(id, token, 40);
    const auto directory =
        std::filesystem::temp_directory_path() / (std::wstring(L"GVN-general-reset-") + token);
    std::filesystem::create_directory(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{directory};
    const auto equal = [&](const Settings &expected, const Settings &actual) {
        expected.save(directory / L"expected.ini");
        actual.save(directory / L"actual.ini");
        const auto bytes = [](const auto &path) {
            std::ifstream file(path, std::ios::binary);
            return std::vector<char>(std::istreambuf_iterator<char>(file), {});
        };
        require(bytes(directory / L"expected.ini") == bytes(directory / L"actual.ini"),
                "General reset changed another field, internal location, or a pending value");
        require(actual.last_file == expected.last_file &&
                    actual.last_archive_entry == expected.last_archive_entry &&
                    actual.history == expected.history,
                "General reset lost internal metadata");
    };
    const auto reset_expected = [&](Settings value) {
        for (const auto &flag : flags)
            value.*flag.member = defaults.*flag.member;
        value.cut_color = defaults.cut_color;
        value.grid = defaults.grid;
        return value;
    };
    const auto check_controls = [&](PreferencesDialog &dialog, const Settings &expected) {
        for (const auto &flag : flags) {
            require(dialog.control(flag.id) != nullptr, "General reset is missing a checkbox");
            require((SendMessageW(dialog.control(flag.id), BM_GETCHECK, 0, 0) == BST_CHECKED) ==
                        expected.*flag.member,
                    "General reset checkbox differs");
        }
        wchar_t text[32]{};
        GetWindowTextW(dialog.control(preference_id::Grid), text, 32);
        require(std::wstring(text) == std::to_wstring(expected.grid), "General reset grid differs");
        GetWindowTextW(dialog.control(CutColor), text, 32);
        wchar_t hex[16]{};
        swprintf_s(hex, L"%06X", static_cast<unsigned>(expected.cut_color));
        require(std::wstring(text) == hex, "General reset cut color differs");
        require((IsWindowEnabled(dialog.control(ActivateReused)) != FALSE) == !expected.allow_multiple,
                "General reset activation dependency differs");
        require((IsWindowEnabled(dialog.control(CutColor)) != FALSE) == !expected.cut_transparent,
                "General reset cut-color dependency differs");
    };
    size_t commits{}, errors{};
    Settings applied = seed;
    const auto on_apply = [&](const Settings &value) {
        applied = value;
        ++commits;
    };
    const auto on_error = [&](HWND, const std::wstring &) { ++errors; };
    {
        PreferencesDialog dialog(seed, on_apply, on_error);
        require(dialog.create(owner) != nullptr, "Cannot create General-reset cancel dialog");
        check_controls(dialog, seed);
        SendMessageW(dialog.control(Reset), BM_CLICK, 0, 0);
        check_controls(dialog, reset_expected(seed));
        require(!dialog.control(FullDown) && !dialog.control(ExternalList), "Reset built unrelated pages");
        SendMessageW(dialog.control(IDCANCEL), BM_CLICK, 0, 0);
        require(commits == 0 && errors == 0, "Reset then cancel committed settings");
        equal(seed, applied);
    }
    {
        PreferencesDialog dialog(seed, on_apply, on_error);
        require(dialog.create(owner) != nullptr, "Cannot create General-reset apply dialog");
        dialog.select_page(PreferencePage::Background);
        const auto cell = dialog.control(Cell);
        SetWindowTextW(cell, L"13");
        dialog.select_page(PreferencePage::General);
        SetWindowTextW(dialog.control(preference_id::Grid), L"0");
        SetWindowTextW(dialog.control(CutColor), L"ZZZZZZ");
        SendMessageW(dialog.control(Reset), BM_CLICK, 0, 0);
        check_controls(dialog, reset_expected(seed));
        SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
        auto expected = reset_expected(seed);
        expected.checker_size = 13;
        require(commits == 1 && errors == 0 && !IsWindowEnabled(dialog.control(ApplyButton)),
                "General reset did not replace invalid current-page input");
        equal(expected, applied);
        require(!dialog.control(FullDown) && !dialog.control(ExternalList), "Apply built unvisited pages");
        dialog.select_page(PreferencePage::Background);
        require(dialog.control(Cell) == cell, "Reset recreated an edited other-page control");
        SetWindowTextW(cell, L"0");
        dialog.select_page(PreferencePage::General);
        SendMessageW(dialog.control(Reset), BM_CLICK, 0, 0);
        SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
        require(commits == 1 && errors == 1, "General reset hid an invalid other-page value");
        equal(expected, applied);
        SetWindowTextW(cell, L"17");
        dialog.select_page(PreferencePage::General);
        SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
        expected.checker_size = 17;
        require(commits == 2 && errors == 1, "Corrected other-page value could not apply");
        equal(expected, applied);
        SetWindowTextW(dialog.control(preference_id::Grid), L"33");
        SendMessageW(dialog.control(IDCANCEL), BM_CLICK, 0, 0);
        require(commits == 2, "Cancel after apply committed a later edit");
        equal(expected, applied);
    }
    {
        bool reject = true;
        Settings accepted = seed;
        PreferencesDialog dialog(
            seed,
            [&](const Settings &value) {
                if (reject)
                    throw std::runtime_error("Synthetic settings persistence failure");
                accepted = value;
            },
            on_error);
        require(dialog.create(owner) != nullptr, "Cannot create General-reset failed-apply dialog");
        SendMessageW(dialog.control(Reset), BM_CLICK, 0, 0);
        SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
        require(errors == 2 && IsWindow(dialog.window()), "Failed reset apply was not kept open");
        equal(seed, accepted);
        reject = false;
        SendMessageW(dialog.control(IDOK), BM_CLICK, 0, 0);
        require(!IsWindow(dialog.window()), "Retry after failed reset apply did not close");
        equal(reset_expected(seed), accepted);
    }
}
} // namespace

void gif_style_contract(HWND owner) {
    using namespace gvn;
    using namespace preference_id;
    Settings seed, applied;
    seed.gif_speed = 4;
    seed.full_down = 10;
    size_t commits = 0, errors = 0;
    PreferencesDialog dialog(
        seed,
        [&](const Settings &value) {
            applied = value;
            ++commits;
        },
        [&](HWND, const std::wstring &) { ++errors; });
    require(dialog.create(owner) != nullptr, "Cannot create GIF settings contract");
    require(!dialog.control(GifSpeed), "GIF controls were created before visiting Display");
    SetWindowTextW(dialog.control(preference_id::Grid), L"13");
    SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    require(commits == 1 && applied.gif_speed == 4, "General apply lost unvisited GIF style");
    dialog.select_page(PreferencePage::Display);
    auto control = dialog.control(GifSpeed);
    require(control && SendMessageW(control, CB_GETCOUNT, 0, 0) == 5 &&
                SendMessageW(control, CB_GETCURSEL, 0, 0) == 4,
            "GIF choices/default selection mismatch");
    for (int style = 0; style < 5; ++style) {
        SendMessageW(control, CB_SETCURSEL, style, 0);
        SendMessageW(GetParent(control), WM_COMMAND, MAKEWPARAM(GifSpeed, CBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(control));
        SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
        require(commits == static_cast<size_t>(style + 2) && applied.gif_speed == style &&
                    applied.grid == 13 && applied.full_down == 10,
                "GIF apply failed or changed another page");
    }
    dialog.select_page(PreferencePage::Background);
    SetWindowTextW(dialog.control(Cell), L"17");
    dialog.select_page(PreferencePage::Display);
    SendMessageW(control, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    require(SendMessageW(control, CB_GETCURSEL, 0, 0) == CB_ERR,
            "Test did not create an invalid GIF selection");
    SendMessageW(GetParent(control), WM_COMMAND, MAKEWPARAM(GifSpeed, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(control));
    SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    require(commits == 6 && errors == 1, "Invalid GIF choice did not reject the transaction");
    SendMessageW(dialog.control(Reset), BM_CLICK, 0, 0);
    require(SendMessageW(control, CB_GETCURSEL, 0, 0) == 0, "Display reset did not reset GIF style");
    SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    require(commits == 7 && errors == 1 && applied.gif_speed == 0 && applied.checker_size == 17 &&
                applied.full_down == 10,
            "GIF reset discarded another page's pending value");
    SendMessageW(control, CB_SETCURSEL, 3, 0);
    SendMessageW(dialog.control(IDCANCEL), BM_CLICK, 0, 0);
    require(commits == 7 && applied.gif_speed == 0, "GIF cancel applied a pending choice");
}

void animation_repeat_contract(HWND owner) {
    using namespace gvn;
    using namespace preference_id;
    Settings seed, applied;
    seed.animation_file_loops = false;
    seed.gif_speed = 4;
    size_t commits = 0;
    PreferencesDialog dialog(seed, [&](const Settings &value) {
        applied = value;
        ++commits;
    });
    require(dialog.create(owner) != nullptr, "Cannot create repeat-policy dialog");
    SetWindowTextW(dialog.control(preference_id::Grid), L"13");
    SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    require(commits == 1 && !applied.animation_file_loops, "General apply changed unvisited repeat policy");
    dialog.select_page(PreferencePage::Loading);
    auto choice = dialog.control(AnimationFileLoops);
    require(choice && SendMessageW(choice, BM_GETCHECK, 0, 0) == BST_UNCHECKED,
            "Repeat-policy checkbox did not show the supplied value");
    SendMessageW(choice, BM_CLICK, 0, 0);
    SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    require(commits == 2 && applied.animation_file_loops && applied.gif_speed == 4 && applied.grid == 13,
            "Repeat-policy apply lost another page");
    SendMessageW(choice, BM_CLICK, 0, 0);
    SendMessageW(dialog.control(Reset), BM_CLICK, 0, 0);
    require(SendMessageW(choice, BM_GETCHECK, 0, 0) == BST_CHECKED,
            "Loading reset did not restore file-count policy");
    SendMessageW(dialog.control(IDCANCEL), BM_CLICK, 0, 0);
    require(commits == 2 && applied.animation_file_loops, "Repeat-policy cancel committed pending edits");
    PreferencesDialog reset(
        seed,
        [&](const Settings &value) {
            applied = value;
            ++commits;
        },
        {}, PreferencePage::Loading);
    require(reset.create(owner) != nullptr, "Cannot create direct Loading dialog");
    SendMessageW(reset.control(Reset), BM_CLICK, 0, 0);
    SendMessageW(reset.control(ApplyButton), BM_CLICK, 0, 0);
    require(commits == 3 && applied.animation_file_loops && applied.gif_speed == 4,
            "Applied Loading reset did not retain other pages");
    SendMessageW(reset.control(IDCANCEL), BM_CLICK, 0, 0);
}

void display_reset_contract(HWND owner) {
    using namespace gvn;
    using namespace preference_id;
    struct Flag {
        int id;
        bool Settings::*member;
    };
    struct Number {
        int id;
        int Settings::*member;
        int sample;
        bool combo;
    };
    const Flag flags[]{{CenterImage, &Settings::center_image},
                       {preference_id::ComicRTL, &Settings::comic_rtl},
                       {ComicRotateBefore, &Settings::comic_rotate_before}};
    const Number numbers[]{
        {InitialFit, &Settings::initial_fit, 4, true},     {GifSpeed, &Settings::gif_speed, 4, true},
        {FitMax, &Settings::fit_max, 123, false},
        {DisplayDown, &Settings::display_down, 10, true},  {DisplayUp, &Settings::display_up, 9, true},
        {ComicHeightMode, &Settings::comic_mode, 2, true}, {ComicDown, &Settings::comic_down, 10, true},
        {ComicUp, &Settings::comic_up, 9, true},           {DisplayMode, &Settings::display_mode, 5, true}};
    Settings seed, defaults, applied;
    require(defaults.center_image && defaults.gif_speed == 0 && defaults.fit_max == 5000 &&
                defaults.fit_simple == 0 && defaults.display_down == 10 && defaults.display_up == 1 &&
                !defaults.comic_rotate_before && defaults.comic_mode == 0 && defaults.comic_down == 10 &&
                defaults.comic_up == 1,
            "Display defaults differ from the actual reference Reset controls");
    for (const auto &flag : flags)
        seed.*flag.member = !(defaults.*flag.member);
    for (const auto &number : numbers)
        seed.*number.member = number.sample;
    seed.comic_fill = 0x123456;
    seed.view_rotation = 6;
    seed.save_as_retarget = true;
    seed.comic_single_wide = true;
    seed.checker_size = 19;
    seed.zoom_step = 65;
    seed.full_down = 8;
    seed.external[7] = L"Z:\\GVN-display-reset-editor.exe";
    seed.last_file = L"Z:\\GVN-display-reset-日本語.png";
    seed.history = {L"Z:\\GVN-display-reset-history"};
    GUID id{};
    require(SUCCEEDED(CoCreateGuid(&id)), "Cannot create Display-reset identity");
    wchar_t token[40]{};
    StringFromGUID2(id, token, 40);
    const auto directory =
        std::filesystem::temp_directory_path() / (std::wstring(L"GVN-display-reset-") + token);
    std::filesystem::create_directory(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{directory};
    const auto equal = [&](const Settings &expected, const Settings &actual) {
        expected.save(directory / L"expected.ini");
        actual.save(directory / L"actual.ini");
        const auto bytes = [](const auto &path) {
            std::ifstream stream(path, std::ios::binary);
            return std::vector<char>(std::istreambuf_iterator<char>(stream), {});
        };
        require(bytes(directory / L"expected.ini") == bytes(directory / L"actual.ini"),
                "Display transaction changed another setting or lost a pending value");
    };
    const auto reset_expected = [&](Settings value) {
        for (const auto &flag : flags)
            value.*flag.member = defaults.*flag.member;
        for (const auto &number : numbers)
            value.*number.member = defaults.*number.member;
        value.comic_fill = defaults.comic_fill;
        return value;
    };
    const auto controls = [&](PreferencesDialog &dialog, const Settings &expected) {
        for (const auto &flag : flags) {
            const auto control = dialog.control(flag.id);
            require(control &&
                        (SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED) == expected.*flag.member,
                    "Display checkbox value differs or control is missing");
        }
        for (const auto &number : numbers) {
            const auto control = dialog.control(number.id);
            require(control != nullptr, "Display numeric control is missing");
            if (number.combo)
                require(SendMessageW(control, CB_GETCURSEL, 0, 0) ==
                    ((number.id==DisplayDown||number.id==DisplayUp||number.id==ComicDown||number.id==ComicUp)?display_quality_index(expected.*number.member):expected.*number.member),
                        "Display combo value differs");
            else {
                wchar_t text[32]{};
                GetWindowTextW(control, text, 32);
                require(std::wstring(text) == std::to_wstring(expected.*number.member),
                        "Display numeric text differs");
            }
        }
        wchar_t text[32]{}, expected_hex[16]{};
        GetWindowTextW(dialog.control(ComicFill), text, 32);
        swprintf_s(expected_hex, L"%06X", static_cast<unsigned>(expected.comic_fill));
        require(std::wstring(text) == expected_hex, "Display fill color differs");
    };
    size_t commits{}, errors{};
    applied = seed;
    const auto on_apply = [&](const Settings &value) {
        applied = value;
        ++commits;
    };
    const auto on_error = [&](HWND, const std::wstring &) { ++errors; };
    {
        PreferencesDialog dialog(seed, on_apply, on_error);
        require(dialog.create(owner) != nullptr, "Cannot create Display preservation dialog");
        require(!dialog.control(ComicRotateBefore), "Display checkbox was created eagerly");
        SetWindowTextW(dialog.control(preference_id::Grid), L"13");
        SetWindowTextW(dialog.control(preference_id::Grid), std::to_wstring(seed.grid).c_str());
        SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
        require(commits == 1, "Unvisited Display apply failed");
        equal(seed, applied);
        dialog.select_page(PreferencePage::Display);
        controls(dialog, seed);
        SendMessageW(dialog.control(Reset), BM_CLICK, 0, 0);
        controls(dialog, reset_expected(seed));
        SendMessageW(dialog.control(IDCANCEL), BM_CLICK, 0, 0);
        require(commits == 1 && errors == 0, "Display reset then cancel committed settings");
        equal(seed, applied);
    }
    {
        PreferencesDialog dialog(seed, on_apply, on_error);
        require(dialog.create(owner) != nullptr, "Cannot create Display reset dialog");
        dialog.select_page(PreferencePage::Background);
        const auto cell = dialog.control(Cell);
        SetWindowTextW(cell, L"13");
        dialog.select_page(PreferencePage::Display);
        SetWindowTextW(dialog.control(FitMax), L"0");
        SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
        require(commits == 1 && errors == 1, "Invalid Display value was accepted");
        equal(seed, applied);
        SendMessageW(dialog.control(Reset), BM_CLICK, 0, 0);
        controls(dialog, reset_expected(seed));
        SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
        auto expected = reset_expected(seed);
        expected.checker_size = 13;
        require(commits == 2 && errors == 1, "Display reset failed to replace invalid current-page input");
        equal(expected, applied);
        require(!dialog.control(FullDown) && !dialog.control(ExternalList), "Reset built unrelated pages");
        require(dialog.control(Cell) == cell, "Display reset recreated another page");
        SetWindowTextW(cell, L"0");
        SendMessageW(dialog.control(Reset), BM_CLICK, 0, 0);
        SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
        require(commits == 2 && errors == 2, "Display reset hid an invalid other-page value");
        equal(expected, applied);
        SetWindowTextW(cell, L"17");
        SendMessageW(dialog.control(ComicRotateBefore), BM_CLICK, 0, 0);
        SendMessageW(dialog.control(IDOK), BM_CLICK, 0, 0);
        expected.checker_size = 17;
        expected.comic_rotate_before = true;
        require(commits == 3 && errors == 2 && !IsWindow(dialog.window()), "Corrected Display OK failed");
        equal(expected, applied);
    }
    {
        bool reject = true;
        Settings accepted = seed;
        PreferencesDialog dialog(
            seed,
            [&](const Settings &value) {
                if (reject)
                    throw std::runtime_error("Synthetic Display persistence failure");
                accepted = value;
            },
            on_error, PreferencePage::Display);
        require(dialog.create(owner) != nullptr, "Cannot create failed Display apply dialog");
        SendMessageW(dialog.control(Reset), BM_CLICK, 0, 0);
        SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
        require(errors == 3 && IsWindow(dialog.window()), "Failed Display apply did not remain open");
        equal(seed, accepted);
        reject = false;
        SendMessageW(dialog.control(IDOK), BM_CLICK, 0, 0);
        require(!IsWindow(dialog.window()), "Display apply retry did not close");
        equal(reset_expected(seed), accepted);
    }
}

void save_copy_policy_contract(HWND owner) {
    using namespace gvn;
    using namespace preference_id;
    Settings seed, applied;
    seed.save_as_retarget = true;
    seed.comic_rotate_before = true;
    size_t commits{};
    PreferencesDialog dialog(seed, [&](const Settings &value) {
        applied = value;
        ++commits;
    });
    require(dialog.create(owner) != nullptr, "Cannot create save-copy policy dialog");
    require(!dialog.control(SaveAsRetarget), "Unvisited save-copy field was created eagerly");
    SetWindowTextW(dialog.control(preference_id::Grid), L"17");
    SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    require(commits == 1 && applied.save_as_retarget && applied.comic_rotate_before,
            "General apply changed an unvisited save-copy policy");
    dialog.select_page(PreferencePage::FileOperations);
    const auto control = dialog.control(SaveAsRetarget);
    require(control && SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED,
            "Save-copy checkbox did not restore the supplied policy");
    SendMessageW(control, BM_CLICK, 0, 0);
    SendMessageW(dialog.control(ApplyButton), BM_CLICK, 0, 0);
    require(commits == 2 && !applied.save_as_retarget && applied.grid == 17 && applied.comic_rotate_before,
            "Save-copy apply changed another page");
    SendMessageW(control, BM_CLICK, 0, 0);
    SendMessageW(dialog.control(IDCANCEL), BM_CLICK, 0, 0);
    require(commits == 2 && !applied.save_as_retarget, "Cancel committed the pending save-copy policy");
    PreferencesDialog reset(seed, [&](const Settings &value) {
        applied = value;
        ++commits;
    }, {}, PreferencePage::FileOperations);
    require(reset.create(owner) != nullptr, "Cannot create FileOperations reset dialog");
    SendMessageW(reset.control(Reset), BM_CLICK, 0, 0);
    require(SendMessageW(reset.control(SaveAsRetarget), BM_GETCHECK, 0, 0) == BST_UNCHECKED,
            "FileOperations reset did not restore the reference save-copy default");
    SendMessageW(reset.control(IDOK), BM_CLICK, 0, 0);
    require(commits == 3 && !applied.save_as_retarget && applied.comic_rotate_before,
            "Save-copy reset lost another page's value");
}

int wmain(int argc, wchar_t **argv) {
    auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !gvn_test::noninteractive(station) || !SetProcessWindowStation(station))
        return 2;
    auto desktop = CreateDesktopW(L"GVN-Preferences-Probe", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop)) {
        std::cerr << "desktop: " << GetLastError() << '\n';
        return 3;
    }
    if (FAILED(OleInitialize(nullptr)))
        return 6;
    if (!gvn::initialize_controls()) {
        std::cerr << "controls: " << GetLastError() << '\n';
        return 7;
    }
    const auto start = std::chrono::steady_clock::now();
    gvn::Settings settings;
    const bool timing_only=argc==2&&std::wstring(argv[1])==L"--timing-only";
    if (argc == 2&&!timing_only)
        settings.load(argv[1]); // Read only; never apply or save the supplied configuration.
    const auto elapsed = [](auto since) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
    };
    const auto load_ms = elapsed(start);
    HWND owner = CreateWindowExW(0, L"STATIC", L"GVN probe owner", WS_OVERLAPPEDWINDOW, 0, 0, 900, 700,
                                 nullptr, nullptr, nullptr, nullptr);
    if (!owner)
        return 4;
    try {
        std::cout << "{\"noninteractive\":true,\"config_load_ms\":" << load_ms << ",\"samples\":["
                  << std::flush;
        for (int i = 0; i < (timing_only?1:3); ++i) {
            const auto opened = std::chrono::steady_clock::now();
            gvn::PreferencesDialog dialog(settings, [](const auto &) {});
            if (!dialog.create(owner))
                return 5;
            ShowWindow(dialog.window(), SW_SHOWNOACTIVATE);
            RedrawWindow(dialog.window(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            const auto create_ms = elapsed(opened);
            require(!dialog.control(gvn::preference_id::KeyList) &&
                        !dialog.control(gvn::preference_id::DisplayDown),
                    "Startup built unrelated pages");
            std::cout << (i ? "," : "") << "{\"create_ms\":" << create_ms << ",\"phases_ms\":{";
            bool comma{};
            for (const auto &[name, ms] : dialog.initialization_timings()) {
                std::cout << (comma ? "," : "") << '"' << name << "\":" << ms;
                comma = true;
            }
            std::cout << "}" << std::flush;
            const auto tabs = std::chrono::steady_clock::now();
            for (int page = 0; page < static_cast<int>(gvn::PreferencePage::Count); ++page)
                dialog.select_page(static_cast<gvn::PreferencePage>(page));
            const auto tabs_ms = elapsed(tabs);
            const auto revisit = std::chrono::steady_clock::now();
            for (int page = 0; page < static_cast<int>(gvn::PreferencePage::Count); ++page)
                dialog.select_page(static_cast<gvn::PreferencePage>(page));
            const auto revisit_ms = elapsed(revisit);
            const auto closed = std::chrono::steady_clock::now();
            SendMessageW(dialog.window(), WM_CLOSE, 0, 0);
            std::cout << ",\"all_tabs_ms\":" << tabs_ms << ",\"revisit_all_tabs_ms\":" << revisit_ms
                      << ",\"close_ms\":" << elapsed(closed) << "}" << std::flush;
            require(create_ms < 1500, "Settings opening exceeded 1500 ms");
            require(revisit_ms < 3000, "Cached tab switching exceeded 3000 ms total");
        }
        if(timing_only){std::cout<<"],\"timings_only\":true}\n";DestroyWindow(owner);OleUninitialize();return 0;}
        preservation_contract(owner, settings);
        general_reset_contract(owner);
        gif_style_contract(owner);
        animation_repeat_contract(owner);
        display_reset_contract(owner);
        save_copy_policy_contract(owner);
        gvn_test::background_preferences_contract(owner);
        gvn_test::zoom_preferences_contract(owner);
        gvn_test::mask_preferences_contract(owner);
        std::cout << "],\"preservation_contract\":true,\"general_reset_contract\":true,"
                     "\"general_controls_checked\":12,\"gif_style_contract\":true,\"animation_repeat_"
                     "contract\":true,\"display_reset_contract\":true,\"display_controls_checked\":15,"
                     "\"save_copy_policy_contract\":true,\"background_contract\":true,"
                     "\"background_conditions_checked\":20,\"background_controls_checked\":9,"
                     "\"zoom_reference_contract\":true,\"zoom_controls_checked\":9,"
                     "\"mask_contract\":true,\"mask_controls_checked\":16}\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        DestroyWindow(owner);
        OleUninitialize();
        return 8;
    }
    DestroyWindow(owner);
    OleUninitialize();
    return 0;
}
