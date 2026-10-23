#include "gvn/edit.h"

namespace gvn {
Image edit_palette(const Image &image, const std::vector<Pixel> &palette, const EditMemory &memory) {
    if (image.frames.size() != 1)
        throw std::invalid_argument("Palette editing requires one indexed frame");
    const auto &frame = image.frames[0];
    if (!frame.indexed || frame.indexed->width != image.width || frame.indexed->height != image.height ||
        !frame.indexed->matches(frame.pixels))
        throw std::invalid_argument("Current image has no matching palette indices");
    if (palette.size() != frame.indexed->palette.size())
        throw std::invalid_argument("Palette editing cannot change the table size");
    for (size_t i = 0; i < palette.size(); ++i)
        if (palette[i].a != frame.indexed->palette[i].a)
            throw std::invalid_argument("RGB palette editing must preserve alpha");
    Image result;
    result.width = image.width; result.height = image.height;
    result.format = image.format; result.path = image.path;
    result.file_bytes = image.file_bytes; result.load_ms = image.load_ms; result.metadata = image.metadata;
    auto pixels = memory.allocate(image.width, image.height);
    auto indexed = std::make_shared<IndexedPixels>(*frame.indexed);
    indexed->palette = palette;
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x)
            pixels[size_t(y) * image.width + x] = palette[indexed->index(x, y)];
    result.frames.push_back({std::move(pixels), frame.duration_ms, std::move(indexed)});
    return result;
}
} // namespace gvn
