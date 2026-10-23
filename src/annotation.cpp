#include "gvn/annotation.h"
#include <windows.h>
#include <cmath>
#include <cwctype>
#include <limits>
namespace gvn {
namespace {
uint8_t byte(double v){return uint8_t(std::clamp(std::lround(v),0l,255l));}
double distance(FloatPoint p,FloatPoint a,FloatPoint b){
    const double dx=b.x-a.x,dy=b.y-a.y,d=dx*dx+dy*dy;
    const double t=d?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/d,0.,1.):0.;
    return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
}
Pixel sample(const Image &s,double x,double y){
    if(x<-.5||y<-.5||x>=s.width-.5||y>=s.height-.5)return {};
    const int ix=int(std::floor(x)),iy=int(std::floor(y));const double dx=x-ix,dy=y-iy;
    if(std::abs(dx)<1e-9&&std::abs(dy)<1e-9)return s.frames[0].pixels[size_t(std::clamp(iy,0,int(s.height)-1))*s.width+std::clamp(ix,0,int(s.width)-1)];
    double a{},r{},g{},b{};
    for(int j=0;j<2;++j)for(int i=0;i<2;++i){
        const auto p=s.frames[0].pixels[size_t(std::clamp(iy+j,0,int(s.height)-1))*s.width+std::clamp(ix+i,0,int(s.width)-1)];
        const double alpha=(i?dx:1-dx)*(j?dy:1-dy)*p.a;
        a+=alpha;r+=alpha*p.r;g+=alpha*p.g;b+=alpha*p.b;
    }
    return a>0?Pixel{byte(r/a),byte(g/a),byte(b/a),byte(a)}:Pixel{};
}
struct TextDc {
    HDC dc{CreateCompatibleDC(nullptr)};
    HBITMAP bitmap{};HFONT font{};HGDIOBJ previous_bitmap{},previous_font{};
    ~TextDc(){if(previous_font)SelectObject(dc,previous_font);if(previous_bitmap)SelectObject(dc,previous_bitmap);
        if(font)DeleteObject(font);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
};
}
int annotation_padding(const AnnotationEffects &e){
    if((e.outline&&(!std::isfinite(e.outline_width)||e.outline_width<1||e.outline_width>16))||
       (e.shadow&&(std::abs(int64_t(e.shadow_offset))>64||e.shadow_blur<0||e.shadow_blur>32)))
        throw std::invalid_argument("フチ・影の数値の範囲を確認してください。");
    return int(std::ceil(e.outline?e.outline_width:0))+(e.shadow?std::abs(e.shadow_offset)+e.shadow_blur:0)+2;
}
Image decorate_annotation(const Image &source,const AnnotationEffects &e,const EditMemory &memory){
    if(!e.outline&&!e.shadow)return source;
    const int pad=annotation_padding(e),w=int(source.width)+2*pad,h=int(source.height)+2*pad;
    const auto count=memory.pixels(w,h);if(memory.before_allocate)memory.before_allocate(count*10);
    auto out=new_image(w,h,memory);
    // A small distance field gives a round outline in linear time, independent of its width.
    std::vector<float> distance(count,1e9f);
    for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x){
        const auto a=source.frames.at(0).pixels[size_t(y)*source.width+x].a;
        if(a)distance[size_t(y+pad)*w+x+pad]=1.f-a/255.f;
    }
    if(e.outline){
        constexpr float diagonal=1.41421356f;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto &d=distance[size_t(y)*w+x];
            if(x)d=std::min(d,distance[size_t(y)*w+x-1]+1);if(y){d=std::min(d,distance[size_t(y-1)*w+x]+1);
                if(x)d=std::min(d,distance[size_t(y-1)*w+x-1]+diagonal);if(x+1<w)d=std::min(d,distance[size_t(y-1)*w+x+1]+diagonal);}}
        for(int y=h-1;y>=0;--y)for(int x=w-1;x>=0;--x){auto &d=distance[size_t(y)*w+x];
            if(x+1<w)d=std::min(d,distance[size_t(y)*w+x+1]+1);if(y+1<h){d=std::min(d,distance[size_t(y+1)*w+x]+1);
                if(x)d=std::min(d,distance[size_t(y+1)*w+x-1]+diagonal);if(x+1<w)d=std::min(d,distance[size_t(y+1)*w+x+1]+diagonal);}}
        for(size_t n=0;n<count;++n){auto c=e.outline_color;c.a=byte(std::clamp(e.outline_width+1-distance[n],0.,1.)*c.a);out.frames[0].pixels[n]=c;}
    }
    for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x){auto &p=out.frames[0].pixels[size_t(y+pad)*w+x+pad];p=over(source.frames[0].pixels[size_t(y)*source.width+x],p);}
    if(e.shadow){
        std::vector<uint8_t> mask(count),scratch(count);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int sx=x-e.shadow_offset,sy=y-e.shadow_offset;
            if(sx>=0&&sy>=0&&sx<w&&sy<h)mask[size_t(y)*w+x]=out.frames[0].pixels[size_t(sy)*w+sx].a;}
        // Three separable box passes approximate a Gaussian without a large convolution kernel.
        // Split the requested radius across the passes; applying it three times
        // makes a nominal 3px shadow spread to 9px and look excessively soft.
        if(e.shadow_blur)for(int pass=0;pass<3;++pass){
            const int radius=(e.shadow_blur+pass)/3,divisor=2*radius+1;if(!radius)continue;
            for(int y=0;y<h;++y){unsigned sum{};for(int x=0;x<=radius&&x<w;++x)sum+=mask[size_t(y)*w+x];
                for(int x=0;x<w;++x){scratch[size_t(y)*w+x]=uint8_t((sum+divisor/2)/divisor);
                    if(x-radius>=0)sum-=mask[size_t(y)*w+x-radius];if(x+radius+1<w)sum+=mask[size_t(y)*w+x+radius+1];}}
            for(int x=0;x<w;++x){unsigned sum{};for(int y=0;y<=radius&&y<h;++y)sum+=scratch[size_t(y)*w+x];
                for(int y=0;y<h;++y){mask[size_t(y)*w+x]=uint8_t((sum+divisor/2)/divisor);
                    if(y-radius>=0)sum-=scratch[size_t(y-radius)*w+x];if(y+radius+1<h)sum+=scratch[size_t(y+radius+1)*w+x];}}
        }
        for(size_t n=0;n<count;++n){auto c=e.shadow_color;c.a=uint8_t((unsigned(c.a)*mask[n]+127)/255);out.frames[0].pixels[n]=over(out.frames[0].pixels[n],c);}
    }
    return out;
}
Image draw_pen(const Image &source,std::span<const FloatPoint> points,PenStyle style,const SelectionRegion &selection,const EditMemory &memory){
    if(points.empty()||points.size()>65536||!std::isfinite(style.width)||style.width<1||style.width>512||source.frames.empty())
        throw std::invalid_argument("Invalid pen stroke");
    double l=source.width,t=source.height,r=0,b=0;
    for(auto p:points){if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>1e8||std::abs(p.y)>1e8)throw std::invalid_argument("Invalid stroke point");
        l=std::min(l,p.x);t=std::min(t,p.y);r=std::max(r,p.x);b=std::max(b,p.y);}
    const bool effects=style.effects.outline||style.effects.shadow;
    const double radius=style.width/2,margin=radius+1+(effects?annotation_padding(style.effects):0);
    const int x0=int(std::floor(std::clamp(l-margin,0.,double(source.width)))),y0=int(std::floor(std::clamp(t-margin,0.,double(source.height))));
    const int x1=int(std::ceil(std::clamp(r+margin,0.,double(source.width)))),y1=int(std::ceil(std::clamp(b+margin,0.,double(source.height))));
    const int width=std::max(0,x1-x0),height=std::max(0,y1-y0);
    const auto count=memory.pixels(source.width,source.height);if(memory.before_allocate)memory.before_allocate(count*sizeof(Pixel));
    auto out=source;out.frames.resize(1);out.frames[0].indexed.reset();out.indexed_source.reset();out.rgb16.reset();
    if(!width||!height)return out;
    const auto mask_size=size_t(width)*height;if(memory.before_allocate)memory.before_allocate(mask_size);
    std::vector<uint8_t> coverage(mask_size);
    for(size_t n=0;n<std::max(size_t(1),points.size()-1);++n){
        const auto a=points[n],z=points[std::min(n+1,points.size()-1)];
        const int left=int(std::floor(std::clamp(std::min(a.x,z.x)-margin,double(x0),double(x1))));
        const int right=int(std::ceil(std::clamp(std::max(a.x,z.x)+margin,double(x0),double(x1))));
        const int top=int(std::floor(std::clamp(std::min(a.y,z.y)-margin,double(y0),double(y1))));
        const int bottom=int(std::ceil(std::clamp(std::max(a.y,z.y)+margin,double(y0),double(y1))));
        for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
            if(!effects&&!selection.empty()&&!selection.contains(x,y))continue;
            const auto alpha=byte(std::clamp(radius+.5-distance({x+.5,y+.5},a,z),0.,1.)*style.color.a);
            auto &value=coverage[size_t(y-y0)*width+x-x0];value=std::max(value,alpha);
        }
    }
    if(effects){
        auto stamp=new_image(width,height,memory);for(size_t n=0;n<mask_size;++n){auto c=style.color;c.a=coverage[n];stamp.frames[0].pixels[n]=c;}
        const auto decorated=decorate_annotation(stamp,style.effects,memory);const int pad=annotation_padding(style.effects);
        for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){if(!selection.empty()&&!selection.contains(x,y))continue;
            const auto c=decorated.frames[0].pixels[size_t(y-y0+pad)*decorated.width+x-x0+pad];if(c.a){auto &p=out.frames[0].pixels[size_t(y)*source.width+x];p=over(c,p);}}
        return out;
    }
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){auto color=style.color;color.a=coverage[size_t(y-y0)*width+x-x0];
        if(color.a){auto &p=out.frames[0].pixels[size_t(y)*source.width+x];p=over(color,p);}}
    return out;
}
Image make_text_stamp(const std::wstring &text,const std::wstring &family,double pixels,Pixel color,const EditMemory &memory,const AnnotationEffects &effects){
    if(text.empty()||text.size()>4096||family.size()>128||!std::isfinite(pixels)||pixels<1||pixels>512||
       std::all_of(text.begin(),text.end(),[](wchar_t c){return std::iswspace(c)!=0;}))throw std::invalid_argument("文字を入力してください。");
    TextDc drawing;if(!drawing.dc)throw std::runtime_error("Cannot create text surface");
    constexpr int scale=4,padding=2;
    drawing.font=CreateFontW(-int(std::lround(pixels*scale)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,family.c_str());
    if(!drawing.font)throw std::runtime_error("Cannot create text font");
    drawing.previous_font=SelectObject(drawing.dc,drawing.font);
    RECT bounds{};const UINT flags=DT_LEFT|DT_TOP|DT_NOPREFIX|DT_EXPANDTABS;
    if(!DrawTextW(drawing.dc,text.c_str(),int(text.size()),&bounds,flags|DT_CALCRECT))throw std::runtime_error("Cannot measure text");
    const int w=(bounds.right+scale-1)/scale+padding*2,h=(bounds.bottom+scale-1)/scale+padding*2;
    if(w>8192||h>8192||uint64_t(w)*h>16*1024*1024)throw std::invalid_argument("文字の大きさまたは量を小さくしてください。");
    const int sw=w*scale,sh=h*scale;memory.pixels(sw,sh);
    if(memory.before_allocate)memory.before_allocate(size_t(sw)*sh*4);
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),sw,-sh,1,32,BI_RGB};void *bits{};
    drawing.bitmap=CreateDIBSection(drawing.dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!drawing.bitmap||!bits)throw std::bad_alloc();
    drawing.previous_bitmap=SelectObject(drawing.dc,drawing.bitmap);std::fill_n(static_cast<uint32_t *>(bits),size_t(sw)*sh,0u);
    SetBkMode(drawing.dc,TRANSPARENT);SetTextColor(drawing.dc,RGB(255,255,255));
    RECT area{padding*scale,padding*scale,sw,sh};
    DrawTextW(drawing.dc,text.c_str(),int(text.size()),&area,flags);GdiFlush();
    auto out=new_image(w,h,memory);const auto mask=static_cast<const uint32_t *>(bits);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){unsigned sum{};
        for(int j=0;j<scale;++j)for(int i=0;i<scale;++i)sum+=mask[size_t(y*scale+j)*sw+x*scale+i]&255;
        auto p=color;p.a=uint8_t((sum*color.a+2040)/(16*255));out.frames[0].pixels[size_t(y)*w+x]=p;}
    return effects.outline||effects.shadow?decorate_annotation(out,effects,memory):std::move(out);
}
Image crop_oriented(const Image &source,const FloatingBox &box,double output_scale,const EditMemory &memory){
    if(!box.valid()||!std::isfinite(output_scale)||output_scale<.01||output_scale>100||source.frames.empty())
        throw std::invalid_argument("Invalid crop bounds");
    const auto w=uint32_t(std::max(1.,std::round(box.width*output_scale))),h=uint32_t(std::max(1.,std::round(box.height*output_scale)));
    auto out=new_image(w,h,memory);out.metadata=source.metadata;out.path=source.path;
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const auto p=box.point(((x+.5)/w-.5)*box.width,((y+.5)/h-.5)*box.height);
        out.frames[0].pixels[size_t(y)*w+x]=sample(source,p.x-.5,p.y-.5);}
    return out;
}
FloatingBox resize_box_at_anchor(FloatingBox box,double width,double height,double angle,int anchor){
    if(anchor<0||anchor>8)throw std::invalid_argument("Invalid anchor");
    const double x=(anchor%3-1)/2.,y=(anchor/3-1)/2.;const auto fixed=box.point(x*box.width,y*box.height);
    box.width=width;box.height=height;box.angle=angle;const auto after=box.point(x*width,y*height);
    box.cx+=fixed.x-after.x;box.cy+=fixed.y-after.y;
    if(!box.valid())throw std::invalid_argument("Invalid edit bounds");return box;
}
} // namespace gvn
