#include "gvn/view_rotation.h"

namespace gvn {
int view_rotation_quarters(int mode, uint32_t width, uint32_t height) {
    if (mode < 0 || mode > 7)
        throw std::invalid_argument("Invalid display rotation option");
    const bool left = mode == 1 || (mode == 3 && width < height) || (mode == 5 && width > height);
    const bool right = mode == 2 || (mode == 4 && width < height) || (mode == 6 && width > height);
    return left ? 3 : right ? 1 : mode == 7 ? 2 : 0;
}
void apply_view_rotation(Image &image, int mode) {
    const auto quarters = view_rotation_quarters(mode, image.width, image.height);
    if (!quarters)
        return;
    const auto count = checked_image_pixels(image.width, image.height);
    if (image.frames.empty() || std::any_of(image.frames.begin(), image.frames.end(),
                                            [&](const auto &frame) { return frame.pixels.size() != count; }))
        throw std::invalid_argument("Invalid image frames for display rotation");
    const uint32_t width = quarters % 2 ? image.height : image.width;
    const uint32_t height = quarters % 2 ? image.width : image.height;
    const auto source_at = [&](uint32_t x, uint32_t y) {
        return quarters == 1   ? std::pair{y, image.height - 1 - x}
               : quarters == 2 ? std::pair{image.width - 1 - x, image.height - 1 - y}
                               : std::pair{image.width - 1 - y, x};
    };
    // Prepare allocations before changing any pixel or metadata.
    std::vector<std::shared_ptr<const IndexedPixels>> indexed(image.frames.size());
    for (size_t i = 0; i < image.frames.size(); ++i) {
        const auto &source = image.frames[i];
        const auto &old = source.indexed;
        if (!old || old->width != image.width || old->height != image.height || !old->valid() ||
            !old->matches(source.pixels))
            continue;
        auto next = std::make_shared<IndexedPixels>();
        next->width = width;
        next->height = height;
        next->bits = old->bits;
        next->palette = old->palette;
        next->packed.resize(next->stride() * height);
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x) {
                const auto [sx, sy] = source_at(x, y);
                next->set_index(x, y, old->index(sx, sy));
            }
        indexed[i] = std::move(next);
    }
    std::vector<Pixel> scratch;
    if (quarters % 2)
        scratch.resize(count);
    for (size_t i = 0; i < image.frames.size(); ++i) {
        auto &frame = image.frames[i];
        if (quarters == 2)
            std::reverse(frame.pixels.begin(), frame.pixels.end());
        else {
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x) {
                    const auto [sx, sy] = source_at(x, y);
                    scratch[size_t(y) * width + x] = frame.pixels[size_t(sy) * image.width + sx];
                }
            frame.pixels.swap(scratch);
        }
        frame.indexed = std::move(indexed[i]);
    }
    image.width = width;
    image.height = height;
    if (image.indexed_source)
        image.indexed_quarters = (image.indexed_quarters + quarters) % 4;
    if(quarters%2)std::swap(image.metadata.dpi_x,image.metadata.dpi_y);
}
} // namespace gvn
