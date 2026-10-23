#include "gvn/codecs.h"
#include "gvn/edit.h"
#include "gvn/saving.h"
#include "test.h"
using namespace gvn;
namespace {
Image source(unsigned bits) {
    Image image; image.width=7; image.height=3; image.format=L"PNG"; image.path=L"source 日本語.png";
    image.file_bytes=187; image.load_ms=9; image.metadata.original_bpp=bits;
    image.metadata.original_width=7; image.metadata.original_height=3;
    image.metadata.original_color_type=ImageColorType::Palette;
    image.metadata.dpi_x=123; image.metadata.dpi_y=234; image.metadata.dpi_defined=true;
    auto indexed=std::make_shared<IndexedPixels>(); indexed->width=7; indexed->height=3; indexed->bits=bits;
    const auto size=1u<<bits;
    for(unsigned i=0;i<size;++i) indexed->palette.push_back({uint8_t(i*7),uint8_t(i*11),uint8_t(i*13),uint8_t(i==1?127:i==2?0:255)});
    if(size>2) indexed->palette[0]=indexed->palette[1]=Pixel{31,37,41,127};
    indexed->packed.resize(indexed->stride()*image.height,0xff);
    image.frames.push_back({std::vector<Pixel>(21),77});
    for(unsigned y=0;y<3;++y) for(unsigned x=0;x<7;++x) {
        const auto value=uint8_t((x+y)%std::min(size,8u)); indexed->set_index(x,y,value);
        image.frames[0].pixels[y*7+x]=indexed->palette[value];
    }
    image.frames[0].indexed=indexed; return image;
}
}
TEST(palette_edit_changes_only_selected_numbers_and_keeps_duplicate_rgb_alpha_and_unused_entries) {
    for(unsigned bits:{1,2,4,8}) {
        const auto image=source(bits); const auto original=image.frames[0].pixels;
        const auto packed=image.frames[0].indexed->packed; auto colors=image.frames[0].indexed->palette;
        colors[1].r=101; colors[1].g=103; colors[1].b=107;
        const auto edited=edit_palette(image,colors);
        CHECK(edited.frames[0].indexed->packed==packed&&edited.frames[0].indexed->palette==colors);
        CHECK(edited.frames[0].indexed->matches(edited.frames[0].pixels));
        for(unsigned y=0;y<3;++y) for(unsigned x=0;x<7;++x) {
            const auto number=image.frames[0].indexed->index(x,y); const auto at=y*7+x;
            CHECK(edited.frames[0].pixels[at]==(number==1?colors[1]:original[at]));
            CHECK(edited.frames[0].pixels[at].a==original[at].a);
        }
        CHECK(image.frames[0].pixels==original&&image.frames[0].indexed->packed==packed);
        if(bits>=4) {
            const auto previous=edited.frames[0].pixels; colors.back()={17,19,23,colors.back().a};
            const auto unused=edit_palette(edited,colors); CHECK(unused.frames[0].pixels==previous);
            CHECK(unused.frames[0].indexed->palette.back()==colors.back());
        }
    }
}
TEST(palette_edit_png_roundtrip_and_undo_source_keep_every_index_and_transparent_rgb) {
    const auto image=source(4); auto colors=image.frames[0].indexed->palette;
    colors[2]={71,73,79,0}; const auto edited=edit_palette(image,colors);
    const auto decoded=decode(encode_png(edited,0));
    CHECK(decoded.frames[0].pixels==edited.frames[0].pixels&&decoded.frames[0].indexed->palette==colors);
    CHECK(decoded.frames[0].indexed->bits==4);
    for(unsigned y=0;y<3;++y) for(unsigned x=0;x<7;++x)
        CHECK(decoded.frames[0].indexed->index(x,y)==image.frames[0].indexed->index(x,y));
    const auto restored=edit_palette(edited,image.frames[0].indexed->palette);
    CHECK(restored.frames[0].pixels==image.frames[0].pixels&&restored.frames[0].indexed->packed==image.frames[0].indexed->packed);
    CHECK(edited.path==image.path&&edited.format==image.format&&edited.metadata==image.metadata);
    CHECK(edited.file_bytes==image.file_bytes&&edited.load_ms==image.load_ms&&edited.frames[0].duration_ms==77);
}
TEST(palette_edit_rejects_missing_stale_misshaped_resized_and_alpha_changing_tables) {
    const auto image=source(4); auto colors=image.frames[0].indexed->palette;
    auto changed=image; changed.frames[0].pixels[0]={11,13,17,19}; THROWS(edit_palette(changed,colors));
    changed=image; changed.frames[0].indexed.reset(); THROWS(edit_palette(changed,colors));
    changed=image; changed.width++; THROWS(edit_palette(changed,colors));
    changed=image; changed.frames.push_back(image.frames[0]); THROWS(edit_palette(changed,colors));
    colors.pop_back(); THROWS(edit_palette(image,colors)); colors=image.frames[0].indexed->palette;
    colors[1].a=255; THROWS(edit_palette(image,colors));
    auto invalid=std::make_shared<IndexedPixels>(*image.frames[0].indexed); invalid->packed.pop_back();
    changed=image; changed.frames[0].indexed=invalid; THROWS(edit_palette(changed,image.frames[0].indexed->palette));
    CHECK(image.frames[0].indexed->matches(image.frames[0].pixels));
}
TEST(palette_edit_allocation_failure_never_changes_source_or_palette_and_uses_one_rgba_allocation) {
    const auto image=source(8); const auto original=image.frames[0].pixels; const auto indexed=image.frames[0].indexed;
    auto colors=indexed->palette; colors[1].r=239;
    EditMemory memory; unsigned allocations=0; size_t count=0;
    memory.before_allocate=[&](size_t bytes){++allocations;count=bytes;throw std::bad_alloc();};
    THROWS(edit_palette(image,colors,memory)); CHECK(allocations==1&&count==original.size()*sizeof(Pixel));
    CHECK(image.frames[0].pixels==original&&image.frames[0].indexed==indexed&&indexed->palette[1].r!=239);
    memory.before_allocate=[&](size_t bytes){++allocations;count=bytes;};
    const auto actual=edit_palette(image,colors,memory);
    CHECK(allocations==2&&actual.frames[0].indexed->matches(actual.frames[0].pixels));
}
int main(){return run_tests();}
