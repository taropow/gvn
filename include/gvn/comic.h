#pragma once
#include "core.h"

namespace gvn {
struct ComicOptions {
    // Reference modes: 0 centered opaque padding, 1 shrink, 2 enlarge.
    // Old GVN: 3 resize second to first, 4 top-aligned transparent padding.
    int mode{}, down{2}, up{1};
    uint32_t fill{};
    bool rotate_before{}, single_wide{true};
    bool operator==(const ComicOptions &) const = default;
    std::wstring key() const;
};
// Immutable still images; writes the final joined image without full resized intermediates.
Image compose_comic_pages(const Image &, const Image &, bool right_to_left, const ComicOptions &);
} // namespace gvn
