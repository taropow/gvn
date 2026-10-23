#include "gvn/brightness_dialog.h"
#include "gvn/codecs.h"
#include "gvn/saving.h"
#include "gvn/settings.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <chrono>
#include <fstream>
#include <set>
using namespace gvn;
namespace id=brightness_control;
namespace {
const auto fixtures=std::filesystem::path(GVN_SOURCE_ROOT)/L"tests/fixtures";
Image processed(const Image &source,const BrightnessAdjustment &choice){auto result=adjust_brightness(source,choice);return result?std::move(*result):source;}
struct Fixture {
    HWND owner{};std::shared_ptr<const Image> shown;std::unique_ptr<BrightnessDialog> dialog;Image source=load_image(fixtures/L"brightness/source.png");bool current{true},fail{};unsigned processed{};
    Fixture(){owner=CreateWindowExW(0,L"STATIC",L"Owned brightness test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);}
    ~Fixture(){dialog.reset();DestroyWindow(owner);}
    void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(dialog&&IsWindow(dialog->window())&&IsDialogMessageW(dialog->window(),&m))continue;TranslateMessage(&m);DispatchMessageW(&m);}}
    template<class F>void until(F condition){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);do{pump();if(condition())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(std::chrono::steady_clock::now()<deadline);CHECK(condition());}
    void settle(){until([&]{return dialog->ready();});}
    void open(BrightnessChoice choice={}){dialog=std::make_unique<BrightnessDialog>(choice,[&](const BrightnessAdjustment &value){++processed;if(fail)throw std::bad_alloc();auto result=adjust_brightness(source,value);return result?std::make_shared<Image>(std::move(*result)):nullptr;},[&](std::shared_ptr<const Image> image){shown=std::move(image);},[&]{return current;});CHECK(dialog->create(owner));ShowWindow(dialog->window(),SW_SHOWNOACTIVATE);settle();}
    void set(int key,const wchar_t *text){SetWindowTextW(dialog->control(key),text);}
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);pump();if(IsWindow(dialog->window()))settle();}
    void scroll(int axis,int position){SendMessageW(dialog->window(),WM_HSCROLL,MAKEWPARAM(SB_THUMBPOSITION,WORD(position)),reinterpret_cast<LPARAM>(dialog->control(id::ScrollBrightness+axis)));}
    SCROLLINFO info(int axis){SCROLLINFO value{sizeof(value),SIF_ALL};CHECK(GetScrollInfo(dialog->control(id::ScrollBrightness+axis),SB_CTL,&value));return value;}
};
}
TEST(brightness_reference_all_parameters_and_combinations_match_observed_rgb_tables) {
    const auto source=load_image(fixtures/L"brightness/source.png");std::ifstream cases(fixtures/L"brightness/cases.tsv");std::string name;int b{},c{},g{},tolerance{},checked{};
    while(cases>>name>>b>>c>>g>>tolerance){const auto actual=processed(source,{b,c,g}),reference=load_image(fixtures/L"brightness"/(name+".png"));CHECK(actual.width==reference.width&&actual.height==reference.height);for(size_t n=0;n<actual.frames[0].pixels.size();++n){const auto a=actual.frames[0].pixels[n],r=reference.frames[0].pixels[n];CHECK(a.a==r.a&&std::abs(int(a.r)-r.r)<=tolerance&&std::abs(int(a.g)-r.g)<=tolerance&&std::abs(int(a.b)-r.b)<=tolerance);}++checked;}CHECK(checked==18);
}
TEST(brightness_alpha_selection_palette_numbers_and_failed_allocation_preserve_source) {
    const auto source=load_image(fixtures/L"palette/png-4-alpha.png");const auto before=source.frames[0].pixels;const auto indices=source.frames[0].indexed;CHECK(indices);const BrightnessAdjustment choice{20,35,1250};const auto whole=adjust_brightness(source,choice);CHECK(whole&&whole->frames[0].indexed&&whole->frames[0].indexed->packed==indices->packed&&whole->frames[0].indexed->palette.size()==indices->palette.size());
    const Rect roi{1,1,2,1};const auto partial=adjust_brightness(source,choice,roi);CHECK(partial&&!partial->frames[0].indexed);for(int y=0;y<int(source.height);++y)for(int x=0;x<int(source.width);++x){const auto at=size_t(y)*source.width+x;CHECK(whole->frames[0].pixels[at].a==before[at].a);CHECK(partial->frames[0].pixels[at]==(y==1&&x>=1&&x<3?whole->frames[0].pixels[at]:before[at]));}
    CHECK(decode(encode_for_save(*whole,0,SaveFormat::Png,{})).frames[0].pixels==whole->frames[0].pixels);CHECK(!adjust_brightness(source,{})&&!adjust_brightness(source,choice,{1000,1000,2,2}));
    EditMemory memory;memory.before_allocate=[](size_t){throw std::bad_alloc();};THROWS(adjust_brightness(source,choice,{},memory));CHECK(source.frames[0].pixels==before&&source.frames[0].indexed==indices);THROWS(adjust_brightness(source,{256,0,1000}));THROWS(adjust_brightness(source,{0,-256,1000}));THROWS(adjust_brightness(source,{0,0,399}));THROWS(adjust_brightness(source,{0,0,2501}));auto invalid=source;invalid.frames[0].pixels.pop_back();THROWS(adjust_brightness(invalid,{}));
}
TEST(brightness_actual_controls_gamma_mapping_reset_and_preview_candidate_reuse) {
    Fixture f;f.open();CHECK(f.processed==0&&!f.shown&&f.dialog->options()==BrightnessChoice{});CHECK(f.info(0).nMin==-255&&f.info(0).nMax==255&&f.info(2).nMin==-150&&f.info(2).nMax==150);
    for(const auto [position,gamma]:{std::pair{-150,400},std::pair{-100,600},std::pair{-1,996},std::pair{1,1010},std::pair{100,2000},std::pair{150,2500}}){f.scroll(2,position);f.settle();CHECK(f.dialog->options().adjustment.gamma==gamma&&f.shown);}
    f.set(id::Gamma,L"0.750");f.settle();CHECK(f.info(2).nPos==-62);f.set(id::Brightness,L"20");f.set(id::Contrast,L"35");f.set(id::Gamma,L"1.250");f.settle();CHECK(f.shown&&f.dialog->options().adjustment==BrightnessAdjustment{20,35,1250});
    const auto count=f.processed;f.click(id::Preview);CHECK(!f.shown&&!f.dialog->options().preview);f.set(id::Brightness,L"30");f.settle();CHECK(f.processed==count&&!f.shown);f.click(id::Preview);CHECK(f.shown&&f.processed==count+1);const auto candidate=f.shown;f.click(IDOK);CHECK(f.dialog->result()&&f.dialog->result()->image==candidate&&!f.shown&&f.processed==count+1);
    f.open();f.set(id::Brightness,L"35");f.set(id::Contrast,L"40");f.set(id::Gamma,L"1.700");f.settle();f.click(id::ResetBrightness);f.click(id::ResetContrast);f.click(id::ResetGamma);CHECK(f.dialog->options().adjustment.neutral()&&!f.shown);f.click(IDOK);CHECK(f.dialog->result()&&!f.dialog->result()->image);
}
TEST(brightness_invalid_input_failure_and_lost_context_withdraw_preview_and_cancel) {
    Fixture f;f.open();f.set(id::Brightness,L"25");f.settle();CHECK(f.shown);
    for(const auto *bad:{L"",L"0.399",L"2.501",L"1.0001",L"1e0"}){f.set(id::Gamma,bad);CHECK(!f.dialog->valid()&&!IsWindowEnabled(f.dialog->control(IDOK))&&!f.shown);f.set(id::Gamma,L"1.000");f.settle();CHECK(f.shown);}
    f.fail=true;f.set(id::Contrast,L"20");f.settle();CHECK(!f.dialog->valid()&&!f.shown);f.fail=false;f.set(id::Contrast,L"25");f.settle();CHECK(f.dialog->valid()&&f.shown);f.current=false;f.until([&]{return !f.dialog->valid()&&!f.shown;});SendMessageW(f.dialog->window(),WM_COMMAND,IDOK,0);CHECK(IsWindow(f.dialog->window())&&!f.dialog->result());f.click(IDCANCEL);CHECK(!f.dialog->result()&&!f.shown);
}
TEST(brightness_preview_preference_roundtrips_and_legacy_defaults_on) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-brightness-"+std::to_wstring(GetCurrentProcessId())+L".ini");Settings source;CHECK(source.brightness_preview);source.brightness_preview=false;source.save(path);Settings loaded;loaded.load(path);CHECK(!loaded.brightness_preview);CHECK(WritePrivateProfileStringW(L"Viewer",L"brightness_preview",nullptr,path.c_str()));Settings legacy;legacy.load(path);CHECK(legacy.brightness_preview);std::filesystem::remove(path);
}
void print_brightness_tree(HWND window,HDC dc,const RECT &root) {
    DWORD pid{};GetWindowThreadProcessId(window,&pid);CHECK(pid==GetCurrentProcessId());if(!(GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE))return;
    RECT bounds{};CHECK(GetWindowRect(window,&bounds));const int saved=SaveDC(dc);CHECK(saved);SetViewportOrgEx(dc,bounds.left-root.left,bounds.top-root.top,nullptr);IntersectClipRect(dc,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top);
    SendMessageW(window,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_NONCLIENT|PRF_CLIENT|PRF_ERASEBKGND);
    for(auto child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))print_brightness_tree(child,dc,root);RestoreDC(dc,saved);
}
void brightness_snapshot(const std::filesystem::path &path) {
    Fixture f;f.open();f.set(id::Brightness,L"20");f.set(id::Contrast,L"35");f.set(id::Gamma,L"1.250");f.settle();const auto window=f.dialog->window();RECT bounds{};CHECK(GetWindowRect(window,&bounds));const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;const auto dc=CreateCompatibleDC(nullptr);CHECK(dc);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};void *pixels{};const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);CHECK(bitmap&&pixels);const auto previous=SelectObject(dc,bitmap);std::fill_n(static_cast<uint32_t*>(pixels),size_t(width)*height,0xff5a5a5a);print_brightness_tree(window,dc,bounds);GdiFlush();
    Image image;image.width=width;image.height=height;image.frames.push_back({std::vector<Pixel>(size_t(width)*height),100});const auto bytes=static_cast<const uint8_t*>(pixels);std::set<uint32_t> colors;for(size_t n=0;n<image.frames[0].pixels.size();++n){image.frames[0].pixels[n]={bytes[n*4+2],bytes[n*4+1],bytes[n*4],255};colors.insert(static_cast<const uint32_t*>(pixels)[n]&0xffffff);}SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);CHECK(colors.size()>8);save_image(image,0,path);f.click(IDCANCEL);std::cout<<"PASS actual brightness control-tree snapshot "<<width<<"x"<<height<<" colors="<<colors.size()<<'\n';
}
int main(int argc,char **argv){gvn_test::ClipboardLock lock;if(!lock)return 2;const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;const auto desktop=CreateDesktopW(L"GVN-brightness-test",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;if(argc>1){brightness_snapshot(argv[1]);OleUninitialize();return 0;}const auto result=run_tests();OleUninitialize();return result;}
