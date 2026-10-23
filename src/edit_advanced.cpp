#include "gvn/edit.h"
#include <cmath>
#include <array>
namespace gvn {
namespace {
uint8_t byte(double value) {
    return static_cast<uint8_t>(std::clamp(std::lround(value), 0l, 255l));
}
Rect area(const Image &image, Rect rect) {
    if (rect.empty())
        rect = {0, 0, static_cast<int>(image.width), static_cast<int>(image.height)};
    return intersect(rect, {0, 0, static_cast<int>(image.width), static_cast<int>(image.height)});
}
Pixel sample(const Image &image, double x, double y) {
    const auto &pixels = image.frames[0].pixels;
    const int x0 = static_cast<int>(floor(x)), y0 = static_cast<int>(floor(y));
    const double dx = x - x0, dy = y - y0;
    double r{}, g{}, b{}, a{};
    for (int yy = 0; yy < 2; ++yy)
        for (int xx = 0; xx < 2; ++xx) {
            const int sx = x0 + xx, sy = y0 + yy;
            if (sx < 0 || sy < 0 || sx >= static_cast<int>(image.width) ||
                sy >= static_cast<int>(image.height))
                continue;
            const auto p = pixels[static_cast<size_t>(sy) * image.width + sx];
            const double weight = (xx ? dx : 1 - dx) * (yy ? dy : 1 - dy) * p.a;
            r += weight * p.r;
            g += weight * p.g;
            b += weight * p.b;
            a += weight;
        }
    return a ? Pixel{byte(r / a), byte(g / a), byte(b / a), byte(a)} : Pixel{};
}
} // namespace
Image rotate_image(const Image &image, double degrees, const EditMemory &memory) {
    if (!std::isfinite(degrees))
        throw std::runtime_error("Invalid rotation");
    degrees = std::remainder(degrees, 360.0);
    const auto quarter = std::round(degrees / 90);
    if (std::abs(degrees / 90 - quarter) < 1e-8)
        return rotate_quarters(image, static_cast<int>(quarter) % 4);
    const double angle = degrees * 3.141592653589793 / 180, c = cos(angle), s = sin(angle);
    Image out;
    out.width = static_cast<uint32_t>(ceil(std::abs(c) * image.width + std::abs(s) * image.height));
    out.height = static_cast<uint32_t>(ceil(std::abs(s) * image.width + std::abs(c) * image.height));
    out.path = image.path;
    out.format = L"RGBA";
    out.metadata=image.metadata;
    out.rgb16 = image.rgb16;
    out.frames.push_back({memory.allocate(out.width, out.height), 100});
    for (uint32_t y = 0; y < out.height; ++y)
        for (uint32_t x = 0; x < out.width; ++x) {
            const double xx = x - (out.width - 1) / 2.0, yy = y - (out.height - 1) / 2.0;
            out.frames[0].pixels[static_cast<size_t>(y) * out.width + x] =
                sample(image, c * xx + s * yy + (image.width - 1) / 2.0,
                       -s * xx + c * yy + (image.height - 1) / 2.0);
        }
    return out;
}
Image levels(const Image &image, double black, double white, double gamma, double out_black, double out_white,
             int channel, Rect rect) {
    if (!std::isfinite(black) || !std::isfinite(white) || !std::isfinite(out_black) ||
        !std::isfinite(out_white) || black < 0 || white > 255 || black >= white || gamma <= 0 ||
        !std::isfinite(gamma) || out_black < 0 || out_white > 255 || out_white < 0 || out_black > 255 ||
        channel < -1 || channel > 2)
        throw std::runtime_error("Invalid levels parameters");
    auto out = still_frame(image, 0, false);
    rect = area(image, rect);
    std::array<uint8_t, 256> table;
    for (int i = 0; i < 256; ++i)
        table[i] =
            byte(out_black + (out_white - out_black) *
                                 std::pow(std::clamp((i - black) / (white - black), 0.0, 1.0), 1 / gamma));
    for (int y = rect.y; y < rect.y + rect.h; ++y)
        for (int x = rect.x; x < rect.x + rect.w; ++x) {
            auto &p = out.frames[0].pixels[static_cast<size_t>(y) * out.width + x];
            if (channel < 0 || channel == 0)
                p.r = table[p.r];
            if (channel < 0 || channel == 1)
                p.g = table[p.g];
            if (channel < 0 || channel == 2)
                p.b = table[p.b];
        }
    return out;
}
Image auto_levels(const Image &image, bool per_channel, Rect rect) {
    rect = area(image, rect);
    int low[3]{255, 255, 255}, high[3]{};
    for (int y = rect.y; y < rect.y + rect.h; ++y)
        for (int x = rect.x; x < rect.x + rect.w; ++x) {
            const auto p = image.frames[0].pixels[static_cast<size_t>(y) * image.width + x];
            if (!p.a)
                continue;
            const uint8_t rgb[]{p.r, p.g, p.b};
            for (int i = 0; i < 3; ++i) {
                low[i] = std::min(low[i], static_cast<int>(rgb[i]));
                high[i] = std::max(high[i], static_cast<int>(rgb[i]));
            }
        }
    auto out = still_frame(image, 0, false);
    if (!per_channel) {
        const int black = *std::min_element(low, low + 3), white = *std::max_element(high, high + 3);
        if (black < white)
            out = levels(out, black, white, 1, 0, 255, -1, rect);
    } else
        for (int i = 0; i < 3; ++i)
            if (low[i] < high[i])
                out = levels(out, low[i], high[i], 1, 0, 255, i, rect);
    return out;
}
Image gaussian(const Image &image, double sigma, double mix, Rect rect) {
    if (!std::isfinite(sigma) || !std::isfinite(mix) || sigma <= 0 || sigma > 32 || mix < 0 || mix > 1)
        throw std::runtime_error("Invalid blur parameters");
    const int radius = std::max(1, static_cast<int>(ceil(sigma * 3)));
    std::vector<double> kernel(radius * 2 + 1);
    double sum{};
    for (int i = -radius; i <= radius; ++i) {
        const double weight = exp(-(i * i) / (2 * sigma * sigma));
        kernel[i + radius] = weight;
        sum += weight;
    }
    for (auto &k : kernel)
        k /= sum;
    auto horizontal = still_frame(image, 0, false), blurred = still_frame(image, 0, false);
    const auto &src = image.frames[0].pixels;
    // Both passes filter premultiplied colors; hidden RGB never creates a colored fringe.
    auto pass = [&](const std::vector<Pixel> &input, std::vector<Pixel> &output, bool vertical) {
        for (uint32_t y = 0; y < image.height; ++y)
            for (uint32_t x = 0; x < image.width; ++x) {
                double r{}, g{}, b{}, a{};
                for (int i = -radius; i <= radius; ++i) {
                    const int xx = std::clamp(static_cast<int>(x) + (vertical ? 0 : i), 0,
                                              static_cast<int>(image.width) - 1),
                              yy = std::clamp(static_cast<int>(y) + (vertical ? i : 0), 0,
                                              static_cast<int>(image.height) - 1);
                    const auto p = input[static_cast<size_t>(yy) * image.width + xx];
                    const auto weight = kernel[i + radius] * p.a;
                    r += p.r * weight;
                    g += p.g * weight;
                    b += p.b * weight;
                    a += weight;
                }
                output[static_cast<size_t>(y) * image.width + x] =
                    a ? Pixel{byte(r / a), byte(g / a), byte(b / a), byte(a)} : Pixel{};
            }
    };
    pass(src, horizontal.frames[0].pixels, false);
    pass(horizontal.frames[0].pixels, blurred.frames[0].pixels, true);
    auto out = still_frame(image, 0, false);
    rect = area(image, rect);
    for (int y = rect.y; y < rect.y + rect.h; ++y)
        for (int x = rect.x; x < rect.x + rect.w; ++x) {
            const size_t i = static_cast<size_t>(y) * image.width + x;
            auto p = blurred.frames[0].pixels[i], q = src[i];
            const double a = p.a * mix + q.a * (1 - mix);
            out.frames[0].pixels[i] = a ? Pixel{byte((p.r * p.a * mix + q.r * q.a * (1 - mix)) / a),
                                                byte((p.g * p.a * mix + q.g * q.a * (1 - mix)) / a),
                                                byte((p.b * p.a * mix + q.b * q.a * (1 - mix)) / a), byte(a)}
                                        : Pixel{};
        }
    return out;
}
Image unsharp(const Image &image, double sigma, double strength, double threshold, Rect rect) {
    const auto blurred = gaussian(image, sigma);
    auto out = still_frame(image, 0, false);
    rect = area(image, rect);
    for (int y = rect.y; y < rect.y + rect.h; ++y)
        for (int x = rect.x; x < rect.x + rect.w; ++x) {
            const size_t i = static_cast<size_t>(y) * image.width + x;
            auto &p = out.frames[0].pixels[i];
            const auto q = blurred.frames[0].pixels[i];
            auto adjust = [&](uint8_t value, uint8_t soft) {
                const int delta = value - soft;
                return std::abs(delta) < threshold ? value : byte(value + delta * strength);
            };
            p = {adjust(p.r, q.r), adjust(p.g, q.g), adjust(p.b, q.b), p.a};
        }
    return out;
}
Image swap_channels(const Image &image, int mode, Rect rect) {
    MaskOptions options; options.swap = mode;
    return mask_image(image, Mask::Swap, options, rect);
}
Image glass_mask(const Image &image, int block, bool vertical, Rect rect, bool remainder) {
    MaskOptions options; options.glass_size = block; options.glass_remainder = remainder;
    return mask_image(image, vertical ? Mask::Vertical : Mask::Horizontal, options, rect);
}
Image xor_mask(const Image &image, Rect rect, unsigned channels) {
    MaskOptions options; options.xor_channels = channels;
    return mask_image(image, Mask::Xor, options, rect);
}
Image win_mask(const Image &image, Rect rect) {
    return mask_image(image, Mask::Win, {}, rect);
}
Image replace_color(const Image &image, Pixel from, Pixel to, Rect rect) {
    auto out = still_frame(image, 0, false);
    rect = area(image, rect);
    for (int y = rect.y; y < rect.y + rect.h; ++y)
        for (int x = rect.x; x < rect.x + rect.w; ++x) {
            auto &p = out.frames[0].pixels[static_cast<size_t>(y) * image.width + x];
            if (p.r == from.r && p.g == from.g && p.b == from.b)
                p = {to.r, to.g, to.b, p.a};
        }
    return out;
}
} // namespace gvn
