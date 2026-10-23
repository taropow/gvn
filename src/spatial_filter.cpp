#include "gvn/spatial_filter.h"
#include <cmath>
namespace gvn {
namespace {
uint8_t byte(double v){return uint8_t(std::clamp(std::lround(v),0l,255l));}
}
bool valid_spatial_filter(const SpatialFilter &v) {
    return v.radius>=1&&v.radius<=100&&v.strength>=1&&v.strength<=400&&v.threshold>=0&&v.threshold<=255&&v.transparency>=0&&v.transparency<=99;
}
SpatialFilter filter_preset(FilterPreset preset) {
    switch(preset) {
    case FilterPreset::Sharpen:return {true,6,100,0,0};
    case FilterPreset::SharpenStrong:return {true,8,200,0,0};
    case FilterPreset::SharpenEdges:return {true,6,150,15,0};
    case FilterPreset::Blur:return {false,4,100,0,0};
    case FilterPreset::BlurStrong:return {false,8,100,0,0};
    case FilterPreset::BlurSoft:return {false,50,100,0,65};
    }
    throw std::invalid_argument("Unknown spatial preset");
}
Image spatial_filter(const Image &image,const SpatialFilter &choice,Rect rect,size_t frame,const EditMemory &memory) {
    if(!valid_spatial_filter(choice)||frame>=image.frames.size()||image.frames[frame].pixels.size()!=checked_image_pixels(image.width,image.height))
        throw std::invalid_argument("Invalid spatial filter input");
    Image out;out.width=image.width;out.height=image.height;out.path=image.path;out.format=L"RGBA";out.metadata=image.metadata;
    out.frames.push_back({memory.allocate(out.width,out.height),100});const auto &src=image.frames[frame].pixels;out.frames[0].pixels=src;
    const Rect bounds{0,0,int(image.width),int(image.height)};if(rect.empty())rect=bounds;else rect=intersect(rect,bounds);if(rect.empty())return out;
    // Inferred from actual reference Gaussian outputs at 0.4,0.6,0.8,1,2,5.
    // This models the reference radius; it is not a claim about its internals.
    const double sigma=choice.radius/10.+.17;const int radius=int(std::ceil(sigma*3));std::vector<double> kernel(size_t(radius)*2+1);double sum{};
    for(int n=-radius;n<=radius;++n){const auto weight=std::exp(-double(n*n)/(2*sigma*sigma));kernel[size_t(n+radius)]=weight;sum+=weight;}for(auto &weight:kernel)weight/=sum;
    const int top=std::max(0,rect.y-radius),bottom=std::min(int(image.height),rect.y+rect.h+radius);auto horizontal=memory.allocate(rect.w,bottom-top);
    for(int y=top;y<bottom;++y)for(int x=rect.x;x<rect.x+rect.w;++x) {
        double r{},g{},b{},a{};for(int n=-radius;n<=radius;++n){const auto p=src[size_t(y)*image.width+std::clamp(x+n,0,int(image.width)-1)];const auto k=kernel[size_t(n+radius)]*p.a;r+=p.r*k;g+=p.g*k;b+=p.b*k;a+=k;}
        horizontal[size_t(y-top)*rect.w+x-rect.x]=a?Pixel{byte(r/a),byte(g/a),byte(b/a),byte(a)}:Pixel{};
    }
    const double mix=1.-choice.transparency/100.;
    for(int y=rect.y;y<rect.y+rect.h;++y)for(int x=rect.x;x<rect.x+rect.w;++x) {
        double r{},g{},b{},a{};for(int n=-radius;n<=radius;++n){const auto p=horizontal[size_t(std::clamp(y+n,0,int(image.height)-1)-top)*rect.w+x-rect.x];const auto k=kernel[size_t(n+radius)]*p.a;r+=p.r*k;g+=p.g*k;b+=p.b*k;a+=k;}
        const Pixel soft=a?Pixel{byte(r/a),byte(g/a),byte(b/a),byte(a)}:Pixel{};const auto at=size_t(y)*image.width+x;const auto p=src[at];
        if(choice.sharpen) {
            // Native threshold is a soft threshold AFTER scaling the difference.
            const auto sharpen=[&](uint8_t value,uint8_t blurred){const double delta=(int(value)-blurred)*choice.strength/100.;return byte(value+std::copysign(std::max(0.,std::abs(delta)-choice.threshold),delta));};
            out.frames[0].pixels[at]={sharpen(p.r,soft.r),sharpen(p.g,soft.g),sharpen(p.b,soft.b),p.a};
        } else {
            const double alpha=soft.a*mix+p.a*(1-mix);out.frames[0].pixels[at]=alpha?Pixel{byte((soft.r*soft.a*mix+p.r*p.a*(1-mix))/alpha),byte((soft.g*soft.a*mix+p.g*p.a*(1-mix))/alpha),byte((soft.b*soft.a*mix+p.b*p.a*(1-mix))/alpha),byte(alpha)}:Pixel{};
        }
    }
    return out;
}
} // namespace gvn
