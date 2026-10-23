#pragma once
#include "core.h"
#include <string_view>

namespace gvn {
enum class Mask { Vertical, Horizontal, Negative, Xor, Swap, Q0, Win, FL, MekoForward, MekoReverse, Rainbow };
struct MaskOptions {
    int glass_size{8}, swap{3};
    unsigned negative_channels{7}, xor_channels{7}, q0_flags{7};
    bool glass_remainder{};
    bool fl_negative{true};
    int rainbow_size{8};
    bool operator==(const MaskOptions &) const = default;
};
Image mask_image(const Image &, Mask, const MaskOptions & = {}, Rect selection = {});
Image rainbow_image(const Image &, int block_size, Rect selection = {}, size_t frame = 0);
inline std::wstring normalize_cp_code(std::wstring_view input) {
    std::wstring result;
    for(auto c:input) {
        if(c>=L'a'&&c<=L'z')c-=L'a'-L'A';
        if(c>=L'A'&&c<=L'Z') {result.push_back(c);if(result.size()==16)break;}
    }
    return result;
}
Image cp_image(const Image &, std::wstring_view code, Rect selection = {}, size_t frame = 0);
} // namespace gvn
