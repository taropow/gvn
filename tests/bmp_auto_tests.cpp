#include "gvn/saving.h"
#include "gvn/codecs.h"
#include "gvn/edit.h"
#include "../src/image_metadata.h"
#include "test.h"
#include <array>
#include <fstream>
using namespace gvn;
namespace {
const auto fixture_root = std::filesystem::path(GVN_SOURCE_ROOT) / L"tests/fixtures/auto-bmp";
const std::array<const char *, 4> names{"rgb555.bmp", "rgb555-fields.bmp", "rgb565.bmp", "rgb565-v4.bmp"};
uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
std::vector<uint8_t> bytes(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    CHECK(stream.good());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
std::vector<uint16_t> codes(const std::vector<uint8_t> &data) {
    CHECK(data.size() >= 54 && data[28] == 16);
    const auto width = le32(data.data() + 18);
    const auto height = int32_t(le32(data.data() + 22));
    const size_t count = height < 0 ? -int64_t(height) : height;
    const size_t offset = le32(data.data() + 10), stride = (width * 2ull + 3) & ~size_t{3};
    CHECK(offset <= data.size() && count * stride <= data.size() - offset);
    std::vector<uint16_t> result;
    for (size_t y = 0; y < count; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const auto p = data.data() + offset + (height < 0 ? y : count - 1 - y) * stride + size_t(x) * 2;
            result.push_back(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
        }
    return result;
}
std::vector<uint8_t> automatic(const Image &image, bool top = false) {
    SaveOptions options;
    options.bmp.top_down = top;
    options.bmp.rle = true; // Applies only to 4/8bpp; never to retained true color.
    return encode_for_save(image, 0, SaveFormat::Bmp, options);
}
void roundtrip(const Image &image) {
    const auto encoded = automatic(image, true);
    CHECK(encoded[28] == 16);
    const bool rgb565 = image.rgb16->layout == Rgb16Layout::Rgb565;
    CHECK(le32(encoded.data() + 30) == (rgb565 ? 3u : 0u));
    if (rgb565)
        CHECK(le32(encoded.data() + 54) == 0xf800 && le32(encoded.data() + 58) == 0x7e0 &&
              le32(encoded.data() + 62) == 0x1f);
    const auto before_decode = encoded;
    const auto decoded = decode(encoded);
    if (!decoded.rgb16) {
        auto direct = decoded;
        Bmp16RetentionTrace trace;
        retain_bmp16_colors(direct, encoded, kDecodeLimit, image.rgb16->layout, &trace);
        std::cout << "Retention diagnostic: layout=" << rgb565 << " size=" << decoded.width << ','
                  << decoded.height << " header=" << le32(encoded.data() + 14)
                  << " offset=" << le32(encoded.data() + 10) << " compression=" << le32(encoded.data() + 30)
                  << " raw_height=" << int32_t(le32(encoded.data() + 22)) << " direct=" << bool(direct.rgb16)
                  << " rgba_equal=" << (decoded.frames[0].pixels == image.frames[0].pixels)
                  << " input_changed=" << (before_decode != encoded)
                  << " reason=" << (trace.reason ? trace.reason : "none") << " x/y=" << trace.x << ',' << trace.y
                  << " packed=" << trace.packed << " RGB=" << unsigned(trace.pixel.r) << ','
                  << unsigned(trace.pixel.g) << ',' << unsigned(trace.pixel.b) << " code=" << trace.code
                  << " inverse=" << trace.previous_inverse << " forward=" << trace.previous_forward << '\n';
        const auto raw = codes(encoded);
        for (size_t i = 0; i < raw.size(); ++i) {
            const auto p = decoded.frames[0].pixels[i];
            const auto expected = (unsigned(image.rgb16->red[p.r]) << (rgb565 ? 11 : 10)) |
                                  (unsigned(image.rgb16->green[p.g]) << 5) | image.rgb16->blue[p.b];
            if (raw[i] != expected) {
                std::cout << "First raw/decoded mapping mismatch: pixel=" << i << " raw=" << raw[i]
                          << " expected=" << expected << " RGB=" << unsigned(p.r) << ',' << unsigned(p.g)
                          << ',' << unsigned(p.b) << '\n';
                break;
            }
        }
    }
    CHECK(decoded.width == image.width && decoded.height == image.height && decoded.rgb16);
    CHECK(decoded.rgb16->layout == image.rgb16->layout);
    CHECK(decoded.frames[0].pixels == image.frames[0].pixels);
}
}
TEST(auto_bmp_keeps_all_555_and_565_component_codes_and_current_rgba) {
    for (const auto name : names) {
        const auto input = bytes(fixture_root / name);
        const auto image = decode(input);
        CHECK(image.rgb16 && image.rgb16->matches(image.frames[0].pixels));
        CHECK(image.bytes() == image.width * image.height * sizeof(Pixel) + sizeof(Rgb16Colors));
        const auto original = codes(input);
        for (bool top : {false, true}) {
            const auto output = automatic(image, top);
            CHECK(codes(output) == original);
            CHECK(decode(output).frames[0].pixels == image.frames[0].pixels);
        }
        roundtrip(image);
    }
}
TEST(auto_bmp_preservation_survives_geometry_and_source_stays_immutable) {
    const auto image = load_image(fixture_root / "rgb565.bmp");
    const auto source = image.frames[0].pixels;
    for (const auto &edited : {rotate_quarters(image, 1), flip(image, true), crop(image, {1, 0, 61, 3}),
                               resize_image(image, 130, 6, false), glass_mask(image, 3, false)}) {
        CHECK(edited.rgb16 == image.rgb16);
        roundtrip(edited);
    }
    CHECK(image.frames[0].pixels == source && image.rgb16->matches(source));
}
TEST(auto_bmp_uses_current_pixels_instead_of_historical_bpp_and_falls_back_without_color_loss) {
    const auto original = load_image(fixture_root / "rgb565-sparse.bmp");
    auto image = original;
    image.metadata.original_bpp = 24;
    image.metadata.source_format = L"historical metadata only";
    roundtrip(image);
    image.frames[0].pixels[0] = {17, 99, 151, 255};
    const auto encoded = automatic(image);
    CHECK(encoded[28] == 24 && decode(encoded).frames[0].pixels == image.frames[0].pixels);
    CHECK(!image.rgb16->matches(image.frames[0].pixels));
    image = original;
    image.frames[0].pixels[0].a = 128;
    CHECK(automatic(image)[28] == 24);
    image = original;
    image.rgb16.reset();
    CHECK(image.metadata.original_bpp == 16 && automatic(image)[28] == 24);
    const auto interpolated = resize_image(original, 3, 3, true);
    CHECK(interpolated.rgb16 == original.rgb16 && automatic(interpolated)[28] == 24);
    CHECK(original.rgb16->matches(original.frames[0].pixels));
}
TEST(auto_bmp_descriptor_respects_decode_budget_and_explicit_depth_still_wins) {
    const auto input = bytes(fixture_root / "rgb565.bmp");
    const auto image = decode(input);
    const size_t rgba_bytes = image.width * image.height * sizeof(Pixel);
    const auto limited = decode(input, L"", rgba_bytes);
    CHECK(!limited.rgb16 && limited.bytes() == rgba_bytes && limited.frames[0].pixels == image.frames[0].pixels);
    CHECK(automatic(limited)[28] == 24);
    for (unsigned bits : {16u, 24u}) {
        SaveOptions options;
        options.bmp_bits = bits;
        const auto output = encode_for_save(image, 0, SaveFormat::Bmp, options);
        CHECK(output[28] == bits && le32(output.data() + 30) == 0);
    }
    Rgb16Colors invalid;
    THROWS(encode_bmp(image, 0, 16, {}, nullptr, &invalid));
    auto indexed_image = image;
    const auto raster = index_image(image, 0, 8);
    auto indexed = std::make_shared<IndexedPixels>();
    indexed->width = image.width; indexed->height = image.height; indexed->bits = 8;
    indexed->palette = raster.palette; indexed->packed = raster.indices;
    indexed_image.frames[0].indexed = indexed;
    CHECK(indexed->matches(image.frames[0].pixels) && automatic(indexed_image)[28] == 8);
}
int main(int argc, char **argv) {
    const auto result = run_tests();
    if (result || argc == 1)
        return result;
    try {
        CHECK(argc == 3 && std::string(argv[1]) == "--oracle");
        const auto parent = std::filesystem::path(GVN_SOURCE_ROOT) / L".cache/bmp-auto-oracle";
        const auto output = std::filesystem::weakly_canonical(argv[2]);
        CHECK(output.parent_path() == parent && output.filename().wstring().starts_with(L"run-") &&
              std::filesystem::is_directory(output) && std::filesystem::is_empty(output));
        const auto write = [&](const std::string &name, const std::vector<uint8_t> &data) {
            std::ofstream file(output / name, std::ios::binary);
            file.write(reinterpret_cast<const char *>(data.data()), data.size());
            file.close(); CHECK(file.good());
        };
        for (const auto name : names) {
            const auto image = load_image(fixture_root / name);
            for (bool top : {false, true})
                write(std::string(name) + (top ? "-top.bmp" : "-bottom.bmp"), automatic(image, top));
        }
        const auto image = load_image(fixture_root / "rgb565.bmp");
        write("rotated.bmp", automatic(rotate_quarters(image, 1), true));
        write("cropped.bmp", automatic(crop(image, {1, 0, 61, 3})));
        auto changed = image;
        changed.frames[0].pixels[0] = {17, 99, 151, 255};
        write("changed-reference.png", encode_png(changed, 0));
        write("changed24.bmp", automatic(changed));
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
