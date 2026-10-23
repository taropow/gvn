#include "gvn/external.h"
#include "test.h"
#include <windows.h>
#include <shellapi.h>
using namespace gvn;
namespace {
ExternalContext context() {
    return {L"C:\\GVN 本体", L"D:\\Folder 日本語 space\\many.parts_image.png", L"D:\\Folder 日本語 space", true, {}};
}
ExternalApplication app() {
    ExternalApplication result;
    result.name = L"画像エディタ";
    result.executable = L"C:\\Program Files\\画像エディタ.exe";
    return result;
}
std::vector<std::wstring> parsed(const std::wstring &parameters) {
    int argc{};
    auto argv = CommandLineToArgvW((L"receiver.exe " + parameters).c_str(), &argc);
    CHECK(argv);
    std::vector<std::wstring> result;
    for (int i = 1; i < argc; ++i) result.emplace_back(argv[i]);
    LocalFree(argv);
    return result;
}
} // namespace
TEST(defaults_match_nine_observed_reference_slots) {
    const auto all = default_external_applications();
    for (size_t i = 0; i < all.size(); ++i) {
        const auto &entry = all[i];
        CHECK(entry.executable.empty() && entry.show == ExternalShow::Normal && !entry.quit_after_launch &&
              !entry.separator && !entry.include_archive_member && !entry.archive_file_for_folder);
        CHECK(entry.send == (i == 8 ? ExternalSend::Folder : ExternalSend::File));
        CHECK(entry.verb == (i == 7 ? L"open" : L""));
        if (i < 7) CHECK(entry.name.empty());
    }
    CHECK(all[7].name == L"現在の ファイル を開く" && all[8].name == L"現在の フォルダ を開く");
}
TEST(names_follow_observed_bmp_limit_without_splitting_a_surrogate_pair) {
    CHECK(external_application_name(std::wstring(60, L'A')) == std::wstring(50, L'A'));
    CHECK(external_application_name(std::wstring(60, L'画')) == std::wstring(50, L'画'));
    CHECK(external_application_name(L"").empty());
    CHECK(external_application_name(std::wstring(49, L'A') + L"\U0001f600x") == std::wstring(49, L'A'));
    CHECK(external_application_name(std::wstring(48, L'A') + L"\U0001f600x").size() == 50);
    THROWS(external_application_name(std::wstring(L"ab\0cd", 5)));
}
TEST(legacy_paths_keep_displayed_png_policy_and_empty_rows_get_reference_defaults) {
    for (size_t i = 0; i < 9; ++i) {
        CHECK(migrate_external_application(i, L"") == default_external_applications()[i]);
        auto migrated = migrate_external_application(i, L"D:\\Editors 日本語\\old editor.exe");
        CHECK(migrated.name == L"old editor" && migrated.executable == L"D:\\Editors 日本語\\old editor.exe");
        CHECK(migrated.send == ExternalSend::DisplayedPng && migrated.verb.empty());
    }
    THROWS(migrate_external_application(9, L"x"));
}
TEST(move_and_reset_keep_every_field_and_other_slots) {
    auto all = default_external_applications();
    auto value = app(); value.send = ExternalSend::Clipboard; value.show = ExternalShow::Maximized;
    value.verb = L"edit"; value.quit_after_launch = value.separator = value.include_archive_member = value.archive_file_for_folder = true;
    all[4] = value;
    const auto before = all;
    CHECK(move_external_application(all, 4, -1) == 3 && all[3] == value && all[4] == before[3]);
    CHECK(move_external_application(all, 3, 1) == 4 && all == before);
    CHECK(move_external_application(all, 0, -1) == 0 && all == before);
    CHECK(move_external_application(all, 8, 1) == 8 && all == before);
    reset_external_application(all, 4);
    for (size_t i = 0; i < 9; ++i) CHECK(all[i] == (i == 4 ? default_external_applications()[i] : before[i]));
    all[7] = all[8] = value; reset_external_application(all, 7); reset_external_application(all, 8);
    CHECK(all[7] == default_external_applications()[7] && all[8] == default_external_applications()[8]);
    THROWS(move_external_application(all, 9, 1)); THROWS(move_external_application(all, 0, 0));
    THROWS(reset_external_application(all, 9));
}
TEST(bare_file_folder_none_and_clipboard_match_receiver_arguments) {
    auto entry = app(); const auto source = context();
    for (auto send : {ExternalSend::None, ExternalSend::File, ExternalSend::Folder, ExternalSend::Clipboard}) {
        entry.send = send; const auto launch = plan_external_application(entry, source);
        CHECK(launch.file == entry.executable && launch.directory == source.viewer_directory && !launch.explicit_arguments);
        CHECK(launch.copy_image == (send == ExternalSend::Clipboard));
        const auto expected = send == ExternalSend::File ? std::vector{source.file_path} :
                              send == ExternalSend::Folder ? std::vector{source.folder_path} : std::vector<std::wstring>{};
        CHECK(parsed(launch.parameters) == expected);
    }
}
TEST(quoted_executable_without_arguments_keeps_automatic_file_send) {
    auto entry = app(); entry.executable = L"\"" + entry.executable + L"\"";
    auto launch = plan_external_application(entry, context());
    CHECK(!launch.explicit_arguments && parsed(launch.parameters) == std::vector{context().file_path});
    entry.executable += L"   "; launch = plan_external_application(entry, context());
    CHECK(!launch.explicit_arguments && parsed(launch.parameters) == std::vector{context().file_path});
}
TEST(explicit_arguments_suppress_automatic_paths_and_replace_each_macro_once) {
    auto entry = app(); const auto executable = entry.executable;
    entry.executable = L"\"" + executable + L"\" /literal";
    CHECK(parsed(plan_external_application(entry, context()).parameters) == std::vector<std::wstring>{L"/literal"});
    entry.executable = L"\"" + executable + L"\" /repeat \"($f)\" \"($f)\" \"($d)\" \"\"";
    entry.send = ExternalSend::Folder;
    auto source = context(); auto launch = plan_external_application(entry, source);
    CHECK(launch.explicit_arguments && parsed(launch.parameters) == std::vector<std::wstring>{L"/repeat", source.file_path, source.file_path, source.folder_path, L""});
    source.file_path = L"D:\\($d)\\file.png";
    CHECK(parsed(plan_external_application(entry, source).parameters)[1] == source.file_path);
}
TEST(unquoted_macro_expansion_stays_raw_for_user_controlled_argument_quoting) {
    auto entry = app(); entry.executable = L"\"" + entry.executable + L"\" ($f)";
    CHECK(plan_external_application(entry, context()).parameters == context().file_path);
    CHECK(parsed(plan_external_application(entry, context()).parameters).size() > 1);
}
TEST(relative_executable_is_based_on_viewer_not_process_current_directory) {
    auto entry = app(); entry.executable = L"Editors 日本語\\receiver.exe";
    CHECK(plan_external_application(entry, context()).file == L"C:\\GVN 本体\\Editors 日本語\\receiver.exe");
    entry.executable = L"\"Editors 日本語\\receiver.exe\" /relative \"($f)\"";
    auto launch = plan_external_application(entry, context());
    CHECK(launch.file == L"C:\\GVN 本体\\Editors 日本語\\receiver.exe" && parsed(launch.parameters) == std::vector<std::wstring>{L"/relative", context().file_path});
}
TEST(show_and_verb_and_quit_are_passed_to_native_launch_boundary) {
    auto entry = app(); entry.verb = L"edit"; entry.quit_after_launch = true;
    for (auto show : {ExternalShow::Normal, ExternalShow::Minimized, ExternalShow::Maximized}) {
        entry.show = show; const auto launch = plan_external_application(entry, context());
        CHECK(launch.verb == L"edit" && launch.quit_after_launch);
        CHECK(launch.show == (show == ExternalShow::Normal ? SW_SHOWNORMAL : show == ExternalShow::Minimized ? SW_SHOWMINIMIZED : SW_SHOWMAXIMIZED));
    }
}
TEST(empty_image_keeps_app_launch_and_viewer_folder_without_copying_clipboard) {
    auto entry = app(); auto source = context(); source.file_path.clear(); source.folder_path.clear(); source.image_available = false;
    for (auto send : {ExternalSend::None, ExternalSend::File, ExternalSend::Folder, ExternalSend::Clipboard}) {
        entry.send = send; const auto launch = plan_external_application(entry, source);
        CHECK(!launch.copy_image && launch.file == entry.executable);
        CHECK(parsed(launch.parameters) == (send == ExternalSend::Folder ? std::vector{source.viewer_directory.wstring()} : std::vector<std::wstring>{}));
    }
}
TEST(empty_executable_file_and_folder_use_the_source_as_association_target) {
    auto entry = default_external_applications()[7];
    auto launch = plan_external_application(entry, context());
    CHECK(launch.file == context().file_path && launch.parameters.empty() && launch.verb == L"open");
    entry = default_external_applications()[8]; launch = plan_external_application(entry, context());
    CHECK(launch.file == context().folder_path && launch.parameters.empty() && launch.verb.empty());
}
TEST(displayed_png_extension_is_explicit_and_never_replaces_original_file_mode) {
    auto entry = app(); auto source = context(); source.displayed_png = L"C:\\GVN temp\\edited Alpha.png";
    CHECK(parsed(plan_external_application(entry, source).parameters) == std::vector{source.file_path});
    entry.send = ExternalSend::DisplayedPng;
    CHECK(parsed(plan_external_application(entry, source).parameters) == std::vector{source.displayed_png->wstring()});
    source.displayed_png.reset(); THROWS(plan_external_application(entry, source));
    source.image_available = false; CHECK(plan_external_application(entry, source).parameters.empty());
}
TEST(automatic_quoting_roundtrips_empty_unicode_quotes_and_trailing_slashes) {
    for (const auto &text : std::vector<std::wstring>{L"", L"a", L"C:\\Folder 日本語 space\\", L"\\\\server\\share\\", L"a\"b\\\\\"c\\", L"\U0001f600.png"})
        CHECK(parsed(quote_external_argument(text)) == std::vector{text});
}
TEST(malformed_fields_ambiguous_paths_and_expanded_overflow_are_rejected) {
    auto entry = app(); auto source = context();
    entry.executable = L"\"unclosed"; THROWS(plan_external_application(entry, source));
    entry.executable = L"\"\" /option"; THROWS(plan_external_application(entry, source));
    entry.executable = L"a\"b.exe"; THROWS(plan_external_application(entry, source));
    entry.executable = L"D:relative.exe"; THROWS(plan_external_application(entry, source));
    entry = app(); entry.verb = std::wstring(L"a\0b", 3); THROWS(plan_external_application(entry, source));
    entry = app(); entry.send = static_cast<ExternalSend>(99); THROWS(plan_external_application(entry, source));
    entry = app(); entry.show = static_cast<ExternalShow>(-1); THROWS(plan_external_application(entry, source));
    entry = app(); source.viewer_directory = L"relative"; THROWS(plan_external_application(entry, source));
    source = context(); entry.executable = L"\"receiver.exe\" ($f)($f)"; source.file_path = std::wstring(20000, L'x');
    THROWS(plan_external_application(entry, source));
    source.file_path = std::wstring(32760, L'x'); entry.executable = L"C:\\receiver.exe";
    THROWS(plan_external_application(entry, source));
}
int main() { return run_tests(); }
