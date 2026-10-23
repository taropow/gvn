#include "gvn/codecs.h"
#include "gvn/edit.h"
#include "gvn/view_rotation.h"
#include "gvn/saving.h"
#include "test.h"
#include <fstream>
#include <string_view>
using namespace gvn;
namespace {
const auto fixtures=std::filesystem::path(GVN_SOURCE_ROOT)/L"tests/fixtures";
std::vector<uint8_t> unpack(const IndexedPixels &source) {
    std::vector<uint8_t> out;for(uint32_t y=0;y<source.height;++y)for(uint32_t x=0;x<source.width;++x)out.push_back(source.index(x,y));return out;
}
struct FailingSource:IndexedSource {
    mutable unsigned calls{};
    size_t bytes() const override{return 17;}
    std::shared_ptr<const IndexedPixels> read(size_t) const override{++calls;throw std::bad_alloc();}
};
struct CountingSource:IndexedSource {
    std::shared_ptr<const IndexedSource> source;
    mutable unsigned calls{};
    explicit CountingSource(std::shared_ptr<const IndexedSource> value):source(std::move(value)){}
    size_t bytes() const override{return source->bytes();}
    std::shared_ptr<const IndexedPixels> read(size_t frame) const override{++calls;return source->read(frame);}
};
std::filesystem::path output;
void artifact(const std::wstring &name,std::span<const uint8_t> bytes) {
    if(output.empty())return;
    std::ofstream file{output/name,std::ios::binary};file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));CHECK(bool(file));
}
}
TEST(animated_indices_are_lazy_snapshot_data_and_keep_duplicate_and_unused_palette_slots) {
    auto bytes=read_file(fixtures/L"animated-saving/source.gif");const auto size=bytes.size();auto image=decode_animation(bytes,kDecodeLimit);
    CHECK(image.frames.size()==2&&image.indexed_source&&image.indexed_source->bytes()==size);
    CHECK(image.bytes()==size+2*9*7*sizeof(Pixel));const auto copy=image;CHECK(copy.indexed_source==image.indexed_source);
    std::fill(bytes.begin(),bytes.end(),0);CHECK(std::all_of(image.frames.begin(),image.frames.end(),[](const Frame &f){return !f.indexed;}));
    for(size_t n=0;n<2;++n) {
        const auto indices=image_indices(image,n);CHECK(indices&&indices->bits==8&&indices->palette.size()==256&&indices->palette[0]==indices->palette[1]);
        for(uint32_t y=0;y<7;++y)for(uint32_t x=0;x<9;++x)CHECK(indices->index(x,y)==uint8_t(x+y*9+n*80));
        for(unsigned i=0;i<256;++i){const auto source=i==1?0:i;CHECK(indices->palette[i]==Pixel{uint8_t(source*31+20),uint8_t(source*53+40),uint8_t(source*97+60),255});}
        CHECK(indices->matches(image.frames[n].pixels)&&!image.frames[n].indexed);
    }
}
TEST(animated_disposal_background_previous_transparency_and_loading_alpha_projection_keep_numbers) {
    std::array<std::vector<uint8_t>,4> expected;for(auto &r:expected)r.assign(24,0);
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)expected[0][y*6+x]=1;
    expected[1]=expected[0];for(unsigned y=1;y<3;++y)for(unsigned x=1;x<3;++x)expected[1][y*6+x]=2;
    for(unsigned y=0;y<2;++y)for(unsigned x=2;x<4;++x)expected[2][y*6+x]=3;
    for(unsigned y=1;y<3;++y)for(unsigned x=3;x<5;++x)expected[3][y*6+x]=4;
    for(const bool preserve:{false,true})for(const bool file_background:{false,true}) {
        LoadingOptions options;options.preserve_alpha=preserve;options.gif_background=file_background;options.background=0x123456;
        const auto image=load_image(fixtures/L"disposal.gif",kDecodeLimit,options);CHECK(image.indexed_source&&image.frames.size()==4);
        for(size_t n=0;n<4;++n){const auto indices=image_indices(image,n);CHECK(indices&&unpack(*indices)==expected[n]&&indices->matches(image.frames[n].pixels));}
    }
}
TEST(animated_local_tables_preserve_painted_numbers_or_reject_conflicting_number_colors) {
    for(const auto name:{L"local-compatible",L"local-conflict"}) {
        const auto image=load_image(fixtures/(std::wstring(L"animation-indices/")+name+L".gif"));CHECK(image.indexed_source);
        for(size_t n=0;n<2;++n){const auto expected=load_image(fixtures/(std::wstring(L"animation-indices/")+name+L"-"+std::to_wstring(n)+L".png"));CHECK(image.frames[n].pixels==expected.frames[0].pixels);}
        const auto first=image_indices(image,0);CHECK(first&&unpack(*first)==std::vector<uint8_t>({1,2,0,0,1,2,0,0}));
        const auto second=image_indices(image,1);
        if(std::wstring_view(name)==L"local-compatible")CHECK(second&&unpack(*second)==std::vector<uint8_t>({1,2,2,0,1,2,0,0})&&second->palette[3]==Pixel{0,0,200,255});
        else CHECK(!second);
    }
}
TEST(animated_deferred_indices_follow_view_rotation_and_detach_into_editable_current_frame) {
    const auto source=load_image(fixtures/L"disposal.gif");const int modes[]{0,2,7,1};
    for(int quarters=0;quarters<4;++quarters) {
        auto rotated=source;apply_view_rotation(rotated,modes[quarters]);CHECK(rotated.indexed_source==source.indexed_source&&rotated.indexed_quarters==unsigned(quarters));
        for(size_t n=0;n<source.frames.size();++n) {
            const auto expected=rotate_quarters(still_frame(source,n),quarters);const auto indices=image_indices(rotated,n);
            CHECK(indices&&indices->packed==expected.frames[0].indexed->packed&&indices->palette==expected.frames[0].indexed->palette);
            const auto current=still_frame(rotated,n);CHECK(!current.indexed_source&&current.indexed_quarters==0&&current.frames[0].indexed&&current.frames[0].pixels==rotated.frames[n].pixels);
            const auto selected=crop(current,{0,0,2,2});CHECK(selected.frames[0].indexed&&selected.frames[0].indexed->matches(selected.frames[0].pixels));
        }
    }
    auto twice=source;apply_view_rotation(twice,2);apply_view_rotation(twice,1);CHECK(twice.indexed_quarters==0&&image_indices(twice,1)->packed==image_indices(source,1)->packed);
}
TEST(animated_optional_provenance_budget_stale_pixels_and_read_failure_leave_rgba_available) {
    const auto bytes=read_file(fixtures/L"animated-saving/source.gif");constexpr size_t rgba=2*9*7*sizeof(Pixel);
    auto exact=decode_animation(bytes,rgba+bytes.size());CHECK(exact.indexed_source&&exact.bytes()==rgba+bytes.size());
    auto tight=decode_animation(bytes,rgba+bytes.size()-1);CHECK(!tight.indexed_source&&tight.bytes()==rgba&&tight.frames[1].pixels==exact.frames[1].pixels);
    auto full=decode_animation(bytes,rgba);CHECK(!full.indexed_source&&full.bytes()==rgba);
    auto changed=exact;changed.frames[0].pixels[0].r^=1;CHECK(!image_indices(changed,0)&&image_indices(exact,0));
    auto failing=std::make_shared<FailingSource>();changed=exact;changed.indexed_source=failing;CHECK(!image_indices(changed,0)&&failing->calls==1&&changed.bytes()==rgba+17);
    changed.frames[0].indexed=image_indices(exact,0);CHECK(image_indices(changed,0)==changed.frames[0].indexed&&failing->calls==1);
    THROWS(image_indices(exact,2));
}
TEST(animated_direct_png_bmp_auto_and_gif_saves_keep_original_numbers_and_read_once) {
    auto image=load_image(fixtures/L"animated-saving/source.gif");const auto original=image.frames;
    auto counter=std::make_shared<CountingSource>(image.indexed_source);image.indexed_source=counter;
    for(size_t n=0;n<2;++n)for(const auto format:{SaveFormat::Png,SaveFormat::Bmp,SaveFormat::Gif}) {
        const auto before=counter->calls;const auto bytes=encode_for_save(image,n,format,{});CHECK(counter->calls==before+1);
        LoadingOptions options;options.gif_animation=false;const auto saved=decode(bytes,L"",kDecodeLimit,options);
        const auto indices=saved.frames[0].indexed;CHECK(indices&&indices->bits==8&&indices->palette.size()==256);
        CHECK(saved.frames[0].pixels==image.frames[n].pixels);
        for(uint32_t y=0;y<7;++y)for(uint32_t x=0;x<9;++x)CHECK(indices->index(x,y)==uint8_t(x+y*9+n*80));
        for(unsigned i=0;i<256;++i){const auto value=i==1?0:i;CHECK(indices->palette[i]==Pixel{uint8_t(value*31+20),uint8_t(value*53+40),uint8_t(value*97+60),255});}
        const auto extension=format==SaveFormat::Png?L".png":format==SaveFormat::Bmp?L".bmp":L".gif";
        artifact(L"duplicates-"+std::to_wstring(n)+extension,bytes);
        if(format==SaveFormat::Png) {
            const auto calls=counter->calls;const auto interchange=decode(encode(image,n,L".png"));CHECK(counter->calls==calls+1);
            CHECK(interchange.frames[0].indexed&&interchange.frames[0].indexed->packed==indices->packed&&interchange.frames[0].indexed->palette==indices->palette);
            CHECK(interchange.frames[0].pixels==image.frames[n].pixels);
        }
    }
    const auto before=counter->calls;const auto raster=index_image(image,1);CHECK(counter->calls==before+1&&raster.indices[0]==80);
    CHECK(image.frames.size()==original.size());for(size_t n=0;n<2;++n)CHECK(image.frames[n].pixels==original[n].pixels&&!image.frames[n].indexed);
}
TEST(animated_png_alpha_and_rotated_current_frame_preserve_exact_composite_and_numbers) {
    auto image=load_image(fixtures/L"disposal.gif");const auto original=image;
    for(size_t n=0;n<4;++n) {
        const auto bytes=encode_png(image,n);const auto saved=decode(bytes);const auto indices=image_indices(image,n);
        CHECK(saved.frames[0].pixels==image.frames[n].pixels&&saved.frames[0].indexed&&indices);
        CHECK(saved.frames[0].indexed->packed==indices->packed&&saved.frames[0].indexed->palette==indices->palette);
        artifact(L"disposal-"+std::to_wstring(n)+L".png",bytes);
    }
    apply_view_rotation(image,2);const auto bytes=encode_png(image,3);const auto saved=decode(bytes);const auto indices=image_indices(image,3);
    CHECK(saved.width==4&&saved.height==6&&saved.frames[0].pixels==image.frames[3].pixels&&indices);
    CHECK(saved.frames[0].indexed&&saved.frames[0].indexed->packed==indices->packed);
    CHECK(original.width==6&&original.height==4);artifact(L"disposal-rotated.png",bytes);
}
TEST(animated_conflicting_tables_and_stale_source_save_current_rgba_without_false_original_numbers) {
    auto image=load_image(fixtures/L"animation-indices/local-conflict.gif");CHECK(!image_indices(image,1));
    auto saved=decode(encode_png(image,1));CHECK(!saved.frames[0].indexed&&saved.frames[0].pixels==image.frames[1].pixels);
    artifact(L"conflict.png",encode_png(image,1));
    image=load_image(fixtures/L"animated-saving/source.gif");image.frames[1].pixels[0].r^=1;
    saved=decode(encode_png(image,1));CHECK(!saved.frames[0].indexed&&saved.frames[0].pixels==image.frames[1].pixels);
    for(const auto format:{SaveFormat::Bmp,SaveFormat::Gif}) {
        saved=decode(encode_for_save(image,1,format,{}));CHECK(saved.frames[0].pixels==image.frames[1].pixels);
    }
}
int main(int argc,char **argv){if(argc>1){output=argv[1];std::filesystem::create_directories(output);}return run_tests();}
