#include "gvn/ui_theme.h"
#include "gvn/commands.h"
#include <commctrl.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <oleacc.h>
#include <vector>
#include <cwctype>
#include <algorithm>
#include <map>
#include <string>
#include <set>
#include <tuple>
namespace gvn {
HICON command_icon(UINT,int,bool);
namespace {
UiPalette colors{RGB(246,247,249),RGB(255,255,255),RGB(35,39,46),RGB(104,112,124),RGB(216,220,226),RGB(30,104,210),RGB(227,238,253),RGB(255,255,255)};
uint64_t generation{1};int density{};
struct Resources {std::map<COLORREF,HBRUSH> brushes;std::map<UINT,HFONT> fonts;~Resources(){for(auto [c,b]:brushes)DeleteObject(b);for(auto [d,f]:fonts)DeleteObject(f);}} resources;
std::wstring class_name(HWND h){wchar_t text[80]{};GetClassNameW(h,text,80);return text;}
bool named(const std::wstring &a,const wchar_t *b){return _wcsicmp(a.c_str(),b)==0;}
void fill(HDC dc,const RECT &r,COLORREF c){FillRect(dc,&r,ui_brush(c));}
void frame(HDC dc,const RECT &r,COLORREF c){FrameRect(dc,&r,ui_brush(c));}
void line(HDC dc,int x,int y,int z,int w,COLORREF c,int width=1){auto pen=CreatePen(PS_SOLID,width,c);auto old=SelectObject(dc,pen);MoveToEx(dc,x,y,nullptr);LineTo(dc,z,w);SelectObject(dc,old);DeleteObject(pen);}
void label(HWND h,HDC dc,std::wstring text,RECT r,COLORREF c,UINT format=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){
 auto font=reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0));auto old=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,c);DrawTextW(dc,text.c_str(),int(text.size()),&r,format);SelectObject(dc,old);
}
std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(size_t(n)+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(n);return s;}
#include "ui_menu_theme.inc"
bool paint_control(HWND h,HDC dc,const std::wstring &kind){
 RECT r{};GetClientRect(h,&r);const int px=std::max(1,MulDiv(1,GetDpiForWindow(h),96));
 if(named(kind,L"Button")){
  const auto style=GetWindowLongPtrW(h,GWL_STYLE)&BS_TYPEMASK;if(style==BS_OWNERDRAW||style==BS_GROUPBOX)return false;
  const bool check=style==BS_CHECKBOX||style==BS_AUTOCHECKBOX||style==BS_3STATE||style==BS_AUTO3STATE,radio=style==BS_RADIOBUTTON||style==BS_AUTORADIOBUTTON;
  const bool enabled=IsWindowEnabled(h)!=FALSE,down=(SendMessageW(h,BM_GETSTATE,0,0)&BST_PUSHED)!=0,active=SendMessageW(h,BM_GETCHECK,0,0)!=BST_UNCHECKED;
  fill(dc,r,check||radio?colors.surface:down?colors.selected:colors.panel);
  auto bounds=r;
  if(check||radio){const int size=MulDiv(14,GetDpiForWindow(h),96),y=(r.bottom-size)/2;RECT box{0,y,size,y+size};
   fill(dc,box,active?colors.accent:colors.input);frame(dc,box,enabled?colors.border:colors.muted);
   if(active){if(radio){InflateRect(&box,-4*px,-4*px);fill(dc,box,colors.panel);}else{line(dc,3*px,y+7*px,6*px,y+10*px,RGB(255,255,255),2*px);line(dc,6*px,y+10*px,11*px,y+4*px,RGB(255,255,255),2*px);}}
   bounds.left=size+6*px;
  }else{frame(dc,r,(GetFocus()==h||style==BS_DEFPUSHBUTTON)?colors.accent:colors.border);bounds.left+=6*px;bounds.right-=6*px;}
  label(h,dc,text(h),bounds,enabled?colors.text:colors.muted,DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|(check||radio?DT_LEFT:DT_CENTER));
  if(GetFocus()==h){RECT focus=r;InflateRect(&focus,-3*px,-3*px);DrawFocusRect(dc,&focus);}return true;
 }
 if(named(kind,WC_TABCONTROLW)){
  fill(dc,r,colors.surface);for(int i=0;i<TabCtrl_GetItemCount(h);++i){RECT item{};if(!TabCtrl_GetItemRect(h,i,&item))continue;wchar_t title[160]{};TCITEMW info{};info.mask=TCIF_TEXT;info.pszText=title;info.cchTextMax=160;TabCtrl_GetItem(h,i,&info);
   const bool selected=TabCtrl_GetCurSel(h)==i;if(selected)fill(dc,item,colors.selected);label(h,dc,title,item,selected?colors.accent:colors.text,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
   if(selected){RECT bar{item.left+6*px,item.bottom-2*px,item.right-6*px,item.bottom};fill(dc,bar,colors.accent);}}
  return true;
 }
 if(named(kind,STATUSCLASSNAMEW)){
  fill(dc,r,colors.surface);const int count=int(SendMessageW(h,SB_GETPARTS,0,0));
  for(int i=0;i<count;++i){RECT item{};SendMessageW(h,SB_GETRECT,i,reinterpret_cast<LPARAM>(&item));const int n=LOWORD(SendMessageW(h,SB_GETTEXTLENGTHW,i,0));std::wstring s(size_t(n)+1,L'\0');SendMessageW(h,SB_GETTEXTW,i,reinterpret_cast<LPARAM>(s.data()));s.resize(n);item.left+=5*px;item.right-=4*px;label(h,dc,s,item,colors.muted);}
  line(dc,0,0,r.right,0,colors.border);return true;
 }
 if(named(kind,L"ComboBox")&&(GetWindowLongPtrW(h,GWL_STYLE)&3)==CBS_DROPDOWNLIST){
  fill(dc,r,colors.input);frame(dc,r,GetFocus()==h?colors.accent:colors.border);auto area=r;area.left+=5*px;area.right-=22*px;label(h,dc,text(h),area,IsWindowEnabled(h)?colors.text:colors.muted);const int x=r.right-13*px,y=r.bottom/2;
  line(dc,x-3*px,y-2*px,x,y+px,colors.muted);line(dc,x,y+px,x+3*px,y-2*px,colors.muted);return true;
 }
 if(named(kind,L"ScrollBar")){
  fill(dc,r,colors.surface);SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(h,SB_CTL,&info);const bool vertical=(GetWindowLongPtrW(h,GWL_STYLE)&SBS_VERT)!=0;
  const int length=vertical?r.bottom:r.right,thick=vertical?r.right:r.bottom,arrow=std::min(thick,length/2),track=std::max(0,length-2*arrow),range=std::max(1,info.nMax-info.nMin+1);
  const int size=std::min(track,std::max(12,track*int(info.nPage)/range)),travel=track-size,maximum=std::max(1,range-int(info.nPage));const int at=arrow+travel*std::clamp(info.nPos-info.nMin,0,maximum)/maximum;
  RECT thumb=vertical?RECT{3*px,at,r.right-3*px,at+size}:RECT{at,3*px,at+size,r.bottom-3*px};fill(dc,thumb,colors.muted);
  for(int end:{0,1}){const int c=end?length-arrow/2:arrow/2,m=thick/2,d=end?1:-1;if(vertical){line(dc,m-3*px,c-d*px,m,c+2*d*px,colors.muted);line(dc,m,c+2*d*px,m+3*px,c-d*px,colors.muted);}else{line(dc,c-d*px,m-3*px,c+2*d*px,m,colors.muted);line(dc,c+2*d*px,m,c-d*px,m+3*px,colors.muted);}}
  return true;
 }
 if(named(kind,TRACKBAR_CLASSW)){
  fill(dc,r,colors.surface);RECT thumb{},channel{};SendMessageW(h,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));SendMessageW(h,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
  const int y=r.bottom/2;RECT rail{channel.left,y-px,channel.right,y+px};fill(dc,rail,colors.border);rail.right=(thumb.left+thumb.right)/2;fill(dc,rail,colors.accent);thumb.top=y-6*px;thumb.bottom=y+6*px;fill(dc,thumb,colors.accent);return true;
 }
 return false;
}
LRESULT CALLBACK theme_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
 if(m==WM_NCDESTROY){RemoveWindowSubclass(h,theme_proc,77);return DefSubclassProc(h,m,w,l);}
 if(colors.contrast)return DefSubclassProc(h,m,w,l);
 if(m==WM_MEASUREITEM&&l){auto &draw=*reinterpret_cast<MEASUREITEMSTRUCT *>(l);if(draw.CtlType==ODT_MENU&&measure_menu(h,draw))return TRUE;}
 if(m==WM_DRAWITEM&&l){const auto &draw=*reinterpret_cast<DRAWITEMSTRUCT *>(l);if(draw.CtlType==ODT_MENU&&draw_menu(h,draw))return TRUE;}
 if(m==WM_MENUCHAR&&themed_menus.contains(reinterpret_cast<HMENU>(l)))return menu_char(w,reinterpret_cast<HMENU>(l));
 if(m==WM_NCPAINT||m==WM_NCACTIVATE||m==WM_PRINT){const auto result=DefSubclassProc(h,m,w,l);menu_gaps(h,m==WM_PRINT?reinterpret_cast<HDC>(w):nullptr);return result;}
 if((m==WM_LBUTTONDOWN||m==WM_LBUTTONUP||m==WM_MOUSEMOVE||m==WM_ENABLE)&&named(class_name(h),L"ScrollBar")){const auto result=DefSubclassProc(h,m,w,l);RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);return result;}
 if(m==WM_CTLCOLORSTATIC||m==WM_CTLCOLORBTN||m==WM_CTLCOLOREDIT||m==WM_CTLCOLORLISTBOX||m==WM_CTLCOLORDLG){auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,IsWindowEnabled(reinterpret_cast<HWND>(l))?colors.text:colors.muted);const auto c=m==WM_CTLCOLOREDIT||m==WM_CTLCOLORLISTBOX?colors.input:colors.surface;SetBkColor(dc,c);return reinterpret_cast<LRESULT>(ui_brush(c));}
 if(m==WM_NOTIFY){auto header=reinterpret_cast<NMHDR *>(l);if(header&&header->code==NM_CUSTOMDRAW&&named(class_name(header->hwndFrom),TOOLBARCLASSNAMEW))return ui_toolbar_draw(l);}
 if(m==WM_PAINT||m==WM_PRINTCLIENT){const auto kind=class_name(h);const bool candidate=named(kind,L"Button")||named(kind,WC_TABCONTROLW)||named(kind,STATUSCLASSNAMEW)||named(kind,TRACKBAR_CLASSW)||named(kind,L"ScrollBar")||(named(kind,L"ComboBox")&&(GetWindowLongPtrW(h,GWL_STYLE)&3)==CBS_DROPDOWNLIST);
  if(candidate){if(named(kind,L"Button")&&((GetWindowLongPtrW(h,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW||(GetWindowLongPtrW(h,GWL_STYLE)&BS_TYPEMASK)==BS_GROUPBOX))return DefSubclassProc(h,m,w,l);
   PAINTSTRUCT paint{};const auto dc=m==WM_PAINT?BeginPaint(h,&paint):reinterpret_cast<HDC>(w);const bool done=paint_control(h,dc,kind);if(m==WM_PAINT)EndPaint(h,&paint);if(done)return 0;}
 }
 if(m==WM_ERASEBKGND){const auto kind=class_name(h);if(named(kind,L"#32770")||named(kind,L"Static")||kind.starts_with(L"GVN.Toolbar")||kind==L"GVN.ToolsHost"||kind==L"GVN.EditorChrome"||kind==L"GVN.AnimationBar"){RECT r{};GetClientRect(h,&r);fill(reinterpret_cast<HDC>(w),r,colors.surface);return 1;}}
 return DefSubclassProc(h,m,w,l);
}
}
const UiPalette &ui_palette(){return colors;}
uint64_t ui_theme_generation(){return generation;}
HBRUSH ui_brush(COLORREF c){auto &brush=resources.brushes[c];if(!brush)brush=CreateSolidBrush(c);return brush;}
int ui_button_size(bool large){return (large?32:28)+(density?8:0);}
int ui_icon_size(bool large){return large?24:20;}
void set_ui_theme(int mode,int spacing){
 HIGHCONTRASTW hc{sizeof(hc)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0);const bool contrast=(hc.dwFlags&HCF_HIGHCONTRASTON)!=0;
 DWORD light=1,size=sizeof(light);if(mode==0)RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
 const bool dark=mode==2||(mode==0&&!light);if(colors.dark==dark&&colors.contrast==contrast&&density==spacing&&!contrast)return;
 density=spacing;++generation;
 if(contrast)colors={GetSysColor(COLOR_BTNFACE),GetSysColor(COLOR_WINDOW),GetSysColor(COLOR_WINDOWTEXT),GetSysColor(COLOR_GRAYTEXT),GetSysColor(COLOR_WINDOWFRAME),GetSysColor(COLOR_HIGHLIGHT),GetSysColor(COLOR_HIGHLIGHT),GetSysColor(COLOR_WINDOW),dark,true};
 else if(dark)colors={RGB(34,36,40),RGB(42,45,50),RGB(236,239,244),RGB(169,176,188),RGB(65,70,80),RGB(111,174,255),RGB(48,65,87),RGB(28,30,34),true,false};
 else colors={RGB(246,247,249),RGB(255,255,255),RGB(35,39,46),RGB(104,112,124),RGB(216,220,226),RGB(30,104,210),RGB(227,238,253),RGB(255,255,255),false,false};
}
void ui_theme_menu(HMENU menu,bool bar){theme_menu(menu,bar);}
void ui_menu_icons(HMENU menu){if(menu){std::vector<HMENU> list;collect_menus(menu,list);for(auto h:list)icon_menus.insert(h);}}
void ui_destroy_menu(HMENU menu){if(menu){std::vector<HMENU> list;collect_menus(menu,list);DestroyMenu(menu);for(const auto handle:list){themed_menus.erase(handle);icon_menus.erase(handle);}}}
void ui_set_scroll_info(HWND h,const SCROLLINFO &info){
 SCROLLINFO old{sizeof(old),SIF_ALL};GetScrollInfo(h,SB_CTL,&old);
 const bool changed=((info.fMask&SIF_RANGE)&&(info.nMin!=old.nMin||info.nMax!=old.nMax))||((info.fMask&SIF_PAGE)&&info.nPage!=old.nPage)||((info.fMask&SIF_POS)&&info.nPos!=old.nPos);
 SetScrollInfo(h,SB_CTL,&info,FALSE);if(changed)RedrawWindow(h,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
}
void apply_ui_theme(HWND h,bool descendants){
 if(!h)return;DWORD_PTR value{};if(!GetWindowSubclass(h,theme_proc,77,&value))SetWindowSubclass(h,theme_proc,77,0);
 const auto dpi=GetDpiForWindow(h);const auto stamp=(generation<<16)|dpi;
 if(reinterpret_cast<UINT_PTR>(GetPropW(h,L"GVN.ThemeGeneration"))!=stamp){
  SetPropW(h,L"GVN.ThemeGeneration",reinterpret_cast<HANDLE>(UINT_PTR(stamp)));const auto kind=class_name(h);
  if(dpi){auto &font=resources.fonts[dpi];if(!font)font=CreateFontW(-MulDiv(12,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Yu Gothic UI");SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);}
  if(!(GetWindowLongPtrW(h,GWL_STYLE)&WS_CHILD)){BOOL dark=colors.dark&&!colors.contrast;DwmSetWindowAttribute(h,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));if(const auto menu=GetMenu(h)){ui_theme_menu(menu,true);DrawMenuBar(h);}}
  if(named(kind,WC_LISTVIEWW)){ListView_SetBkColor(h,colors.input);ListView_SetTextBkColor(h,colors.input);ListView_SetTextColor(h,colors.text);}
  InvalidateRect(h,nullptr,TRUE);
 }
 if(descendants)for(auto child=GetWindow(h,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))apply_ui_theme(child,true);
}
LRESULT ui_toolbar_draw(LPARAM l){
 auto draw=reinterpret_cast<NMTBCUSTOMDRAW *>(l);auto &cd=draw->nmcd;
 if(cd.dwDrawStage==CDDS_PREPAINT){fill(cd.hdc,cd.rc,colors.surface);return CDRF_NOTIFYITEMDRAW;}
 if(cd.dwDrawStage!=CDDS_ITEMPREPAINT)return CDRF_DODEFAULT;
 const auto h=cd.hdr.hwndFrom;TBBUTTONINFOW info{sizeof(info)};wchar_t title[256]{};info.dwMask=TBIF_IMAGE|TBIF_STATE|TBIF_STYLE|TBIF_TEXT;info.pszText=title;info.cchText=256;if(SendMessageW(h,TB_GETBUTTONINFOW,cd.dwItemSpec,reinterpret_cast<LPARAM>(&info))<0)return CDRF_DODEFAULT;
 const bool active=(info.fsState&TBSTATE_CHECKED)||(cd.uItemState&(CDIS_SELECTED|CDIS_HOT));if(active)fill(cd.hdc,cd.rc,colors.selected);
 const auto list=reinterpret_cast<HIMAGELIST>(SendMessageW(h,TB_GETIMAGELIST,0,0));int iw{},ih{};ImageList_GetIconSize(list,&iw,&ih);
 const bool show_text=(info.fsStyle&BTNS_SHOWTEXT)!=0;const int x=show_text?cd.rc.left+5:(cd.rc.left+cd.rc.right-iw)/2,y=(cd.rc.top+cd.rc.bottom-ih)/2;
 if(info.iImage>=0)ImageList_DrawEx(list,info.iImage,cd.hdc,x,y,0,0,CLR_NONE,colors.muted,(info.fsState&TBSTATE_ENABLED)?ILD_NORMAL:ILD_BLEND50);
 if(show_text){auto r=cd.rc;r.left=x+iw+5;label(h,cd.hdc,title,r,colors.text);}
 if(info.fsState&TBSTATE_CHECKED){auto bar=cd.rc;bar.top=bar.bottom-2;fill(cd.hdc,bar,colors.accent);}
 if(cd.uItemState&CDIS_FOCUS)DrawFocusRect(cd.hdc,&cd.rc);return CDRF_SKIPDEFAULT;
}
}
