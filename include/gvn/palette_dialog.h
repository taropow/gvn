#pragma once
#include "core.h"
#include <windows.h>
#include <functional>
#include <optional>
namespace gvn {
namespace palette_control {
constexpr int Index=510, Red=511, Green=512, Blue=513, SlideRed=514, SlideGreen=515,
    SlideBlue=516, Pick=517, Grid=518, Swatch=519, Hover=520, Preview=521, Error=522,
    Apply=523, Spin=524;
}
class PaletteDialog {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    using Process=std::function<std::shared_ptr<Image>(const std::vector<Pixel> &)>;
    using Preview=std::function<void(std::shared_ptr<const Image>)>;
    // A null image commits only the preview preference, without changing Undo.
    using Commit=std::function<void(std::shared_ptr<Image>,bool)>;
    using Current=std::function<bool()>;
    using Picker=std::function<std::optional<uint32_t>(HWND,uint32_t)>;
    PaletteDialog(std::vector<Pixel>,bool,Process,Preview,Commit,Current={},Picker={});
    ~PaletteDialog();
    void show(HWND);
    HWND create(HWND);
    HWND window() const;
    HWND control(int) const;
    bool ready() const;
    bool valid() const;
    bool accepted() const;
    bool preview_enabled() const;
    unsigned selected_index() const;
    const std::vector<Pixel> &palette() const;
    std::shared_ptr<const Image> preview_image() const;
};
void choose_palette(HWND,const std::vector<Pixel> &,bool,const PaletteDialog::Process &,
    const PaletteDialog::Preview &,const PaletteDialog::Commit &,const PaletteDialog::Current &);
} // namespace gvn
