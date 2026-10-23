#include "gvn/new_window.h"
#include "test.h"
#include "test_desktop.h"
#include <cstring>
#include <cmath>
#include <limits>
#include <fstream>
#include <utility>
using namespace gvn;
namespace {
struct Handle {
    HANDLE value{};
    ~Handle() { if (value) CloseHandle(value); }
};
struct Directory {
    std::filesystem::path path;
    Directory() {
        static unsigned sequence{};
        path = std::filesystem::temp_directory_path() /
               (L"GVN-new-window-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++sequence));
        CHECK(std::filesystem::create_directory(path));
    }
    ~Directory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
struct Environment {
    std::wstring name, original;
    Environment(std::wstring key, const std::wstring &value) : name(std::move(key)) {
        original.resize(32768);
        original.resize(GetEnvironmentVariableW(name.c_str(), original.data(), 32768));
        CHECK(SetEnvironmentVariableW(name.c_str(), value.c_str()));
    }
    ~Environment() { SetEnvironmentVariableW(name.c_str(), original.empty() ? nullptr : original.c_str()); }
};
bool marker_is_present(HANDLE candidate, const std::wstring &name) {
    Handle expected{OpenEventW(SYNCHRONIZE, FALSE, name.c_str())};
    CHECK(expected.value);
    // An open handle cannot be allocated twice. If OpenEvent just allocated the
    // candidate's numeric slot, that slot was previously absent, not inherited.
    if (candidate == expected.value) return false;
    // This SDK installation does not ship Kernelbase.lib; resolve this Windows 10 API
    // from the already loaded system module instead of adding a test-only SDK dependency.
    const auto module = GetModuleHandleW(L"kernelbase.dll");
    CHECK(module);
    using CompareObjects = BOOL(WINAPI *)(HANDLE, HANDLE);
    const auto compare = reinterpret_cast<CompareObjects>(GetProcAddress(module, "CompareObjectHandles"));
    CHECK(compare);
    return compare(candidate, expected.value) != FALSE;
}
Image alpha() {
    Image result;
    result.width = 3;
    result.height = 2;
    result.frames.resize(1);
    result.frames[0].pixels = {{7, 11, 13, 0}, {31, 59, 61, 1}, {67, 71, 73, 127},
                               {79, 83, 89, 128}, {97, 101, 103, 254}, {107, 109, 113, 255}};
    return result;
}
void word(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, 4);
}
std::vector<uint8_t> read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
struct OwnedChild {
    ViewerProcess process;
    HANDLE release;
    OwnedChild(ViewerProcess value, HANDLE event) : process(std::move(value)), release(event) {}
    ~OwnedChild() {
        SetEvent(release);
        if (WaitForSingleObject(process.handle(), 3000) == WAIT_TIMEOUT) {
            TerminateProcess(process.handle(), 91); // Only this test's newly returned child handle.
            WaitForSingleObject(process.handle(), 3000);
        }
    }
};
void real_child(NewWindowContent content) {
    Directory directory;
    const auto config = directory.path / L"設定 folder & $() space" / L"settings 日本語.ini";
    std::filesystem::create_directory(config.parent_path());
    const std::wstring profile_bytes = L"Original profile must stay unchanged.";
    { std::wofstream output(config); output << profile_bytes; }
    const auto previous_config = read(config);
    const auto expected = encode_new_window(content);
    const auto base = L"Local\\GVN.NewWindowTest." + std::to_wstring(GetCurrentProcessId()) + L"." +
                      std::to_wstring(GetTickCount64());
    Handle ready{CreateEventW(nullptr, TRUE, FALSE, (base + L".ready").c_str())};
    Handle release{CreateEventW(nullptr, TRUE, FALSE, (base + L".release").c_str())};
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle unrelated{CreateEventW(&security, TRUE, FALSE, (base + L".unrelated").c_str())};
    CHECK(ready.value && release.value && unrelated.value);
    Environment sync(L"GVN_NEW_WINDOW_TEST_SYNC", base);
    Environment marker(L"GVN_NEW_WINDOW_TEST_MARKER", std::to_wstring(reinterpret_cast<UINT_PTR>(unrelated.value)));
    OwnedChild child(launch_new_viewer(viewer_executable(), config, content), release.value);
    CHECK(child.process.id() && child.process.id() != GetCurrentProcessId());
    CHECK(WaitForSingleObject(ready.value, 5000) == WAIT_OBJECT_0);
    // The child has started but cannot read the snapshot until after this mutation.
    if (auto image = std::get_if<Image>(&content)) {
        image->frames[0].pixels.assign(6, {255, 0, 0, 255});
        image->metadata.dpi_x=image->metadata.dpi_y=1;image->metadata.dpi_defined=false;
    }
    else if (auto path = std::get_if<std::filesystem::path>(&content)) *path = L"changed after launch.png";
    CHECK(SetEvent(release.value));
    CHECK(WaitForSingleObject(child.process.handle(), 5000) == WAIT_OBJECT_0);
    DWORD code{};
    CHECK(GetExitCodeProcess(child.process.handle(), &code) && !code);
    CHECK(WaitForSingleObject(unrelated.value, 0) == WAIT_TIMEOUT);
    auto target = config;
    target += L".received";
    const auto actual = read(target);
    CHECK(actual.size() == expected.size() + 3);
    std::cout << "NEW_WINDOW_RECEIVER_FLAGS read_only=" << static_cast<unsigned>(actual[0])
              << " unrelated_rejected=" << static_cast<unsigned>(actual[1])
              << " noninteractive=" << static_cast<unsigned>(actual[2]) << '\n';
    CHECK(actual[0] == 1 && actual[1] == 1 && actual[2] == 1);
    CHECK(std::equal(expected.begin(), expected.end(), actual.begin() + 3));
    CHECK(read(config) == previous_config);
}
int receive(int count, wchar_t **arguments) {
    try {
        wchar_t base[512]{}, marker[64]{};
        CHECK(GetEnvironmentVariableW(L"GVN_NEW_WINDOW_TEST_SYNC", base, 512));
        CHECK(GetEnvironmentVariableW(L"GVN_NEW_WINDOW_TEST_MARKER", marker, 64));
        const auto marker_handle = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(marker)));
        const bool unrelated_rejected = !marker_is_present(marker_handle, std::wstring(base) + L".unrelated");
        if (!unrelated_rejected) CHECK(SetEvent(marker_handle));
        Handle ready{OpenEventW(EVENT_MODIFY_STATE, FALSE, (std::wstring(base) + L".ready").c_str())};
        Handle release{OpenEventW(SYNCHRONIZE, FALSE, (std::wstring(base) + L".release").c_str())};
        CHECK(ready.value && release.value && SetEvent(ready.value));
        CHECK(WaitForSingleObject(release.value, 5000) == WAIT_OBJECT_0);
        const auto mapping = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(std::stoull(arguments[2])));
        const auto writable = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0);
        if (writable) UnmapViewOfFile(writable);
        std::cout << "NEW_WINDOW_CHILD_HANDLES marker=" << marker_handle << " ready=" << ready.value
                  << " release=" << release.value << " rejected=" << unrelated_rejected << '\n';
        const auto startup = read_new_window_arguments(count, arguments);
        CHECK(startup);
        const auto bytes = encode_new_window(startup->content);
        auto target = startup->settings;
        target += L".received";
        std::ofstream output(target, std::ios::binary);
        const uint8_t flags[]{static_cast<uint8_t>(!writable), static_cast<uint8_t>(unrelated_rejected),
                              static_cast<uint8_t>(gvn_test::noninteractive(GetProcessWindowStation()))};
        output.write(reinterpret_cast<const char *>(flags), sizeof(flags));
        output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        return output.good() ? 0 : 3;
    } catch (const std::exception &error) {
        std::cerr << "NEW_WINDOW_RECEIVER_ERROR " << error.what() << '\n';
        return 4;
    } catch (...) { return 4; }
}
} // namespace

TEST(new_window_marker_check_detects_actual_objects_and_rejects_valid_unrelated_handles) {
    const auto name = L"Local\\GVN.NewWindowMarker." + std::to_wstring(GetCurrentProcessId()) + L"." +
                      std::to_wstring(GetTickCount64());
    Handle marker{CreateEventW(nullptr, TRUE, FALSE, name.c_str())};
    Handle other{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    Handle alias;
    CHECK(marker.value && other.value);
    CHECK(DuplicateHandle(GetCurrentProcess(), marker.value, GetCurrentProcess(), &alias.value,
                          0, FALSE, DUPLICATE_SAME_ACCESS));
    CHECK(alias.value != marker.value);
    CHECK(marker_is_present(marker.value, name));
    CHECK(marker_is_present(alias.value, name));
    CHECK(SetEvent(other.value)); // A successful SetEvent does not identify the parent's marker.
    CHECK(WaitForSingleObject(marker.value, 0) == WAIT_TIMEOUT);
    CHECK(!marker_is_present(other.value, name));
    CHECK(!marker_is_present(nullptr, name));
    Handle stale{OpenEventW(SYNCHRONIZE, FALSE, name.c_str())};
    CHECK(stale.value);
    const auto vacant = std::exchange(stale.value, nullptr);
    CHECK(CloseHandle(vacant));
    CHECK(!marker_is_present(vacant, name)); // OpenEvent may reuse this just-closed slot.
}
TEST(new_window_empty_roundtrip_has_no_implicit_startup_file) {
    auto bytes = encode_new_window(std::monostate{});
    CHECK(bytes.size() == 32 && std::holds_alternative<std::monostate>(decode_new_window(bytes)));
    word(bytes, 16, 1);
    THROWS(decode_new_window(bytes));
}
TEST(new_window_file_roundtrip_normalizes_unicode_relative_paths_and_rejects_nulls) {
    const auto path = std::filesystem::path(L"folder 日本語/../space & $()/many.parts.png");
    const auto result = decode_new_window(encode_new_window(path));
    CHECK(std::get<std::filesystem::path>(result) == std::filesystem::absolute(path).lexically_normal());
    THROWS(encode_new_window(std::filesystem::path{}));
    THROWS(encode_new_window(std::filesystem::path(std::wstring(L"a\0b", 3))));
    THROWS(encode_new_window(std::filesystem::path(std::wstring(32768, L'x'))));
}
TEST(new_window_frame_roundtrip_preserves_all_alpha_and_hidden_rgb_without_source_identity) {
    auto source = alpha();
    source.path = L"original.png";
    const auto result = std::get<Image>(decode_new_window(encode_new_window(source)));
    CHECK(result.width == source.width && result.height == source.height && result.frames.size() == 1);
    CHECK(result.frames[0].pixels == source.frames[0].pixels && result.path.empty());
}
TEST(new_window_rejects_unknown_headers_truncation_extra_bytes_and_reserved_fields) {
    const auto original = encode_new_window(alpha());
    for (size_t length : {size_t(0), size_t(1), size_t(31), original.size() - 1})
        THROWS(decode_new_window(std::span(original).first(length)));
    for (auto [offset, value] : {std::pair<size_t, uint32_t>{0, 0}, {4, 2}, {8, 3}, {12, 23}, {16, 0},
                                {20, 65536}, {24, 1}, {28, 1}}) {
        auto broken = original;
        word(broken, offset, value);
        THROWS(decode_new_window(broken));
    }
    auto extra = original;
    extra.push_back(0);
    THROWS(decode_new_window(extra));
}
TEST(new_window_rejects_malformed_path_payloads_and_receiver_relative_paths) {
    const auto original = encode_new_window(std::filesystem::absolute(L"日本語.png"));
    for (size_t offset : {size_t(32), original.size() - 2}) {
        auto broken = original;
        broken[offset] = offset == 32 ? 0 : 65;
        broken[offset + 1] = 0;
        THROWS(decode_new_window(broken));
    }
    auto relative = original;
    relative[34] = 'x'; // Remove the drive colon; receiver must not use its own CWD.
    THROWS(decode_new_window(relative));
    auto dimensions = original;
    word(dimensions, 20, 1);
    THROWS(decode_new_window(dimensions));
}
TEST(new_window_rejects_invalid_frames_and_oversized_image_before_allocating_pixels) {
    auto image = alpha();
    image.frames.push_back(image.frames[0]);
    THROWS(encode_new_window(image));
    image.frames.resize(1);
    image.frames[0].pixels.pop_back();
    THROWS(encode_new_window(image));
    auto bytes = encode_new_window(alpha());
    word(bytes, 16, 65535);
    word(bytes, 20, 65535);
    THROWS(decode_new_window(bytes));
}
TEST(new_window_startup_leaves_normal_arguments_and_rejects_malformed_internal_forms) {
    wchar_t app[] = L"GVN.exe", flag[] = L"--new-window", normal[] = L"--settings";
    wchar_t handle[] = L"1", size[] = L"32", config[] = L"settings.ini";
    wchar_t *regular[]{app, normal, config};
    CHECK(!read_new_window_arguments(3, regular));
    wchar_t *values[]{app, flag, handle, size, config};
    THROWS(read_new_window_arguments(4, values));
    for (auto bad : {L"0", L"-1", L"1x", L"18446744073709551616", L""}) {
        values[2] = const_cast<wchar_t *>(bad);
        THROWS(read_new_window_arguments(5, values));
    }
    values[2] = handle;
    for (auto bad : {L"0", L"31", L"1073741857", L"+32", L"32 "}) {
        values[3] = const_cast<wchar_t *>(bad);
        THROWS(read_new_window_arguments(5, values));
    }
}
TEST(new_window_real_empty_child_inherits_private_desktop_only_snapshot_and_preserves_profile) {
    real_child(std::monostate{});
}
TEST(new_window_real_file_child_uses_frozen_unicode_source_after_parent_changes) {
    real_child(std::filesystem::absolute(L"画像 & $() space/many.parts.png"));
}
TEST(new_window_real_image_child_keeps_snapshot_alpha_after_parent_changes) {
    real_child(alpha());
}
TEST(new_window_failed_launch_releases_mapping_attributes_and_process_handles) {
    Directory directory;
    auto attempt = [&] { return launch_new_viewer(directory.path / L"missing.exe", directory.path / L"settings.ini", alpha()); };
    THROWS(attempt());
    DWORD before{}, after{};
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (int i = 0; i < 8; ++i) THROWS(attempt());
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after) && after == before);
}
TEST(new_window_defined_resolution_is_exact_and_legacy_payload_remains_readable) {
    auto source=alpha();source.metadata.dpi_x=144.5;source.metadata.dpi_y=288.25;source.metadata.dpi_defined=true;
    const auto bytes=encode_new_window(source);CHECK(bytes.size()==32+16+6*4);
    uint32_t version{};std::memcpy(&version,bytes.data()+4,4);CHECK(version==2);
    double resolution[2]{};std::memcpy(resolution,bytes.data()+32,16);CHECK(resolution[0]==144.5&&resolution[1]==288.25);
    const auto result=std::get<Image>(decode_new_window(bytes));CHECK(result.metadata.dpi_defined&&result.metadata.dpi_x==144.5&&result.metadata.dpi_y==288.25&&result.frames[0].pixels==source.frames[0].pixels&&result.path.empty());
    auto legacy=bytes;legacy.erase(legacy.begin()+32,legacy.begin()+48);word(legacy,4,1);word(legacy,12,24);
    const auto old=std::get<Image>(decode_new_window(legacy));CHECK(!old.metadata.dpi_defined&&old.metadata.dpi_x==72&&old.metadata.dpi_y==72&&old.frames[0].pixels==source.frames[0].pixels);
}
TEST(new_window_rejects_nonfinite_nonpositive_resolution_and_invalid_extended_payload) {
    auto source=alpha();source.metadata.dpi_defined=true;const auto original=encode_new_window(source);
    for(double value:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        for(int axis=0;axis<2;++axis){auto broken=original;std::memcpy(broken.data()+32+axis*8,&value,8);THROWS(decode_new_window(broken));}
        source.metadata.dpi_x=value;THROWS(encode_new_window(source));
    }
    auto broken=original;broken.pop_back();word(broken,12,uint32_t(broken.size()-32));THROWS(decode_new_window(broken));
    broken=original;word(broken,8,0);THROWS(decode_new_window(broken));
    broken=original;word(broken,4,3);THROWS(decode_new_window(broken));
}
TEST(new_window_real_child_keeps_frozen_resolution_and_alpha_after_parent_changes) {
    auto source=alpha();source.metadata.dpi_defined=true;source.metadata.dpi_x=144.5;source.metadata.dpi_y=288.25;real_child(source);
}
int wmain(int count, wchar_t **arguments) {
    if (count > 1 && std::wstring_view(arguments[1]) == L"--new-window") return receive(count, arguments);
    gvn_test::ClipboardLock lock;
    if (!lock) return 2;
    const auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !gvn_test::noninteractive(station) || !SetProcessWindowStation(station)) return 2;
    const auto desktop = CreateDesktopW(L"GVN-new-window-tests", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop)) return 2;
    return run_tests();
}
