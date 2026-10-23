#pragma once
#include <windows.h>
#include <filesystem>

namespace gvn {
bool same_disk_file(const std::filesystem::path &source, const std::filesystem::path &target);
void copy_file_checked(const std::filesystem::path &source, const std::filesystem::path &target,
                       bool overwrite);
void move_file_checked(const std::filesystem::path &source, const std::filesystem::path &target,
                       bool overwrite);
void delete_file_checked(HWND owner, const std::filesystem::path &path, bool recycle);
} // namespace gvn
