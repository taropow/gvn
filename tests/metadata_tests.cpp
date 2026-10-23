#include "gvn/codecs.h"
#include "gvn/edit.h"
#include "gvn/view_rotation.h"
#include "gvn/transfer.h"
#include "gvn/new_window.h"
#include "../src/image_metadata.h"
#include "test.h"
#include <cmath>
#ifdef near
#undef near
#endif
using namespace gvn;
const std::filesystem::path fixtures=GVN_FIXTURES;
void near(double a,double b){CHECK(std::abs(a-b)<.02);}
TEST(metadata_static_formats_report_source_depth_resolution_and_embedded_profile) {
    struct Case {const wchar_t *name;unsigned bits;double x,y;bool icc;};
    for(const auto c:{Case{L"rgba-300x150.png",32,300,150,true},Case{L"rgb-300x150.bmp",24,300,150,false},
                     Case{L"rgb-200x100.tiff",24,200,100,true},Case{L"gray16-100x50.tiff",16,100,50,false}}) {
        const auto image=load_image(fixtures/L"metadata"/c.name);const auto &m=image.metadata;
        CHECK(m.original_width==3&&m.original_height==2&&m.original_bpp==c.bits&&m.dpi_defined&&m.icc_profile==c.icc);
        near(m.dpi_x,c.x);near(m.dpi_y,c.y);
    }
    const auto jpeg=load_image(fixtures/L"metadata/rotated-300x150.jpg");
    CHECK(jpeg.width==2&&jpeg.height==3&&jpeg.metadata.original_width==2&&jpeg.metadata.original_height==3&&jpeg.metadata.original_bpp==24&&jpeg.metadata.icc_profile);
    near(jpeg.metadata.dpi_x,150);near(jpeg.metadata.dpi_y,300);
    LoadingOptions raw;raw.exif_orientation=false;
    const auto unrotated=load_image(fixtures/L"metadata/rotated-300x150.jpg",kDecodeLimit,raw);
    CHECK(unrotated.width==3&&unrotated.height==2);near(unrotated.metadata.dpi_x,300);near(unrotated.metadata.dpi_y,150);
}
TEST(metadata_psd_and_apng_preserve_all_original_pixels_frames_and_delays) {
    for(const auto names:{std::pair{L"metadata/rgba-300x150.psd",L"psd-8-0.psd"},std::pair{L"metadata/alpha-300x150.apng",L"blend.apng"}}) {
        const auto enriched=load_image(fixtures/names.first),original=load_image(fixtures/names.second);
        CHECK(enriched.metadata.original_bpp==32&&enriched.metadata.dpi_defined&&enriched.metadata.icc_profile);
        near(enriched.metadata.dpi_x,300);near(enriched.metadata.dpi_y,150);
        CHECK(enriched.width==original.width&&enriched.height==original.height&&enriched.frames.size()==original.frames.size()&&enriched.file_loops==original.file_loops);
        for(size_t n=0;n<original.frames.size();++n)CHECK(enriched.frames[n].pixels==original.frames[n].pixels&&enriched.frames[n].duration_ms==original.frames[n].duration_ms);
    }
    const auto high=load_image(fixtures/L"psd-16-0.psd");CHECK(high.metadata.original_bpp==64);
    const auto gray=load_image(fixtures/L"psd-8-1-gray.psd");CHECK(gray.metadata.original_bpp==16);
}
TEST(metadata_unspecified_dpi_uses_reference72_and_palette_depth_remains_distinct) {
    const auto alpha=load_image(fixtures/L"alpha.png");CHECK(!alpha.metadata.dpi_defined&&alpha.metadata.dpi_x==72&&alpha.metadata.dpi_y==72&&!alpha.metadata.icc_profile&&alpha.metadata.original_bpp==32);
    const auto indexed=load_image(fixtures/L"palette/png-4-alpha.png");CHECK(indexed.metadata.original_bpp==4);
    CHECK(load_image(fixtures/L"palette/bmp-8-rle.bmp").metadata.original_bpp==8);
    const auto gif=load_image(fixtures/L"disposal.gif");CHECK(gif.metadata.original_bpp>0&&gif.metadata.original_bpp<=8&&!gif.metadata.dpi_defined&&gif.metadata.dpi_x==72);
}
TEST(metadata_editing_retains_origin_and_resolution_while_quarter_rotation_swaps_axes) {
    const auto source=load_image(fixtures/L"metadata/rgba-300x150.png");
    for(const auto out:{crop(source,{0,0,2,1}),resize_image(source,6,4),canvas_image(source,5,4),effect(source,Effect::Negative),rotate_image(source,30)})
        CHECK(out.metadata==source.metadata);
    const auto quarter=rotate_quarters(source,1);near(quarter.metadata.dpi_x,150);near(quarter.metadata.dpi_y,300);
    CHECK(quarter.metadata.original_width==3&&quarter.metadata.original_height==2&&quarter.metadata.original_bpp==32);
    auto view=source;apply_view_rotation(view,2);near(view.metadata.dpi_x,150);near(view.metadata.dpi_y,300);
    CHECK(still_frame(source,0).metadata==source.metadata);
}
TEST(metadata_missing_axis_and_invalid_numbers_follow_reference_without_touching_pixels) {
    ImageMetadata m;CHECK(set_image_resolution(m,0,150)&&m.dpi_x==150&&m.dpi_y==150&&m.dpi_defined);
    CHECK(set_image_resolution(m,300,0)&&m.dpi_x==300&&m.dpi_y==300);
    const auto old=m;
    CHECK(!set_image_resolution(m,0,0)&&!set_image_resolution(m,-1,72)&&!set_image_resolution(m,NAN,72)&&!set_image_resolution(m,72,INFINITY)&&m==old);
    auto image=load_image(fixtures/L"alpha.png");const auto pixels=image.frames[0].pixels;
    auto bytes=read_file(fixtures/L"alpha.png");bytes.resize(33);bytes.insert(bytes.end(),{255,255,255,255,'p','H','Y','s',0,0,0,0});
    read_container_metadata(image,bytes);CHECK(image.frames[0].pixels==pixels&&!image.metadata.dpi_defined);
}
TEST(metadata_original_color_type_comes_from_container_or_native_pixel_format) {
    using T=ImageColorType;
    for(const auto c:{std::pair{L"palette/png-4-alpha.png",T::Palette},{L"palette/bmp-8-rle.bmp",T::Palette},{L"disposal.gif",T::Palette},
        {L"metadata/rgba-300x150.png",T::Rgb},{L"metadata/alpha-300x150.apng",T::Rgb},{L"metadata/rgb-300x150.bmp",T::Rgb},
        {L"loading/alpha-v4.bmp",T::Bitfields},{L"loading/alpha-v5.bmp",T::Bitfields},{L"metadata/rotated-300x150.jpg",T::Rgb},
        {L"psd-8-0.psd",T::Rgb},{L"psd-8-1-gray.psd",T::Gray},{L"metadata/rgb-200x100.tiff",T::Rgb},
        {L"metadata/gray16-100x50.tiff",T::Gray},{L"loading/gray2-background.png",T::Gray},{L"info/cmyk.tiff",T::Cmyk}}) {
        const auto image=load_image(fixtures/c.first);CHECK(image.metadata.original_color_type==c.second);
    }
    const auto palette=load_image(fixtures/L"palette/png-4-alpha.png");const auto out=rotate_image(palette,30);
    CHECK(!out.frames[0].indexed&&out.metadata==palette.metadata&&out.metadata.original_color_type==T::Palette);
    CHECK(std::wstring(image_color_type_name(T::Palette))==L"パレット"&&std::wstring(image_color_type_name(T::Bitfields))==L"ビットフィールド"&&std::wstring(image_color_type_name(static_cast<T>(255)))==L"不明");
}
TEST(metadata_clipboard_dib_and_new_window_pixel_snapshot_report_actual_source_types) {
    const auto source=load_image(fixtures/L"alpha.png");const auto dib=read_dib(make_dibv5(source,0));
    CHECK(dib.metadata.original_color_type==ImageColorType::Bitfields&&dib.metadata.original_bpp==32&&dib.metadata.original_width==source.width&&dib.frames[0].pixels==source.frames[0].pixels);
    const auto content=decode_new_window(encode_new_window(source));const auto &snapshot=std::get<Image>(content);
    CHECK(snapshot.metadata.original_color_type==ImageColorType::Rgb&&snapshot.metadata.original_bpp==32&&snapshot.metadata.original_height==source.height&&snapshot.frames[0].pixels==source.frames[0].pixels);
}
int main(){return run_tests();}
