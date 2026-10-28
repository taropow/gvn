    bool feedback_visible() const{return app.toast.window()&&(GetWindowLongPtrW(app.toast.window(),GWL_STYLE)&WS_VISIBLE);}
    void feedback_copy_paste_alpha_shortcuts_and_timer(){
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;
        app.replace(selection_fixture(64,48),false,true);const auto pixels=app.image->frames[0].pixels;
        SetFocus(app.hwnd);const auto focus=GetFocus();app.command(Copy,true);
        CHECK(feedback_visible()&&!app.toast.pending()&&app.toast.timer_active()&&command_pips==1);
        CHECK(app.toast.text().starts_with(L"コピーしました")&&app.toast.key()==L"Ctrl+C");
        CHECK(app.toast.deadline()>=GetTickCount64()+1900);
        CHECK(GetFocus()==focus&&GetParent(app.toast.window())==app.hwnd);
        CHECK(SendMessageW(app.toast.window(),WM_NCHITTEST,0,0)==HTTRANSPARENT);
        CHECK(GetWindowLongPtrW(app.toast.window(),GWL_EXSTYLE)&WS_EX_NOACTIVATE);
        CHECK(paste_image(app.hwnd).frames[0].pixels==pixels);
        app.feedback_sound_known=false;app.command(NewPaste,true);
        CHECK(command_pips==2&&app.toast.text().starts_with(L"貼り付けました")&&app.image->frames[0].pixels==pixels);
        RECT box{};GetWindowRect(app.toast.window(),&box);MapWindowPoints(nullptr,app.hwnd,reinterpret_cast<POINT *>(&box),2);const auto canvas=app.toast_area();
        CHECK(box.left>=canvas.left&&box.right<=canvas.right&&box.top>=canvas.top&&box.bottom<=canvas.bottom);
        auto next=app.settings;next.set_keys(Copy,{L"Ctrl+Shift+C"});next.toast_duration=500;app.apply_settings(next);app.command(Copy,true);
        CHECK(app.toast.key()==L"Ctrl+Shift+C");
        SendMessageW(app.toast.window(),WM_TIMER,1,0);CHECK(feedback_visible());
        const auto started=GetTickCount64();CHECK(app.toast.deadline()>=started+400&&app.toast.deadline()<=started+500);
        const auto limit=GetTickCount64()+2000;
        while(feedback_visible()&&GetTickCount64()<limit){pump();MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
        CHECK(!feedback_visible()&&!app.toast.timer_active()&&notices.empty()&&GetTickCount64()-started>=400);
    }
    void feedback_silent_paste_failures_keep_document_and_pending_edit(){
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;app.replace(selection_fixture(32,24),false,true);
        const auto image=app.image;const auto pixels=image->frames[0].pixels;
        clipboard_text(L"not an image");for(auto id:{Paste,NewPaste,PasteSelection,NewWindowPaste})command(id);
        CHECK(!app.toast.window()&&command_pips==0);
        CHECK(OpenClipboard(app.hwnd));CHECK(EmptyClipboard());auto bad=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,16);CHECK(bad);
        CHECK(SetClipboardData(RegisterClipboardFormatW(L"PNG"),bad));CHECK(CloseClipboard());
        command(Paste);CHECK(!feedback_visible()&&!app.toast.timer_active()&&!app.feedback_busy&&command_pips==0);
        command(ToolCrop);CHECK(app.cropping);clipboard_text(L"no image");command(Paste);CHECK(app.cropping);
        command(CancelSelection);app.toast.dismiss();const auto count=command_pips;
        command(Paste);CHECK(command_pips==count&&!feedback_visible());
        CHECK(app.image==image&&image->frames[0].pixels==pixels&&!app.modified&&notices.empty());
    }
    void feedback_settings_independent_sound_throttle_and_reused_window(){
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.replace(selection_fixture(32,24),false,true);
        app.settings.toast_enabled=false;command(Copy);CHECK(command_pips==1&&!app.toast.window());
        app.settings.command_sound=false;command(Copy);CHECK(command_pips==1&&!app.toast.window());
        app.settings.toast_enabled=true;command(Copy);CHECK(feedback_visible()&&command_pips==1);
        const auto window=app.toast.window();const auto gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        app.settings.command_sound=true;app.feedback_sound_known=false;command(Copy);const auto count=command_pips;
        for(int n=0;n<10;++n){app.feedback_last_sound=GetTickCount64();command(Copy);}
        CHECK(command_pips==count&&app.toast.window()==window&&GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdi+1);
        auto next=app.settings;next.toast_enabled=false;next.command_sound=false;app.apply_settings(next);
        CHECK(!feedback_visible()&&!app.toast.timer_active());
        app.dialogs.command_pip=[]{throw std::runtime_error("No audio device");};app.settings.command_sound=true;app.feedback_sound_known=false;
        command(Copy);CHECK(!feedback_visible()&&paste_image(app.hwnd).width==32);
    }
    void feedback_async_clipboard_file_complete_failure_and_cancel(){
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;
        clipboard_text(fixture(L"alpha.png").wstring());app.command(Paste,true);
        CHECK(app.loading&&app.load_feedback&&app.toast.pending()&&command_pips==0);
        wait();CHECK(!app.load_feedback&&!app.toast.pending()&&command_pips==1&&app.image->path.filename()==L"alpha.png");
        const auto image=app.image;const auto pixels=image->frames[0].pixels;const auto count=command_pips;
        clipboard_text(fixture(L"truncated.psd").wstring());command(Paste);wait();
        CHECK(app.image==image&&image->frames[0].pixels==pixels&&!app.load_feedback&&!feedback_visible()&&command_pips==count&&notices.empty());
        clipboard_text(fixture(L"alpha.png").wstring());command(Paste);CHECK(app.load_feedback);app.cancel_load();app.update();pump();
        CHECK(!app.load_feedback&&!feedback_visible()&&command_pips==count);
    }
    void feedback_edit_save_transfer_success_and_cancel(){
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;
        const auto source=copy(L"alpha.png",L"source/alpha.png");open(source);const auto original=app.image;
        const auto execute=[&](UINT id){app.feedback_sound_known=false;command(id);};
        execute(RotateRight);CHECK(app.toast.text().find(L"適用しました")!=std::wstring::npos&&app.image!=original);
        execute(Undo);CHECK(app.toast.text()==L"元に戻しました"&&app.image==original);
        execute(Redo);CHECK(app.toast.text()==L"やり直しました");
        const auto count=command_pips;resizes.push_back({});execute(Resize);CHECK(command_pips==count);
        paths.push_back(temporary/L"saved.png");execute(SaveAs);CHECK(std::filesystem::is_regular_file(temporary/L"saved.png")&&app.toast.text().find(L"保存しました")!=std::wstring::npos);
        const auto saved=command_pips;paths.push_back({});execute(SaveAs);CHECK(command_pips==saved);
        execute(CopyPath);CHECK(app.toast.text()==L"パスをコピーしました");
        auto target=temporary/L"copy";std::filesystem::create_directory(target);directories.push_back(target);execute(FileCopy);
        CHECK(std::filesystem::is_regular_file(target/app.image->path.filename())&&app.toast.text()==L"ファイルをコピーしました");
        const auto registered=temporary/L"registered";std::filesystem::create_directory(registered);
        app.settings.copy_destinations[0]=registered.wstring();app.settings.set_keys(CopyDestBase,{L"Ctrl+Alt+Shift+C"});
        app.command(CopyDestBase,true);CHECK(app.toast.key()==L"Ctrl+Alt+Shift+C"&&std::filesystem::is_regular_file(registered/app.image->path.filename()));
        execute(ArmDrag);CHECK(app.toast.text().find(L"ドラッグできます")!=std::wstring::npos);
        const auto before=command_pips;execute(ZoomIn);execute(Checker);CHECK(command_pips==before);
        app.settings.toast_view_commands=true;execute(Checker);CHECK(command_pips==before+1&&app.toast.text().find(L"オン")!=std::wstring::npos);
        CHECK(notices.empty());
    }
    void feedback_preferences_native_controls_apply_reset(){
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.replace(selection_fixture(32,24),false,true);command(Copy);
        std::vector<std::wstring> errors;
        PreferencesDialog dialog(app.settings,[&](const Settings &next){app.apply_settings(next);},[&](HWND,const auto &error){errors.push_back(error);});const auto window=dialog.create(app.hwnd);CHECK(window);ShowWindow(window,SW_SHOWNOACTIVATE);
        for(int id:{preference_id::ToastEnabled,preference_id::CommandSound})SendMessageW(dialog.control(id),BM_CLICK,0,0);
        SendMessageW(dialog.control(preference_id::ToastViewCommands),BM_CLICK,0,0);
        SetWindowTextW(dialog.control(preference_id::ToastDuration),L"500");SendMessageW(dialog.control(preference_id::ApplyButton),BM_CLICK,0,0);
        CHECK(errors.empty()&&!app.settings.toast_enabled&&!app.settings.command_sound&&app.settings.toast_view_commands&&app.settings.toast_duration==500);
        CHECK(!feedback_visible()&&!app.toast.timer_active());
        for(const auto text:{L"100",L"10000",L"500"}){
            SetWindowTextW(dialog.control(preference_id::ToastDuration),text);SendMessageW(dialog.control(preference_id::ApplyButton),BM_CLICK,0,0);
            CHECK(errors.empty()&&app.settings.toast_duration==std::stoi(text));
        }
        for(const auto text:{L"99",L"10001"}){
            const auto count=errors.size();SetWindowTextW(dialog.control(preference_id::ToastDuration),text);
            SendMessageW(dialog.control(preference_id::ApplyButton),BM_CLICK,0,0);CHECK(errors.size()==count+1&&app.settings.toast_duration==500);
        }
        SetWindowTextW(dialog.control(preference_id::ToastDuration),L"500");SendMessageW(dialog.control(preference_id::ToastEnabled),BM_CLICK,0,0);
        SendMessageW(dialog.control(preference_id::ApplyButton),BM_CLICK,0,0);SendMessageW(dialog.control(IDCANCEL),BM_CLICK,0,0);
        const auto before=GetTickCount64();command(Copy);CHECK(feedback_visible()&&app.toast.deadline()>=before+500&&app.toast.deadline()<=GetTickCount64()+500);
    }
