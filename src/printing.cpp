#include "gvn/printing.h"
#include <cmath>
#include <limits>

namespace gvn {
namespace {
bool positive(double n){return std::isfinite(n)&&n>0;}
bool margin(double n){return std::isfinite(n)&&n>=0&&n<=1000000;}
int coordinate(double n) {
    if(!std::isfinite(n)||n<INT_MIN+1.||n>INT_MAX-1.)throw std::invalid_argument("Printer coordinate exceeds GDI range");
    return static_cast<int>(std::lround(n));
}
}
PrintLayout print_layout(uint32_t w,uint32_t h,const ImageMetadata &m,const PrintPaper &p,const PrintOptions &o) {
    if(!w||!h||!positive(p.width)||!positive(p.height)||p.width>1000000||p.height>1000000||
       !margin(p.left)||!margin(p.top)||!margin(p.right)||!margin(p.bottom)||
       !margin(o.left)||!margin(o.top)||!margin(o.right)||!margin(o.bottom)||
       !positive(o.width)||!positive(o.height)||o.width>1000000||o.height>1000000||
       o.position<0||o.position>8||o.unit<0||o.unit>1||o.density< -5||o.density>5||o.copies<1||o.copies>9999||
       int(o.sizing)<0||int(o.sizing)>4||int(o.rotation)<0||int(o.rotation)>2)
        throw std::invalid_argument("Invalid print settings");
    PrintLayout out;
    out.area={std::max(o.left,p.left),std::max(o.top,p.top),0,0};
    out.area.width=p.width-out.area.x-std::max(o.right,p.right);
    out.area.height=p.height-out.area.y-std::max(o.bottom,p.bottom);
    if(out.area.width<=0||out.area.height<=0)throw std::invalid_argument("Margins leave no printable area");
    double width=w,height=h,dx=m.dpi_x,dy=m.dpi_y;
    if(o.rotation!=PrintRotation::None&&((width>height&&out.area.width<out.area.height)||(width<height&&out.area.width>out.area.height))) {
        out.quarter_turns=o.rotation==PrintRotation::Left?3:1;
        std::swap(width,height);std::swap(dx,dy);
    }
    switch(o.sizing) {
    case PrintSizing::Contain:case PrintSizing::Cover: {
        const double x=out.area.width/width,y=out.area.height/height;
        const double scale=o.sizing==PrintSizing::Contain?std::min(x,y):std::max(x,y);
        width*=scale;height*=scale;break;
    }
    case PrintSizing::Stretch:width=out.area.width;height=out.area.height;break;
    case PrintSizing::ImageDpi:
        if(!positive(dx)&&positive(dy))dx=dy;
        if(!positive(dy)&&positive(dx))dy=dx;
        if(!positive(dx)||!positive(dy))dx=dy=72;
        width=width*25.4/dx;height=height*25.4/dy;break;
    case PrintSizing::Specified:width=o.width;height=o.height;break;
    }
    out.image={out.area.x+(out.area.width-width)*(o.position%3)/2,
        out.area.y+(out.area.height-height)*(o.position/3)/2,width,height};
    return out;
}
PrintColors::PrintColors(int density) {
    const auto level=std::clamp(density,-5,5);
    // Reference output uses an inverse gamma rounded to integer thousandths.
    const auto inverse=level>0?std::round(20000./(20+level)):1000.-level*50;
    const auto gamma=1000./inverse;
    for(int n=0;n<256;++n)channels[n]=density?static_cast<uint8_t>(std::clamp(std::lround(255*std::pow(n/255.,gamma)),0l,255l)):uint8_t(n);
}
Pixel PrintColors::operator()(Pixel pixel) const {
    pixel=over(pixel,{255,255,255,255});
    pixel.r=channels[pixel.r];pixel.g=channels[pixel.g];pixel.b=channels[pixel.b];
    std::swap(pixel.r,pixel.b);return pixel;
}
Pixel print_pixel(Pixel pixel,int density){return PrintColors(density)(pixel);}
bool draw_print_page(HDC dc,const Image &image,size_t frame,const PrintLayout &layout,double sx,double sy,double ox,double oy,int density) {
    if(!dc||!positive(sx)||!positive(sy)||frame>=image.frames.size())throw std::invalid_argument("Invalid print destination");
    const bool rotated=layout.quarter_turns%2;
    const uint32_t width=rotated?image.height:image.width,height=rotated?image.width:image.height;
    const auto &source=image.frames.at(frame).pixels;
    std::vector<Pixel> pixels(checked_image_pixels(width,height));
    const PrintColors colors(density);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x) {
        const size_t from=layout.quarter_turns==1?size_t(image.height-1-x)*image.width+y:
            layout.quarter_turns==3?size_t(x)*image.width+image.width-1-y:size_t(y)*image.width+x;
        pixels[size_t(y)*width+x]=colors(source.at(from));
    }
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),LONG(width),-LONG(height),1,32,BI_RGB};
    const auto &a=layout.area;const auto &b=layout.image;
    const int saved=SaveDC(dc);if(!saved)return false;
    struct Restore {HDC dc;int saved;~Restore(){RestoreDC(dc,saved);}} restore{dc,saved};
    IntersectClipRect(dc,coordinate(a.x*sx-ox),coordinate(a.y*sy-oy),coordinate((a.x+a.width)*sx-ox),coordinate((a.y+a.height)*sy-oy));
    SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
    const int lines=StretchDIBits(dc,coordinate(b.x*sx-ox),coordinate(b.y*sy-oy),std::max(1,coordinate(b.width*sx)),std::max(1,coordinate(b.height*sy)),
        0,0,width,height,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    return lines!=GDI_ERROR&&lines!=0;
}
}
