#include "gvn/color_dialog.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <cmath>
#include <limits>
using namespace gvn;
namespace id=color_control;
namespace {
struct Fixture {
    HWND owner=CreateWindowExW(0,L"STATIC",L"Owned color tests",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,800,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ColorDialog dialog;
    explicit Fixture(uint32_t rgb=0xf40070):dialog(rgb){CHECK(owner);CHECK(dialog.create(owner));ShowWindow(dialog.window(),SW_SHOWNOACTIVATE);}
    ~Fixture(){if(IsWindow(dialog.window()))DestroyWindow(dialog.window());DestroyWindow(owner);}
    void set(int key,const wchar_t *text){CHECK(SetWindowTextW(dialog.control(key),text));}
    void click(int key){SendMessageW(dialog.control(key),BM_CLICK,0,0);}
    void rgb(int r,int g,int b){for(auto [key,n]:{std::pair{id::Red,r},std::pair{id::Green,g},std::pair{id::Blue,b}})set(key,std::to_wstring(n).c_str());}
    void hsv(int h,int s,int v){for(auto [key,n]:{std::pair{id::Hue,h},std::pair{id::Saturation,s},std::pair{id::Value,v}})set(key,std::to_wstring(n).c_str());}
    void mouse(UINT m,int x,int y,WPARAM flags=MK_LBUTTON){SendMessageW(dialog.control(id::Wheel),m,flags,MAKELPARAM(x,y));}
};
struct Surface {
    HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap{};HGDIOBJ old{};
    Surface(int w,int h){const auto screen=GetDC(nullptr);bitmap=CreateCompatibleBitmap(screen,w,h);ReleaseDC(nullptr,screen);old=SelectObject(dc,bitmap);}
    ~Surface(){SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}
};
}
TEST(color_hsv_reference_primaries_grayscale_and_exact_rgb_roundtrips){
    for(auto [rgb,hue]:{std::pair{0xff0000u,0.},std::pair{0xffff00u,60.},std::pair{0x00ff00u,120.},std::pair{0x00ffffu,180.},std::pair{0x0000ffu,240.},std::pair{0xff00ffu,300.}}){const auto hsv=rgb_to_hsv(rgb);CHECK(hsv.hue==hue&&hsv.saturation==1&&hsv.value==1);CHECK(hsv_to_rgb(hsv)==rgb);}
    uint32_t state=123;for(int n=0;n<1024;++n){state=state*1664525+1013904223;const auto rgb=state&0xffffff;CHECK(hsv_to_rgb(rgb_to_hsv(rgb))==rgb);}
    const auto gray=rgb_to_hsv(0x808080,234);CHECK(gray.hue==234&&gray.saturation==0&&hsv_to_rgb(gray)==0x808080);
    CHECK(hsv_to_rgb({360,1,1})==0xff0000);THROWS(rgb_to_hsv(0x1000000));THROWS(hsv_to_rgb({-1,1,1}));THROWS(hsv_to_rgb({0,1.1,1}));THROWS(hsv_to_rgb({0,1,std::numeric_limits<double>::quiet_NaN()}));
}
TEST(color_numeric_hex_and_achromatic_hue_stay_synchronized_without_early_commit){
    Fixture f(0x102030);CHECK(f.dialog.color()==0x102030&&!f.dialog.result());f.rgb(255,0,0);CHECK(f.dialog.color()==0xff0000&&f.dialog.hsv().hue==0);
    f.hsv(240,100,100);CHECK(f.dialog.color()==0x0000ff&&GetDlgItemInt(f.dialog.window(),id::Blue,nullptr,FALSE)==255);
    f.hsv(120,0,50);CHECK(f.dialog.color()==0x808080&&f.dialog.hsv().hue==120);f.set(id::Saturation,L"100");CHECK(f.dialog.color()==0x008000);
    f.set(id::Hex,L"#a1b2C3");CHECK(f.dialog.color()==0xa1b2c3&&f.dialog.valid());
    CHECK(GetDlgItemInt(f.dialog.window(),id::Red,nullptr,FALSE)==161);f.click(IDOK);CHECK(f.dialog.result()==0xa1b2c3);
}
TEST(color_invalid_input_disables_ok_and_cancel_or_restore_keeps_original_color){
    Fixture f(0x112233);for(auto [key,value]:{std::pair{id::Red,L"256"},std::pair{id::Hue,L"361"},std::pair{id::Saturation,L"-1"},std::pair{id::Value,L"nan"},std::pair{id::Hex,L"GGFFFF"},std::pair{id::Red,L""}}){
        f.set(key,value);CHECK(!f.dialog.valid()&&!IsWindowEnabled(f.dialog.control(IDOK)));f.click(IDOK);CHECK(IsWindow(f.dialog.window())&&!f.dialog.result());f.click(id::Swatch);CHECK(f.dialog.valid()&&f.dialog.color()==0x112233);
    }
    f.rgb(99,128,217);f.click(IDCANCEL);CHECK(!f.dialog.result());
}
TEST(color_ring_sv_drag_clamps_and_capture_cancellation_and_keyboard_work){
    Fixture f(0xff0000);const auto wheel=f.dialog.control(id::Wheel);RECT bounds{};GetClientRect(wheel,&bounds);const auto g=color_wheel_geometry(bounds.right,bounds.bottom);
    const int center=int(g.x),y=int(g.y),radius=int((g.inner+g.outer)/2);
    f.mouse(WM_LBUTTONDOWN,center+radius,y);CHECK(GetCapture()==wheel);f.mouse(WM_MOUSEMOVE,center,y+radius);f.mouse(WM_LBUTTONUP,center,y+radius,0);CHECK(std::abs(f.dialog.hsv().hue-90)<.01&&GetCapture()!=wheel);
    const int x=(g.square.left+g.square.right)/2,top=g.square.top;
    f.mouse(WM_LBUTTONDOWN,x,top);f.mouse(WM_MOUSEMOVE,g.square.right+80,g.square.top-50);f.mouse(WM_LBUTTONUP,g.square.right+80,g.square.top-50,0);CHECK(f.dialog.hsv().saturation==1&&f.dialog.hsv().value==1);
    f.mouse(WM_LBUTTONDOWN,x,top);SendMessageW(wheel,WM_CANCELMODE,0,0);CHECK(GetCapture()!=wheel);const auto prior=f.dialog.color();f.mouse(WM_MOUSEMOVE,0,0);CHECK(f.dialog.color()==prior);
    const auto hue=f.dialog.hsv().hue;SendMessageW(wheel,WM_KEYDOWN,VK_RIGHT,0);CHECK(std::abs(f.dialog.hsv().hue-hue-1)<.01);f.click(IDCANCEL);CHECK(!f.dialog.result());
}
TEST(color_wheel_paints_reference_corners_and_keeps_gdi_resources_bounded){
    set_ui_theme(1);Fixture f(0xff0000);const auto wheel=f.dialog.control(id::Wheel);RECT bounds{};GetClientRect(wheel,&bounds);const auto g=color_wheel_geometry(bounds.right,bounds.bottom);Surface surface(bounds.right,bounds.bottom);
    SetFocus(f.dialog.control(id::Red));SendMessageW(wheel,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface.dc),PRF_CLIENT);
    CHECK(GetPixel(surface.dc,g.square.left+1,g.square.bottom-2)==RGB(2,2,2));
    const auto near_red=GetPixel(surface.dc,g.square.right-3,g.square.top+2);CHECK(GetRValue(near_red)>245&&GetGValue(near_red)<10&&GetBValue(near_red)<10);
    const auto resources=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD warm{};
    for(int n=0;n<40;++n){f.hsv(n*9,73,91);SendMessageW(wheel,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface.dc),PRF_CLIENT);Surface swatch(124,24);DRAWITEMSTRUCT draw{};draw.CtlType=ODT_BUTTON;draw.CtlID=id::Swatch;draw.hwndItem=f.dialog.control(id::Swatch);draw.hDC=swatch.dc;draw.rcItem={0,0,124,24};SendMessageW(f.dialog.window(),WM_DRAWITEM,id::Swatch,reinterpret_cast<LPARAM>(&draw));if(n==19)warm=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);}
    const auto after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);std::cout<<"GDI baseline="<<resources<<" warm="<<warm<<" after="<<after<<std::endl;CHECK(after<=warm+1);
    set_ui_theme(2);apply_ui_theme(f.dialog.window());SendMessageW(wheel,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(surface.dc),PRF_CLIENT);CHECK(GetPixel(surface.dc,0,0)==ui_palette().surface);set_ui_theme(1);
}
TEST(color_modeless_enter_escape_and_modal_owner_transaction){
    {Fixture f;MSG key{f.dialog.window(),WM_KEYDOWN,VK_ESCAPE};CHECK(IsDialogMessageW(f.dialog.window(),&key));CHECK(!IsWindow(f.dialog.window())&&!f.dialog.result());}
    Fixture f(0x112233);f.click(IDCANCEL);bool owner_disabled{};SetPropW(f.owner,L"GVN.TestOwnerDisabled",&owner_disabled);const auto timer=SetTimer(f.owner,50,10,[](HWND owner,UINT,UINT_PTR id,DWORD){
        const auto popup=GetWindow(owner,GW_ENABLEDPOPUP);if(popup==owner||!GetDlgItem(popup,color_control::Hex))return;*static_cast<bool *>(GetPropW(owner,L"GVN.TestOwnerDisabled"))=!IsWindowEnabled(owner);
        SetWindowTextW(GetDlgItem(popup,color_control::Hex),L"AABBCC");KillTimer(owner,id);SendMessageW(GetDlgItem(popup,IDOK),BM_CLICK,0,0);
    });CHECK(timer);const auto chosen=choose_color(f.owner,0x102030);KillTimer(f.owner,timer);RemovePropW(f.owner,L"GVN.TestOwnerDisabled");CHECK(chosen==0xaabbcc&&owner_disabled&&IsWindowEnabled(f.owner));
}
TEST(dark_menu_owner_draw_accessible_strings_shortcuts_and_mnemonics_follow_theme){
    set_ui_theme(2);Fixture f;const auto bar=CreateMenu(),popup=CreatePopupMenu();AppendMenuW(popup,MF_STRING|MF_CHECKED,100,L"開く(&O)\tCtrl+O");AppendMenuW(popup,MF_STRING|MF_GRAYED,101,L"保存(&S)\tCtrl+S");AppendMenuW(popup,MF_SEPARATOR,0,nullptr);AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(popup),L"ファイル(&F)");
    ui_theme_menu(bar,true);SetMenu(f.owner,bar);apply_ui_theme(f.owner);DrawMenuBar(f.owner);
    for(const auto menu:{bar,popup})for(int n=0;n<GetMenuItemCount(menu);++n){MENUITEMINFOW info{sizeof(info)};info.fMask=MIIM_DATA|MIIM_FTYPE|MIIM_STRING;wchar_t title[128]{};info.dwTypeData=title;info.cch=128;CHECK(GetMenuItemInfoW(menu,n,TRUE,&info));CHECK(info.fType&MFT_OWNERDRAW);
        if(!(info.fType&MFT_SEPARATOR))CHECK(*title);MEASUREITEMSTRUCT measure{};measure.CtlType=ODT_MENU;measure.itemData=info.dwItemData;CHECK(SendMessageW(f.owner,WM_MEASUREITEM,0,reinterpret_cast<LPARAM>(&measure)));CHECK(measure.itemWidth>0&&measure.itemHeight>0);
        Surface surface(400,40);DRAWITEMSTRUCT draw{};draw.CtlType=ODT_MENU;draw.itemData=info.dwItemData;draw.hDC=surface.dc;draw.rcItem={0,0,400,40};draw.itemState=ODS_SELECTED;CHECK(SendMessageW(f.owner,WM_DRAWITEM,0,reinterpret_cast<LPARAM>(&draw)));CHECK(GetPixel(surface.dc,390,2)==ui_palette().selected);
    }
    const auto mnemonic=SendMessageW(f.owner,WM_MENUCHAR,L'O',reinterpret_cast<LPARAM>(popup));CHECK(HIWORD(mnemonic)==MNC_EXECUTE&&LOWORD(mnemonic)==0);
    set_ui_theme(1);apply_ui_theme(f.owner);SetMenu(f.owner,nullptr);ui_destroy_menu(bar);
}
int main(){
    gvn_test::ClipboardLock lock;if(!lock)return 2;const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;
    const auto desktop=CreateDesktopW(L"GVN-color-circle-test",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(FAILED(OleInitialize(nullptr))||!initialize_controls())return 4;
    const auto result=run_tests();OleUninitialize();return result;
}
