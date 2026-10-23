#include "wallpaper_platform.h"
#include <cstring>
#include <utility>

namespace gvn {
namespace {
constexpr DWORD value_limit=65536;
[[noreturn]] void fail(const char *message,LSTATUS code) {
    throw std::runtime_error(std::string(message)+" (Win32 "+std::to_string(code)+")");
}
struct Key {
    HKEY value{};
    explicit Key(HKEY key=nullptr):value(key) {}
    Key(const Key &)=delete;
    Key(Key &&other) noexcept:value(std::exchange(other.value,nullptr)) {}
    ~Key() {if (value) RegCloseKey(value);}
};
Key open(HKEY root,const std::wstring &subkey,bool write) {
    HKEY key{};
    const auto code=write?RegCreateKeyExW(root,subkey.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)
                         :RegOpenKeyExW(root,subkey.c_str(),0,KEY_QUERY_VALUE,&key);
    if (!write && code==ERROR_FILE_NOT_FOUND) return Key{};
    if (code!=ERROR_SUCCESS) fail("Cannot open wallpaper settings",code);
    return Key{key};
}
std::optional<WallpaperValue> read_value(HKEY key,LPCWSTR name) {
    if (!key) return {};
    DWORD type{},size{};auto code=RegQueryValueExW(key,name,nullptr,&type,nullptr,&size);
    if (code==ERROR_FILE_NOT_FOUND) return {};
    if (code!=ERROR_SUCCESS) fail("Cannot read wallpaper setting",code);
    if (size>value_limit) throw std::runtime_error("Wallpaper setting is too large");
    WallpaperValue result{type,std::vector<uint8_t>(size)};
    code=RegQueryValueExW(key,name,nullptr,&type,result.bytes.data(),&size);
    if (code!=ERROR_SUCCESS) fail("Cannot read wallpaper setting",code);
    result.type=type;result.bytes.resize(size);return result;
}
void validate(const std::optional<WallpaperValue> &value) {
    if (value && value->bytes.size()>value_limit) throw std::invalid_argument("Wallpaper setting is too large");
}
void write_value(HKEY key,LPCWSTR name,const std::optional<WallpaperValue> &value) {
    const auto code=value?RegSetValueExW(key,name,0,value->type,value->bytes.data(),static_cast<DWORD>(value->bytes.size()))
                         :RegDeleteValueW(key,name);
    if (code!=ERROR_SUCCESS && !(code==ERROR_FILE_NOT_FOUND && !value)) fail("Cannot write wallpaper setting",code);
}
WallpaperValue text_value(const std::wstring &text) {
    WallpaperValue result{REG_SZ,std::vector<uint8_t>((text.size()+1)*sizeof(wchar_t))};
    memcpy(result.bytes.data(),text.c_str(),result.bytes.size());return result;
}
}
WallpaperValues wallpaper_values(WallpaperMode mode) {
    const auto style=wallpaper_style(mode);return {text_value(style.tile),text_value(style.style)};
}
namespace detail {
WallpaperPlatform wallpaper_platform_for_registry(HKEY root,std::wstring subkey,
    std::function<BOOL(UINT,UINT,PVOID,UINT)> system,std::wstring mutex_name) {
    if (!root || subkey.empty() || subkey.find(L'\0')!=std::wstring::npos || !system)
        throw std::invalid_argument("Invalid wallpaper platform");
    WallpaperPlatform result;
    result.mutex_name=std::move(mutex_name);
    result.read=[root,subkey,system] {
        const auto key=open(root,subkey,false);
        WallpaperSnapshot snapshot{{read_value(key.value,L"TileWallpaper"),read_value(key.value,L"WallpaperStyle")},{}};
        std::vector<wchar_t> path(32768);
        if (!system(SPI_GETDESKWALLPAPER,static_cast<UINT>(path.size()),path.data(),0))
            fail("Cannot read current wallpaper",GetLastError());
        const auto size=wcsnlen_s(path.data(),path.size());
        if (size==path.size()) throw std::runtime_error("Invalid current wallpaper path");
        snapshot.image=std::wstring(path.data(),size);return snapshot;
    };
    result.write=[root,subkey](const WallpaperValues &values) {
        validate(values.tile);validate(values.style);
        const auto key=open(root,subkey,true);
        write_value(key.value,L"TileWallpaper",values.tile);write_value(key.value,L"WallpaperStyle",values.style);
    };
    result.apply=[system](const std::filesystem::path &path) {
        const auto text=path.wstring();
        if (text.size()>32767 || text.find(L'\0')!=std::wstring::npos)
            throw std::invalid_argument("Invalid wallpaper path");
        if (!system(SPI_SETDESKWALLPAPER,0,const_cast<wchar_t *>(text.c_str()),SPIF_UPDATEINIFILE|SPIF_SENDCHANGE))
            fail("Cannot apply wallpaper",GetLastError());
    };
    return result;
}
}
WallpaperPlatform native_wallpaper_platform() {
    return detail::wallpaper_platform_for_registry(HKEY_CURRENT_USER,L"Control Panel\\Desktop",
                                                  SystemParametersInfoW,L"Local\\GVN.Wallpaper");
}
} // namespace gvn
