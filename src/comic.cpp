#include "gvn/comic.h"
#include "gvn/edit.h"
#include "display_source.h"

namespace gvn {
std::wstring ComicOptions::key() const {
    return std::to_wstring(mode) + L":" +
           std::to_wstring(mode == 0   ? fill
                           : mode == 1 ? down
                           : mode == 2 ? up
                                       : 0) +
           L":" + std::to_wstring(rotate_before) + L":" + std::to_wstring(single_wide);
}
namespace {
void validate(const Image &image) {
    if (image.frames.size() != 1 ||
        image.frames[0].pixels.size() != checked_pixels(image.width, image.height))
        throw std::invalid_argument("Comic composition requires a valid still image");
}
uint32_t scaled_width(const Image &image, uint32_t height) {
    const auto width = (static_cast<uint64_t>(image.width) * height + image.height / 2) / image.height;
    if (width > UINT32_MAX)
        throw std::runtime_error("Comic page width exceeds image limit");
    return static_cast<uint32_t>(std::max<uint64_t>(1, width));
}
uint32_t simple_position(uint32_t source, uint32_t destination, uint32_t at) {
    if (destination >= source)
        return static_cast<uint32_t>((2ull * at + 1) * source / (2ull * destination));
    if (destination == 1)
        return 0;
    return static_cast<uint32_t>((2ull * at * (source - 1) + destination - 1) / (2ull * (destination - 1)));
}
void write_page(Image &out, const Image &page, uint32_t left, uint32_t top, uint32_t width, uint32_t height,
                int method) {
    auto &destination = out.frames[0].pixels;
    const auto &source = page.frames[0].pixels;
    if (width == page.width && height == page.height) {
        for (uint32_t y = 0; y < height; ++y)
            std::copy_n(source.begin() + static_cast<size_t>(y) * width, width,
                        destination.begin() + static_cast<size_t>(y + top) * out.width + left);
        return;
    }
    if (method == DisplaySimple) {
        // Match the reference coordinate convention while retaining straight RGBA exactly.
        for (uint32_t y = 0; y < height; ++y) {
            const auto sy = page.height - 1 - simple_position(page.height, height, height - 1 - y);
            for (uint32_t x = 0; x < width; ++x)
                destination[static_cast<size_t>(y + top) * out.width + left + x] =
                    source[static_cast<size_t>(sy) * page.width + simple_position(page.width, width, x)];
        }
        return;
    }
    detail::DisplaySource input{
        {reinterpret_cast<const uint8_t *>(source.data()), source.size() * sizeof(Pixel)},
        page.width,
        page.height,
        true};
    DisplayTile tile;
    tile.width = width;
    // At most 1 MiB of output staging, except a single exceptionally wide row.
    const auto rows = std::max(1u, std::min(64u, 1024u * 1024u / width / 4));
    for (uint32_t y = 0; y < height; y += tile.height) {
        tile.height = std::min(rows, height - y);
        tile.bgra.resize(checked_pixels(width, tile.height, 1, 64ull * 1024 * 1024) * 4);
        detail::resample_source_tile(input, width, height, 0, y, method, tile);
        for (uint32_t yy = 0; yy < tile.height; ++yy)
            for (uint32_t x = 0; x < width; ++x) {
                const auto p = tile.bgra.data() + (static_cast<size_t>(yy) * width + x) * 4;
                const auto alpha = p[3];
                const auto straight = [&](uint8_t value) {
                    return static_cast<uint8_t>(
                        alpha ? std::min(255u, (unsigned(value) * 255 + alpha / 2) / alpha) : 0);
                };
                destination[static_cast<size_t>(y + yy + top) * out.width + left + x] = {
                    straight(p[2]), straight(p[1]), straight(p[0]), alpha};
            }
    }
}
} // namespace
Image compose_comic_pages(const Image &first, const Image &second, bool rtl, const ComicOptions &options) {
    validate(first);
    validate(second);
    if (options.mode < 0 || options.mode > 4 || options.down < 0 || options.down > DisplayLanczos3 ||
        options.up < 0 || options.up > DisplayLanczos3 || options.fill > 0xffffff)
        throw std::invalid_argument("Invalid comic composition options");
    if (options.mode >= 3) {
        if (options.mode == 4 || first.height == second.height)
            return join_pages(first, second, rtl);
        const auto width = static_cast<uint64_t>(second.width) * first.height / second.height;
        if (width > UINT32_MAX)
            throw std::runtime_error("Comic page width exceeds image limit");
        return join_pages(
            first, resize_image(second, static_cast<uint32_t>(std::max<uint64_t>(1, width)), first.height),
            rtl);
    }
    const auto height =
        options.mode == 1 ? std::min(first.height, second.height) : std::max(first.height, second.height);
    const auto first_width = options.mode == 0 ? first.width : scaled_width(first, height);
    const auto second_width = options.mode == 0 ? second.width : scaled_width(second, height);
    const auto total = static_cast<uint64_t>(first_width) + second_width;
    if (total > UINT32_MAX)
        throw std::runtime_error("Comic image width exceeds image limit");
    Image out;
    out.width = static_cast<uint32_t>(total);
    out.height = height;
    const Pixel fill{static_cast<uint8_t>(options.fill >> 16), static_cast<uint8_t>(options.fill >> 8),
                     static_cast<uint8_t>(options.fill), 255};
    out.frames.push_back(
        {std::vector<Pixel>(checked_pixels(out.width, out.height), options.mode == 0 ? fill : Pixel{}), 100});
    const auto method = options.mode == 1 ? options.down : options.up;
    write_page(out, first, rtl ? second_width : 0, options.mode == 0 ? (height - first.height) / 2 : 0,
               first_width, options.mode == 0 ? first.height : height, method);
    write_page(out, second, rtl ? 0 : first_width, options.mode == 0 ? (height - second.height) / 2 : 0,
               second_width, options.mode == 0 ? second.height : height, method);
    out.format = L"見開き";
    out.metadata=first.metadata;
    return out;
}
} // namespace gvn
