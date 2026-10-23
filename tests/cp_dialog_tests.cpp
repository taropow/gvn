#include "gvn/cp_dialog.h"
#include "gvn/masks.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <chrono>
#include <array>

using namespace gvn;
namespace id=gvn::cp_control;
namespace {
struct Fixture {
    HWND owner{};
    Image source;
    std::unique_ptr<CpDialog> dialog;
    std::shared_ptr<const Image> presented;
    std::vector<std::wstring> processed;
    bool current{true}, fail{};
    Fixture() {
        owner=CreateWindowExW(0,L"STATIC",L"Owned CP dialog tests",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
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
    void open(CpChoice options={}) {
        dialog.reset();presented.reset();processed.clear();
        dialog=std::make_unique<CpDialog>(options,[&](std::wstring_view code) {
            processed.emplace_back(code);
            if(fail)throw std::bad_alloc();
            return std::make_shared<Image>(cp_image(source,code));
        },[&](auto value){presented=std::move(value);},[&]{return current;});
        CHECK(dialog->create(owner));ShowWindow(owner,SW_SHOWNOACTIVATE);ShowWindow(dialog->window(),SW_SHOW);
        CHECK((GetWindowLongPtrW(owner,GWL_STYLE)&WS_VISIBLE)&&
              (GetWindowLongPtrW(dialog->window(),GWL_STYLE)&WS_VISIBLE));
        until([&]{return dialog->ready();});
    }
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);pump();}
    void settle(){until([&]{return dialog->ready();});}
    void set(const wchar_t *value){SetWindowTextW(dialog->control(id::Code),value);pump();}
    std::wstring value(){std::wstring text(GetWindowTextLengthW(dialog->control(id::Code))+1,L'\0');GetWindowTextW(dialog->control(id::Code),text.data(),static_cast<int>(text.size()));text.pop_back();return text;}
};
}

TEST(cp_empty_code_is_lazy_and_accept_is_a_noop_result) {
    Fixture f;f.open();CHECK(f.dialog->options()==(CpChoice{L"",true}));
    CHECK(f.processed.empty()&&!f.presented&&f.dialog->valid());
    f.click(IDOK);CHECK(f.dialog->result()&&f.dialog->result()->choice.code.empty()&&!f.dialog->result()->image);
}
TEST(cp_edit_matches_native_filter_limit_and_uppercase_commit) {
    Fixture f;f.open({L"",false});CHECK(SendMessageW(f.dialog->control(id::Code),EM_GETLIMITTEXT,0,0)==30000);
    for(const auto &[input,output]:std::array<std::pair<const wchar_t*,const wchar_t*>,4>{{{L"a b",L"ab"},{L"ab1",L"ab"},{L"あ",L""},{L"abcdefghijklmnopq",L"abcdefghijklmnop"}}}) {
        f.set(input);CHECK(f.value()==output&&f.dialog->options().code==output&&f.dialog->valid());CHECK(f.processed.empty());
    }
    const auto long_input=std::wstring(10000,L'1')+L"a b";f.set(long_input.c_str());CHECK(f.value()==L"ab");
    f.click(IDOK);CHECK(f.dialog->result()&&f.dialog->result()->choice==(CpChoice{L"AB",false}));
    CHECK(f.processed==std::vector<std::wstring>{L"AB"});
}
TEST(cp_preview_exact_rgba_case_only_cache_and_shared_accept) {
    Fixture f;f.open({L"ab",true});CHECK(f.dialog->options().code==L"AB"&&f.presented);
    CHECK(f.presented->frames[0].pixels==cp_image(f.source,L"AB").frames[0].pixels);
    const auto cached=f.presented;f.set(L"aB");f.settle();CHECK(f.processed==std::vector<std::wstring>{L"AB"}&&f.presented==cached);
    f.click(IDOK);CHECK(f.dialog->result()&&f.dialog->result()->image==cached&&!f.presented);
}
TEST(cp_preview_off_releases_and_accept_is_lazy) {
    Fixture f;f.open({L"A",true});std::weak_ptr<const Image> old=f.presented;
    f.click(id::Preview);f.settle();CHECK(old.expired()&&!f.presented);
    f.set(L"B");f.settle();CHECK(f.processed==std::vector<std::wstring>{L"A"});
    f.click(IDOK);CHECK(f.dialog->result()&&f.processed==std::vector<std::wstring>({L"A",L"B"}));
    CHECK(f.dialog->result()->choice==(CpChoice{L"B",false}));
}
TEST(cp_typing_coalesces_clear_is_noop_and_cancel_releases) {
    Fixture f;f.open();
    for(const auto text:{L"a",L"ab",L"abc"})SetWindowTextW(f.dialog->control(id::Code),text);
    f.settle();CHECK(f.processed==std::vector<std::wstring>{L"ABC"});
    std::weak_ptr<const Image> old=f.presented;f.set(L"あ");f.settle();CHECK(old.expired()&&!f.presented&&f.dialog->options().code.empty());
    f.set(L"abc");f.settle();old=f.presented;f.click(IDCANCEL);CHECK(!f.dialog->result()&&old.expired()&&!f.presented);
    for(int i=0;i<12;++i){f.open({L"A",true});old=f.presented;f.dialog.reset();CHECK(old.expired()&&!f.presented);}
}
TEST(cp_allocation_failure_blocks_forced_accept_and_can_recover_with_empty) {
    Fixture f;f.fail=true;f.open({L"A",true});
    CHECK(!f.dialog->valid()&&!f.presented&&!IsWindowEnabled(f.dialog->control(IDOK)));
    CHECK(GetWindowTextLengthW(f.dialog->control(id::Error))>0);
    SendMessageW(f.dialog->window(),WM_COMMAND,IDOK,0);f.pump();CHECK(IsWindow(f.dialog->window())&&!f.dialog->result());
    f.set(L"");f.settle();CHECK(f.dialog->valid()&&IsWindowEnabled(f.dialog->control(IDOK))&&!f.presented);
    f.fail=false;f.set(L"AB");f.settle();CHECK(f.presented&&f.dialog->valid());f.click(IDOK);CHECK(f.dialog->result());
}
TEST(cp_context_loss_clears_preview_and_cannot_be_revalidated_by_switching_back) {
    Fixture f;f.open({L"A",true});f.current=false;
    f.until([&]{return !IsWindowEnabled(f.dialog->control(IDOK));});CHECK(!f.dialog->valid()&&!f.presented);
    f.current=true;SendMessageW(f.dialog->window(),WM_COMMAND,IDOK,0);f.pump();CHECK(IsWindow(f.dialog->window())&&!f.dialog->result());f.click(IDCANCEL);
}
TEST(cp_controls_font_and_synthetic_dpi_layout_are_visible_and_disjoint) {
    Fixture f;f.open();
    for(const auto dpi:{96u,144u,192u,96u}) {
        RECT bounds{20,20,20+MulDiv(410,dpi,96),20+MulDiv(290,dpi,96)};
        SendMessageW(f.dialog->window(),WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&bounds));f.pump();
        RECT client{};GetClientRect(f.dialog->window(),&client);std::vector<RECT> areas;
        for(const int key:{243,id::Code,244,id::Preview,id::Error,IDOK,IDCANCEL}) {
            const auto h=f.dialog->control(key);CHECK(h&&(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE));
            RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,f.dialog->window(),reinterpret_cast<POINT*>(&r),2);
            CHECK(r.left>=0&&r.top>=0&&r.right<=client.right&&r.bottom<=client.bottom);
            for(const auto &other:areas){RECT intersection{};CHECK(!IntersectRect(&intersection,&r,&other));}areas.push_back(r);
            LOGFONTW font{};CHECK(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0)),sizeof(font),&font));CHECK(font.lfHeight==-MulDiv(9,dpi,72));
        }
    }
}
int main() {
    gvn_test::ClipboardLock lock;if(!lock)return 2;
    const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
    if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;
    const auto desktop=CreateDesktopW(L"GVN-cp-dialog-test",nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop||!SetThreadDesktop(desktop))return 3;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;
    const auto result=run_tests();OleUninitialize();return result;
}
