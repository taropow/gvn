#include "gvn/edit.h"
#include "gvn/resample.h"
#include "gvn/codecs.h"
#include "test.h"
#include <cmath>
using namespace gvn;
namespace {
Image solid(uint32_t w,uint32_t h,Pixel p) {
    Image image;image.width=w;image.height=h;image.frames.push_back({std::vector<Pixel>(size_t(w)*h,p),100});return image;
}
}
TEST(resize_filters_match_existing_opaque_reference_crops_for_ten_methods_down_and_up) {
    const auto directory=std::filesystem::path(GVN_FIXTURES)/L"comic";
    const auto first=load_image(directory/L"first.png"),second=load_image(directory/L"second-17.png");
    for(int method=1;method<=10;++method)for(int mode:{1,2}) {
        const auto target_height=mode==1?std::min(first.height,second.height):std::max(first.height,second.height);
        const auto width=[&](const Image &p){return uint32_t((uint64_t(p.width)*target_height+p.height/2)/p.height);};
        const auto a=resize_image(first,width(first),target_height,ResizeFilters{method,method});
        const auto b=resize_image(second,width(second),target_height,ResizeFilters{method,method});
        const auto expected=load_image(directory/(L"method-"+std::wstring(method<10?L"0":L"")+std::to_wstring(method)+L"-height-17-mode-"+std::to_wstring(mode)+L".png"));
        CHECK(expected.width==a.width+b.width&&expected.height==target_height);
        unsigned maximum{};
        for(uint32_t y=0;y<target_height;++y)for(uint32_t x=0;x<expected.width;++x) {
            const auto p=x<b.width?b.frames[0].pixels[size_t(y)*b.width+x]:a.frames[0].pixels[size_t(y)*a.width+x-b.width];
            const auto q=expected.frames[0].pixels[size_t(y)*expected.width+x];CHECK(p.a==255&&q.a==255);
            maximum=std::max({maximum,unsigned(std::abs(int(p.r)-q.r)),unsigned(std::abs(int(p.g)-q.g)),unsigned(std::abs(int(p.b)-q.b))});
        }
        std::cout<<"ORACLE resize method="<<method<<" mode="<<mode<<" max_channel_error="<<maximum<<'\n';
        CHECK(maximum<=2);
    }
}
TEST(resize_all_methods_preserve_low_alpha_color_and_exclude_hidden_rgb) {
    for(int method=0;method<=10;++method) {
        const auto faint=solid(3,2,{201,17,93,1});
        for(const auto size:{std::pair{5u,3u},std::pair{2u,1u},std::pair{5u,1u}}) {
            const auto out=resize_image(faint,size.first,size.second,ResizeFilters{method,method});
            for(const auto p:out.frames[0].pixels)CHECK(p==Pixel{201,17,93,1});
        }
        auto edge=solid(3,2,{0,220,0,128});edge.frames[0].pixels[0]={255,0,255,0};edge.frames[0].pixels[3]={255,0,255,0};
        const auto out=resize_image(edge,7,5,ResizeFilters{method,method});
        for(const auto p:out.frames[0].pixels)if(p.a)CHECK(p.r==0&&p.g==220&&p.b==0);
    }
    auto pair=solid(2,1,{});pair.frames[0].pixels={{200,20,30,1},{0,150,0,255}};
    CHECK(resize_image(pair,1,1,ResizeFilters{1,1}).frames[0].pixels[0]==Pixel{1,149,0,128});
    const auto tall=resize_image(solid(3,1400,{201,17,93,1}),2,2,ResizeFilters{2,2});
    for(const auto p:tall.frames[0].pixels)CHECK(p==Pixel{201,17,93,1});
}
TEST(resize_sharpen_changes_only_filtered_shrink_rgb_and_keeps_alpha) {
    auto source=solid(9,5,{});
    for(uint32_t y=0;y<5;++y)for(uint32_t x=0;x<9;++x)source.frames[0].pixels[y*9+x]={uint8_t(x<4?30:190),uint8_t(y*35+25),80,uint8_t(64+y*30)};
    const auto normal=resize_image(source,5,3,ResizeFilters{1,1,false}),sharp=resize_image(source,5,3,ResizeFilters{1,1,true});
    CHECK(normal.frames[0].pixels!=sharp.frames[0].pixels);
    for(size_t i=0;i<normal.frames[0].pixels.size();++i)CHECK(normal.frames[0].pixels[i].a==sharp.frames[0].pixels[i].a);
    CHECK(resize_image(source,5,3,ResizeFilters{0,0,true}).frames[0].pixels==resize_image(source,5,3,false).frames[0].pixels);
    CHECK(resize_image(source,11,7,ResizeFilters{1,1,true}).frames[0].pixels==resize_image(source,11,7,ResizeFilters{1,1,false}).frames[0].pixels);
    source.frames[0].pixels[0]={123,45,67,0};source.frames[0].pixels[1]={201,17,93,1};
    CHECK(resize_image(source,9,5,ResizeFilters{10,7,true}).frames[0].pixels==source.frames[0].pixels);
}
TEST(resize_filter_invalid_input_and_memory_fail_before_output_allocation) {
    auto source=solid(3,2,{20,40,80,128});size_t allocations{};
    EditMemory memory{24,false,[&](size_t){++allocations;}};
    THROWS(resize_image(source,6,4,ResizeFilters{10,7,true},memory));CHECK(!allocations);
    for(const auto filters:{ResizeFilters{-1,1},ResizeFilters{11,1},ResizeFilters{1,11}}) {
        THROWS(resize_image(source,3,2,filters,memory));CHECK(!allocations);
    }
    THROWS(resize_image(source,0,1,ResizeFilters{1,1},memory));CHECK(!allocations);
    THROWS(resize_image(Image{},2,2,ResizeFilters{1,1},memory));CHECK(!allocations);
    source.frames[0].pixels.pop_back();THROWS(resize_image(source,2,2,ResizeFilters{1,1},memory));CHECK(!allocations);
}
