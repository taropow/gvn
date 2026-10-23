#include "gvn/color_dialog.h"
#include "gvn/ui_theme.h"
#include "gvn/core.h"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace gvn {
namespace {
namespace id=color_control;
double normalized_hue(double h){return h==360?0:h;}
std::wstring decimal(double n){wchar_t value[32]{};swprintf_s(value,L"%.1f",n);std::wstring s=value;if(s.ends_with(L".0"))s.resize(s.size()-2);return s;}
std::wstring hex(uint32_t rgb){wchar_t value[8]{};swprintf_s(value,L"%06X",rgb);return value;}
struct Template {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    Template(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;dialog.cx=200;dialog.cy=260;}
};
}
HsvColor rgb_to_hsv(uint32_t rgb,double fallback){
    if(rgb>0xffffff||!std::isfinite(fallback)||fallback<0||fallback>360)throw std::invalid_argument("Invalid RGB or hue");
    const double r=(rgb>>16)/255.,g=(rgb>>8&255)/255.,b=(rgb&255)/255.;
    const double maximum=std::max({r,g,b}),minimum=std::min({r,g,b}),delta=maximum-minimum;
    double hue=normalized_hue(fallback);
    if(delta>0){hue=60*(maximum==r?(g-b)/delta:maximum==g?2+(b-r)/delta:4+(r-g)/delta);if(hue<0)hue+=360;}
    return {hue,maximum==0?0:delta/maximum,maximum};
}
uint32_t hsv_to_rgb(HsvColor hsv){
    if(!std::isfinite(hsv.hue)||!std::isfinite(hsv.saturation)||!std::isfinite(hsv.value)||hsv.hue<0||hsv.hue>360||hsv.saturation<0||hsv.saturation>1||hsv.value<0||hsv.value>1)throw std::invalid_argument("Invalid HSV");
    const double h=normalized_hue(hsv.hue)/60,c=hsv.value*hsv.saturation,x=c*(1-std::abs(std::fmod(h,2)-1)),m=hsv.value-c;
    std::array<double,3> rgb{};
    switch(int(h)){case 0:rgb={c,x,0};break;case 1:rgb={x,c,0};break;case 2:rgb={0,c,x};break;case 3:rgb={0,x,c};break;case 4:rgb={x,0,c};break;default:rgb={c,0,x};}
    return uint32_t(std::lround((rgb[0]+m)*255))<<16|uint32_t(std::lround((rgb[1]+m)*255))<<8|uint32_t(std::lround((rgb[2]+m)*255));
}
ColorWheelGeometry color_wheel_geometry(int width,int height){
    const double side=std::max(1,std::min(width,height)),x=width/2.,y=height/2.,outer=side*.482,inner=side*.392;
    const int half=int(std::floor(inner/std::sqrt(2.)))-1;
    return {x,y,inner,outer,{int(x)-half,int(y)-half,int(x)+half+1,int(y)+half+1}};
}
struct ColorDialog::Impl {
    HWND hwnd{};UINT dpi{96};uint32_t original{},rgb{};HsvColor hsv;
    bool modal{},syncing{},input_valid{true};int dragging{};
    std::optional<uint32_t> selected;
    std::vector<uint32_t> ring,bitmap;int bitmap_w{},bitmap_h{};double bitmap_hue{-1};uint64_t palette_generation{};
    HWND control(int key) const{return GetDlgItem(hwnd,key);}
    int px(int n) const{return MulDiv(n,dpi,96);}
    void fill(int keep=0){
        syncing=true;
        if(keep!=1)for(int n=0;n<3;++n)SetWindowTextW(control(id::Red+n),std::to_wstring(rgb>>(16-n*8)&255).c_str());
        if(keep!=2){SetWindowTextW(control(id::Hue),decimal(hsv.hue).c_str());SetWindowTextW(control(id::Saturation),decimal(hsv.saturation*100).c_str());SetWindowTextW(control(id::Value),decimal(hsv.value*100).c_str());}
        if(keep!=3)SetWindowTextW(control(id::Hex),hex(rgb).c_str());
        input_valid=true;EnableWindow(control(IDOK),TRUE);SetWindowTextW(control(id::Error),L"");syncing=false;
        InvalidateRect(control(id::Wheel),nullptr,FALSE);InvalidateRect(control(id::Swatch),nullptr,FALSE);
    }
    double number(int key,double maximum,bool integer=false){
        wchar_t text[32]{};GetWindowTextW(control(key),text,32);wchar_t *end{};const auto value=wcstod(text,&end);
        if(!*text||!end||*end||!std::isfinite(value)||value<0||value>maximum||(integer&&value!=std::floor(value)))throw std::invalid_argument("Invalid color component");return value;
    }
    void changed(int key){
        if(syncing)return;
        try{
            if(key>=id::Red&&key<=id::Blue){rgb=(uint32_t(number(id::Red,255,true))<<16)|(uint32_t(number(id::Green,255,true))<<8)|uint32_t(number(id::Blue,255,true));hsv=rgb_to_hsv(rgb,hsv.hue);fill(1);}
            else if(key>=id::Hue&&key<=id::Value){const HsvColor next{normalized_hue(number(id::Hue,360)),number(id::Saturation,100)/100,number(id::Value,100)/100};const auto next_rgb=hsv_to_rgb(next);hsv=next;rgb=next_rgb;fill(2);}
            else if(key==id::Hex){wchar_t value[16]{};GetWindowTextW(control(key),value,16);const wchar_t *start=value[0]==L'#'?value+1:value;
                if(wcslen(start)!=6||wcsspn(start,L"0123456789abcdefABCDEF")!=6)throw std::invalid_argument("Invalid RGB hex");
                rgb=static_cast<uint32_t>(wcstoul(start,nullptr,16));hsv=rgb_to_hsv(rgb,hsv.hue);fill(3);}
        }catch(...){input_valid=false;EnableWindow(control(IDOK),FALSE);SetWindowTextW(control(id::Error),L"RGB: 0–255　H: 0–360　S/V: 0–100");}
    }
    void layout(){
        const auto place=[&](int key,int x,int y,int w,int h){SetWindowPos(control(key),nullptr,px(x),px(y),px(w),px(h),SWP_NOZORDER|SWP_NOACTIVATE);};
        place(id::Wheel,20,10,240,240);
        for(int n=0;n<3;++n){place(800+n,12+n*90,264,18,24);place(id::Red+n,31+n*90,264,57,24);place(803+n,12+n*90,296,18,24);place(id::Hue+n,31+n*90,296,57,24);}
        place(806,12,328,18,24);place(id::Hex,31,328,97,24);place(id::Swatch,144,328,124,24);
        place(id::Error,12,357,256,18);place(IDCANCEL,90,381,92,28);place(IDOK,190,381,78,28);
    }
    void raster(int w,int h){
        const auto &palette=ui_palette();const auto background=(uint32_t(GetRValue(palette.surface))<<16)|(uint32_t(GetGValue(palette.surface))<<8)|GetBValue(palette.surface);
        const auto geometry=color_wheel_geometry(w,h);
        if(w!=bitmap_w||h!=bitmap_h||palette_generation!=ui_theme_generation()){
            bitmap_w=w;bitmap_h=h;palette_generation=ui_theme_generation();bitmap_hue=-1;ring.assign(size_t(w)*h,background);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x){const double dx=x+.5-geometry.x,dy=y+.5-geometry.y,distance=std::hypot(dx,dy),coverage=std::clamp(std::min(distance-geometry.inner,geometry.outer-distance)+.5,0.,1.);
                if(coverage==0)continue;double hue=std::atan2(dy,dx)*180/std::numbers::pi;if(hue<0)hue+=360;const auto color=hsv_to_rgb({hue,1,1});uint32_t pixel{};
                for(int shift:{0,8,16})pixel|=uint32_t(std::lround((color>>shift&255)*coverage+(background>>shift&255)*(1-coverage)))<<shift;
                ring[size_t(y)*w+x]=pixel;
            }
        }
        if(bitmap_hue!=hsv.hue||bitmap.size()!=ring.size()){
            bitmap=ring;bitmap_hue=hsv.hue;const auto hue=hsv_to_rgb({hsv.hue,1,1});const auto &r=geometry.square;
            for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x){const double s=double(x-r.left)/(r.right-r.left-1),v=1-double(y-r.top)/(r.bottom-r.top-1);uint32_t color{};
                for(int shift:{0,8,16})color|=uint32_t(std::lround(v*(255*(1-s)+(hue>>shift&255)*s)))<<shift;bitmap[size_t(y)*w+x]=color;
            }
        }
    }
    void paint_wheel(HWND window,HDC dc){
        RECT area{};GetClientRect(window,&area);if(area.right<8||area.bottom<8)return;raster(area.right,area.bottom);
        BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),area.right,-area.bottom,1,32,BI_RGB};
        SetDIBitsToDevice(dc,0,0,area.right,area.bottom,0,0,0,area.bottom,bitmap.data(),&info,DIB_RGB_COLORS);
        const auto geometry=color_wheel_geometry(area.right,area.bottom);const double angle=hsv.hue*std::numbers::pi/180,radius=(geometry.inner+geometry.outer)/2;
        const auto marker=[&](double x,double y,int size){const int cx=int(std::lround(x)),cy=int(std::lround(y));const auto brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
            for(auto [color,width]:{std::pair{RGB(255,255,255),3},std::pair{RGB(0,0,0),1}}){const auto pen=CreatePen(PS_SOLID,px(width),color);const auto old=SelectObject(dc,pen);Ellipse(dc,cx-size,cy-size,cx+size+1,cy+size+1);SelectObject(dc,old);DeleteObject(pen);}SelectObject(dc,brush);};
        marker(geometry.x+std::cos(angle)*radius,geometry.y+std::sin(angle)*radius,px(7));
        const auto &r=geometry.square;marker(r.left+hsv.saturation*(r.right-r.left-1),r.top+(1-hsv.value)*(r.bottom-r.top-1),px(5));
        if(GetFocus()==window){auto focus=area;InflateRect(&focus,-1,-1);DrawFocusRect(dc,&focus);}
    }
    void pointer(POINT p){
        RECT r{};GetClientRect(control(id::Wheel),&r);const auto g=color_wheel_geometry(r.right,r.bottom);
        if(dragging==1){double hue=std::atan2(p.y-g.y,p.x-g.x)*180/std::numbers::pi;if(hue<0)hue+=360;hsv.hue=hue;}
        if(dragging==2){hsv.saturation=std::clamp(double(p.x-g.square.left)/(g.square.right-g.square.left-1),0.,1.);hsv.value=1-std::clamp(double(p.y-g.square.top)/(g.square.bottom-g.square.top-1),0.,1.);}
        rgb=hsv_to_rgb(hsv);fill();
    }
    static LRESULT CALLBACK wheel_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
        const auto self=reinterpret_cast<Impl *>(data);
        if(m==WM_PAINT||m==WM_PRINTCLIENT){PAINTSTRUCT paint{};const auto dc=m==WM_PAINT?BeginPaint(h,&paint):reinterpret_cast<HDC>(w);self->paint_wheel(h,dc);if(m==WM_PAINT)EndPaint(h,&paint);return 0;}
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_GETDLGCODE)return DLGC_WANTARROWS;
        if(m==WM_KEYDOWN&&(w==VK_LEFT||w==VK_RIGHT||w==VK_UP||w==VK_DOWN)){self->hsv.hue=std::fmod(self->hsv.hue+((w==VK_LEFT||w==VK_DOWN)?-1:1)*((GetKeyState(VK_SHIFT)&0x8000)?10:1)+360,360);self->rgb=hsv_to_rgb(self->hsv);self->fill();return 0;}
        const POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        if(m==WM_LBUTTONDOWN){RECT bounds{};GetClientRect(h,&bounds);const auto g=color_wheel_geometry(bounds.right,bounds.bottom);const double radius=std::hypot(point.x-g.x,point.y-g.y);
            self->dragging=PtInRect(&g.square,point)?2:radius>=g.inner&&radius<=g.outer?1:0;if(self->dragging){SetFocus(h);SetCapture(h);self->pointer(point);}return 0;}
        if(m==WM_MOUSEMOVE&&self->dragging&&GetCapture()==h){if(w&MK_LBUTTON)self->pointer(point);else{self->dragging=0;ReleaseCapture();}return 0;}
        if(m==WM_LBUTTONUP){if(self->dragging&&GetCapture()==h)self->pointer(point);self->dragging=0;if(GetCapture()==h)ReleaseCapture();return 0;}
        if(m==WM_CAPTURECHANGED||m==WM_CANCELMODE){self->dragging=0;if(m==WM_CANCELMODE&&GetCapture()==h)ReleaseCapture();}
        if(m==WM_SETFOCUS||m==WM_KILLFOCUS)InvalidateRect(h,nullptr,FALSE);
        return DefSubclassProc(h,m,w,l);
    }
    void finish(bool accept){if(accept&&!input_valid)return;if(accept)selected=rgb;if(GetCapture()==control(id::Wheel))ReleaseCapture();if(modal)EndDialog(hwnd,accept?IDOK:IDCANCEL);else DestroyWindow(hwnd);}
    void initialize(){
        SetWindowTextW(hwnd,L"色の選択");dpi=GetDpiForWindow(hwnd);
        const auto add=[&](int key,const wchar_t *cls,const wchar_t *name,DWORD style){const auto h=CreateWindowExW(0,cls,name,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,reinterpret_cast<HMENU>(INT_PTR(key)),GetModuleHandleW(nullptr),nullptr);if(!h)throw std::runtime_error("Cannot create color control");return h;};
        const auto wheel=add(id::Wheel,L"STATIC",L"色相リングと彩度・明度",WS_TABSTOP|SS_NOTIFY);SetWindowSubclass(wheel,wheel_proc,81,reinterpret_cast<DWORD_PTR>(this));
        const wchar_t *labels[]{L"R",L"G",L"B",L"H",L"S",L"V"};
        for(int n=0;n<6;++n){add(800+n,L"STATIC",labels[n],SS_CENTERIMAGE);const auto edit=add(n<3?id::Red+n:id::Hue+n-3,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL);SendMessageW(edit,EM_SETLIMITTEXT,n<3?3:8,0);}
        add(806,L"STATIC",L"#",SS_CENTERIMAGE);const auto value=add(id::Hex,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL);SendMessageW(value,EM_SETLIMITTEXT,7,0);
        add(id::Swatch,L"BUTTON",L"元の色に戻す／選択色",WS_TABSTOP|BS_OWNERDRAW);add(id::Error,L"STATIC",L"",0);
        add(IDCANCEL,L"BUTTON",L"キャンセル",WS_TABSTOP|BS_PUSHBUTTON);add(IDOK,L"BUTTON",L"OK",WS_TABSTOP|BS_DEFPUSHBUTTON);
        RECT bounds{0,0,px(280),px(421)};AdjustWindowRectExForDpi(&bounds,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);
        RECT owner{};if(!GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner))SystemParametersInfoW(SPI_GETWORKAREA,0,&owner,0);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
        const auto placed=place_window({owner.left+(owner.right-owner.left-bounds.right+bounds.left)/2,owner.top+(owner.bottom-owner.top-bounds.bottom+bounds.top)/2,bounds.right-bounds.left,bounds.bottom-bounds.top},{monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top},false);
        SetWindowPos(hwnd,nullptr,placed.x,placed.y,placed.w,placed.h,SWP_NOZORDER|SWP_NOACTIVATE);layout();fill();apply_ui_theme(hwnd);SetFocus(control(id::Red));SendMessageW(control(id::Red),EM_SETSEL,0,-1);
    }
    INT_PTR message(UINT m,WPARAM w,LPARAM l){
        if(m==WM_INITDIALOG){initialize();return FALSE;}
        if(m==WM_COMMAND){const int key=LOWORD(w),code=HIWORD(w);if(key==IDOK||key==IDCANCEL){finish(key==IDOK);return TRUE;}if(key==id::Swatch&&code==BN_CLICKED){rgb=original;hsv=rgb_to_hsv(rgb,hsv.hue);fill();return TRUE;}if(code==EN_CHANGE){changed(key);return TRUE;}}
        if(m==WM_CLOSE){finish(false);return TRUE;}
        if(m==WM_DPICHANGED){dpi=HIWORD(w);const auto bounds=reinterpret_cast<RECT *>(l);SetWindowPos(hwnd,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER|SWP_NOACTIVATE);layout();apply_ui_theme(hwnd);return TRUE;}
        if(m==WM_DRAWITEM&&w==id::Swatch){const auto &draw=*reinterpret_cast<DRAWITEMSTRUCT *>(l);auto area=draw.rcItem;
            for(int n=0;n<2;++n){const auto color=n?rgb:original;auto part=area;if(!n)part.right=(area.left+area.right)/2;else part.left=(area.left+area.right)/2;
                const auto brush=CreateSolidBrush(RGB(color>>16&255,color>>8&255,color&255));FillRect(draw.hDC,&part,brush);DeleteObject(brush);}
            FrameRect(draw.hDC,&area,ui_brush(ui_palette().border));const int mid=(area.left+area.right)/2;RECT line{mid,area.top,mid+1,area.bottom};FillRect(draw.hDC,&line,ui_brush(ui_palette().border));if(draw.itemState&ODS_FOCUS){InflateRect(&area,-3,-3);DrawFocusRect(draw.hDC,&area);}return TRUE;}
        return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<Impl *>(GetWindowLongPtrW(h,DWLP_USER));if(m==WM_INITDIALOG){self=reinterpret_cast<Impl *>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}if(!self)return FALSE;
        try{return self->message(m,w,l);}catch(...){self->finish(false);return TRUE;}
    }
};
ColorDialog::ColorDialog(uint32_t rgb):impl(std::make_unique<Impl>()){impl->original=impl->rgb=rgb;impl->hsv=rgb_to_hsv(rgb);}
ColorDialog::~ColorDialog(){if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);}
std::optional<uint32_t> ColorDialog::show(HWND owner){Template value;impl->modal=true;if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)throw std::runtime_error("Cannot open color dialog");return impl->selected;}
HWND ColorDialog::create(HWND owner){Template value;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create color dialog");return h;}
HWND ColorDialog::window() const{return impl->hwnd;}
HWND ColorDialog::control(int key) const{return impl->control(key);}
uint32_t ColorDialog::color() const{return impl->rgb;}
HsvColor ColorDialog::hsv() const{return impl->hsv;}
bool ColorDialog::valid() const{return impl->input_valid;}
std::optional<uint32_t> ColorDialog::result() const{return impl->selected;}
std::optional<uint32_t> choose_color(HWND owner,uint32_t rgb){ColorDialog dialog(rgb);return dialog.show(owner);}
} // namespace gvn
