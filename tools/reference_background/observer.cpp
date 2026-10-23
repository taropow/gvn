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
static decltype(&BeginPaint) originalBeginPaint;
static decltype(&EndPaint) originalEndPaint;
static decltype(&GetDC) originalGetDC;
static decltype(&ReleaseDC) originalReleaseDC;
static HWND captureWindow{};
static HDC captureDc{};
static RECT captureRect{};
static __declspec(thread) PAINTSTRUCT originalPaint;
static __declspec(thread) HWND originalPaintWindow;
static HDC WINAPI owned_begin(HWND window, LPPAINTSTRUCT paint) {
    const auto dc = originalBeginPaint(window, paint);
    if (window == captureWindow && captureDc) {
        originalPaint = *paint;originalPaintWindow = window;
        paint->hdc = captureDc;paint->fErase = TRUE;
        paint->rcPaint = captureRect;
        SelectClipRgn(captureDc, nullptr);
        return captureDc;
    }
    return dc;
}
static BOOL WINAPI owned_end(HWND window, const PAINTSTRUCT *paint) {
    if (window == originalPaintWindow && captureDc && paint->hdc == captureDc) {
        GdiFlush();originalPaintWindow = nullptr;
        return originalEndPaint(window, &originalPaint);
    }
    return originalEndPaint(window, paint);
}
static HDC WINAPI owned_get(HWND window) {
    return window == captureWindow && captureDc ? captureDc : originalGetDC(window);
}
static int WINAPI owned_release(HWND window, HDC dc) {
    return captureDc && dc == captureDc ? 1 : originalReleaseDC(window, dc);
}
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

// Capture only bounded, probe-requested memory-DC sources. No global screen DC.
static unsigned backgroundCount{}, backgroundSequence{}, backgroundUsed{};
static void background_source(HDC source, int sx, int sy, int width, int height) {
    if (width < 32 || height < 32 || width > 1600 || height > 1200 ||
        static_cast<unsigned long long>(width) * height > 1024 * 1024 || backgroundCount >= 1000)
        return;
    wchar_t path[32768];
    swprintf_s(path, L"%sbackground-capture.bin", snapshotFolder);
    HANDLE config = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (config == INVALID_HANDLE_VALUE) return;
    DWORD values[2]{}, read{};
    const bool valid = ReadFile(config, values, sizeof(values), &read, nullptr) && read == sizeof(values);
    CloseHandle(config);
    if (!valid || !values[0] || !values[1] || values[1] > 16) return;
    if (values[0] != backgroundSequence) { backgroundSequence = values[0]; backgroundUsed = 0; }
    if (backgroundUsed >= values[1]) return;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits{};
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC memory = bitmap ? CreateCompatibleDC(nullptr) : nullptr;
    if (!memory) { if (bitmap) DeleteObject(bitmap); return; }
    const auto previous = SelectObject(memory, bitmap);
    const bool copied = originalBlt(memory, 0, 0, width, height, source, sx, sy, SRCCOPY) != FALSE;
    if (copied) {
        const unsigned index = backgroundCount++;
        swprintf_s(path, L"%sbackground-%05u.bmp", snapshotFolder, index);
        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            BITMAPFILEHEADER header{};
            header.bfType = 0x4d42;
            header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
            header.bfSize = header.bfOffBits + width * height * 4;
            DWORD written{};
            WriteFile(file, &header, sizeof(header), &written, nullptr);
            WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr);
            WriteFile(file, bits, width * height * 4, &written, nullptr);
            CloseHandle(file);
            ++backgroundUsed;
            record(9, index, width, height);
            record(10, values[0], sx, sy);
        }
    }
    SelectObject(memory, previous);
    DeleteDC(memory);DeleteObject(bitmap);
}

static BOOL WINAPI blt(HDC out, int x, int y, int width, int height, HDC in, int sx, int sy, DWORD rop) {
    auto result = originalBlt(out, x, y, width, height, in, sx, sy, rop);
    if (result) background_source(in, sx, sy, width, height);
    record(2, width, height, GetPixel(in, sx + width / 2, sy + height / 2));
    return result;
}
static BOOL WINAPI stretch(HDC out, int x, int y, int width, int height, HDC in, int sx, int sy, int sw,
                           int sh, DWORD rop) {
    auto result = originalStretch(out, x, y, width, height, in, sx, sy, sw, sh, rop);
    if (result) background_source(in, sx, sy, sw, sh);
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
            if (!strcmp(n, "BeginPaint")) {
                replacement = (ULONG_PTR)&owned_begin;save = (void **)&originalBeginPaint;
            }
            if (!strcmp(n, "EndPaint")) {
                replacement = (ULONG_PTR)&owned_end;save = (void **)&originalEndPaint;
            }
            if (!strcmp(n, "GetDC")) {
                replacement = (ULONG_PTR)&owned_get;save = (void **)&originalGetDC;
            }
            if (!strcmp(n, "ReleaseDC")) {
                replacement = (ULONG_PTR)&owned_release;save = (void **)&originalReleaseDC;
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
    return original && originalBlt && originalStretch && originalDib && originalDevice ? 0 : 100 + patched;
}
extern "C" DWORD WINAPI Capture(void *) {
    USEROBJECTFLAGS flags{};
    DWORD needed{};
    if (!GetUserObjectInformationW(GetProcessWindowStation(), UOI_FLAGS, &flags, sizeof(flags), &needed) ||
        (flags.dwFlags & WSF_VISIBLE))
        return 20;
    wchar_t path[32768];
    swprintf_s(path, L"%scapture-owned.bin", snapshotFolder);
    HANDLE input = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) return 21;
    DWORD values[7]{}, read{};
    const bool valid = ReadFile(input, values, sizeof(values), &read, nullptr) &&
                       (read == 3 * sizeof(DWORD) || read == sizeof(values));
    CloseHandle(input);
    if (!valid || !values[1] || values[1] > 10000 || values[2] < 1 || values[2] > 4) return 22;
    const HWND window = reinterpret_cast<HWND>(static_cast<ULONG_PTR>(values[0]));
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    RECT client{};
    if (pid != GetCurrentProcessId() || !GetClientRect(window, &client)) return 23;
    RECT region = client;
    if (read == sizeof(values)) {
        // Keep the real client/paint coordinates; allocate only a bounded owned ROI.
        if (values[2] != 4 || !values[5] || !values[6] || values[5] > 1600 || values[6] > 1200 ||
            static_cast<unsigned long long>(values[3]) + values[5] > static_cast<unsigned>(client.right) ||
            static_cast<unsigned long long>(values[4]) + values[6] > static_cast<unsigned>(client.bottom))
            return 24;
        region = {static_cast<LONG>(values[3]), static_cast<LONG>(values[4]),
                  static_cast<LONG>(values[3] + values[5]), static_cast<LONG>(values[4] + values[6])};
    }
    const int width = region.right - region.left, height = region.bottom - region.top;
    if (client.right <= 0 || client.bottom <= 0 || width <= 0 || height <= 0 || width > 1600 || height > 1200)
        return 24;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;info.bmiHeader.biBitCount = 32;
    void *bits{};
    HDC memory = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memory || !bitmap || !bits) { if (bitmap) DeleteObject(bitmap); if (memory) DeleteDC(memory); return 25; }
    const auto previous = SelectObject(memory, bitmap);
    SetViewportOrgEx(memory, -region.left, -region.top, nullptr);
    memset(bits, 0xcc, width * height * 4);
    DWORD_PTR reply{};
    bool painted{};
    if (values[2] == 1)
        painted = SendMessageTimeoutW(window, WM_PRINT, reinterpret_cast<WPARAM>(memory),
                                       PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND,
                                       SMTO_ABORTIFHUNG, 3000, &reply) != 0;
    else if (values[2] == 2)
        painted = PrintWindow(window, memory, PW_CLIENTONLY | 2) != FALSE;
    else if (values[2] == 3) {
        SendMessageTimeoutW(window, WM_ERASEBKGND, reinterpret_cast<WPARAM>(memory), 0,
                            SMTO_ABORTIFHUNG, 3000, &reply);
        painted = SendMessageTimeoutW(window, WM_PAINT, reinterpret_cast<WPARAM>(memory), 0,
                                       SMTO_ABORTIFHUNG, 3000, &reply) != 0;
    } else {
        if (!originalBeginPaint || !originalEndPaint) {
            SelectObject(memory, previous);DeleteObject(bitmap);DeleteDC(memory);return 28;
        }
        captureWindow = window;captureDc = memory;captureRect = region;
        InvalidateRect(window, &region, TRUE);
        painted = SendMessageTimeoutW(window, WM_PAINT, 0, 0, SMTO_ABORTIFHUNG, 3000, &reply) != 0;
        captureDc = nullptr;captureWindow = nullptr;captureRect = {};
    }
    GdiFlush();
    swprintf_s(path, L"%sowned-%05lu-mode%lu.bmp", snapshotFolder, values[1], values[2]);
    HANDLE output = painted ? CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                          FILE_ATTRIBUTE_NORMAL, nullptr) : INVALID_HANDLE_VALUE;
    DWORD result = output == INVALID_HANDLE_VALUE ? 26 : 0;
    if (!result) {
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + width * height * 4;
        DWORD written{};
        if (!WriteFile(output, &header, sizeof(header), &written, nullptr) ||
            !WriteFile(output, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr) ||
            !WriteFile(output, bits, width * height * 4, &written, nullptr)) result = 27;
        CloseHandle(output);
    }
    SelectObject(memory, previous);DeleteObject(bitmap);DeleteDC(memory);
    return result;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}
