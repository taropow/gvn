#include "gvn/saving.h"
#include "gvn/codecs.h"
#include "test.h"
#include <miniz.h>
#include <cstring>
#include <fstream>
#include <cmath>
using namespace gvn;
namespace {
Image sample(uint32_t width = 37, uint32_t height = 23) {
    Image image;
    image.width = width;
    image.height = height;
    image.frames.resize(1);
    image.frames[0].pixels.resize(checked_pixels(width, height));
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            image.frames[0].pixels[y * static_cast<size_t>(width) + x] = {
                static_cast<uint8_t>(x * 31 + y * 19), static_cast<uint8_t>(x * 7 + y * 13),
                static_cast<uint8_t>(x * 11 + y * 43), static_cast<uint8_t>(x * 37 + y * 71)};
    return image;
}
uint32_t be32(const uint8_t *data) {
    return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) | (uint32_t(data[2]) << 8) | data[3];
}
struct Chunk {
    std::string type;
    std::vector<uint8_t> data;
};
std::vector<Chunk> chunks(const std::vector<uint8_t> &bytes) {
    CHECK(bytes.size() > 33 && bytes[0] == 137 && std::memcmp(bytes.data() + 1, "PNG\r\n\x1a\n", 7) == 0);
    std::vector<Chunk> result;
    size_t offset = 8;
    while (offset < bytes.size()) {
        CHECK(bytes.size() - offset >= 12);
        const auto size = be32(bytes.data() + offset);
        CHECK(size <= bytes.size() - offset - 12);
        const auto crc = mz_crc32(0, bytes.data() + offset + 4, size + 4);
        CHECK(crc == be32(bytes.data() + offset + 8 + size));
        result.push_back({std::string(reinterpret_cast<const char *>(bytes.data() + offset + 4), 4),
                          {bytes.begin() + offset + 8, bytes.begin() + offset + 8 + size}});
        offset += size + 12;
    }
    CHECK(result.front().type == "IHDR" && result.back().type == "IEND" && offset == bytes.size());
    return result;
}
struct Segment {
    uint8_t marker;
    std::vector<uint8_t> data;
};
std::vector<Segment> jpeg_segments(const std::vector<uint8_t> &bytes) {
    CHECK(bytes.size() > 4 && bytes[0] == 255 && bytes[1] == 0xd8);
    std::vector<Segment> segments;
    size_t offset = 2;
    while (offset < bytes.size()) {
        if (bytes[offset++] != 255)
            continue;
        while (offset < bytes.size() && bytes[offset] == 255)
            ++offset;
        CHECK(offset < bytes.size());
        const auto marker = bytes[offset++];
        if (!marker || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        if (marker == 0xd9) {
            CHECK(offset == bytes.size());
            break;
        }
        CHECK(bytes.size() - offset >= 2);
        const size_t size = (size_t(bytes[offset]) << 8) | bytes[offset + 1];
        CHECK(size >= 2 && size <= bytes.size() - offset);
        segments.push_back({marker, {bytes.begin() + offset + 2, bytes.begin() + offset + size}});
        offset += size;
    }
    return segments;
}
uint64_t squared_error(const Image &a, const Image &b) {
    CHECK(a.width == b.width && a.height == b.height);
    uint64_t sum{};
    for (size_t i = 0; i < a.frames[0].pixels.size(); ++i) {
        const auto x = a.frames[0].pixels[i], y = b.frames[0].pixels[i];
        for (const auto delta : {int(x.r) - y.r, int(x.g) - y.g, int(x.b) - y.b})
            sum += delta * delta;
    }
    return sum;
}
} // namespace
TEST(png_all_compression_filters_interlace_keep_every_rgba_byte) {
    const auto source = sample();
    const auto expected = source.frames[0].pixels;
    for (int level = 0; level <= 9; ++level)
        for (unsigned filter = 0; filter <= 6; ++filter)
            for (bool interlaced : {false, true}) {
                PngSaveOptions options;
                options.compression = level;
                options.filter = static_cast<PngFilter>(filter);
                options.interlaced = interlaced;
                const auto bytes = encode_png(source, 0, options);
                const auto structure = chunks(bytes);
                CHECK(structure[0].data.size() == 13 && structure[0].data[9] == 6);
                CHECK(structure[0].data[12] == static_cast<uint8_t>(interlaced));
                const auto loaded = decode(bytes);
                CHECK(loaded.width == source.width && loaded.height == source.height);
                CHECK(loaded.frames[0].pixels == expected);
                CHECK(source.frames[0].pixels == expected);
            }
}
TEST(png_forced_filters_are_written_in_scanlines_and_compression_changes_stream) {
    const auto source = sample(64, 32);
    for (unsigned filter = 1; filter <= 5; ++filter) {
        PngSaveOptions options;
        options.filter = static_cast<PngFilter>(filter);
        std::vector<uint8_t> compressed;
        for (const auto &chunk : chunks(encode_png(source, 0, options)))
            if (chunk.type == "IDAT")
                compressed.insert(compressed.end(), chunk.data.begin(), chunk.data.end());
        const size_t stride = source.width * 4 + 1;
        std::vector<uint8_t> raw(stride * source.height);
        mz_ulong size = static_cast<mz_ulong>(raw.size());
        CHECK(mz_uncompress(raw.data(), &size, compressed.data(), static_cast<mz_ulong>(compressed.size())) ==
              MZ_OK);
        CHECK(size == raw.size());
        for (size_t y = 0; y < source.height; ++y)
            CHECK(raw[y * stride] == filter - 1);
    }
    auto flat = sample(128, 128);
    std::fill(flat.frames[0].pixels.begin(), flat.frames[0].pixels.end(), Pixel{1, 2, 3, 100});
    PngSaveOptions options;
    options.compression = 0;
    const auto stored = encode_png(flat, 0, options);
    options.compression = 9;
    const auto compressed = encode_png(flat, 0, options);
    CHECK(compressed.size() < stored.size() / 4);
}
TEST(png_alpha_matte_and_rgb_transparency_have_distinct_real_output) {
    auto source = sample(3, 1);
    source.frames[0].pixels = {{255, 0, 0, 255}, {0, 0, 255, 128}, {1, 2, 3, 0}};
    PngSaveOptions options;
    auto alpha = decode(encode_png(source, 0, options));
    CHECK(alpha.frames[0].pixels == source.frames[0].pixels);
    options.transparency = PngTransparency::Opaque;
    options.matte = 0x123456;
    const auto flattened_bytes = encode_png(source, 0, options);
    CHECK(chunks(flattened_bytes)[0].data[9] == 2);
    const auto flat = decode(flattened_bytes);
    const Pixel background{18, 52, 86, 255};
    for (size_t i = 0; i < 3; ++i)
        CHECK(flat.frames[0].pixels[i] == over(source.frames[0].pixels[i], background));
    options.transparency = PngTransparency::Color;
    options.transparent_color = 0xff0000;
    const auto color_bytes = encode_png(source, 0, options);
    const auto structure = chunks(color_bytes);
    const auto trns =
        std::find_if(structure.begin(), structure.end(), [](const auto &c) { return c.type == "tRNS"; });
    CHECK(trns != structure.end() && trns->data == std::vector<uint8_t>({0, 255, 0, 0, 0, 0}));
    const auto keyed = decode(color_bytes);
    CHECK(keyed.frames[0].pixels[0].a == 0 && keyed.frames[0].pixels[1] == flat.frames[0].pixels[1]);
    CHECK(keyed.frames[0].pixels[2] == background);
}
TEST(png_speed_and_corner_presets_match_their_saved_structure_and_pixels) {
    CHECK(PngSaveOptions{}.filter == PngFilter::Speed && JpegSaveOptions{}.quality == 90);
    auto source = sample(32, 17);
    PngSaveOptions options;
    std::vector<uint8_t> packed;
    for (const auto &c : chunks(encode_png(source, 0, options)))
        if (c.type == "IDAT")
            packed.insert(packed.end(), c.data.begin(), c.data.end());
    const size_t stride = source.width * 4 + 1;
    std::vector<uint8_t> raw(stride * source.height);
    mz_ulong size = static_cast<mz_ulong>(raw.size());
    CHECK(mz_uncompress(raw.data(), &size, packed.data(), static_cast<mz_ulong>(packed.size())) == MZ_OK);
    for (size_t y = 0; y < source.height; ++y)
        CHECK(raw[y * stride] == 1 || raw[y * stride] == 2);
    source = sample(2, 2);
    source.frames[0].pixels = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 128}, {1, 2, 3, 0}};
    const Pixel matte{255, 255, 255, 255};
    for (unsigned corner = 0; corner < 4; ++corner) {
        options.transparency =
            static_cast<PngTransparency>(static_cast<unsigned>(PngTransparency::TopLeft) + corner);
        const auto color = over(source.frames[0].pixels[corner], matte);
        const auto bytes = encode_png(source, 0, options);
        const auto structure = chunks(bytes);
        const auto trns =
            std::find_if(structure.begin(), structure.end(), [](const auto &c) { return c.type == "tRNS"; });
        CHECK(trns != structure.end() &&
              trns->data == std::vector<uint8_t>({0, color.r, 0, color.g, 0, color.b}));
        const auto decoded = decode(bytes);
        for (unsigned i = 0; i < 4; ++i)
            CHECK(decoded.frames[0].pixels[i].a == (i == corner ? 0 : 255));
    }
}
TEST(png_adam7_handles_tiny_narrow_and_current_animation_frame_without_mutating_source) {
    for (const auto size : {std::pair{1u, 1u}, {1u, 13u}, {13u, 1u}, {7u, 11u}}) {
        auto source = sample(size.first, size.second);
        source.frames.push_back(source.frames[0]);
        source.frames[1].pixels[0] = {12, 34, 56, 78};
        PngSaveOptions options;
        options.interlaced = true;
        const auto decoded = decode(encode_png(source, 1, options));
        CHECK(decoded.frames.size() == 1 && decoded.frames[0].pixels == source.frames[1].pixels);
    }
}
TEST(jpeg_quality_progressive_and_444_are_encoded_and_independently_decoded) {
    auto source = sample(128, 96);
    for (auto &pixel : source.frames[0].pixels)
        pixel.a = 255;
    Image low, high;
    for (const auto quality : {0, 1, 15, 50, 93, 95, 100}) {
        std::vector<Pixel> baseline;
        for (const bool progressive : {false, true}) {
            JpegSaveOptions options;
            options.quality = quality;
            options.progressive = progressive;
            const auto bytes = encode_jpeg(source, 0, options);
            const auto structure = jpeg_segments(bytes);
            const auto sof = std::find_if(structure.begin(), structure.end(),
                                          [](const auto &s) { return s.marker == 0xc0 || s.marker == 0xc2; });
            CHECK(sof != structure.end() && sof->marker == (progressive ? 0xc2 : 0xc0));
            CHECK(sof->data.size() == 15 && sof->data[5] == 3);
            CHECK(sof->data[7] == 0x11 && sof->data[10] == 0x11 && sof->data[13] == 0x11);
            CHECK(std::count_if(structure.begin(), structure.end(),
                                [](const auto &s) { return s.marker == 0xda; }) == (progressive ? 10 : 1));
            const auto loaded = decode(bytes);
            CHECK(loaded.width == source.width && loaded.height == source.height);
            if (!progressive)
                baseline = loaded.frames[0].pixels;
            else
                CHECK(loaded.frames[0].pixels == baseline);
            if (quality == 15)
                low = loaded;
            if (quality == 95)
                high = loaded;
        }
    }
    CHECK(squared_error(source, high) * 8 < squared_error(source, low));
}
TEST(jpeg_alpha_is_composited_against_selected_matte_and_source_is_preserved) {
    auto source = sample(16, 16);
    std::fill(source.frames[0].pixels.begin(), source.frames[0].pixels.end(), Pixel{255, 0, 0, 0});
    JpegSaveOptions options;
    options.quality = 100;
    options.matte = 0x123456;
    const auto expected = source.frames[0].pixels;
    const auto loaded = decode(encode_jpeg(source, 0, options));
    for (const auto p : loaded.frames[0].pixels)
        CHECK(std::abs(int(p.r) - 18) <= 1 && std::abs(int(p.g) - 52) <= 1 && std::abs(int(p.b) - 86) <= 1 &&
              p.a == 255);
    CHECK(source.frames[0].pixels == expected);
}
TEST(save_options_and_invalid_frames_are_rejected_before_encoding) {
    auto source = sample();
    for (int level : {-1, 10}) {
        PngSaveOptions o;
        o.compression = level;
        THROWS(encode_png(source, 0, o));
    }
    for (int quality : {-1, 101}) {
        JpegSaveOptions o;
        o.quality = quality;
        THROWS(encode_jpeg(source, 0, o));
    }
    PngSaveOptions png;
    png.filter = static_cast<PngFilter>(99);
    THROWS(encode_png(source, 0, png));
    png = {};
    png.transparency = static_cast<PngTransparency>(99);
    THROWS(encode_png(source, 0, png));
    png = {};
    png.transparent_color = 0x1000000;
    THROWS(encode_png(source, 0, png));
    THROWS(encode_png(source, 9));
    THROWS(encode_jpeg(source, 9));
    source.frames[0].pixels.pop_back();
    THROWS(encode_png(source, 0));
    THROWS(encode_jpeg(source, 0));
    source = sample(65501, 1);
    THROWS(encode_jpeg(source, 0));
}
TEST(output_resolution_preserves_axes_fractional_jpeg_and_png_bmp_quantization) {
    auto source=sample(3,2);const auto unchanged=source.frames[0].pixels;
    for(const auto dpi:{std::pair{300.25,150.5},std::pair{72.,96.},std::pair{.001,.5},std::pair{65535.,65534.5}}) {
        source.metadata.dpi_x=dpi.first;source.metadata.dpi_y=dpi.second;source.metadata.dpi_defined=true;
        for(const auto format:{SaveFormat::Png,SaveFormat::Jpeg,SaveFormat::Bmp}) {
            const auto out=decode(encode_for_save(source,0,format,{}));CHECK(out.metadata.dpi_defined);
            const auto expected=[&](double value){return format==SaveFormat::Jpeg?value:std::max(1.,std::round(value/.0254))*.0254;};
            CHECK(std::abs(out.metadata.dpi_x-expected(dpi.first))<.00002&&std::abs(out.metadata.dpi_y-expected(dpi.second))<.00002);
            if(format==SaveFormat::Png)CHECK(out.frames[0].pixels==unchanged);
            if(format==SaveFormat::Bmp)for(size_t n=0;n<unchanged.size();++n)CHECK(out.frames[0].pixels[n]==over(unchanged[n],{255,255,255,255}));
        }
        CHECK(source.frames[0].pixels==unchanged&&source.metadata.dpi_x==dpi.first);
    }
}
TEST(output_resolution_override_keeps_source_and_indexed_bmp_modes) {
    auto source=sample(128,4);for(auto &p:source.frames[0].pixels)p={10,20,30,255};const auto unchanged=source.frames[0].pixels;
    ImageMetadata metadata;metadata.dpi_x=144.5;metadata.dpi_y=288.25;metadata.dpi_defined=true;
    for(unsigned bits:{1u,4u,8u,24u})for(bool compressed:{false,true}) {
        SaveOptions options;options.bmp_bits=bits;options.bmp.rle=compressed;options.bmp.top_down=!compressed;
        const auto bytes=encode_for_save(source,0,SaveFormat::Bmp,options,&metadata);const auto out=decode(bytes);
        CHECK(out.metadata.dpi_defined&&std::abs(out.metadata.dpi_x-144.5)<.013&&std::abs(out.metadata.dpi_y-288.25)<.013);
        if(compressed&&(bits==4||bits==8))CHECK(bytes[30]==(bits==4?2:1));
    }
    const auto png=decode(encode(source,0,L".png",.93f,&metadata));
    std::cout<<"Interchange PNG dpi="<<png.metadata.dpi_x<<','<<png.metadata.dpi_y<<" defined="<<png.metadata.dpi_defined<<" rgba_equal="<<(png.frames[0].pixels==unchanged)<<'\n';
    CHECK(png.frames[0].pixels==unchanged&&std::abs(png.metadata.dpi_x-144.5)<.013);
    CHECK(!source.metadata.dpi_defined&&source.frames[0].pixels==unchanged);
    const auto gif=decode(encode_for_save(source,0,SaveFormat::Gif,{},&metadata));CHECK(!gif.metadata.dpi_defined&&gif.metadata.dpi_x==72);
}
TEST(output_resolution_invalid_values_fail_without_mutating_source) {
    auto source=sample(3,2);const auto unchanged=source.frames[0].pixels;ImageMetadata metadata;metadata.dpi_defined=true;
    for(const auto bad:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),1e12}) {
        metadata.dpi_x=bad;
        for(const auto format:{SaveFormat::Png,SaveFormat::Jpeg,SaveFormat::Bmp})THROWS(encode_for_save(source,0,format,{},&metadata));
        THROWS(encode(source,0,L".png",.93f,&metadata));
        CHECK(source.frames[0].pixels==unchanged&&!source.metadata.dpi_defined);
    }
}
int main(int argc, char **argv) {
    if(argc==3&&std::string(argv[1])=="--resolution-oracle") {
        try {
            const auto parent=std::filesystem::path(GVN_SOURCE_ROOT)/L".cache/saving-oracle",output=std::filesystem::weakly_canonical(argv[2]);
            CHECK(output.parent_path()==parent&&output.filename().wstring().starts_with(L"run-")&&std::filesystem::is_empty(output));
            auto source=sample(3,2);const auto write=[&](const auto &name,const std::vector<uint8_t> &bytes){std::ofstream f(output/name,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.close();CHECK(f.good());};
            write("source.rgba",std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(source.frames[0].pixels.data()),reinterpret_cast<const uint8_t*>(source.frames[0].pixels.data()+source.frames[0].pixels.size())));
            for(int n=0;n<2;++n){source.metadata.dpi_defined=true;source.metadata.dpi_x=n?.001:73.25;source.metadata.dpi_y=n?65535.:144.75;
                write(std::to_string(n)+".png",encode_png(source,0));write(std::to_string(n)+".jpg",encode_jpeg(source,0));write(std::to_string(n)+".bmp",encode_bmp(source,0));
                if(!n)write("interchange.png",encode(source,0,L".png"));}
            std::cout<<"7 resolution files and source RGBA written\n";return 0;
        }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
    }
    if (argc == 3 && std::string(argv[1]) == "--oracle") {
        try {
            const auto root =
                (std::filesystem::path(GVN_SOURCE_ROOT) / L".cache/saving-oracle").lexically_normal();
            const auto output = std::filesystem::weakly_canonical(argv[2]);
            CHECK(output.parent_path() == root && output.filename().wstring().starts_with(L"run-"));
            CHECK(std::filesystem::is_directory(output) && std::filesystem::is_empty(output));
            auto source = sample(37, 23);
            source.frames[0].pixels[0] = {255, 0, 0, 255};
            source.frames[0].pixels[1] = {0, 0, 255, 128};
            source.frames[0].pixels[2] = {1, 2, 3, 0};
            const auto unchanged = source.frames[0].pixels;
            const auto write = [&](const std::string &name, const uint8_t *data, size_t size) {
                const auto path = output / name;
                CHECK(!std::filesystem::exists(path));
                std::ofstream stream(path, std::ios::binary);
                stream.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
                stream.close();
                CHECK(stream.good());
            };
            write("source.rgba", reinterpret_cast<const uint8_t *>(unchanged.data()),
                  unchanged.size() * sizeof(Pixel));
            for (int level = 0; level <= 9; ++level)
                for (unsigned filter = 0; filter <= 6; ++filter)
                    for (bool interlaced : {false, true}) {
                        PngSaveOptions options;
                        options.compression = level;
                        options.filter = static_cast<PngFilter>(filter);
                        options.interlaced = interlaced;
                        const auto bytes = encode_png(source, 0, options);
                        write("png-" + std::to_string(level) + "-" + std::to_string(filter) + "-" +
                                  std::to_string(interlaced) + ".png",
                              bytes.data(), bytes.size());
                    }
            PngSaveOptions png;
            png.transparency = PngTransparency::Opaque;
            png.matte = 0x123456;
            auto bytes = encode_png(source, 0, png);
            write("opaque.png", bytes.data(), bytes.size());
            png.transparency = PngTransparency::Color;
            png.transparent_color = 0xff0000;
            bytes = encode_png(source, 0, png);
            write("color.png", bytes.data(), bytes.size());
            for (unsigned corner = 0; corner < 4; ++corner) {
                png.transparency =
                    static_cast<PngTransparency>(static_cast<unsigned>(PngTransparency::TopLeft) + corner);
                bytes = encode_png(source, 0, png);
                write("corner-" + std::to_string(corner) + ".png", bytes.data(), bytes.size());
            }
            for (int quality : {0, 1, 15, 50, 93, 95, 100})
                for (bool progressive : {false, true}) {
                    JpegSaveOptions jpeg;
                    jpeg.quality = quality;
                    jpeg.progressive = progressive;
                    jpeg.matte = 0x123456;
                    bytes = encode_jpeg(source, 0, jpeg);
                    write("jpeg-" + std::to_string(quality) + "-" + std::to_string(progressive) + ".jpg",
                          bytes.data(), bytes.size());
                }
            CHECK(source.frames[0].pixels == unchanged);
            std::cout << "{\"width\":37,\"height\":23,\"source_unchanged\":true,\"png_options\":140,\"png_"
                         "transparency\":6,\"jpeg_options\":14}\n";
            return 0;
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    return run_tests();
}
