    void modern_preferences_search_reset_categories_and_theme_apply(){
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.replace(new_image(320,200),false,true);const auto pixels=app.image->frames[0].pixels;
        const auto keys=app.settings.keys_for(Copy);int applies{};
        PreferencesDialog dialog(app.settings,[&](const auto &next){++applies;app.apply_settings(next);});const auto window=dialog.create(app.hwnd);CHECK(window);ShowWindow(window,SW_SHOWNOACTIVATE);ShowWindow(window,SW_SHOWNOACTIVATE);
        CHECK(TabCtrl_GetItemCount(dialog.control(preference_id::Groups))==7);
        for(int i=0;i<int(PreferencePage::Count);++i){dialog.select_page(static_cast<PreferencePage>(i));CHECK(dialog.selected_page()==i&&dialog.page_tab_index(static_cast<PreferencePage>(i))>=0);}
        for(int group=0;group<7;++group){auto tabs=dialog.control(preference_id::Groups);TabCtrl_SetCurSel(tabs,group);NMHDR notify{tabs,preference_id::Groups,TCN_SELCHANGE};SendMessageW(window,WM_NOTIFY,preference_id::Groups,reinterpret_cast<LPARAM>(&notify));CHECK(TabCtrl_GetCurSel(tabs)==group);}
        auto search=dialog.control(preference_id::Search),results=dialog.control(preference_id::SearchResults);
        const auto type_query=[&](const wchar_t *query){SetFocus(search);SetWindowTextW(search,L"");for(const wchar_t *p=query;*p;++p)SendMessageW(search,WM_CHAR,*p,1);};
        // App integration hosts keep top-level windows hidden; the separate
        // autopilot verifies the visible popup and actual keyboard focus.
        type_query(L"wheel zoom");CHECK((GetWindowLongPtrW(results,GWL_STYLE)&WS_VISIBLE)&&SendMessageW(results,LB_GETCOUNT,0,0)>0);
        SendMessageW(results,LB_SETCURSEL,0,0);SendMessageW(results,WM_KEYDOWN,VK_RETURN,0);
        CHECK(dialog.selected_page()==int(PreferencePage::Mouse));
        const auto page=GetParent(dialog.control(preference_id::Wheel));const auto initial_wheel=SendMessageW(dialog.control(preference_id::Wheel),CB_GETCURSEL,0,0);SendMessageW(dialog.control(preference_id::Wheel),CB_SETCURSEL,initial_wheel==0?1:0,0);
        SendMessageW(page,WM_COMMAND,MAKEWPARAM(preference_id::Wheel,CBN_SELCHANGE),reinterpret_cast<LPARAM>(dialog.control(preference_id::Wheel)));
        wchar_t label[256]{};GetWindowTextW(dialog.control(preference_id::Wheel+10000),label,256);CHECK(std::wstring(label).starts_with(L"● "));
        SendMessageW(dialog.control(preference_id::ResetField),BM_CLICK,0,0);CHECK(SendMessageW(dialog.control(preference_id::Wheel),CB_GETCURSEL,0,0)==Settings{}.wheel);
        type_query(L"dark");CHECK(SendMessageW(results,LB_GETCOUNT,0,0)==1);SendMessageW(results,WM_KEYDOWN,VK_RETURN,0);
        CHECK(dialog.selected_page()==int(PreferencePage::General));
        SendMessageW(dialog.control(preference_id::UiTheme),CB_SETCURSEL,2,0);SendMessageW(GetParent(dialog.control(preference_id::UiTheme)),WM_COMMAND,MAKEWPARAM(preference_id::UiTheme,CBN_SELCHANGE),0);
        SendMessageW(dialog.control(preference_id::ApplyButton),BM_CLICK,0,0);CHECK(applies==1&&app.settings.ui_theme==2&&ui_palette().dark&&app.settings.keys_for(Copy)==keys);
        CHECK(app.image->frames[0].pixels==pixels);
        type_query(L"存在しない検索語xyz");CHECK(SendMessageW(results,LB_GETCOUNT,0,0)==1);SendMessageW(results,WM_KEYDOWN,VK_RETURN,0);CHECK(IsWindow(window));
        SendMessageW(dialog.control(IDCANCEL),BM_CLICK,0,0);CHECK(!IsWindow(window)&&app.settings.ui_theme==2);
    }
    void modern_theme_overflow_density_and_stable_native_resources(){
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);auto next=app.settings;next.ui_theme=1;next.ui_density=0;next.toolbar_overflow=true;next.window_toolbar_min=false;app.apply_settings(next);
        app.replace(new_image(320,200),false,true);const auto original=app.image;
        SetWindowPos(app.hwnd,nullptr,0,0,360,600,SWP_NOMOVE|SWP_NOZORDER);CHECK(!app.toolbar.hidden_commands().empty());CHECK(!(SendMessageW(app.toolbar.hwnd,TB_GETSTATE,MoreCommands,0)&TBSTATE_HIDDEN));
        CHECK(!ui_palette().dark);const auto generation=ui_theme_generation();const auto gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        for(int n=0;n<5;++n){app.layout();app.update();CHECK(ui_theme_generation()==generation);}
        CHECK(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+2);
        next=app.settings;next.ui_theme=2;next.ui_density=1;app.apply_settings(next);CHECK(ui_palette().dark&&ui_button_size(false)==36&&app.image==original);
        next=app.settings;next.toolbar_overflow=false;app.apply_settings(next);app.layout();CHECK(app.toolbar.hidden_commands().empty()&&(SendMessageW(app.toolbar.hwnd,TB_GETSTATE,MoreCommands,0)&TBSTATE_HIDDEN));
        CHECK(app.top>32&&app.image==original);
        next=app.settings;next.ui_theme=1;next.ui_density=0;next.toolbar_overflow=true;app.apply_settings(next);SetWindowPos(app.hwnd,nullptr,0,0,1648,760,SWP_NOMOVE|SWP_NOZORDER);
        CHECK(app.toolbar.hidden_commands().empty()&&(SendMessageW(app.toolbar.hwnd,TB_GETSTATE,MoreCommands,0)&TBSTATE_HIDDEN));
        open(fixture(L"disposal.gif"));for(bool large:{false,true})for(int width:{360,1100}){next=app.settings;next.ui_density=1;next.toolbar_large=large;app.apply_settings(next);SetWindowPos(app.hwnd,nullptr,0,0,width,700,SWP_NOMOVE|SWP_NOZORDER);RECT area{},icons{};GetClientRect(app.playback.hwnd,&area);GetWindowRect(app.playback.toolbar.hwnd,&icons);MapWindowPoints(nullptr,app.playback.hwnd,reinterpret_cast<POINT *>(&icons),2);CHECK(icons.bottom<=area.bottom&&icons.right<=area.right);}
        int information{};app.dialogs.image_info=[&](const auto &...){++information;return false;};NMMOUSE click{};click.hdr={app.status,0,NM_CLICK};click.dwItemSpec=0;SendMessageW(app.hwnd,WM_NOTIFY,0,reinterpret_cast<LPARAM>(&click));CHECK(information==1);
    }
    void modern_details_state_and_command_shortcuts_preserve_edits(){
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;app.replace(new_image(320,200),false,true);const auto original=app.image;
        command(ToolPen);CHECK(!skitch_visible(app.editor_chrome.control(editor_id::ShadowBlur)));
        SendMessageW(app.editor_chrome.control(editor_id::Details),BM_CLICK,0,0);CHECK(skitch_visible(app.editor_chrome.control(editor_id::ShadowBlur)));
        SetWindowTextW(app.editor_chrome.control(editor_id::ShadowBlur),L"7");SendMessageW(app.editor_chrome.control(editor_id::Details),BM_CLICK,0,0);CHECK(!skitch_visible(app.editor_chrome.control(editor_id::ShadowBlur)));
        SendMessageW(app.editor_chrome.control(editor_id::Details),BM_CLICK,0,0);CHECK(app.editor_chrome.text(editor_id::ShadowBlur)==L"7");
        command(Actual);selection_mouse(WM_LBUTTONDOWN,20,20,MK_LBUTTON);CHECK(app.editor_session().activity==EditorActivity::Pen);selection_mouse(WM_LBUTTONUP,80,40);CHECK(app.modified);command(Undo);CHECK(app.image==original);
        command(ToolCrop);CHECK(app.editor_session().activity==EditorActivity::Crop&&!SendMessageW(app.toolbar.hwnd,TB_ISBUTTONENABLED,Save,0));command(CancelSelection);CHECK(app.image==original);
        auto next=app.settings;next.set_keys(Copy,{L"Ctrl+Shift+C"});app.apply_settings(next);wchar_t text[256]{};SendMessageW(app.toolbar.hwnd,TB_GETBUTTONTEXTW,Copy,reinterpret_cast<LPARAM>(text));CHECK(std::wstring(text).find(L"Ctrl+Shift+C")!=std::wstring::npos);
        command(ToolText);selection_mouse(WM_LBUTTONDOWN,40,40,MK_LBUTTON);CHECK(app.editor_session().activity==EditorActivity::Text);command(CancelSelection);CHECK(app.image==original&&app.editor_session().activity==EditorActivity::Ready);
    }
