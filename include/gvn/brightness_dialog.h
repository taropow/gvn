#pragma once
#include "brightness.h"
#include <windows.h>
#include <functional>
namespace gvn {
struct BrightnessChoice {
    BrightnessAdjustment adjustment;
    bool preview{true};
    bool operator==(const BrightnessChoice &) const = default;
};
struct BrightnessResult {BrightnessChoice choice;std::shared_ptr<Image> image;};
namespace brightness_control {
constexpr int Brightness=800,Contrast=801,Gamma=802,ScrollBrightness=803,ScrollContrast=804,ScrollGamma=805,ResetBrightness=806,ResetContrast=807,ResetGamma=808,Preview=809,Error=810,LabelBrightness=811,LabelContrast=812,LabelGamma=813;
}
class BrightnessDialog {
    struct Impl;std::unique_ptr<Impl> impl;
  public:
    using Process=std::function<std::shared_ptr<Image>(const BrightnessAdjustment &)>;
    using Preview=std::function<void(std::shared_ptr<const Image>)>;
    using Current=std::function<bool()>;
    BrightnessDialog(BrightnessChoice,Process,Preview,Current={});~BrightnessDialog();
    std::optional<BrightnessResult> show(HWND);HWND create(HWND);HWND window() const;HWND control(int) const;
    BrightnessChoice options() const;bool ready() const;bool valid() const;std::shared_ptr<const Image> preview_image() const;std::optional<BrightnessResult> result() const;
};
std::optional<BrightnessResult> choose_brightness(HWND,BrightnessChoice,const BrightnessDialog::Process &,const BrightnessDialog::Preview &,const BrightnessDialog::Current &);
} // namespace gvn
