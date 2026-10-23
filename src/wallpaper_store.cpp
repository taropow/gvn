#include "gvn/wallpaper.h"
#include <windows.h>
#include <objbase.h>
#include <exception>
#include <utility>

namespace gvn {
namespace {
[[noreturn]] void win_error(const char *message) {
    const auto code=GetLastError();
    throw std::runtime_error(std::string(message)+" (Win32 "+std::to_string(code)+")");
}
struct Lock {
    HANDLE value{};
    bool owned{};
    explicit Lock(const WallpaperPlatform &platform) {
        if (!platform.read || !platform.write || !platform.apply || platform.mutex_name.empty() ||
            platform.mutex_name.size()>240 || platform.mutex_name.find(L'\0')!=std::wstring::npos ||
            platform.wait_ms>60000) throw std::invalid_argument("Invalid wallpaper platform or lock");
        value=CreateMutexW(nullptr,FALSE,platform.mutex_name.c_str());
        if (!value) win_error("Cannot create wallpaper operation lock");
        const auto result=WaitForSingleObject(value,platform.wait_ms);
        owned=result==WAIT_OBJECT_0 || result==WAIT_ABANDONED;
        if (!owned) {
            CloseHandle(value);value=nullptr;
            throw std::runtime_error("Another wallpaper operation is still running");
        }
    }
    ~Lock() {if (owned) ReleaseMutex(value);if (value) CloseHandle(value);}
    Lock(const Lock &)=delete;
};
DWORD attributes(const std::filesystem::path &path) {
    const auto value=GetFileAttributesW(path.c_str());
    if (value==INVALID_FILE_ATTRIBUTES && GetLastError()!=ERROR_FILE_NOT_FOUND && GetLastError()!=ERROR_PATH_NOT_FOUND)
        win_error("Cannot inspect wallpaper file");
    return value;
}
bool directory_exists(const std::filesystem::path &path) {
    const auto value=attributes(path);
    if (value==INVALID_FILE_ATTRIBUTES) return false;
    if (!(value&FILE_ATTRIBUTE_DIRECTORY) || value&FILE_ATTRIBUTE_REPARSE_POINT)
        throw std::runtime_error("Wallpaper storage is not an ordinary directory");
    return true;
}
bool managed_name(const std::wstring &name) {
    if (name.size()!=46 || name.compare(0,5,L"GVN-{") || name[41]!=L'}' || _wcsicmp(name.c_str()+42,L".bmp")) return false;
    for (size_t i=5;i<41;++i) {
        if (i==13 || i==18 || i==23 || i==28) {if (name[i]!=L'-') return false;}
        else if (!((name[i]>=L'0' && name[i]<=L'9') || (name[i]>=L'a' && name[i]<=L'f') || (name[i]>=L'A' && name[i]<=L'F'))) return false;
    }
    return true;
}
std::filesystem::path unique(const std::filesystem::path &directory,const wchar_t *extension) {
    GUID guid{};if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Cannot name wallpaper file");
    wchar_t name[40]{};if (!StringFromGUID2(guid,name,40)) throw std::runtime_error("Cannot format wallpaper file name");
    return directory/(std::wstring(L"GVN-")+name+extension);
}
bool remove_owned(const std::filesystem::path &path) {
    if (DeleteFileW(path.c_str())) return true;
    return GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND;
}
std::filesystem::path write_new(const std::filesystem::path &directory,const std::vector<uint8_t> &bytes) {
    for (int attempt=0;attempt<4;++attempt) {
        const auto path=unique(directory,L".bmp");
        const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file==INVALID_HANDLE_VALUE) {
            if (GetLastError()==ERROR_FILE_EXISTS || GetLastError()==ERROR_ALREADY_EXISTS) continue;
            win_error("Cannot create wallpaper image");
        }
        DWORD written{};
        const bool success=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) &&
                           written==bytes.size() && FlushFileBuffers(file);
        const auto error=GetLastError();CloseHandle(file);
        if (!success) {remove_owned(path);SetLastError(error);win_error("Cannot write wallpaper image");}
        return path;
    }
    throw std::runtime_error("Cannot reserve a new wallpaper file name");
}
std::filesystem::path backup(const std::filesystem::path &path) {
    for (int attempt=0;attempt<4;++attempt) {
        const auto copy=unique(path.parent_path(),L".gvn-wallpaper-backup");
        if (CreateHardLinkW(copy.c_str(),path.c_str(),nullptr) || CopyFileW(path.c_str(),copy.c_str(),TRUE)) return copy;
        if (GetLastError()!=ERROR_FILE_EXISTS && GetLastError()!=ERROR_ALREADY_EXISTS)
            win_error("Cannot retain wallpaper file for recovery");
    }
    throw std::runtime_error("Cannot reserve wallpaper recovery file");
}
bool same_file(const std::filesystem::path &a,const std::filesystem::path &b) {
    if (a.empty() || b.empty() || b.native().find(L'\0')!=std::wstring::npos) return false;
    if (!_wcsicmp(std::filesystem::absolute(a).lexically_normal().c_str(),std::filesystem::absolute(b).lexically_normal().c_str())) return true;
    std::error_code error;return std::filesystem::equivalent(a,b,error) && !error;
}
std::string message(std::exception_ptr error) {
    try {std::rethrow_exception(error);} catch (const std::exception &e) {return e.what();} catch (...) {return "Wallpaper operation failed";}
}
bool restore(const WallpaperPlatform &platform,const WallpaperSnapshot &snapshot) {
    bool success=true;
    try {platform.write(snapshot.values);} catch (...) {success=false;}
    try {platform.apply(snapshot.image);} catch (...) {success=false;}
    return success;
}
[[noreturn]] void recovery_error(std::exception_ptr error,std::vector<std::filesystem::path> files) {
    auto text=message(error)+"; wallpaper recovery needs attention";
    for (const auto &path:files) {
        const auto name=path.u8string();text+="\n"+std::string(reinterpret_cast<const char *>(name.data()),name.size());
    }
    throw WallpaperRecoveryError(std::move(text),std::move(files));
}
}
std::filesystem::path wallpaper_directory(const std::filesystem::path &config) {
    const auto text=config.wstring();
    if (text.empty() || text.find(L'\0')!=std::wstring::npos || !config.has_filename())
        throw std::invalid_argument("A wallpaper settings file is required");
    const auto absolute=std::filesystem::absolute(config).lexically_normal();
    const auto directory=absolute.parent_path()/L"wallpaper";
    if (directory.native().size()+128>32767) throw std::invalid_argument("Wallpaper storage path is too long");
    const auto value=attributes(absolute);
    if (value!=INVALID_FILE_ATTRIBUTES && value&FILE_ATTRIBUTE_DIRECTORY)
        throw std::invalid_argument("Wallpaper settings path is a directory");
    return directory;
}
std::vector<std::filesystem::path> managed_wallpaper_files(const std::filesystem::path &config) {
    const auto directory=wallpaper_directory(config);std::vector<std::filesystem::path> result;
    if (!directory_exists(directory)) return result;
    for (const auto &entry:std::filesystem::directory_iterator(directory)) {
        if (!managed_name(entry.path().filename().wstring())) continue;
        const auto value=attributes(entry.path());
        if (value!=INVALID_FILE_ATTRIBUTES && !(value&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) result.push_back(entry.path());
    }
    std::sort(result.begin(),result.end());return result;
}
bool has_wallpaper_files(const std::filesystem::path &config) noexcept {
    try {return !managed_wallpaper_files(config).empty();} catch (...) {return false;}
}
WallpaperResult set_wallpaper_image(const Image &image,size_t frame,const std::filesystem::path &config,
                                    WallpaperMode mode,const WallpaperPlatform &platform) {
    const auto values=wallpaper_values(mode);const auto bytes=encode_wallpaper_bmp(image,frame);
    const auto directory=wallpaper_directory(config);Lock lock(platform);
    if (!directory_exists(directory)) {std::filesystem::create_directories(directory);if (!directory_exists(directory)) throw std::runtime_error("Cannot create wallpaper storage");}
    const auto old=managed_wallpaper_files(config);const auto snapshot=platform.read();
    const auto path=write_new(directory,bytes);bool touched=false;
    try {touched=true;platform.write(values);platform.apply(path);}
    catch (...) {
        const auto error=std::current_exception();const bool restored=!touched || restore(platform,snapshot);
        std::vector<std::filesystem::path> retained;
        if (!restored || !remove_owned(path)) retained.push_back(path);
        if (!restored || !retained.empty()) recovery_error(error,std::move(retained));
        std::rethrow_exception(error);
    }
    WallpaperResult result;result.file=path;
    for (const auto &file:old) {
        if (same_file(file,image.path)) result.retained.push_back(file);
        else if (!remove_owned(file)) {result.retained.push_back(file);result.cleanup_incomplete=true;}
        else ++result.removed;
    }
    return result;
}
WallpaperResult delete_wallpaper_images(const std::filesystem::path &config,const WallpaperPlatform &platform) {
    Lock lock(platform);const auto files=managed_wallpaper_files(config);if (files.empty()) return {};
    for (const auto &file:files) if (attributes(file)&FILE_ATTRIBUTE_READONLY) throw std::runtime_error("Wallpaper file is read-only");
    const auto snapshot=platform.read();
    struct Backup {std::filesystem::path original,copy;bool deleted{};};std::vector<Backup> copies;bool touched=false;
    copies.reserve(files.size());
    try {
        for (const auto &file:files) copies.push_back({file,backup(file)});
        touched=true;platform.write(wallpaper_values(WallpaperMode::Center));platform.apply({});
        for (auto &entry:copies) {
            if (!DeleteFileW(entry.original.c_str())) win_error("Cannot delete wallpaper file");
            entry.deleted=true;
        }
    } catch (...) {
        const auto error=std::current_exception();std::vector<std::filesystem::path> retained;
        for (auto &entry:copies) {
            if (entry.deleted) {
                if (!MoveFileExW(entry.copy.c_str(),entry.original.c_str(),MOVEFILE_WRITE_THROUGH)) retained.push_back(entry.copy);
            } else if (!remove_owned(entry.copy)) retained.push_back(entry.copy);
        }
        const bool restored=!touched || restore(platform,snapshot);
        if (!restored || !retained.empty()) recovery_error(error,std::move(retained));
        std::rethrow_exception(error);
    }
    WallpaperResult result;result.removed=copies.size();
    for (const auto &entry:copies) if (!remove_owned(entry.copy)) {result.retained.push_back(entry.copy);result.cleanup_incomplete=true;}
    return result;
}
} // namespace gvn
