    void interactive_display_and_copy_benchmark(){
        app.settings.confirm_unsaved=false;app.settings.display_up=app.settings.display_down=DisplayLanczos3;
        app.replace(selection_fixture(2048,1536),false,true);
        SetWindowPos(app.hwnd,nullptr,0,0,1800,1400,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOZORDER);app.layout();
        app.view.scale=1.25;app.view.manual=true;const auto original=app.image;
        CHECK(app.asynchronous_display());
        CHECK(SUCCEEDED(app.renderer.draw(app.hwnd,nullptr,0,app.view,app.top,app.bottom,nullptr)));
        const auto sync_start=std::chrono::steady_clock::now();
        CHECK(SUCCEEDED(app.renderer.draw(app.hwnd,app.image.get(),0,app.view,app.top,app.bottom,nullptr)));
        const auto sync_draw=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-sync_start).count();
        app.renderer.invalidate();
        const auto draw_start=std::chrono::steady_clock::now();CHECK(SUCCEEDED(app.render_scene()));
        const auto draw_receipt=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-draw_start).count();
        CHECK(app.display_sampler);const bool editor=app.editor_mode;command(ToggleEditor);CHECK(app.editor_mode!=editor);
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!app.renderer.sample_current(app.image.get(),0,app.view)&&std::chrono::steady_clock::now()<deadline){app.render_scene();pump();Sleep(1);}
        CHECK(app.renderer.sample_current(app.image.get(),0,app.view)&&app.renderer.filter(app.view)==DisplayLanczos3);
        const auto synchronous=std::chrono::steady_clock::now();app.copy_current_selection();
        const auto sync_copy=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-synchronous).count();
        const auto start=std::chrono::steady_clock::now();app.command(Copy,true);
        const auto receipt=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        CHECK(app.copy_feedback&&app.clipboard_worker);command(Checker);
        CHECK(app.clipboard_worker->wait_idle(std::chrono::seconds(10)));pump();
        const auto copy_ready=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        CHECK(!app.copy_feedback&&paste_image(app.hwnd).frames[0].pixels==original->frames[0].pixels&&app.image==original);
        std::cout<<"INTERACTIVE_RESPONSE {\"sync_draw_ms\":"<<sync_draw<<",\"async_draw_receipt_ms\":"<<draw_receipt
            <<",\"sync_copy_ms\":"<<sync_copy<<",\"async_copy_receipt_ms\":"<<receipt
            <<",\"async_copy_ready_ms\":"<<copy_ready<<"}\n";
        app.view.scale=1;CHECK(!app.asynchronous_display());
        app.replace(selection_fixture(16,12),false,true);app.command(Copy);CHECK(!app.copy_feedback);
        CHECK(paste_image(app.hwnd).width==16&&notices.empty());
    }
    void interactive_copy_ordering_and_external_clipboard_guard(){
        app.settings.confirm_unsaved=false;app.replace(selection_fixture(1024,1024),false,true);
        app.image_metadata=app.image->metadata;app.image_metadata->dpi_defined=true;app.image_metadata->dpi_x=144;app.image_metadata->dpi_y=288;
        const auto original=app.image;app.selection=SelectionRegion(100,100,100,100).inverted(1024,1024);
        const auto chosen=app.selection;auto expected=selection_image(*original,chosen);const auto marker=selection_fixture(7,5);
        copy_image(app.hwnd,marker,0);std::atomic<unsigned> entered{};std::atomic<bool> release{};
        const auto cleanup=std::shared_ptr<void>(nullptr,[&](void*){app.cancel_async_copy();app.clipboard_worker.reset();});
        app.clipboard_encoder=[&](const ClipboardCopyRequest &request,const CancelCheck &cancel){
            ++entered;while(!release&&!cancel())Sleep(1);check_cancelled(cancel);return prepare_clipboard_copy(request,cancel);
        };
        const auto await=[&](unsigned jobs){const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(entered<jobs&&std::chrono::steady_clock::now()<limit)Sleep(1);CHECK(entered>=jobs);};
        const auto settle=[&]{CHECK(app.clipboard_worker->wait_idle(std::chrono::seconds(10)));
            const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(app.copy_feedback&&std::chrono::steady_clock::now()<end){pump();Sleep(1);}CHECK(!app.copy_feedback);};
        app.command(Copy,true);CHECK(app.copy_feedback&&app.feedback_busy);await(1);
        command(CloseImage);CHECK(!app.image);app.command(NewPaste,true);
        CHECK(!app.image&&app.copy_feedback->deferred.size()==1);release=true;settle();
        CHECK(app.image&&app.image->frames[0].pixels==expected.frames[0].pixels);
        const auto pasted=paste_selection_clipboard(app.hwnd);CHECK(pasted.origin&&pasted.origin->canvas_width==1024);
        CHECK(pasted.image.metadata.dpi_defined&&std::abs(pasted.image.metadata.dpi_x-144)<.03&&std::abs(pasted.image.metadata.dpi_y-288)<.03);
        CHECK(original->frames[0].pixels[0]==expected.frames[0].pixels[0]);
        release=false;app.command(Copy);await(2);copy_image(app.hwnd,marker,0);release=true;settle();
        CHECK(paste_image(app.hwnd).frames[0].pixels==marker.frames[0].pixels);
        release=false;app.command(Copy);await(3);
        auto changed=selection_fixture(1024,1024);changed.frames[0].pixels[0]={5,9,27,61};app.replace(std::move(changed),false,true);
        const auto latest=app.image;app.command(Copy);await(4);release=true;settle();
        CHECK(paste_image(app.hwnd).frames[0].pixels==latest->frames[0].pixels);
        release=false;app.command(Copy);await(5);
        std::promise<void> opened;auto locked=opened.get_future();
        auto receiver=std::async(std::launch::async,[&]{
            CHECK(OpenClipboard(nullptr));opened.set_value();const auto handle=GetClipboardData(RegisterClipboardFormatW(L"PNG"));
            CHECK(handle);const auto data=static_cast<const uint8_t *>(GlobalLock(handle));CHECK(data);
            std::vector<uint8_t> bytes(data,data+GlobalSize(handle));GlobalUnlock(handle);CHECK(CloseClipboard());return bytes;
        });
        CHECK(locked.wait_for(std::chrono::seconds(3))==std::future_status::ready);release=true;
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(receiver.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready&&std::chrono::steady_clock::now()<end){pump();Sleep(1);}
        CHECK(receiver.wait_for(std::chrono::seconds(0))==std::future_status::ready);
        CHECK(decode(receiver.get()).frames[0].pixels==latest->frames[0].pixels);settle();
        release=false;app.command(Copy);await(6);release=true;
        DestroyWindow(app.hwnd);CHECK(!app.clipboard_worker&&!app.copy_feedback);
        CHECK(paste_image(nullptr).frames[0].pixels==latest->frames[0].pixels);
        CHECK(notices.empty());
    }
