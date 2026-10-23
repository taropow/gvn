    void prompt_toolbar_current_snapshot_edits_navigation_and_no_file_reread() {
        CHECK(!SendMessageW(app.toolbar.hwnd,TB_ISBUTTONENABLED,ShowPrompt,0));
        size_t shown{};std::wstring expected=L"RESOLVED POSITIVE";
        app.dialogs.prompt_viewer=[&](HWND owner,const PromptDocument &data){++shown;CHECK(owner==app.hwnd&&data.positive.starts_with(expected));};
        command(ShowPrompt);CHECK(shown==0);
        const auto path=temporary/L"生成した画像.png";std::filesystem::copy_file(fixture(L"prompts/parts.png"),path);
        open(path);CHECK(SendMessageW(app.toolbar.hwnd,TB_ISBUTTONENABLED,ShowPrompt,0));
        const auto source=app.image;const auto pixels=source->frames[0].pixels;const auto record=source->metadata.prompt_source;
        std::filesystem::remove(path);command(ShowPrompt);CHECK(shown==1&&app.image==source&&source->frames[0].pixels==pixels&&!app.modified);
        command(RotateRight);const auto edited=app.image;command(ShowPrompt);CHECK(shown==2&&app.image==edited&&app.modified&&app.image->metadata.prompt_source==record);
        command(Undo);command(ShowPrompt);CHECK(shown==3&&app.image==source);
        open(fixture(L"prompts/a1111.png"));expected=L"UPSTREAM POSITIVE";command(ShowPrompt);CHECK(shown==4);
        open(fixture(L"alpha.png"));expected=L"";command(ShowPrompt);CHECK(shown==5);
        auto options=app.settings;options.set_keys(ShowPrompt,{L"Ctrl+Alt+P"});app.apply_settings(options);
        wchar_t label[128]{};CHECK(SendMessageW(app.toolbar.hwnd,TB_GETBUTTONTEXTW,ShowPrompt,reinterpret_cast<LPARAM>(label))>0);CHECK(std::wstring(label).find(L"Ctrl+Alt+P")!=std::wstring::npos);
        CHECK(notices.empty());
    }
    void prompt_encoded_clipboard_and_archive_follow_the_displayed_image() {
        app.settings.confirm_unsaved=false;
        size_t shown{};app.dialogs.prompt_viewer=[&](HWND,const PromptDocument &data){++shown;CHECK(data.positive.starts_with(L"RESOLVED POSITIVE"));};
        const auto bytes=read_file(fixture(L"prompts/parts-only.png"));const auto memory=GlobalAlloc(GMEM_MOVEABLE,bytes.size());CHECK(memory);const auto data=GlobalLock(memory);CHECK(data);memcpy(data,bytes.data(),bytes.size());GlobalUnlock(memory);
        CHECK(OpenClipboard(app.hwnd));CHECK(EmptyClipboard());CHECK(SetClipboardData(RegisterClipboardFormatW(L"PNG"),memory));CHECK(CloseClipboard());
        command(Paste);CHECK(app.image&&app.image->path.empty()&&app.image->metadata.prompt_source);command(ShowPrompt);CHECK(shown==1);
        open(fixture(L"prompts/bundle.zip"));CHECK(!app.archive.empty()&&app.image->metadata.prompt_source);command(ShowPrompt);CHECK(shown==2&&notices.empty());
    }
