#pragma once
#include <windows.h>
#include "gvn/wallpaper.h"
namespace gvn::detail {
// Tests use an owned volatile registry key and an SPI spy; no user desktop changes.
WallpaperPlatform wallpaper_platform_for_registry(HKEY root,std::wstring subkey,
    std::function<BOOL(UINT,UINT,PVOID,UINT)> system,std::wstring mutex_name);
}
