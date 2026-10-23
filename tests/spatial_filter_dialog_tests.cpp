#include "gvn/spatial_filter_dialog.h"
#include "gvn/settings.h"
#include "gvn/codecs.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <chrono>
using namespace gvn;
namespace id=filter_control;
namespace {
const auto fixtures=std::filesystem::path(GVN_SOURCE_ROOT)/L"tests/fixtures/spatial-filter";
struct Fixture {
    HWND owner{};std::shared_ptr<Image> source=std::make_shared<Image>(load_image(fixtures/L"colors/source.png"));std::unique_ptr<SpatialFilterDialog> dialog;bool current{true};
    Fixture(){owner=CreateWindowExW(0,L"STATIC",L"Owned filter test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1000,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);}
    ~Fixture(){dialog.reset();DestroyWindow(owner);}
    void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(dialog&&IsWindow(dialog->window())&&IsDialogMessageW(dialog->window(),&m))continue;TranslateMessage(&m);DispatchMessageW(&m);}}
    template<class F>void until(F predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);do{pump();if(predicate())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(std::chrono::steady_clock::now()<end);CHECK(predicate());}
    void open(SpatialFilter choice){dialog=std::make_unique<SpatialFilterDialog>(source,0,Rect{},choice,[&]{return current;});CHECK(dialog->create(owner));ShowWindow(dialog->window(),SW_SHOWNOACTIVATE);settle();}
    void settle(){until([&]{return dialog->ready();});}
    void set(int key,const wchar_t *text){SetWindowTextW(dialog->control(key),text);}
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);pump();if(IsWindow(dialog->window()))settle();}
    void scroll(int n,int value){SendMessageW(dialog->window(),WM_HSCROLL,MAKEWPARAM(SB_THUMBPOSITION,WORD(value)),reinterpret_cast<LPARAM>(dialog->control(id::ScrollRadius+n)));}
    SCROLLINFO info(int n){SCROLLINFO result{sizeof(result),SIF_ALL};CHECK(GetScrollInfo(dialog->control(id::ScrollRadius+n),SB_CTL,&result));return result;}
};
void matches(const Image &actual,const std::filesystem::path &path){const auto expected=load_image(path);CHECK(actual.width==expected.width&&actual.height==expected.height);for(size_t n=0;n<actual.frames[0].pixels.size();++n){const auto a=actual.frames[0].pixels[n],b=expected.frames[0].pixels[n];CHECK(a.a==b.a&&std::abs(int(a.r)-b.r)<=2&&std::abs(int(a.g)-b.g)<=2&&std::abs(int(a.b)-b.b)<=2);}}
}
TEST(filter_dialog_actual_edits_scrollbars_reference_preview_and_cancel_are_connected) {
    Fixture f;f.open({true});CHECK(f.dialog->valid()&&f.dialog->options()==SpatialFilter{true});CHECK(f.info(0).nMin==1&&f.info(0).nMax==100&&f.info(1).nMin==1&&f.info(1).nMax==400&&f.info(2).nMin==0&&f.info(2).nMax==255);
    const auto original=f.source->frames[0].pixels;f.scroll(0,8);f.scroll(1,200);f.settle();matches(f.dialog->adjusted_preview(),fixtures/L"colors/sharpen-strong.png");
    for(auto value:{L"",L"0",L"10.1",L"0.15",L"nan",L"1e0"}){f.set(id::Radius,value);CHECK(!f.dialog->valid()&&!IsWindowEnabled(f.dialog->control(IDOK)));f.set(id::Radius,L"0.6");f.settle();CHECK(f.dialog->valid());}
    f.set(id::Amount,L"150");f.set(id::Threshold,L"15");f.settle();matches(f.dialog->adjusted_preview(),fixtures/L"colors/sharpen-edges.png");CHECK(f.source->frames[0].pixels==original);f.click(IDCANCEL);CHECK(!f.dialog->result()&&f.source->frames[0].pixels==original);
    f.open({false});CHECK(!f.dialog->control(id::Threshold)&&f.info(1).nMin==0&&f.info(1).nMax==99);f.scroll(0,50);f.scroll(1,65);f.settle();matches(f.dialog->adjusted_preview(),fixtures/L"colors/blur-soft.png");
    f.set(id::Amount,L"100");CHECK(!f.dialog->valid());f.set(id::Amount,L"65");f.settle();f.click(IDOK);CHECK(f.dialog->result()==SpatialFilter{false,50,100,0,65}&&f.source->frames[0].pixels==original);
}
TEST(filter_dialog_large_source_pan_halo_alpha_and_lost_document_are_bounded) {
    Fixture f;*f.source=new_image(1024,512);auto &pixels=f.source->frames[0].pixels;for(size_t n=0;n<pixels.size();++n)pixels[n]={uint8_t(n),uint8_t(n>>5),uint8_t(n>>10),uint8_t(n%256)};
    const auto original=pixels;f.open({false,8});CHECK(f.dialog->original_preview().width==256&&f.dialog->original_preview().height==160);const auto full=spatial_filter(*f.source,{false,8});
    const auto check_view=[&]{const auto area=f.dialog->viewport();const auto &preview=f.dialog->adjusted_preview();CHECK(preview.width==256&&preview.height==160);for(int y=0;y<area.h;++y)for(int x=0;x<area.w;++x)CHECK(preview.frames[0].pixels[size_t(y)*area.w+x]==full.frames[0].pixels[size_t(y+area.y)*full.width+x+area.x]);};
    check_view();const auto before=f.dialog->viewport();SendMessageW(f.dialog->control(id::Preview),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,50));SendMessageW(f.dialog->control(id::Preview),WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(60,20));SendMessageW(f.dialog->control(id::Preview),WM_LBUTTONUP,0,MAKELPARAM(60,20));f.settle();CHECK(f.dialog->viewport().x==before.x+40&&f.dialog->viewport().y==before.y+30);check_view();
    f.dialog->pan(10000,10000);f.settle();CHECK(f.dialog->viewport().x==768&&f.dialog->viewport().y==352);check_view();CHECK(pixels==original);
    f.current=false;f.until([&]{return !f.dialog->valid()&&!IsWindowEnabled(f.dialog->control(IDOK));});SendMessageW(f.dialog->window(),WM_COMMAND,IDOK,0);CHECK(IsWindow(f.dialog->window())&&!f.dialog->result());f.click(IDCANCEL);CHECK(pixels==original);
}
TEST(filter_preferences_roundtrip_independent_dialog_values) {
    Settings settings;CHECK(settings.filter_choice(true)==SpatialFilter{true}&&settings.filter_choice(false)==SpatialFilter{false});GUID guid{};CHECK(SUCCEEDED(CoCreateGuid(&guid)));wchar_t name[40]{};StringFromGUID2(guid,name,40);const auto folder=std::filesystem::temp_directory_path()/(std::wstring(L"GVN-filter-")+name);std::filesystem::create_directory(folder);const auto path=folder/L"settings.ini";
    settings.unsharp_radius=10;settings.unsharp_strength=180;settings.unsharp_threshold=17;settings.gaussian_radius=50;settings.gaussian_transparency=65;settings.save(path);Settings loaded;loaded.load(path);
    CHECK(loaded.filter_choice(true)==SpatialFilter{true,10,180,17,65}&&loaded.filter_choice(false)==SpatialFilter{false,50,180,17,65});std::filesystem::remove(path);std::filesystem::remove(folder);
}
int main(){gvn_test::ClipboardLock lock;if(!lock)return 2;const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;const auto desktop=CreateDesktopW(L"GVN-filter-ui",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;const auto result=run_tests();OleUninitialize();return result;}
