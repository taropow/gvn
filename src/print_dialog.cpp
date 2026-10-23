#include "gvn/ui_theme.h"
#include "gvn/printing.h"
#include <commctrl.h>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace gvn {
namespace {
namespace id=print_control;
struct Template {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    Template(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;dialog.cx=500;dialog.cy=390;}
};
std::wstring text(HWND h) {
    std::wstring s(size_t(GetWindowTextLengthW(h))+1,L'\0');GetWindowTextW(h,s.data(),int(s.size()));s.pop_back();return s;
}
std::wstring decimal(double n) {
    std::wostringstream s;s<<std::fixed<<std::setprecision(9)<<n;auto result=s.str();
    while(result.size()>1&&result.back()==L'0')result.pop_back();if(result.back()==L'.')result.pop_back();return result;
}
double number(HWND h) {
    const auto s=text(h);if(s.empty()||s.find_first_not_of(L"0123456789.")!=std::wstring::npos)throw std::invalid_argument("Invalid number");
    size_t used{};const double n=std::stod(s,&used);if(used!=s.size()||!std::isfinite(n))throw std::invalid_argument("Invalid number");return n;
}
}
struct PrintDialog::Impl {
    const Image &image;size_t frame;ImageMetadata metadata;PrintOptions draft;PrintDevice printer;Chooser chooser;
    HWND hwnd{};HFONT font{};UINT dpi{96};bool syncing{},valid{};
    std::optional<PrintOptions> selected;std::optional<PrintLayout> plan;
    Impl(const Image &i,size_t f,const ImageMetadata &m,PrintOptions o,PrintDevice p,Chooser c):image(i),frame(f),metadata(m),draft(o),printer(std::move(p)),chooser(std::move(c)){}
    HWND control(int key) const{return GetDlgItem(hwnd,key);}
    static LRESULT CALLBACK combo_print(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
        const auto result=DefSubclassProc(h,msg,w,l);
        if(msg==WM_PRINT||msg==WM_PRINTCLIENT) {
            const auto item=SendMessageW(h,CB_GETCURSEL,0,0);COMBOBOXINFO info{sizeof(info)};
            if(item>=0&&GetComboBoxInfo(h,&info)) {
                DRAWITEMSTRUCT draw{};draw.CtlType=ODT_COMBOBOX;draw.CtlID=GetDlgCtrlID(h);draw.itemID=UINT(item);
                draw.itemAction=ODA_DRAWENTIRE;draw.itemState=ODS_COMBOBOXEDIT|(IsWindowEnabled(h)?0:ODS_DISABLED);
                draw.hwndItem=h;draw.hDC=HDC(w);draw.rcItem=info.rcItem;
                SendMessageW(GetParent(h),WM_DRAWITEM,draw.CtlID,LPARAM(&draw));
            }
        }
        return result;
    }
    void paint_combo(const DRAWITEMSTRUCT &draw) {
        if(draw.itemID==UINT(-1))return;
        const auto saved=SaveDC(draw.hDC);const bool selected=(draw.itemState&ODS_SELECTED)!=0;
        FillRect(draw.hDC,&draw.rcItem,GetSysColorBrush(selected?COLOR_HIGHLIGHT:COLOR_WINDOW));
        SetTextColor(draw.hDC,GetSysColor((draw.itemState&ODS_DISABLED)?COLOR_GRAYTEXT:selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT));
        SetBkMode(draw.hDC,TRANSPARENT);SelectObject(draw.hDC,font?font:GetStockObject(DEFAULT_GUI_FONT));
        const auto length=SendMessageW(draw.hwndItem,CB_GETLBTEXTLEN,draw.itemID,0);
        if(length>=0) {
            std::wstring label(size_t(length)+1,L'\0');SendMessageW(draw.hwndItem,CB_GETLBTEXT,draw.itemID,LPARAM(label.data()));
            RECT area=draw.rcItem;area.left+=5;DrawTextW(draw.hDC,label.c_str(),-1,&area,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        }
        RestoreDC(draw.hDC,saved);
    }
    void set(int key,const std::wstring &value){SetWindowTextW(control(key),value.c_str());}
    double unit() const{return draft.unit?25.4:10.;}
    void read() {
        const auto factor=unit();
        const auto length=[&](int key,double previous){return text(control(key))==decimal(previous/factor)?previous:number(control(key))*factor;};
        draft.left=length(id::Left,draft.left);draft.top=length(id::Top,draft.top);
        draft.right=length(id::Right,draft.right);draft.bottom=length(id::Bottom,draft.bottom);
        draft.width=length(id::Width,draft.width);draft.height=length(id::Height,draft.height);
        const auto copies=text(control(id::Copies));
        if(copies.empty()||copies.find_first_not_of(L"0123456789")!=std::wstring::npos)throw std::invalid_argument("Invalid copies");
        draft.copies=std::stoi(copies);
    }
    void refresh() {
        try {
            read();plan=print_layout(image.width,image.height,metadata,printer.paper,draft);valid=true;set(id::Error,L"");
        }catch(...) {valid=false;plan.reset();set(id::Error,L"数値・部数・余白を確認してください。余白の内側に印刷領域が必要です。");}
        EnableWindow(control(IDOK),valid&&printer.ready);
        const bool specified=draft.sizing==PrintSizing::Specified;
        for(int key:{id::Width,id::Height,id::Aspect})EnableWindow(control(key),specified);
        InvalidateRect(control(id::Preview),nullptr,FALSE);
    }
    void fill() {
        syncing=true;const double factor=unit();
        set(id::Printer,printer.name.empty()?L"プリンタを選択してください":printer.name);
        set(id::Paper,printer.paper_name+L"  "+decimal(printer.paper.width/factor)+L" × "+decimal(printer.paper.height/factor)+(draft.unit?L" inch":L" cm"));
        set(id::Copies,std::to_wstring(draft.copies));
        set(id::Left,decimal(draft.left/factor));set(id::Top,decimal(draft.top/factor));
        set(id::Right,decimal(draft.right/factor));set(id::Bottom,decimal(draft.bottom/factor));
        set(id::Width,decimal(draft.width/factor));set(id::Height,decimal(draft.height/factor));
        const auto &p=printer.paper;
        set(id::Minimum,L"最小余白 L "+decimal(p.left/factor)+L" / T "+decimal(p.top/factor)+L" / R "+decimal(p.right/factor)+L" / B "+decimal(p.bottom/factor));
        SendMessageW(control(id::Sizing),CB_SETCURSEL,int(draft.sizing),0);
        SendMessageW(control(id::Unit),CB_SETCURSEL,draft.unit,0);
        SendMessageW(control(id::Rotation),CB_SETCURSEL,int(draft.rotation),0);
        SendMessageW(control(id::Density),CB_SETCURSEL,draft.density+5,0);
        SendMessageW(control(id::Aspect),BM_SETCHECK,draft.aspect?BST_CHECKED:BST_UNCHECKED,0);
        for(int n=0;n<9;++n)SendMessageW(control(id::Position+n),BM_SETCHECK,draft.position==n?BST_CHECKED:BST_UNCHECKED,0);
        syncing=false;refresh();
    }
    void changed(int key,int code) {
        if(syncing)return;
        if(key==id::Setup&&code==BN_CLICKED) {
            try {if(chooser)if(auto device=chooser(hwnd)){printer=std::move(*device);fill();}}
            catch(...){set(id::Error,L"プリンタの設定を取得できませんでした。");}return;
        }
        if(key>=id::Position&&key<id::Position+9&&code==BN_CLICKED){draft.position=key-id::Position;fill();return;}
        if(key==id::Aspect&&code==BN_CLICKED){draft.aspect=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;refresh();return;}
        if(code==CBN_SELCHANGE) {
            if(key==id::Unit) {
                const int next=int(SendMessageW(control(key),CB_GETCURSEL,0,0));
                try {read();draft.unit=next;fill();}catch(...){SendMessageW(control(key),CB_SETCURSEL,draft.unit,0);refresh();}return;
            }
            if(key==id::Sizing)draft.sizing=PrintSizing(SendMessageW(control(key),CB_GETCURSEL,0,0));
            if(key==id::Density)draft.density=int(SendMessageW(control(key),CB_GETCURSEL,0,0))-5;
            if(key==id::Rotation)draft.rotation=PrintRotation(SendMessageW(control(key),CB_GETCURSEL,0,0));
            refresh();return;
        }
        if(code==EN_CHANGE) {
            if(draft.aspect&&(key==id::Width||key==id::Height))try {
                const bool rotate=draft.rotation!=PrintRotation::None&&((image.width>image.height&&printer.paper.width-draft.left-draft.right<printer.paper.height-draft.top-draft.bottom)||(image.width<image.height&&printer.paper.width-draft.left-draft.right>printer.paper.height-draft.top-draft.bottom));
                const double ratio=rotate?double(image.height)/image.width:double(image.width)/image.height;
                const double n=number(control(key));syncing=true;set(key==id::Width?id::Height:id::Width,decimal(key==id::Width?n/ratio:n*ratio));syncing=false;
            }catch(...){syncing=false;}
            refresh();
        }
    }
    void paint(const DRAWITEMSTRUCT &draw) {
        const auto dc=draw.hDC;const auto r=draw.rcItem;FillRect(dc,&r,GetSysColorBrush(COLOR_APPWORKSPACE));
        if(!plan)return;
        const int room_w=r.right-r.left-20,room_h=r.bottom-r.top-20;
        const double scale=std::min(room_w/printer.paper.width,room_h/printer.paper.height);
        const int w=std::max(1,int(std::lround(printer.paper.width*scale))),h=std::max(1,int(std::lround(printer.paper.height*scale)));
        std::vector<Pixel> raster(size_t(w)*h,{255,255,255,255});
        const PrintColors colors(draft.density);
        const auto &a=plan->area;const auto &b=plan->image;const auto &source=image.frames.at(frame).pixels;
        const uint32_t sw=plan->quarter_turns%2?image.height:image.width,sh=plan->quarter_turns%2?image.width:image.height;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const double px=(x+.5)/scale,py=(y+.5)/scale;
            if(px<a.x||py<a.y||px>=a.x+a.width||py>=a.y+a.height||px<b.x||py<b.y||px>=b.x+b.width||py>=b.y+b.height)continue;
            const auto ix=std::min(sw-1,uint32_t((px-b.x)/b.width*sw)),iy=std::min(sh-1,uint32_t((py-b.y)/b.height*sh));
            const size_t from=plan->quarter_turns==1?size_t(image.height-1-ix)*image.width+iy:plan->quarter_turns==3?size_t(ix)*image.width+image.width-1-iy:size_t(iy)*image.width+ix;
            raster[size_t(y)*w+x]=colors(source.at(from));
        }
        BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};
        const int x=r.left+(r.right-r.left-w)/2,y=r.top+(r.bottom-r.top-h)/2;
        SetDIBitsToDevice(dc,x,y,w,h,0,0,0,h,raster.data(),&info,DIB_RGB_COLORS);
        const auto pen=CreatePen(PS_DOT,1,RGB(130,130,130));const auto old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc,x+int(a.x*scale),y+int(a.y*scale),x+int((a.x+a.width)*scale),y+int((a.y+a.height)*scale));
        SelectObject(dc,old_pen);SelectObject(dc,old_brush);DeleteObject(pen);
    }
    void initialize() {
        SetWindowTextW(hwnd,L"印刷");
        dpi=GetDpiForWindow(hwnd);font=CreateFontW(-MulDiv(12,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Yu Gothic UI");
        const auto add=[&](int key,const wchar_t *cls,const wchar_t *label,DWORD style,int x,int y,int w,int h) {
            const bool combo=std::wstring(cls)==L"COMBOBOX";
            if(combo)style|=CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;
            const auto child=CreateWindowExW(std::wstring(cls)==L"EDIT"?WS_EX_CLIENTEDGE:0,cls,label,WS_CHILD|WS_VISIBLE|style,
                MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(w,dpi,96),MulDiv(h,dpi,96),hwnd,HMENU(INT_PTR(key)),GetModuleHandleW(nullptr),nullptr);
            SendMessageW(child,WM_SETFONT,WPARAM(font),FALSE);
            if(combo){SetWindowSubclass(child,combo_print,1,0);SendMessageW(child,CB_SETITEMHEIGHT,0,MulDiv(20,dpi,96));SendMessageW(child,CB_SETITEMHEIGHT,WPARAM(-1),MulDiv(20,dpi,96));}
            return child;
        };
        add(IDOK,L"BUTTON",L"印刷(&P)",WS_TABSTOP|BS_DEFPUSHBUTTON,590,572,88,30);
        add(IDCANCEL,L"BUTTON",L"キャンセル",WS_TABSTOP|BS_PUSHBUTTON,690,572,96,30);
        add(600,L"BUTTON",L"プリンタの設定",BS_GROUPBOX,12,8,774,112);
        add(id::Printer,L"STATIC",L"",SS_LEFT,24,32,555,22);add(id::Paper,L"STATIC",L"",SS_LEFT,24,62,555,22);
        add(id::Setup,L"BUTTON",L"プリンタの設定…",WS_TABSTOP|BS_PUSHBUTTON,590,32,180,30);
        add(601,L"STATIC",L"部数",SS_LEFT,590,79,48,22);add(id::Copies,L"EDIT",L"1",WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,640,74,130,26);
        add(602,L"BUTTON",L"余白 / 位置",BS_GROUPBOX,12,128,414,194);
        const wchar_t *labels[]{L"左",L"上",L"右",L"下"};
        for(int n=0;n<4;++n) {
            const int x=24+(n%2)*130,y=154+(n/2)*40;
            add(603+n,L"STATIC",labels[n],SS_LEFT,x,y+5,24,22);add(id::Left+n,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,x+30,y,88,26);
        }
        add(id::Minimum,L"STATIC",L"",SS_LEFT,24,242,388,56);
        const wchar_t *arrows[]{L"↖",L"↑",L"↗",L"←",L"●",L"→",L"↙",L"↓",L"↘"};
        for(int n=0;n<9;++n)add(id::Position+n,L"BUTTON",arrows[n],WS_TABSTOP|BS_RADIOBUTTON|BS_PUSHLIKE,302+(n%3)*34,154+(n/3)*29,32,27);
        add(607,L"BUTTON",L"画像のサイズ",BS_GROUPBOX,12,330,414,162);
        add(id::Sizing,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,24,354,388,180);
        for(const wchar_t *s:{L"印刷領域に収める（縦横比を保持）",L"印刷領域を埋める（はみ出しを切り取る）",L"印刷領域に引き伸ばす",L"画像のDPI値に従う",L"指定サイズ"})SendMessageW(control(id::Sizing),CB_ADDSTRING,0,LPARAM(s));
        add(608,L"STATIC",L"幅",SS_LEFT,24,399,24,22);add(id::Width,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,54,394,136,26);
        add(609,L"STATIC",L"高さ",SS_LEFT,212,399,38,22);add(id::Height,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,260,394,152,26);
        add(id::Aspect,L"BUTTON",L"元の縦横比を保つ",WS_TABSTOP|BS_AUTOCHECKBOX,24,446,388,24);
        add(id::Preview,L"STATIC",L"",SS_OWNERDRAW,440,128,346,364);
        add(610,L"STATIC",L"単位",SS_LEFT,12,510,38,22);add(id::Unit,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,54,505,90,120);
        for(const wchar_t *s:{L"cm",L"inch"})SendMessageW(control(id::Unit),CB_ADDSTRING,0,LPARAM(s));
        add(611,L"STATIC",L"色の濃さ",SS_LEFT,160,510,70,22);add(id::Density,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,234,505,192,220);
        for(int n=-5;n<=5;++n){const auto s=n?std::to_wstring(n)+(n<0?L"（薄く）":L"（濃く）"):L"0（変更しない）";SendMessageW(control(id::Density),CB_ADDSTRING,0,LPARAM(s.c_str()));}
        add(612,L"STATIC",L"ページに合わせて回転",SS_LEFT,440,510,150,22);add(id::Rotation,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,596,505,190,150);
        for(const wchar_t *s:{L"しない",L"左へ90度",L"右へ90度"})SendMessageW(control(id::Rotation),CB_ADDSTRING,0,LPARAM(s));
        add(id::Error,L"STATIC",L"",SS_LEFT,12,543,774,26);
        RECT bounds{0,0,MulDiv(800,dpi,96),MulDiv(616,dpi,96)};AdjustWindowRectExForDpi(&bounds,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,0,dpi);
        RECT parent{};GetWindowRect(GetParent(hwnd),&parent);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
        const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        const int x=std::clamp((parent.left+parent.right-width)/2,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-width));
        const int y=std::clamp((parent.top+parent.bottom-height)/2,monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-height));
        SetWindowPos(hwnd,nullptr,x,y,width,height,SWP_NOZORDER);fill();
    }
    static INT_PTR CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l) {
        auto *self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));
        if(msg==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,LONG_PTR(self));self->syncing=true;self->initialize();apply_ui_theme(h);return TRUE;}
        if(!self)return FALSE;
        try {
            if(msg==WM_MEASUREITEM&&reinterpret_cast<MEASUREITEMSTRUCT*>(l)->CtlType==ODT_COMBOBOX){reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight=MulDiv(20,self->dpi,96);return TRUE;}
            if(msg==WM_DRAWITEM&&reinterpret_cast<DRAWITEMSTRUCT*>(l)->CtlType==ODT_COMBOBOX){self->paint_combo(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
            if(msg==WM_DRAWITEM&&w==id::Preview){self->paint(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
            if(msg==WM_COMMAND) {
                if(LOWORD(w)==IDCANCEL){DestroyWindow(h);return TRUE;}
                if(LOWORD(w)==IDOK){self->refresh();if(self->valid&&self->printer.ready){self->selected=self->draft;DestroyWindow(h);}return TRUE;}
                self->changed(LOWORD(w),HIWORD(w));return TRUE;
            }
            if(msg==WM_CLOSE){DestroyWindow(h);return TRUE;}
        }catch(...){self->set(id::Error,L"印刷設定を処理できませんでした。");EnableWindow(self->control(IDOK),FALSE);}
        return FALSE;
    }
    ~Impl(){if(IsWindow(hwnd))DestroyWindow(hwnd);if(font)DeleteObject(font);}
};
PrintDialog::PrintDialog(const Image &i,size_t f,const ImageMetadata &m,PrintOptions o,PrintDevice p,Chooser c):impl(std::make_unique<Impl>(i,f,m,o,std::move(p),std::move(c))){(void)i.frames.at(f);}
PrintDialog::~PrintDialog()=default;
HWND PrintDialog::create(HWND owner){Template t;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&t.dialog,owner,Impl::proc,LPARAM(impl.get()));if(!h)throw std::runtime_error("Cannot create print dialog");ShowWindow(h,SW_SHOW);return h;}
HWND PrintDialog::window()const{return impl->hwnd;}
HWND PrintDialog::control(int key)const{return impl->control(key);}
std::optional<PrintOptions> PrintDialog::result()const{return impl->selected;}
std::optional<PrintLayout> PrintDialog::layout()const{return impl->plan;}
PrintDevice PrintDialog::device()const{return impl->printer;}
std::optional<PrintOptions> PrintDialog::run(HWND owner) {
    const bool enabled=IsWindowEnabled(owner);if(enabled)EnableWindow(owner,FALSE);
    struct Restore{HWND owner;bool enabled;~Restore(){if(enabled){EnableWindow(owner,TRUE);SetActiveWindow(owner);}}} restore{owner,enabled};
    create(owner);MSG msg{};
    while(IsWindow(window())){const auto result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(!result)PostQuitMessage(int(msg.wParam));break;}if(!IsDialogMessageW(window(),&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    return result();
}
}
