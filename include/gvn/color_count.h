#pragma once
#include "core.h"
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
namespace gvn {
struct ColorCountMemory {
    uint64_t bytes{UINT64_MAX};
    std::function<void(size_t)> before_allocate;
};
// Count displayed premultiplied RGBA colors without changing the original straight RGBA.
// Null means cancelled. Opaque images use a2MiB bitmap; partial-alpha keys are sorted in chunks.
std::optional<uint64_t> count_image_colors(std::span<const Pixel>,std::stop_token = {},const ColorCountMemory & = {});
}
