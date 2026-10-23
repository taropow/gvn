#pragma once
#include "gvn/resample.h"
#include "pixel_conversion.h"
#include <array>
#include <cstring>

namespace gvn::detail {
// A synchronous borrowed view; no image-sized conversion buffer is owned here.
struct DisplaySource {
    std::span<const uint8_t> bytes;
    uint32_t width{}, height{};
    bool straight_rgba{};
    void copy_row(uint32_t y, uint32_t x, uint32_t count, uint8_t *out) const {
        const auto input = bytes.data() + (static_cast<size_t>(y) * width + x) * 4;
        if (!straight_rgba) {
            std::memcpy(out, input, static_cast<size_t>(count) * 4);
            return;
        }
        premultiply_rgba(input, out, count);
    }
};
class DisplayRows {
    struct Row {
        uint32_t index{UINT32_MAX};
        uint64_t used{};
        std::vector<uint8_t> pixels;
    };
    DisplaySource source;
    std::array<Row, 16> rows;
    size_t capacity;
    uint32_t first, count;
    uint64_t clock{};

  public:
    // Returned rows start at first, not at the left edge of the full image.
    DisplayRows(DisplaySource input, uint32_t left, uint32_t width)
        : source(input), capacity(std::clamp<size_t>(1024 * 1024 / (width * 4ull), 1, rows.size())),
          first(left), count(width) {}
    const uint8_t *get(uint32_t y) {
        if (!source.straight_rgba)
            return source.bytes.data() + (static_cast<size_t>(y) * source.width + first) * 4;
        auto oldest = &rows[0];
        for (size_t i = 0; i < capacity; ++i) {
            auto &row = rows[i];
            if (row.index == y) {
                row.used = ++clock;
                return row.pixels.data();
            }
            if (row.used < oldest->used)
                oldest = &row;
        }
        oldest->pixels.resize(static_cast<size_t>(count) * 4);
        source.copy_row(y, first, count, oldest->pixels.data());
        oldest->index = y;
        oldest->used = ++clock;
        return oldest->pixels.data();
    }
    size_t bytes() const {
        size_t result{};
        for (const auto &row : rows)
            result += row.pixels.capacity();
        return result;
    }
};
void resample_source_tile(DisplaySource source, uint32_t scaled_width, uint32_t scaled_height, uint32_t x,
                          uint32_t y, int method, DisplayTile &tile);
} // namespace gvn::detail
