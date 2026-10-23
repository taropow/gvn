#pragma once
#include "core.h"
#include <windows.h>
#include <optional>
namespace gvn {
namespace selection_rect_control {enum {Left=700,Top,Right,Bottom,Width,Height,ImageSize=720,Hint,Error,Spin=730};}
class SelectionRectDialog {
    struct Impl;std::unique_ptr<Impl> impl;
  public:
    SelectionRectDialog(uint32_t width,uint32_t height,Rect current);
    ~SelectionRectDialog();
    HWND create(HWND);
    HWND window() const;
    HWND control(int) const;
    bool ready() const;
    std::optional<Rect> bounds() const;
    std::optional<Rect> result() const;
    std::optional<Rect> run(HWND);
};
std::optional<Rect> choose_selection_rect(HWND,uint32_t width,uint32_t height,Rect current);
}
