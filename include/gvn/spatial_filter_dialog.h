#pragma once
#include "spatial_filter.h"
#include <windows.h>
#include <functional>
namespace gvn {
namespace filter_control {
constexpr int Preview=700,Radius=701,Amount=702,Threshold=703,ScrollRadius=704,ScrollAmount=705,ScrollThreshold=706,Error=707,Hint=708,LabelRadius=709,LabelAmount=710,LabelThreshold=711;
}
class SpatialFilterDialog {
    struct Impl;std::unique_ptr<Impl> impl;
  public:
    using Current=std::function<bool()>;
    SpatialFilterDialog(std::shared_ptr<const Image>,size_t,Rect,SpatialFilter,Current={});
    ~SpatialFilterDialog();
    std::optional<SpatialFilter> show(HWND);
    HWND create(HWND);
    HWND window() const;HWND control(int) const;
    SpatialFilter options() const;std::optional<SpatialFilter> result() const;
    bool ready() const;bool valid() const;
    const Image &original_preview() const;const Image &adjusted_preview() const;
    Rect viewport() const;void pan(int,int);
};
std::optional<SpatialFilter> choose_spatial_filter(HWND,std::shared_ptr<const Image>,size_t,Rect,SpatialFilter,const SpatialFilterDialog::Current &);
} // namespace gvn
