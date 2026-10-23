// Fresh-process timings of GVN's real renderer; owns only a hidden HWND.
#include "gvn/codecs.h"
#include "gvn/renderer.h"
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <fstream>
#include <iostream>

using namespace gvn;
int wmain(int argc, wchar_t **argv) {
    if (argc != 5 || FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 2;
    try {
        const std::wstring mode = argv[1];
        if (mode != L"default" && mode != L"software")
            return 3;
        const bool empty = std::wstring(argv[2]) == L"-";
        const auto image = empty ? Image{} : load_image(argv[2]);
        const int width = std::stoi(argv[4]);
        if (width < 100 || width > 7680)
            return 4;
        const int height = width * 69 / 100;
        const auto hwnd = CreateWindowExW(0, L"STATIC", L"GVN renderer diagnostics", WS_POPUP, 0, 0, width,
                                          height, nullptr, nullptr, nullptr, nullptr);
        if (!hwnd)
            return 5;
        Renderer renderer;
        renderer.backend = mode == L"software" ? RenderBackend::Software : RenderBackend::SystemDefault;
        View view;
        view.resize(width, height);
        if (!empty)
            view.set_image(image.width, image.height);
        std::ofstream output{std::filesystem::path(argv[3])};
        output << "{\"mode\":\"" << (mode == L"software" ? "software" : "default")
               << "\",\"empty\":" << (empty ? "true" : "false") << ",\"width\":" << width
               << ",\"height\":" << height << ",\"draws\":[";
        for (int draw = 0; draw < 32; ++draw) {
            DrawMetrics metrics;
            renderer.trace = &metrics;
            const auto started = std::chrono::steady_clock::now();
            if (draw == 11)
                view.manual = true;
            if (draw > 10) {
                view.pan_x -= 1;
            }
            const auto result = renderer.draw(hwnd, empty ? nullptr : &image, 0, view, 0, 0);
            const auto total =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            if (FAILED(result))
                throw std::runtime_error("Probe rendering failed");
            output << (draw ? "," : "") << "{\"total_ms\":" << total
                   << ",\"factory_ms\":" << metrics.factory_ms << ",\"target_ms\":" << metrics.target_ms
                   << ",\"brush_ms\":" << metrics.brush_ms << ",\"sampling_ms\":" << metrics.sampling_ms
                   << ",\"upload_ms\":" << metrics.upload_ms << ",\"background_ms\":" << metrics.background_ms
                   << ",\"scene_ms\":" << metrics.scene_ms << "}";
        }
        PROCESS_MEMORY_COUNTERS_EX memory{sizeof(memory)};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                             sizeof(memory));
        output << "],\"private_bytes\":" << memory.PrivateUsage
               << ",\"working_set\":" << memory.WorkingSetSize << "}\n";
        renderer.trace = nullptr;
        DestroyWindow(hwnd);
        CoUninitialize();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        CoUninitialize();
        return 1;
    }
    return 0;
}
