#pragma once
#include "gvn/renderer.h"
#include "gvn/codecs.h"
#include "test.h"
#include <array>
#include <fstream>
#include <sstream>
namespace gvn_test {
struct BackgroundCase {
    int palette{}, preset{};
    bool tiled{};
    uint32_t first{}, second{};
    std::string expected;
};
inline const auto background_root = std::filesystem::path(GVN_FIXTURES) / L"window-background";
inline std::vector<BackgroundCase> background_cases() {
    std::ifstream stream(background_root / L"cases.tsv");CHECK(stream);
    std::vector<BackgroundCase> result;
    for (std::string line; std::getline(stream, line);) {
        std::istringstream row(line);std::array<std::string,6> fields;
        for (auto &field : fields) CHECK(std::getline(row,field,'\t'));
        result.push_back({std::stoi(fields[0]),std::stoi(fields[1]),std::stoi(fields[2])!=0,
                          static_cast<uint32_t>(std::stoul(fields[3])),static_cast<uint32_t>(std::stoul(fields[4])),fields[5]});
    }
    CHECK(result.size()==20);return result;
}
struct BackgroundSurface {
    HWND window{};HDC dc{};HBITMAP bitmap{};HGDIOBJ previous{};void *pixels{};
    int width{},height{};
    BackgroundSurface(int w,int h) : width(w),height(h) {
        window=CreateWindowExW(0,L"STATIC",L"GVN background offscreen",WS_POPUP,0,0,w,h,nullptr,nullptr,nullptr,nullptr);CHECK(window);
        dc=CreateCompatibleDC(nullptr);CHECK(dc);
        BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);CHECK(bitmap && pixels);
        previous=SelectObject(dc,bitmap);CHECK(previous);
    }
    ~BackgroundSurface(){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);DestroyWindow(window);}
    uint32_t color(int x,int y) const {return static_cast<const uint32_t *>(pixels)[y*width+x]&0xffffff;}
};
template<class T> void set_view_background(T &r,bool tiled,int preset,uint32_t first,uint32_t second) {
    r.background=first;
    // Baseline compatibility configures the old solid exterior. The same test
    // body selects the new independent viewport policy when that API is added.
    if constexpr (requires(T &value) {value.background_tiled;value.background_pattern;value.background_first;value.background_second;}) {
        r.background_tiled=tiled;r.background_pattern=preset;r.background_first=first;r.background_second=second;
    }
}
template<class T> void reset_pattern_resources(T &r) {
    if constexpr (requires(T &value) {value.reset_patterns();}) r.reset_patterns();
    else r.reset_background();
}
}
