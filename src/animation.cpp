#include "gvn/codecs.h"
#include "libplum.h"
#include "loading.h"
#include "image_metadata.h"
#include "animation_indices.h"
#include "apng_frames.h"
#include <array>
#include <cstring>
#include <utility>

namespace gvn {
namespace {
struct GifExtensions {
    std::vector<uint8_t> normalized;
    std::optional<uint16_t> loops;
    size_t header_end{};
    std::vector<std::pair<size_t, size_t>> frames;
};
GifExtensions gif_extensions(std::span<const uint8_t> bytes, int style,
                             size_t maximum_frames = std::numeric_limits<size_t>::max()) {
    if (style < 0 || style > 4)
        throw std::invalid_argument("Invalid GIF playback style");
    size_t cursor = 0;
    const auto take = [&](size_t count) {
        if (cursor > bytes.size() || count > bytes.size() - cursor)
            throw std::runtime_error("Truncated GIF extension or image data");
        auto result = bytes.subspan(cursor, count);
        cursor += count;
        return result;
    };
    const auto block = [&]() { return take(take(1)[0]); };
    const auto table = [&](uint8_t flags) {
        if (flags & 0x80)
            take(size_t{6} << (flags & 7));
    };
    const auto header = take(13);
    table(header[10]);
    GifExtensions result;
    result.header_end = cursor;
    size_t frame_start = cursor;
    while (cursor < bytes.size()) {
        const auto tag = take(1)[0];
        if (tag == 0x3b) {
            if (!result.frames.empty())
                result.frames.back().second += cursor - 1 - frame_start;
            return result;
        }
        if (tag == 0x2c) {
            table(take(9)[8]);
            take(1); // The existing codec validates and expands the LZW data.
            while (!block().empty()) {
            }
            result.frames.emplace_back(frame_start, cursor - frame_start);
            if (result.frames.size() > maximum_frames)
                throw std::runtime_error("GIF frames exceed the memory limit");
            frame_start = cursor;
        } else if (tag == 0x21) {
            const auto label = take(1)[0];
            if (label == 0xf9) {
                if (take(1)[0] != 4)
                    throw std::runtime_error("Invalid GIF graphic control extension");
                const auto offset = cursor;
                const auto flags = take(4)[0];
                if (take(1)[0])
                    throw std::runtime_error("Invalid GIF graphic control terminator");
                const auto method = (flags >> 2) & 7;
                if (method >= 4) {
                    // Keep, background and previous are 1, 2 and 3 in the GIF file.
                    static constexpr uint8_t map[5][4] = {
                        {1, 1, 1, 1}, {3, 3, 3, 3}, {3, 1, 1, 1}, {1, 1, 2, 3}, {3, 2, 2, 2}};
                    if (result.normalized.empty())
                        result.normalized.assign(bytes.begin(), bytes.end());
                    result.normalized[offset] =
                        static_cast<uint8_t>((flags & ~0x1c) | (map[style][method - 4] << 2));
                }
            } else {
                auto data = block();
                if (data.empty())
                    continue;
                // libplum recognizes the concatenated 14-byte application data;
                // subblock boundaries do not change its meaning. Keep bounded storage.
                std::array<uint8_t, 14> application{};
                size_t total = 0;
                do {
                    if (label == 0xff) {
                        if (total < application.size())
                            std::copy_n(data.begin(), std::min(data.size(), application.size() - total),
                                        application.begin() + total);
                        total = std::min<size_t>(15, total + data.size());
                    }
                    data = block();
                } while (!data.empty());
                if (label == 0xff && total == application.size() &&
                    !std::memcmp(application.data(), "NETSCAPE2.0", 11) && application[11] == 1) {
                    if (result.loops)
                        throw std::runtime_error("Duplicate GIF loop extension");
                    result.loops = static_cast<uint16_t>(application[12] | (uint16_t(application[13]) << 8));
                }
            }
        } else
            throw std::runtime_error("Invalid GIF block tag");
    }
    throw std::runtime_error("Missing GIF trailer");
}
} // namespace
bool is_apng(std::span<const uint8_t> bytes) {
    static const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), signature, 8))
        return false;
    size_t position = 8;
    while (position <= bytes.size() && bytes.size() - position >= 12) {
        const uint32_t length = (uint32_t(bytes[position]) << 24) | (uint32_t(bytes[position + 1]) << 16) |
                                (uint32_t(bytes[position + 2]) << 8) | bytes[position + 3];
        if (length > bytes.size() - position - 12)
            return false;
        if (!std::memcmp(bytes.data() + position + 4, "acTL", 4))
            return true;
        if (!std::memcmp(bytes.data() + position + 4, "IDAT", 4) ||
            !std::memcmp(bytes.data() + position + 4, "IEND", 4))
            return false;
        position += 12 + length;
    }
    return false;
}
namespace {
Image decode_gif_framewise(std::span<const uint8_t> bytes, size_t limit, const LoadingOptions &options) {
    if (bytes.size() < 13)
        throw std::runtime_error("Truncated GIF header");
    const uint32_t width = bytes[6] | (uint32_t(bytes[7]) << 8),
                   height = bytes[8] | (uint32_t(bytes[9]) << 8);
    const size_t stride = checked_pixels(width, height, 1, limit);
    const auto extensions = gif_extensions(bytes, options.gif_speed, limit / sizeof(Pixel) / stride);
    if (extensions.frames.empty())
        throw std::runtime_error("GIF has no frames");
    checked_pixels(width, height, extensions.frames.size(), limit);
    if (!extensions.normalized.empty())
        bytes = extensions.normalized;
    Image image;
    image.width = width;
    image.height = height;
    image.format = L"GIF";
    image.file_loops = !extensions.loops ? 1 : !*extensions.loops ? 0 : *extensions.loops;
    if (extensions.loops && *extensions.loops && options.gif_speed != 2 && options.gif_speed != 3)
        ++image.file_loops;
    image.animation_compensate = options.gif_speed != 2 && options.gif_speed != 3;
    image.frames.reserve(extensions.frames.size());
    std::vector<Pixel> canvas, previous;
    Pixel clear{};
    std::optional<Pixel> file_background;
    Rect old_area{};
    uint8_t old_disposal = 0;
    for (size_t frame = 0; frame < extensions.frames.size(); ++frame) {
        const auto [start, length] = extensions.frames[frame];
        std::vector<uint8_t> encoded;
        encoded.reserve(extensions.header_end + length + 1);
        encoded.insert(encoded.end(), bytes.begin(), bytes.begin() + extensions.header_end);
        encoded.insert(encoded.end(), bytes.begin() + start, bytes.begin() + start + length);
        encoded.push_back(0x3b);
        unsigned error{};
        std::unique_ptr<plum_image, decltype(&plum_destroy_image)> source(
            plum_load_image_limited(encoded.data(), encoded.size(), PLUM_COLOR_32 | PLUM_ALPHA_INVERT,
                                    limit / sizeof(Pixel), &error), plum_destroy_image);
        if (!source)
            throw std::runtime_error(std::string("GIF frame decode failed: ") + plum_get_error_text(error));
        if (source->type != PLUM_IMAGE_GIF || source->frames != 1 || source->width != width || source->height != height)
            throw std::runtime_error("GIF frame dimensions changed");
        if (extensions.frames.size() == 1 && !options.gif_transparency) {
            auto first = options;
            first.gif_animation = false;
            return decode(bytes, L"", limit, first);
        }
        const auto *background = plum_find_metadata(source.get(), PLUM_METADATA_BACKGROUND);
        if (!frame) {
            if (background && background->size >= sizeof(Pixel)) {
                std::memcpy(&clear, background->data, sizeof(Pixel));
                file_background = clear;
                file_background->a = 255;
            }
            const auto *depths = plum_find_metadata(source.get(), PLUM_METADATA_COLOR_DEPTH);
            clear.a = depths && depths->size >= 4 && static_cast<const uint8_t *>(depths->data)[3] ? 0 : 255;
            if (clear.a && !options.gif_background)
                clear = {static_cast<uint8_t>(options.background >> 16), static_cast<uint8_t>(options.background >> 8),
                         static_cast<uint8_t>(options.background), 255};
            canvas.assign(stride, clear);
        } else {
            const auto method = old_disposal % PLUM_DISPOSAL_REPLACE;
            if (method == PLUM_DISPOSAL_PREVIOUS && !previous.empty())
                canvas = previous;
            else if (method == PLUM_DISPOSAL_BACKGROUND)
                for (int y = old_area.y; y < old_area.y + old_area.h; ++y)
                    std::fill_n(canvas.begin() + static_cast<size_t>(y) * width + old_area.x, old_area.w, clear);
        }
        uint8_t method{};
        const auto *disposal = plum_find_metadata(source.get(), PLUM_METADATA_FRAME_DISPOSAL);
        if (disposal && disposal->size)
            method = static_cast<const uint8_t *>(disposal->data)[0];
        if (method % PLUM_DISPOSAL_REPLACE == PLUM_DISPOSAL_PREVIOUS)
            previous = canvas;
        else
            previous.clear();
        Rect area{0, 0, static_cast<int>(width), static_cast<int>(height)};
        const auto *areas = plum_find_metadata(source.get(), PLUM_METADATA_FRAME_AREA);
        if (areas && areas->size >= sizeof(plum_rectangle)) {
            const auto &rect = *static_cast<const plum_rectangle *>(areas->data);
            area = intersect({static_cast<int>(rect.left), static_cast<int>(rect.top), static_cast<int>(rect.width),
                              static_cast<int>(rect.height)}, area);
        }
        const auto *pixels = reinterpret_cast<const Pixel *>(source->data32);
        const bool replace = !frame || old_disposal >= PLUM_DISPOSAL_REPLACE;
        for (int y = area.y; y < area.y + area.h; ++y)
            for (int x = area.x; x < area.x + area.w; ++x) {
                const size_t at = static_cast<size_t>(y) * width + x;
                canvas[at] = replace ? pixels[at] : over(pixels[at], canvas[at]);
            }
        uint64_t ns = 100000000;
        const auto *duration = plum_find_metadata(source.get(), PLUM_METADATA_FRAME_DURATION);
        if (duration && duration->size >= sizeof(ns))
            std::memcpy(&ns, duration->data, sizeof(ns));
        auto ms = ns / 1000000;
        if (!ms || (options.gif_speed == 1 && ms < 100) || (options.gif_speed >= 2 && ms < 20))
            ms = 100;
        image.frames.push_back({canvas, std::clamp<uint64_t>(ms, 10, 3600000)});
        old_area = area;
        old_disposal = method;
    }
    read_container_metadata(image,bytes);
    finish_loading(image, options, file_background);
    if(image.animated()) {
        const auto used=image.bytes();
        if(used<=limit)try{image.indexed_source=gif_index_source(bytes,width,height,clear,options,file_background,limit-used);}catch(...){}
    }
    return image;
}
} // namespace
Image decode_animation(std::span<const uint8_t> bytes, size_t limit, const LoadingOptions &options) {
    if (bytes.empty() || bytes.size() > kSourceLimit)
        throw std::runtime_error("Invalid animation source size");
    const bool gif = bytes.size() >= 6 &&
                     (!std::memcmp(bytes.data(), "GIF87a", 6) || !std::memcmp(bytes.data(), "GIF89a", 6));
    if (gif)
        return decode_gif_framewise(bytes, limit, options);
    if (is_apng(bytes))
        if (auto image = decode_apng_framewise(bytes, limit, options))
            return std::move(*image);
    if (gif && bytes.size() >= 13)
        checked_pixels(bytes[6] | (uint32_t(bytes[7]) << 8), bytes[8] | (uint32_t(bytes[9]) << 8), 1, limit);
    const auto extensions = gif ? gif_extensions(bytes, options.gif_speed) : GifExtensions{};
    if (!extensions.normalized.empty())
        bytes = extensions.normalized;
    unsigned error = 0;
    std::unique_ptr<plum_image, decltype(&plum_destroy_image)> source(
        plum_load_image_limited(bytes.data(), bytes.size(), PLUM_COLOR_32 | PLUM_ALPHA_INVERT,
                                limit / sizeof(Pixel), &error),
        plum_destroy_image);
    if (!source)
        throw std::runtime_error(std::string("Animation decode failed: ") + plum_get_error_text(error));
    if (source->type == PLUM_IMAGE_GIF && source->frames == 1 && !options.gif_transparency) {
        // libplum substitutes the logical background RGB at transparent indexes.
        // WIC retains the original palette color when transparency is disabled.
        auto first = options;
        first.gif_animation = false;
        return decode(bytes, L"", limit, first);
    }
    const size_t count = checked_pixels(source->width, source->height, source->frames, limit),
                 stride = static_cast<size_t>(source->width) * source->height;
    (void)count;
    const auto *duration = plum_find_metadata(source.get(), PLUM_METADATA_FRAME_DURATION);
    const auto *disposal = plum_find_metadata(source.get(), PLUM_METADATA_FRAME_DISPOSAL);
    const auto *areas = plum_find_metadata(source.get(), PLUM_METADATA_FRAME_AREA);
    const auto *loops = plum_find_metadata(source.get(), PLUM_METADATA_LOOP_COUNT);
    const auto *background = plum_find_metadata(source.get(), PLUM_METADATA_BACKGROUND);
    const auto *depths = plum_find_metadata(source.get(), PLUM_METADATA_COLOR_DEPTH);
    Pixel clear{};
    if (source->type == PLUM_IMAGE_GIF) {
        if (background && background->size >= 4)
            std::memcpy(&clear, background->data, 4);
        clear.a = (depths && depths->size >= 4 && static_cast<const uint8_t *>(depths->data)[3]) ? 0 : 255;
        if (clear.a && !options.gif_background)
            clear = {static_cast<uint8_t>(options.background >> 16),
                     static_cast<uint8_t>(options.background >> 8), static_cast<uint8_t>(options.background),
                     255};
    }
    Image image;
    image.width = source->width;
    image.height = source->height;
    image.format = source->type == PLUM_IMAGE_GIF ? L"GIF" : L"APNG";
    if (loops && loops->size >= 4)
        std::memcpy(&image.file_loops, loops->data, 4);
    if (gif) {
        image.file_loops = !extensions.loops ? 1 : !*extensions.loops ? 0 : *extensions.loops;
        if (extensions.loops && *extensions.loops && options.gif_speed != 2 && options.gif_speed != 3)
            ++image.file_loops;
        image.animation_compensate = options.gif_speed != 2 && options.gif_speed != 3;
    }
    std::vector<Pixel> canvas(stride, clear), previous;
    Rect old_area{};
    uint8_t old_disposal = 0;
    for (size_t frame = 0; frame < source->frames; ++frame) {
        if (frame) {
            const auto method = old_disposal % PLUM_DISPOSAL_REPLACE;
            if (method == PLUM_DISPOSAL_PREVIOUS && !previous.empty())
                canvas = previous;
            else if (method == PLUM_DISPOSAL_BACKGROUND)
                for (int y = old_area.y; y < old_area.y + old_area.h; ++y)
                    std::fill_n(canvas.begin() + static_cast<size_t>(y) * image.width + old_area.x,
                                old_area.w, clear);
        }
        uint8_t method = 0;
        if (disposal && frame < disposal->size)
            method = static_cast<const uint8_t *>(disposal->data)[frame];
        if (method % PLUM_DISPOSAL_REPLACE == PLUM_DISPOSAL_PREVIOUS)
            previous = canvas;
        else
            previous.clear();
        Rect area{0, 0, static_cast<int>(image.width), static_cast<int>(image.height)};
        if (areas && (frame + 1) * sizeof(plum_rectangle) <= areas->size) {
            const auto &rect = static_cast<const plum_rectangle *>(areas->data)[frame];
            area = {static_cast<int>(rect.left), static_cast<int>(rect.top), static_cast<int>(rect.width),
                    static_cast<int>(rect.height)};
            area = intersect(area, {0, 0, static_cast<int>(image.width), static_cast<int>(image.height)});
        }
        const bool replace = frame == 0 || old_disposal >= PLUM_DISPOSAL_REPLACE;
        const auto *pixels = reinterpret_cast<const Pixel *>(source->data32) + frame * stride;
        for (int y = area.y; y < area.y + area.h; ++y)
            for (int x = area.x; x < area.x + area.w; ++x) {
                const size_t i = static_cast<size_t>(y) * image.width + x;
                canvas[i] = replace ? pixels[i] : over(pixels[i], canvas[i]);
            }
        uint64_t ns = 100000000;
        if (duration && (frame + 1) * sizeof(uint64_t) <= duration->size)
            ns = static_cast<const uint64_t *>(duration->data)[frame];
        if (gif || ns || source->frames == 1) {
            auto ms = ns / 1000000;
            if (gif && (!ms || (options.gif_speed == 1 && ms < 100) || (options.gif_speed >= 2 && ms < 20)))
                ms = 100;
            image.frames.push_back({canvas, std::clamp<uint64_t>(ms, 10, 3600000)});
        }
        old_area = area;
        old_disposal = method;
    }
    if (image.frames.empty())
        throw std::runtime_error("Animation has no visible frame");
    std::optional<Pixel> file_background;
    if (background && background->size >= sizeof(Pixel)) {
        Pixel color;
        std::memcpy(&color, background->data, sizeof(color));
        color.a = 255;
        file_background = color;
    }
    read_container_metadata(image,bytes);
    finish_loading(image, options, file_background);
    return image;
}
} // namespace gvn
