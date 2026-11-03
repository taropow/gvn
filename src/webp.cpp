#include "gvn/codecs.h"
#include "image_metadata.h"
#include "loading.h"
#include <webp/decode.h>
#include <webp/demux.h>
#include <climits>
#include <cstring>

namespace gvn {
namespace {
struct Chunk {
    WebPChunkIterator value{};
    bool present{};
    Chunk(const WebPDemuxer *demux, const char *name)
        : present(WebPDemuxGetChunk(demux, name, 1, &value) != 0) {}
    ~Chunk() { if (present) WebPDemuxReleaseChunkIterator(&value); }
    std::span<const uint8_t> bytes() const {
        return present ? std::span(value.chunk.bytes, value.chunk.size) : std::span<const uint8_t>{};
    }
};
// WebP EXIF chunks may contain a TIFF header directly or the JPEG EXIF prefix.
int read_exif(std::span<const uint8_t> bytes, ImageMetadata &metadata) {
    if (bytes.size() >= 6 && !std::memcmp(bytes.data(), "Exif\0\0", 6)) bytes = bytes.subspan(6);
    if (bytes.size() < 8) return 1;
    const bool little = bytes[0] == 'I' && bytes[1] == 'I';
    if (!little && !(bytes[0] == 'M' && bytes[1] == 'M')) return 1;
    const auto fits = [&](size_t p, size_t n) { return p <= bytes.size() && n <= bytes.size() - p; };
    const auto word = [&](size_t p) -> uint32_t {
        if (!fits(p, 2)) return 0;
        return little ? uint32_t(bytes[p]) | (uint32_t(bytes[p+1]) << 8)
                      : (uint32_t(bytes[p]) << 8) | bytes[p+1];
    };
    const auto dword = [&](size_t p) -> uint32_t {
        if (!fits(p, 4)) return 0;
        return little ? word(p) | (word(p+2) << 16) : (word(p) << 16) | word(p+2);
    };
    if (word(2) != 42) return 1;
    const size_t directory = dword(4);
    if (directory < 8 || !fits(directory, 2)) return 1;
    const size_t count = word(directory);
    if (!fits(directory+2, count*12)) return 1;
    int orientation = 1, unit = 2;
    double x{}, y{};
    for (size_t i = 0; i < count; ++i) {
        const size_t p = directory+2+i*12;
        if (dword(p+4) != 1) continue;
        if (word(p+2) == 3) {
            const auto value = word(p+8);
            if (word(p) == 0x112 && value >= 1 && value <= 8) orientation = static_cast<int>(value);
            if (word(p) == 0x128) unit = static_cast<int>(value);
        } else if (word(p+2) == 5 && (word(p) == 0x11a || word(p) == 0x11b)) {
            const size_t offset = dword(p+8);
            if (fits(offset, 8) && dword(offset+4)) {
                const double value = double(dword(offset))/dword(offset+4);
                (word(p) == 0x11a ? x : y) = value;
            }
        }
    }
    if (unit == 2 || unit == 3) set_image_resolution(metadata, x*(unit == 3 ? 2.54 : 1.), y*(unit == 3 ? 2.54 : 1.));
    return orientation;
}
void orient(Image &image, int orientation, const CancelCheck &cancel) {
    if (orientation == 1) return;
    const auto width = image.width, height = image.height;
    const bool swap = orientation >= 5;
    const uint32_t output_width = swap ? height : width;
    for (auto &frame : image.frames) {
        std::vector<Pixel> pixels(frame.pixels.size());
        for (uint32_t y = 0; y < height; ++y) {
            check_cancelled(cancel);
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t dx = x, dy = y;
                switch (orientation) {
                case 2: dx = width-1-x; break;
                case 3: dx = width-1-x; dy = height-1-y; break;
                case 4: dy = height-1-y; break;
                case 5: dx = y; dy = x; break;
                case 6: dx = height-1-y; dy = x; break;
                case 7: dx = height-1-y; dy = width-1-x; break;
                case 8: dx = y; dy = width-1-x; break;
                }
                pixels[size_t(dy)*output_width+dx] = frame.pixels[size_t(y)*width+x];
            }
        }
        frame.pixels = std::move(pixels);
    }
    if (swap) {
        std::swap(image.width, image.height);
        std::swap(image.metadata.dpi_x, image.metadata.dpi_y);
    }
}
} // namespace
bool is_webp(std::span<const uint8_t> bytes) {
    return bytes.size() >= 12 && !std::memcmp(bytes.data(), "RIFF", 4) &&
           !std::memcmp(bytes.data()+8, "WEBP", 4);
}
Image decode_webp(std::span<const uint8_t> bytes, size_t limit, const LoadingOptions &options,
                  const CancelCheck &cancel) {
    check_cancelled(cancel);
    if (!is_webp(bytes) || bytes.size() > kSourceLimit) throw std::runtime_error("Invalid WebP source");
    const WebPData data{bytes.data(), bytes.size()};
    WebPDemuxState state{};
    std::unique_ptr<WebPDemuxer, decltype(&WebPDemuxDelete)> demux(
        WebPDemuxPartial(&data, &state), WebPDemuxDelete);
    if (!demux || state != WEBP_DEMUX_DONE) throw std::runtime_error("Truncated or invalid WebP container");
    const auto width = WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_WIDTH);
    const auto height = WebPDemuxGetI(demux.get(), WEBP_FF_CANVAS_HEIGHT);
    const auto count = WebPDemuxGetI(demux.get(), WEBP_FF_FRAME_COUNT);
    const auto flags = WebPDemuxGetI(demux.get(), WEBP_FF_FORMAT_FLAGS);
    if (!count) throw std::runtime_error("WebP has no frames");
    const auto frames = options.webp_animation ? count : 1u;
    const size_t stride = checked_pixels(width, height, frames, limit)/frames;
    Image image;
    image.width = width; image.height = height; image.format = L"WebP";
    image.file_loops = flags & ANIMATION_FLAG ? WebPDemuxGetI(demux.get(), WEBP_FF_LOOP_COUNT) : 1;
    image.frames.reserve(frames);
    if (!(flags & ANIMATION_FLAG)) {
        Frame frame; frame.pixels.resize(stride);
        if (!WebPDecodeRGBAInto(bytes.data(), bytes.size(), reinterpret_cast<uint8_t *>(frame.pixels.data()),
                                stride*sizeof(Pixel), static_cast<int>(width*sizeof(Pixel))))
            throw std::runtime_error("WebP pixel decoding failed");
        image.frames.push_back(std::move(frame));
    } else {
        WebPAnimDecoderOptions configuration{};
        if (!WebPAnimDecoderOptionsInit(&configuration)) throw std::runtime_error("WebP decoder ABI mismatch");
        configuration.color_mode = MODE_RGBA; configuration.use_threads = 1;
        std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(
            WebPAnimDecoderNew(&data, &configuration), WebPAnimDecoderDelete);
        if (!decoder) throw std::runtime_error("Invalid WebP animation");
        uint64_t elapsed{};
        for (uint32_t index = 0; index < frames; ++index) {
            check_cancelled(cancel);
            WebPIterator info{};
            if (!WebPDemuxGetFrame(demux.get(), static_cast<int>(index+1), &info))
                throw std::runtime_error("Missing WebP frame");
            const auto duration = static_cast<uint64_t>(info.duration);
            WebPDemuxReleaseIterator(&info);
            elapsed += duration;
            // libwebp's cumulative timestamp is a signed int. Reject before it can overflow.
            if (elapsed > INT_MAX) throw std::runtime_error("WebP timeline exceeds the supported duration");
            uint8_t *pixels{}; int timestamp{};
            if (!WebPAnimDecoderGetNext(decoder.get(), &pixels, &timestamp) || !pixels || timestamp != elapsed)
                throw std::runtime_error("WebP animation frame decoding failed");
            check_cancelled(cancel);
            Frame frame; frame.pixels.resize(stride);
            std::memcpy(frame.pixels.data(), pixels, stride*sizeof(Pixel));
            frame.duration_ms = std::clamp<uint64_t>(duration, 10, 3600000);
            image.frames.push_back(std::move(frame));
        }
    }
    check_cancelled(cancel);
    read_container_metadata(image, bytes);
    image.metadata.original_color_type = ImageColorType::Rgb;
    image.metadata.original_bpp = flags & ALPHA_FLAG ? 32 : 24;
    const Chunk icc(demux.get(), "ICCP");
    image.metadata.icc_profile = !icc.bytes().empty();
    image.metadata.icc_bytes = static_cast<uint32_t>(icc.bytes().size());
    const Chunk exif(demux.get(), "EXIF");
    const int orientation = read_exif(exif.bytes(), image.metadata);
    if (options.exif_orientation) orient(image, orientation, cancel);
    finish_loading(image, options);
    return image;
}
} // namespace gvn
