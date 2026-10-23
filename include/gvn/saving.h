#pragma once
#include "core.h"
#include <cstdint>
#include <vector>

namespace gvn {
enum class PngFilter { Adaptive, None, Sub, Up, Average, Paeth, Speed };
enum class PngTransparency { Alpha, Opaque, Color, TopLeft, TopRight, BottomLeft, BottomRight };
struct PngSaveOptions {
    bool interlaced{};
    int compression{6};
    PngFilter filter{PngFilter::Speed};
    PngTransparency transparency{PngTransparency::Alpha};
    uint32_t transparent_color{}, matte{0xffffff}; // RGB, 0xRRGGBB.
    bool operator==(const PngSaveOptions &) const = default;
};
struct JpegSaveOptions {
    int quality{90};
    bool progressive{};
    uint32_t matte{0xffffff};
    bool operator==(const JpegSaveOptions &) const = default;
};
// A save-time indexed raster. Explicit indices preserve duplicate palette entries.
// Viewer RGBA buffers are not changed or retained by these encoders.
struct IndexedRaster {
    uint32_t width{}, height{};
    unsigned bits{8}; // 1, 4 or 8; palette entries are opaque RGB.
    std::vector<Pixel> palette;
    std::vector<uint8_t> indices;
    bool operator==(const IndexedRaster &) const = default;
};
struct BmpSaveOptions {
    bool top_down{}, rle{};
    uint32_t matte{0xffffff};
    bool operator==(const BmpSaveOptions &) const = default;
};
enum class GifTransparency { None, Index, TopLeft, TopRight, BottomLeft, BottomRight };
struct GifSaveOptions {
    bool interlaced{}, uncompressed{};
    GifTransparency transparency{GifTransparency::None};
    unsigned transparent_index{};
    uint32_t matte{0xffffff};
    bool operator==(const GifSaveOptions &) const = default;
};
enum class SaveFormat { Png, Jpeg, Bmp, Gif };
struct SaveOptions {
    PngSaveOptions png;
    JpegSaveOptions jpeg;
    BmpSaveOptions bmp;
    GifSaveOptions gif;
    SaveFormat last_format{SaveFormat::Png};
    unsigned bmp_bits{}; // 0: matching indexed/retained RGB16 depth, otherwise 24; or 1/4/8/16/24.
    bool dither{true}, keep_times{}, uppercase_extension{};
    bool operator==(const SaveOptions &) const = default;
};
std::vector<uint8_t> encode_for_save(const Image &image, size_t frame, SaveFormat format,
                                     const SaveOptions &options,const ImageMetadata *metadata=nullptr);
// Only missing extensions are supplied or uppercased; explicit extensions choose the codec.
std::filesystem::path save_target(std::filesystem::path path, const SaveOptions &options);
SaveFormat save_format(const std::filesystem::path &path);
void save_image(const Image &image, size_t frame, const std::filesystem::path &path,
                const SaveOptions &options, bool overwrite = true,const ImageMetadata *metadata=nullptr);
// Exact colors keep their first-use order. Larger color sets use the WIC quantizer.
IndexedRaster index_image(const Image &image, size_t frame, unsigned bits = 8, uint32_t matte = 0xffffff,
                          bool dither = true);
std::vector<uint8_t> encode_bmp(const Image &image, size_t frame, const BmpSaveOptions &options = {},const ImageMetadata *metadata=nullptr);
// True-color BMP: 16 uses RGB555; 24 retains all composited RGB bytes.
std::vector<uint8_t> encode_bmp(const Image &image, size_t frame, unsigned bits,
                              const BmpSaveOptions &options = {}, const ImageMetadata *metadata = nullptr,
                              const Rgb16Colors *preserve = nullptr);
std::vector<uint8_t> encode_bmp(const IndexedRaster &image, const BmpSaveOptions &options = {},const ImageMetadata *metadata=nullptr);
std::vector<uint8_t> encode_gif(const Image &image, size_t frame, const GifSaveOptions &options = {},
                                bool dither = true);
std::vector<uint8_t> encode_gif(const IndexedRaster &image, const GifSaveOptions &options = {});
// Saving is independent of the display decoder. Default PNG keeps every RGBA byte.
std::vector<uint8_t> encode_png(const Image &image, size_t frame, const PngSaveOptions &options = {},const ImageMetadata *metadata=nullptr);
std::vector<uint8_t> encode_jpeg(const Image &image, size_t frame, const JpegSaveOptions &options = {},const ImageMetadata *metadata=nullptr);
} // namespace gvn
