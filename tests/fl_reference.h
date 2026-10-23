#pragma once
#include "mask_reference.h"

namespace gvn_test {
struct FlReference {
    std::string label, kind;
    bool selected{}, negative{}, alpha{};
    gvn::Rect rect;
    gvn::MaskOptions options;
    gvn::Image source, expected;
    std::vector<uint8_t> expected_alpha;
};
inline std::vector<FlReference> fl_references(const wchar_t *name = L"fl-reference", size_t count = 106) {
    const auto folder = std::filesystem::path(GVN_FIXTURES) / name;
    std::ifstream rows(folder / L"cases.tsv"), data(folder / L"pixels.bin", std::ios::binary);
    CHECK(rows && data);
    const auto rgb = [&](uint32_t width, uint32_t height) {
        gvn::Image image; image.width = width; image.height = height;
        image.frames.push_back({std::vector<gvn::Pixel>(static_cast<size_t>(width) * height), 100});
        for (auto &pixel : image.frames[0].pixels) {
            std::array<uint8_t,3> color{}; CHECK(data.read(reinterpret_cast<char *>(color.data()),3));
            pixel = {color[0],color[1],color[2],255};
        }
        return image;
    };
    const auto indexed = [&](gvn::Image &image, unsigned bits, unsigned colors) {
        if (!bits) { CHECK(colors == 0); return; }
        auto indices = std::make_shared<gvn::IndexedPixels>();
        indices->width = image.width; indices->height = image.height; indices->bits = bits;
        for (unsigned i = 0; i < colors; ++i) {
            std::array<uint8_t,3> color{}; CHECK(data.read(reinterpret_cast<char *>(color.data()),3));
            indices->palette.push_back({color[0],color[1],color[2],255});
        }
        indices->packed.resize(indices->stride() * image.height);
        for (uint32_t y = 0; y < image.height; ++y) for (uint32_t x = 0; x < image.width; ++x) {
            char value{}; CHECK(data.get(value)); indices->set_index(x,y,static_cast<uint8_t>(value));
        }
        CHECK(indices->matches(image.frames[0].pixels)); image.frames[0].indexed = std::move(indices);
    };
    std::vector<FlReference> result;
    for (std::string line; std::getline(rows,line);) {
        std::array<std::string,22> f; std::istringstream row(line);
        for (auto &field : f) CHECK(std::getline(row,field,'\t'));
        CHECK(static_cast<uint64_t>(data.tellg()) == std::stoull(f[16]));
        FlReference item; item.label=f[0]; item.kind=f[1]; item.selected=std::stoi(f[2])!=0;
        const auto width=std::stoul(f[3]), height=std::stoul(f[4]);
        item.rect={std::stoi(f[5]),std::stoi(f[6]),std::stoi(f[7]),std::stoi(f[8])};
        item.options={std::stoi(f[9]),std::stoi(f[10]),std::stoul(f[11]),std::stoul(f[12]),std::stoul(f[13]),std::stoi(f[14])!=0};
        item.negative=std::stoi(f[15])!=0; item.alpha=std::stoi(f[21])!=0;
        item.options.fl_negative=item.negative;
        item.source=rgb(width,height); item.expected=rgb(width,height);
        indexed(item.source,std::stoul(f[17]),std::stoul(f[19]));
        indexed(item.expected,std::stoul(f[18]),std::stoul(f[20]));
        if (item.alpha) {
            item.expected_alpha.resize(static_cast<size_t>(width)*height);
            CHECK(data.read(reinterpret_cast<char *>(item.expected_alpha.data()),item.expected_alpha.size()));
        }
        result.push_back(std::move(item));
    }
    CHECK(result.size()==count && data.peek()==std::char_traits<char>::eof()); return result;
}
inline gvn::Mask fl_operation(const FlReference &item) {
    return item.kind=="fl" ? gvn::Mask::FL
                           : mask_reference_operation(item.kind);
}
inline void fl_check_indexed(const gvn::Image &actual, const gvn::Image &expected) {
    const auto a=actual.frames[0].indexed, e=expected.frames[0].indexed;
    CHECK(bool(a)==bool(e));
    if (a) {
        CHECK(a->bits==e->bits && a->palette==e->palette && a->matches(actual.frames[0].pixels));
        for (uint32_t y=0;y<actual.height;++y) for (uint32_t x=0;x<actual.width;++x) CHECK(a->index(x,y)==e->index(x,y));
    }
}
} // namespace gvn_test
