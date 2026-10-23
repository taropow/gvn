#include "apng_frames.h"
#include "image_metadata.h"
#include "loading.h"
#include "libplum.h"
#include "miniz.h"
#include <array>
#include <cstring>

namespace gvn {
namespace {
struct LayoutError {};
uint32_t word(std::span<const uint8_t> bytes, size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4)
        throw LayoutError{};
    return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) |
           (uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
}
void put(std::vector<uint8_t> &out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<uint8_t>(value >> shift));
}
void chunk(std::vector<uint8_t> &out, const char *type, std::span<const uint8_t> data) {
    if (data.size() > kSourceLimit - 12 || out.size() > kSourceLimit - data.size() - 12)
        throw std::runtime_error("APNG frame exceeds the source limit");
    put(out, static_cast<uint32_t>(data.size()));
    const auto at = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put(out, static_cast<uint32_t>(mz_crc32(0, out.data() + at, data.size() + 4)));
}
struct ApngFrame {
    uint32_t width{}, height{}, x{}, y{};
    uint16_t numerator{}, denominator{};
    uint8_t disposal{}, blend{};
    bool idat{};
    std::vector<std::span<const uint8_t>> data;
};
struct Container {
    std::array<uint8_t, 13> header{};
    uint32_t width{}, height{}, loops{};
    std::vector<std::span<const uint8_t>> globals, poster;
    std::vector<ApngFrame> frames;
};
Container inspect(std::span<const uint8_t> bytes, size_t limit) {
    static constexpr uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < 33 || std::memcmp(bytes.data(), signature, 8))
        throw LayoutError{};
    Container c;
    size_t at = 8;
    uint64_t sequence = 0;
    uint32_t declared = 0;
    bool header = false, animation = false, data = false, data_ended = false, end = false;
    while (at < bytes.size()) {
        if (bytes.size() - at < 12)
            throw LayoutError{};
        const size_t size = word(bytes, at);
        if (size > bytes.size() - at - 12)
            throw LayoutError{};
        const auto *type = bytes.data() + at + 4;
        const auto payload = bytes.subspan(at + 8, size);
        if (word(bytes, at + size + 8) != mz_crc32(0, type, size + 4))
            throw LayoutError{};
        const auto is = [&](const char *name) { return !std::memcmp(type, name, 4); };
        if (!header && !is("IHDR"))
            throw LayoutError{};
        if (data && !is("IDAT"))
            data_ended = true;
        if (is("IHDR")) {
            if (header || size != 13)
                throw LayoutError{};
            std::copy(payload.begin(), payload.end(), c.header.begin());
            c.width = word(payload, 0);
            c.height = word(payload, 4);
            const auto depth = payload[8], color = payload[9];
            const bool valid = color == 0 ? depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16
                             : color == 3 ? depth == 1 || depth == 2 || depth == 4 || depth == 8
                             : (color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16);
            if (!valid || payload[10] || payload[11] || payload[12] > 1 ||
                !c.width || !c.height || c.width > 65535 || c.height > 65535)
                throw LayoutError{};
            checked_pixels(c.width, c.height, 1, limit);
            header = true;
        } else if (is("acTL")) {
            if (animation || data || size != 8)
                throw LayoutError{};
            declared = word(payload, 0);
            c.loops = word(payload, 4);
            if (!declared || declared > 100000 || c.loops > 0x7fffffff)
                throw LayoutError{};
            animation = true;
        } else if (is("fcTL")) {
            if (!animation || size != 26 || word(payload, 0) != sequence++ ||
                (!c.frames.empty() && c.frames.back().data.empty()) || c.frames.size() >= declared)
                throw LayoutError{};
            ApngFrame f;
            f.width = word(payload, 4); f.height = word(payload, 8);
            f.x = word(payload, 12); f.y = word(payload, 16);
            f.numerator = uint16_t((payload[20] << 8) | payload[21]);
            f.denominator = uint16_t((payload[22] << 8) | payload[23]);
            f.disposal = payload[24]; f.blend = payload[25]; f.idat = !data;
            if (!f.width || !f.height || f.x > c.width || f.y > c.height ||
                f.width > c.width - f.x || f.height > c.height - f.y || f.disposal > 2 || f.blend > 1 ||
                (f.idat && (f.width != c.width || f.height != c.height || f.x || f.y)))
                throw LayoutError{};
            c.frames.push_back(std::move(f));
        } else if (is("IDAT")) {
            if (!animation || data_ended || (!c.frames.empty() && !c.frames.back().idat))
                throw LayoutError{};
            if (c.frames.empty())
                c.poster.push_back(payload);
            else
                c.frames.back().data.push_back(payload);
            data = true;
        } else if (is("fdAT")) {
            if (!data || c.frames.empty() || c.frames.back().idat || size < 4 || word(payload, 0) != sequence++)
                throw LayoutError{};
            c.frames.back().data.push_back(payload.subspan(4));
        } else if (is("IEND")) {
            if (size || !data || c.frames.size() != declared || c.frames.back().data.empty())
                throw LayoutError{};
            end = true;
            break;
        } else {
            // Preserve all pre-image chunks so libplum applies its existing palette,
            // transparency/background rules and validates optional pixel metadata.
            if (!data)
                c.globals.push_back(bytes.subspan(at, size + 12));
            else if (!(type[0] & 32) || is("tRNS") || is("bKGD") || is("PLTE"))
                throw LayoutError{};
        }
        at += size + 12;
    }
    if (!end || !animation)
        throw LayoutError{};
    checked_pixels(c.width, c.height, c.frames.size() + !c.poster.empty(), limit);
    return c;
}
using Decoded = std::unique_ptr<plum_image, decltype(&plum_destroy_image)>;
Decoded decode_frame(const Container &c, const ApngFrame &frame, size_t limit) {
    static constexpr uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::vector<uint8_t> encoded(std::begin(signature), std::end(signature));
    auto header = c.header;
    for (size_t i = 0; i < 4; ++i) {
        header[i] = static_cast<uint8_t>(frame.width >> (24 - i * 8));
        header[4 + i] = static_cast<uint8_t>(frame.height >> (24 - i * 8));
    }
    chunk(encoded, "IHDR", header);
    for (auto part : c.globals) {
        if (part.size() > kSourceLimit - encoded.size())
            throw std::runtime_error("APNG metadata exceeds the source limit");
        encoded.insert(encoded.end(), part.begin(), part.end());
    }
    for (auto part : frame.data)
        chunk(encoded, "IDAT", part);
    chunk(encoded, "IEND", {});
    unsigned error{};
    Decoded result(plum_load_image_limited(encoded.data(), encoded.size(), PLUM_COLOR_32 | PLUM_ALPHA_INVERT,
                                           limit / sizeof(Pixel), &error), plum_destroy_image);
    if (!result)
        throw std::runtime_error(std::string("APNG frame decode failed: ") + plum_get_error_text(error));
    if (result->frames != 1 || result->width != frame.width || result->height != frame.height)
        throw std::runtime_error("APNG frame dimensions changed");
    return result;
}
} // namespace
std::optional<Image> decode_apng_framewise(std::span<const uint8_t> bytes, size_t limit,
                                          const LoadingOptions &options) {
    Container c;
    try {
        c = inspect(bytes, limit);
    } catch (const LayoutError &) {
        return {};
    }
    std::optional<Pixel> background;
    const auto read_background = [&](const plum_image *frame) {
        const auto *meta = plum_find_metadata(frame, PLUM_METADATA_BACKGROUND);
        if (meta && meta->size >= sizeof(Pixel)) {
            Pixel p;
            std::memcpy(&p, meta->data, sizeof(p));
            p.a = 255;
            background = p;
        }
    };
    if (!c.poster.empty()) {
        ApngFrame poster;
        poster.width = c.width; poster.height = c.height; poster.data = std::move(c.poster);
        const auto frame = decode_frame(c, poster, limit);
        read_background(frame.get());
    }
    Image image;
    image.width = c.width; image.height = c.height; image.format = L"APNG"; image.file_loops = c.loops;
    image.frames.reserve(c.frames.size());
    std::vector<Pixel> canvas(static_cast<size_t>(c.width) * c.height), previous;
    for (size_t i = 0; i < c.frames.size(); ++i) {
        const auto &control = c.frames[i];
        const auto frame = decode_frame(c, control, limit);
        if (!i)
            read_background(frame.get());
        if (control.disposal == 2) {
            previous.resize(static_cast<size_t>(control.width) * control.height);
            for (size_t y = 0; y < control.height; ++y)
                std::copy_n(canvas.begin() + (control.y + y) * c.width + control.x, control.width,
                            previous.begin() + y * control.width);
        }
        const auto *pixels = reinterpret_cast<const Pixel *>(frame->data32);
        const bool replace = (!i && control.idat) || !control.blend;
        for (size_t y = 0; y < control.height; ++y)
            for (size_t x = 0; x < control.width; ++x) {
                const size_t target = (control.y + y) * c.width + control.x + x;
                const auto p = pixels[y * control.width + x];
                canvas[target] = replace ? p : over(p, canvas[target]);
            }
        const uint64_t denominator = control.denominator ? control.denominator : 100;
        const uint64_t ns = control.numerator ? (uint64_t(control.numerator) * 1000000000 + denominator / 2) / denominator : 1;
        image.frames.push_back({canvas, std::clamp<uint64_t>(ns / 1000000, 10, 3600000)});
        if (control.disposal == 1)
            for (size_t y = 0; y < control.height; ++y)
                std::fill_n(canvas.begin() + (control.y + y) * c.width + control.x, control.width, Pixel{});
        else if (control.disposal == 2)
            for (size_t y = 0; y < control.height; ++y)
                std::copy_n(previous.begin() + y * control.width, control.width,
                            canvas.begin() + (control.y + y) * c.width + control.x);
    }
    read_container_metadata(image, bytes);
    finish_loading(image, options, background);
    return image;
}
} // namespace gvn
