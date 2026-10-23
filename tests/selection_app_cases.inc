    Image selection_fixture(unsigned width=128,unsigned height=96) {
        auto value=new_image(width,height);
        for(size_t n=0;n<value.frames[0].pixels.size();++n)value.frames[0].pixels[n]={uint8_t(n*17),uint8_t(n*31),uint8_t(n*7),uint8_t(n*13)};
        return value;
    }
    POINT selection_screen(int x,int y) {
        const bool actual=std::abs(app.view.scale-1)<1e-9;
        return {LONG(std::lround(app.left+(actual?std::floor(app.view.left()):app.view.left())+x*app.view.scale)),
                LONG(std::lround(app.top+(actual?std::floor(app.view.top()):app.view.top())+y*app.view.scale))};
    }
    void selection_mouse(UINT message,int x,int y,WPARAM flags=0) {
        const auto p=selection_screen(x,y);app.message(message,flags,MAKELPARAM(p.x,p.y));
    }
    void floating_paste_origin_transform_cancel_commit_and_undo() {
        app.settings.confirm_unsaved=false;app.replace(selection_fixture(),false,true);
        const auto original=app.image,previous_undo=app.undo_image;app.selection={5,5,20,20};
        const auto source=selection_fixture(32,24);copy_image(app.hwnd,source,0,SelectionOrigin{30,25,128,96});
        const auto sequence=GetClipboardSequenceNumber();command(PasteSelection);
        CHECK(app.floating&&app.image==original&&app.undo_image==previous_undo&&!app.modified);
        CHECK(app.floating->box.cx==46&&app.floating->box.cy==37&&app.floating->original_position->x==30);
        CHECK(GetClipboardSequenceNumber()==sequence&&SendMessageW(app.toolbar.hwnd,TB_COMMANDTOINDEX,TransformSelection,0)>=0&&app.editor_chrome.control(editor_id::Apply)&&app.editor_chrome.control(editor_id::Cancel));
        selection_mouse(WM_LBUTTONDOWN,46,37,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,61,47,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,61,47);
        CHECK(app.floating->box.cx==61&&app.floating->box.cy==47&&app.image==original);
        texts.push_back(L"10 12 48 36 30");command(TransformSelection);
        CHECK(app.floating->box.width==48&&app.floating->box.angle==30);
        command(CancelSelection);CHECK(!app.floating&&app.image==original&&app.selection.x==5&&app.selection.w==20&&app.undo_image==previous_undo);
        CHECK(SendMessageW(app.toolbar.hwnd,TB_COMMANDTOINDEX,TransformSelection,0)<0);
        command(PasteSelection);texts.push_back(L"10 12 48 36 30");command(TransformSelection);
        const auto expected=paste_floating(*original,source,app.floating->box);
        app.message(WM_KEYDOWN,VK_RETURN,0);CHECK(!app.floating&&app.modified&&app.image->frames[0].pixels==expected.frames[0].pixels);
        CHECK(app.undo_image==original&&GetClipboardSequenceNumber()==sequence);
        command(Undo);CHECK(app.image==original&&!app.modified);
        command(CloseImage);command(PasteSelection);CHECK(!app.image&&app.floating&&app.view.canvas_w==128&&app.view.canvas_h==96);
        command(CommitSelection);CHECK(app.image&&app.image->width==128&&app.image->height==96&&app.modified);
        command(CloseImage);copy_image(app.hwnd,source,0);command(PasteSelection);
        CHECK(app.floating&&!app.floating->original_position&&app.floating->box.cx==16&&app.floating->box.cy==12);
        command(CancelSelection);CHECK(!app.image&&!app.floating);
    }
    void selection_tools_native_input_boolean_alpha_delete_move_and_undo() {
        app.settings.confirm_unsaved=false;app.replace(selection_fixture(),false,true);const auto original=app.image;
        app.view.scale=4;app.view.manual=true;app.update();command(SelectPolygon);
        selection_mouse(WM_LBUTTONDOWN,4,4,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,4,4);
        selection_mouse(WM_MOUSEMOVE,40,4);selection_mouse(WM_LBUTTONDOWN,40,4,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,40,4);
        selection_mouse(WM_LBUTTONDOWN,4,36,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,4,36);app.message(WM_KEYDOWN,VK_RETURN,0);
        CHECK(!app.polygon_drawing&&app.selection.contains(5,5)&&!app.selection.contains(39,35));
        const auto triangle=app.selection;command(Copy);const auto copied=paste_selection_clipboard(app.hwnd);
        CHECK(copied.origin&&copied.origin->x==triangle.x&&copied.origin->y==triangle.y&&copied.origin->canvas_width==128);
        CHECK(copied.image.frames[0].pixels==selection_image(*original,triangle).frames[0].pixels);
        command(DeleteSelection);for(int y=0;y<96;++y)for(int x=0;x<128;++x)CHECK(app.image->frames[0].pixels[size_t(y)*128+x]==(triangle.contains(x,y)?Pixel{}:original->frames[0].pixels[size_t(y)*128+x]));
        command(Undo);CHECK(app.image==original);app.selection=triangle;command(InvertSelection);CHECK(!app.selection.contains(5,5)&&app.selection.contains(100,80));
        command(InvertSelection);CHECK(app.selection.contains(5,5)&&!app.selection.contains(100,80));
        command(SelectMove);CHECK(app.floating&&app.image==original);
        app.message(WM_KEYDOWN,VK_RIGHT,0);app.message(WM_KEYDOWN,VK_DOWN,0);
        const auto moved=paste_floating(erase_selection(*original,triangle),selection_image(*original,triangle),app.floating->box);
        command(CommitSelection);CHECK(app.image->frames[0].pixels==moved.frames[0].pixels);command(Undo);CHECK(app.image==original);
        command(SelectFreehand);selection_mouse(WM_LBUTTONDOWN,50,40,MK_LBUTTON);selection_mouse(WM_MOUSEMOVE,80,40,MK_LBUTTON);
        selection_mouse(WM_MOUSEMOVE,80,70,MK_LBUTTON);selection_mouse(WM_LBUTTONUP,50,70);
        CHECK(app.selection.contains(60,50)&&!app.selection.contains(5,5));
        const auto freehand=app.selection;command(SelectRectangle);
        selection_mouse(WM_LBUTTONDOWN,5,5,MK_LBUTTON|MK_SHIFT);selection_mouse(WM_MOUSEMOVE,20,20,MK_LBUTTON|MK_SHIFT);selection_mouse(WM_LBUTTONUP,20,20);
        CHECK(app.selection.contains(10,10)&&app.selection.contains(60,50));
        command(ClearSelection);CHECK(app.selection.empty()&&app.renderer.viewport_left==app.left);
        command(SelectPolygon);selection_mouse(WM_LBUTTONDOWN,5,5,MK_LBUTTON);app.message(WM_KEYDOWN,VK_ESCAPE,0);CHECK(!app.polygon_drawing&&app.selection.empty());
        app.selection=freehand;command(SelectFreehand);selection_mouse(WM_LBUTTONDOWN,3,3,MK_LBUTTON);app.message(WM_CANCELMODE,0,0);
        CHECK(!app.selecting&&app.selection.contains(60,50));
    }
    std::vector<uint32_t> selection_capture() {
        RECT bounds{};GetClientRect(app.hwnd,&bounds);const int width=bounds.right,height=bounds.bottom;
        const auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};void *data{};
        const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);CHECK(dc&&bitmap&&data);
        const auto previous=SelectObject(dc,bitmap);CHECK(SUCCEEDED(app.render_scene(dc)));
        std::vector<uint32_t> result(static_cast<uint32_t *>(data),static_cast<uint32_t *>(data)+size_t(width)*height);
        SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);return result;
    }
    void marching_ants_animate_one_device_pixel_and_leave_rgba_unchanged() {
        ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);auto image=new_image(512,512);
        std::fill(image.frames[0].pixels.begin(),image.frames[0].pixels.end(),Pixel{70,90,110,255});
        app.replace(std::move(image),false,true);const auto original=app.image;const auto pixels=original->frames[0].pixels;
        RECT bounds{};GetClientRect(app.hwnd,&bounds);const int stride=bounds.right;
        for(bool editing:{false,true}){if(app.editor_mode!=editing)command(ToggleEditor);
        for(double scale:{.25,1.,4.}){
            app.view.scale=scale;app.view.pan_x=app.view.pan_y=0;app.view.manual=true;app.selection={};app.update();const auto base=selection_capture();
            app.selection={176,216,160,80};app.update();CHECK(app.ants_timer);app.renderer.ants_phase=0;
            const auto a=selection_capture();CHECK(a==selection_capture());const auto bytes=app.renderer.source_bytes();
            app.message(WM_TIMER,39,0);CHECK(app.renderer.ants_phase==1);const auto b=selection_capture();CHECK(a!=b);
            CHECK(app.renderer.source_bytes()==bytes&&app.selection.storage_bytes()==0);
            const bool actual=std::abs(scale-1)<1e-9;
            const int x=int(std::floor(app.left+(actual?std::floor(app.view.left()):app.view.left())+176*scale))+5;
            const int y=int(std::floor(app.top+(actual?std::floor(app.view.top()):app.view.top())+216*scale));
            int white=0,black=0,changed=0;
            for(int offset=0;offset<24;++offset){const auto i=size_t(y)*stride+x+offset;const auto color=a[i]&0xffffff;
                white+=color==0xffffff;black+=color==0;changed+=a[i]!=b[i];
                CHECK(a[i-stride]==base[i-stride]&&a[i+stride]==base[i+stride]);}
            CHECK(white>=8&&black>=8&&changed>=4);
            app.renderer.ants_phase=0;const FloatPoint box[]{{176,216},{336,216},{336,296},{176,296}};
            app.selection=SelectionRegion::polygon(box,512,512);CHECK(selection_capture()==a);
            CHECK(app.image==original&&original->frames[0].pixels==pixels);
        }}
        ShowWindow(app.hwnd,SW_MINIMIZE);CHECK(!app.ants_timer);ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);app.update();CHECK(app.ants_timer);
        command(ClearSelection);CHECK(!app.ants_timer);const auto phase=app.renderer.ants_phase;app.message(WM_TIMER,39,0);CHECK(phase==app.renderer.ants_phase);
        command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==pixels);
    }
    void selection_outline_and_transform_focused_performance() {
        app.replace(selection_fixture(2048,1536),false,true);app.selection={300,300,1000,800};app.view.apply_fit(Fit::Contain);app.update();
        CHECK(SUCCEEDED(app.render_scene()));const auto bytes=app.renderer.source_bytes(),tile=app.renderer.sample_bytes();
        const auto measure=[&](int count,const auto &action){const auto start=std::chrono::steady_clock::now();for(int n=0;n<count;++n)action(n);
            return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/count;};
        const double ants=measure(40,[&](int){CHECK(SUCCEEDED(app.render_scene()));});
        CHECK(app.renderer.source_bytes()==bytes&&app.renderer.sample_bytes()==tile&&app.selection.storage_bytes()==0);
        const FloatPoint polygon[]{{300,300},{1500,350},{1200,1200},{350,1100}};
        const double raster=measure(10,[&](int){app.selection=SelectionRegion::polygon(polygon,2048,1536);});
        CHECK(app.selection.storage_bytes()<1024*1024);command(SelectMove);CHECK(SUCCEEDED(app.render_scene()));
        const auto pixels=app.image->frames[0].pixels;const double transform=measure(30,[&](int n){app.floating->box.angle=n;app.floating->box.cx+=.1;CHECK(SUCCEEDED(app.render_scene()));});
        CHECK(app.image->frames[0].pixels==pixels);command(CancelSelection);
        std::cout<<"SELECTION_PERF ants_ms="<<ants<<" polygon_ms="<<raster<<" transform_ms="<<transform<<" region_bytes="<<app.selection.storage_bytes()<<" source_bytes="<<bytes<<" tile_bytes="<<tile<<'\n';
        CHECK(ants<80&&raster<100&&transform<100);
    }
    void floating_invalid_metadata_and_allocation_failure_preserve_document() {
        app.settings.confirm_unsaved=false;app.replace(selection_fixture(),false,true);
        const auto original=app.image,undo=app.undo_image;const auto source=selection_fixture(32,24);
        copy_image(app.hwnd,source,0,SelectionOrigin{15,17,128,96});
        CHECK(OpenClipboard(app.hwnd));const auto global=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,64);CHECK(global);
        CHECK(SetClipboardData(RegisterClipboardFormatW(selection_clipboard_format),global));CHECK(CloseClipboard());
        command(PasteSelection);CHECK(app.floating&&app.floating->invalid_metadata&&!app.floating->original_position);
        CHECK(app.floating->source->frames[0].pixels==source.frames[0].pixels);
        app.editing.allocating=[](size_t){throw std::bad_alloc();};THROWS(command(CommitSelection));
        CHECK(app.floating&&app.image==original&&app.undo_image==undo&&!app.modified);app.editing.allocating={};
        command(CancelSelection);app.loading=true;THROWS(command(PasteSelection));app.loading=false;
        CHECK(!app.floating&&app.image==original);
        clipboard_text(L"This is not an image path");THROWS(command(PasteSelection));CHECK(app.image==original&&app.undo_image==undo);
        open(fixture(L"blend.apng"));const auto animation=app.image;const auto frame=app.player.frame;const bool playing=app.player.playing;
        copy_image(app.hwnd,source,0);command(PasteSelection);CHECK(!app.player.playing&&app.image==animation);
        command(CancelSelection);CHECK(app.player.frame==frame&&app.player.playing==playing&&app.image==animation);
        command(PasteSelection);answers.push_back(IDCANCEL);command(CommitSelection);CHECK(app.floating&&app.image==animation);
        answers.push_back(IDOK);command(CommitSelection);CHECK(!app.floating&&!app.image->animated()&&app.image->path.empty());
        command(Undo);CHECK(app.image==animation&&app.player.frame==frame&&app.player.playing==playing);
    }
    void polygon_effects_and_cut_preserve_unselected_pixels_and_alpha() {
        app.settings.confirm_unsaved=false;app.replace(selection_fixture(),false,true);const auto original=app.image;
        const FloatPoint points[]{{5,5},{100,5},{5,80}};const auto region=SelectionRegion::polygon(points,128,96);
        for(auto id:{Negative,Brightness,Levels,Gaussian}){
            app.selection=region;
            if(id==Brightness)texts.push_back(L"20");if(id==Levels)texts.push_back(L"10, 220, 1.2, 0, 255, 0");if(id==Gaussian)texts.push_back(L"1");
            command(id);CHECK(app.image!=original);
            for(int y=0;y<96;++y)for(int x=0;x<128;++x)if(!region.contains(x,y))CHECK(app.image->frames[0].pixels[size_t(y)*128+x]==original->frames[0].pixels[size_t(y)*128+x]);
            command(Undo);CHECK(app.image==original);
        }
        app.selection=region;app.settings.cut_transparent=true;command(Cut);
        CHECK(app.image->frames[0].pixels==erase_selection(*original,region).frames[0].pixels);
        CHECK(paste_image(app.hwnd).frames[0].pixels==selection_image(*original,region).frames[0].pixels);
        command(Undo);CHECK(app.image==original);
    }
