#include "gvn/instance.h"
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace gvn {
namespace {
constexpr uint32_t kMagic = 0x31495647;
constexpr size_t kMaximumCharacters = 32768, kMaximumPayload = 256 * 1024;
struct Header {
    uint32_t magic, profile, path;
};
std::wstring normalized(const std::filesystem::path &path) {
    if (path.empty() || path.native().find(L'\0') != std::wstring::npos ||
        path.native().size() >= kMaximumCharacters)
        throw std::invalid_argument("Invalid instance input path");
    auto result = std::filesystem::absolute(path).lexically_normal().wstring();
    if (result.empty() || result.size() >= kMaximumCharacters || result.find(L'\0') != std::wstring::npos)
        throw std::invalid_argument("Invalid instance profile or image path");
    return result;
}
HWND peer(const InstanceIdentity &identity) {
    struct Search {
        const InstanceIdentity &identity;
        HWND found{};
    } search{identity};
    EnumWindows(
        [](HWND window, LPARAM value) -> BOOL {
            auto &search = *reinterpret_cast<Search *>(value);
            wchar_t name[64]{};
            GetClassNameW(window, name, 64);
            if (wcscmp(name, kViewerWindowClass) == 0 &&
                GetPropW(window, search.identity.property.c_str()) == reinterpret_cast<HANDLE>(1)) {
                search.found = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}
} // namespace
InstanceIdentity instance_identity(const std::filesystem::path &settings) {
    const auto value = normalized(settings);
    const int length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(),
                                     static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr, 0);
    if (length <= 0)
        throw std::runtime_error("Cannot normalize instance profile");
    InstanceIdentity result;
    result.profile.resize(length);
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, value.data(), static_cast<int>(value.size()),
                       result.profile.data(), length, nullptr, nullptr, 0))
        throw std::runtime_error("Cannot normalize instance profile");
    uint64_t hash = 14695981039346656037ull;
    for (const auto character : result.profile)
        for (const auto byte : {unsigned(character) & 255u, unsigned(character) >> 8}) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
    std::wostringstream key;
    key << std::hex << std::setw(16) << std::setfill(L'0') << hash;
    result.property = L"GVN.Instance.1." + key.str();
    result.mutex = L"Local\\GVN.Start.1." + key.str();
    return result;
}
std::vector<uint8_t> instance_request(const InstanceIdentity &identity, const std::filesystem::path &path) {
    const auto image = path.empty() ? std::wstring{} : normalized(path);
    if (identity.profile.empty() || identity.profile.size() >= kMaximumCharacters ||
        identity.profile.find(L'\0') != std::wstring::npos)
        throw std::invalid_argument("Invalid instance profile");
    const Header header{kMagic, static_cast<uint32_t>(identity.profile.size() + 1),
                        static_cast<uint32_t>(image.size() + 1)};
    std::vector<uint8_t> bytes(sizeof(header) + (size_t(header.profile) + header.path) * sizeof(wchar_t));
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), identity.profile.c_str(), header.profile * sizeof(wchar_t));
    std::memcpy(bytes.data() + sizeof(header) + header.profile * sizeof(wchar_t), image.c_str(),
                header.path * sizeof(wchar_t));
    return bytes;
}
std::optional<std::filesystem::path> read_instance_request(const InstanceIdentity &identity,
                                                           const COPYDATASTRUCT &copy) {
    if (copy.dwData != kInstanceCopyData || !copy.lpData || copy.cbData < sizeof(Header) ||
        copy.cbData > kMaximumPayload)
        return {};
    Header header{};
    std::memcpy(&header, copy.lpData, sizeof(header));
    if (header.magic != kMagic || !header.profile || !header.path || header.profile > kMaximumCharacters ||
        header.path > kMaximumCharacters ||
        sizeof(Header) + (uint64_t(header.profile) + header.path) * sizeof(wchar_t) != copy.cbData)
        return {};
    std::wstring profile(header.profile, L'\0'), path(header.path, L'\0');
    const auto bytes = static_cast<const uint8_t *>(copy.lpData) + sizeof(header);
    std::memcpy(profile.data(), bytes, profile.size() * sizeof(wchar_t));
    std::memcpy(path.data(), bytes + profile.size() * sizeof(wchar_t), path.size() * sizeof(wchar_t));
    if (profile.back() != L'\0' || path.back() != L'\0' || profile.find(L'\0') != profile.size() - 1 ||
        path.find(L'\0') != path.size() - 1)
        return {};
    profile.pop_back();
    path.pop_back();
    if (profile != identity.profile || (!path.empty() && !std::filesystem::path(path).is_absolute()))
        return {};
    return std::filesystem::path(std::move(path));
}
bool publish_instance(HWND window, const InstanceIdentity &identity) {
    return SetPropW(window, identity.property.c_str(), reinterpret_cast<HANDLE>(1)) != FALSE;
}
void unpublish_instance(HWND window, const InstanceIdentity &identity) {
    RemovePropW(window, identity.property.c_str());
}
void activate_viewer(HWND window) {
    if (IsIconic(window))
        ShowWindow(window, SW_RESTORE);
    auto target = GetLastActivePopup(window);
    DWORD process{};
    GetWindowThreadProcessId(target, &process);
    if (!target || process != GetCurrentProcessId() || !IsWindowEnabled(target) ||
        !(GetWindowLongPtrW(target, GWL_STYLE) & WS_VISIBLE))
        target = window;
    SetForegroundWindow(target);
    SetActiveWindow(target);
    SetFocus(target);
}
InstanceStartup::~InstanceStartup() {
    ready();
    if (mutex)
        CloseHandle(mutex);
}
void InstanceStartup::ready() {
    if (held) {
        ReleaseMutex(mutex);
        held = false;
    }
}
InstanceDecision InstanceStartup::decide(bool allow_multiple, const std::filesystem::path &path) {
    if (allow_multiple)
        return InstanceDecision::Start;
    mutex = CreateMutexW(nullptr, FALSE, identity.mutex.c_str());
    if (!mutex)
        return InstanceDecision::Busy;
    const auto status = WaitForSingleObject(mutex, 2000);
    if (status != WAIT_OBJECT_0 && status != WAIT_ABANDONED)
        return InstanceDecision::Busy;
    held = true;
    const auto bytes = instance_request(identity, path);
    COPYDATASTRUCT copy{kInstanceCopyData, static_cast<DWORD>(bytes.size()),
                        const_cast<uint8_t *>(bytes.data())};
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto window = peer(identity);
        if (!window)
            return InstanceDecision::Start;
        DWORD process{};
        GetWindowThreadProcessId(window, &process);
        AllowSetForegroundWindow(process);
        DWORD_PTR reply{};
        if (SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copy),
                                SMTO_ABORTIFHUNG | SMTO_BLOCK, 750, &reply) &&
            reply == 1)
            return InstanceDecision::Forwarded;
        if (IsWindow(window))
            return InstanceDecision::Busy;
    }
    return InstanceDecision::Start;
}
} // namespace gvn
