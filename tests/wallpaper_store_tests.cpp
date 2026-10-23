#include "gvn/wallpaper.h"
#include "gvn/codecs.h"
#include "../src/wallpaper_platform.h"
#include "test.h"
#include <atomic>
#include <cstring>
#include <fstream>
#include <future>
#include <thread>
using namespace gvn;
namespace {
std::wstring identity() {
    GUID guid{};CHECK(SUCCEEDED(CoCreateGuid(&guid)));wchar_t text[40]{};CHECK(StringFromGUID2(guid,text,40));return text;
}
std::vector<uint8_t> bytes(const std::filesystem::path &path) {
    std::ifstream file(path,std::ios::binary);CHECK(file.good());return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path &path,const std::string &text) {
    std::ofstream file(path,std::ios::binary);CHECK(file.good());file<<text;CHECK(file.good());
}
struct Directory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/(L"GVN-wallpaper-store-"+identity());
    Directory() {CHECK(std::filesystem::create_directory(path));}
    ~Directory() {std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
struct Context {
    Directory temporary;
    std::filesystem::path config=temporary.path/L"設定 & $() α.ini",source=temporary.path/L"元画像 Alpha.png";
    WallpaperSnapshot state{wallpaper_values(WallpaperMode::Height),source};
    std::vector<std::string> events;
    std::vector<std::filesystem::path> applied;
    WallpaperPlatform platform;
    std::function<void(const std::filesystem::path &)> on_apply;
    bool fail_write{},fail_apply{},fail_restore{};
    Context() {
        std::filesystem::copy_file(std::filesystem::path(GVN_FIXTURES)/L"wallpaper-reference/alpha/source.png",source);
        write(config,"Original settings remain unchanged.");
        platform.mutex_name=L"Local\\GVN.Wallpaper.Tests."+identity();
        platform.read=[&] {events.push_back("read");return state;};
        platform.write=[&](const WallpaperValues &values) {
            events.push_back("write");state.values.tile=values.tile;
            if (std::exchange(fail_write,false)) throw std::runtime_error("Injected style write failure");
            state.values=values;
        };
        platform.apply=[&](const std::filesystem::path &path) {
            events.push_back("apply");applied.push_back(path);
            if (!path.empty()) CHECK(std::filesystem::is_regular_file(path));
            state.image=path;
            if (on_apply) on_apply(path);
            if (std::exchange(fail_apply,false) || fail_restore) throw std::runtime_error("Injected wallpaper apply failure");
        };
    }
    Image image() const {return load_image(source);}
    std::vector<std::filesystem::path> files() const {return managed_wallpaper_files(config);}
    std::vector<std::filesystem::path> all_files() const {
        std::vector<std::filesystem::path> out;const auto directory=wallpaper_directory(config);
        if (std::filesystem::exists(directory)) for (const auto &entry:std::filesystem::directory_iterator(directory)) out.push_back(entry.path());
        std::sort(out.begin(),out.end());return out;
    }
    WallpaperResult set() {return set_wallpaper_image(image(),0,config,WallpaperMode::Width,platform);}
    void two_files() {
        const auto first=set();auto input=load_image(first.file);
        const auto second=set_wallpaper_image(input,0,config,WallpaperMode::Height,platform);
        CHECK(first.file!=second.file && files().size()==2 && second.retained==std::vector<std::filesystem::path>{first.file});
    }
};
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~Handle() {if (value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
};
struct HeldMutex {
    HANDLE value{};
    ~HeldMutex() {if (value) {ReleaseMutex(value);CloseHandle(value);}}
};
struct PrivateKey {
    std::wstring name=L"GVN-WallpaperTest-"+identity();HKEY key{};
    PrivateKey() {
        CHECK(RegCreateKeyExW(HKEY_CURRENT_USER,name.c_str(),0,nullptr,REG_OPTION_VOLATILE,KEY_ALL_ACCESS,nullptr,&key,nullptr)==ERROR_SUCCESS);
        HKEY desktop{};CHECK(RegCreateKeyExW(key,L"Desktop",0,nullptr,REG_OPTION_VOLATILE,KEY_ALL_ACCESS,nullptr,&desktop,nullptr)==ERROR_SUCCESS);RegCloseKey(desktop);
    }
    ~PrivateKey() {if (key) RegCloseKey(key);RegDeleteTreeW(HKEY_CURRENT_USER,name.c_str());RegDeleteKeyW(HKEY_CURRENT_USER,name.c_str());}
};
}
TEST(wallpaper_set_five_modes_uses_private_generated_files_and_keeps_input_settings) {
    Context c;const auto original=bytes(c.source),settings=bytes(c.config);const auto image=c.image();
    const auto expected=load_image(std::filesystem::path(GVN_FIXTURES)/L"wallpaper-reference/alpha/expected.bmp");
    for (int mode=0;mode<5;++mode) {
        c.events.clear();const auto result=set_wallpaper_image(image,0,c.config,static_cast<WallpaperMode>(mode),c.platform);
        CHECK(result.file.parent_path()==wallpaper_directory(c.config) && result.retained.empty());
        CHECK(c.files()==std::vector<std::filesystem::path>{result.file} && c.state.image==result.file);
        CHECK(c.state.values==wallpaper_values(static_cast<WallpaperMode>(mode)));
        CHECK(load_image(result.file).frames[0].pixels==expected.frames[0].pixels);
        CHECK(c.events==std::vector<std::string>{"read","write","apply"});
        CHECK(bytes(c.source)==original && bytes(c.config)==settings);
    }
}
TEST(wallpaper_setting_from_generated_source_never_overwrites_or_removes_that_input) {
    Context c;const auto first=c.set();const auto original=bytes(first.file);auto input=load_image(first.file);
    input.frames[0].pixels[0]={1,2,3,255};const auto result=set_wallpaper_image(input,0,c.config,WallpaperMode::Tile,c.platform);
    CHECK(result.file!=first.file && bytes(first.file)==original && c.files().size()==2);
    CHECK(result.retained==std::vector<std::filesystem::path>{first.file});
    CHECK(!result.cleanup_incomplete);
    CHECK(load_image(result.file).frames[0].pixels[0]==Pixel{1,2,3,255});
}
TEST(wallpaper_storage_collision_and_invalid_config_do_not_call_system_or_change_files) {
    Context c;write(c.temporary.path/L"wallpaper","A user file is not a storage directory.");const auto original=bytes(c.temporary.path/L"wallpaper");
    THROWS(c.set());CHECK(c.events.empty() && bytes(c.temporary.path/L"wallpaper")==original);
    THROWS(wallpaper_directory({}));THROWS(wallpaper_directory(c.temporary.path));
    THROWS(wallpaper_directory(std::filesystem::path(std::wstring(L"a\0b",3))));
    CHECK(!has_wallpaper_files(c.temporary.path));
}
TEST(wallpaper_write_and_apply_failures_restore_exact_snapshot_and_previous_bitmap) {
    for (const bool write_failure:{false,true}) {
        Context c;const auto old=c.set();const auto before=c.state;const auto original=bytes(old.file);
        c.fail_write=write_failure;c.fail_apply=!write_failure;
        THROWS(c.set());CHECK(c.state==before && c.files()==std::vector<std::filesystem::path>{old.file});
        CHECK(bytes(old.file)==original && c.all_files()==c.files());
    }
}
TEST(wallpaper_failed_rollback_keeps_recovery_bitmap_and_previous_source) {
    Context c;const auto old=c.set();const auto original=bytes(old.file);c.fail_restore=true;
    bool caught=false;
    try {c.set();} catch (const WallpaperRecoveryError &error) {
        caught=true;CHECK(error.recovery_files.size()==1 && std::filesystem::is_regular_file(error.recovery_files[0]));
        CHECK(error.recovery_files[0]!=old.file);
    }
    CHECK(caught && bytes(old.file)==original && c.files().size()==2);
}
TEST(wallpaper_old_locked_file_is_retained_after_successful_new_application) {
    Context c;const auto old=c.set();
    Handle lock{CreateFileW(old.file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    CHECK(lock.value!=INVALID_HANDLE_VALUE);const auto result=c.set();
    CHECK(c.state.image==result.file && result.file!=old.file && std::filesystem::exists(old.file));
    CHECK(result.retained==std::vector<std::filesystem::path>{old.file});
    CHECK(result.cleanup_incomplete);
}
TEST(wallpaper_empty_delete_and_unrecognized_files_never_clear_desktop) {
    Context c;std::filesystem::create_directory(wallpaper_directory(c.config));
    const auto other=wallpaper_directory(c.config)/L"ordinary.bmp";write(other,"Unrelated data");
    write(wallpaper_directory(c.config)/L"GVN-not-a-guid.bmp","Unrelated data");
    CHECK(!has_wallpaper_files(c.config));const auto result=delete_wallpaper_images(c.config,c.platform);
    CHECK(!result.removed && result.retained.empty() && c.events.empty() && bytes(other)==std::vector<uint8_t>({'U','n','r','e','l','a','t','e','d',' ','d','a','t','a'}));
}
TEST(wallpaper_delete_clears_then_removes_only_generated_images_and_backups) {
    Context c;c.two_files();const auto source=bytes(c.source),settings=bytes(c.config);
    const auto other=wallpaper_directory(c.config)/L"keep.png";write(other,"Unrelated data");
    c.events.clear();const auto result=delete_wallpaper_images(c.config,c.platform);
    CHECK(result.removed==2 && result.retained.empty() && !has_wallpaper_files(c.config));
    CHECK(c.state.image.empty() && c.state.values==wallpaper_values(WallpaperMode::Center));
    CHECK(c.events==std::vector<std::string>{"read","write","apply"});
    CHECK(c.all_files()==std::vector<std::filesystem::path>{other});
    CHECK(bytes(c.source)==source && bytes(c.config)==settings);
}
TEST(wallpaper_delete_apply_failure_restores_all_generated_bytes_and_system_state) {
    Context c;c.two_files();const auto before=c.state;const auto files=c.files();
    const auto first=bytes(files[0]),second=bytes(files[1]);c.fail_apply=true;
    THROWS(delete_wallpaper_images(c.config,c.platform));
    CHECK(c.state==before && c.files()==files && c.all_files()==files);
    CHECK(bytes(files[0])==first && bytes(files[1])==second);
}
TEST(wallpaper_partial_delete_failure_restores_already_deleted_bitmap_before_system) {
    Context c;c.two_files();const auto before=c.state;const auto files=c.files();
    const auto first=bytes(files[0]),second=bytes(files[1]);
    Handle lock{CreateFileW(files.back().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    CHECK(lock.value!=INVALID_HANDLE_VALUE);
    c.events.clear();
    THROWS(delete_wallpaper_images(c.config,c.platform));
    CHECK(c.state==before && c.files()==files && bytes(files[0])==first && bytes(files[1])==second);
    CHECK(c.events==std::vector<std::string>{"read","write","apply","write","apply"});
}
TEST(wallpaper_readonly_delete_is_rejected_before_any_system_mutation) {
    Context c;const auto file=c.set().file;const auto before=c.state;c.events.clear();
    CHECK(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_READONLY));
    THROWS(delete_wallpaper_images(c.config,c.platform));
    CHECK(c.events.empty() && c.state==before && std::filesystem::exists(file));
    CHECK(SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL));
}
TEST(wallpaper_busy_operation_times_out_before_reading_or_mutating_system) {
    Context c;const auto image=c.image();HeldMutex mutex{CreateMutexW(nullptr,TRUE,c.platform.mutex_name.c_str())};CHECK(mutex.value);
    c.platform.wait_ms=25;
    auto task=std::async(std::launch::async,[&] {
        bool busy=false;
        try {set_wallpaper_image(image,0,c.config,WallpaperMode::Width,c.platform);}
        catch (const std::runtime_error &error) {busy=std::string(error.what()).find("Another wallpaper operation")!=std::string::npos;}
        CHECK(busy);
    });task.get();
    CHECK(c.events.empty() && c.files().empty());
}
TEST(wallpaper_native_registry_bridge_preserves_raw_types_missing_values_and_spi_arguments) {
    PrivateKey key;std::filesystem::path current=L"C:\\original wallpaper.bmp";size_t gets{},sets{};
    const auto system=[&](UINT action,UINT parameter,PVOID data,UINT flags)->BOOL {
        CHECK(data);
        if (action==SPI_GETDESKWALLPAPER) {CHECK(parameter==32768 && !flags);wcscpy_s(static_cast<LPWSTR>(data),parameter,current.c_str());++gets;return TRUE;}
        CHECK(action==SPI_SETDESKWALLPAPER && !parameter && flags==(SPIF_UPDATEINIFILE|SPIF_SENDCHANGE));
        current=static_cast<LPCWSTR>(data);++sets;return TRUE;
    };
    auto platform=detail::wallpaper_platform_for_registry(key.key,L"Desktop",system,L"Local\\GVN.Wallpaper.NativeTests."+identity());
    const auto missing=platform.read();CHECK(!missing.values.tile && !missing.values.style && missing.image==current);
    WallpaperValues raw{WallpaperValue{REG_BINARY,{0,3,255}},WallpaperValue{REG_DWORD,{6,0,0,0}}};
    platform.write(raw);CHECK(platform.read().values==raw);
    for (int mode=0;mode<5;++mode) {const auto value=wallpaper_values(static_cast<WallpaperMode>(mode));platform.write(value);CHECK(platform.read().values==value);}
    platform.apply(L"C:\\日本語 & $()\\wallpaper.bmp");CHECK(current==L"C:\\日本語 & $()\\wallpaper.bmp");
    platform.apply({});CHECK(current.empty() && sets==2 && gets==7);
    platform.write(missing.values);CHECK(!platform.read().values.tile && !platform.read().values.style);
}
TEST(wallpaper_native_bridge_rejects_oversized_values_and_invalid_paths_without_spi_set) {
    PrivateKey key;size_t calls{};
    auto platform=detail::wallpaper_platform_for_registry(key.key,L"Desktop",[&](UINT,UINT,PVOID,UINT)->BOOL {++calls;return TRUE;},L"Local\\GVN.Wallpaper.NativeTests."+identity());
    THROWS(platform.write({WallpaperValue{REG_BINARY,std::vector<uint8_t>(65537)}, {}}));
    THROWS(platform.apply(std::filesystem::path(std::wstring(L"a\0b",3))));CHECK(!calls);
    HKEY desktop{};CHECK(RegOpenKeyExW(key.key,L"Desktop",0,KEY_SET_VALUE,&desktop)==ERROR_SUCCESS);
    std::vector<uint8_t> large(65537);CHECK(RegSetValueExW(desktop,L"WallpaperStyle",0,REG_BINARY,large.data(),static_cast<DWORD>(large.size()))==ERROR_SUCCESS);RegCloseKey(desktop);
    THROWS(platform.read());CHECK(!calls);
}
TEST(wallpaper_native_bridge_propagates_spi_failures_without_real_desktop_calls) {
    PrivateKey key;size_t calls{};
    auto platform=detail::wallpaper_platform_for_registry(key.key,L"Desktop",[&](UINT action,UINT,PVOID,UINT)->BOOL {
        CHECK(action==SPI_GETDESKWALLPAPER || action==SPI_SETDESKWALLPAPER);
        ++calls;SetLastError(ERROR_ACCESS_DENIED);return FALSE;
    },L"Local\\GVN.Wallpaper.NativeTests."+identity());
    THROWS(platform.read());THROWS(platform.apply({}));CHECK(calls==2);
}
int main() {
    const auto status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if (FAILED(status)) return 2;
    const auto result=run_tests();CoUninitialize();return result;
}
