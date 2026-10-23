#pragma once
#include "edit.h"
#include <array>
#include <optional>
namespace gvn {
struct BrightnessAdjustment {
    int brightness{},contrast{},gamma{1000}; // Gamma in thousandths.
    bool neutral() const{return !brightness&&!contrast&&gamma==1000;}
    bool operator==(const BrightnessAdjustment &) const = default;
};
bool valid_brightness(const BrightnessAdjustment &);
std::array<uint8_t,256> brightness_table(const BrightnessAdjustment &);
std::optional<Image> adjust_brightness(const Image &,const BrightnessAdjustment &,Rect selection={},const EditMemory &memory={});
} // namespace gvn
