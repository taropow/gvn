#pragma once
#include "core.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
namespace gvn {
struct RainbowChoice {
    int size{8};
    bool preview{true};
    bool operator==(const RainbowChoice &) const = default;
};
struct RainbowResult {
    RainbowChoice choice;
    std::shared_ptr<Image> image;
};
namespace rainbow_control { constexpr int SizeBase=210, Preview=220, Error=221; }
class RainbowDialog {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    using Process=std::function<std::shared_ptr<Image>(int)>;
    using Preview=std::function<void(std::shared_ptr<const Image>)>;
    using Current=std::function<bool()>;
    RainbowDialog(RainbowChoice, Process, Preview, Current = {});
    ~RainbowDialog();
    std::optional<RainbowResult> show(HWND);
    HWND create(HWND);
    HWND window() const;
    HWND control(int) const;
    RainbowChoice options() const;
    bool ready() const;
    bool valid() const;
    std::shared_ptr<const Image> preview_image() const;
    std::optional<RainbowResult> result() const;
};
std::optional<RainbowResult> choose_rainbow(HWND, RainbowChoice, const RainbowDialog::Process &,
                                         const RainbowDialog::Preview &, const RainbowDialog::Current &);
} // namespace gvn
