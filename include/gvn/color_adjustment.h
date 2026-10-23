#pragma once
#include "edit.h"
#include <optional>
namespace gvn {
enum class GrayMethod { Middle,Average,Luma };
struct ColorAdjustment {
    int hue{},saturation{},lightness{};
    bool unified{};
    GrayMethod gray{GrayMethod::Luma};
    bool neutral() const{return !unified&&!hue&&!saturation&&!lightness;}
    bool operator==(const ColorAdjustment &) const = default;
};
bool valid_color_adjustment(const ColorAdjustment &);
// A null result means no processing is needed. Preserve alpha and untouched ROI
// pixels exactly. Whole indexed images retain their original numbers and table.
std::optional<Image> color_adjustment(const Image &,const ColorAdjustment &,Rect selection={},
                                     const EditMemory &memory={});
} // namespace gvn
