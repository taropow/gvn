    SIZE copy_current_selection() {
        cancel_async_copy();
        const auto metadata=current_metadata();
        if(selection.empty()){
            // The document is immutable for this synchronous command; no full
            // still_frame allocation is needed merely to encode one frame.
            copy_image(hwnd,*image,player.frame,std::nullopt,&metadata);
            return {LONG(image->width),LONG(image->height)};
        }
        const auto area=selection.clipped(image->width,image->height);
        if(area.empty())throw std::invalid_argument("Empty selection");
        auto selected=new_image(area.w,area.h);selected.metadata=metadata;
        const auto &source=image->frames.at(player.frame).pixels;
        for(int y=0;y<area.h;++y)for(int x=0;x<area.w;++x)
            selected.frames[0].pixels[size_t(y)*area.w+x]=area.contains(x+area.x,y+area.y)
                ?source[size_t(y+area.y)*image->width+x+area.x]:Pixel{};
        copy_image(hwnd,selected,0,SelectionOrigin{selection.x,selection.y,image->width,image->height},&metadata);
        return {area.w,area.h};
    }
    struct PreparedExport {
        std::weak_ptr<Image> source;
        size_t frame{};bool named{};
        std::wstring name;SaveFormat format{};SaveOptions options;ImageMetadata metadata;
        std::filesystem::path path;uintmax_t bytes{};std::filesystem::file_time_type stamp;
    };
    std::optional<PreparedExport> prepared_export;
    std::filesystem::path prepared_export_path(bool named) {
        const auto metadata=current_metadata();
        if(!named&&!modified&&!image->animated()&&image->format!=L"見開き"&&!image->path.empty()&&std::filesystem::is_regular_file(image->path))
            return std::filesystem::absolute(image->path);
        const auto name=named?editor_chrome.text(editor_id::Name):std::wstring{};
        const auto format=named?editor_chrome.export_format():SaveFormat::Png;
        if(prepared_export){const auto &cached=*prepared_export;std::error_code error;
            if(cached.source.lock()==image&&cached.frame==player.frame&&cached.named==named&&cached.name==name&&
                cached.format==format&&cached.options==settings.saving&&cached.metadata==metadata&&
                std::filesystem::is_regular_file(cached.path,error)&&!error){
                const auto bytes=std::filesystem::file_size(cached.path,error);
                if(!error&&bytes==cached.bytes){const auto stamp=std::filesystem::last_write_time(cached.path,error);
                    if(!error&&stamp==cached.stamp)return cached.path;}
            }
        }
        const auto path=named?export_drag_path(*image,player.frame,name,format,settings.saving,&metadata)
            :drag_path(*image,player.frame,true,&metadata);
        prepared_export=PreparedExport{image,player.frame,named,name,format,settings.saving,metadata,path,
            std::filesystem::file_size(path),std::filesystem::last_write_time(path)};
        return path;
    }
