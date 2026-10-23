#include "gvn/image_info_dialog.h"
#include <commctrl.h>
#include <mutex>
#include <thread>
namespace gvn {
std::wstring ImageInfoData::text() const {std::wstring result;for(const auto &[name,value]:fields)result+=name+L": "+value+L"\n";return result;}
namespace {
namespace id=image_info_control;
struct DialogTemplate {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    DialogTemplate(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|DS_MODALFRAME|DS_SETFONT;dialog.cx=400;dialog.cy=300;}
};
std::wstring text_of(HWND window){std::wstring value(size_t(GetWindowTextLengthW(window))+1,L'\0');GetWindowTextW(window,value.data(),static_cast<int>(value.size()));value.pop_back();return value;}
LRESULT CALLBACK field_keys(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    if(m==WM_KEYDOWN&&w=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(h,EM_SETSEL,0,-1);return 0;}return DefSubclassProc(h,m,w,l);
}
}
struct ImageInfoDialog::Impl {
    ImageInfoData data;bool automatic{},modal{},initialized{},pending{};Counter counter;HWND hwnd{};HFONT font{};UINT dpi{96};
    struct Result {std::mutex mutex;bool done{},failed{};std::optional<uint64_t> count;};
    std::shared_ptr<Result> result;std::optional<uint64_t> count;std::jthread worker;
    HWND control(int key)const{return GetDlgItem(hwnd,key);}
    bool enabled()const{return data.image&&!data.image->animated()&&!data.image->frames.empty();}
    void start() {
        if(!enabled()||!automatic||pending||count)return;
        if(!counter)throw std::runtime_error("Missing color counter");
        SetWindowTextW(control(id::Error),L"");SetWindowTextW(control(id::Colors),L"計算中…");
        result=std::make_shared<Result>();const auto target=result;const auto function=counter;
        worker=std::jthread([target,function](std::stop_token stop){
            std::optional<uint64_t> value;bool failed{};try{value=function(stop);}catch(...){failed=true;}
            std::lock_guard lock(target->mutex);target->count=value;target->failed=failed;target->done=true;
        });pending=true;SetTimer(hwnd,1,25,nullptr);
    }
    void poll() {
        if(!pending||!result)return;bool failed{};std::optional<uint64_t> value;
        {std::lock_guard lock(result->mutex);if(!result->done)return;value=result->count;failed=result->failed;}
        const bool stopped=worker.get_stop_token().stop_requested();worker.join();pending=false;KillTimer(hwnd,1);
        if(value){count=value;SetWindowTextW(control(id::Colors),(std::to_wstring(*count)+L" 色").c_str());}
        else {
            SetWindowTextW(control(id::Colors),L"");
            if(failed)SetWindowTextW(control(id::Error),L"色数を計算できませんでした。メモリ上限などを確認してください。");
            else if(automatic&&stopped)start();
        }
    }
    void set_font() {
        const auto next=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
        if(!next)throw std::runtime_error("Cannot create info font");
        EnumChildWindows(hwnd,[](HWND h,LPARAM font)->BOOL{SendMessageW(h,WM_SETFONT,font,TRUE);return TRUE;},reinterpret_cast<LPARAM>(next));if(font)DeleteObject(font);font=next;
    }
    int height()const{return static_cast<int>(data.fields.size())*27+118;}
    void layout() {
        const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT client{};GetClientRect(hwnd,&client);const int width=MulDiv(client.right,96,dpi),h=MulDiv(client.bottom,96,dpi);
        const auto move=[&](int key,int x,int y,int w,int h){MoveWindow(control(key),px(x),px(y),px(w),px(h),TRUE);};
        for(size_t n=0;n<data.fields.size();++n){const auto y=12+static_cast<int>(n)*27;move(id::Label+static_cast<int>(n),12,y+3,130,22);move(id::Field+static_cast<int>(n),152,y,width-168,24);}
        const int y=static_cast<int>(data.fields.size())*27+22;
        move(503,12,y+3,130,22);move(id::Colors,152,y,190,24);move(id::Auto,356,y,width-372,26);
        move(id::Error,16,y+32,width-32,std::max(30,h-y-80));move(IDOK,width-104,h-40,88,28);
    }
    void finish(){worker.request_stop();if(modal)EndDialog(hwnd,IDOK);else DestroyWindow(hwnd);}
    INT_PTR message(UINT m,WPARAM w,LPARAM l) {
        if(m==WM_INITDIALOG) {
            SetWindowTextW(hwnd,L"画像情報");dpi=GetDpiForWindow(hwnd);
            const auto make=[&](int key,const wchar_t *cls,const wchar_t *label,DWORD style){if(!CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(key)),GetModuleHandleW(nullptr),nullptr))throw std::runtime_error("Cannot create info field");};
            // Like the native MassiGra dialog, make OK the initial tab stop. A readonly
            // edit created first can retain a scrolled selection from its initial1px width.
            make(IDOK,L"BUTTON",L"OK",WS_TABSTOP|BS_DEFPUSHBUTTON);
            for(size_t n=0;n<data.fields.size();++n){make(id::Label+static_cast<int>(n),L"STATIC",(data.fields[n].first+L":").c_str(),SS_RIGHT);make(id::Field+static_cast<int>(n),L"EDIT",data.fields[n].second.c_str(),WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|ES_READONLY);SetWindowSubclass(control(id::Field+static_cast<int>(n)),field_keys,1,0);}
            make(503,L"STATIC",L"使われている色数:",SS_RIGHT);make(id::Colors,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL|ES_READONLY);SetWindowSubclass(control(id::Colors),field_keys,1,0);
            make(id::Auto,L"BUTTON",L"自動的に計算(&A)",WS_TABSTOP|BS_AUTOCHECKBOX);EnableWindow(control(id::Auto),enabled());SendMessageW(control(id::Auto),BM_SETCHECK,enabled()&&automatic?BST_CHECKED:BST_UNCHECKED,0);
            make(id::Error,L"STATIC",L"",0);set_font();
            RECT area{0,0,MulDiv(680,dpi,96),MulDiv(height(),dpi,96)};AdjustWindowRectExForDpi(&area,static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);
            RECT owner{};GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner);const int width=area.right-area.left,h=area.bottom-area.top;
            SetWindowPos(hwnd,nullptr,owner.left+std::max(0,int(owner.right-owner.left-width)/2),owner.top+std::max(0,int(owner.bottom-owner.top-h)/2),width,h,SWP_NOZORDER);
            initialized=true;layout();start();SetFocus(control(IDOK));return FALSE;
        }
        if(m==WM_TIMER&&w==1){poll();return TRUE;}
        if(m==WM_SIZE&&initialized){layout();return TRUE;}
        if(m==WM_GETMINMAXINFO&&initialized){auto &value=*reinterpret_cast<MINMAXINFO*>(l);value.ptMinTrackSize={MulDiv(520,dpi,96),MulDiv(height()+40,dpi,96)};return TRUE;}
        if(m==WM_DPICHANGED){dpi=HIWORD(w);set_font();const auto &area=*reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,area.left,area.top,area.right-area.left,area.bottom-area.top,SWP_NOZORDER);layout();return TRUE;}
        if(m==WM_COMMAND){const auto key=LOWORD(w);if(key==IDOK||key==IDCANCEL){finish();return TRUE;}if(key==id::Auto&&HIWORD(w)==BN_CLICKED&&enabled()){automatic=SendMessageW(control(id::Auto),BM_GETCHECK,0,0)==BST_CHECKED;if(automatic)start();else{worker.request_stop();if(!count)SetWindowTextW(control(id::Colors),L"");}return TRUE;}}
        if(m==WM_CLOSE){finish();return TRUE;}
        if(m==WM_DESTROY){initialized=false;worker.request_stop();KillTimer(hwnd,1);return TRUE;}
        return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));if(m==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}if(!self)return FALSE;
        try{return self->message(m,w,l);}catch(...){SetWindowTextW(self->control(id::Error),L"情報画面の処理に失敗しました。");return TRUE;}
    }
};
ImageInfoDialog::ImageInfoDialog(ImageInfoData data,bool automatic,Counter counter):impl(std::make_unique<Impl>()){if(data.fields.size()>50)throw std::invalid_argument("Too many information fields");impl->data=std::move(data);impl->automatic=automatic;impl->counter=std::move(counter);}
ImageInfoDialog::~ImageInfoDialog(){impl->worker.request_stop();if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);if(impl->font)DeleteObject(impl->font);}
bool ImageInfoDialog::show(HWND owner){DialogTemplate value;impl->modal=true;if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)throw std::runtime_error("Cannot open info dialog");return impl->automatic;}
HWND ImageInfoDialog::create(HWND owner){DialogTemplate value;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create info dialog");return h;}
HWND ImageInfoDialog::window()const{return impl->hwnd;}
HWND ImageInfoDialog::control(int key)const{return impl->control(key);}
bool ImageInfoDialog::ready()const{return impl->initialized;}
bool ImageInfoDialog::automatic()const{return impl->automatic;}
bool ImageInfoDialog::pending()const{return impl->pending;}
std::optional<uint64_t> ImageInfoDialog::count()const{return impl->count;}
std::wstring ImageInfoDialog::text()const{std::wstring result;for(size_t n=0;n<impl->data.fields.size();++n)result+=text_of(control(image_info_control::Label+static_cast<int>(n)))+L" "+text_of(control(image_info_control::Field+static_cast<int>(n)))+L"\n";return result;}
bool show_image_info(HWND owner,const ImageInfoData &data,bool automatic,const ImageInfoDialog::Counter &counter){ImageInfoDialog dialog(data,automatic,counter);return dialog.show(owner);}
}
