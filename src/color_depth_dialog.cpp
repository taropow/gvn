#include "gvn/color_depth_dialog.h"
#include <commctrl.h>
#include <windowsx.h>
namespace gvn {
namespace {
namespace id=color_depth_control;
constexpr ColorDepth choices[]{ColorDepth::Indexed8,ColorDepth::Rgb555,ColorDepth::Rgb565,ColorDepth::Rgb24,ColorDepth::Optimal};
constexpr const wchar_t *labels[]{L"&8 BPP（256色）",L"16 BPP RGB 5&55（3万2千色）",L"16 BPP RGB 5&65（6万5千色）",L"&24 BPP（1677万色）",L"最適なBPPにする(&A)"};
struct Template {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    Template(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;dialog.cx=280;dialog.cy=250;}
};
}
struct ColorDepthDialog::Impl {
    HWND hwnd{};HFONT font{};UINT dpi{96};unsigned bits{};std::optional<Rgb16Layout> layout;
    bool modal{},initialized{},lost{};int wheel{};ColorDepthChoice draft;Current current;
    std::optional<ColorDepthChoice> selected;
    HWND control(int key) const{return GetDlgItem(hwnd,key);}
    bool context() const{return !lost&&(!current||current());}
    bool enabled(int n) const {
        return !(n==0&&bits==8)&&!(n==1&&bits==16&&layout==Rgb16Layout::Rgb555)&&
               !(n==2&&bits==16&&layout==Rgb16Layout::Rgb565)&&!(n==3&&bits==24);
    }
    void checks() {for(int n=0;n<5;++n)SendMessageW(control(id::ChoiceBase+n),BM_SETCHECK,draft.depth==choices[n]?BST_CHECKED:BST_UNCHECKED,0);}
    void choose(int n) {if(context()&&n>=0&&n<5&&enabled(n))draft.depth=choices[n];checks();}
    void invalidate() {
        lost=true;EnableWindow(control(IDOK),FALSE);
        SetWindowTextW(control(id::Error),L"表示画像が変わりました。閉じてから開き直してください。");
    }
    void scroll(int delta) {
        if(!context()){invalidate();return;}wheel+=delta;
        while(wheel>=WHEEL_DELTA||wheel<=-WHEEL_DELTA) {
            const int step=wheel>0?-1:1;wheel+=step*WHEEL_DELTA;int at=0;
            while(at<4&&draft.depth!=choices[at])++at;
            do{at=(at+step+5)%5;}while(!enabled(at));choose(at);SetFocus(control(id::ChoiceBase+at));
        }
    }
    static LRESULT CALLBACK child(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        if(m==WM_MOUSEWHEEL){SendMessageW(reinterpret_cast<Impl*>(data)->hwnd,m,w,l);return 0;}
        return DefSubclassProc(h,m,w,l);
    }
    void set_font() {
        const auto next=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Yu Gothic UI");
        if(!next)throw std::runtime_error("Cannot create color depth font");
        for(int key:{id::ChoiceBase,id::ChoiceBase+1,id::ChoiceBase+2,id::ChoiceBase+3,id::ChoiceBase+4,id::Dither,id::Current,id::Alpha,id::Error,id::Group,IDOK,IDCANCEL})
            SendMessageW(control(key),WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(font)DeleteObject(font);font=next;
    }
    void arrange() {
        const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT area{};GetClientRect(hwnd,&area);const int width=MulDiv(area.right,96,dpi),height=MulDiv(area.bottom,96,dpi);
        const auto move=[&](int key,int x,int y,int w,int h){MoveWindow(control(key),px(x),px(y),px(w),px(h),TRUE);};
        move(id::Current,16,14,width-32,24);move(id::Group,16,46,width-32,166);
        for(int n=0;n<5;++n)move(id::ChoiceBase+n,30,68+n*26,width-60,22);
        move(id::Dither,16,226,width-32,24);move(id::Alpha,16,262,width-32,38);move(id::Error,16,304,width-32,height-350);
        move(IDOK,width-196,height-42,82,28);move(IDCANCEL,width-104,height-42,88,28);
    }
    void finish(bool accept) {
        if(accept&&!context()){invalidate();return;}
        if(accept)selected=draft;KillTimer(hwnd,1);
        if(modal)EndDialog(hwnd,accept?IDOK:IDCANCEL);else DestroyWindow(hwnd);
    }
    INT_PTR message(UINT m,WPARAM w,LPARAM l) {
        if(m==WM_INITDIALOG) {
            SetWindowTextW(hwnd,L"減色/増色");dpi=GetDpiForWindow(hwnd);
            const auto add=[&](int key,const wchar_t *cls,const wchar_t *text,DWORD style){
                const auto h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,reinterpret_cast<HMENU>(INT_PTR(key)),GetModuleHandleW(nullptr),nullptr);
                if(!h)throw std::runtime_error("Cannot create color depth control");return h;};
            const auto caption=L"現在: "+std::to_wstring(bits)+L" BPP"+(bits==16?(layout==Rgb16Layout::Rgb565?std::wstring(L" RGB565"):std::wstring(L" RGB555")):std::wstring{});
            add(id::Current,L"STATIC",caption.c_str(),0);add(id::Group,L"BUTTON",L"色数指定",BS_GROUPBOX);
            for(int n=0;n<5;++n){const auto h=add(id::ChoiceBase+n,L"BUTTON",labels[n],BS_AUTORADIOBUTTON|WS_TABSTOP|(n==0?WS_GROUP:0));EnableWindow(h,enabled(n));
                if(!SetWindowSubclass(h,child,1,reinterpret_cast<DWORD_PTR>(this)))throw std::runtime_error("Cannot initialize depth wheel");}
            const auto dither=add(id::Dither,L"BUTTON",L"誤差拡散する(&D)",BS_AUTOCHECKBOX|WS_TABSTOP|WS_GROUP);
            SendMessageW(dither,BM_SETCHECK,draft.dither?BST_CHECKED:BST_UNCHECKED,0);SetWindowSubclass(dither,child,1,reinterpret_cast<DWORD_PTR>(this));
            add(id::Alpha,L"STATIC",L"Alphaは維持します。\nAlpha付き画像はPNGで保存できます。",0);add(id::Error,L"STATIC",L"",0);
            add(IDOK,L"BUTTON",L"OK",WS_TABSTOP|WS_GROUP|BS_DEFPUSHBUTTON);add(IDCANCEL,L"BUTTON",L"キャンセル",WS_TABSTOP);set_font();checks();
            const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT area{0,0,px(420),px(380)};
            AdjustWindowRectExForDpi(&area,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);
            RECT owner{};GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
            const auto placed=place_window({owner.left+(owner.right-owner.left-area.right+area.left)/2,owner.top+(owner.bottom-owner.top-area.bottom+area.top)/2,area.right-area.left,area.bottom-area.top},
                {monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top},false);
            SetWindowPos(hwnd,nullptr,placed.x,placed.y,placed.w,placed.h,SWP_NOZORDER|SWP_NOACTIVATE);arrange();initialized=true;
            if(!SetTimer(hwnd,1,80,nullptr))throw std::runtime_error("Cannot monitor color depth context");
            SetFocus(control(id::ChoiceBase+4));return FALSE;
        }
        if(m==WM_COMMAND&&initialized) {
            const auto key=LOWORD(w);if(key==IDOK||key==IDCANCEL){finish(key==IDOK);return TRUE;}
            if(key>=id::ChoiceBase&&key<id::ChoiceBase+5){choose(key-id::ChoiceBase);return TRUE;}
            if(key==id::Dither){draft.dither=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;return TRUE;}
        }
        if(m==WM_MOUSEWHEEL&&initialized){scroll(GET_WHEEL_DELTA_WPARAM(w));return TRUE;}
        if(m==WM_TIMER&&w==1&&!context()&&!lost){invalidate();return TRUE;}
        if(m==WM_DPICHANGED){dpi=HIWORD(w);const auto r=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);set_font();arrange();return TRUE;}
        if(m==WM_CLOSE){finish(false);return TRUE;}
        if(m==WM_DESTROY){KillTimer(hwnd,1);initialized=false;return TRUE;}return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));
        if(m==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}if(!self)return FALSE;
        try{return self->message(m,w,l);}catch(...){self->invalidate();if(!self->initialized)PostMessageW(h,WM_CLOSE,0,0);return TRUE;}
    }
};
ColorDepthDialog::ColorDepthDialog(unsigned bits,std::optional<Rgb16Layout> layout,bool dither,Current current):impl(std::make_unique<Impl>()) {
    if((bits!=1&&bits!=2&&bits!=4&&bits!=8&&bits!=16&&bits!=24)||(bits==16&&(!layout||(*layout!=Rgb16Layout::Rgb555&&*layout!=Rgb16Layout::Rgb565))))
        throw std::invalid_argument("Invalid current color depth");
    impl->bits=bits;impl->layout=layout;impl->draft.dither=dither;impl->current=std::move(current);
}
ColorDepthDialog::~ColorDepthDialog(){if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);if(impl->font)DeleteObject(impl->font);}
std::optional<ColorDepthChoice> ColorDepthDialog::show(HWND owner){Template value;impl->modal=true;if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)throw std::runtime_error("Cannot open color depth dialog");return impl->selected;}
HWND ColorDepthDialog::create(HWND owner){Template value;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create color depth dialog");return h;}
HWND ColorDepthDialog::window() const{return impl->hwnd;}
HWND ColorDepthDialog::control(int key) const{return impl->control(key);}
ColorDepthChoice ColorDepthDialog::options() const{return impl->draft;}
std::optional<ColorDepthChoice> ColorDepthDialog::result() const{return impl->selected;}
bool ColorDepthDialog::ready() const{return impl->initialized;}
bool ColorDepthDialog::valid() const{return impl->context();}
std::optional<ColorDepthChoice> choose_color_depth(HWND owner,unsigned bits,std::optional<Rgb16Layout> layout,bool dither,const ColorDepthDialog::Current &current){ColorDepthDialog dialog(bits,layout,dither,current);return dialog.show(owner);}
} // namespace gvn
