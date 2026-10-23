#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <iostream>
int wmain(int argc, wchar_t **argv) {
    if (argc != 4)
        return 2;
    const DWORD process_id = wcstoul(argv[1], nullptr, 10), thread_id = wcstoul(argv[2], nullptr, 10);
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id);
    if (!process)
        return 3;
    wchar_t path[32768]{};
    DWORD length = 32768;
    if (!QueryFullProcessImageNameW(process, 0, path, &length) || _wcsicmp(path, argv[3]))
        return 4;
    HANDLE thread =
        OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION, FALSE, thread_id);
    if (!thread || GetProcessIdOfThread(thread) != process_id)
        return 5;
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS);
    SymInitialize(process, "", TRUE);
    if (SuspendThread(thread) == static_cast<DWORD>(-1))
        return 6;
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    const BOOL captured = GetThreadContext(thread, &context);
    if (!captured) {
        ResumeThread(thread);
        return 7;
    }
    STACKFRAME64 frame{};
    frame.AddrPC = {context.Rip, 0, AddrModeFlat};
    frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
    frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
    for (int i = 0; i < 24; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        const auto base = SymGetModuleBase64(process, frame.AddrPC.Offset);
        wchar_t module[512]{};
        GetModuleBaseNameW(process, reinterpret_cast<HMODULE>(base), module, 512);
        char storage[sizeof(SYMBOL_INFO) + 1024]{};
        auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 1024;
        DWORD64 offset{};
        std::wcout << module << L"+0x" << std::hex << frame.AddrPC.Offset - base;
        std::wcout << L" ";
        if (SymFromAddr(process, frame.AddrPC.Offset, &offset, symbol))
            std::cout << symbol->Name << "+0x" << offset;
        std::cout << "\n";
    }
    ResumeThread(thread);
    SymCleanup(process);
    CloseHandle(thread);
    CloseHandle(process);
}
