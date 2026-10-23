    // Theme discovery and native control traversal belong to configuration,
    // system-theme and DPI changes, rather than each zoom/frame update.
    int applied_theme{-1},applied_density{-1};
    uint64_t applied_palette{};
    UINT applied_theme_dpi{};
    void sync_ui_theme(bool system_changed=false) {
        if(system_changed||applied_theme!=settings.ui_theme||applied_density!=settings.ui_density){
            set_ui_theme(settings.ui_theme,settings.ui_density);
            applied_theme=settings.ui_theme;applied_density=settings.ui_density;
        }
        const auto dpi=GetDpiForWindow(hwnd);
        const auto palette=ui_theme_generation();
        if(system_changed||applied_palette!=palette||applied_theme_dpi!=dpi){
            apply_ui_theme(hwnd);applied_palette=palette;applied_theme_dpi=dpi;
        }
    }
    void update_caption_and_status() {
        std::wostringstream title;
        if (image) {
            title << (modified ? L"* " : L"") << caption_name() << L" <" << (view.manual ? L"" : L"Fit ")
                  << std::fixed << std::setprecision(0) << view.scale * 100 << L" %> — GVN";
            if (!status_shown && !files.empty() && !removed_current)
                title << L" (" << index + 1 << L" / " << files.size() << L")";
            status_text(0,
                        std::to_wstring(image->width) + L" × " + std::to_wstring(image->height) + L"  RGBA");
            status_text(1, files.empty() || removed_current
                               ? L"—"
                               : std::to_wstring(index + 1) + L" / " + std::to_wstring(files.size()));
            std::wostringstream scale;
            scale << std::fixed << std::setprecision(1) << view.scale * 100 << L" %";
            status_text(2, scale.str());
            status_text(3, size_text(image->file_bytes) + L" / " + size_text(image->bytes()));
        } else {
            title << (folder.empty() ? L"GVN Image Viewer" : folder.wstring() + L" — GVN");
            status_text(0, loading ? L"画像を読み込み中です…"
                                  : L"画像を開いてください。タップするとクリップボードの内容がペーストされます");
        }
        auto next_title=title.str();
        if(renderer.overlay.text!=next_title){
            renderer.overlay.text=std::move(next_title);
            SetWindowTextW(hwnd,renderer.overlay.text.c_str());
        }
        if(image||floating){
            const auto session=editor_session();
            if(image&&session.activity==EditorActivity::Browse){
                auto detail=image->format+L"  "+std::to_wstring(image->load_ms)+L" ms";
                if(image->animated())detail+=L"  "+std::to_wstring(player.frame+1)+L"/"+std::to_wstring(image->frames.size());
                if(!selection.empty())detail+=L"  選択 "+std::to_wstring(selection.w)+L"×"+std::to_wstring(selection.h);
                if(loading)detail+=L"  読み込み中…";
                if(renderer.filter(view)!=DisplaySimple&&renderer.sampling_limited(view))detail+=L"  画質: 高速Linear（表示領域の上限）";
                if(drag_armed)detail=L"左ドラッグでファイルを渡す（Escで解除）";
                status_text(4,detail);
            }else status_text(image?4:0,session.hint());
        }
        refresh_overlay();
    }
    void update_command_controls() {
        const auto commands=command_context();
        for(UINT id:{Save,Copy,ArmDrag,Info,ShowPrompt,ZoomIn,ZoomOut,FitImage,Actual,Fullscreen,Undo,Redo,FileDelete,Previous,Next})toolbar.enable(id,common_command_enabled(id,commands));
        toolbar.text(FileDelete,command_caption(settings,FileDelete,settings.recycle_deleted?L"ごみ箱へ":L"ファイルを削除").c_str());

        const bool reversed = comic && settings.comic_rtl && settings.comic_toolbar_reverse;
        toolbar.text(Previous,command_caption(settings,Previous,reversed?L"次の画像":L"前の画像").c_str());
        toolbar.text(Next,command_caption(settings,Next,reversed?L"前の画像":L"次の画像").c_str());
        toolbar.check(Slideshow, slideshow);
        toolbar.check(ToggleEditor,editor_mode);
        toolbar.text(ToggleEditor,command_caption(settings,ToggleEditor,editor_mode?L"画像編集モードを終了":L"画像編集モードに切り替え").c_str());
        if(editor_mode){
            for(UINT id:{SelectRectangle,SelectFreehand,SelectPolygon,SelectMove,ToolPan,ToolPen,ToolText,ToolCrop})editor_tools.check(id,checked(id));
            for(UINT id:{SelectRectangle,SelectFreehand,SelectPolygon,SelectAll,InvertSelection,ToolCrop,ToolPen,ToolText,Resize,Canvas,RotateRight,FlipH,FlipV,Brightness,ColorAdjust,Levels})editor_tools.enable(id,image&&!floating&&!loading);
            for(UINT id:{SelectMove,DeleteSelection,ClearSelection})editor_tools.enable(id,image&&!selection.empty()&&!floating&&!loading);
            editor_tools.enable(PasteSelection,!loading&&!floating);
        }
        if(cropping)editor_chrome.bounds(cropping->box,cropping->scale);
        else if(floating&&!floating->text_annotation)editor_chrome.bounds(floating->box);
    }
