#include "gvn/saving.h"
#include "saving_indexed.h"
#include "image_resolution.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <gif_lib.h>
#include <libplum.h>
#include <array>
#include <cstring>
#include <unordered_map>

using Microsoft::WRL::ComPtr;
namespace gvn {
namespace {
constexpr size_t output_limit = 512ull * 1024 * 1024;
void check(HRESULT result, const char *message) {
    if (FAILED(result))
        throw std::runtime_error(message);
}
struct ComScope {
    HRESULT result{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    ComScope() {
        if (FAILED(result) && result != RPC_E_CHANGED_MODE)
            throw std::runtime_error("COM initialization failed");
    }
    ~ComScope() {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};
ComPtr<IWICImagingFactory> factory() {
    ComPtr<IWICImagingFactory> result;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&result)),
          "WIC unavailable");
    return result;
}
Pixel rgb(uint32_t color) {
    return {static_cast<uint8_t>(color >> 16), static_cast<uint8_t>(color >> 8), static_cast<uint8_t>(color),
            255};
}
void validate_image(const Image &image, size_t frame) {
    if (frame >= image.frames.size() ||
        image.frames[frame].pixels.size() != checked_image_pixels(image.width, image.height))
        throw std::invalid_argument("Invalid frame for saving");
}
unsigned capacity(unsigned bits) {
    if (bits != 1 && bits != 4 && bits != 8)
        throw std::invalid_argument("Indexed color depth must be 1, 4 or 8");
    return 1u << bits;
}
void validate_indexed(const IndexedRaster &image) {
    const auto colors = capacity(image.bits);
    if (image.indices.size() != checked_image_pixels(image.width, image.height) || image.palette.empty() ||
        image.palette.size() > colors)
        throw std::invalid_argument("Invalid indexed raster for saving");
    for (auto color : image.palette)
        if (color.a != 255)
            throw std::invalid_argument("Indexed raster palette must be opaque");
    for (auto index : image.indices)
        if (index >= image.palette.size())
            throw std::invalid_argument("Palette index is out of range");
}
uint32_t le32(const uint8_t *data) {
    return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | (uint32_t(data[2]) << 16) |
           (uint32_t(data[3]) << 24);
}
void put32(uint8_t *data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        data[i] = static_cast<uint8_t>(value >> (8 * i));
}
void top_down(std::vector<uint8_t> &bytes, uint32_t width, uint32_t height, unsigned bits) {
    const auto stride = ((static_cast<size_t>(width) * bits + 31) / 32) * 4;
    if (bytes.size() < 54 || bytes[0] != 'B' || bytes[1] != 'M' || le32(bytes.data() + 14) < 40 ||
        le32(bytes.data() + 18) != width || le32(bytes.data() + 22) != height ||
        (le32(bytes.data() + 30) != 0 && !(bits == 16 && le32(bytes.data() + 30) == 3)) ||
        bytes[28] != bits || bytes[29] != 0)
        throw std::runtime_error("Unexpected BMP header from encoder");
    const size_t offset = le32(bytes.data() + 10);
    if (offset > bytes.size() || stride * height > bytes.size() - offset)
        throw std::runtime_error("Incomplete BMP raster from encoder");
    // WIC writes ordinary BMP. Reverse only its complete encoded rows and signed height.
    std::vector<uint8_t> row(stride);
    for (uint32_t y = 0; y < height / 2; ++y) {
        auto first = bytes.data() + offset + stride * y;
        auto last = bytes.data() + offset + stride * (height - 1ull - y);
        std::memcpy(row.data(), first, stride);
        std::memcpy(first, last, stride);
        std::memcpy(last, row.data(), stride);
    }
    put32(bytes.data() + 22, static_cast<uint32_t>(-static_cast<int32_t>(height)));
}
// Some readers drop the final nibble of odd absolute RLE4 blocks. Use an even
// absolute block plus a one-pixel encoded run; both are standard BMP RLE4 commands.
void compatible_rle4(std::vector<uint8_t> &bytes) {
    const size_t offset = le32(bytes.data() + 10);
    if (offset > bytes.size())
        throw std::runtime_error("Invalid RLE4 output offset");
    std::vector<uint8_t> result;
    size_t copied{}, position = offset;
    const auto append = [&](const uint8_t *data, size_t count) {
        if (count > output_limit - result.size())
            throw std::runtime_error("Encoded BMP exceeds the output byte limit");
        result.insert(result.end(), data, data + count);
    };
    const auto pair = [&](uint8_t first, uint8_t second) {
        const uint8_t values[]{first, second};
        append(values, 2);
    };
    while (position < bytes.size()) {
        if (bytes.size() - position < 2)
            throw std::runtime_error("Incomplete RLE4 output command");
        const auto start = position;
        const auto count = bytes[position++], value = bytes[position++];
        if (count || value == 0)
            continue;
        if (value == 1)
            break;
        const size_t size = value == 2 ? 2 : ((size_t(value) + 1) / 2 + 1) & ~size_t{1};
        if (size > bytes.size() - position)
            throw std::runtime_error("Incomplete RLE4 output data");
        if (value > 2 && value % 2) {
            append(bytes.data() + copied, start - copied);
            const auto even = value - 1;
            if (even == 2)
                pair(2, bytes[position]);
            else {
                pair(0, static_cast<uint8_t>(even));
                append(bytes.data() + position, even / 2);
                if ((even / 2) % 2) {
                    const uint8_t zero{};
                    append(&zero, 1);
                }
            }
            pair(1, bytes[position + even / 2] & 0xf0);
            copied = position + size;
        }
        position += size;
    }
    if (!copied)
        return;
    append(bytes.data() + copied, bytes.size() - copied);
    put32(result.data() + 2, static_cast<uint32_t>(result.size()));
    put32(result.data() + 34, static_cast<uint32_t>(result.size() - offset));
    bytes = std::move(result);
}
template <class Row>
std::vector<uint8_t> wic_bmp(uint32_t width, uint32_t height, unsigned bits, const std::vector<Pixel> &colors,
                             Row fill, bool rgb565 = false) {
    ComScope com;
    const auto wic = factory();
    ComPtr<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Cannot create BMP stream");
    ComPtr<IWICBitmapEncoder> encoder;
    check(wic->CreateEncoder(GUID_ContainerFormatBmp, nullptr, &encoder), "BMP encoder unavailable");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "BMP initialization failed");
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(&frame, nullptr), "Cannot create BMP frame");
    check(frame->Initialize(nullptr), "Cannot initialize BMP frame");
    check(frame->SetSize(width, height), "Cannot set BMP size");
    const auto requested = bits == 1   ? GUID_WICPixelFormat1bppIndexed
                           : bits == 4 ? GUID_WICPixelFormat4bppIndexed
                           : bits == 8 ? GUID_WICPixelFormat8bppIndexed
                           : bits == 16 ? (rgb565 ? GUID_WICPixelFormat16bppBGR565 : GUID_WICPixelFormat16bppBGR555)
                                       : GUID_WICPixelFormat24bppBGR;
    auto format = requested;
    check(frame->SetPixelFormat(&format), "Cannot set BMP color depth");
    if (format != requested)
        throw std::runtime_error("BMP encoder changed the requested color depth");
    if (bits <= 8) {
        std::array<WICColor, 256> table{};
        for (size_t i = 0; i < colors.size(); ++i)
            table[i] =
                0xff000000u | (uint32_t(colors[i].r) << 16) | (uint32_t(colors[i].g) << 8) | colors[i].b;
        ComPtr<IWICPalette> palette;
        check(wic->CreatePalette(&palette), "Cannot create BMP palette");
        check(palette->InitializeCustom(table.data(), capacity(bits)), "Cannot initialize BMP palette");
        check(frame->SetPalette(palette.Get()), "Cannot set BMP palette");
    }
    const auto stride = static_cast<UINT>(((static_cast<size_t>(width) * bits + 31) / 32) * 4);
    std::vector<uint8_t> row(stride);
    for (uint32_t y = 0; y < height; ++y) {
        std::fill(row.begin(), row.end(), 0);
        fill(y, row);
        check(frame->WritePixels(1, stride, stride, row.data()), "Cannot write BMP scanline");
    }
    check(frame->Commit(), "Cannot commit BMP frame");
    check(encoder->Commit(), "Cannot commit BMP output");
    STATSTG stat{};
    check(stream->Stat(&stat, STATFLAG_NONAME), "Cannot read BMP output size");
    if (!stat.cbSize.QuadPart || stat.cbSize.QuadPart > output_limit)
        throw std::runtime_error("Encoded BMP exceeds the output byte limit");
    std::vector<uint8_t> bytes(static_cast<size_t>(stat.cbSize.QuadPart));
    check(stream->Seek({}, STREAM_SEEK_SET, nullptr), "Cannot rewind BMP output");
    ULONG count{};
    check(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &count), "Cannot read BMP output");
    if (count != bytes.size())
        throw std::runtime_error("Incomplete BMP output");
    return bytes;
}
struct Output {
    std::vector<uint8_t> bytes;
    bool failed{};
    int append(const uint8_t *data, int count) noexcept {
        if (failed || count < 0 || static_cast<size_t>(count) > output_limit - bytes.size()) {
            failed = true;
            return 0;
        }
        try {
            bytes.insert(bytes.end(), data, data + count);
            return count;
        } catch (...) {
            failed = true;
            return 0;
        }
    }
};
int plum_output(void *context, void *bytes, int count) {
    return static_cast<Output *>(context)->append(static_cast<const uint8_t *>(bytes), count);
}
int gif_output(GifFileType *file, const GifByteType *bytes, int count) {
    return static_cast<Output *>(file->UserData)->append(bytes, count);
}
struct GifContext {
    Output output;
    GifFileType *file{};
    ~GifContext() {
        if (file) {
            int error{};
            EGifCloseFile(file, &error);
        }
    }
    void require(int result) {
        if (result != GIF_OK || output.failed) {
            const auto message = file ? GifErrorString(file->Error) : nullptr;
            throw std::runtime_error(message ? message : "GIF encoding failed");
        }
    }
};
// Literal-only LZW is still framed by GIFLIB. Frequent clears keep a fixed code width;
// no dictionary references are emitted. Subblocks are streamed rather than buffered twice.
struct LiteralCodes {
    GifContext &context;
    std::array<GifByteType, 256> block{};
    unsigned used{}, pending_bits{}, pending{}, code_bits{}, clear{}, literals{};
    bool first{true};
    void flush() {
        if (!used)
            return;
        block[0] = static_cast<GifByteType>(used);
        context.require(first ? EGifPutCode(context.file, code_bits - 1, block.data())
                              : EGifPutCodeNext(context.file, block.data()));
        first = false;
        used = 0;
    }
    void byte(uint8_t value) {
        block[++used] = value;
        if (used == 255)
            flush();
    }
    void code(unsigned value) {
        pending |= value << pending_bits;
        pending_bits += code_bits;
        while (pending_bits >= 8) {
            byte(static_cast<uint8_t>(pending));
            pending >>= 8;
            pending_bits -= 8;
        }
    }
    void pixel(uint8_t value) {
        if (!literals) // At most clear-2 literals avoid a dictionary-width transition.
            code(clear);
        code(value);
        if (++literals == clear - 2)
            literals = 0;
    }
    void finish() {
        code(clear + 1);
        if (pending_bits)
            byte(static_cast<uint8_t>(pending));
        flush();
        context.require(EGifPutCodeNext(context.file, nullptr));
    }
};
} // namespace

IndexedRaster index_image(const Image &image, size_t frame, unsigned bits, uint32_t matte, bool dither) {
    validate_image(image, frame);
    return detail::index_image(image, frame, bits, matte, dither, image_indices(image, frame));
}
IndexedRaster detail::index_image(const Image &image, size_t frame, unsigned bits, uint32_t matte,
                                 bool dither, const std::shared_ptr<const IndexedPixels> &indexed) {
    validate_image(image, frame);
    const auto maximum = capacity(bits);
    if (matte > 0xffffff)
        throw std::invalid_argument("Invalid indexed matte color");
    IndexedRaster result{image.width, image.height, bits};
    result.indices.reserve(image.frames[frame].pixels.size());
    const auto background = rgb(matte);
    if (indexed && indexed->bits <= bits) {
        for (const auto color : indexed->palette)
            result.palette.push_back(over(color, background));
        for (uint32_t y = 0; y < image.height; ++y)
            for (uint32_t x = 0; x < image.width; ++x)
                result.indices.push_back(indexed->index(x, y));
        return result;
    }
    std::unordered_map<uint32_t, uint8_t> exact;
    for (auto pixel : image.frames[frame].pixels) {
        pixel = over(pixel, background);
        const auto key = (uint32_t(pixel.r) << 16) | (uint32_t(pixel.g) << 8) | pixel.b;
        auto found = exact.find(key);
        if (found == exact.end()) {
            if (exact.size() == maximum)
                break;
            const auto index = static_cast<uint8_t>(result.palette.size());
            result.palette.push_back(pixel);
            found = exact.emplace(key, index).first;
        }
        result.indices.push_back(found->second);
    }
    if (result.indices.size() == image.frames[frame].pixels.size())
        return result;
    ComScope com;
    const auto wic = factory();
    ComPtr<IWICBitmap> bitmap;
    check(wic->CreateBitmap(image.width, image.height, GUID_WICPixelFormat24bppBGR, WICBitmapCacheOnLoad,
                            &bitmap),
          "Cannot create quantizer bitmap");
    {
        // Populate WIC's own 24-bit storage directly. Avoid a full temporary RGBA copy
        // followed by CreateBitmapFromMemory's second copy of that temporary.
        ComPtr<IWICBitmapLock> lock;
        const WICRect rectangle{0, 0, static_cast<INT>(image.width), static_cast<INT>(image.height)};
        check(bitmap->Lock(&rectangle, WICBitmapLockWrite, &lock), "Cannot lock quantizer bitmap");
        UINT stride{}, bytes{};
        BYTE *data{};
        check(lock->GetStride(&stride), "Cannot read quantizer stride");
        check(lock->GetDataPointer(&bytes, &data), "Cannot access quantizer pixels");
        if (stride < image.width * 3ull || !data ||
            size_t(stride) * (image.height - 1ull) + image.width * 3ull > bytes)
            throw std::runtime_error("Incomplete quantizer bitmap");
        for (uint32_t y = 0; y < image.height; ++y)
            for (uint32_t x = 0; x < image.width; ++x) {
                const auto pixel = over(image.frames[frame].pixels[size_t(y) * image.width + x], background);
                auto output = data + size_t(y) * stride + size_t(x) * 3;
                output[0] = pixel.b;
                output[1] = pixel.g;
                output[2] = pixel.r;
            }
    }
    ComPtr<IWICPalette> palette;
    check(wic->CreatePalette(&palette), "Cannot create quantizer palette");
    check(palette->InitializeFromBitmap(bitmap.Get(), maximum, FALSE), "Color quantization failed");
    std::array<WICColor, 256> colors{};
    UINT count{};
    check(palette->GetColors(maximum, colors.data(), &count), "Cannot read quantizer palette");
    if (!count || count > maximum)
        throw std::runtime_error("Invalid quantizer palette size");
    result.palette.clear();
    for (UINT i = 0; i < count; ++i)
        result.palette.push_back(rgb(colors[i]));
    ComPtr<IWICFormatConverter> converted;
    check(wic->CreateFormatConverter(&converted), "Cannot create quantizer converter");
    check(converted->Initialize(bitmap.Get(), GUID_WICPixelFormat8bppIndexed,
                                dither ? WICBitmapDitherTypeErrorDiffusion : WICBitmapDitherTypeNone,
                                palette.Get(), 0, WICBitmapPaletteTypeCustom),
          "Cannot convert indexed pixels");
    result.indices.resize(image.frames[frame].pixels.size());
    check(converted->CopyPixels(nullptr, image.width, static_cast<UINT>(result.indices.size()),
                                result.indices.data()),
          "Cannot read indexed pixels");
    validate_indexed(result);
    return result;
}
std::vector<uint8_t> encode_bmp(const Image &image, size_t frame, const BmpSaveOptions &options,const ImageMetadata *metadata) {
    return encode_bmp(image, frame, 24, options, metadata);
}
std::vector<uint8_t> encode_bmp(const Image &image, size_t frame, unsigned bits,
                                const BmpSaveOptions &options, const ImageMetadata *metadata,
                                const Rgb16Colors *preserve) {
    validate_image(image, frame);
    detail::validate_resolution(metadata?*metadata:image.metadata);
    if (bits != 16 && bits != 24)
        throw std::invalid_argument("True-color BMP depth must be 16 or 24");
    if (preserve && (bits != 16 || !preserve->matches(image.frames[frame].pixels)))
        throw std::invalid_argument("Current pixels do not match retained 16-bit colors");
    if (options.matte > 0xffffff)
        throw std::invalid_argument("Invalid BMP matte color");
    const auto background = rgb(options.matte);
    const bool rgb565 = preserve && preserve->layout == Rgb16Layout::Rgb565;
    auto bytes = wic_bmp(image.width, image.height, bits, {}, [&](uint32_t y, auto &row) {
        for (uint32_t x = 0; x < image.width; ++x) {
            const auto pixel = over(image.frames[frame].pixels[y * size_t(image.width) + x], background);
            if (bits == 16) {
                const auto packed = preserve ? (unsigned(preserve->red[pixel.r]) << (rgb565 ? 11 : 10)) |
                                                   (unsigned(preserve->green[pixel.g]) << 5) | preserve->blue[pixel.b]
                                             : (unsigned(pixel.r >> 3) << 10) | (unsigned(pixel.g >> 3) << 5) |
                                                   unsigned(pixel.b >> 3);
                row[x * 2ull] = static_cast<uint8_t>(packed);
                row[x * 2ull + 1] = static_cast<uint8_t>(packed >> 8);
            } else {
                row[x * 3ull] = pixel.b;
                row[x * 3ull + 1] = pixel.g;
                row[x * 3ull + 2] = pixel.r;
            }
        }
    }, rgb565);
    if (options.top_down)
        top_down(bytes, image.width, image.height, bits);
    detail::bmp_resolution(bytes,metadata?*metadata:image.metadata);
    return bytes;
}
std::vector<uint8_t> encode_bmp(const IndexedRaster &image, const BmpSaveOptions &options,const ImageMetadata *metadata) {
    validate_indexed(image);
    if(metadata)detail::validate_resolution(*metadata);
    if (options.matte > 0xffffff)
        throw std::invalid_argument("Invalid BMP matte color");
    if (options.rle && image.bits != 1) {
        std::array<Pixel, 256> colors;
        colors.fill({0, 0, 0, 255});
        std::copy(image.palette.begin(), image.palette.end(), colors.begin());
        plum_image source{};
        source.type = PLUM_IMAGE_BMP;
        source.color_format = PLUM_COLOR_32 | PLUM_ALPHA_INVERT;
        source.width = image.width;
        source.height = image.height;
        source.frames = 1;
        source.max_palette_index = static_cast<uint8_t>(capacity(image.bits) - 1);
        source.palette = colors.data();
        source.data = const_cast<uint8_t *>(image.indices.data());
        Output output;
        plum_callback callback{plum_output, &output};
        unsigned error{};
        if (!plum_store_image(&source, &callback, PLUM_MODE_CALLBACK, &error) || output.failed)
            throw std::runtime_error("BMP RLE encoding failed");
        if (output.bytes.size() >= 54 && le32(output.bytes.data() + 30) == (image.bits == 4 ? 2u : 1u)) {
            if (image.bits == 4)
                compatible_rle4(output.bytes);
            const auto ordinary_size = 54ull + capacity(image.bits) * 4ull +
                                       ((size_t(image.width) * image.bits + 31) / 32) * 4 * image.height;
            if (output.bytes.size() < ordinary_size){
                if(metadata)detail::bmp_resolution(output.bytes,*metadata);
                return std::move(output.bytes); // Compressed BMP must be bottom-up.
            }
        }
    }
    auto bytes = wic_bmp(image.width, image.height, image.bits, image.palette, [&](uint32_t y, auto &row) {
        const auto source = image.indices.data() + y * size_t(image.width);
        for (uint32_t x = 0; x < image.width; ++x) {
            const auto position = size_t(x) * image.bits;
            row[position / 8] |= source[x] << (8 - image.bits - position % 8);
        }
    });
    if (options.top_down)
        top_down(bytes, image.width, image.height, image.bits);
    if(metadata)detail::bmp_resolution(bytes,*metadata);
    return bytes;
}
std::vector<uint8_t> encode_gif(const Image &image, size_t frame, const GifSaveOptions &options,
                                bool dither) {
    validate_image(image, frame);
    if (image.width > 65535 || image.height > 65535)
        throw std::invalid_argument("GIF dimensions exceed the codec limit");
    const auto source = image_indices(image, frame);
    auto raster = detail::index_image(image, frame, 8, options.matte, dither, source);
    if (source && options.transparency != GifTransparency::None) {
        auto index = options.transparent_index;
        if (options.transparency >= GifTransparency::TopLeft) {
            const bool right = options.transparency == GifTransparency::TopRight ||
                               options.transparency == GifTransparency::BottomRight;
            const bool bottom = options.transparency == GifTransparency::BottomLeft ||
                                options.transparency == GifTransparency::BottomRight;
            index = source->index(right ? image.width - 1 : 0, bottom ? image.height - 1 : 0);
        }
        if (index < source->palette.size()) {
            raster.palette[index] = source->palette[index];
            raster.palette[index].a = 255; // Preserve even the hidden RGB of the selected transparent entry.
        }
    }
    return encode_gif(raster, options);
}
std::vector<uint8_t> encode_gif(const IndexedRaster &image, const GifSaveOptions &options) {
    validate_indexed(image);
    if (image.width > 65535 || image.height > 65535 || options.matte > 0xffffff ||
        options.transparent_index > 255 ||
        static_cast<unsigned>(options.transparency) > static_cast<unsigned>(GifTransparency::BottomRight))
        throw std::invalid_argument("Invalid GIF save options or dimensions");
    int transparent = NO_TRANSPARENT_COLOR;
    if (options.transparency == GifTransparency::Index)
        transparent = static_cast<int>(options.transparent_index);
    else if (options.transparency >= GifTransparency::TopLeft) {
        const bool right = options.transparency == GifTransparency::TopRight ||
                           options.transparency == GifTransparency::BottomRight;
        const bool bottom = options.transparency == GifTransparency::BottomLeft ||
                            options.transparency == GifTransparency::BottomRight;
        transparent =
            image
                .indices[(bottom ? image.height - 1ull : 0) * image.width + (right ? image.width - 1ull : 0)];
    }
    unsigned table_size = 2;
    while (table_size < image.palette.size() || static_cast<int>(table_size) <= transparent)
        table_size *= 2;
    std::array<GifColorType, 256> colors{};
    for (size_t i = 0; i < image.palette.size(); ++i)
        colors[i] = {image.palette[i].r, image.palette[i].g, image.palette[i].b};
    const std::unique_ptr<ColorMapObject, decltype(&GifFreeMapObject)> map(
        GifMakeMapObject(table_size, colors.data()), GifFreeMapObject);
    if (!map)
        throw std::bad_alloc();
    GifContext context;
    int error{};
    context.file = EGifOpen(&context.output, gif_output, &error);
    if (!context.file)
        throw std::runtime_error("Cannot create GIF encoder");
    EGifSetGifVersion(context.file, true);
    context.require(
        EGifPutScreenDesc(context.file, image.width, image.height, map->BitsPerPixel, 0, map.get()));
    if (transparent != NO_TRANSPARENT_COLOR) {
        const GraphicsControlBlock control{DISPOSE_DO_NOT, false, 0, transparent};
        std::array<GifByteType, 4> extension{};
        if (EGifGCBToExtension(&control, extension.data()) != extension.size())
            throw std::runtime_error("Cannot create GIF transparency extension");
        context.require(EGifPutExtension(context.file, GRAPHICS_EXT_FUNC_CODE,
                                         static_cast<int>(extension.size()), extension.data()));
    }
    context.require(
        EGifPutImageDesc(context.file, 0, 0, image.width, image.height, options.interlaced, nullptr));
    LiteralCodes literals{context};
    literals.code_bits = std::max(map->BitsPerPixel, 2) + 1;
    literals.clear = 1u << (literals.code_bits - 1);
    std::vector<uint8_t> row(image.width);
    const auto write_row = [&](uint32_t y) {
        const auto source = image.indices.data() + size_t(y) * image.width;
        if (options.uncompressed)
            for (uint32_t x = 0; x < image.width; ++x)
                literals.pixel(source[x]);
        else {
            // GIFLIB masks the supplied buffer, so never pass the caller's indices.
            std::copy_n(source, image.width, row.data());
            context.require(EGifPutLine(context.file, row.data(), image.width));
        }
    };
    if (options.interlaced) {
        static constexpr uint32_t starts[]{0, 4, 2, 1}, steps[]{8, 8, 4, 2};
        for (unsigned pass = 0; pass < 4; ++pass)
            for (uint32_t y = starts[pass]; y < image.height; y += steps[pass])
                write_row(y);
    } else
        for (uint32_t y = 0; y < image.height; ++y)
            write_row(y);
    if (options.uncompressed)
        literals.finish();
    const auto file = context.file;
    context.file = nullptr; // EGifCloseFile frees the handle even on an error.
    context.require(EGifCloseFile(file, &error));
    if (context.output.failed)
        throw std::runtime_error("GIF output allocation failed");
    return std::move(context.output.bytes);
}
} // namespace gvn
