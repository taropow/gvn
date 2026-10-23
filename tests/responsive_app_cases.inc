    struct NativeUiProbe {
        size_t title{},status{},rows{},positions{},ranges{},bitmaps{};
        std::vector<HWND> windows;
        static LRESULT CALLBACK count(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
            auto &self=*reinterpret_cast<NativeUiProbe *>(data);
            if(m==WM_SETTEXT)++self.title;
            if(m==SB_SETTEXTW)++self.status;
            if(m==TB_SETROWS)++self.rows;
            if(m==TBM_SETPOS)++self.positions;
            if(m==TBM_SETRANGEMAX||m==TBM_SETRANGEMIN)++self.ranges;
            if(m==TB_CHANGEBITMAP)++self.bitmaps;
            return DefSubclassProc(h,m,w,l);
        }
        NativeUiProbe(std::initializer_list<HWND> handles):windows(handles) {
            for(auto h:windows)SetWindowSubclass(h,count,901,reinterpret_cast<DWORD_PTR>(this));
        }
        void reset(){title=status=rows=positions=ranges=bitmaps=0;}
        ~NativeUiProbe(){for(auto h:windows)RemoveWindowSubclass(h,count,901);}
    };
    // Reproducible native UI benchmark. Decoding and D2D drawing are intentionally
    // excluded so the measurements isolate presentation and window layout work.
    void responsive_native_update_and_layout_benchmark() {
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        app.settings.confirm_unsaved = false;
        app.replace(selection_fixture(2048,1536),false,true);
        const auto image=app.image;
        const auto pixels=image->frames[0].pixels;
        NativeUiProbe messages{app.hwnd,app.status,app.editor_tools.hwnd};
        const auto measure=[&](const char *name,int count,const auto &action) {
            for(int n=0;n<3;++n)action(n);
            std::array<double,5> samples{};messages.reset();
            for(auto &sample:samples){
                const auto start=std::chrono::steady_clock::now();
                for(int n=0;n<count;++n)action(n);
                sample=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/count;
            }
            std::cout<<"NATIVE_PERF {\"case\":\""<<name<<"\",\"iterations\":"<<count*5<<",\"samples_us\":[";
            for(size_t n=0;n<samples.size();++n)std::cout<<(n?",":"")<<samples[n];
            std::cout<<"],\"title_writes\":"<<messages.title<<",\"status_writes\":"<<messages.status<<",\"tool_row_layouts\":"<<messages.rows<<"}\n";
        };
        measure("view_update",50,[&](int){app.update();});
        app.view.manual=true;
        measure("zoom_update",50,[&](int n){app.view.scale=n%2?0.5:1.;app.update();});
        command(ToggleEditor);
        measure("editor_update",50,[&](int){app.update();});
        measure("editor_layout",20,[&](int){app.layout();});
        measure("editor_resize",20,[&](int n){SetWindowPos(app.hwnd,nullptr,0,0,n%2?900:1000,700,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);});
        CHECK(app.image==image&&image->frames[0].pixels==pixels&&!app.modified&&notices.empty());
    }
    void responsive_native_updates_preserve_status_theme_and_document() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);
        app.settings.confirm_unsaved=false;app.replace(selection_fixture(320,240),false,true);
        command(ToggleEditor);app.layout();app.update();
        const auto image=app.image;const auto pixels=image->frames[0].pixels;
        const auto palette=ui_theme_generation();
        {
            NativeUiProbe probe{app.hwnd,app.status,app.editor_tools.hwnd};
            for(int n=0;n<20;++n){app.layout();app.update();}
            CHECK(probe.title==0&&probe.status==0&&probe.rows==0&&ui_theme_generation()==palette);
        }
        app.view.manual=true;app.view.scale=.25;app.modified=true;app.update();
        wchar_t title[1024]{};GetWindowTextW(app.hwnd,title,1024);
        CHECK(std::wstring(title).starts_with(L"* ")&&std::wstring(title).find(L"25 %")!=std::wstring::npos);
        CHECK(std::wstring(title)==app.renderer.overlay.text&&status(2)==L"25.0 %");
        app.settings.window_toolbar_min=false;
        for(int width:{380,1200,700}){
            SetWindowPos(app.hwnd,nullptr,0,0,width,680,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            CHECK(status(0)==L"320 × 240  RGBA"&&status(2)==L"25.0 %"&&status(4)==app.editor_session().hint());
        }
        for(int theme:{1,2,0}){
            auto next=app.settings;next.ui_theme=theme;next.ui_density=theme%2;app.apply_settings(next);
            CHECK(app.applied_theme==theme&&app.applied_density==next.ui_density);
            app.message(WM_SETTINGCHANGE,0,0);app.message(WM_THEMECHANGED,0,0);
            CHECK(app.applied_palette==ui_theme_generation());
            if(theme!=0)CHECK(ui_palette().dark==(theme==2));
            CHECK(SUCCEEDED(app.render_scene()));
        }
        CHECK(app.image==image&&image->frames[0].pixels==pixels&&app.modified);
        command(CloseImage);CHECK(!app.image&&status(0).find(L"タップすると")!=std::wstring::npos);
        app.replace(selection_fixture(32,24),false,true);CHECK(status(0)==L"32 × 24  RGBA");
        CHECK(notices.empty());
    }
    void responsive_animation_controls_only_write_changed_values() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);
        app.settings.confirm_unsaved=false;open(fixture(L"disposal.gif"));command(StopAnimation);app.update();
        const auto original=app.image;const auto pixels=original->frames[0].pixels;
        {
            NativeUiProbe probe{app.playback.counter,app.slider,app.playback.toolbar.hwnd};
            for(int n=0;n<20;++n)app.update();
            CHECK(probe.title==0&&probe.positions==0&&probe.ranges==0&&probe.bitmaps==0);
            app.player.seek(1);app.update();
            CHECK(probe.title==1&&probe.positions==1&&probe.ranges==0);
            CHECK(SendMessageW(app.slider,TBM_GETPOS,0,0)==1);
        }
        SendMessageW(app.slider,TBM_SETPOS,TRUE,0);
        app.message(WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(app.slider));
        CHECK(app.player.frame==0&&!app.player.playing);
        command(Play);CHECK(app.player.playing);
        auto next=app.settings;next.ui_theme=ui_palette().dark?1:2;app.apply_settings(next);
        wchar_t text[128]{};SendMessageW(app.playback.toolbar.hwnd,TB_GETBUTTONTEXTW,Play,reinterpret_cast<LPARAM>(text));
        CHECK(std::wstring(text)==L"一時停止"&&app.player.playing);
        command(StopAnimation);open(fixture(L"blend.apng"));command(StopAnimation);
        CHECK(app.player.frame==0&&SendMessageW(app.slider,TBM_GETPOS,0,0)==0);
        CHECK(SendMessageW(app.slider,TBM_GETRANGEMAX,0,0)==LRESULT(app.image->frames.size()-1));
        CHECK(original->frames[0].pixels==pixels&&!app.modified&&notices.empty());
    }
