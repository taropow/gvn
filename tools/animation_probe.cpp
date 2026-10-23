#include <windows.h>
#include <bcrypt.h>
#include "gvn/codecs.h"
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

static std::string digest(const gvn::Frame &frame) {
    std::array<unsigned char, 32> output{};
    const auto bytes = frame.pixels.size() * sizeof(gvn::Pixel);
    if (bytes > std::numeric_limits<ULONG>::max() || BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<gvn::Pixel *>(frame.pixels.data())),
        static_cast<ULONG>(bytes), output.data(), static_cast<ULONG>(output.size())) < 0)
        throw std::runtime_error("Cannot hash frame");
    std::ostringstream text;
    for (auto value : output)
        text << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(value);
    return text.str();
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 3 || argc > 5)
        return 2;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized))
        return 3;
    struct Scope { ~Scope() { CoUninitialize(); } } scope;
    std::ofstream output(std::filesystem::path(argv[2]), std::ios::binary);
    if (!output)
        return 4;
    try {
        gvn::LoadingOptions options;
        if (argc > 3)
            options.gif_speed = std::stoi(argv[3]);
        const auto limit = argc > 4 ? std::stoull(argv[4]) : gvn::kDecodeLimit;
        const auto image = gvn::load_image(argv[1], limit, options);
        output << "{\"passed\":true,\"width\":" << image.width << ",\"height\":" << image.height
               << ",\"frames\":" << image.frames.size() << ",\"loops\":" << image.file_loops
               << ",\"compensate\":" << (image.animation_compensate ? "true" : "false")
               << ",\"image_bytes\":" << image.bytes() << ",\"frame_data\":[";
        for (size_t i = 0; i < image.frames.size(); ++i) {
            if (i)
                output << ',';
            output << "{\"duration_ms\":" << image.frames[i].duration_ms << ",\"sha256\":\""
                   << digest(image.frames[i]) << "\"}";
        }
        output << "]}\n";
        return 0;
    } catch (const std::exception &) {
        output << "{\"passed\":false}\n";
        return 1;
    }
}
