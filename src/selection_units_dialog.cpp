#include "gvn/ui_theme.h"
#include "gvn/selection_units_dialog.h"
#include <commctrl.h>
namespace gvn {
namespace {
namespace id=selection_control;
struct Template {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    Template(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;dialog.cx=280;dialog.cy=220;}
};
int number(HWND field) {
    wchar_t value[16]{};const int length=GetWindowTextW(field,value,16);
    if(!length||length>3)throw std::invalid_argument("Invalid selection unit");
    for(int n=0;n<length;++n)if(value[n]<L'0'||value[n]>L'9')throw std::invalid_argument("Invalid selection unit");
    const int n=_wtoi(value);if(n<1||n>999)throw std::invalid_argument("Invalid selection unit");return n;
}
}
struct SelectionUnitsDialog::Impl {
    SelectionUnits draft;std::optional<SelectionUnits> selected;HWND hwnd{};HFONT font{};UINT dpi{96};bool initialized{},syncing{},valid{};
    explicit Impl(SelectionUnits u):draft(u){if(!valid_selection_units(u))throw std::invalid_argument("Invalid selection units");}
    HWND control(int key) const{return GetDlgItem(hwnd,key);}
    void check() {
        if(syncing)return;
        try {draft={number(control(id::StartX)),number(control(id::StartY)),number(control(id::SizeX)),number(control(id::SizeY))};valid=true;SetWindowTextW(control(id::Error),L"");}
        catch(...){valid=false;SetWindowTextW(control(id::Error),L"各単位は1〜999ピクセルで指定してください。");}
        EnableWindow(control(IDOK),valid);
    }
    void initialize() {
        SetWindowTextW(hwnd,L"選択範囲の単位");dpi=GetDpiForWindow(hwnd);
        font=CreateFontW(-MulDiv(12,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Yu Gothic UI");
        const auto add=[&](int key,const wchar_t *cls,const wchar_t *label,DWORD style,int x,int y,int w,int h) {
            const auto child=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,label,WS_CHILD|WS_VISIBLE|style,
                MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(w,dpi,96),MulDiv(h,dpi,96),hwnd,HMENU(INT_PTR(key)),GetModuleHandleW(nullptr),nullptr);
            if(!child)throw std::runtime_error("Cannot create selection unit control");SendMessageW(child,WM_SETFONT,WPARAM(font),FALSE);return child;
        };
        add(IDOK,L"BUTTON",L"OK",WS_TABSTOP|BS_DEFPUSHBUTTON,206,306,88,28);
        add(IDCANCEL,L"BUTTON",L"キャンセル",WS_TABSTOP|BS_PUSHBUTTON,304,306,104,28);
        add(700,L"BUTTON",L"始点の単位",BS_GROUPBOX,12,8,396,120);
        add(701,L"BUTTON",L"サイズの単位",BS_GROUPBOX,12,140,396,120);
        const wchar_t *labels[]{L"水平(&H)",L"垂直(&V)",L"水平(&Z)",L"垂直(&T)"};
        const int values[]{draft.start_x,draft.start_y,draft.size_x,draft.size_y};
        syncing=true;
        for(int n=0;n<4;++n) {
            const int y=36+(n/2)*132+(n%2)*42;
            add(702+n,L"STATIC",labels[n],SS_LEFT,24,y+5,76,22);
            const auto field=add(id::StartX+n,L"EDIT",std::to_wstring(values[n]).c_str(),WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,104,y,112,26);
            SendMessageW(field,EM_SETLIMITTEXT,3,0);
            const auto spin=add(id::Spin+n,UPDOWN_CLASSW,L"",UDS_ALIGNRIGHT|UDS_ARROWKEYS|UDS_SETBUDDYINT,200,y,16,26);
            SendMessageW(spin,UDM_SETBUDDY,WPARAM(field),0);SendMessageW(spin,UDM_SETRANGE32,1,999);SendMessageW(spin,UDM_SETPOS32,0,values[n]);
            for(int k=0;k<3;++k)add(id::Quick+n*3+k,L"BUTTON",k==0?L"1":k==1?L"8":L"16",WS_TABSTOP|BS_PUSHBUTTON,234+k*52,y,46,26);
        }
        add(id::Error,L"STATIC",L"",SS_LEFT,12,270,396,28);syncing=false;check();
        RECT r{0,0,MulDiv(420,dpi,96),MulDiv(346,dpi,96)};AdjustWindowRectExForDpi(&r,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,0,dpi);
        RECT parent{};GetWindowRect(GetParent(hwnd),&parent);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
        const int w=r.right-r.left,h=r.bottom-r.top;
        const int x=std::clamp((parent.left+parent.right-w)/2,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-w));
        const int y=std::clamp((parent.top+parent.bottom-h)/2,monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-h));
        SetWindowPos(hwnd,nullptr,x,y,w,h,SWP_NOZORDER);initialized=true;
    }
    static INT_PTR CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l) {
        auto *self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));
        if(msg==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,LONG_PTR(self));try{self->initialize();apply_ui_theme(h);return TRUE;}catch(...){DestroyWindow(h);return FALSE;}}
        if(!self)return FALSE;
        if(msg==WM_COMMAND) {
            const int key=LOWORD(w);
            if(key==IDCANCEL){DestroyWindow(h);return TRUE;}
            if(key==IDOK){self->check();if(self->valid){self->selected=self->draft;DestroyWindow(h);}return TRUE;}
            if(key>=id::Quick&&key<id::Quick+12&&HIWORD(w)==BN_CLICKED) {
                const int n=(key-id::Quick)/3,k=(key-id::Quick)%3;const auto field=self->control(id::StartX+n);
                SetWindowTextW(field,k==0?L"1":k==1?L"8":L"16");SetFocus(field);SendMessageW(field,EM_SETSEL,0,-1);return TRUE;
            }
            if(key>=id::StartX&&key<=id::SizeY&&HIWORD(w)==EN_CHANGE){self->check();return TRUE;}
        }
        if(msg==WM_CLOSE){DestroyWindow(h);return TRUE;}
        return FALSE;
    }
    ~Impl(){if(IsWindow(hwnd))DestroyWindow(hwnd);if(font)DeleteObject(font);}
};
SelectionUnitsDialog::SelectionUnitsDialog(SelectionUnits u):impl(std::make_unique<Impl>(u)){}
SelectionUnitsDialog::~SelectionUnitsDialog()=default;
HWND SelectionUnitsDialog::create(HWND owner){Template t;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,owner,Impl::proc,LPARAM(impl.get()));if(!h||!impl->initialized)throw std::runtime_error("Cannot create selection unit dialog");ShowWindow(h,SW_SHOW);return h;}
HWND SelectionUnitsDialog::window()const{return impl->hwnd;}
HWND SelectionUnitsDialog::control(int key)const{return impl->control(key);}
bool SelectionUnitsDialog::ready()const{return impl->initialized;}
std::optional<SelectionUnits> SelectionUnitsDialog::result()const{return impl->selected;}
std::optional<SelectionUnits> SelectionUnitsDialog::run(HWND owner) {
    const bool enabled=IsWindowEnabled(owner);if(enabled)EnableWindow(owner,FALSE);
    struct Restore{HWND h;bool enabled;~Restore(){if(enabled){EnableWindow(h,TRUE);SetActiveWindow(h);}}} restore{owner,enabled};
    create(owner);MSG m{};
    while(IsWindow(window())){const auto r=GetMessageW(&m,nullptr,0,0);if(r<=0){if(!r)PostQuitMessage(int(m.wParam));break;}if(!IsDialogMessageW(window(),&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    return result();
}
std::optional<SelectionUnits> choose_selection_units(HWND owner,SelectionUnits u){SelectionUnitsDialog dialog(u);return dialog.run(owner);}
}
