#include "gvn/color_dialog.h"
#include "gvn/ui_theme.h"
#include "gvn/resize_dialog.h"
#include "gvn/display_quality.h"
#include "gvn/core.h"
#include <commctrl.h>
#include <commdlg.h>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <utility>

namespace gvn {
namespace {
namespace id=resize_control;
struct DialogTemplate {
    DLGTEMPLATE dialog{};
    WORD menu{},cls{},title{},points{9};
    wchar_t face[13]{L"Yu Gothic UI"};
    DialogTemplate() {
        dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_SETFONT;
        dialog.cx=300;dialog.cy=270;
    }
};
std::wstring text(HWND h) {
    std::wstring value(static_cast<size_t>(GetWindowTextLengthW(h))+1,L'\0');
    GetWindowTextW(h,value.data(),static_cast<int>(value.size()));value.pop_back();return value;
}
uint32_t dimension(double value) {
    if(!std::isfinite(value)||value<.5||value>65535.499999)
        throw std::invalid_argument("Image dimensions must be between1and65535pixels");
    return static_cast<uint32_t>(std::lround(value));
}
uint32_t proportional(uint32_t value,uint32_t original,uint32_t other) {
    const auto result=(uint64_t(value)*other+original/2)/original;
    if(result>65535)throw std::invalid_argument("Proportional image size exceeds65535pixels");
    return static_cast<uint32_t>(std::max<uint64_t>(1,result));
}
uint32_t integer(HWND h) {
    const auto value=text(h);
    if(value.empty()||!std::all_of(value.begin(),value.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';}))
        throw std::invalid_argument("Enter an integer size");
    size_t used{};const auto n=std::stoull(value,&used);
    if(used!=value.size()||n<1||n>65535)throw std::invalid_argument("Image dimensions must be between1and65535pixels");
    return static_cast<uint32_t>(n);
}
double percentage(HWND h) {
    const auto value=text(h);
    if(value.empty()||value.find_first_not_of(L"0123456789.")!=std::wstring::npos)
        throw std::invalid_argument("Enter a positive percentage");
    size_t used{};const auto n=std::stod(value,&used);
    if(used!=value.size()||!std::isfinite(n)||n<=0)throw std::invalid_argument("Enter a positive percentage");
    return n;
}
std::wstring percent(uint32_t n,uint32_t original) {
    std::wostringstream out;out<<std::fixed<<std::setprecision(3)<<100.0*n/original;return out.str();
}
std::wstring bytes(uint32_t w,uint32_t h) {
    const auto count=uint64_t(w)*h*4;
    std::wostringstream out;out<<w<<L" × "<<h<<L" px  /  "<<std::fixed<<std::setprecision(2)
                              <<double(count)/(1024*1024)<<L" MiB (RGBA)";return out.str();
}
}
namespace {
std::wstring decimal(double value) {
    std::wostringstream out;out<<std::fixed<<std::setprecision(9)<<value;auto s=out.str();
    while(s.size()>1&&s.back()==L'0')s.pop_back();if(s.back()==L'.')s.pop_back();return s;
}
double inches(int unit){return unit==2?1.:unit==3?1./2.54:1./25.4;}
}
struct ResizeDialog::Impl {
    uint32_t original_width{},original_height{};
    ResizeChoice draft;
    Validator validate;
    HWND hwnd{};
    HFONT font{};
    UINT dpi{96};
    bool initialized{},syncing{},valid_input{},modal{};
    std::optional<ResizeChoice> selected;
    std::optional<CanvasChoice> canvas;
    HWND control(int key) const{return GetDlgItem(hwnd,key);}
    void filter_controls() {
        for(const auto key:{id::Width,id::Height,id::WidthPercent,id::HeightPercent,id::Half,id::Double,id::KeepAspect,id::Smooth})EnableWindow(control(key),draft.resample);
        for(const auto key:{id::Down,id::Up,id::Sharpen})EnableWindow(control(key),draft.resample&&draft.smooth);
    }
    double canvas_factor(bool horizontal) const {
        if(!canvas||!canvas->unit)return 1;
        if(canvas->unit==1)return (horizontal?original_width:original_height)/100.;
        return (horizontal?canvas->dpi_x:canvas->dpi_y)*inches(canvas->unit);
    }
    uint32_t size_value(bool horizontal) const {
        const auto key=horizontal?id::Width:id::Height;
        return canvas&&canvas->unit?dimension(percentage(control(key))*canvas_factor(horizontal)):integer(control(key));
    }
    void read_dpi() {
        if(canvas)return;
        const auto factor=draft.density_unit?2.54:1.;
        const auto x=percentage(control(id::DpiX))*factor,y=percentage(control(id::DpiY))*factor;
        if(x<.001||y<.001||x>65535||y>65535)throw std::invalid_argument("DPI must be between0.001and65535");
        if(std::abs(x-draft.dpi_x)>1e-8)draft.dpi_x=x;if(std::abs(y-draft.dpi_y)>1e-8)draft.dpi_y=y;
    }
    void dpi_values() {
        syncing=true;SetWindowTextW(control(id::DpiX),decimal(draft.dpi_x/(draft.density_unit?2.54:1.)).c_str());
        SetWindowTextW(control(id::DpiY),decimal(draft.dpi_y/(draft.density_unit?2.54:1.)).c_str());syncing=false;
    }
    void canvas_controls() {
        for(const auto key:{id::Red,id::Green,id::Blue,id::Alpha,id::Color})EnableWindow(control(key),!canvas->transparent);
        for(int n=0;n<9;++n)SendMessageW(control(id::Anchor+n),BM_SETCHECK,n==canvas->anchor?BST_CHECKED:BST_UNCHECKED,0);
    }
    // Native radios retain accessible names and check states; paint only the square.
    static LRESULT CALLBACK anchor_cell(HWND cell,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
        if(m==WM_PAINT||m==WM_PRINTCLIENT){
            PAINTSTRUCT paint{};const auto dc=m==WM_PAINT?BeginPaint(cell,&paint):reinterpret_cast<HDC>(w);
            RECT area{};GetClientRect(cell,&area);const auto &colors=ui_palette();
            FillRect(dc,&area,ui_brush(SendMessageW(cell,BM_GETCHECK,0,0)==BST_CHECKED?colors.accent:colors.panel));
            FrameRect(dc,&area,ui_brush(colors.muted));
            if(GetFocus()==cell){InflateRect(&area,-2,-2);DrawFocusRect(dc,&area);}
            if(m==WM_PAINT)EndPaint(cell,&paint);return 0;
        }
        if(m==WM_KEYDOWN&&(w==VK_LEFT||w==VK_RIGHT||w==VK_UP||w==VK_DOWN)){
            const int n=GetDlgCtrlID(cell)-id::Anchor,x=n%3,y=n/3;
            const int next=(w==VK_LEFT?(x+2)%3:w==VK_RIGHT?(x+1)%3:x)+3*(w==VK_UP?(y+2)%3:w==VK_DOWN?(y+1)%3:y);
            const auto target=GetDlgItem(GetParent(cell),id::Anchor+next);SetFocus(target);SendMessageW(target,BM_CLICK,0,0);return 0;
        }
        const auto result=DefSubclassProc(cell,m,w,l);
        if(m==BM_SETCHECK||m==WM_SETFOCUS||m==WM_KILLFOCUS)InvalidateRect(cell,nullptr,FALSE);
        return result;
    }
    void canvas_values() {
        syncing=true;
        for(int n=0;n<3;++n)SetWindowTextW(control(id::Red+n),std::to_wstring((canvas->color>>(16-8*n))&255).c_str());
        SetWindowTextW(control(id::Alpha),std::to_wstring(canvas->alpha).c_str());syncing=false;
    }
    void read_canvas() {
        uint32_t rgb{};
        for(int n=0;n<4;++n) {
            const auto value=text(control(id::Red+n));
            if(value.empty()||value.find_first_not_of(L"0123456789")!=std::wstring::npos)throw std::invalid_argument("Invalid canvas RGBA");
            const auto channel=std::stoul(value);if(channel>255)throw std::invalid_argument("Invalid canvas RGBA");
            if(n<3)rgb=(rgb<<8)|channel;else canvas->alpha=static_cast<int>(channel);
        }
        canvas->color=rgb;
    }
    static LRESULT CALLBACK combo_print(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
        const auto result=DefSubclassProc(h,m,w,l);
        if(m==WM_PRINT||m==WM_PRINTCLIENT) {
            COMBOBOXINFO info{sizeof(info)};const auto selected=SendMessageW(h,CB_GETCURSEL,0,0);
            if(selected>=0&&GetComboBoxInfo(h,&info)) {
                DRAWITEMSTRUCT draw{};draw.CtlType=ODT_COMBOBOX;draw.CtlID=GetDlgCtrlID(h);
                draw.itemID=static_cast<UINT>(selected);draw.itemAction=ODA_DRAWENTIRE;
                draw.itemState=ODS_COMBOBOXEDIT|(IsWindowEnabled(h)?0:ODS_DISABLED);
                draw.hwndItem=h;draw.hDC=reinterpret_cast<HDC>(w);draw.rcItem=info.rcItem;
                SendMessageW(GetParent(h),WM_DRAWITEM,draw.CtlID,reinterpret_cast<LPARAM>(&draw));
            }
        }
        return result;
    }
    void fail() {
        valid_input=false;EnableWindow(control(IDOK),FALSE);
        SetWindowTextW(control(id::Error),canvas?L"寸法・倍率とメモリ上限を確認してください。\nRGBとAlphaは0～255で入力してください。":L"寸法・倍率は正の数、解像度は0.001～65535 DPIです。\nピクセル寸法・メモリ上限も確認してください。");
    }
    void check() {
        try {
            const auto w=!canvas&&!draft.resample?original_width:size_value(true),h=!canvas&&!draft.resample?original_height:size_value(false);
            read_dpi();
            if(!canvas){percentage(control(id::PrintWidth));percentage(control(id::PrintHeight));}
            if(canvas&&!canvas->transparent)read_canvas();
            checked_image_pixels(w,h);
            if(validate&&(canvas||draft.resample))validate(w,h);
            draft.width=w;draft.height=h;valid_input=true;
            EnableWindow(control(IDOK),TRUE);SetWindowTextW(control(id::Error),L"");
            SetWindowTextW(control(id::Bytes),bytes(w,h).c_str());
        }catch(...){fail();}
    }
    void set_dimensions(uint32_t w,uint32_t h,int editing=0) {
        syncing=true;
        if(editing!=id::Width)SetWindowTextW(control(id::Width),(canvas&&canvas->unit?decimal(w/canvas_factor(true)):std::to_wstring(w)).c_str());
        if(editing!=id::Height)SetWindowTextW(control(id::Height),(canvas&&canvas->unit?decimal(h/canvas_factor(false)):std::to_wstring(h)).c_str());
        if(editing!=id::WidthPercent)SetWindowTextW(control(id::WidthPercent),percent(w,original_width).c_str());
        if(editing!=id::HeightPercent)SetWindowTextW(control(id::HeightPercent),percent(h,original_height).c_str());
        if(!canvas) {
            if(editing!=id::PrintWidth)SetWindowTextW(control(id::PrintWidth),decimal(w/draft.dpi_x/inches(draft.print_unit)).c_str());
            if(editing!=id::PrintHeight)SetWindowTextW(control(id::PrintHeight),decimal(h/draft.dpi_y/inches(draft.print_unit)).c_str());
        }
        syncing=false;check();
    }
    void changed(int key) {
        if(syncing||!initialized)return;
        try {
            auto w=size_value(true),h=size_value(false);
            if(key==id::WidthPercent)w=dimension(percentage(control(key))*original_width/100);
            if(key==id::HeightPercent)h=dimension(percentage(control(key))*original_height/100);
            if(draft.keep_aspect) {
                if(key==id::Height||key==id::HeightPercent)w=proportional(h,original_height,original_width);
                else h=proportional(w,original_width,original_height);
            }
            set_dimensions(w,h,key);
        }catch(...){fail();}
    }
    void scale(bool twice) {
        try {
            const auto w=size_value(true),h=size_value(false);
            set_dimensions(twice?dimension(double(w)*2):std::max(1u,(w+1)/2),
                           twice?dimension(double(h)*2):std::max(1u,(h+1)/2));
        }catch(...){fail();}
    }
    void layout() {
        const auto px=[&](int n){return MulDiv(n,dpi,96);};
        const auto place=[&](int key,int x,int y,int w,int h){MoveWindow(control(key),px(x),px(y),px(w),px(h),TRUE);};
        place(230,12,10,436,66);place(id::Original,24,34,412,24);
        place(231,12,84,436,178);
        place(232,24,111,60,20);place(id::Width,84,106,100,26);place(233,190,111,35,20);
        place(234,244,111,60,20);place(id::Height,304,106,100,26);place(235,410,111,30,20);
        place(236,24,147,60,20);place(id::WidthPercent,84,142,100,26);place(237,190,147,35,20);
        place(238,244,147,60,20);place(id::HeightPercent,304,142,100,26);place(239,410,147,30,20);
        place(id::Bytes,24,180,412,24);place(id::Half,24,218,124,28);place(id::Double,160,218,124,28);
        place(id::KeepAspect,20,canvas?275:402,420,24);
        if(canvas) {
            place(id::CanvasUnit,304,218,132,180);
            place(240,20,309,150,22);place(241,184,309,250,22);
            for(int n=0;n<9;++n)place(id::Anchor+n,20+(n%3)*22,336+(n/3)*22,20,20);
            place(id::Transparent,184,336,250,24);
            for(int n=0;n<4;++n){place(242+n,184+n*66,368,60,20);place(id::Red+n,184+n*66,391,60,26);}
            place(id::Color,184,427,250,28);
            place(id::Error,20,466,420,38);place(IDOK,254,512,88,28);place(IDCANCEL,352,512,96,28);return;
        }
        place(280,12,270,436,94);place(281,24,294,48,20);place(282,196,294,44,20);
        place(id::PrintWidth,80,289,100,26);place(id::PrintHeight,244,289,100,26);place(id::PrintUnit,352,289,84,180);
        place(283,24,330,48,20);place(284,196,330,44,20);place(id::DpiX,80,325,100,26);place(id::DpiY,244,325,100,26);place(id::DpiUnit,352,325,84,180);
        place(id::Resample,20,373,420,24);place(id::Smooth,20,431,420,24);
        place(240,20,465,204,22);place(241,244,465,204,22);
        place(id::Down,20,488,204,180);place(id::Up,244,488,204,180);
        place(id::Sharpen,20,526,420,24);
        place(id::Error,20,562,420,36);place(IDOK,254,606,88,28);place(IDCANCEL,352,606,96,28);
    }
    void set_font() {
        const auto next=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");
        if(!next)throw std::runtime_error("Cannot create resize font");
        EnumChildWindows(hwnd,[](HWND child,LPARAM font)->BOOL{SendMessageW(child,WM_SETFONT,font,TRUE);return TRUE;},reinterpret_cast<LPARAM>(next));
        for(const auto key:{id::Down,id::Up,id::CanvasUnit,id::PrintUnit,id::DpiUnit}) if(control(key)) {
            SendMessageW(control(key),CB_SETITEMHEIGHT,0,MulDiv(20,dpi,96));
            SendMessageW(control(key),CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),MulDiv(20,dpi,96));
        }
        if(font)DeleteObject(font);font=next;
    }
    void finish(bool accept) {
        if(accept){if(!valid_input)return;check();if(!valid_input)return;selected=draft;}
        if(modal)EndDialog(hwnd,accept?IDOK:IDCANCEL);else DestroyWindow(hwnd);
    }
    INT_PTR message(UINT m,WPARAM w,LPARAM l) {
        if(m==WM_INITDIALOG) {
            SetWindowTextW(hwnd,canvas?L"キャンバスサイズ":L"画像の拡大縮小");dpi=GetDpiForWindow(hwnd);
            const auto make=[&](int key,const wchar_t *cls,const wchar_t *label,DWORD style,DWORD ex=0) {
                if(!CreateWindowExW(ex,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,1,1,hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(key)),GetModuleHandleW(nullptr),nullptr))
                    throw std::runtime_error("Cannot create resize control");
            };
            make(230,L"BUTTON",L"元のサイズ",BS_GROUPBOX);make(id::Original,L"STATIC",bytes(original_width,original_height).c_str(),0);
            make(231,L"BUTTON",L"新しいサイズ",BS_GROUPBOX);
            for(const auto key:{id::Width,id::Height,id::WidthPercent,id::HeightPercent})
                make(key,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,WS_EX_CLIENTEDGE);
            make(232,L"STATIC",L"幅",0);make(233,L"STATIC",L"px",0);make(234,L"STATIC",L"高さ",0);make(235,L"STATIC",L"px",0);
            make(236,L"STATIC",L"比率",0);make(237,L"STATIC",L"%",0);make(238,L"STATIC",L"比率",0);make(239,L"STATIC",L"%",0);
            make(id::Bytes,L"STATIC",L"",0);make(id::Half,L"BUTTON",L"今の半分(&H)",WS_TABSTOP|BS_PUSHBUTTON);
            make(id::Double,L"BUTTON",L"今の2倍(&D)",WS_TABSTOP|BS_PUSHBUTTON);
            make(id::KeepAspect,L"BUTTON",L"元の縦横比を保つ(&A)",WS_TABSTOP|BS_AUTOCHECKBOX);
            const auto combo=[&](int key,std::initializer_list<const wchar_t*> labels,int selected) {
                make(key,WC_COMBOBOXW,L"",WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS);
                for(const auto label:labels)SendMessageW(control(key),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
                SendMessageW(control(key),CB_SETCURSEL,selected,0);SetWindowSubclass(control(key),combo_print,1,0);
            };
            if(canvas) {
                combo(id::CanvasUnit,{L"px",L"%",L"inch",L"cm",L"mm"},canvas->unit);
                make(240,L"STATIC",L"基準点",0);make(241,L"STATIC",L"余白の色と透明度（0～255）",0);
                const wchar_t *anchors[]{L"左上",L"上中央",L"右上",L"左中央",L"中央",L"右中央",L"左下",L"下中央",L"右下"};
                for(int n=0;n<9;++n)make(id::Anchor+n,L"BUTTON",anchors[n],WS_TABSTOP|BS_AUTORADIOBUTTON|BS_PUSHLIKE|(n==0?WS_GROUP:0));
                make(id::Transparent,L"BUTTON",L"透明の余白(&T)",WS_TABSTOP|WS_GROUP|BS_AUTOCHECKBOX);
                const wchar_t *channels[]{L"赤 R",L"緑 G",L"青 B",L"Alpha"};
                for(int n=0;n<4;++n){make(242+n,L"STATIC",channels[n],0);make(id::Red+n,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|ES_NUMBER,WS_EX_CLIENTEDGE);}
                make(id::Color,L"BUTTON",L"色を選択…(&C)",WS_TABSTOP|BS_PUSHBUTTON);
                SendMessageW(control(id::Transparent),BM_SETCHECK,canvas->transparent?BST_CHECKED:BST_UNCHECKED,0);
                canvas_values();canvas_controls();
                const wchar_t *units[]{L"px",L"%",L"inch",L"cm",L"mm"};
                SetWindowTextW(control(233),units[canvas->unit]);SetWindowTextW(control(235),units[canvas->unit]);
            } else {
            make(280,L"BUTTON",L"プリントサイズ / 解像度",BS_GROUPBOX);make(281,L"STATIC",L"幅",0);make(282,L"STATIC",L"高さ",0);
            make(283,L"STATIC",L"X解像度",0);make(284,L"STATIC",L"Y解像度",0);
            for(const auto key:{id::PrintWidth,id::PrintHeight,id::DpiX,id::DpiY})make(key,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,WS_EX_CLIENTEDGE);
            combo(id::PrintUnit,{L"inch",L"cm",L"mm"},draft.print_unit-2);combo(id::DpiUnit,{L"dpi",L"dpcm"},draft.density_unit);
            make(id::Resample,L"BUTTON",L"画像を拡大縮小（OFFは解像度だけ変更）(&R)",WS_TABSTOP|BS_AUTOCHECKBOX);
            SendMessageW(control(id::Resample),BM_SETCHECK,draft.resample?BST_CHECKED:BST_UNCHECKED,0);dpi_values();
            make(id::Smooth,L"BUTTON",L"補間方法を使う（OFFは無補正）(&L)",WS_TABSTOP|BS_AUTOCHECKBOX);
            make(240,L"STATIC",L"縮小方法",0);make(241,L"STATIC",L"拡大方法",0);
            const auto &methods=display_quality_labels;
            for(const auto key:{id::Down,id::Up}) {
                make(key,WC_COMBOBOXW,L"",WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS);
                for(const auto label:methods)SendMessageW(control(key),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
                SendMessageW(control(key),CB_SETCURSEL,display_quality_index(key==id::Down?draft.down:draft.up),0);
                SetWindowSubclass(control(key),combo_print,1,0);
            }
            make(id::Sharpen,L"BUTTON",L"縮小した後にシャープ加工(&S)",WS_TABSTOP|BS_AUTOCHECKBOX);
            }
            make(id::Error,L"STATIC",L"",0);make(IDOK,L"BUTTON",L"OK",WS_TABSTOP|BS_DEFPUSHBUTTON);
            make(IDCANCEL,L"BUTTON",L"キャンセル",WS_TABSTOP|BS_PUSHBUTTON);
            SendMessageW(control(id::KeepAspect),BM_SETCHECK,draft.keep_aspect?BST_CHECKED:BST_UNCHECKED,0);
            SendMessageW(control(id::Smooth),BM_SETCHECK,draft.smooth?BST_CHECKED:BST_UNCHECKED,0);
            SendMessageW(control(id::Sharpen),BM_SETCHECK,draft.sharpen?BST_CHECKED:BST_UNCHECKED,0);
            set_font();if(!canvas)filter_controls();const auto px=[&](int n){return MulDiv(n,dpi,96);};RECT bounds{0,0,px(460),px(canvas?552:646)};
            AdjustWindowRectExForDpi(&bounds,static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_STYLE)),FALSE,
                static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)),dpi);
            RECT owner{};GetWindowRect(GetWindow(hwnd,GW_OWNER),&owner);MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor);
            const auto placed=place_window({owner.left+(owner.right-owner.left-bounds.right+bounds.left)/2,
                owner.top+(owner.bottom-owner.top-bounds.bottom+bounds.top)/2,bounds.right-bounds.left,bounds.bottom-bounds.top},
                {monitor.rcWork.left,monitor.rcWork.top,monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top},false);
            SetWindowPos(hwnd,nullptr,placed.x,placed.y,placed.w,placed.h,SWP_NOZORDER|SWP_NOACTIVATE);
            layout();initialized=true;set_dimensions(draft.width,draft.height);SetFocus(control(id::Width));
            SendMessageW(control(id::Width),EM_SETSEL,0,-1);return FALSE;
        }
        if(m==WM_COMMAND&&initialized) {
            const auto key=LOWORD(w),event=HIWORD(w);
            if(canvas) {
                if(key>=id::Anchor&&key<id::Anchor+9&&event==BN_CLICKED){canvas->anchor=key-id::Anchor;canvas_controls();return TRUE;}
                if(key==id::Transparent&&event==BN_CLICKED){canvas->transparent=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;canvas_controls();check();return TRUE;}
                if(key>=id::Red&&key<=id::Alpha&&event==EN_CHANGE&&!syncing){check();return TRUE;}
                if(key==id::Color&&event==BN_CLICKED) {
                    if(const auto value=choose_color(hwnd,canvas->color)){canvas->color=*value;canvas_values();check();}return TRUE;
                }
            }
            if(key==IDOK||key==IDCANCEL){finish(key==IDOK);return TRUE;}
            if(canvas&&key==id::CanvasUnit&&event==CBN_SELCHANGE) {
                canvas->unit=static_cast<int>(SendMessageW(control(key),CB_GETCURSEL,0,0));
                const wchar_t *units[]{L"px",L"%",L"inch",L"cm",L"mm"};
                SetWindowTextW(control(233),units[canvas->unit]);SetWindowTextW(control(235),units[canvas->unit]);
                set_dimensions(draft.width,draft.height);return TRUE;
            }
            if(!canvas) {
                if((key==id::PrintUnit||key==id::DpiUnit)&&event==CBN_SELCHANGE) {
                    if(key==id::PrintUnit)draft.print_unit=2+static_cast<int>(SendMessageW(control(key),CB_GETCURSEL,0,0));
                    else{draft.density_unit=static_cast<int>(SendMessageW(control(key),CB_GETCURSEL,0,0));dpi_values();}
                    set_dimensions(draft.width,draft.height);return TRUE;
                }
                if(key==id::Resample&&event==BN_CLICKED) {
                    draft.resample=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;
                    filter_controls();set_dimensions(draft.resample?draft.width:original_width,draft.resample?draft.height:original_height);return TRUE;
                }
                if((key==id::DpiX||key==id::DpiY||key==id::PrintWidth||key==id::PrintHeight)&&event==EN_CHANGE&&!syncing) {
                    try {
                        read_dpi();auto w=draft.width,h=draft.height;
                        if(key==id::DpiX||key==id::DpiY)draft.dpi_defined=true;
                        else {
                            const bool horizontal=key==id::PrintWidth;const auto size=percentage(control(key))*inches(draft.print_unit);
                            if(draft.resample) {
                                if(horizontal)w=dimension(size*draft.dpi_x);else h=dimension(size*draft.dpi_y);
                                if(draft.keep_aspect){if(horizontal)h=proportional(w,original_width,original_height);else w=proportional(h,original_height,original_width);}
                            } else {
                                if(horizontal)draft.dpi_x=original_width/size;else draft.dpi_y=original_height/size;
                                draft.dpi_defined=true;dpi_values();
                            }
                        }
                        set_dimensions(w,h,key);
                    }catch(...){fail();}return TRUE;
                }
            }
            if((key==id::Width||key==id::Height||key==id::WidthPercent||key==id::HeightPercent)&&event==EN_CHANGE){changed(key);return TRUE;}
            if(key==id::KeepAspect&&event==BN_CLICKED){draft.keep_aspect=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;changed(id::Width);return TRUE;}
            if(key==id::Smooth&&event==BN_CLICKED){draft.smooth=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;filter_controls();return TRUE;}
            if(key==id::Sharpen&&event==BN_CLICKED){draft.sharpen=SendMessageW(control(key),BM_GETCHECK,0,0)==BST_CHECKED;return TRUE;}
            if((key==id::Down||key==id::Up)&&event==CBN_SELCHANGE) {
                const auto selected=static_cast<int>(SendMessageW(control(key),CB_GETCURSEL,0,0));
                if(selected>=0&&selected<3)(key==id::Down?draft.down:draft.up)=display_quality_filter(selected);
                return TRUE;
            }
            if((key==id::Half||key==id::Double)&&event==BN_CLICKED){scale(key==id::Double);return TRUE;}
        }
        if(m==WM_DPICHANGED) {
            dpi=HIWORD(w);const auto r=reinterpret_cast<RECT*>(l);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
            set_font();layout();return TRUE;
        }
        if(m==WM_CLOSE){finish(false);return TRUE;}
        if(m==WM_MEASUREITEM && reinterpret_cast<MEASUREITEMSTRUCT*>(l)->CtlType==ODT_COMBOBOX) {
            reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight=MulDiv(20,dpi,96);return TRUE;
        }
        if(m==WM_DRAWITEM && reinterpret_cast<DRAWITEMSTRUCT*>(l)->CtlType==ODT_COMBOBOX) {
            const auto &draw=*reinterpret_cast<DRAWITEMSTRUCT*>(l);if(draw.itemID==static_cast<UINT>(-1))return TRUE;
            const auto saved=SaveDC(draw.hDC);const bool selected=(draw.itemState&ODS_SELECTED)!=0;
            FillRect(draw.hDC,&draw.rcItem,ui_brush(selected?ui_palette().selected:ui_palette().input));
            SetTextColor(draw.hDC,(draw.itemState&ODS_DISABLED)?ui_palette().muted:ui_palette().text);
            SetBkMode(draw.hDC,TRANSPARENT);SelectObject(draw.hDC,font?font:GetStockObject(DEFAULT_GUI_FONT));
            const auto length=SendMessageW(draw.hwndItem,CB_GETLBTEXTLEN,draw.itemID,0);
            if(length>=0) {
                std::wstring label(static_cast<size_t>(length)+1,L'\0');SendMessageW(draw.hwndItem,CB_GETLBTEXT,draw.itemID,reinterpret_cast<LPARAM>(label.data()));
                RECT area=draw.rcItem;area.left+=5;DrawTextW(draw.hDC,label.c_str(),-1,&area,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
            }
            RestoreDC(draw.hDC,saved);return TRUE;
        }
        if(m==WM_DESTROY){initialized=false;return TRUE;}
        return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto self=reinterpret_cast<Impl*>(GetWindowLongPtrW(h,DWLP_USER));
        if(m==WM_INITDIALOG){self=reinterpret_cast<Impl*>(l);self->hwnd=h;SetWindowLongPtrW(h,DWLP_USER,l);}
        if(!self)return FALSE;
        try{const auto result=self->message(m,w,l);if(m==WM_INITDIALOG||m==WM_DPICHANGED){apply_ui_theme(h);if(self->canvas)for(int n=0;n<9;++n){const auto cell=self->control(id::Anchor+n);RemoveWindowSubclass(cell,anchor_cell,78);SetWindowSubclass(cell,anchor_cell,78,0);}}return result;}catch(...){self->fail();return TRUE;}
    }
};
ResizeDialog::ResizeDialog(uint32_t w,uint32_t h,ResizeChoice choice,Validator validate):impl(std::make_unique<Impl>()) {
    if(choice.down<0||choice.down>10||choice.up<0||choice.up>10)throw std::invalid_argument("Invalid resize filter selection");
    if(!std::isfinite(choice.dpi_x)||!std::isfinite(choice.dpi_y)||choice.dpi_x<=0||choice.dpi_y<=0||choice.print_unit<2||choice.print_unit>4||choice.density_unit<0||choice.density_unit>1)throw std::invalid_argument("Invalid resize resolution or units");
    checked_image_pixels(w,h);impl->original_width=w;impl->original_height=h;
    if(!choice.width)choice.width=w;if(!choice.height)choice.height=h;
    choice.down=canonical_display_filter(choice.down);choice.up=canonical_display_filter(choice.up);
    impl->draft=choice;impl->validate=std::move(validate);
}
ResizeDialog::~ResizeDialog(){if(IsWindow(impl->hwnd))DestroyWindow(impl->hwnd);if(impl->font)DeleteObject(impl->font);}
std::unique_ptr<ResizeDialog> ResizeDialog::for_canvas(uint32_t w,uint32_t h,CanvasChoice choice,Validator validate) {
    if(choice.anchor<0||choice.anchor>8||choice.color>0xffffff||choice.alpha<0||choice.alpha>255)
        throw std::invalid_argument("Invalid canvas options");
    if(choice.unit<0||choice.unit>4||!std::isfinite(choice.dpi_x)||!std::isfinite(choice.dpi_y)||choice.dpi_x<=0||choice.dpi_y<=0)throw std::invalid_argument("Invalid canvas units or resolution");
    auto result=std::make_unique<ResizeDialog>(w,h,ResizeChoice{choice.width,choice.height,choice.keep_aspect},std::move(validate));
    result->impl->canvas=choice;return result;
}
std::optional<ResizeChoice> ResizeDialog::show(HWND owner) {
    DialogTemplate value;impl->modal=true;
    if(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()))==-1)
        throw std::runtime_error("Cannot open resize dialog");
    return impl->selected;
}
HWND ResizeDialog::create(HWND owner){DialogTemplate value;const auto h=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&value.dialog,owner,Impl::procedure,reinterpret_cast<LPARAM>(impl.get()));if(!IsWindow(h))throw std::runtime_error("Cannot create resize dialog");return h;}
HWND ResizeDialog::window() const{return impl->hwnd;}
HWND ResizeDialog::control(int key) const{return impl->control(key);}
bool ResizeDialog::ready() const{return impl->initialized;}
bool ResizeDialog::valid() const{return impl->valid_input;}
ResizeChoice ResizeDialog::options() const{return impl->draft;}
std::optional<ResizeChoice> ResizeDialog::result() const{return impl->selected;}
CanvasChoice ResizeDialog::canvas_options() const {
    if(!impl->canvas)throw std::logic_error("Not a canvas dialog");
    auto choice=*impl->canvas;choice.width=impl->draft.width;choice.height=impl->draft.height;choice.keep_aspect=impl->draft.keep_aspect;return choice;
}
std::optional<CanvasChoice> ResizeDialog::canvas_result() const {return impl->selected?std::optional{canvas_options()}:std::nullopt;}
std::optional<ResizeChoice> choose_resize(HWND owner,uint32_t w,uint32_t h,ResizeChoice choice,const ResizeDialog::Validator &validate){ResizeDialog dialog(w,h,choice,validate);return dialog.show(owner);}
std::optional<CanvasChoice> choose_canvas(HWND owner,uint32_t w,uint32_t h,CanvasChoice choice,const ResizeDialog::Validator &validate){auto dialog=ResizeDialog::for_canvas(w,h,choice,validate);dialog->show(owner);return dialog->canvas_result();}
} // namespace gvn
