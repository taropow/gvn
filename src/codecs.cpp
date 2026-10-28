#include "gvn/codecs.h"
#include "gvn/saving.h"
#include "loading.h"
#include "indexed_loading.h"
#include "saving_file.h"
#include "image_metadata.h"
#include "image_resolution.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <shlwapi.h>
#include <propvarutil.h>
#include <chrono>
#include <fstream>
#include <cstring>
#include <cwctype>

using Microsoft::WRL::ComPtr;
namespace gvn {
namespace {
void check(HRESULT hr, const char *message) {
    if (FAILED(hr))
        throw std::runtime_error(message);
}
struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
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
    ComPtr<IWICImagingFactory> value;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&value)),
          "WIC unavailable");
    return value;
}
uint32_t metadata_number(IWICMetadataQueryReader *reader, const wchar_t *name, uint32_t initial = 0) {
    if (!reader)
        return initial;
    PROPVARIANT value;
    PropVariantInit(&value);
    auto result = initial;
    if (SUCCEEDED(reader->GetMetadataByName(name, &value))) {
        if (value.vt == VT_UI1)
            result = value.bVal;
        else if (value.vt == VT_UI2)
            result = value.uiVal;
        else if (value.vt == VT_UI4)
            result = value.ulVal;
        else if (value.vt == VT_BOOL)
            result = value.boolVal != VARIANT_FALSE;
    }
    PropVariantClear(&value);
    return result;
}
std::optional<Pixel> png_background(IWICImagingFactory *wic, IWICBitmapFrameDecode *source,
                                    IWICMetadataQueryReader *reader, unsigned depth) {
    if (!reader || depth == 0 || depth > 16)
        return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    std::optional<Pixel> result;
    if (SUCCEEDED(reader->GetMetadataByName(L"/bKGD/BackgroundColor", &value))) {
        const uint32_t maximum = (1u << depth) - 1;
        const auto channel = [&](uint32_t v) {
            return static_cast<uint8_t>((v * 255 + maximum / 2) / maximum);
        };
        if (value.vt == VT_UI2 && value.uiVal <= maximum) {
            const auto gray = channel(value.uiVal);
            result = Pixel{gray, gray, gray, 255};
        } else if (value.vt == (VT_VECTOR | VT_UI2) && value.caui.cElems == 3 && value.caui.pElems &&
                   value.caui.pElems[0] <= maximum && value.caui.pElems[1] <= maximum &&
                   value.caui.pElems[2] <= maximum) {
            result = Pixel{channel(value.caui.pElems[0]), channel(value.caui.pElems[1]),
                           channel(value.caui.pElems[2]), 255};
        } else if (value.vt == VT_UI1) {
            ComPtr<IWICPalette> palette;
            WICColor colors[256]{};
            UINT count{};
            if (SUCCEEDED(wic->CreatePalette(&palette)) && SUCCEEDED(source->CopyPalette(palette.Get())) &&
                SUCCEEDED(palette->GetColors(256, colors, &count)) && value.bVal < count) {
                const auto color = colors[value.bVal];
                result = Pixel{static_cast<uint8_t>(color >> 16), static_cast<uint8_t>(color >> 8),
                               static_cast<uint8_t>(color), 255};
            }
        }
    }
    PropVariantClear(&value);
    return result;
}
} // namespace
std::vector<uint8_t> read_file(const std::filesystem::path &path, size_t source_limit,const CancelCheck &cancel) {
    check_cancelled(cancel);
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("Cannot open file");
    const auto length = stream.tellg();
    if (length <= 0 || static_cast<uint64_t>(length) > std::min(kSourceLimit, source_limit))
        throw std::runtime_error("File is empty or exceeds the source byte limit");
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    stream.seekg(0);
    for(size_t at=0;at<bytes.size();){
        check_cancelled(cancel);const auto count=std::min<size_t>(1024*1024,bytes.size()-at);
        if(!stream.read(reinterpret_cast<char *>(bytes.data()+at),count))throw std::runtime_error("File read failed");
        at+=count;
    }
    check_cancelled(cancel);
    return bytes;
}
Image decode(std::span<const uint8_t> bytes, const std::wstring &, size_t limit,
             const LoadingOptions &options,const CancelCheck &cancel) {
    check_cancelled(cancel);
    if (bytes.empty() || bytes.size() > 512ull * 1024 * 1024)
        throw std::runtime_error("Invalid source size");
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "8BPS", 4) == 0)
        return decode_psd(bytes, limit,cancel);
    if (bytes.size() >= 54 && bytes[0] == 'B' && bytes[1] == 'M' && bytes[29] == 0 &&
        ((bytes[28] == 4 && bytes[30] == 2) || (bytes[28] == 8 && bytes[30] == 1)) && bytes[31] == 0 &&
        bytes[32] == 0 && bytes[33] == 0) {
        auto image = read_indexed_rle_bmp(bytes, limit);
        check_cancelled(cancel);
        read_container_metadata(image,bytes);
        finish_loading(image, options);
        return image;
    }
    if (is_apng(bytes))
        return decode_animation(bytes, limit, options,cancel);
    check_cancelled(cancel);
    ComScope com;
    auto wic = factory();
    ComPtr<IWICStream> stream;
    check(wic->CreateStream(&stream), "Create image stream failed");
    check(stream->InitializeFromMemory(const_cast<BYTE *>(bytes.data()), static_cast<DWORD>(bytes.size())),
          "Invalid image stream");
    ComPtr<IWICBitmapDecoder> decoder;
    check(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder),
          "Unsupported or corrupt image");
    GUID format{};
    decoder->GetContainerFormat(&format);
    if (format == GUID_ContainerFormatGif && options.gif_animation) {
        UINT count{};
        check(decoder->GetFrameCount(&count), "Cannot read GIF frame count");
        if (count > 1) {
            decoder.Reset();
            stream.Reset();
            wic.Reset();
            return decode_animation(bytes, limit, options,cancel);
        }
    }
    ComPtr<IWICBitmapFrameDecode> source;
    check(decoder->GetFrame(0, &source), "Image has no frame");
    ComPtr<IWICBitmapSource> input = source;
    UINT width{}, height{};
    check(source->GetSize(&width, &height), "Invalid image dimensions");
    checked_pixels(width, height, 1, limit);
    ComPtr<IWICMetadataQueryReader> metadata;
    source->GetMetadataQueryReader(&metadata);
    bool swapped_axes{};
    if (options.exif_orientation && format == GUID_ContainerFormatJpeg && metadata) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value)) && value.vt == VT_UI2) {
            static const WICBitmapTransformOptions transforms[] = {
                WICBitmapTransformRotate0,
                WICBitmapTransformRotate0,
                WICBitmapTransformFlipHorizontal,
                WICBitmapTransformRotate180,
                WICBitmapTransformFlipVertical,
                static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 |
                                                       WICBitmapTransformFlipHorizontal),
                WICBitmapTransformRotate90,
                static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 |
                                                       WICBitmapTransformFlipHorizontal),
                WICBitmapTransformRotate270};
            if (value.uiVal > 1 && value.uiVal < 9) {
                ComPtr<IWICBitmapFlipRotator> rotate;
                check(wic->CreateBitmapFlipRotator(&rotate), "Rotation unavailable");
                check(rotate->Initialize(source.Get(), transforms[value.uiVal]), "EXIF rotation failed");
                input = rotate;
                swapped_axes=value.uiVal>=5;
                input->GetSize(&width, &height);
            }
        }
        PropVariantClear(&value);
    }
    check_cancelled(cancel);
    Image image;
    image.width = width;
    image.height = height;
    image.frames.resize(1);
    image.frames[0].pixels.resize(checked_pixels(width, height, 1, limit));
    bool indexed_canvas = true;
    if (format == GUID_ContainerFormatGif) {
        ComPtr<IWICMetadataQueryReader> screen;
        decoder->GetMetadataQueryReader(&screen);
        indexed_canvas = metadata_number(screen.Get(), L"/logscrdesc/Width", width) == width &&
                         metadata_number(screen.Get(), L"/logscrdesc/Height", height) == height &&
                         !metadata_number(metadata.Get(), L"/imgdesc/Left") &&
                         !metadata_number(metadata.Get(), L"/imgdesc/Top");
    }
    if (input.Get() == source.Get())
        image.frames[0].indexed = read_indexed(wic.Get(), source.Get(), width, height, limit, !indexed_canvas);
    if (const auto &indexed = image.frames[0].indexed) {
        expand_indexed(*indexed, image.frames[0].pixels);
    } else {
        ComPtr<IWICFormatConverter> converter;
        check(wic->CreateFormatConverter(&converter), "Converter unavailable");
        check(converter->Initialize(input.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                                    nullptr, 0, WICBitmapPaletteTypeCustom),
              "Cannot convert image to RGBA");
        if(cancel){
            for(UINT y=0;y<height;y+=256){
                check_cancelled(cancel);const UINT rows=std::min(256u,height-y);
                const WICRect region{0,static_cast<INT>(y),static_cast<INT>(width),static_cast<INT>(rows)};
                check(converter->CopyPixels(&region,width*4,width*rows*4,
                    reinterpret_cast<BYTE *>(image.frames[0].pixels.data()+size_t(y)*width)),"Image pixel decode failed");
            }
        }else check(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(image.frames[0].pixels.size()*4),
                     reinterpret_cast<BYTE *>(image.frames[0].pixels.data())),"Image pixel decode failed");
    }
    image.format = format == GUID_ContainerFormatPng    ? L"PNG"
                   : format == GUID_ContainerFormatJpeg ? L"JPEG"
                   : format == GUID_ContainerFormatGif  ? L"GIF"
                   : format == GUID_ContainerFormatBmp  ? L"BMP"
                   : format == GUID_ContainerFormatTiff ? L"TIFF"
                                                        : L"ICO";
    std::optional<Pixel> background;
    if (format == GUID_ContainerFormatPng && options.png_background && !options.preserve_alpha &&
        bytes.size() > 24)
        background = png_background(wic.Get(), source.Get(), metadata.Get(), bytes[24]);
    if (format == GUID_ContainerFormatGif) {
        ComPtr<IWICMetadataQueryReader> screen;
        decoder->GetMetadataQueryReader(&screen);
        ComPtr<IWICPalette> palette;
        WICColor colors[256]{};
        UINT count{};
        const auto index = metadata_number(screen.Get(), L"/logscrdesc/BackgroundColorIndex", 256);
        if (index < 256 && SUCCEEDED(wic->CreatePalette(&palette)) &&
            SUCCEEDED(decoder->CopyPalette(palette.Get())) &&
            SUCCEEDED(palette->GetColors(256, colors, &count)) && index < count) {
            const auto color = colors[index];
            background = Pixel{static_cast<uint8_t>(color >> 16), static_cast<uint8_t>(color >> 8),
                               static_cast<uint8_t>(color), 255};
        }
        const auto logical_width = metadata_number(screen.Get(), L"/logscrdesc/Width", width),
                   logical_height = metadata_number(screen.Get(), L"/logscrdesc/Height", height),
                   left = metadata_number(metadata.Get(), L"/imgdesc/Left"),
                   top = metadata_number(metadata.Get(), L"/imgdesc/Top");
        checked_pixels(logical_width, logical_height, 1, limit);
        if (logical_width != width || logical_height != height || left || top) {
            Pixel clear = background.value_or(Pixel{});
            if (metadata_number(metadata.Get(), L"/grctlext/TransparencyFlag"))
                clear.a = 0;
            std::vector<Pixel> canvas(checked_pixels(logical_width, logical_height, 1, limit), clear);
            for (uint32_t y = 0; y < height && uint64_t(y) + top < logical_height; ++y)
                for (uint32_t x = 0; x < width && uint64_t(x) + left < logical_width; ++x)
                    canvas[(y + top) * static_cast<size_t>(logical_width) + x + left] =
                        image.frames[0].pixels[y * static_cast<size_t>(width) + x];
            if (const auto &indexed = image.frames[0].indexed)
                image.frames[0].indexed = indexed_logical_screen(
                    *indexed, logical_width, logical_height, left, top, clear,
                    metadata_number(metadata.Get(), L"/imgdesc/LocalColorTableFlag") ? 256 : index, limit);
            image.frames[0].pixels = std::move(canvas);
            image.width = logical_width;
            image.height = logical_height;
        }
    }
    check_cancelled(cancel);
    read_container_metadata(image,bytes);
    GUID pixel_format{};
    ComPtr<IWICComponentInfo> component;
    ComPtr<IWICPixelFormatInfo> pixel_info;
    if(SUCCEEDED(source->GetPixelFormat(&pixel_format))) {
        if(image.metadata.original_color_type==ImageColorType::Unknown) {
            if(pixel_format==GUID_WICPixelFormat1bppIndexed||pixel_format==GUID_WICPixelFormat2bppIndexed||pixel_format==GUID_WICPixelFormat4bppIndexed||pixel_format==GUID_WICPixelFormat8bppIndexed)image.metadata.original_color_type=ImageColorType::Palette;
            else if(pixel_format==GUID_WICPixelFormatBlackWhite||pixel_format==GUID_WICPixelFormat2bppGray||pixel_format==GUID_WICPixelFormat4bppGray||pixel_format==GUID_WICPixelFormat8bppGray||pixel_format==GUID_WICPixelFormat16bppGray||pixel_format==GUID_WICPixelFormat16bppGrayFixedPoint||pixel_format==GUID_WICPixelFormat16bppGrayHalf||pixel_format==GUID_WICPixelFormat32bppGrayFloat||pixel_format==GUID_WICPixelFormat32bppGrayFixedPoint)image.metadata.original_color_type=ImageColorType::Gray;
            else if(pixel_format==GUID_WICPixelFormat32bppCMYK||pixel_format==GUID_WICPixelFormat64bppCMYK||pixel_format==GUID_WICPixelFormat40bppCMYKAlpha||pixel_format==GUID_WICPixelFormat80bppCMYKAlpha)image.metadata.original_color_type=ImageColorType::Cmyk;
            else image.metadata.original_color_type=ImageColorType::Rgb;
        }
        if(!image.metadata.original_bpp&&SUCCEEDED(wic->CreateComponentInfo(pixel_format,&component))&&SUCCEEDED(component.As(&pixel_info))) {
            UINT bits{};if(SUCCEEDED(pixel_info->GetBitsPerPixel(&bits)))image.metadata.original_bpp=bits;
        }
    }
    // EXIF/TIFF physical resolution is valid only when both rational tags and their unit exist.
    const auto rational=[&](const std::wstring &name)->double {
        PROPVARIANT value;PropVariantInit(&value);double result{};
        if(metadata&&SUCCEEDED(metadata->GetMetadataByName(name.c_str(),&value))&&value.vt==VT_UI8) {
            const auto denominator=value.uhVal.HighPart;if(denominator)result=value.uhVal.LowPart/double(denominator);
        }
        PropVariantClear(&value);return result;
    };
    if(format==GUID_ContainerFormatJpeg||format==GUID_ContainerFormatTiff) {
        const std::wstring prefix=format==GUID_ContainerFormatJpeg?L"/app1/ifd/":L"/ifd/";
        const auto unit=metadata_number(metadata.Get(),(prefix+L"{ushort=296}").c_str(),2);
        if(unit==2||unit==3)set_image_resolution(image.metadata,rational(prefix+L"{ushort=282}")*(unit==2?1.:2.54),rational(prefix+L"{ushort=283}")*(unit==2?1.:2.54));
    }
    UINT contexts{};
    if(SUCCEEDED(source->GetColorContexts(0,nullptr,&contexts))&&contexts&&contexts<=16) {
        std::vector<ComPtr<IWICColorContext>> owned(contexts);std::vector<IWICColorContext*> raw(contexts);
        bool ready=true;
        for(UINT n=0;n<contexts;++n){if(FAILED(wic->CreateColorContext(&owned[n]))){ready=false;break;}raw[n]=owned[n].Get();}
        UINT actual{};
        if(ready&&SUCCEEDED(source->GetColorContexts(contexts,raw.data(),&actual)))for(auto *context:raw) {
            WICColorContextType type{};UINT count{};
            if(SUCCEEDED(context->GetType(&type))&&type==WICColorContextProfile&&SUCCEEDED(context->GetProfileBytes(0,nullptr,&count))&&count) {
                image.metadata.icc_profile=true;image.metadata.icc_bytes=count;break;
            }
        }
    }
    if(swapped_axes)std::swap(image.metadata.dpi_x,image.metadata.dpi_y);
    if(format==GUID_ContainerFormatBmp&&options.bitfield_to24)normalize_bmp_bitfields(image,bytes);
    finish_loading(image, options, background);
    if (format == GUID_ContainerFormatBmp && !options.bitfield_to24 &&
        (pixel_format == GUID_WICPixelFormat16bppBGR555 || pixel_format == GUID_WICPixelFormat16bppBGR565))
        retain_bmp16_colors(image, bytes, limit, pixel_format == GUID_WICPixelFormat16bppBGR565
                                                   ? Rgb16Layout::Rgb565 : Rgb16Layout::Rgb555);
    return image;
}
Image load_image(const std::filesystem::path &path, size_t limit, const LoadingOptions &options,
                 size_t source_limit,const CancelCheck &cancel) {
    const auto start = std::chrono::steady_clock::now();
    auto bytes = read_file(path, source_limit,cancel);
    auto image = decode(bytes, path.extension().wstring(), limit, options,cancel);
    check_cancelled(cancel);
    image.path = std::filesystem::absolute(path);
    image.file_bytes = bytes.size();
    image.load_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count());
    image.metadata.source_load_ms=image.load_ms;
    return image;
}
std::vector<uint8_t> encode(const Image &image, size_t frame, const std::wstring &ext, float quality,const ImageMetadata *metadata) {
    const auto &resolution=metadata?*metadata:image.metadata;
    detail::validate_resolution(resolution);
    if (frame >= image.frames.size() ||
        image.frames[frame].pixels.size() != checked_image_pixels(image.width, image.height))
        throw std::runtime_error("Invalid image for saving");
    auto extension = ext;
    for (auto &c : extension)
        c = static_cast<wchar_t>(std::towlower(c));
    const GUID container = extension == L".png"                            ? GUID_ContainerFormatPng
                           : extension == L".jpg" || extension == L".jpeg" ? GUID_ContainerFormatJpeg
                           : extension == L".bmp"                          ? GUID_ContainerFormatBmp
                           : extension == L".gif"                          ? GUID_ContainerFormatGif
                                                                           : GUID_NULL;
    if (container == GUID_NULL)
        throw std::runtime_error("Save supports PNG, JPEG, BMP and GIF");
    // Use the normal PNG writer for density and retained source numbers. WIC's
    // RGBA encoder cannot retain duplicate or unused entries of an indexed image.
    if(container==GUID_ContainerFormatPng&&
       (resolution.dpi_defined||image.frames[frame].indexed||image.indexed_source))
        return encode_png(image,frame,{},&resolution);
    ComScope com;
    auto wic = factory();
    ComPtr<IStream> stream;
    check(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Cannot create output stream");
    ComPtr<IWICBitmapEncoder> encoder;
    check(wic->CreateEncoder(container, nullptr, &encoder), "Encoder unavailable");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Encoder initialization failed");
    ComPtr<IWICBitmapFrameEncode> output;
    ComPtr<IPropertyBag2> options;
    check(encoder->CreateNewFrame(&output, &options), "Cannot create output frame");
    if (container == GUID_ContainerFormatJpeg) {
        PROPBAG2 property{};
        property.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = std::clamp(quality, 0.01f, 1.0f);
        options->Write(1, &property, &value);
    }
    check(output->Initialize(options.Get()), "Output frame initialization failed");
    check(output->SetSize(image.width, image.height), "Cannot set output size");
    if(resolution.dpi_defined&&container!=GUID_ContainerFormatGif)
        check(output->SetResolution(resolution.dpi_x,resolution.dpi_y),"Cannot set output resolution");
    auto format = container == GUID_ContainerFormatPng   ? GUID_WICPixelFormat32bppRGBA
                  : container == GUID_ContainerFormatGif ? GUID_WICPixelFormat8bppIndexed
                                                         : GUID_WICPixelFormat24bppBGR;
    check(output->SetPixelFormat(&format), "Output pixel format unavailable");
    auto pixels = image.frames[frame].pixels;
    if (container != GUID_ContainerFormatPng && container != GUID_ContainerFormatGif)
        for (auto &p : pixels)
            p = over(p, {255, 255, 255, 255});
    ComPtr<IWICBitmap> bitmap;
    check(wic->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppRGBA,
                                      image.width * 4, static_cast<UINT>(pixels.size() * 4),
                                      reinterpret_cast<BYTE *>(pixels.data()), &bitmap),
          "Cannot create output bitmap");
    if(resolution.dpi_defined&&container!=GUID_ContainerFormatGif)
        check(bitmap->SetResolution(resolution.dpi_x,resolution.dpi_y),"Cannot set bitmap resolution");
    ComPtr<IWICPalette> palette;
    if (container == GUID_ContainerFormatGif) {
        check(wic->CreatePalette(&palette), "Palette unavailable");
        check(palette->InitializeFromBitmap(bitmap.Get(), 256, TRUE), "Palette generation failed");
        output->SetPalette(palette.Get());
    }
    ComPtr<IWICFormatConverter> converted;
    check(wic->CreateFormatConverter(&converted), "Output converter unavailable");
    check(converted->Initialize(bitmap.Get(), format, WICBitmapDitherTypeErrorDiffusion, palette.Get(), 0,
                                WICBitmapPaletteTypeCustom),
          "Output conversion failed");
    check(output->WriteSource(converted.Get(), nullptr), "Output encoding failed");
    check(output->Commit(), "Output frame commit failed");
    check(encoder->Commit(), "Output commit failed");
    STATSTG stat{};
    check(stream->Stat(&stat, STATFLAG_NONAME), "Cannot read output size");
    std::vector<uint8_t> bytes(static_cast<size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG count{};
    check(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &count), "Cannot read encoded bytes");
    if (count != bytes.size())
        throw std::runtime_error("Incomplete encoded image");
    return bytes;
}
void save_image(const Image &image, size_t frame, const std::filesystem::path &path, float quality,
                bool overwrite,const ImageMetadata *metadata) {
    const auto bytes = encode(image, frame, path.extension().wstring(), quality,metadata);
    detail::write_encoded_image(bytes, path, overwrite);
}
} // namespace gvn
