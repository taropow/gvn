#pragma once
#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
namespace gvn {
struct CanvasChoice {
    uint32_t width{},height{};
    bool keep_aspect{};
    int anchor{4};
    bool transparent{true};
    uint32_t color{0xffffff};
    int alpha{255};
    double dpi_x{72},dpi_y{72};
    int unit{}; // px, percent, inch, cm, mm.
    bool operator==(const CanvasChoice &) const = default;
};
struct ResizeChoice {
    uint32_t width{}, height{};
    bool keep_aspect{true}, smooth{true};
    int down{1}, up{1};
    bool sharpen{};
    double dpi_x{72},dpi_y{72};
    bool dpi_defined{},resample{true};
    int print_unit{3},density_unit{}; // inch2/cm3/mm4;dpi0/dpcm1.
    bool operator==(const ResizeChoice &) const = default;
};
namespace resize_control {
constexpr int Original=210, Width=211, Height=212, WidthPercent=213, HeightPercent=214,
              KeepAspect=215, Smooth=216, Half=217, Double=218, Bytes=219, Error=220,
              Down=221, Up=222, Sharpen=223;
constexpr int Anchor=250,Transparent=259,Red=260,Green=261,Blue=262,Alpha=263,Color=264;
constexpr int DpiX=270,DpiY=271,PrintWidth=272,PrintHeight=273,PrintUnit=274,DpiUnit=275,Resample=276,CanvasUnit=277;
}
class ResizeDialog {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    using Validator=std::function<void(uint32_t,uint32_t)>;
    static std::unique_ptr<ResizeDialog> for_canvas(uint32_t,uint32_t,CanvasChoice,Validator={});
    ResizeDialog(uint32_t original_width,uint32_t original_height,ResizeChoice,Validator={});
    ~ResizeDialog();
    std::optional<ResizeChoice> show(HWND);
    HWND create(HWND);
    HWND window() const;
    HWND control(int) const;
    bool ready() const;
    bool valid() const;
    ResizeChoice options() const;
    std::optional<ResizeChoice> result() const;
    CanvasChoice canvas_options() const;
    std::optional<CanvasChoice> canvas_result() const;
};
std::optional<ResizeChoice> choose_resize(HWND,uint32_t,uint32_t,ResizeChoice,const ResizeDialog::Validator &);
std::optional<CanvasChoice> choose_canvas(HWND,uint32_t,uint32_t,CanvasChoice,const ResizeDialog::Validator &);
} // namespace gvn
