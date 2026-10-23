#include "gvn/selection_region.h"
#include "gvn/selection_clipboard.h"
#include "test.h"
#include <windows.h>
#include <limits>
using namespace gvn;
TEST(polygon_runs_boolean_and_inversion_match_pixel_truth) {
    const FloatPoint points[]{{2,1},{8,1},{2,7}};
    const auto region=SelectionRegion::polygon(points,10,9);
    CHECK(region.contains(2,1)&&!region.contains(7,6));CHECK(region.outline()&&!region.outline()->empty());
    const auto inverse=region.inverted(10,9),twice=inverse.inverted(10,9);
    const SelectionRegion rect{4,0,5,5};
    for(auto mode:{SelectionCombine::Add,SelectionCombine::Subtract,SelectionCombine::Intersect}){
        const auto combined=region.combined(rect,mode,10,9);
        for(int y=0;y<9;++y)for(int x=0;x<10;++x){const bool a=region.contains(x,y),b=rect.contains(x,y);
            CHECK(inverse.contains(x,y)!=a);CHECK(twice.contains(x,y)==a);
            CHECK(combined.contains(x,y)==(mode==SelectionCombine::Add?(a||b):mode==SelectionCombine::Subtract?(a&&!b):(a&&b)));}}
    CHECK(SelectionRegion(0,0,10,9).inverted(10,9).empty());
    CHECK(SelectionRegion{}.inverted(10,9).contains(9,8));
    const auto hole=SelectionRegion(2,2,3,3).inverted(10,9);CHECK(hole.contains(0,0)&&!hole.contains(3,3));
}
TEST(selection_alpha_crop_delete_and_filter_leave_outside_untouched) {
    auto source=new_image(10,9);for(size_t n=0;n<source.frames[0].pixels.size();++n)source.frames[0].pixels[n]={200,30,90,uint8_t(n*17)};
    const FloatPoint points[]{{2,1},{8,1},{2,7}};const auto region=SelectionRegion::polygon(points,10,9);
    const auto cut=selection_image(source,region),erased=erase_selection(source,region);
    auto filtered=source;for(auto &p:filtered.frames[0].pixels)p={1,2,3,255};restrict_to_selection(filtered,source,region);
    for(int y=0;y<9;++y)for(int x=0;x<10;++x){const auto n=size_t(y)*10+x;
        CHECK(erased.frames[0].pixels[n]==(region.contains(x,y)?Pixel{}:source.frames[0].pixels[n]));
        CHECK(filtered.frames[0].pixels[n]==(region.contains(x,y)?Pixel{1,2,3,255}:source.frames[0].pixels[n]));}
    for(int y=0;y<region.h;++y)for(int x=0;x<region.w;++x)CHECK(cut.frames[0].pixels[size_t(y)*cut.width+x]==(region.contains(x+region.x,y+region.y)?source.frames[0].pixels[size_t(y+region.y)*10+x+region.x]:Pixel{}));
    EditMemory memory;memory.before_allocate=[](size_t){throw std::bad_alloc();};
    THROWS(selection_image(source,region,memory));THROWS(erase_selection(source,region,{},memory));
}
TEST(floating_geometry_handles_fixed_anchor_rotation_and_limits) {
    const FloatingBox box{30,20,20,10,30};const auto handles=box.handles();
    CHECK(floating_hit(box,handles[0],.01)==0);CHECK(floating_hit(box,{30,20},.01)==FloatMove);
    CHECK(floating_hit(box,{200,200},1)==FloatRotate);
    const auto scaled=FloatingDrag{box,handles[4],4}.moved(box.point(20,10));
    CHECK(std::hypot(scaled.handles()[0].x-handles[0].x,scaled.handles()[0].y-handles[0].y)<1e-8);
    const auto shifted=FloatingDrag{box,{30,20},FloatMove}.moved({40,25});CHECK(shifted.cx==40&&shifted.cy==25);
    const auto rotated=FloatingDrag{box,{40,20},FloatRotate}.moved({30,30});CHECK(std::abs(rotated.angle-120)<1e-9);
    CHECK(!FloatingBox{0,0,0,10,0}.valid());CHECK(!FloatingBox{0,0,10,10,std::numeric_limits<double>::infinity()}.valid());
    CHECK(FloatingDrag{box,handles[4],4}.moved({1e9,1e9},true).valid());
}
TEST(floating_compositor_identity_rotation_clipping_and_premultiplied_alpha) {
    auto base=new_image(6,5),source=new_image(2,2);
    source.frames[0].pixels={{255,0,0,128},{0,255,0,255},{0,0,255,64},{200,100,50,0}};
    const auto result=paste_floating(base,source,{2,2,2,2,0});
    CHECK(result.frames[0].pixels[1*6+1]==source.frames[0].pixels[0]);CHECK(result.frames[0].pixels[1*6+2]==source.frames[0].pixels[1]);
    const auto rotate=paste_floating(base,source,{2,2,2,2,90});CHECK(rotate.frames[0].pixels[1*6+2]==source.frames[0].pixels[0]);
    CHECK(paste_floating(base,source,{-10,-10,2,2,0}).frames[0].pixels==base.frames[0].pixels);
    auto edge=new_image(2,1);edge.frames[0].pixels={{255,0,0,255},{0,0,255,0}};
    const auto enlarged=paste_floating(base,edge,{3,2,6,1,0});
    for(auto p:enlarged.frames[0].pixels)if(p.a)CHECK(p.r==255&&p.b==0);
    EditMemory memory;memory.before_allocate=[](size_t){throw std::bad_alloc();};THROWS(paste_floating(base,source,{2,2,2,2,0},memory));
}
TEST(selection_binary_protocol_rejects_malformed_and_preserves_signed_origin_and_rgba) {
    auto image=new_image(3,2);image.frames[0].pixels={{1,2,3,255},{2,3,4,128},{5,6,7,0},{8,9,10,64},{11,12,13,127},{14,15,16,254}};
    const auto png=encode(image,0,L".png");const auto bytes=encode_selection_clipboard(png,3,2,{-12,34,1920,1080});
    const auto decoded=decode_selection_clipboard(bytes);CHECK(decoded.origin&&decoded.origin->x==-12&&decoded.origin->y==34&&decoded.origin->canvas_width==1920);CHECK(decoded.image.frames[0].pixels==image.frames[0].pixels);
    auto padded=bytes;padded.resize(bytes.size()+16);CHECK(decode_selection_clipboard(padded).origin->x==-12);
    CHECK(!decode_selection_clipboard(encode_selection_clipboard(png,3,2,{0,0,3,2},false)).origin);
    for(size_t position:{size_t(0),size_t(8),size_t(12),size_t(24),size_t(32),size_t(40),size_t(44),size_t(48),size_t(64)}){
        auto bad=bytes;if(position==24)std::fill(bad.begin()+24,bad.begin()+28,0);else bad[position]^=0x80;THROWS(decode_selection_clipboard(bad));}
    THROWS(decode_selection_clipboard(std::span(bytes).first(63)));THROWS(decode_selection_clipboard(std::span(bytes).first(bytes.size()-1)));
}
int main(){CoInitializeEx(nullptr,COINIT_MULTITHREADED);const int result=run_tests();CoUninitialize();return result;}
