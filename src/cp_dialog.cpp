#include "gvn/ui_theme.h"
#include "gvn/cp_dialog.h"
#include "gvn/masks.h"
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace gvn {
namespace {
namespace id=cp_control;
struct DialogTemplate {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    DialogTemplate(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;dialog.cx=280;dialog.cy=170;}
};
std::wstring filtered(std::wstring_view input) {
    std::wstring result;
    for(const auto c:input)if((c>=L'A'&&c<=L'Z')||(c>=L'a'&&c<=L'z')){result.push_back(c);if(result.size()==16)break;}
    return result;
}
std::wstring error_text(const char *text) {
    const auto length=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
    std::wstring result(static_cast<size_t>(std::max(1,length)),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text,-1,result.data(),length);result.pop_back();return result;
}
}
struct CpDialog::Impl {
    HWND hwnd{};HFONT font{};UINT dpi{96};
    bool modal{},initialized{},pending{},failed{},lost{},filtering{};
    CpChoice draft;Process process;Preview notify;Current current;
    std::shared_ptr<Image> candidate;std::wstring candidate_code;
    std::optional<CpResult> selected;
    HWND control(int key) const{return GetDlgItem(hwnd,key);}
    bool context() const{return !lost&&(!current||current());}
    void withdraw() noexcept {try{if(notify)notify({});}catch(...) {}}
    void fail(const char *message) noexcept {
        pending=false;failed=true;KillTimer(hwnd,1);candidate.reset();candidate_code.clear();withdraw();
        EnableWindow(control(IDOK),FALSE);
        try{SetWindowTextW(control(id::Error),error_text(message).c_str());}
        catch(...){SetWindowTextW(control(id::Error),L"プレビューを作成できません。");}
    }
    void layout() {
        const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT r{};GetClientRect(hwnd,&r);
        const int width=MulDiv(r.right,96,dpi),height=MulDiv(r.bottom,96,dpi);
        const auto move=[&](int key,int x,int y,int w,int h){MoveWindow(control(key),px(x),px(y),px(w),px(h),TRUE);};
        move(243,18,14,width-36,24);move(id::Code,18,40,width-36,28);move(244,18,76,width-36,24);
        move(id::Preview,18,110,width-36,26);move(id::Error,18,146,width-36,std::max(36,height-194));
        move(IDOK,width-196,height-40,82,28);move(IDCANCEL,width-104,height-40,88,28);
    }
    void set_font() {
        LOGFONTW value{};value.lfHeight=-MulDiv(9,dpi,72);wcscpy_s(value.lfFaceName,L"Yu Gothic UI");
        const auto next=CreateFontIndirectW(&value);if(!next)throw std::runtime_error("Cannot create CP dialog font");
        for(const int key:{243,id::Code,244,id::Preview,id::Error,IDOK,IDCANCEL})SendMessageW(control(key),WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(font)DeleteObject(font);font=next;
    }
    bool compute() {
        if(!context()){lost=true;fail("Displayed image changed; cancel this dialog and reopen it");return false;}
        pending=false;KillTimer(hwnd,1);
        try {
            const auto code=normalize_cp_code(draft.code);
            if(code.empty()){candidate.reset();candidate_code.clear();withdraw();}
            else if(!candidate||candidate_code!=code) {
                auto next=process(code);if(!next||next->frames.size()!=1)throw std::runtime_error("Cannot create CP preview");
                candidate=std::move(next);candidate_code=code;
            }
            if(!context()){lost=true;fail("Displayed image changed; cancel this dialog and reopen it");return false;}
            failed=false;SetWindowTextW(control(id::Error),L"");EnableWindow(control(IDOK),TRUE);
            if(draft.preview&&notify&&candidate)notify(candidate);
            return true;
        }catch(const std::exception &e){fail(e.what());return false;}catch(...){fail("Cannot create CP preview");return false;}
    }
    void schedule() {
        KillTimer(hwnd,1);failed=false;SetWindowTextW(control(id::Error),L"");
        if(!context()){lost=true;fail("Displayed image changed; cancel this dialog and reopen it");return;}
        if(!draft.preview||draft.code.empty()) {
            pending=false;candidate.reset();candidate_code.clear();withdraw();EnableWindow(control(IDOK),TRUE);return;
        }
        pending=true;EnableWindow(control(IDOK),FALSE);
        if(!SetTimer(hwnd,1,30,nullptr))throw std::runtime_error("Cannot schedule CP preview");
    }
    void changed() {
        if(filtering)return;
        const auto length=std::min(30000,GetWindowTextLengthW(control(id::Code)));
        std::wstring input(static_cast<size_t>(length)+1,L'\0');GetWindowTextW(control(id::Code),input.data(),length+1);input.pop_back();
        auto next=filtered(input);
        if(next!=input) {
            DWORD start{},end{};SendMessageW(control(id::Code),EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
            const auto first=filtered(std::wstring_view(input).substr(0,std::min<size_t>(start,input.size()))).size();
            const auto last=filtered(std::wstring_view(input).substr(0,std::min<size_t>(end,input.size()))).size();
            filtering=true;SetWindowTextW(control(id::Code),next.c_str());SendMessageW(control(id::Code),EM_SETSEL,first,last);filtering=false;
        }
        draft.code=std::move(next);schedule();
    }
    void finish(bool accept) {
        if(accept&&!compute())return;
        if(accept)selected=CpResult{{normalize_cp_code(draft.code),draft.preview},candidate};
        withdraw();KillTimer(hwnd,1);KillTimer(hwnd,2);if(modal)EndDialog(hwnd,accept?IDOK:IDCANCEL);else DestroyWindow(hwnd);
    }
    INT_PTR message(UINT m,WPARAM w,LPARAM l) {
        if(m==WM_INITDIALOG) {
            SetWindowTextW(hwnd,L"CPマスク");dpi=GetDpiForWindow(hwnd);
            const auto make=[&](const wchar_t *cls,const wchar_t *text,DWORD style,int key,DWORD extended=0) {
                if(!CreateWindowExW(extended,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(key)),GetModuleHandleW(nullptr),nullptr))throw std::runtime_error("Cannot create CP dialog control");
            };
            make(L"STATIC",L"CPコード(&C)",0,243);make(L"EDIT",draft.code.c_str(),WS_TABSTOP|ES_AUTOHSCROLL,id::Code,WS_EX_CLIENTEDGE);
            SendMessageW(control(id::Code),EM_SETLIMITTEXT,30000,0);make(L"STATIC",L"半角英字・最大16文字",0,244);
            make(L"BUTTON",L"プレビュー(&P)",WS_TABSTOP|BS_AUTOCHECKBOX,id::Preview);SendMessageW(control(id::Preview),BM_SETCHECK,draft.preview?BST_CHECKED:BST_UNCHECKED,0);
            make(L"STATIC",L"",0,id::Error);make(L"BUTTON",L"OK",WS_GROUP|WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK);make(L"BUTTON",L"キャンセル",WS_TABSTOP,IDCANCEL);set_font();
            const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT client{0,0,px(380),px(250)};
            AdjustWindowRectExForDpi(&client,static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);
            RECT owner{};GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi);
            const auto placed=place_window({owner.left+(owner.right-owner.left-client.right+client.left)/2,owner.top+(owner.bottom-owner.top-client.bottom+client.top)/2,client.right-client.left,client.bottom-client.top},{mi.rcWork.left,mi.rcWork.top,mi.rcWork.right-mi.rcWork.left,mi.rcWork.bottom-mi.rcWork.top},false);
            SetWindowPos(hwnd,nullptr,placed.x,placed.y,placed.w,placed.h,SWP_NOZORDER|SWP_NOACTIVATE);
            initialized=true;layout();schedule();SetTimer(hwnd,2,80,nullptr);SetFocus(control(id::Code));SendMessageW(control(id::Code),EM_SETSEL,0,-1);return FALSE;
        }
        if(m==WM_COMMAND&&initialized) {
            const auto key=LOWORD(w);
            if(key==IDOK||key==IDCANCEL){finish(key==IDOK);return TRUE;}
            if(key==id::Code&&HIWORD(w)==EN_CHANGE){changed();return TRUE;}
            if(key==id::Preview&&HIWORD(w)==BN_CLICKED){draft.preview=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;schedule();return TRUE;}
        }
        if(m==WM_TIMER){if(w==1)compute();else if(w==2&&!context()&&!lost){lost=true;fail("Displayed image changed; cancel this dialog and reopen it");}return TRUE;}
        if(m==WM_DPICHANGED){dpi=HIWORD(w);const auto r=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);set_font();layout();return TRUE;}
        if(m==WM_CLOSE){finish(false);return TRUE;}
        if(m==WM_DESTROY){KillTimer(hwnd,1);KillTimer(hwnd,2);withdraw();candidate.reset();initialized=false;return TRUE;}
        return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));if(m==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}if(!self)return FALSE;
        try{const auto result=self->message(m,w,l);if(m==WM_INITDIALOG||m==WM_DPICHANGED)apply_ui_theme(h);return result;}catch(const std::exception &e){self->fail(e.what());return TRUE;}catch(...){self->fail("Cannot open CP dialog");return TRUE;}
    }
};
CpDialog::CpDialog(CpChoice initial,Process process,Preview preview,Current current):impl(std::make_unique<Impl>()) {
    initial.code=normalize_cp_code(initial.code);impl->draft=std::move(initial);impl->process=std::move(process);impl->notify=std::move(preview);impl->current=std::move(current);
    if(!impl->process)throw std::invalid_argument("CP dialog needs a processor");
}
CpDialog::~CpDialog(){if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);if(impl->font)DeleteObject(impl->font);}
std::optional<CpResult> CpDialog::show(HWND owner){DialogTemplate value;impl->modal=true;if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)throw std::runtime_error("Cannot open CP dialog");return impl->selected;}
HWND CpDialog::create(HWND owner){DialogTemplate value;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create CP dialog");return h;}
HWND CpDialog::window() const{return impl->hwnd;}
HWND CpDialog::control(int key) const{return impl->control(key);}
CpChoice CpDialog::options() const{return impl->draft;}
bool CpDialog::ready() const{return impl->initialized&&!impl->pending;}
bool CpDialog::valid() const{return impl->context()&&!impl->failed;}
std::shared_ptr<const Image> CpDialog::preview_image() const{return impl->draft.preview?impl->candidate:nullptr;}
std::optional<CpResult> CpDialog::result() const{return impl->selected;}
std::optional<CpResult> choose_cp(HWND owner,CpChoice choice,const CpDialog::Process &process,const CpDialog::Preview &preview,const CpDialog::Current &current){CpDialog dialog(std::move(choice),process,preview,current);return dialog.show(owner);}
}
