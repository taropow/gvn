#include "gvn/image_info_dialog.h"
#include "gvn/codecs.h"
#include "gvn/settings.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
using namespace gvn;
namespace id=image_info_control;
namespace {
const std::filesystem::path fixtures=GVN_FIXTURES;
template<class F>void pump(F ready){const auto end=GetTickCount64()+3000;while(!ready()){CHECK(GetTickCount64()<end);MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(1);}}
std::wstring text(HWND h){std::wstring value(size_t(GetWindowTextLengthW(h))+1,L'\0');GetWindowTextW(h,value.data(),static_cast<int>(value.size()));value.pop_back();return value;}
struct Fixture {
    HWND owner{};std::unique_ptr<ImageInfoDialog> dialog;std::shared_ptr<Image> image=std::make_shared<Image>();
    Fixture(){image->width=image->height=1;image->frames.push_back({{{1,2,3,255}},100});owner=CreateWindowExW(0,L"STATIC",L"Owned info tests",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,900,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);}
    ~Fixture(){dialog.reset();DestroyWindow(owner);}
    void open(bool automatic,ImageInfoDialog::Counter counter){dialog=std::make_unique<ImageInfoDialog>(ImageInfoData{image,{{L"ファイル名",L"テスト.png"},{L"フルパス",L"G:\\private test\\テスト.png"}}},automatic,std::move(counter));CHECK(dialog->create(owner));ShowWindow(dialog->window(),SW_SHOW);CHECK(dialog->ready());}
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);}
};
}
TEST(color_count_matches_seven_native_reference_cases_and_keeps_straight_rgba_and_palette) {
    for(const auto [name,expected]:{std::pair{L"rgb.png",3u},{L"rgba-same.png",4u},{L"rgba-hidden.png",3u},{L"rgba-low.png",3u},{L"palette-duplicate.png",2u},{L"palette-unused.png",1u}}) {
        const auto image=load_image(fixtures/L"info"/name);const auto pixels=image.frames[0].pixels;const auto palette=image.frames[0].indexed;
        CHECK(count_image_colors(image.frames[0].pixels)==expected&&image.frames[0].pixels==pixels&&image.frames[0].indexed==palette);
    }
    CHECK(count_image_colors({})==0);
}
TEST(color_count_merges_repeated_partial_alpha_across_sorted_chunks_with_bounded_storage) {
    std::vector<Pixel> pixels(200000);for(size_t n=0;n<pixels.size();++n)pixels[n]={uint8_t((n/2)%256),0,0,uint8_t(n%2?64:128)};
    pixels.insert(pixels.end(),{{10,20,30,255},{10,20,30,255},{90,80,70,255},{1,2,3,0},{4,5,6,0}});
    const auto before=pixels;size_t allocated{};CHECK(count_image_colors(pixels,{},ColorCountMemory{4*1024*1024,[&](size_t n){allocated+=n;}})==197);
    CHECK(pixels==before&&allocated<3*1024*1024);
}
TEST(color_count_cancellation_and_memory_failure_preserve_input) {
    std::vector<Pixel> pixels(200000,{10,20,30,128});const auto before=pixels;size_t calls{};
    THROWS(count_image_colors(pixels,{},ColorCountMemory{(1u<<21)-1,[&](size_t){++calls;}}));CHECK(!calls);
    THROWS(count_image_colors(pixels,{},ColorCountMemory{(1u<<21)+pixels.size()*4-1,[&](size_t){++calls;}}));CHECK(calls==1);
    std::stop_source stopped;stopped.request_stop();CHECK(!count_image_colors(pixels,stopped.get_token(),ColorCountMemory{UINT64_MAX,[&](size_t){++calls;}})&&calls==1);
    for(size_t cancel_at:{1u,2u,3u}){std::stop_source stop;size_t allocations{};CHECK(!count_image_colors(pixels,stop.get_token(),ColorCountMemory{UINT64_MAX,[&](size_t){if(++allocations==cancel_at)stop.request_stop();}}));}
    CHECK(pixels==before);
}
TEST(info_readonly_fields_default_lazy_count_and_completed_result_survives_uncheck) {
    Fixture f;size_t calls{};f.open(false,[&](std::stop_token){++calls;return 7;});
    CHECK(!calls&&!f.dialog->pending()&&!f.dialog->count()&&text(f.dialog->control(id::Colors)).empty());
    CHECK((GetWindowLongPtrW(f.dialog->control(id::Field),GWL_STYLE)&ES_READONLY)!=0);
    CHECK(f.dialog->text().find(L"フルパス: G:\\private test\\テスト.png")!=std::wstring::npos);
    f.click(id::Auto);pump([&]{return !f.dialog->pending();});CHECK(calls==1&&f.dialog->count()==7&&f.dialog->automatic());
    f.click(id::Auto);CHECK(!f.dialog->automatic()&&text(f.dialog->control(id::Colors))==L"7 色");
    f.click(id::Auto);CHECK(calls==1&&!f.dialog->pending());f.click(IDOK);CHECK(!IsWindow(f.dialog->window()));
}
TEST(info_count_worker_keeps_native_controls_responsive_and_cancels_cooperatively) {
    Fixture f;std::atomic<bool> entered{},cancelled{};std::mutex mutex;std::condition_variable_any condition;
    f.open(true,[&](std::stop_token stop)->std::optional<uint64_t>{entered=true;std::unique_lock lock(mutex);condition.wait(lock,stop,[]{return false;});cancelled=stop.stop_requested();return {};});
    pump([&]{return entered.load();});CHECK(f.dialog->pending());SendMessageW(f.dialog->window(),WM_NULL,0,0);
    SendMessageW(f.dialog->control(id::Field+1),EM_SETSEL,0,-1);DWORD start{},end{};SendMessageW(f.dialog->control(id::Field+1),EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));CHECK(end>start);
    f.click(id::Auto);pump([&]{return !f.dialog->pending();});CHECK(cancelled&&!f.dialog->automatic()&&!f.dialog->count());
}
TEST(info_failed_count_can_retry_and_closing_stops_pending_worker) {
    Fixture f;size_t calls{};f.open(true,[&](std::stop_token)->std::optional<uint64_t>{if(!calls++)throw std::bad_alloc();return 3;});pump([&]{return !f.dialog->pending();});
    CHECK(!text(f.dialog->control(id::Error)).empty()&&IsWindowEnabled(f.dialog->control(IDOK))&&!f.dialog->count());
    f.click(id::Auto);f.click(id::Auto);pump([&]{return !f.dialog->pending();});CHECK(f.dialog->count()==3&&text(f.dialog->control(id::Error)).empty());
    std::atomic<bool> entered{},stopped{};std::mutex mutex;std::condition_variable_any condition;
    f.open(true,[&](std::stop_token token)->std::optional<uint64_t>{entered=true;std::unique_lock lock(mutex);condition.wait(lock,token,[]{return false;});stopped=token.stop_requested();return {};});
    pump([&]{return entered.load();});f.dialog.reset();CHECK(stopped);
}
TEST(info_animation_disables_count_without_clearing_saved_preference) {
    Fixture f;f.image->frames.push_back(f.image->frames.front());size_t calls{};f.open(true,[&](std::stop_token){++calls;return 1;});
    CHECK(!IsWindowEnabled(f.dialog->control(id::Auto))&&SendMessageW(f.dialog->control(id::Auto),BM_GETCHECK,0,0)==BST_UNCHECKED);
    CHECK(f.dialog->automatic()&&!calls&&!f.dialog->pending()&&text(f.dialog->control(id::Colors)).empty());
}
TEST(info_readonly_field_ctrl_a_and_copy_preserve_full_unicode_text) {
    Fixture f;f.open(false,{});const auto field=f.dialog->control(id::Field+1);SetFocus(field);
    {
        struct Restore {BYTE saved[256]{};Restore(){CHECK(GetKeyboardState(saved));}~Restore(){SetKeyboardState(saved);}} restore;
        BYTE keys[256]{};for(size_t n=0;n<256;++n)keys[n]=restore.saved[n];keys[VK_CONTROL]|=0x80;CHECK(SetKeyboardState(keys));
        SendMessageW(field,WM_KEYDOWN,'A',0);DWORD start{},end{};SendMessageW(field,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));CHECK(start==0&&end==text(field).size());
    }
    SendMessageW(field,WM_COPY,0,0);CHECK(OpenClipboard(f.owner));struct Close {~Close(){CloseClipboard();}} close;
    const auto handle=GetClipboardData(CF_UNICODETEXT);CHECK(handle);const auto data=static_cast<const wchar_t*>(GlobalLock(handle));CHECK(data);
    const std::wstring copied=data;GlobalUnlock(handle);CHECK(copied==L"G:\\private test\\テスト.png");
}
TEST(info_color_count_setting_defaults_off_and_roundtrips_without_other_settings) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-info-"+std::to_wstring(GetCurrentProcessId())+L".ini");std::filesystem::remove(path);
    Settings fresh;fresh.load(path);CHECK(!fresh.info_auto_colors);fresh.info_auto_colors=true;fresh.save(path);Settings saved;saved.load(path);CHECK(saved.info_auto_colors);
    saved.info_auto_colors=false;saved.save(path);fresh.load(path);CHECK(!fresh.info_auto_colors);std::filesystem::remove(path);
}
int main(){gvn_test::ClipboardLock lock;if(!lock)return 2;const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;const auto desktop=CreateDesktopW(L"GVN-image-info-test",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;const auto result=run_tests();OleUninitialize();return result;}
