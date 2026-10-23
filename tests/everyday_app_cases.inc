    void everyday_damage_timer_and_minimized_updates() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;
        app.replace(selection_fixture(1024,768),false,true);command(Actual);
        app.view.centered=false;app.view.pan_x=app.view.pan_y=0;
        app.selection={50,50,200,150};app.update();
        CHECK(SUCCEEDED(app.render_scene()));ValidateRect(app.hwnd,nullptr);
        // A noninteractive station has no visible desktop update region. Record
        // the actual InvalidateRect arguments, then draw those regions natively.
        app.recorded_damage.clear();app.message(WM_TIMER,39,0);
        uint64_t pixels{};for(const auto &rect:app.recorded_damage)pixels+=uint64_t(rect.right-rect.left)*(rect.bottom-rect.top);
        CHECK(pixels>0&&pixels<30000);
        CHECK(SUCCEEDED(app.render_scene(nullptr,app.recorded_damage)));
        CHECK(app.renderer.last_damage_pixels<30000&&app.renderer.last_damage_regions>1);
        const auto original=app.image;
        app.renderer.pen_style.width=8;app.renderer.pen_style.effects.outline=true;
        app.renderer.pen_style.effects.outline_width=2;app.renderer.pen_style.effects.shadow=true;
        app.renderer.pen_style.effects.shadow_blur=3;app.renderer.pen_style.effects.shadow_offset=4;
        app.recorded_damage.clear();app.invalidate_pen_segment({130,120},{150,130});
        CHECK(app.recorded_damage.size()==1);const auto changed=app.recorded_damage[0];
        CHECK((changed.right-changed.left)*(changed.bottom-changed.top)<10000);
        open(fixture(L"blend.apng"));command(StopAnimation);
        auto now=std::chrono::steady_clock::now();app.animation_now=[&]{return now;};
        command(Play);CHECK(app.animation_timer&&app.animation_interval>=USER_TIMER_MINIMUM);
        const auto duration=app.player.durations[app.player.frame];
        CHECK(app.animation_interval==std::max<uint64_t>(USER_TIMER_MINIMUM,duration));
        const auto first=app.player.frame;now+=std::chrono::milliseconds(duration);
        app.message(WM_TIMER,1,0);CHECK(app.player.frame!=first);
        ShowWindow(app.hwnd,SW_MINIMIZE);CHECK(IsIconic(app.hwnd));
        NativeUiProbe probe{app.hwnd,app.status,app.playback.counter,app.slider};probe.reset();
        const auto hidden_frame=app.player.frame;
        now+=std::chrono::milliseconds(app.player.durations[hidden_frame]);app.message(WM_TIMER,1,0);
        CHECK(app.player.frame!=hidden_frame&&app.animation_timer);
        CHECK(probe.title==0&&probe.status==0&&probe.positions==0);
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);CHECK(!IsIconic(app.hwnd));app.update();
        CHECK(SendMessageW(app.slider,TBM_GETPOS,0,0)==LRESULT(app.player.frame));
        command(StopAnimation);CHECK(!app.animation_timer&&app.animation_interval==0);
        CHECK(original->width==1024&&original->height==768);
        CHECK(notices.empty());
    }
    void everyday_copy_and_export_reuse_preserve_pixels_alpha_origin_and_metadata() {
        app.settings.confirm_unsaved=false;app.replace(selection_fixture(128,96),false,true);
        const auto original=app.image;const auto rgba=original->frames[0].pixels;
        app.image_metadata=original->metadata;app.image_metadata->dpi_defined=true;
        app.image_metadata->dpi_x=144;app.image_metadata->dpi_y=288;
        command(Copy);auto copied=paste_selection_clipboard(app.hwnd);
        CHECK(copied.image.frames[0].pixels==rgba&&!copied.origin);
        CHECK(std::abs(copied.image.metadata.dpi_x-144)<.1&&std::abs(copied.image.metadata.dpi_y-288)<.1);
        const FloatPoint polygon[]{{12,10},{80,12},{40,70}};app.selection=SelectionRegion::polygon(polygon,128,96);
        const auto expected=selection_image(*original,app.selection);command(Copy);
        copied=paste_selection_clipboard(app.hwnd);CHECK(copied.origin&&copied.origin->x==app.selection.x&&copied.origin->y==app.selection.y);
        CHECK(copied.image.frames[0].pixels==expected.frames[0].pixels);
        app.selection={};app.image->frames.push_back(app.image->frames[0]);
        app.image->frames[1].pixels[1]={39,47,61,3};app.player.reset(*app.image);app.player.seek(1);
        command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==app.image->frames[1].pixels);
        app.player.playing=false;app.update();
        std::vector<std::filesystem::path> exports;
        app.dialogs.file_drag=[&](const auto &path){exports.push_back(path);CHECK(std::filesystem::is_regular_file(path));};
        app.export_drag();app.export_drag();CHECK(exports.size()==2&&exports[0]==exports[1]);
        CHECK(load_image(exports[0]).frames[0].pixels==app.image->frames[1].pixels);
        {std::ofstream changed(exports[0],std::ios::binary|std::ios::app);changed.put('!');}
        app.export_drag();CHECK(exports.back()!=exports[0]);
        const auto second=exports.back();SetWindowTextW(app.editor_chrome.control(editor_id::Name),L"different");
        app.export_drag();CHECK(exports.back()!=second&&exports.back().filename()==L"different.png");
        const auto third=exports.back();app.image_metadata->dpi_x=300;app.export_drag();CHECK(exports.back()!=third);
        app.begin_file_drag();const auto legacy=exports.back();app.begin_file_drag();CHECK(exports.back()==legacy);
        CHECK(app.image==original&&original->frames[0].pixels==rgba);
        CHECK(notices.empty());
    }
    void everyday_async_display_accepts_commands_preserves_quality_and_discards_stale_jobs() {
        app.settings.confirm_unsaved=false;app.settings.display_down=DisplayLanczos3;
        app.replace(selection_fixture(4096,2048),false,true);command(FitImage);
        const auto source=app.image;CHECK(app.view.scale<1&&app.asynchronous_display());
        const auto original=source->frames[0].pixels;
        const auto start=std::chrono::steady_clock::now();CHECK(SUCCEEDED(app.render_scene()));
        const auto receipt=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        CHECK(app.display_sampler);const bool editor=app.editor_mode;command(ToggleEditor);CHECK(app.editor_mode!=editor);
        const auto settle=[&]{const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(!app.renderer.sample_current(app.image.get(),app.player.frame,app.view)&&std::chrono::steady_clock::now()<deadline){
                app.render_scene();pump();Sleep(1);
            }
            CHECK(app.renderer.sample_current(app.image.get(),app.player.frame,app.view));};
        settle();CHECK(app.renderer.filter(app.view)==DisplayLanczos3&&app.renderer.source_bytes()==0);
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"ASYNC_DISPLAY {\"initial_receipt_ms\":"<<receipt<<",\"final_quality_ms\":"<<elapsed<<"}\n";
        app.message(WM_ENTERSIZEMOVE,0,0);app.view.resize(app.view.width+20,app.view.height+10);
        CHECK(SUCCEEDED(app.render_scene())&&app.renderer.source_bytes()==0);
        app.message(WM_EXITSIZEMOVE,0,0);settle();
        command(ZoomOut);CHECK(SUCCEEDED(app.render_scene()));
        app.replace(selection_fixture(32,24),false,true);pump();
        CHECK(app.image!=source&&app.image->width==32);
        CHECK(SUCCEEDED(app.render_scene()));
        if(app.display_sampler){CHECK(app.display_sampler->wait_idle(std::chrono::seconds(5)));pump();}
        CHECK(app.image->width==32&&source->frames[0].pixels==original);
        command(CloseImage);CHECK(!app.image);CHECK(notices.empty());
    }
