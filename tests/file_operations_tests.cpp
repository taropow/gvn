#include "gvn/file_operations.h"
#include "gvn/codecs.h"
#include "test.h"
#include <fstream>
#include <iostream>
using namespace gvn;
namespace {
struct OwnedFiles {
    std::filesystem::path first_base, second_base, first, second;
    OwnedFiles() {
        GUID id{};
        CHECK(SUCCEEDED(CoCreateGuid(&id)));
        wchar_t token[40]{};
        StringFromGUID2(id, token, 40);
        first_base =
            std::filesystem::absolute(std::filesystem::path(GVN_SOURCE_ROOT) / L".cache/fileops-tests")
                .lexically_normal();
        second_base = std::filesystem::absolute(std::filesystem::path(GVN_BINARY_ROOT) / L"fileops-tests")
                          .lexically_normal();
        first = first_base / token;
        second = second_base / token;
        std::filesystem::create_directories(first);
        std::filesystem::create_directories(second);
    }
    ~OwnedFiles() {
        for (const auto &[path, base] : {std::pair{first, first_base}, std::pair{second, second_base}}) {
            if (path.parent_path() != base || path.filename().wstring().front() != L'{')
                continue;
            std::error_code ec;
            for (const auto &file : std::filesystem::recursive_directory_iterator(path, ec))
                if (file.is_regular_file())
                    SetFileAttributesW(file.path().c_str(), FILE_ATTRIBUTE_NORMAL);
            std::filesystem::remove_all(path, ec); // Checked unique roots created by this object only.
        }
    }
    auto write(const std::filesystem::path &folder, const wchar_t *name, const char *value) {
        const auto path = folder / name;
        std::ofstream out(path, std::ios::binary);
        out << value;
        out.close();
        CHECK(out.good());
        return path;
    }
};
} // namespace
TEST(copy_commits_only_when_allowed_and_protects_source_and_other_hardlinks) {
    OwnedFiles own;
    const auto source = own.write(own.first, L"元 画像.png", "original pixels");
    const auto target = own.write(own.first, L"target.png", "existing bytes");
    const auto before = read_file(source), old = read_file(target);
    THROWS(copy_file_checked(source, target, false));
    CHECK(read_file(source) == before && read_file(target) == old);
    const auto old_alias = own.first / L"old-link.png";
    CHECK(CreateHardLinkW(old_alias.c_str(), target.c_str(), nullptr));
    copy_file_checked(source, target, true);
    CHECK(read_file(source) == before && read_file(target) == before && read_file(old_alias) == old);
    const auto alias = own.first / L"source-link.png";
    CHECK(CreateHardLinkW(alias.c_str(), source.c_str(), nullptr));
    CHECK(same_disk_file(source, alias));
    THROWS(copy_file_checked(source, alias, true));
    THROWS(move_file_checked(source, alias, true));
    CHECK(read_file(source) == before && read_file(alias) == before);
}
TEST(copy_failure_keeps_existing_destination_and_cleans_only_its_temporary_file) {
    OwnedFiles own;
    const auto source = own.write(own.first, L"source.png", "source");
    const auto target = own.write(own.first, L"target.png", "target");
    CHECK(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY));
    THROWS(copy_file_checked(source, target, true));
    CHECK(read_file(source) != read_file(target));
    CHECK(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL));
    HANDLE lock =
        CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    THROWS(copy_file_checked(source, target, true));
    CloseHandle(lock);
    CHECK(read_file(source) != read_file(target));
    for (const auto &entry : std::filesystem::directory_iterator(own.first))
        CHECK(entry.path().wstring().find(L".gvn-copying") == std::wstring::npos);
}
TEST(move_between_owned_roots_preserves_bytes_and_reports_source_removal) {
    OwnedFiles own;
    const auto source = own.write(own.first, L"move.png", "move source");
    const auto target = own.write(own.second, L"move.png", "move destination");
    const auto expected = read_file(source);
    THROWS(move_file_checked(source, target, false));
    CHECK(std::filesystem::exists(source) && read_file(target) != expected);
    move_file_checked(source, target, true);
    CHECK(!std::filesystem::exists(source) && read_file(target) == expected);
}
TEST(direct_delete_handles_only_existing_files_and_keeps_locked_sources) {
    OwnedFiles own;
    const auto source = own.write(own.first, L"delete.png", "owned delete fixture");
    HANDLE lock =
        CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    THROWS(delete_file_checked(nullptr, source, false));
    CHECK(std::filesystem::exists(source));
    CloseHandle(lock);
    delete_file_checked(nullptr, source, false);
    CHECK(!std::filesystem::exists(source));
    THROWS(delete_file_checked(nullptr, own.first, false));
}
TEST(save_refuses_unconfirmed_existing_destination_and_keeps_original_bytes) {
    OwnedFiles own;
    const auto target = own.write(own.first, L"existing.png", "existing destination");
    const auto bytes = read_file(target);
    Image source;
    source.width = 2;
    source.height = 1;
    source.frames.push_back({{{255, 0, 0, 128}, {0, 0, 255, 0}}, 100});
    THROWS(save_image(source, 0, target, .93f, false));
    CHECK(read_file(target) == bytes);
    const auto fresh = own.first / L"new.png";
    save_image(source, 0, fresh, .93f, false);
    CHECK(load_image(fresh).frames[0].pixels == source.frames[0].pixels);
    for (const auto &entry : std::filesystem::directory_iterator(own.first))
        CHECK(entry.path().wstring().find(L".gvn-writing") == std::wstring::npos);
}
int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 2;
    const int result = run_tests();
    CoUninitialize();
    return result;
}
