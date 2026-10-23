#include "gvn/saving.h"
#include "image_resolution.h"
#include <algorithm>
#include <array>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <png.h>
#include <jpeglib.h>

namespace gvn {
namespace {
constexpr size_t output_limit = 512ull * 1024 * 1024;
void validate_image(const Image &image, size_t frame) {
    if (frame >= image.frames.size() ||
        image.frames[frame].pixels.size() != checked_image_pixels(image.width, image.height))
        throw std::invalid_argument("Invalid frame for saving");
}
Pixel rgb(uint32_t value) {
    return {static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value),
            255};
}
void flatten_row(const Pixel *pixels, uint32_t width, Pixel matte, uint8_t *row) {
    for (uint32_t x = 0; x < width; ++x) {
        const auto color = over(pixels[x], matte);
        row[x * 3ull] = color.r;
        row[x * 3ull + 1] = color.g;
        row[x * 3ull + 2] = color.b;
    }
}
// The jump targets contain no C++ objects constructed after setjmp. Mutable state lives
// on the heap, and allocation exceptions in callbacks are caught before png_error.
struct PngContext {
    png_structp png{};
    png_infop info{};
    std::vector<uint8_t> output, row;
    std::array<char, 192> error{};
    std::array<png_color, 256> palette{};
    std::array<png_byte, 256> alpha{};
    ~PngContext() {
        if (png)
            png_destroy_write_struct(&png, info ? &info : nullptr);
    }
};
void png_failure(png_structp png, png_const_charp message) {
    const auto context = static_cast<PngContext *>(png_get_error_ptr(png));
    const auto length = std::min(std::strlen(message), context->error.size() - 1);
    std::memcpy(context->error.data(), message, length);
    context->error[length] = 0;
    png_longjmp(png, 1);
}
void png_warning(png_structp, png_const_charp) {}
void png_output(png_structp png, png_bytep data, png_size_t length) {
    const auto context = static_cast<PngContext *>(png_get_io_ptr(png));
    if (length > output_limit - context->output.size())
        png_error(png, "Encoded PNG exceeds the output byte limit");
    bool appended = true;
    try {
        context->output.insert(context->output.end(), data, data + length);
    } catch (...) {
        appended = false;
    }
    if (!appended)
        png_error(png, "Cannot allocate PNG output");
}
struct JpegError {
    jpeg_error_mgr manager{};
    std::jmp_buf jump;
    std::array<char, JMSG_LENGTH_MAX> message{};
};
void jpeg_failure(j_common_ptr compressor) {
    const auto error = reinterpret_cast<JpegError *>(compressor->err);
    (*compressor->err->format_message)(compressor, error->message.data());
    std::longjmp(error->jump, 1);
}
struct JpegContext {
    jpeg_compress_struct compressor{};
    JpegError error;
    unsigned char *output{};
    unsigned long size{};
    bool created{};
    std::vector<uint8_t> row;
    ~JpegContext() {
        if (created)
            jpeg_destroy_compress(&compressor);
        std::free(output);
    }
};
} // namespace
std::vector<uint8_t> encode_png(const Image &image, size_t frame, const PngSaveOptions &options,const ImageMetadata *metadata) {
    validate_image(image, frame);
    const auto &resolution=metadata?*metadata:image.metadata;detail::validate_resolution(resolution);
    if (options.compression < 0 || options.compression > 9 ||
        static_cast<unsigned>(options.filter) > static_cast<unsigned>(PngFilter::Speed) ||
        static_cast<unsigned>(options.transparency) > static_cast<unsigned>(PngTransparency::BottomRight) ||
        options.transparent_color > 0xffffff || options.matte > 0xffffff)
        throw std::invalid_argument("Invalid PNG save options");
    const auto context = std::make_unique<PngContext>();
    const bool alpha = options.transparency == PngTransparency::Alpha;
    const auto matte = rgb(options.matte);
    const auto &pixels = image.frames[frame].pixels;
    const auto indexed = image_indices(image, frame);
    auto key = rgb(options.transparent_color);
    if (options.transparency >= PngTransparency::TopLeft) {
        const bool right = options.transparency == PngTransparency::TopRight ||
                           options.transparency == PngTransparency::BottomRight;
        const bool bottom = options.transparency == PngTransparency::BottomLeft ||
                            options.transparency == PngTransparency::BottomRight;
        key =
            over(pixels[(bottom ? image.height - 1ull : 0) * image.width + (right ? image.width - 1ull : 0)],
                 matte);
    }
    int transparent_entries{};
    if (indexed)
        for (size_t i = 0; i < indexed->palette.size(); ++i) {
            const auto color = alpha ? indexed->palette[i] : over(indexed->palette[i], matte);
            context->palette[i] = {color.r, color.g, color.b};
            context->alpha[i] = alpha ? color.a : 255;
            if (options.transparency >= PngTransparency::Color && color.r == key.r && color.g == key.g &&
                color.b == key.b)
                context->alpha[i] = 0;
            if (context->alpha[i] != 255)
                transparent_entries = static_cast<int>(i + 1);
        }
    if (!alpha && !indexed)
        context->row.resize(static_cast<size_t>(image.width) * 3);
    context->png = png_create_write_struct(PNG_LIBPNG_VER_STRING, context.get(), png_failure, png_warning);
    if (!context->png)
        throw std::bad_alloc();
    context->info = png_create_info_struct(context->png);
    if (!context->info)
        throw std::bad_alloc();
    if (setjmp(png_jmpbuf(context->png)))
        throw std::runtime_error(std::string("PNG save failed: ") + context->error.data());
    png_set_write_fn(context->png, context.get(), png_output, nullptr);
    png_set_IHDR(context->png, context->info, image.width, image.height, indexed ? indexed->bits : 8,
                 indexed ? PNG_COLOR_TYPE_PALETTE
                 : alpha ? PNG_COLOR_TYPE_RGBA
                         : PNG_COLOR_TYPE_RGB,
                 options.interlaced ? PNG_INTERLACE_ADAM7 : PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    static constexpr int filters[]{PNG_ALL_FILTERS,
                                   PNG_FILTER_NONE,
                                   PNG_FILTER_SUB,
                                   PNG_FILTER_UP,
                                   PNG_FILTER_AVG,
                                   PNG_FILTER_PAETH,
                                   PNG_FILTER_SUB | PNG_FILTER_UP};
    png_set_filter(context->png, PNG_FILTER_TYPE_BASE, filters[static_cast<unsigned>(options.filter)]);
    png_set_compression_level(context->png, options.compression);
    if (indexed) {
        png_set_PLTE(context->png, context->info, context->palette.data(),
                     static_cast<int>(indexed->palette.size()));
        if (transparent_entries)
            png_set_tRNS(context->png, context->info, context->alpha.data(), transparent_entries, nullptr);
    } else if (options.transparency >= PngTransparency::Color) {
        png_color_16 transparent{};
        transparent.red = key.r;
        transparent.green = key.g;
        transparent.blue = key.b;
        png_set_tRNS(context->png, context->info, nullptr, 0, &transparent);
    }
    if(resolution.dpi_defined){const auto [x,y]=detail::resolution_ppm(resolution);png_set_pHYs(context->png,context->info,x,y,PNG_RESOLUTION_METER);}
    png_write_info(context->png, context->info);
    const int passes = png_set_interlace_handling(context->png);
    for (int pass = 0; pass < passes; ++pass)
        for (uint32_t y = 0; y < image.height; ++y) {
            const auto row = image.frames[frame].pixels.data() + static_cast<size_t>(y) * image.width;
            if (indexed)
                png_write_row(context->png, indexed->packed.data() + size_t(y) * indexed->stride());
            else if (alpha)
                png_write_row(context->png, reinterpret_cast<png_const_bytep>(row));
            else {
                flatten_row(row, image.width, matte, context->row.data());
                png_write_row(context->png, context->row.data());
            }
        }
    png_write_end(context->png, context->info);
    return std::move(context->output);
}
std::vector<uint8_t> encode_jpeg(const Image &image, size_t frame, const JpegSaveOptions &options,const ImageMetadata *metadata) {
    validate_image(image, frame);
    const auto &resolution=metadata?*metadata:image.metadata;detail::validate_resolution(resolution);
    if (options.quality < 0 || options.quality > 100 || options.matte > 0xffffff)
        throw std::invalid_argument("Invalid JPEG save options");
    if (image.width > JPEG_MAX_DIMENSION || image.height > JPEG_MAX_DIMENSION)
        throw std::invalid_argument("JPEG dimensions exceed the codec limit");
    const auto context = std::make_unique<JpegContext>();
    const auto matte = rgb(options.matte);
    context->row.resize(static_cast<size_t>(image.width) * 3);
    context->compressor.err = jpeg_std_error(&context->error.manager);
    context->error.manager.error_exit = jpeg_failure;
    if (setjmp(context->error.jump))
        throw std::runtime_error(std::string("JPEG save failed: ") + context->error.message.data());
    context->created = true;
    jpeg_create_compress(&context->compressor);
    jpeg_mem_dest(&context->compressor, &context->output, &context->size);
    context->compressor.image_width = image.width;
    context->compressor.image_height = image.height;
    context->compressor.input_components = 3;
    context->compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&context->compressor);
    if(resolution.dpi_defined){context->compressor.density_unit=1;context->compressor.X_density=static_cast<UINT16>(std::clamp(std::llround(resolution.dpi_x),1ll,65535ll));context->compressor.Y_density=static_cast<UINT16>(std::clamp(std::llround(resolution.dpi_y),1ll,65535ll));}
    jpeg_set_quality(&context->compressor, options.quality, TRUE);
    for (int component = 0; component < 3; ++component) {
        context->compressor.comp_info[component].h_samp_factor = 1;
        context->compressor.comp_info[component].v_samp_factor = 1;
    }
    context->compressor.optimize_coding = TRUE;
    if (options.progressive)
        jpeg_simple_progression(&context->compressor);
    jpeg_start_compress(&context->compressor, TRUE);
    if(resolution.dpi_defined){const auto exif=detail::resolution_exif(resolution);jpeg_write_marker(&context->compressor,JPEG_APP0+1,exif.data(),static_cast<unsigned>(exif.size()));}
    while (context->compressor.next_scanline < image.height) {
        const auto row = image.frames[frame].pixels.data() +
                         static_cast<size_t>(context->compressor.next_scanline) * image.width;
        flatten_row(row, image.width, matte, context->row.data());
        JSAMPROW data = context->row.data();
        if (jpeg_write_scanlines(&context->compressor, &data, 1) != 1)
            throw std::runtime_error("JPEG scanline was not written");
    }
    jpeg_finish_compress(&context->compressor);
    if (!context->size || context->size > output_limit)
        throw std::runtime_error("Invalid encoded JPEG size");
    return {context->output, context->output + context->size};
}
} // namespace gvn
