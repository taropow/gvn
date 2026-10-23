#pragma once
#include "edit.h"
#include <array>
#include <optional>
namespace gvn {
struct LevelCurve {
    int black{},gamma{1000},white{255},output_black{},output_white{255};
    bool neutral() const{return black==0&&gamma==1000&&white==255&&output_black==0&&output_white==255;}
    bool operator==(const LevelCurve &) const = default;
};
struct LevelsAdjustment {
    // Slot0 is the combined mode;1..3 are independent red/green/blue.
    std::array<LevelCurve,4> curves;
    bool individual{};
    bool neutral() const;
    bool operator==(const LevelsAdjustment &) const = default;
};
using LevelsHistogram=std::array<std::array<uint64_t,256>,4>;
bool valid_level_curve(const LevelCurve &);
bool valid_levels(const LevelsAdjustment &);
std::array<uint8_t,256> level_table(const LevelCurve &);
LevelsHistogram levels_histogram(const Image &,Rect selection={});
LevelsAdjustment automatic_levels(LevelsAdjustment,const LevelsHistogram &);
std::optional<Image> adjust_levels(const Image &,const LevelsAdjustment &,Rect selection={},const EditMemory &memory={});
} // namespace gvn
