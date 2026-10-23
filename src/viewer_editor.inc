    void start_pen(POINT point) {
        if(!image||loading||floating||cropping)return;
        const auto style=editor_chrome.pen();cancel_pointer();SetFocus(hwnd);
        pen_player=player;player.playing=false;KillTimer(hwnd,2);
        renderer.pen_style=style;renderer.pen_points={floating_point(point)};SetCapture(hwnd);update();
    }
    void finish_pen(POINT point) {
        auto points=renderer.pen_points;if(points.size()<65536)points.push_back(floating_point(point));else points.back()=floating_point(point);const auto style=renderer.pen_style;
        cancel_pointer();const auto region=selection;
        edit([&](const Image &source){return draw_pen(source,points,style,region,edit_memory());},false);
    }
    void start_text(POINT point) {
        if(!image||loading||floating||cropping)return;
        auto stamp=std::make_shared<Image>(editor_chrome.text_image(edit_memory()));
        auto canvas=std::make_shared<Image>(still_frame(*image,player.frame,false));
        const auto p=floating_point(point);FloatingPaste next{image,canvas,stamp,{},player,view,{},slideshow,false};
        next.previous_selection=selection;next.text_annotation=true;
        next.box={p.x+stamp->width/2.,p.y+stamp->height/2.,double(stamp->width),double(stamp->height),0};
        cancel_pointer();floating=std::move(next);selection={};player.playing=false;KillTimer(hwnd,2);toolbar.floating(true);
        refresh_menu();layout();update();
    }
    void refresh_text() {
        if(!floating||!floating->text_annotation)return;
        auto stamp=std::make_shared<Image>(editor_chrome.text_image(edit_memory()));auto box=floating->box;
        const auto corner=box.point(-box.width/2,-box.height/2);
        box.width=stamp->width*(box.width/floating->source->width);box.height=stamp->height*(box.height/floating->source->height);
        const auto after=box.point(-box.width/2,-box.height/2);box.cx+=corner.x-after.x;box.cy+=corner.y-after.y;
        if(!box.valid())throw std::invalid_argument("文字のサイズが大きすぎます。");floating->source=std::move(stamp);floating->box=box;update();
    }
    void begin_crop() {
        if(!image||loading)return;choose_tool(SelectionTool::Crop);
        const Rect bounds=selection.empty()?Rect{0,0,int(image->width),int(image->height)}:Rect{selection.x,selection.y,selection.w,selection.h};
        CropPreview next{image,{bounds.x+bounds.w/2.,bounds.y+bounds.h/2.,double(bounds.w),double(bounds.h),0},selection,player,1};
        cropping=std::move(next);selection={};player.playing=false;KillTimer(hwnd,2);layout();update();
    }
    void cancel_crop() {
        if(!cropping)return;auto old=std::move(*cropping);cropping.reset();crop_drag.reset();renderer.crop_box.reset();
        if(image==old.origin){selection=std::move(old.selection);player=std::move(old.player);reset_animation_clock();}
        cancel_pointer();if(slideshow)SetTimer(hwnd,2,settings.interval,nullptr);layout();update();
    }
    void commit_crop() {
        if(!cropping)return;if(image!=cropping->origin){cancel_crop();return;}
        apply_editor_bounds();if(!begin_edit())return;
        auto frame=still_frame(*image,player.frame,false);frame.metadata=current_metadata();
        if(image->animated()||image->format==L"見開き")frame.path.clear();
        auto result=std::make_shared<Image>(crop_oriented(frame,cropping->box,cropping->scale,edit_memory()));
        cancel_crop();replace(std::move(result));
    }
    void apply_editor_bounds() {
        if(cropping){const auto box=editor_chrome.edited_bounds(cropping->box);const auto scale=editor_chrome.output_scale();cropping->box=box;cropping->scale=scale;}
        else if(floating&&!floating->text_annotation)floating->box=editor_chrome.edited_bounds(floating->box);
        update();
    }
    void export_drag() {
        if(!image||loading||floating||cropping)return;
        const auto source=image;const auto metadata=current_metadata();
        const auto path=export_drag_path(*source,player.frame,editor_chrome.text(editor_id::Name),editor_chrome.export_format(),settings.saving,&metadata);
        const auto was_playing=player.playing;cancel_pointer();player.playing=false;update();
        try{dialogs.file_drag(path);}catch(...){if(image==source)player.playing=was_playing;update();throw;}
        if(image==source)player.playing=was_playing;update();
    }
    void editor_notification(int id,int code) {
        using namespace editor_id;
        // Editing native text fields must keep their keyboard focus and IME.
        if(id==Apply){if(cropping)commit_crop();else if(floating){if(floating->text_annotation)refresh_text();else apply_editor_bounds();commit_floating();}SetFocus(hwnd);return;}
        if(id==Cancel){command(CancelSelection);SetFocus(hwnd);return;}
        if(id==Drag){export_drag();return;}
        if(editor_chrome.notification(id,code)){refresh_text();return;}
        if(id==TextUpdate){try{refresh_text();editor_chrome.error(L"");}catch(const std::exception &e){editor_chrome.error(widen(e.what()));}return;}
        if((cropping||(floating&&!floating->text_annotation))&&id>=X&&id<=Scale&&code==EN_KILLFOCUS){
            try{apply_editor_bounds();editor_chrome.error(L"");}catch(const std::exception &e){editor_chrome.error(widen(e.what()));}return;}
        if(((id==TextSize||(id>=OutlineWidth&&id<=ShadowOpacity))&&code==EN_KILLFOCUS)||(id==Font&&(code==CBN_SELCHANGE||code==CBN_KILLFOCUS)))
            try{refresh_text();editor_chrome.error(L"");}catch(const std::exception &e){editor_chrome.error(widen(e.what()));}
    }
