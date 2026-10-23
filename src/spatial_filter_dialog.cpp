#include "gvn/ui_theme.h"
#include "gvn/spatial_filter_dialog.h"
#include <commctrl.h>
#include <windowsx.h>
#include <cmath>
#include <sstream>
namespace gvn {
namespace {
namespace id=filter_control;
struct Template {
    DLGTEMPLATE dialog{};WORD menu{},cls{},title{},points{9};wchar_t face[13]{L"Yu Gothic UI"};
    Template(){dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;dialog.cx=280;dialog.cy=340;}
};
int input(HWND h,bool radius) {
    wchar_t text[32]{};if(!GetWindowTextW(h,text,32))throw std::invalid_argument("Incomplete filter number");
    for(auto p=text;*p;++p)if((*p<L'0'||*p>L'9')&&!(radius&&*p==L'.'))throw std::invalid_argument("Invalid filter number");
    std::wistringstream in(text);double number{};if(!(in>>number)||!(in>>std::ws).eof()||!std::isfinite(number)||number<0||number>10000)throw std::invalid_argument("Invalid filter number");
    const double scaled=radius?number*10:number;if(std::abs(scaled-std::round(scaled))>1e-8)throw std::invalid_argument("Invalid filter precision");return int(std::round(scaled));
}
std::wstring radius_text(int tenths){return std::to_wstring(tenths/10)+L"."+std::to_wstring(tenths%10);}
}
struct SpatialFilterDialog::Impl {
    HWND hwnd{};HFONT font{};UINT dpi{96};bool modal{},initialized{},syncing{},pending{},input_valid{true},failed{},lost{},dragging{};POINT drag{};
    std::shared_ptr<const Image> source;size_t frame{};Rect selection,viewport;Image before;std::optional<Image> after;SpatialFilter draft;Current current;std::optional<SpatialFilter> selected;std::vector<uint32_t> painted;
    HWND control(int key) const{return GetDlgItem(hwnd,key);}
    bool context() const{return !lost&&(!current||current());}
    int count() const{return draft.sharpen?3:2;}
    int value(int n) const{return n==0?draft.radius:n==1?(draft.sharpen?draft.strength:draft.transparency):draft.threshold;}
    int minimum(int n) const{return n==0||(n==1&&draft.sharpen)?1:0;}
    int maximum(int n) const{return n==0?100:n==1?(draft.sharpen?400:99):255;}
    void error(const wchar_t *text,bool invalid=false) {
        pending=false;failed=!invalid;input_valid=!invalid;KillTimer(hwnd,1);after.reset();SetWindowTextW(control(id::Error),text);EnableWindow(control(IDOK),FALSE);InvalidateRect(control(id::Preview),nullptr,FALSE);
    }
    void positions() {
        for(int n=0;n<count();++n){SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,minimum(n),maximum(n),0,value(n)};SetScrollInfo(control(id::ScrollRadius+n),SB_CTL,&info,TRUE);}
    }
    void fill() {
        syncing=true;for(int n=0;n<count();++n)SetWindowTextW(control(id::Radius+n),(n==0?radius_text(value(n)):std::to_wstring(value(n))).c_str());positions();syncing=false;
    }
    void read() {
        auto next=draft;next.radius=input(control(id::Radius),true);if(draft.sharpen){next.strength=input(control(id::Amount),false);next.threshold=input(control(id::Threshold),false);}else next.transparency=input(control(id::Amount),false);
        if(!valid_spatial_filter(next))throw std::invalid_argument("Filter values out of range");draft=next;
    }
    void original() {
        before={};before.width=uint32_t(viewport.w);before.height=uint32_t(viewport.h);before.frames.push_back({std::vector<Pixel>(size_t(viewport.w)*viewport.h),100});
        for(int y=0;y<viewport.h;++y)std::copy_n(source->frames[frame].pixels.begin()+size_t(y+viewport.y)*source->width+viewport.x,viewport.w,before.frames[0].pixels.begin()+size_t(y)*viewport.w);
    }
    bool compute() {
        pending=false;KillTimer(hwnd,1);if(!context()){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");return false;}if(!input_valid)return false;
        try {
            // Copy only a 1:1 viewport and its filter halo, even for a huge source.
            const int halo=int(std::ceil((draft.radius/10.+.17)*3));const auto box=intersect({viewport.x-halo,viewport.y-halo,viewport.w+halo*2,viewport.h+halo*2},{0,0,int(source->width),int(source->height)});
            Image work;work.width=box.w;work.height=box.h;work.frames.push_back({std::vector<Pixel>(size_t(box.w)*box.h),100});
            for(int y=0;y<box.h;++y)std::copy_n(source->frames[frame].pixels.begin()+size_t(y+box.y)*source->width+box.x,box.w,work.frames[0].pixels.begin()+size_t(y)*box.w);
            const auto roi=selection.empty()?box:intersect(selection,box);std::optional<Image> filtered;
            if(!roi.empty())filtered=spatial_filter(work,draft,{roi.x-box.x,roi.y-box.y,roi.w,roi.h});
            after=before;if(filtered)for(int y=0;y<viewport.h;++y)std::copy_n(filtered->frames[0].pixels.begin()+size_t(y+viewport.y-box.y)*box.w+viewport.x-box.x,viewport.w,after->frames[0].pixels.begin()+size_t(y)*viewport.w);
            failed=false;SetWindowTextW(control(id::Error),L"");EnableWindow(control(IDOK),TRUE);InvalidateRect(control(id::Preview),nullptr,FALSE);return true;
        }catch(...){error(L"プレビューを作成できません。入力内容を確認してください。");return false;}
    }
    void schedule() {
        KillTimer(hwnd,1);after.reset();failed=false;pending=false;SetWindowTextW(control(id::Error),L"");
        if(!context()){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");return;}
        EnableWindow(control(IDOK),FALSE);InvalidateRect(control(id::Preview),nullptr,FALSE);if(!input_valid||dragging)return;
        pending=true;if(!SetTimer(hwnd,1,30,nullptr))throw std::runtime_error("Cannot schedule filter preview");
    }
    void changed() {
        if(!initialized||syncing)return;try{read();input_valid=true;positions();schedule();}catch(...){error(draft.sharpen?L"半径0.1～10.0、強さ1～400、しきい値0～255を入力してください。":L"半径0.1～10.0、透明度0～99を入力してください。",true);}
    }
    void pan(int dx,int dy) {
        const int x=std::clamp(viewport.x+dx,0,int(source->width)-viewport.w),y=std::clamp(viewport.y+dy,0,int(source->height)-viewport.h);
        if(viewport.x==x&&viewport.y==y)return;viewport.x=x;viewport.y=y;original();if(initialized)schedule();
    }
    void scroll(int n,WPARAM w) {
        const int code=LOWORD(w);if(code==SB_ENDSCROLL)return;SCROLLINFO info{sizeof(info),SIF_POS|SIF_TRACKPOS};GetScrollInfo(control(id::ScrollRadius+n),SB_CTL,&info);int next=info.nPos;
        if(code==SB_LINELEFT)--next;else if(code==SB_LINERIGHT)++next;else if(code==SB_PAGELEFT)next-=10;else if(code==SB_PAGERIGHT)next+=10;else if(code==SB_TOP)next=minimum(n);else if(code==SB_BOTTOM)next=maximum(n);else if(code==SB_THUMBTRACK)next=info.nTrackPos;else if(code==SB_THUMBPOSITION)next=short(HIWORD(w));else return;
        next=std::clamp(next,minimum(n),maximum(n));SetWindowTextW(control(id::Radius+n),(n==0?radius_text(next):std::to_wstring(next)).c_str());
    }
    void paint(const DRAWITEMSTRUCT &item) {
        const auto &image=!dragging&&after?*after:before;const auto width=item.rcItem.right-item.rcItem.left,height=item.rcItem.bottom-item.rcItem.top;painted.resize(size_t(width)*height);const int tile=std::max(1,MulDiv(8,dpi,96));
        const int left=(width-int(image.width))/2,top=(height-int(image.height))/2;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){const uint8_t gray=((x/tile+y/tile)&1)?192:238;Pixel p{gray,gray,gray,255};if(x>=left&&y>=top&&x<left+int(image.width)&&y<top+int(image.height))p=over(image.frames[0].pixels[size_t(y-top)*image.width+x-left],p);painted[size_t(y)*width+x]=0xff000000u|(uint32_t(p.r)<<16)|(uint32_t(p.g)<<8)|p.b;}
        BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};SetDIBitsToDevice(item.hDC,0,0,width,height,0,0,0,height,painted.data(),&info,DIB_RGB_COLORS);FrameRect(item.hDC,&item.rcItem,GetSysColorBrush(COLOR_WINDOWFRAME));
    }
    static LRESULT CALLBACK preview_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
        auto self=reinterpret_cast<Impl*>(data);
        if(m==WM_LBUTTONDOWN){self->drag={GET_X_LPARAM(l),GET_Y_LPARAM(l)};self->dragging=true;KillTimer(self->hwnd,1);self->pending=false;EnableWindow(self->control(IDOK),FALSE);SetCapture(h);InvalidateRect(h,nullptr,FALSE);return 0;}
        if(m==WM_MOUSEMOVE&&self->dragging){const POINT now{GET_X_LPARAM(l),GET_Y_LPARAM(l)};try{self->pan(self->drag.x-now.x,self->drag.y-now.y);self->drag=now;}catch(...){self->error(L"プレビューを移動できません。");}return 0;}
        if((m==WM_LBUTTONUP||m==WM_CAPTURECHANGED)&&self->dragging){self->dragging=false;if(GetCapture()==h)ReleaseCapture();try{self->schedule();}catch(...){self->error(L"プレビューを更新できません。");}return 0;}
        if(m==WM_SETCURSOR){SetCursor(LoadCursorW(nullptr,IDC_SIZEALL));return TRUE;}return DefSubclassProc(h,m,w,l);
    }
    void set_font() {
        const auto next=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,0,0,L"Yu Gothic UI");if(!next)throw std::runtime_error("Cannot create filter font");
        for(int key:{id::Radius,id::Amount,id::Threshold,id::LabelRadius,id::LabelAmount,id::LabelThreshold,id::Error,id::Hint,IDOK,IDCANCEL})if(control(key))SendMessageW(control(key),WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);if(font)DeleteObject(font);font=next;
    }
    void arrange() {
        const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT area{};GetClientRect(hwnd,&area);const int width=MulDiv(area.right,96,dpi),height=MulDiv(area.bottom,96,dpi);
        const auto move=[&](int key,int x,int y,int w,int h){MoveWindow(control(key),px(x),px(y),px(w),px(h),TRUE);};
        move(id::Preview,16,14,width-32,180);move(id::Hint,16,202,width-32,38);
        for(int n=0;n<count();++n){move(id::LabelRadius+n,16,250+n*48,94,24);move(id::Radius+n,112,246+n*48,66,26);move(id::ScrollRadius+n,188,252+n*48,width-204,18);}
        move(id::Error,16,250+count()*48,width-32,52);move(IDOK,width-196,height-40,82,28);move(IDCANCEL,width-104,height-40,88,28);
    }
    void finish(bool accept) {
        if(accept&&(!input_valid||!compute()))return;if(accept)selected=draft;KillTimer(hwnd,1);KillTimer(hwnd,2);if(modal)EndDialog(hwnd,accept?IDOK:IDCANCEL);else DestroyWindow(hwnd);
    }
    INT_PTR message(UINT m,WPARAM w,LPARAM l) {
        if(m==WM_INITDIALOG) {
            SetWindowTextW(hwnd,draft.sharpen?L"アンシャープマスク":L"ガウスぼかし");dpi=GetDpiForWindow(hwnd);
            const auto add=[&](int key,const wchar_t *cls,const wchar_t *text,DWORD style){const auto h=CreateWindowExW(wcscmp(cls,L"EDIT")==0?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,reinterpret_cast<HMENU>(INT_PTR(key)),GetModuleHandleW(nullptr),nullptr);if(!h)throw std::runtime_error("Cannot create filter control");return h;};
            const auto preview=add(id::Preview,L"STATIC",L"",SS_OWNERDRAW|SS_NOTIFY);if(!SetWindowSubclass(preview,preview_proc,1,reinterpret_cast<DWORD_PTR>(this)))throw std::runtime_error("Cannot initialize filter preview");
            add(id::Hint,L"STATIC",L"プレビューを左ドラッグすると移動します。\n押している間は元画像を表示します。",0);
            for(int n=0;n<count();++n){add(id::LabelRadius+n,L"STATIC",n==0?L"半径(&R)":n==1?(draft.sharpen?L"強さ(&S) %":L"透明度(&S) %"):L"しきい値(&T)",0);const auto edit=add(id::Radius+n,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL);SendMessageW(edit,EM_SETLIMITTEXT,12,0);add(id::ScrollRadius+n,L"SCROLLBAR",L"",SBS_HORZ|WS_TABSTOP);}
            add(id::Error,L"STATIC",L"",0);add(IDOK,L"BUTTON",L"OK",BS_DEFPUSHBUTTON|WS_TABSTOP);add(IDCANCEL,L"BUTTON",L"キャンセル",WS_TABSTOP);set_font();fill();
            const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT area{0,0,px(420),px(draft.sharpen?490:442)};AdjustWindowRectExForDpi(&area,DWORD(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);
            RECT owner{};GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);const auto placed=place_window({owner.left+(owner.right-owner.left-area.right+area.left)/2,owner.top+(owner.bottom-owner.top-area.bottom+area.top)/2,area.right-area.left,area.bottom-area.top},{monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top},false);
            SetWindowPos(hwnd,nullptr,placed.x,placed.y,placed.w,placed.h,SWP_NOZORDER|SWP_NOACTIVATE);arrange();initialized=true;schedule();if(!SetTimer(hwnd,2,80,nullptr))throw std::runtime_error("Cannot monitor filter context");SetFocus(control(id::Radius));return FALSE;
        }
        if(m==WM_DRAWITEM){paint(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
        if(m==WM_COMMAND&&initialized){const auto key=LOWORD(w);if(key==IDCANCEL){finish(false);return TRUE;}if(key==IDOK){finish(true);return TRUE;}if(key>=id::Radius&&key<id::Radius+count()&&HIWORD(w)==EN_CHANGE){changed();return TRUE;}}
        if(m==WM_HSCROLL&&initialized){for(int n=0;n<count();++n)if(reinterpret_cast<HWND>(l)==control(id::ScrollRadius+n)){scroll(n,w);return TRUE;}}
        if(m==WM_TIMER){if(w==1)compute();else if(w==2&&!context()&&!lost){lost=true;error(L"表示画像が変わりました。閉じてから開き直してください。");}return TRUE;}
        if(m==WM_DPICHANGED){dpi=HIWORD(w);const auto r=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);set_font();arrange();return TRUE;}
        if(m==WM_CLOSE){finish(false);return TRUE;}if(m==WM_DESTROY){KillTimer(hwnd,1);KillTimer(hwnd,2);after.reset();initialized=false;return TRUE;}return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));if(m==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}if(!self)return FALSE;
        try{const auto result=self->message(m,w,l);if(m==WM_INITDIALOG||m==WM_DPICHANGED)apply_ui_theme(h);return result;}catch(...){self->error(L"フィルタ画面を開けません。");if(!self->initialized)PostMessageW(h,WM_CLOSE,0,0);return TRUE;}
    }
};
SpatialFilterDialog::SpatialFilterDialog(std::shared_ptr<const Image> source,size_t frame,Rect selection,SpatialFilter initial,Current current):impl(std::make_unique<Impl>()) {
    if(!source||!valid_spatial_filter(initial)||frame>=source->frames.size()||source->frames[frame].pixels.size()!=checked_image_pixels(source->width,source->height))throw std::invalid_argument("Invalid filter preview source");
    impl->source=std::move(source);impl->frame=frame;impl->selection=selection;impl->draft=initial;impl->current=std::move(current);const auto &image=*impl->source;
    const int width=std::min(256u,image.width),height=std::min(160u,image.height);impl->viewport={std::max(0,(int(image.width)-width)/2),std::max(0,(int(image.height)-height)/2),width,height};
    const auto selected=intersect(selection,{0,0,int(image.width),int(image.height)});if(!selected.empty()){impl->viewport.x=std::clamp(selected.x+selected.w/2-width/2,0,int(image.width)-width);impl->viewport.y=std::clamp(selected.y+selected.h/2-height/2,0,int(image.height)-height);}impl->original();
}
SpatialFilterDialog::~SpatialFilterDialog(){if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);if(impl->font)DeleteObject(impl->font);}
std::optional<SpatialFilter> SpatialFilterDialog::show(HWND owner){Template value;impl->modal=true;if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)throw std::runtime_error("Cannot open filter dialog");return impl->selected;}
HWND SpatialFilterDialog::create(HWND owner){Template value;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create filter dialog");return h;}
HWND SpatialFilterDialog::window() const{return impl->hwnd;}HWND SpatialFilterDialog::control(int key) const{return impl->control(key);}
SpatialFilter SpatialFilterDialog::options() const{return impl->draft;}std::optional<SpatialFilter> SpatialFilterDialog::result() const{return impl->selected;}
bool SpatialFilterDialog::ready() const{return impl->initialized&&!impl->pending;}bool SpatialFilterDialog::valid() const{return impl->context()&&impl->input_valid&&!impl->failed;}
const Image &SpatialFilterDialog::original_preview() const{return impl->before;}const Image &SpatialFilterDialog::adjusted_preview() const{return impl->after?*impl->after:impl->before;}
Rect SpatialFilterDialog::viewport() const{return impl->viewport;}void SpatialFilterDialog::pan(int dx,int dy){impl->pan(dx,dy);}
std::optional<SpatialFilter> choose_spatial_filter(HWND owner,std::shared_ptr<const Image> source,size_t frame,Rect rect,SpatialFilter choice,const SpatialFilterDialog::Current &current){SpatialFilterDialog dialog(std::move(source),frame,rect,choice,current);return dialog.show(owner);}
} // namespace gvn
