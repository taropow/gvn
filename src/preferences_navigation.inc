    HWND groups{},search{},search_results{};
    MouseSettings saved_mouse;
    std::vector<int> tab_pages;
    struct SearchTarget {int page{},field{};};std::vector<SearchTarget> search_targets;
    std::map<int,std::wstring> initial_values;
    int focused_field{};
    static constexpr const wchar_t *group_titles[]{L"基本",L"表示",L"操作",L"ファイル",L"再生",L"編集",L"連携"};
    static int group_for(int page){switch(static_cast<Page>(page)){
        case Page::General:case Page::Window:return 0;
        case Page::Background:case Page::Display:case Page::Zoom:case Page::Fullscreen:case Page::Bars:return 1;
        case Page::Keys:case Page::Mouse:case Page::MouseCustom:return 2;
        case Page::Move:case Page::FileOperations:case Page::Loading:return 3;
        case Page::Slideshow:return 4;case Page::Mask:return 5;case Page::External:return 6;default:return 0;}}
    std::wstring field_value(const Field &f) const{
        auto h=find(f.id);if(!h)return L"";
        if(f.kind==Kind::Check)return std::to_wstring(SendMessageW(h,BM_GETCHECK,0,0));
        if(f.kind==Kind::Combo)return std::to_wstring(SendMessageW(h,CB_GETCURSEL,0,0));
        const auto value=get_text(h);
        if(f.kind==Kind::Number||f.kind==Kind::Seconds){wchar_t *end{};const double number=wcstod(value.c_str(),&end);if(end&&!*end&&end!=value.c_str()&&std::isfinite(number))return std::to_wstring(number);}
        return f.kind==Kind::Color?folded(value):value;
    }
    void remember_fields(){for(const auto &f:schema)if(find(f.id)&&!initial_values.contains(f.id))initial_values[f.id]=field_value(f);}
    bool field_changed(const Field &f) const{
        if(f.id==cid::Wheel||f.id==cid::Middle){const auto subset=[&](const MouseSettings &mouse){std::vector<MouseBinding> result;for(const auto &b:mouse.bindings)if(f.id==cid::Wheel?(b.gesture==MouseGesture::WheelUp||b.gesture==MouseGesture::WheelDown):b.gesture==MouseGesture::MiddleClick)result.push_back(b);return result;};if(subset(draft.mouse)!=subset(saved_mouse))return true;}
        const auto p=initial_values.find(f.id);return p!=initial_values.end()&&p->second!=field_value(f);
    }
    void mark_fields(){if(building)return;for(const auto &f:schema)if(find(f.id)){
        const auto label=std::wstring(field_changed(f)?L"● ":L"")+f.label;const auto h=find(f.kind==Kind::Check?f.id:f.id+10000);
        if(h&&get_text(h)!=label)SetWindowTextW(h,label.c_str());}
    }
    void rebuild_tabs(int page){
        const int group=group_for(page);if(!tab_pages.empty()&&group_for(tab_pages[0])==group)return;
        SendMessageW(tab,WM_SETREDRAW,FALSE,0);TabCtrl_DeleteAllItems(tab);tab_pages.clear();
        for(int i=0;i<int(Page::Count);++i)if(group_for(i)==group){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t *>(titles[i]);TabCtrl_InsertItem(tab,int(tab_pages.size()),&item);tab_pages.push_back(i);}
        TabCtrl_SetCurSel(groups,group);SendMessageW(tab,WM_SETREDRAW,TRUE,0);InvalidateRect(tab,nullptr,TRUE);InvalidateRect(groups,nullptr,TRUE);
    }
    int page_index(int page) const{const auto p=std::find(tab_pages.begin(),tab_pages.end(),page);return p==tab_pages.end()?-1:int(p-tab_pages.begin());}
    static std::wstring folded(std::wstring value){for(auto &ch:value)ch=wchar_t(towlower(ch));return value;}
    std::wstring search_alias(const Field &f) const{
        if(f.id==cid::Wheel)return L" ホイール 上 下 拡大 縮小 wheel zoom";
        if(f.id==cid::UiTheme)return L" theme テーマ ダーク ライト dark light 外観";
        if(f.id==cid::UiDensity)return L" 密度 アイコン 大きさ コンパクト 余白";
        if(f.id==cid::Checker||f.id==cid::PasteNew||f.id==cid::Light||f.id==cid::Dark||f.id==cid::PreserveAlpha)return L" alpha アルファ 透明 クリップボード ペースト";
        return L"";
    }
    void search_changed(){
        const auto query=folded(get_text(search));SendMessageW(search_results,LB_RESETCONTENT,0,0);search_targets.clear();
        if(query.empty()){ShowWindow(search_results,SW_HIDE);return;}
        const auto matches=[&](const std::wstring &candidate){std::wistringstream words(query);std::wstring word;const auto haystack=folded(candidate);while(words>>word)if(haystack.find(word)==std::wstring::npos)return false;return true;};
        for(const auto &f:schema){const int page=int(f.page);const auto title=std::wstring(group_titles[group_for(page)])+L" › "+titles[page]+L" › "+f.label;
            if(matches(title+search_alias(f))){const auto row=SendMessageW(search_results,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));SendMessageW(search_results,LB_SETITEMDATA,row,search_targets.size());search_targets.push_back({page,f.id});}}
        for(auto [page,alias]:{std::pair{Page::Keys,L"ショートカット キー キーバインド shortcut"},{Page::MouseCustom,L"マウス ボタン ホイール 割り当て ジェスチャ"},{Page::External,L"連携 外部アプリ クリスタ CLIP STUDIO Photoshop"},{Page::FileOperations,L"保存 コピー 移動 振り分け先 フォルダ"}}){
            const auto title=std::wstring(titles[int(page)])+L" › "+alias;if(matches(title)){const auto row=SendMessageW(search_results,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));SendMessageW(search_results,LB_SETITEMDATA,row,search_targets.size());search_targets.push_back({int(page),0});}}
        if(search_targets.empty())SendMessageW(search_results,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"一致する設定がありません"));
        else SendMessageW(search_results,LB_SETCURSEL,0,0);
        RECT rc{};GetClientRect(hwnd,&rc);SetWindowPos(search_results,HWND_TOP,px(12),px(43),std::max(px(250),int(rc.right)-px(24)),px(200),SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
    void open_search_result(){
        const auto row=SendMessageW(search_results,LB_GETCURSEL,0,0);if(row<0||search_targets.empty())return;
        const auto index=SendMessageW(search_results,LB_GETITEMDATA,row,0);if(index<0||size_t(index)>=search_targets.size())return;const auto target=search_targets[index];
        SetWindowTextW(search,L"");ShowWindow(search_results,SW_HIDE);select(target.page);
        if(const auto c=find(target.field)){focused_field=target.field;EnableWindow(find(cid::ResetField),TRUE);reveal_control(c);SetFocus(c);InvalidateRect(c,nullptr,TRUE);}else SetFocus(tab);
    }
    void reset_field(int id){
        const auto p=std::find_if(schema.begin(),schema.end(),[&](const auto &f){return f.id==id;});if(p==schema.end())return;
        const auto &f=*p;Settings defaults;const bool previous=building;building=true;const auto c=find(id);
        if(f.kind==Kind::Check)SendMessageW(c,BM_SETCHECK,f.flag_value(defaults)?BST_CHECKED:BST_UNCHECKED,0);
        else if(f.kind==Kind::Combo)SendMessageW(c,CB_SETCURSEL,f.quality?display_quality_index(f.number_value(defaults)):f.number_value(defaults),0);
        else if(f.kind==Kind::Font){draft.overlay_family=defaults.overlay_family;draft.overlay_points=defaults.overlay_points;draft.overlay_weight=defaults.overlay_weight;draft.overlay_charset=defaults.overlay_charset;SetWindowTextW(c,font_label().c_str());}
        else{const auto value=f.kind==Kind::Color?hex(defaults.*f.color):f.kind==Kind::Seconds?std::to_wstring(f.number_value(defaults)/1000.0):std::to_wstring(f.number_value(defaults));SetWindowTextW(c,value.c_str());}
        building=previous;
        if(id==cid::Wheel){draft.mouse.basic_wheel(defaults.mouse.wheel_preset()!=0);fill_mouse();}
        if(id==cid::Middle){draft.mouse.basic_middle(defaults.mouse.middle_preset());fill_mouse();}
        changed();invalidate_preview();layout();
    }
    void related_settings(HWND anchor){
        auto menu=CreatePopupMenu();for(int page=0;page<int(Page::Count);++page)if(page!=selected&&(group_for(page)==group_for(selected)||page==int(Page::Keys)||page==int(Page::MouseCustom)))AppendMenuW(menu,MF_STRING,UINT(page+1),titles[page]);
        RECT rect{};GetWindowRect(anchor,&rect);ui_theme_menu(menu);const auto value=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,rect.left,rect.bottom,0,hwnd,nullptr);ui_destroy_menu(menu);if(value)select(int(value)-1);
    }
    static LRESULT CALLBACK search_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
        const auto self=reinterpret_cast<Impl *>(data);
        if(m==WM_GETDLGCODE&&((w==VK_RETURN)||(w==VK_DOWN)||(w==VK_ESCAPE)))return DLGC_WANTALLKEYS;
        if(m==WM_KEYDOWN){if(w==VK_RETURN){self->open_search_result();return 0;}if(w==VK_ESCAPE){SetWindowTextW(self->search,L"");SetFocus(self->search);return 0;}if(w==VK_DOWN&&h==self->search){SetFocus(self->search_results);return 0;}}
        return DefSubclassProc(h,m,w,l);
    }
