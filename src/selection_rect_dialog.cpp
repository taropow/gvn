#include "gvn/selection_rect_dialog.h"
#include <commctrl.h>
#include <array>
#include <cmath>
namespace gvn {
namespace {
namespace id=selection_rect_control;
struct Template {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    Template(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;dialog.cx=290;dialog.cy=230;}
};
int number(HWND field,int low,int high) {
    wchar_t s[16]{};const int length=GetWindowTextW(field,s,16);
    if(!length||length>10)throw std::invalid_argument("Invalid selection coordinate");
    int64_t value{};
    for(int n=0;n<length;++n){if(s[n]<L'0'||s[n]>L'9')throw std::invalid_argument("Invalid selection coordinate");value=value*10+s[n]-L'0';}
    if(value<low||value>high)throw std::invalid_argument("Selection coordinate is outside image");return int(value);
}
}
struct SelectionRectDialog::Impl {
    int width,height;std::array<int,4> draft;std::optional<Rect> selected;
    HWND hwnd{};HFONT font{};UINT dpi{96};bool initialized{},syncing{},valid{};
    Impl(uint32_t w,uint32_t h,Rect current):width(int(w)),height(int(h)) {
        if(!w||!h||w>INT_MAX||h>INT_MAX)throw std::invalid_argument("Invalid selection image size");
        current=intersect(current,{0,0,width,height});if(current.empty())current={0,0,width,height};
        draft={current.x,current.y,current.x+current.w-1,current.y+current.h-1};
    }
    HWND control(int key) const{return GetDlgItem(hwnd,key);}
    Rect rect() const{return {std::min(draft[0],draft[2]),std::min(draft[1],draft[3]),std::abs(draft[2]-draft[0])+1,std::abs(draft[3]-draft[1])+1};}
    void set(int key,int n){SetWindowTextW(control(key),std::to_wstring(n).c_str());}
    void fill(bool coordinates) {
        syncing=true;
        if(coordinates)for(int n=0;n<4;++n)set(id::Left+n,draft[n]);
        const auto r=rect();set(id::Width,r.w);set(id::Height,r.h);syncing=false;
    }
    void changed(int key) {
        if(!initialized||syncing)return;
        try {
            std::array<int,4> next{number(control(id::Left),0,width-1),number(control(id::Top),0,height-1),number(control(id::Right),0,width-1),number(control(id::Bottom),0,height-1)};
            if(key==id::Width||key==id::Height||key==0) {
                const int w=number(control(id::Width),1,width),h=number(control(id::Height),1,height);
                if(key==id::Width){next[0]=std::min(next[0],width-w);next[2]=next[0]+w-1;}
                else if(key==id::Height){next[1]=std::min(next[1],height-h);next[3]=next[1]+h-1;}
                else if(w!=std::abs(next[2]-next[0])+1||h!=std::abs(next[3]-next[1])+1)throw std::invalid_argument("Inconsistent selection coordinates");
            }
            draft=next;if(key)fill(key==id::Width||key==id::Height);
            valid=true;SetWindowTextW(control(id::Error),L"");
        }catch(...){valid=false;SetWindowTextW(control(id::Error),L"画像内の座標と、1以上の幅・高さを指定してください。");}
        EnableWindow(control(IDOK),valid);
    }
    void initialize() {
        SetWindowTextW(hwnd,L"選択範囲");dpi=GetDpiForWindow(hwnd);
        font=CreateFontW(-MulDiv(12,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Yu Gothic UI");
        const auto add=[&](int key,const wchar_t *cls,const wchar_t *label,DWORD style,int x,int y,int w,int h) {
            const auto child=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,label,WS_CHILD|WS_VISIBLE|style,
                MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(w,dpi,96),MulDiv(h,dpi,96),hwnd,HMENU(INT_PTR(key)),GetModuleHandleW(nullptr),nullptr);
            if(!child)throw std::runtime_error("Cannot create selection rectangle control");SendMessageW(child,WM_SETFONT,WPARAM(font),FALSE);return child;
        };
        add(IDOK,L"BUTTON",L"OK",WS_TABSTOP|BS_DEFPUSHBUTTON,226,312,88,28);
        add(IDCANCEL,L"BUTTON",L"キャンセル",WS_TABSTOP|BS_PUSHBUTTON,324,312,104,28);
        add(750,L"BUTTON",L"範囲の座標",BS_GROUPBOX,12,8,416,122);
        add(751,L"BUTTON",L"範囲のサイズ",BS_GROUPBOX,12,142,416,72);
        const wchar_t *labels[]{L"左(&L)",L"上(&T)",L"右(&R)",L"下(&B)",L"幅(&W)",L"高さ(&H)"};
        const auto r=rect();const int values[]{draft[0],draft[1],draft[2],draft[3],r.w,r.h};syncing=true;
        for(int n=0;n<6;++n) {
            const int x=n%2?230:24,y=n<4?36+(n/2)*46:170;
            add(752+n,L"STATIC",labels[n],SS_LEFT,x,y+5,62,22);
            const auto field=add(id::Left+n,L"EDIT",std::to_wstring(values[n]).c_str(),WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,x+66,y,112,26);
            SendMessageW(field,EM_SETLIMITTEXT,10,0);
            const bool vertical=n%2;const auto spin=add(id::Spin+n,UPDOWN_CLASSW,L"",UDS_ARROWKEYS|UDS_SETBUDDYINT|UDS_NOTHOUSANDS|(vertical?0:UDS_HORZ),x+152,y,26,26);
            SendMessageW(spin,UDM_SETBUDDY,WPARAM(field),0);const int low=n<4?0:1,high=n<4?(vertical?height:width)-1:vertical?height:width;
            SendMessageW(spin,UDM_SETRANGE32,vertical?high:low,vertical?low:high);SendMessageW(spin,UDM_SETPOS32,0,values[n]);
        }
        const auto label=L"画像のサイズ："+std::to_wstring(width)+L" × "+std::to_wstring(height)+L" ピクセル";
        add(id::ImageSize,L"STATIC",label.c_str(),SS_LEFT,24,228,390,22);
        add(id::Hint,L"STATIC",L"右・下は範囲に含む画素の座標です（0から開始）。",SS_LEFT,24,254,390,22);
        add(id::Error,L"STATIC",L"",SS_LEFT,12,280,416,26);
        syncing=false;initialized=true;changed(0);
        RECT bounds{0,0,MulDiv(440,dpi,96),MulDiv(352,dpi,96)};AdjustWindowRectExForDpi(&bounds,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,0,dpi);
        RECT parent{};GetWindowRect(GetParent(hwnd),&parent);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
        const int w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
        const int x=std::clamp((parent.left+parent.right-w)/2,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-w));
        const int y=std::clamp((parent.top+parent.bottom-h)/2,monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-h));
        SetWindowPos(hwnd,nullptr,x,y,w,h,SWP_NOZORDER);
    }
    static INT_PTR CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l) {
        auto *self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));
        if(msg==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,LONG_PTR(self));try{self->initialize();return TRUE;}catch(...){DestroyWindow(h);return FALSE;}}
        if(!self)return FALSE;
        if(msg==WM_COMMAND) {
            const int key=LOWORD(w);
            if(key==IDCANCEL){DestroyWindow(h);return TRUE;}
            if(key==IDOK){self->changed(0);if(self->valid){self->selected=self->rect();DestroyWindow(h);}return TRUE;}
            if(key>=id::Left&&key<=id::Height&&HIWORD(w)==EN_CHANGE){self->changed(key);return TRUE;}
        }
        if(msg==WM_CLOSE){DestroyWindow(h);return TRUE;}
        return FALSE;
    }
    ~Impl(){if(IsWindow(hwnd))DestroyWindow(hwnd);if(font)DeleteObject(font);}
};
SelectionRectDialog::SelectionRectDialog(uint32_t w,uint32_t h,Rect r):impl(std::make_unique<Impl>(w,h,r)){}
SelectionRectDialog::~SelectionRectDialog()=default;
HWND SelectionRectDialog::create(HWND owner){Template t;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,owner,Impl::proc,LPARAM(impl.get()));if(!h||!impl->initialized)throw std::runtime_error("Cannot create selection rectangle dialog");ShowWindow(h,SW_SHOW);return h;}
HWND SelectionRectDialog::window()const{return impl->hwnd;}
HWND SelectionRectDialog::control(int key)const{return impl->control(key);}
bool SelectionRectDialog::ready()const{return impl->initialized;}
std::optional<Rect> SelectionRectDialog::bounds()const{return impl->valid?std::optional(impl->rect()):std::nullopt;}
std::optional<Rect> SelectionRectDialog::result()const{return impl->selected;}
std::optional<Rect> SelectionRectDialog::run(HWND owner) {
    const bool enabled=IsWindowEnabled(owner);if(enabled)EnableWindow(owner,FALSE);
    struct Restore{HWND h;bool enabled;~Restore(){if(enabled){EnableWindow(h,TRUE);SetActiveWindow(h);}}} restore{owner,enabled};
    create(owner);MSG m{};while(IsWindow(window())){const auto r=GetMessageW(&m,nullptr,0,0);if(r<=0){if(!r)PostQuitMessage(int(m.wParam));break;}if(!IsDialogMessageW(window(),&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    return result();
}
std::optional<Rect> choose_selection_rect(HWND owner,uint32_t w,uint32_t h,Rect r){SelectionRectDialog dialog(w,h,r);return dialog.run(owner);}
}
