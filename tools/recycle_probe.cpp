// Recycle and restore only this invocation's GUID-owned fixture; never empty the recycle bin.
#include "gvn/file_operations.h"
#include "gvn/codecs.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <propsys.h>
#include <wrl/client.h>
#include <fstream>
#include <iostream>
using Microsoft::WRL::ComPtr;
static void check(HRESULT value, const char *message) {
    if (FAILED(value))
        throw std::runtime_error(std::string(message) + ": " +
                                 std::to_string(static_cast<unsigned long>(value)));
}
int main() {
    struct Apartment {
        HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Apartment() {
            if (SUCCEEDED(status))
                CoUninitialize();
        }
    } apartment;
    if (FAILED(apartment.status))
        return 2;
    struct Owner {
        HWND window = CreateWindowExW(0, L"STATIC", L"GVN owned recycle probe", WS_OVERLAPPEDWINDOW, 0, 0,
                                      100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~Owner() {
            if (window)
                DestroyWindow(window);
        }
    } owner;
    if (!owner.window) {
        return 2;
    }
    try {
        GUID id{};
        check(CoCreateGuid(&id), "identity");
        wchar_t token[40]{};
        StringFromGUID2(id, token, 40);
        const auto root =
            (std::filesystem::path(GVN_SOURCE_ROOT) / L".cache/fileops-recycle").lexically_normal();
        std::filesystem::create_directories(root);
        const auto owned = root / token;
        if (!std::filesystem::create_directory(owned))
            throw std::runtime_error("Owned recycle directory already exists");
        const auto source = owned / L"gvn-owned-recycle.png";
        std::filesystem::copy_file(std::filesystem::path(GVN_SOURCE_ROOT) / L"tests/fixtures/alpha.png",
                                   source);
        const auto bytes = gvn::read_file(source);
        gvn::delete_file_checked(owner.window, source, true);
        if (std::filesystem::exists(source))
            throw std::runtime_error("Recycle left source");
        PROPERTYKEY deleted_from{};
        check(PSGetPropertyKeyFromName(L"System.Recycle.DeletedFrom", &deleted_from), "Recycle property");
        ComPtr<IShellItem> bin;
        check(SHGetKnownFolderItem(FOLDERID_RecycleBinFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&bin)),
              "Recycle folder");
        ComPtr<IEnumShellItems> items;
        check(bin->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)), "Recycle enumeration");
        ComPtr<IShellItem> found;
        for (;;) {
            ComPtr<IShellItem> item;
            if (items->Next(1, &item, nullptr) != S_OK)
                break;
            ComPtr<IShellItem2> properties;
            if (FAILED(item.As(&properties)))
                continue;
            PWSTR from{};
            if (FAILED(properties->GetString(deleted_from, &from)))
                continue;
            const bool own = _wcsicmp(from, owned.c_str()) == 0;
            CoTaskMemFree(from);
            if (own) {
                if (found)
                    throw std::runtime_error("Multiple matches for unique owned path");
                found = item;
            }
        }
        if (!found)
            throw std::runtime_error("Owned recycled fixture was not found");
        // Match the full unique parent before reading or restoring any recycle-bin item.
        PWSTR recycled_path{};
        check(found->GetDisplayName(SIGDN_FILESYSPATH, &recycled_path), "Owned recycled path");
        const std::filesystem::path recycled(recycled_path);
        CoTaskMemFree(recycled_path);
        if (gvn::read_file(recycled) != bytes)
            throw std::runtime_error("Owned recycled bytes differ");
        ComPtr<IShellItem> destination;
        check(SHCreateItemFromParsingName(owned.c_str(), nullptr, IID_PPV_ARGS(&destination)),
              "Restore parent");
        ComPtr<IFileOperation> restore;
        check(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&restore)),
              "Restore operation");
        check(restore->SetOperationFlags(FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOFX_EARLYFAILURE),
              "Restore flags");
        check(restore->MoveItem(found.Get(), destination.Get(), source.filename().c_str(), nullptr),
              "Restore item");
        check(restore->PerformOperations(), "Restore execution");
        BOOL aborted{};
        check(restore->GetAnyOperationsAborted(&aborted), "Restore status");
        if (aborted || gvn::read_file(source) != bytes || std::filesystem::exists(recycled))
            throw std::runtime_error("Restore did not roundtrip the owned fixture");
        // Delete the restored fixture directly; both paths are created above and ownership-checked.
        gvn::delete_file_checked(nullptr, source, false);
        if (owned.parent_path() != root || owned.filename().wstring() != token)
            throw std::runtime_error("Cleanup ownership mismatch");
        if (!std::filesystem::remove(owned))
            throw std::runtime_error("Cleanup failed");
        std::cout << "{\"passed\":true,\"recycled_bytes\":" << bytes.size()
                  << ",\"restore_bytes_equal\":true,\"cleanup\":true,\"scope\":\"actual Shell recycle and "
                     "restore; GUID-owned fixture only; no user window or clipboard operations\"}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
