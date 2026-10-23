// Included in ui_theme.cpp's anonymous namespace. Menu metadata follows the
// native HMENU lifetime; strings remain available to Win32 and accessibility.
struct MenuItem {
    MSAAMENUINFO accessible{};
    std::wstring title;
    HMENU menu{},submenu{};
    UINT position{},type{};
    bool bar{};
};
std::map<HMENU,std::vector<MenuItem>> themed_menus;
void theme_menu(HMENU menu,bool bar){
    if(!menu)return;
    const int count=GetMenuItemCount(menu);if(count<0)return;
    auto &items=themed_menus[menu];items.resize(count);
    MENUINFO background{sizeof(background)};background.fMask=MIM_BACKGROUND;background.hbrBack=ui_brush(colors.surface);SetMenuInfo(menu,&background);
    for(int n=0;n<count;++n){wchar_t title[2048]{};MENUITEMINFOW info{sizeof(info)};info.fMask=MIIM_STRING|MIIM_FTYPE|MIIM_SUBMENU;info.dwTypeData=title;info.cch=2048;if(!GetMenuItemInfoW(menu,n,TRUE,&info))continue;
        auto &item=items[n];item.title=title;item.menu=menu;item.submenu=info.hSubMenu;item.position=n;item.type=info.fType&~MFT_OWNERDRAW;item.bar=bar;
        item.accessible={MSAA_MENU_SIG,DWORD(item.title.size()),item.title.data()};
        MENUITEMINFOW draw{sizeof(draw)};draw.fMask=MIIM_FTYPE|MIIM_DATA;draw.fType=item.type|(colors.contrast?0:MFT_OWNERDRAW);draw.dwItemData=colors.contrast?0:reinterpret_cast<ULONG_PTR>(&item);SetMenuItemInfoW(menu,n,TRUE,&draw);
        if(info.hSubMenu)theme_menu(info.hSubMenu,false);
    }
}
void collect_menus(HMENU menu,std::vector<HMENU> &list){
    for(int n=0;n<GetMenuItemCount(menu);++n)if(const auto child=GetSubMenu(menu,n))collect_menus(child,list);
    list.push_back(menu);
}
MenuItem *menu_item(ULONG_PTR data){
    if(!data)return nullptr;
    const auto item=reinterpret_cast<MenuItem *>(data);
    return item->accessible.dwMSAASignature==MSAA_MENU_SIG?item:nullptr;
}
bool draw_menu(HWND,const DRAWITEMSTRUCT &,bool gaps=true);
void menu_gaps(HWND owner,HDC supplied=nullptr){
    const auto menu=GetMenu(owner);if(colors.contrast||!menu)return;const auto items=themed_menus.find(menu);if(items==themed_menus.end())return;
    MENUBARINFO info{sizeof(info)};RECT window{};if(!GetMenuBarInfo(owner,OBJID_MENU,0,&info)||!GetWindowRect(owner,&window))return;
    const auto dc=supplied?supplied:GetWindowDC(owner);if(!dc)return;const int saved=SaveDC(dc);
    OffsetRect(&info.rcBar,-window.left,-window.top);fill(dc,info.rcBar,colors.surface);
    // Native PrintWindow does not reliably paint owner-drawn nonclient menu
    // labels into its target DC. Paint the complete bar through the same path.
    for(const auto &item:items->second){RECT area{};if(!GetMenuItemRect(owner,menu,item.position,&area))continue;OffsetRect(&area,-window.left,-window.top);
        DRAWITEMSTRUCT draw{};draw.CtlType=ODT_MENU;draw.itemData=reinterpret_cast<ULONG_PTR>(&item);draw.hDC=dc;draw.rcItem=area;
        const auto state=GetMenuState(menu,item.position,MF_BYPOSITION);if(state&MF_HILITE)draw.itemState|=ODS_SELECTED;if(state&(MF_GRAYED|MF_DISABLED))draw.itemState|=ODS_DISABLED;
        draw_menu(owner,draw,false);
    }
    RestoreDC(dc,saved);if(!supplied)ReleaseDC(owner,dc);
}
bool measure_menu(HWND owner,MEASUREITEMSTRUCT &draw){
    const auto item=menu_item(draw.itemData);if(!item)return false;const auto dpi=GetDpiForWindow(owner);const int px=std::max(1,MulDiv(1,dpi,96));
    const auto dc=GetDC(owner);const auto font=reinterpret_cast<HFONT>(SendMessageW(owner,WM_GETFONT,0,0));const auto old=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));RECT bounds{};auto title=item->title;
    std::replace(title.begin(),title.end(),L'\t',L' ');DrawTextW(dc,title.c_str(),-1,&bounds,DT_CALCRECT|DT_SINGLELINE);SelectObject(dc,old);ReleaseDC(owner,dc);
    draw.itemWidth=bounds.right+MulDiv(item->bar?12:66,dpi,96);draw.itemHeight=(item->type&MFT_SEPARATOR)?8*px:item->bar?GetSystemMetricsForDpi(SM_CYMENU,dpi):MulDiv(26,dpi,96);return true;
}
bool draw_menu(HWND owner,const DRAWITEMSTRUCT &draw,bool gaps){
    const auto item=menu_item(draw.itemData);if(!item)return false;const int px=std::max(1,MulDiv(1,GetDpiForWindow(owner),96));
    const bool disabled=(draw.itemState&(ODS_DISABLED|ODS_GRAYED))!=0,selected=(draw.itemState&(ODS_SELECTED|ODS_HOTLIGHT))!=0;
    const auto ink=disabled?colors.muted:colors.text;fill(draw.hDC,draw.rcItem,selected?colors.selected:colors.surface);auto r=draw.rcItem;
    if(item->type&MFT_SEPARATOR){line(draw.hDC,r.left+24*px,(r.top+r.bottom)/2,r.right-8*px,(r.top+r.bottom)/2,colors.border);return true;}
    auto title=item->title;const auto tab=title.find(L'\t');const auto shortcut=tab==std::wstring::npos?L"":title.substr(tab+1);if(tab!=std::wstring::npos)title.resize(tab);
    auto text_area=r;text_area.left+=item->bar?6*px:28*px;text_area.right-=item->bar?6*px:24*px;
    const UINT format=DT_SINGLELINE|DT_VCENTER|((draw.itemState&ODS_NOACCEL)?DT_HIDEPREFIX:0);
    label(owner,draw.hDC,title,text_area,ink,format|DT_LEFT);if(!item->bar)label(owner,draw.hDC,shortcut,text_area,disabled?colors.muted:ink,format|DT_RIGHT);
    if(draw.itemState&ODS_CHECKED){const int x=r.left+9*px,y=(r.top+r.bottom)/2;line(draw.hDC,x-2*px,y,x,y+3*px,ink,2*px);line(draw.hDC,x,y+3*px,x+5*px,y-3*px,ink,2*px);}
    if(item->submenu&&!item->bar){const int x=r.right-10*px,y=(r.top+r.bottom)/2;line(draw.hDC,x-2*px,y-3*px,x+px,y,ink);line(draw.hDC,x+px,y,x-2*px,y+3*px,ink);}
    if(item->bar&&gaps)menu_gaps(owner);return true;
}
LRESULT menu_char(WPARAM w,HMENU menu){
    const auto p=themed_menus.find(menu);if(p==themed_menus.end())return 0;
    std::vector<int> matches;int highlighted=-1;
    for(const auto &item:p->second){const auto state=GetMenuState(menu,item.position,MF_BYPOSITION);if(state&MF_HILITE)highlighted=int(item.position);if(state&(MF_DISABLED|MF_GRAYED|MF_SEPARATOR))continue;
        const auto at=item.title.find(L'&');if(at!=std::wstring::npos&&at+1<item.title.size()&&towlower(item.title[at+1])==towlower(wchar_t(LOWORD(w))))matches.push_back(int(item.position));}
    if(matches.empty())return MAKELRESULT(0,MNC_IGNORE);if(matches.size()==1)return MAKELRESULT(matches[0],MNC_EXECUTE);
    const auto next=std::find_if(matches.begin(),matches.end(),[&](int index){return index>highlighted;});return MAKELRESULT(next==matches.end()?matches[0]:*next,MNC_SELECT);
}
