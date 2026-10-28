    std::unique_ptr<DisplaySampler> display_sampler;
    std::optional<DisplayRequest> failed_sampling;
    DisplayRequest display_request() const {return {image,player.frame,view,renderer.filter(view)};}
    bool asynchronous_display() const {
        return image&&!floating&&!cropping&&!previewing_mask()&&(!image->animated()||!player.playing)&&
            !renderer.resizing&&!renderer.dragging_low&&background_display_sample(*image,view,renderer.filter(view));
    }
    void cancel_invalid_sampling() {
        const auto current=display_request();
        if(failed_sampling&&!failed_sampling->same(current))failed_sampling.reset();
        if(display_sampler&&(!asynchronous_display()||!display_sampler->matches(current)))display_sampler->cancel();
    }
    bool request_display_sample() {
        if(!asynchronous_display())return false;
        const auto current=display_request();
        if(failed_sampling&&failed_sampling->same(current))return false;
        if(renderer.sample_current(image.get(),player.frame,view)){
            if(display_sampler&&!display_sampler->idle())display_sampler->cancel();
            return false;
        }
        if(!display_sampler){const auto window=hwnd;
            display_sampler=std::make_unique<DisplaySampler>([window]{PostMessageW(window,kDisplayReady,0,0);});}
        display_sampler->request(current);return true;
    }
    void accept_display_sample() {
        if(!display_sampler)return;
        auto sample=display_sampler->take();
        if(!sample||!asynchronous_display()||!sample->request.same(display_request()))return;
        if(sample->failed){failed_sampling=std::move(sample->request);display_sampler->cancel();}
        else renderer.adopt_sample(image.get(),player.frame,view,sample->method,std::move(sample->tile));
        InvalidateRect(hwnd,nullptr,FALSE);
    }
