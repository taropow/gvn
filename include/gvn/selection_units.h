#pragma once
#include "core.h"
namespace gvn {
struct SelectionUnits {
    int start_x{1},start_y{1},size_x{1},size_y{1};
    bool operator==(const SelectionUnits &) const = default;
};
bool valid_selection_units(const SelectionUnits &);
// Mouse endpoints are inclusive pixels; choose the same rectangle in either drag direction.
Rect selection_rectangle(int x1,int y1,int x2,int y2,int width,int height,const SelectionUnits &);
}
