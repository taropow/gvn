#include "gvn/selection_rect_dialog.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <array>
using namespace gvn;
namespace id=selection_rect_control;
namespace {
std::wstring text(HWND h){std::wstring s(size_t(GetWindowTextLengthW(h))+1,L'\0');GetWindowTextW(h,s.data(),int(s.size()));s.pop_back();return s;}
void same(Rect a,Rect b){CHECK(a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h);}
struct Fixture {
    HWND owner{};std::unique_ptr<SelectionRectDialog> dialog;
    Fixture(){owner=CreateWindowExW(0,L"STATIC",L"Owned numeric selection tests",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);}
    ~Fixture(){dialog.reset();DestroyWindow(owner);}
    void open(Rect r={8,8,24,20},uint32_t w=96,uint32_t h=80){dialog=std::make_unique<SelectionRectDialog>(w,h,r);CHECK(dialog->create(owner)&&dialog->ready());}
    void set(int key,int value){SetWindowTextW(dialog->control(key),std::to_wstring(value).c_str());}
    std::array<int,6> values(){std::array<int,6> out{};for(int n=0;n<6;++n)out[n]=std::stoi(text(dialog->control(id::Left+n)));return out;}
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);}
    void spin(int field,bool increase) {
        const int n=field-id::Left;const auto h=dialog->control(id::Spin+n);RECT r{};CHECK(GetClientRect(h,&r));
        const bool vertical=n%2;const int x=vertical?r.right/2:increase?3*r.right/4:r.right/4;
        const int y=vertical?(increase?3*r.bottom/4:r.bottom/4):r.bottom/2;
        SendMessageW(h,WM_MOUSEMOVE,0,MAKELPARAM(x,y));SendMessageW(h,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(h,WM_LBUTTONUP,0,MAKELPARAM(x,y));
    }
};
}
TEST(selection_numeric_six_fields_match_ten_reference_links_and_edge_shifts) {
    struct Case {int field,value;std::array<int,6> expected;};
    for(const auto c:{Case{id::Left,12,{12,8,31,27,20,20}},Case{id::Top,10,{8,10,31,27,24,18}},
        Case{id::Right,35,{8,8,35,27,28,20}},Case{id::Bottom,29,{8,8,31,29,24,22}},
        Case{id::Width,18,{8,8,25,27,18,20}},Case{id::Height,17,{8,8,31,24,24,17}},
        Case{id::Left,90,{90,8,31,27,60,20}},Case{id::Right,5,{8,8,5,27,4,20}},
        Case{id::Width,90,{6,8,95,27,90,20}},Case{id::Height,75,{8,5,31,79,24,75}}}) {
        Fixture f;f.open();f.set(c.field,c.value);CHECK(f.values()==c.expected&&IsWindowEnabled(f.dialog->control(IDOK)));
        f.click(IDOK);CHECK(f.dialog->result());const auto r=*f.dialog->result();
        CHECK(r.x==std::min(c.expected[0],c.expected[2])&&r.y==std::min(c.expected[1],c.expected[3])&&r.w==c.expected[4]&&r.h==c.expected[5]);
    }
}
TEST(selection_numeric_prefills_current_or_full_and_normalizes_reversed_on_commit) {
    Fixture f;f.open({});CHECK((f.values()==std::array<int,6>{0,0,95,79,96,80}));f.click(IDCANCEL);
    f.open();CHECK((f.values()==std::array<int,6>{8,8,31,27,24,20}));f.set(id::Left,90);f.click(IDOK);
    const auto result=f.dialog->result();CHECK(result);same(*result,{31,8,60,20});f.open(*result);CHECK((f.values()==std::array<int,6>{31,8,90,27,60,20}));f.click(IDCANCEL);
    f.open({-5,-4,10,10});same(*f.dialog->bounds(),{0,0,5,6});
}
TEST(selection_numeric_spin_ranges_and_actual_arrows_follow_reference_axes) {
    Fixture f;f.open();const int low[]{0,79,0,79,1,80},high[]{95,0,95,0,96,1};
    for(int n=0;n<6;++n){int a{},b{};SendMessageW(f.dialog->control(id::Spin+n),UDM_GETRANGE32,WPARAM(&a),LPARAM(&b));CHECK(a==low[n]&&b==high[n]);}
    for(int n=0;n<6;++n) {
        f.open();const auto before=f.values();f.spin(id::Left+n,false);CHECK(f.values()[n]==before[n]-1);
        f.spin(id::Left+n,true);CHECK(f.values()[n]==before[n]);
    }
    f.open();f.set(id::Top,10);f.spin(id::Top,false);CHECK(f.values()[1]==9);
}
TEST(selection_numeric_invalid_input_stays_open_and_cancel_discards_edits) {
    Fixture f;f.open();const Rect original{8,8,24,20};
    SetWindowTextW(f.dialog->control(id::Left),L"");CHECK(!f.dialog->bounds()&&!IsWindowEnabled(f.dialog->control(IDOK)));
    f.set(id::Width,10);CHECK(text(f.dialog->control(id::Left)).empty()&&!f.dialog->bounds());
    f.set(id::Left,8);f.set(id::Width,0);CHECK(!f.dialog->bounds());SendMessageW(f.dialog->window(),WM_COMMAND,IDOK,0);CHECK(IsWindow(f.dialog->window())&&!f.dialog->result());
    f.set(id::Width,97);CHECK(!f.dialog->bounds());f.set(id::Width,18);CHECK(f.dialog->bounds());
    SetWindowTextW(f.dialog->control(id::Top),L"-1");CHECK(!f.dialog->bounds());f.set(id::Top,8);f.click(IDCANCEL);CHECK(!f.dialog->result());same(original,{8,8,24,20});
}
TEST(selection_numeric_single_pixel_and_large_axis_use_full_native_ranges) {
    Fixture f;f.open({},1,1);CHECK((f.values()==std::array<int,6>{0,0,0,0,1,1}));f.click(IDOK);same(*f.dialog->result(),{0,0,1,1});
    f.open({},65535,1);CHECK(text(f.dialog->control(id::Width))==L"65535");f.set(id::Left,65534);CHECK(text(f.dialog->control(id::Width))==L"1");f.click(IDOK);same(*f.dialog->result(),{65534,0,1,1});
    THROWS(SelectionRectDialog(0,1,{}));THROWS(SelectionRectDialog(1,0,{}));
}
int main(){gvn_test::ClipboardLock lock;if(!lock)return 2;auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;auto desktop=CreateDesktopW(L"GVN-selection-rect-test",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(!initialize_controls())return 4;return run_tests();}
