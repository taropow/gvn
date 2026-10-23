#include "gvn/brightness_dialog.h"
#include <commctrl.h>
#include <cmath>
#include <sstream>
namespace gvn {
namespace {
namespace id=brightness_control;
struct Template {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    Template(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;dialog.cx=330;dialog.cy=205;}
};
std::wstring gamma_text(int value){auto decimal=std::to_wstring(value%1000);return std::to_wstring(value/1000)+L"."+std::wstring(3-decimal.size(),L'0')+decimal;}
int number(HWND window,bool gamma) {
    wchar_t text[32]{};if(!GetWindowTextW(window,text,32))throw std::invalid_argument("Incomplete brightness number");std::wistringstream in(text);
    if(!gamma){int value{};if(!(in>>value)||!(in>>std::ws).eof())throw std::invalid_argument("Invalid brightness integer");return value;}
    for(auto at=text;*at;++at)if((*at<L'0'||*at>L'9')&&*at!=L'.')throw std::invalid_argument("Invalid gamma number");double value{};
    if(!(in>>value)||!(in>>std::ws).eof()||!std::isfinite(value)||value<.4||value>2.5||std::abs(value*1000.-std::round(value*1000.))>1e-7)throw std::invalid_argument("Gamma out of range");return int(std::lround(value*1000.));
}
int nearest_even(int numerator,int divisor){int value=numerator/divisor;const int remainder=numerator%divisor;if(remainder*2>divisor||(remainder*2==divisor&&(value&1)))++value;return value;}
int gamma_position(int value){return value>=1000?nearest_even(value-1000,10):-nearest_even(1000-value,4);}
}
struct BrightnessDialog::Impl {
    HWND hwnd{};HFONT font{};UINT dpi{96};bool modal{},initialized{},syncing{},pending{},input_valid{true},failed{},lost{};BrightnessChoice draft;Process process;Preview notify;Current current;
    std::shared_ptr<Image> candidate;std::optional<BrightnessAdjustment> candidate_adjustment;std::optional<BrightnessResult> selected;
    HWND control(int key) const{return GetDlgItem(hwnd,key);}bool context() const{return !lost&&(!current||current());}
    void withdraw() noexcept{try{if(notify)notify({});}catch(...) {}}
    void error(const wchar_t *text,bool invalid=false) {
        pending=false;failed=!invalid;input_valid=!invalid;KillTimer(hwnd,1);candidate.reset();candidate_adjustment.reset();withdraw();SetWindowTextW(control(id::Error),text);EnableWindow(control(IDOK),FALSE);
    }
    void read() {auto next=draft.adjustment;next.brightness=number(control(id::Brightness),false);next.contrast=number(control(id::Contrast),false);next.gamma=number(control(id::Gamma),true);if(!valid_brightness(next))throw std::invalid_argument("Brightness values out of range");draft.adjustment=next;}
    void positions() {
        const int values[]{draft.adjustment.brightness,draft.adjustment.contrast,gamma_position(draft.adjustment.gamma)};
        for(int n=0;n<3;++n){SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,n==2?-150:-255,n==2?150:255,0,values[n]};SetScrollInfo(control(id::ScrollBrightness+n),SB_CTL,&info,TRUE);}
    }
    void fill() {syncing=true;SetWindowTextW(control(id::Brightness),std::to_wstring(draft.adjustment.brightness).c_str());SetWindowTextW(control(id::Contrast),std::to_wstring(draft.adjustment.contrast).c_str());SetWindowTextW(control(id::Gamma),gamma_text(draft.adjustment.gamma).c_str());SendMessageW(control(id::Preview),BM_SETCHECK,draft.preview?BST_CHECKED:BST_UNCHECKED,0);positions();syncing=false;}
    bool compute() {
        pending=false;KillTimer(hwnd,1);if(!context()){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");return false;}if(!input_valid)return false;
        try {
            if(draft.adjustment.neutral()){candidate.reset();candidate_adjustment.reset();}
            else if(!candidate||candidate_adjustment!=draft.adjustment){auto next=process(draft.adjustment);if(!next||next->frames.size()!=1)throw std::runtime_error("Invalid brightness preview");candidate=std::move(next);candidate_adjustment=draft.adjustment;}
            if(!context()){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");return false;}
            failed=false;SetWindowTextW(control(id::Error),L"");EnableWindow(control(IDOK),TRUE);if(draft.preview&&candidate){if(notify)notify(candidate);}else withdraw();return true;
        }catch(...){error(L"プレビューを作成できません。画像と入力内容を確認してください。");return false;}
    }
    void schedule() {
        KillTimer(hwnd,1);pending=false;failed=false;SetWindowTextW(control(id::Error),L"");withdraw();
        if(!context()){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");return;}
        if(!input_valid)return;
        if(!draft.preview||draft.adjustment.neutral()){candidate.reset();candidate_adjustment.reset();EnableWindow(control(IDOK),TRUE);return;}
        pending=true;EnableWindow(control(IDOK),FALSE);if(!SetTimer(hwnd,1,30,nullptr))throw std::runtime_error("Cannot schedule brightness preview");
    }
    void changed(){if(!initialized||syncing)return;try{read();input_valid=true;positions();schedule();}catch(...){error(L"明るさ・コントラストは-255～255、ガンマは0.400～2.500を入力してください。",true);}}
    void scroll(int n,WPARAM w) {
        const int code=LOWORD(w);if(code==SB_ENDSCROLL)return;SCROLLINFO info{sizeof(info),SIF_POS|SIF_TRACKPOS};GetScrollInfo(control(id::ScrollBrightness+n),SB_CTL,&info);int value=info.nPos,low=n==2?-150:-255,high=n==2?150:255;
        if(code==SB_LINELEFT)--value;else if(code==SB_LINERIGHT)++value;else if(code==SB_PAGELEFT)value-=10;else if(code==SB_PAGERIGHT)value+=10;else if(code==SB_TOP)value=low;else if(code==SB_BOTTOM)value=high;else if(code==SB_THUMBTRACK)value=info.nTrackPos;else if(code==SB_THUMBPOSITION)value=short(HIWORD(w));else return;
        value=std::clamp(value,low,high);if(n==2)SetWindowTextW(control(id::Gamma),gamma_text(1000+value*(value<0?4:10)).c_str());else SetWindowTextW(control(id::Brightness+n),std::to_wstring(value).c_str());
    }
    void arrange() {
        const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT client{};GetClientRect(hwnd,&client);const int width=MulDiv(client.right,96,dpi),height=MulDiv(client.bottom,96,dpi);
        const auto move=[&](int key,int x,int y,int w,int h){MoveWindow(control(key),px(x),px(y),px(w),px(h),TRUE);};
        for(int n=0;n<3;++n){move(id::LabelBrightness+n,16,16+n*54,122,24);move(id::Brightness+n,144,12+n*54,72,26);move(id::ResetBrightness+n,224,12+n*54,64,26);move(id::ScrollBrightness+n,16,42+n*54,width-152,18);}
        move(id::Preview,width-122,18,110,26);move(id::Error,16,184,width-32,height-230);move(IDOK,width-196,height-40,82,28);move(IDCANCEL,width-104,height-40,88,28);
    }
    void set_font() {
        const auto next=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Yu Gothic UI");if(!next)throw std::runtime_error("Cannot create brightness font");
        for(int key:{id::Brightness,id::Contrast,id::Gamma,id::ResetBrightness,id::ResetContrast,id::ResetGamma,id::LabelBrightness,id::LabelContrast,id::LabelGamma,id::Preview,id::Error,IDOK,IDCANCEL})SendMessageW(control(key),WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(font)DeleteObject(font);font=next;
    }
    void finish(bool accept) {if(accept&&!compute())return;if(accept)selected=BrightnessResult{draft,candidate};withdraw();KillTimer(hwnd,1);KillTimer(hwnd,2);if(modal)EndDialog(hwnd,accept?IDOK:IDCANCEL);else DestroyWindow(hwnd);}
    INT_PTR message(UINT m,WPARAM w,LPARAM l) {
        if(m==WM_INITDIALOG) {
            SetWindowTextW(hwnd,L"明るさ調整");dpi=GetDpiForWindow(hwnd);
            const auto make=[&](int key,const wchar_t *cls,const wchar_t *text,DWORD style){const auto h=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,reinterpret_cast<HMENU>(INT_PTR(key)),GetModuleHandleW(nullptr),nullptr);if(!h)throw std::runtime_error("Cannot create brightness control");return h;};
            for(int n=0;n<3;++n){make(id::LabelBrightness+n,L"STATIC",n==0?L"明るさ(&B)":n==1?L"コントラスト(&C)":L"ガンマ(&G)",0);const auto h=make(id::Brightness+n,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP);SendMessageW(h,EM_SETLIMITTEXT,12,0);make(id::ResetBrightness+n,L"BUTTON",n==2?L"<< 1":L"<< 0",WS_TABSTOP);make(id::ScrollBrightness+n,L"SCROLLBAR",L"",SBS_HORZ|WS_TABSTOP);}
            make(id::Preview,L"BUTTON",L"プレビュー(&P)",BS_AUTOCHECKBOX|WS_TABSTOP);make(id::Error,L"STATIC",L"",0);make(IDOK,L"BUTTON",L"OK",BS_DEFPUSHBUTTON|WS_TABSTOP);make(IDCANCEL,L"BUTTON",L"キャンセル",WS_TABSTOP);set_font();fill();
            const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT client{0,0,px(460),px(282)};AdjustWindowRectExForDpi(&client,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);RECT owner{};GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);const auto placed=place_window({owner.left+(owner.right-owner.left-client.right+client.left)/2,owner.top+(owner.bottom-owner.top-client.bottom+client.top)/2,client.right-client.left,client.bottom-client.top},{monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top},false);SetWindowPos(hwnd,nullptr,placed.x,placed.y,placed.w,placed.h,SWP_NOZORDER|SWP_NOACTIVATE);arrange();initialized=true;schedule();if(!SetTimer(hwnd,2,80,nullptr))throw std::runtime_error("Cannot monitor brightness context");SetFocus(control(id::Brightness));return FALSE;
        }
        if(m==WM_COMMAND&&initialized){const auto key=LOWORD(w);if(key==IDOK||key==IDCANCEL){finish(key==IDOK);return TRUE;}if(key>=id::Brightness&&key<=id::Gamma&&HIWORD(w)==EN_CHANGE){changed();return TRUE;}if(key>=id::ResetBrightness&&key<=id::ResetGamma){SetWindowTextW(control(id::Brightness+key-id::ResetBrightness),key==id::ResetGamma?L"1.000":L"0");return TRUE;}if(key==id::Preview){draft.preview=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;schedule();return TRUE;}}
        if(m==WM_HSCROLL&&initialized){for(int n=0;n<3;++n)if(reinterpret_cast<HWND>(l)==control(id::ScrollBrightness+n)){scroll(n,w);return TRUE;}}
        if(m==WM_TIMER){if(w==1)compute();else if(w==2&&!context()&&!lost){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");}return TRUE;}
        if(m==WM_DPICHANGED){dpi=HIWORD(w);const auto rect=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);set_font();arrange();return TRUE;}
        if(m==WM_CLOSE){finish(false);return TRUE;}if(m==WM_DESTROY){KillTimer(hwnd,1);KillTimer(hwnd,2);withdraw();candidate.reset();initialized=false;return TRUE;}return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));if(m==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}if(!self)return FALSE;try{return self->message(m,w,l);}catch(...){self->error(L"明るさ調整を実行できません。");if(!self->initialized)PostMessageW(h,WM_CLOSE,0,0);return TRUE;}}
};
BrightnessDialog::BrightnessDialog(BrightnessChoice value,Process process,Preview preview,Current current):impl(std::make_unique<Impl>()){if(!valid_brightness(value.adjustment)||!process)throw std::invalid_argument("Invalid brightness dialog source");impl->draft=value;impl->process=std::move(process);impl->notify=std::move(preview);impl->current=std::move(current);}
BrightnessDialog::~BrightnessDialog(){if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);if(impl->font)DeleteObject(impl->font);}
std::optional<BrightnessResult> BrightnessDialog::show(HWND owner){Template value;impl->modal=true;if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)throw std::runtime_error("Cannot open brightness dialog");return impl->selected;}
HWND BrightnessDialog::create(HWND owner){Template value;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create brightness dialog");return h;}
HWND BrightnessDialog::window() const{return impl->hwnd;}HWND BrightnessDialog::control(int key) const{return impl->control(key);}
BrightnessChoice BrightnessDialog::options() const{return impl->draft;}bool BrightnessDialog::ready() const{return impl->initialized&&!impl->pending;}bool BrightnessDialog::valid() const{return impl->context()&&impl->input_valid&&!impl->failed;}
std::shared_ptr<const Image> BrightnessDialog::preview_image() const{return impl->draft.preview?impl->candidate:nullptr;}std::optional<BrightnessResult> BrightnessDialog::result() const{return impl->selected;}
std::optional<BrightnessResult> choose_brightness(HWND owner,BrightnessChoice choice,const BrightnessDialog::Process &process,const BrightnessDialog::Preview &preview,const BrightnessDialog::Current &current){BrightnessDialog dialog(choice,process,preview,current);return dialog.show(owner);}
} // namespace gvn
