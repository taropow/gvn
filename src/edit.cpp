#include "gvn/edit.h"
#include "gvn/resample.h"
#include <cmath>
#include <array>
#include <random>
namespace gvn {
Image paste_region(const Image &image, const Image &overlay, Rect rect) {
    auto out = still_frame(image, 0, false);
    rect = intersect(rect, {0, 0, static_cast<int>(image.width), static_cast<int>(image.height)});
    const int width = std::min(rect.w, static_cast<int>(overlay.width)),
              height = std::min(rect.h, static_cast<int>(overlay.height));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            auto &target = out.frames[0].pixels[static_cast<size_t>(rect.y + y) * image.width + rect.x + x];
            target = over(overlay.frames[0].pixels[static_cast<size_t>(y) * overlay.width + x], target);
        }
    return out;
}
static uint8_t byte(double value) {
    return static_cast<uint8_t>(std::clamp(std::lround(value), 0l, 255l));
}
static Image blank(const Image &source, uint32_t width, uint32_t height, const EditMemory &memory = {}) {
    Image out;
    out.width = width;
    out.height = height;
    out.frames.push_back({memory.allocate(width, height), 100});
    out.path = source.path;
    out.format = L"RGBA";
    out.metadata=source.metadata;
    out.rgb16 = source.rgb16;
    return out;
}
Image new_image(uint32_t width, uint32_t height, const EditMemory &memory) {
    return blank({}, width, height, memory);
}
template <class Map> static void transform_indices(const Image &source, Image &out, Map map) {
    const auto &original = source.frames.at(0);
    if (!original.indexed || original.indexed->width != source.width ||
        original.indexed->height != source.height || !original.indexed->matches(original.pixels))
        return;
    auto indexed = std::make_shared<IndexedPixels>();
    indexed->width = out.width;
    indexed->height = out.height;
    indexed->bits = original.indexed->bits;
    indexed->palette = original.indexed->palette;
    indexed->packed.resize(indexed->stride() * out.height);
    for (uint32_t y = 0; y < out.height; ++y)
        for (uint32_t x = 0; x < out.width; ++x) {
            const auto [sx, sy] = map(x, y);
            indexed->set_index(x, y, original.indexed->index(sx, sy));
        }
    out.frames[0].indexed = std::move(indexed);
}
Image still_frame(const Image &source, size_t frame, bool retain_indexed) {
    Image out = blank(source, source.width, source.height);
    out.frames[0].pixels = source.frames.at(frame).pixels;
    if (retain_indexed)
        out.frames[0].indexed = image_indices(source, frame);
    return out;
}
Image crop(const Image &image, Rect rect) {
    rect = intersect(rect, {0, 0, static_cast<int>(image.width), static_cast<int>(image.height)});
    if (rect.empty())
        throw std::runtime_error("Selection is empty");
    auto out = blank(image, rect.w, rect.h);
    for (int y = 0; y < rect.h; ++y)
        std::copy_n(image.frames.at(0).pixels.begin() + static_cast<size_t>(y + rect.y) * image.width +
                        rect.x,
                    rect.w, out.frames[0].pixels.begin() + static_cast<size_t>(y) * rect.w);
    transform_indices(image, out, [&](uint32_t x, uint32_t y) { return std::pair{x + rect.x, y + rect.y}; });
    return out;
}
Image rotate_quarters(const Image &image, int quarters) {
    quarters = (quarters % 4 + 4) % 4;
    auto out =
        blank(image, quarters % 2 ? image.height : image.width, quarters % 2 ? image.width : image.height);
    if(quarters%2)std::swap(out.metadata.dpi_x,out.metadata.dpi_y);
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x) {
            const uint32_t xx = quarters == 1   ? image.height - 1 - y
                                : quarters == 2 ? image.width - 1 - x
                                : quarters == 3 ? y
                                                : x;
            const uint32_t yy = quarters == 1   ? x
                                : quarters == 2 ? image.height - 1 - y
                                : quarters == 3 ? image.width - 1 - x
                                                : y;
            out.frames[0].pixels[static_cast<size_t>(yy) * out.width + xx] =
                image.frames[0].pixels[static_cast<size_t>(y) * image.width + x];
        }
    transform_indices(image, out, [&](uint32_t x, uint32_t y) {
        return quarters == 1   ? std::pair{y, image.height - 1 - x}
               : quarters == 2 ? std::pair{image.width - 1 - x, image.height - 1 - y}
               : quarters == 3 ? std::pair{image.width - 1 - y, x}
                               : std::pair{x, y};
    });
    return out;
}
Image flip(const Image &image, bool horizontal) {
    auto out = blank(image, image.width, image.height);
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x)
            out.frames[0].pixels[static_cast<size_t>(y) * image.width + x] =
                image.frames[0]
                    .pixels[static_cast<size_t>(horizontal ? y : image.height - 1 - y) * image.width +
                            (horizontal ? image.width - 1 - x : x)];
    transform_indices(image, out, [&](uint32_t x, uint32_t y) {
        return std::pair{horizontal ? image.width - 1 - x : x, horizontal ? y : image.height - 1 - y};
    });
    return out;
}
Image resize_image(const Image &image, uint32_t width, uint32_t height, bool smooth,
                   const EditMemory &memory) {
    auto out = blank(image, width, height, memory);
    const auto &src = image.frames[0].pixels;
    auto &dst = out.frames[0].pixels;
    if (width == image.width && height == image.height) {
        std::copy(src.begin(), src.end(), dst.begin());
        out.frames[0].indexed = image.frames[0].indexed;
        return out;
    }
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            const double fx = std::max(0.0, (x + 0.5) * image.width / width - 0.5),
                         fy = std::max(0.0, (y + 0.5) * image.height / height - 0.5);
            if (!smooth) {
                dst[static_cast<size_t>(y) * width + x] =
                    src[static_cast<size_t>(std::min(image.height - 1, static_cast<uint32_t>(fy + 0.5))) *
                            image.width +
                        std::min(image.width - 1, static_cast<uint32_t>(fx + 0.5))];
                continue;
            }
            const uint32_t x0 = std::min(image.width - 1, static_cast<uint32_t>(fx)),
                           y0 = std::min(image.height - 1, static_cast<uint32_t>(fy)),
                           x1 = std::min(image.width - 1, x0 + 1), y1 = std::min(image.height - 1, y0 + 1);
            const double dx = fx - x0, dy = fy - y0;
            const std::array<Pixel, 4> p{src[static_cast<size_t>(y0) * image.width + x0],
                                         src[static_cast<size_t>(y0) * image.width + x1],
                                         src[static_cast<size_t>(y1) * image.width + x0],
                                         src[static_cast<size_t>(y1) * image.width + x1]};
            const double weights[4]{(1 - dx) * (1 - dy), dx * (1 - dy), (1 - dx) * dy, dx * dy};
            double a{}, r{}, g{}, b{};
            for (int i = 0; i < 4; ++i) {
                const double w = weights[i] * p[i].a;
                a += w;
                r += w * p[i].r;
                g += w * p[i].g;
                b += w * p[i].b;
            }
            dst[static_cast<size_t>(y) * width + x] =
                a > 0 ? Pixel{byte(r / a), byte(g / a), byte(b / a), byte(a)} : Pixel{};
        }
    if (!smooth)
        transform_indices(image, out, [&](uint32_t x, uint32_t y) {
            const auto fx = std::max(0.0, (x + .5) * image.width / width - .5),
                       fy = std::max(0.0, (y + .5) * image.height / height - .5);
            return std::pair{std::min(image.width - 1, static_cast<uint32_t>(fx + .5)),
                             std::min(image.height - 1, static_cast<uint32_t>(fy + .5))};
        });
    return out;
}
Image resize_image(const Image &image, uint32_t width, uint32_t height, const ResizeFilters &filters,
                   const EditMemory &memory) {
    if (filters.down < DisplaySimple || filters.down > DisplayLanczos3 ||
        filters.up < DisplaySimple || filters.up > DisplayLanczos3 || image.frames.empty() ||
        image.frames[0].pixels.size() != checked_image_pixels(image.width,image.height))
        throw std::invalid_argument("Invalid resize filters or image");
    const int x = width == image.width ? DisplaySimple : width < image.width ? filters.down : filters.up;
    const int y = height == image.height ? DisplaySimple : height < image.height ? filters.down : filters.up;
    if (x == DisplaySimple && y == DisplaySimple)
        return resize_image(image,width,height,false,memory);
    auto out=blank(image,width,height,memory);
    resample_edit_rgba(image.frames[0].pixels,image.width,image.height,out.frames[0].pixels,width,height,x,y);
    if (filters.sharpen && filters.down != DisplaySimple && (width < image.width || height < image.height)) {
        // Mild, alpha-aware unsharp pass. Keep alpha and ignore hidden RGB in transparent neighbours.
        auto &pixels=out.frames[0].pixels;
        std::vector<Pixel> previous(pixels.begin(),pixels.begin()+width),current=previous,next;
        next.assign(pixels.begin()+size_t(std::min(1u,height-1))*width,pixels.begin()+size_t(std::min(1u,height-1)+1)*width);
        for(uint32_t row=0;row<height;++row) {
            for(uint32_t column=0;column<width;++column) {
                const auto p=current[column];if(!p.a)continue;
                double alpha{},red{},green{},blue{};
                const std::vector<Pixel> *rows[]{&previous,&current,&next};
                for(int dy=0;dy<3;++dy)for(int dx=-1;dx<=1;++dx) {
                    const auto at=static_cast<uint32_t>(std::clamp<int>(int(column)+dx,0,int(width)-1));
                    const auto q=(*rows[dy])[at];const double weight=(dy==1?2:1)*(dx==0?2:1)*q.a;
                    alpha+=weight;red+=q.r*weight;green+=q.g*weight;blue+=q.b*weight;
                }
                if(alpha>0)pixels[size_t(row)*width+column]={byte(p.r+(p.r-red/alpha)*.25),byte(p.g+(p.g-green/alpha)*.25),byte(p.b+(p.b-blue/alpha)*.25),p.a};
            }
            previous.swap(current);current.swap(next);
            const auto at=std::min(row+2,height-1);
            next.assign(pixels.begin()+size_t(at)*width,pixels.begin()+size_t(at+1)*width);
        }
    }
    return out;
}
Image canvas_image(const Image &image, uint32_t width, uint32_t height, const EditMemory &memory) {
    return canvas_image(image,width,height,CanvasOptions{},memory);
}
Image canvas_image(const Image &image,uint32_t width,uint32_t height,const CanvasOptions &options,const EditMemory &memory) {
    if(options.anchor<0||options.anchor>8||image.frames.empty()||
       image.frames[0].pixels.size()!=checked_image_pixels(image.width,image.height))
        throw std::invalid_argument("Invalid canvas image or anchor");
    auto out = blank(image, width, height, memory);
    if(width==image.width&&height==image.height){out.frames[0]=image.frames[0];return out;}
    std::fill(out.frames[0].pixels.begin(),out.frames[0].pixels.end(),options.background);
    const int dx = (static_cast<int>(width) - static_cast<int>(image.width)) * (options.anchor%3) / 2,
              dy = (static_cast<int>(height) - static_cast<int>(image.height)) * (options.anchor/3) / 2;
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x) {
            const int xx = static_cast<int>(x) + dx, yy = static_cast<int>(y) + dy;
            if (xx >= 0 && yy >= 0 && xx < static_cast<int>(width) && yy < static_cast<int>(height))
                out.frames[0].pixels[static_cast<size_t>(yy) * width + xx] =
                    image.frames[0].pixels[static_cast<size_t>(y) * image.width + x];
        }
    return out;
}
Image join_pages(const Image &first, const Image &second, bool rtl) {
    auto out = blank(first, first.width + second.width, std::max(first.height, second.height));
    const Image *pages[2]{rtl ? &second : &first, rtl ? &first : &second};
    uint32_t left{};
    for (auto page : pages) {
        for (uint32_t y = 0; y < page->height; ++y)
            std::copy_n(page->frames[0].pixels.begin() + static_cast<size_t>(y) * page->width, page->width,
                        out.frames[0].pixels.begin() + static_cast<size_t>(y) * out.width + left);
        left += page->width;
    }
    return out;
}
Image effect(const Image &image, Effect type, double amount, Rect selection) {
    auto out = still_frame(image, 0, false);
    const auto &src = image.frames[0].pixels;
    auto &dst = out.frames[0].pixels;
    if (selection.empty())
        selection = {0, 0, static_cast<int>(image.width), static_cast<int>(image.height)};
    selection = intersect(selection, {0, 0, static_cast<int>(image.width), static_cast<int>(image.height)});
    std::mt19937 random(0x47564e);
    if (type == Effect::Mosaic) {
        const int block = std::clamp(static_cast<int>(amount), 2, 256);
        for (int by = selection.y; by < selection.y + selection.h; by += block)
            for (int bx = selection.x; bx < selection.x + selection.w; bx += block) {
                const int right = std::min(bx + block, selection.x + selection.w),
                          bottom = std::min(by + block, selection.y + selection.h);
                double r{}, g{}, b{}, a{};
                size_t count{};
                for (int y = by; y < bottom; ++y)
                    for (int x = bx; x < right; ++x) {
                        const auto p = src[static_cast<size_t>(y) * image.width + x];
                        r += p.r * p.a;
                        g += p.g * p.a;
                        b += p.b * p.a;
                        a += p.a;
                        ++count;
                    }
                const Pixel value{byte(a ? r / a : 0), byte(a ? g / a : 0), byte(a ? b / a : 0),
                                  byte(a / count)};
                for (int y = by; y < bottom; ++y)
                    std::fill(dst.begin() + static_cast<size_t>(y) * image.width + bx,
                              dst.begin() + static_cast<size_t>(y) * image.width + right, value);
            }
        return out;
    }
    for (int y = selection.y; y < selection.y + selection.h; ++y)
        for (int x = selection.x; x < selection.x + selection.w; ++x) {
            const size_t index = static_cast<size_t>(y) * image.width + x;
            Pixel p = src[index];
            double r = p.r, g = p.g, b = p.b;
            const double gray = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            switch (type) {
            case Effect::Negative:
                r = 255 - r;
                g = 255 - g;
                b = 255 - b;
                break;
            case Effect::Grayscale:
                r = g = b = gray;
                break;
            case Effect::Sepia:
                r = p.r * .393 + p.g * .769 + p.b * .189;
                g = p.r * .349 + p.g * .686 + p.b * .168;
                b = p.r * .272 + p.g * .534 + p.b * .131;
                break;
            case Effect::Threshold:
                r = g = b = gray >= amount ? 255 : 0;
                break;
            case Effect::Brightness:
                r += amount;
                g += amount;
                b += amount;
                break;
            case Effect::Contrast:
                r = (r - 127.5) * amount + 127.5;
                g = (g - 127.5) * amount + 127.5;
                b = (b - 127.5) * amount + 127.5;
                break;
            case Effect::Gamma:
                r = 255 * std::pow(r / 255, 1 / std::max(.01, amount));
                g = 255 * std::pow(g / 255, 1 / std::max(.01, amount));
                b = 255 * std::pow(b / 255, 1 / std::max(.01, amount));
                break;
            case Effect::Saturation:
                r = gray + (r - gray) * amount;
                g = gray + (g - gray) * amount;
                b = gray + (b - gray) * amount;
                break;
            case Effect::Hue: {
                const double angle = amount * 3.141592653589793 / 180, c = cos(angle), s = sin(angle);
                r = (.213 + .787 * c - .213 * s) * p.r + (.715 - .715 * c - .715 * s) * p.g +
                    (.072 - .072 * c + .928 * s) * p.b;
                g = (.213 - .213 * c + .143 * s) * p.r + (.715 + .285 * c + .140 * s) * p.g +
                    (.072 - .072 * c - .283 * s) * p.b;
                b = (.213 - .213 * c - .787 * s) * p.r + (.715 - .715 * c + .715 * s) * p.g +
                    (.072 + .928 * c + .072 * s) * p.b;
                break;
            }
            case Effect::Mosaic:
                break;
            case Effect::Noise: {
                const double n = static_cast<int>(random() % 201) - 100;
                r += n * amount;
                g += n * amount;
                b += n * amount;
                break;
            }
            case Effect::Posterize: {
                const double levels = std::clamp(amount, 2.0, 256.0) - 1;
                r = round(r / 255 * levels) / levels * 255;
                g = round(g / 255 * levels) / levels * 255;
                b = round(b / 255 * levels) / levels * 255;
                break;
            }
            default: {
                const double blur[9]{1, 2, 1, 2, 4, 2, 1, 2, 1}, sharp[9]{0, -1, 0, -1, 5, -1, 0, -1, 0},
                    edge[9]{-1, -1, -1, -1, 8, -1, -1, -1, -1}, emboss[9]{-2, -1, 0, -1, 1, 1, 0, 1, 2};
                const double *kernel = type == Effect::Blur      ? blur
                                       : type == Effect::Sharpen ? sharp
                                       : type == Effect::Edge    ? edge
                                                                 : emboss;
                r = g = b = 0;
                double alpha{};
                for (int yy = -1; yy <= 1; ++yy)
                    for (int xx = -1; xx <= 1; ++xx) {
                        const auto q = src[static_cast<size_t>(
                                               std::clamp(y + yy, 0, static_cast<int>(image.height) - 1)) *
                                               image.width +
                                           std::clamp(x + xx, 0, static_cast<int>(image.width) - 1)];
                        const double k = kernel[(yy + 1) * 3 + xx + 1];
                        if (type == Effect::Blur) {
                            r += q.r * q.a * k;
                            g += q.g * q.a * k;
                            b += q.b * q.a * k;
                            alpha += q.a * k;
                        } else {
                            r += q.r * k;
                            g += q.g * k;
                            b += q.b * k;
                        }
                    }
                if (type == Effect::Blur) {
                    r = alpha ? r / alpha : 0;
                    g = alpha ? g / alpha : 0;
                    b = alpha ? b / alpha : 0;
                    p.a = byte(alpha / 16);
                }
                if (type == Effect::Emboss) {
                    r += 128;
                    g += 128;
                    b += 128;
                }
                break;
            }
            }
            dst[index] = {byte(r), byte(g), byte(b), p.a};
        }
    return out;
}
} // namespace gvn
