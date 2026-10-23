#include "gvn/comic.h"
#include "gvn/codecs.h"
#include "gvn/edit.h"
#include "test.h"
#include <iostream>
using namespace gvn;
namespace {
Image solid(uint32_t width, uint32_t height, Pixel pixel) {
    Image out;
    out.width = width;
    out.height = height;
    out.frames.push_back({std::vector<Pixel>(checked_pixels(width, height), pixel), 100});
    return out;
}
} // namespace
TEST(comic_all_reference_methods_and_odd_padding_match_opaque_pixels) {
    const auto directory = std::filesystem::path(GVN_FIXTURES) / L"comic";
    const auto first = load_image(directory / L"first.png");
    size_t compared{};
    for (int second_height : {17, 16}) {
        const auto second = load_image(directory / (L"second-" + std::to_wstring(second_height) + L".png"));
        for (int method = 0; method <= (second_height == 17 ? 10 : 0); ++method)
            for (int mode = 0; mode < 3; ++mode) {
                const auto path =
                    directory / (L"method-" + std::wstring(method < 10 ? L"0" : L"") +
                                 std::to_wstring(method) + L"-height-" + std::to_wstring(second_height) +
                                 L"-mode-" + std::to_wstring(mode) + L".png");
                const auto expected = load_image(path);
                const auto actual = compose_comic_pages(first, second, true, {mode, method, method, 0});
                CHECK(actual.width == expected.width && actual.height == expected.height);
                unsigned maximum{};
                for (size_t i = 0; i < actual.frames[0].pixels.size(); ++i) {
                    const auto a = actual.frames[0].pixels[i], b = expected.frames[0].pixels[i];
                    CHECK(a.a == 255 && b.a == 255);
                    maximum =
                        std::max({maximum, unsigned(std::abs(int(a.r) - b.r)),
                                  unsigned(std::abs(int(a.g) - b.g)), unsigned(std::abs(int(a.b) - b.b))});
                }
                std::cout << "ORACLE comic " << second_height << ' ' << method << ' ' << mode
                          << " max_channel_error=" << maximum << '\n';
                CHECK(maximum <= (method == 0 || mode == 0 ? 0u : 2u));
                ++compared;
            }
    }
    CHECK(compared == 36);
}
TEST(comic_padding_changes_only_gaps_and_preserves_alpha_hidden_rgb_and_direction) {
    const auto first = solid(2, 3, {250, 80, 15, 0}), second = solid(1, 6, {9, 90, 180, 127});
    for (bool rtl : {false, true}) {
        const auto actual = compose_comic_pages(first, second, rtl, {0, 2, 1, 0x123456});
        CHECK(actual.width == 3 && actual.height == 6);
        for (unsigned y = 0; y < 6; ++y)
            for (unsigned x = 0; x < 3; ++x) {
                const bool left = rtl ? x != 0 : x < 2;
                const Pixel expected = !left             ? Pixel{9, 90, 180, 127}
                                       : y >= 1 && y < 4 ? Pixel{250, 80, 15, 0}
                                                         : Pixel{0x12, 0x34, 0x56, 255};
                CHECK(actual.frames[0].pixels[y * 3 + x] == expected);
            }
    }
    CHECK(first.frames[0].pixels.front() == Pixel{250, 80, 15, 0});
}
TEST(comic_target_sizes_round_width_and_keep_both_legacy_policies) {
    const auto first = solid(7, 11, {1, 2, 3, 255}), second = solid(5, 17, {20, 30, 40, 127});
    CHECK(compose_comic_pages(first, second, false, {1}).width == 10);
    CHECK(compose_comic_pages(first, second, false, {1}).height == 11);
    CHECK(compose_comic_pages(first, second, false, {2}).width == 16);
    CHECK(compose_comic_pages(first, second, false, {2}).height == 17);
    const auto old_resize = join_pages(first, resize_image(second, 3, 11), true);
    CHECK(compose_comic_pages(first, second, true, {3}).frames[0].pixels == old_resize.frames[0].pixels);
    const auto old_pad = join_pages(first, second, false);
    CHECK(compose_comic_pages(first, second, false, {4}).frames[0].pixels == old_pad.frames[0].pixels);
}
TEST(comic_all_filtered_methods_keep_constant_alpha_and_simple_keeps_hidden_colors) {
    const auto first = solid(3, 5, {1, 2, 3, 255}), second = solid(5, 11, {64, 128, 192, 128});
    for (int method = 0; method <= 10; ++method) {
        const auto actual = compose_comic_pages(first, second, false, {1, method, method});
        for (unsigned y = 0; y < actual.height; ++y)
            for (unsigned x = first.width; x < actual.width; ++x) {
                const auto p = actual.frames[0].pixels[y * actual.width + x];
                CHECK(p.a == 128 && std::abs(int(p.r) - 64) <= 2 && std::abs(int(p.g) - 128) <= 2 &&
                      std::abs(int(p.b) - 192) <= 2);
            }
    }
    const auto hidden = solid(5, 11, {123, 45, 67, 0});
    const auto actual = compose_comic_pages(first, hidden, false, {1, 0, 0});
    CHECK(actual.frames[0].pixels[first.width] == Pixel{123, 45, 67, 0});
}
TEST(comic_invalid_input_and_oversized_results_fail_before_output_allocation) {
    const auto page = solid(1, 2, {1, 2, 3, 255});
    THROWS(compose_comic_pages(Image{}, page, false, {}));
    auto invalid = page;
    invalid.frames.push_back(invalid.frames[0]);
    THROWS(compose_comic_pages(invalid, page, false, {}));
    for (const auto options : {ComicOptions{-1}, ComicOptions{5}, ComicOptions{1, 11}, ComicOptions{2, 2, -1},
                               ComicOptions{0, 2, 1, 0x1000000}})
        THROWS(compose_comic_pages(page, page, false, options));
    const auto tall = solid(1, 50000, {}), wide = solid(50000, 1, {});
    THROWS(compose_comic_pages(tall, wide, false, {2}));
    THROWS(compose_comic_pages(tall, wide, false, {4}));
}
TEST(comic_cache_identity_tracks_only_the_active_composition_options) {
    ComicOptions a{0, 2, 1, 0x123456}, b = a;
    b.down = 10;
    b.up = 10;
    CHECK(a.key() == b.key());
    b.fill++;
    CHECK(a.key() != b.key());
    a.mode = b.mode = 1;
    b.fill = a.fill;
    CHECK(a.key() != b.key());
    b.down = a.down;
    CHECK(a.key() == b.key());
    a.mode = b.mode = 2;
    CHECK(a.key() != b.key());
    b.up = a.up;
    CHECK(a.key() == b.key());
}
int main() {
    return run_tests();
}
