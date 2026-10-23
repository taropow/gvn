#pragma once
#include "color_adjustment.h"
#include <windows.h>
#include <functional>
namespace gvn {
namespace color_adjust_control {
constexpr int Original=600,Adjusted=601,Hue=602,Saturation=603,Lightness=604,
    ScrollHue=605,ScrollSaturation=606,ScrollLightness=607,ResetHue=608,ResetSaturation=609,ResetLightness=610,
    Unified=611,Gray=612,Error=613,Alpha=614,LabelHue=615,LabelSaturation=616,LabelLightness=617,
    LabelOriginal=618,LabelAdjusted=619,LabelGray=620;
}
class ColorAdjustmentDialog {
    struct Impl;std::unique_ptr<Impl> impl;
  public:
    using Current=std::function<bool()>;
    ColorAdjustmentDialog(std::shared_ptr<const Image>,size_t,Rect,ColorAdjustment,Current={});
    ~ColorAdjustmentDialog();
    std::optional<ColorAdjustment> show(HWND);
    HWND create(HWND);
    HWND window() const;
    HWND control(int) const;
    ColorAdjustment options() const;
    std::optional<ColorAdjustment> result() const;
    bool ready() const;
    bool valid() const;
    const Image &original_preview() const;
    const Image &adjusted_preview() const;
    Rect preview_selection() const;
};
std::optional<ColorAdjustment> choose_color_adjustment(HWND,std::shared_ptr<const Image>,size_t,Rect,ColorAdjustment,const ColorAdjustmentDialog::Current &);
} // namespace gvn
