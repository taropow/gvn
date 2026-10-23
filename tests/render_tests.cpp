#include "gvn/renderer.h"
#include "gvn/codecs.h"
#include "test.h"
#include "test_desktop.h"
#include "../src/pixel_conversion.h"
#include <cmath>
#include <cstring>
using namespace gvn;
TEST(simd_premultiply_matches_scalar_for_every_color_alpha_pair_unaligned_and_tails) {
    std::vector<Pixel> rgba;
    for (unsigned alpha = 0; alpha <= 255; ++alpha)
        for (unsigned color = 0; color <= 255; ++color)
            rgba.push_back({static_cast<uint8_t>(color), static_cast<uint8_t>(color * 17),
                            static_cast<uint8_t>(255 - color), static_cast<uint8_t>(alpha)});
    const auto original = rgba;
    std::vector<uint8_t> output(rgba.size() * 4 + 16, 0x5a);
    const auto check = [&](size_t start, size_t count, size_t offset) {
        std::fill_n(output.begin(), offset, 0xa5);
        std::fill_n(output.begin() + offset + count * 4, 16, 0xa5);
        detail::premultiply_rgba(reinterpret_cast<const uint8_t *>(rgba.data() + start),
                                 output.data() + offset, count);
        for (size_t i = 0; i < count; ++i) {
            const auto p = rgba[start + i];
            const auto at = offset + i * 4;
            CHECK(output[at] == (p.b * p.a + 127) / 255);
            CHECK(output[at + 1] == (p.g * p.a + 127) / 255);
            CHECK(output[at + 2] == (p.r * p.a + 127) / 255);
            CHECK(output[at + 3] == p.a);
        }
        CHECK(std::all_of(output.begin(), output.begin() + offset, [](auto p) { return p == 0xa5; }));
        CHECK(std::all_of(output.begin() + offset + count * 4, output.begin() + offset + count * 4 + 16,
                          [](auto p) { return p == 0xa5; }));
    };
    check(0, rgba.size(), 0);
    for (size_t start : {1, 2, 3, 127, 255, 256, 32765, 65024})
        for (size_t count : {0, 1, 2, 3, 4, 5, 7, 16, 127, 255})
            for (size_t offset : {0, 1, 3, 7})
                check(start, count, offset);
    CHECK(rgba == original);
}
TEST(borrowed_rgba_rows_match_legacy_pixels_for_all_filters_and_keep_workspace_bounded) {
    std::vector<Pixel> rgba(128 * 73);
    const uint8_t alphas[]{0, 1, 37, 127, 128, 254, 255};
    std::vector<uint8_t> bgra(rgba.size() * 4);
    for (size_t i = 0; i < rgba.size(); ++i) {
        const Pixel p{static_cast<uint8_t>(i * 17 + 3), static_cast<uint8_t>(i * 97 + 11),
                      static_cast<uint8_t>(i * 29 + 101), alphas[i % std::size(alphas)]};
        rgba[i] = p;
        bgra[i * 4] = static_cast<uint8_t>((p.b * p.a + 127) / 255);
        bgra[i * 4 + 1] = static_cast<uint8_t>((p.g * p.a + 127) / 255);
        bgra[i * 4 + 2] = static_cast<uint8_t>((p.r * p.a + 127) / 255);
        bgra[i * 4 + 3] = p.a;
    }
    const auto original = rgba;
    std::vector<View> views;
    for (const auto size : {SIZE{73, 41}, SIZE{430, 300}, SIZE{1, 1}}) {
        View view;
        view.resize(size.cx, size.cy);
        view.set_image(128, 73);
        views.push_back(view);
    }
    for (const double scale : {.35, 3.25, 50.0}) {
        View view;
        view.manual = true;
        view.centered = false;
        view.resize(71, 53);
        view.set_image(128, 73, true);
        view.scale = scale;
        view.pan_x = -32 * scale - .125;
        view.pan_y = -8 * scale - .5;
        views.push_back(view);
    }
    for (const auto &view : views)
        for (int method = 0; method <= kLastDisplayFilter; ++method) {
            const auto expected = resample_display(bgra, 128, 73, view, method);
            const auto actual = resample_rgba_display(rgba, 128, 73, view, method);
            CHECK(actual.width == expected.width && actual.height == expected.height);
            CHECK(actual.left == expected.left && actual.top == expected.top);
            CHECK(actual.bgra == expected.bgra);
            CHECK(actual.conversion_bytes <= 1024 * 1024 && actual.workspace_bytes <= 35ull * 1024 * 1024);
        }
    CHECK(rgba == original);
    THROWS(resample_rgba_display({}, 128, 73, views[0], 0));
}
TEST(fitted_renderer_releases_full_conversion_cache_and_reuses_its_tile) {
    HWND window = CreateWindowExW(0, L"STATIC", L"GVN borrowed source", WS_POPUP, 0, 0, 128, 96, nullptr,
                                  nullptr, nullptr, nullptr);
    CHECK(window);
    Image source;
    source.width = 256;
    source.height = 128;
    source.frames.push_back({std::vector<Pixel>(256 * 128, {51, 77, 151, 127}), 0});
    source.frames[0].pixels[300] = {123, 88, 9, 0};
    const auto original = source.frames[0].pixels;
    View view;
    view.resize(128, 96);
    view.set_image(source.width, source.height);
    Renderer renderer;
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(renderer.source_bytes() == 0 && renderer.sample_bytes() > 0);
    const auto bytes = renderer.sample_bytes();
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(renderer.sample_bytes() == bytes && renderer.sample_current(&source, 0, view));
    view.scale = 1;
    view.manual = true;
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(renderer.source_bytes() >= source.bytes() && renderer.sample_bytes() == 0);
    view.scale = 1.25;
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(renderer.source_bytes() >= source.bytes() && renderer.sample_bytes() > 0);
    view.apply_fit(Fit::Contain);
    renderer.resizing = true;
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(renderer.source_bytes() >= source.bytes());
    renderer.resizing = false;
    view.apply_fit(Fit::Contain);
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(renderer.source_bytes() == 0 && renderer.sample_bytes() > 0);
    CHECK(source.frames[0].pixels == original);
    DestroyWindow(window);
}
TEST(automatic_backend_reuses_small_targets_and_defers_large_upgrade_until_sizing_ends) {
    HWND window = CreateWindowExW(0, L"STATIC", L"GVN backend policy", WS_POPUP, 0, 0, 128, 96, nullptr,
                                  nullptr, nullptr, nullptr);
    CHECK(window);
    Image source;
    source.width = 256;
    source.height = 128;
    source.frames.push_back({std::vector<Pixel>(256 * 128, {51, 77, 151, 127}), 0});
    source.frames[0].pixels[300] = {123, 88, 9, 0};
    const auto original = source.frames[0].pixels;
    View view;
    view.resize(128, 96);
    view.set_image(source.width, source.height);
    Renderer renderer;
    DrawMetrics first;
    renderer.trace = &first;
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(renderer.software_target() && first.target_ms > 0 && first.factory_ms > 0);
    const auto bytes = renderer.sample_bytes();
    DrawMetrics again;
    renderer.trace = &again;
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(again.factory_ms == 0 && again.target_ms == 0 && again.upload_ms >= 0);
    CHECK(renderer.sample_bytes() == bytes && renderer.sample_current(&source, 0, view));
    renderer.backend = RenderBackend::SystemDefault;
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(!renderer.software_target() && renderer.sample_bytes() == bytes);
    renderer.backend = RenderBackend::Automatic;
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(renderer.software_target());
    CHECK(SetWindowPos(window, nullptr, 0, 0, 2048, 1024, SWP_NOZORDER | SWP_NOACTIVATE));
    renderer.resize(2048, 1024);
    view.resize(2048, 1024);
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(renderer.software_target());
    renderer.resizing = true;
    CHECK(SetWindowPos(window, nullptr, 0, 0, 2049, 1024, SWP_NOZORDER | SWP_NOACTIVATE));
    renderer.resize(2049, 1024);
    view.resize(2049, 1024);
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(renderer.software_target());
    renderer.resizing = false;
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(!renderer.software_target());
    CHECK(renderer.sample_current(&source, 0, view) && renderer.source_bytes() == 0);
    CHECK(SetWindowPos(window, nullptr, 0, 0, 128, 96, SWP_NOZORDER | SWP_NOACTIVATE));
    renderer.resize(128, 96);
    view.resize(128, 96);
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(!renderer.software_target());
    renderer.reset_background();
    CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
    CHECK(renderer.software_target() && renderer.sample_current(&source, 0, view));
    CHECK(source.frames[0].pixels == original);
    renderer.trace = nullptr;
    DestroyWindow(window);
}
TEST(area_average_uses_premultiplied_alpha) {
    const std::vector<uint8_t> source{0, 0, 255, 255, 0, 0, 0, 0};
    View v;
    v.resize(1, 1);
    v.set_image(2, 1);
    const auto tile = resample_display(source, 2, 1, v, DisplayIntegra);
    CHECK(tile.width == 1 && tile.height == 1 && tile.bgra.size() == 4);
    CHECK(tile.bgra[0] == 0 && tile.bgra[1] == 0);
    CHECK(tile.bgra[2] >= 127 && tile.bgra[2] <= 128 && tile.bgra[3] == tile.bgra[2]);
    const std::vector<uint8_t> checker{0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255};
    v.set_image(2, 2);
    const auto average = resample_display(checker, 2, 2, v, DisplayIntegra);
    CHECK(average.bgra[0] >= 127 && average.bgra[0] <= 128);
    CHECK(average.bgra[1] == average.bgra[0] && average.bgra[2] == average.bgra[0]);
}
TEST(wic_filters_are_distinct_and_preserve_constant_colors) {
    std::vector<uint8_t> source(8 * 8 * 4);
    for (size_t i = 0; i < source.size(); i += 4) {
        source[i] = 12;
        source[i + 1] = 36;
        source[i + 2] = 60;
        source[i + 3] = 128;
    }
    View v;
    v.resize(24, 24);
    v.set_image(8, 8);
    for (int method = 0; method <= kLastDisplayFilter; ++method) {
        const auto tile = resample_display(source, 8, 8, v, method);
        for (size_t i = 0; i < tile.bgra.size(); ++i)
            CHECK(std::abs(int(tile.bgra[i]) - int(source[i % 4])) <= 1);
    }
    for (size_t i = 0; i < source.size(); i += 4) {
        source[i] = source[i + 1] = source[i + 2] = (i / 4 == 27) ? 255 : 0;
        source[i + 3] = 255;
    }
    const auto linear = resample_display(source, 8, 8, v, 1).bgra;
    const auto cubic = resample_display(source, 8, 8, v, DisplayWicCubic).bgra;
    const auto fant = resample_display(source, 8, 8, v, DisplayWicFant).bgra;
    const auto high = resample_display(source, 8, 8, v, DisplayWicHighCubic).bgra;
    CHECK(linear != cubic && linear != fant && high != linear);
}
TEST(visible_tile_matches_full_scaled_image_and_bounds_memory) {
    std::vector<uint8_t> source(8 * 8 * 4);
    for (size_t i = 0; i < source.size(); i += 4) {
        source[i] = static_cast<uint8_t>(i);
        source[i + 1] = 128;
        source[i + 2] = static_cast<uint8_t>(255 - i);
        source[i + 3] = 255;
    }
    View full;
    full.centered = false;
    full.resize(24, 24);
    full.set_image(8, 8);
    View cropped = full;
    cropped.manual = true;
    cropped.resize(7, 5);
    cropped.pan_x = -5;
    cropped.pan_y = -4;
    for (int method = 0; method <= kLastDisplayFilter; ++method) {
        const auto whole = resample_display(source, 8, 8, full, method);
        const auto tile = resample_display(source, 8, 8, cropped, method);
        const auto x0 = static_cast<size_t>(tile.left + 5), y0 = static_cast<size_t>(tile.top + 4);
        for (size_t y = 0; y < tile.height; ++y)
            for (size_t x = 0; x < tile.width * 4; ++x)
                CHECK(tile.bgra[y * tile.width * 4 + x] == whole.bgra[((y + y0) * whole.width + x0) * 4 + x]);
    }
    source.assign(256 * 256 * 4, 255);
    View huge;
    huge.manual = true;
    huge.centered = false;
    huge.resize(31, 29);
    huge.set_image(256, 256, true);
    huge.scale = 50;
    huge.pan_x = -7000;
    huge.pan_y = -5000;
    const auto tile = resample_display(source, 256, 256, huge, 4);
    CHECK(tile.bgra.size() < 5000); // A full 12800 x 12800 image would be 625 MiB.
    THROWS(resample_display({}, 256, 256, huge, 4));
    THROWS(resample_display(source, 256, 256, huge, kLastDisplayFilter + 1));
}
TEST(native_filters_keep_premultiplied_alpha_and_bounded_workspace) {
    const std::vector<uint8_t> edge{0, 0, 255, 255, 0, 0, 0, 0};
    View up;
    up.centered = false;
    up.resize(50, 25);
    up.set_image(2, 1);
    for (int method = 0; method <= DisplayLanczos3; ++method) {
        const auto tile = resample_display(edge, 2, 1, up, method);
        for (size_t i = 0; i < tile.bgra.size(); i += 4) {
            CHECK(tile.bgra[i] == 0 && tile.bgra[i + 1] == 0);
            CHECK(tile.bgra[i + 2] == tile.bgra[i + 3]);
        }
        CHECK(tile.workspace_bytes <= 34ull * 1024 * 1024);
    }
    std::vector<uint8_t> source(65535 * 4 * 4, 128);
    View cropped;
    cropped.centered = false;
    cropped.manual = true;
    cropped.resize(31, 1);
    cropped.set_image(65535, 4, true);
    cropped.scale = .01;
    cropped.pan_x = -300;
    for (int method : {DisplayIntegra, DisplayLanczos3}) {
        const auto tile = resample_display(source, 65535, 4, cropped, method);
        CHECK(tile.bgra.size() < 200 && tile.workspace_bytes < 2ull * 1024 * 1024);
        CHECK(std::all_of(tile.bgra.begin(), tile.bgra.end(), [](auto value) { return value == 128; }));
    }
}
TEST(native_filters_match_official_reference_pixels) {
    const auto directory = std::filesystem::path(GVN_FIXTURES) / L"resampling";
    size_t cases{}, vectors{}, mismatches{};
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_directory())
            continue;
        const auto source = load_image(entry.path() / L"source.png");
        std::vector<uint8_t> bgra;
        for (const auto pixel : source.frames[0].pixels)
            bgra.insert(bgra.end(), {pixel.b, pixel.g, pixel.r, pixel.a});
        ++cases;
        for (int method = 0; method <= DisplayLanczos3; ++method) {
            const auto path = entry.path() / (L"method-" + std::to_wstring(method) + L".png");
            if (!std::filesystem::exists(path))
                continue;
            const auto expected = load_image(path);
            DisplayTile actual;
            actual.width = expected.width;
            actual.height = expected.height;
            actual.bgra.resize(checked_pixels(actual.width, actual.height) * 4);
            resample_native_tile(bgra, source.width, source.height, actual.width, actual.height, 0, 0, method,
                                 actual);
            int error{};
            for (size_t i = 0; i < expected.frames[0].pixels.size(); ++i) {
                const auto pixel = expected.frames[0].pixels[i];
                for (const auto [channel, value] :
                     {std::pair{0, pixel.b}, {1, pixel.g}, {2, pixel.r}, {3, pixel.a}})
                    error = std::max(error, std::abs(int(actual.bgra[i * 4 + channel]) - int(value)));
            }
            std::cout << "REFERENCE " << entry.path().filename().string() << " method=" << method
                      << " max_channel_error=" << error << '\n';
            if (error > (method == DisplaySimple ? 0 : 2)) {
                ++mismatches;
                const auto debug = std::filesystem::path(GVN_FIXTURES).parent_path().parent_path() /
                                   L".cache/resampling-actual";
                std::filesystem::create_directories(debug);
                Image image;
                image.width = actual.width;
                image.height = actual.height;
                image.frames.push_back({std::vector<Pixel>(checked_pixels(image.width, image.height)), 0});
                for (size_t i = 0; i < image.frames[0].pixels.size(); ++i)
                    image.frames[0].pixels[i] = {actual.bgra[i * 4 + 2], actual.bgra[i * 4 + 1],
                                                 actual.bgra[i * 4], actual.bgra[i * 4 + 3]};
                save_image(
                    image, 0,
                    debug / (entry.path().filename().wstring() + L"-" + std::to_wstring(method) + L".png"));
            }
            ++vectors;
        }
    }
    CHECK(cases >= 8 && vectors >= 70);
    CHECK(mismatches == 0);
}
TEST(native_filters_real_renderer_matches_reference_and_keeps_source) {
    HWND window = CreateWindowExW(0, L"STATIC", L"GVN filter pixel test", WS_POPUP, 0, 0, 320, 240, nullptr,
                                  nullptr, nullptr, nullptr);
    CHECK(window);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), 320, -240, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void *pixels{};
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    CHECK(bitmap && pixels);
    const auto previous = SelectObject(dc, bitmap);
    size_t comparisons{};
    for (const auto &entry :
         std::filesystem::directory_iterator(std::filesystem::path(GVN_FIXTURES) / L"resampling")) {
        if (!entry.is_directory())
            continue;
        const auto source = load_image(entry.path() / L"source.png");
        const auto before = source.frames[0].pixels;
        Renderer renderer;
        renderer.checkerboard = false;
        for (int method = 0; method <= DisplayLanczos3; ++method) {
            const auto expected_path = entry.path() / (L"method-" + std::to_wstring(method) + L".png");
            if (!std::filesystem::exists(expected_path))
                continue;
            const auto expected = load_image(expected_path);
            if (expected.width > 320 || expected.height > 240 ||
                static_cast<uint64_t>(expected.width) * source.height !=
                    static_cast<uint64_t>(expected.height) * source.width)
                continue; // The viewer keeps the image aspect; arbitrary dimensions use the core test.
            View view;
            view.centered = false;
            view.resize(expected.width, expected.height);
            view.set_image(source.width, source.height);
            renderer.down = renderer.up = method;
            CHECK(SUCCEEDED(renderer.print(window, dc, &source, 0, view, 0, nullptr)));
            GdiFlush();
            int error{};
            const auto actual = static_cast<const uint8_t *>(pixels);
            for (size_t y = 0; y < expected.height; ++y)
                for (size_t x = 0; x < expected.width; ++x) {
                    const auto want = expected.frames[0].pixels[y * expected.width + x];
                    const auto offset = (y * 320 + x) * 4;
                    error = std::max({error, std::abs(int(actual[offset]) - want.b),
                                      std::abs(int(actual[offset + 1]) - want.g),
                                      std::abs(int(actual[offset + 2]) - want.r)});
                }
            std::cout << "RENDER_REFERENCE " << entry.path().filename().string() << " method=" << method
                      << " max_channel_error=" << error << '\n';
            CHECK(error <= (method == DisplaySimple ? 0 : 2));
            CHECK(source.frames[0].pixels == before);
            ++comparisons;
        }
    }
    CHECK(comparisons >= 90);
    Image alpha_image;
    alpha_image.width = 2;
    alpha_image.height = 1;
    alpha_image.frames.push_back({{{255, 0, 0, 128}, {0, 255, 0, 0}}, 0});
    const auto original = alpha_image.frames[0].pixels;
    const std::vector<uint8_t> premultiplied{0, 0, 128, 128, 0, 0, 0, 0};
    Renderer alpha_renderer;
    alpha_renderer.checkerboard = false;
    alpha_renderer.background = 0x203040;
    View alpha_view;
    alpha_view.centered = false;
    alpha_view.resize(50, 25);
    alpha_view.set_image(2, 1);
    for (int method = 0; method <= kLastDisplayFilter; ++method) {
        alpha_renderer.down = alpha_renderer.up = method;
        const auto expected = resample_display(premultiplied, 2, 1, alpha_view, method);
        CHECK(SUCCEEDED(alpha_renderer.print(window, dc, &alpha_image, 0, alpha_view, 0, nullptr)));
        GdiFlush();
        const auto actual = static_cast<const uint8_t *>(pixels);
        for (size_t y = 0; y < 25; ++y)
            for (size_t x = 0; x < 50; ++x) {
                const auto input = (y * 50 + x) * 4;
                const auto output = (y * 320 + x) * 4;
                for (size_t channel = 0; channel < 3; ++channel) {
                    const int background[] = {64, 48, 32};
                    const int value = expected.bgra[input + channel] +
                                      (background[channel] * (255 - expected.bgra[input + 3]) + 127) / 255;
                    CHECK(std::abs(int(actual[output + channel]) - value) <= 1);
                }
            }
        CHECK(alpha_image.frames[0].pixels == original);
    }
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DestroyWindow(window);
}
TEST(device_reset_preserves_cpu_tile_and_rebuilds_only_target_resources) {
    HWND window = CreateWindowExW(0, L"STATIC", L"GVN device reset", WS_POPUP, 0, 0, 32, 32, nullptr, nullptr,
                                  nullptr, nullptr);
    CHECK(window);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), 32, -32, 1, 32, BI_RGB};
    void *pixels{};
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    CHECK(bitmap && pixels);
    const auto previous = SelectObject(dc, bitmap);
    Image image;
    image.width = image.height = 8;
    image.frames.push_back({std::vector<Pixel>(64, {255, 0, 0, 128}), 0});
    image.frames[0].pixels[27] = {0, 255, 0, 0};
    const auto source = image.frames[0].pixels;
    View view;
    view.resize(32, 32);
    view.set_image(8, 8);
    view.scale = 2.5;
    Renderer renderer;
    renderer.up = DisplayCubic;
    CHECK(SUCCEEDED(renderer.print(window, dc, &image, 0, view, 0, nullptr)));
    GdiFlush();
    CHECK(renderer.sample_current(&image, 0, view));
    const auto bytes = renderer.sample_bytes();
    CHECK(bytes > 0);
    const std::vector<uint32_t> before(static_cast<uint32_t *>(pixels),
                                       static_cast<uint32_t *>(pixels) + 32 * 32);
    renderer.reset_background();
    CHECK(renderer.sample_bytes() == bytes && renderer.sample_current(&image, 0, view));
    CHECK(SUCCEEDED(renderer.print(window, dc, &image, 0, view, 0, nullptr)));
    GdiFlush();
    CHECK(std::equal(before.begin(), before.end(), static_cast<uint32_t *>(pixels)));
    view.pan_x += .25;
    CHECK(!renderer.sample_current(&image, 0, view));
    CHECK(SUCCEEDED(renderer.print(window, dc, &image, 0, view, 0, nullptr)));
    CHECK(renderer.sample_current(&image, 0, view));
    renderer.up = DisplayBell;
    CHECK(!renderer.sample_current(&image, 0, view));
    renderer.invalidate();
    CHECK(renderer.sample_bytes() == 0 && !renderer.sample_current(&image, 0, view));
    CHECK(image.frames[0].pixels == source);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DestroyWindow(window);
}
TEST(actual_size_wider_than_gpu_bitmap_limit_uses_visible_source_tile) {
    Image source;
    source.width = 65535;
    source.height = 2;
    source.frames.push_back({std::vector<Pixel>(checked_pixels(source.width, source.height)), 0});
    for (size_t i = 0; i < source.frames[0].pixels.size(); ++i)
        source.frames[0].pixels[i] = {static_cast<uint8_t>(i), 33, 99, 255};
    HWND window = CreateWindowExW(0, L"STATIC", L"GVN wide source", WS_POPUP, 0, 0, 128, 32, nullptr, nullptr,
                                  nullptr, nullptr);
    CHECK(window);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), 128, -32, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void *pixels{};
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    CHECK(bitmap && pixels);
    const auto previous = SelectObject(dc, bitmap);
    Renderer renderer;
    renderer.checkerboard = false;
    View view;
    view.centered = false;
    view.manual = true;
    view.resize(128, 32);
    view.set_image(source.width, source.height, true);
    view.pan_x = -65000;
    for (const auto backend :
         {RenderBackend::Automatic, RenderBackend::Software, RenderBackend::SystemDefault}) {
        renderer.backend = backend;
        CHECK(SUCCEEDED(renderer.draw(window, &source, 0, view, 0, 0)));
        CHECK(renderer.sample_bytes() > 0 && renderer.sample_bytes() < 4096);
        CHECK(SUCCEEDED(renderer.print(window, dc, &source, 0, view, 0, nullptr)));
        GdiFlush();
        CHECK(renderer.sample_bytes() > 0 && renderer.sample_bytes() < 4096);
        const auto actual = static_cast<const uint8_t *>(pixels);
        for (size_t x = 0; x < 128; ++x) {
            CHECK(actual[x * 4] == 99 && actual[x * 4 + 1] == 33);
            CHECK(actual[x * 4 + 2] == static_cast<uint8_t>(65000 + x));
        }
    }
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DestroyWindow(window);
}
TEST(display_policy_uses_chosen_quality_for_fit_zoom_editor_and_temporary_fast_drag) {
    Renderer r;View v;v.scale=.5;r.down=DisplayLanczos3;r.up=DisplayLinear;
    CHECK(r.filter(v)==DisplayLanczos3);v.manual=true;CHECK(r.filter(v)==DisplayLanczos3);
    r.selection_mode=true;r.zoom_reduce=false;r.zoom_enlarge=false;r.zoom_simple=r.fit_simple=100;
    CHECK(r.filter(v)==DisplayLanczos3);v.scale=2;CHECK(r.filter(v)==DisplayLinear);
    r.up=DisplayLanczos3;CHECK(r.filter(v)==DisplayLanczos3);r.dragging_low=true;CHECK(r.filter(v)==DisplayLinear);
    r.dragging_low=false;CHECK(r.filter(v)==DisplayLanczos3);r.nearest=true;CHECK(r.filter(v)==DisplaySimple);
    r.nearest=false;v.scale=1;CHECK(r.filter(v)==DisplaySimple);
    v.scale=10;v.canvas_w=v.canvas_h=2000;v.width=8000;v.height=5000;
    CHECK(r.sampling_limited(v)&&r.filter(v)==DisplayLinear);v.width=3840;v.height=2160;
    CHECK(!r.sampling_limited(v)&&r.filter(v)==DisplayLanczos3);
}
TEST(empty_viewport_is_only_dark_checkerboard_after_resize_image_and_device_reset) {
    HWND window = CreateWindowExW(0, L"STATIC", L"GVN empty render test", WS_POPUP, 0, 0, 320, 240, nullptr,
                                  nullptr, nullptr, nullptr);
    CHECK(window);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), 320, -240, 1, 32, BI_RGB};
    void *pixels{};
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    CHECK(bitmap && pixels);
    const auto old = SelectObject(dc, bitmap);
    Renderer renderer;
    renderer.background = 0x123456;
    renderer.checker_light = 0xef1234;
    renderer.checker_dark = 0x12ef34;
    renderer.checker_size = 5;
    renderer.overlay_visible = true;
    renderer.overlay.text = L"This caption must not appear without an image";
    const auto actual = static_cast<const uint32_t *>(pixels);
    View view;
    for (const auto area : {Rect{0, 23, 287, 173}, Rect{0, 0, 320, 240}, Rect{0, 41, 91, 65}}) {
        view.resize(area.w, area.h);
        for (bool enabled : {false, true}) {
            renderer.checkerboard = enabled;
            renderer.reset_background();
            CHECK(SUCCEEDED(renderer.draw(window, nullptr, 0, view, area.y, 0)));
            CHECK(SUCCEEDED(renderer.print(window, dc, nullptr, 0, view, area.y, nullptr)));
            GdiFlush();
            for (int y = 0; y < 240; ++y)
                for (int x = 0; x < 320; ++x) {
                    const bool inside = x < area.w && y >= area.y && y < area.y + area.h;
                    const auto expected =
                        inside ? ((x / 32 + (y - area.y) / 32) % 2 ? 0x303030 : 0x202020) : 0x123456;
                    CHECK((actual[y * 320 + x] & 0xffffff) == expected);
                }
            CHECK(renderer.overlay_rect.empty() && renderer.overlay_layout_builds == 0);
        }
    }
    Image transparent;
    transparent.width = transparent.height = 1;
    transparent.frames.push_back({{{123, 45, 67, 0}}, 0});
    view.centered = false;
    view.resize(320, 240);
    view.set_image(1, 1);
    view.scale = 1;
    renderer.overlay_visible = false;
    for (bool enabled : {true, false}) {
        renderer.checkerboard = enabled;
        CHECK(SUCCEEDED(renderer.draw(window, &transparent, 0, view, 0, 0)));
        CHECK(SUCCEEDED(renderer.print(window, dc, &transparent, 0, view, 0, nullptr)));
        GdiFlush();
        CHECK((actual[0] & 0xffffff) == (enabled ? 0xef1234 : 0x123456));
        CHECK(renderer.source_bytes() > 0);
        CHECK(transparent.frames[0].pixels[0] == Pixel{123, 45, 67, 0});
        CHECK(SUCCEEDED(renderer.draw(window, nullptr, 0, view, 0, 0)));
        CHECK(SUCCEEDED(renderer.print(window, dc, nullptr, 0, view, 0, nullptr)));
        GdiFlush();
        CHECK((actual[0] & 0xffffff) == 0x202020 && (actual[32] & 0xffffff) == 0x303030);
        CHECK(renderer.source_bytes() == 0 && renderer.sample_bytes() == 0);
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DestroyWindow(window);
}
TEST(caption_overlay_real_pixels_background_modes_and_layout_cache) {
    HWND window = CreateWindowExW(0, L"STATIC", L"GVN overlay render test", WS_POPUP, 0, 0, 320, 240, nullptr,
                                  nullptr, nullptr, nullptr);
    CHECK(window);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), 320, -240, 1, 32, BI_RGB};
    void *pixels{};
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    CHECK(bitmap && pixels);
    const auto old = SelectObject(dc, bitmap);
    Image image;
    image.width = 320;
    image.height = 200;
    image.frames.push_back({std::vector<Pixel>(320 * 200, {0x33, 0x44, 0x55, 255}), 100});
    const auto source = image.frames[0].pixels;
    View v;
    v.resize(320, 200);
    v.set_image(320, 200);
    Renderer r;
    r.background = 0x334455;
    r.overlay_visible = true;
    r.overlay.text = L"file.png <100 %>";
    r.overlay.font_size = 24;
    r.overlay.foreground = 0xffffff;
    r.overlay.backcolor = 0x00ff00;
    for (int mode = 0; mode < 3; ++mode) {
        r.overlay.background = mode;
        for (int position = 0; position < 9; ++position) {
            r.overlay.position = position;
            CHECK(SUCCEEDED(r.print(window, dc, &image, 0, v, 20, nullptr)));
            GdiFlush();
            const auto box = r.overlay_rect;
            CHECK(!box.empty() && box.x >= 4 && box.y >= 24 && box.x + box.w <= 316 && box.y + box.h <= 216);
            size_t white{}, green{}, tinted_shadow{};
            for (int y = 0; y < 240; ++y)
                for (int x = 0; x < 320; ++x) {
                    const auto p = static_cast<const uint32_t *>(pixels)[y * 320 + x] & 0xffffff;
                    const int red = (p >> 16) & 255, g = (p >> 8) & 255, blue = p & 255;
                    if (g > red + 40 && g > blue + 40) {
                        CHECK(x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h);
                        ++tinted_shadow;
                    }
                    if (p == 0xffffff || p == 0x00ff00) {
                        CHECK(x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h);
                        white += p == 0xffffff;
                        green += p == 0x00ff00;
                    }
                }
            CHECK(white > 10);
            CHECK(mode == 0 ? tinted_shadow == 0 : tinted_shadow > 10);
            CHECK(mode != 1 || green > 10);
        }
    }
    const auto builds = r.overlay_layout_builds;
    CHECK(SUCCEEDED(r.print(window, dc, &image, 0, v, 20, nullptr)));
    CHECK(r.overlay_layout_builds == builds);
    r.overlay.text = std::wstring(500, L'X') + L" 日本語.png <100 %>";
    r.overlay.italic = r.overlay.underline = r.overlay.strikeout = true;
    r.overlay.weight = 700;
    CHECK(SUCCEEDED(r.print(window, dc, &image, 0, v, 20, nullptr)));
    CHECK(r.overlay_rect.w == 312 && r.overlay_layout_builds == builds + 1);
    // The rightmost glyphs must be the same as the suffix alone, not the long prefix.
    GdiFlush();
    const std::vector<uint32_t> long_caption(static_cast<uint32_t *>(pixels),
                                             static_cast<uint32_t *>(pixels) + 320 * 240);
    r.overlay.text = L"日本語.png <100 %>";
    CHECK(SUCCEEDED(r.print(window, dc, &image, 0, v, 20, nullptr)));
    GdiFlush();
    size_t compared_ink{}, differences{};
    for (int y = r.overlay_rect.y; y < r.overlay_rect.y + r.overlay_rect.h; ++y)
        for (int x = 260; x < 314; ++x) {
            const auto a = long_caption[y * 320 + x] & 0xffffff;
            const auto b = static_cast<const uint32_t *>(pixels)[y * 320 + x] & 0xffffff;
            compared_ink += a != r.background;
            differences += a != b;
        }
    CHECK(compared_ink > 20 && differences == 0);
    r.overlay_visible = false;
    CHECK(SUCCEEDED(r.print(window, dc, &image, 0, v, 20, nullptr)));
    CHECK(r.overlay_rect.empty() && image.frames[0].pixels == source);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    DestroyWindow(window);
}
int main(int argc, char **argv) {
    const auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
    if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 3;
    const auto desktop=CreateDesktopW(L"GVN-RenderTests",nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop||!SetThreadDesktop(desktop))return 4;
    if (argc > 1) {
        std::erase_if(all_tests(), [&](const auto &test) { return std::string(test.name) != argv[1]; });
        if (all_tests().empty()) return 2;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 2;
    const int result = run_tests();
    CoUninitialize();
    return result;
}


#include "background_test_support.h"
TEST(viewport_background_matches_all_twenty_actual_reference_clients) {
    gvn_test::BackgroundSurface surface(640,480);
    Renderer renderer;renderer.nearest=true;
    auto source=load_image(gvn_test::background_root/L"source.png");
    const auto original=source.frames[0].pixels;
    View view;view.resize(620,387);view.set_image(7,11);view.scale=1;view.manual=true;
    size_t compared{};
    for(const auto &item:gvn_test::background_cases()) {
        gvn_test::set_view_background(renderer,item.tiled,item.preset,item.first,item.second);
        gvn_test::reset_pattern_resources(renderer);
        CHECK(SUCCEEDED(renderer.print(surface.window,surface.dc,&source,0,view,23,nullptr)));
        GdiFlush();const auto expected=load_image(gvn_test::background_root/item.expected);
        CHECK(expected.width==620 && expected.height==387);
        for(int y=0;y<387;++y)for(int x=0;x<620;++x) {
            const auto pixel=expected.frames[0].pixels[y*620+x];
            const uint32_t rgb=(uint32_t(pixel.r)<<16)|(uint32_t(pixel.g)<<8)|pixel.b;
            if(surface.color(x,y+23)!=rgb)
                throw std::runtime_error("background palette="+std::to_string(item.palette)+" preset="+std::to_string(item.preset)+" tiled="+std::to_string(item.tiled)+" x="+std::to_string(x)+" y="+std::to_string(y));
        }
        CHECK(source.frames[0].pixels==original);++compared;
    }
    CHECK(compared==20);
}
TEST(empty_background_respects_palette_and_tile_while_alpha_controls_stay_independent) {
    gvn_test::BackgroundSurface surface(320,240);
    Renderer renderer;View view;view.resize(287,173);
    renderer.checker_light=0xff0011;renderer.checker_dark=0x00ff22;renderer.checker_size=5;
    for(bool tiled:{true,false})for(bool alpha_checker:{true,false}) {
        renderer.checkerboard=alpha_checker;
        gvn_test::set_view_background(renderer,tiled,4,0x17334f,0x597b9d);
        gvn_test::reset_pattern_resources(renderer);
        CHECK(SUCCEEDED(renderer.print(surface.window,surface.dc,nullptr,0,view,23,nullptr)));GdiFlush();
        for(int y=0;y<173;++y)for(int x=0;x<287;++x) {
            const auto expected=tiled && ((x/128+y/128)&1)?0x597b9d:0x17334f;
            CHECK(surface.color(x,y+23)==expected);
        }
    }
}
TEST(alpha_checker_and_its_solid_fallback_do_not_replace_the_outer_tile) {
    gvn_test::BackgroundSurface surface(320,240);
    Renderer renderer;renderer.nearest=true;
    View view;view.centered=false;view.resize(320,240);view.set_image(1,1);view.scale=1;view.manual=true;
    Image image;image.width=image.height=1;image.frames.push_back({{{90,70,60,0}},100});
    const auto original=image.frames[0].pixels;
    gvn_test::set_view_background(renderer,true,2,0x112233,0x8899aa);
    renderer.background=0xabcdef;renderer.checker_light=0x00ff00;renderer.checker_dark=0xff0000;
    for(bool alpha_checker:{true,false}) {
        renderer.checkerboard=alpha_checker;
        CHECK(SUCCEEDED(renderer.print(surface.window,surface.dc,&image,0,view,0,nullptr)));GdiFlush();
        CHECK(surface.color(0,0)==(alpha_checker?0x00ff00u:0xabcdefu));
        CHECK(surface.color(16,0)==0x112233 && surface.color(32,0)==0x8899aa);
        CHECK(image.frames[0].pixels==original);
    }
}
TEST(background_pattern_change_keeps_d2d_target_image_and_sampling_cache) {
    gvn_test::BackgroundSurface surface(128,96);
    Renderer renderer;View view;view.resize(128,96);view.set_image(256,128);
    Image image;image.width=256;image.height=128;
    image.frames.push_back({std::vector<Pixel>(256*128,{120,70,30,128}),100});
    CHECK(SUCCEEDED(renderer.draw(surface.window,&image,0,view,0,0)));
    const auto source_bytes=renderer.source_bytes(),sample_bytes=renderer.sample_bytes();
    gvn_test::set_view_background(renderer,true,4,0x17334f,0x597b9d);
    renderer.checker_light=0x2468ac;
    gvn_test::reset_pattern_resources(renderer);
    DrawMetrics timing;renderer.trace=&timing;
    CHECK(SUCCEEDED(renderer.draw(surface.window,&image,0,view,0,0)));
    CHECK(timing.target_ms==0 && timing.factory_ms==0);
    CHECK(renderer.source_bytes()==source_bytes && renderer.sample_bytes()==sample_bytes);
}

TEST(background_contexts_match_official_zoom_pan_fullscreen_and_empty_rois) {
    const auto root = std::filesystem::path(GVN_FIXTURES) / L"window-background-contexts";
    auto source = load_image(root / L"source.png");
    const auto original = source.frames[0].pixels;
    std::ifstream stream(root / L"cases.tsv"); CHECK(stream);
    size_t cases{}, compared{};
    for (std::string line; std::getline(stream, line);) {
        std::istringstream row(line); std::array<std::string,17> f;
        for (auto &field : f) CHECK(std::getline(row,field,'\t'));
        const int width=std::stoi(f[5]),height=std::stoi(f[6]),x0=std::stoi(f[7]),y0=std::stoi(f[8]);
        const int rw=std::stoi(f[9]),rh=std::stoi(f[10]);
        const Rect reference_box{std::stoi(f[11]),std::stoi(f[12]),std::stoi(f[13]),std::stoi(f[14])};
        const bool has_image=reference_box.w>0;
        // Fullscreen corner ROIs do not contain the centered source; the image still exists.
        const bool empty=f[0].find("empty")!=std::string::npos;
        gvn_test::BackgroundSurface surface(width,height+23);
        Renderer renderer; renderer.nearest=true;
        gvn_test::set_view_background(renderer,std::stoi(f[1])!=0,std::stoi(f[2]),
                                      static_cast<uint32_t>(std::stoul(f[3])),static_cast<uint32_t>(std::stoul(f[4])));
        View view; view.resize(width,height); view.set_image(7,11);
        view.manual=true; view.scale=std::stod(f[15])/100;
        if(f[0].find("after-pan")!=std::string::npos) view.pan_y=-32;
        CHECK(SUCCEEDED(renderer.print(surface.window,surface.dc,empty?nullptr:&source,0,view,23,nullptr)));
        GdiFlush(); const auto expected=load_image(root/f[16]);
        CHECK(expected.width==rw && expected.height==rh);
        const auto in_box=[](int x,int y,const Rect &box,int margin) {
            return box.w>0 && box.h>0 && x>=box.x-margin && y>=box.y-margin &&
                   x<box.x+box.w+margin && y<box.y+box.h+margin;
        };
        const Rect current_box{static_cast<int>(std::floor(view.left())),static_cast<int>(std::floor(view.top())),
                               static_cast<int>(std::ceil(source.width*view.scale)),static_cast<int>(std::ceil(source.height*view.scale))};
        size_t pixels{};
        for(int y=0;y<rh;++y) for(int x=0;x<rw;++x) {
            if((has_image && in_box(x,y,reference_box,2)) || (!empty && in_box(x+x0,y+y0,current_box,2))) continue;
            const auto pixel=expected.frames[0].pixels[y*rw+x];
            const uint32_t rgb=(uint32_t(pixel.r)<<16)|(uint32_t(pixel.g)<<8)|pixel.b;
            if(surface.color(x+x0,y+y0+23)!=rgb)
                throw std::runtime_error("official background context "+f[0]+" x="+std::to_string(x+x0)+" y="+std::to_string(y+y0));
            ++pixels;
        }
        CHECK(pixels>=static_cast<size_t>(rw)*rh/4 && source.frames[0].pixels==original);
        compared+=pixels; ++cases;
    }
    CHECK(cases==52 && compared>10000000);
}
TEST(caption_native_fonts_match_windows_gdi_pixels_and_keep_source_and_cache) {
    gvn_test::BackgroundSurface actual(640, 240), expected(640, 240);
    Image image; image.width=640;image.height=200;
    image.frames.push_back({std::vector<Pixel>(640*200, {0,0,0,255}),100});
    const auto source=image.frames[0].pixels;
    View view;view.resize(640,200);view.set_image(640,200);
    Renderer renderer;renderer.overlay_visible=true;
    renderer.overlay.text=L"Abg 0123 日本語。（）";
    renderer.overlay.background=0;renderer.overlay.foreground=0xffffff;
    renderer.background_tiled=false;renderer.background_first=0;
    for (const auto &family : {L"System",L"@MS Gothic"}) {
        renderer.overlay.family=family;
        renderer.overlay.charset=std::wstring(family).starts_with(L"@")?SHIFTJIS_CHARSET:DEFAULT_CHARSET;
        renderer.overlay.font_size=std::wstring(family).starts_with(L"@")?32.0f:16.0f;
        renderer.overlay.underline=true;renderer.overlay.strikeout=true;
        CHECK(SUCCEEDED(renderer.print(actual.window,actual.dc,&image,0,view,20,nullptr)));
        CHECK(renderer.overlay_uses_gdi() && !renderer.overlay_rect.empty());
        GdiFlush();
        LOGFONTW lf{};lf.lfHeight=-int(renderer.overlay.font_size);lf.lfWeight=400;
        lf.lfCharSet=BYTE(renderer.overlay.charset);lf.lfQuality=ANTIALIASED_QUALITY;
        lf.lfUnderline=lf.lfStrikeOut=true;
        wcsncpy_s(lf.lfFaceName,family,_TRUNCATE);
        const auto font=CreateFontIndirectW(&lf);CHECK(font);
        const auto old=SelectObject(expected.dc,font);
        wchar_t selected[64]{};CHECK(GetTextFaceW(expected.dc,64,selected)>0);
        CHECK(std::wstring(selected)==family ||
              (std::wstring(family)==L"@MS Gothic" && std::wstring(selected)==L"@ＭＳ ゴシック"));
        TEXTMETRICW metrics{};CHECK(GetTextMetricsW(expected.dc,&metrics));
        CHECK(std::wstring(family).starts_with(L"@") || !(metrics.tmPitchAndFamily&TMPF_TRUETYPE));
        std::memset(expected.pixels,0,640*240*4);SetBkMode(expected.dc,TRANSPARENT);
        SetTextColor(expected.dc,RGB(255,255,255));
        CHECK(TextOutW(expected.dc,renderer.overlay_rect.x+2,renderer.overlay_rect.y+2,
                       renderer.overlay.text.c_str(),int(renderer.overlay.text.size())));
        GdiFlush();size_t ink{};
        for(int y=20;y<220;++y)for(int x=0;x<640;++x) {
            CHECK(actual.color(x,y)==expected.color(x,y));ink+=actual.color(x,y)!=0;
        }
        CHECK(ink>30);
        const auto builds=renderer.overlay_layout_builds;
        CHECK(SUCCEEDED(renderer.print(actual.window,actual.dc,&image,0,view,20,nullptr)));
        CHECK(renderer.overlay_layout_builds==builds);
        CHECK(renderer.overlay_mask_bytes()<=640*200*4);
        SelectObject(expected.dc,old);DeleteObject(font);
    }
    renderer.overlay.text=std::wstring(500,L'X')+L" file 日本語.png";
    renderer.overlay.dpi=192;renderer.overlay.font_size=48;renderer.overlay.background=2;
    renderer.overlay.backcolor=0x00ff00;renderer.overlay.position=8;
    CHECK(SUCCEEDED(renderer.print(actual.window,actual.dc,&image,0,view,20,nullptr)));
    CHECK(renderer.overlay_rect.w<=624 && renderer.overlay_rect.y+renderer.overlay_rect.h<=212);
    renderer.overlay.family=L"Arial";renderer.overlay.font_size=32768.0f*96/72;
    CHECK(SUCCEEDED(renderer.print(actual.window,actual.dc,&image,0,view,20,nullptr)));
    CHECK(!renderer.overlay_uses_gdi() && renderer.overlay_mask_bytes()==0);
    CHECK(renderer.overlay_rect.w<=624 && renderer.overlay_rect.h<=184);
    CHECK(image.frames[0].pixels==source);
}
TEST(lanczos_real_pixels_are_identical_for_fit_manual_and_selection_and_differ_from_unfiltered) {
    const auto window=CreateWindowExW(0,L"STATIC",L"GVN quality pixels",WS_POPUP,0,0,320,240,nullptr,nullptr,nullptr,nullptr);CHECK(window);
    const auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),320,-240,1,32,BI_RGB};void *data{};
    const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);CHECK(bitmap&&data);const auto old=SelectObject(dc,bitmap);
    Image image;image.width=96;image.height=80;image.frames.push_back({std::vector<Pixel>(96*80),100});
    for(unsigned y=0;y<80;++y)for(unsigned x=0;x<96;++x)image.frames[0].pixels[y*96+x]={uint8_t((x*17+y*23)%256),uint8_t((x+y)%2?255:0),uint8_t(y*11),uint8_t(40+(x*19+y*31)%216)};
    const auto source=image.frames[0].pixels;Renderer renderer;renderer.down=renderer.up=DisplayLanczos3;
    View view;view.centered=false;view.resize(320,240);view.set_image(96,80);
    const auto capture=[&]{CHECK(SUCCEEDED(renderer.print(window,dc,&image,0,view,0,nullptr)));GdiFlush();return std::vector<uint32_t>(static_cast<uint32_t*>(data),static_cast<uint32_t*>(data)+320*240);};
    for(double scale:{.37,2.25}){
        view.scale=scale;view.manual=false;renderer.down=renderer.up=DisplayLanczos3;renderer.selection_mode=false;
        const auto quality=capture();view.manual=true;renderer.selection_mode=true;renderer.fit_simple=renderer.zoom_simple=100;renderer.zoom_reduce=renderer.zoom_enlarge=false;
        CHECK(capture()==quality&&renderer.filter(view)==DisplayLanczos3);
        renderer.down=renderer.up=DisplaySimple;const auto unfiltered=capture();CHECK(unfiltered!=quality);
        renderer.down=renderer.up=DisplayLinear;const auto fast=capture();CHECK(fast!=quality&&fast!=unfiltered);
        CHECK(image.frames[0].pixels==source);
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);DestroyWindow(window);
}
