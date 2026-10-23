#pragma once
#include "edit.h"
namespace gvn {
enum class FilterPreset { Sharpen,SharpenStrong,SharpenEdges,Blur,BlurStrong,BlurSoft };
struct SpatialFilter {
    bool sharpen{};
    int radius{6}; // Tenths of a pixel, matching the native dialog.
    int strength{100},threshold{},transparency{};
    bool operator==(const SpatialFilter &) const = default;
};
bool valid_spatial_filter(const SpatialFilter &);
SpatialFilter filter_preset(FilterPreset);
Image spatial_filter(const Image &,const SpatialFilter &,Rect selection={},size_t frame=0,const EditMemory &memory={});
} // namespace gvn
