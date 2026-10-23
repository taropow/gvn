#pragma once
#include "core.h"
#include <windows.h>
#include <functional>
#include <array>
#include <optional>

namespace gvn {
enum class PrintSizing { Contain,Cover,Stretch,ImageDpi,Specified };
enum class PrintRotation { None,Left,Right };
struct PrintOptions {
    double left{10},top{10},right{10},bottom{10}; // Millimetres; clamp to printer minimum.
    double width{100},height{100};
    int position{4},unit{},density{},copies{1}; // cm/inch, density -5..5.
    PrintSizing sizing{PrintSizing::Contain};
    PrintRotation rotation{PrintRotation::None};
    bool aspect{true};
    bool operator==(const PrintOptions &) const = default;
};
struct PrintPaper {
    double width{210},height{297},left{},top{},right{},bottom{};
};
struct PrintBox {double x{},y{},width{},height{};};
struct PrintLayout {
    PrintBox area,image;
    int quarter_turns{};
};
PrintLayout print_layout(uint32_t width,uint32_t height,const ImageMetadata &,const PrintPaper &,const PrintOptions &);
Pixel print_pixel(Pixel,int density);
struct PrintColors {
    std::array<uint8_t,256> channels;
    explicit PrintColors(int density);
    Pixel operator()(Pixel) const;
};
// GDI destination coordinates correspond to physical paper coordinates, including printer offsets.
bool draw_print_page(HDC,const Image &,size_t,const PrintLayout &,double pixels_per_mm_x,double pixels_per_mm_y,double offset_x=0,double offset_y=0,int density=0);

struct PrintDevice {
    std::wstring name,paper_name;
    PrintPaper paper;
    bool ready{};
};
namespace print_control {
enum { Printer=500,Paper,Setup,Copies,Left,Top,Right,Bottom,Minimum,Position=520,
    Sizing=540,Width,Height,Aspect,Unit,Density,Rotation,Preview,Error };
}
class PrintDialog {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    using Chooser=std::function<std::optional<PrintDevice>(HWND)>;
    PrintDialog(const Image &,size_t,const ImageMetadata &,PrintOptions,PrintDevice,Chooser={});
    ~PrintDialog();
    HWND create(HWND owner);
    HWND window() const;
    HWND control(int) const;
    std::optional<PrintOptions> result() const;
    std::optional<PrintLayout> layout() const;
    PrintDevice device() const;
    std::optional<PrintOptions> run(HWND);
};
void print_image(HWND,const Image &,size_t,const ImageMetadata *metadata=nullptr);
struct PrintJobTrace {
    int job_id{}, pages_started{}, pages_ended{}, abort_result{};
    bool completed{}, cancelled{}, aborted{};
};
// Shared by the viewer and the owned virtual-printer probe. The caller owns the HDC;
// an empty output path retains the selected printer's normal output behavior.
void spool_print_image(HWND,HDC,const Image &,size_t,const PrintLayout &,const PrintOptions &,
                       const std::filesystem::path &output={},PrintJobTrace *trace=nullptr);
}
