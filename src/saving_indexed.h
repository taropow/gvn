#pragma once
#include "gvn/saving.h"

namespace gvn::detail {
// The caller obtains the checked source once through image_indices().
IndexedRaster index_image(const Image &image, size_t frame, unsigned bits, uint32_t matte,
                          bool dither, const std::shared_ptr<const IndexedPixels> &source);
}
