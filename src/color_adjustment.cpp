#include "gvn/color_adjustment.h"
#include <cmath>
namespace gvn {
namespace {
struct Rgb {double r{},g{},b{};};
struct Hls {double h{},l{},s{};};
uint8_t byte(double v){return uint8_t(std::clamp(std::floor(v+.5),0.,255.));}
double wrap(double h){h=std::fmod(h,1.);return h<0?h+1:h;}
Hls to_hls(Rgb value) {
    const double high=std::max({value.r,value.g,value.b}),low=std::min({value.r,value.g,value.b});
    Hls result;result.l=(high+low)*.5;if(high==low)return result;
    const auto delta=high-low;result.s=result.l<=.5?delta/(high+low):delta/(2.-high-low);
    const auto r=(high-value.r)/delta,g=(high-value.g)/delta,b=(high-value.b)/delta;
    result.h=wrap((value.r==high?b-g:value.g==high?2.+r-b:4.+g-r)/6.);return result;
}
double hue(double low,double high,double h) {
    h=wrap(h);if(h<1./6.)return low+(high-low)*h*6.;if(h<.5)return high;
    if(h<2./3.)return low+(high-low)*(2./3.-h)*6.;return low;
}
Rgb from_hls(Hls value) {
    if(!value.s)return {value.l,value.l,value.l};
    const auto high=value.l<=.5?value.l*(1.+value.s):value.l+value.s-value.l*value.s;
    const auto low=2.*value.l-high;return {hue(low,high,value.h+1./3.),hue(low,high,value.h),hue(low,high,value.h-1./3.)};
}
uint8_t gray(Rgb value,GrayMethod method) {
    const double level=method==GrayMethod::Middle?(std::max({value.r,value.g,value.b})+std::min({value.r,value.g,value.b}))*.5:
        method==GrayMethod::Average?(value.r+value.g+value.b)/3.:value.r*.299+value.g*.587+value.b*.114;
    return byte(level);
}
double brightness(double value,double amount) {return amount>=0?value+(1.-value)*amount:value*(1.+amount);}
Pixel apply(Pixel pixel,const ColorAdjustment &choice) {
    Rgb rgb;
    if(choice.unified) {
        // Tint uses the selected integer gray level, then changes its lightness
        // before creating the new hue/saturation. It is not a final RGB fade.
        const double light=brightness(gray({double(pixel.r),double(pixel.g),double(pixel.b)},choice.gray)/255.,choice.lightness/100.);
        rgb=from_hls({choice.hue/360.,light,choice.saturation/100.});
    }else {
        auto hls=to_hls({pixel.r/255.,pixel.g/255.,pixel.b/255.});hls.h=wrap(hls.h+choice.hue/360.);
        if(choice.saturation>0&&hls.s>0)hls.s=choice.saturation==100?1.:std::min(1.,hls.s/(1.-choice.saturation/100.));
        rgb=from_hls(hls);
        if(choice.saturation<0) {
            const double level=gray({rgb.r*255.,rgb.g*255.,rgb.b*255.},choice.gray)/255.;const double amount=-choice.saturation/100.;
            rgb.r+=(level-rgb.r)*amount;rgb.g+=(level-rgb.g)*amount;rgb.b+=(level-rgb.b)*amount;
        }
        const auto amount=choice.lightness/100.;rgb.r=brightness(rgb.r,amount);rgb.g=brightness(rgb.g,amount);rgb.b=brightness(rgb.b,amount);
    }
    return {byte(rgb.r*255.),byte(rgb.g*255.),byte(rgb.b*255.),pixel.a};
}
}
bool valid_color_adjustment(const ColorAdjustment &choice) {
    return choice.hue>=(choice.unified?0:-180)&&choice.hue<=(choice.unified?360:180)&&
        choice.saturation>=(choice.unified?0:-100)&&choice.saturation<=100&&choice.lightness>=-100&&choice.lightness<=100&&
        (choice.gray==GrayMethod::Middle||choice.gray==GrayMethod::Average||choice.gray==GrayMethod::Luma);
}
std::optional<Image> color_adjustment(const Image &image,const ColorAdjustment &choice,Rect selection,const EditMemory &memory) {
    if(!valid_color_adjustment(choice)||image.frames.size()!=1||image.frames[0].pixels.size()!=checked_image_pixels(image.width,image.height))
        throw std::invalid_argument("Invalid color adjustment or still image");
    if(choice.neutral())return {};
    const Rect entire{0,0,int(image.width),int(image.height)};selection=selection.empty()?entire:intersect(selection,entire);
    if(selection.empty())return {};
    const auto &source=image.frames[0];const auto &indexed=source.indexed;
    if(selection.x==0&&selection.y==0&&selection.w==int(image.width)&&selection.h==int(image.height)&&indexed&&
       indexed->width==image.width&&indexed->height==image.height&&indexed->matches(source.pixels)) {
        auto colors=indexed->palette;for(auto &color:colors)color=apply(color,choice);return edit_palette(image,colors,memory);
    }
    Image out;out.width=image.width;out.height=image.height;out.path=image.path;out.format=image.format;
    out.file_bytes=image.file_bytes;out.load_ms=image.load_ms;out.metadata=image.metadata;
    out.frames.push_back({memory.allocate(out.width,out.height),source.duration_ms});auto &pixels=out.frames[0].pixels;
    std::copy(source.pixels.begin(),source.pixels.end(),pixels.begin());
    for(int y=selection.y;y<selection.y+selection.h;++y)for(int x=selection.x;x<selection.x+selection.w;++x) {
        const auto at=size_t(y)*image.width+x;pixels[at]=apply(source.pixels[at],choice);
    }
    return out;
}
} // namespace gvn
