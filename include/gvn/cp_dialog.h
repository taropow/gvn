#pragma once
#include "core.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
namespace gvn {
struct CpChoice {std::wstring code;bool preview{true};bool operator==(const CpChoice &) const=default;};
struct CpResult {CpChoice choice;std::shared_ptr<Image> image;}; // Empty code is a no-op: image is null.
namespace cp_control {constexpr int Code=240,Preview=241,Error=242;}
class CpDialog {
    struct Impl;std::unique_ptr<Impl> impl;
  public:
    using Process=std::function<std::shared_ptr<Image>(std::wstring_view)>;
    using Preview=std::function<void(std::shared_ptr<const Image>)>;
    using Current=std::function<bool()>;
    CpDialog(CpChoice,Process,Preview,Current={});
    ~CpDialog();
    std::optional<CpResult> show(HWND);
    HWND create(HWND);
    HWND window() const;
    HWND control(int) const;
    CpChoice options() const;
    bool ready() const;
    bool valid() const;
    std::shared_ptr<const Image> preview_image() const;
    std::optional<CpResult> result() const;
};
std::optional<CpResult> choose_cp(HWND,CpChoice,const CpDialog::Process &,const CpDialog::Preview &,const CpDialog::Current &);
}
