#include "gvn/levels_dialog.h"
#include <commctrl.h>
#include <cmath>
#include <sstream>
namespace gvn {
namespace {
namespace id=levels_control;
struct Template {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    Template(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;dialog.cx=410;dialog.cy=270;}
};
std::wstring gamma_text(int value){auto decimal=std::to_wstring(value%1000);return std::to_wstring(value/1000)+L"."+std::wstring(3-decimal.size(),L'0')+decimal;}
int number(HWND h,bool gamma){
    wchar_t text[32]{};if(!GetWindowTextW(h,text,32))throw std::invalid_argument("Incomplete level value");std::wistringstream in(text);
    if(!gamma){int value{};if(!(in>>value)||!(in>>std::ws).eof())throw std::invalid_argument("Invalid level integer");return value;}
    for(auto at=text;*at;++at)if((*at<L'0'||*at>L'9')&&*at!=L'.')throw std::invalid_argument("Invalid level gamma");double value{};
    if(!(in>>value)||!(in>>std::ws).eof()||!std::isfinite(value)||value<.001||value>10||std::abs(value*1000-std::round(value*1000))>1e-7)throw std::invalid_argument("Invalid level gamma range");return int(std::lround(value*1000));
}
}
struct LevelsDialog::Impl {
    HWND hwnd{};HFONT font{};UINT dpi{96};bool modal{},initialized{},syncing{},pending{},input_valid{true},failed{},lost{};int channel{};LevelsChoice draft;LevelsHistogram histogram;Process process;Preview notify;Current current;
    std::shared_ptr<Image> candidate;std::optional<LevelsAdjustment> candidate_adjustment;std::optional<LevelsResult> selected;
    HWND control(int key) const{return GetDlgItem(hwnd,key);}bool context() const{return !lost&&(!current||current());}
    int slot() const{return draft.adjustment.individual?channel+1:0;}
    void withdraw() noexcept{try{if(notify)notify({});}catch(...) {}}
    void repaint(){InvalidateRect(control(id::Histogram),nullptr,FALSE);InvalidateRect(control(id::Gradient),nullptr,FALSE);}
    void error(const wchar_t *text,bool invalid=false){pending=false;failed=!invalid;input_valid=!invalid;KillTimer(hwnd,1);candidate.reset();candidate_adjustment.reset();withdraw();SetWindowTextW(control(id::Error),text);EnableWindow(control(IDOK),FALSE);}
    void read(){auto curve=draft.adjustment.curves[slot()];curve.black=number(control(id::Black),false);curve.gamma=number(control(id::Gamma),true);curve.white=number(control(id::White),false);curve.output_black=number(control(id::OutputBlack),false);curve.output_white=number(control(id::OutputWhite),false);if(!valid_level_curve(curve))throw std::invalid_argument("Invalid level curve");draft.adjustment.curves[slot()]=curve;}
    void fill(){
        syncing=true;const auto &v=draft.adjustment.curves[slot()];const int values[]{v.black,v.gamma,v.white,v.output_black,v.output_white};
        for(int n=0;n<5;++n){const auto text=n==1?gamma_text(values[n]):std::to_wstring(values[n]);SetWindowTextW(control(id::Black+n),text.c_str());SendMessageW(control(id::SpinBlack+n),UDM_SETPOS32,0,values[n]);}
        SendMessageW(control(id::Individual),BM_SETCHECK,draft.adjustment.individual?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(control(id::Preview),BM_SETCHECK,draft.preview?BST_CHECKED:BST_UNCHECKED,0);
        for(int n=0;n<3;++n){SendMessageW(control(id::Red+n),BM_SETCHECK,n==channel?BST_CHECKED:BST_UNCHECKED,0);EnableWindow(control(id::Red+n),draft.adjustment.individual);}
        EnableWindow(control(id::ColorGroup),draft.adjustment.individual);syncing=false;repaint();
    }
    bool compute(){
        pending=false;KillTimer(hwnd,1);if(!context()){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");return false;}if(!input_valid)return false;
        try{
            if(draft.adjustment.neutral()){candidate.reset();candidate_adjustment.reset();}
            else if(!candidate||candidate_adjustment!=draft.adjustment){auto next=process(draft.adjustment);if(!next||next->frames.size()!=1)throw std::runtime_error("Invalid level preview");candidate=std::move(next);candidate_adjustment=draft.adjustment;}
            if(!context()){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");return false;}
            failed=false;SetWindowTextW(control(id::Error),L"");EnableWindow(control(IDOK),TRUE);if(draft.preview&&candidate){if(notify)notify(candidate);}else withdraw();return true;
        }catch(...){error(L"プレビューを作成できません。画像と入力内容を確認してください。");return false;}
    }
    void schedule(){
        KillTimer(hwnd,1);pending=false;withdraw();repaint();
        if(!context()){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");return;}
        if(!input_valid)return;
        failed=false;SetWindowTextW(control(id::Error),L"");
        if(!draft.preview||draft.adjustment.neutral()){candidate.reset();candidate_adjustment.reset();EnableWindow(control(IDOK),TRUE);return;}
        pending=true;EnableWindow(control(IDOK),FALSE);if(!SetTimer(hwnd,1,30,nullptr))throw std::runtime_error("Cannot schedule levels preview");
    }
    void changed(){if(!initialized||syncing)return;try{read();input_valid=true;schedule();}catch(...){error(L"黒点・白点は0～255（入力黒点＜白点）、ガンマは0.001～10.000です。",true);}}
    void change_mode(bool individual,int next_channel){
        try{read();draft.adjustment.individual=individual;channel=next_channel;input_valid=true;fill();schedule();}
        catch(...){SendMessageW(control(id::Individual),BM_SETCHECK,draft.adjustment.individual?BST_CHECKED:BST_UNCHECKED,0);for(int n=0;n<3;++n)SendMessageW(control(id::Red+n),BM_SETCHECK,n==channel?BST_CHECKED:BST_UNCHECKED,0);error(L"入力値を修正してからRGBの対象を切り替えてください。",true);}
    }
    void spin(int n,int delta){
        try{read();const auto &v=draft.adjustment.curves[slot()];const int values[]{v.black,v.gamma,v.white,v.output_black,v.output_white};int low=n==1?1:0,high=n==1?10000:255;if(n==0)high=v.white-1;if(n==2)low=v.black+1;const int next=int(std::clamp(int64_t(values[n])+delta,int64_t(low),int64_t(high)));SetWindowTextW(control(id::Black+n),(n==1?gamma_text(next):std::to_wstring(next)).c_str());}
        catch(...){error(L"入力値を修正してください。",true);}
    }
    void paint(const DRAWITEMSTRUCT &item){
        const RECT rect=item.rcItem;FillRect(item.hDC,&rect,GetSysColorBrush(COLOR_WINDOW));const int width=rect.right-rect.left,height=rect.bottom-rect.top;if(width<3||height<3)return;
        if(item.CtlID==id::Gradient){
            const auto &curve=draft.adjustment.curves[slot()];for(int x=0;x<width;++x){const int value=int(std::lround(curve.output_black+(curve.output_white-curve.output_black)*double(x)/std::max(1,width-1)));const auto brush=CreateSolidBrush(RGB(value,value,value));RECT band{rect.left+x,rect.top,rect.left+x+1,rect.bottom};FillRect(item.hDC,&band,brush);DeleteObject(brush);}
        }else{
            const auto &bins=histogram[slot()];const auto maximum=*std::max_element(bins.begin(),bins.end());const COLORREF colors[]{RGB(80,80,80),RGB(190,45,45),RGB(35,145,55),RGB(45,75,190)};const auto brush=CreateSolidBrush(colors[slot()]);
            if(maximum)for(int n=0;n<256;++n){const int bar=int(std::lround(double(bins[n])/maximum*(height-2)));RECT band{rect.left+1+n*(width-2)/256,rect.bottom-1-bar,rect.left+1+(n+1)*(width-2)/256,rect.bottom-1};if(band.right==band.left)++band.right;FillRect(item.hDC,&band,brush);}DeleteObject(brush);
        }
        FrameRect(item.hDC,&rect,GetSysColorBrush(COLOR_3DSHADOW));
    }
    void arrange(){
        const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT client{};GetClientRect(hwnd,&client);const int width=MulDiv(client.right,96,dpi),height=MulDiv(client.bottom,96,dpi),left=width-198;
        const auto move=[&](int key,int x,int y,int w,int h){MoveWindow(control(key),px(x),px(y),px(w),px(h),TRUE);};
        move(id::InputGroup,12,12,left-12,214);move(id::OutputGroup,12,236,left-12,116);const int column=(left-36)/3;
        for(int n=0;n<3;++n){const int x=24+n*column;move(id::LabelBlack+n,x,34,column-12,22);move(id::Black+n,x,60,column-42,26);move(id::SpinBlack+n,x+column-42,60,18,26);}
        move(id::Histogram,24,100,left-36,110);move(id::Gradient,24,260,left-36,22);
        for(int n=0;n<2;++n){const int x=24+n*(left-36)/2;move(id::LabelOutputBlack+n,x,292,58,24);move(id::OutputBlack+n,x+60,290,(left-36)/2-90,26);move(id::SpinOutputBlack+n,x+(left-36)/2-30,290,18,26);}
        move(id::Individual,left+20,24,164,26);move(id::ColorGroup,left+20,52,154,102);for(int n=0;n<3;++n)move(id::Red+n,left+36,66+n*26,126,24);move(id::Preview,left+20,170,164,26);
        move(IDOK,left+24,242,150,30);move(IDCANCEL,left+24,284,150,30);move(id::Automatic,left+24,326,150,30);move(id::Error,18,366,width-36,height-376);
    }
    void set_font(){
        const auto next=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Yu Gothic UI");if(!next)throw std::runtime_error("Cannot create levels font");
        for(int key=id::Black;key<=id::LabelOutputWhite;++key)SendMessageW(control(key),WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);for(int key:{IDOK,IDCANCEL})SendMessageW(control(key),WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(font)DeleteObject(font);font=next;
    }
    void finish(bool accept){if(accept&&!compute())return;if(accept)selected=LevelsResult{draft,candidate};withdraw();KillTimer(hwnd,1);KillTimer(hwnd,2);if(modal)EndDialog(hwnd,accept?IDOK:IDCANCEL);else DestroyWindow(hwnd);}
    INT_PTR message(UINT m,WPARAM w,LPARAM l){
        if(m==WM_INITDIALOG){
            SetWindowTextW(hwnd,L"レベル調整");dpi=GetDpiForWindow(hwnd);INITCOMMONCONTROLSEX common{sizeof(common),ICC_UPDOWN_CLASS};if(!InitCommonControlsEx(&common))throw std::runtime_error("Cannot initialize level controls");
            const auto make=[&](int key,const wchar_t *cls,const wchar_t *text,DWORD style){const auto h=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,reinterpret_cast<HMENU>(INT_PTR(key)),GetModuleHandleW(nullptr),nullptr);if(!h)throw std::runtime_error("Cannot create level control");return h;};
            make(id::InputGroup,L"BUTTON",L"入力レベル",BS_GROUPBOX);make(id::OutputGroup,L"BUTTON",L"出力レベル",BS_GROUPBOX);const wchar_t *labels[]{L"黒点(&C)",L"ガンマ(&M)",L"白点(&W)",L"黒点(&K)",L"白点(&H)"};
            for(int n=0;n<5;++n){make(id::LabelBlack+n,L"STATIC",labels[n],0);const auto h=make(id::Black+n,L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP);SendMessageW(h,EM_SETLIMITTEXT,12,0);const auto spin=make(id::SpinBlack+n,UPDOWN_CLASSW,L"",UDS_ARROWKEYS);SendMessageW(spin,UDM_SETBUDDY,reinterpret_cast<WPARAM>(h),0);SendMessageW(spin,UDM_SETRANGE32,n==1?1:0,n==1?10000:255);}
            make(id::Histogram,L"STATIC",L"",SS_OWNERDRAW);make(id::Gradient,L"STATIC",L"",SS_OWNERDRAW);make(id::Individual,L"BUTTON",L"RGB 個別(&N)",BS_AUTOCHECKBOX|WS_TABSTOP);make(id::ColorGroup,L"BUTTON",L"",BS_GROUPBOX);for(int n=0;n<3;++n)make(id::Red+n,L"BUTTON",n==0?L"&Red":n==1?L"&Green":L"&Blue",BS_AUTORADIOBUTTON|WS_TABSTOP|(n==0?WS_GROUP:0));
            make(id::Preview,L"BUTTON",L"プレビュー(&P)",BS_AUTOCHECKBOX|WS_TABSTOP|WS_GROUP);make(id::Automatic,L"BUTTON",L"自動設定(&A)",WS_TABSTOP);make(id::Error,L"STATIC",L"",0);make(IDOK,L"BUTTON",L"OK",BS_DEFPUSHBUTTON|WS_TABSTOP);make(IDCANCEL,L"BUTTON",L"キャンセル",WS_TABSTOP);set_font();fill();
            const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT client{0,0,px(640),px(420)};AdjustWindowRectExForDpi(&client,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);RECT owner{};GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);const auto placed=place_window({owner.left+(owner.right-owner.left-client.right+client.left)/2,owner.top+(owner.bottom-owner.top-client.bottom+client.top)/2,client.right-client.left,client.bottom-client.top},{monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top},false);SetWindowPos(hwnd,nullptr,placed.x,placed.y,placed.w,placed.h,SWP_NOZORDER|SWP_NOACTIVATE);arrange();initialized=true;schedule();if(!SetTimer(hwnd,2,80,nullptr))throw std::runtime_error("Cannot monitor levels context");SetFocus(control(id::Black));return FALSE;
        }
        if(m==WM_COMMAND&&initialized){const auto key=LOWORD(w);if(key==IDOK||key==IDCANCEL){finish(key==IDOK);return TRUE;}if(key>=id::Black&&key<=id::OutputWhite&&HIWORD(w)==EN_CHANGE){changed();return TRUE;}if(key==id::Individual){change_mode(SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED,channel);return TRUE;}if(key>=id::Red&&key<=id::Blue){change_mode(true,key-id::Red);return TRUE;}if(key==id::Preview){draft.preview=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;schedule();return TRUE;}if(key==id::Automatic){draft.adjustment=automatic_levels(draft.adjustment,histogram);input_valid=true;fill();schedule();return TRUE;}}
        if(m==WM_NOTIFY&&initialized){const auto event=reinterpret_cast<NMUPDOWN*>(l);if(event->hdr.code==UDN_DELTAPOS&&event->hdr.idFrom>=id::SpinBlack&&event->hdr.idFrom<=id::SpinOutputWhite){spin(int(event->hdr.idFrom)-id::SpinBlack,event->iDelta);SetWindowLongPtrW(hwnd,DWLP_MSGRESULT,TRUE);return TRUE;}}
        if(m==WM_DRAWITEM){const auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item&&(item->CtlID==id::Histogram||item->CtlID==id::Gradient)){paint(*item);return TRUE;}}
        if(m==WM_TIMER){if(w==1)compute();else if(w==2&&!context()&&!lost){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");}return TRUE;}
        if(m==WM_DPICHANGED){dpi=HIWORD(w);const auto rect=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);set_font();arrange();return TRUE;}
        if(m==WM_CLOSE){finish(false);return TRUE;}if(m==WM_DESTROY){KillTimer(hwnd,1);KillTimer(hwnd,2);withdraw();candidate.reset();initialized=false;return TRUE;}return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));if(m==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}if(!self)return FALSE;try{return self->message(m,w,l);}catch(...){self->error(L"レベル調整を実行できません。");if(!self->initialized)PostMessageW(h,WM_CLOSE,0,0);return TRUE;}}
};
LevelsDialog::LevelsDialog(LevelsChoice choice,LevelsHistogram histogram,Process process,Preview preview,Current current):impl(std::make_unique<Impl>()){
    if(!valid_levels(choice.adjustment)||!process)throw std::invalid_argument("Invalid levels dialog source");impl->draft=choice;impl->histogram=std::move(histogram);impl->process=std::move(process);impl->notify=std::move(preview);impl->current=std::move(current);
}
LevelsDialog::~LevelsDialog(){if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);if(impl->font)DeleteObject(impl->font);}
std::optional<LevelsResult> LevelsDialog::show(HWND owner){Template value;impl->modal=true;if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)throw std::runtime_error("Cannot open levels dialog");return impl->selected;}
HWND LevelsDialog::create(HWND owner){Template value;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create levels dialog");return h;}
HWND LevelsDialog::window() const{return impl->hwnd;}HWND LevelsDialog::control(int key) const{return impl->control(key);}
LevelsChoice LevelsDialog::options() const{return impl->draft;}bool LevelsDialog::ready() const{return impl->initialized&&!impl->pending;}bool LevelsDialog::valid() const{return impl->context()&&impl->input_valid&&!impl->failed;}
std::shared_ptr<const Image> LevelsDialog::preview_image() const{return impl->draft.preview?impl->candidate:nullptr;}std::optional<LevelsResult> LevelsDialog::result() const{return impl->selected;}
std::optional<LevelsResult> choose_levels(HWND owner,LevelsChoice choice,const LevelsHistogram &histogram,const LevelsDialog::Process &process,const LevelsDialog::Preview &preview,const LevelsDialog::Current &current){LevelsDialog dialog(choice,histogram,process,preview,current);return dialog.show(owner);}
} // namespace gvn
