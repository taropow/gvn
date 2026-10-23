    void begin_floating(SelectionClipboard content) {
        auto source=std::make_shared<Image>(still_frame(content.image,0,false));
        auto canvas=image?std::make_shared<Image>(still_frame(*image,player.frame,false)):
            std::make_shared<Image>(new_image(content.origin?content.origin->canvas_width:source->width,
                                              content.origin?content.origin->canvas_height:source->height,edit_memory()));
        if(cropping)cancel_crop();
        if(floating)cancel_floating();
        FloatingPaste next{image,canvas,source,{},player,view,content.origin,slideshow,content.invalid_metadata};
        next.previous_selection=selection;
        next.box={double(content.origin?content.origin->x:0)+source->width/2.,double(content.origin?content.origin->y:0)+source->height/2.,double(source->width),double(source->height),0};
        floating=std::move(next);cancel_pointer();selection={};renderer.selection_path.clear();player.playing=false;reset_animation_clock();KillTimer(hwnd,2);
        drag_armed=false;toolbar.floating(true);
        if(!image){view.set_image(canvas->width,canvas->height,false);view.apply_fit(Fit::Contain);}
        refresh_menu();layout();update();
    }
    void cancel_floating() {
        if(!floating)return;
        auto old=std::move(*floating);floating.reset();floating_drag.reset();renderer.set_floating({});
        toolbar.floating(false);cancel_pointer();
        if(image==old.origin){player=std::move(old.previous_player);selection=old.previous_selection;reset_animation_clock();if(!image)view=old.previous_view;}
        if(old.slideshow&&slideshow)SetTimer(hwnd,2,settings.interval,nullptr);
        refresh_menu();layout();update();
    }
    void commit_floating() {
        if(!floating)return;
        if(image!=floating->origin){cancel_floating();return;}
        if(image&&!begin_edit())return;
        auto result=paste_floating(*floating->canvas,*floating->source,floating->box,edit_memory());
        if(image){result.metadata=current_metadata();if(image->animated()||image->format==L"見開き")result.path.clear();}
        // Finish every allocation before discarding the uncommitted placement.
        auto prepared=std::make_shared<Image>(std::move(result));const bool synthetic=!floating->origin;
        cancel_floating();
        if(synthetic)replace_clipboard_document(std::move(*prepared));else replace(std::move(prepared));
    }
    FloatPoint floating_point(POINT p) const {return ViewportTransform(view,0,top).to_image({double(p.x),double(p.y)});}
    CommandContext command_context() const{return {image!=nullptr,editor_session().pending(),loading,image&&!image->path.empty()&&archive.empty()&&!removed_current,!selection.empty(),undo_image!=nullptr,!redo_history.empty(),!files.empty()};}
    EditorSession editor_session() const{return EditorSession::resolve(editor_mode,selecting||polygon_drawing,!renderer.pen_points.empty(),floating.has_value(),floating&&floating->text_annotation,cropping.has_value());}
    EditorPanel editor_panel() const{const auto state=editor_session();if(state.activity==EditorActivity::Crop)return EditorPanel::Crop;if(state.activity==EditorActivity::Text)return EditorPanel::Text;if(state.activity==EditorActivity::Transform)return EditorPanel::Transform;return selection_tool==SelectionTool::Pen?EditorPanel::Pen:selection_tool==SelectionTool::Text?EditorPanel::Text:EditorPanel::None;}
    void floating_numbers() {
        if(!floating)return;const auto source=floating->source;const auto box=floating->box;
        std::wostringstream initial;initial<<box.cx-box.width/2<<L' '<<box.cy-box.height/2<<L' '<<box.width<<L' '<<box.height<<L' '<<box.angle;
        const auto text=dialogs.text(hwnd,L"貼り付けの位置・サイズ・角度",L"X Y 幅 高さ 角度（px / 度、半角空白区切り）",initial.str());
        if(!text||!floating||floating->source!=source)return;
        double x{},y{},width{},height{},angle{};std::wistringstream in(*text);
        if(!(in>>x>>y>>width>>height>>angle)||!(in>>std::ws).eof())throw std::invalid_argument("Enter X Y width height angle");
        FloatingBox next{x+width/2,y+height/2,width,height,angle};
        if(!next.valid())throw std::invalid_argument("Invalid floating selection bounds");floating->box=next;update();
    }
    void move_selection() {
        if(!image||selection.empty()||loading)return;
        auto frame=still_frame(*image,player.frame,false);
        auto source=std::make_shared<Image>(selection_image(frame,selection,edit_memory()));
        auto canvas=std::make_shared<Image>(erase_selection(frame,selection,{},edit_memory()));
        FloatingPaste next{image,canvas,source,{},player,view,SelectionOrigin{selection.x,selection.y,image->width,image->height},slideshow,false};
        next.previous_selection=selection;
        next.box={selection.x+source->width/2.,selection.y+source->height/2.,double(source->width),double(source->height),0};
        floating=std::move(next);cancel_pointer();selection={};renderer.selection_path.clear();
        player.playing=false;reset_animation_clock();KillTimer(hwnd,2);drag_armed=false;toolbar.floating(true);
        refresh_menu();layout();update();
    }
    static SelectionCombine selection_modifier(WPARAM flags) {
        if(GetKeyState(VK_MENU)&0x8000)return SelectionCombine::Subtract;
        if(flags&MK_CONTROL)return SelectionCombine::Intersect;
        if(flags&MK_SHIFT)return SelectionCombine::Add;
        return SelectionCombine::Replace;
    }
    void finish_selection_path() {
        if(!image)return;
        auto next=SelectionRegion::polygon(selection_points,image->width,image->height);
        next=selection_before_drag.combined(next,selection_combine,image->width,image->height);
        selecting=polygon_drawing=false;selection_points.clear();renderer.selection_path.clear();
        selection=std::move(next);if(GetCapture()==hwnd)ReleaseCapture();update();
    }
    void cancel_selection_path() {
        if(selecting||polygon_drawing)selection=selection_before_drag;
        selecting=polygon_drawing=false;selection_points.clear();renderer.selection_path.clear();
    }
    void set_edit_mode(bool enabled) {
        if(editor_mode==enabled)return;
        cancel_pointer();if(floating)cancel_floating();if(cropping)cancel_crop();
        if(!enabled){previous_editor_tool=selection_tool;selection_tool=SelectionTool::Browse;}
        else selection_tool=previous_editor_tool;
        editor_mode=enabled;drag_armed=false;refresh_menu();layout();update();
    }
    void choose_tool(SelectionTool tool) {
        cancel_pointer();if(floating)cancel_floating();if(cropping)cancel_crop();
        set_edit_mode(true);selection_tool=previous_editor_tool=tool;
        if(tool==SelectionTool::Move)move_selection();
        refresh_menu();layout();update();
    }
    POINT canvas_point(POINT p) const {p.x-=left;return p;}
    POINT client_point(POINT p) const {p.x+=left;return p;}
    void start_selection(unsigned button,POINT point,WPARAM flags,bool double_click) {
        const auto tool=selection_active()&&!(selection_tool==SelectionTool::Move&&selection_modifier(flags)!=SelectionCombine::Replace)?selection_tool:SelectionTool::Rectangle;
        auto &press=mouse_presses[button];press.consumed=true;
        const auto p=image_point(point);const FloatPoint next{double(p.x),double(p.y)};
        if(tool==SelectionTool::Move){
            if(selection.contains(p.x,p.y)){move_selection();if(floating){const auto at=floating_point(point);floating_drag_button=button;floating_drag=FloatingDrag{floating->box,at,floating_hit(floating->box,at,7./view.scale)};SetCapture(hwnd);}}
            return;
        }
        if(tool==SelectionTool::Polygon){
            if(!polygon_drawing){selection_before_drag=selection;selection_combine=selection_modifier(flags);selection_points.clear();polygon_drawing=true;}
            if(selection_points.empty()||selection_points.back().x!=next.x||selection_points.back().y!=next.y)selection_points.push_back(next);
            if(selection_points.size()>65536)throw std::runtime_error("Too many selection points");
            if(double_click){finish_selection_path();return;}
            renderer.selection_path=selection_points;renderer.selection_path.push_back(next);update();return;
        }
        selection_before_drag=selection;selection_combine=selection_modifier(flags);selection_drag_tool=tool;
        active_drag_button=button+1;active_drag_action=MouseSelect;selecting=true;selection_start=p;
        selection_drag_units=settings.selection_units();selection_points.clear();
        if(tool==SelectionTool::Freehand){selection_points.push_back(next);renderer.selection_path=selection_points;}
        else selection={};
        SetCapture(hwnd);update();
    }
