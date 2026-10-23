#include "gvn/codecs.h"
#include "gvn/edit.h"
#include "gvn/saving.h"
#include "test.h"
#include <array>
#include <fstream>
#include <iostream>
using namespace gvn;
namespace {
const auto fixtures = std::filesystem::path(GVN_SOURCE_ROOT) / L"tests/fixtures/palette";
struct Case {
    const char *name;
    unsigned bits;
    int alpha;
};
constexpr Case cases[]{{"png-1-opaque.png", 1, 0}, {"png-1-alpha.png", 1, 1},  {"png-2-opaque.png", 2, 0},
                       {"png-2-alpha.png", 2, 1},  {"png-4-opaque.png", 4, 0}, {"png-4-alpha.png", 4, 1},
                       {"png-8-opaque.png", 8, 0}, {"png-8-alpha.png", 8, 1},  {"bmp-1.bmp", 1, 0},
                       {"bmp-4.bmp", 4, 0},        {"bmp-8.bmp", 8, 0},        {"gif-opaque.gif", 8, 0},
                       {"gif-alpha.gif", 8, 2},    {"bmp-4-rle.bmp", 4, 0},    {"bmp-8-rle.bmp", 8, 0}};
Pixel color(unsigned i, int alpha) {
    const auto rgb_index = i == 1 ? 0 : i;
    return {static_cast<uint8_t>(rgb_index * 37), static_cast<uint8_t>(rgb_index * 71),
            static_cast<uint8_t>(rgb_index * 109),
            static_cast<uint8_t>(alpha == 1             ? i * 53
                                 : alpha == 2 && i == 2 ? 0
                                                        : 255)};
}
void original(const Image &image, const Case &source) {
    CHECK(image.width == 37 && image.height == 23 && image.frames.size() == 1);
    const auto &frame = image.frames[0];
    CHECK(frame.indexed && frame.indexed->bits == source.bits && frame.indexed->matches(frame.pixels));
    CHECK(frame.indexed->palette.size() == (1u << source.bits));
    for (unsigned i = 0; i < frame.indexed->palette.size(); ++i)
        CHECK(frame.indexed->palette[i] == color(i, source.alpha));
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x)
            CHECK(frame.indexed->index(x, y) == (x * 7 + y * 13 + x * y) % (1u << source.bits));
}
void same_indices(const Image &a, const Image &b, bool same_bits = true) {
    CHECK(a.width == b.width && a.height == b.height && a.frames.size() == 1 && b.frames.size() == 1);
    CHECK(a.frames[0].indexed && b.frames[0].indexed);
    const auto &x = *a.frames[0].indexed, &y = *b.frames[0].indexed;
    CHECK(x.matches(a.frames[0].pixels) && y.matches(b.frames[0].pixels));
    if (same_bits)
        CHECK(x.bits == y.bits);
    for (uint32_t row = 0; row < a.height; ++row)
        for (uint32_t col = 0; col < a.width; ++col)
            CHECK(x.index(col, row) == y.index(col, row));
}
Image operation(const Image &image, unsigned op) {
    switch (op) {
    case 0:
        return still_frame(image, 0);
    case 1:
        return crop(image, {3, 2, 17, 13});
    case 2:
        return rotate_quarters(image, 1);
    case 3:
        return rotate_quarters(image, 2);
    case 4:
        return rotate_quarters(image, 3);
    case 5:
        return flip(image, true);
    case 6:
        return flip(image, false);
    case 7:
        return resize_image(image, 53, 31, false);
    default:
        throw std::runtime_error("Unknown palette transform");
    }
}
} // namespace
TEST(indexed_decode_keeps_every_original_palette_entry_and_number) {
    for (const auto &c : cases)
        original(load_image(fixtures / c.name), c);
}
TEST(full_palette_capacity_accepts_all_bit_patterns_but_partial_palette_rejects_missing_entries) {
    for (const auto bits : {1u, 2u, 4u, 8u}) {
        const auto image = load_image(fixtures / ("png-" + std::to_string(bits) + "-opaque.png"));
        auto indexed = *image.frames[0].indexed;
        std::fill(indexed.packed.begin(), indexed.packed.end(), 255);
        CHECK(indexed.valid());
        indexed.palette.pop_back();
        CHECK(!indexed.valid());
    }
}
TEST(indexed_metadata_is_packed_shared_and_counted_in_cache_budget) {
    for (const auto &c : cases) {
        const auto source = load_image(fixtures / c.name);
        const auto &frame = source.frames[0];
        const auto expected =
            ((size_t(source.width) * c.bits + 7) / 8) * source.height + (1u << c.bits) * sizeof(Pixel);
        CHECK(frame.indexed->bytes() == expected);
        CHECK(source.bytes() == frame.pixels.size() * sizeof(Pixel) + expected);
        CHECK(still_frame(source, 0).frames[0].indexed == frame.indexed);
        CHECK(!still_frame(source, 0, false).frames[0].indexed);
        CHECK(load_image(fixtures / c.name, source.bytes()).bytes() == source.bytes());
        THROWS(load_image(fixtures / c.name, source.bytes() - 1));
    }
}
TEST(indexed_png_all_filters_compression_and_interlace_keep_alpha_and_duplicate_numbers) {
    for (const auto &c : cases) {
        const auto source = load_image(fixtures / c.name);
        for (int interlace : {0, 1})
            for (unsigned filter = 0; filter < 7; ++filter) {
                PngSaveOptions opts;
                opts.interlaced = interlace != 0;
                opts.filter = static_cast<PngFilter>(filter);
                opts.compression = filter % 2 ? 0 : 9;
                const auto saved = decode(encode_png(source, 0, opts));
                same_indices(source, saved);
                CHECK(source.frames[0].pixels == saved.frames[0].pixels);
                CHECK(source.frames[0].indexed->palette == saved.frames[0].indexed->palette);
            }
    }
}
TEST(indexed_geometry_preserves_numbers_and_color_edits_drop_old_metadata) {
    const auto source = load_image(fixtures / L"png-4-alpha.png");
    for (unsigned i = 0; i < 8; ++i) {
        const auto out = operation(source, i);
        CHECK(out.frames[0].indexed && out.frames[0].indexed->matches(out.frames[0].pixels));
        const auto decoded = decode(encode_png(out, 0));
        same_indices(out, decoded);
        CHECK(decoded.frames[0].pixels == out.frames[0].pixels);
    }
    for (const auto &out :
         {effect(source, Effect::Negative), levels(source, 0, 255, 1.2), gaussian(source, 1),
          xor_mask(source, {1, 1, 4, 4}), replace_color(source, {0, 0, 0, 255}, {1, 2, 3, 255}),
          paste_region(source, source, {0, 0, 4, 4}), resize_image(source, 53, 31, true)}) {
        CHECK(!out.frames[0].indexed);
        CHECK(decode(encode_png(out, 0)).frames[0].pixels == out.frames[0].pixels);
    }
    original(source, cases[5]);
}
TEST(indexed_loading_alpha_options_transform_palette_and_pixels_together) {
    for (bool transparency : {false, true}) {
        LoadingOptions opts;
        opts.png_transparency = transparency;
        opts.preserve_alpha = false;
        opts.background = 0x123456;
        const auto image = load_image(fixtures / L"png-8-alpha.png", kDecodeLimit, opts);
        CHECK(image.frames[0].indexed->matches(image.frames[0].pixels));
        for (const auto pixel : image.frames[0].pixels)
            CHECK(pixel.a == 255);
        same_indices(load_image(fixtures / L"png-8-alpha.png"), image);
    }
}

TEST(full_mask_palette_and_logical_indices_match_five_official_4bit_results) {
    const auto directory = std::filesystem::path(GVN_SOURCE_ROOT) / L"tests/fixtures/mask-reference/palette";
    const auto source = load_image(directory / L"source.png");
    CHECK(source.frames[0].indexed && source.frames[0].indexed->bits == 4);
    std::ifstream cases_file(directory / L"cases.tsv"); CHECK(cases_file);
    size_t checked{};
    for (std::string kind; std::getline(cases_file, kind);) {
        const auto operation = kind == "negative" ? Mask::Negative : kind == "xor" ? Mask::Xor
                             : kind == "swap" ? Mask::Swap : kind == "vertical" ? Mask::Vertical : Mask::Q0;
        const auto actual = mask_image(source, operation);
        const auto expected = load_image(directory / (kind + ".png"));
        CHECK(actual.frames[0].pixels == expected.frames[0].pixels);
        const auto &indexed = actual.frames[0].indexed;
        CHECK(indexed && indexed->bits == 4 && indexed->palette.size() == 16 && indexed->matches(actual.frames[0].pixels));
        std::ifstream colors(directory / (kind + ".palette"), std::ios::binary);
        std::ifstream indices(directory / (kind + ".indices"), std::ios::binary); CHECK(colors && indices);
        for (const auto pixel : indexed->palette) {
            CHECK(colors.get() == pixel.r && colors.get() == pixel.g && colors.get() == pixel.b && pixel.a == 255);
        }
        for (uint32_t y = 0; y < actual.height; ++y) for (uint32_t x = 0; x < actual.width; ++x)
            CHECK(indices.get() == indexed->index(x,y));
        CHECK(colors.peek() == std::char_traits<char>::eof() && indices.peek() == std::char_traits<char>::eof());
        const auto saved = decode(encode_png(actual, 0));
        same_indices(actual, saved); CHECK(saved.frames[0].pixels == actual.frames[0].pixels);
        ++checked;
    }
    CHECK(checked == 5);
}
TEST(indexed_bmp_auto_depth_and_gif_saving_reuse_numbers_instead_of_requantizing) {
    for (const auto &c : cases) {
        const auto source = load_image(fixtures / c.name);
        SaveOptions opts;
        opts.bmp.top_down = true;
        opts.bmp.rle = true;
        const auto bmp = encode_for_save(source, 0, SaveFormat::Bmp, opts);
        CHECK(bmp[28] == (c.bits == 2 ? 4 : c.bits));
        const auto decoded_bmp = decode(bmp);
        if (!decoded_bmp.frames[0].indexed)
            throw std::runtime_error(std::string(c.name) +
                                     " saved BMP lost indices; compression=" + std::to_string(bmp[30]));
        same_indices(source, decoded_bmp, false);
        opts.gif.interlaced = true;
        opts.gif.uncompressed = true;
        const auto gif = decode(encode_for_save(source, 0, SaveFormat::Gif, opts));
        if (!gif.frames[0].indexed)
            throw std::runtime_error(std::string(c.name) + " saved GIF lost indices");
        same_indices(source, gif, false);
    }
}
TEST(stale_or_invalid_index_metadata_never_overwrites_current_rgba_pixels) {
    auto source = load_image(fixtures / L"png-4-alpha.png");
    source.frames[0].pixels[0] = {111, 222, 77, 99};
    CHECK(!source.frames[0].indexed->matches(source.frames[0].pixels));
    CHECK(decode(encode_png(source, 0)).frames[0].pixels == source.frames[0].pixels);
    CHECK(encode_for_save(source, 0, SaveFormat::Bmp, SaveOptions{})[28] == 24);
    auto invalid = std::make_shared<IndexedPixels>(*source.frames[0].indexed);
    invalid->bits = 3;
    CHECK(!invalid->valid());
    source.frames[0].indexed = invalid;
    CHECK(decode(encode_png(source, 0)).frames[0].pixels == source.frames[0].pixels);
    invalid->bits = 4;
    invalid->packed.pop_back();
    CHECK(!invalid->valid());
    invalid->packed.resize(invalid->stride() * invalid->height);
    invalid->palette.resize(1);
    invalid->set_index(0, 0, 3);
    CHECK(!invalid->valid());
}
TEST(gif_save_dither_switch_changes_quantized_pixels_and_keeps_original_rgba) {
    const auto source = load_image(fixtures.parent_path() / L"saving/rgba.png");
    const auto original = source.frames[0].pixels;
    SaveOptions options;
    options.dither = false;
    const auto plain = decode(encode_for_save(source, 0, SaveFormat::Gif, options));
    options.dither = true;
    const auto dithered = decode(encode_for_save(source, 0, SaveFormat::Gif, options));
    CHECK(plain.width == source.width && plain.height == source.height);
    CHECK(dithered.width == source.width && dithered.height == source.height);
    CHECK(plain.frames[0].pixels != dithered.frames[0].pixels);
    CHECK(source.frames[0].pixels == original && !source.frames[0].indexed);
}
namespace {
int emit(const std::filesystem::path &directory) {
    std::filesystem::create_directories(directory);
    size_t count{};
    std::cout << "{\"cases\":[";
    const auto write = [&](const Image &image, const Case &c, unsigned op, const char *format,
                           const SaveOptions &opts) {
        const auto name = std::to_string(count) + "." + format;
        const auto kind = std::string(format) == "png"   ? SaveFormat::Png
                          : std::string(format) == "bmp" ? SaveFormat::Bmp
                                                         : SaveFormat::Gif;
        save_image(image, 0, directory / name, opts, false);
        if (count++)
            std::cout << ",";
        std::cout << "{\"file\":\"" << name << "\",\"source\":\"" << c.name << "\",\"op\":" << op
                  << ",\"format\":\"" << format << "\",\"filter\":" << static_cast<unsigned>(opts.png.filter)
                  << ",\"compression\":" << opts.png.compression << ",\"interlaced\":" << opts.png.interlaced
                  << ",\"transparency\":" << static_cast<unsigned>(opts.png.transparency) << "}";
    };
    for (const auto &c : cases) {
        const auto image = load_image(fixtures / c.name);
        for (unsigned op = 0; op < 8; ++op) {
            auto out = operation(image, op);
            SaveOptions opts;
            write(out, c, op, "png", opts);
            write(out, c, op, "bmp", opts);
            write(out, c, op, "gif", opts);
        }
        SaveOptions rle;
        rle.bmp.rle = true;
        rle.bmp.top_down = true;
        write(image, c, 0, "bmp", rle);
    }
    for (const auto &c : cases)
        if (c.alpha == 1) {
            const auto image = load_image(fixtures / c.name);
            for (unsigned f = 0; f < 7; ++f)
                for (bool interlace : {false, true})
                    for (int level : {0, 6, 9}) {
                        SaveOptions opts;
                        opts.png.filter = static_cast<PngFilter>(f);
                        opts.png.interlaced = interlace;
                        opts.png.compression = level;
                        write(image, c, 0, "png", opts);
                    }
            for (unsigned trans = 1; trans < 7; ++trans) {
                SaveOptions opts;
                opts.png.transparency = static_cast<PngTransparency>(trans);
                write(image, c, 0, "png", opts);
            }
        }
    std::cout << "],\"outputs\":" << count << ",\"passed\":true}\n";
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--oracle")
            return emit(argv[2]);
    } catch (const std::exception &error) {
        std::cerr << error.what();
        return 1;
    }
    return run_tests();
}
