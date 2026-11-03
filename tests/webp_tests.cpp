#include "gvn/archive.h"
#include "gvn/settings.h"
#include "test.h"
#include <cstring>
using namespace gvn;
const std::filesystem::path fixtures = std::filesystem::path(GVN_FIXTURES)/L"webp";
std::vector<Pixel> oracle(const std::wstring &name, unsigned frame = 0) {
    const auto bytes=read_file(fixtures/(name+L"."+std::to_wstring(frame)+L".rgba"));
    CHECK(bytes.size()%sizeof(Pixel)==0);
    std::vector<Pixel> pixels(bytes.size()/sizeof(Pixel));
    std::memcpy(pixels.data(),bytes.data(),bytes.size()); return pixels;
}
TEST(webp_lossless_alpha_hidden_rgb_and_icc_are_preserved) {
    const auto image=load_image(fixtures/L"alpha.webp");
    CHECK(image.width==3&&image.height==2&&image.format==L"WebP"&&!image.animated());
    CHECK(image.frames[0].pixels==oracle(L"alpha.webp"));
    CHECK(image.frames[0].pixels[2]==Pixel{0,0,255,0});
    CHECK(image.metadata.original_width==3&&image.metadata.original_height==2&&image.metadata.original_bpp==32);
    CHECK(image.metadata.icc_profile&&image.metadata.icc_bytes>0&&image.metadata.original_color_type==ImageColorType::Rgb);
}
TEST(webp_lossy_matches_independent_pixel_oracle) {
    const auto image=load_image(fixtures/L"lossy.webp");
    CHECK(image.width==32&&image.height==24&&image.metadata.original_bpp==24);
    CHECK(image.frames[0].pixels==oracle(L"lossy.webp"));
}
TEST(webp_animation_composites_alpha_disposal_timing_and_loop_count) {
    const auto image=load_image(fixtures/L"animation.webp");
    CHECK(image.width==4&&image.height==4&&image.frames.size()==3&&image.file_loops==2);
    const uint64_t durations[]{40,80,120};
    for(unsigned frame=0;frame<3;++frame) {
        CHECK(image.frames[frame].duration_ms==durations[frame]);
        CHECK(image.frames[frame].pixels==oracle(L"animation.webp",frame));
    }
    CHECK(load_image(fixtures/L"infinite.webp").file_loops==0);
}
TEST(webp_first_frame_setting_avoids_full_movie_budget) {
    LoadingOptions first;first.webp_animation=false;
    const auto image=load_image(fixtures/L"animation.webp",64,first);
    CHECK(image.frames.size()==1&&image.frames[0].pixels==oracle(L"animation.webp"));
    THROWS(load_image(fixtures/L"animation.webp",64));
    THROWS(load_image(fixtures/L"animation.webp",63,first));
}
TEST(webp_transparency_and_background_follow_loading_settings) {
    LoadingOptions options;options.webp_transparency=false;
    auto expected=oracle(L"alpha.webp");for(auto &p:expected)p.a=255;
    CHECK(load_image(fixtures/L"alpha.webp",kDecodeLimit,options).frames[0].pixels==expected);
    options.webp_transparency=true;options.preserve_alpha=false;options.background=0x305070;
    expected=oracle(L"alpha.webp");for(auto &p:expected)p=over(p,Pixel{0x30,0x50,0x70,255});
    CHECK(load_image(fixtures/L"alpha.webp",kDecodeLimit,options).frames[0].pixels==expected);
    const auto movie=load_image(fixtures/L"animation.webp",kDecodeLimit,options);
    for(const auto &frame:movie.frames)for(const auto &p:frame.pixels)CHECK(p.a==255);
}
TEST(webp_all_exif_orientations_and_disable_keep_original_metadata) {
    for(int orientation=1;orientation<=8;++orientation) {
        const auto name=L"exif-"+std::to_wstring(orientation)+L".webp";
        const auto image=load_image(fixtures/name);
        CHECK(image.width==(orientation>=5?3:2)&&image.height==(orientation>=5?2:3));
        CHECK(image.frames[0].pixels==oracle(name));
        CHECK(image.metadata.original_width==2&&image.metadata.original_height==3&&image.metadata.dpi_defined);
        CHECK(image.metadata.dpi_x==(orientation>=5?150:300)&&image.metadata.dpi_y==(orientation>=5?300:150));
        LoadingOptions raw;raw.exif_orientation=false;
        const auto unrotated=load_image(fixtures/name,kDecodeLimit,raw);
        CHECK(unrotated.width==2&&unrotated.height==3&&unrotated.frames[0].pixels==oracle(L"exif-1.webp"));
    }
}
TEST(webp_signature_detection_truncation_memory_and_cancellation_are_bounded) {
    auto bytes=read_file(fixtures/L"alpha.webp");CHECK(is_webp(bytes));
    CHECK(decode(bytes,L".png").format==L"WebP"); // Content, not the extension, selects the codec.
    for(size_t length:{size_t(0),size_t(12),bytes.size()/2,bytes.size()-1})THROWS(decode(std::span(bytes).first(length)));
    THROWS(decode(bytes,L"",23));
    THROWS(load_image(fixtures/L"alpha.webp",kDecodeLimit,{},bytes.size()-1));
    THROWS(decode(bytes,L"",kDecodeLimit,{},[]{return true;}));
    for(size_t p=24;p<30;++p)bytes[p]=255;
    THROWS(decode(bytes));
}
TEST(webp_uppercase_extension_folder_and_zip_use_the_native_decoder) {
    CHECK(supported(L"image.WEBP")&&!supported(L"image.wp2"));
    Archive archive(read_file(fixtures/L"images.zip"));const auto entries=archive.entries();CHECK(entries.size()==2);
    for(const auto &entry:entries) {
        const auto image=archive.read(entry.index);CHECK(image.format==L"WebP");
        CHECK(image.frames[0].pixels==oracle(entry.name.find(L"alpha")!=std::wstring::npos?L"alpha.webp":L"animation.webp"));
    }
}
TEST(webp_settings_roundtrip_legacy_defaults_and_cache_identity) {
    const auto path=std::filesystem::temp_directory_path()/(L"GVN-WebP-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    Settings settings;CHECK(settings.webp_animation&&settings.webp_transparency);
    const auto defaults=settings.loading_options().flags();settings.webp_animation=false;
    const auto first=settings.loading_options().flags();settings.webp_transparency=false;
    CHECK(first!=defaults&&settings.loading_options().flags()!=first);
    settings.save(path);Settings restored;restored.load(path);
    CHECK(!restored.webp_animation&&!restored.webp_transparency&&restored.loading_options()==settings.loading_options());
    CHECK(WritePrivateProfileStringW(L"Viewer",L"webp_animation",nullptr,path.c_str()));
    CHECK(WritePrivateProfileStringW(L"Viewer",L"webp_transparency",nullptr,path.c_str()));
    Settings legacy;legacy.load(path);CHECK(legacy.webp_animation&&legacy.webp_transparency);
    std::filesystem::remove(path);
}

namespace {
uint32_t little32(const std::vector<uint8_t> &v,size_t p) {
    return uint32_t(v[p])|(uint32_t(v[p+1])<<8)|(uint32_t(v[p+2])<<16)|(uint32_t(v[p+3])<<24);
}
void store32(std::vector<uint8_t> &v,size_t p,uint32_t value) {
    for(unsigned n=0;n<4;++n)v[p+n]=static_cast<uint8_t>(value>>(8*n));
}
std::vector<uint8_t> replace_chunk(const std::vector<uint8_t> &input,const char *tag,const std::vector<uint8_t> &payload) {
    std::vector<uint8_t> output(input.begin(),input.begin()+12);
    for(size_t p=12;p+8<=input.size();) {
        const size_t size=little32(input,p+4),end=p+8+size+(size&1);CHECK(end<=input.size());
        if(!std::memcmp(input.data()+p,tag,4)) {
            const auto start=output.size();output.insert(output.end(),input.begin()+p,input.begin()+p+8);
            store32(output,start+4,static_cast<uint32_t>(payload.size()));
            output.insert(output.end(),payload.begin(),payload.end());if(payload.size()&1)output.push_back(0);
        } else output.insert(output.end(),input.begin()+p,input.begin()+end);
        p=end;
    }
    store32(output,4,static_cast<uint32_t>(output.size()-8));return output;
}
}
TEST(webp_little_endian_prefixed_and_malformed_exif_are_bounded) {
    const auto source=read_file(fixtures/L"exif-6.webp");
    const std::vector<uint8_t> tiff{'I','I',42,0,8,0,0,0,1,0,0x12,1,3,0,1,0,0,0,6,0,0,0,0,0,0,0};
    for(const bool prefix:{false,true}) {
        auto payload=tiff;if(prefix)payload.insert(payload.begin(),{'E','x','i','f',0,0});
        const auto image=decode(replace_chunk(source,"EXIF",payload));
        CHECK(image.width==3&&image.height==2&&image.frames[0].pixels==oracle(L"exif-6.webp"));
    }
    auto broken=tiff;store32(broken,4,0xffffffff);
    const auto image=decode(replace_chunk(source,"EXIF",broken));
    CHECK(image.width==2&&image.height==3&&image.frames[0].pixels==oracle(L"exif-1.webp"));
}
TEST(webp_animation_rejects_timestamp_overflow_before_the_codec_addition) {
    const auto source=read_file(fixtures/L"animation.webp");
    std::vector<uint8_t> movie(source.begin(),source.begin()+12),frame;
    for(size_t p=12;p+8<=source.size();) {
        const size_t size=little32(source,p+4),end=p+8+size+(size&1);CHECK(end<=source.size());
        if(!std::memcmp(source.data()+p,"ANMF",4)) {
            if(frame.empty())frame.assign(source.begin()+p,source.begin()+end);
        } else movie.insert(movie.end(),source.begin()+p,source.begin()+end);
        p=end;
    }
    CHECK(frame.size()>23);frame[20]=frame[21]=frame[22]=255;
    for(int i=0;i<130;++i)movie.insert(movie.end(),frame.begin(),frame.end());
    store32(movie,4,static_cast<uint32_t>(movie.size()-8));
    bool bounded{};try{decode(movie);}catch(const std::runtime_error &e){bounded=std::string(e.what()).find("timeline")!=std::string::npos;}
    CHECK(bounded);
    LoadingOptions first;first.webp_animation=false;CHECK(decode(movie,L"",64,first).frames.size()==1);
}
TEST(webp_animation_can_cancel_after_decoding_a_frame) {
    const auto source=read_file(fixtures/L"animation.webp");unsigned checks{};
    THROWS(decode(source,L"",kDecodeLimit,{},[&]{return ++checks>=4;}));CHECK(checks==4);
}

int main(){return run_tests();}
