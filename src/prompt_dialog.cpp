#include "gvn/prompt_dialog.h"
#include "gvn/ui_theme.h"
#include <commctrl.h>
#include <map>
#include <cstring>
namespace gvn {
namespace {
namespace id=prompt_control;
struct Template {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    Template(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|DS_MODALFRAME|DS_SETFONT;dialog.cx=490;dialog.cy=320;}
};
LRESULT CALLBACK value_keys(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(m==WM_KEYDOWN&&w=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){SendMessageW(h,EM_SETSEL,0,-1);return 0;}
    return DefSubclassProc(h,m,w,l);
}
bool copy(HWND owner,const std::wstring &text){
    if(text.empty())return false;const size_t bytes=(text.size()+1)*sizeof(wchar_t);
    auto memory=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!memory)return false;
    auto data=GlobalLock(memory);if(!data){GlobalFree(memory);return false;}
    memcpy(data,text.c_str(),bytes);GlobalUnlock(memory);
    bool sent{};if(OpenClipboard(owner)){if(EmptyClipboard())sent=SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;CloseClipboard();}
    if(!sent)GlobalFree(memory);return sent;
}
std::wstring display(std::wstring text){std::wstring out;out.reserve(text.size());for(const auto c:text){if(c==L'\0')out+=L"\\u0000";else if(c==L'\n'){if(out.empty()||out.back()!=L'\r')out+=L'\r';out+=c;}else out+=c;}return out;}
}
struct PromptDialog::Impl {
    PromptDocument data;HWND hwnd{};HFONT font{};UINT dpi{96};bool modal{},initialized{};size_t selected{};
    std::vector<HTREEITEM> leaves;
    HWND control(int key)const{return GetDlgItem(hwnd,key);}
    void font_and_theme(){
        const auto next=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
        if(!next)throw std::runtime_error("Cannot create prompt font");
        EnumChildWindows(hwnd,[](HWND h,LPARAM f)->BOOL{SendMessageW(h,WM_SETFONT,f,TRUE);return TRUE;},reinterpret_cast<LPARAM>(next));if(font)DeleteObject(font);font=next;
        apply_ui_theme(hwnd);const auto &palette=ui_palette();TreeView_SetBkColor(control(id::Tree),palette.surface);TreeView_SetTextColor(control(id::Tree),palette.text);
    }
    void show_field(size_t n){
        if(n>=data.fields.size())return;selected=n;const auto &field=data.fields[n];
        SetWindowTextW(control(id::Heading),(field.group+L" / "+field.label).c_str());
        SetWindowTextW(control(id::Value),display(field.value.empty()?L"（空・情報なし）":field.value).c_str());
        SendMessageW(control(id::Value),EM_SETSEL,0,0);SendMessageW(control(id::Value),EM_SCROLLCARET,0,0);
        EnableWindow(control(id::Copy),!field.value.empty());SetWindowTextW(control(id::Notice),L"");
    }
    void layout(){
        const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT r{};GetClientRect(hwnd,&r);const int width=MulDiv(r.right,96,dpi),height=MulDiv(r.bottom,96,dpi);
        const auto move=[&](int key,int x,int y,int w,int h){MoveWindow(control(key),px(x),px(y),px(std::max(1,w)),px(std::max(1,h)),TRUE);};
        const int tree=std::clamp(width/3,180,300),x=tree+28;
        move(id::Status,14,10,width-28,24);move(id::Source,14,36,width-28,22);
        move(id::Tree,14,66,tree,height-126);move(id::Heading,x,66,width-x-14,26);move(id::Value,x,98,width-x-14,height-158);
        move(id::Notice,14,height-54,tree,20);
        move(id::Copy,width-542,height-44,124,30);move(id::Positive,width-410,height-44,144,30);move(id::Negative,width-258,height-44,144,30);move(IDOK,width-106,height-44,92,30);
    }
    void finish(){if(modal)EndDialog(hwnd,IDOK);else DestroyWindow(hwnd);}
    INT_PTR message(UINT m,WPARAM w,LPARAM l){
        if(m==WM_INITDIALOG){
            SetWindowTextW(hwnd,L"プロンプト表示");dpi=GetDpiForWindow(hwnd);
            const auto make=[&](int key,const wchar_t *cls,const wchar_t *text,DWORD style,DWORD ex=0){if(!CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,reinterpret_cast<HMENU>(INT_PTR(key)),GetModuleHandleW(nullptr),nullptr))throw std::runtime_error("Cannot create prompt control");};
            make(IDOK,L"BUTTON",L"閉じる",WS_TABSTOP|BS_DEFPUSHBUTTON);
            make(id::Status,L"STATIC",data.status.c_str(),SS_ENDELLIPSIS);make(id::Source,L"STATIC",data.source_name.c_str(),SS_PATHELLIPSIS);
            make(id::Tree,WC_TREEVIEWW,L"",WS_TABSTOP|TVS_HASLINES|TVS_LINESATROOT|TVS_HASBUTTONS|TVS_SHOWSELALWAYS,WS_EX_CLIENTEDGE);
            make(id::Heading,L"STATIC",L"",SS_ENDELLIPSIS);make(id::Value,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY|WS_VSCROLL,WS_EX_CLIENTEDGE);
            SendMessageW(control(id::Value),EM_SETLIMITTEXT,4*1024*1024,0);
            SetWindowSubclass(control(id::Value),value_keys,1,0);make(id::Notice,L"STATIC",L"",0);
            make(id::Copy,L"BUTTON",L"この欄をコピー",WS_TABSTOP);make(id::Positive,L"BUTTON",L"正の全文をコピー",WS_TABSTOP);make(id::Negative,L"BUTTON",L"負の全文をコピー",WS_TABSTOP);
            EnableWindow(control(id::Positive),!data.positive.empty());EnableWindow(control(id::Negative),!data.negative.empty());
            std::map<std::wstring,HTREEITEM> groups;
            for(size_t n=0;n<data.fields.size();++n){const auto &field=data.fields[n];auto parent=groups.find(field.group);
                if(parent==groups.end()){TVINSERTSTRUCTW entry{};entry.hParent=TVI_ROOT;entry.hInsertAfter=TVI_LAST;entry.item.mask=TVIF_TEXT|TVIF_PARAM;entry.item.pszText=const_cast<wchar_t*>(field.group.c_str());entry.item.lParam=0;parent=groups.emplace(field.group,TreeView_InsertItem(control(id::Tree),&entry)).first;}
                TVINSERTSTRUCTW entry{};entry.hParent=parent->second;entry.hInsertAfter=TVI_LAST;entry.item.mask=TVIF_TEXT|TVIF_PARAM;entry.item.pszText=const_cast<wchar_t*>(field.label.c_str());entry.item.lParam=LPARAM(n+1);leaves.push_back(TreeView_InsertItem(control(id::Tree),&entry));
            }
            for(const auto &[name,item]:groups)if(name==L"スロット"||name==L"メイン"||name==L"送信済み全文"||name==L"全文")TreeView_Expand(control(id::Tree),item,TVE_EXPAND);
            font_and_theme();RECT bounds{0,0,MulDiv(880,dpi,96),MulDiv(600,dpi,96)};AdjustWindowRectExForDpi(&bounds,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);
            RECT owner{};GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner);const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
            SetWindowPos(hwnd,nullptr,owner.left+std::max(0,int(owner.right-owner.left-width)/2),owner.top+std::max(0,int(owner.bottom-owner.top-height)/2),width,height,SWP_NOZORDER);
            initialized=true;layout();
            size_t initial{};for(size_t n=0;n<data.fields.size();++n)if((data.fields[n].group==L"送信済み全文"||data.fields[n].group==L"全文")&&data.fields[n].label==L"ポジティブ"){initial=n;break;}
            if(!leaves.empty())TreeView_SelectItem(control(id::Tree),leaves[initial]);SetFocus(control(id::Tree));return FALSE;
        }
        if(m==WM_NOTIFY){const auto header=reinterpret_cast<NMHDR*>(l);if(header->hwndFrom==control(id::Tree)){
            if(header->code==TVN_SELCHANGEDW){const auto change=reinterpret_cast<NMTREEVIEWW*>(l);if(change->itemNew.lParam)show_field(size_t(change->itemNew.lParam-1));else if(const auto child=TreeView_GetChild(control(id::Tree),change->itemNew.hItem)){TreeView_Expand(control(id::Tree),change->itemNew.hItem,TVE_EXPAND);TreeView_SelectItem(control(id::Tree),child);}return TRUE;}
            if(header->code==NM_CUSTOMDRAW){auto draw=reinterpret_cast<NMTVCUSTOMDRAW*>(l);if(draw->nmcd.dwDrawStage==CDDS_PREPAINT){SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,CDRF_NOTIFYITEMDRAW);return TRUE;}if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){if(draw->nmcd.lItemlParam>0&&size_t(draw->nmcd.lItemlParam)<=data.fields.size()&&!(draw->nmcd.uItemState&CDIS_SELECTED)&&data.fields[size_t(draw->nmcd.lItemlParam-1)].muted)draw->clrText=ui_palette().muted;SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,CDRF_DODEFAULT);return TRUE;}}
        }}
        if(m==WM_COMMAND){const auto key=LOWORD(w);if(key==IDOK||key==IDCANCEL){finish();return TRUE;}
            const std::wstring *value=key==id::Positive?&data.positive:key==id::Negative?&data.negative:key==id::Copy&&selected<data.fields.size()?&data.fields[selected].value:nullptr;
            if(value){SetWindowTextW(control(id::Notice),copy(hwnd,*value)?L"コピーしました":L"コピーできませんでした");return TRUE;}}
        if(m==WM_SIZE&&initialized){layout();return TRUE;}
        if(m==WM_GETMINMAXINFO&&initialized){auto info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={MulDiv(700,dpi,96),MulDiv(420,dpi,96)};return TRUE;}
        if(m==WM_DPICHANGED){dpi=HIWORD(w);font_and_theme();const auto &r=*reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER);layout();return TRUE;}
        if(m==WM_CLOSE){finish();return TRUE;}if(m==WM_DESTROY){initialized=false;return TRUE;}return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));if(m==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}if(!self)return FALSE;try{return self->message(m,w,l);}catch(...){if(m==WM_INITDIALOG){if(self->modal)EndDialog(h,IDCANCEL);else DestroyWindow(h);}return TRUE;}}
};
PromptDialog::PromptDialog(PromptDocument data):impl(std::make_unique<Impl>()){if(data.fields.size()>1024)throw std::invalid_argument("Too many prompt fields");impl->data=std::move(data);}
PromptDialog::~PromptDialog(){if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);if(impl->font)DeleteObject(impl->font);}
void PromptDialog::show(HWND owner){Template t;impl->modal=true;if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)throw std::runtime_error("Cannot open prompt dialog");}
HWND PromptDialog::create(HWND owner){Template t;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create prompt dialog");return h;}
HWND PromptDialog::window()const{return impl->hwnd;}HWND PromptDialog::control(int key)const{return impl->control(key);}bool PromptDialog::ready()const{return impl->initialized;}
const PromptDocument &PromptDialog::document()const{return impl->data;}size_t PromptDialog::selected()const{return impl->selected;}
void PromptDialog::select(size_t n){if(n>=impl->leaves.size())throw std::out_of_range("Prompt field");TreeView_SelectItem(impl->control(prompt_control::Tree),impl->leaves[n]);TreeView_EnsureVisible(impl->control(prompt_control::Tree),impl->leaves[n]);}
void show_prompt(HWND owner,const PromptDocument &data){PromptDialog dialog(data);dialog.show(owner);}
}
