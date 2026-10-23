    bool skitch_visible(HWND child){if(!child)return false;for(;;){if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE))return false;if(child==app.hwnd)return true;child=GetParent(child);if(!child)return false;}}
    void skitch_pen_text_native_controls_alpha_apply_cancel_undo() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;
        app.replace(new_image(320,200),false,true);const auto original=app.image;
        command(ToolPen);command(Actual);CHECK(app.editor_mode&&app.selection_tool==App::SelectionTool::Pen);
        CHECK(skitch_visible(app.editor_chrome.control(editor_id::Width)));
        SetWindowTextW(app.editor_chrome.control(editor_id::Width),L"8");
        size_t allocations{};app.editing.allocating=[&](size_t){++allocations;};
        selection_mouse(WM_LBUTTONDOWN,30,40,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,100,80,MK_LBUTTON);CHECK(allocations==0);
        CHECK(app.pen_player&&app.image==original&&!app.modified&&app.renderer.pen_points.size()==2);
        selection_mouse(WM_LBUTTONUP,110,80);CHECK(allocations>0&&!app.pen_player&&app.modified&&app.undo_image==original);app.editing.allocating={};
        CHECK(app.image->frames[0].pixels[40*320+30].a>0&&app.image->frames[0].pixels[0].a==0);
        command(Undo);CHECK(app.image==original&&!app.modified);
        selection_mouse(WM_LBUTTONDOWN,35,40,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,90,50,MK_LBUTTON);
        SendMessageW(app.hwnd,WM_CANCELMODE,0,0);CHECK(!app.pen_player&&app.image==original&&app.renderer.pen_points.empty());
        command(ToolText);command(Actual);auto input=app.editor_chrome.control(editor_id::Text);
        SetWindowTextW(input,L"日本語 ABC\n文字");SetWindowTextW(app.editor_chrome.control(editor_id::TextSize),L"24");
        MSG key{};key.hwnd=input;key.message=WM_KEYDOWN;key.wParam=VK_SPACE;CHECK(!app.route_key(key));
        selection_mouse(WM_LBUTTONDOWN,40,50,MK_LBUTTON);CHECK(app.floating&&app.floating->text_annotation&&app.image==original&&!app.modified);
        CHECK(skitch_visible(app.editor_chrome.control(editor_id::Apply))&&skitch_visible(app.editor_chrome.control(editor_id::Cancel)));
        const auto stamp=app.floating->source;CHECK(stamp->height>30);size_t alpha{};for(auto p:stamp->frames[0].pixels)alpha+=p.a>0;CHECK(alpha>50);
        SendMessageW(app.editor_chrome.control(editor_id::Cancel),BM_CLICK,0,0);CHECK(!app.floating&&app.image==original&&!app.modified);
        selection_mouse(WM_LBUTTONDOWN,40,50,MK_LBUTTON);SetWindowTextW(input,L"即時適用した文字");SendMessageW(app.editor_chrome.control(editor_id::Apply),BM_CLICK,0,0);
        CHECK(!app.floating&&app.modified&&app.undo_image==original);CHECK(app.image->frames[0].pixels[0].a==0);
        command(Undo);CHECK(app.image==original&&!app.modified);
    }
    void skitch_crop_handles_numeric_bounds_buttons_cancel_apply_undo() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;
        auto source=new_image(320,200);for(int y=0;y<200;++y)for(int x=0;x<320;++x)source.frames[0].pixels[size_t(y)*320+x]={uint8_t(x),uint8_t(y),50,uint8_t((x+y)%255+1)};
        app.replace(std::move(source),false,true);const auto original=app.image;command(ToolCrop);command(Actual);
        CHECK(app.cropping&&app.image==original&&!app.modified&&app.cropping->box.width==320);
        auto width_control=app.editor_chrome.control(editor_id::W);SetWindowTextW(width_control,L"0");
        MSG invalid{};invalid.hwnd=width_control;invalid.message=WM_KEYDOWN;invalid.wParam=VK_RETURN;
        CHECK(app.route_key(invalid)&&app.cropping&&app.cropping->box.width==320&&!app.modified);
        CHECK(!app.editor_chrome.text(editor_id::Error).empty());SetWindowTextW(width_control,L"320");
        for(auto [id,value]:{std::pair{editor_id::X,L"20"},{editor_id::Y,L"30"},{editor_id::W,L"120"},{editor_id::H,L"90"}})SetWindowTextW(app.editor_chrome.control(id),value);
        SendMessageW(app.editor_chrome.top,WM_COMMAND,MAKEWPARAM(editor_id::W,EN_KILLFOCUS),reinterpret_cast<LPARAM>(app.editor_chrome.control(editor_id::W)));
        CHECK(app.cropping->box.width==120&&app.cropping->box.height==90&&app.cropping->box.cx==80&&app.cropping->box.cy==75);
        selection_mouse(WM_LBUTTONDOWN,60,60,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,70,68,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,70,68);
        CHECK(app.cropping->box.cx==90&&app.cropping->box.cy==83&&app.image==original&&!app.modified);
        CHECK(SUCCEEDED(app.render_scene())&&app.renderer.crop_box);
        SendMessageW(app.editor_chrome.control(editor_id::Cancel),BM_CLICK,0,0);CHECK(!app.cropping&&app.image==original&&!app.modified);
        app.selection={20,30,120,90};command(ToolCrop);command(Actual);SendMessageW(app.editor_chrome.control(editor_id::Apply),BM_CLICK,0,0);
        CHECK(!app.cropping&&app.image->width==120&&app.image->height==90&&app.modified&&app.undo_image==original);
        CHECK(app.image->frames[0].pixels[0]==original->frames[0].pixels[30*320+20]);
        command(Undo);CHECK(app.image==original&&!app.modified);
        app.selection={40,50,80,60};command(SelectMove);CHECK(app.floating);
        CHECK(skitch_visible(app.editor_chrome.control(editor_id::Apply))&&skitch_visible(app.editor_chrome.control(editor_id::Cancel)));
        CHECK(SendMessageW(app.toolbar.hwnd,TB_COMMANDTOINDEX,CommitSelection,0)<0);
        SendMessageW(app.editor_chrome.control(editor_id::Cancel),BM_CLICK,0,0);CHECK(!app.floating&&app.image==original);
    }
    void skitch_bottom_export_moves_status_icon_names_formats_and_ole_payload() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;
        auto source=new_image(40,30);source.frames[0].pixels[0]={240,20,90,128};source.frames[0].pixels[1]={15,90,200,255};
        app.replace(std::move(source),false,true);const auto original=app.image;
        CHECK(SendMessageW(app.status,SB_GETICON,0,0)<=0&&skitch_visible(app.editor_chrome.bottom));
        auto drag=app.editor_chrome.control(editor_id::Drag);SetWindowTextW(app.editor_chrome.control(editor_id::Name),L"テスト画像");
        std::vector<std::filesystem::path> exported;
        app.dialogs.file_drag=[&](const auto &path){CHECK(GetCapture()!=drag&&!app.player.playing&&std::filesystem::is_regular_file(path));
            FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};const auto data=file_data_object(path);CHECK(data->QueryGetData(&format)==S_OK);
            const auto decoded=load_image(path);CHECK(decoded.width==40&&decoded.height==30);
            if(path.extension()==L".png")CHECK(decoded.frames[0].pixels==original->frames[0].pixels);exported.push_back(path);};
        const auto send=[&](UINT msg,int x,int y,WPARAM flags=MK_LBUTTON){SendMessageW(drag,msg,flags,MAKELPARAM(x,y));};
        SetFocus(app.hwnd);send(WM_LBUTTONDOWN,10,10);CHECK(app.editor_chrome.dragging());send(WM_MOUSEMOVE,11,10);send(WM_LBUTTONUP,11,10,0);CHECK(exported.empty());
        for(int n=0;n<4;++n){SendMessageW(app.editor_chrome.control(editor_id::Format),CB_SETCURSEL,n,0);
            send(WM_LBUTTONDOWN,10,10);CHECK(GetCapture()==drag);send(WM_MOUSEMOVE,60,30);CHECK(GetCapture()!=drag&&exported.size()==size_t(n+1));}
        CHECK(exported[0].filename()==L"テスト画像.png"&&exported[1].extension()==L".jpg"&&exported[2].extension()==L".bmp"&&exported[3].extension()==L".gif");
        for(const auto &p:exported)std::filesystem::remove_all(p.parent_path());
        CHECK(app.image==original&&!app.modified);command(ToolCrop);CHECK(!IsWindowEnabled(drag));command(CancelSelection);CHECK(IsWindowEnabled(drag));
    }
