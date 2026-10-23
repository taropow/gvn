// Read-only API/dispatch observer for our pinned x86 reference child. No clock replacement.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
static_assert(sizeof(void*)==4,"Official0.45 reference is x86");
static HANDLE output=INVALID_HANDLE_VALUE;
static SRWLOCK lock=SRWLOCK_INIT;
static WNDPROC original{};
static HWND main_window{};
static thread_local UINT command{};
static void log(const char *api,ULONG_PTR window=0,UINT_PTR identity=0,UINT interval=0,ULONG_PTR result=0) {
 char line[320]{};const int count=sprintf_s(line,"{\"api\":\"%s\",\"window\":%llu,\"id\":%llu,\"interval\":%u,\"result\":%llu,\"command\":%u}\n",api,static_cast<unsigned long long>(window),static_cast<unsigned long long>(identity),interval,static_cast<unsigned long long>(result),command);
 AcquireSRWLockExclusive(&lock);DWORD written{};WriteFile(output,line,count,&written,nullptr);FlushFileBuffers(output);ReleaseSRWLockExclusive(&lock);
}
static UINT_PTR WINAPI timer(HWND window,UINT_PTR identity,UINT interval,TIMERPROC procedure) {
 const auto result=SetTimer(window,identity,interval,procedure);log("SetTimer",reinterpret_cast<ULONG_PTR>(window),identity,interval,result);return result;
}
static BOOL WINAPI kill(HWND window,UINT_PTR identity) {
 const auto result=KillTimer(window,identity);log("KillTimer",reinterpret_cast<ULONG_PTR>(window),identity,0,result);return result;
}
static LRESULT CALLBACK observe(HWND window,UINT message,WPARAM w,LPARAM l) {
 if (message==WM_APP+77 && w==GetCurrentProcessId()) {log("queue-barrier",reinterpret_cast<ULONG_PTR>(window),l);return 0;}
 if (message!=WM_COMMAND) return CallWindowProcW(original,window,message,w,l);
 const auto prior=command;command=LOWORD(w);log("command-begin",reinterpret_cast<ULONG_PTR>(window),command);
 const auto result=CallWindowProcW(original,window,message,w,l);log("command-end",reinterpret_cast<ULONG_PTR>(window),command);command=prior;return result;
}
extern "C" DWORD WINAPI Install(void*) {
 USEROBJECTFLAGS flags{};DWORD needed{};
 if (!GetUserObjectInformationW(GetProcessWindowStation(),UOI_FLAGS,&flags,sizeof(flags),&needed) || flags.dwFlags&WSF_VISIBLE) return 20;
 HMODULE self{};GetModuleHandleExW(6,reinterpret_cast<LPCWSTR>(&Install),&self);wchar_t path[32768]{};
 if (!GetModuleFileNameW(self,path,32768)) return 21;
 auto end=wcsrchr(path,L'\\');if (!end) return 22;*(end+1)=0;
 const std::wstring file=std::wstring(path)+L"window-events-"+std::to_wstring(GetCurrentProcessId())+L".jsonl";
 output=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if (output==INVALID_HANDLE_VALUE) return 23;
 auto base=reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
 auto imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);unsigned patched{};
 for (;imports->Name;++imports) {
  if (!imports->OriginalFirstThunk) continue;
  auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->OriginalFirstThunk);auto slots=reinterpret_cast<IMAGE_THUNK_DATA*>(base+imports->FirstThunk);
  for (;names->u1.AddressOfData;++names,++slots) {
   if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
   auto item=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
   const bool set=!strcmp(reinterpret_cast<char*>(item->Name),"SetTimer"),remove=!strcmp(reinterpret_cast<char*>(item->Name),"KillTimer");
   if (!set && !remove) continue;
   DWORD previous{},ignored{};if (!VirtualProtect(&slots->u1.Function,sizeof(void*),PAGE_READWRITE,&previous)) return 24;
   slots->u1.Function=reinterpret_cast<ULONG_PTR>(set?reinterpret_cast<void*>(&timer):reinterpret_cast<void*>(&kill));
   VirtualProtect(&slots->u1.Function,sizeof(void*),previous,&ignored);++patched;
  }
 }
 if (patched!=2) return 100+patched;
 EnumWindows([](HWND window,LPARAM)->BOOL {
  DWORD pid{};GetWindowThreadProcessId(window,&pid);wchar_t name[128]{};GetClassNameW(window,name,128);
  if (pid==GetCurrentProcessId() && !wcscmp(name,L"TF811202_MassiGra_Main")) {main_window=window;return FALSE;}return TRUE;
 },0);
 if (!main_window) return 25;
 original=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(main_window,GWLP_WNDPROC));
 if (!original || SetWindowLongPtrW(main_window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&observe))!=reinterpret_cast<LONG_PTR>(original)) return 26;
 log("Install",reinterpret_cast<ULONG_PTR>(main_window),patched);return 0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {if (reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
