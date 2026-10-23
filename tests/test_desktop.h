#pragma once
#include <windows.h>

namespace gvn_test {
// Anonymous stations are named from the logon session and can be shared by separate test processes.
// Serialize their clipboard use, including the optional external reference probe.
class ClipboardLock {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\GVN.NoninteractiveClipboard");
    bool held{};

  public:
    ClipboardLock() {
        if (mutex) {
            const auto result = WaitForSingleObject(mutex, 30000);
            held = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
    }
    ~ClipboardLock() {
        if (held)
            ReleaseMutex(mutex);
        if (mutex)
            CloseHandle(mutex);
    }
    explicit operator bool() const {
        return held;
    }
    ClipboardLock(const ClipboardLock &) = delete;
    ClipboardLock &operator=(const ClipboardLock &) = delete;
};
inline bool noninteractive(HWINSTA station) {
    USEROBJECTFLAGS flags{};
    DWORD needed{};
    return GetUserObjectInformationW(station, UOI_FLAGS, &flags, sizeof(flags), &needed) &&
           !(flags.dwFlags & WSF_VISIBLE);
}
} // namespace gvn_test
