#pragma once
#include "edit.h"
#include <array>
namespace gvn {
struct FloatPoint { double x{}, y{}; };
struct FloatingBox {
    double cx{}, cy{}, width{1}, height{1}, angle{};
    bool valid() const;
    FloatPoint point(double x, double y) const;
    FloatPoint local(FloatPoint) const;
    std::array<FloatPoint,8> handles() const;
};
enum FloatingHit { FloatNone=-1, FloatMove=8, FloatRotate=9 };
int floating_hit(const FloatingBox &, FloatPoint, double tolerance);
struct FloatingDrag {
    FloatingBox initial;
    FloatPoint start;
    int hit{FloatNone};
    FloatingBox moved(FloatPoint, bool constrain=false) const;
};
Image paste_floating(const Image &base,const Image &source,const FloatingBox &,const EditMemory &memory={});
} // namespace gvn
