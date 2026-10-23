#pragma once
#include "edit.h"
#include <optional>
namespace gvn {
enum class ColorDepth { Optimal, Indexed8, Rgb555, Rgb565, Rgb24 };
struct ColorDepthChoice {
    ColorDepth depth{ColorDepth::Optimal};
    bool dither{true};
    bool operator==(const ColorDepthChoice &) const = default;
};
// Current validated representation, independent of historical source-file metadata.
unsigned color_depth_bits(const Image &);
// A null result is a lossless no-op and must not create an Undo entry.
// RGB reduction keeps every original alpha byte. When a combined RGBA palette
// needs more than256 entries, retain RGBA storage rather than discard alpha.
std::optional<Image> convert_color_depth(const Image &, ColorDepthChoice,
                                        const EditMemory &memory = {});
} // namespace gvn
