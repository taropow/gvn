#pragma once
#include "test.h"
#include <windows.h>
#include <objbase.h>
#include <winioctl.h>
#include <filesystem>
#include <fstream>
#include <vector>

struct FolderTestTree {
    std::filesystem::path base = std::filesystem::temp_directory_path(), root;
    std::vector<std::filesystem::path> junctions;
    FolderTestTree() {
        GUID id{};
        CHECK(SUCCEEDED(CoCreateGuid(&id)));
        wchar_t text[40]{};
        CHECK(StringFromGUID2(id, text, 40));
        root = base / (std::wstring(L"GVN-folder-test-") + text);
        CHECK(std::filesystem::create_directory(root));
    }
    ~FolderTestTree() {
        if (root.parent_path() != base || !root.filename().native().starts_with(L"GVN-folder-test-"))
            return;
        for (const auto &path : junctions) {
            const auto attributes = GetFileAttributesW(path.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                RemoveDirectoryW(path.c_str()); // Remove the owned junction, never traverse its target.
        }
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    auto directory(const std::filesystem::path &relative) {
        const auto path = root / relative;
        std::filesystem::create_directories(path);
        return path;
    }
    auto file(const std::filesystem::path &relative, DWORD attributes = FILE_ATTRIBUTE_NORMAL) {
        const auto path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << "synthetic extension discovery fixture";
        stream.close();
        CHECK(stream && SetFileAttributesW(path.c_str(), attributes));
        return path;
    }
    auto junction(const std::filesystem::path &relative, const std::filesystem::path &target) {
        const auto path = directory(relative);
        junctions.push_back(path);
        const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        CHECK(handle != INVALID_HANDLE_VALUE);
        struct Buffer {
            DWORD tag;
            WORD length, reserved, substitute_offset, substitute_length, print_offset, print_length;
            wchar_t text[32768];
        } buffer{};
        const auto substitute = L"\\??\\" + target.native(), print = target.native();
        const auto payload = substitute + L'\0' + print + L'\0';
        CHECK(payload.size() < std::size(buffer.text));
        buffer.tag = IO_REPARSE_TAG_MOUNT_POINT;
        buffer.length = static_cast<WORD>(8 + payload.size() * sizeof(wchar_t));
        buffer.substitute_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
        buffer.print_offset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
        buffer.print_length = static_cast<WORD>(print.size() * sizeof(wchar_t));
        memcpy(buffer.text, payload.data(), payload.size() * sizeof(wchar_t));
        DWORD returned{};
        const bool accepted = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &buffer, 8 + buffer.length,
                                              nullptr, 0, &returned, nullptr) != FALSE;
        CloseHandle(handle);
        CHECK(accepted);
        CHECK(GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT);
        return path;
    }
};
