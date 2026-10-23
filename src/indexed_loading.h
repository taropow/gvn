#pragma once
#include "gvn/core.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <libplum.h>

namespace gvn {
inline void expand_indexed(const IndexedPixels &source, std::vector<Pixel> &pixels) {
    // Source shape/indices and destination size are checked before this decode step.
    // Fixed bit depths avoid a variable shift/division for every displayed pixel.
    const auto expand = [&]<unsigned Bits>() {
        const auto stride = (size_t(source.width) * Bits + 7) / 8;
        for (uint32_t y = 0; y < source.height; ++y) {
            const auto *row = source.packed.data() + size_t(y) * stride;
            auto *output = pixels.data() + size_t(y) * source.width;
            for (uint32_t x = 0; x < source.width; ++x) {
                const auto bit = size_t(x) * Bits;
                output[x] = source.palette[(row[bit / 8] >> (8 - Bits - bit % 8)) & ((1u << Bits) - 1)];
            }
        }
    };
    switch (source.bits) {
    case 1:
        expand.template operator()<1>();
        break;
    case 2:
        expand.template operator()<2>();
        break;
    case 4:
        expand.template operator()<4>();
        break;
    case 8:
        expand.template operator()<8>();
        break;
    default:
        throw std::runtime_error("Invalid indexed source bit depth");
    }
}
inline Image read_indexed_rle_bmp(std::span<const uint8_t> bytes, size_t limit) {
    unsigned error{};
    const std::unique_ptr<plum_image, decltype(&plum_destroy_image)> source(
        plum_load_image_limited(bytes.data(), bytes.size(),
                                PLUM_COLOR_32 | PLUM_ALPHA_INVERT | PLUM_PALETTE_LOAD, limit / sizeof(Pixel),
                                &error),
        plum_destroy_image);
    if (!source)
        throw std::runtime_error(std::string("Indexed BMP decode failed: ") + plum_get_error_text(error));
    if (source->type != PLUM_IMAGE_BMP || source->frames != 1 || !source->palette)
        throw std::runtime_error("Invalid indexed BMP raster");
    const auto count = checked_pixels(source->width, source->height, 1, limit);
    auto indexed = std::make_shared<IndexedPixels>();
    indexed->width = source->width;
    indexed->height = source->height;
    indexed->bits = bytes[28]; // Dispatch accepts only the 4/8-bit RLE variants.
    const size_t colors = size_t(source->max_palette_index) + 1;
    if (indexed->stride() * source->height + colors * sizeof(Pixel) > limit - count * sizeof(Pixel))
        throw std::runtime_error("Decoded image and palette exceed the memory limit");
    const auto *palette = reinterpret_cast<const Pixel *>(source->palette);
    indexed->palette.assign(palette, palette + colors);
    indexed->packed.resize(indexed->stride() * source->height);
    Image image;
    image.width = source->width;
    image.height = source->height;
    image.format = L"BMP";
    image.frames.push_back({std::vector<Pixel>(count), 100});
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x) {
            const auto index = source->data8[size_t(y) * image.width + x];
            if (index >= colors)
                throw std::runtime_error("Invalid BMP palette index");
            indexed->set_index(x, y, index);
            image.frames[0].pixels[size_t(y) * image.width + x] = indexed->palette[index];
        }
    if (!indexed->valid())
        throw std::runtime_error("Invalid indexed BMP pixels");
    image.frames[0].indexed = std::move(indexed);
    return image;
}
inline std::shared_ptr<const IndexedPixels> read_indexed(IWICImagingFactory *wic,
                                                         IWICBitmapFrameDecode *source, uint32_t width,
                                                         uint32_t height, size_t limit, bool optional = false) {
    WICPixelFormatGUID format{};
    if (FAILED(source->GetPixelFormat(&format)))
        throw std::runtime_error("Cannot read source pixel format");
    const unsigned bits = format == GUID_WICPixelFormat1bppIndexed   ? 1
                          : format == GUID_WICPixelFormat2bppIndexed ? 2
                          : format == GUID_WICPixelFormat4bppIndexed ? 4
                          : format == GUID_WICPixelFormat8bppIndexed ? 8
                                                                     : 0;
    if (!bits)
        return {};
    const auto result = std::make_shared<IndexedPixels>();
    result->width = width;
    result->height = height;
    result->bits = bits;
    Microsoft::WRL::ComPtr<IWICPalette> palette;
    WICColor colors[256]{};
    UINT count{};
    if (FAILED(wic->CreatePalette(&palette)) || FAILED(source->CopyPalette(palette.Get())) ||
        FAILED(palette->GetColors(256, colors, &count)) || !count || count > (1u << bits))
        throw std::runtime_error("Cannot read original image palette");
    const auto rgba_bytes = checked_pixels(width, height, 1, limit) * sizeof(Pixel);
    const auto extra = result->stride() * height + count * sizeof(Pixel);
    if (extra > limit - rgba_bytes) {
        if (optional)
            return {};
        throw std::runtime_error("Decoded image and palette exceed the memory limit");
    }
    for (UINT i = 0; i < count; ++i) {
        const auto color = colors[i];
        result->palette.push_back({static_cast<uint8_t>(color >> 16), static_cast<uint8_t>(color >> 8),
                                   static_cast<uint8_t>(color), static_cast<uint8_t>(color >> 24)});
    }
    result->packed.resize(result->stride() * height);
    if (FAILED(source->CopyPixels(nullptr, static_cast<UINT>(result->stride()),
                                  static_cast<UINT>(result->packed.size()), result->packed.data())) ||
        !result->valid())
        throw std::runtime_error("Invalid original palette indexes");
    return result;
}

// Move the actual decoded indices onto a GIF logical screen. Never reconstruct
// indices from RGBA: duplicate palette entries must retain their source numbers.
inline std::shared_ptr<const IndexedPixels> indexed_logical_screen(
    const IndexedPixels &source, uint32_t width, uint32_t height, uint32_t left, uint32_t top,
    Pixel clear, uint32_t preferred_background, size_t limit) {
    const auto rgba_bytes = checked_pixels(width, height, 1, limit) * sizeof(Pixel);
    auto result = std::make_shared<IndexedPixels>();
    result->width = width;
    result->height = height;
    result->bits = source.bits;
    result->palette = source.palette;
    size_t background = 0;
    const bool margin = left || top || source.width < width || source.height < height;
    if (margin) {
        if (preferred_background < result->palette.size() && result->palette[preferred_background] == clear)
            background = preferred_background;
        else {
            const auto found = std::find(result->palette.begin(), result->palette.end(), clear);
            if (found != result->palette.end())
                background = static_cast<size_t>(found - result->palette.begin());
            else {
                if (result->palette.size() == (1u << result->bits))
                    return {}; // A full local table plus its logical background needs another palette entry.
                background = result->palette.size();
                result->palette.push_back(clear);
            }
        }
    }
    const auto extra = result->stride() * height + result->palette.size() * sizeof(Pixel);
    if (extra > limit - rgba_bytes)
        return {}; // Optional provenance must not prevent an otherwise valid RGBA image from opening.
    result->packed.resize(result->stride() * height);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            const bool inside = x >= left && y >= top && x - left < source.width && y - top < source.height;
            result->set_index(x, y, inside ? source.index(x - left, y - top) : static_cast<uint8_t>(background));
        }
    return result;
}
} // namespace gvn
