#pragma once
#include "gvn/preferences.h"
#include <fstream>

namespace gvn_test {
inline void mask_preferences_contract(HWND owner) {
    using namespace gvn;
    namespace id = preference_id;
    const auto check = [](bool ok, const char *reason) { if (!ok) throw std::runtime_error(reason); };
    const auto directory = std::filesystem::temp_directory_path() /
                           (L"GVN-mask-ui-" + std::to_wstring(GetCurrentProcessId()));
    check(std::filesystem::create_directory(directory), "Mask UI temporary identity collision");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{directory};
    const auto bytes = [](const auto &path) {
        std::ifstream file(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(file), {});
    };
    const auto same = [&](const Settings &expected, const Settings &actual) {
        expected.save(directory / L"expected.ini"); actual.save(directory / L"actual.ini");
        check(bytes(directory / L"expected.ini") == bytes(directory / L"actual.ini"), "Mask page changed another setting");
    };
    struct Flag { int control; bool Settings::*member; };
    const Flag flags[]{{id::FLNegative,&Settings::fl_negative},{id::GlassRemainder,&Settings::glass_remainder},
        {id::MaskNegativeRed,&Settings::mask_negative_red},{id::MaskNegativeGreen,&Settings::mask_negative_green},
        {id::MaskNegativeBlue,&Settings::mask_negative_blue},{id::MaskXorRed,&Settings::mask_xor_red},
        {id::MaskXorGreen,&Settings::mask_xor_green},{id::MaskXorBlue,&Settings::mask_xor_blue},
        {id::Q0Vertical,&Settings::mask_q0_vertical},{id::Q0Horizontal,&Settings::mask_q0_horizontal},
        {id::Q0Negative,&Settings::mask_q0_negative},{id::Q0Xor,&Settings::mask_q0_xor},
        {id::Q0FlipVertical,&Settings::mask_q0_flip_vertical},{id::Q0FlipHorizontal,&Settings::mask_q0_flip_horizontal}};
    Settings expected, applied;
    expected.zoom_mode = 1; expected.zoom_above = 4999; expected.overlay_family = L"MS Gothic";
    expected.background_first = 0x123456; expected.grid = 19;
    size_t commits{}, errors{};
    const auto apply = [&](const Settings &value) { applied = value; value.save(directory / L"settings.ini"); ++commits; };
    const auto error = [&](HWND, const std::wstring &) { ++errors; };
    const auto click = [&](PreferencesDialog &dialog, int control) {
        check(dialog.control(control) != nullptr, "Mask control is missing");
        SendMessageW(dialog.control(control), BM_CLICK, 0, 0);
    };
    PreferencesDialog dialog(expected, apply, error, PreferencePage::Mask);
    check(dialog.create(owner) != nullptr, "Cannot create mask page");
    check(SendMessageW(dialog.control(id::MaskSwapChoice), CB_GETCOUNT, 0, 0) == 5, "Mask swap choices differ");
    std::vector<RECT> boxes;
    const auto page = GetParent(dialog.control(id::GlassSize)); RECT bounds{}; GetClientRect(page, &bounds);
    MapWindowPoints(page, nullptr, reinterpret_cast<POINT *>(&bounds), 2);
    for (const auto &flag : flags) {
        const auto control = dialog.control(flag.control); check(control != nullptr, "Mask checkbox missing");
        RECT box{}; GetWindowRect(control, &box);
        check(box.left >= bounds.left && box.right <= bounds.right && box.top >= bounds.top && box.bottom <= bounds.bottom,
              "Mask checkbox is clipped in the default layout");
        for (const auto &other : boxes) { RECT intersection{}; check(!IntersectRect(&intersection, &box, &other), "Mask checkboxes overlap"); }
        boxes.push_back(box);
    }
    SetWindowTextW(dialog.control(id::GlassSize), L"1000"); click(dialog, id::ApplyButton);
    check(commits == 0 && errors == 1, "Invalid glass size was accepted");
    SetWindowTextW(dialog.control(id::GlassSize), L"999"); click(dialog, id::ApplyButton);
    expected.glass_size = 999; check(commits == 1 && errors == 1, "Glass999 rejected"); same(expected, applied);
    for (const auto &flag : flags) {
        click(dialog, flag.control); expected.*flag.member = !(expected.*flag.member);
        click(dialog, id::ApplyButton); same(expected, applied);
        Settings loaded; loaded.load(directory / L"settings.ini"); same(expected, loaded);
    }
    for (int choice = 0; choice < 5; ++choice) {
        const auto control = dialog.control(id::MaskSwapChoice);
        check(SendMessageW(control, CB_SETCURSEL, choice, 0) == choice, "Mask RGB selection failed");
        SendMessageW(GetParent(control), WM_COMMAND, MAKEWPARAM(id::MaskSwapChoice, CBN_SELCHANGE), reinterpret_cast<LPARAM>(control));
        click(dialog, id::ApplyButton); expected.mask_swap = choice; same(expected, applied);
    }
    click(dialog, id::Reset); click(dialog, IDCANCEL); same(expected, applied);
    PreferencesDialog reset(applied, apply, error, PreferencePage::Mask);
    check(reset.create(owner) != nullptr, "Cannot reopen mask page");
    reset.select_page(PreferencePage::Background); SetWindowTextW(reset.control(id::Cell), L"13");
    reset.select_page(PreferencePage::Mask); click(reset, id::Reset); click(reset, IDOK);
    const Settings defaults;
    expected.glass_size = 8; expected.mask_swap = 3; expected.checker_size = 13;
    for (const auto &flag : flags) expected.*flag.member = defaults.*flag.member;
    check(errors == 1 && !IsWindow(reset.window()), "Mask reset failed"); same(expected, applied);
    Settings loaded; loaded.load(directory / L"settings.ini"); same(expected, loaded);
}
} // namespace gvn_test
