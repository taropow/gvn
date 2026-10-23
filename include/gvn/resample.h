#pragma once
#include "core.h"
#include <span>
namespace gvn {
enum DisplayFilter {
    DisplaySimple,
    DisplayLinear,
    DisplayIntegra,
    DisplayHermite,
    DisplayBell,
    DisplayBSpline,
    DisplayMitchell,
    DisplayCubic,
    DisplayLan225,
    DisplayLan250,
    DisplayLanczos3,
    DisplayWicCubic,
    DisplayWicFant,
    DisplayWicHighCubic
};
constexpr int kLastDisplayFilter = DisplayWicHighCubic;
// Image editing keeps straight RGBA precision until the final output conversion.
// The caller owns the validated destination; scanline workspace is bounded.
void resample_edit_rgba(std::span<const Pixel> source, uint32_t source_width, uint32_t source_height,
                        std::span<Pixel> destination, uint32_t width, uint32_t height,
                        int horizontal_method, int vertical_method);
struct DisplayTile {
    uint32_t width{}, height{};
    double left{}, top{}, right{}, bottom{};
    std::vector<uint8_t> bgra;
    size_t workspace_bytes{};
    size_t conversion_bytes{};
};
// Native filters use bounded separable scanline buffers; input/output remain premultiplied BGRA.
void resample_native_tile(std::span<const uint8_t> premultiplied_bgra, uint32_t width, uint32_t height,
                          uint32_t scaled_width, uint32_t scaled_height, uint32_t x, uint32_t y, int method,
                          DisplayTile &tile);
DisplayTile resample_display(std::span<const uint8_t> premultiplied_bgra, uint32_t width, uint32_t height,
                             const View &view, int method);
// Borrows immutable straight RGBA and converts only requested rows; output remains premultiplied BGRA.
DisplayTile resample_rgba_display(std::span<const Pixel> rgba, uint32_t width, uint32_t height,
                                  const View &view, int method);
} // namespace gvn
