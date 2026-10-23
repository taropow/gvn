#pragma once
#include "levels.h"
#include <windows.h>
#include <functional>
namespace gvn {
struct LevelsChoice {LevelsAdjustment adjustment;bool preview{true};bool operator==(const LevelsChoice &) const = default;};
struct LevelsResult {LevelsChoice choice;std::shared_ptr<Image> image;};
namespace levels_control {
constexpr int Black=840,Gamma=841,White=842,OutputBlack=843,OutputWhite=844;
constexpr int SpinBlack=845,SpinGamma=846,SpinWhite=847,SpinOutputBlack=848,SpinOutputWhite=849;
constexpr int Individual=850,Red=851,Green=852,Blue=853,Preview=854,Automatic=855,Histogram=856,Gradient=857,Error=858;
constexpr int InputGroup=859,OutputGroup=860,ColorGroup=861,LabelBlack=862,LabelGamma=863,LabelWhite=864,LabelOutputBlack=865,LabelOutputWhite=866;
}
class LevelsDialog {
    struct Impl;std::unique_ptr<Impl> impl;
  public:
    using Process=std::function<std::shared_ptr<Image>(const LevelsAdjustment &)>;
    using Preview=std::function<void(std::shared_ptr<const Image>)>;
    using Current=std::function<bool()>;
    LevelsDialog(LevelsChoice,LevelsHistogram,Process,Preview,Current={});~LevelsDialog();
    std::optional<LevelsResult> show(HWND);HWND create(HWND);HWND window() const;HWND control(int) const;
    LevelsChoice options() const;bool ready() const;bool valid() const;std::shared_ptr<const Image> preview_image() const;std::optional<LevelsResult> result() const;
};
std::optional<LevelsResult> choose_levels(HWND,LevelsChoice,const LevelsHistogram &,const LevelsDialog::Process &,const LevelsDialog::Preview &,const LevelsDialog::Current &);
} // namespace gvn
