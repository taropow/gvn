#include "gvn/palette_dialog.h"
#include "gvn/edit.h"
#include "gvn/settings.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <chrono>
using namespace gvn;
namespace id=palette_control;
namespace {
struct Fixture {
    HWND owner{};Image source;std::unique_ptr<PaletteDialog> dialog;
    std::shared_ptr<const Image> shown;std::vector<std::shared_ptr<Image>> commits;
    unsigned calls{};bool current{true},process_fail{},commit_fail{},preference{true};
    std::optional<uint32_t> picked{0x123456};unsigned pick_calls{};
    Fixture() {
        owner=CreateWindowExW(0,L"STATIC",L"Owned palette editor tests",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,900,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);
        source.width=7;source.height=3;auto indexed=std::make_shared<IndexedPixels>();indexed->width=7;indexed->height=3;indexed->bits=4;
        for(unsigned i=0;i<16;++i)indexed->palette.push_back({uint8_t(i*7),uint8_t(i*11),uint8_t(i*13),uint8_t(i==2?0:127)});
        indexed->palette[0]=indexed->palette[1]=Pixel{31,37,41,127};indexed->packed.resize(indexed->stride()*3);
        source.frames.push_back({std::vector<Pixel>(21),100});
        for(unsigned y=0;y<3;++y)for(unsigned x=0;x<7;++x){const auto n=uint8_t((x+y)%8);indexed->set_index(x,y,n);source.frames[0].pixels[y*7+x]=indexed->palette[n];}
        source.frames[0].indexed=indexed;
    }
    ~Fixture(){dialog.reset();DestroyWindow(owner);}
    void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(dialog&&IsWindow(dialog->window())&&IsDialogMessageW(dialog->window(),&m))continue;TranslateMessage(&m);DispatchMessageW(&m);}}
    template<class F>void until(F predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);do{pump();if(predicate())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(std::chrono::steady_clock::now()<end);CHECK(predicate());}
    void open(bool preview=true) {
        dialog=std::make_unique<PaletteDialog>(source.frames[0].indexed->palette,preview,[&](const auto &colors){++calls;if(process_fail)throw std::bad_alloc();return std::make_shared<Image>(edit_palette(source,colors));},
            [&](auto value){shown=std::move(value);},[&](auto value,bool enabled){if(commit_fail)throw std::runtime_error("Injected commit failure");preference=enabled;if(value){commits.push_back(value);source=*value;}},[&]{return current;},
            [&](HWND,uint32_t){++pick_calls;return picked;});
        CHECK(dialog->create(owner));ShowWindow(dialog->window(),SW_SHOWNOACTIVATE);until([&]{return dialog->ready();});
    }
    void set(int key,const wchar_t *text){SetWindowTextW(dialog->control(key),text);}
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);pump();}
    void settle(){until([&]{return dialog->ready();});}
    void select(unsigned index){set(id::Index,std::to_wstring(index).c_str());}
};
}
TEST(palette_dialog_numbers_sliders_duplicate_alpha_apply_and_cancel_have_distinct_commit_states) {
    Fixture f;f.open();CHECK(f.calls==0&&!f.shown&&f.dialog->selected_index()==0&&!IsWindowEnabled(f.dialog->control(id::Apply)));
    const auto before=f.source.frames[0].pixels;const auto indices=f.source.frames[0].indexed->packed;
    f.select(1);f.set(id::Red,L"101");f.set(id::Green,L"103");f.set(id::Blue,L"107");f.settle();
    CHECK(f.shown&&f.calls==1&&f.source.frames[0].pixels==before);
    CHECK(f.dialog->palette()[0]==Pixel{31,37,41,127}&&f.dialog->palette()[1]==Pixel{101,103,107,127});
    const int expected[]{101,103,107};for(unsigned n=0;n<3;++n)CHECK(SendMessageW(f.dialog->control(id::SlideRed+n),TBM_GETPOS,0,0)==expected[n]);
    f.click(id::Apply);CHECK(f.commits.size()==1&&IsWindow(f.dialog->window())&&!f.shown&&!IsWindowEnabled(f.dialog->control(id::Apply)));
    CHECK(f.source.frames[0].indexed->packed==indices&&f.source.frames[0].indexed->matches(f.source.frames[0].pixels));
    const auto applied=f.source.frames[0].pixels;f.set(id::Red,L"211");f.settle();CHECK(f.shown);
    f.click(IDCANCEL);CHECK(!f.dialog->accepted()&&!f.shown&&f.source.frames[0].pixels==applied&&f.commits.size()==1);
}
TEST(palette_dialog_preview_off_is_lazy_grid_keyboard_hover_and_color_picker_keep_alpha) {
    Fixture f;f.open(false);f.select(1);f.set(id::Red,L"101");f.settle();CHECK(f.calls==0&&!f.shown);
    auto grid=f.dialog->control(id::Grid);RECT r{};GetClientRect(grid,&r);
    SendMessageW(grid,WM_MOUSEMOVE,0,MAKELPARAM(r.right*5/8,r.bottom/8));
    wchar_t hover[256]{};GetWindowTextW(f.dialog->control(id::Hover),hover,256);CHECK(std::wstring(hover).find(L"番号: 2")!=std::wstring::npos&&f.dialog->selected_index()==1);
    SendMessageW(grid,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(r.right*5/8,r.bottom/8));CHECK(f.dialog->selected_index()==2);
    SendMessageW(grid,WM_KEYDOWN,VK_RIGHT,0);CHECK(f.dialog->selected_index()==3);
    SendMessageW(grid,WM_KEYDOWN,VK_HOME,0);CHECK(f.dialog->selected_index()==0);
    SendMessageW(f.dialog->control(id::SlideBlue),TBM_SETPOS,TRUE,19);
    SendMessageW(f.dialog->window(),WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(f.dialog->control(id::SlideBlue)));
    CHECK(f.dialog->palette()[0].b==19&&GetDlgItemInt(f.dialog->window(),id::Blue,nullptr,FALSE)==19);
    f.select(2);SendMessageW(grid,WM_MOUSEMOVE,0,MAKELPARAM(r.right*5/8,r.bottom/8));
    SendMessageW(f.dialog->control(id::Pick),BM_CLICK,0,0);CHECK(f.pick_calls==1&&f.dialog->palette()[2]==Pixel{18,52,86,0}&&f.calls==0);
    GetWindowTextW(f.dialog->control(id::Hover),hover,256);CHECK(std::wstring(hover).find(L"RGB: 18, 52, 86")!=std::wstring::npos);
    const auto colors=f.dialog->palette();f.picked.reset();f.click(id::Pick);CHECK(f.pick_calls==2&&f.dialog->palette()==colors);
    f.click(IDOK);CHECK(f.dialog->accepted()&&f.calls==1&&f.commits.size()==1&&!f.preference);
    CHECK(f.source.frames[0].indexed->palette[2]==Pixel{18,52,86,0});
}
TEST(palette_dialog_invalid_numbers_and_commit_failure_never_change_committed_image) {
    Fixture f;f.open();const auto original=f.source.frames[0].pixels;
    for(auto invalid:{L"",L"999",L"-1",L"x"}) {
        f.set(id::Index,invalid);CHECK(!f.dialog->valid()&&!IsWindowEnabled(f.dialog->control(IDOK))&&!f.shown);
        f.set(id::Red,L"1");CHECK(!f.dialog->valid());f.select(1);f.settle();CHECK(f.dialog->valid());
    }
    f.set(id::Red,L"256");CHECK(!f.dialog->valid());f.set(id::Red,L"99");f.settle();CHECK(f.dialog->valid());
    f.commit_fail=true;f.click(id::Apply);CHECK(f.commits.empty()&&f.source.frames[0].pixels==original&&!f.shown&&!f.dialog->valid());
    f.commit_fail=false;f.set(id::Red,L"100");f.settle();f.click(id::Apply);CHECK(f.commits.size()==1&&f.dialog->valid());f.click(IDCANCEL);
}
TEST(palette_dialog_allocation_failure_and_document_replacement_release_only_preview) {
    Fixture f;f.open();const auto original=f.source.frames[0].pixels;f.process_fail=true;
    f.set(id::Red,L"100");f.settle();CHECK(!f.dialog->valid()&&!f.shown&&f.commits.empty()&&f.source.frames[0].pixels==original);
    f.process_fail=false;f.set(id::Red,L"101");f.settle();CHECK(f.dialog->valid()&&f.shown);
    std::weak_ptr<const Image> candidate=f.shown;f.current=false;
    // Current() changes before the UI timer consumes it; wait for the actual cleanup too.
    f.until([&]{return !f.dialog->valid()&&candidate.expired()&&!f.shown;});
    CHECK(candidate.expired()&&!f.shown&&!IsWindowEnabled(f.dialog->control(id::Apply)));f.click(IDCANCEL);CHECK(f.commits.empty());
}
TEST(palette_dialog_dpi_fonts_and_controls_stay_inside_client_area_without_overlap) {
    Fixture f;f.open();
    for(UINT dpi:{96u,144u,192u,96u}) {
        RECT size{20,20,20+MulDiv(560,dpi,96),20+MulDiv(650,dpi,96)};
        SendMessageW(f.dialog->window(),WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&size));f.pump();
        RECT client{};GetClientRect(f.dialog->window(),&client);std::vector<RECT> areas;
        for(int key:{id::Index,id::Red,id::Green,id::Blue,id::SlideRed,id::SlideGreen,id::SlideBlue,id::Pick,id::Grid,id::Swatch,id::Hover,id::Preview,id::Error,id::Apply,IDOK,IDCANCEL}) {
            const auto h=f.dialog->control(key);CHECK(h&&(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE));
            RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,f.dialog->window(),reinterpret_cast<POINT*>(&r),2);
            CHECK(r.left>=0&&r.top>=0&&r.right<=client.right&&r.bottom<=client.bottom);
            for(const auto &other:areas){RECT intersection{};CHECK(!IntersectRect(&intersection,&r,&other));}areas.push_back(r);
            if(key>=id::SlideRed&&key<=id::SlideBlue) {
                // A native trackbar draws no text and returns no WM_GETFONT font.
                // Verify its real range and thumb geometry at each DPI instead.
                CHECK(SendMessageW(h,TBM_GETRANGEMIN,0,0)==0&&SendMessageW(h,TBM_GETRANGEMAX,0,0)==255);
                RECT thumb{},area{};SendMessageW(h,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));GetClientRect(h,&area);
                CHECK(thumb.right>thumb.left&&thumb.bottom>thumb.top&&thumb.left>=0&&thumb.top>=0&&thumb.right<=area.right&&thumb.bottom<=area.bottom);
            }else {
                LOGFONTW font{};CHECK(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0)),sizeof(font),&font));CHECK(font.lfHeight==-MulDiv(9,dpi,72));
            }
        }
    }
}
TEST(palette_preview_preference_defaults_and_ini_roundtrip_are_independent_of_mask_previews) {
    Settings initial;CHECK(initial.palette_preview);GUID guid{};CHECK(SUCCEEDED(CoCreateGuid(&guid)));wchar_t id[40]{};StringFromGUID2(guid,id,40);
    const auto directory=std::filesystem::temp_directory_path()/(std::wstring(L"GVN-palette-settings-")+id);std::filesystem::create_directory(directory);
    const auto path=directory/L"settings.ini";initial.palette_preview=false;initial.rainbow_preview=true;initial.cp_preview=true;initial.save(path);
    Settings saved;saved.load(path);CHECK(!saved.palette_preview&&saved.rainbow_preview&&saved.cp_preview);
    std::filesystem::remove(path);std::filesystem::remove(directory);
}
int main() {
    gvn_test::ClipboardLock lock;if(!lock)return 2;
    const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
    if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;
    const auto desktop=CreateDesktopW(L"GVN-palette-dialog-test",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;
    const auto result=run_tests();OleUninitialize();return result;
}
