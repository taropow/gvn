#include "gvn/ui_theme.h"
#include "gvn/palette_dialog.h"
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <array>
namespace gvn {
namespace {
namespace id=palette_control;
struct Template {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    Template(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;dialog.cx=340;dialog.cy=380;}
};
int number(HWND field,int maximum) {
    wchar_t text[16]{};const auto length=GetWindowTextW(field,text,16);
    if(!length||length>3)throw std::invalid_argument("Invalid palette number");
    int value{};for(int i=0;i<length;++i){if(text[i]<L'0'||text[i]>L'9')throw std::invalid_argument("Invalid palette number");value=value*10+text[i]-L'0';}
    if(value>maximum)throw std::out_of_range("Palette number out of range");return value;
}
std::optional<uint32_t> pick_color(HWND owner,uint32_t rgb) {
    COLORREF custom[16]{};CHOOSECOLORW value{sizeof(value)};value.hwndOwner=owner;
    value.rgbResult=RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255);value.lpCustColors=custom;value.Flags=CC_FULLOPEN|CC_RGBINIT;
    if(!ChooseColorW(&value))return {};
    return (uint32_t(GetRValue(value.rgbResult))<<16)|(uint32_t(GetGValue(value.rgbResult))<<8)|GetBValue(value.rgbResult);
}
}
struct PaletteDialog::Impl {
    HWND hwnd{};HFONT font{};UINT dpi{96};bool modal{},initialized{},syncing{},input_valid{true},failed{},lost{},pending{},accept{};
    bool preview{true};unsigned selected{};int hovered{-1};
    std::vector<Pixel> draft,baseline,candidate_palette;
    Process process;Preview notify;Commit commit;Current current;Picker picker;
    std::shared_ptr<Image> candidate;
    HWND control(int key) const{return GetDlgItem(hwnd,key);}
    bool context() const{return !lost&&(!current||current());}
    bool dirty() const{return draft!=baseline;}
    void withdraw() noexcept {try{if(notify)notify({});}catch(...) {}}
    void buttons() {
        const bool available=context()&&input_valid&&!failed;
        EnableWindow(control(IDOK),available&&!pending);EnableWindow(control(id::Apply),available&&!pending&&dirty());
    }
    void fail(const wchar_t *message) {
        pending=false;failed=true;KillTimer(hwnd,1);candidate.reset();candidate_palette.clear();withdraw();
        SetWindowTextW(control(id::Error),message);buttons();
    }
    void invalid() {
        input_valid=false;pending=false;KillTimer(hwnd,1);candidate.reset();candidate_palette.clear();withdraw();
        SetWindowTextW(control(id::Error),L"番号はパレットの範囲内、RGBは0〜255で入力してください。");buttons();
    }
    void fill() {
        syncing=true;SetWindowTextW(control(id::Index),std::to_wstring(selected).c_str());
        const auto pixel=draft[selected];const int values[]{pixel.r,pixel.g,pixel.b};
        for(int n=0;n<3;++n){SetWindowTextW(control(id::Red+n),std::to_wstring(values[n]).c_str());SendMessageW(control(id::SlideRed+n),TBM_SETPOS,TRUE,values[n]);}
        syncing=false;InvalidateRect(control(id::Swatch),nullptr,FALSE);InvalidateRect(control(id::Grid),nullptr,FALSE);hover(hovered,true);
    }
    bool compute() {
        pending=false;KillTimer(hwnd,1);
        if(!context()){lost=true;fail(L"表示画像が変わりました。閉じてから開き直してください。");return false;}
        if(!input_valid){invalid();return false;}
        if(!dirty()){candidate.reset();candidate_palette.clear();withdraw();failed=false;buttons();return true;}
        try {
            if(!candidate||candidate_palette!=draft) {
                auto value=process(draft);
                if(!value||value->frames.size()!=1||!value->frames[0].indexed||value->frames[0].indexed->palette!=draft)
                    throw std::runtime_error("Invalid palette preview");
                candidate=std::move(value);candidate_palette=draft;
            }
            if(!context()){lost=true;fail(L"表示画像が変わりました。閉じてから開き直してください。");return false;}
            failed=false;SetWindowTextW(control(id::Error),L"");if(preview&&notify)notify(candidate);buttons();return true;
        }catch(...){fail(L"パレットの変更を作成できません。画像と入力内容を確認してください。");return false;}
    }
    void schedule() {
        KillTimer(hwnd,1);pending=false;failed=false;input_valid=true;SetWindowTextW(control(id::Error),L"");
        if(!context()){lost=true;fail(L"表示画像が変わりました。閉じてから開き直してください。");return;}
        if(!preview||!dirty()){candidate.reset();candidate_palette.clear();withdraw();buttons();return;}
        pending=true;buttons();if(!SetTimer(hwnd,1,30,nullptr))throw std::runtime_error("Palette preview timer failed");
    }
    void select(unsigned value) {selected=value;fill();schedule();}
    void changed(int key) {
        if(!initialized||syncing)return;
        try {
            const auto index=number(control(id::Index),int(draft.size()-1));
            if(key==id::Index){select(unsigned(index));return;}
            const auto r=number(control(id::Red),255),g=number(control(id::Green),255),b=number(control(id::Blue),255);
            auto &pixel=draft[selected];pixel.r=uint8_t(r);pixel.g=uint8_t(g);pixel.b=uint8_t(b);
            const int values[]{r,g,b};for(int n=0;n<3;++n)SendMessageW(control(id::SlideRed+n),TBM_SETPOS,TRUE,values[n]);
            InvalidateRect(control(id::Swatch),nullptr,FALSE);InvalidateRect(control(id::Grid),nullptr,FALSE);hover(hovered,true);schedule();
        }catch(...){invalid();}
    }
    bool apply(bool close) {
        if(!context()){lost=true;fail(L"表示画像が変わりました。閉じてから開き直してください。");return false;}
        if(!input_valid)return false;
        if(dirty()&&!compute())return false;
        try {
            // Allocate the next baseline before the callback can commit the document.
            auto next_baseline=draft;auto value=dirty()?candidate:nullptr;
            withdraw();commit(value,preview);baseline.swap(next_baseline);
            candidate.reset();candidate_palette.clear();pending=false;failed=false;KillTimer(hwnd,1);buttons();
            if(close)finish(true);return true;
        }catch(...){fail(L"変更を適用できません。画像または設定の保存先を確認してください。");return false;}
    }
    void finish(bool accepted) {
        accept=accepted;withdraw();KillTimer(hwnd,1);KillTimer(hwnd,2);
        if(modal)EndDialog(hwnd,accepted?IDOK:IDCANCEL);else DestroyWindow(hwnd);
    }
    int columns() const{return draft.size()<=4?2:draft.size()<=16?4:16;}
    RECT cell(unsigned index) const {
        RECT area{};GetClientRect(control(id::Grid),&area);const auto cols=columns(),rows=(int(draft.size())+cols-1)/cols;
        return {int(index%cols)*area.right/cols,int(index/cols)*area.bottom/rows,
            (int(index%cols)+1)*area.right/cols,(int(index/cols)+1)*area.bottom/rows};
    }
    int hit(int x,int y) const {
        RECT area{};GetClientRect(control(id::Grid),&area);
        if(x<0||y<0||x>=area.right||y>=area.bottom||!area.right||!area.bottom)return -1;
        const int cols=columns(),rows=(int(draft.size())+cols-1)/cols;
        const int index=(((y+1)*rows-1)/area.bottom)*cols+((x+1)*cols-1)/area.right;return index<int(draft.size())?index:-1;
    }
    void hover(int index,bool refresh=false) {
        if(index==hovered&&!refresh)return;hovered=index;
        std::wstring text;
        if(index>=0){const auto p=draft[index];text=L"番号: "+std::to_wstring(index)+L"   RGB: "+std::to_wstring(p.r)+L", "+std::to_wstring(p.g)+L", "+std::to_wstring(p.b)+L"   Alpha: "+std::to_wstring(p.a);}
        SetWindowTextW(control(id::Hover),text.c_str());
    }
    void color(HDC dc,RECT area,Pixel pixel) {
        if(pixel.a==255){const auto brush=CreateSolidBrush(RGB(pixel.r,pixel.g,pixel.b));FillRect(dc,&area,brush);DeleteObject(brush);return;}
        const int tile=std::max(1,MulDiv(6,dpi,96));
        for(int y=area.top;y<area.bottom;y+=tile)for(int x=area.left;x<area.right;x+=tile) {
            const uint8_t gray=((x/tile+y/tile)&1)?192:238;const auto p=over(pixel,{gray,gray,gray,255});
            const auto brush=CreateSolidBrush(RGB(p.r,p.g,p.b));RECT block{x,y,std::min<LONG>(x+tile,area.right),std::min<LONG>(y+tile,area.bottom)};
            FillRect(dc,&block,brush);DeleteObject(brush);
        }
    }
    void draw(const DRAWITEMSTRUCT &item) {
        if(item.CtlID==id::Swatch){color(item.hDC,item.rcItem,draft[selected]);FrameRect(item.hDC,&item.rcItem,GetSysColorBrush(COLOR_WINDOWFRAME));return;}
        if(item.CtlID!=id::Grid)return;
        FillRect(item.hDC,&item.rcItem,GetSysColorBrush(COLOR_BTNFACE));
        for(unsigned i=0;i<draft.size();++i){const auto r=cell(i);color(item.hDC,r,draft[i]);FrameRect(item.hDC,&r,GetSysColorBrush(COLOR_WINDOWFRAME));}
        auto r=cell(selected);InflateRect(&r,-1,-1);const auto brush=CreateSolidBrush(RGB(255,0,0));
        FrameRect(item.hDC,&r,brush);InflateRect(&r,-1,-1);FrameRect(item.hDC,&r,brush);DeleteObject(brush);
    }
    static LRESULT CALLBACK grid(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        auto &self=*reinterpret_cast<Impl*>(data);
        try {
            if(m==WM_GETDLGCODE)return DLGC_WANTARROWS;
            if(m==WM_LBUTTONDOWN){const int index=self.hit(GET_X_LPARAM(l),GET_Y_LPARAM(l));if(index>=0){SetFocus(h);self.select(unsigned(index));}return 0;}
            if(m==WM_MOUSEMOVE){self.hover(self.hit(GET_X_LPARAM(l),GET_Y_LPARAM(l)));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);return 0;}
            if(m==WM_MOUSELEAVE){self.hover(-1);return 0;}
            if(m==WM_KEYDOWN) {
                const int delta=w==VK_LEFT?-1:w==VK_RIGHT?1:w==VK_UP?-self.columns():w==VK_DOWN?self.columns():0;
                if(delta||w==VK_HOME||w==VK_END){const auto next=w==VK_HOME?0:w==VK_END?int(self.draft.size()-1):std::clamp(int(self.selected)+delta,0,int(self.draft.size()-1));self.select(unsigned(next));return 0;}
            }
        }catch(...){self.fail(L"パレットを操作できません。");return 0;}
        return DefSubclassProc(h,m,w,l);
    }
    void layout() {
        const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT area{};GetClientRect(hwnd,&area);
        const int width=MulDiv(area.right,96,dpi),height=MulDiv(area.bottom,96,dpi);
        const auto move=[&](int key,int x,int y,int w,int h){MoveWindow(control(key),px(x),px(y),px(w),px(h),TRUE);};
        move(id::Swatch,16,16,72,72);move(530,108,16,42,22);move(id::Index,154,14,66,26);move(id::Spin,220,14,18,26);
        move(534,260,16,width-276,22);
        for(int n=0;n<3;++n){move(531+n,108,49+n*31,20,24);move(id::Red+n,132,47+n*31,60,26);move(id::SlideRed+n,204,45+n*31,width-220,28);}
        move(id::Pick,16,102,80,28);move(id::Hover,16,147,width-32,36);move(id::Grid,16,196,width-32,height-330);
        move(id::Preview,16,height-119,width-32,24);move(id::Error,16,height-86,width-32,30);
        move(IDOK,width-292,height-42,80,28);move(IDCANCEL,width-204,height-42,88,28);move(id::Apply,width-108,height-42,92,28);
    }
    void set_font() {
        const auto next=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Yu Gothic UI");
        if(!next)throw std::runtime_error("Cannot create palette font");
        for(int key:{id::Index,id::Red,id::Green,id::Blue,id::SlideRed,id::SlideGreen,id::SlideBlue,id::Pick,id::Grid,id::Swatch,id::Hover,id::Preview,id::Error,id::Apply,IDOK,IDCANCEL,530,531,532,533,534})
            SendMessageW(control(key),WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(font)DeleteObject(font);font=next;
    }
    void initialize() {
        SetWindowTextW(hwnd,L"パレット編集");dpi=GetDpiForWindow(hwnd);
        const auto add=[&](int key,const wchar_t *cls,const wchar_t *text,DWORD style){
            const auto h=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,reinterpret_cast<HMENU>(INT_PTR(key)),GetModuleHandleW(nullptr),nullptr);
            if(!h)throw std::runtime_error("Cannot create palette control");return h;};
        add(530,L"STATIC",L"番号(&I)",0);for(int n=0;n<3;++n)add(531+n,L"STATIC",n==0?L"&R":n==1?L"&G":L"&B",0);
        add(534,L"STATIC",L"カーソル位置の色",0);
        for(int key:{id::Index,id::Red,id::Green,id::Blue}){add(key,L"EDIT",L"",WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL);SendMessageW(control(key),EM_LIMITTEXT,3,0);}
        add(id::Spin,UPDOWN_CLASSW,L"",UDS_SETBUDDYINT|UDS_ARROWKEYS);SendMessageW(control(id::Spin),UDM_SETBUDDY,reinterpret_cast<WPARAM>(control(id::Index)),0);SendMessageW(control(id::Spin),UDM_SETRANGE32,0,draft.size()-1);
        for(int n=0;n<3;++n){add(id::SlideRed+n,TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS);SendMessageW(control(id::SlideRed+n),TBM_SETRANGE,TRUE,MAKELPARAM(0,255));}
        add(id::Pick,L"BUTTON",L"色選択(&C)…",WS_TABSTOP|BS_PUSHBUTTON);
        add(id::Swatch,L"STATIC",L"",SS_OWNERDRAW);add(id::Grid,L"STATIC",L"",WS_TABSTOP|SS_OWNERDRAW|SS_NOTIFY);
        if(!SetWindowSubclass(control(id::Grid),grid,1,reinterpret_cast<DWORD_PTR>(this)))throw std::runtime_error("Cannot initialize palette grid");
        add(id::Hover,L"EDIT",L"",ES_READONLY|ES_MULTILINE);add(id::Preview,L"BUTTON",L"プレビュー(&P)",WS_TABSTOP|BS_AUTOCHECKBOX);
        SendMessageW(control(id::Preview),BM_SETCHECK,preview?BST_CHECKED:BST_UNCHECKED,0);
        add(id::Error,L"STATIC",L"",0);add(IDOK,L"BUTTON",L"OK",WS_TABSTOP|BS_DEFPUSHBUTTON);add(IDCANCEL,L"BUTTON",L"キャンセル",WS_TABSTOP);add(id::Apply,L"BUTTON",L"適用(&A)",WS_TABSTOP);
        set_font();const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT area{0,0,px(520),px(592)};
        AdjustWindowRectExForDpi(&area,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);
        RECT owner{};GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
        const auto placed=place_window({owner.left+(owner.right-owner.left-area.right+area.left)/2,owner.top+(owner.bottom-owner.top-area.bottom+area.top)/2,area.right-area.left,area.bottom-area.top},
            {monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top},false);
        SetWindowPos(hwnd,nullptr,placed.x,placed.y,placed.w,placed.h,SWP_NOZORDER|SWP_NOACTIVATE);
        initialized=true;layout();fill();buttons();SetTimer(hwnd,2,80,nullptr);SetFocus(control(id::Index));
    }
    INT_PTR message(UINT m,WPARAM w,LPARAM l) {
        if(m==WM_INITDIALOG){initialize();return FALSE;}
        if(m==WM_DRAWITEM){draw(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
        if(m==WM_COMMAND&&initialized) {
            const auto key=LOWORD(w),event=HIWORD(w);
            if(key==IDCANCEL){finish(false);return TRUE;}
            if(key==IDOK||key==id::Apply){apply(key==IDOK);return TRUE;}
            if(key>=id::Index&&key<=id::Blue&&event==EN_CHANGE){changed(key);return TRUE;}
            if(key==id::Preview&&event==BN_CLICKED){preview=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;if(input_valid)schedule();return TRUE;}
            if(key==id::Pick&&event==BN_CLICKED&&context()) {
                const auto p=draft[selected];const auto result=picker(hwnd,(uint32_t(p.r)<<16)|(uint32_t(p.g)<<8)|p.b);
                if(result&&*result<=0xffffff){auto &value=draft[selected];value.r=uint8_t(*result>>16);value.g=uint8_t(*result>>8);value.b=uint8_t(*result);fill();schedule();}return TRUE;
            }
        }
        if(m==WM_HSCROLL&&initialized) {
            for(int n=0;n<3;++n)if(reinterpret_cast<HWND>(l)==control(id::SlideRed+n)) {
                SetWindowTextW(control(id::Red+n),std::to_wstring(SendMessageW(control(id::SlideRed+n),TBM_GETPOS,0,0)).c_str());return TRUE;
            }
        }
        if(m==WM_TIMER){if(w==1)compute();else if(w==2&&!context()&&!lost){lost=true;fail(L"表示画像が変わりました。閉じてから開き直してください。");}return TRUE;}
        if(m==WM_DPICHANGED){dpi=HIWORD(w);const auto area=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,area->left,area->top,area->right-area->left,area->bottom-area->top,SWP_NOZORDER|SWP_NOACTIVATE);set_font();layout();return TRUE;}
        if(m==WM_CLOSE){finish(false);return TRUE;}
        if(m==WM_DESTROY){KillTimer(hwnd,1);KillTimer(hwnd,2);withdraw();candidate.reset();initialized=false;return TRUE;}
        return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));
        if(m==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}
        if(!self)return FALSE;try{const auto result=self->message(m,w,l);if(m==WM_INITDIALOG||m==WM_DPICHANGED)apply_ui_theme(h);return result;}catch(...){self->fail(L"パレット編集を実行できません。");return TRUE;}
    }
};
PaletteDialog::PaletteDialog(std::vector<Pixel> palette,bool preview,Process process,Preview notify,Commit commit,Current current,Picker picker):impl(std::make_unique<Impl>()) {
    if(palette.empty()||palette.size()>256||!process||!commit)throw std::invalid_argument("Invalid palette editor context");
    impl->draft=std::move(palette);impl->baseline=impl->draft;impl->preview=preview;impl->process=std::move(process);
    impl->notify=std::move(notify);impl->commit=std::move(commit);impl->current=std::move(current);impl->picker=picker?std::move(picker):pick_color;
}
PaletteDialog::~PaletteDialog(){if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);if(impl->font)DeleteObject(impl->font);}
void PaletteDialog::show(HWND owner){Template value;impl->modal=true;if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)throw std::runtime_error("Cannot open palette dialog");}
HWND PaletteDialog::create(HWND owner){Template value;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create palette dialog");return h;}
HWND PaletteDialog::window() const{return impl->hwnd;}
HWND PaletteDialog::control(int key) const{return impl->control(key);}
bool PaletteDialog::ready() const{return impl->initialized&&!impl->pending;}
bool PaletteDialog::valid() const{return impl->context()&&impl->input_valid&&!impl->failed;}
bool PaletteDialog::accepted() const{return impl->accept;}
bool PaletteDialog::preview_enabled() const{return impl->preview;}
unsigned PaletteDialog::selected_index() const{return impl->selected;}
const std::vector<Pixel> &PaletteDialog::palette() const{return impl->draft;}
std::shared_ptr<const Image> PaletteDialog::preview_image() const{return impl->preview?impl->candidate:nullptr;}
void choose_palette(HWND owner,const std::vector<Pixel> &palette,bool preview,const PaletteDialog::Process &process,const PaletteDialog::Preview &notify,const PaletteDialog::Commit &commit,const PaletteDialog::Current &current) {
    PaletteDialog dialog(palette,preview,process,notify,commit,current);dialog.show(owner);
}
} // namespace gvn
