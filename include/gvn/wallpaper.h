#pragma once
#include "core.h"
#include <functional>
#include <optional>
#include <utility>

namespace gvn {
enum class WallpaperMode { Center, Tile, Stretch, Width, Height };
struct WallpaperStyle {
    std::wstring tile, style;
    bool operator==(const WallpaperStyle &) const = default;
};
// Native MassiGra labels map to Windows' placement values, without image resampling.
WallpaperStyle wallpaper_style(WallpaperMode mode);
// Produce one complete frame; never alter source RGBA, indices, other frames or metadata.
// Opaque indexed sources keep their palette. Alpha is flattened only in the output BMP.
std::vector<uint8_t> encode_wallpaper_bmp(const Image &image, size_t frame,
                                          uint32_t matte = 0x808080);
struct WallpaperValue {
    uint32_t type{};
    std::vector<uint8_t> bytes;
    bool operator==(const WallpaperValue &) const = default;
};
struct WallpaperValues {
    std::optional<WallpaperValue> tile, style;
    bool operator==(const WallpaperValues &) const = default;
};
struct WallpaperSnapshot {
    WallpaperValues values;
    std::filesystem::path image;
    bool operator==(const WallpaperSnapshot &) const = default;
};
WallpaperValues wallpaper_values(WallpaperMode mode);
struct WallpaperPlatform {
    std::function<WallpaperSnapshot()> read;
    std::function<void(const WallpaperValues &)> write;
    std::function<void(const std::filesystem::path &)> apply;
    std::wstring mutex_name;
    uint32_t wait_ms{5000};
};
WallpaperPlatform native_wallpaper_platform();
struct WallpaperResult {
    std::filesystem::path file;
    size_t removed{};
    std::vector<std::filesystem::path> retained;
    // Retaining the current input is intentional; only failed cleanup requires a notice.
    bool cleanup_incomplete{};
};
class WallpaperRecoveryError : public std::runtime_error {
public:
    std::vector<std::filesystem::path> recovery_files;
    WallpaperRecoveryError(std::string message,std::vector<std::filesystem::path> files)
        : std::runtime_error(std::move(message)),recovery_files(std::move(files)) {}
};
std::filesystem::path wallpaper_directory(const std::filesystem::path &config);
std::vector<std::filesystem::path> managed_wallpaper_files(const std::filesystem::path &config);
bool has_wallpaper_files(const std::filesystem::path &config) noexcept;
WallpaperResult set_wallpaper_image(const Image &image,size_t frame,const std::filesystem::path &config,
                                    WallpaperMode mode,const WallpaperPlatform &platform);
WallpaperResult delete_wallpaper_images(const std::filesystem::path &config,
                                        const WallpaperPlatform &platform);
} // namespace gvn
