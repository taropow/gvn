#pragma once
#include "masks.h"
#include "core.h"
#include <functional>
namespace gvn {
struct EditMemory {
    uint64_t physical_bytes{UINT64_MAX};
    bool allow_physical_overflow{};
    // Optional allocation observer, also used to inject a failure without exhausting the machine.
    std::function<void(size_t)> before_allocate;
    size_t pixels(uint64_t width, uint64_t height) const;
    std::vector<Pixel> allocate(uint64_t width, uint64_t height) const;
};
uint64_t physical_memory_bytes();
Image new_image(uint32_t width, uint32_t height, const EditMemory &memory = {});
Image still_frame(const Image &image, size_t frame, bool retain_indexed = true);
Image crop(const Image &image, Rect rectangle);
Image rotate_quarters(const Image &image, int quarters);
Image flip(const Image &image, bool horizontal);
Image resize_image(const Image &image, uint32_t width, uint32_t height, bool smooth = true,
                   const EditMemory &memory = {});
struct ResizeFilters {
    int down{1}, up{1};
    bool sharpen{};
};
Image resize_image(const Image &image, uint32_t width, uint32_t height, const ResizeFilters &filters,
                   const EditMemory &memory = {});
Image canvas_image(const Image &image, uint32_t width, uint32_t height, const EditMemory &memory = {});
struct CanvasOptions {
    int anchor{4}; // row-major3x3: top-left0, center4, bottom-right8.
    Pixel background{};
};
Image canvas_image(const Image &image, uint32_t width, uint32_t height, const CanvasOptions &,
                   const EditMemory &memory = {});
Image join_pages(const Image &first, const Image &second, bool right_to_left);
enum class Effect {
    Negative,
    Grayscale,
    Sepia,
    Threshold,
    Brightness,
    Contrast,
    Gamma,
    Saturation,
    Hue,
    Blur,
    Sharpen,
    Edge,
    Emboss,
    Mosaic,
    Noise,
    Posterize
};
Image effect(const Image &image, Effect type, double amount = 1, Rect selection = {});
Image rotate_image(const Image &image, double clockwise_degrees, const EditMemory &memory = {});
Image levels(const Image &image, double black, double white, double gamma, double output_black = 0,
             double output_white = 255, int channel = -1, Rect selection = {});
Image auto_levels(const Image &image, bool per_channel, Rect selection = {});
Image gaussian(const Image &image, double sigma, double mix = 1, Rect selection = {});
Image unsharp(const Image &image, double sigma, double strength, double threshold, Rect selection = {});
Image swap_channels(const Image &image, int mode, Rect selection = {});
Image glass_mask(const Image &image, int width, bool vertical, Rect selection = {}, bool remainder = false);
Image xor_mask(const Image &image, Rect selection = {}, unsigned channels = 7);
Image win_mask(const Image &image, Rect selection = {});
Image replace_color(const Image &image, Pixel from, Pixel to, Rect selection = {});
// Edit table entries, including unused/duplicate colors. Preserve indices and alpha.
// Reject stale provenance instead of replacing newer current pixels.
Image edit_palette(const Image &image, const std::vector<Pixel> &palette,
                   const EditMemory &memory = {});
Image paste_region(const Image &image, const Image &overlay, Rect selection);
} // namespace gvn
