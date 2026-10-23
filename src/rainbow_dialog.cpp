#include "gvn/rainbow_dialog.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>
namespace gvn {
namespace {
constexpr int sizes[]{1,2,4,8,16,32};
namespace id=rainbow_control;
struct DialogTemplate {
    DLGTEMPLATE dialog{};
    WORD menu{},cls{},title{},points{9};
    wchar_t face[13]{L"Yu Gothic UI"};
    DialogTemplate() {
        dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;
        dialog.cx=250;dialog.cy=180;
    }
};
std::wstring wide(const char *value) {
    const auto length=MultiByteToWideChar(CP_UTF8,0,value,-1,nullptr,0);
    std::wstring result(static_cast<size_t>(std::max(1,length)),L'\0');
    MultiByteToWideChar(CP_UTF8,0,value,-1,result.data(),length);result.pop_back();return result;
}
}
struct RainbowDialog::Impl {
    HWND hwnd{};
    HFONT font{};
    UINT dpi{96};
    bool modal{},initialized{},pending{},failed{},lost{};
    RainbowChoice draft;
    Process process;
    Preview notify;
    Current current;
    std::shared_ptr<Image> candidate;
    int candidate_size{};
    std::optional<RainbowResult> selected;
    HWND control(int key) const {return GetDlgItem(hwnd,key);}
    bool context() const {return !lost&&(!current||current());}
    void withdraw() noexcept {try {if(notify)notify({});}catch(...) {}}
    void fail(const char *message) noexcept {
        pending=false;failed=true;KillTimer(hwnd,1);candidate.reset();candidate_size=0;withdraw();
        EnableWindow(control(IDOK),FALSE);
        try {SetWindowTextW(control(id::Error),wide(message).c_str());}
        catch(...) {SetWindowTextW(control(id::Error),L"プレビューを作成できません。");}
    }
    void layout() {
        const auto px=[&](int n){return MulDiv(n,dpi,96);};
        RECT r{};GetClientRect(hwnd,&r);const auto width=MulDiv(r.right,96,dpi),height=MulDiv(r.bottom,96,dpi);
        const auto move=[&](int key,int x,int y,int w,int h){MoveWindow(control(key),px(x),px(y),px(w),px(h),TRUE);};
        move(230,12,10,width-24,132);
        for(int i=0;i<6;++i)move(id::SizeBase+i,28+(i%2)*(width-56)/2,32+(i/2)*32,(width-60)/2,26);
        move(id::Preview,20,154,width-40,26);move(id::Error,20,188,width-40,std::max(30,height-236));
        move(IDOK,width-196,height-40,82,28);move(IDCANCEL,width-104,height-40,88,28);
    }
    void set_font() {
        LOGFONTW value{};value.lfHeight=-MulDiv(9,dpi,72);wcscpy_s(value.lfFaceName,L"Yu Gothic UI");
        const auto next=CreateFontIndirectW(&value);if(!next)throw std::runtime_error("Cannot create Rainbow dialog font");
        for(const int key:{230,id::SizeBase,id::SizeBase+1,id::SizeBase+2,id::SizeBase+3,id::SizeBase+4,id::SizeBase+5,id::Preview,id::Error,IDOK,IDCANCEL})
            SendMessageW(control(key),WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(font)DeleteObject(font);font=next;
    }
    bool compute() {
        if(!context()){lost=true;fail("Displayed image changed; cancel this dialog and reopen it");return false;}
        pending=false;KillTimer(hwnd,1);
        try {
            if(!candidate||candidate_size!=draft.size) {
                auto next=process(draft.size);
                if(!next||next->frames.size()!=1)throw std::runtime_error("Cannot create Rainbow preview");
                candidate=std::move(next);candidate_size=draft.size;
            }
            if(!context()){lost=true;fail("Displayed image changed; cancel this dialog and reopen it");return false;}
            failed=false;SetWindowTextW(control(id::Error),L"");EnableWindow(control(IDOK),TRUE);
            if(draft.preview&&notify)notify(candidate);
            return true;
        }catch(const std::exception &error){fail(error.what());return false;}
        catch(...){fail("Cannot create Rainbow preview");return false;}
    }
    void schedule() {
        KillTimer(hwnd,1);failed=false;SetWindowTextW(control(id::Error),L"");
        if(!context()){lost=true;fail("Displayed image changed; cancel this dialog and reopen it");return;}
        if(!draft.preview) {
            pending=false;candidate.reset();candidate_size=0;withdraw();EnableWindow(control(IDOK),TRUE);return;
        }
        pending=true;EnableWindow(control(IDOK),FALSE);
        if(!SetTimer(hwnd,1,30,nullptr))throw std::runtime_error("Cannot schedule Rainbow preview");
    }
    void finish(bool accept) {
        if(accept&&!compute())return;
        if(accept)selected=RainbowResult{draft,candidate};
        withdraw();KillTimer(hwnd,1);KillTimer(hwnd,2);
        if(modal)EndDialog(hwnd,accept?IDOK:IDCANCEL);else DestroyWindow(hwnd);
    }
    INT_PTR message(UINT m,WPARAM w,LPARAM l) {
        if(m==WM_INITDIALOG) {
            SetWindowTextW(hwnd,L"虹色マスク");dpi=GetDpiForWindow(hwnd);
            const auto make=[&](const wchar_t *cls,const wchar_t *text,DWORD style,int key) {
                if(!CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(key)),GetModuleHandleW(nullptr),nullptr))
                    throw std::runtime_error("Cannot create Rainbow dialog control");
            };
            make(L"BUTTON",L"ブロックサイズ",BS_GROUPBOX,230);
            const wchar_t *labels[]{L"&1 × 1",L"&2 × 2",L"&4 × 4",L"&8 × 8",L"1&6 × 16",L"&32 × 32"};
            for(int i=0;i<6;++i) {
                make(L"BUTTON",labels[i],WS_TABSTOP|BS_AUTORADIOBUTTON|(i==0?WS_GROUP:0),id::SizeBase+i);
                SendMessageW(control(id::SizeBase+i),BM_SETCHECK,draft.size==sizes[i]?BST_CHECKED:BST_UNCHECKED,0);
            }
            make(L"BUTTON",L"プレビュー(&P)",WS_GROUP|WS_TABSTOP|BS_AUTOCHECKBOX,id::Preview);
            SendMessageW(control(id::Preview),BM_SETCHECK,draft.preview?BST_CHECKED:BST_UNCHECKED,0);
            make(L"STATIC",L"",0,id::Error);
            make(L"BUTTON",L"OK",WS_GROUP|WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK);make(L"BUTTON",L"キャンセル",WS_TABSTOP,IDCANCEL);
            set_font();
            const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT client{0,0,px(340),px(270)};
            AdjustWindowRectExForDpi(&client,static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);
            RECT owner{};GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner);MONITORINFO mi{sizeof(mi)};
            GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi);
            const auto placed=place_window({owner.left+(owner.right-owner.left-client.right+client.left)/2,owner.top+(owner.bottom-owner.top-client.bottom+client.top)/2,client.right-client.left,client.bottom-client.top},
                {mi.rcWork.left,mi.rcWork.top,mi.rcWork.right-mi.rcWork.left,mi.rcWork.bottom-mi.rcWork.top},false);
            SetWindowPos(hwnd,nullptr,placed.x,placed.y,placed.w,placed.h,SWP_NOZORDER|SWP_NOACTIVATE);
            initialized=true;layout();schedule();SetTimer(hwnd,2,80,nullptr);
            const auto selected_size=static_cast<int>(std::find(std::begin(sizes),std::end(sizes),draft.size)-std::begin(sizes));
            SetFocus(control(id::SizeBase+selected_size));return FALSE;
        }
        if(m==WM_COMMAND&&initialized) {
            const auto key=LOWORD(w);
            if(key==IDOK||key==IDCANCEL){finish(key==IDOK);return TRUE;}
            if(key>=id::SizeBase&&key<id::SizeBase+6&&HIWORD(w)==BN_CLICKED) {
                draft.size=sizes[key-id::SizeBase];CheckRadioButton(hwnd,id::SizeBase,id::SizeBase+5,key);schedule();return TRUE;
            }
            if(key==id::Preview&&HIWORD(w)==BN_CLICKED){draft.preview=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;schedule();return TRUE;}
        }
        if(m==WM_TIMER) {
            if(w==1)compute();
            else if(w==2&&!context()&&!lost){lost=true;fail("Displayed image changed; cancel this dialog and reopen it");}
            return TRUE;
        }
        if(m==WM_DPICHANGED) {
            dpi=HIWORD(w);const auto r=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);set_font();layout();return TRUE;
        }
        if(m==WM_CLOSE){finish(false);return TRUE;}
        if(m==WM_DESTROY){KillTimer(hwnd,1);KillTimer(hwnd,2);withdraw();candidate.reset();initialized=false;return TRUE;}
        return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));
        if(m==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}
        if(!self)return FALSE;
        try{return self->message(m,w,l);}
        catch(const std::exception &e){self->fail(e.what());return TRUE;}
        catch(...){self->fail("Cannot open Rainbow dialog");return TRUE;}
    }
};
RainbowDialog::RainbowDialog(RainbowChoice value,Process process,Preview preview,Current current):impl(std::make_unique<Impl>()) {
    if(std::find(std::begin(sizes),std::end(sizes),value.size)==std::end(sizes))value.size=8;
    impl->draft=value;impl->process=std::move(process);impl->notify=std::move(preview);impl->current=std::move(current);
    if(!impl->process)throw std::invalid_argument("Rainbow dialog needs a processor");
}
RainbowDialog::~RainbowDialog(){if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);if(impl->font)DeleteObject(impl->font);}
std::optional<RainbowResult> RainbowDialog::show(HWND owner) {
    DialogTemplate value;impl->modal=true;
    if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)throw std::runtime_error("Cannot open Rainbow dialog");
    return impl->selected;
}
HWND RainbowDialog::create(HWND owner){DialogTemplate value;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create Rainbow dialog");return h;}
HWND RainbowDialog::window() const{return impl->hwnd;}
HWND RainbowDialog::control(int key) const{return impl->control(key);}
RainbowChoice RainbowDialog::options() const{return impl->draft;}
bool RainbowDialog::ready() const{return impl->initialized&&!impl->pending;}
bool RainbowDialog::valid() const{return impl->context()&&!impl->failed;}
std::shared_ptr<const Image> RainbowDialog::preview_image() const{return impl->draft.preview?impl->candidate:nullptr;}
std::optional<RainbowResult> RainbowDialog::result() const{return impl->selected;}
std::optional<RainbowResult> choose_rainbow(HWND owner,RainbowChoice initial,const RainbowDialog::Process &process,const RainbowDialog::Preview &preview,const RainbowDialog::Current &current) {
    RainbowDialog dialog(initial,process,preview,current);return dialog.show(owner);
}
} // namespace gvn
