#include "gvn/renderer.h"
#include "../tests/test_desktop.h"
#include <chrono>
#include <iostream>
#include <psapi.h>

using namespace gvn;
using Clock = std::chrono::steady_clock;
static void check(bool ok) { if (!ok) throw std::runtime_error("Native render benchmark failed"); }
static size_t private_bytes() {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    check(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)));
    return memory.PrivateUsage;
}
static uint64_t hash(std::span<const uint8_t> bytes) {
    uint64_t result = 14695981039346656037ull;
    for (auto byte : bytes) result = (result ^ byte) * 1099511628211ull;
    return result;
}
int main() {
    const auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !gvn_test::noninteractive(station) || !SetProcessWindowStation(station)) return 3;
    const auto desktop = CreateDesktopW(L"GVN-RenderBenchmark", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop)) return 4;
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 5;
    try {
        HWND window = CreateWindowExW(0, L"STATIC", L"Owned render benchmark", WS_POPUP,
                                      0, 0, 960, 640, nullptr, nullptr, nullptr, nullptr);
        check(window != nullptr);
        std::cout << "{\"method\":\"Synthetic RGBA, private desktop, native draw including EndDraw; excludes file decode and DWM presentation\",\"cases\":[";
        bool first = true;
        for (auto size : {SIZE{4096, 3072}, SIZE{8192, 4096}}) {
            auto image = new_image(size.cx, size.cy);
            for (size_t n = 0; n < image.frames[0].pixels.size(); ++n)
                image.frames[0].pixels[n] = {uint8_t(n * 7 + 3), uint8_t(n * 29 + 17), uint8_t(n * 13), uint8_t(n % 256)};
            for (int method : {DisplaySimple, DisplayLinear, DisplayLanczos3})
                for (double scale : {0.0, .75, 2.5, 50.0}) {
                    View base; base.resize(960, 640); base.set_image(image.width, image.height);
                    if (scale) { base.manual = true; base.centered = false; base.scale = scale; base.pan_x = -271 * scale; base.pan_y = -193 * scale; }
                    const auto expected = resample_rgba_display(image.frames[0].pixels, image.width, image.height, base, method);
                    if (!first) std::cout << ','; first = false;
                    std::cout << "{\"width\":" << size.cx << ",\"height\":" << size.cy << ",\"filter\":" << method
                              << ",\"scale\":" << scale << ",\"tile_hash\":\"" << hash(expected.bgra) << "\",\"samples\":[";
                    for (int repeat = 0; repeat < 5; ++repeat) {
                        Renderer renderer; renderer.down = renderer.up = method;
                        View view = base; DrawMetrics metrics; renderer.trace = &metrics;
                        const auto before = private_bytes(); const auto start = Clock::now();
                        check(SUCCEEDED(renderer.draw(window, &image, 0, view, 0, 0)));
                        const double first_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                        const auto first_sampling_ms = metrics.sampling_ms;
                        const auto after = private_bytes(); const auto source_bytes = renderer.source_bytes();
                        double pan_ms = 0;
                        for (int pan = 0; pan < 3; ++pan) {
                            view.pan_x -= 19; view.pan_y -= 11;
                            const auto at = Clock::now(); check(SUCCEEDED(renderer.draw(window, &image, 0, view, 0, 0)));
                            pan_ms += std::chrono::duration<double, std::milli>(Clock::now() - at).count();
                        }
                        if (repeat) std::cout << ',';
                        std::cout << "{\"first_ms\":" << first_ms << ",\"sampling_ms\":" << first_sampling_ms
                                  << ",\"pan_ms\":" << pan_ms / 3 << ",\"source_bytes\":" << source_bytes
                                  << ",\"workspace_bytes\":" << renderer.sample_workspace_bytes()
                                  << ",\"private_delta\":" << (after > before ? after - before : 0) << '}';
                    }
                    std::cout << "]}" << std::flush;
                }
        }
        std::cout << "]}\n";
        DestroyWindow(window); CoUninitialize(); return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
