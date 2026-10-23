#include "gvn/spatial_filter.h"
#include "gvn/codecs.h"
#include "test.h"
#include <windows.h>
using namespace gvn;
namespace {
const auto fixtures=std::filesystem::path(GVN_SOURCE_ROOT)/L"tests/fixtures/spatial-filter";
void matches(const Image &actual,const Image &reference,int tolerance=2) {
    CHECK(actual.width==reference.width&&actual.height==reference.height);for(size_t n=0;n<actual.frames[0].pixels.size();++n){const auto a=actual.frames[0].pixels[n],b=reference.frames[0].pixels[n];CHECK(a.a==b.a&&std::abs(int(a.r)-b.r)<=tolerance&&std::abs(int(a.g)-b.g)<=tolerance&&std::abs(int(a.b)-b.b)<=tolerance);}
}
}
TEST(six_presets_match_actual_reference_pixels_and_dialog_values) {
    const wchar_t *names[]{L"sharpen",L"sharpen-strong",L"sharpen-edges",L"blur",L"blur-strong",L"blur-soft"};
    const SpatialFilter choices[]{{true,6,100,0,0},{true,8,200,0,0},{true,6,150,15,0},{false,4,100,0,0},{false,8,100,0,0},{false,50,100,0,65}};
    for(const auto *pattern:{L"colors",L"impulse"}) {
        const auto image=load_image(fixtures/pattern/L"source.png");for(int n=0;n<6;++n){const auto choice=filter_preset(FilterPreset(n));CHECK(choice==choices[n]);const auto actual=spatial_filter(image,choice);matches(actual,load_image(fixtures/pattern/(std::wstring(names[n])+L".png")));CHECK(!actual.frames[0].indexed&&!actual.rgb16);}
    }
    const auto image=load_image(fixtures/L"calibration/source.png");const wchar_t *radii[]{L"0.6",L"1.0",L"2.0",L"5.0"};const int values[]{6,10,20,50};
    for(int n=0;n<4;++n)matches(spatial_filter(image,{false,values[n],100,0,0}),load_image(fixtures/L"calibration"/(L"gaussian-"+std::wstring(radii[n])+L".png")));
}
TEST(filters_preserve_alpha_provenance_selection_current_frame_and_hidden_rgb_independence) {
    auto source=load_image(fixtures/L"colors/source.png");source.frames.push_back(source.frames[0]);
    for(size_t n=0;n<source.frames[1].pixels.size();++n){auto &p=source.frames[1].pixels[n];p.a=uint8_t(n%256);if(!p.a)p={255,0,0,0};}
    const auto original=source.frames[1].pixels;const Rect roi{2,3,11,9};
    for(int n=0;n<6;++n){const auto choice=filter_preset(FilterPreset(n));const auto full=spatial_filter(source,choice,{},1),part=spatial_filter(source,choice,roi,1);for(int y=0;y<int(source.height);++y)for(int x=0;x<int(source.width);++x){const auto at=size_t(y)*source.width+x;CHECK(part.frames[0].pixels[at]==(x>=roi.x&&x<roi.x+roi.w&&y>=roi.y&&y<roi.y+roi.h?full.frames[0].pixels[at]:original[at]));if(choice.sharpen)CHECK(full.frames[0].pixels[at].a==original[at].a);}CHECK(source.frames[1].pixels==original);}
    auto other=source;for(auto &p:other.frames[1].pixels)if(!p.a)p={0,0,255,0};
    for(auto preset:{FilterPreset::Blur,FilterPreset::BlurSoft})CHECK(spatial_filter(source,filter_preset(preset),{},1).frames[0].pixels==spatial_filter(other,filter_preset(preset),{},1).frames[0].pixels);
    CHECK(spatial_filter(source,{false},Rect{1000,1000,2,2},1).frames[0].pixels==original);
    for(const auto size:{std::pair{1u,7u},std::pair{7u,1u},std::pair{1u,1u}}){auto thin=new_image(size.first,size.second);thin.frames[0].pixels[0]={240,60,10,127};for(int n=0;n<6;++n)CHECK(spatial_filter(thin,filter_preset(FilterPreset(n))).frames[0].pixels.size()==size.first*size.second);}
}
TEST(filters_reject_invalid_data_and_allocation_failure_without_touching_source) {
    auto source=load_image(fixtures/L"colors/source.png");const auto pixels=source.frames[0].pixels;
    THROWS(spatial_filter(source,{false,0}));THROWS(spatial_filter(source,{true,6,401}));THROWS(spatial_filter(source,{true,6,100,256}));THROWS(spatial_filter(source,{false,6,100,0,100}));THROWS(spatial_filter(source,{false}, {},1));
    for(int fail=1;fail<=2;++fail){int allocations{};EditMemory memory;memory.before_allocate=[&](size_t){if(++allocations==fail)throw std::bad_alloc();};THROWS(spatial_filter(source,{false}, {},0,memory));CHECK(allocations==fail&&source.frames[0].pixels==pixels);}
    EditMemory limited;limited.physical_bytes=16;THROWS(spatial_filter(source,{false},{},0,limited));auto broken=source;broken.frames[0].pixels.pop_back();THROWS(spatial_filter(broken,{false}));
}
int main(){if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;const auto result=run_tests();CoUninitialize();return result;}
