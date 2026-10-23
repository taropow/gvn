#pragma once
#include "gvn/codecs.h"
#include "test.h"
#include <array>
#include <fstream>
#include <sstream>

namespace gvn_test {
struct RotationFormatCase {
    std::string kind, shape, source, indices, expected;
    int mode{};
    uint32_t width{}, height{};
    size_t frames{};
};
inline const auto rotation_formats_root = std::filesystem::path(GVN_FIXTURES) / L"view-rotation-formats";
inline const std::vector<RotationFormatCase> &rotation_format_cases() {
    static const auto cases = [] {
        std::ifstream input(rotation_formats_root / L"cases.tsv");
        CHECK(input);
        std::vector<RotationFormatCase> result;
        for (std::string line; std::getline(input, line);) {
            std::istringstream stream(line);
            std::array<std::string, 9> fields;
            for (auto &field : fields)
                CHECK(std::getline(stream, field, '\t'));
            result.push_back({fields[0], fields[1], fields[3], fields[7], fields[8], std::stoi(fields[2]),
                              static_cast<uint32_t>(std::stoul(fields[4])),
                              static_cast<uint32_t>(std::stoul(fields[5])), std::stoul(fields[6])});
        }
        CHECK(result.size() == 120);
        return result;
    }();
    return cases;
}
inline void check_rotation_format(const gvn::Image &actual, const gvn::Image &decoded,
                                  const RotationFormatCase &item, const std::filesystem::path &source) {
    const auto context = item.kind + "/" + item.shape + " mode=" + std::to_string(item.mode);
    const auto fail = [&](const char *what) { throw std::runtime_error(context + ": " + what); };
    if (actual.width != item.width || actual.height != item.height || actual.frames.size() != item.frames ||
        decoded.frames.size() != item.frames)
        fail("dimensions or frame count");
    if (actual.format != decoded.format || actual.path != source || actual.file_bytes != decoded.file_bytes ||
        actual.file_loops != decoded.file_loops || actual.animation_compensate != decoded.animation_compensate)
        fail("source/animation metadata");
    if (item.frames == 2 && actual.file_loops != 0)
        fail("infinite GIF loop extension");
    const auto map = gvn::read_file(rotation_formats_root / item.indices);
    const size_t count = static_cast<size_t>(actual.width) * actual.height;
    CHECK(map.size() == count * 2);
    for (size_t frame = 0; frame < item.frames; ++frame) {
        const auto &a = actual.frames[frame];
        const auto &before = decoded.frames[frame];
        const auto rgba = gvn::read_file(rotation_formats_root / (item.expected + "-frame" + std::to_string(frame) + ".rgba"));
        CHECK(rgba.size() == count * 4 && a.pixels.size() == count && before.pixels.size() == count);
        if (a.duration_ms != before.duration_ms || a.duration_ms != (item.frames == 2 ? (frame ? 180 : 120) : 100))
            fail("frame duration");
        for (size_t pixel = 0; pixel < count; ++pixel) {
            const size_t index = map[pixel * 2] | (size_t(map[pixel * 2 + 1]) << 8);
            CHECK(index < count);
            if (a.pixels[pixel] != before.pixels[index])
                fail("whole decoded RGBA coordinate permutation");
            const auto value = a.pixels[pixel];
            const gvn::Pixel expected{rgba[pixel * 4], rgba[pixel * 4 + 1], rgba[pixel * 4 + 2], rgba[pixel * 4 + 3]};
            if (value.a != expected.a || ((expected.a || item.kind == "png-alpha") && value != expected))
                fail("independent RGBA fixture");
        }
        if (before.indexed) {
            if (!a.indexed || !a.indexed->valid() || !a.indexed->matches(a.pixels) ||
                a.indexed->palette != before.indexed->palette || a.indexed->bits != before.indexed->bits)
                fail("original palette");
            for (size_t pixel = 0; pixel < count; ++pixel) {
                const size_t index = map[pixel * 2] | (size_t(map[pixel * 2 + 1]) << 8);
                if (a.indexed->index(static_cast<uint32_t>(pixel % actual.width), static_cast<uint32_t>(pixel / actual.width)) !=
                    before.indexed->index(static_cast<uint32_t>(index % decoded.width), static_cast<uint32_t>(index / decoded.width)))
                    fail("palette coordinate permutation");
            }
        }
    }
}
} // namespace gvn_test
