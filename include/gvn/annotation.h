#pragma once
#include "floating_selection.h"
#include "selection_region.h"
#include <span>
namespace gvn {
struct PenStyle {
    double width{6};
    Pixel color{245,30,84,255};
};
// One gesture is one edit. Coverage is combined before compositing, so translucent
// strokes do not accumulate alpha at joins or when the pointer crosses itself.
Image draw_pen(const Image &,std::span<const FloatPoint>,PenStyle,const SelectionRegion & = {},const EditMemory & = {});
Image make_text_stamp(const std::wstring &,const std::wstring &family,double pixels,Pixel,const EditMemory & = {});
Image crop_oriented(const Image &,const FloatingBox &,double output_scale=1,const EditMemory & = {});
FloatingBox resize_box_at_anchor(FloatingBox,double width,double height,double angle,int anchor);
} // namespace gvn
