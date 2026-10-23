#include "gvn/brightness.h"
#include <cmath>
namespace gvn {
bool valid_brightness(const BrightnessAdjustment &value) {
    return value.brightness>=-255&&value.brightness<=255&&value.contrast>=-255&&value.contrast<=255&&value.gamma>=400&&value.gamma<=2500;
}
std::array<uint8_t,256> brightness_table(const BrightnessAdjustment &value) {
    if(!valid_brightness(value))throw std::invalid_argument("Invalid brightness parameters");
    std::array<uint8_t,256> table;
    for(int n=0;n<256;++n) {
        double color=std::clamp(n+value.brightness,0,255);
        // Observed order: clamp brightness, contrast about127.5 with a
        // truncated signed increment, clamp, then reciprocal gamma.
        if(value.contrast==255)color=color<128?0:255;
        else color=std::clamp(color+std::trunc((color-127.5)*(2.*value.contrast/(255.-value.contrast))),0.,255.);
        if(value.gamma!=1000)color=255.*std::pow(color/255.,1000./value.gamma);
        table[n]=uint8_t(std::clamp(std::lround(color),0l,255l));
    }
    return table;
}
std::optional<Image> adjust_brightness(const Image &image,const BrightnessAdjustment &choice,Rect selection,const EditMemory &memory) {
    if(!valid_brightness(choice)||image.frames.size()!=1||image.frames[0].pixels.size()!=checked_image_pixels(image.width,image.height))throw std::invalid_argument("Invalid brightness source or parameters");
    if(choice.neutral())return {};const Rect entire{0,0,int(image.width),int(image.height)};selection=selection.empty()?entire:intersect(selection,entire);if(selection.empty())return {};
    const auto table=brightness_table(choice);const auto apply=[&](Pixel p){return Pixel{table[p.r],table[p.g],table[p.b],p.a};};const auto &frame=image.frames[0];const auto &indexed=frame.indexed;
    if(selection.x==0&&selection.y==0&&selection.w==int(image.width)&&selection.h==int(image.height)&&indexed&&indexed->width==image.width&&indexed->height==image.height&&indexed->matches(frame.pixels)) {
        auto palette=indexed->palette;for(auto &pixel:palette)pixel=apply(pixel);return edit_palette(image,palette,memory);
    }
    Image out;out.width=image.width;out.height=image.height;out.path=image.path;out.format=image.format;out.file_bytes=image.file_bytes;out.load_ms=image.load_ms;out.metadata=image.metadata;
    out.frames.push_back({memory.allocate(out.width,out.height),frame.duration_ms});auto &pixels=out.frames[0].pixels;std::copy(frame.pixels.begin(),frame.pixels.end(),pixels.begin());
    for(int y=selection.y;y<selection.y+selection.h;++y)for(int x=selection.x;x<selection.x+selection.w;++x){const auto at=size_t(y)*image.width+x;pixels[at]=apply(frame.pixels[at]);}
    return out;
}
} // namespace gvn
