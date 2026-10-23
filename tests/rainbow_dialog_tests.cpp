#include "gvn/rainbow_dialog.h"
#include "gvn/masks.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <chrono>
#include <array>

using namespace gvn;
namespace id=gvn::rainbow_control;
namespace {
struct Fixture {
    HWND owner{};
    Image source;
    std::unique_ptr<RainbowDialog> dialog;
    std::shared_ptr<const Image> presented;
    std::vector<int> processed;
    bool current{true}, fail{};
    Fixture() {
        owner=CreateWindowExW(0,L"STATIC",L"Owned Rainbow dialog tests",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                             0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        CHECK(owner);
        // All windows stay on our noninteractive station; explicitly show the private owner.
        ShowWindow(owner,SW_SHOWNOACTIVATE);
        source.width=53;source.height=47;
        source.frames.push_back({std::vector<Pixel>(53*47),100});
        for(size_t i=0;i<source.frames[0].pixels.size();++i)
            source.frames[0].pixels[i]={static_cast<uint8_t>(i*17),static_cast<uint8_t>(i*7),
                                       static_cast<uint8_t>(i*31),static_cast<uint8_t>(i)};
    }
    ~Fixture(){dialog.reset();DestroyWindow(owner);}
    void pump() {
        MSG m{};
        while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {
            if(dialog&&IsWindow(dialog->window())&&IsDialogMessageW(dialog->window(),&m))continue;
            TranslateMessage(&m);DispatchMessageW(&m);
        }
    }
    template<class F> void until(F condition) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        do {
            pump();if(condition())return;
            MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
        }while(std::chrono::steady_clock::now()<deadline);
        CHECK(condition());
    }
    void open(RainbowChoice options={}) {
        dialog.reset();presented.reset();processed.clear();
        dialog=std::make_unique<RainbowDialog>(options,[&](int size) {
            processed.push_back(size);
            if(fail)throw std::bad_alloc();
            return std::make_shared<Image>(rainbow_image(source,size));
        },[&](auto value){presented=std::move(value);},[&]{return current;});
        CHECK(dialog->create(owner));ShowWindow(owner,SW_SHOWNOACTIVATE);ShowWindow(dialog->window(),SW_SHOW);
        CHECK((GetWindowLongPtrW(owner,GWL_STYLE)&WS_VISIBLE)&&
              (GetWindowLongPtrW(dialog->window(),GWL_STYLE)&WS_VISIBLE));
        until([&]{return dialog->ready();});
    }
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);pump();}
    void settle(){until([&]{return dialog->ready();});}
};
}
TEST(rainbow_six_real_radios_preview_exact_rgba_and_cached_accept) {
    Fixture f;f.open();
    CHECK(f.dialog->options()==(RainbowChoice{8,true}));
    CHECK(f.processed==std::vector<int>{8}&&f.presented);
    const auto original=f.source.frames[0].pixels;
    constexpr int sizes[]{1,2,4,8,16,32};
    for(int i=0;i<6;++i) {
        f.click(id::SizeBase+i);f.settle();
        CHECK(f.dialog->options().size==sizes[i]&&f.dialog->valid()&&f.presented);
        CHECK(f.presented->frames[0].pixels==rainbow_image(f.source,sizes[i]).frames[0].pixels);
        CHECK(f.source.frames[0].pixels==original);
        for(int j=0;j<6;++j)
            CHECK(SendMessageW(f.dialog->control(id::SizeBase+j),BM_GETCHECK,0,0)==(i==j?BST_CHECKED:BST_UNCHECKED));
    }
    const auto cached=f.presented;const auto count=f.processed.size();
    f.click(IDOK);
    CHECK(!IsWindow(f.dialog->window())&&!f.presented&&f.dialog->result());
    CHECK(f.dialog->result()->choice==(RainbowChoice{32,true}));
    CHECK(f.dialog->result()->image==cached&&f.processed.size()==count);
}
TEST(rainbow_preview_off_is_lazy_and_accept_computes_once) {
    Fixture f;f.open({2,false});
    CHECK(f.processed.empty()&&!f.presented&&IsWindowEnabled(f.dialog->control(IDOK)));
    f.click(id::SizeBase+2);f.settle();
    CHECK(f.dialog->options()==(RainbowChoice{4,false})&&f.processed.empty());
    f.click(IDOK);
    CHECK(f.processed==std::vector<int>{4}&&f.dialog->result()&&!f.presented);
    CHECK(f.dialog->result()->image->frames[0].pixels==rainbow_image(f.source,4).frames[0].pixels);
}
TEST(rainbow_preview_off_releases_candidate_and_cancel_changes_no_source) {
    Fixture f;f.open();const auto original=f.source.frames[0].pixels;
    std::weak_ptr<const Image> candidate=f.presented;
    f.click(id::Preview);f.settle();
    CHECK(candidate.expired()&&!f.presented&&!f.dialog->preview_image());
    f.click(id::SizeBase);f.click(id::Preview);f.settle();
    CHECK(f.presented&&f.processed==std::vector<int>({8,1}));
    candidate=f.presented;f.click(IDCANCEL);
    CHECK(!f.dialog->result()&&!f.presented&&candidate.expired());
    CHECK(f.source.frames[0].pixels==original);
}
TEST(rainbow_preview_changes_coalesce_and_closing_releases_buffers) {
    Fixture f;f.open({8,false});
    // Do not pump the timer between native radio notifications.
    SendMessageW(f.dialog->control(id::Preview),BM_CLICK,0,0);
    for(int i=0;i<6;++i)SendMessageW(f.dialog->control(id::SizeBase+i),BM_CLICK,0,0);
    CHECK(!f.dialog->ready()&&!IsWindowEnabled(f.dialog->control(IDOK)));
    f.settle();CHECK(f.processed==std::vector<int>{32});
    std::weak_ptr<const Image> candidate=f.presented;
    SendMessageW(f.dialog->window(),WM_CLOSE,0,0);f.pump();
    CHECK(!f.dialog->result()&&candidate.expired()&&!f.presented);
    for(int i=0;i<12;++i) {
        f.open();candidate=f.presented;f.dialog.reset();
        CHECK(candidate.expired()&&!f.presented);
    }
}
TEST(rainbow_allocation_error_blocks_accept_and_can_recover) {
    Fixture f;f.open();f.fail=true;
    f.click(id::SizeBase);f.settle();
    CHECK(!f.dialog->valid()&&!f.presented&&!IsWindowEnabled(f.dialog->control(IDOK)));
    CHECK(GetWindowTextLengthW(f.dialog->control(id::Error))>0);
    SendMessageW(f.dialog->window(),WM_COMMAND,IDOK,0);f.pump();
    CHECK(IsWindow(f.dialog->window())&&!f.dialog->result());
    f.fail=false;f.click(id::SizeBase+1);f.settle();
    CHECK(f.dialog->valid()&&f.presented&&IsWindowEnabled(f.dialog->control(IDOK)));
    f.click(IDOK);CHECK(f.dialog->result());
}
TEST(rainbow_changed_document_invalidates_preview_and_refuses_forced_accept) {
    Fixture f;f.open();f.current=false;
    f.until([&]{return !IsWindowEnabled(f.dialog->control(IDOK));});
    CHECK(!f.dialog->valid()&&!f.presented);
    f.current=true; // A context loss is latched; switching back cannot commit an old transaction.
    SendMessageW(f.dialog->window(),WM_COMMAND,IDOK,0);f.pump();
    CHECK(IsWindow(f.dialog->window())&&!f.dialog->result());
    f.click(IDCANCEL);
}
TEST(rainbow_controls_font_and_synthetic_dpi_layout_are_visible_and_disjoint) {
    Fixture f;f.open();
    for(const auto dpi:{96u,144u,192u,96u}) {
        RECT bounds{20,20,20+MulDiv(370,dpi,96),20+MulDiv(310,dpi,96)};
        SendMessageW(f.dialog->window(),WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&bounds));f.pump();
        RECT client{};GetClientRect(f.dialog->window(),&client);
        std::vector<RECT> areas;
        for(const int key:{id::SizeBase,id::SizeBase+1,id::SizeBase+2,id::SizeBase+3,id::SizeBase+4,id::SizeBase+5,id::Preview,id::Error,IDOK,IDCANCEL}) {
            const auto h=f.dialog->control(key);
            // IsWindowVisible also checks the private desktop, which is intentionally not shown.
            CHECK(h&&(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE));
            RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,f.dialog->window(),reinterpret_cast<POINT*>(&r),2);
            CHECK(r.left>=0&&r.top>=0&&r.right<=client.right&&r.bottom<=client.bottom);
            for(const auto &other:areas){RECT intersection{};CHECK(!IntersectRect(&intersection,&r,&other));}
            areas.push_back(r);
            LOGFONTW font{};CHECK(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0)),sizeof(font),&font));
            CHECK(font.lfHeight==-MulDiv(9,dpi,72));
        }
    }
}
int main() {
    gvn_test::ClipboardLock lock;if(!lock)return 2;
    const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
    if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;
    const auto desktop=CreateDesktopW(L"GVN-rainbow-dialog-test",nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop||!SetThreadDesktop(desktop))return 3;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;
    const auto result=run_tests();OleUninitialize();return result;
}
