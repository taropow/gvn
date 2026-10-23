#pragma once
#include <cstdint>
namespace gvn {
struct LoadingOptions {
    bool preserve_alpha{true}, bmp_transparency{true}, gif_transparency{true}, png_transparency{true},
        gif_background{}, png_background{}, gif_animation{true}, exif_orientation{true};
    uint32_t background{0xffffff};
    // MassiGra 0.45: unrestricted, IE, Firefox, Opera, Chrome.
    int gif_speed{};
    // Display source loading applies this after decoding; raw clipboard decode stays independent.
    int view_rotation{};
    // Raw decode/clipboard keeps source precision. The viewer passes its ON default explicitly.
    bool bitfield_to24{};
    bool operator==(const LoadingOptions &) const = default;
    uint32_t flags() const {
        return static_cast<uint32_t>(preserve_alpha) | (bmp_transparency << 1) | (gif_transparency << 2) |
               (png_transparency << 3) | (gif_background << 4) | (png_background << 5) |
               (gif_animation << 6) | (exif_orientation << 7) | (static_cast<uint32_t>(gif_speed) << 8) |
               (static_cast<uint32_t>(view_rotation) << 11) | (static_cast<uint32_t>(bitfield_to24) << 15);
    }
};
} // namespace gvn
