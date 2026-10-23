#include "gvn/settings.h"
#include "test.h"
#include "../src/settings_profile.h"
#include <fstream>
#include <tuple>
using namespace gvn;
TEST(save_format_options_roundtrip_every_field_and_enum) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-save-options-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings settings;
    CHECK(settings.saving == SaveOptions{} && settings.saving.jpeg.quality == 90 &&
          settings.saving.png.transparency == PngTransparency::Alpha);
    for (unsigned mode = 0; mode < 7; ++mode) {
        auto &save = settings.saving;
        save.last_format = static_cast<SaveFormat>(mode % 4);
        save.bmp_bits = std::array{0u, 1u, 4u, 8u, 24u}[mode % 5];
        save.keep_times = save.uppercase_extension = save.bmp.top_down = save.bmp.rle = save.gif.interlaced =
            save.gif.uncompressed = save.jpeg.progressive = save.png.interlaced = mode % 2;
        save.dither = !(mode % 2);
        save.png.compression = mode;
        save.jpeg.quality = mode * 15;
        save.gif.transparent_index = mode * 40;
        save.png.filter = static_cast<PngFilter>(mode);
        save.png.transparency = static_cast<PngTransparency>(mode);
        save.gif.transparency = static_cast<GifTransparency>(mode % 6);
        save.png.matte = 0x123456 + mode;
        save.png.transparent_color = 0x234567 + mode;
        save.jpeg.matte = 0x345678 + mode;
        save.bmp.matte = 0x456789 + mode;
        save.gif.matte = 0x56789a + mode;
        settings.save(path);
        Settings restored;
        restored.load(path);
        CHECK(restored.saving == save);
    }
    std::filesystem::remove(path);
}
TEST(save_format_ini_legacy_defaults_and_invalid_values_are_bounded) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-save-options-invalid-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings settings;
    settings.save(path);
    CHECK(WritePrivateProfileStringW(L"Saving", nullptr, nullptr, path.c_str()));
    settings = {};
    settings.load(path);
    CHECK(settings.saving == SaveOptions{});
    for (auto [key, value] : {std::pair{L"png_compression", L"999"},
                              {L"jpeg_quality", L"-5"},
                              {L"gif_transparent_index", L"999"},
                              {L"bmp_bits", L"2"},
                              {L"png_filter", L"99"},
                              {L"png_transparency", L"-2"},
                              {L"gif_transparency", L"999"},
                              {L"last_format", L"-1"},
                              {L"bmp_matte", L"999999999"},
                              {L"png_matte", L"-1"},
                              {L"jpeg_matte", L"3junk"},
                              {L"gif_matte", L"overflow"},
                              {L"uppercase_extension", L"999"}})
        CHECK(WritePrivateProfileStringW(L"Saving", key, value, path.c_str()));
    settings.load(path);
    const auto &save = settings.saving;
    CHECK(save.png.compression == 9 && save.jpeg.quality == 0 && save.gif.transparent_index == 255 &&
          save.bmp_bits == 0 && save.png.filter == PngFilter::Speed &&
          save.png.transparency == PngTransparency::Alpha &&
          save.gif.transparency == GifTransparency::BottomRight && save.last_format == SaveFormat::Png &&
          save.bmp.matte == 0xffffff && save.png.matte == 0 && save.jpeg.matte == 0xffffff &&
          save.gif.matte == 0xffffff && save.uppercase_extension);
    std::filesystem::remove(path);
}
TEST(bmp_16_save_depth_roundtrips_and_invalid_neighboring_values_remain_rejected) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-bmp16-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings value;
    value.saving.bmp_bits = 16;
    value.saving.bmp = {true, true, 0x123456};
    value.save(path);
    Settings read;
    read.load(path);
    CHECK(read.saving == value.saving);
    for (const auto invalid : {L"15", L"17"}) {
        CHECK(WritePrivateProfileStringW(L"Saving", L"bmp_bits", invalid, path.c_str()));
        Settings damaged;
        damaged.load(path);
        CHECK(damaged.saving.bmp_bits == 0);
    }
    std::filesystem::remove(path);
}
TEST(file_operation_options_defaults_and_roundtrip) {
    Settings settings;
    CHECK(settings.save_dialog && settings.confirm_overwrite && settings.remember_save_folder &&
          settings.rename_resort && settings.confirm_delete && settings.recycle_deleted &&
          !settings.next_after_fileop && settings.confirm_move && settings.confirm_copy);
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-fileops-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    settings.save_dialog = settings.confirm_overwrite = settings.remember_save_folder =
        settings.rename_resort = settings.confirm_delete = settings.recycle_deleted = settings.confirm_move =
            settings.confirm_copy = false;
    settings.next_after_fileop = true;
    settings.last_save_folder = L"G:\\試験画像\\保存 先";
    settings.save(path);
    Settings restored;
    restored.load(path);
    CHECK(!restored.save_dialog && !restored.confirm_overwrite && !restored.remember_save_folder &&
          !restored.rename_resort && !restored.confirm_delete && !restored.recycle_deleted &&
          restored.next_after_fileop && !restored.confirm_move && !restored.confirm_copy);
    CHECK(restored.last_save_folder == settings.last_save_folder);
    std::filesystem::remove(path);
}
TEST(cache_policy_defaults_roundtrip_and_clamps_legacy_or_invalid_values) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-cache-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings defaults;
    CHECK(defaults.cache_mib == 64 && defaults.prefetch_enabled);
    defaults.cache_mib = 128;
    defaults.prefetch_enabled = false;
    defaults.save(path);
    Settings saved;
    saved.load(path);
    CHECK(saved.cache_mib == 128 && !saved.prefetch_enabled);
    for (const auto value : {0, 1, 64, 4096}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"cache_mib", std::to_wstring(value).c_str(),
                                         path.c_str()));
        saved.load(path);
        CHECK(saved.cache_mib == value);
    }
    CHECK(WritePrivateProfileStringW(L"Viewer", L"cache_mib", L"-1", path.c_str()));
    saved.load(path);
    CHECK(saved.cache_mib == 0);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"cache_mib", L"99999", path.c_str()));
    saved.load(path);
    CHECK(saved.cache_mib == 4096);
    CHECK(WritePrivateProfileStringW(L"Viewer", nullptr, nullptr, path.c_str()));
    saved = Settings{};
    saved.load(path);
    CHECK(saved.cache_mib == 64 && saved.prefetch_enabled);
    std::filesystem::remove(path);
}
TEST(viewer_defaults_unsaved_option_and_one_time_wheel_migration) {
    Settings settings;
    CHECK(!settings.confirm_unsaved && settings.wheel == 1 && settings.mouse.wheel_preset() == 1);
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-defaults-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    settings.confirm_unsaved = true;
    settings.mouse.basic_wheel(false);
    settings.mouse.assign(MouseGesture::X1Click, 0, 0, Checker);
    settings.save(path);
    Settings read;
    read.load(path);
    CHECK(read.confirm_unsaved && read.mouse.wheel_preset() == 0);
    CHECK(read.mouse.action(MouseGesture::X1Click, 0, 0) == Checker);
    CHECK(WritePrivateProfileStringW(L"MouseBindings", L"DefaultsVersion", nullptr, path.c_str()));
    read = Settings{};
    read.load(path);
    CHECK(read.mouse.wheel_preset() == 1 && read.wheel == 1);
    CHECK(read.mouse.action(MouseGesture::X1Click, 0, 0) == Checker);
    read.mouse.basic_wheel(false);
    read.confirm_unsaved = false;
    read.save(path);
    read = Settings{};
    read.load(path);
    CHECK(!read.confirm_unsaved && read.mouse.wheel_preset() == 0);
    std::filesystem::remove(path);
}
TEST(loading_settings_roundtrip_validation_and_legacy_attribute_migration) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-loading-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings settings;
    settings.preserve_alpha = settings.bmp_transparency = settings.gif_transparency =
        settings.png_transparency = false;
    settings.gif_background = settings.png_background = true;
    settings.gif_animation = settings.exif_orientation = false;
    settings.hidden = settings.protected_files = true;
    settings.system_files = false;
    settings.transparency_background = 0x123456;
    settings.save(path);
    Settings read;
    read.load(path);
    CHECK(read.loading_options() == settings.loading_options());
    CHECK(read.hidden && !read.system_files && read.protected_files);
    CHECK(WritePrivateProfileSectionW(L"Viewer", L"hidden=1\0\0", path.c_str()));
    read = Settings{};
    read.load(path);
    CHECK(read.hidden && read.system_files && read.protected_files);
    auto default_loading=LoadingOptions{};default_loading.bitfield_to24=true;
    CHECK(read.loading_options() == default_loading);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"transparency_background", L"999999999", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer", L"exif_orientation", L"corrupt", path.c_str()));
    read.load(path);
    CHECK(read.transparency_background == 0xffffff && read.exif_orientation);
    std::filesystem::remove(path);
}
TEST(section_profile_matches_windows_key_reader_and_reloads) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-profile-semantics-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    const std::wstring contents =
        L"\ufeff[Viewer]\r\n  toolbar = \"0\"  \r\nTOOLBAR=1\r\n;comment=ignored\r\n"
        L"sort= '3' \r\nempty=\r\nspaces= a b  \r\nquoted=\" a b \"\r\n"
        L"single='a=b;c'\r\nmismatch=\"hello'\r\nnoequals\r\ntabs=\t x \t\r\n"
        L"mixed=\"'quoted'\"\r\n日本語=透過画像\r\n[viewer]\r\nextra=1\r\n"
        L"[Destinations]\r\n1=\"C:\\画像\\a=b;c\"\r\n";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(contents.data()), contents.size() * sizeof(wchar_t));
    }
    const auto oracle = [&](const wchar_t *section, const std::wstring &key, const std::wstring &initial) {
        std::vector<wchar_t> buffer(32768);
        GetPrivateProfileStringW(section, key.c_str(), initial.c_str(), buffer.data(),
                                 static_cast<DWORD>(buffer.size()), path.c_str());
        return std::wstring(buffer.data());
    };
    detail::ProfileReader reader(path);
    for (const auto section : {L"Viewer", L"VIEWER", L"Destinations", L"Missing"})
        for (const auto key :
             {L"toolbar", L"SORT", L"empty", L"spaces", L"quoted", L"single", L"mismatch", L"noequals",
              L"tabs", L"mixed", L"日本語", L"extra", L"comment", L"1", L"missing"})
            for (const auto initial : {L"fallback", L" trailing  ", L"tab\t ", L"\"quoted\"", L""}) {
                const auto expected = oracle(section, key, initial);
                if (reader.read(section, key, initial) != expected)
                    throw std::runtime_error("Section reader differs from Windows INI semantics");
            }
    Settings first;
    first.load(path);
    CHECK(!first.toolbar && first.sort == 3 && first.copy_destinations[0] == L"C:\\画像\\a=b;c");
    CHECK(first.move_destinations == first.copy_destinations);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"sort", L"4", path.c_str()));
    Settings changed;
    changed.load(path);
    CHECK(changed.sort == 4); // The cache lasts for one load only.
    std::filesystem::remove(path);
    detail::ProfileReader missing(path);
    CHECK(missing.read(L"Viewer", L"toolbar", L"1") == L"1");
}
TEST(section_profile_handles_sections_and_values_larger_than_initial_buffer) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-large-profile-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::wstring contents = L"\ufeff[Viewer]\r\n";
    for (int i = 0; i < 5000; ++i)
        contents += L"unknown" + std::to_wstring(i) + L"=ignored long row\r\n";
    contents += L"sort=4\r\n[Destinations]\r\n1=\"" + std::wstring(40000, L'画') + L"\"\r\n";
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(contents.data()), contents.size() * sizeof(wchar_t));
    }
    Settings settings;
    settings.load(path);
    CHECK(settings.sort == 4);
    std::vector<wchar_t> expected(32768);
    GetPrivateProfileStringW(L"Destinations", L"1", L"", expected.data(), static_cast<DWORD>(expected.size()),
                             path.c_str());
    CHECK(settings.copy_destinations[0] == expected.data());
    CHECK(settings.move_destinations == settings.copy_destinations);
    // Windows also bounds/parses overlong individual lines. Match its result,
    // while ensuring this value required growing the section buffer.
    CHECK(settings.copy_destinations[0].size() > 4096);
    std::filesystem::remove(path);
}
TEST(key_parse_and_dispatch) {
    UINT key{}, modifiers{};
    CHECK(parse_key(L"Ctrl+Shift+D", key, modifiers) && key == 'D' && modifiers == 3);
    CHECK(parse_key(L"F24", key, modifiers) && key == VK_F24);
    CHECK(!parse_key(L"F25", key, modifiers));
    CHECK(!parse_key(L"Ctrl+O+P", key, modifiers));
    Settings settings;
    CHECK(settings.command_for('D', 3) == ArmDrag);
    CHECK(settings.command_for('D', 1) == 0);
    CHECK(settings.command_for(VK_ADD, 0) == ZoomIn);
    for (const UINT key : std::vector<UINT>{'A', '0', VK_F24, VK_TAB, VK_OEM_4, VK_NUMPAD5, VK_OEM_PLUS}) {
        const auto text = format_key(key, 3);
        UINT read{}, mods{};
        CHECK(!text.empty() && parse_key(text, read, mods) && read == key && mods == 3);
    }
}
TEST(jis_shortcuts_parse_format_and_reject_unsupported_ime_keys) {
    for (const auto [name, expected] :
         std::vector<std::pair<std::wstring, UINT>>{{L"Convert", VK_CONVERT}, {L"変換", VK_CONVERT},
                                                   {L"NonConvert", VK_NONCONVERT}, {L"無変換", VK_NONCONVERT}}) {
        UINT key{}, modifiers{};
        CHECK(parse_key(name, key, modifiers) && key == expected && modifiers == 0);
        CHECK(assignable_key(name));
        for (UINT mods = 0; mods < 8; ++mods) {
            const auto text = format_key(expected, mods);
            CHECK(!text.empty() && parse_key(text, key, modifiers) && key == expected && modifiers == mods);
        }
    }
    for (const UINT key : {VK_KANA, VK_KANJI, VK_CAPITAL, VK_NUMLOCK, VK_SCROLL, VK_PROCESSKEY})
        CHECK(format_key(key, 0).empty());
    UINT key{}, modifiers{};
    CHECK(!parse_key(L"Ctrl+変換+無変換", key, modifiers));
}
TEST(jis_shortcuts_alias_conflicts_and_ini_roundtrip) {
    Settings settings;
    settings.set_keys(FlipH, {L"無変換", L"Shift+NonConvert"});
    settings.set_keys(FlipV, {L"Convert", L"Ctrl+変換"});
    CHECK(settings.command_for(VK_NONCONVERT, 0) == FlipH);
    CHECK(settings.command_for(VK_NONCONVERT, 2) == FlipH);
    CHECK(settings.command_for(VK_CONVERT, 0) == FlipV);
    CHECK(settings.command_for(VK_CONVERT, 1) == FlipV);
    CHECK(settings.command_for(VK_CONVERT, 2) == 0);
    THROWS(settings.set_keys(FlipH, {L"NonConvert", L"無変換"}));
    THROWS(settings.set_keys(Next, {L"NonConvert"}));
    CHECK(settings.keys_for(FlipH) == std::vector<std::wstring>{L"無変換", L"Shift+NonConvert"});
    wchar_t temporary[32768]{};
    CHECK(GetTempPathW(32768, temporary));
    const auto file = std::filesystem::path(temporary) /
                      (L"GVN-jis-shortcuts-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    settings.save(file);
    Settings read;
    read.load(file);
    CHECK(read.keys_for(FlipH) == settings.keys_for(FlipH));
    CHECK(read.keys_for(FlipV) == settings.keys_for(FlipV));
    CHECK(read.command_for(VK_NONCONVERT, 2) == FlipH);
    CHECK(read.command_for(VK_CONVERT, 1) == FlipV);
    CHECK(read.command_for('O', 1) == Open);
    std::filesystem::remove(file);
}
TEST(direct_delete_catalog_default_key_and_mouse_assignment_roundtrip) {
    Settings settings;
    CHECK(settings.command_for(VK_DELETE, 0) == FileDelete);
    CHECK(settings.command_for(VK_DELETE, 2) == FileDeleteDirect);
    CHECK(std::wstring(command_name(FileDeleteDirect)).find(L"ごみ箱を使わない") != std::wstring::npos);
    settings.mouse.assign(MouseGesture::MiddleClick, 0, 0, FileDeleteDirect);
    CHECK(settings.mouse.valid() && settings.mouse.action(MouseGesture::MiddleClick, 0, 0) == FileDeleteDirect);
    settings.set_keys(FileDeleteDirect, {L"Shift+Delete", L"Alt+D"});
    wchar_t temporary[32768]{};
    CHECK(GetTempPathW(32768, temporary));
    const auto file = std::filesystem::path(temporary) /
                      (L"GVN-direct-delete-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    settings.save(file);
    Settings read;
    read.load(file);
    CHECK(read.keys_for(FileDeleteDirect) == settings.keys_for(FileDeleteDirect));
    CHECK(read.mouse.action(MouseGesture::MiddleClick, 0, 0) == FileDeleteDirect);
    CHECK(read.recycle_deleted && read.confirm_delete);
    std::filesystem::remove(file);
}
TEST(direct_delete_new_shortcut_does_not_replace_existing_profile_keys) {
    wchar_t temporary[32768]{};
    CHECK(GetTempPathW(32768, temporary));
    const auto file = std::filesystem::path(temporary) /
                      (L"GVN-direct-delete-migration-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings old;
    old.set_keys(FileDeleteDirect, {});
    old.set_keys(FlipH, {L"Shift+Delete"});
    old.set_keys(Open, {L"Alt+O"});
    old.save(file);
    CHECK(WritePrivateProfileStringW(L"KeyBindings", std::to_wstring(FileDeleteDirect).c_str(), nullptr, file.c_str()));
    Settings migrated;
    migrated.load(file);
    CHECK(migrated.keys_for(FileDeleteDirect).empty());
    CHECK(migrated.command_for(VK_DELETE, 2) == FlipH && migrated.command_for('O', 4) == Open);
    migrated.set_keys(FlipH, {});
    migrated.set_keys(FileDeleteDirect, {L"Shift+Delete"});
    migrated.save(file);
    Settings explicit_binding;
    explicit_binding.load(file);
    CHECK(explicit_binding.command_for(VK_DELETE, 2) == FileDeleteDirect);
    explicit_binding.set_keys(FileDeleteDirect, {});
    explicit_binding.save(file);
    Settings cleared;
    cleared.load(file);
    CHECK(cleared.keys_for(FileDeleteDirect).empty());
    CHECK(WritePrivateProfileStringW(L"KeyBindings", std::to_wstring(Open).c_str(), L"invalid key", file.c_str()));
    Settings corrupt;
    corrupt.load(file);
    CHECK(corrupt.keys_for(FileDeleteDirect).empty() && corrupt.command_for(VK_DELETE, 0) == FileDelete);
    std::filesystem::remove(file);
    Settings fresh;
    fresh.load(file);
    CHECK(fresh.command_for(VK_DELETE, 2) == FileDeleteDirect);
}
TEST(settings_roundtrip_and_corruption) {
    wchar_t temp[32768]{};
    GetTempPathW(32768, temp);
    const auto path = std::filesystem::path(temp) /
                      (L"GVN-settings-test-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings settings;
    settings.interval = 4200;
    settings.toolbar = false;
    settings.scrollbars = true;
    settings.full_scrollbars = true;
    settings.copy_destinations[2] = L"C:\\画像\\保存先";
    settings.remember(L"C:\\画像");
    settings.remember(L"D:\\写真");
    settings.remember(L"C:\\画像");
    settings.bindings[0].key = L"Alt+O";
    settings.save(path);
    Settings read;
    read.load(path);
    CHECK(read.interval == 4200 && !read.toolbar);
    CHECK(read.scrollbars && read.full_scrollbars);
    CHECK(read.copy_destinations[2] == settings.copy_destinations[2]);
    CHECK(read.history == settings.history);
    CHECK(read.command_for('O', 4) == Open);
    WritePrivateProfileStringW(L"Viewer", L"interval", L"-20", path.c_str());
    WritePrivateProfileStringW(L"Viewer", L"sort", L"corrupt", path.c_str());
    read.load(path);
    CHECK(read.interval == 250 && read.sort == 0);
    std::filesystem::remove(path);
}
TEST(registered_destinations_migrate_legacy_and_keep_explicit_empty_slots) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-destination-migration-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    Settings{}.save(path);
    for (int i = 1; i <= 10; ++i) {
        const auto key = std::to_wstring(i), value = L"../旧送り先/画像 " + key;
        CHECK(WritePrivateProfileStringW(L"Destinations", key.c_str(), value.c_str(), path.c_str()));
    }
    // Remove modern sections so this file is genuinely an older shared configuration.
    CHECK(WritePrivateProfileStringW(L"CopyDestinations", nullptr, nullptr, path.c_str()));
    CHECK(WritePrivateProfileStringW(L"MoveDestinations", nullptr, nullptr, path.c_str()));
    Settings legacy;
    legacy.load(path);
    for (int i = 0; i < 10; ++i) {
        CHECK(legacy.copy_destinations[i] == L"../旧送り先/画像 " + std::to_wstring(i + 1));
        CHECK(legacy.move_destinations[i] == legacy.copy_destinations[i]);
    }
    CHECK(WritePrivateProfileStringW(L"CopyDestinations", L"1", L"", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"MoveDestinations", L"2", L"../移動専用", path.c_str()));
    Settings mixed;
    mixed.load(path);
    CHECK(mixed.copy_destinations[0].empty() && mixed.move_destinations[0] == legacy.move_destinations[0]);
    CHECK(mixed.move_destinations[1] == L"../移動専用" &&
          mixed.copy_destinations[1] == legacy.copy_destinations[1]);
    mixed.copy_destinations = {};
    mixed.move_destinations = {};
    mixed.save(path);
    Settings reset;
    reset.load(path);
    CHECK(reset.copy_destinations == mixed.copy_destinations &&
          reset.move_destinations == mixed.move_destinations);
    // Old keys remain available to an old executable, but cannot resurrect cleared modern slots.
    detail::ProfileReader profile(path);
    CHECK(profile.read(L"Destinations", L"1", L"") == legacy.copy_destinations[0]);
    std::filesystem::remove(path);
}
TEST(registered_destinations_twenty_independent_values_roundtrip) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-destination-roundtrip-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings settings;
    for (int i = 0; i < 10; ++i) {
        settings.copy_destinations[i] = L"C:\\画像 コピー\\" + std::to_wstring(i) + L"=a;b";
        settings.move_destinations[i] = L"../移動先/" + std::to_wstring(i);
    }
    settings.copy_destinations[9] += std::wstring(5000, L'画');
    settings.move_destinations[9] += std::wstring(5000, L'移');
    settings.save(path);
    Settings read;
    read.load(path);
    CHECK(read.copy_destinations == settings.copy_destinations);
    CHECK(read.move_destinations == settings.move_destinations);
    CHECK(read.copy_destinations != read.move_destinations);
    std::filesystem::remove(path);
}
TEST(file_transfer_dialog_modes_basic_paths_and_old_settings_roundtrip) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-transfer-options-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings initial;
    CHECK(!initial.copy_registered && !initial.move_registered && initial.copy_basic_path.empty() &&
          initial.move_basic_path.empty());
    initial.copy_destinations[3] = L"../copy-4";
    initial.move_destinations[8] = L"../move-9";
    initial.save(path);
    for (const auto name : {L"copy_registered", L"move_registered"})
        CHECK(WritePrivateProfileStringW(L"Viewer", name, nullptr, path.c_str()));
    CHECK(WritePrivateProfileStringW(L"CopyDestinations", L"Basic", nullptr, path.c_str()));
    CHECK(WritePrivateProfileStringW(L"MoveDestinations", L"Basic", nullptr, path.c_str()));
    Settings old;
    old.load(path);
    CHECK(old.copy_destinations == initial.copy_destinations &&
          old.move_destinations == initial.move_destinations);
    CHECK(!old.copy_registered && !old.move_registered && old.copy_basic_path.empty() &&
          old.move_basic_path.empty());
    old.copy_registered = true;
    old.copy_basic_path = L"C:\\画像\\copy = target;name";
    old.move_basic_path = L"../移動先/" + std::wstring(5000, L'画');
    old.save(path);
    Settings read;
    read.load(path);
    CHECK(read.copy_registered && !read.move_registered);
    CHECK(read.copy_basic_path == old.copy_basic_path && read.move_basic_path == old.move_basic_path);
    CHECK(read.copy_destinations == old.copy_destinations && read.move_destinations == old.move_destinations);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"copy_registered", L"garbage", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer", L"move_registered", L"-8", path.c_str()));
    Settings corrupt;
    corrupt.load(path);
    CHECK(!corrupt.copy_registered && !corrupt.move_registered);
    corrupt.copy_basic_path.clear();
    corrupt.move_basic_path.clear();
    corrupt.save(path);
    Settings empty;
    empty.load(path);
    CHECK(empty.copy_basic_path.empty() && empty.move_basic_path.empty());
    std::filesystem::remove(path);
}
TEST(caption_overlay_font_colors_roundtrip_and_validation) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-overlay-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings s;
    s.overlay_full = false;
    s.overlay_window = s.overlay_animation = s.overlay_italic = s.overlay_underline = s.overlay_strikeout =
        true;
    s.overlay_family = L"Yu Gothic UI";
    s.overlay_points = 185;
    s.overlay_weight = 700;
    s.overlay_position = 8;
    s.overlay_background = 1;
    s.overlay_foreground = 0x12abef;
    s.overlay_backcolor = 0x987654;
    s.save(path);
    Settings read;
    read.load(path);
    CHECK(!read.overlay_full && read.overlay_window && read.overlay_animation);
    CHECK(read.overlay_italic && read.overlay_underline && read.overlay_strikeout);
    CHECK(read.overlay_family == s.overlay_family && read.overlay_points == 185 &&
          read.overlay_weight == 700);
    CHECK(read.overlay_position == 8 && read.overlay_background == 1);
    CHECK(read.overlay_foreground == 0x12abef && read.overlay_backcolor == 0x987654);
    WritePrivateProfileStringW(L"Viewer", L"overlay_position", L"999", path.c_str());
    WritePrivateProfileStringW(L"Viewer", L"overlay_background", L"-1", path.c_str());
    WritePrivateProfileStringW(L"Viewer", L"overlay_points", L"0", path.c_str());
    WritePrivateProfileStringW(L"Viewer", L"overlay_weight", L"not a number", path.c_str());
    WritePrivateProfileStringW(L"Viewer", L"overlay_family", L"", path.c_str());
    read = Settings{};
    read.load(path);
    CHECK(read.overlay_position == 8 && read.overlay_background == 0 && read.overlay_points == 10);
    CHECK(read.overlay_weight == 400 && read.overlay_family == L"MS UI Gothic");
    s.overlay_family = L"Arial\r\noverlay_full=1";
    s.save(path);
    read.load(path);
    CHECK(!read.overlay_full && read.overlay_family.find_first_of(L"\r\n") == std::wstring::npos);
    std::filesystem::remove(path);
}
TEST(display_quality_zoom_roundtrip_and_legacy_defaults) {
    wchar_t temp[32768]{};
    GetTempPathW(32768, temp);
    const auto path = std::filesystem::path(temp) /
                      (L"GVN-display-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings original;
    original.center_image = false;
    original.display_down = 3;
    original.display_up = 4;
    original.full_down = 2;
    original.full_up = 0;
    original.fit_max = 150;
    original.fit_simple = 800;
    original.zoom_simple = 200;
    original.zoom_mode = 1;
    original.zoom_below = 5;
    original.zoom_above = 50;
    original.zoom_reduce = false;
    original.zoom_enlarge = false;
    original.drag_simple = false;
    original.full_hide_taskbar = false;
    original.full_hide_wait = true;
    original.save(path);
    Settings saved;
    saved.load(path);
    CHECK(!saved.center_image && saved.display_down == 3 && saved.display_up == 4);
    CHECK(saved.full_down == 2 && saved.full_up == 0 && saved.fit_max == 150 && saved.fit_simple == 800);
    CHECK(saved.zoom_simple == 200 && saved.zoom_mode == 1 && saved.zoom_below == 5 &&
          saved.zoom_above == 50);
    CHECK(!saved.zoom_reduce && !saved.zoom_enlarge && !saved.drag_simple);
    CHECK(!saved.full_hide_taskbar && saved.full_hide_wait);
    WritePrivateProfileStringW(L"Viewer", L"display_up", L"99", path.c_str());
    WritePrivateProfileStringW(L"Viewer", L"fit_max", L"0", path.c_str());
    WritePrivateProfileStringW(L"Viewer", L"zoom_mode", L"-1", path.c_str());
    saved.load(path);
    CHECK(saved.display_up == 13 && saved.fit_max == 1 && saved.zoom_mode == 0);
    // Old profiles keep their explicit nearest overrides without inventing filter choices.
    WritePrivateProfileSectionW(L"Viewer", L"nearest=1\0full_nearest=1\0display_down=1\0\0", path.c_str());
    Settings legacy;
    legacy.load(path);
    CHECK(legacy.nearest && legacy.full_nearest);
    CHECK(legacy.display_down == 1 && legacy.display_up == 1 && legacy.center_image &&
          legacy.fit_max == 5000);
    std::filesystem::remove(path);
}
TEST(display_filter_schema_preserves_legacy_choices_and_all_native_indices) {
    wchar_t temporary[32768]{};
    GetTempPathW(32768, temporary);
    const auto path = std::filesystem::path(temporary) /
                      (L"GVN-filter-schema-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    const auto write = [&](int version, int value) {
        std::wstring section;
        const auto field = [&](const wchar_t *key, int number) {
            section += key + (L"=" + std::to_wstring(number));
            section.push_back(L'\0');
        };
        if (version)
            field(L"display_filter_version", version);
        for (const auto key : {L"display_down", L"display_up", L"full_down", L"full_up"})
            field(key, value);
        section.push_back(L'\0');
        CHECK(WritePrivateProfileSectionW(L"Viewer", section.c_str(), path.c_str()));
    };
    for (int value = 0; value <= 13; ++value) {
        write(1, value);
        Settings read;
        read.load(path);
        CHECK(read.display_down == value && read.display_up == value && read.full_down == value &&
              read.full_up == value);
    }
    for (int value = 0; value <= 4; ++value) {
        write(0, value);
        Settings read;
        read.load(path);
        const auto expected = value < 2 ? value : value + 9;
        CHECK(read.display_down == expected && read.display_up == expected && read.full_down == expected &&
              read.full_up == expected);
        read.save(path);
        Settings next;
        next.load(path);
        CHECK(next.display_down == expected && next.full_up == expected);
    }
    CHECK(WritePrivateProfileSectionW(L"Viewer", L"nearest=1\0\0", path.c_str()));
    Settings missing;
    missing.display_down = 8;
    missing.load(path);
    CHECK(missing.display_down == 8); // Missing fields preserve an existing native choice.
    CHECK(WritePrivateProfileSectionW(L"Viewer", L"display_filter_version=1\0display_down=bad\0\0",
                                      path.c_str()));
    missing.load(path);
    CHECK(missing.display_down == 8);
    std::filesystem::remove(path);
}
TEST(multiple_shortcuts_clear_reorder_roundtrip_and_conflicts) {
    Settings settings;
    CHECK(settings.first_key(Print) == L"Ctrl+P");
    CHECK(settings.keys_for(ArmDrag).size() == 2);
    std::vector<std::wstring> keys;
    for (int i = 1; i <= 8; ++i)
        keys.push_back(L"Ctrl+Alt+" + std::to_wstring(i));
    settings.set_keys(FlipH, keys);
    for (int i = 1; i <= 8; ++i)
        CHECK(settings.command_for('0' + i, 5) == FlipH);
    auto rejected = [&](UINT command, std::vector<std::wstring> proposed) {
        bool threw{};
        try {
            settings.set_keys(command, proposed);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        CHECK(threw);
    };
    auto nine = keys;
    nine.push_back(L"Ctrl+Alt+9");
    rejected(FlipH, nine);
    rejected(Open, {keys[0]});
    rejected(Open, {L"Ctrl+S"});
    rejected(Open, {L"Alt+Tab"});
    rejected(Open, {L"Ctrl+Alt+Delete"});
    rejected(Open, {L"Esc"});
    rejected(Open, {L"Ctrl+Alt+Shift+Tab"});
    rejected(Open, {L"Ctrl+O", L"Control+O"});
    rejected(FlipV, {L"Add"}); // Alias of the existing Plus key.
    CHECK(settings.keys_for(FlipH) == keys);
    CHECK(settings.first_key(Open) == L"Ctrl+O");
    settings.set_keys(Print, {});
    settings.set_keys(Open, {L"Ctrl+P", L"Alt+O"});
    settings.set_keys(ArmDrag, {});
    CHECK(settings.command_for(VK_F6, 0) == 0);
    CHECK(settings.command_for('D', 3) == 0);
    settings.set_keys(CopyDestBase + 9, {L"Shift+F12"});
    settings.set_keys(ExternalBase + 8, {L"Alt+F12"});
    wchar_t temp[32768]{};
    GetTempPathW(32768, temp);
    const auto file = std::filesystem::path(temp) /
                      (L"GVN-multiple-keys-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    settings.save(file);
    Settings read;
    read.load(file);
    for (const auto &entry : command_catalog())
        CHECK(read.keys_for(entry.id) == settings.keys_for(entry.id));
    CHECK(read.command_for('P', 1) == Open && read.command_for('O', 1) == 0);
    CHECK(read.command_for(VK_F12, 2) == CopyDestBase + 9);
    CHECK(read.command_for(VK_F12, 4) == ExternalBase + 8);
    WritePrivateProfileStringW(L"KeyBindings", std::to_wstring(Open).c_str(), L"Ctrl+S", file.c_str());
    read.load(file);
    CHECK(read.command_for('O', 1) == Open && read.command_for('S', 1) == Save);
    std::filesystem::remove(file);
}
TEST(settings_customization_persists_and_history_can_be_disabled) {
    wchar_t temp[32768]{};
    GetTempPathW(32768, temp);
    const auto file = std::filesystem::path(temp) /
                      (L"GVN-customization-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings settings;
    settings.remember(L"C:\\old history");
    settings.undo_enabled = settings.remember_history = settings.paste_new = false;
    settings.full_nearest = settings.full_hide_cursor = settings.title_path = settings.topmost = true;
    settings.animation_autoplay = settings.confirm_overwrite = settings.confirm_delete = false;
    CHECK(!settings.confirm_unsaved);
    settings.confirm_unsaved = true;
    settings.initial_fit = 4;
    settings.zoom_step = 75;
    settings.skip_count = 17;
    settings.checker_light = 0x123456;
    settings.checker_dark = 0xabcdef;
    settings.checker_size = 13;
    settings.glass_size = 16;
    settings.save(file);
    Settings read;
    read.load(file);
    CHECK(!read.undo_enabled && !read.remember_history && !read.paste_new && read.history.empty());
    read.remember(L"C:\\must not be retained");
    CHECK(read.history.empty());
    CHECK(read.full_nearest && read.full_hide_cursor && read.title_path && read.topmost);
    CHECK(!read.animation_autoplay && !read.confirm_overwrite && !read.confirm_delete);
    CHECK(read.confirm_unsaved);
    CHECK(read.initial_fit == 4 && read.zoom_step == 75 && read.skip_count == 17);
    CHECK(read.checker_light == 0x123456 && read.checker_dark == 0xabcdef && read.checker_size == 13);
    CHECK(read.glass_size == 16);
    std::filesystem::remove(file);
}
TEST(legacy_shortcuts_migrate_without_losing_f6_or_overriding_custom_f6) {
    wchar_t temp[32768]{};
    GetTempPathW(32768, temp);
    const auto file = std::filesystem::path(temp) /
                      (L"GVN-legacy-keys-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings{}.save(file);
    WritePrivateProfileStringW(L"KeyBindings", nullptr, nullptr, file.c_str());
    WritePrivateProfileStringW(L"Keys", std::to_wstring(Open).c_str(), L"Alt+O", file.c_str());
    WritePrivateProfileStringW(L"Keys", std::to_wstring(ArmDrag).c_str(), L"Alt+D", file.c_str());
    Settings migrated;
    migrated.load(file);
    CHECK(migrated.command_for('O', 4) == Open && migrated.command_for('O', 1) == 0);
    CHECK(migrated.command_for('D', 4) == ArmDrag && migrated.command_for(VK_F6, 0) == ArmDrag);
    WritePrivateProfileStringW(L"Keys", std::to_wstring(Open).c_str(), L"F6", file.c_str());
    migrated.load(file);
    CHECK(migrated.command_for(VK_F6, 0) == Open);
    CHECK(migrated.keys_for(ArmDrag) == std::vector<std::wstring>{L"Alt+D"});
    migrated.set_keys(Open, {});
    migrated.set_keys(ArmDrag, {});
    migrated.save(file);
    Settings modern;
    modern.load(file);
    CHECK(modern.command_for(VK_F6, 0) == 0 && modern.keys_for(ArmDrag).empty());
    std::filesystem::remove(file);
}
TEST(mouse_chords_modifiers_inheritance_and_explicit_none) {
    using G = MouseGesture;
    MouseSettings mouse;
    CHECK(mouse.valid());
    CHECK(mouse.action(G::LeftDrag, 0, 0) == MousePan);
    CHECK(mouse.action(G::LeftDrag, 0, 2) == MouseSelect);
    CHECK(mouse.action(G::LeftDrag, 0, 4) == MouseFile);
    CHECK(mouse.action(G::WheelUp, 0, 1) == MouseZoomIn);
    CHECK(mouse.action(G::WheelUp, mouse_left, 0) == Next);
    CHECK(mouse.action(G::WheelDown, mouse_left, 0) == Previous);
    CHECK(mouse.action(G::WheelUp, mouse_right, 0) == ScrollLeft);
    mouse.assign(G::LeftClick, mouse_right, 0, Next);
    CHECK(mouse.action(G::LeftClick, mouse_right, 2) == Next);
    mouse.assign(G::LeftClick, mouse_right, 2, MouseNone);
    CHECK(mouse.action(G::LeftClick, mouse_right, 2) == MouseNone);
    mouse.assign(G::LeftClick, mouse_right, 2, MouseInherit);
    CHECK(mouse.action(G::LeftClick, mouse_right, 2) == Next);
    mouse.assign(G::X2Double, mouse_x1 | mouse_middle, 5, FlipH);
    CHECK(mouse.action(G::X2Double, mouse_x1 | mouse_middle, 5) == FlipH);
    CHECK(mouse.middle_preset() == 1 && mouse.wheel_preset() == 1);
    mouse.assign(G::MiddleClick, 0, 0, Checker);
    CHECK(mouse.middle_preset() == 3);
    mouse.basic_middle(0);
    CHECK(mouse.middle_preset() == 0 && mouse.action(G::MiddleClick, 0, 0) == MouseNone);
}
TEST(mouse_rejects_conflicts_and_system_window_move_disables_left_chords) {
    using G = MouseGesture;
    MouseSettings mouse;
    const auto original = mouse;
    auto rejected = [&](G g, unsigned held, unsigned mods, unsigned action) {
        bool threw{};
        try {
            mouse.assign(g, held, mods, action);
        } catch (const std::invalid_argument &) {
            threw = true;
        }
        CHECK(threw && mouse == original);
    };
    rejected(G::RightDrag, 0, 0, MousePan);
    rejected(G::RightDrag, 0, 0, MouseWindow);
    rejected(G::LeftClick, mouse_left, 0, Next);
    rejected(G::LeftClick, 0, 8, Next);
    rejected(G::LeftClick, 0, 0, MouseSelect);
    rejected(G::Hover, 0, 0, Next);
    mouse.assign(G::LeftClick, 0, 0, FlipH);
    mouse.assign(G::LeftDrag, 0, 0, MouseWindow);
    CHECK(mouse.action(G::LeftClick, 0, 0) == MouseNone);
    CHECK(mouse.action(G::WheelUp, mouse_left, 0) == MouseNone);
    CHECK(mouse.action(G::WheelUp, mouse_right, 0) == ScrollLeft);
    mouse.assign(G::LeftDrag, 0, 0, MousePan);
    CHECK(mouse.action(G::LeftClick, 0, 0) == FlipH);
}
TEST(mouse_options_bindings_roundtrip_corruption_and_legacy) {
    using G = MouseGesture;
    wchar_t temp[32768]{};
    GetTempPathW(32768, temp);
    const auto file =
        std::filesystem::path(temp) / (L"GVN-mouse-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings settings;
    settings.mouse.assign(G::LeftClick, mouse_right, 1, FlipH);
    settings.mouse.assign(G::RightClick, 0, 0, MouseNone);
    settings.mouse.assign(G::Hover, 0, 0, MouseHover);
    settings.mouse.assign(G::LeftDrag, 0, 0, MouseNone);
    settings.mouse.assign(G::RightDrag, 0, 0, MousePan);
    settings.mouse.wheel_scroll = 137;
    settings.mouse.drag_scroll = 75;
    settings.mouse.drag_zoom = 240;
    settings.mouse.drag_axis = 3;
    settings.mouse.drag_threshold = 9;
    settings.mouse.hover_margin = 25;
    settings.mouse.hover_min = 1;
    settings.mouse.ignore_activation = settings.mouse.slow_wheel = true;
    settings.save(file);
    Settings read;
    read.load(file);
    CHECK(read.mouse == settings.mouse);
    WritePrivateProfileStringW(L"MouseBindings", L"0", L"500 0 0 104", file.c_str());
    read.load(file);
    CHECK(read.mouse.bindings == MouseSettings{}.bindings);
    WritePrivateProfileStringW(L"MouseBindings", nullptr, nullptr, file.c_str());
    WritePrivateProfileStringW(L"Viewer", L"wheel", L"1", file.c_str());
    WritePrivateProfileStringW(L"Viewer", L"middle", L"2", file.c_str());
    read.load(file);
    CHECK(read.mouse.action(G::WheelUp, 0, 0) == MouseZoomIn);
    CHECK(read.mouse.action(G::MiddleClick, 0, 0) == Fullscreen);
    read.mouse.bindings.clear();
    read.save(file);
    Settings empty;
    empty.load(file);
    CHECK(empty.mouse.bindings.empty() && empty.mouse.action(G::LeftDrag, 0, 0) == MouseNone);
    std::filesystem::remove(file);
}
TEST(window_and_bars_roundtrip_negative_coordinates_and_corruption) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-window-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings s;
    s.window_center = true;
    s.window_toolbar_min = false;
    s.restore_maximized = s.restore_fullscreen = true;
    s.startup_position = 2;
    s.startup_state = 3;
    s.window_x = -1400;
    s.window_y = 80;
    s.window_width = 640;
    s.window_height = 480;
    s.last_x = -900;
    s.last_y = 40;
    s.last_width = 900;
    s.last_height = 600;
    s.last_state = 2;
    s.last_window_valid = true;
    s.caption = s.outer_frame = false;
    s.inner_frame = true;
    s.full_toolbar = false;
    s.full_status = false;
    s.full_menu = true;
    s.full_topmost = true;
    s.toolbar_large = s.toolbar_center = s.toolbar_line = s.toolbar_auto = s.status_auto = true;
    s.title_mode = 2;
    s.display_mode = 4;
    s.full_display_mode = 2;
    s.full_initial_fit = 3;
    s.save(path);
    Settings read;
    read.load(path);
    CHECK(read.window_center && !read.window_toolbar_min && read.restore_maximized &&
          read.restore_fullscreen);
    CHECK(read.window_x == -1400 && read.window_y == 80 && read.window_width == 640 &&
          read.window_height == 480);
    CHECK(read.last_x == -900 && read.last_width == 900 && read.last_state == 2 && read.last_window_valid);
    CHECK(read.startup_position == 2 && read.startup_state == 3 && !read.caption && !read.outer_frame &&
          read.inner_frame);
    CHECK(!read.full_toolbar && !read.full_status && read.full_menu && read.full_topmost);
    CHECK(read.toolbar_large && read.toolbar_center && read.toolbar_line && read.toolbar_auto &&
          read.status_auto);
    CHECK(read.title_mode == 2 && !read.title_path && read.display_mode == 4 && read.full_display_mode == 2 &&
          read.full_initial_fit == 3);
    WritePrivateProfileStringW(L"Viewer", L"window_width", L"-1", path.c_str());
    WritePrivateProfileStringW(L"Viewer", L"startup_state", L"999", path.c_str());
    read.load(path);
    CHECK(read.window_width == 340 && read.startup_state == 3);
    std::filesystem::remove(path);
}
TEST(movement_modes_preserve_legacy_stop_loop_and_roundtrip_independent_options) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-movement-options-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    Settings fresh;
    fresh.load(path);
    CHECK(fresh.end_action == 1 && !fresh.list_loop && !fresh.boundary_sound);
    CHECK(!fresh.folder_history && !fresh.previous_folder_first && !fresh.adjacent_previous_first &&
          !fresh.parent_first && fresh.comic_toolbar_reverse);
    for (int legacy : {0, 1}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"list_loop", legacy ? L"1" : L"0", path.c_str()));
        Settings old;
        old.load(path);
        CHECK(old.effective_end_action() == (legacy ? 2 : 0) && old.list_loop == (legacy != 0));
    }
    for (int mode = 0; mode < 4; ++mode) {
        Settings value;
        value.set_end_action(mode);
        value.folder_history = value.previous_folder_first = value.boundary_sound = mode % 2;
        value.adjacent_previous_first = value.parent_first = value.comic_toolbar_reverse = !(mode % 2);
        value.save(path);
        Settings read;
        read.load(path);
        CHECK(read.end_action == mode && read.effective_end_action() == mode &&
              read.list_loop == (mode == 2));
        CHECK(read.folder_history == value.folder_history &&
              read.previous_folder_first == value.previous_folder_first);
        CHECK(read.adjacent_previous_first == value.adjacent_previous_first &&
              read.parent_first == value.parent_first);
        CHECK(read.boundary_sound == value.boundary_sound &&
              read.comic_toolbar_reverse == value.comic_toolbar_reverse);
    }
    CHECK(WritePrivateProfileStringW(L"Viewer", L"list_loop", L"1", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer", L"end_action", L"3", path.c_str()));
    Settings preferred;
    preferred.load(path);
    CHECK(preferred.end_action == 3 && !preferred.list_loop);
    preferred.list_loop = true; // Legacy callers still request looping through this mirror.
    CHECK(preferred.effective_end_action() == 2);
    preferred.save(path);
    Settings canonical;
    canonical.load(path);
    CHECK(canonical.end_action == 2 && canonical.list_loop);
    std::filesystem::remove(path);
}
TEST(movement_hand_edited_modes_and_flags_are_bounded) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-movement-invalid-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings{}.save(path);
    for (const auto &[text, expected] :
         std::array<std::pair<const wchar_t *, int>, 3>{{{L"-8", 0}, {L"99", 3}, {L"broken", 1}}}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"end_action", text, path.c_str()));
        CHECK(WritePrivateProfileStringW(L"Viewer", L"folder_history", L"-1", path.c_str()));
        CHECK(WritePrivateProfileStringW(L"Viewer", L"boundary_sound", L"99", path.c_str()));
        Settings read;
        read.load(path);
        CHECK(read.end_action == expected && !read.list_loop && !read.folder_history && read.boundary_sound);
    }
    std::filesystem::remove(path);
}
TEST(jump_options_defaults_legacy_roundtrip_and_invalid_values) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-jump-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    Settings fresh;
    fresh.load(path);
    CHECK(fresh.comic_jump_reverse && fresh.jump_preview && fresh.jump_quality == 1);
    for (bool reversed : {false, true})
        for (bool preview : {false, true})
            for (int quality = 0; quality < 3; ++quality) {
                Settings value;
                value.comic_jump_reverse = reversed;
                value.jump_preview = preview;
                value.jump_quality = quality;
                value.save(path);
                Settings read;
                read.load(path);
                CHECK(read.comic_jump_reverse == reversed && read.jump_preview == preview &&
                      read.jump_quality == quality);
            }
    for (const auto name : {L"comic_jump_reverse", L"jump_preview", L"jump_quality"})
        CHECK(WritePrivateProfileStringW(L"Viewer", name, nullptr, path.c_str()));
    Settings legacy;
    legacy.load(path);
    CHECK(legacy.comic_jump_reverse && legacy.jump_preview && legacy.jump_quality == 1);
    for (const auto &[value, expected] :
         std::array<std::pair<const wchar_t *, int>, 3>{{{L"-1", 0}, {L"9", 2}, {L"broken", 1}}}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"jump_quality", value, path.c_str()));
        Settings read;
        read.load(path);
        CHECK(read.jump_quality == expected);
    }
    std::filesystem::remove(path);
}
TEST(comic_defaults_legacy_migration_all_methods_and_invalid_values) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-comic-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    Settings fresh;
    fresh.load(path);
    CHECK(fresh.comic_mode == 0 && fresh.comic_down == 10 && fresh.comic_up == 1 && fresh.comic_fill == 0);
    for (int mode = 0; mode < 5; ++mode)
        for (int method = 0; method <= 10; ++method) {
            Settings value;
            value.comic_mode = mode;
            value.comic_down = method;
            value.comic_up = 10 - method;
            value.comic_fill = 0x123456 + method;
            value.save(path);
            Settings read;
            read.load(path);
            CHECK(read.comic_options() == value.comic_options());
        }
    CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_mode", nullptr, path.c_str()));
    for (bool old : {false, true}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_height", old ? L"1" : L"0", path.c_str()));
        Settings read;
        read.load(path);
        CHECK(read.comic_mode == (old ? 3 : 4));
    }
    CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_mode", L"0", path.c_str()));
    Settings preferred;
    preferred.load(path);
    CHECK(preferred.comic_mode == 0);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_mode", L"99", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_down", L"-3", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_up", L"99", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_fill", L"-4", path.c_str()));
    Settings bounded;
    bounded.load(path);
    CHECK(bounded.comic_mode == 4 && bounded.comic_down == 0 && bounded.comic_up == 10 &&
          bounded.comic_fill == 0);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_mode", L"broken", path.c_str()));
    Settings invalid;
    invalid.load(path);
    CHECK(invalid.comic_mode == 0);
    std::filesystem::remove(path);
}
TEST(instance_options_defaults_roundtrip_and_missing_legacy_keys) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-instance-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings defaults;
    CHECK(defaults.allow_multiple && defaults.activate_reused && defaults.activate_dropped);
    for (unsigned flags = 0; flags < 8; ++flags) {
        Settings value;
        value.allow_multiple = flags & 1;
        value.activate_reused = flags & 2;
        value.activate_dropped = flags & 4;
        value.save(path);
        Settings read;
        read.load(path);
        CHECK(read.allow_multiple == value.allow_multiple && read.activate_reused == value.activate_reused &&
              read.activate_dropped == value.activate_dropped);
    }
    for (const auto key : {L"allow_multiple", L"activate_reused", L"activate_dropped"})
        CHECK(WritePrivateProfileStringW(L"Viewer", key, nullptr, path.c_str()));
    Settings legacy;
    legacy.load(path);
    CHECK(legacy.allow_multiple && legacy.activate_reused && legacy.activate_dropped);
    std::filesystem::remove(path);
}
TEST(reopen_last_defaults_roundtrip_and_internal_location_preservation) {
    Settings defaults;
    CHECK(!defaults.reopen_last && defaults.last_file.empty() && defaults.last_archive_entry.empty());
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-reopen-settings-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    for (bool enabled : {false, true}) {
        Settings source;
        source.reopen_last = enabled;
        source.last_file = L"G:\\画像 空白\\見開き.zip|42";
        source.last_archive_entry = L"本/日本語 page 02.png";
        source.remember_history = false;
        source.save(path);
        Settings read;
        read.load(path);
        CHECK(read.reopen_last == enabled && read.last_file == source.last_file &&
              read.last_archive_entry == source.last_archive_entry && !read.remember_history);
    }
    CHECK(WritePrivateProfileStringW(L"Viewer", L"reopen_last", nullptr, path.c_str()));
    Settings legacy;
    legacy.load(path);
    CHECK(!legacy.reopen_last && !legacy.last_file.empty());
    Settings dirty;
    dirty.last_file = L"C:\\bad\nname.png\r\n";
    dirty.last_archive_entry = L"entry\nname.png";
    dirty.save(path);
    Settings clean;
    clean.load(path);
    CHECK(clean.last_file.find_first_of(L"\r\n") == std::wstring::npos &&
          clean.last_archive_entry == dirty.last_archive_entry);
    for (const auto &name :
         {std::wstring(L" \t'日本語\\\r\n=\"😀.png"), std::wstring(8193, L'名') + L".png"}) {
        dirty.last_archive_entry = name;
        dirty.save(path);
        Settings decoded;
        decoded.load(path);
        CHECK(decoded.last_archive_entry == name);
    }
    CHECK(WritePrivateProfileStringW(L"Viewer", L"last_archive_entry_0", L"zzzz", path.c_str()));
    Settings malformed;
    malformed.load(path);
    CHECK(malformed.last_archive_entry.empty());
    dirty.last_archive_entry.clear();
    dirty.save(path);
    Settings cleared;
    cleared.load(path);
    CHECK(cleared.last_archive_entry.empty());
    std::filesystem::remove(path);
}
TEST(cut_background_defaults_roundtrip_legacy_and_invalid_values) {
    Settings defaults;
    CHECK(defaults.cut_transparent && defaults.cut_color == 0);
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-cut-background-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    for (bool transparent : {false, true})
        for (uint32_t color : {0u, 1u, 0x123456u, 0xabcdefu, 0xffffffu}) {
            Settings source;
            source.cut_transparent = transparent;
            source.cut_color = color;
            source.save(path);
            Settings read;
            read.load(path);
            CHECK(read.cut_transparent == transparent && read.cut_color == color);
        }
    for (const auto key : {L"cut_transparent", L"cut_color"})
        CHECK(WritePrivateProfileStringW(L"Viewer", key, nullptr, path.c_str()));
    Settings legacy;
    legacy.load(path);
    CHECK(legacy.cut_transparent && legacy.cut_color == 0);
    for (const auto &[text, expected] : {std::pair{L"999999999", 0xffffffu}, {L"-1", 0u}, {L"invalid", 0u}}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"cut_color", text, path.c_str()));
        Settings invalid;
        invalid.load(path);
        CHECK(invalid.cut_color == expected);
    }
    std::filesystem::remove(path);
}
TEST(ime_default_roundtrip_legacy_and_bounded_values) {
    Settings defaults;
    CHECK(defaults.disable_ime);
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-ime-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    for (bool disabled : {false, true}) {
        Settings source;
        source.disable_ime = disabled;
        source.save(path);
        Settings read;
        read.load(path);
        CHECK(read.disable_ime == disabled);
    }
    CHECK(WritePrivateProfileStringW(L"Viewer", L"disable_ime", nullptr, path.c_str()));
    Settings legacy;
    legacy.load(path);
    CHECK(legacy.disable_ime);
    for (const auto &[value, expected] : {std::pair{L"-1", false}, {L"999", true}, {L"invalid", true}}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"disable_ime", value, path.c_str()));
        Settings read;
        read.load(path);
        CHECK(read.disable_ime == expected);
    }
    std::filesystem::remove(path);
}
TEST(edit_memory_default_roundtrip_legacy_and_bounded_values) {
    CHECK(!Settings{}.allow_physical_overflow);
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-edit-memory-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    for (bool enabled : {false, true}) {
        Settings source;
        source.allow_physical_overflow = enabled;
        source.save(path);
        Settings read;
        read.load(path);
        CHECK(read.allow_physical_overflow == enabled);
    }
    CHECK(WritePrivateProfileStringW(L"Viewer", L"allow_physical_overflow", nullptr, path.c_str()));
    Settings legacy;
    legacy.load(path);
    CHECK(!legacy.allow_physical_overflow);
    for (const auto &[value, expected] : {std::pair{L"-1", false}, {L"999", true}, {L"invalid", false}}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"allow_physical_overflow", value, path.c_str()));
        Settings read;
        read.load(path);
        CHECK(read.allow_physical_overflow == expected);
    }
    std::filesystem::remove(path);
}

TEST(gif_style_defaults_roundtrip_all_choices_cache_flags_and_invalid_ini) {
    CHECK(Settings{}.gif_speed == 0 && LoadingOptions{}.gif_speed == 0);
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-gif-style-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::vector<uint32_t> flags;
    for (int style = 0; style < 5; ++style) {
        Settings source;
        source.gif_speed = style;
        source.save(path);
        Settings loaded;
        loaded.load(path);
        CHECK(loaded.gif_speed == style && loaded.loading_options().gif_speed == style);
        flags.push_back(loaded.loading_options().flags());
    }
    std::sort(flags.begin(), flags.end());
    CHECK(std::adjacent_find(flags.begin(), flags.end()) == flags.end());
    CHECK(WritePrivateProfileStringW(L"Viewer", L"gif_speed", nullptr, path.c_str()));
    Settings legacy;
    legacy.load(path);
    CHECK(legacy.gif_speed == 0);
    for (const auto &[value, expected] : {std::pair{L"-1", 0}, {L"999", 4}, {L"invalid", 0}}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"gif_speed", value, path.c_str()));
        Settings loaded;
        loaded.load(path);
        CHECK(loaded.gif_speed == expected);
    }
    std::filesystem::remove(path);
}

TEST(animation_file_counts_roundtrip_legacy_infinite_migration_and_invalid_ini) {
    CHECK(Settings{}.animation_file_loops);
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-repeat-policy-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    for (bool loop : {false, true})
        for (bool honor : {false, true}) {
            Settings value;
            value.animation_loop = loop;
            value.animation_file_loops = honor;
            value.save(path);
            Settings read;
            read.load(path);
            CHECK(read.animation_loop == loop && read.animation_file_loops == honor);
        }
    for (bool loop : {false, true}) {
        Settings value;
        value.animation_loop = loop;
        value.save(path);
        CHECK(WritePrivateProfileStringW(L"Viewer", L"animation_file_loops", nullptr, path.c_str()));
        Settings migrated;
        migrated.load(path);
        CHECK(migrated.animation_loop == loop && !migrated.animation_file_loops);
        migrated.save(path);
        Settings again;
        again.load(path);
        CHECK(!again.animation_file_loops);
    }
    CHECK(WritePrivateProfileStringW(L"Viewer", L"animation_loop", nullptr, path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer", L"animation_file_loops", nullptr, path.c_str()));
    Settings fresh;
    fresh.load(path);
    CHECK(fresh.animation_file_loops);
    for (const auto &[value, expected] : {std::pair{L"-1", false}, {L"999", true}, {L"invalid", true}}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"animation_file_loops", value, path.c_str()));
        Settings read;
        read.load(path);
        CHECK(read.animation_file_loops == expected);
    }
    std::filesystem::remove(path);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        std::erase_if(all_tests(), [&](const auto &test) { return std::string(test.name) != argv[1]; });
        if (all_tests().empty()) return 2;
    }
    return run_tests();
}

TEST(view_rotation_roundtrip_and_cache_flags_keep_all_gif_and_rotation_choices_distinct) {
    CHECK(Settings{}.view_rotation == 0 && !Settings{}.comic_rotate_before && !Settings{}.comic_single_wide);
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-view-rotation-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    for (int mode = 0; mode < 8; ++mode) {
        Settings value;
        value.view_rotation = mode;
        value.gif_speed = mode % 5;
        value.comic_rotate_before = (mode & 1) != 0;
        value.comic_single_wide = (mode & 2) != 0;
        value.save(path);
        Settings read;
        read.load(path);
        CHECK(read.view_rotation == mode && read.loading_options().view_rotation == mode);
        CHECK(read.gif_speed == value.gif_speed && read.comic_rotate_before == value.comic_rotate_before &&
              read.comic_single_wide == value.comic_single_wide);
        CHECK(read.comic_options().rotate_before == value.comic_rotate_before &&
              read.comic_options().single_wide == value.comic_single_wide);
    }
    std::vector<uint32_t> flags;
    for (int mode = 0; mode < 8; ++mode)
        for (int gif = 0; gif < 5; ++gif) {
            LoadingOptions options;
            options.view_rotation = mode;
            options.gif_speed = gif;
            flags.push_back(options.flags());
        }
    std::sort(flags.begin(), flags.end());
    CHECK(flags.size() == 40 && std::adjacent_find(flags.begin(), flags.end()) == flags.end());
    std::filesystem::remove(path);
}
TEST(view_rotation_legacy_wide_policy_explicit_off_and_invalid_ini_are_preserved) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-view-rotation-legacy-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    Settings fresh;
    fresh.load(path);
    CHECK(fresh.view_rotation == 0 && !fresh.comic_rotate_before && !fresh.comic_single_wide);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_mode", L"0", path.c_str()));
    Settings legacy;
    legacy.load(path);
    CHECK(legacy.comic_single_wide);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_mode", nullptr, path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_height", L"1", path.c_str()));
    Settings older;
    older.load(path);
    CHECK(older.comic_single_wide && older.comic_mode == 3);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_single_wide", L"0", path.c_str()));
    Settings explicit_off;
    explicit_off.load(path);
    CHECK(!explicit_off.comic_single_wide);
    explicit_off.save(path);
    Settings again;
    again.load(path);
    CHECK(!again.comic_single_wide);
    for (const auto &[value, expected] : {std::pair{L"-1", 0}, {L"999", 7}, {L"invalid", 0}}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"view_rotation", value, path.c_str()));
        Settings read;
        read.load(path);
        CHECK(read.view_rotation == expected);
    }
    for (const auto &[value, expected] : {std::pair{L"-1", false}, {L"999", true}, {L"invalid", false}}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_rotate_before", value, path.c_str()));
        CHECK(WritePrivateProfileStringW(L"Viewer", L"comic_single_wide", value, path.c_str()));
        Settings read;
        read.load(path);
        CHECK(read.comic_rotate_before == expected && read.comic_single_wide == expected);
    }
    std::filesystem::remove(path);
}

TEST(save_copy_policy_new_default_legacy_migration_and_explicit_choice) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-save-copy-legacy-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    Settings fresh;
    fresh.load(path);
    CHECK(!fresh.save_as_retarget);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"save_dialog", L"1", path.c_str()));
    Settings legacy;
    legacy.load(path);
    CHECK(legacy.save_as_retarget);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"save_as_retarget", L"0", path.c_str()));
    Settings explicit_copy;
    explicit_copy.load(path);
    CHECK(!explicit_copy.save_as_retarget);
    for (const auto &[input, expected] : {std::pair{L"-1", false}, {L"999", true}, {L"invalid", false}}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"save_as_retarget", input, path.c_str()));
        Settings value;
        value.load(path);
        CHECK(value.save_as_retarget == expected);
    }
    std::filesystem::remove(path);
}
TEST(save_copy_policy_roundtrip_keeps_explicit_off_in_an_existing_profile) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-save-copy-roundtrip-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    for (const bool policy : {false, true, false}) {
        Settings value;
        value.save_as_retarget = policy;
        value.comic_rotate_before = true;
        value.save(path);
        Settings loaded;
        loaded.load(path);
        CHECK(loaded.save_as_retarget == policy && loaded.comic_rotate_before);
        CHECK(GetPrivateProfileIntW(L"Viewer", L"save_as_retarget", 99, path.c_str()) ==
              static_cast<UINT>(policy));
    }
    std::filesystem::remove(path);
}

TEST(display_page_defaults_match_reference_and_preserve_explicit_filters) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-display-reference-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    // Actual MassiGra0.45 Display controls after Reset, not values copied from Settings{}.
    const auto reference_defaults = [](const Settings &value) {
        CHECK(value.display_down == 10 && value.display_up == 1);
        CHECK(value.center_image && value.gif_speed == 0 && value.fit_max == 5000 &&
              value.fit_simple == 0);
        CHECK(!value.comic_rotate_before && value.comic_mode == 0 && value.comic_down == 10 &&
              value.comic_up == 1);
    };
    reference_defaults(Settings{});
    Settings missing_file;
    missing_file.load(path);
    reference_defaults(missing_file);
    CHECK(WritePrivateProfileSectionW(L"Viewer", L"nearest=1\0\0", path.c_str()));
    Settings missing_filter;
    missing_filter.load(path);
    reference_defaults(missing_filter);
    CHECK(missing_filter.nearest);
    for (int version : {0, 1}) {
        CHECK(WritePrivateProfileStringW(L"Viewer", L"display_filter_version",
                                         std::to_wstring(version).c_str(), path.c_str()));
        CHECK(WritePrivateProfileStringW(L"Viewer", L"display_down", L"1", path.c_str()));
        Settings existing;
        existing.load(path);
        CHECK(existing.display_down == 1 && existing.nearest);
        existing.save(path);
        Settings roundtrip;
        roundtrip.load(path);
        CHECK(roundtrip.display_down == 1 && roundtrip.nearest);
    }
    std::filesystem::remove(path);
}

TEST(viewport_background_reference_defaults_and_all_presets_roundtrip) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-viewport-background-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    Settings missing;
    missing.load(path);
    CHECK(!std::filesystem::exists(path));
    CHECK(missing.background_tiled && missing.background_pattern == 2 &&
          missing.background_first == 0x202020 && missing.background_second == 0x303030);
    for (bool tiled : {false, true})
        for (int preset = 0; preset < 5; ++preset) {
            Settings value;
            value.background_tiled = tiled;
            value.background_pattern = preset;
            value.background_first = 0x172d43;
            value.background_second = 0x596571;
            value.background = 0x123456;
            value.checker = !tiled;
            value.checker_light = 0xef1234;
            value.checker_dark = 0x12ef34;
            value.checker_size = 13;
            value.save(path);
            Settings restored;
            restored.load(path);
            CHECK(restored.background_tiled == tiled && restored.background_pattern == preset &&
                  restored.background_first == value.background_first &&
                  restored.background_second == value.background_second);
            CHECK(restored.background == value.background && restored.checker == value.checker &&
                  restored.checker_light == value.checker_light && restored.checker_dark == value.checker_dark &&
                  restored.checker_size == 13);
        }
    std::filesystem::remove(path);
}

TEST(viewport_background_legacy_migration_is_read_only_and_explicit_palette_wins) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-viewport-background-legacy-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    CHECK(WritePrivateProfileSectionW(L"Viewer", L"background=1193046\0checker_size=13\0\0", path.c_str()));
    const auto bytes = [&] {
        std::ifstream stream(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), {});
    };
    const auto before = bytes();
    Settings migrated;
    migrated.load(path);
    CHECK(bytes() == before);
    CHECK(migrated.background == 0x123456 && migrated.background_first == 0x123456 &&
          migrated.background_second == 0x303030 && migrated.background_pattern == 2 &&
          migrated.background_tiled && migrated.checker_size == 13);
    migrated.save(path);
    Settings again;
    again.load(path);
    CHECK(again.background_first == 0x123456 && again.background == 0x123456);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"background_first", L"66051", path.c_str()));
    again.load(path);
    CHECK(again.background_first == 0x010203 && again.background == 0x123456);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"background_first", L"", path.c_str()));
    again.load(path);
    CHECK(again.background_first == 0x123456);
    std::filesystem::remove(path);
}

TEST(viewport_background_invalid_values_are_bounded_without_affecting_alpha_options) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-viewport-background-invalid-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    for (const auto &[input, preset, color, tiled] :
         {std::tuple{L"-17", 0, 0u, false}, std::tuple{L"999999999", 4, 0xffffffu, true},
          std::tuple{L"invalid", 2, 0x202020u, true}}) {
        CHECK(WritePrivateProfileSectionW(L"Viewer", L"checker_size=13\0checker=0\0\0", path.c_str()));
        for (const auto key : {L"background_pattern", L"background_first", L"background_second", L"background_tiled"})
            CHECK(WritePrivateProfileStringW(L"Viewer", key, input, path.c_str()));
        Settings value;
        value.load(path);
        CHECK(value.background_pattern == preset && value.background_tiled == tiled &&
              value.background_first == color &&
              value.background_second == (std::wstring_view(input) == L"invalid" ? 0x303030u : color));
        CHECK(!value.checker && value.checker_size == 13 && value.background == 0x202226);
    }
    std::filesystem::remove(path);
}

TEST(zoom_reference_defaults_and_high_step_range_preserve_explicit_legacy_choice) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-zoom-reference-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    std::filesystem::remove(path);
    Settings fresh;fresh.load(path);
    CHECK(fresh.zoom_mode==2 && !fresh.drag_simple && fresh.zoom_reduce && fresh.zoom_enlarge &&
          fresh.zoom_simple==0 && fresh.zoom_below==10 && fresh.zoom_above==25);
    CHECK(!std::filesystem::exists(path));
    for(const int mode:{0,1,2}) {
        Settings value;value.zoom_mode=mode;value.drag_simple=true;value.zoom_below=100;value.zoom_above=5000;
        value.save(path);Settings loaded;loaded.load(path);
        CHECK(loaded.zoom_mode==mode && loaded.drag_simple && loaded.zoom_below==100 && loaded.zoom_above==5000);
    }
    std::filesystem::remove(path);
}

TEST(glass_reference_upper_range_roundtrips_and_preserves_explicit_legacy_one) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-glass-range-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    Settings fresh; fresh.load(path); CHECK(fresh.glass_size == 8);
    CHECK(!std::filesystem::exists(path));
    for (const int size : {1,2,8,256,999}) {
        Settings value; value.glass_size = size; value.save(path);
        Settings loaded; loaded.load(path); CHECK(loaded.glass_size == size);
    }
    std::filesystem::remove(path);
}

TEST(mask_options_reference_defaults_all_flags_legacy_and_invalid_ini) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-mask-options-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    Settings fresh; fresh.load(path);
    CHECK(fresh.mask_options() == MaskOptions{} && !std::filesystem::exists(path));
    bool Settings::*const flags[]{&Settings::glass_remainder, &Settings::mask_negative_red,
        &Settings::mask_negative_green, &Settings::mask_negative_blue, &Settings::mask_xor_red,
        &Settings::mask_xor_green, &Settings::mask_xor_blue, &Settings::mask_q0_vertical,
        &Settings::mask_q0_horizontal, &Settings::mask_q0_negative, &Settings::mask_q0_xor,
        &Settings::mask_q0_flip_vertical, &Settings::mask_q0_flip_horizontal, &Settings::fl_negative};
    for (const auto member : flags) {
        Settings value; value.*member = !(value.*member); value.grid = 19; value.zoom_above = 4999;
        value.save(path); Settings loaded; loaded.load(path);
        for (const auto check : flags) CHECK(loaded.*check == value.*check);
        CHECK(loaded.mask_options() == value.mask_options() && loaded.grid == 19 && loaded.zoom_above == 4999);
    }
    for (unsigned bits = 0; bits < 64; ++bits) {
        Settings value; value.mask_q0_vertical = bits & 1; value.mask_q0_horizontal = bits & 2;
        value.mask_q0_negative = bits & 4; value.mask_q0_xor = bits & 8;
        value.mask_q0_flip_vertical = bits & 16; value.mask_q0_flip_horizontal = bits & 32;
        value.mask_swap = bits % 5; value.glass_size = bits == 0 ? 999 : bits == 1 ? 1 : 8;
        value.save(path); Settings loaded; loaded.load(path);
        CHECK(loaded.mask_options() == value.mask_options() && loaded.mask_options().q0_flags == bits);
    }
    std::filesystem::remove(path);
    WritePrivateProfileStringW(L"Viewer", L"glass_size", L"1", path.c_str());
    Settings legacy; legacy.load(path); CHECK(legacy.glass_size == 1 && legacy.mask_swap == 3 &&
                                               legacy.mask_options().q0_flags == 7);
    WritePrivateProfileStringW(L"Viewer", L"glass_size", L"2000", path.c_str());
    WritePrivateProfileStringW(L"Viewer", L"mask_swap", L"99", path.c_str());
    WritePrivateProfileStringW(L"Viewer", L"mask_negative_red", L"-99", path.c_str());
    WritePrivateProfileStringW(L"Viewer", L"mask_q0_xor", L"99", path.c_str());
    Settings bounded; bounded.load(path); CHECK(bounded.glass_size == 999 && bounded.mask_swap == 4 &&
                                                !bounded.mask_negative_red && bounded.mask_q0_xor);
    std::filesystem::remove(path);
}
TEST(rainbow_dialog_options_defaults_persistence_and_legacy_ini) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-rainbow-settings-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    std::filesystem::remove(path);Settings fresh;fresh.load(path);
    CHECK(fresh.rainbow_size==8&&fresh.rainbow_preview&&fresh.mask_options().rainbow_size==8&&!std::filesystem::exists(path));
    for(const int size:{1,2,4,8,16,32})for(const bool preview:{false,true}) {
        Settings value;value.rainbow_size=size;value.rainbow_preview=preview;value.glass_size=999;value.fl_negative=false;value.grid=13;
        value.save(path);Settings loaded;loaded.load(path);
        CHECK(loaded.rainbow_size==size&&loaded.rainbow_preview==preview&&loaded.mask_options()==value.mask_options()&&loaded.grid==13);
    }
    std::filesystem::remove(path);WritePrivateProfileStringW(L"Viewer",L"glass_size",L"1",path.c_str());
    Settings legacy;legacy.load(path);CHECK(legacy.glass_size==1&&legacy.rainbow_size==8&&legacy.rainbow_preview);
    WritePrivateProfileStringW(L"Viewer",L"rainbow_size",L"3",path.c_str());
    WritePrivateProfileStringW(L"Viewer",L"rainbow_preview",L"0",path.c_str());
    Settings invalid;invalid.load(path);CHECK(invalid.rainbow_size==8&&!invalid.rainbow_preview);
    std::filesystem::remove(path);
}
TEST(cp_dialog_options_filter_persistence_defaults_and_independent_preview) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-cp-settings-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    std::filesystem::remove(path);Settings fresh;fresh.load(path);CHECK(fresh.cp_code.empty()&&fresh.cp_preview&&!std::filesystem::exists(path));
    for(const auto code:{L"",L"ab",L"a b",L"ab1",L"あ",L"abcdefghijklmnopq"})for(const bool preview:{false,true}) {
        Settings value;value.cp_code=code;value.cp_preview=preview;value.rainbow_preview=!preview;value.rainbow_size=4;value.grid=17;
        value.save(path);Settings loaded;loaded.load(path);
        CHECK(loaded.cp_code==normalize_cp_code(code)&&loaded.cp_preview==preview&&loaded.rainbow_preview==!preview&&loaded.rainbow_size==4&&loaded.grid==17);
    }
    std::filesystem::remove(path);WritePrivateProfileStringW(L"Viewer",L"glass_size",L"1",path.c_str());
    Settings legacy;legacy.load(path);CHECK(legacy.glass_size==1&&legacy.cp_code.empty()&&legacy.cp_preview);
    WritePrivateProfileStringW(L"Viewer",L"cp_code",L"a b1",path.c_str());WritePrivateProfileStringW(L"Viewer",L"cp_preview",L"0",path.c_str());
    Settings filtered;filtered.load(path);CHECK(filtered.cp_code==L"AB"&&!filtered.cp_preview&&filtered.rainbow_preview);
    std::filesystem::remove(path);
}
TEST(slideshow_fullscreen_roundtrip_legacy_defaults_and_unrelated_values) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-slideshow-window-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    Settings fresh; fresh.load(path);
    CHECK(fresh.slide_fullscreen && !std::filesystem::exists(path));
    for (bool value : {false, true}) {
        Settings saved; saved.slide_fullscreen = value;
        saved.slide_random = !value; saved.interval = 600000;
        saved.startup_position = 2; saved.startup_state = 3;
        saved.restore_maximized = saved.restore_fullscreen = true;
        saved.save(path);
        Settings restored; restored.load(path);
        CHECK(restored.slide_fullscreen == value && restored.slide_random == !value &&
              restored.interval == 600000 && restored.startup_position == 2 && restored.startup_state == 3 &&
              restored.restore_maximized && restored.restore_fullscreen);
    }
    CHECK(WritePrivateProfileStringW(L"Viewer", L"slide_fullscreen", nullptr, path.c_str()));
    Settings legacy; legacy.load(path);
    CHECK(legacy.slide_fullscreen && legacy.interval == 600000 && legacy.startup_state == 3);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"slide_fullscreen", L"0", path.c_str()));
    Settings explicit_off; explicit_off.load(path); CHECK(!explicit_off.slide_fullscreen);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"slide_fullscreen", L"1", path.c_str()));
    Settings explicit_on; explicit_on.load(path); CHECK(explicit_on.slide_fullscreen);
    std::filesystem::remove(path);
}
TEST(window_startup_reference_defaults_preserve_explicit_old_profile_values) {
    // Official fresh controls and saved native profile: prior position, 0/0/480/360,
    // normal state, restore-max ON, restore-full OFF, slide interval 5000 ms.
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-startup-defaults-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    std::filesystem::remove(path);
    Settings fresh; fresh.load(path);
    CHECK(!std::filesystem::exists(path) && fresh.startup_position == 1 && fresh.startup_state == 0 &&
          fresh.restore_maximized && !fresh.restore_fullscreen && fresh.window_x == 0 && fresh.window_y == 0 &&
          fresh.window_width == 480 && fresh.window_height == 360 && fresh.last_width == 480 &&
          fresh.last_height == 360 && fresh.interval == 5000);
    for (int position : {0, 1, 2}) {
        Settings old; old.startup_position = position; old.startup_state = 1;
        old.window_x = old.window_y = 100; old.window_width = 1080; old.window_height = 760;
        old.restore_maximized = old.restore_fullscreen = false; old.interval = 3000;
        old.last_window_valid = true; old.last_x = -900; old.last_y = 40;
        old.last_width = 620; old.last_height = 420; old.last_state = 1; old.save(path);
        Settings restored; restored.load(path);
        CHECK(restored.startup_position == position && restored.startup_state == 1 &&
              restored.window_x == 100 && restored.window_y == 100 && restored.window_width == 1080 &&
              restored.window_height == 760 && !restored.restore_maximized && !restored.restore_fullscreen &&
              restored.interval == 3000 && restored.last_window_valid && restored.last_x == -900 &&
              restored.last_y == 40 && restored.last_width == 620 && restored.last_height == 420 && restored.last_state == 1);
    }
    for (const auto key : {L"startup_position", L"restore_maximized", L"interval"})
        CHECK(WritePrivateProfileStringW(L"Viewer", key, nullptr, path.c_str()));
    Settings sparse; sparse.load(path);
    CHECK(sparse.startup_position == 1 && sparse.restore_maximized && sparse.interval == 5000 &&
          sparse.window_width == 1080 && sparse.window_height == 760 && sparse.last_x == -900);
    std::filesystem::remove(path);
}
TEST(slideshow_policy_flags_roundtrip_all_combinations_and_do_not_change_navigation_options) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-slide-policy-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    Settings fresh; CHECK(fresh.slide_loop && !fresh.slide_next_folder && !fresh.slide_subtree);
    for(int bits=0;bits<8;++bits) {
        Settings value; value.slide_loop=(bits&1)!=0; value.slide_next_folder=(bits&2)!=0;
        value.slide_subtree=(bits&4)!=0; value.set_end_action(bits%4); value.sort=bits%5; value.save(path);
        Settings restored; restored.load(path);
        CHECK(restored.slide_loop==value.slide_loop && restored.slide_next_folder==value.slide_next_folder &&
              restored.slide_subtree==value.slide_subtree && restored.end_action==value.end_action &&
              restored.list_loop==value.list_loop && restored.sort==value.sort);
    }
    for(const auto key:{L"slide_loop",L"slide_next_folder",L"slide_subtree"})
        CHECK(WritePrivateProfileStringW(L"Viewer",key,nullptr,path.c_str()));
    Settings old; old.load(path); CHECK(old.slide_loop && !old.slide_next_folder && !old.slide_subtree);
    std::filesystem::remove(path);
}
TEST(custom_window_valid_integer_ranges_roundtrip_without_changing_legacy_corruption_recovery) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-custom-window-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    for(const auto size:{SIZE{1,1},SIZE{100,80},SIZE{32768,65535},SIZE{INT_MAX,480}}) {
        Settings value; value.window_x=INT_MAX; value.window_y=-INT_MAX;
        value.window_width=size.cx; value.window_height=size.cy; value.startup_position=2; value.save(path);
        Settings read; read.load(path);
        CHECK(read.window_x==INT_MAX && read.window_y==-INT_MAX && read.window_width==size.cx &&
              read.window_height==size.cy && read.startup_position==2);
    }
    CHECK(WritePrivateProfileStringW(L"Viewer",L"window_width",L"-1",path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer",L"window_height",L"-1",path.c_str()));
    Settings damaged; damaged.load(path); CHECK(damaged.window_width==340 && damaged.window_height==240);
    std::filesystem::remove(path);
}
TEST(caption_native_font_charset_and_large_sizes_roundtrip) {
    const auto path = std::filesystem::temp_directory_path() /
                      (L"GVN-native-font-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Settings value;
    for (const auto charset : {int(DEFAULT_CHARSET), int(SHIFTJIS_CHARSET), int(OEM_CHARSET)}) {
        value.overlay_charset = charset;
        value.overlay_family = charset == OEM_CHARSET ? L"Terminal" : L"@MS Gothic";
        value.overlay_points = 327680;
        value.save(path);
        Settings read; read.load(path);
        CHECK(read.overlay_family == value.overlay_family && read.overlay_charset == charset &&
              read.overlay_points == 327680);
    }
    CHECK(WritePrivateProfileStringW(L"Viewer", L"overlay_charset", L"999", path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer", L"overlay_points", L"2147483647", path.c_str()));
    Settings read; read.load(path);
    CHECK(read.overlay_charset == 255 && read.overlay_points == INT_MAX);
    CHECK(WritePrivateProfileStringW(L"Viewer", L"overlay_charset", nullptr, path.c_str()));
    Settings legacy; legacy.load(path); CHECK(legacy.overlay_charset == DEFAULT_CHARSET);
    std::filesystem::remove(path);
}
TEST(space_hand_and_left_wheel_defaults_migrate_once_and_keep_customization) {
    using G=MouseGesture;const auto path=std::filesystem::temp_directory_path()/(L"GVN-hand-defaults-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    Settings value;CHECK(value.command_for(VK_SPACE,0)==ToolPan);CHECK(value.mouse.action(G::WheelUp,mouse_left,0)==Next&&value.mouse.action(G::WheelDown,mouse_left,0)==Previous);
    value.set_keys(ToolPan,{});value.set_keys(Play,{L"Space"});value.mouse.assign(G::WheelUp,mouse_left,0,ScrollUp);value.mouse.assign(G::WheelDown,mouse_left,0,ScrollDown);value.save(path);
    WritePrivateProfileStringW(L"KeyBindings",L"DefaultsVersion",nullptr,path.c_str());WritePrivateProfileStringW(L"MouseBindings",L"DefaultsVersion",L"2",path.c_str());
    Settings migrated;migrated.load(path);CHECK(migrated.command_for(VK_SPACE,0)==ToolPan&&migrated.keys_for(Play).empty());CHECK(migrated.mouse.action(G::WheelUp,mouse_left,0)==Next&&migrated.mouse.action(G::WheelDown,mouse_left,0)==Previous);
    migrated.set_keys(ToolPan,{});migrated.set_keys(Play,{L"Space"});migrated.mouse.assign(G::WheelUp,mouse_left,0,ScrollUp);migrated.mouse.assign(G::WheelDown,mouse_left,0,ScrollDown);migrated.save(path);
    Settings custom;custom.load(path);CHECK(custom.command_for(VK_SPACE,0)==Play&&custom.mouse.action(G::WheelUp,mouse_left,0)==ScrollUp);
    custom.set_keys(Play,{});custom.set_keys(Next,{L"Space"});custom.mouse.assign(G::WheelUp,mouse_left,0,Checker);custom.save(path);
    WritePrivateProfileStringW(L"KeyBindings",L"DefaultsVersion",nullptr,path.c_str());WritePrivateProfileStringW(L"MouseBindings",L"DefaultsVersion",L"2",path.c_str());
    Settings older_custom;older_custom.load(path);CHECK(older_custom.command_for(VK_SPACE,0)==Next&&older_custom.keys_for(ToolPan).empty());CHECK(older_custom.mouse.action(G::WheelUp,mouse_left,0)==Checker&&older_custom.mouse.action(G::WheelDown,mouse_left,0)==ScrollDown);
    older_custom.set_keys(Next,{});older_custom.save(path);WritePrivateProfileStringW(L"KeyBindings",L"DefaultsVersion",nullptr,path.c_str());Settings disabled;disabled.load(path);CHECK(disabled.command_for(VK_SPACE,0)==0);
    std::filesystem::remove(path);
}
TEST(redo_default_migration_preserves_existing_custom_shortcuts){
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-redo-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    Settings value;CHECK(value.command_for('Z',3)==Redo&&value.command_for('Y',1)==Crop);
    value.save(path);WritePrivateProfileStringW(L"KeyBindings",std::to_wstring(Redo).c_str(),nullptr,path.c_str());
    Settings migrated;migrated.load(path);CHECK(migrated.command_for('Z',3)==Redo);
    value.set_keys(Redo,{});value.set_keys(Next,{L"Ctrl+Shift+Z"});value.set_keys(Open,{L"Alt+O"});value.save(path);
    WritePrivateProfileStringW(L"KeyBindings",std::to_wstring(Redo).c_str(),nullptr,path.c_str());
    Settings custom;custom.load(path);CHECK(custom.command_for('Z',3)==Next&&custom.command_for('O',4)==Open&&custom.keys_for(Redo).empty());
    custom.set_keys(Next,{});custom.set_keys(Redo,{});custom.save(path);Settings disabled;disabled.load(path);CHECK(disabled.keys_for(Redo).empty());std::filesystem::remove(path);
}

TEST(modern_ui_preferences_persist_and_clamp_without_changing_bindings){
    const auto path=std::filesystem::temp_directory_path()/L"gvn-modern-settings-test.ini";
    Settings before;before.ui_theme=2;before.ui_density=1;before.toolbar_overflow=false;const auto keys=before.keys_for(Copy);before.save(path);
    WritePrivateProfileSectionW(L"Viewer",L"allow_multiple=1\0\0",path.c_str());
    Settings after;after.load(path);CHECK(after.ui_theme==2&&after.ui_density==1&&!after.toolbar_overflow&&after.keys_for(Copy)==keys);WritePrivateProfileStringW(L"Interface",L"ui_theme",L"99",path.c_str());after.load(path);CHECK(after.ui_theme==2);std::filesystem::remove(path);
}

TEST(command_feedback_settings_defaults_persist_and_clamp){
    Settings value;CHECK(value.toast_enabled&&value.command_sound&&!value.toast_view_commands&&value.toast_duration==2000);
    const auto path=std::filesystem::temp_directory_path()/L"gvn-feedback-settings-test.ini";
    value.toast_enabled=false;value.command_sound=false;value.toast_view_commands=true;value.toast_duration=750;value.save(path);
    WritePrivateProfileSectionW(L"Interface",L"ui_theme=1\0\0",path.c_str());
    Settings next;next.load(path);CHECK(!next.toast_enabled&&!next.command_sound&&next.toast_view_commands&&next.toast_duration==750);
    WritePrivateProfileStringW(L"Feedback",L"toast_duration",L"1",path.c_str());next.load(path);CHECK(next.toast_duration==500);
    WritePrivateProfileStringW(L"Feedback",L"toast_duration",L"999999",path.c_str());next.load(path);CHECK(next.toast_duration==10000);
    std::filesystem::remove(path);
}
