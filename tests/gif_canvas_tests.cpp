#include "gvn/codecs.h"
#include "gvn/edit.h"
#include "gvn/saving.h"
#include "test.h"
#include <array>
using namespace gvn;
namespace {
const auto folder=std::filesystem::path(GVN_SOURCE_ROOT)/L"tests/fixtures/gif-canvas";
Image load(const wchar_t *name, const LoadingOptions &options={}) {
    return load_image(folder/(std::wstring(name)+L".gif"),kDecodeLimit,options);
}
void identity(const Image &image, const IndexedPixels &original) {
    const auto &indexed=image.frames[0].indexed;
    CHECK(indexed&&indexed->matches(image.frames[0].pixels)&&indexed->palette==original.palette);
    CHECK(indexed->width==original.width&&indexed->height==original.height);
    for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x)
        CHECK(indexed->index(x,y)==original.index(x,y));
}
}
TEST(gif_logical_screen_keeps_duplicate_indices_and_actual_background_number) {
    const auto image=load(L"global-duplicate");
    CHECK(image.width==11&&image.height==7&&image.frames.size()==1);
    const auto &indexed=image.frames[0].indexed;
    CHECK(indexed&&indexed->bits==8&&indexed->palette.size()==256&&indexed->matches(image.frames[0].pixels));
    CHECK(indexed->palette[0]==indexed->palette[1]);
    CHECK(indexed->index(0,0)==1&&indexed->index(2,1)==0&&indexed->index(3,1)==1&&indexed->index(4,1)==2);
    identity(decode(encode_png(image,0)),*indexed);
    SaveOptions options;const auto bmp=encode_for_save(image,0,SaveFormat::Bmp,options);CHECK(bmp[28]==8);
    identity(decode(bmp),*indexed);
}
TEST(gif_logical_screen_appends_exact_clear_color_without_replacing_source_table_entries) {
    for(auto name:{L"global-alpha",L"local-background"}) {
        const auto image=load(name);const auto &indexed=image.frames[0].indexed;
        CHECK(indexed&&indexed->palette.size()==17&&indexed->matches(image.frames[0].pixels));
        CHECK(indexed->index(0,0)==16&&indexed->index(2,1)==0&&indexed->index(3,1)==1&&indexed->index(4,1)==2);
        CHECK(indexed->palette[0]==indexed->palette[1]);
        if(std::wstring(name)==L"global-alpha") {
            CHECK(indexed->palette[2]==Pixel{2,142,218,0});CHECK(indexed->palette[16]==Pixel{0,0,0,0});
        } else CHECK(indexed->palette[16]==Pixel{213,215,217,255});
        identity(decode(encode_png(image,0)),*indexed);
        LoadingOptions options;options.preserve_alpha=false;options.background=0x123456;
        const auto flattened=load(name,options);CHECK(flattened.frames[0].indexed&&flattened.frames[0].indexed->matches(flattened.frames[0].pixels));
    }
}
TEST(gif_local_table_overflow_and_tight_budget_keep_current_rgba_without_false_indices) {
    const auto image=load(L"local-overflow");CHECK(!image.frames[0].indexed);
    CHECK(image.frames[0].pixels[0]==Pixel{213,215,217,255});
    SaveOptions options;CHECK(encode_for_save(image,0,SaveFormat::Bmp,options)[28]==24);
    const auto source=load(L"global-duplicate");const auto file=folder/L"global-duplicate.gif";
    const auto tight=load_image(file,source.width*source.height*sizeof(Pixel));
    CHECK(!tight.frames[0].indexed&&tight.frames[0].pixels==source.frames[0].pixels);
    const auto exact=load_image(file,source.bytes());CHECK(exact.frames[0].indexed&&exact.bytes()==source.bytes());
    const auto less=load_image(file,source.bytes()-1);CHECK(!less.frames[0].indexed&&less.frames[0].pixels==source.frames[0].pixels);
    const auto cropped=load(L"cropped-global");CHECK(cropped.width==5&&cropped.height==2&&cropped.frames[0].indexed);
    CHECK(cropped.frames[0].indexed->matches(cropped.frames[0].pixels)&&cropped.frames[0].indexed->index(1,0)==1);
}
TEST(gif_logical_palette_survives_geometry_and_never_overwrites_new_colors) {
    const auto source=load(L"global-alpha");const auto rotated=rotate_quarters(source,1);
    CHECK(rotated.frames[0].indexed&&rotated.frames[0].indexed->matches(rotated.frames[0].pixels));
    identity(decode(encode_png(rotated,0)),*rotated.frames[0].indexed);
    auto stale=source;stale.frames[0].pixels[0]={19,23,29,31};
    CHECK(!stale.frames[0].indexed->matches(stale.frames[0].pixels));
    const auto encoded=encode_png(stale,0);CHECK(encoded[25]==6);
    CHECK(decode(encoded).frames[0].pixels==stale.frames[0].pixels);
    const auto negative=effect(source,Effect::Negative);CHECK(!negative.frames[0].indexed);
    CHECK(decode(encode_png(negative,0)).frames[0].pixels==negative.frames[0].pixels);
    identity(decode(encode_png(source,0)),*source.frames[0].indexed);
}
int main(){return run_tests();}
