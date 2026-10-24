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
