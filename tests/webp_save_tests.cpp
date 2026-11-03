#include "gvn/archive.h"
#include "gvn/settings.h"
#include "gvn/transfer.h"
#include "test.h"
#include <cmath>
#include <fstream>
using namespace gvn;
const std::filesystem::path fixtures = std::filesystem::path(GVN_FIXTURES)/L"webp";
namespace {
struct Directory {
    std::filesystem::path path;
    Directory() {
        static unsigned index{};
        path=std::filesystem::temp_directory_path()/(L"GVN-WebP-save-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++index));
        CHECK(std::filesystem::create_directory(path));
    }
    ~Directory() { for(const auto &entry:std::filesystem::directory_iterator(path))std::filesystem::remove(entry.path());std::filesystem::remove(path); }
};
Image sample() {
    Image image;image.width=64;image.height=48;image.frames.resize(1);
    for(unsigned y=0;y<48;++y)for(unsigned x=0;x<64;++x)
        image.frames[0].pixels.push_back({uint8_t(x*13+y*3),uint8_t(x*3+y*17),uint8_t(x*7+y*11),uint8_t(x*5+y*9)});
    return image;
}
void write(const std::filesystem::path &path,const std::vector<uint8_t> &bytes) {
    std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char *>(bytes.data()),bytes.size());CHECK(output.good());
}
}
TEST(webp_save_lossless_roundtrip_preserves_hidden_rgb_and_input) {
    const auto source=load_image(fixtures/L"alpha.webp");const auto before=source.frames[0].pixels;
    const auto bytes=encode_webp(source,0);const auto restored=decode(bytes);
    CHECK(restored.format==L"WebP"&&restored.frames[0].pixels==before&&source.frames[0].pixels==before);
    CHECK(restored.frames[0].pixels[2]==Pixel{0,0,255,0});
}
TEST(webp_save_lossy_quality_changes_pixels_without_losing_alpha) {
    const auto source=sample();WebpSaveOptions options;options.lossless=false;options.quality=5;
    const auto low_bytes=encode_webp(source,0,options);const auto low=decode(low_bytes);
    options.quality=95;const auto high_bytes=encode_webp(source,0,options);const auto high=decode(high_bytes);
    CHECK(low_bytes!=high_bytes&&high_bytes.size()>low_bytes.size());
    uint64_t low_error{},high_error{};
    for(size_t n=0;n<source.frames[0].pixels.size();++n) {
        const auto p=source.frames[0].pixels[n],l=low.frames[0].pixels[n],h=high.frames[0].pixels[n];
        CHECK(l.a==p.a&&h.a==p.a);
        if(p.a)for(auto error:{std::pair{int(p.r)-l.r,int(p.r)-h.r},std::pair{int(p.g)-l.g,int(p.g)-h.g},std::pair{int(p.b)-l.b,int(p.b)-h.b}}) {
            low_error+=error.first*error.first;high_error+=error.second*error.second;
        }
    }
    CHECK(high_error<low_error);
}
TEST(webp_save_opaque_background_and_dpi_override_are_preserved) {
    const auto source=load_image(fixtures/L"alpha.webp");WebpSaveOptions options;options.preserve_alpha=false;options.matte=0x123456;
    auto metadata=source.metadata;metadata.dpi_defined=true;metadata.dpi_x=254;metadata.dpi_y=127;
    const auto restored=decode(encode_webp(source,0,options,&metadata));
    for(size_t n=0;n<source.frames[0].pixels.size();++n)CHECK(restored.frames[0].pixels[n]==over(source.frames[0].pixels[n],Pixel{0x12,0x34,0x56,255}));
    CHECK(restored.metadata.dpi_defined&&std::abs(restored.metadata.dpi_x-254)<.0001&&std::abs(restored.metadata.dpi_y-127)<.0001);
    CHECK(!source.metadata.dpi_defined);
}
TEST(webp_save_invalid_parameters_and_cancellation_do_not_create_output) {
    auto source=sample();WebpSaveOptions options;
    for(int quality:{-1,101}){options.quality=quality;THROWS(encode_webp(source,0,options));}
    options={};for(int method:{-1,7}){options.method=method;THROWS(encode_webp(source,0,options));}
    options={};options.matte=0x1000000;THROWS(encode_webp(source,0,options));
    THROWS(encode_webp(source,1));
    THROWS(encode_webp(source,0,{},nullptr,[]{return true;}));
    unsigned calls{};THROWS(encode_webp(source,0,{},nullptr,[&]{return ++calls>=3;}));CHECK(calls>=3);
    source.width=16384;THROWS(encode_webp(source,0));
    Directory folder;const auto path=folder.path/L"keep.webp";write(path,{1,2,3,4});
    SaveOptions save;save.webp.quality=101;source=sample();THROWS(save_image(source,0,path,save));CHECK(read_file(path)==std::vector<uint8_t>{1,2,3,4});
}
TEST(webp_save_extension_settings_and_default_format_roundtrip) {
    Directory folder;Settings settings;settings.saving.last_format=SaveFormat::Webp;
    settings.saving.webp={false,false,73,6,0x123456};settings.save(folder.path/L"settings.ini");
    Settings restored;restored.load(folder.path/L"settings.ini");CHECK(restored.saving==settings.saving);
    CHECK(save_format(L"mixed.WeBp")==SaveFormat::Webp&&save_target(L"image",restored.saving)==L"image.webp");
    restored.saving.uppercase_extension=true;CHECK(save_target(L"image",restored.saving)==L"image.WEBP");
    CHECK(WritePrivateProfileStringW(L"Saving",L"webp_quality",L"999",(folder.path/L"settings.ini").c_str()));
    restored.load(folder.path/L"settings.ini");CHECK(restored.saving.webp.quality==100);
}
TEST(webp_save_current_animation_frame_cannot_overwrite_original) {
    Directory folder;const auto source_path=folder.path/L"movie.webp";
    std::filesystem::copy_file(fixtures/L"animation.webp",source_path);const auto original=read_file(source_path);
    const auto source=load_image(source_path);SaveOptions options;options.last_format=SaveFormat::Webp;
    THROWS(save_image(source,1,source_path,options));CHECK(read_file(source_path)==original);
    const auto target=folder.path/L"frame.webp";save_image(source,1,target,options);
    const auto restored=load_image(target);CHECK(restored.frames.size()==1&&restored.frames[0].pixels==source.frames[1].pixels);
}
TEST(webp_save_drag_export_uses_webp_extension_and_exact_alpha) {
    const auto source=load_image(fixtures/L"alpha.webp");
    const auto path=export_drag_path(source,0,L"WebP export",SaveFormat::Webp,{});
    CHECK(path.extension()==L".webp"&&load_image(path).frames[0].pixels==source.frames[0].pixels);
    std::filesystem::remove(path);std::filesystem::remove(path.parent_path());
}
int main(int argc,char **argv) {
    const auto result=run_tests();
    if(!result&&argc>1) {
        const std::filesystem::path folder=argv[1];std::filesystem::create_directories(folder);
        const auto alpha=load_image(fixtures/L"alpha.webp");
        write(folder/L"lossless.webp",encode_webp(alpha,0));
        WebpSaveOptions opaque;opaque.preserve_alpha=false;opaque.matte=0x123456;
        write(folder/L"opaque.webp",encode_webp(alpha,0,opaque));
        auto metadata=alpha.metadata;metadata.dpi_defined=true;metadata.dpi_x=254;metadata.dpi_y=127;
        write(folder/L"dpi.webp",encode_webp(alpha,0,{},&metadata));
    }
    return result;
}
