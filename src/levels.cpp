#include "gvn/levels.h"
#include <cmath>
namespace gvn {
bool valid_level_curve(const LevelCurve &v){return v.black>=0&&v.white<=255&&v.black<v.white&&v.gamma>=1&&v.gamma<=10000&&v.output_black>=0&&v.output_black<=255&&v.output_white>=0&&v.output_white<=255;}
bool valid_levels(const LevelsAdjustment &v){return std::all_of(v.curves.begin(),v.curves.end(),valid_level_curve);}
bool LevelsAdjustment::neutral() const {
    return individual?std::all_of(curves.begin()+1,curves.end(),[](const auto &v){return v.neutral();}):curves[0].neutral();
}
std::array<uint8_t,256> level_table(const LevelCurve &v){
    if(!valid_level_curve(v))throw std::invalid_argument("Invalid level curve");
    std::array<uint8_t,256> table;
    for(int n=0;n<256;++n){const double unit=std::clamp(double(n-v.black)/(v.white-v.black),0.,1.);table[n]=uint8_t(std::clamp(std::lround(v.output_black+(v.output_white-v.output_black)*std::pow(unit,1000./v.gamma)),0l,255l));}
    return table;
}
namespace {
Rect region(const Image &image,Rect rect){
    if(image.frames.size()!=1||image.frames[0].pixels.size()!=checked_image_pixels(image.width,image.height))throw std::invalid_argument("Invalid levels source");
    const Rect entire{0,0,int(image.width),int(image.height)};return rect.empty()?entire:intersect(rect,entire);
}
}
LevelsHistogram levels_histogram(const Image &image,Rect rect){
    rect=region(image,rect);LevelsHistogram result{};
    for(int y=rect.y;y<rect.y+rect.h;++y)for(int x=rect.x;x<rect.x+rect.w;++x){
        const auto p=image.frames[0].pixels[size_t(y)*image.width+x];if(!p.a)continue;
        const uint8_t values[]{p.r,p.g,p.b};for(int n=0;n<3;++n){++result[n+1][values[n]];++result[0][values[n]];}
    }
    return result;
}
LevelsAdjustment automatic_levels(LevelsAdjustment choice,const LevelsHistogram &histogram){
    if(!valid_levels(choice))throw std::invalid_argument("Invalid auto levels source");
    const int first=choice.individual?1:0,last=choice.individual?4:1;
    for(int n=first;n<last;++n){auto &curve=choice.curves[n];curve={};const auto &bins=histogram[n];int low=0,high=255;while(low<256&&!bins[low])++low;while(high>=0&&!bins[high])--high;if(low<high){curve.black=low;curve.white=high;}}
    return choice;
}
std::optional<Image> adjust_levels(const Image &image,const LevelsAdjustment &choice,Rect rect,const EditMemory &memory){
    rect=region(image,rect);if(!valid_levels(choice))throw std::invalid_argument("Invalid levels parameters");if(choice.neutral()||rect.empty())return {};
    std::array<std::array<uint8_t,256>,3> tables;for(int n=0;n<3;++n)tables[n]=level_table(choice.curves[choice.individual?n+1:0]);
    const auto transform=[&](Pixel p){return Pixel{tables[0][p.r],tables[1][p.g],tables[2][p.b],p.a};};const auto &frame=image.frames[0];const auto &indexed=frame.indexed;
    if(rect.x==0&&rect.y==0&&rect.w==int(image.width)&&rect.h==int(image.height)&&indexed&&indexed->width==image.width&&indexed->height==image.height&&indexed->matches(frame.pixels)){
        auto palette=indexed->palette;for(auto &p:palette)p=transform(p);return edit_palette(image,palette,memory);
    }
    Image out;out.width=image.width;out.height=image.height;out.path=image.path;out.format=image.format;out.file_bytes=image.file_bytes;out.load_ms=image.load_ms;out.metadata=image.metadata;out.frames.push_back({memory.allocate(image.width,image.height),frame.duration_ms});auto &pixels=out.frames[0].pixels;std::copy(frame.pixels.begin(),frame.pixels.end(),pixels.begin());
    for(int y=rect.y;y<rect.y+rect.h;++y)for(int x=rect.x;x<rect.x+rect.w;++x){const auto at=size_t(y)*image.width+x;pixels[at]=transform(frame.pixels[at]);}
    return out;
}
} // namespace gvn
