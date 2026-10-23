#pragma once
#include <windows.h>
#include <cstdint>
#include <memory>
#include <optional>

namespace gvn {
struct HsvColor {double hue{},saturation{},value{};}; // degrees, 0..1, 0..1
HsvColor rgb_to_hsv(uint32_t rgb,double achromatic_hue=0);
uint32_t hsv_to_rgb(HsvColor);
struct ColorWheelGeometry {
    double x{},y{},inner{},outer{};
    RECT square{};
};
ColorWheelGeometry color_wheel_geometry(int width,int height);
namespace color_control {
constexpr int Wheel=700,Hue=703,Saturation=704,Value=705,Red=706,Green=707,Blue=708,
              Hex=710,Swatch=711,Error=712;
}
class ColorDialog {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    explicit ColorDialog(uint32_t rgb);
    ~ColorDialog();
    std::optional<uint32_t> show(HWND owner);
    HWND create(HWND owner);
    HWND window() const;
    HWND control(int) const;
    uint32_t color() const;
    HsvColor hsv() const;
    bool valid() const;
    std::optional<uint32_t> result() const;
};
std::optional<uint32_t> choose_color(HWND owner,uint32_t rgb);
} // namespace gvn
