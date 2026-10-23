#include "gvn/saving.h"
#include "gvn/codecs.h"
#include "test.h"
#include <array>
#include <cmath>
#include <fstream>
using namespace gvn;
namespace {
uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
Image sample(uint32_t width = 5) {
    Image image;
    image.width = width;
    image.height = 3;
    image.frames.resize(2);
    image.frames[0].pixels.assign(width * 3, {255, 0, 255, 255});
    constexpr std::array<uint8_t, 7> alpha{0, 1, 63, 127, 128, 254, 255};
    for (unsigned i = 0; i < width * 3; ++i)
        image.frames[1].pixels.push_back({uint8_t(i * 29), uint8_t(i * 71 + 9), uint8_t(i * 13 + 3),
                                         alpha[i % alpha.size()]});
    image.metadata.dpi_defined = true;
    image.metadata.dpi_x = 144.5;
    image.metadata.dpi_y = 288.25;
    return image;
}
Pixel flatten(Pixel p) {
    const auto channel = [&](unsigned c, unsigned matte) {
        return uint8_t((c * p.a + matte * (255 - p.a) + 127) / 255);
    };
    return {channel(p.r, 18), channel(p.g, 52), channel(p.b, 86), 255};
}
void inspect(const Image &source, const std::vector<uint8_t> &bytes, unsigned bits, bool top) {
    CHECK(bytes.size() >= 54 && bytes[0] == 'B' && bytes[1] == 'M');
    CHECK(le32(bytes.data() + 2) == bytes.size() && le32(bytes.data() + 14) == 40);
    CHECK(le32(bytes.data() + 18) == source.width);
    CHECK(int32_t(le32(bytes.data() + 22)) == (top ? -int32_t(source.height) : int32_t(source.height)));
    CHECK(bytes[26] == 1 && bytes[27] == 0 && bytes[28] == bits && bytes[29] == 0);
    CHECK(le32(bytes.data() + 30) == 0 && le32(bytes.data() + 46) == 0); // BI_RGB, no palette.
    const size_t start = le32(bytes.data() + 10), stride = ((source.width * bits + 31) / 32) * 4;
    CHECK(start == 54 && bytes.size() == start + stride * source.height);
    for (unsigned y = 0; y < source.height; ++y) {
        const auto row = bytes.data() + start + (top ? y : source.height - 1 - y) * stride;
        for (unsigned x = 0; x < source.width; ++x) {
            const auto p = flatten(source.frames[1].pixels[size_t(y) * source.width + x]);
            if (bits == 16) {
                const auto packed = unsigned(row[x * 2]) | (unsigned(row[x * 2 + 1]) << 8);
                CHECK((packed & 0x8000) == 0);
                CHECK(((packed >> 10) & 31) == p.r / 8 && ((packed >> 5) & 31) == p.g / 8 &&
                      (packed & 31) == p.b / 8);
            } else
                CHECK(row[x * 3] == p.b && row[x * 3 + 1] == p.g && row[x * 3 + 2] == p.r);
        }
        for (size_t x = source.width * bits / 8; x < stride; ++x)
            CHECK(row[x] == 0);
    }
    const auto decoded = decode(bytes);
    CHECK(decoded.width == source.width && decoded.height == source.height && decoded.frames.size() == 1);
    CHECK(decoded.metadata.original_bpp == bits && decoded.metadata.dpi_defined);
    CHECK(std::abs(decoded.metadata.dpi_x - source.metadata.dpi_x) < .013 &&
          std::abs(decoded.metadata.dpi_y - source.metadata.dpi_y) < .013);
    for (size_t i = 0; i < decoded.frames[0].pixels.size(); ++i) {
        const auto actual = decoded.frames[0].pixels[i], expected = flatten(source.frames[1].pixels[i]);
        CHECK(actual.a == 255);
        if (bits == 24)
            CHECK(actual == expected);
        else {
            const auto expanded = [](unsigned v) { return int((v / 8) * 255 / 31); };
            CHECK(std::abs(int(actual.r) - expanded(expected.r)) <= 1 &&
                  std::abs(int(actual.g) - expanded(expected.g)) <= 1 &&
                  std::abs(int(actual.b) - expanded(expected.b)) <= 1);
        }
    }
}
std::vector<uint8_t> output(const Image &source, unsigned bits, bool top, bool rle) {
    SaveOptions options;
    options.bmp_bits = bits;
    options.bmp = {top, rle, 0x123456};
    return encode_for_save(source, 1, SaveFormat::Bmp, options);
}
}
TEST(bmp_16_rgb555_keeps_rows_padding_matte_frame_and_resolution) {
    for (unsigned width : {1u, 5u}) {
        const auto image = sample(width);
        const auto first = image.frames[0].pixels, selected = image.frames[1].pixels;
        const auto metadata = image.metadata;
        for (bool top : {false, true})
            for (bool rle : {false, true}) {
                const auto bytes = output(image, 16, top, rle);
                inspect(image, bytes, 16, top);
                CHECK(image.frames[0].pixels == first && image.frames[1].pixels == selected &&
                      image.metadata == metadata);
            }
    }
}
TEST(bmp_24_and_default_true_color_keep_full_composited_rgb) {
    const auto image = sample();
    for (bool top : {false, true})
        for (bool rle : {false, true}) {
            const BmpSaveOptions options{top, rle, 0x123456};
            const auto bytes = output(image, 24, top, rle);
            CHECK(bytes == encode_bmp(image, 1, options));
            inspect(image, bytes, 24, top);
        }
}
TEST(bmp_16_rejects_invalid_inputs_and_accepts_effective_resolution_override) {
    auto image = sample();
    THROWS(encode_bmp(image, 3, 16));
    for (unsigned bits : {0u, 1u, 8u, 15u, 17u, 32u})
        THROWS(encode_bmp(image, 1, bits));
    BmpSaveOptions options;
    options.matte = 0x1000000;
    THROWS(encode_bmp(image, 1, 16, options));
    options = {};
    ImageMetadata effective;
    effective.dpi_defined = true;
    effective.dpi_x = 72.25;
    effective.dpi_y = 300.5;
    const auto decoded = decode(encode_bmp(image, 1, 16, options, &effective));
    CHECK(std::abs(decoded.metadata.dpi_x - 72.25) < .013 &&
          std::abs(decoded.metadata.dpi_y - 300.5) < .013);
    CHECK(image.metadata.dpi_x == 144.5 && image.metadata.dpi_y == 288.25);
    image.frames[1].pixels.pop_back();
    THROWS(encode_bmp(image, 1, 16));
}
int main(int argc, char **argv) {
    const auto result = run_tests();
    if (result || argc == 1)
        return result;
    try {
        CHECK(argc == 3 && std::string(argv[1]) == "--oracle");
        const auto parent = std::filesystem::path(GVN_SOURCE_ROOT) / L".cache/bmp-saving-oracle";
        const auto path = std::filesystem::weakly_canonical(argv[2]);
        CHECK(path.parent_path() == parent && path.filename().wstring().starts_with(L"run-") &&
              std::filesystem::is_directory(path) && std::filesystem::is_empty(path));
        const auto write = [&](const auto &name, const std::vector<uint8_t> &bytes) {
            std::ofstream stream(path / name, std::ios::binary);
            stream.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            stream.close();
            CHECK(stream.good());
        };
        const auto source = sample();
        const auto &pixels = source.frames[1].pixels;
        write("source.rgba", {reinterpret_cast<const uint8_t *>(pixels.data()),
                              reinterpret_cast<const uint8_t *>(pixels.data() + pixels.size())});
        for (auto bits : {16u, 24u})
            for (bool top : {false, true})
                for (bool rle : {false, true})
                    write(std::to_string(bits) + "-" + std::to_string(top) + "-" + std::to_string(rle) +
                              ".bmp", output(source, bits, top, rle));
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
