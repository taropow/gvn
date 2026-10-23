#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace gvn::detail {
// Equivalent to clamp(lround(value), 0, maximum), for finite filter results.
// Compare the fraction instead of adding .5: the latter rounds nextafter(.5,0)
// incorrectly. Clamping first also keeps the integer conversion in byte range.
inline uint8_t display_round(double value, int maximum) {
    value = std::clamp(value, 0.0, double(maximum));
    const auto whole = static_cast<int>(value);
    return static_cast<uint8_t>(whole + (value - whole >= .5));
}

class DisplaySum {
#if defined(_M_X64) || defined(__SSE2__)
    __m128d low = _mm_setzero_pd(), high = _mm_setzero_pd();
#else
    std::array<double, 4> sum{};
#endif
  public:
    void add(const uint8_t *pixel, double weight) {
#if defined(_M_X64) || defined(__SSE2__)
        uint32_t word; std::memcpy(&word, pixel, sizeof(word));
        const auto zero = _mm_setzero_si128();
        const auto bytes = _mm_cvtsi32_si128(static_cast<int>(word));
        const auto words = _mm_unpacklo_epi8(bytes, zero);
        const auto integers = _mm_unpacklo_epi16(words, zero);
        const auto factor = _mm_set1_pd(weight);
        low = _mm_add_pd(low, _mm_mul_pd(_mm_cvtepi32_pd(integers), factor));
        high = _mm_add_pd(high, _mm_mul_pd(_mm_cvtepi32_pd(_mm_srli_si128(integers, 8)), factor));
#else
        for (size_t c = 0; c < 4; ++c) sum[c] += pixel[c] * weight;
#endif
    }
    void add(const double *pixel, double weight) {
#if defined(_M_X64) || defined(__SSE2__)
        const auto factor = _mm_set1_pd(weight);
        low = _mm_add_pd(low, _mm_mul_pd(_mm_loadu_pd(pixel), factor));
        high = _mm_add_pd(high, _mm_mul_pd(_mm_loadu_pd(pixel + 2), factor));
#else
        for (size_t c = 0; c < 4; ++c) sum[c] += pixel[c] * weight;
#endif
    }
    std::array<double, 4> value() const {
#if defined(_M_X64) || defined(__SSE2__)
        std::array<double, 4> result;
        _mm_storeu_pd(result.data(), low); _mm_storeu_pd(result.data() + 2, high);
        return result;
#else
        return sum;
#endif
    }
};
// Each channel keeps the original tap order and double precision. No FMA,
// fast-math or change to the inter-pass clipping/rounding is used.
inline void add_display_row(double *sum, const uint8_t *pixels, size_t channels, double weight) {
    size_t at = 0;
#if defined(_M_X64) || defined(__SSE2__)
    const auto zero = _mm_setzero_si128(); const auto factor = _mm_set1_pd(weight);
    for (; at + 4 <= channels; at += 4) {
        uint32_t word; std::memcpy(&word, pixels + at, sizeof(word));
        const auto integers = _mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(static_cast<int>(word)), zero), zero);
        _mm_storeu_pd(sum + at, _mm_add_pd(_mm_loadu_pd(sum + at), _mm_mul_pd(_mm_cvtepi32_pd(integers), factor)));
        _mm_storeu_pd(sum + at + 2, _mm_add_pd(_mm_loadu_pd(sum + at + 2), _mm_mul_pd(_mm_cvtepi32_pd(_mm_srli_si128(integers, 8)), factor)));
    }
#endif
    for (; at < channels; ++at) sum[at] += pixels[at] * weight;
}
inline void expand_display_row(const uint8_t *pixels, double *output, size_t channels) {
    size_t at = 0;
#if defined(_M_X64) || defined(__SSE2__)
    const auto zero = _mm_setzero_si128();
    for (; at + 4 <= channels; at += 4) {
        uint32_t word; std::memcpy(&word, pixels + at, sizeof(word));
        const auto integers = _mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(static_cast<int>(word)), zero), zero);
        _mm_storeu_pd(output + at, _mm_cvtepi32_pd(integers));
        _mm_storeu_pd(output + at + 2, _mm_cvtepi32_pd(_mm_srli_si128(integers, 8)));
    }
#endif
    for (; at < channels; ++at) output[at] = pixels[at];
}
// Averaging has unit weights. A column sum is at most 65535*255, and the
// final rectangle uses uint64_t; no floating-point rounding is involved.
inline void add_average_row(uint32_t *sum, const uint8_t *pixels, size_t channels) {
    size_t at = 0;
#if defined(_M_X64) || defined(__SSE2__)
    const auto zero = _mm_setzero_si128();
    const auto add = [&](size_t offset, __m128i values) {
        _mm_storeu_si128(reinterpret_cast<__m128i *>(sum + offset),
            _mm_add_epi32(_mm_loadu_si128(reinterpret_cast<const __m128i *>(sum + offset)), values));
    };
    for (; at + 16 <= channels; at += 16) {
        const auto bytes = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pixels + at));
        const auto low = _mm_unpacklo_epi8(bytes, zero), high = _mm_unpackhi_epi8(bytes, zero);
        add(at, _mm_unpacklo_epi16(low, zero)); add(at + 4, _mm_unpackhi_epi16(low, zero));
        add(at + 8, _mm_unpacklo_epi16(high, zero)); add(at + 12, _mm_unpackhi_epi16(high, zero));
    }
#endif
    for (; at < channels; ++at) sum[at] += pixels[at];
}
} // namespace gvn::detail
