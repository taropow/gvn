#include "gvn/resize_dialog.h"
#include "gvn/settings.h"
#include "gvn/edit.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <cmath>
using namespace gvn;
namespace id=resize_control;
namespace {
struct Fixture {
    HWND owner{};
    std::unique_ptr<ResizeDialog> dialog;
    Fixture(){owner=CreateWindowExW(0,L"STATIC",L"Owned resize tests",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);CHECK(owner);}
    ~Fixture(){dialog.reset();DestroyWindow(owner);}
    void open(uint32_t w,uint32_t h,ResizeChoice choice={},ResizeDialog::Validator validate={}) {
        dialog=std::make_unique<ResizeDialog>(w,h,choice,std::move(validate));CHECK(dialog->create(owner));ShowWindow(dialog->window(),SW_SHOW);CHECK(dialog->ready());
    }
    void set(int key,const wchar_t *value){SetWindowTextW(dialog->control(key),value);}
    void click(int key){SendMessageW(dialog->control(key),BM_CLICK,0,0);}
};
}
TEST(resize_controls_keep_original_aspect_percentages_half_double_and_cancel) {
    Fixture f;f.open(300,200);
    f.set(id::Width,L"450");CHECK(f.dialog->options().width==450 && f.dialog->options().height==300);
    f.set(id::Height,L"100");CHECK(f.dialog->options().width==150 && f.dialog->options().height==100);
    f.set(id::WidthPercent,L"25");CHECK(f.dialog->options().width==75 && f.dialog->options().height==50);
    f.click(id::Double);CHECK(f.dialog->options().width==150 && f.dialog->options().height==100);
    f.click(id::Half);CHECK(f.dialog->options().width==75 && f.dialog->options().height==50);
    f.click(id::KeepAspect);f.set(id::HeightPercent,L"50");
    CHECK(!f.dialog->options().keep_aspect && f.dialog->options().width==75 && f.dialog->options().height==100);
    f.click(id::Smooth);CHECK(!f.dialog->options().smooth);
    f.click(IDCANCEL);CHECK(!f.dialog->result());
}
TEST(resize_rejects_invalid_dimensions_percentages_and_memory_without_allocating) {
    Fixture f;size_t allocations{};EditMemory memory{24,false,[&](size_t){++allocations;}};
    f.open(3,2,{},[&](uint32_t w,uint32_t h){memory.pixels(w,h);});CHECK(f.dialog->valid());
    for(const auto bad:{L"",L"0",L"65536",L"1x",L"-5",L"999999999999999999999999"}) {
        f.set(id::Width,bad);CHECK(!f.dialog->valid() && !IsWindowEnabled(f.dialog->control(IDOK)));
    }
    f.set(id::Width,L"6");CHECK(!f.dialog->valid());
    f.set(id::Width,L"3");CHECK(f.dialog->valid());
    f.set(id::WidthPercent,L"nan");CHECK(!f.dialog->valid());
    f.set(id::WidthPercent,L"100");CHECK(f.dialog->valid());
    CHECK(!allocations);f.click(IDOK);CHECK(f.dialog->result() && f.dialog->result()->width==3 && !allocations);
    f.open(1,65535);f.set(id::Width,L"65535");CHECK(!f.dialog->valid());f.click(IDCANCEL);
}
TEST(resize_settings_defaults_migrate_legacy_interpolation_and_roundtrip_independently) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-resize-ui-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    std::filesystem::remove(path);
    Settings fresh;fresh.load(path);CHECK(fresh.resize_keep_aspect && fresh.resize_smooth && !std::filesystem::exists(path));
    CHECK(WritePrivateProfileStringW(L"Viewer",L"nearest",L"1",path.c_str()));
    Settings legacy;legacy.load(path);CHECK(legacy.nearest && !legacy.resize_smooth);
    legacy.resize_keep_aspect=false;legacy.resize_smooth=true;legacy.save(path);
    Settings saved;saved.load(path);CHECK(saved.nearest && saved.resize_smooth && !saved.resize_keep_aspect);
    std::filesystem::remove(path);
}
TEST(identity_resize_preserves_full_rgba_including_hidden_and_low_alpha_colors) {
    const std::vector<Pixel> pixels{{123,45,67,0},{200,10,30,1},{19,99,250,128},{2,4,6,255}};
    Image source;source.width=2;source.height=2;source.frames.push_back({pixels,100});
    for(bool smooth:{false,true}) {
        const auto out=resize_image(source,2,2,smooth);
        CHECK(out.frames[0].pixels==pixels);
    }
}
TEST(resize_filter_controls_preserve_independent_choices_and_master_switch) {
    Fixture f;f.open(300,200,{300,200,true,true,10,7,true});
    for(const auto key:{id::Down,id::Up})CHECK(SendMessageW(f.dialog->control(key),CB_GETCOUNT,0,0)==3);
    CHECK(SendMessageW(f.dialog->control(id::Down),CB_GETCURSEL,0,0)==2);
    CHECK(SendMessageW(f.dialog->control(id::Up),CB_GETCURSEL,0,0)==2);
    f.click(id::Smooth);
    for(const auto key:{id::Down,id::Up,id::Sharpen})CHECK(!IsWindowEnabled(f.dialog->control(key)));
    CHECK(f.dialog->options().down==10&&f.dialog->options().up==10&&f.dialog->options().sharpen);
    f.click(id::Smooth);
    for(const auto key:{id::Down,id::Up,id::Sharpen})CHECK(IsWindowEnabled(f.dialog->control(key)));
    SendMessageW(f.dialog->control(id::Down),CB_SETCURSEL,2,0);
    SendMessageW(f.dialog->window(),WM_COMMAND,MAKEWPARAM(id::Down,CBN_SELCHANGE),reinterpret_cast<LPARAM>(f.dialog->control(id::Down)));
    f.click(id::Sharpen);f.click(IDOK);
    CHECK(f.dialog->result()&&f.dialog->result()->down==10&&f.dialog->result()->up==10&&!f.dialog->result()->sharpen);
    THROWS(ResizeDialog(1,1,{1,1,true,true,11,1}));
}
TEST(resize_filter_settings_roundtrip_all_choices_and_reject_invalid_ini_values) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-resize-filter-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    std::filesystem::remove(path);Settings fresh;fresh.load(path);
    CHECK(fresh.resize_down==1&&fresh.resize_up==1&&!fresh.resize_sharpen);
    for(int method=0;method<=10;++method) {
        fresh.resize_down=method;fresh.resize_up=10-method;fresh.resize_sharpen=method%2!=0;fresh.save(path);
        Settings saved;saved.load(path);
        CHECK(saved.resize_down==method&&saved.resize_up==10-method&&saved.resize_sharpen==fresh.resize_sharpen);
    }
    CHECK(WritePrivateProfileStringW(L"Viewer",L"resize_down",L"-1",path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer",L"resize_up",L"11",path.c_str()));
    Settings bad;bad.load(path);CHECK(bad.resize_down>=0&&bad.resize_down<=10&&bad.resize_up>=0&&bad.resize_up<=10);
    std::filesystem::remove(path);
}
TEST(resize_physical_size_and_density_units_preserve_pixels_on_unit_switch) {
    Fixture f;ResizeChoice choice{300,200};choice.dpi_x=300;choice.dpi_y=150;choice.dpi_defined=true;
    f.open(300,200,choice);
    const auto select=[&](int key,int value){SendMessageW(f.dialog->control(key),CB_SETCURSEL,value,0);SendMessageW(f.dialog->window(),WM_COMMAND,MAKEWPARAM(key,CBN_SELCHANGE),reinterpret_cast<LPARAM>(f.dialog->control(key)));};
    for(int unit:{0,2,1,0}){select(id::PrintUnit,unit);CHECK(f.dialog->valid()&&f.dialog->options().width==300&&f.dialog->options().height==200);}
    f.set(id::PrintWidth,L"2");CHECK(f.dialog->options().width==600&&f.dialog->options().height==400);
    f.set(id::DpiX,L"600");CHECK(f.dialog->options().width==600&&f.dialog->options().height==400);
    select(id::DpiUnit,1);CHECK(std::abs(f.dialog->options().dpi_x-600)<1e-8&&std::abs(f.dialog->options().dpi_y-150)<1e-8);
    f.set(id::DpiY,L"100");CHECK(std::abs(f.dialog->options().dpi_y-254)<1e-8&&f.dialog->options().width==600);
    select(id::DpiUnit,0);select(id::PrintUnit,1);f.set(id::PrintWidth,L"2.54");
    CHECK(f.dialog->options().width==600&&f.dialog->options().height==400);
    f.click(id::KeepAspect);select(id::PrintUnit,2);f.set(id::PrintHeight,L"25.4");
    CHECK(f.dialog->options().width==600&&f.dialog->options().height==254);
    f.click(IDOK);CHECK(f.dialog->result()&&f.dialog->result()->dpi_defined&&f.dialog->result()->print_unit==4);
}
TEST(resolution_only_does_not_validate_pixel_allocation_and_rejects_invalid_metadata) {
    Fixture f;ResizeChoice choice{600,400};choice.resample=false;size_t validations{};
    f.open(600,400,choice,[&](uint32_t,uint32_t){++validations;throw std::bad_alloc();});
    CHECK(f.dialog->valid()&&!validations);
    for(const auto key:{id::Width,id::Height,id::WidthPercent,id::HeightPercent,id::Half,id::Double,id::KeepAspect,id::Smooth,id::Down,id::Up,id::Sharpen})CHECK(!IsWindowEnabled(f.dialog->control(key)));
    f.set(id::DpiX,L"300");f.set(id::DpiY,L"200");
    for(const auto bad:{L"0",L"nan",L"65536",L"0.0001",L"-5",L""}){
        f.set(id::DpiX,bad);CHECK(!f.dialog->valid()&&!IsWindowEnabled(f.dialog->control(IDOK)));
        SendMessageW(f.dialog->window(),WM_COMMAND,IDOK,0);CHECK(IsWindow(f.dialog->window())&&!f.dialog->result());
    }
    f.set(id::DpiX,L"300");CHECK(f.dialog->valid());
    f.set(id::PrintWidth,L"5.08");f.set(id::PrintHeight,L"5.08");
    CHECK(f.dialog->valid()&&std::abs(f.dialog->options().dpi_x-300)<1e-8&&std::abs(f.dialog->options().dpi_y-200)<1e-8);
    CHECK(f.dialog->options().width==600&&f.dialog->options().height==400&&!validations);
    f.set(id::PrintWidth,L"0");CHECK(!f.dialog->valid());f.set(id::PrintWidth,L"10.16");
    CHECK(std::abs(f.dialog->options().dpi_x-150)<1e-8&&f.dialog->valid()&&!validations);
    f.click(IDOK);CHECK(f.dialog->result()&&!f.dialog->result()->resample);
}
TEST(canvas_five_units_use_independent_document_dpi_and_original_pixel_aspect) {
    Fixture f;CanvasChoice choice;choice.dpi_x=300;choice.dpi_y=150;
    f.dialog=ResizeDialog::for_canvas(300,300,choice);CHECK(f.dialog->create(f.owner));
    const auto select=[&](int unit){SendMessageW(f.dialog->control(id::CanvasUnit),CB_SETCURSEL,unit,0);SendMessageW(f.dialog->window(),WM_COMMAND,MAKEWPARAM(id::CanvasUnit,CBN_SELCHANGE),reinterpret_cast<LPARAM>(f.dialog->control(id::CanvasUnit)));};
    for(int unit:{2,3,4,1,0}){select(unit);CHECK(f.dialog->valid()&&f.dialog->canvas_options().width==300&&f.dialog->canvas_options().height==300);}
    select(2);f.set(id::Width,L"2");CHECK(f.dialog->canvas_options().width==600&&f.dialog->canvas_options().height==300);
    f.click(id::KeepAspect);f.set(id::Width,L"3");CHECK(f.dialog->canvas_options().width==900&&f.dialog->canvas_options().height==900);
    select(1);f.set(id::Height,L"50");CHECK(f.dialog->canvas_options().width==150&&f.dialog->canvas_options().height==150);
    select(4);f.set(id::Width,L"25.4");CHECK(f.dialog->canvas_options().width==300&&f.dialog->canvas_options().height==300);
    f.set(id::Height,L"0");CHECK(!f.dialog->valid());f.set(id::Height,L"25.4");CHECK(f.dialog->valid());
    f.click(IDOK);CHECK(f.dialog->canvas_result()&&f.dialog->canvas_result()->width==150&&f.dialog->canvas_result()->height==150&&f.dialog->canvas_result()->unit==4);
}
TEST(resolution_and_unit_preferences_roundtrip_and_invalid_ini_is_bounded) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-resolution-"+std::to_wstring(GetCurrentProcessId())+L".ini");std::filesystem::remove(path);
    Settings fresh;fresh.load(path);CHECK(fresh.resize_resample&&fresh.resize_print_unit==3&&fresh.resize_density_unit==0&&fresh.canvas_unit==0);
    fresh.resize_resample=false;fresh.resize_print_unit=4;fresh.resize_density_unit=1;fresh.canvas_unit=2;fresh.save(path);
    Settings saved;saved.load(path);CHECK(!saved.resize_resample&&saved.resize_print_unit==4&&saved.resize_density_unit==1&&saved.canvas_unit==2);
    CHECK(WritePrivateProfileStringW(L"Viewer",L"resize_print_unit",L"9",path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer",L"resize_density_unit",L"-1",path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer",L"canvas_unit",L"9",path.c_str()));
    saved.load(path);CHECK(saved.resize_print_unit>=2&&saved.resize_print_unit<=4&&saved.resize_density_unit>=0&&saved.resize_density_unit<=1&&saved.canvas_unit>=0&&saved.canvas_unit<=4);
    std::filesystem::remove(path);
}
int main() {
    gvn_test::ClipboardLock lock;if(!lock)return 2;
    const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
    if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;
    const auto desktop=CreateDesktopW(L"GVN-resize-dialog-test",nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop||!SetThreadDesktop(desktop))return 3;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;
    const auto result=run_tests();OleUninitialize();return result;
}
