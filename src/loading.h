#pragma once
#include "gvn/codecs.h"
#include <optional>
namespace gvn {
inline void finish_loading(Image &image, const LoadingOptions &options,
                           std::optional<Pixel> file_background = {}) {
    const bool gif = image.format == L"GIF", png = image.format == L"PNG" || image.format == L"APNG";
    if (!gif && !png && image.format != L"BMP")
        return;
    const bool transparent = gif   ? (image.animated() || options.gif_transparency)
                             : png ? options.png_transparency
                                   : options.bmp_transparency;
    if (transparent && options.preserve_alpha)
        return;
    Pixel background{static_cast<uint8_t>(options.background >> 16),
                     static_cast<uint8_t>(options.background >> 8), static_cast<uint8_t>(options.background),
                     255};
    if (file_background && ((gif && options.gif_background) || (png && options.png_background)))
        background = *file_background;
    background.a = 255;
    for (auto &frame : image.frames) {
        if (frame.indexed) {
            auto indexed = std::make_shared<IndexedPixels>(*frame.indexed);
            for (auto &color : indexed->palette)
                if (transparent)
                    color = over(color, background);
                else
                    color.a = 255;
            frame.indexed = std::move(indexed);
        }
        for (auto &pixel : frame.pixels)
            if (transparent)
                pixel = over(pixel, background);
            else
                pixel.a = 255;
    }
}
} // namespace gvn
