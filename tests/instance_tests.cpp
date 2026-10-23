#include "gvn/instance.h"
#include "test.h"
#include "test_desktop.h"
#include <cstring>
using namespace gvn;
namespace {
auto profile() {
    return std::filesystem::temp_directory_path() /
           (L"GVN-instance-tests-" + std::to_wstring(GetCurrentProcessId())) / L"settings.ini";
}
COPYDATASTRUCT envelope(std::vector<uint8_t> &bytes) {
    return {kInstanceCopyData, static_cast<DWORD>(bytes.size()), bytes.data()};
}
void word(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
bool acknowledge = true;
std::optional<InstanceIdentity> receiver;
std::optional<std::filesystem::path> received;
LRESULT CALLBACK receive(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COPYDATA) {
        received = read_instance_request(*receiver, *reinterpret_cast<COPYDATASTRUCT *>(l));
        return received && acknowledge ? 1 : 0;
    }
    return DefWindowProcW(window, message, w, l);
}
} // namespace
TEST(instance_identity_normalizes_case_and_dot_segments_and_isolates_profiles) {
    auto path = profile();
    const auto first = instance_identity(path);
    auto changed = path.wstring();
    CharUpperBuffW(changed.data(), static_cast<DWORD>(changed.size()));
    const auto second =
        instance_identity(std::filesystem::path(changed).parent_path() / L"nested/../settings.ini");
    CHECK(first.profile == second.profile && first.property == second.property &&
          first.mutex == second.mutex);
    const auto other = instance_identity(path.parent_path() / L"other.ini");
    CHECK(first.profile != other.profile && first.property != other.property && first.mutex != other.mutex);
    THROWS(instance_identity({}));
    THROWS(instance_identity(std::wstring(L"bad\0profile", 11)));
    THROWS(instance_identity(std::wstring(32768, L'x')));
}
TEST(instance_protocol_roundtrips_unicode_empty_and_relative_sender_paths) {
    const auto identity = instance_identity(profile());
    for (const auto &path : {std::filesystem::path{}, std::filesystem::path(L"画像 フォルダ/半透明.png"),
                             profile().parent_path() / L"😀 alpha.bmp"}) {
        auto bytes = instance_request(identity, path);
        const auto value = read_instance_request(identity, envelope(bytes));
        CHECK(value && *value == (path.empty() ? path : std::filesystem::absolute(path).lexically_normal()));
        // The receiver copies unaligned WM_COPYDATA input rather than casting its payload.
        bytes.insert(bytes.begin(), 0);
        COPYDATASTRUCT unaligned{kInstanceCopyData, static_cast<DWORD>(bytes.size() - 1), bytes.data() + 1};
        CHECK(read_instance_request(identity, unaligned) == value);
    }
}
TEST(instance_protocol_rejects_foreign_profiles_headers_lengths_and_terminators) {
    const auto identity = instance_identity(profile());
    auto good = instance_request(identity, profile().parent_path() / L"alpha.png");
    auto copy = envelope(good);
    CHECK(!read_instance_request(instance_identity(profile().parent_path() / L"other.ini"), copy));
    copy.dwData++;
    CHECK(!read_instance_request(identity, copy));
    copy = envelope(good);
    copy.lpData = nullptr;
    CHECK(!read_instance_request(identity, copy));
    for (DWORD length : std::initializer_list<DWORD>{0, 11, static_cast<DWORD>(good.size() - 1),
                                                     static_cast<DWORD>(good.size() + 1), 262145}) {
        copy = envelope(good);
        copy.cbData = length;
        CHECK(!read_instance_request(identity, copy));
    }
    for (auto [offset, value] :
         {std::pair<size_t, uint32_t>{0, 0}, {4, 0}, {8, 0}, {4, 32769}, {8, 0xffffffff}}) {
        auto broken = good;
        word(broken, offset, value);
        CHECK(!read_instance_request(identity, envelope(broken)));
    }
    for (size_t offset : {size_t(12), size_t(12 + identity.profile.size() * 2), good.size() - 2}) {
        auto broken = good;
        broken[offset] ^= 0x41;
        CHECK(!read_instance_request(identity, envelope(broken)));
    }
    auto relative = good;
    const size_t path_offset = 12 + (identity.profile.size() + 1) * 2;
    // A payload that starts with a letter rather than a drive root must not be interpreted in receiver CWD.
    relative[path_offset + 2] = 'x';
    CHECK(!read_instance_request(identity, envelope(relative)));
}
TEST(instance_protocol_rejects_embedded_null_and_oversized_sender_values) {
    const auto identity = instance_identity(profile());
    auto broken = instance_request(identity, profile().parent_path() / L"alpha.png");
    broken[14] = broken[15] = 0;
    CHECK(!read_instance_request(identity, envelope(broken)));
    auto bad_identity = identity;
    bad_identity.profile.clear();
    THROWS(instance_request(bad_identity, {}));
    THROWS(instance_request(identity, std::wstring(L"a\0b", 3)));
    THROWS(instance_request(identity, std::wstring(32768, L'x')));
}
TEST(instance_startup_publishes_forwards_rejects_and_releases_owned_registration) {
    receiver = instance_identity(profile());
    acknowledge = true;
    InstanceStartup first(*receiver);
    CHECK(first.decide(false, {}) == InstanceDecision::Start);
    const auto window =
        CreateWindowExW(0, kViewerWindowClass, L"Owned instance protocol", WS_OVERLAPPEDWINDOW, 0, 0, 400,
                        300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(window && publish_instance(window, *receiver));
    first.ready();
    {
        InstanceStartup second(*receiver);
        const auto path = profile().parent_path() / L"new image.png";
        CHECK(second.decide(false, path) == InstanceDecision::Forwarded && received == path);
    }
    {
        InstanceStartup multiple(*receiver);
        CHECK(multiple.decide(true, {}) == InstanceDecision::Start);
    }
    {
        acknowledge = false;
        InstanceStartup rejected(*receiver);
        CHECK(rejected.decide(false, {}) == InstanceDecision::Busy);
    }
    unpublish_instance(window, *receiver);
    {
        InstanceStartup next(*receiver);
        CHECK(next.decide(false, {}) == InstanceDecision::Start);
    }
    CHECK(DestroyWindow(window));
}
int main() {
    gvn_test::ClipboardLock lock;
    if (!lock)
        return 2;
    auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !gvn_test::noninteractive(station) || !SetProcessWindowStation(station))
        return 2;
    auto desktop = CreateDesktopW(L"GVN-instance-tests", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop))
        return 2;
    WNDCLASSEXW window{sizeof(window)};
    window.lpfnWndProc = receive;
    window.hInstance = GetModuleHandleW(nullptr);
    window.lpszClassName = kViewerWindowClass;
    if (!RegisterClassExW(&window))
        return 2;
    return run_tests();
}
