#include "gvn/settings.h"
#include "test.h"
#include <fstream>
using namespace gvn;
namespace {
struct File {
    std::filesystem::path path;
    File() {
        static size_t count{};
        path = std::filesystem::temp_directory_path() / (L"GVN-external-settings-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++count) + L".ini");
        CHECK(!std::filesystem::exists(path));
    }
    ~File() { std::error_code ignored; std::filesystem::remove(path, ignored); }
};
void put(const File &file, const wchar_t *section, const std::wstring &key, const wchar_t *value) {
    CHECK(WritePrivateProfileStringW(section, key.c_str(), value, file.path.c_str()));
}
std::vector<char> bytes(const File &file) {
    std::ifstream input(file.path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
} // namespace
TEST(all_nine_external_entries_roundtrip_every_option_and_quoted_command) {
    File file; Settings original;
    for (size_t i = 0; i < 9; ++i) {
        ExternalApplication entry;
        entry.name = L"画像エディタ 日本語 " + std::to_wstring(i + 1);
        entry.executable = L"\"D:\\Editor 日本語\\app.exe\" /option \"($f)\" \"($d)\"";
        entry.verb = L"edit";
        entry.send = static_cast<ExternalSend>(i % 5); entry.show = static_cast<ExternalShow>(i % 3);
        entry.quit_after_launch = i & 1; entry.separator = i & 2;
        entry.include_archive_member = i & 4; entry.archive_file_for_folder = i & 1;
        original.set_external_application(i, entry);
    }
    original.save(file.path); Settings restored; restored.load(file.path);
    CHECK(restored.external_applications() == original.external_applications());
    CHECK(restored.external == original.external);
    const auto before = bytes(file); restored.save(file.path); CHECK(bytes(file) == before);
}
TEST(old_registered_paths_migrate_once_without_changing_the_old_file_on_load) {
    File file;
    put(file, L"External", L"1", L"D:\\Editor space\\editor.exe");
    put(file, L"External", L"8", L"D:\\Other\\other.exe");
    const auto before = bytes(file); Settings settings; settings.load(file.path); CHECK(bytes(file) == before);
    CHECK(settings.external_application(0) == migrate_external_application(0, L"D:\\Editor space\\editor.exe"));
    CHECK(settings.external_application(7) == migrate_external_application(7, L"D:\\Other\\other.exe"));
    CHECK(settings.external_application(8) == default_external_applications()[8]);
    settings.save(file.path); Settings restored; restored.load(file.path);
    CHECK(restored.external_applications() == settings.external_applications());
}
TEST(empty_names_hide_entries_and_outer_quotes_edge_spaces_and_apostrophes_survive_ini) {
    File file; Settings settings;
    const auto values = std::vector<std::wstring>{L"\"D:\\a b.exe\"", L"  \"D:\\a b.exe\" /x \"($f)\"  ", L"'literal'", L"", L"\"a\"b\""};
    for (size_t i = 0; i < values.size(); ++i) {
        ExternalApplication entry; entry.name = L""; entry.executable = values[i]; entry.verb = L" 'edit' ";
        settings.set_external_application(i, entry);
    }
    settings.save(file.path); Settings restored; restored.load(file.path);
    CHECK(restored.external_applications() == settings.external_applications());
}
TEST(sparse_or_unknown_metadata_uses_each_legacy_row_without_losing_other_rows) {
    File file; put(file, L"External", L"2", L"D:\\old.exe");
    put(file, L"ExternalOptions", L"Version", L"1");
    put(file, L"ExternalOptions", L"1.present", L"1");
    put(file, L"ExternalOptions", L"1.name", L"Custom");
    put(file, L"ExternalOptions", L"1.command", L"D:\\new.exe");
    put(file, L"ExternalOptions", L"1.send", L"3");
    Settings settings; settings.load(file.path);
    CHECK(settings.external_application(0).name == L"Custom" && settings.external_application(0).send == ExternalSend::Clipboard);
    CHECK(settings.external_application(1) == migrate_external_application(1, L"D:\\old.exe"));
    put(file, L"ExternalOptions", L"Version", L"999"); settings.load(file.path);
    CHECK(settings.external_application(0) == default_external_applications()[0]);
    CHECK(settings.external_application(1) == migrate_external_application(1, L"D:\\old.exe"));
}
TEST(corrupt_enum_and_boolean_values_are_bounded_and_long_names_are_trimmed) {
    File file; Settings settings; settings.save(file.path);
    for (const auto &[key, value] : std::vector<std::pair<std::wstring, const wchar_t *>>{
             {L"1.send", L"99999999999999999999999"}, {L"1.show", L"-1"}, {L"1.quit", L"9"},
             {L"1.separator", L"1junk"}, {L"1.archive_member", L"true"}, {L"1.archive_folder", L"-5"}})
        put(file, L"ExternalOptions", key, value);
    put(file, L"ExternalOptions", L"1.name", std::wstring(60, L'画').c_str());
    Settings restored; restored.load(file.path); auto entry = restored.external_application(0);
    CHECK(entry.name == std::wstring(50, L'画') && entry.send == ExternalSend::File && entry.show == ExternalShow::Normal);
    CHECK(!entry.quit_after_launch && !entry.separator && !entry.include_archive_member && !entry.archive_file_for_folder);
}
TEST(legacy_direct_assignment_and_new_setter_preserve_each_others_intent) {
    Settings settings; settings.external[7] = L"D:\\old editor.exe";
    CHECK(settings.external_application(7).send == ExternalSend::DisplayedPng);
    auto next = settings.external_application(7); next.name = L"New name"; next.send = ExternalSend::File;
    settings.set_external_application(7, next);
    CHECK(settings.external_application(7) == next && settings.external[7] == next.executable);
    settings.external[7] = L"D:\\second.exe";
    CHECK(settings.external_application(7).name == L"second" && settings.external_application(7).send == ExternalSend::DisplayedPng);
}
TEST(invalid_persisted_text_and_choices_do_not_partially_write_a_settings_file) {
    File file; Settings settings; settings.save(file.path); const auto before = bytes(file);
    for (const auto &value : {std::wstring(L"a\0b", 3), std::wstring(L"a\nb"), std::wstring(L"a\rb"), std::wstring(32761, L'x')}) {
        settings.external_options[0].executable = value; THROWS(settings.save(file.path)); CHECK(bytes(file) == before);
    }
    settings.external_options[0].executable.clear(); settings.external_options[0].send = static_cast<ExternalSend>(-1);
    THROWS(settings.save(file.path)); CHECK(bytes(file) == before);
    settings.external_options[0].send = ExternalSend::File; settings.external_options[0].show = static_cast<ExternalShow>(42);
    THROWS(settings.save(file.path)); CHECK(bytes(file) == before);
}
TEST(reordering_and_single_row_reset_survive_reload_without_changing_other_options) {
    File file; Settings settings; settings.grid = 17; settings.confirm_unsaved = true;
    auto all = settings.external_applications(); all[2].name = L"Moved"; all[2].executable = L"D:\\editor.exe"; all[2].include_archive_member = true;
    CHECK(move_external_application(all, 2, -1) == 1); settings.set_external_applications(all); settings.save(file.path);
    Settings restored; restored.load(file.path); CHECK(restored.external_applications() == all && restored.grid == 17 && restored.confirm_unsaved);
    reset_external_application(all, 1); restored.set_external_applications(all); restored.save(file.path);
    Settings again; again.load(file.path); CHECK(again.external_applications() == all && again.grid == 17 && again.confirm_unsaved);
}
int main() { return run_tests(); }
