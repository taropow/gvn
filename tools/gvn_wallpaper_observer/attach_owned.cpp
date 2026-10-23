#include <windows.h>
#include <cstdio>
#include <tlhelp32.h>
static_assert(sizeof(void *)==8,"This controller only attaches its owned x64 child");
int wmain(int argc, wchar_t **argv) {
    if (argc != 4 && (argc != 5 || wcscmp(argv[4], L"capture")))
        return 1;
    DWORD pid = wcstoul(argv[1], nullptr, 10);
    // The controller and reference must be siblings created by the same live probe.
    DWORD myParent = 0, targetParent = 0;
    PROCESSENTRY32W item{};
    item.dwSize = sizeof(item);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 10;
    if (Process32FirstW(snapshot, &item))
        do {
            if (item.th32ProcessID == GetCurrentProcessId())
                myParent = item.th32ParentProcessID;
            if (item.th32ProcessID == pid)
                targetParent = item.th32ParentProcessID;
        } while (Process32NextW(snapshot, &item));
    CloseHandle(snapshot);
    if (!myParent || myParent != targetParent)
        return 11;
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_LIMITED_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
                                 FALSE, pid);
    if (!process)
        return 2;
    wchar_t path[32768];
    DWORD size = 32768;
    if (!QueryFullProcessImageNameW(process, 0, path, &size) || _wcsicmp(path, argv[2]))
        return 3;
    BOOL wow64{};
    if (!IsWow64Process(process,&wow64) || wow64 || GetFileAttributesW(argv[3])==INVALID_FILE_ATTRIBUTES)
        return 12;
    size = (DWORD)((wcslen(argv[3]) + 1) * sizeof(wchar_t));
    void *remote = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(process, remote, argv[3], size, nullptr))
        return 4;
    auto loader = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loader, remote, 0, nullptr);
    if (!thread || WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0)
        return 5;
    DWORD loader_result{};
    GetExitCodeThread(thread, &loader_result);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    // Thread exit values are DWORD. Discover the full x64 module base from this owned child.
    ULONG_PTR base{};MODULEENTRY32W module{};module.dwSize=sizeof(module);
    snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);
    if (snapshot==INVALID_HANDLE_VALUE) return 13;
    if (Module32FirstW(snapshot,&module)) do {
        if (!_wcsicmp(module.szExePath,argv[3])) {base=reinterpret_cast<ULONG_PTR>(module.modBaseAddr);break;}
    } while (Module32NextW(snapshot,&module));
    CloseHandle(snapshot);
    if (!base) return 6;
    HMODULE local = LoadLibraryExW(argv[3], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!local)
        return 7;
    auto install = GetProcAddress(local, argc == 5 ? "Capture" : "Install");
    if (!install)
        return 8;
    auto offset = (ULONG_PTR)install - (ULONG_PTR)local;
    thread =
        CreateRemoteThread(process, nullptr, 0, (LPTHREAD_START_ROUTINE)(base + offset), nullptr, 0, nullptr);
    if (!thread || WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0)
        return 9;
    DWORD result{};
    GetExitCodeThread(thread, &result);
    CloseHandle(thread);
    CloseHandle(process);
    FreeLibrary(local);
    printf("owned GVN wallpaper hook: %lu\n", result);
    return (int)result;
}
