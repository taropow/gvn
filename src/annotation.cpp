#include "gvn/annotation.h"
#include <windows.h>
#include <cmath>
#include <cwctype>
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
Image draw_pen(const Image &source,std::span<const FloatPoint> points,PenStyle style,const SelectionRegion &selection,const EditMemory &memory){
    if(points.empty()||points.size()>65536||!std::isfinite(style.width)||style.width<1||style.width>512||source.frames.empty())
        throw std::invalid_argument("Invalid pen stroke");
    double l=source.width,t=source.height,r=0,b=0;
    for(auto p:points){if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>1e8||std::abs(p.y)>1e8)throw std::invalid_argument("Invalid stroke point");
        l=std::min(l,p.x);t=std::min(t,p.y);r=std::max(r,p.x);b=std::max(b,p.y);}
    const double radius=style.width/2,margin=radius+1;
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
            if(!selection.empty()&&!selection.contains(x,y))continue;
            const auto alpha=byte(std::clamp(radius+.5-distance({x+.5,y+.5},a,z),0.,1.)*style.color.a);
            auto &value=coverage[size_t(y-y0)*width+x-x0];value=std::max(value,alpha);
        }
    }
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){auto color=style.color;color.a=coverage[size_t(y-y0)*width+x-x0];
        if(color.a){auto &p=out.frames[0].pixels[size_t(y)*source.width+x];p=over(color,p);}}
    return out;
}
Image make_text_stamp(const std::wstring &text,const std::wstring &family,double pixels,Pixel color,const EditMemory &memory){
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
    return out;
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
