    static std::vector<UINT> context_ids(HMENU menu){
        std::vector<UINT> out;for(int n=0;n<GetMenuItemCount(menu);++n){const auto id=GetMenuItemID(menu,n);if(id&&id!=UINT(-1))out.push_back(id);}return out;
    }
    static std::wstring context_text(HMENU menu,UINT id){wchar_t text[2048]{};GetMenuStringW(menu,id,text,2048,MF_BYCOMMAND);return text;}
    void context_compact_empty_selected_animation_and_lazy_full_menu(){
        app.settings.confirm_unsaved=false;clipboard_text(L"unchanged context clipboard");const auto clipboard=GetClipboardSequenceNumber();
        unsigned popups{};
        app.dialogs.popup=[&](HWND,HMENU menu,POINT){++popups;
            CHECK(menu==app.contextual_root&&app.contextual_all&&!app.contextual_all_ready);
            CHECK(context_ids(menu)==std::vector<UINT>{Open,Paste,NewImage,ConfigureContextMenu});
            CHECK(GetMenuItemCount(app.contextual_all)==1&&GetClipboardSequenceNumber()==clipboard);return 0u;};
        command(PopupContext);CHECK(popups==1&&!app.contextual_root&&!app.image);
        app.replace(selection_fixture(128,96),false,true);app.selection={20,15,40,30};
        const auto original=app.image;const auto rgba=original->frames[0].pixels;app.settings.set_keys(Copy,{L"Ctrl+Shift+C"});
        app.dialogs.popup=[&](HWND,HMENU menu,POINT){++popups;const auto ids=context_ids(menu);
            CHECK(ids.front()==Copy&&context_text(menu,Copy).find(L"選択範囲をコピー")!=std::wstring::npos);
            CHECK(context_text(menu,Copy).find(L"Ctrl+Shift+C")!=std::wstring::npos);
            for(UINT id:{Cut,ClearSelection,InvertSelection,Crop,DeleteSelection})CHECK(std::find(ids.begin(),ids.end(),id)!=ids.end());
            CHECK(std::find(ids.begin(),ids.end(),FileDelete)==ids.end()&&!app.contextual_all_ready);
            CHECK(GetClipboardSequenceNumber()==clipboard);
            app.message(WM_INITMENUPOPUP,reinterpret_cast<WPARAM>(app.contextual_all),0);
            CHECK(app.contextual_all_ready&&GetMenuItemCount(app.contextual_all)==GetMenuItemCount(app.main_menu));
            CHECK(GetSubMenu(app.contextual_all,0)!=GetSubMenu(app.main_menu,0));return 0u;};
        app.pointer_down(1,{10,app.top+10},MK_RBUTTON);app.pointer_up(1,{10,app.top+10});
        CHECK(popups==2&&app.selection.x==20&&app.selection.y==15&&app.selection.w==40&&app.selection.h==30&&app.image==original&&original->frames[0].pixels==rgba);
        app.settings.context_menu_mode=1;app.dialogs.popup=[&](HWND,HMENU menu,POINT){++popups;CHECK(menu!=app.contextual_root&&GetMenuItemCount(menu)==GetMenuItemCount(app.main_menu));return 0u;};
        command(PopupContext);CHECK(popups==3);app.settings.context_menu_mode=0;
        app.settings.mouse.assign(MouseGesture::RightClick,0,0,Copy);app.pointer_down(1,{10,app.top+10},MK_RBUTTON);app.pointer_up(1,{10,app.top+10});
        CHECK(popups==3&&paste_image(app.hwnd).frames[0].pixels==selection_image(*original,app.selection).frames[0].pixels);
        open(fixture(L"blend.apng"));command(StopAnimation);
        app.dialogs.popup=[&](HWND,HMENU menu,POINT){const auto ids=context_ids(menu);for(UINT id:{Play,FramePrevious,FrameNext,Loop})CHECK(std::find(ids.begin(),ids.end(),id)!=ids.end());CHECK(context_text(menu,Play).starts_with(L"再生"));return UINT(FrameNext);};
        command(PopupContext);CHECK(app.player.frame==1&&notices.empty());
    }
    void context_keeps_floating_and_crop_until_explicit_apply_or_cancel(){
        app.settings.confirm_unsaved=false;app.replace(selection_fixture(64,48),false,true);const auto original=app.image;
        app.selection={12,10,24,20};command(SelectMove);CHECK(app.floating);const auto box=app.floating->box;
        unsigned shown{};app.dialogs.popup=[&](HWND,HMENU menu,POINT){++shown;const auto ids=context_ids(menu);CHECK(ids[0]==CommitSelection&&ids[1]==CancelSelection);
            CHECK(GetMenuState(menu,Copy,MF_BYCOMMAND)&MF_GRAYED);return 0u;};
        app.pointer_down(1,{10,app.top+10},MK_RBUTTON);app.pointer_up(1,{10,app.top+10});
        CHECK(shown==1&&app.floating&&app.floating->box.cx==box.cx&&app.floating->box.cy==box.cy&&app.image==original);
        app.dialogs.popup=[&](HWND,HMENU,POINT){return UINT(CancelSelection);};command(PopupContext);CHECK(!app.floating&&app.image==original);
        command(ToolCrop);CHECK(app.cropping);SetWindowTextW(app.editor_chrome.control(editor_id::W),L"32");SetWindowTextW(app.editor_chrome.control(editor_id::H),L"24");app.apply_editor_bounds();
        app.dialogs.popup=[&](HWND,HMENU menu,POINT){CHECK(app.cropping&&context_ids(menu).front()==CommitSelection);return 0u;};
        command(PopupContext);CHECK(app.cropping&&app.image==original);
        app.dialogs.popup=[&](HWND,HMENU,POINT){return UINT(CommitSelection);};command(PopupContext);
        CHECK(!app.cropping&&app.image->width==32&&app.image->height==24);command(Undo);CHECK(app.image==original&&notices.empty());
    }
    void context_preferences_customize_order_visibility_separators_apply_cancel_reset(){
        app.settings.confirm_unsaved=false;app.replace(selection_fixture(64,48),false,true);
        std::vector<std::wstring> errors;
        PreferencesDialog dialog(app.settings,[&](const Settings &s){app.apply_settings(s);},[&](HWND,const auto &e){errors.push_back(e);},PreferencePage::ContextMenu);
        CHECK(dialog.create(app.hwnd));ShowWindow(dialog.window(),SW_SHOWNOACTIVATE);
        const auto h=[&](int id){const auto c=dialog.control(id);CHECK(c);return c;};
        const auto click=[&](int id){SendMessageW(h(id),BM_CLICK,0,0);};
        CHECK(!IsWindowEnabled(h(preference_id::ContextList)));
        SendMessageW(h(preference_id::ContextMode),CB_SETCURSEL,2,0);
        SendMessageW(dialog.window(),WM_COMMAND,MAKEWPARAM(preference_id::ContextMode,CBN_SELCHANGE),reinterpret_cast<LPARAM>(h(preference_id::ContextMode)));
        CHECK(IsWindowEnabled(h(preference_id::ContextList)));
        SendMessageW(h(preference_id::ContextList),LB_SETCURSEL,0,0);click(preference_id::ContextDown);click(preference_id::ContextRemove);
        click(preference_id::ContextSeparatorButton);
        size_t index{};while(context_menu_choices()[index].id!=Copy)++index;SendMessageW(h(preference_id::ContextChoice),CB_SETCURSEL,index,0);click(preference_id::ContextAdd);
        click(preference_id::ApplyButton);CHECK(errors.empty()&&app.settings.context_menu_mode==2&&app.settings.context_menu_items.front()==Paste);
        CHECK(std::count(app.settings.context_menu_items.begin(),app.settings.context_menu_items.end(),Copy)==1);
        const auto saved=app.settings.context_menu_items;
        app.dialogs.popup=[&](HWND,HMENU menu,POINT){CHECK(context_ids(menu).front()==Paste);return 0u;};command(PopupContext);
        click(preference_id::ContextRemove);click(IDCANCEL);CHECK(app.settings.context_menu_items==saved);
        PreferencesDialog reset(app.settings,[&](const Settings &s){app.apply_settings(s);},[&](HWND,const auto &e){errors.push_back(e);},PreferencePage::ContextMenu);
        CHECK(reset.create(app.hwnd));ShowWindow(reset.window(),SW_SHOWNOACTIVATE);SendMessageW(reset.control(preference_id::Reset),BM_CLICK,0,0);SendMessageW(reset.control(preference_id::ApplyButton),BM_CLICK,0,0);
        CHECK(app.settings.context_menu_mode==0&&app.settings.context_menu_items==default_context_items());SendMessageW(reset.control(IDCANCEL),BM_CLICK,0,0);CHECK(errors.empty()&&notices.empty());
    }
    void feedback_minimum_deadline_survives_pending_timer_and_canvas_repaint(){
        app.settings.confirm_unsaved=false;app.replace(selection_fixture(64,48),false,true);app.settings.toast_duration=1;
        const auto before=GetTickCount64();command(Copy);CHECK(feedback_visible()&&app.toast.deadline()>=before+100);
        // This private window station has no physical update region: UpdateWindow
        // may not dispatch WM_PAINT here. Check visibility/deadline across parent
        // rendering; saved native captures separately check the toast pixels.
        const auto deadline=app.toast.deadline();app.message(WM_PAINT,0,0);
        CHECK(feedback_visible()&&app.toast.deadline()==deadline&&app.toast.text().starts_with(L"コピーしました"));
        app.feedback_begin(Copy,L"コピー中…");SendMessageW(app.toast.window(),WM_TIMER,1,0);CHECK(feedback_visible()&&app.toast.pending());
        const auto completed=GetTickCount64();app.feedback_done(Copy,L"コピーしました");CHECK(app.toast.deadline()>=completed+100);
        SendMessageW(app.toast.window(),WM_TIMER,1,0);CHECK(feedback_visible()&&!app.toast.pending());
        CHECK(paste_image(app.hwnd).frames[0].pixels==app.image->frames[0].pixels&&notices.empty());
    }
