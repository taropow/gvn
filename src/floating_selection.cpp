#include "gvn/floating_selection.h"
#include <cmath>
#include <numbers>
namespace gvn {
namespace {
double radians(double a){return a*std::numbers::pi/180;}
constexpr std::array<FloatPoint,8> directions{{{-1,-1},{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0}}};
Pixel sample(const Image &s,double x,double y) {
    const int ix=int(std::floor(x)),iy=int(std::floor(y));const double dx=x-ix,dy=y-iy;
    double a{},r{},g{},b{};
    for(int j=0;j<2;++j)for(int i=0;i<2;++i){
        const auto p=s.frames[0].pixels[size_t(std::clamp(iy+j,0,int(s.height)-1))*s.width+std::clamp(ix+i,0,int(s.width)-1)];
        const double weight=(i?dx:1-dx)*(j?dy:1-dy),alpha=weight*p.a;
        a+=alpha;r+=alpha*p.r;g+=alpha*p.g;b+=alpha*p.b;
    }
    if(a<=0)return {};
    auto byte=[](double v){return uint8_t(std::clamp(std::lround(v),0l,255l));};
    return {byte(r/a),byte(g/a),byte(b/a),byte(a)};
}
}
bool FloatingBox::valid() const {
    return std::isfinite(cx)&&std::isfinite(cy)&&std::isfinite(width)&&std::isfinite(height)&&std::isfinite(angle)&&
           std::abs(cx)<=4294967296.&&std::abs(cy)<=4294967296.&&width>=1&&height>=1&&width<=65535&&height<=65535;
}
FloatPoint FloatingBox::point(double x,double y) const {
    const auto c=std::cos(radians(angle)),s=std::sin(radians(angle));return {cx+c*x-s*y,cy+s*x+c*y};
}
FloatPoint FloatingBox::local(FloatPoint p) const {
    const auto c=std::cos(radians(angle)),s=std::sin(radians(angle));p.x-=cx;p.y-=cy;return {c*p.x+s*p.y,-s*p.x+c*p.y};
}
std::array<FloatPoint,8> FloatingBox::handles() const {
    std::array<FloatPoint,8> p{};for(size_t i=0;i<8;++i)p[i]=point(directions[i].x*width/2,directions[i].y*height/2);return p;
}
int floating_hit(const FloatingBox &box,FloatPoint p,double tolerance) {
    const auto handles=box.handles();
    for(int i=0;i<8;++i)if(std::hypot(p.x-handles[i].x,p.y-handles[i].y)<=tolerance)return i;
    const auto local=box.local(p);
    return std::abs(local.x)<=box.width/2&&std::abs(local.y)<=box.height/2?FloatMove:FloatRotate;
}
FloatingBox FloatingDrag::moved(FloatPoint p,bool constrain) const {
    auto box=initial;if(hit==FloatNone)return box;
    if(hit==FloatMove){box.cx+=p.x-start.x;box.cy+=p.y-start.y;}
    else if(hit==FloatRotate){
        if(std::hypot(p.x-box.cx,p.y-box.cy)<1e-9)return box;
        const auto a=std::atan2(start.y-box.cy,start.x-box.cx),b=std::atan2(p.y-box.cy,p.x-box.cx);
        box.angle=std::remainder(initial.angle+(b-a)*180/std::numbers::pi,360.);
        if(constrain)box.angle=std::round(box.angle/15)*15;
    }else if(hit>=0&&hit<8){
        const auto from=initial.local(start),to=initial.local(p),d=directions[hit];
        auto width=std::clamp(initial.width+d.x*(to.x-from.x),1.,65535.);
        auto height=std::clamp(initial.height+d.y*(to.y-from.y),1.,65535.);
        if(constrain){
            const auto sx=width/initial.width,sy=height/initial.height;
            auto scale=d.x==0?sy:d.y==0?sx:std::abs(sx-1)>std::abs(sy-1)?sx:sy;
            scale=std::clamp(scale,std::max(1/initial.width,1/initial.height),std::min(65535/initial.width,65535/initial.height));
            width=initial.width*scale;height=initial.height*scale;
        }
        const auto center=initial.point(d.x*(width-initial.width)/2,d.y*(height-initial.height)/2);
        box.cx=center.x;box.cy=center.y;box.width=width;box.height=height;
    }
    return box.valid()?box:initial;
}
Image paste_floating(const Image &base,const Image &source,const FloatingBox &box,const EditMemory &memory) {
    if(!box.valid()||base.frames.empty()||source.frames.empty())throw std::invalid_argument("Invalid floating selection");
    if(base.frames[0].pixels.size()!=checked_image_pixels(base.width,base.height)||source.frames[0].pixels.size()!=checked_image_pixels(source.width,source.height))
        throw std::invalid_argument("Invalid floating selection pixels");
    const auto bytes=memory.pixels(base.width,base.height);if(memory.before_allocate)memory.before_allocate(bytes*sizeof(Pixel));
    auto out=base;out.frames.resize(1);out.frames[0].indexed.reset();out.indexed_source.reset();out.rgb16.reset();
    const auto corners=box.handles();double left=double(base.width),right{},top=double(base.height),bottom{};
    for(int i:{0,2,4,6}){left=std::min(left,corners[i].x);right=std::max(right,corners[i].x);top=std::min(top,corners[i].y);bottom=std::max(bottom,corners[i].y);}
    const int x0=int(std::floor(std::clamp(left,0.,double(base.width)))),x1=int(std::ceil(std::clamp(right,0.,double(base.width))));
    const int y0=int(std::floor(std::clamp(top,0.,double(base.height)))),y1=int(std::ceil(std::clamp(bottom,0.,double(base.height))));
    for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
        const auto local=box.local({x+.5,y+.5});
        if(local.x< -box.width/2||local.x>=box.width/2||local.y< -box.height/2||local.y>=box.height/2)continue;
        const auto pixel=sample(source,(local.x/box.width+.5)*source.width-.5,(local.y/box.height+.5)*source.height-.5);
        auto &destination=out.frames[0].pixels[size_t(y)*base.width+x];destination=over(pixel,destination);
    }
    return out;
}
} // namespace gvn
