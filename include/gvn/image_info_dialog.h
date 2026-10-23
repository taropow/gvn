#pragma once
#include "color_count.h"
#include <windows.h>
#include <utility>
namespace gvn {
struct ImageInfoData {
    std::shared_ptr<const Image> image;
    std::vector<std::pair<std::wstring,std::wstring>> fields;
    std::wstring text() const;
};
namespace image_info_control {constexpr int Field=300,Label=400,Colors=500,Auto=501,Error=502;}
class ImageInfoDialog {
    struct Impl;std::unique_ptr<Impl> impl;
  public:
    using Counter=std::function<std::optional<uint64_t>(std::stop_token)>;
    ImageInfoDialog(ImageInfoData,bool automatic,Counter);
    ~ImageInfoDialog();
    bool show(HWND);
    HWND create(HWND);
    HWND window() const;
    HWND control(int) const;
    bool ready() const;
    bool automatic() const;
    bool pending() const;
    std::optional<uint64_t> count() const;
    std::wstring text() const; // Actual native field contents for inspection/copy.
};
bool show_image_info(HWND,const ImageInfoData &,bool,const ImageInfoDialog::Counter &);
}
