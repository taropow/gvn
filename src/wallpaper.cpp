#include "gvn/wallpaper.h"
#include "gvn/saving.h"
#include "gvn/edit.h"

namespace gvn {
WallpaperStyle wallpaper_style(WallpaperMode mode) {
    switch (mode) {
    case WallpaperMode::Center: return {L"0",L"0"};
    case WallpaperMode::Tile: return {L"1",L"0"};
    case WallpaperMode::Stretch: return {L"0",L"2"};
    case WallpaperMode::Width: return {L"0",L"10"};
    case WallpaperMode::Height: return {L"0",L"6"};
    default: throw std::invalid_argument("Invalid wallpaper placement");
    }
}
std::vector<uint8_t> encode_wallpaper_bmp(const Image &image,size_t frame,uint32_t matte) {
    if (matte>0xffffff || frame>=image.frames.size() ||
        image.frames[frame].pixels.size()!=checked_image_pixels(image.width,image.height))
        throw std::invalid_argument("Invalid wallpaper image or matte");
    SaveOptions options;
    options.bmp.matte=matte;
    const auto &pixels=image.frames[frame].pixels;
    if (std::all_of(pixels.begin(),pixels.end(),[](Pixel p) {return p.a==255;}))
        return encode_for_save(image,frame,SaveFormat::Bmp,options);
    auto flattened=still_frame(image,frame,false);
    const unsigned red=(matte>>16)&255,green=(matte>>8)&255,blue=matte&255;
    for (auto &p:flattened.frames[0].pixels) {
        const unsigned alpha=p.a,inverse=255-alpha;
        // Official output truncates after division by255; ordinary over() rounds.
        p={static_cast<uint8_t>((unsigned(p.r)*alpha+red*inverse)/255),
           static_cast<uint8_t>((unsigned(p.g)*alpha+green*inverse)/255),
           static_cast<uint8_t>((unsigned(p.b)*alpha+blue*inverse)/255),255};
    }
    return encode_for_save(flattened,0,SaveFormat::Bmp,options);
}
} // namespace gvn
