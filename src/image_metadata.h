#pragma once
#include "gvn/core.h"
#include <span>
namespace gvn {
// Inspect container metadata without decoding pixels or copying source bytes.
void read_container_metadata(Image &,std::span<const uint8_t>);
// Expand encoded BMP component codes with round(code*255/max), retaining alpha.
bool normalize_bmp_bitfields(Image &,std::span<const uint8_t>);
bool set_image_resolution(ImageMetadata &,double,double);
struct Bmp16RetentionTrace {
    const char *reason{};
    uint32_t x{}, y{}, packed{}, code{}, previous_inverse{};
    int previous_forward{};
    Pixel pixel{};
};
void retain_bmp16_colors(Image &, std::span<const uint8_t>, size_t limit, Rgb16Layout decoder_layout,
                         Bmp16RetentionTrace *trace = nullptr);
}
