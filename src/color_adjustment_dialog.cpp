#include "gvn/color_adjustment_dialog.h"
#include <commctrl.h>
#include <windowsx.h>
namespace gvn {
namespace {
namespace id=color_adjust_control;
struct Template {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    Template(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;dialog.cx=400;dialog.cy=350;}
};
int number(HWND h,int low,int high) {
    wchar_t text[16]{};const auto length=GetWindowTextW(h,text,16);if(!length||length>5)throw std::invalid_argument("Invalid adjustment number");
    int at=0,sign=1,value=0;if(text[0]==L'-'){sign=-1;++at;}if(at==length)throw std::invalid_argument("Incomplete adjustment number");
    for(;at<length;++at){if(text[at]<L'0'||text[at]>L'9')throw std::invalid_argument("Invalid adjustment number");value=value*10+text[at]-L'0';}
    value*=sign;if(value<low||value>high)throw std::out_of_range("Adjustment number out of range");return value;
}
}
struct ColorAdjustmentDialog::Impl {
    HWND hwnd{};HFONT font{};UINT dpi{96};bool modal{},initialized{},syncing{},pending{},input_valid{true},failed{},lost{},has_roi{true};
    ColorAdjustment draft;Current current;Image before;std::optional<Image> after;Rect roi;
    std::optional<ColorAdjustment> selected;std::vector<uint32_t> painted;
    HWND control(int key) const{return GetDlgItem(hwnd,key);}
    bool context() const{return !lost&&(!current||current());}
    int minimum(int axis) const{return axis==0?(draft.unified?0:-180):axis==1?(draft.unified?0:-100):-100;}
    int maximum(int axis) const{return axis==0?(draft.unified?360:180):100;}
    void error(const wchar_t *message,bool invalid=false) {
        pending=false;failed=!invalid;input_valid=!invalid;KillTimer(hwnd,1);after.reset();SetWindowTextW(control(id::Error),message);EnableWindow(control(IDOK),FALSE);InvalidateRect(control(id::Adjusted),nullptr,FALSE);
    }
    void read() {
        auto next=draft;next.hue=number(control(id::Hue),minimum(0),maximum(0));next.saturation=number(control(id::Saturation),minimum(1),maximum(1));
        next.lightness=number(control(id::Lightness),-100,100);const auto gray=SendMessageW(control(id::Gray),CB_GETCURSEL,0,0);
        if(gray<0||gray>2)throw std::invalid_argument("Invalid gray method");next.gray=static_cast<GrayMethod>(gray);draft=next;
    }
    void positions() {
        const int values[]{draft.hue,draft.saturation,draft.lightness};
        for(int n=0;n<3;++n){SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,minimum(n),maximum(n),0,values[n]};SetScrollInfo(control(id::ScrollHue+n),SB_CTL,&info,TRUE);}
    }
    void fill() {
        syncing=true;const int values[]{draft.hue,draft.saturation,draft.lightness};
        for(int n=0;n<3;++n)SetWindowTextW(control(id::Hue+n),std::to_wstring(values[n]).c_str());
        SendMessageW(control(id::Unified),BM_SETCHECK,draft.unified?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(control(id::Gray),CB_SETCURSEL,int(draft.gray),0);positions();syncing=false;
    }
    bool compute() {
        pending=false;KillTimer(hwnd,1);
        if(!context()){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");return false;}
        if(!input_valid)return false;
        try{after=has_roi?color_adjustment(before,draft,roi):std::nullopt;failed=false;SetWindowTextW(control(id::Error),L"");EnableWindow(control(IDOK),TRUE);InvalidateRect(control(id::Adjusted),nullptr,FALSE);return true;}
        catch(...){error(L"比較画像を作成できません。入力内容を確認してください。");return false;}
    }
    void schedule() {
        pending=false;failed=false;input_valid=true;KillTimer(hwnd,1);SetWindowTextW(control(id::Error),L"");
        if(!context()){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");return;}
        if(draft.neutral()||!has_roi){after.reset();EnableWindow(control(IDOK),TRUE);InvalidateRect(control(id::Adjusted),nullptr,FALSE);return;}
        pending=true;EnableWindow(control(IDOK),FALSE);if(!SetTimer(hwnd,1,30,nullptr))throw std::runtime_error("Cannot schedule adjustment preview");
    }
    void changed() {
        if(!initialized||syncing)return;
        try{read();positions();schedule();}catch(...){error(L"色合い・鮮やかさ・明るさを範囲内の整数で入力してください。",true);}
    }
    void scroll(int axis,WPARAM w) {
        const int code=LOWORD(w);if(code==SB_ENDSCROLL)return;
        SCROLLINFO info{sizeof(info),SIF_POS|SIF_TRACKPOS};GetScrollInfo(control(id::ScrollHue+axis),SB_CTL,&info);int value=info.nPos;
        if(code==SB_LINELEFT)--value;else if(code==SB_LINERIGHT)++value;else if(code==SB_PAGELEFT)value-=10;else if(code==SB_PAGERIGHT)value+=10;
        else if(code==SB_TOP)value=minimum(axis);else if(code==SB_BOTTOM)value=maximum(axis);
        else if(code==SB_THUMBTRACK)value=info.nTrackPos;else if(code==SB_THUMBPOSITION)value=short(HIWORD(w));else return;
        SetWindowTextW(control(id::Hue+axis),std::to_wstring(std::clamp(value,minimum(axis),maximum(axis))).c_str());
    }
    void paint(const DRAWITEMSTRUCT &item) {
        const auto &image=item.CtlID==id::Adjusted&&after?*after:before;auto area=item.rcItem;
        FillRect(item.hDC,&area,GetSysColorBrush(COLOR_BTNFACE));FrameRect(item.hDC,&area,GetSysColorBrush(COLOR_WINDOWFRAME));InflateRect(&area,-2,-2);
        const auto scale=std::min(double(area.right-area.left)/image.width,double(area.bottom-area.top)/image.height);
        const int width=std::max(1,int(image.width*scale)),height=std::max(1,int(image.height*scale));painted.resize(size_t(width)*height);
        const int tile=std::max(1,MulDiv(8,dpi,96));
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const auto pixel=image.frames[0].pixels[(uint64_t(y)*image.height/height)*image.width+uint64_t(x)*image.width/width];
            const uint8_t gray=((x/tile+y/tile)&1)?192:238;const auto p=over(pixel,{gray,gray,gray,255});painted[size_t(y)*width+x]=0xff000000u|(uint32_t(p.r)<<16)|(uint32_t(p.g)<<8)|p.b;
        }
        BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};
        StretchDIBits(item.hDC,area.left+(area.right-area.left-width)/2,area.top+(area.bottom-area.top-height)/2,width,height,0,0,width,height,painted.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    }
    void paint_gray(const DRAWITEMSTRUCT &item) {
        const auto saved=SaveDC(item.hDC);const bool selected=(item.itemState&ODS_SELECTED)!=0;
        FillRect(item.hDC,&item.rcItem,GetSysColorBrush(selected?COLOR_HIGHLIGHT:COLOR_WINDOW));
        SetTextColor(item.hDC,GetSysColor((item.itemState&ODS_DISABLED)?COLOR_GRAYTEXT:selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT));
        SetBkMode(item.hDC,TRANSPARENT);SelectObject(item.hDC,font?font:GetStockObject(DEFAULT_GUI_FONT));
        if(item.itemID!=static_cast<UINT>(-1)) {
            wchar_t text[32]{};SendMessageW(item.hwndItem,CB_GETLBTEXT,item.itemID,reinterpret_cast<LPARAM>(text));
            auto area=item.rcItem;area.left+=MulDiv(3,dpi,96);DrawTextW(item.hDC,text,-1,&area,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        if(item.itemState&ODS_FOCUS)DrawFocusRect(item.hDC,&item.rcItem);RestoreDC(item.hDC,saved);
    }
    void arrange() {
        const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT area{};GetClientRect(hwnd,&area);const int width=MulDiv(area.right,96,dpi),height=MulDiv(area.bottom,96,dpi),pane=(width-48)/2;
        const auto move=[&](int key,int x,int y,int w,int h){MoveWindow(control(key),px(x),px(y),px(w),px(h),TRUE);};
        move(id::LabelOriginal,16,14,pane,22);move(id::LabelAdjusted,32+pane,14,pane,22);move(id::Original,16,42,pane,200);move(id::Adjusted,32+pane,42,pane,200);
        for(int n=0;n<3;++n){move(id::LabelHue+n,16,262+n*40,90,24);move(id::Hue+n,110,258+n*40,64,26);move(id::ResetHue+n,180,258+n*40,60,26);move(id::ScrollHue+n,250,264+n*40,width-266,18);}
        move(id::Unified,16,386,224,24);move(id::LabelGray,250,388,140,22);move(id::Gray,396,384,width-412,180);
        move(id::Alpha,16,420,width-32,24);move(id::Error,16,448,width-32,height-494);move(IDOK,width-196,height-40,82,28);move(IDCANCEL,width-104,height-40,88,28);
    }
    void set_font() {
        const auto next=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Yu Gothic UI");if(!next)throw std::runtime_error("Cannot create adjustment font");
        for(int key:{id::Hue,id::Saturation,id::Lightness,id::ResetHue,id::ResetSaturation,id::ResetLightness,id::Unified,id::Gray,id::Error,id::Alpha,id::LabelHue,id::LabelSaturation,id::LabelLightness,id::LabelOriginal,id::LabelAdjusted,id::LabelGray,IDOK,IDCANCEL})SendMessageW(control(key),WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        SendMessageW(control(id::Gray),CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),MulDiv(20,dpi,96));SendMessageW(control(id::Gray),CB_SETITEMHEIGHT,0,MulDiv(20,dpi,96));
        if(font)DeleteObject(font);font=next;
    }
    void finish(bool accept) {
        if(accept&&(!input_valid||!compute()))return;if(accept)selected=draft;KillTimer(hwnd,1);KillTimer(hwnd,2);
        if(modal)EndDialog(hwnd,accept?IDOK:IDCANCEL);else DestroyWindow(hwnd);
    }
    INT_PTR message(UINT m,WPARAM w,LPARAM l) {
        if(m==WM_INITDIALOG) {
            SetWindowTextW(hwnd,L"色合い調整");dpi=GetDpiForWindow(hwnd);
            const auto add=[&](int key,const wchar_t *cls,const wchar_t *text,DWORD style){const auto h=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,reinterpret_cast<HMENU>(INT_PTR(key)),GetModuleHandleW(nullptr),nullptr);if(!h)throw std::runtime_error("Cannot create adjustment control");return h;};
            add(id::LabelOriginal,L"STATIC",L"元の画像",0);add(id::LabelAdjusted,L"STATIC",L"処理後の画像",0);add(id::Original,L"STATIC",L"",SS_OWNERDRAW);add(id::Adjusted,L"STATIC",L"",SS_OWNERDRAW);
            for(int n=0;n<3;++n){add(id::LabelHue+n,L"STATIC",n==0?L"色合い(&H)":n==1?L"鮮やかさ(&S)":L"明るさ(&L)",0);const auto h=add(id::Hue+n,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL);SendMessageW(h,EM_SETLIMITTEXT,5,0);add(id::ResetHue+n,L"BUTTON",L"<< 0",WS_TABSTOP);add(id::ScrollHue+n,L"SCROLLBAR",L"",SBS_HORZ|WS_TABSTOP);}
            add(id::Unified,L"BUTTON",L"色彩を統一(&U)",BS_AUTOCHECKBOX|WS_TABSTOP);add(id::LabelGray,L"STATIC",L"グレイ化の方法(&G)",0);const auto combo=add(id::Gray,L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL|WS_TABSTOP);
            for(const auto *text:{L"中間値",L"平均値",L"輝度値"})SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
            add(id::Alpha,L"STATIC",L"Alphaは維持します。比較表示は縮小プレビューです。",0);add(id::Error,L"STATIC",L"",0);add(IDOK,L"BUTTON",L"OK",BS_DEFPUSHBUTTON|WS_TABSTOP);add(IDCANCEL,L"BUTTON",L"キャンセル",WS_TABSTOP);set_font();fill();
            const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT area{0,0,px(620),px(520)};AdjustWindowRectExForDpi(&area,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);
            RECT owner{};GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
            const auto placed=place_window({owner.left+(owner.right-owner.left-area.right+area.left)/2,owner.top+(owner.bottom-owner.top-area.bottom+area.top)/2,area.right-area.left,area.bottom-area.top},
                {monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top},false);
            SetWindowPos(hwnd,nullptr,placed.x,placed.y,placed.w,placed.h,SWP_NOZORDER|SWP_NOACTIVATE);arrange();initialized=true;schedule();if(!SetTimer(hwnd,2,80,nullptr))throw std::runtime_error("Cannot monitor adjustment context");SetFocus(control(id::Hue));return FALSE;
        }
        if(m==WM_MEASUREITEM&&reinterpret_cast<MEASUREITEMSTRUCT*>(l)->CtlType==ODT_COMBOBOX){reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight=MulDiv(20,dpi,96);return TRUE;}
        if(m==WM_DRAWITEM){const auto &item=*reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item.CtlType==ODT_COMBOBOX)paint_gray(item);else paint(item);return TRUE;}
        if(m==WM_COMMAND&&initialized) {
            const auto key=LOWORD(w),event=HIWORD(w);if(key==IDCANCEL){finish(false);return TRUE;}if(key==IDOK){finish(true);return TRUE;}
            if(key>=id::Hue&&key<=id::Lightness&&event==EN_CHANGE){changed();return TRUE;}
            if(key>=id::ResetHue&&key<=id::ResetLightness){SetWindowTextW(control(id::Hue+key-id::ResetHue),L"0");return TRUE;}
            if(key==id::Unified){draft.unified=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;draft.hue=0;draft.saturation=draft.unified?25:0;draft.lightness=0;fill();schedule();return TRUE;}
            if(key==id::Gray&&event==CBN_SELCHANGE){changed();return TRUE;}
        }
        if(m==WM_HSCROLL&&initialized){for(int n=0;n<3;++n)if(reinterpret_cast<HWND>(l)==control(id::ScrollHue+n)){scroll(n,w);return TRUE;}}
        if(m==WM_TIMER){if(w==1)compute();else if(w==2&&!context()&&!lost){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");}return TRUE;}
        if(m==WM_DPICHANGED){dpi=HIWORD(w);const auto area=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,area->left,area->top,area->right-area->left,area->bottom-area->top,SWP_NOZORDER|SWP_NOACTIVATE);set_font();arrange();return TRUE;}
        if(m==WM_CLOSE){finish(false);return TRUE;}if(m==WM_DESTROY){KillTimer(hwnd,1);KillTimer(hwnd,2);after.reset();initialized=false;return TRUE;}return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));if(m==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}if(!self)return FALSE;
        try{return self->message(m,w,l);}catch(...){self->error(L"色合い調整を実行できません。");if(!self->initialized)PostMessageW(h,WM_CLOSE,0,0);return TRUE;}
    }
};
ColorAdjustmentDialog::ColorAdjustmentDialog(std::shared_ptr<const Image> source,size_t frame,Rect selection,ColorAdjustment initial,Current current):impl(std::make_unique<Impl>()) {
    if(!source||!valid_color_adjustment(initial)||frame>=source->frames.size()||source->frames[frame].pixels.size()!=checked_image_pixels(source->width,source->height))throw std::invalid_argument("Invalid adjustment preview source");
    impl->draft=initial;impl->current=std::move(current);const double scale=std::min({1.,256./source->width,192./source->height});auto &image=impl->before;
    image.width=std::max(1u,uint32_t(source->width*scale));image.height=std::max(1u,uint32_t(source->height*scale));image.frames.push_back({std::vector<Pixel>(size_t(image.width)*image.height),100});
    for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x)image.frames[0].pixels[size_t(y)*image.width+x]=source->frames[frame].pixels[(uint64_t(y)*source->height/image.height)*source->width+uint64_t(x)*source->width/image.width];
    if(selection.empty())impl->roi={0,0,int(image.width),int(image.height)};
    else {
        const auto clipped=intersect(selection,{0,0,int(source->width),int(source->height)});
        const auto ceil_scaled=[](uint64_t value,uint32_t to,uint32_t from){return int((value*to+from-1)/from);};
        const auto left=ceil_scaled(clipped.x,image.width,source->width),top=ceil_scaled(clipped.y,image.height,source->height);
        impl->roi={left,top,ceil_scaled(uint64_t(clipped.x)+clipped.w,image.width,source->width)-left,ceil_scaled(uint64_t(clipped.y)+clipped.h,image.height,source->height)-top};impl->has_roi=!impl->roi.empty();
    }
}
ColorAdjustmentDialog::~ColorAdjustmentDialog(){if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);if(impl->font)DeleteObject(impl->font);}
std::optional<ColorAdjustment> ColorAdjustmentDialog::show(HWND owner){Template value;impl->modal=true;if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)throw std::runtime_error("Cannot open adjustment dialog");return impl->selected;}
HWND ColorAdjustmentDialog::create(HWND owner){Template value;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create adjustment dialog");return h;}
HWND ColorAdjustmentDialog::window() const{return impl->hwnd;}
HWND ColorAdjustmentDialog::control(int key) const{return impl->control(key);}
ColorAdjustment ColorAdjustmentDialog::options() const{return impl->draft;}
std::optional<ColorAdjustment> ColorAdjustmentDialog::result() const{return impl->selected;}
bool ColorAdjustmentDialog::ready() const{return impl->initialized&&!impl->pending;}
bool ColorAdjustmentDialog::valid() const{return impl->context()&&impl->input_valid&&!impl->failed;}
const Image &ColorAdjustmentDialog::original_preview() const{return impl->before;}
const Image &ColorAdjustmentDialog::adjusted_preview() const{return impl->after?*impl->after:impl->before;}
Rect ColorAdjustmentDialog::preview_selection() const{return impl->roi;}
std::optional<ColorAdjustment> choose_color_adjustment(HWND owner,std::shared_ptr<const Image> source,size_t frame,Rect roi,ColorAdjustment options,const ColorAdjustmentDialog::Current &current){ColorAdjustmentDialog dialog(std::move(source),frame,roi,options,current);return dialog.show(owner);}
} // namespace gvn
