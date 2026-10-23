#include "gvn/color_dialog.h"
#include "gvn/editor_chrome.h"
#include "gvn/ui.h"
#include "gvn/ui_theme.h"
#include <commdlg.h>
#include <windowsx.h>
#include <sstream>
#include <iomanip>
#include <cmath>
namespace gvn {
namespace {
using namespace editor_id;
struct AnnotationLayout {POINT base{},outline{},shadow{},details{};int text_y{},height{};};
AnnotationLayout annotation_layout(int width,EditorPanel mode,bool details){
    const bool text=mode==EditorPanel::Text;int x=8,y=6,limit=width-8;
    if(text){if(width<680)y+=34;else limit-=204;}
    const auto group=[&](int w){if(x>8&&x+w>limit){x=8;y+=32;limit=width-8;}const POINT at{x,y};x+=w+12;return at;};
    AnnotationLayout out;out.base=group(text?300:230);out.outline=group(details?150:44);out.shadow=group(details?300:44);out.details=group(80);
    out.text_y=y+32;out.height=text?out.text_y+40:y+32;return out;
}
std::wstring numeric(double value){std::wostringstream out;out<<std::fixed<<std::setprecision(2)<<value;
    auto s=out.str();while(s.size()>1&&s.back()==L'0')s.pop_back();if(s.back()==L'.')s.pop_back();return s;}
}
EditorChrome::~EditorChrome(){if(font)DeleteObject(font);if(drag_icon)DestroyIcon(drag_icon);}
void EditorChrome::position(HWND child,int x,int y,int w,int h){
    const RECT next{x,y,x+w,y+h};const auto previous=placements.find(child);
    const bool moved=previous==placements.end()||!EqualRect(&previous->second,&next);
    const bool visible=(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)!=0;
    if(moved||!visible){SetWindowPos(child,nullptr,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW|(moved?0:SWP_NOMOVE|SWP_NOSIZE));placements[child]=next;}
}
bool EditorChrome::owns(HWND child) const {return child&&(child==top||child==bottom||IsChild(top,child)||IsChild(bottom,child));}
void EditorChrome::put(int id,const std::wstring &value){if(text(id)!=value)SetWindowTextW(control(id),value.c_str());}
std::wstring EditorChrome::text(int id) const {const auto child=control(id);const int n=GetWindowTextLengthW(child);std::wstring out(size_t(n)+1,L'\0');
    GetWindowTextW(child,out.data(),n+1);out.resize(n);return out;}
void EditorChrome::create(HWND parent,bool editing){
    owner=parent;
    if(top&&bottom&&(!editing||control(Width)))return;
    WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    cls.hbrBackground=ui_brush(ui_palette().surface);cls.lpszClassName=L"GVN.EditorChrome";RegisterClassW(&cls);
    if(!top)top=CreateWindowExW(0,cls.lpszClassName,L"編集オプション",WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,parent,nullptr,cls.hInstance,this);
    if(!bottom)bottom=CreateWindowExW(0,cls.lpszClassName,L"ファイルをドラッグ",WS_CHILD|WS_CLIPCHILDREN,0,0,1,1,parent,nullptr,cls.hInstance,this);
    auto add=[&](int id,const wchar_t *kind,const wchar_t *value,DWORD style,HWND host){
        auto child=CreateWindowExW(0,kind,value,WS_CHILD|WS_TABSTOP|style|(wcscmp(kind,L"EDIT")==0?WS_BORDER:0),0,0,1,1,host,reinterpret_cast<HMENU>(INT_PTR(id)),cls.hInstance,nullptr);
        controls[id]=child;apply_ui_theme(child,false);if(font)SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);return child;};
    if(!control(Name)){
        add(Name,L"EDIT",export_default.empty()?L"画像":export_default.c_str(),ES_AUTOHSCROLL,bottom);SendMessageW(control(Name),EM_SETLIMITTEXT,120,0);
        add(Format,L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,bottom);
        for(auto label:{L".png",L".jpg",L".bmp",L".gif"})SendMessageW(control(Format),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(control(Format),CB_SETCURSEL,0,0);
        auto drag=add(Drag,L"BUTTON",L"ドラッグ",BS_OWNERDRAW,bottom);
        SetWindowSubclass(drag,drag_procedure,1,reinterpret_cast<DWORD_PTR>(this));
    }
    apply_ui_theme(top,false);apply_ui_theme(bottom,false);
    if(!editing||control(Width))return;
    auto field=[&](int id,const wchar_t *label,const wchar_t *value){labels[id]=CreateWindowW(L"STATIC",label,WS_CHILD|SS_CENTERIMAGE,0,0,1,1,top,nullptr,cls.hInstance,nullptr);
        if(font)SendMessageW(labels[id],WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        add(id,L"EDIT",value,ES_AUTOHSCROLL,top);SendMessageW(control(id),EM_SETLIMITTEXT,32,0);};
    add(Color,L"BUTTON",L"色",BS_OWNERDRAW,top);field(Width,L"線幅",L"6");field(Opacity,L"不透明度",L"100");
    add(Font,L"COMBOBOX",L"",CBS_DROPDOWN|WS_VSCROLL,top);
    for(auto value:{L"Yu Gothic UI",L"Meiryo",L"Arial",L"MS UI Gothic"})SendMessageW(control(Font),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
    SetWindowTextW(control(Font),L"Yu Gothic UI");field(TextSize,L"サイズ",L"32");
    add(Text,L"EDIT",L"文字",ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,top);SendMessageW(control(Text),EM_SETLIMITTEXT,4096,0);
    add(Details,L"BUTTON",L"詳細 ▾",BS_AUTOCHECKBOX|BS_PUSHLIKE,top);
    add(Outline,L"BUTTON",L"フチ",BS_AUTOCHECKBOX,top);add(OutlineColor,L"BUTTON",L"色",BS_OWNERDRAW,top);field(OutlineWidth,L"幅",L"2");
    add(Shadow,L"BUTTON",L"影",BS_AUTOCHECKBOX,top);add(ShadowColor,L"BUTTON",L"色",BS_OWNERDRAW,top);
    field(ShadowOffset,L"距離",L"3");field(ShadowBlur,L"ぼかし",L"3");field(ShadowOpacity,L"濃さ %",L"50");
    for(auto [id,label]:{std::pair{X,L"X"},{Y,L"Y"},{W,L"幅"},{H,L"高さ"},{Angle,L"角度 °"},{Scale,L"出力 %"}})field(id,label,id==Scale?L"100":L"0");
    add(Anchor,L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS|WS_VSCROLL,top);
    for(auto label:{L"基準：左上",L"基準：上",L"基準：右上",L"基準：左",L"基準：中央",L"基準：右",L"基準：左下",L"基準：下",L"基準：右下"})SendMessageW(control(Anchor),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendMessageW(control(Anchor),CB_SETCURSEL,4,0);
    add(Apply,L"BUTTON",L"✓ 適用",BS_PUSHBUTTON,top);add(Cancel,L"BUTTON",L"× キャンセル",BS_PUSHBUTTON,top);
    add(Hint,L"STATIC",L"左のツールで編集します",SS_CENTERIMAGE,top);add(Error,L"STATIC",L"",SS_CENTERIMAGE,top);
}
LRESULT CALLBACK EditorChrome::procedure(HWND window,UINT message,WPARAM w,LPARAM l){
    auto self=reinterpret_cast<EditorChrome *>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){self=static_cast<EditorChrome *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
    if(self&&message==WM_COMMAND){if(self->filling)return 0;
        if(LOWORD(w)==Text&&HIWORD(w)==EN_CHANGE){SetTimer(window,1,180,nullptr);return 0;}
        return SendMessageW(self->owner,message,w,l);}
    if(self&&message==WM_TIMER&&w==1){KillTimer(window,1);SendMessageW(self->owner,WM_COMMAND,TextUpdate,0);return 0;}
    if(self&&message==WM_MEASUREITEM&&reinterpret_cast<MEASUREITEMSTRUCT *>(l)->CtlType==ODT_COMBOBOX){
        reinterpret_cast<MEASUREITEMSTRUCT *>(l)->itemHeight=MulDiv(20,GetDpiForWindow(window),96);return TRUE;
    }
    if(self&&message==WM_DRAWITEM&&reinterpret_cast<DRAWITEMSTRUCT *>(l)->CtlType==ODT_COMBOBOX){
        const auto item=reinterpret_cast<DRAWITEMSTRUCT *>(l);auto area=item->rcItem;
        const bool highlight=(item->itemState&ODS_SELECTED)&&!(item->itemState&ODS_COMBOBOXEDIT);
        FillRect(item->hDC,&area,ui_brush(highlight?ui_palette().selected:ui_palette().input));
        SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,(item->itemState&ODS_DISABLED)?ui_palette().muted:ui_palette().text);
        auto selected=item->itemID==UINT(-1)?SendMessageW(item->hwndItem,CB_GETCURSEL,0,0):item->itemID;
        if(selected>=0){const auto count=SendMessageW(item->hwndItem,CB_GETLBTEXTLEN,selected,0);std::wstring text(size_t(std::max<LRESULT>(0,count))+1,L'\0');
            SendMessageW(item->hwndItem,CB_GETLBTEXT,selected,reinterpret_cast<LPARAM>(text.data()));area.left+=3;
            const auto old=SelectObject(item->hDC,self->font);DrawTextW(item->hDC,text.c_str(),-1,&area,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(item->hDC,old);}
        if(item->itemState&ODS_FOCUS)DrawFocusRect(item->hDC,&area);return TRUE;
    }
    if(self&&message==WM_DRAWITEM&&(reinterpret_cast<DRAWITEMSTRUCT *>(l)->CtlID==Color||reinterpret_cast<DRAWITEMSTRUCT *>(l)->CtlID==OutlineColor||reinterpret_cast<DRAWITEMSTRUCT *>(l)->CtlID==ShadowColor)){
        const auto item=reinterpret_cast<DRAWITEMSTRUCT *>(l);auto area=item->rcItem;
        FillRect(item->hDC,&area,ui_brush((item->itemState&ODS_SELECTED)?ui_palette().selected:ui_palette().input));
        FrameRect(item->hDC,&area,ui_brush(ui_palette().border));
        const auto c=item->CtlID==OutlineColor?self->outline_color:item->CtlID==ShadowColor?self->shadow_color:self->color;
        const auto fill=CreateSolidBrush((item->itemState&ODS_DISABLED)?GetSysColor(COLOR_GRAYTEXT):RGB(c.r,c.g,c.b));const auto old=SelectObject(item->hDC,fill);
        const int diameter=std::min(MulDiv(16,GetDpiForWindow(window),96),int(area.bottom-4));
        const int x=(area.right-diameter)/2,y=(area.bottom-diameter)/2;Ellipse(item->hDC,x,y,x+diameter,y+diameter);SelectObject(item->hDC,old);DeleteObject(fill);
        if(item->itemState&ODS_FOCUS){InflateRect(&area,-3,-3);DrawFocusRect(item->hDC,&area);}return TRUE;
    }
    if(self&&message==WM_DRAWITEM&&reinterpret_cast<DRAWITEMSTRUCT *>(l)->CtlID==Drag){
        const auto item=reinterpret_cast<DRAWITEMSTRUCT *>(l);auto area=item->rcItem;
        FillRect(item->hDC,&area,ui_brush(self->drag_hot&&!(item->itemState&ODS_DISABLED)?ui_palette().selected:ui_palette().surface));
        const auto dpi=GetDpiForWindow(window);const int icon=MulDiv(18,dpi,96);
        if(!self->drag_icon)self->drag_icon=command_icon(ArmDrag,icon);
        const auto focus=area;const bool compact=area.right-area.left<MulDiv(90,dpi,96);
        DrawIconEx(item->hDC,compact?(area.right-icon)/2:area.left+MulDiv(12,dpi,96),(area.bottom-icon)/2,self->drag_icon,icon,icon,0,nullptr,DI_NORMAL);
        area.left+=MulDiv(36,dpi,96);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,(item->itemState&ODS_DISABLED)?ui_palette().muted:ui_palette().text);
        const auto old=SelectObject(item->hDC,self->font);if(!compact)DrawTextW(item->hDC,L"ドラッグ",-1,&area,DT_LEFT|DT_VCENTER|DT_SINGLELINE);SelectObject(item->hDC,old);
        if(item->itemState&ODS_FOCUS){area=focus;InflateRect(&area,-3,-3);DrawFocusRect(item->hDC,&area);}return TRUE;
    }
    return DefWindowProcW(window,message,w,l);
}
LRESULT CALLBACK EditorChrome::drag_procedure(HWND child,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    auto self=reinterpret_cast<EditorChrome *>(data);POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
    if(message==WM_MOUSEMOVE&&!self->drag_hot){self->drag_hot=true;TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,child,0};TrackMouseEvent(&track);InvalidateRect(child,nullptr,FALSE);}
    if(message==WM_MOUSELEAVE){self->drag_hot=false;InvalidateRect(child,nullptr,FALSE);}
    if(message==WM_LBUTTONDOWN&&IsWindowEnabled(child)){SetFocus(child);SetCapture(child);self->drag_start=p;self->drag_pending=GetCapture()==child;return 0;}
    if(message==WM_MOUSEMOVE&&self->drag_pending){if(!(w&MK_LBUTTON)){self->drag_pending=false;ReleaseCapture();return 0;}
        const auto dpi=GetDpiForWindow(child);
        if(std::abs(p.x-self->drag_start.x)>=GetSystemMetricsForDpi(SM_CXDRAG,dpi)||std::abs(p.y-self->drag_start.y)>=GetSystemMetricsForDpi(SM_CYDRAG,dpi)){
            self->drag_pending=false;ReleaseCapture();SendMessageW(self->owner,WM_COMMAND,Drag,0);}return 0;}
    if(message==WM_LBUTTONUP||message==WM_CANCELMODE||message==WM_CAPTURECHANGED){self->drag_pending=false;if(GetCapture()==child)ReleaseCapture();}
    if(message==WM_SETCURSOR&&IsWindowEnabled(child)){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}
    return DefSubclassProc(child,message,w,l);
}
int EditorChrome::top_height(int width,UINT dpi,EditorPanel mode,bool applying,bool details){
    const int logical=MulDiv(width,96,dpi);if(mode==EditorPanel::None)return 0;
    if(mode==EditorPanel::Pen||mode==EditorPanel::Text)return MulDiv(annotation_layout(logical,mode,details).height,dpi,96);
    const bool narrow=logical<640,geometry=mode==EditorPanel::Crop||mode==EditorPanel::Transform;
    const int offset=applying&&narrow?34:0;return MulDiv(geometry?(narrow?126+offset:84):38,dpi,96);
}
int EditorChrome::layout_top(HWND parent,int width,int y,UINT new_dpi,bool visible,EditorPanel mode,bool applying){
    if(!visible||mode==EditorPanel::None){if(top)ShowWindow(top,SW_HIDE);return 0;}create(parent);panel=mode;
    if(dpi!=new_dpi){if(font)DeleteObject(font);dpi=new_dpi;font=CreateFontW(-MulDiv(12,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,DEFAULT_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
        for(auto [id,c]:controls)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);for(auto [id,c]:labels)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
    const auto px=[&](int n){return MulDiv(n,dpi,96);};const int logical=MulDiv(width,96,dpi);
    if(anchor_dpi!=new_dpi){SendMessageW(control(Anchor),CB_SETITEMHEIGHT,WPARAM(-1),px(20));SendMessageW(control(Anchor),CB_SETITEMHEIGHT,0,px(20));anchor_dpi=new_dpi;}
    const bool narrow=logical<640;
    const int offset=applying&&narrow?34:0;
    const int height=measure_top(width,dpi,mode,applying);
    position(top,0,y,width,height);std::vector<HWND> shown;shown.reserve(controls.size()+labels.size());
    auto place=[&](int id,int x,int row,int w,int h=26){auto c=control(id);shown.push_back(c);position(c,px(x),px(row),px(w),px(h));};
    auto field=[&](int id,int x,int row,int w){shown.push_back(labels[id]);position(labels[id],px(x),px(row),px(56),px(26));place(id,x+56,row,w);};
    const int buttons=204;
    if(applying){place(Cancel,std::max(4,logical-buttons),6,112);place(Apply,std::max(120,logical-84),6,78);}
    if(mode==EditorPanel::Pen||mode==EditorPanel::Text){
        const auto grid=annotation_layout(logical,mode,details_open);
        const auto compact=[&](int id,int x,int y,int label_width,int edit_width){shown.push_back(labels[id]);position(labels[id],px(x),px(y),px(label_width),px(26));place(id,x+label_width,y,edit_width);};
        const auto [x,by]=grid.base;
        if(mode==EditorPanel::Text){place(Font,x,by,176,180);compact(TextSize,x+184,by,36,44);place(Color,x+272,by,28);place(Text,8,grid.text_y,std::max(80,std::min(960,logical-16)),34);}
        else {place(Color,x,by,28);compact(Width,x+40,by,28,44);compact(Opacity,x+124,by,48,44);}
        const auto [ox,oy]=grid.outline;place(Outline,ox,oy,44);if(details_open){place(OutlineColor,ox+48,oy,28);compact(OutlineWidth,ox+84,oy,26,40);}
        const auto [sx,sy]=grid.shadow;place(Shadow,sx,sy,44);if(details_open){place(ShadowColor,sx+48,sy,28);compact(ShadowOffset,sx+84,sy,26,36);compact(ShadowBlur,sx+152,sy,36,36);compact(ShadowOpacity,sx+230,sy,34,36);}
        place(Details,grid.details.x,grid.details.y,80);
        sync_effect_controls();
    }else if(mode==EditorPanel::Crop||mode==EditorPanel::Transform){
        field(X,8,6+offset,60);field(Y,136,6+offset,60);field(W,8,43+offset,60);field(H,136,43+offset,60);
        if(narrow){field(Angle,8,80+offset,60);if(mode==EditorPanel::Crop)field(Scale,136,80+offset,60);place(Anchor,264,43+offset,std::max(80,logical-270),220);}
        else {field(Angle,264,43,60);if(mode==EditorPanel::Crop)field(Scale,392,43,60);place(Anchor,264,6,122,220);}
    }else {place(Hint,8,8,std::max(80,logical-16));}
    const auto hide=[&](HWND c){if((GetWindowLongPtrW(c,GWL_STYLE)&WS_VISIBLE)&&std::find(shown.begin(),shown.end(),c)==shown.end())ShowWindow(c,SW_HIDE);};
    for(auto [id,c]:controls)if(GetParent(c)==top)hide(c);for(auto [id,c]:labels)hide(c);
    return height;
}
void EditorChrome::layout_status(HWND parent,HWND status,const RECT &bounds,UINT new_dpi,bool visible,bool enabled){
    if(!visible){if(bottom)ShowWindow(bottom,SW_HIDE);return;}create(parent,false);
    if(palette_generation!=ui_theme_generation()){palette_generation=ui_theme_generation();if(drag_icon){DestroyIcon(drag_icon);drag_icon=nullptr;}InvalidateRect(bottom,nullptr,TRUE);}
    if(GetParent(bottom)!=status){SetParent(bottom,status);placements.erase(bottom);}
    if(dpi!=new_dpi||!font){if(font)DeleteObject(font);if(drag_icon){DestroyIcon(drag_icon);drag_icon=nullptr;}dpi=new_dpi;font=CreateFontW(-MulDiv(12,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,DEFAULT_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
        for(auto [id,c]:controls)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);for(auto [id,c]:labels)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);}
    auto px=[&](int n){return MulDiv(n,new_dpi,96);};const int width=std::max(1L,bounds.right-bounds.left),height=std::max(1L,bounds.bottom-bounds.top);
    const int format_width=std::min(px(68),std::max(px(48),width/5)),drag_width=width<px(260)?px(24):px(104),gap=width<px(260)?px(3):px(6),name_width=std::max(1,width-format_width-drag_width-2*gap);
    if(format_dpi!=new_dpi){SendMessageW(control(Format),CB_SETITEMHEIGHT,WPARAM(-1),px(18));SendMessageW(control(Format),CB_SETITEMHEIGHT,0,px(18));format_dpi=new_dpi;}
    position(bottom,bounds.left,bounds.top,width,height);
    const int y=std::max(0,(height-px(24))/2);
    position(control(Drag),0,y,drag_width,px(24));position(control(Name),drag_width+gap,y,name_width,px(24));position(control(Format),drag_width+name_width+2*gap,y,format_width,px(180));
    for(int id:{Name,Format,Drag})if(bool(IsWindowEnabled(control(id)))!=enabled)EnableWindow(control(id),enabled);
}
void EditorChrome::sync_effect_controls(){
    const bool outline=SendMessageW(control(Outline),BM_GETCHECK,0,0)==BST_CHECKED,shadow=SendMessageW(control(Shadow),BM_GETCHECK,0,0)==BST_CHECKED;
    for(int id:{OutlineColor,OutlineWidth,ShadowColor,ShadowOffset,ShadowBlur,ShadowOpacity}){
        const bool enabled=id==OutlineColor||id==OutlineWidth?outline:shadow;
        if(bool(IsWindowEnabled(control(id)))!=enabled)EnableWindow(control(id),enabled);
        if(labels.contains(id)&&bool(IsWindowEnabled(labels[id]))!=enabled)EnableWindow(labels[id],enabled);
    }
}
bool EditorChrome::notification(int id,int code){
    if(id==Details&&code==BN_CLICKED){details_open=SendMessageW(control(Details),BM_GETCHECK,0,0)==BST_CHECKED;return true;}
    if((id==Color||id==OutlineColor||id==ShadowColor)&&code==BN_CLICKED){auto &c=id==OutlineColor?outline_color:id==ShadowColor?shadow_color:color;
        if(const auto choice=choose_color(owner,(uint32_t(c.r)<<16)|(uint32_t(c.g)<<8)|c.b)){c={uint8_t(*choice>>16),uint8_t(*choice>>8),uint8_t(*choice),c.a};InvalidateRect(control(id),nullptr,TRUE);}return true;}
    if((id==Outline||id==Shadow)&&code==BN_CLICKED){sync_effect_controls();return true;}
    return false;
}
double EditorChrome::number(int id,double minimum,double maximum) const {const auto value=text(id);wchar_t *end{};const auto n=wcstod(value.c_str(),&end);
    while(end&&iswspace(*end))++end;if(value.empty()||!end||*end||!std::isfinite(n)||n<minimum||n>maximum)throw std::invalid_argument("数値の範囲を確認してください。");return n;}
AnnotationEffects EditorChrome::effects() const {AnnotationEffects e;e.outline=SendMessageW(control(Outline),BM_GETCHECK,0,0)==BST_CHECKED;e.shadow=SendMessageW(control(Shadow),BM_GETCHECK,0,0)==BST_CHECKED;
    if(e.outline){e.outline_width=number(OutlineWidth,1,16);e.outline_color=outline_color;}
    if(e.shadow){e.shadow_color=shadow_color;e.shadow_color.a=uint8_t(std::lround(number(ShadowOpacity,0,100)*2.55));e.shadow_offset=int(std::lround(number(ShadowOffset,-64,64)));e.shadow_blur=int(std::lround(number(ShadowBlur,0,32)));}return e;}
PenStyle EditorChrome::pen() const {auto p=color;p.a=uint8_t(std::lround(number(Opacity,0,100)*2.55));return {number(Width,1,512),p,effects()};}
Image EditorChrome::text_image(const EditMemory &memory) const {return make_text_stamp(text(Text),text(Font),number(TextSize,1,512),color,memory,effects());}
void EditorChrome::bounds(const FloatingBox &box,double scale){if(!top)return;filling=true;
    for(auto [id,value]:{std::pair{X,box.cx-box.width/2},{Y,box.cy-box.height/2},{W,box.width},{H,box.height},{Angle,box.angle},{Scale,scale*100}})
        if(GetFocus()!=control(id))put(id,numeric(value));filling=false;}
FloatingBox EditorChrome::edited_bounds(const FloatingBox &initial) const {
    const auto anchor=int(SendMessageW(control(Anchor),CB_GETCURSEL,0,0));auto box=resize_box_at_anchor(initial,number(W,1,65535),number(H,1,65535),number(Angle,-360000,360000),anchor);
    // X/Y are the axis-aligned center-offset coordinates shown in the controls.
    if(std::abs(number(X,-1e7,1e7)-(initial.cx-initial.width/2))>.011)box.cx=number(X,-1e7,1e7)+box.width/2;
    if(std::abs(number(Y,-1e7,1e7)-(initial.cy-initial.height/2))>.011)box.cy=number(Y,-1e7,1e7)+box.height/2;
    return box;
}
std::wstring EditorChrome::timestamp_name(const SYSTEMTIME &when){
    wchar_t name[40]{};swprintf_s(name,L"image%04u%02u%02u_%02u%02u%02u",unsigned(when.wYear),unsigned(when.wMonth),unsigned(when.wDay),unsigned(when.wHour),unsigned(when.wMinute),unsigned(when.wSecond));return name;
}
void EditorChrome::reset_export_name(){
    SYSTEMTIME now{};GetLocalTime(&now);export_default=timestamp_name(now);
    if(control(Name)){filling=true;put(Name,export_default);filling=false;}
}
SaveFormat EditorChrome::export_format() const {const auto value=SendMessageW(control(Format),CB_GETCURSEL,0,0);return value==1?SaveFormat::Jpeg:value==2?SaveFormat::Bmp:value==3?SaveFormat::Gif:SaveFormat::Png;}
void EditorChrome::error(const std::wstring &value){if(!top)return;put(Error,value);if(!value.empty()){RECT r{};GetClientRect(top,&r);SetWindowPos(control(Error),HWND_TOP,4,r.bottom-18,r.right-8,18,SWP_NOACTIVATE|SWP_SHOWWINDOW);}else ShowWindow(control(Error),SW_HIDE);}
} // namespace gvn
