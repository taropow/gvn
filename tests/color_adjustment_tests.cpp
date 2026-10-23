#include "gvn/color_adjustment.h"
#include "gvn/codecs.h"
#include "gvn/saving.h"
#include "color_adjustment_reference.h"
#include "test.h"
using namespace gvn;
namespace {
const auto fixtures=std::filesystem::path(GVN_SOURCE_ROOT)/L"tests/fixtures";
const ColorAdjustment choice{75,-40,15,false,GrayMethod::Luma};
}
TEST(color_adjustment_reference_hue_saturation_tint_lightness_and_gray_methods_match_within_one_level) {
    const auto source=load_image(fixtures/L"color-adjustment/source.png");const auto before=source.frames[0].pixels;
    for(const auto &row:gvn_test::color_references) {
        const auto reference=load_image(fixtures/L"color-adjustment"/row.file);const auto result=color_adjustment(source,row.choice);
        const auto &actual=result?result->frames[0].pixels:source.frames[0].pixels;CHECK(actual.size()==reference.frames[0].pixels.size());
        unsigned maximum{},different{};
        for(size_t i=0;i<actual.size();++i){const auto a=actual[i],b=reference.frames[0].pixels[i];CHECK(a.a==b.a);const auto delta=unsigned(std::max({std::abs(int(a.r)-b.r),std::abs(int(a.g)-b.g),std::abs(int(a.b)-b.b)}));maximum=std::max(maximum,delta);different+=delta!=0;}
        std::wcout<<L"Reference "<<row.file<<L" maximum="<<maximum<<L" different="<<different<<L'\n';CHECK(maximum<=1);
        if(row.choice.neutral())CHECK(!result);
    }
    CHECK(source.frames[0].pixels==before);
}
TEST(color_adjustment_whole_palette_keeps_duplicate_unused_indices_alpha_and_source_while_partial_promotes) {
    auto source=load_image(fixtures/L"palette/png-4-alpha.png");auto indices=std::make_shared<IndexedPixels>(*source.frames[0].indexed);
    for(uint32_t y=0;y<source.height;++y)for(uint32_t x=0;x<source.width;++x){const auto n=uint8_t(indices->index(x,y)%8);indices->set_index(x,y,n);source.frames[0].pixels[size_t(y)*source.width+x]=indices->palette[n];}
    source.frames[0].indexed=indices;const auto before=source.frames[0].pixels;const auto packed=indices->packed;
    const auto whole=color_adjustment(source,choice);CHECK(whole&&whole->frames[0].indexed&&whole->frames[0].indexed->packed==packed);
    CHECK(whole->frames[0].indexed->matches(whole->frames[0].pixels)&&whole->frames[0].indexed->palette.size()==indices->palette.size());
    CHECK(whole->frames[0].indexed->palette.back()!=indices->palette.back());
    for(size_t n=0;n<indices->palette.size();++n)CHECK(whole->frames[0].indexed->palette[n].a==indices->palette[n].a);
    const Rect roi{1,1,3,2};const auto partial=color_adjustment(source,choice,roi);CHECK(partial&&!partial->frames[0].indexed&&!partial->rgb16);
    for(uint32_t y=0;y<source.height;++y)for(uint32_t x=0;x<source.width;++x){const auto i=size_t(y)*source.width+x;const bool inside=x>=1&&x<4&&y>=1&&y<3;CHECK(partial->frames[0].pixels[i]==(inside?whole->frames[0].pixels[i]:before[i]));}
    CHECK(source.frames[0].pixels==before&&source.frames[0].indexed==indices&&indices->packed==packed);
}
TEST(color_adjustment_alpha_hidden_rgb_and_png_roundtrip_survive_both_modes) {
    auto source=load_image(fixtures/L"palette/png-4-alpha.png");source.frames[0].indexed.reset();const auto before=source.frames[0].pixels;
    for(const auto option:{choice,ColorAdjustment{300,70,15,true,GrayMethod::Average}}) {
        const auto result=color_adjustment(source,option);CHECK(result&&!result->frames[0].indexed);
        for(size_t i=0;i<before.size();++i)CHECK(result->frames[0].pixels[i].a==before[i].a);
        CHECK(decode(encode_png(*result,0)).frames[0].pixels==result->frames[0].pixels);
        if(option.unified)CHECK(result->frames[0].pixels[0].a==before[0].a&&result->frames[0].pixels[0]!=before[0]);
    }
    CHECK(source.frames[0].pixels==before);
}
TEST(color_adjustment_invalid_options_shape_limits_and_allocation_failure_leave_source_unchanged) {
    const auto source=load_image(fixtures/L"palette/png-4-alpha.png");const auto before=source.frames[0].pixels;const auto indices=source.frames[0].indexed;
    for(const auto bad:{ColorAdjustment{181,0,0,false,GrayMethod::Luma},ColorAdjustment{-1,25,0,true,GrayMethod::Luma},ColorAdjustment{361,25,0,true,GrayMethod::Luma},ColorAdjustment{0,-101,0,false,GrayMethod::Luma},ColorAdjustment{0,-1,0,true,GrayMethod::Luma},ColorAdjustment{0,0,101,false,GrayMethod::Luma},ColorAdjustment{0,0,0,false,static_cast<GrayMethod>(99)}}){CHECK(!valid_color_adjustment(bad));THROWS(color_adjustment(source,bad));}
    THROWS(color_adjustment({},choice));auto invalid=source;invalid.frames.push_back(invalid.frames[0]);THROWS(color_adjustment(invalid,choice));
    EditMemory memory;memory.physical_bytes=1;THROWS(color_adjustment(source,choice,{},memory));unsigned calls{};
    memory.physical_bytes=UINT64_MAX;memory.before_allocate=[&](size_t){++calls;throw std::bad_alloc();};THROWS(color_adjustment(source,choice,{},memory));CHECK(calls==1);
    auto stale=source;stale.frames[0].pixels[0]={1,3,7,11};const auto result=color_adjustment(stale,{90,0,0,false,GrayMethod::Luma});CHECK(result&&!result->frames[0].indexed&&result->frames[0].pixels[0].a==11);
    CHECK(source.frames[0].pixels==before&&source.frames[0].indexed==indices);
}
TEST(color_adjustment_noop_metadata_and_rgb16_promotion_have_explicit_semantics) {
    auto source=load_image(fixtures/L"auto-bmp/rgb565.bmp");CHECK(source.rgb16);source.metadata.dpi_defined=true;source.metadata.dpi_x=123.45;
    const auto before=source.frames[0].pixels;const auto original=source.rgb16;unsigned calls{};EditMemory memory;memory.before_allocate=[&](size_t){++calls;};
    CHECK(!color_adjustment(source,{}, {},memory)&&calls==0);CHECK(!color_adjustment(source,choice,{10000,10000,2,2},memory)&&calls==0);
    const auto result=color_adjustment(source,choice,{},memory);CHECK(result&&calls==1&&!result->rgb16&&!result->frames[0].indexed);
    CHECK(result->metadata==source.metadata&&result->path==source.path&&result->file_bytes==source.file_bytes&&result->load_ms==source.load_ms);
    CHECK(source.rgb16==original&&source.frames[0].pixels==before);
}
int main(){return run_tests();}
