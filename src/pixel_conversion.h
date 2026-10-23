#pragma once
#include <cstddef>
#include <cstdint>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace gvn::detail {
inline void premultiply_rgba(const uint8_t *source, uint8_t *output, size_t count) {
    size_t i{};
#if defined(_M_X64) || defined(__SSE2__)
    const auto zero = _mm_setzero_si128();
    const auto alpha_mask = _mm_set1_epi32(static_cast<int>(0xff000000u));
    const auto rounding = _mm_set1_epi16(128);
    const auto multiply = [&](const __m128i colors) {
        auto alpha = _mm_shufflelo_epi16(colors, _MM_SHUFFLE(3, 3, 3, 3));
        alpha = _mm_shufflehi_epi16(alpha, _MM_SHUFFLE(3, 3, 3, 3));
        auto product = _mm_add_epi16(_mm_mullo_epi16(colors, alpha), rounding);
        // Exact rounded division by 255 for products 0..65025.
        product = _mm_add_epi16(product, _mm_srli_epi16(product, 8));
        return _mm_srli_epi16(product, 8);
    };
    for (; i + 4 <= count; i += 4) {
        const auto input = _mm_loadu_si128(reinterpret_cast<const __m128i *>(source + i * 4));
        const auto alpha = _mm_and_si128(input, alpha_mask);
        if (_mm_movemask_epi8(_mm_cmpeq_epi32(alpha, zero)) == 0xffff) {
            _mm_storeu_si128(reinterpret_cast<__m128i *>(output + i * 4), zero);
            continue;
        }
        auto low = _mm_unpacklo_epi8(input, zero), high = _mm_unpackhi_epi8(input, zero);
        low = _mm_shufflelo_epi16(low, _MM_SHUFFLE(3, 0, 1, 2));
        low = _mm_shufflehi_epi16(low, _MM_SHUFFLE(3, 0, 1, 2));
        high = _mm_shufflelo_epi16(high, _MM_SHUFFLE(3, 0, 1, 2));
        high = _mm_shufflehi_epi16(high, _MM_SHUFFLE(3, 0, 1, 2));
        __m128i result;
        if (_mm_movemask_epi8(_mm_cmpeq_epi32(alpha, alpha_mask)) == 0xffff)
            result = _mm_packus_epi16(low, high);
        else {
            result = _mm_packus_epi16(multiply(low), multiply(high));
            result = _mm_or_si128(_mm_andnot_si128(alpha_mask, result), alpha);
        }
        _mm_storeu_si128(reinterpret_cast<__m128i *>(output + i * 4), result);
    }
#endif
    for (; i < count; ++i) {
        const auto p = source + i * 4;
        const auto r = p[0], g = p[1], b = p[2], a = p[3];
        output[i * 4] = static_cast<uint8_t>((b * a + 127) / 255);
        output[i * 4 + 1] = static_cast<uint8_t>((g * a + 127) / 255);
        output[i * 4 + 2] = static_cast<uint8_t>((r * a + 127) / 255);
        output[i * 4 + 3] = a;
    }
}
} // namespace gvn::detail
