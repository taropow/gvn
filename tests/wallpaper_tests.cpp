#include "gvn/wallpaper.h"
#include "gvn/codecs.h"
#include "test.h"
#include <windows.h>
#include <objbase.h>
#include <cstring>
#include <fstream>
#include <sstream>
using namespace gvn;
namespace {
struct Directory {
    std::filesystem::path path;
    Directory() {
        GUID guid{};CHECK(SUCCEEDED(CoCreateGuid(&guid)));wchar_t name[40]{};CHECK(StringFromGUID2(guid,name,40));
        path=std::filesystem::temp_directory_path()/(std::wstring(L"GVN-wallpaper-tests-")+name);
        CHECK(std::filesystem::create_directory(path));
    }
    ~Directory() {std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
Image decoded(const std::vector<uint8_t> &bytes,const std::filesystem::path &path) {
    {std::ofstream file(path,std::ios::binary);CHECK(file.good());file.write(reinterpret_cast<const char *>(bytes.data()),bytes.size());CHECK(file.good());}
    return load_image(path);
}
unsigned bits(const std::vector<uint8_t> &bytes) {CHECK(bytes.size()>=54);uint16_t value{};memcpy(&value,bytes.data()+28,2);return value;}
}
TEST(wallpaper_placement_rejects_invalid_values_without_resampling_contract) {
    CHECK(wallpaper_style(WallpaperMode::Center)==WallpaperStyle{L"0",L"0"});
    CHECK(wallpaper_style(WallpaperMode::Tile)==WallpaperStyle{L"1",L"0"});
    CHECK(wallpaper_style(WallpaperMode::Stretch)==WallpaperStyle{L"0",L"2"});
    CHECK(wallpaper_style(WallpaperMode::Width)==WallpaperStyle{L"0",L"10"});
    CHECK(wallpaper_style(WallpaperMode::Height)==WallpaperStyle{L"0",L"6"});
    THROWS(wallpaper_style(static_cast<WallpaperMode>(-1)));THROWS(wallpaper_style(static_cast<WallpaperMode>(5)));
}
namespace {
void reference_exports(const std::filesystem::path &directory) {
    const auto fixture=std::filesystem::path(GVN_FIXTURES)/L"wallpaper-reference";
    std::ifstream file(fixture/L"cases.tsv");CHECK(file.good());std::string line;CHECK(std::getline(file,line));
    size_t count{},pixels{};
    while (std::getline(file,line)) {
        if (!line.empty() && line.back()=='\r') line.pop_back();
        std::vector<std::string> cells;std::istringstream row(line);std::string cell;
        while (std::getline(row,cell,'\t')) cells.push_back(cell);CHECK(cells.size()==11);
        auto source=load_image(fixture/cells[2]);const auto expected=load_image(fixture/cells[3]);
        const auto original=source.frames[0].pixels;const auto indexed=source.frames[0].indexed;
        const auto source_path=source.path;const auto source_format=source.format;
        const auto placement=wallpaper_style(static_cast<WallpaperMode>(std::stoi(cells[1])));
        CHECK(placement.tile==std::wstring(cells[7].begin(),cells[7].end()) && placement.style==std::wstring(cells[8].begin(),cells[8].end()));
        const auto bytes=encode_wallpaper_bmp(source,0);const auto actual=decoded(bytes,directory/(cells[0]+".bmp"));
        CHECK(actual.width==std::stoul(cells[4]) && actual.height==std::stoul(cells[5]) && bits(bytes)==std::stoul(cells[6]));
        CHECK(actual.frames.size()==1 && actual.frames[0].pixels==expected.frames[0].pixels);
        if (expected.frames[0].indexed) {
            CHECK(actual.frames[0].indexed && actual.frames[0].indexed->palette==expected.frames[0].indexed->palette);
            for (uint32_t y=0;y<actual.height;++y) for (uint32_t x=0;x<actual.width;++x)
                CHECK(actual.frames[0].indexed->index(x,y)==expected.frames[0].indexed->index(x,y));
        }
        CHECK(source.frames[0].pixels==original && source.frames[0].indexed==indexed && source.frames.size()==1);
        CHECK(source.path==source_path && source.format==source_format);
        ++count;pixels+=size_t(actual.width)*actual.height;
    }
    CHECK(count==40 && pixels==19950);std::cout<<"REFERENCE wallpaper "<<count<<" conditions "<<pixels<<" RGB pixels\n";
}
}
TEST(wallpaper_all_forty_native_bmps_preserve_dimensions_pixels_palette_and_source_alpha) {
    Directory directory;reference_exports(directory.path);
}
TEST(wallpaper_current_frame_keeps_all_original_frames_alpha_timing_and_loop_data) {
    Directory directory;Image source;source.width=3;source.height=2;source.path=L"original.gif";source.format=L"GIF";
    source.file_loops=3;source.animation_compensate=false;source.frames.resize(2);
    source.frames[0].duration_ms=17;source.frames[0].pixels.assign(6,{9,13,17,255});
    source.frames[1].duration_ms=83;source.frames[1].pixels={{20,40,60,0},{255,0,0,128},{0,255,0,1},{19,23,29,255},{7,11,13,254},{31,37,41,64}};
    const auto first=source.frames[0].pixels,second=source.frames[1].pixels;
    const auto bytes=encode_wallpaper_bmp(source,1);const auto actual=decoded(bytes,directory.path/L"current.bmp");
    CHECK(actual.frames.size()==1 && actual.frames[0].pixels[0]==Pixel{128,128,128,255});
    CHECK(actual.frames[0].pixels[1]==Pixel{191,63,63,255} && actual.frames[0].pixels[2]==Pixel{127,128,127,255});
    CHECK(actual.frames[0].pixels[3]==Pixel{19,23,29,255} && bits(bytes)==24);
    CHECK(source.frames.size()==2 && source.frames[0].pixels==first && source.frames[1].pixels==second);
    CHECK(source.frames[0].duration_ms==17 && source.frames[1].duration_ms==83 && source.file_loops==3 && !source.animation_compensate);
    CHECK(source.path==L"original.gif" && source.format==L"GIF");
}
TEST(wallpaper_invalid_frame_dimensions_and_matte_fail_before_encoding) {
    Image empty;THROWS(encode_wallpaper_bmp(empty,0));
    Image source;source.width=2;source.height=1;source.frames.resize(1);source.frames[0].pixels={{1,2,3,0},{4,5,6,255}};
    const auto original=source.frames[0].pixels;
    THROWS(encode_wallpaper_bmp(source,1));THROWS(encode_wallpaper_bmp(source,0,0x1000000));
    source.frames[0].pixels.pop_back();THROWS(encode_wallpaper_bmp(source,0));
    source.frames[0].pixels=original;source.width=0;THROWS(encode_wallpaper_bmp(source,0));
    CHECK(source.frames[0].pixels==original);
}
int main(int argc,char **argv) {
    const auto status=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if (FAILED(status)) return 2;
    int result=1;
    try {
        if (argc==3 && std::string(argv[1])=="--oracle") {
            const auto directory=std::filesystem::absolute(argv[2]);CHECK(std::filesystem::create_directory(directory));
            reference_exports(directory);result=0;
        } else {CHECK(argc==1);result=run_tests();}
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';}
    CoUninitialize();return result;
}
