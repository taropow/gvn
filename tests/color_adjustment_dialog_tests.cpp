#include "gvn/color_adjustment_dialog.h"
#include "gvn/settings.h"
#include "gvn/codecs.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <chrono>
using namespace gvn;
namespace id=color_adjust_control;
namespace {
const auto fixtures=std::filesystem::path(GVN_SOURCE_ROOT)/L"tests/fixtures";
struct Fixture {
    HWND owner{};std::shared_ptr<Image> source=std::make_shared<Image>(load_image(fixtures/L"color-adjustment/source.png"));
    std::unique_ptr<ColorAdjustmentDialog> dialog;bool current{true};
    Fixture(){owner=CreateWindowExW(0,L"STATIC",L"Owned adjustment test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1000,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);}
    ~Fixture(){dialog.reset();DestroyWindow(owner);}
    void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(dialog&&IsWindow(dialog->window())&&IsDialogMessageW(dialog->window(),&m))continue;TranslateMessage(&m);DispatchMessageW(&m);}}
    template<class F>void until(F predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);do{pump();if(predicate())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(std::chrono::steady_clock::now()<end);CHECK(predicate());}
    void open(ColorAdjustment choice={},Rect roi={}){dialog=std::make_unique<ColorAdjustmentDialog>(source,0,roi,choice,[&]{return current;});CHECK(dialog->create(owner));ShowWindow(dialog->window(),SW_SHOWNOACTIVATE);settle();}
    void settle(){until([&]{return dialog->ready();});}
    void set(int key,const wchar_t *value){SetWindowTextW(dialog->control(key),value);}
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);pump();settle_if_open();}
    void settle_if_open(){if(IsWindow(dialog->window()))settle();}
    void gray(int index){SendMessageW(dialog->control(id::Gray),CB_SETCURSEL,index,0);SendMessageW(dialog->window(),WM_COMMAND,MAKEWPARAM(id::Gray,CBN_SELCHANGE),reinterpret_cast<LPARAM>(dialog->control(id::Gray)));}
    SCROLLINFO scroll_info(int axis){SCROLLINFO info{sizeof(info),SIF_ALL};CHECK(GetScrollInfo(dialog->control(id::ScrollHue+axis),SB_CTL,&info));return info;}
    void scroll_to(int axis,int value){SendMessageW(dialog->window(),WM_HSCROLL,MAKEWPARAM(SB_THUMBPOSITION,WORD(value)),reinterpret_cast<LPARAM>(dialog->control(id::ScrollHue+axis)));}
};
void within_one(const Image &actual,const Image &expected) {
    CHECK(actual.width==expected.width&&actual.height==expected.height);for(size_t i=0;i<actual.frames[0].pixels.size();++i){const auto a=actual.frames[0].pixels[i],b=expected.frames[0].pixels[i];CHECK(a.a==b.a&&std::abs(int(a.r)-b.r)<=1&&std::abs(int(a.g)-b.g)<=1&&std::abs(int(a.b)-b.b)<=1);}
}
}
TEST(adjustment_dialog_numbers_scrollbars_resets_and_actual_preview_reference_are_connected) {
    Fixture f;f.open();const auto original=f.source->frames[0].pixels;CHECK(f.dialog->options().neutral()&&f.dialog->valid());
    const int values[]{75,-40,15};for(int n=0;n<3;++n)f.scroll_to(n,values[n]);f.settle();
    CHECK(f.dialog->options()==ColorAdjustment{75,-40,15,false,GrayMethod::Luma});
    for(int n=0;n<3;++n)CHECK(f.scroll_info(n).nPos==values[n]&&int(GetDlgItemInt(f.dialog->window(),id::Hue+n,nullptr,TRUE))==values[n]);
    within_one(f.dialog->adjusted_preview(),load_image(fixtures/L"color-adjustment/combined-desaturate.png"));CHECK(f.dialog->original_preview().frames[0].pixels==original&&f.source->frames[0].pixels==original);
    for(int n=0;n<3;++n)f.click(id::ResetHue+n);CHECK(f.dialog->options().neutral()&&f.dialog->adjusted_preview().frames[0].pixels==original);
    f.click(IDOK);CHECK(f.dialog->result()==ColorAdjustment{});
}
TEST(adjustment_dialog_unified_ranges_last_values_gray_choice_and_cancel_match_reference) {
    Fixture f;f.open({45,60,20,true,GrayMethod::Average});within_one(f.dialog->adjusted_preview(),load_image(fixtures/L"color-adjustment/unify-light.png"));
    CHECK(f.scroll_info(0).nMin==0&&f.scroll_info(0).nMax==360&&f.scroll_info(1).nMin==0&&f.scroll_info(1).nMax==100);
    f.click(id::Unified);CHECK(f.dialog->options()==ColorAdjustment{0,0,0,false,GrayMethod::Average});CHECK(f.scroll_info(0).nMin==-180&&f.scroll_info(1).nMin==-100);
    f.click(id::Unified);CHECK(f.dialog->options()==ColorAdjustment{0,25,0,true,GrayMethod::Average});
    f.set(id::Hue,L"240");f.set(id::Saturation,L"100");f.gray(2);f.settle();within_one(f.dialog->adjusted_preview(),load_image(fixtures/L"color-adjustment/unify-blue.png"));
    const auto before=f.source->frames[0].pixels;f.click(IDCANCEL);CHECK(!f.dialog->result()&&f.source->frames[0].pixels==before);
}
TEST(adjustment_dialog_invalid_numbers_and_lost_source_withdraw_preview_and_block_ok) {
    Fixture f;f.open();const auto original=f.dialog->original_preview().frames[0].pixels;
    for(auto value:{L"",L"-",L"x",L"181",L"-181"}){f.set(id::Hue,value);CHECK(!f.dialog->valid()&&!IsWindowEnabled(f.dialog->control(IDOK))&&f.dialog->adjusted_preview().frames[0].pixels==original);f.set(id::Hue,L"75");f.settle();CHECK(f.dialog->valid());}
    f.set(id::Saturation,L"-101");CHECK(!f.dialog->valid());f.click(id::Unified);CHECK(f.dialog->valid()&&f.dialog->options().saturation==25);
    f.set(id::Hue,L"-1");CHECK(!f.dialog->valid());f.set(id::Hue,L"360");f.set(id::Saturation,L"101");CHECK(!f.dialog->valid());f.set(id::Saturation,L"75");f.settle();
    f.current=false;f.until([&]{return !f.dialog->valid()&&!IsWindowEnabled(f.dialog->control(IDOK))&&f.dialog->adjusted_preview().frames[0].pixels==original;});
    SendMessageW(f.dialog->window(),WM_COMMAND,IDOK,0);CHECK(IsWindow(f.dialog->window())&&!f.dialog->result());f.click(IDCANCEL);CHECK(!f.dialog->result());
}
TEST(adjustment_dialog_preview_is_bounded_and_nearest_source_roi_never_colors_outside_selection) {
    Fixture f;f.source=std::make_shared<Image>();f.source->width=1280;f.source->height=720;f.source->frames.resize(1);
    for(unsigned y=0;y<720;++y)for(unsigned x=0;x<1280;++x)f.source->frames[0].pixels.push_back({uint8_t(x),uint8_t(y),uint8_t(x+y),uint8_t(x*13+y*17)});
    const auto original=f.source->frames[0].pixels;const ColorAdjustment choice{90,0,0,false,GrayMethod::Luma};const Rect roi{5,6,9,13};f.open(choice,roi);
    const auto &before=f.dialog->original_preview();const auto &after=f.dialog->adjusted_preview();CHECK(before.width==256&&before.height==144&&before.frames[0].pixels.size()<=256*192);
    const auto whole=color_adjustment(before,choice);CHECK(whole);
    for(unsigned y=0;y<before.height;++y)for(unsigned x=0;x<before.width;++x){const auto sx=uint64_t(x)*1280/before.width,sy=uint64_t(y)*720/before.height;const bool inside=sx>=5&&sx<14&&sy>=6&&sy<19;const auto at=size_t(y)*before.width+x;CHECK(after.frames[0].pixels[at]==(inside?whole->frames[0].pixels[at]:before.frames[0].pixels[at]));}
    CHECK(f.source->frames[0].pixels==original);f.dialog.reset();f.open(choice,{1,1,1,1});CHECK(f.dialog->adjusted_preview().frames[0].pixels==f.dialog->original_preview().frames[0].pixels);
}
TEST(adjustment_dialog_dpi_text_fonts_bounds_and_settings_roundtrip_keep_saved_unified_values) {
    Fixture f;f.open();
    for(UINT dpi:{96u,144u,192u,96u}) {
        RECT size{10,10,10+MulDiv(650,dpi,96),10+MulDiv(590,dpi,96)};SendMessageW(f.dialog->window(),WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&size));f.pump();
        RECT client{};GetClientRect(f.dialog->window(),&client);std::vector<RECT> areas;
        for(int key:{id::Original,id::Adjusted,id::Hue,id::Saturation,id::Lightness,id::ResetHue,id::ResetSaturation,id::ResetLightness,id::ScrollHue,id::ScrollSaturation,id::ScrollLightness,id::Unified,id::Gray,id::Error,id::Alpha,id::LabelHue,id::LabelSaturation,id::LabelLightness,id::LabelOriginal,id::LabelAdjusted,id::LabelGray,IDOK,IDCANCEL}) {
            const auto h=f.dialog->control(key);CHECK(h&&(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE));RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,f.dialog->window(),reinterpret_cast<POINT*>(&r),2);CHECK(r.left>=0&&r.top>=0&&r.right<=client.right&&r.bottom<=client.bottom);
            for(const auto &other:areas){RECT intersection{};CHECK(!IntersectRect(&intersection,&r,&other));}areas.push_back(r);
            if(key!=id::Original&&key!=id::Adjusted&&!(key>=id::ScrollHue&&key<=id::ScrollLightness)){LOGFONTW font{};CHECK(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0)),sizeof(font),&font));CHECK(font.lfHeight==-MulDiv(9,dpi,72));}
        }
    }
    Settings initial;CHECK(initial.color_adjustment_choice()==ColorAdjustment{});GUID guid{};CHECK(SUCCEEDED(CoCreateGuid(&guid)));wchar_t name[40]{};StringFromGUID2(guid,name,40);
    const auto directory=std::filesystem::temp_directory_path()/(std::wstring(L"GVN-adjustment-")+name);std::filesystem::create_directory(directory);const auto path=directory/L"settings.ini";
    initial.color_adjust_unified=true;initial.color_adjust_hue=45;initial.color_adjust_saturation=60;initial.color_adjust_lightness=20;initial.color_adjust_gray=1;initial.save(path);
    Settings loaded;loaded.load(path);CHECK(loaded.color_adjustment_choice()==ColorAdjustment{45,60,20,true,GrayMethod::Average});loaded.color_adjust_unified=false;loaded.save(path);
    Settings normal;normal.load(path);CHECK(normal.color_adjustment_choice()==ColorAdjustment{0,0,0,false,GrayMethod::Average}&&normal.color_adjust_hue==45&&normal.color_adjust_saturation==60&&normal.color_adjust_lightness==20&&normal.color_depth_dither);
    std::filesystem::remove(path);std::filesystem::remove(directory);
}
int main() {
    gvn_test::ClipboardLock lock;if(!lock)return 2;const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
    if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;const auto desktop=CreateDesktopW(L"GVN-adjustment-ui",nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop||!SetThreadDesktop(desktop))return 3;SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;const auto result=run_tests();OleUninitialize();return result;
}
