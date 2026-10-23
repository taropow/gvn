#include "gvn/file_operations.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <stdexcept>
#include <string>

namespace gvn {
namespace {
[[noreturn]] void error(const char *message) {
    throw std::runtime_error(std::string(message) + " (Win32 " + std::to_string(GetLastError()) + ")");
}
void source_file(const std::filesystem::path &path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec)
        throw std::runtime_error("Source is not an available regular file");
}
void destination_file(const std::filesystem::path &source, const std::filesystem::path &target,
                      bool overwrite) {
    source_file(source);
    if (same_disk_file(source, target))
        throw std::runtime_error("Source and destination refer to the same file");
    const auto attributes = GetFileAttributesW(target.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (!overwrite)
            throw std::runtime_error("Destination appeared or already exists; confirm again");
        if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY))
            throw std::runtime_error("Destination is a directory or read-only file");
    }
}
void remove_temporary(const std::filesystem::path &path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if (attributes & FILE_ATTRIBUTE_READONLY)
            SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
        DeleteFileW(path.c_str());
    }
}
} // namespace
bool same_disk_file(const std::filesystem::path &source, const std::filesystem::path &target) {
    if (source.empty() || target.empty())
        return false;
    std::error_code ec;
    const bool equivalent = std::filesystem::equivalent(source, target, ec);
    if (!ec)
        return equivalent;
    return std::filesystem::absolute(source).lexically_normal() ==
           std::filesystem::absolute(target).lexically_normal();
}
void copy_file_checked(const std::filesystem::path &source, const std::filesystem::path &target,
                       bool overwrite) {
    destination_file(source, target, overwrite);
    GUID identity{};
    if (FAILED(CoCreateGuid(&identity)))
        throw std::runtime_error("Cannot create copy identity");
    wchar_t token[40]{};
    StringFromGUID2(identity, token, 40);
    auto temporary = target;
    temporary += std::wstring(L".") + token + L".gvn-copying";
    if (!CopyFileW(source.c_str(), temporary.c_str(), TRUE)) {
        const auto reason = GetLastError();
        if (reason != ERROR_FILE_EXISTS && reason != ERROR_ALREADY_EXISTS)
            remove_temporary(temporary);
        SetLastError(reason);
        error("Cannot prepare file copy");
    }
    if (!MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0))) {
        const auto reason = GetLastError();
        remove_temporary(temporary);
        SetLastError(reason);
        error("Cannot commit file copy");
    }
}
void move_file_checked(const std::filesystem::path &source, const std::filesystem::path &target,
                       bool overwrite) {
    destination_file(source, target, overwrite);
    if (!MoveFileExW(source.c_str(), target.c_str(),
                     MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH |
                         (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)))
        error("Cannot move file");
    std::error_code ec;
    if (std::filesystem::exists(source, ec) || ec)
        throw std::runtime_error("Destination was copied, but removal of the source could not be confirmed; "
                                 "the source view is retained");
}
void delete_file_checked(HWND owner, const std::filesystem::path &path, bool recycle) {
    source_file(path);
    if (!recycle) {
        if (!DeleteFileW(path.c_str()))
            error("Cannot delete file");
        return;
    }
    Microsoft::WRL::ComPtr<IFileOperation> operation;
    if (FAILED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation))))
        throw std::runtime_error("Cannot create file operation");
    const auto check = [](HRESULT result, const char *stage) {
        if (FAILED(result))
            throw std::runtime_error(std::string("Cannot recycle file at ") + stage + " (HRESULT " +
                                     std::to_string(static_cast<unsigned long>(result)) + ")");
    };
    if (owner)
        check(operation->SetOwnerWindow(owner), "owner");
    check(operation->SetOperationFlags(FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOF_NOCONFIRMATION |
                                       FOF_NOERRORUI | FOF_SILENT | FOFX_EARLYFAILURE),
          "flags");
    Microsoft::WRL::ComPtr<IShellItem> item;
    const auto native = std::filesystem::absolute(path).lexically_normal();
    check(SHCreateItemFromParsingName(native.c_str(), nullptr, IID_PPV_ARGS(&item)), "source path");
    check(operation->DeleteItem(item.Get(), nullptr), "queue");
    check(operation->PerformOperations(), "execution");
    BOOL aborted{};
    check(operation->GetAnyOperationsAborted(&aborted), "completion status");
    if (aborted)
        throw std::runtime_error("File recycling was aborted");
    std::error_code ec;
    if (std::filesystem::exists(path, ec) || ec)
        throw std::runtime_error("File removal was not confirmed");
}
} // namespace gvn
