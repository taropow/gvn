#pragma once
#include "gvn/edit.h"
#include "test.h"
#include <array>
#include <fstream>
#include <sstream>

namespace gvn_test {
struct MaskReference {
    std::string label, kind;
    int size{}, swap{}, negative{}, xor_channels{}, q0{};
    bool remainder{}, fl_negative{};
    gvn::Image expected;
};
inline gvn::Image mask_rgb(std::istream &input) {
    gvn::Image image;
    image.width = 29; image.height = 19;
    image.frames.push_back({std::vector<gvn::Pixel>(29 * 19), 100});
    for (auto &pixel : image.frames[0].pixels) {
        std::array<unsigned char, 3> rgb{};
        CHECK(input.read(reinterpret_cast<char *>(rgb.data()), rgb.size()));
        pixel = {rgb[0], rgb[1], rgb[2], 255};
    }
    return image;
}
inline gvn::Image mask_reference_source() {
    std::ifstream input(std::filesystem::path(GVN_FIXTURES) / L"mask-reference/source.rgb", std::ios::binary);
    CHECK(input); auto result = mask_rgb(input);
    CHECK(input.peek() == std::char_traits<char>::eof());
    return result;
}
inline std::vector<MaskReference> mask_references() {
    const auto directory = std::filesystem::path(GVN_FIXTURES) / L"mask-reference";
    std::ifstream cases(directory / L"cases.tsv"), pixels(directory / L"expected.rgb", std::ios::binary);
    CHECK(cases && pixels);
    std::vector<MaskReference> result;
    for (std::string line; std::getline(cases, line);) {
        std::array<std::string, 11> fields; std::istringstream row(line);
        for (auto &field : fields) CHECK(std::getline(row, field, '\t'));
        CHECK(std::stoull(fields[9]) == result.size() * 29 * 19 * 3 && std::stoul(fields[10]) == 29 * 19 * 3);
        result.push_back({fields[0], fields[1], std::stoi(fields[2]), std::stoi(fields[3]),
                          std::stoi(fields[4]), std::stoi(fields[5]), std::stoi(fields[6]),
                          std::stoi(fields[7]) != 0, std::stoi(fields[8]) != 0, mask_rgb(pixels)});
    }
    CHECK(result.size() == 562 && pixels.peek() == std::char_traits<char>::eof());
    return result;
}
inline gvn::MaskOptions mask_reference_options(const MaskReference &item) {
    return {item.size, item.swap, unsigned(item.negative), unsigned(item.xor_channels),
            unsigned(item.q0), item.remainder};
}
inline gvn::Mask mask_reference_operation(const std::string &kind) {
    if (kind == "vertical") return gvn::Mask::Vertical;
    if (kind == "horizontal") return gvn::Mask::Horizontal;
    if (kind == "negative") return gvn::Mask::Negative;
    if (kind == "xor") return gvn::Mask::Xor;
    if (kind == "swap") return gvn::Mask::Swap;
    if (kind == "q0") return gvn::Mask::Q0;
    CHECK(kind == "win"); return gvn::Mask::Win;
}
inline gvn::Image apply_mask_reference(const gvn::Image &source, const MaskReference &item, gvn::Rect rect = {}) {
    return item.kind == "image_negative" ? gvn::effect(source, gvn::Effect::Negative, 1, rect)
         : gvn::mask_image(source, mask_reference_operation(item.kind), mask_reference_options(item), rect);
}
inline gvn::Image mask_reference_alpha_source() {
    auto result = mask_reference_source();
    for (size_t i = 0; i < result.frames[0].pixels.size(); ++i)
        result.frames[0].pixels[i].a = static_cast<uint8_t>(i);
    return result;
}
inline gvn::Image mask_reference_alpha_expected(const MaskReference &item,
                                               const std::vector<MaskReference> &references) {
    auto result = item.expected;
    const MaskReference *mapping = &item;
    const bool spatial = item.kind == "q0" || item.kind == "vertical" || item.kind == "horizontal" || item.kind == "win";
    if (item.kind == "q0") {
        const auto found = std::find_if(references.begin(), references.end(), [&](const auto &r) {
            return r.kind == "q0" && r.size == item.size && r.remainder == item.remainder &&
                   r.q0 == (item.q0 & ~12);
        });
        CHECK(found != references.end()); mapping = &*found;
    }
    const auto source = mask_reference_source();
    for (size_t i = 0; i < result.frames[0].pixels.size(); ++i) {
        size_t original = i;
        if (spatial) {
            const auto color = mapping->expected.frames[0].pixels[i];
            const auto &pixels = source.frames[0].pixels;
            const auto found = std::find(pixels.begin(), pixels.end(), color);
            CHECK(found != pixels.end()); original = static_cast<size_t>(found - pixels.begin());
        }
        result.frames[0].pixels[i].a = static_cast<uint8_t>(original);
    }
    return result;
}
} // namespace gvn_test
