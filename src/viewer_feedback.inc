    CommandToast toast;
    UINT feedback_command{};
    std::wstring feedback_key;
    uint64_t feedback_epoch{},feedback_completed{},feedback_last_sound{};
    bool feedback_busy{},feedback_sound_known{};
    struct LoadFeedback {uint64_t sequence{},epoch{};UINT command{};std::wstring key;};
    std::optional<LoadFeedback> load_feedback;
    RECT toast_area() const {RECT r{};GetClientRect(hwnd,&r);r.left=left;r.top=top;r.bottom=std::max(r.top,r.bottom-bottom);return r;}
    bool feedback_allowed() const{return !smoke&&!profile;}
    const std::wstring &feedback_shortcut(UINT id) const {
        static const std::wstring none;
        const bool registered=(id==FileCopy&&feedback_command>=CopyDestBase&&feedback_command<CopyDestBase+10)||
            (id==FileMove&&feedback_command>=MoveDestBase&&feedback_command<MoveDestBase+10)||
            (id==External&&feedback_command>=ExternalBase&&feedback_command<ExternalBase+9);
        return feedback_command==id||registered?feedback_key:none;
    }
    void feedback_begin(UINT id,const std::wstring &text){
        if(!feedback_allowed()||(!settings.toast_enabled&&!settings.command_sound))return;
        ++feedback_epoch;feedback_busy=true;
        if(settings.toast_enabled)toast.show(hwnd,toast_area(),text,feedback_shortcut(id),true,0);
    }
    void feedback_done(UINT id,const std::wstring &text,const std::wstring *key=nullptr){
        ++feedback_completed;feedback_busy=false;
        if(!feedback_allowed())return;
        ++feedback_epoch;
        if(settings.toast_enabled)toast.show(hwnd,toast_area(),text,key?*key:feedback_shortcut(id),false,UINT(settings.toast_duration));
        const auto now=GetTickCount64();
        if(settings.command_sound&&(!feedback_sound_known||now-feedback_last_sound>=150)){
            feedback_sound_known=true;feedback_last_sound=now;try{dialogs.command_pip();}catch(...){}
        }
    }
    void feedback_cancel(){if(feedback_busy){toast.dismiss();feedback_busy=false;++feedback_epoch;}}
    void cancel_load_feedback(){
        if(load_feedback&&load_feedback->epoch==feedback_epoch)feedback_cancel();
        load_feedback.reset();
    }
    void start_load_feedback(UINT id){
        if(!loading)return;
        feedback_begin(id,id==Reload?L"再読み込み中…":L"クリップボードの画像ファイルを読み込み中…");
        load_feedback=LoadFeedback{loader->sequence(),feedback_epoch,id,feedback_key};
    }
    void complete_load_feedback(uint64_t sequence){
        if(!load_feedback||load_feedback->sequence!=sequence)return;
        auto done=std::move(*load_feedback);load_feedback.reset();
        if(done.epoch==feedback_epoch)feedback_done(done.command,done.command==Reload?L"画像を再読み込みしました":L"クリップボードの画像ファイルを開きました",&done.key);
    }
    std::pair<double,double> feedback_density() const {
        if(image_metadata)return {image_metadata->dpi_x,image_metadata->dpi_y};
        return image?std::pair{image->metadata.dpi_x,image->metadata.dpi_y}:std::pair{0.,0.};
    }
    static bool feedback_image_command(UINT id){
        return id==Crop||id==DeleteSelection||(id>=RotateRight&&id<=Canvas)||(id>=Negative&&id<=Posterize)||
            (id>=RotateAngle&&id<=ReplaceColor)||(id>=MaskNegative&&id<=CPMask)||(id>=PaletteEdit&&id<=BlurSoft);
    }
    struct FeedbackCommand {
        App &app;UINT id,previous_id;std::wstring previous_key;
        const Image *image,*undo;size_t undos,redos;bool crop,floating,text,playing;size_t frame;
        uint64_t completed,epoch;int exceptions;std::pair<double,double> density;
        FeedbackCommand(App &owner,UINT command,bool keyboard):app(owner),id(command),previous_id(owner.feedback_command),previous_key(std::move(owner.feedback_key)),
            image(owner.image.get()),undo(owner.undo_image.get()),undos(owner.undo_history.size()),redos(owner.redo_history.size()),
            crop(owner.cropping.has_value()),floating(owner.floating.has_value()),text(owner.floating&&owner.floating->text_annotation),
            playing(owner.player.playing),frame(owner.player.frame),completed(owner.feedback_completed),epoch(owner.feedback_epoch),exceptions(std::uncaught_exceptions()),density(owner.feedback_density()){
            app.feedback_command=id;app.feedback_key=keyboard?app.settings.first_key(id):L"";
        }
        ~FeedbackCommand(){
            try{
                if(exceptions==std::uncaught_exceptions()&&completed==app.feedback_completed){
                    if(id==CommitSelection&&image!=app.image.get())app.feedback_done(id,crop?L"トリミングを適用しました":text?L"文字を適用しました":L"自由変形を適用しました");
                    else if((id==Undo||id==Redo)&&(image!=app.image.get()||undo!=app.undo_image.get()||undos!=app.undo_history.size()||redos!=app.redo_history.size()))app.feedback_done(id,id==Undo?L"元に戻しました":L"やり直しました");
                    else if((id==CancelSelection||id==Undo||id==ClearSelection)&&(crop||floating)&&!app.cropping&&!app.floating)app.feedback_done(id,L"編集を中止しました");
                    else if(feedback_image_command(id)&&(image!=app.image.get()||density!=app.feedback_density()))app.feedback_done(id,std::wstring(command_name(id))+L"を適用しました");
                    else if(app.settings.toast_view_commands){
                        if(id==Fullscreen||id==Topmost||id==Comic||id==KeepZoom||id==Checker||id==Loop)app.feedback_done(id,std::wstring(command_name(id))+(app.checked(id)?L"：オン":L"：オフ"));
                        else if((id==Play||id==StopAnimation)&&app.image&&(playing!=app.player.playing||frame!=app.player.frame))app.feedback_done(id,app.player.playing?L"再生しました":L"一時停止しました");
                    }
                }
                if(completed==app.feedback_completed&&epoch!=app.feedback_epoch&&
                   (!app.load_feedback||app.load_feedback->epoch!=app.feedback_epoch)&&
                   (!app.copy_feedback||app.copy_feedback->epoch!=app.feedback_epoch))app.feedback_cancel();
            }catch(...){app.feedback_cancel();} // Notification failure never changes a command result.
            app.feedback_command=previous_id;app.feedback_key=std::move(previous_key);
        }
    };
