#pragma once
#include "core.h"

namespace gvn {
// None, left, right, portrait left/right, landscape left/right, half turn.
int view_rotation_quarters(int mode, uint32_t width, uint32_t height);
// An owned source image: retain every frame, straight RGBA, palette and metadata.
// Odd turns use one frame-sized scratch buffer; half turns reverse pixels in place.
void apply_view_rotation(Image &image, int mode);
} // namespace gvn
