    void click_editor_tool(UINT id) {
        RECT bounds{};CHECK(SendMessageW(app.editor_tools.hwnd,TB_GETRECT,id,reinterpret_cast<LPARAM>(&bounds)));
        const auto point=MAKELPARAM((bounds.left+bounds.right)/2,(bounds.top+bounds.bottom)/2);
        SendMessageW(app.editor_tools.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(app.editor_tools.hwnd,WM_LBUTTONUP,0,point);
    }
    void editor_toggle_native_sidebar_and_viewport_preserve_document() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;app.replace(selection_fixture(),false,true);const auto original=app.image,undo=app.undo_image;
        CHECK(!app.editor_mode&&app.left==0&&!(GetWindowLongPtrW(app.editor_tools.bar_window(),GWL_STYLE)&WS_VISIBLE));
        CHECK(SendMessageW(app.toolbar.hwnd,TB_COMMANDTOINDEX,ToggleEditor,0)>=0);
        command(ToggleEditor);CHECK(app.editor_mode&&app.left>0&&(GetWindowLongPtrW(app.editor_tools.bar_window(),GWL_STYLE)&WS_VISIBLE));
        CHECK(app.selection_active()&&app.selection_tool==App::SelectionTool::Rectangle);
        CHECK(SendMessageW(app.toolbar.hwnd,TB_ISBUTTONCHECKED,ToggleEditor,0));
        const auto count=SendMessageW(app.editor_tools.hwnd,TB_BUTTONCOUNT,0,0);CHECK(count==25);
        int column=-1,previous=-1;
        for(int n=0;n<count;++n){TBBUTTON button{};RECT bounds{};SendMessageW(app.editor_tools.hwnd,TB_GETBUTTON,n,reinterpret_cast<LPARAM>(&button));
            CHECK(SendMessageW(app.editor_tools.hwnd,TB_GETITEMRECT,n,reinterpret_cast<LPARAM>(&bounds)));
            if(!button.idCommand)continue;if(column<0)column=bounds.left;CHECK(bounds.left==column&&bounds.top>previous);previous=bounds.top;
            CHECK(std::wstring(command_label(button.idCommand))!=L"GVN");}
        RECT client{},tools{};GetClientRect(app.hwnd,&client);GetWindowRect(app.editor_tools.bar_window(),&tools);
        CHECK(int(app.view.width)==client.right-app.left&&app.renderer.viewport_left==app.left);
        click_editor_tool(SelectFreehand);CHECK(app.selection_tool==App::SelectionTool::Freehand);
        CHECK(SendMessageW(app.editor_tools.hwnd,TB_ISBUTTONCHECKED,SelectFreehand,0));
        click_editor_tool(SelectRectangle);CHECK(app.selection_tool==App::SelectionTool::Rectangle);command(Actual);
        selection_mouse(WM_LBUTTONDOWN,10,12,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,50,40,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,50,40);
        std::cout<<"EDITOR_RECT "<<app.selection.x<<' '<<app.selection.y<<' '<<app.selection.w<<' '<<app.selection.h<<" scale="<<app.view.scale<<'\n';
        CHECK(app.selection.x==10&&app.selection.y==12&&app.selection.w==41&&app.selection.h==29);
        command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==crop(*original,{10,12,41,29}).frames[0].pixels);
        const auto selection=app.selection;command(ToggleEditor);
        CHECK(!app.editor_mode&&app.left==0&&!(GetWindowLongPtrW(app.editor_tools.bar_window(),GWL_STYLE)&WS_VISIBLE)&&!app.selection_active());
        CHECK(app.image==original&&app.undo_image==undo&&!app.modified&&app.selection.w==selection.w);
        command(ToggleEditor);CHECK(app.selection_tool==App::SelectionTool::Rectangle);
        click_editor_tool(ToolPan);CHECK(!app.selection_active()&&app.selection_tool==App::SelectionTool::Pan);
        command(ToggleEditor);CHECK(!app.editor_mode&&app.image==original&&app.undo_image==undo);
    }
    void editor_unifies_legacy_keys_mouse_tools_units_and_cancel() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;app.replace(selection_fixture(),false,true);const auto original=app.image;
        CHECK(app.settings.command_for('S',0)==SelectMode);command(SelectMode);
        CHECK(app.editor_mode&&app.selection_tool==App::SelectionTool::Rectangle);command(SelectMode);CHECK(!app.editor_mode);
        app.settings.grid=4;app.settings.selection_custom=false;
        selection_mouse(WM_LBUTTONDOWN,5,7,MK_LBUTTON|MK_SHIFT);selection_mouse(WM_MOUSEMOVE,29,32,MK_LBUTTON|MK_SHIFT);selection_mouse(WM_LBUTTONUP,29,32);
        const auto legacy=app.selection;CHECK(!legacy.empty()&&legacy.x%4==0&&legacy.y%4==0&&legacy.w%4==0&&legacy.h%4==0);
        command(ClearSelection);command(SelectRectangle);
        selection_mouse(WM_LBUTTONDOWN,5,7,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,29,32,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,29,32);
        CHECK(app.selection.x==legacy.x&&app.selection.y==legacy.y&&app.selection.w==legacy.w&&app.selection.h==legacy.h);
        app.settings.selection_custom=true;app.settings.custom_selection_units={3,5,6,10};command(ClearSelection);
        selection_mouse(WM_LBUTTONDOWN,7,9,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,45,47,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,45,47);
        CHECK(app.selection.x%3==0&&app.selection.y%5==0&&app.selection.w%6==0&&app.selection.h%10==0);
        const auto before=app.selection;command(SelectPolygon);
        selection_mouse(WM_LBUTTONDOWN,10,10,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,10,10);selection_mouse(WM_LBUTTONDOWN,60,12,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,60,12);
        command(ToggleEditor);CHECK(!app.polygon_drawing&&!app.selecting&&app.selection.w==before.w&&app.image==original);
        command(SelectFreehand);app.settings.mouse.assign(MouseGesture::X1Drag,0,0,MouseSelect);
        const auto at=[&](int x,int y){const auto p=selection_screen(x,y);return MAKELPARAM(p.x,p.y);};
        app.message(WM_XBUTTONDOWN,MAKEWPARAM(MK_XBUTTON1,XBUTTON1),at(10,10));app.message(WM_MOUSEMOVE,MK_XBUTTON1,at(60,10));app.message(WM_MOUSEMOVE,MK_XBUTTON1,at(10,60));
        app.message(WM_XBUTTONUP,MAKEWPARAM(0,XBUTTON1),at(10,60));CHECK(!app.selection.rectangular()&&app.selection.contains(12,12)&&!app.selection.contains(59,59));
        command(SelectMove);CHECK(app.floating);command(ToggleEditor);CHECK(!app.floating&&!app.editor_mode&&app.image==original&&!app.modified);
    }
    void editor_short_window_tools_scroll_and_animation_layout() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;app.replace(selection_fixture(),false,true);command(ToggleEditor);
        SetWindowPos(app.hwnd,nullptr,0,0,360,240,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);app.layout();
        SCROLLINFO info{sizeof(info),SIF_ALL};CHECK(GetScrollInfo(app.editor_tools.bar_window(),SB_VERT,&info));CHECK(info.nMax>int(info.nPage));
        SendMessageW(app.editor_tools.bar_window(),WM_VSCROLL,SB_BOTTOM,0);RECT tools{},bar{};GetWindowRect(app.editor_tools.hwnd,&bar);GetWindowRect(app.editor_tools.bar_window(),&tools);CHECK(bar.top<tools.top);
        const int left=app.left;SendMessageW(app.editor_tools.bar_window(),WM_VSCROLL,SB_TOP,0);GetWindowRect(app.editor_tools.hwnd,&bar);CHECK(bar.top==tools.top&&app.left==left);
        open(fixture(L"blend.apng"));CHECK(app.editor_mode&&app.image->animated());
        RECT playback{},editor{};GetWindowRect(app.playback.hwnd,&playback);GetWindowRect(app.editor_tools.bar_window(),&editor);CHECK(editor.bottom<=playback.top);
        const auto source=app.image;command(ToggleEditor);CHECK(app.left==0&&app.image==source);command(ToggleEditor);CHECK(app.left>0&&app.image==source);
        auto options=app.settings;options.toolbar_large=true;app.apply_settings(options);CHECK(app.left>left);CHECK(app.image==source);
    }
    void plain_inside_drag_starts_masked_move_transform_and_cancel_undo() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;app.replace(selection_fixture(),false,true);command(Actual);
        const auto original=app.image;const auto sequence=GetClipboardSequenceNumber();
        for(UINT tool:{SelectRectangle,SelectFreehand,SelectPolygon}){
            const auto undo=app.undo_image;
            command(tool);const FloatPoint points[]{{10,10},{80,10},{10,70}};
            app.selection=tool==SelectRectangle?SelectionRegion{10,10,70,60}:SelectionRegion::polygon(points,128,96);app.update();const auto region=app.selection;
            selection_mouse(WM_LBUTTONDOWN,20,20,MK_LBUTTON);CHECK(!app.floating&&app.mouse_presses[0].selection_move_pending);
            selection_mouse(WM_MOUSEMOVE,21,21,MK_LBUTTON);CHECK(!app.floating);
            selection_mouse(WM_LBUTTONUP,21,21);CHECK(!app.floating&&app.selection.contains(20,20)&&!app.modified);
            selection_mouse(WM_LBUTTONDOWN,11,11,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,26,20,MK_LBUTTON);
            CHECK(app.floating&&app.selection_tool==App::SelectionTool::Move&&app.checked(SelectMove));
            CHECK(app.floating_drag&&app.floating_drag->hit==FloatMove&&app.image==original&&app.undo_image==undo);
            CHECK(app.floating->box.width==region.w&&app.floating->box.height==region.h);
            CHECK(app.floating->source->frames[0].pixels==selection_image(*original,region).frames[0].pixels);
            CHECK(app.floating->canvas->frames[0].pixels==erase_selection(*original,region).frames[0].pixels);
            CHECK(app.floating->box.cx==region.x+region.w/2.+15&&app.floating->box.cy==region.y+region.h/2.+9);
            selection_mouse(WM_LBUTTONUP,26,20);CHECK(!app.floating_drag&&app.floating);
            const auto initial=app.floating->box;const auto handle=initial.handles()[4];
            selection_mouse(WM_LBUTTONDOWN,int(handle.x),int(handle.y),MK_LBUTTON);CHECK(app.floating_drag&&app.floating_drag->hit==4);
            selection_mouse(WM_MOUSEMOVE,int(handle.x)+10,int(handle.y)+10,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,int(handle.x)+10,int(handle.y)+10);
            CHECK(app.floating->box.width>initial.width&&app.floating->box.height>initial.height);
            const auto enlarged=app.floating->box;
            selection_mouse(WM_LBUTTONDOWN,120,90,MK_LBUTTON);CHECK(app.floating_drag&&app.floating_drag->hit==FloatRotate);
            selection_mouse(WM_MOUSEMOVE,105,95,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,105,95);CHECK(app.floating->box.angle!=enlarged.angle);
            command(CancelSelection);CHECK(!app.floating&&app.image==original&&app.undo_image==undo&&app.selection.contains(20,20));
            command(tool);selection_mouse(WM_LBUTTONDOWN,20,20,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,35,29,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,35,29);
            CHECK(app.floating);const auto expected=paste_floating(*app.floating->canvas,*app.floating->source,app.floating->box);
            command(CommitSelection);CHECK(app.modified&&app.undo_image==original&&app.image->frames[0].pixels==expected.frames[0].pixels);
            command(Undo);CHECK(app.image==original&&!app.modified);CHECK(GetClipboardSequenceNumber()==sequence);
        }
    }
    void selection_modifiers_and_mask_holes_never_start_automatic_move() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.replace(selection_fixture(),false,true);command(Actual);
        const FloatPoint points[]{{10,10},{80,10},{10,70}};const auto triangle=SelectionRegion::polygon(points,128,96);const auto original=app.image;
        for(UINT modifier:std::array<UINT,3>{MK_SHIFT,MK_CONTROL,0}){
            command(SelectRectangle);app.selection=triangle;app.update();BYTE keys[256]{};
            if(!modifier)keys[VK_MENU]=0x80;CHECK(SetKeyboardState(keys));
            selection_mouse(WM_LBUTTONDOWN,20,20,MK_LBUTTON|modifier);selection_mouse(WM_MOUSEMOVE,40,40,MK_LBUTTON|modifier);selection_mouse(WM_LBUTTONUP,40,40);
            CHECK(!app.floating&&app.image==original);
            if(modifier==MK_SHIFT)CHECK(app.selection.contains(15,15)&&app.selection.contains(39,39));
            else if(modifier==MK_CONTROL)CHECK(!app.selection.contains(15,15)&&app.selection.contains(21,21));
            else CHECK(app.selection.contains(15,15)&&!app.selection.contains(21,21));
            BYTE neutral[256]{};CHECK(SetKeyboardState(neutral));
        }
        command(SelectRectangle);app.selection=triangle;app.update();CHECK(!triangle.contains(70,60));
        selection_mouse(WM_LBUTTONDOWN,70,60,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,90,80,MK_LBUTTON);
        CHECK(!app.floating&&app.selecting);app.message(WM_CANCELMODE,0,0);CHECK(app.selection.contains(15,15));
        selection_mouse(WM_LBUTTONDOWN,20,20,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,45,40,MK_LBUTTON|MK_SHIFT);
        selection_mouse(WM_LBUTTONUP,45,40);CHECK(!app.floating&&app.selection.contains(15,15)&&app.selection.contains(44,39));
        command(SelectRectangle);app.selection=triangle;app.update();selection_mouse(WM_LBUTTONDOWN,20,20,MK_LBUTTON);
        app.message(WM_KEYDOWN,VK_ESCAPE,0);selection_mouse(WM_MOUSEMOVE,45,40,MK_LBUTTON);CHECK(!app.floating&&!app.mouse_buttons&&app.selection.empty());
    }
    void space_and_hand_pan_small_images_restore_tools_and_custom_keys() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;app.replace(selection_fixture(),false,true);command(Actual);command(SelectRectangle);
        app.selection={10,10,40,40};app.update();const auto original=app.image,undo=app.undo_image;const auto scale=app.view.scale;
        CHECK(app.settings.command_for(VK_SPACE,0)==ToolPan);CHECK(app.view.canvas_w<app.view.width&&app.view.canvas_h<app.view.height);
        const auto drag=[&]{const POINT a{100,app.top+100},b{145,app.top+125};
            app.message(WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(a.x+app.left,a.y));app.message(WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(b.x+app.left,b.y));app.message(WM_LBUTTONUP,0,MAKELPARAM(b.x+app.left,b.y));};
        app.message(WM_KEYDOWN,VK_SPACE,0);CHECK(app.space_pan&&app.checked(ToolPan)&&app.selection_tool==App::SelectionTool::Rectangle);
        app.message(WM_KEYDOWN,VK_SPACE,1<<30);drag();CHECK(app.view.pan_x==45&&app.view.pan_y==25&&app.view.free_pan);
        app.message(WM_KEYUP,VK_SPACE,0);CHECK(!app.space_pan&&app.selection_tool==App::SelectionTool::Rectangle&&app.view.pan_x==45);
        CHECK(app.selection.contains(20,20)&&!app.floating&&app.image==original&&app.undo_image==undo&&!app.modified&&app.view.scale==scale);
        command(ToolPan);drag();CHECK(app.view.pan_x==90&&app.view.pan_y==50&&app.selection_tool==App::SelectionTool::Pan);
        command(Actual);CHECK(!app.view.free_pan&&app.view.pan_x==0&&app.view.pan_y==0);
        app.message(WM_KEYDOWN,VK_SPACE,0);app.message(WM_KILLFOCUS,0,0);CHECK(!app.space_pan&&!app.mouse_buttons);
        command(SelectRectangle);const auto selection=app.selection;command(SelectMove);CHECK(app.floating);const auto box=app.floating->box;
        app.message(WM_KEYDOWN,VK_SPACE,0);drag();app.message(WM_KEYUP,VK_SPACE,0);CHECK(app.floating&&app.floating->box.cx==box.cx&&app.floating->box.width==box.width&&app.view.pan_x==45);
        command(CancelSelection);CHECK(app.image==original&&app.selection.w==selection.w);
        app.settings.set_keys(ToolPan,{});app.settings.set_keys(Play,{L"Space"});app.message(WM_KEYDOWN,VK_SPACE,0);CHECK(!app.space_pan);
        open(fixture(L"blend.apng"));const bool playing=app.player.playing;app.message(WM_KEYDOWN,VK_SPACE,0);CHECK(app.player.playing!=playing&&!app.space_pan);
    }
    void left_held_wheel_default_navigates_up_next_down_previous() {
        for(int i=1;i<=3;++i)copy(L"alpha.png",std::to_wstring(i)+L".png");open(temporary/L"2.png");
        CHECK(app.settings.mouse.action(MouseGesture::WheelUp,mouse_left,0)==Next&&app.settings.mouse.action(MouseGesture::WheelDown,mouse_left,0)==Previous);
        const auto index=app.index;const auto scale=app.view.scale;POINT p{100,app.top+100};ClientToScreen(app.hwnd,&p);
        app.message(WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,app.top+100));
        app.message(WM_MOUSEWHEEL,MAKEWPARAM(MK_LBUTTON,WHEEL_DELTA),MAKELPARAM(p.x,p.y));wait();CHECK(app.index==index+1);
        app.message(WM_LBUTTONUP,0,MAKELPARAM(100,app.top+100));
        app.message(WM_MOUSEWHEEL,MAKEWPARAM(MK_LBUTTON,-WHEEL_DELTA),MAKELPARAM(p.x,p.y));wait();CHECK(app.index==index&&app.view.scale==scale);
        app.message(WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(p.x,p.y));CHECK(app.view.scale>scale&&app.index==index);
    }
    void three_quality_choices_drive_actual_zoom_editor_fullscreen_without_changing_rgba() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;app.replace(selection_fixture(128,96),false,true);
        const auto original=app.image;const auto pixels=original->frames[0].pixels;
        app.settings.nearest=app.settings.full_nearest=true;app.settings.fit_simple=app.settings.zoom_simple=100;
        app.settings.zoom_reduce=app.settings.zoom_enlarge=false;
        for(int quality=0;quality<3;++quality){
            PreferencesDialog dialog(app.settings,[&](const Settings &value){app.apply_settings(value);},[&](HWND,const std::wstring &){CHECK(false);});
            CHECK(dialog.create(app.hwnd));
            for(auto page:{PreferencePage::Display,PreferencePage::Fullscreen}){
                dialog.select_page(page);
                const auto ids=page==PreferencePage::Display?std::array<int,2>{preference_id::DisplayDown,preference_id::DisplayUp}:std::array<int,2>{preference_id::FullDown,preference_id::FullUp};
                for(auto id:ids){const auto control=dialog.control(id);CHECK(control&&SendMessageW(control,CB_GETCOUNT,0,0)==3);
                    for(int index=0;index<3;++index){wchar_t label[32]{};SendMessageW(control,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(label));CHECK(std::wstring(label)==display_quality_labels[index]);}
                    CHECK(SendMessageW(control,CB_SETCURSEL,quality,0)==quality);
                    SendMessageW(GetParent(control),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control));}
            }
            dialog.select_page(PreferencePage::Zoom);CHECK(!dialog.control(preference_id::ZoomReduce)&&!dialog.control(preference_id::ZoomEnlarge)&&!dialog.control(preference_id::ZoomSimple));
            CHECK(!dialog.control(preference_id::Nearest)&&!dialog.control(preference_id::FullNearest)&&!dialog.control(preference_id::FitSimple));
            SendMessageW(dialog.control(preference_id::ApplyButton),BM_CLICK,0,0);SendMessageW(dialog.control(IDCANCEL),BM_CLICK,0,0);
            const int method=display_quality_filter(quality);CHECK(app.settings.display_down==method&&app.settings.display_up==method&&app.settings.full_down==method&&app.settings.full_up==method);
            for(bool full:{false,true}){if(app.full!=full)command(Fullscreen);
            for(bool editing:{false,true}){if(app.editor_mode!=editing)command(ToggleEditor);
            for(bool manual:{false,true})for(double scale:{.37,2.25}){
                app.view.scale=scale;app.view.manual=manual;app.view.pan_x=app.view.pan_y=0;app.update();
                CHECK(app.renderer.filter(app.view)==method);CHECK(SUCCEEDED(app.render_scene()));
                CHECK(app.renderer.sample_current(app.image.get(),0,app.view)&&app.image==original&&original->frames[0].pixels==pixels&&!app.modified);
            }}}
            if(app.full)command(Fullscreen);if(app.editor_mode)command(ToggleEditor);
            command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==pixels);
        }
    }

    void live_resize_restores_quality_preserves_document_and_removes_empty_toolbar_row(){
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.settings.confirm_unsaved=false;command(DisplayUnchanged);
        app.replace(new_image(1600,1200),false,true);app.clear_undo();const auto original=app.image;const auto rgba=original->frames[0].pixels;
        app.settings.display_down=app.settings.display_up=DisplayLanczos3;command(FitImage);app.update();
        for(int width:{1300,480,520,1300}){
            SetWindowPos(app.hwnd,nullptr,0,0,width,850,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);app.layout();app.update();
            LONG bottom{};for(int n=0;n<SendMessageW(app.toolbar.hwnd,TB_BUTTONCOUNT,0,0);++n){TBBUTTON button{};RECT rect{};
                SendMessageW(app.toolbar.hwnd,TB_GETBUTTON,n,reinterpret_cast<LPARAM>(&button));if(!(button.fsStyle&BTNS_SEP)&&!(button.fsState&TBSTATE_HIDDEN)){
                    CHECK(SendMessageW(app.toolbar.hwnd,TB_GETITEMRECT,n,reinterpret_cast<LPARAM>(&rect)));bottom=std::max(bottom,rect.bottom);}}
            CHECK(app.toolbar.height()>=bottom&&app.toolbar.height()-bottom<=MulDiv(6,GetDpiForWindow(app.hwnd),96));
            if(width==1300)CHECK(app.toolbar.height()<=MulDiv(ui_button_size(app.toolbar.large_buttons())+2,GetDpiForWindow(app.hwnd),96));
        }
        CHECK(SUCCEEDED(app.render_scene())&&app.renderer.sample_current(original.get(),0,app.view));
        app.message(WM_ENTERSIZEMOVE,0,0);
        for(int n=0;n<4;++n){SetWindowPos(app.hwnd,nullptr,0,0,1000+n*20,700+n*15,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            CHECK(SUCCEEDED(app.render_scene())&&app.renderer.resizing&&app.renderer.filter(app.view)==DisplayLinear);
            CHECK(app.renderer.sample_bytes()==0&&app.renderer.source_bytes()>=original->bytes());}
        app.message(WM_EXITSIZEMOVE,0,0);CHECK(SUCCEEDED(app.render_scene()));
        CHECK(!app.renderer.resizing&&app.renderer.filter(app.view)==DisplayLanczos3&&app.renderer.sample_current(original.get(),0,app.view)&&app.renderer.source_bytes()==0);
        command(ToolPen);app.layout();app.update();app.layout();int changes{};
        auto field=app.editor_chrome.control(editor_id::Width);
        const SUBCLASSPROC probe=[](HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data)->LRESULT{
            if(msg==WM_SHOWWINDOW||msg==WM_WINDOWPOSCHANGED)++*reinterpret_cast<int *>(data);return DefSubclassProc(h,msg,w,l);};CHECK(SetWindowSubclass(field,probe,987,reinterpret_cast<DWORD_PTR>(&changes)));
        // Remove the probe before any assertion can unwind its counter.
        app.layout();app.layout();app.layout();RemoveWindowSubclass(field,probe,987);
        CHECK(changes==0&&skitch_visible(field));CHECK(app.image==original&&!app.modified&&!app.undo_image&&original->frames[0].pixels==rgba);
        app.message(WM_ENTERSIZEMOVE,0,0);app.message(WM_CANCELMODE,0,0);CHECK(!app.renderer.resizing);CHECK(SUCCEEDED(app.render_scene())&&app.renderer.filter(app.view)==DisplayLanczos3);
    }
