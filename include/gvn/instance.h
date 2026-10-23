#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gvn {
inline constexpr wchar_t kViewerWindowClass[] = L"GVN.ImageViewer";
inline constexpr ULONG_PTR kInstanceCopyData = 0x47564e49;
struct InstanceIdentity {
    std::wstring profile, property, mutex;
};
InstanceIdentity instance_identity(const std::filesystem::path &settings);
std::vector<uint8_t> instance_request(const InstanceIdentity &, const std::filesystem::path &);
std::optional<std::filesystem::path> read_instance_request(const InstanceIdentity &, const COPYDATASTRUCT &);
bool publish_instance(HWND, const InstanceIdentity &);
void unpublish_instance(HWND, const InstanceIdentity &);
void activate_viewer(HWND);
enum class InstanceDecision { Start, Forwarded, Busy };
class InstanceStartup {
    InstanceIdentity identity;
    HANDLE mutex{};
    bool held{};

  public:
    explicit InstanceStartup(InstanceIdentity value) : identity(std::move(value)) {}
    ~InstanceStartup();
    InstanceStartup(const InstanceStartup &) = delete;
    InstanceStartup &operator=(const InstanceStartup &) = delete;
    InstanceDecision decide(bool allow_multiple, const std::filesystem::path &);
    void ready();
};
} // namespace gvn
