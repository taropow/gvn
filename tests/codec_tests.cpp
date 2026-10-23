#include "gvn/codecs.h"
#include "test.h"
#include <array>
using namespace gvn;
const std::filesystem::path fixtures = GVN_FIXTURES;
const std::vector<Pixel> expected = {{255, 0, 0, 255}, {0, 255, 0, 128},     {0, 0, 255, 0},
                                     {10, 20, 30, 64}, {250, 240, 230, 255}, {0, 0, 0, 0}};
TEST(png_preserves_all_alpha_and_hidden_rgb) {
    const auto image = load_image(fixtures / L"alpha.png");
    CHECK(image.width == 3 && image.height == 2);
    CHECK(image.frames[0].pixels == expected);
    CHECK(!image.animated());
}
TEST(static_formats_and_exif) {
    const auto bitmap = load_image(fixtures / L"rgb.bmp");
    CHECK(bitmap.width == 3 && bitmap.height == 2);
    CHECK(bitmap.frames[0].pixels[1] == Pixel{0, 255, 0, 255});
    CHECK(load_image(fixtures / L"rgb.jpg").format == L"JPEG");
    const auto rotated = load_image(fixtures / L"rotated.jpg");
    CHECK(rotated.width == 2 && rotated.height == 3);
}
TEST(tiff_and_ico_first_image_with_alpha) {
    const auto tiff = load_image(fixtures / L"alpha.tiff");
    CHECK(tiff.width == 3 && tiff.height == 2 && tiff.frames.size() == 1);
    CHECK(tiff.frames[0].pixels == expected);
    const auto icon = load_image(fixtures / L"alpha.ico");
    CHECK(icon.width == 16 && icon.height == 16 && icon.frames.size() == 1);
    CHECK(icon.frames[0].pixels[0].a == 0);
    CHECK(icon.frames[0].pixels[8 * 16 + 8] == Pixel{20, 100, 200, 128});
}
TEST(psd_8_16_raw_rle_zip_predict) {
    for (int bits : {8, 16})
        for (int compression = 0; compression < 4; ++compression) {
            const auto name = L"psd-" + std::to_wstring(bits) + L"-" + std::to_wstring(compression) + L".psd";
            const auto image = load_image(fixtures / name);
            CHECK(image.frames[0].pixels == expected);
            CHECK(image.width == 3 && image.height == 2);
        }
}
TEST(psd_grayscale) {
    const auto image = load_image(fixtures / L"psd-8-1-gray.psd");
    CHECK(image.frames[0].pixels[0] == Pixel{255, 255, 255, 255});
    CHECK(image.frames[0].pixels[1] == Pixel{0, 0, 0, 128});
}
TEST(reject_bad_and_oversized) {
    THROWS(load_image(fixtures / L"truncated.psd"));
    THROWS(load_image(fixtures / L"invalid.bin"));
    THROWS(load_image(fixtures / L"alpha.png", 23));
    THROWS(load_image(fixtures / L"psd-16-1.psd", 23));
    auto bytes = read_file(fixtures / L"psd-8-1.psd");
    bytes.back() = 0xff;
    bytes[56] = 127;
    THROWS(decode(bytes));
}
TEST(png_roundtrip_is_exact) {
    const auto image = load_image(fixtures / L"alpha.png");
    const auto bytes = encode(image, 0, L".png");
    const auto copy = decode(bytes);
    CHECK(copy.frames[0].pixels == expected);
}
TEST(other_encoders) {
    const auto image = load_image(fixtures / L"alpha.png");
    for (const auto ext : {L".bmp", L".jpg", L".gif"}) {
        const auto copy = decode(encode(image, 0, ext));
        CHECK(copy.width == 3 && copy.height == 2);
    }
    THROWS(encode(image, 0, L".psd"));
}
TEST(animated_frames_match_independent_oracle) {
    for (const auto name : {L"disposal.gif", L"blend.apng", L"excluded.png"}) {
        const auto image = load_image(fixtures / name);
        CHECK(image.frames.size() == 4);
        CHECK(image.frames[0].duration_ms == 40);
        CHECK(image.frames[1].duration_ms == 120);
        for (size_t f = 0; f < 4; ++f) {
            const auto reference =
                load_image(fixtures / (std::wstring(name) + L"-frame-" + std::to_wstring(f) + L".png"));
            CHECK(reference.frames[0].pixels.size() == image.frames[f].pixels.size());
            for (size_t i = 0; i < reference.frames[0].pixels.size(); ++i) {
                const auto a = image.frames[f].pixels[i], b = reference.frames[0].pixels[i];
                if (std::abs(int(a.a) - b.a) > 1 ||
                    (a.a && (std::abs(int(a.r) - b.r) > 1 || std::abs(int(a.g) - b.g) > 1 ||
                             std::abs(int(a.b) - b.b) > 1))) {
                    std::wcerr << name << L" frame=" << f << L" pixel=" << i << L" actual=" << int(a.r)
                               << L"," << int(a.g) << L"," << int(a.b) << L"," << int(a.a) << L" expected="
                               << int(b.r) << L"," << int(b.g) << L"," << int(b.b) << L"," << int(b.a)
                               << L"\n";
                    throw std::runtime_error("animation oracle mismatch");
                }
            }
        }
    }
}
TEST(animation_limits_and_png_detection) {
    CHECK(!is_apng(read_file(fixtures / L"alpha.png")));
    CHECK(is_apng(read_file(fixtures / L"excluded.png")));
    THROWS(load_image(fixtures / L"blend.apng", 6 * 4 * 4 * 3));
    auto bytes = read_file(fixtures / L"blend.apng");
    bytes.resize(50);
    THROWS(decode(bytes));
}
TEST(loading_transparency_preserve_ignore_and_composite) {
    for (const auto name : {L"rgba-background.png", L"alpha-v4.bmp", L"alpha-v5.bmp"}) {
        const auto path = fixtures / L"loading" / name;
        const auto raw = load_image(path);
        CHECK(raw.frames[0].pixels[1] == Pixel{0, 255, 0, 128});
        CHECK(raw.frames[0].pixels[2] == Pixel{0, 0, 255, 0});
        CHECK(raw.frames[0].pixels[5] == Pixel{19, 28, 37, 0});
        LoadingOptions options;
        options.preserve_alpha = false;
        options.background = 0x123456;
        const auto composite = load_image(path, kDecodeLimit, options);
        CHECK(composite.frames[0].pixels[2] == Pixel{18, 52, 86, 255});
        CHECK(composite.frames[0].pixels[1] == Pixel{9, 154, 43, 255});
        options.png_transparency = options.bmp_transparency = false;
        const auto opaque = load_image(path, kDecodeLimit, options);
        CHECK(opaque.frames[0].pixels[2] == Pixel{0, 0, 255, 255});
        CHECK(opaque.frames[0].pixels[5] == Pixel{19, 28, 37, 255});
    }
}
TEST(png_embedded_backgrounds_rgb_indexed_low_depth_and_16bit) {
    LoadingOptions options;
    options.preserve_alpha = false;
    options.png_background = true;
    options.background = 0xfedcba;
    for (const auto name : {L"rgba-background.png", L"indexed-background.png", L"gray2-background.png",
                            L"gray16-background.png", L"rgb16-trns-background.png"}) {
        const auto path = fixtures / L"loading" / name;
        const auto image = load_image(path, kDecodeLimit, options);
        const size_t index = std::wstring(name) == L"rgba-background.png" ? 2 : 0;
        const auto expected = std::wstring(name) == L"gray2-background.png"    ? Pixel{170, 170, 170, 255}
                              : std::wstring(name) == L"gray16-background.png" ? Pixel{51, 51, 51, 255}
                                                                               : Pixel{18, 52, 86, 255};
        CHECK(image.frames[0].pixels[index] == expected);
        options.png_background = false;
        CHECK(load_image(path, kDecodeLimit, options).frames[0].pixels[index] == Pixel{254, 220, 186, 255});
        options.png_background = true;
        options.preserve_alpha = true;
        CHECK(load_image(path, kDecodeLimit, options).frames[0].pixels[index].a == 0);
        options.preserve_alpha = false;
    }
    // Missing metadata falls back to the explicitly selected color.
    CHECK(load_image(fixtures / L"alpha.png", kDecodeLimit, options).frames[0].pixels[2] ==
          Pixel{254, 220, 186, 255});
    const auto animated = load_image(fixtures / L"loading/background.apng", kDecodeLimit, options);
    CHECK(animated.frames.size() == 4 && animated.frames[0].pixels.back() == Pixel{18, 52, 86, 255});
    options.preserve_alpha = true;
    CHECK(
        load_image(fixtures / L"loading/background.apng", kDecodeLimit, options).frames[0].pixels.back().a ==
        0);
}
TEST(gif_loading_static_animation_first_frame_and_embedded_background) {
    const auto path = fixtures / L"loading";
    LoadingOptions options;
    const auto raw = load_image(path / L"animated-background.gif");
    CHECK(raw.animated() && raw.frames[0].pixels[0].a == 0);
    options.gif_transparency = false;
    CHECK(load_image(path / L"animated-background.gif", kDecodeLimit, options).frames[0].pixels ==
          raw.frames[0].pixels);
    CHECK(load_image(path / L"static-background.gif", kDecodeLimit, options).frames[0].pixels[0] ==
          Pixel{10, 20, 30, 255});
    options.preserve_alpha = false;
    options.gif_background = true;
    const auto animated = load_image(path / L"animated-background.gif", kDecodeLimit, options);
    CHECK(animated.frames[0].pixels[0] == Pixel{18, 52, 86, 255});
    options.gif_animation = false;
    options.gif_transparency = true;
    const auto first = load_image(path / L"animated-background.gif", kDecodeLimit, options);
    CHECK(first.frames.size() == 1 && first.frames[0].pixels[0] == Pixel{18, 52, 86, 255});
    CHECK(first.frames[0].indexed && first.bytes() > 24);
    THROWS(load_image(path / L"animated-background.gif", first.bytes() - 1, options));
    options.gif_animation = true;
    THROWS(load_image(path / L"animated-background.gif", 24, options));
    options.gif_animation = false;
    options.preserve_alpha = true;
    const auto offset = load_image(path / L"first-offset.gif", 96, options);
    CHECK(offset.width == 6 && offset.height == 4 && offset.frames.size() == 1);
    CHECK(offset.frames[0].pixels[0].a == 0 && offset.frames[0].pixels[1 * 6 + 3] == Pixel{255, 0, 0, 255});
    options.gif_transparency = false;
    CHECK(load_image(path / L"first-offset.gif", 96, options).frames[0].pixels[1 * 6 + 2].a == 255);
}
TEST(jpeg_loading_orientation_all_eight_values_can_be_disabled) {
    for (int orientation = 1; orientation <= 8; ++orientation) {
        const auto base = L"exif-" + std::to_wstring(orientation);
        const auto image = load_image(fixtures / L"loading" / (base + L".jpg"));
        const auto oracle = load_image(fixtures / L"loading" / (base + L"-expected.png"));
        CHECK(image.width == oracle.width && image.height == oracle.height);
        for (size_t i = 0; i < image.frames[0].pixels.size(); ++i) {
            const auto a = image.frames[0].pixels[i], b = oracle.frames[0].pixels[i];
            if (std::abs(int(a.r) - b.r) > 2 || std::abs(int(a.g) - b.g) > 2 ||
                std::abs(int(a.b) - b.b) > 2 || a.a != 255)
                throw std::runtime_error("EXIF orientation " + std::to_string(orientation) + " pixel " +
                                         std::to_string(i) + " differs from Pillow oracle");
        }
        LoadingOptions options;
        options.exif_orientation = false;
        const auto unrotated = load_image(fixtures / L"loading" / (base + L".jpg"), kDecodeLimit, options);
        const auto first = load_image(fixtures / L"loading" / L"exif-1.jpg");
        CHECK(unrotated.width == 3 && unrotated.height == 2 &&
              unrotated.frames[0].pixels == first.frames[0].pixels);
    }
}
int main() {
    return run_tests();
}
