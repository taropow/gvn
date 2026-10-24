    void help_opens_markdown_with_association_fallback_without_changing_image() {
        open(fixture(L"alpha.png"));const auto original=app.image;const auto pixels=app.image->frames[0].pixels;
        const auto manual=viewer_executable().parent_path()/L"gvn-manual.md";
        CHECK(std::filesystem::is_regular_file(manual));
        const auto before=external_launches.size(),messages=notices.size();
        command(Help);app.message(WM_KEYDOWN,VK_F1,0);
        CHECK(external_launches.size()==before+2&&notices.size()==messages);
        for(size_t i=before;i<external_launches.size();++i){
            const auto &launch=external_launches[i];CHECK(launch.file==manual.wstring()&&launch.verb==L"open");
            CHECK(launch.parameters.empty()&&launch.directory==manual.parent_path()&&launch.show==SW_SHOWNORMAL);
        }
        const auto special=temporary/L"日本語 & % 操作説明.md";
        std::filesystem::copy_file(manual,special);
        for(int error:{int(ERROR_NO_ASSOCIATION),int(SE_ERR_NOASSOC)}){
            external_launches.clear();
            app.dialogs.external_launch=[&](HWND owner,const ExternalLaunch &request){
                CHECK(owner==app.hwnd);external_launches.push_back(request);
                if(external_launches.size()==1)throw std::system_error(error,std::system_category());
                return true;
            };
            app.show_help(special);CHECK(external_launches.size()==2);
            const auto &fallback=external_launches.back();
            CHECK(std::filesystem::path(fallback.file).filename()==L"notepad.exe");
            CHECK(std::filesystem::path(fallback.file).is_absolute());
            CHECK(fallback.parameters==quote_external_argument(special.wstring())&&fallback.directory==temporary);
        }
        external_launches.clear();app.show_help(temporary/L"missing.md");
        CHECK(external_launches.empty()&&notices.back().find(L"操作説明ファイルが見つかりません")!=std::wstring::npos);
        app.dialogs.external_launch=[&](HWND,const ExternalLaunch &request)->bool{
            external_launches.push_back(request);throw std::system_error(ERROR_ACCESS_DENIED,std::system_category());
        };
        THROWS(app.show_help(special));CHECK(external_launches.size()==1);
        CHECK(app.image==original&&app.image->frames[0].pixels==pixels&&!app.modified);
    }
    void startup_background_is_initialized_before_first_renderer_frame() {
        CHECK(app.paints==0&&!app.image);
        RECT client{};GetClientRect(app.hwnd,&client);
        gvn_test::BackgroundSurface surface(client.right,client.bottom);
        const auto erased=[&] {
            FillRect(surface.dc,&client,ui_brush(RGB(255,0,255)));
            CHECK(SendMessageW(app.hwnd,WM_ERASEBKGND,reinterpret_cast<WPARAM>(surface.dc),0)==1);
        };
        const int x=app.renderer.viewport_left,y=app.top;
        for(int pattern:{0,2,4}){
            app.renderer.background_tiled=true;app.renderer.background_pattern=pattern;
            app.renderer.background_first=0x172a43;app.renderer.background_second=0x65839b;
            erased();const int cell=8<<pattern;
            std::cout<<"STARTUP_ERASE pattern="<<pattern<<" pixel="<<surface.color(x+1,y+1)<<'\n';
            CHECK(surface.color(x+1,y+1)==0x172a43);
            CHECK(surface.color(x+cell+1,y+1)==0x65839b);
            CHECK(surface.color(x+cell+1,y+cell+1)==0x172a43);
            CHECK(app.paints==0&&app.renderer.source_bytes()==0&&app.renderer.sample_bytes()==0);
        }
        app.renderer.background_tiled=false;erased();
        CHECK(surface.color(x+1,y+1)==0x172a43&&surface.color(x+42,y+42)==0x172a43);
        const int saved=SaveDC(surface.dc);IntersectClipRect(surface.dc,x+5,y+5,x+20,y+20);
        app.renderer.background_first=0x443322;
        CHECK(SendMessageW(app.hwnd,WM_ERASEBKGND,reinterpret_cast<WPARAM>(surface.dc),0)==1);
        RestoreDC(surface.dc,saved);
        CHECK(surface.color(x+10,y+10)==0x443322&&surface.color(x+2,y+2)==0x172a43);
        CHECK(app.paints==0);
        InvalidateRect(app.hwnd,nullptr,FALSE);SendMessageW(app.hwnd,WM_PAINT,0,0);
        CHECK(app.paints>0&&app.smoke_error.empty());
        // Once the renderer has presented, preserve its retained surface during erasure/resize.
        erased();CHECK(surface.color(x+1,y+1)==0xff00ff);
    }
