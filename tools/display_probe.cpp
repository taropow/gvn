// Compare source conversion strategies with identical pixels and view changes.
#include "gvn/codecs.h"
#include "gvn/resample.h"
#include "../src/pixel_conversion.h"
#include <windows.h>
#include <chrono>
#include <fstream>
#include <iostream>

using namespace gvn;
int wmain(int argc, wchar_t **argv) {
    if (argc != 3 || FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 2;
    try {
        const auto image = load_image(argv[1]);
        const auto &rgba = image.frames[0].pixels;
        const auto fingerprint = [&] {
            uint64_t hash = 1469598103934665603ull;
            for (const auto p : rgba)
                for (const auto value : {p.r, p.g, p.b, p.a})
                    hash = (hash ^ value) * 1099511628211ull;
            return hash;
        };
        const auto original = fingerprint();
        const auto measured = [](const auto &work) {
            const auto started = std::chrono::steady_clock::now();
            work();
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        };
        std::ofstream output{std::filesystem::path(argv[2])};
        output << "{\"width\":" << image.width << ",\"height\":" << image.height
               << ",\"source_bytes\":" << image.bytes() << ",\"samples\":[";
        bool comma{};
        for (int method = 0; method <= kLastDisplayFilter; ++method)
            for (int sample = 0; sample < 3; ++sample) {
                View view;
                view.resize(1000, 690);
                view.set_image(image.width, image.height);
                std::vector<uint8_t> full, optimized;
                DisplayTile baseline, borrowed, cached_tile;
                double before{}, after{}, cached{};
                const auto old_initial = [&] {
                    before = measured([&] {
                        full.resize(rgba.size() * 4);
                        for (size_t i = 0; i < rgba.size(); ++i) {
                            const auto p = rgba[i];
                            full[i * 4] = static_cast<uint8_t>((p.b * p.a + 127) / 255);
                            full[i * 4 + 1] = static_cast<uint8_t>((p.g * p.a + 127) / 255);
                            full[i * 4 + 2] = static_cast<uint8_t>((p.r * p.a + 127) / 255);
                            full[i * 4 + 3] = p.a;
                        }
                        baseline = resample_display(full, image.width, image.height, view, method);
                    });
                };
                const auto new_initial = [&] {
                    after = measured([&] {
                        borrowed = resample_rgba_display(rgba, image.width, image.height, view, method);
                    });
                };
                if (sample % 2) {
                    new_initial();
                    old_initial();
                } else {
                    old_initial();
                    new_initial();
                }
                cached = measured([&] {
                    optimized.resize(rgba.size() * 4);
                    detail::premultiply_rgba(reinterpret_cast<const uint8_t *>(rgba.data()), optimized.data(),
                                             rgba.size());
                    cached_tile = resample_display(optimized, image.width, image.height, view, method);
                });
                if (cached_tile.bgra != baseline.bgra)
                    throw std::runtime_error("SIMD cached display pixels differ");
                if (optimized != full)
                    throw std::runtime_error("SIMD source conversion differs");
                if (baseline.bgra != borrowed.bgra)
                    throw std::runtime_error("Initial display pixels differ");
                if (comma)
                    output << ',';
                comma = true;
                output << "{\"method\":" << method << ",\"sample\":" << sample
                       << ",\"legacy_initial_ms\":" << before << ",\"borrowed_initial_ms\":" << after
                       << ",\"simd_cached_initial_ms\":" << cached
                       << ",\"conversion_bytes\":" << borrowed.conversion_bytes
                       << ",\"workspace_bytes\":" << borrowed.workspace_bytes << ",\"changes\":[";
                for (int change = 0; change < 3; ++change) {
                    if (change != 1)
                        view.resize(940 + change * 20, 640 + change * 10);
                    view.apply_fit(Fit::Contain);
                    view.pan_x = (change - 1) * 11.25;
                    view.pan_y = (change - 1) * 7.5;
                    const auto old_warm = [&] {
                        before = measured([&] {
                            baseline = resample_display(full, image.width, image.height, view, method);
                        });
                    };
                    const auto new_warm = [&] {
                        after = measured([&] {
                            borrowed = resample_rgba_display(rgba, image.width, image.height, view, method);
                        });
                    };
                    if ((sample + change) % 2) {
                        new_warm();
                        old_warm();
                    } else {
                        old_warm();
                        new_warm();
                    }
                    if (baseline.bgra != borrowed.bgra)
                        throw std::runtime_error("Changed display pixels differ");
                    output << (change ? "," : "") << "{\"legacy_cached_ms\":" << before
                           << ",\"borrowed_ms\":" << after << '}';
                }
                output << "]}";
            }
        if (fingerprint() != original)
            throw std::runtime_error("Source RGBA was modified");
        output << "],\"source_unchanged\":true,\"all_pixels_equal\":true}\n";
        if (!output)
            throw std::runtime_error("Cannot write display measurements");
        CoUninitialize();
        std::cout << "PASS all 14 filters, 3 initial samples and 3 cached view changes per sample\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        CoUninitialize();
        return 1;
    }
}
