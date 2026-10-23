#ifdef GVN_APP_TESTS
    std::vector<RECT> recorded_damage;
#endif
    RECT canvas_damage(FloatPoint a,FloatPoint b,double padding=3) const {
        const ViewportTransform transform(view,left,top);
        a=transform.to_client(a);b=transform.to_client(b);
        const auto clip=[](double v,double lo,double hi){return LONG(std::clamp(v,lo,hi));};
        return {clip(std::floor(std::min(a.x,b.x)-padding),left,left+view.width),
                clip(std::floor(std::min(a.y,b.y)-padding),top,top+view.height),
                clip(std::ceil(std::max(a.x,b.x)+padding),left,left+view.width),
                clip(std::ceil(std::max(a.y,b.y)+padding),top,top+view.height)};
    }
    void invalidate_line(FloatPoint a,FloatPoint b,double padding=3) {
        const auto rect=canvas_damage(a,b,padding);
        if(rect.right>rect.left&&rect.bottom>rect.top){
#ifdef GVN_APP_TESTS
            recorded_damage.push_back(rect);
#endif
            InvalidateRect(hwnd,&rect,FALSE);
        }
    }
    void invalidate_pen_segment(FloatPoint a,FloatPoint b) {
        const auto &style=renderer.pen_style;const auto &effects=style.effects;
        const double reach=style.width/2+(effects.outline?effects.outline_width:0)+
            (effects.shadow?std::abs(effects.shadow_offset)+effects.shadow_blur*2:0);
        invalidate_line(a,b,reach*view.scale+4);
    }
    void invalidate_selection_lines() {
        if(floating){const auto handles=floating->box.handles();
            for(size_t n=0;n<8;n+=2)invalidate_line(handles[n],handles[(n+2)%8],8);
        }else if(!selection.empty()&&selection.rectangular()){
            const double x=selection.x,y=selection.y,r=x+selection.w,b=y+selection.h;
            invalidate_line({x,y},{r,y});invalidate_line({r,y},{r,b});
            invalidate_line({r,b},{x,b});invalidate_line({x,b},{x,y});
        }else if(!selection.empty())invalidate_line({double(selection.x),double(selection.y)},
            {double(selection.x+selection.w),double(selection.y+selection.h)});
        if(renderer.selection_path.size()>1){auto low=renderer.selection_path[0],high=low;
            for(auto point:renderer.selection_path){low.x=std::min(low.x,point.x);low.y=std::min(low.y,point.y);high.x=std::max(high.x,point.x);high.y=std::max(high.y,point.y);}
            invalidate_line(low,high);
        }
    }
