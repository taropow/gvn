#include "gvn/new_window.h"
#include <cstring>
#include <cmath>
#include <string_view>
#include <system_error>
#include <utility>

namespace gvn {
namespace {
constexpr uint32_t magic = 0x574e5647, version = 1, resolution_version = 2;
struct Header {
    uint32_t magic, version, kind, payload, width, height;
    uint64_t reserved;
};
static_assert(sizeof(Header) == 32 && sizeof(wchar_t) == 2);
struct Resolution {double x,y;};
static_assert(sizeof(Resolution)==16&&std::numeric_limits<double>::is_iec559);
constexpr size_t max_snapshot=sizeof(Header)+sizeof(Resolution)+kImageLimit;
bool valid_resolution(Resolution r){return std::isfinite(r.x)&&std::isfinite(r.y)&&r.x>0&&r.y>0;}
struct Handle {
    HANDLE value{};
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Mapping {
    void *value{};
    ~Mapping() { if (value) UnmapViewOfFile(value); }
};
[[noreturn]] void win_error(const char *message) {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), message);
}
std::filesystem::path normalized_path(const std::filesystem::path &path) {
    const auto text = path.wstring();
    if (text.empty() || text.size() > 32767 || text.find(L'\0') != std::wstring::npos)
        throw std::runtime_error("Invalid new-window path");
    auto result = std::filesystem::absolute(path).lexically_normal();
    if (result.native().size() > 32767)
        throw std::runtime_error("New-window path is too long");
    return result;
}
struct ContentView {
    Header header{magic, version, 0, 0, 0, 0, 0};
    std::wstring path;
    const Image *image{};
    Resolution resolution{};
    explicit ContentView(const NewWindowContent &content) {
        if (const auto file = std::get_if<std::filesystem::path>(&content)) {
            path = normalized_path(*file).wstring();
            header.kind = 1;
            header.payload = static_cast<uint32_t>((path.size() + 1) * sizeof(wchar_t));
        } else if ((image = std::get_if<Image>(&content))) {
            const auto count = checked_image_pixels(image->width, image->height);
            if (image->frames.size() != 1 || image->frames[0].pixels.size() != count)
                throw std::runtime_error("A new window requires one complete frame");
            header.kind = 2;
            header.payload = static_cast<uint32_t>(count * sizeof(Pixel));
            header.width = image->width;
            header.height = image->height;
            if(image->metadata.dpi_defined) {
                resolution={image->metadata.dpi_x,image->metadata.dpi_y};
                if(!valid_resolution(resolution))throw std::runtime_error("Invalid new-window resolution");
                header.version=resolution_version;header.payload+=sizeof(Resolution);
            }
        }
    }
    size_t size() const { return sizeof(Header) + header.payload; }
    void write(void *destination) const {
        std::memcpy(destination, &header, sizeof(header));
        auto payload = static_cast<uint8_t *>(destination) + sizeof(header);
        if (header.kind == 1)
            std::memcpy(payload, path.c_str(), header.payload);
        else if (header.kind == 2) {
            if(header.version==resolution_version){std::memcpy(payload,&resolution,sizeof(resolution));payload+=sizeof(resolution);}
            std::memcpy(payload,image->frames[0].pixels.data(),image->frames[0].pixels.size()*sizeof(Pixel));
        }
    }
};
uint64_t unsigned_decimal(const wchar_t *text) {
    if (!text || !*text)
        throw std::runtime_error("Missing new-window startup value");
    uint64_t result{};
    for (; *text; ++text) {
        if (*text < L'0' || *text > L'9' || result > (UINT64_MAX - (*text - L'0')) / 10)
            throw std::runtime_error("Invalid new-window startup value");
        result = result * 10 + (*text - L'0');
    }
    return result;
}
std::wstring quote(std::wstring_view value) {
    std::wstring result = L"\"";
    size_t slashes{};
    for (const auto ch : value) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        result.append(slashes * (ch == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (ch == L'"') result += L'\\';
        result += ch;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}
std::wstring object_name(HANDLE object) {
    DWORD bytes{};
    GetUserObjectInformationW(object, UOI_NAME, nullptr, 0, &bytes);
    if (bytes < sizeof(wchar_t) || bytes > 65536) win_error("Cannot query the viewer desktop");
    std::wstring result(bytes / sizeof(wchar_t), L'\0');
    if (!GetUserObjectInformationW(object, UOI_NAME, result.data(), bytes, &bytes))
        win_error("Cannot read the viewer desktop");
    result.resize(wcslen(result.c_str()));
    return result;
}
struct Attributes {
    LPPROC_THREAD_ATTRIBUTE_LIST value{};
    bool initialized{};
    ~Attributes() {
        if (initialized) DeleteProcThreadAttributeList(value);
        if (value) HeapFree(GetProcessHeap(), 0, value);
    }
};
} // namespace

std::vector<uint8_t> encode_new_window(const NewWindowContent &content) {
    const ContentView view(content);
    std::vector<uint8_t> result(view.size());
    view.write(result.data());
    return result;
}
NewWindowContent decode_new_window(std::span<const uint8_t> bytes) {
    if (bytes.size() < sizeof(Header) || bytes.size() > max_snapshot)
        throw std::runtime_error("Invalid new-window snapshot size");
    Header header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (header.magic != magic || (header.version != version && header.version != resolution_version) || header.reserved ||
        header.payload != bytes.size() - sizeof(Header))
        throw std::runtime_error("Invalid new-window snapshot header");
    if(header.version==resolution_version&&header.kind!=2)throw std::runtime_error("Resolution header requires an image");
    auto data = bytes.subspan(sizeof(Header));
    if (header.kind == 0 && !header.payload && !header.width && !header.height)
        return std::monostate{};
    if (header.kind == 1 && !header.width && !header.height && data.size() >= 4 &&
        data.size() <= 65536 && data.size() % sizeof(wchar_t) == 0) {
        std::wstring text(data.size() / sizeof(wchar_t), L'\0');
        std::memcpy(text.data(), data.data(), data.size());
        if (text.back() || text.find(L'\0') != text.size() - 1)
            throw std::runtime_error("Invalid new-window filename terminator");
        text.pop_back();
        const std::filesystem::path path(text);
        if (!path.is_absolute()) throw std::runtime_error("New-window filename must be absolute");
        return path;
    }
    if (header.kind == 2) {
        const auto count = checked_image_pixels(header.width, header.height);
        const size_t extra=header.version==resolution_version?sizeof(Resolution):0;
        if (data.size() != count * sizeof(Pixel)+extra)
            throw std::runtime_error("Incomplete new-window image");
        Resolution resolution{72,72};
        if(extra){std::memcpy(&resolution,data.data(),sizeof(resolution));if(!valid_resolution(resolution))throw std::runtime_error("Invalid received resolution");data=data.subspan(extra);}
        Image image;
        image.width = header.width;
        image.height = header.height;
        image.format = L"Clipboard snapshot";
        image.metadata.original_width=image.width;image.metadata.original_height=image.height;image.metadata.original_bpp=32;image.metadata.original_color_type=ImageColorType::Rgb;
        image.metadata.dpi_x=resolution.x;image.metadata.dpi_y=resolution.y;image.metadata.dpi_defined=extra!=0;
        image.frames.resize(1);
        image.frames[0].pixels.resize(count);
        std::memcpy(image.frames[0].pixels.data(), data.data(), data.size());
        return image;
    }
    throw std::runtime_error("Unknown new-window content");
}
std::optional<NewWindowStartup> read_new_window_arguments(int count, wchar_t *const *arguments) {
    if (count < 2 || !arguments || !arguments[1] || std::wstring_view(arguments[1]) != L"--new-window")
        return std::nullopt;
    if (count != 5) throw std::runtime_error("Incomplete new-window command line");
    const auto number = unsigned_decimal(arguments[2]), size = unsigned_decimal(arguments[3]);
    if (!number || number > static_cast<uint64_t>(std::numeric_limits<INT_PTR>::max()) ||
        size < sizeof(Header) || size > max_snapshot)
        throw std::runtime_error("Invalid new-window snapshot handle or size");
    Handle mapping{reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(number))};
    const auto config = normalized_path(arguments[4]);
    Mapping view{MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, static_cast<SIZE_T>(size))};
    if (!view.value) win_error("Cannot receive the new-window snapshot");
    return NewWindowStartup{config, decode_new_window({static_cast<const uint8_t *>(view.value), static_cast<size_t>(size)})};
}
std::filesystem::path viewer_executable() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) win_error("Cannot locate the viewer executable");
    path.resize(length);
    return path;
}
ViewerProcess::~ViewerProcess() {
    if (thread_) CloseHandle(thread_);
    if (process_) CloseHandle(process_);
}
ViewerProcess::ViewerProcess(ViewerProcess &&other) noexcept
    : process_(std::exchange(other.process_, nullptr)), thread_(std::exchange(other.thread_, nullptr)) {}
ViewerProcess launch_new_viewer(const std::filesystem::path &executable,
                                const std::filesystem::path &settings,
                                const NewWindowContent &content) {
    const auto exe = normalized_path(executable), config = normalized_path(settings);
    const ContentView content_view(content);
    Handle writable{CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      static_cast<DWORD>(content_view.size()), nullptr)};
    if (!writable.value) win_error("Cannot create the new-window snapshot");
    {
        Mapping view{MapViewOfFile(writable.value, FILE_MAP_WRITE, 0, 0, content_view.size())};
        if (!view.value) win_error("Cannot write the new-window snapshot");
        content_view.write(view.value);
    }
    Handle snapshot;
    if (!DuplicateHandle(GetCurrentProcess(), writable.value, GetCurrentProcess(), &snapshot.value,
                         FILE_MAP_READ, TRUE, 0)) win_error("Cannot share the new-window snapshot");
    Attributes attributes;
    SIZE_T attribute_size{};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    attributes.value = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attribute_size));
    if (!attributes.value) throw std::bad_alloc();
    if (!InitializeProcThreadAttributeList(attributes.value, 1, 0, &attribute_size))
        win_error("Cannot initialize new-window handle inheritance");
    attributes.initialized = true;
    if (!UpdateProcThreadAttribute(attributes.value, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   &snapshot.value, sizeof(snapshot.value), nullptr, nullptr))
        win_error("Cannot restrict new-window handle inheritance");
    auto desktop = object_name(GetProcessWindowStation()) + L"\\" + object_name(GetThreadDesktop(GetCurrentThreadId()));
    auto line = quote(exe.native()) + L" --new-window " +
                std::to_wstring(reinterpret_cast<UINT_PTR>(snapshot.value)) + L" " +
                std::to_wstring(content_view.size()) + L" " + quote(config.native());
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.lpDesktop = desktop.data();
    startup.lpAttributeList = attributes.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT,
                        nullptr, nullptr, &startup.StartupInfo, &process))
        win_error("Cannot start another viewer");
    return ViewerProcess(process);
}
} // namespace gvn
