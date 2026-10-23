#pragma once
#include "saving.h"
#include <windows.h>

namespace gvn {
namespace save_control {
enum : DWORD {
    Common = 30000,
    KeepTimes,
    Uppercase,
    Dither,
    Error,
    Png = 30100,
    PngInterlaced,
    PngCompression,
    PngFilter,
    PngTransparency,
    PngColor,
    PngMatte,
    Jpeg = 30200,
    JpegProgressive,
    JpegQuality,
    JpegMatte,
    Bmp = 30300,
    BmpTopDown,
    BmpRle,
    BmpBits,
    BmpMatte,
    Gif = 30400,
    GifInterlaced,
    GifUncompressed,
    GifTransparency,
    GifIndex,
    GifMatte
};
}
// Native common save dialog plus per-format controls. Cancel leaves options unchanged.
std::filesystem::path choose_save_file(HWND owner, const std::filesystem::path &folder,
                                       const std::wstring &suggested, SaveOptions &options);
} // namespace gvn
