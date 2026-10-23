// Test-only observer for one probe-owned MassiGra child. No clock replacement.
#include <windows.h>
#include <cstring>
#include <cstdio>
static HANDLE logFile = INVALID_HANDLE_VALUE;
using Timer = UINT_PTR(WINAPI *)(HWND, UINT_PTR, UINT, TIMERPROC);
static Timer original;
static decltype(&BitBlt) originalBlt;
static decltype(&StretchBlt) originalStretch;
static decltype(&StretchDIBits) originalDib;
static decltype(&SetDIBitsToDevice) originalDevice;
static wchar_t snapshotFolder[32768];
static unsigned snapshotCount{};
static void record(int kind, int width, int height, COLORREF color) {
    LARGE_INTEGER tick, frequency;
    QueryPerformanceCounter(&tick);
    QueryPerformanceFrequency(&frequency);
    char line[192];
    int size = sprintf_s(line, "%.4f,%lu,%d,%d,%d,%lu\n", 1000.0 * tick.QuadPart / frequency.QuadPart,
                         GetCurrentThreadId(), kind, width, height, color);
    DWORD written;
    WriteFile(logFile, line, size, &written, nullptr);
}
static BOOL WINAPI blt(HDC out, int x, int y, int width, int height, HDC in, int sx, int sy, DWORD rop) {
    auto result = originalBlt(out, x, y, width, height, in, sx, sy, rop);
    record(2, width, height, GetPixel(in, sx + width / 2, sy + height / 2));
    return result;
}
static BOOL WINAPI stretch(HDC out, int x, int y, int width, int height, HDC in, int sx, int sy, int sw,
                           int sh, DWORD rop) {
    auto result = originalStretch(out, x, y, width, height, in, sx, sy, sw, sh, rop);
    record(3, sw, sh, GetPixel(in, sx + sw / 2, sy + sh / 2));
    return result;
}
static COLORREF dibcolor(const void *data, const BITMAPINFO *info) {
    const auto &h = info->bmiHeader;
    if (!data || h.biCompression != BI_RGB || h.biWidth < 1 || !h.biHeight)
        return CLR_INVALID;
    auto width = h.biWidth;
    auto height = abs(h.biHeight);
    auto x = width / 2, y = height / 2;
    auto row = (const unsigned char *)data + ((width * h.biBitCount + 31) / 32 * 4) * y;
    if (h.biBitCount == 24 || h.biBitCount == 32) {
        auto pixel = row + x * (h.biBitCount / 8);
        return RGB(pixel[2], pixel[1], pixel[0]);
    }
    if (h.biBitCount == 1 || h.biBitCount == 4 || h.biBitCount == 8) {
        const auto index = h.biBitCount == 8 ? row[x] : h.biBitCount == 4 ? (row[x / 2] >> (x % 2 ? 0 : 4)) & 15 : (row[x / 8] >> (7 - x % 8)) & 1;
        const auto count = h.biClrUsed ? h.biClrUsed : 1u << h.biBitCount;
        if (static_cast<unsigned>(index) >= count) return CLR_INVALID;
        const auto palette = reinterpret_cast<const RGBQUAD *>(reinterpret_cast<const unsigned char *>(info) + h.biSize);
        const auto color = palette[index];
        return RGB(color.rgbRed, color.rgbGreen, color.rgbBlue);
    }
    return CLR_INVALID;
}

// Optional, test-owned rendering cost. No clock API is replaced.
static void controlled_render_cost(const void *bits, const BITMAPINFO *info) {
    if (info->bmiHeader.biWidth != 64 || abs(info->bmiHeader.biHeight) != 64)
        return;
    wchar_t path[32768];
    swprintf_s(path, L"%sstall-config.bin", snapshotFolder);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD values[3]{}, count{};
    const bool read = ReadFile(file, values, sizeof(values), &count, nullptr) && count == sizeof(values);
    CloseHandle(file);
    static DWORD applied{};
    const auto color = dibcolor(bits, info);
    if (!read || !values[0] || values[0] == applied || !values[1] || values[1] > 1000 || values[2] != color)
        return;
    applied = values[0];
    record(7, values[1], values[0], color);
    Sleep(values[1]);
    record(8, values[1], values[0], color);
}

static void dump(const void *bits, const BITMAPINFO *info) {
    if (!bits || !info) return;
    const auto &h = info->bmiHeader;
    if (h.biSize < 40 || h.biSize > 124 || h.biWidth < 1 || h.biWidth > 16 ||
        h.biHeight < -16 || h.biHeight > 16 || !h.biHeight || h.biCompression != BI_RGB ||
        (h.biBitCount != 1 && h.biBitCount != 4 && h.biBitCount != 8 && h.biBitCount != 24 && h.biBitCount != 32) ||
        snapshotCount >= 2000) return;
    DWORD colors = h.biBitCount <= 8 ? (h.biClrUsed ? h.biClrUsed : 1u << h.biBitCount) : 0;
    if (colors > 256 || (h.biBitCount <= 8 && colors > (1u << h.biBitCount))) return;
    DWORD paletteBytes = colors * sizeof(RGBQUAD);
    DWORD bytes = ((h.biWidth * h.biBitCount + 31) / 32 * 4) * abs(h.biHeight);
    wchar_t path[32768];
    unsigned index = snapshotCount++;
    swprintf_s(path, L"%ssnapshot-%05u.bmp", snapshotFolder, index);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + h.biSize + paletteBytes;
    header.bfSize = header.bfOffBits + bytes;
    DWORD written;
    WriteFile(f, &header, sizeof(header), &written, nullptr);
    WriteFile(f, info, h.biSize + paletteBytes, &written, nullptr);
    WriteFile(f, bits, bytes, &written, nullptr);
    CloseHandle(f);
    record(6, index, h.biBitCount, dibcolor(bits, info));
}

static int WINAPI dib(HDC out, int x, int y, int w, int h, int sx, int sy, int sw, int sh, const void *bits,
                      const BITMAPINFO *info, UINT usage, DWORD rop) {
    auto result = originalDib(out, x, y, w, h, sx, sy, sw, sh, bits, info, usage, rop);
    record(4, info->bmiHeader.biWidth, info->bmiHeader.biHeight, dibcolor(bits, info));
    dump(bits, info);
    controlled_render_cost(bits, info);
    return result;
}
static int WINAPI device(HDC out, int x, int y, DWORD w, DWORD h, int sx, int sy, UINT start, UINT lines,
                         const void *bits, const BITMAPINFO *info, UINT usage) {
    auto result = originalDevice(out, x, y, w, h, sx, sy, start, lines, bits, info, usage);
    record(5, info->bmiHeader.biWidth, info->bmiHeader.biHeight, dibcolor(bits, info));
    dump(bits, info);
    controlled_render_cost(bits, info);
    return result;
}
static UINT_PTR WINAPI observed(HWND hwnd, UINT_PTR id, UINT delay, TIMERPROC proc) {
    auto result = original(hwnd, id, delay, proc);
    char line[160];
    int size = sprintf_s(line, "%llu,%lu,%u,%u,%u,%u\n", GetTickCount64(), GetCurrentThreadId(),
                         (unsigned)(UINT_PTR)hwnd, (unsigned)id, delay, (unsigned)result);
    DWORD written;
    WriteFile(logFile, line, size, &written, nullptr);
    return result;
}
extern "C" DWORD WINAPI Install(void *) {
    wchar_t path[32768];
    HMODULE own{};
    GetModuleHandleExW(6, (LPCWSTR)&Install, &own);
    GetModuleFileNameW(own, path, 32768);
    auto end = wcsrchr(path, L'\\');
    if (!end)
        return 1;
    *(end + 1) = 0;
    wcscpy_s(snapshotFolder, path);
    wcscpy_s(end + 1, 32768 - (end + 1 - path), L"timers.csv");
    logFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                          nullptr);
    if (logFile == INVALID_HANDLE_VALUE)
        return 2;
    auto base = (unsigned char *)GetModuleHandleW(nullptr);
    auto dos = (IMAGE_DOS_HEADER *)base;
    auto nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    auto imports =
        (IMAGE_IMPORT_DESCRIPTOR *)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
                                               .VirtualAddress);
    int patched = 0;
    for (; imports->Name; ++imports) {
        auto names = (IMAGE_THUNK_DATA *)(base + imports->OriginalFirstThunk);
        auto addresses = (IMAGE_THUNK_DATA *)(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            auto name = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            auto n = (char *)name->Name;
            ULONG_PTR replacement{};
            void **save{};
            if (!strcmp(n, "SetTimer")) {
                replacement = (ULONG_PTR)&observed;
                save = (void **)&original;
            }
            if (!strcmp(n, "BitBlt")) {
                replacement = (ULONG_PTR)&blt;
                save = (void **)&originalBlt;
            }
            if (!strcmp(n, "StretchBlt")) {
                replacement = (ULONG_PTR)&stretch;
                save = (void **)&originalStretch;
            }
            if (!strcmp(n, "StretchDIBits")) {
                replacement = (ULONG_PTR)&dib;
                save = (void **)&originalDib;
            }
            if (!strcmp(n, "SetDIBitsToDevice")) {
                replacement = (ULONG_PTR)&device;
                save = (void **)&originalDevice;
            }
            if (!replacement)
                continue;
            DWORD old;
            if (!VirtualProtect(&addresses->u1.Function, sizeof(void *), PAGE_READWRITE, &old))
                return 3;
            *save = (void *)addresses->u1.Function;
            addresses->u1.Function = replacement;
            DWORD unused;
            VirtualProtect(&addresses->u1.Function, sizeof(void *), old, &unused);
            ++patched;
        }
    }
    return patched == 5 ? 0 : 100 + patched;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}
