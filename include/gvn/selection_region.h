#pragma once
#include "floating_selection.h"
#include <memory>
#include <span>

namespace gvn {
enum class SelectionCombine { Replace, Add, Subtract, Intersect };
struct SelectionLine { FloatPoint a, b; };
// Integer pixel coverage stored as scanline runs. Rectangles and their inverse
// need no pixel mask; copying a selection shares its immutable runs/contours.
class SelectionRegion : public Rect {
    struct Span { int first, last; };
    struct Shape { Rect bounds; std::vector<std::vector<Span>> rows; };
    std::shared_ptr<const Shape> shape_;
    Rect inverse_rect_{};
    bool inverse_{};
    int canvas_width_{}, canvas_height_{};
    std::shared_ptr<const std::vector<SelectionLine>> outline_;
    std::vector<Span> row(int y) const;
    void build_outline();
  public:
    SelectionRegion() = default;
    SelectionRegion(int x,int y,int w,int h) : Rect{x,y,w,h} {}
    SelectionRegion(Rect r) : Rect(r) {}
    bool rectangular() const { return !shape_ && !inverse_; }
    bool contains(int px,int py) const;
    size_t storage_bytes() const;
    const std::vector<SelectionLine> *outline() const { return outline_.get(); }
    static SelectionRegion polygon(std::span<const FloatPoint>,int width,int height);
    SelectionRegion inverted(int width,int height) const;
    SelectionRegion combined(const SelectionRegion &,SelectionCombine,int width,int height) const;
    SelectionRegion clipped(int width,int height) const;
};
Image selection_image(const Image &,const SelectionRegion &,const EditMemory & = {});
Image erase_selection(const Image &,const SelectionRegion &,Pixel fill={},const EditMemory & = {});
void restrict_to_selection(Image &edited,const Image &original,const SelectionRegion &,size_t original_frame=0);
} // namespace gvn
