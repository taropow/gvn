#include "gvn/saving.h"
#include "image_resolution.h"
#include <webp/encode.h>
#include <webp/mux.h>
#include <cstring>

namespace gvn {
namespace {
constexpr size_t output_limit = 512ull * 1024 * 1024;
struct Picture {
    WebPPicture value{};
    Picture() {
        if (!WebPPictureInit(&value)) throw std::runtime_error("WebP encoder ABI mismatch");
    }
    ~Picture() { WebPPictureFree(&value); }
};
struct Output {
    std::vector<uint8_t> bytes;
    const CancelCheck &cancel;
    static int write(const uint8_t *data, size_t size, const WebPPicture *picture) noexcept {
        auto &self = *static_cast<Output *>(picture->custom_ptr);
        if (size > output_limit - self.bytes.size()) return 0;
        try { self.bytes.insert(self.bytes.end(), data, data+size); return 1; }
        catch (...) { return 0; }
    }
    static int progress(int, const WebPPicture *picture) noexcept {
        const auto &self = *static_cast<Output *>(picture->custom_ptr);
        try { return !self.cancel || !self.cancel(); }
        catch (...) { return 0; }
    }
};
struct MuxOutput {
    WebPData data{};
    ~MuxOutput() { WebPDataClear(&data); }
};
} // namespace
std::vector<uint8_t> encode_webp(const Image &image, size_t frame, const WebpSaveOptions &options,
                                 const ImageMetadata *metadata, const CancelCheck &cancel) {
    check_cancelled(cancel);
    if (!image.width || !image.height || image.width > WEBP_MAX_DIMENSION || image.height > WEBP_MAX_DIMENSION ||
        frame >= image.frames.size() ||
        image.frames[frame].pixels.size() != checked_image_pixels(image.width, image.height))
        throw std::invalid_argument("Invalid frame or dimensions for WebP saving");
    if (options.quality < 0 || options.quality > 100 || options.method < 0 || options.method > 6 ||
        options.matte > 0xffffff)
        throw std::invalid_argument("Invalid WebP save options");
    const auto &resolution = metadata ? *metadata : image.metadata;
    detail::validate_resolution(resolution);
    WebPConfig configuration{};
    if (!WebPConfigInit(&configuration)) throw std::runtime_error("WebP encoder ABI mismatch");
    configuration.lossless = options.lossless;
    configuration.quality = options.lossless ? 100.f : static_cast<float>(options.quality);
    configuration.method = options.method;
    configuration.exact = 1; // Preserve RGB even in fully transparent lossless pixels.
    configuration.thread_level = 1;
    if (!WebPValidateConfig(&configuration)) throw std::invalid_argument("Invalid WebP configuration");
    Picture picture;
    picture.value.width = static_cast<int>(image.width);
    picture.value.height = static_cast<int>(image.height);
    picture.value.use_argb = 1;
    const auto &pixels = image.frames[frame].pixels;
    if (!WebPPictureImportRGBA(&picture.value, reinterpret_cast<const uint8_t *>(pixels.data()),
                               static_cast<int>(image.width * sizeof(Pixel))))
        throw std::runtime_error("Cannot allocate WebP encoder pixels");
    if (!options.preserve_alpha) {
        const Pixel matte{static_cast<uint8_t>(options.matte >> 16), static_cast<uint8_t>(options.matte >> 8),
                          static_cast<uint8_t>(options.matte), 255};
        for (uint32_t y = 0; y < image.height; ++y) {
            check_cancelled(cancel);
            for (uint32_t x = 0; x < image.width; ++x) {
                const auto p = over(pixels[size_t(y)*image.width+x], matte);
                picture.value.argb[size_t(y)*picture.value.argb_stride+x] =
                    0xff000000u | (uint32_t(p.r) << 16) | (uint32_t(p.g) << 8) | p.b;
            }
        }
    }
    Output output{{}, cancel};
    picture.value.custom_ptr = &output;
    picture.value.writer = Output::write;
    picture.value.progress_hook = Output::progress;
    const bool encoded = WebPEncode(&configuration, &picture.value) != 0;
    check_cancelled(cancel);
    if (!encoded || output.bytes.empty()) throw std::runtime_error("WebP encoding failed");
    if (!resolution.dpi_defined) return std::move(output.bytes);
    const WebPData image_data{output.bytes.data(), output.bytes.size()};
    std::unique_ptr<WebPMux, decltype(&WebPMuxDelete)> mux(WebPMuxCreate(&image_data, 1), WebPMuxDelete);
    if (!mux) throw std::runtime_error("Cannot create WebP metadata container");
    const auto exif = detail::resolution_exif(resolution);
    const WebPData chunk{exif.data()+6, exif.size()-6}; // TIFF payload without JPEG's Exif prefix.
    MuxOutput assembled;
    if (WebPMuxSetChunk(mux.get(), "EXIF", &chunk, 1) != WEBP_MUX_OK ||
        WebPMuxAssemble(mux.get(), &assembled.data) != WEBP_MUX_OK ||
        !assembled.data.size || assembled.data.size > output_limit)
        throw std::runtime_error("Cannot write WebP resolution metadata");
    check_cancelled(cancel);
    return {assembled.data.bytes, assembled.data.bytes+assembled.data.size};
}
} // namespace gvn
