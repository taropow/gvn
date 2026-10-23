#include "gvn/folder_navigation.h"
#include "folder_test_tree.h"
using namespace gvn;
namespace fs=std::filesystem;
namespace {
struct Alias {
    std::wstring device,target;
    explicit Alias(const fs::path &root):target(L"\\??\\"+root.native()){}
    ~Alias(){if(!device.empty())DefineDosDeviceW(DDD_RAW_TARGET_PATH|DDD_REMOVE_DEFINITION|DDD_EXACT_MATCH_ON_REMOVE|DDD_NO_BROADCAST_SYSTEM,device.c_str(),target.c_str());}
    void open() {
        const auto drives=GetLogicalDrives();CHECK(drives);
        for(const auto letter:std::wstring(L"ZYXWVUTSR")) {
            wchar_t name[]{letter,L':',0},existing[32768]{};
            if(drives&(1u<<(letter-L'A')))continue;
            SetLastError(0);if(QueryDosDeviceW(name,existing,DWORD(std::size(existing))))continue;
            CHECK(GetLastError()==ERROR_FILE_NOT_FOUND);
            CHECK(DefineDosDeviceW(DDD_RAW_TARGET_PATH|DDD_NO_BROADCAST_SYSTEM,name,target.c_str()));device=name;
            CHECK(QueryDosDeviceW(device.c_str(),existing,DWORD(std::size(existing)))&&target==existing);return;
        }
        throw std::runtime_error("No free owned test drive letter");
    }
    fs::path path(const wchar_t *relative=L"") const{return fs::path(device+L"\\")/relative;}
    void remove() {
        CHECK(!device.empty());wchar_t existing[32768]{};
        CHECK(QueryDosDeviceW(device.c_str(),existing,DWORD(std::size(existing)))&&target==existing);
        CHECK(DefineDosDeviceW(DDD_RAW_TARGET_PATH|DDD_REMOVE_DEFINITION|DDD_EXACT_MATCH_ON_REMOVE|DDD_NO_BROADCAST_SYSTEM,device.c_str(),target.c_str()));
        SetLastError(0);CHECK(!QueryDosDeviceW(device.c_str(),existing,DWORD(std::size(existing)))&&GetLastError()==ERROR_FILE_NOT_FOUND);device.clear();
    }
};
}
TEST(volume_discovery_on_owned_dos_alias_keeps_drive_root_and_explicit_folder_boundaries) {
    FolderTestTree tree;tree.file(L"a/a.png");tree.file(L"z/z.png");Alias alias(tree.root);alias.open();
    FolderQuery query{alias.path(L"a"),FolderMove::Next};query.collect_files=true;
    auto match=find_folder(query);CHECK(match.directory==alias.path(L"z")&&match.files&&match.files->size()==1&&match.files->front().path==alias.path(L"z/z.png"));
    query.directory=alias.path(L"z");CHECK(find_folder(query).directory.empty());
    query.move=FolderMove::Previous;match=find_folder(query);CHECK(match.directory==alias.path(L"a")&&match.files&&match.files->front().path==alias.path(L"a/a.png"));
    query.move=FolderMove::Next;query.wrap=true;match=find_folder(query);CHECK(match.directory==alias.path(L"a"));
    query.directory=alias.path(L"a");query.root=query.directory;query.wrap=false;CHECK(find_folder(query).directory.empty());
    query.move=FolderMove::Parent;match=find_folder(query);CHECK(match.directory.empty());
    query.root.clear();match=find_folder(query);CHECK(match.directory==alias.path());
    alias.remove();
}
int main(){return run_tests();}
