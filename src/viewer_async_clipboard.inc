    struct CopyFeedback {
        uint64_t sequence{},epoch{};DWORD clipboard_sequence{};std::wstring key;
        std::vector<std::pair<UINT,bool>> deferred;
    };
    std::unique_ptr<ClipboardCopyWorker> clipboard_worker;
    ClipboardCopyWorker::Encoder clipboard_encoder;
    std::optional<CopyFeedback> copy_feedback;
    std::optional<ClipboardCopyResult> prepared_copy;
    void cancel_async_copy(){
        KillTimer(hwnd,kCopyRetryTimer);
        if(clipboard_worker)clipboard_worker->cancel();
        if(copy_feedback&&copy_feedback->epoch==feedback_epoch)feedback_cancel();
        copy_feedback.reset();prepared_copy.reset();
    }
    bool start_async_copy(){
        const auto area=selection.empty()?Rect{0,0,int(image->width),int(image->height)}:
            intersect(selection,{0,0,int(image->width),int(image->height)});
        if(uint64_t(std::max(0,area.w))*std::max(0,area.h)<1024ull*1024)return false;

        if(!clipboard_worker){const auto window=hwnd;
            clipboard_worker=std::make_unique<ClipboardCopyWorker>([window]{PostMessageW(window,kCopyReady,0,0);},clipboard_encoder);}
        announce_clipboard_image(hwnd,!selection.empty());
        const auto clipboard_sequence=GetClipboardSequenceNumber();
        const auto sequence=clipboard_worker->request({image,player.frame,selection,current_metadata()});
        copy_feedback=CopyFeedback{sequence,feedback_epoch,clipboard_sequence,feedback_shortcut(Copy),{}};
        return true;
    }
    bool defer_copy_paste(UINT id,bool keyboard){
        if(!copy_feedback||(id!=Paste&&id!=NewPaste&&id!=PasteSelection&&id!=NewWindowPaste))return false;
        if(GetClipboardOwner()!=hwnd){cancel_async_copy();return false;}
        if(copy_feedback->deferred.size()<16)copy_feedback->deferred.emplace_back(id,keyboard);
        return true;
    }
    bool receive_copy_payload(bool wait){
        if(!copy_feedback||!clipboard_worker)return false;
        if(!prepared_copy){
            if(wait&&!clipboard_worker->wait_idle(std::chrono::seconds(30)))return false;
            prepared_copy=clipboard_worker->take();
        }
        return prepared_copy&&prepared_copy->sequence==copy_feedback->sequence&&prepared_copy->error.empty();
    }
    void render_pending_copy(UINT format){
        if(!copy_feedback||GetClipboardOwner()!=hwnd)return;
        if(format&&format!=CF_DIBV5&&format!=RegisterClipboardFormatW(L"PNG")&&format!=RegisterClipboardFormatW(selection_clipboard_format))return;
        if(!receive_copy_payload(true))return;
        try{
            if(format)render_clipboard_image(prepared_copy->prepared,format);
            else if(publish_delayed_clipboard_image(hwnd,prepared_copy->prepared)){
                if(copy_feedback->epoch==feedback_epoch)feedback_cancel();
                copy_feedback.reset();prepared_copy.reset();KillTimer(hwnd,kCopyRetryTimer);
            }
        }catch(...){}
    }
    void accept_clipboard_copy(){
        KillTimer(hwnd,kCopyRetryTimer);
        if(!copy_feedback)return;
        if(GetClipboardOwner()!=hwnd){cancel_async_copy();return;}
        if(!receive_copy_payload(false)){
            if(prepared_copy&&!prepared_copy->error.empty())cancel_async_copy();
            return;
        }
        try{
            if(!publish_delayed_clipboard_image(hwnd,prepared_copy->prepared)){cancel_async_copy();return;}
        }catch(...){SetTimer(hwnd,kCopyRetryTimer,50,nullptr);return;}
        const auto size=prepared_copy->size;auto pending=std::move(*copy_feedback);
        copy_feedback.reset();prepared_copy.reset();
        if(pending.epoch==feedback_epoch)feedback_done(Copy,L"コピーしました · "+std::to_wstring(size.cx)+L"×"+std::to_wstring(size.cy),&pending.key);
        for(const auto &[id,keyboard]:pending.deferred)command(id,keyboard);
    }
