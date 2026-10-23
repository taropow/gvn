// Reference-only boundary observer. Never calls the system wallpaper setter.
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <algorithm>

static_assert(sizeof(void *) == 4, "The official reference is x86");
static HANDLE log_file = INVALID_HANDLE_VALUE;
static SRWLOCK log_lock = SRWLOCK_INIT;
static std::wstring root, style=L"0", tile=L"0", wallpaper;
static HKEY panel = reinterpret_cast<HKEY>(0x5a170001u);
static HKEY desktop = reinterpret_cast<HKEY>(0x5a170005u);
static std::string utf8(const std::wstring &s) {
    if (s.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);return out;
}
static std::wstring wide(const char *s) {
    if (!s || !*s) return {};
    const int n=MultiByteToWideChar(CP_ACP,0,s,-1,nullptr,0);
    std::wstring out(n,L'\0');MultiByteToWideChar(CP_ACP,0,s,-1,out.data(),n);out.pop_back();return out;
}
static std::string quoted(const std::wstring &s) {
    std::string out="\"";
    for (const unsigned char c:utf8(s)) {
        if (c=='\\' || c=='"') {out+='\\';out+=c;}
        else if (c<32) {char b[7];sprintf_s(b,"\\u%04x",c);out+=b;}
        else out+=c;
    }
    return out+'"';
}
static void event(const char *api,const std::wstring &name=L"",const std::wstring &value=L"",DWORD number=0) {
    const auto line="{\"api\":\""+std::string(api)+"\",\"name\":"+quoted(name)+",\"value\":"+quoted(value)+",\"number\":"+std::to_string(number)+"}\n";
    AcquireSRWLockExclusive(&log_lock);DWORD count{};WriteFile(log_file,line.data(),static_cast<DWORD>(line.size()),&count,nullptr);FlushFileBuffers(log_file);ReleaseSRWLockExclusive(&log_lock);
}
static bool owned(LPCWSTR path) {
    if (!path) return false;
    wchar_t full[32768]{};const auto n=GetFullPathNameW(path,32768,full,nullptr);
    return n && n<32768 && root.size()<n && !_wcsnicmp(full,root.c_str(),root.size());
}
static bool fake(HKEY key) {return key==panel || key==desktop;}
static std::wstring value(LPCWSTR name) {
    if (name && !_wcsicmp(name,L"WallpaperStyle")) return style;
    if (name && !_wcsicmp(name,L"TileWallpaper")) return tile;
    if (name && !_wcsicmp(name,L"Wallpaper")) return wallpaper;
    return L"";
}
static LSTATUS WINAPI open_key(HKEY key,LPCWSTR path,DWORD options,REGSAM access,PHKEY result) {
    if (key==HKEY_CURRENT_USER && path && !_wcsicmp(path,L"Control Panel")) {*result=panel;return ERROR_SUCCESS;}
    if ((key==HKEY_CURRENT_USER && path && !_wcsicmp(path,L"Control Panel\\Desktop")) ||
        (key==panel && path && !_wcsicmp(path,L"Desktop"))) {*result=desktop;return ERROR_SUCCESS;}
    if (fake(key)) return ERROR_FILE_NOT_FOUND;
    return RegOpenKeyExW(key,path,options,access,result);
}
static LSTATUS WINAPI open_key_a(HKEY key,LPCSTR path,DWORD options,REGSAM access,PHKEY result) {
    const auto text=wide(path);return open_key(key,path?text.c_str():nullptr,options,access,result);
}
static LSTATUS WINAPI create_key(HKEY key,LPCWSTR path,DWORD reserved,LPWSTR cls,DWORD options,REGSAM access,
                                 const LPSECURITY_ATTRIBUTES security,PHKEY result,LPDWORD disposition) {
    (void)reserved;(void)cls;(void)options;(void)security;
    event("RegCreateKeyExW",path?path:L"");
    if (disposition) *disposition=REG_OPENED_EXISTING_KEY;
    if ((key==HKEY_CURRENT_USER && path && (!_wcsicmp(path,L"Control Panel") || !_wcsicmp(path,L"Control Panel\\Desktop"))) || key==panel)
        return open_key(key,path,0,access,result);
    return ERROR_ACCESS_DENIED;
}
static LSTATUS WINAPI query(HKEY key,LPCWSTR name,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD size) {
    if (!fake(key)) return RegQueryValueExW(key,name,reserved,type,data,size);
    if (!size) return ERROR_INVALID_PARAMETER;
    const auto text=value(name);const auto required=static_cast<DWORD>((text.size()+1)*sizeof(wchar_t));
    if (type) *type=REG_SZ;
    const auto available=*size;*size=required;
    if (!data) return ERROR_SUCCESS;
    if (available<required) return ERROR_MORE_DATA;
    memcpy(data,text.c_str(),required);return ERROR_SUCCESS;
}
static LSTATUS WINAPI query_a(HKEY key,LPCSTR name,LPDWORD reserved,LPDWORD type,LPBYTE data,LPDWORD size) {
    if (!fake(key)) return RegQueryValueExA(key,name,reserved,type,data,size);
    if (!size) return ERROR_INVALID_PARAMETER;
    const auto text=utf8(value(wide(name).c_str()));const auto required=static_cast<DWORD>(text.size()+1);
    if (type) *type=REG_SZ;const auto available=*size;*size=required;
    if (!data) return ERROR_SUCCESS;
    if (available<required) return ERROR_MORE_DATA;
    memcpy(data,text.c_str(),required);return ERROR_SUCCESS;
}
static LSTATUS WINAPI set_value(HKEY key,LPCWSTR name,DWORD reserved,DWORD type,const BYTE *data,DWORD size) {
    (void)key;(void)reserved;
    std::wstring text;
    if ((type==REG_SZ || type==REG_EXPAND_SZ) && data && !(size%2)) {
        text.assign(reinterpret_cast<const wchar_t *>(data),size/2);
        const auto end=text.find(L'\0');if (end!=std::wstring::npos) text.resize(end);
    } else if (type==REG_DWORD && data && size==4) {DWORD n{};memcpy(&n,data,4);text=std::to_wstring(n);}
    event("RegSetValueExW",name?name:L"",text,type);
    if (name && !_wcsicmp(name,L"WallpaperStyle")) style=text;
    if (name && !_wcsicmp(name,L"TileWallpaper")) tile=text;
    if (name && !_wcsicmp(name,L"Wallpaper")) wallpaper=text;
    return ERROR_SUCCESS; // All registry mutations are virtual, even for unexpected real handles.
}
static LSTATUS WINAPI close_key(HKEY key) {return fake(key)?ERROR_SUCCESS:RegCloseKey(key);}
static LSTATUS WINAPI delete_key(HKEY,LPCWSTR name) {event("blocked-RegDeleteKeyW",name?name:L"");return ERROR_ACCESS_DENIED;}
static LSTATUS WINAPI delete_value(HKEY,LPCWSTR name) {event("blocked-RegDeleteValueW",name?name:L"");return ERROR_ACCESS_DENIED;}
static BOOL WINAPI special_folder(HWND,LPWSTR path,int csidl,BOOL) {
    event("SHGetSpecialFolderPathW",L"",L"",static_cast<DWORD>(csidl));
    if (!path || ((csidl&0xff)!=CSIDL_APPDATA && (csidl&0xff)!=CSIDL_LOCAL_APPDATA)) return FALSE;
    wcscpy_s(path,MAX_PATH,(root+L"profile").c_str());return TRUE;
}
static BOOL WINAPI parameters(UINT action,UINT parameter,PVOID data,UINT flags) {
    if (action==SPI_SETDESKWALLPAPER || action==SPI_SETDESKPATTERN) {
        wallpaper=data?static_cast<LPCWSTR>(data):L"";event("SystemParametersInfoW",std::to_wstring(action),wallpaper,flags);return TRUE;
    }
    if (action==SPI_GETDESKWALLPAPER) {
        if (!data || !parameter) return FALSE;
        wcsncpy_s(static_cast<LPWSTR>(data),parameter,wallpaper.c_str(),_TRUNCATE);return TRUE;
    }
    return SystemParametersInfoW(action,parameter,data,flags);
}
static BOOL WINAPI parameters_a(UINT action,UINT parameter,PVOID data,UINT flags) {
    if (action==SPI_SETDESKWALLPAPER || action==SPI_SETDESKPATTERN) {
        wallpaper=wide(static_cast<LPCSTR>(data));event("SystemParametersInfoA",std::to_wstring(action),wallpaper,flags);return TRUE;
    }
    if (action==SPI_GETDESKWALLPAPER) {
        if (!data || !parameter) return FALSE;
        strncpy_s(static_cast<LPSTR>(data),parameter,utf8(wallpaper).c_str(),_TRUNCATE);return TRUE;
    }
    return SystemParametersInfoA(action,parameter,data,flags);
}
static HANDLE WINAPI create_file(LPCWSTR path,DWORD access,DWORD sharing,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE template_file) {
    if ((access&(GENERIC_WRITE|GENERIC_ALL|MAXIMUM_ALLOWED|DELETE|FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|WRITE_DAC|WRITE_OWNER) || creation!=OPEN_EXISTING || flags&FILE_FLAG_DELETE_ON_CLOSE) && !owned(path)) {
        event("blocked-CreateFileW",path?path:L"");SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;
    }
    return CreateFileW(path,access,sharing,security,creation,flags,template_file);
}
static BOOL WINAPI create_directory(LPCWSTR path,LPSECURITY_ATTRIBUTES security) {
    if (!owned(path)) {event("blocked-CreateDirectoryW",path?path:L"");SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return CreateDirectoryW(path,security);
}
static BOOL WINAPI delete_file(LPCWSTR path) {
    const bool allowed=owned(path);event(allowed?"DeleteFileW":"blocked-DeleteFileW",path?path:L"");
    if (!allowed) {SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return DeleteFileW(path);
}
static BOOL WINAPI copy_file(LPCWSTR from,LPCWSTR to,BOOL fail) {
    if (!owned(to)) {event("blocked-CopyFileW",to?to:L"");SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return CopyFileW(from,to,fail);
}
static BOOL WINAPI move_file(LPCWSTR from,LPCWSTR to) {
    if (!owned(from) || !owned(to)) {event("blocked-MoveFileW",from?from:L"",to?to:L"");SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return MoveFileW(from,to);
}
static BOOL WINAPI remove_directory(LPCWSTR path) {
    if (!owned(path)) {event("blocked-RemoveDirectoryW",path?path:L"");SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return RemoveDirectoryW(path);
}
static BOOL WINAPI attributes(LPCWSTR path,DWORD flags) {
    if (!owned(path)) {event("blocked-SetFileAttributesW",path?path:L"");SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return SetFileAttributesW(path,flags);
}
static BOOL WINAPI profile(LPCWSTR section,LPCWSTR name,LPCWSTR text,LPCWSTR path) {
    if (!owned(path)) {event("blocked-WritePrivateProfileStringW",path?path:L"");SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return WritePrivateProfileStringW(section,name,text,path);
}
static int WINAPI shell_files(LPSHFILEOPSTRUCTW operation) {
    event("blocked-SHFileOperationW",operation && operation->pFrom?operation->pFrom:L"",L"",operation?operation->wFunc:0);
    return ERROR_ACCESS_DENIED;
}
static HINSTANCE WINAPI shell_execute(HWND,LPCWSTR,LPCWSTR file,LPCWSTR,LPCWSTR,int) {
    event("blocked-ShellExecuteW",file?file:L"");return reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED);
}
static HRESULT WINAPI create_object(REFCLSID clsid,LPUNKNOWN,DWORD,REFIID,LPVOID *object) {
    if (object) *object=nullptr;
    wchar_t id[64];swprintf_s(id,L"%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",clsid.Data1,clsid.Data2,clsid.Data3,clsid.Data4[0],clsid.Data4[1],clsid.Data4[2],clsid.Data4[3],clsid.Data4[4],clsid.Data4[5],clsid.Data4[6],clsid.Data4[7]);
    event("blocked-CoCreateInstance",id);return E_ACCESSDENIED;
}
struct Hook {const char *name;ULONG_PTR address;};
static FARPROC WINAPI resolve(HMODULE,LPCSTR);
static const Hook hooks[]{
    {"RegOpenKeyExW",reinterpret_cast<ULONG_PTR>(&open_key)},{"RegOpenKeyExA",reinterpret_cast<ULONG_PTR>(&open_key_a)},
    {"RegCreateKeyExW",reinterpret_cast<ULONG_PTR>(&create_key)},{"RegQueryValueExW",reinterpret_cast<ULONG_PTR>(&query)},
    {"RegQueryValueExA",reinterpret_cast<ULONG_PTR>(&query_a)},{"RegSetValueExW",reinterpret_cast<ULONG_PTR>(&set_value)},
    {"RegCloseKey",reinterpret_cast<ULONG_PTR>(&close_key)},{"RegDeleteKeyW",reinterpret_cast<ULONG_PTR>(&delete_key)},
    {"RegDeleteValueW",reinterpret_cast<ULONG_PTR>(&delete_value)},{"SHGetSpecialFolderPathW",reinterpret_cast<ULONG_PTR>(&special_folder)},
    {"SystemParametersInfoW",reinterpret_cast<ULONG_PTR>(&parameters)},{"SystemParametersInfoA",reinterpret_cast<ULONG_PTR>(&parameters_a)},
    {"CreateFileW",reinterpret_cast<ULONG_PTR>(&create_file)},{"CreateDirectoryW",reinterpret_cast<ULONG_PTR>(&create_directory)},
    {"DeleteFileW",reinterpret_cast<ULONG_PTR>(&delete_file)},{"CopyFileW",reinterpret_cast<ULONG_PTR>(&copy_file)},
    {"MoveFileW",reinterpret_cast<ULONG_PTR>(&move_file)},{"CoCreateInstance",reinterpret_cast<ULONG_PTR>(&create_object)},
    {"RemoveDirectoryW",reinterpret_cast<ULONG_PTR>(&remove_directory)},{"SetFileAttributesW",reinterpret_cast<ULONG_PTR>(&attributes)},
    {"WritePrivateProfileStringW",reinterpret_cast<ULONG_PTR>(&profile)},{"SHFileOperationW",reinterpret_cast<ULONG_PTR>(&shell_files)},
    {"ShellExecuteW",reinterpret_cast<ULONG_PTR>(&shell_execute)},
    {"GetProcAddress",reinterpret_cast<ULONG_PTR>(&resolve)}
};
static FARPROC WINAPI resolve(HMODULE module,LPCSTR name) {
    if (reinterpret_cast<ULONG_PTR>(name)>65535) {
        for (const auto &hook:hooks) if (!strcmp(name,hook.name)) return reinterpret_cast<FARPROC>(hook.address);
        for (const char *prefix:{"RegSet","RegCreate","RegDelete","CreateFile","CreateDirectory","DeleteFile","RemoveDirectory","CopyFile","MoveFile","ReplaceFile","GetTempFileName","WritePrivateProfile","ShellExecute","SHFileOperation","CoCreate","CoGetClassObject"})
            if (!strncmp(name,prefix,strlen(prefix))) {event("blocked-GetProcAddress",wide(name));SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
    }
    return GetProcAddress(module,name);
}
extern "C" DWORD WINAPI Install(void *) {
    USEROBJECTFLAGS flags{};DWORD needed{};
    if (!GetUserObjectInformationW(GetProcessWindowStation(),UOI_FLAGS,&flags,sizeof(flags),&needed) || flags.dwFlags&WSF_VISIBLE) return 20;
    HMODULE self{};GetModuleHandleExW(6,reinterpret_cast<LPCWSTR>(&Install),&self);
    wchar_t path[32768]{};if (!GetModuleFileNameW(self,path,32768)) return 21;
    const auto end=wcsrchr(path,L'\\');if (!end) return 22;*(end+1)=0;root=path;
    if (root.size()+64>=MAX_PATH) return 23;
    CreateDirectoryW((root+L"profile").c_str(),nullptr);
    log_file=CreateFileW((root+L"wallpaper-events.jsonl").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (log_file==INVALID_HANDLE_VALUE) return 24;
    const auto base=reinterpret_cast<BYTE *>(GetModuleHandleW(nullptr));
    const auto dos=reinterpret_cast<IMAGE_DOS_HEADER *>(base);const auto nt=reinterpret_cast<IMAGE_NT_HEADERS *>(base+dos->e_lfanew);
    auto imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    unsigned patched{};bool setter{},reg{},folder{},file{},com{};
    for (;imports->Name;++imports) {
        auto names=reinterpret_cast<IMAGE_THUNK_DATA *>(base+imports->OriginalFirstThunk);
        auto slots=reinterpret_cast<IMAGE_THUNK_DATA *>(base+imports->FirstThunk);
        for (;names->u1.AddressOfData;++names,++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const auto name=reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base+names->u1.AddressOfData);
            for (const auto &hook:hooks) if (!strcmp(reinterpret_cast<const char *>(name->Name),hook.name)) {
                DWORD prior{},ignored{};if (!VirtualProtect(&slots->u1.Function,sizeof(void *),PAGE_READWRITE,&prior)) return 25;
                slots->u1.Function=hook.address;VirtualProtect(&slots->u1.Function,sizeof(void *),prior,&ignored);
                ++patched;event("hook",wide(hook.name));
                setter|=!strcmp(hook.name,"SystemParametersInfoW");reg|=!strcmp(hook.name,"RegSetValueExW");
                folder|=!strcmp(hook.name,"SHGetSpecialFolderPathW");file|=!strcmp(hook.name,"CreateFileW");com|=!strcmp(hook.name,"CoCreateInstance");
            }
        }
    }
    event("Install",root,L"noninteractive; virtual registry and wallpaper; own filesystem only",patched);
    return setter && reg && folder && file && com?0:100+patched;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {if (reason==DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);return TRUE;}
