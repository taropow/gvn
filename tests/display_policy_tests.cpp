#include "gvn/settings.h"
#include "folder_test_tree.h"
#include <fstream>
using namespace gvn;

TEST(fresh_display_policy_keeps_small_images_at_actual_size_and_fullscreen_fits) {
    Settings value;
    CHECK(value.display_mode == 2 && value.full_display_mode == 1);
    CHECK(value.initial_fit == 0 && value.full_initial_fit == 0 && !value.keep_zoom);
    FolderTestTree tree;
    const auto missing = tree.root / L"new profile.ini";
    value.load(missing);
    CHECK(!std::filesystem::exists(missing));
    CHECK(value.display_mode == 2 && value.full_display_mode == 1);
}
TEST(missing_policy_keys_use_reference_defaults_without_rewriting_existing_other_values) {
    FolderTestTree tree;
    const auto path = tree.root / L"partial.ini";
    CHECK(WritePrivateProfileStringW(L"Viewer", L"zoom_step", L"37", path.c_str()));
    Settings value;
    value.load(path);
    CHECK(value.display_mode == 2 && value.full_display_mode == 1 && value.zoom_step == 37);
    CHECK(GetPrivateProfileIntW(L"Viewer", L"display_mode", 99, path.c_str()) == 99);
}
TEST(all_thirty_six_explicit_normal_and_fullscreen_policy_pairs_survive_save_and_reload) {
    FolderTestTree tree;
    for (int normal = 0; normal < 6; ++normal)
        for (int full = 0; full < 6; ++full) {
            const auto path = tree.root / (std::to_wstring(normal) + L"-" + std::to_wstring(full) + L".ini");
            CHECK(WritePrivateProfileStringW(L"Viewer", L"display_mode", std::to_wstring(normal).c_str(), path.c_str()));
            CHECK(WritePrivateProfileStringW(L"Viewer", L"full_display_mode", std::to_wstring(full).c_str(), path.c_str()));
            Settings before;
            before.load(path);
            CHECK(before.display_mode == normal && before.full_display_mode == full);
            before.save(path);
            Settings after;
            after.load(path);
            CHECK(after.display_mode == normal && after.full_display_mode == full);
        }
}
TEST(corrupt_policy_text_uses_independent_defaults_and_numeric_values_are_bounded) {
    FolderTestTree tree;
    const auto path = tree.root / L"invalid.ini";
    CHECK(WritePrivateProfileStringW(L"Viewer", L"display_mode", L"invalid", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer", L"full_display_mode", L"invalid", path.c_str()));
    Settings invalid;
    invalid.load(path);
    CHECK(invalid.display_mode == 2 && invalid.full_display_mode == 1);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"display_mode", L"-12", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer", L"full_display_mode", L"999", path.c_str()));
    Settings bounded;
    bounded.load(path);
    CHECK(bounded.display_mode == 0 && bounded.full_display_mode == 5);
}
TEST(reading_legacy_explicit_fit_policy_keeps_its_selection_and_original_file_bytes) {
    FolderTestTree tree;
    const auto path = tree.root / L"legacy.ini";
    const std::string content = "[Viewer]\ndisplay_mode=1\nfull_display_mode=2\ninitial_fit=2\nfull_initial_fit=1\n";
    { std::ofstream stream(path, std::ios::binary); stream << content; CHECK(stream); }
    Settings value;
    value.load(path);
    CHECK(value.display_mode == 1 && value.full_display_mode == 2);
    CHECK(value.initial_fit == 2 && value.full_initial_fit == 1);
    std::ifstream stream(path, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(stream), {}) == content);
}
int main() { return run_tests(); }
