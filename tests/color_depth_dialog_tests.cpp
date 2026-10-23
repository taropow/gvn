#include "gvn/color_depth_dialog.h"
#include "gvn/settings.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <commctrl.h>
#include <chrono>
using namespace gvn;
namespace id=color_depth_control;
namespace {
constexpr ColorDepth choices[]{ColorDepth::Indexed8,ColorDepth::Rgb555,ColorDepth::Rgb565,ColorDepth::Rgb24,ColorDepth::Optimal};
struct Fixture {
    HWND owner{};std::unique_ptr<ColorDepthDialog> dialog;bool current{true};
    Fixture(){owner=CreateWindowExW(0,L"STATIC",L"Owned color depth test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,800,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);}
    ~Fixture(){dialog.reset();DestroyWindow(owner);}
    void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(dialog&&IsWindow(dialog->window())&&IsDialogMessageW(dialog->window(),&message))continue;TranslateMessage(&message);DispatchMessageW(&message);}}
    void open(unsigned bits=24,std::optional<Rgb16Layout> layout={},bool dither=true){dialog=std::make_unique<ColorDepthDialog>(bits,layout,dither,[&]{return current;});CHECK(dialog->create(owner));ShowWindow(dialog->window(),SW_SHOWNOACTIVATE);pump();CHECK(dialog->ready());}
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);pump();}
};
}
TEST(color_depth_dialog_choices_disable_only_current_format_and_accept_exact_options) {
    for(unsigned bits:{1u,2u,4u,8u,16u,24u})for(auto layout:{Rgb16Layout::Rgb555,Rgb16Layout::Rgb565}) {
        Fixture f;f.open(bits,bits==16?std::optional(layout):std::nullopt);
        CHECK(f.dialog->options()==ColorDepthChoice{ColorDepth::Optimal,true});
        for(int n=0;n<5;++n) {
            const bool enabled=!(bits==8&&n==0)&&!(bits==16&&((layout==Rgb16Layout::Rgb555&&n==1)||(layout==Rgb16Layout::Rgb565&&n==2)))&&!(bits==24&&n==3);
            CHECK(bool(IsWindowEnabled(f.dialog->control(id::ChoiceBase+n)))==enabled);
            if(!enabled)continue;f.click(id::ChoiceBase+n);CHECK(f.dialog->options().depth==choices[n]);
            for(int j=0;j<5;++j)CHECK(SendMessageW(f.dialog->control(id::ChoiceBase+j),BM_GETCHECK,0,0)==(n==j?BST_CHECKED:BST_UNCHECKED));
        }
        f.click(id::Dither);CHECK(!f.dialog->options().dither);f.click(IDOK);CHECK(f.dialog->result()==ColorDepthChoice{ColorDepth::Optimal,false});
    }
}
TEST(color_depth_dialog_wheel_on_dialog_and_radio_skips_disabled_choice_and_accumulates_half_ticks) {
    Fixture f;f.open();
    SendMessageW(f.dialog->window(),WM_MOUSEWHEEL,MAKEWPARAM(0,120),0);CHECK(f.dialog->options().depth==ColorDepth::Rgb565);
    SendMessageW(f.dialog->control(id::ChoiceBase+2),WM_MOUSEWHEEL,MAKEWPARAM(0,WORD(-120)),0);CHECK(f.dialog->options().depth==ColorDepth::Optimal);
    SendMessageW(f.dialog->window(),WM_MOUSEWHEEL,MAKEWPARAM(0,60),0);CHECK(f.dialog->options().depth==ColorDepth::Optimal);
    SendMessageW(f.dialog->window(),WM_MOUSEWHEEL,MAKEWPARAM(0,60),0);CHECK(f.dialog->options().depth==ColorDepth::Rgb565);
    f.click(id::Dither);f.click(IDCANCEL);CHECK(!f.dialog->result());
}
TEST(color_depth_dialog_lost_source_blocks_ok_but_cancel_remains_available) {
    THROWS(ColorDepthDialog(3,{},true));THROWS(ColorDepthDialog(16,{},true));
    Fixture f;f.open();f.current=false;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(IsWindowEnabled(f.dialog->control(IDOK))&&std::chrono::steady_clock::now()<end){f.pump();MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}
    CHECK(!f.dialog->valid()&&!IsWindowEnabled(f.dialog->control(IDOK))&&IsWindowEnabled(f.dialog->control(IDCANCEL)));
    SendMessageW(f.dialog->window(),WM_COMMAND,IDOK,0);CHECK(IsWindow(f.dialog->window())&&!f.dialog->result());f.click(IDCANCEL);CHECK(!f.dialog->result());
}
TEST(color_depth_dialog_dpi_layout_fonts_and_saved_dither_remain_independent) {
    Fixture f;f.open(8,{},false);CHECK(!f.dialog->options().dither);
    for(UINT dpi:{96u,144u,192u,96u}) {
        RECT area{10,10,10+MulDiv(450,dpi,96),10+MulDiv(430,dpi,96)};SendMessageW(f.dialog->window(),WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&area));f.pump();
        RECT client{};GetClientRect(f.dialog->window(),&client);std::vector<RECT> bounds;
        for(int key:{id::ChoiceBase,id::ChoiceBase+1,id::ChoiceBase+2,id::ChoiceBase+3,id::ChoiceBase+4,id::Dither,id::Current,id::Alpha,id::Error,IDOK,IDCANCEL}) {
            const auto h=f.dialog->control(key);CHECK(h&&(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE));RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,f.dialog->window(),reinterpret_cast<POINT*>(&r),2);
            CHECK(r.left>=0&&r.top>=0&&r.right<=client.right&&r.bottom<=client.bottom);
            for(const auto &other:bounds){RECT intersection{};CHECK(!IntersectRect(&intersection,&r,&other));}bounds.push_back(r);
            LOGFONTW font{};CHECK(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0)),sizeof(font),&font));CHECK(font.lfHeight==-MulDiv(9,dpi,72));
        }
    }
    Settings initial;CHECK(initial.color_depth_dither);GUID guid{};CHECK(SUCCEEDED(CoCreateGuid(&guid)));wchar_t name[40]{};StringFromGUID2(guid,name,40);
    const auto directory=std::filesystem::temp_directory_path()/(std::wstring(L"GVN-color-depth-")+name);std::filesystem::create_directory(directory);
    initial.color_depth_dither=false;initial.palette_preview=true;initial.saving.dither=true;const auto path=directory/L"settings.ini";initial.save(path);
    Settings loaded;loaded.load(path);CHECK(!loaded.color_depth_dither&&loaded.palette_preview&&loaded.saving.dither);
    std::filesystem::remove(path);std::filesystem::remove(directory);
}
int main() {
    gvn_test::ClipboardLock lock;if(!lock)return 2;const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
    if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;
    const auto desktop=CreateDesktopW(L"GVN-color-depth-ui",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;
    const auto result=run_tests();OleUninitialize();return result;
}
