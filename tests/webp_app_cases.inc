    void webp_open_navigate_copy_export_and_setting_reload_use_native_pixels() {
        app.settings.animation_autoplay=false;
        const auto still=copy(L"webp/alpha.webp",L"webp-open/1.WEBP");
        const auto movie=copy(L"webp/animation.webp",L"webp-open/2.webp");
        open(still);CHECK(app.image->format==L"WebP"&&app.files.size()==2&&app.image->frames[0].pixels[1].a==128);
        const auto source=app.image;command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==source->frames[0].pixels);
        command(Next);wait();CHECK(app.requested==movie&&app.image->frames.size()==3);
        CHECK(app.player.file_loops==2);command(FrameNext);CHECK(app.player.frame==1&&!app.player.playing);
        const auto frame=app.image->frames[1].pixels;command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==frame);
        const auto saved=temporary/L"webp-frame.png";paths.push_back(saved);command(SaveAs);
        CHECK(load_image(saved).frames[0].pixels==frame);
        auto options=app.settings;options.webp_animation=false;app.apply_settings(options);wait();
        CHECK(app.image->frames.size()==1&&app.image->frames[0].pixels==load_image(movie).frames[0].pixels);
        options.webp_animation=true;app.apply_settings(options);wait();CHECK(app.image->frames.size()==3);
        options.webp_transparency=false;app.apply_settings(options);wait();
        for(const auto &f:app.image->frames)for(const auto &p:f.pixels)CHECK(p.a==255);
        Settings persisted;persisted.load(app.config);CHECK(persisted.webp_animation&&!persisted.webp_transparency);
    }
    void webp_preferences_native_controls_apply_cancel_reset_and_reload() {
        const auto source=copy(L"webp/animation.webp",L"webp-settings/movie.webp");open(source);
        unsigned commits{};
        PreferencesDialog dialog(app.settings,[&](const Settings &value){++commits;app.apply_settings(value);},{},PreferencePage::Loading);
        dialog.create(app.hwnd);
        for(int id:{preference_id::WebpTransparency,preference_id::WebpAnimation}) {
            const auto h=dialog.control(id);CHECK(IsWindow(h)&&SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED);
            SendMessageW(h,BM_SETCHECK,BST_UNCHECKED,0);
            SendMessageW(dialog.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(h));
        }
        SendMessageW(dialog.window(),WM_COMMAND,preference_id::ApplyButton,0);wait();
        CHECK(commits==1&&!app.settings.webp_animation&&!app.settings.webp_transparency&&app.image->frames.size()==1);
        for(const auto &p:app.image->frames[0].pixels)CHECK(p.a==255);
        Settings saved;saved.load(app.config);CHECK(!saved.webp_animation&&!saved.webp_transparency);
        SendMessageW(dialog.window(),WM_COMMAND,preference_id::Reset,0);
        SendMessageW(dialog.window(),WM_COMMAND,IDOK,0);wait();
        CHECK(commits==2&&app.settings.webp_animation&&app.settings.webp_transparency&&app.image->frames.size()==3);
        PreferencesDialog cancel(app.settings,[&](const Settings &){++commits;},{},PreferencePage::Loading);
        cancel.create(app.hwnd);SendMessageW(cancel.control(preference_id::WebpAnimation),BM_SETCHECK,BST_UNCHECKED,0);
        SendMessageW(cancel.window(),WM_COMMAND,IDCANCEL,0);CHECK(commits==2&&app.settings.webp_animation);
    }
    void webp_save_as_overwrite_metadata_and_undo_preserve_alpha() {
        app.settings.save_dialog=false;app.settings.confirm_overwrite=false;app.settings.save_as_retarget=true;
        const auto original_path=copy(L"webp/alpha.webp",L"webp-save/source.webp");open(original_path);
        const auto original=app.image->frames[0].pixels;
        const auto target=temporary/L"saved.webp";paths.push_back(target);command(SaveAs);
        CHECK(load_image(target).frames[0].pixels==original&&app.image->path==target);
        CHECK(app.current_metadata().source_format==L"WebP"&&app.settings.saving.last_format==SaveFormat::Webp);
        CHECK(answers.empty()&&notices.empty()); // Lossless alpha WebP needs no loss warning.
        command(Negative);const auto edited=app.image->frames[0].pixels;command(Save);
        CHECK(paths.empty()&&!app.modified&&load_image(target).frames[0].pixels==edited);
        command(Undo);CHECK(app.image->frames[0].pixels==original&&app.modified);
        Settings persisted;persisted.load(app.config);CHECK(persisted.saving.last_format==SaveFormat::Webp&&persisted.saving.webp.lossless);
        const auto format=app.editor_chrome.control(editor_id::Format);
        CHECK(SendMessageW(format,CB_GETCOUNT,0,0)==5);SendMessageW(format,CB_SETCURSEL,4,0);
        CHECK(app.editor_chrome.export_format()==SaveFormat::Webp);
    }
    void webp_save_animation_frame_keeps_original_movie_and_undo() {
        app.settings.animation_autoplay=false;
        const auto movie=copy(L"webp/animation.webp",L"webp-save/movie.webp");open(movie);
        const auto source=app.image;const auto bytes=read_file(movie);const auto undo=app.undo_image;
        command(FrameNext);const auto expected=source->frames[1].pixels;
        const auto target=temporary/L"frame.webp";paths.push_back(target);command(SaveAs);
        CHECK(load_image(target).frames.size()==1&&load_image(target).frames[0].pixels==expected);
        CHECK(app.image==source&&app.image->frames.size()==3&&app.undo_image==undo&&read_file(movie)==bytes&&answers.empty());
    }
