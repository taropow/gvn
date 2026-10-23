#include "gvn/selection_units_dialog.h"
#include "gvn/settings.h"
#include "gvn/preferences.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <fstream>
using namespace gvn;
namespace id=selection_control;
namespace {
void same(Rect a,Rect b){CHECK(a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h);}
std::wstring text(HWND h){std::wstring s(size_t(GetWindowTextLengthW(h))+1,L'\0');GetWindowTextW(h,s.data(),int(s.size()));s.pop_back();return s;}
struct Fixture {
    HWND owner{};std::unique_ptr<SelectionUnitsDialog> dialog;
    Fixture(){owner=CreateWindowExW(0,L"STATIC",L"Owned selection tests",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);}
    ~Fixture(){dialog.reset();DestroyWindow(owner);}
    void open(SelectionUnits units={}){dialog=std::make_unique<SelectionUnitsDialog>(units);CHECK(dialog->create(owner)&&dialog->ready());}
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);}
};
}
TEST(selection_drag_matches_reference_forward_reverse_click_and_short_units) {
    const SelectionUnits custom{8,4,6,5};
    same(selection_rectangle(13,11,30,27,96,80,{}),{13,11,18,17});
    same(selection_rectangle(13,11,13,11,96,80,{}),{13,11,1,1});
    same(selection_rectangle(13,11,30,27,96,80,{8,8,8,8}),{8,8,24,24});
    same(selection_rectangle(13,11,30,27,96,80,custom),{8,8,24,20});
    same(selection_rectangle(30,27,13,11,96,80,custom),{8,8,24,20});
    same(selection_rectangle(13,11,14,12,96,80,custom),{8,8,12,5});
    same(selection_rectangle(90,74,110,100,96,80,custom),{88,72,6,5});
    CHECK(selection_rectangle(5,5,30,27,96,80,{1,1,128,100}).empty());
    CHECK(selection_rectangle(95,79,96,80,96,80,custom).empty());
}
TEST(selection_units_clamp_to_image_and_reject_invalid_ranges) {
    same(selection_rectangle(-20,-30,300,300,96,80,{}),{0,0,96,80});
    same(selection_rectangle(13,11,30,27,96,80,{1,1,1,1}),selection_rectangle(30,27,13,11,96,80,{}));
    CHECK(!valid_selection_units({0,1,1,1})&&!valid_selection_units({1,1000,1,1})&&!valid_selection_units({1,1,-1,1}));
    CHECK(valid_selection_units({999,999,999,999}));
    THROWS(selection_rectangle(0,0,4,4,10,10,{0,1,1,1}));CHECK(selection_rectangle(0,0,1,1,0,0,{}).empty());
}
TEST(selection_settings_migrate_legacy_keep_custom_on_presets_and_roundtrip) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-selection-"+std::to_wstring(GetCurrentProcessId())+L".ini");std::filesystem::remove(path);
    CHECK(WritePrivateProfileStringW(L"Viewer",L"grid",L"8",path.c_str()));
    Settings old;old.load(path);CHECK(!old.selection_custom&&(old.selection_units()==SelectionUnits{8,8,8,8})&&old.custom_selection_units==old.selection_units());
    old.custom_selection_units={8,4,6,5};old.selection_custom=true;old.save(path);Settings saved;saved.load(path);CHECK(saved.selection_custom&&saved.selection_units()==old.custom_selection_units);
    saved.selection_custom=false;saved.grid=32;saved.save(path);Settings preset;preset.load(path);CHECK((preset.selection_units()==SelectionUnits{32,32,32,32})&&preset.custom_selection_units==old.custom_selection_units);
    CHECK(WritePrivateProfileStringW(L"Viewer",L"selection_size_x",L"0",path.c_str()));CHECK(WritePrivateProfileStringW(L"Viewer",L"selection_size_y",L"1000",path.c_str()));
    preset.load(path);CHECK(preset.custom_selection_units.size_x==1&&preset.custom_selection_units.size_y==999);std::filesystem::remove(path);
}
TEST(selection_dialog_all_twelve_shortcuts_spin_ranges_validation_commit_and_cancel) {
    Fixture f;const SelectionUnits original{2,3,4,5};f.open(original);
    for(int row=0;row<4;++row) {
        int low{},high{};SendMessageW(f.dialog->control(id::Spin+row),UDM_GETRANGE32,WPARAM(&low),LPARAM(&high));CHECK(low==1&&high==999);
        for(int column=0;column<3;++column) {
            const int expected[]{1,8,16};std::wstring before[4];for(int n=0;n<4;++n)before[n]=text(f.dialog->control(id::StartX+n));
            f.click(id::Quick+row*3+column);CHECK(text(f.dialog->control(id::StartX+row))==std::to_wstring(expected[column]));
            for(int n=0;n<4;++n)if(n!=row)CHECK(text(f.dialog->control(id::StartX+n))==before[n]);
        }
    }
    SetWindowTextW(f.dialog->control(id::StartX),L"0");CHECK(!IsWindowEnabled(f.dialog->control(IDOK)));f.click(IDOK);CHECK(IsWindow(f.dialog->window()));
    SetWindowTextW(f.dialog->control(id::StartX),L"999");SetWindowTextW(f.dialog->control(id::SizeY),L"1000");CHECK(!IsWindowEnabled(f.dialog->control(IDOK)));
    SetWindowTextW(f.dialog->control(id::SizeY),L"1");f.click(IDOK);const auto result=f.dialog->result();CHECK((result&&*result==SelectionUnits{999,16,16,1}));
    f.dialog.reset();f.open(original);f.click(id::Quick);f.click(IDCANCEL);CHECK(!f.dialog->result());CHECK((original==SelectionUnits{2,3,4,5}));
}
TEST(selection_general_settings_preserve_custom_unless_common_grid_changes_or_reset) {
    Fixture f;Settings source;source.selection_custom=true;source.custom_selection_units={8,4,6,5};source.grid=1;source.zoom_step=45;Settings applied=source;int calls{};
    PreferencesDialog preferences(source,[&](const Settings &next){applied=next;++calls;});CHECK(preferences.create(f.owner));
    SendMessageW(preferences.control(preference_id::Undo),BM_CLICK,0,0);SendMessageW(preferences.control(preference_id::ApplyButton),BM_CLICK,0,0);CHECK(calls==1&&applied.selection_custom&&applied.custom_selection_units==source.custom_selection_units);
    SetWindowTextW(preferences.control(preference_id::Grid),L"8");SendMessageW(preferences.control(preference_id::ApplyButton),BM_CLICK,0,0);
    CHECK(calls==2&&!applied.selection_custom&&(applied.selection_units()==SelectionUnits{8,8,8,8})&&applied.custom_selection_units==source.custom_selection_units);
    SendMessageW(preferences.control(preference_id::Reset),BM_CLICK,0,0);SendMessageW(preferences.control(preference_id::ApplyButton),BM_CLICK,0,0);
    CHECK(calls==3&&!applied.selection_custom&&(applied.custom_selection_units==SelectionUnits{})&&applied.grid==1&&applied.zoom_step==45);
    SendMessageW(preferences.control(IDCANCEL),BM_CLICK,0,0);
}
int main(){gvn_test::ClipboardLock lock;if(!lock)return 2;auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;auto desktop=CreateDesktopW(L"GVN-selection-test",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(!initialize_controls())return 4;return run_tests();}
