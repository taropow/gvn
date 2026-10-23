#pragma once
#include "gvn/codecs.h"
namespace gvn {
// Unsupported container layouts fall back to the existing whole-animation decoder.
std::optional<Image> decode_apng_framewise(std::span<const uint8_t>, size_t, const LoadingOptions &);
}
