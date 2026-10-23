// Normal GVN + genuine DefWindowProc move loops on a private window station.
#include "gvn/settings.h"
#include "gvn/codecs.h"
#include "../tests/test_desktop.h"
#include <windowsx.h>
#include <array>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
using namespace gvn;
namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}
template<class Ready> bool wait_for(Ready ready, DWORD timeout) {
    const auto end = GetTickCount64() + timeout;
    do { pump(); if (ready()) return true; Sleep(5); } while (GetTickCount64() < end);
    return false;
}
RECT rectangle(HWND window) { RECT r{}; require(GetWindowRect(window, &r), "Cannot read owned rectangle"); return r; }
std::wstring object_name(HANDLE object) {
    DWORD bytes{}; GetUserObjectInformationW(object, UOI_NAME, nullptr, 0, &bytes);
    require(bytes && bytes < 32768, "Cannot read private desktop name");
    std::wstring result(bytes / sizeof(wchar_t), L'\0');
    require(GetUserObjectInformationW(object, UOI_NAME, result.data(), bytes, &bytes), "Cannot read private desktop name");
    result.resize(wcslen(result.c_str())); return result;
}
struct Move {
    HWND window{};
    RECT before{}, after{};
    unsigned starts{}, ends{}, keys{};
    bool observed_loop{}, observed_displacement{}, timed_out{}, cursor_seeded{};
};
Move *current{};
void CALLBACK event(HWINEVENTHOOK, DWORD id, HWND window, LONG, LONG, DWORD, DWORD) {
    if (current && current->window == window) {
        if (id == EVENT_SYSTEM_MOVESIZESTART) ++current->starts;
        if (id == EVENT_SYSTEM_MOVESIZEEND) ++current->ends;
    }
}
struct InputQueue {
    DWORD target{};
    BYTE before[256]{};
    bool attached{};
    InputQueue(DWORD thread, UINT mouse, bool ctrl) : target(thread) {
        attached = AttachThreadInput(GetCurrentThreadId(), target, TRUE) != FALSE;
        require(attached, "Cannot join the owned private input queue");
        require(GetKeyboardState(before), "Cannot read private keyboard state");
        BYTE state[256]{};
        state[mouse == WM_MBUTTONDOWN ? VK_MBUTTON : mouse == WM_RBUTTONDOWN ? VK_RBUTTON : VK_LBUTTON] = 128;
        if (ctrl) state[VK_CONTROL] = state[VK_LCONTROL] = 128;
        require(SetKeyboardState(state), "Cannot seed the private input queue");
    }
    ~InputQueue() { if (attached) { SetKeyboardState(before); AttachThreadInput(GetCurrentThreadId(), target, FALSE); } }
};
Move move(HWND target, UINT mouse, POINT client, POINT screen, bool direct, bool cancel, bool ctrl) {
    DWORD pid{}; const auto thread = GetWindowThreadProcessId(target, &pid);
    require(pid && thread && thread != GetCurrentThreadId(), "Move target must be an owned GUI thread");
    InputQueue input(thread, mouse, ctrl);
    Move result; result.window = target; result.before = rectangle(target);
    const auto hook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND,
                                     nullptr, event, pid, thread, WINEVENT_OUTOFCONTEXT);
    require(hook != nullptr, "Cannot observe the owned move loop");
    POINT old_cursor{}; GetCursorPos(&old_cursor);
    result.cursor_seeded = SetCursorPos(screen.x, screen.y) != FALSE; // Private station only.
    current = &result;
    PostMessageW(target, direct ? WM_NCLBUTTONDOWN : mouse, direct ? HTCAPTION :
                 mouse == WM_MBUTTONDOWN ? MK_MBUTTON : mouse == WM_RBUTTONDOWN ? MK_RBUTTON : MK_LBUTTON,
                 MAKELPARAM(direct ? screen.x : client.x, direct ? screen.y : client.y));
    const std::array<UINT, 4> keys{VK_RIGHT, VK_RIGHT, VK_DOWN, UINT(cancel ? VK_ESCAPE : VK_RETURN)};
    const auto start = GetTickCount64(); auto next = start + 30;
    const bool finished = wait_for([&] {
        GUITHREADINFO info{sizeof(info)};
        require(GetGUIThreadInfo(thread, &info), "Cannot read owned GUI state");
        const bool active = (info.flags & GUI_INMOVESIZE) && info.hwndMoveSize == target;
        result.observed_loop |= active;
        const auto r = rectangle(target);
        result.observed_displacement |= r.left != result.before.left || r.top != result.before.top;
        if (active && result.keys < keys.size() && GetTickCount64() >= next) {
            const auto key = keys[result.keys++];
            PostMessageW(target, WM_KEYDOWN, key, 1 | (MapVirtualKeyW(key, MAPVK_VK_TO_VSC) << 16));
            PostMessageW(target, WM_KEYUP, key, 0xc0000001u | (MapVirtualKeyW(key, MAPVK_VK_TO_VSC) << 16));
            next = GetTickCount64() + 40;
        }
        if (result.observed_loop && !active && result.keys == keys.size()) return true;
        if (!result.observed_loop && GetTickCount64() - start > 400) return true;
        return false;
    }, 2000);
    result.timed_out = !finished;
    if (!finished) {
        PostMessageW(target, WM_CANCELMODE, 0, 0); PostMessageW(target, WM_KEYDOWN, VK_ESCAPE, 1);
        wait_for([&] { GUITHREADINFO info{sizeof(info)}; return GetGUIThreadInfo(thread, &info) && !(info.flags & GUI_INMOVESIZE); }, 1000);
    }
    wait_for([&] { return result.ends != 0; }, 100);
    result.after = rectangle(target);
    current = nullptr; UnhookWinEvent(hook);
    if (result.cursor_seeded) SetCursorPos(old_cursor.x, old_cursor.y);
    PostMessageW(target, mouse == WM_MBUTTONDOWN ? WM_MBUTTONUP : mouse == WM_RBUTTONDOWN ? WM_RBUTTONUP : WM_LBUTTONUP,
                 0, MAKELPARAM(client.x, client.y));
    return result;
}
LRESULT CALLBACK reference_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}
struct Reference {
    std::thread thread;
    HWND window{};
    Reference() {
        std::promise<HWND> ready; auto future = ready.get_future();
        const auto desktop = GetThreadDesktop(GetCurrentThreadId());
        thread = std::thread([&ready, desktop] {
            if (!SetThreadDesktop(desktop)) { ready.set_value(nullptr); return; }
            WNDCLASSW c{}; c.lpfnWndProc = reference_proc; c.hInstance = GetModuleHandleW(nullptr);
            c.lpszClassName = L"GVN.PrivateMoveReference"; RegisterClassW(&c);
            const auto w = CreateWindowW(c.lpszClassName, L"GVN owned move reference", WS_OVERLAPPEDWINDOW,
                                         200, 200, 800, 600, nullptr, nullptr, c.hInstance, nullptr);
            ready.set_value(w);
            if (!w) return;
            MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
        });
        window = future.get();
        if (!window) { thread.join(); throw std::runtime_error("Cannot create reference window"); }
    }
    ~Reference() { if (window) PostMessageW(window, WM_CLOSE, 0, 0); if (thread.joinable()) thread.join(); }
};
HWND main_window(DWORD pid) {
    struct Search { DWORD pid; HWND found; } s{pid, nullptr};
    EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()), [](HWND w, LPARAM data) -> BOOL {
        auto &s = *reinterpret_cast<Search *>(data); DWORD p{}; GetWindowThreadProcessId(w, &p);
        wchar_t name[100]{}; GetClassNameW(w, name, 100);
        if (p == s.pid && !GetWindow(w, GW_OWNER) && (GetWindowLongPtrW(w, GWL_STYLE) & WS_VISIBLE) &&
            wcscmp(name, L"GVN.ImageViewer") == 0) { s.found = w; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&s));
    return s.found;
}
struct Viewer {
    PROCESS_INFORMATION process{};
    HWND window{};
    bool graceful{};
    Viewer(const std::filesystem::path &exe, const std::filesystem::path &profile,
           const std::filesystem::path &image, HANDLE job) {
        auto command = L"\"" + exe.wstring() + L"\" --settings \"" + profile.wstring() + L"\" \"" + image.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_SHOWNORMAL;
        auto desktop = object_name(GetProcessWindowStation()) + L"\\" + object_name(GetThreadDesktop(GetCurrentThreadId()));
        startup.lpDesktop = desktop.data();
        require(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                                nullptr, profile.parent_path().c_str(), &startup, &process), "Cannot launch owned GVN");
        if (!AssignProcessToJobObject(job, process.hProcess)) {
            TerminateProcess(process.hProcess, 1); CloseHandle(process.hThread); CloseHandle(process.hProcess);
            process = {}; throw std::runtime_error("Cannot contain the owned GVN process");
        }
        ResumeThread(process.hThread);
        if (!wait_for([&] { window = main_window(process.dwProcessId); return window != nullptr; }, 5000)) {
            DWORD code{}; GetExitCodeProcess(process.hProcess, &code);
            std::cerr << "Owned GVN PID=" << process.dwProcessId << " exit=" << code << " desktop=";
            std::wcerr << desktop << L'\n';
            EnumDesktopWindows(GetThreadDesktop(GetCurrentThreadId()), [](HWND w, LPARAM pid) -> BOOL {
                DWORD p{}; const auto thread = GetWindowThreadProcessId(w, &p);
                if (p == DWORD(pid)) {
                    wchar_t name[100]{}; GetClassNameW(w, name, 100);
                    std::wcerr << L"Owned class=" << name << L" visible=" << IsWindowVisible(w)
                               << L" visible_style=" << bool(GetWindowLongPtrW(w, GWL_STYLE) & WS_VISIBLE)
                               << L" UIthread=" << thread << L" owner=" << GetWindow(w, GW_OWNER) << L'\n';
                }
                return TRUE;
            }, process.dwProcessId);
            std::cerr << "Diagnostic hold: owned PID remains alive for 30 seconds\n" << std::flush;
            wait_for([&] { return WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0; }, 30000);
            throw std::runtime_error("GVN main window missing");
        }
    }
    void close() {
        PostMessageW(window, WM_CANCELMODE, 0, 0); PostMessageW(window, WM_CLOSE, 0, 0);
        require(wait_for([&] { return WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0; }, 5000), "Owned GVN did not close");
        DWORD code{}; GetExitCodeProcess(process.hProcess, &code); require(code == 0, "Owned GVN exit failed");
        graceful = true;
    }
    ~Viewer() {
        if (process.hProcess) {
            if (!graceful) { PostMessageW(window, WM_CANCELMODE, 0, 0); PostMessageW(window, WM_CLOSE, 0, 0); if (WaitForSingleObject(process.hProcess, 1000) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1); }
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
        }
    }
};
std::vector<uint8_t> png() {
    std::vector<uint8_t> result;
    if (!OpenClipboard(nullptr)) return result;
    const auto handle = GetClipboardData(RegisterClipboardFormatW(L"PNG"));
    if (handle) {
        const auto size = GlobalSize(handle); const auto data = static_cast<const uint8_t *>(GlobalLock(handle));
        if (data && size && size < 16 * 1024 * 1024) result.assign(data, data + size);
        if (data) GlobalUnlock(handle);
    }
    CloseClipboard(); return result;
}
std::vector<uint8_t> copy(Viewer &viewer) {
    std::vector<uint8_t> data;
    require(wait_for([&] {
        PostMessageW(viewer.window, WM_COMMAND, Copy, 0); Sleep(10); pump();
        DWORD owner{}; GetWindowThreadProcessId(GetClipboardOwner(), &owner);
        data = png(); return owner == viewer.process.dwProcessId && !data.empty();
    }, 5000), "Owned GVN clipboard copy failed");
    return data;
}
void write_move(std::ostream &out, const Move &m) {
    out << "{\"dx\":" << m.after.left - m.before.left << ",\"dy\":" << m.after.top - m.before.top
        << ",\"starts\":" << m.starts << ",\"ends\":" << m.ends << ",\"keys\":" << m.keys
        << ",\"observed_loop\":" << m.observed_loop << ",\"observed_displacement\":" << m.observed_displacement
        << ",\"timeout\":" << m.timed_out << ",\"private_cursor_seeded\":" << m.cursor_seeded << '}';
}
}
int wmain(int argc, wchar_t **argv) {
    try {
        require(argc == 4, "Expected executable, owned output and fixture");
        const auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        require(station && gvn_test::noninteractive(station) && SetProcessWindowStation(station), "Cannot create private station");
        const auto desktop_name = L"GVN-WindowMove-" + std::to_wstring(GetCurrentProcessId());
        const auto desktop = CreateDesktopW(desktop_name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        require(desktop && SetThreadDesktop(desktop), "Cannot create private desktop");
        gvn_test::ClipboardLock clipboard; require(bool(clipboard), "Private clipboard lock failed");
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const auto exe = std::filesystem::absolute(argv[1]), out = std::filesystem::absolute(argv[2]), fixture = std::filesystem::absolute(argv[3]);
        require(!std::filesystem::exists(out), "Output must be a new owned directory"); std::filesystem::create_directories(out);
        const auto image = out / L"alpha.png"; std::filesystem::copy_file(fixture, image);
        const auto expected = load_image(image);
        const auto job = CreateJobObjectW(nullptr, nullptr); require(job != nullptr, "Cannot create owned job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        require(SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)), "Cannot constrain child lifetime");
        Reference reference;
        std::ofstream report(out / L"native.json"); report << std::boolalpha << "{\"cases\":[";
        bool all = true;
        for (unsigned mode = 0; mode < 4; ++mode) {
            Settings settings; settings.allow_multiple = true; settings.reopen_last = false; settings.confirm_unsaved = false;
            settings.prefetch_enabled = false; settings.startup_position = 2; settings.window_x = 200; settings.window_y = 200;
            settings.window_width = 800; settings.window_height = 600; settings.caption = mode != 1 && mode != 2;
            const auto mouse = WM_LBUTTONDOWN;
            if (mode != 2) settings.mouse.assign(MouseGesture::LeftDrag, 0, 0, MouseWindow);
            const auto profile = out / (L"viewer-" + std::to_wstring(mode) + L".ini"); settings.save(profile);
            Viewer viewer(exe, profile, image, job);
            const auto before = copy(viewer); require(decode(before).frames[0].pixels == expected.frames[0].pixels, "Initial alpha differs");
            const auto original = rectangle(viewer.window);
            SetWindowLongPtrW(reference.window, GWL_STYLE, GetWindowLongPtrW(viewer.window, GWL_STYLE));
            SetWindowPos(reference.window, nullptr, original.left, original.top, original.right - original.left,
                         original.bottom - original.top, SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            POINT client{80, 80}, screen = client; ClientToScreen(viewer.window, &screen);
            const auto baseline = move(reference.window, mouse, client, screen, true, mode == 3, mode == 2);
            const auto actual = move(viewer.window, mouse, client, screen, false, mode == 3, mode == 2);
            const bool same_size = actual.before.right - actual.before.left == actual.after.right - actual.after.left &&
                                   actual.before.bottom - actual.before.top == actual.after.bottom - actual.after.top;
            const auto after = copy(viewer);
            const bool pixels = after == before && decode(after).frames[0].pixels == expected.frames[0].pixels;
            const bool passed = baseline.observed_loop && actual.observed_loop && !baseline.timed_out && !actual.timed_out &&
                baseline.keys == 4 && actual.keys == 4 && baseline.observed_displacement && actual.observed_displacement &&
                baseline.after.left - baseline.before.left == actual.after.left - actual.before.left &&
                baseline.after.top - baseline.before.top == actual.after.top - actual.before.top && same_size && pixels &&
                (mode == 3 ? actual.after.left == actual.before.left && actual.after.top == actual.before.top :
                             actual.after.left != actual.before.left || actual.after.top != actual.before.top);
            viewer.close();
            if (mode) report << ',';
            report << "{\"mode\":" << mode << ",\"passed\":" << passed << ",\"alpha_unchanged\":" << pixels
                   << ",\"size_unchanged\":" << same_size << ",\"reference\":"; write_move(report, baseline);
            report << ",\"gvn\":"; write_move(report, actual); report << '}'; report.flush();
            std::cout << "mode " << mode << " passed=" << passed << " reference=" << baseline.after.left - baseline.before.left << ',' << baseline.after.top - baseline.before.top
                      << " GVN=" << actual.after.left - actual.before.left << ',' << actual.after.top - actual.before.top << '\n';
            all &= passed;
        }
        CloseHandle(job);
        report << "],\"passed\":" << all << ",\"private_station\":true,\"normal_gvn_process\":true,\"move_result_substitutions\":0,\"scripted_private_input_queue\":true,\"user_desktop_or_clipboard_used\":false}\n";
        return all ? 0 : 1;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
