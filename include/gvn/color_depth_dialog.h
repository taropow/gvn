#pragma once
#include "color_depth.h"
#include <windows.h>
#include <functional>
namespace gvn {
namespace color_depth_control {constexpr int ChoiceBase=560,Dither=565,Current=566,Alpha=567,Error=568,Group=569;}
class ColorDepthDialog {
    struct Impl;std::unique_ptr<Impl> impl;
  public:
    using Current=std::function<bool()>;
    ColorDepthDialog(unsigned,std::optional<Rgb16Layout>,bool,Current={});
    ~ColorDepthDialog();
    std::optional<ColorDepthChoice> show(HWND);
    HWND create(HWND);
    HWND window() const;
    HWND control(int) const;
    ColorDepthChoice options() const;
    std::optional<ColorDepthChoice> result() const;
    bool ready() const;
    bool valid() const;
};
std::optional<ColorDepthChoice> choose_color_depth(HWND,unsigned,std::optional<Rgb16Layout>,bool,const ColorDepthDialog::Current &);
} // namespace gvn
