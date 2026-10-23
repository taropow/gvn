#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

std::wstring environment(const wchar_t* name) {
    DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (!length) throw std::runtime_error("Missing receiver environment");
    std::wstring result(length, L'\0');
    DWORD copied = GetEnvironmentVariableW(name, result.data(), length);
    if (!copied || copied >= length) throw std::runtime_error("Receiver environment changed");
    result.resize(copied);
    return result;
}

std::string quote(const std::wstring& wide) {
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (!wide.empty() && !count) throw std::runtime_error("Invalid UTF16");
    std::string bytes(count, '\0');
    if (count && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), bytes.data(), count, nullptr, nullptr)) throw std::runtime_error("UTF8 conversion failed");
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : bytes) {
        if (ch == '\\' || ch == '"') { result += '\\'; result += static_cast<char>(ch); }
        else if (ch < 32) { result += "\\u00"; result += hex[ch >> 4]; result += hex[ch & 15]; }
        else result += static_cast<char>(ch);
    }
    return result + '"';
}

std::wstring object_name(HANDLE handle) {
    DWORD needed{};
    GetUserObjectInformationW(handle, UOI_NAME, nullptr, 0, &needed);
    if (!needed || needed > 65536) throw std::runtime_error("Object name size");
    std::wstring result(needed / sizeof(wchar_t), L'\0');
    if (!GetUserObjectInformationW(handle, UOI_NAME, result.data(), needed, &needed)) throw std::runtime_error("Object name read");
    result.resize(wcslen(result.c_str()));
    return result;
}

DWORD parent_id() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) throw std::runtime_error("Process snapshot failed");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    DWORD result{};
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        if (entry.th32ProcessID == GetCurrentProcessId()) { result = entry.th32ParentProcessID; break; }
    CloseHandle(snapshot);
    if (!result) throw std::runtime_error("Parent not found");
    return result;
}

void binary_file(const fs::path& path, const void* data, size_t count) {
    std::ofstream file(path, std::ios::binary);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(count));
    file.close();
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        USEROBJECTFLAGS station_flags{}; DWORD needed{};
        HWINSTA station = GetProcessWindowStation();
        if (!GetUserObjectInformationW(station, UOI_FLAGS, &station_flags, sizeof(station_flags), &needed) || station_flags.dwFlags & WSF_VISIBLE) return 10;
        fs::path output(environment(L"GVN_OWNED_RECEIVER_OUTPUT"));
        if (!output.is_absolute() || !fs::is_directory(output)) return 11;
        std::wstring token = environment(L"GVN_OWNED_RECEIVER_TOKEN");
        if (token.size() != 32) return 12;
        std::wstring stem = L"receiver-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = DefWindowProcW; wc.lpszClassName = L"GVN.OwnedExternalReceiver";
        if (!RegisterClassW(&wc)) throw std::runtime_error("Receiver window class");
        HWND window = CreateWindowW(wc.lpszClassName, L"Owned external receiver", WS_OVERLAPPEDWINDOW, 0, 0, 320, 200, nullptr, nullptr, instance, nullptr);
        if (!window) throw std::runtime_error("Receiver window creation");
        ShowWindow(window, show); UpdateWindow(window);
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); GetStartupInfoW(&startup);
        int argc{}; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) throw std::runtime_error("Receiver argv");
        DWORD cwd_size = GetCurrentDirectoryW(0, nullptr);
        std::wstring cwd(cwd_size, L'\0');
        DWORD cwd_length = GetCurrentDirectoryW(cwd_size, cwd.data());
        if (!cwd_length || cwd_length >= cwd_size) throw std::runtime_error("Receiver CWD");
        cwd.resize(cwd_length);
        std::wstring module(32768, L'\0');
        DWORD module_length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        if (!module_length || module_length >= module.size()) throw std::runtime_error("Receiver module path");
        module.resize(module_length);
        std::ostringstream json;
        json << "{\"passed\":true,\"pid\":" << GetCurrentProcessId() << ",\"parent_pid\":" << parent_id()
             << ",\"token\":" << quote(token) << ",\"station\":" << quote(object_name(station))
             << ",\"desktop\":" << quote(object_name(GetThreadDesktop(GetCurrentThreadId()))) << ",\"module\":" << quote(module)
             << ",\"station_visible\":false,\"command_line\":" << quote(GetCommandLineW()) << ",\"argv\":[";
        for (int i = 0; i < argc; ++i) { if (i) json << ','; json << quote(argv[i]); }
        LocalFree(argv);
        json << "],\"cwd\":" << quote(cwd) << ",\"show_argument\":" << show << ",\"startup_flags\":" << startup.dwFlags
             << ",\"startup_show\":" << startup.wShowWindow << ",\"iconic\":" << (IsIconic(window) ? "true" : "false")
             << ",\"zoomed\":" << (IsZoomed(window) ? "true" : "false")
             << ",\"clipboard_sequence\":" << GetClipboardSequenceNumber() << ",\"clipboard_formats\":[";
        bool opened{};
        for (int i = 0; i < 100 && !opened; ++i) { opened = OpenClipboard(window) != FALSE; if (!opened) Sleep(10); }
        if (!opened) throw std::runtime_error("Private clipboard open failed");
        try {
            bool first = true;
            const UINT png = RegisterClipboardFormatW(L"PNG");
            for (UINT fmt = EnumClipboardFormats(0); fmt; fmt = EnumClipboardFormats(fmt)) {
                if (!first) json << ',';
                first = false;
                wchar_t name[256]{}; GetClipboardFormatNameW(fmt, name, 256);
                json << "{\"format\":" << fmt << ",\"name\":" << quote(name);
                if (fmt == CF_DIB || fmt == CF_DIBV5 || fmt == CF_UNICODETEXT || fmt == png) {
                    HANDLE data = GetClipboardData(fmt);
                    SIZE_T size = data ? GlobalSize(data) : 0;
                    if (size > 16 * 1024 * 1024) throw std::runtime_error("Synthetic clipboard exceeded budget");
                    void* bytes = data ? GlobalLock(data) : nullptr;
                    if (bytes) {
                        fs::path name_part = stem + L"-clipboard-" + std::to_wstring(fmt) + L".bin";
                        try { binary_file(output / name_part, bytes, size); }
                        catch (...) { GlobalUnlock(data); throw; }
                        GlobalUnlock(data);
                        json << ",\"bytes\":" << size << ",\"file\":" << quote(name_part.wstring());
                    }
                }
                json << '}';
            }
        } catch (...) { CloseClipboard(); throw; }
        CloseClipboard();
        json << "]}\n";
        std::string bytes = json.str();
        fs::path temporary = output / (stem + L".tmp"), final = output / (stem + L".json");
        binary_file(temporary, bytes.data(), bytes.size());
        if (!MoveFileExW(temporary.c_str(), final.c_str(), 0)) throw std::runtime_error("Publish report failed");
        DestroyWindow(window);
        return 0;
    } catch (...) { return 20; }
}
