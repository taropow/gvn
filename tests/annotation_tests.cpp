#include "gvn/annotation.h"
#include "test.h"
#include "test_desktop.h"
#include <cmath>
using namespace gvn;
TEST(pen_round_stroke_has_coverage_and_never_changes_source){
    auto image=new_image(40,30);const auto before=image.frames[0].pixels;const FloatPoint p[]{{5,6},{30,22}};
    const auto out=draw_pen(image,p,{5,{250,20,80,255}});size_t opaque{},partial{};
    for(auto c:out.frames[0].pixels){opaque+=c.a==255;partial+=c.a>0&&c.a<255;if(c.a)CHECK(c.r==250&&c.g==20&&c.b==80);}
    CHECK(opaque>50&&partial>10&&out.frames[0].pixels[0]==Pixel{}&&image.frames[0].pixels==before);
}
TEST(pen_self_overlap_keeps_opacity_and_respects_selection){
    auto image=new_image(30,30);const FloatPoint p[]{{0,15},{29,15},{0,15},{15,0},{15,29}};
    const SelectionRegion region{5,5,20,20};const auto out=draw_pen(image,p,{6,{120,240,30,128}},region);
    size_t count{};for(int y=0;y<30;++y)for(int x=0;x<30;++x){const auto c=out.frames[0].pixels[size_t(y)*30+x];CHECK(c.a<=128);if(!region.contains(x,y))CHECK(c==Pixel{});count+=c.a==128;}
    CHECK(count>40);
}
TEST(pen_single_dot_and_allocation_failure_are_atomic){
    const auto image=new_image(20,20);const FloatPoint dot[]{{10,10}};const auto out=draw_pen(image,dot,{4,{255,0,0,255}});
    CHECK(out.frames[0].pixels[10*20+10].a==255&&out.frames[0].pixels[0].a==0);
    EditMemory fail;fail.before_allocate=[](size_t){throw std::bad_alloc();};THROWS(draw_pen(image,dot,{}, {},fail));THROWS(crop_oriented(image,{10,10,10,10,0},1,fail));
    CHECK(image.frames[0].pixels[210]==Pixel{});
}
TEST(text_stamp_supports_japanese_multiline_and_transparent_edges){
    const auto stamp=make_text_stamp(L"日本語 ABC\n文字入れ",L"Yu Gothic UI",24,{240,30,80,200});
    const auto single=make_text_stamp(L"日本語 ABC",L"Yu Gothic UI",24,{240,30,80,200});CHECK(stamp.height>single.height&&stamp.width>30);
    size_t ink{},partial{},clear{};for(auto p:stamp.frames[0].pixels){CHECK(p.a<=200);if(p.a){++ink;CHECK(p.r==240&&p.g==30&&p.b==80);partial+=p.a<200;}else ++clear;}
    CHECK(ink>50&&partial>10&&clear>50);
}
TEST(crop_oriented_identity_subrect_and_right_angle){
    auto image=new_image(6,4);for(size_t n=0;n<24;++n)image.frames[0].pixels[n]={uint8_t(n+1),22,33,uint8_t(10+n*10)};
    CHECK(crop_oriented(image,{3,2,6,4,0}).frames[0].pixels==image.frames[0].pixels);
    const auto out=crop_oriented(image,{3,2,2,2,0});CHECK(out.width==2&&out.height==2);
    CHECK(out.frames[0].pixels[0]==image.frames[0].pixels[1*6+2]);
    const auto rotated=crop_oriented(image,{3,2,2,2,90});CHECK(rotated.frames[0].pixels[0]==image.frames[0].pixels[1*6+3]);
    const auto outside=crop_oriented(image,{-8,-8,2,2,30});for(auto p:outside.frames[0].pixels)CHECK(p.a==0);
}
TEST(crop_scale_uses_alpha_correct_interpolation){
    auto image=new_image(2,1);image.frames[0].pixels={{255,0,0,255},{0,255,0,0}};
    const auto out=crop_oriented(image,{1,.5,2,1,0},2);CHECK(out.width==4&&out.height==2);
    for(auto p:out.frames[0].pixels)if(p.a)CHECK(p.r==255&&p.g==0&&p.b==0);
}
TEST(all_nine_anchors_stay_fixed_during_numeric_rotation_and_resize){
    const FloatingBox initial{50,40,30,20,17};
    for(int anchor=0;anchor<9;++anchor){const double x=(anchor%3-1)/2.,y=(anchor/3-1)/2.;const auto a=initial.point(x*30,y*20);
        const auto next=resize_box_at_anchor(initial,60,70,90,anchor);const auto b=next.point(x*60,y*70);CHECK(std::hypot(a.x-b.x,a.y-b.y)<1e-8);}
}
TEST(invalid_annotation_input_is_rejected){
    const auto image=new_image(10,10);const FloatPoint p[]{{2,2}};THROWS(draw_pen(image,{},{}));THROWS(draw_pen(image,p,{0,{}}));
    THROWS(make_text_stamp(L"",L"Arial",20,{}));THROWS(make_text_stamp(L"   \n",L"Arial",20,{}));THROWS(crop_oriented(image,{5,5,0,5,0}));THROWS(crop_oriented(image,{5,5,5,5,0},0));
}
TEST(annotation_outline_shadow_layers_keep_alpha_edges_and_source){
    auto source=new_image(1,1);source.frames[0].pixels[0]={240,20,80,255};const auto original=source.frames[0].pixels;
    AnnotationEffects e;e.outline=true;e.shadow=true;e.shadow_blur=0;e.shadow_offset=5;e.outline_width=2;
    const auto out=decorate_annotation(source,e);const int pad=annotation_padding(e);const auto at=[&](int x,int y){return out.frames[0].pixels[size_t(y+pad)*out.width+x+pad];};
    CHECK(at(0,0)==original[0]);CHECK(at(2,0)==e.outline_color);CHECK(at(5,5)==e.shadow_color);CHECK(out.frames[0].pixels[0].a==0);
    CHECK(source.frames[0].pixels==original);e.shadow_offset=-5;e.shadow_blur=3;
    const auto blurred=decorate_annotation(source,e);size_t shadow{},partial{};for(auto c:blurred.frames[0].pixels){shadow+=c.a&&c.r==0;partial+=c.a>0&&c.a<128;}CHECK(shadow>20&&partial>20);
    EditMemory fail;fail.before_allocate=[](size_t){throw std::bad_alloc();};THROWS(decorate_annotation(source,e,fail));CHECK(source.frames[0].pixels==original);
    e.outline_width=17;THROWS(decorate_annotation(source,e));e.outline_width=2;e.shadow_blur=33;THROWS(decorate_annotation(source,e));
}
TEST(pen_and_text_effects_clip_selection_without_flattening_alpha){
    auto source=new_image(80,60);source.frames[0].pixels[7*80+20]={11,22,33,0};AnnotationEffects e;e.outline=true;e.shadow=true;
    const FloatPoint line[]{{10,25},{60,25}};const SelectionRegion selection{5,5,55,45};
    const auto out=draw_pen(source,line,{4,{240,20,80,255},e},selection);size_t white{},shadow{};
    for(int y=0;y<60;++y)for(int x=0;x<80;++x){auto c=out.frames[0].pixels[size_t(y)*80+x];if(!selection.contains(x,y))CHECK(c.a==0);
        white+=c==e.outline_color;shadow+=c.r==0&&c.a>0&&c.a<255;}
    CHECK(white>20&&shadow>20&&out.frames[0].pixels[25*80+20]==Pixel({240,20,80,255}));
    CHECK(out.frames[0].pixels[7*80+20]==source.frames[0].pixels[7*80+20]);
    const auto plain=make_text_stamp(L"日本語",L"Yu Gothic UI",24,{240,20,80,255});
    const auto decorated=make_text_stamp(L"日本語",L"Yu Gothic UI",24,{240,20,80,255},{},e);
    CHECK(decorated.width>plain.width&&decorated.height>plain.height);white=shadow=0;
    for(auto c:decorated.frames[0].pixels){white+=c==e.outline_color;shadow+=c.r==0&&c.a>0&&c.a<255;}CHECK(white>20&&shadow>20);
}
int main(){
    auto station=CreateWindowStationW(nullptr,0,WINSTA_ALL_ACCESS,nullptr);
    if(!station||!gvn_test::noninteractive(station)||!SetProcessWindowStation(station))return 2;
    auto desktop=CreateDesktopW(L"GVN.AnnotationTests",nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop||!SetThreadDesktop(desktop))return 3;
    return run_tests();
}
