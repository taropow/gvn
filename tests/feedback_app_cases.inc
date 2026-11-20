    bool feedback_visible() const{return app.toast.window()&&(GetWindowLongPtrW(app.toast.window(),GWL_STYLE)&WS_VISIBLE);}
    void clipboard_replace_keeps_file_and_confirms_overwrite() {
        const auto first = copy(L"alpha.png", L"差し替え.png");
        const auto second = copy(L"alpha.png", L"second.png");
        const auto original = load_image(first);
        const auto original_bytes = read_file(first);
        app.settings.paste_mode = PasteReplaceImage;
        app.settings.confirm_overwrite = false;
        app.settings.save_dialog = true;
        open(first);
        const auto old_index = app.index;
        const auto old_file_count = app.files.size();
        const auto pasted = selection_fixture(7, 5);
        copy_image(app.hwnd, pasted, 0);
        command(SelectAll);
        command(Paste);
        CHECK(app.modified && app.selection.empty() && app.image->width == 7 && app.image->height == 5);
        CHECK(same_path(app.image->path, first) && app.current_name == first.filename().wstring());
        CHECK(app.requested == first && app.index == old_index && app.files.size() == old_file_count);
        CHECK(app.image->frames[0].pixels == pasted.frames[0].pixels && read_file(first) == original_bytes);
        command(Undo);
        CHECK(!app.modified && !app.clipboard_replacement && app.image->width == original.width &&
              app.image->height == original.height && app.image->frames[0].pixels == original.frames[0].pixels);
        command(Redo);
        CHECK(app.modified && app.clipboard_replacement && app.image->frames[0].pixels == pasted.frames[0].pixels);

        answers.push_back(IDNO);
        command(Save);
        CHECK(answers.empty() && save_folders.empty() && notices.back().find(first.filename().wstring()) != std::wstring::npos);
        CHECK(app.modified && read_file(first) == original_bytes);
        answers.push_back(IDYES);
        command(Save);
        CHECK(answers.empty() && save_folders.empty() && !app.modified && !app.clipboard_replacement);
        CHECK(load_image(first).frames[0].pixels == pasted.frames[0].pixels);

        command(Paste);
        answers.push_back(IDNO); // Discard the unsaved edit when opening another file.
        drop(second);
        wait();
        CHECK(answers.empty() && same_path(app.image->path, second) && !app.modified && !app.clipboard_replacement);
        clipboard_text(first.wstring());
        command(Paste);
        wait();
        CHECK(same_path(app.image->path, first) && !app.modified && !app.clipboard_replacement);
        copy_image(app.hwnd, pasted, 0);
        command(NewPaste);
        CHECK(app.image->path.empty() && !app.clipboard_replacement);
    }
    void clipboard_replace_protects_source_formats_and_warns_for_jpeg() {
        app.settings.paste_mode = PasteReplaceImage;
        app.settings.save_dialog = false;
        app.settings.confirm_overwrite = false;
        app.settings.gif_animation = false;
        const auto pasted = selection_fixture(7, 5);
        for (const auto name : {L"psd-8-0.psd", L"disposal.gif", L"images.zip", L"blend.apng"}) {
            const auto source = copy(name, name);
            const auto bytes = read_file(source);
            open(source);
            if (source.extension() == L".gif")
                CHECK(!app.image->animated() && app.image->metadata.source_animation);
            copy_image(app.hwnd, pasted, 0);
            command(Paste);
            CHECK(app.image->frames[0].pixels == pasted.frames[0].pixels);
            if (source.extension() == L".gif") {
                // Even a deliberate choice of the previous save path must leave the animation intact.
                paths.push_back(source);
                command(Save);
                CHECK(paths.empty() && app.modified && read_file(source) == bytes);
            }
            const auto output = temporary / (source.filename().wstring() + L".png");
            const auto saves = save_folders.size();
            paths.push_back(output);
            command(Save);
            CHECK(paths.empty() && save_folders.size() == saves + 1);
            CHECK(read_file(source) == bytes && load_image(output).frames[0].pixels == pasted.frames[0].pixels);
        }
        const auto jpeg = copy(L"rgb.jpg", L"source.jpg");
        const auto jpeg_bytes = read_file(jpeg);
        open(jpeg);
        copy_image(app.hwnd, pasted, 0);
        command(Paste);
        answers = {IDYES, IDCANCEL};
        const auto notice_count = notices.size();
        const auto save_count = save_folders.size();
        command(Save);
        CHECK(answers.empty() && app.modified && read_file(jpeg) == jpeg_bytes);
        CHECK(save_folders.size() == save_count && notices.size() == notice_count + 2);
        CHECK(notices[notice_count].find(jpeg.filename().wstring()) != std::wstring::npos &&
              notices.back().find(L"半透明") != std::wstring::npos);
        answers = {IDYES, IDOK};
        command(Save);
        const auto saved = load_image(jpeg);
        CHECK(answers.empty() && !app.modified && saved.width == pasted.width && saved.height == pasted.height);
    }
    void edit_menu_copy_on_white_in_png_dib_and_async() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);
        const auto menu_state=[&]{
            const auto edit=GetSubMenu(app.main_menu,1);
            SendMessageW(app.hwnd,WM_INITMENUPOPUP,reinterpret_cast<WPARAM>(edit),1);
            return GetMenuState(edit,CopyOnWhite,MF_BYCOMMAND);
        };
        CHECK(menu_state()!=UINT(-1)&&(menu_state()&MF_GRAYED));
        const Pixel transparent{15,22,38,0},red{255,0,0,128},solid{12,34,56,255};
        const Pixel white{255,255,255,255},pink{255,127,127,255};
        auto image=new_image(4,4);std::ranges::fill(image.frames[0].pixels,solid);
        image.frames[0].pixels[0]=transparent;image.frames[0].pixels[1]=red;
        app.replace(std::move(image),false,true);
        const auto source=app.image;const auto pixels=source->frames[0].pixels;
        CHECK(menu_state()!=UINT(-1)&&!(menu_state()&MF_GRAYED));
        auto verify_both=[&](const std::vector<Pixel> &expected){
            CHECK(paste_image(app.hwnd).frames[0].pixels==expected);
            CHECK(OpenClipboard(app.hwnd));
            struct Close{~Close(){CloseClipboard();}} close;
            const auto handle=GetClipboardData(CF_DIBV5);CHECK(handle);
            const auto *data=static_cast<const uint8_t *>(GlobalLock(handle));CHECK(data);
            struct Unlock{HGLOBAL value;~Unlock(){GlobalUnlock(value);}} unlock{handle};
            CHECK(read_dib({data,GlobalSize(handle)}).frames[0].pixels==expected);
        };
        auto expected=pixels;expected[0]=white;expected[1]=pink;
        SendMessageW(app.hwnd,WM_COMMAND,CopyOnWhite,0);
        verify_both(expected);CHECK(app.toast.text().starts_with(L"白背景でコピーしました"));
        CHECK(app.image==source&&app.image->frames[0].pixels==pixels&&!app.modified);
        command(Copy);verify_both(pixels);

        const std::array<FloatPoint,3> triangle{{{0,0},{5,0},{0,5}}};
        app.selection=SelectionRegion::polygon(triangle,4,4);
        CHECK(!app.selection.empty()&&app.selection.contains(1,0)&&!app.selection.contains(3,3));
        command(CopyOnWhite);
        const auto selected=paste_selection_clipboard(app.hwnd);
        CHECK(selected.origin&&selected.origin->canvas_width==4&&selected.image.width==4&&selected.image.height==4);
        CHECK(selected.image.frames[0].pixels[1]==pink&&selected.image.frames[0].pixels.back()==white);
        CHECK(app.selection.contains(1,0)&&app.image==source);

        auto large=new_image(1024,1024);std::ranges::fill(large.frames[0].pixels,red);app.replace(std::move(large),false,true);
        command(CopyOnWhite);CHECK(app.copy_feedback&&app.clipboard_worker);
        CHECK(app.clipboard_worker->wait_idle(std::chrono::seconds(10)));pump();
        CHECK(!app.copy_feedback&&app.toast.text().starts_with(L"白背景でコピーしました"));
        verify_both(std::vector<Pixel>(1024*1024,pink));
        CHECK(app.image->frames[0].pixels.front()==red&&notices.empty());
    }
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
        RECT box{};GetWindowRect(app.toast.window(),&box);MONITORINFO monitor{sizeof(monitor)};
        CHECK(GetMonitorInfoW(MonitorFromWindow(app.hwnd,MONITOR_DEFAULTTONEAREST),&monitor));
        CHECK(box.left>=monitor.rcWork.left&&box.right<=monitor.rcWork.right&&box.top>=monitor.rcWork.top&&box.bottom<=monitor.rcWork.bottom);
        CHECK((GetWindowLongPtrW(app.toast.window(),GWL_STYLE)&WS_POPUP)&&!(GetWindowLongPtrW(app.toast.window(),GWL_STYLE)&WS_CHILD));
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
