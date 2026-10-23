#include "gvn/resample.h"
#include "display_source.h"
#include "display_math.h"
#include <array>
#include <cmath>
#include <list>
#include <numbers>
#include <unordered_map>

namespace gvn {
namespace {
double sinc(double x) {
    if (std::abs(x) < 1e-12)
        return 1;
    const auto p = std::numbers::pi * x;
    return std::sin(p) / p;
}
double kernel(double x, int method) {
    x = std::abs(x);
    switch (method) {
    case DisplayHermite:
        return x < 1 ? (2 * x - 3) * x * x + 1 : 0;
    case DisplayBell:
        return x < .5 ? .75 - x * x : x < 1.5 ? .5 * (x - 1.5) * (x - 1.5) : 0;
    case DisplayBSpline:
        return x < 1 ? (3 * x - 6) * x * x / 6 + 2.0 / 3 : x < 2 ? (2 - x) * (2 - x) * (2 - x) / 6 : 0;
    case DisplayMitchell:
        return x < 1   ? ((7 * x - 12) * x * x + 16.0 / 3) / 6
               : x < 2 ? ((-7.0 / 3 * x + 12) * x - 20) * x / 6 + 16.0 / 9
                       : 0;
    case DisplayCubic:
        return x < 1 ? (1.5 * x - 2.5) * x * x + 1 : x < 2 ? ((-.5 * x + 2.5) * x - 4) * x + 2 : 0;
    case DisplayLan225:
    case DisplayLan250:
        // Inferred from the reference's opaque impulse/edge outputs. Support stays 2;
        // only the sinc window widens, strengthening the negative lobes.
        return x < 2 ? sinc(x) * sinc(x / (method == DisplayLan225 ? 2.25 : 2.5)) : 0;
    case DisplayLanczos3:
        return x < 3 ? sinc(x) * sinc(x / 3) : 0;
    default:
        throw std::invalid_argument("Invalid reconstruction kernel");
    }
}
struct Tap {
    uint32_t index;
    double weight;
};
struct Span {
    size_t first, last;
};
struct Axis {
    std::vector<Tap> taps;
    std::vector<Span> spans;
    size_t bytes() const {
        return taps.capacity() * sizeof(Tap) + spans.capacity() * sizeof(Span);
    }
};
uint32_t simple_position(uint32_t source, uint32_t destination, uint32_t at) {
    if (destination >= source)
        return static_cast<uint32_t>((2ull * at + 1) * source / (2ull * destination));
    if (destination == 1)
        return 0;
    return static_cast<uint32_t>((2ull * at * (source - 1) + destination - 1) / (2ull * (destination - 1)));
}
Axis axis(uint32_t source, uint32_t destination, uint32_t origin, uint32_t count, int method,
          bool simple_average) {
    Axis result;
    result.spans.reserve(count);
    const double ratio = static_cast<double>(source) / destination;
    const double rate = std::min(1.0, 1 / ratio);
    const double radius = method == DisplayHermite    ? 1
                          : method == DisplayBell     ? 1.5
                          : method == DisplayLanczos3 ? 3
                                                      : 2;
    const size_t per_pixel =
        method == DisplaySimple                      ? 1
        : method == DisplayIntegra || simple_average ? static_cast<size_t>(std::ceil(ratio)) + 2
        : method == DisplayLinear                    ? 2
                                                     : static_cast<size_t>(std::ceil(2 * radius / rate)) + 2;
    if (per_pixel > (8ull * 1024 * 1024 - count * sizeof(Span)) / sizeof(Tap) / count)
        throw std::length_error("Display coefficient budget exceeded");
    result.taps.reserve(per_pixel * count);
    for (uint32_t at = origin; at < origin + count; ++at) {
        const auto first = result.taps.size();
        const auto add = [&](int index, double weight) {
            if (std::abs(weight) < 1e-14)
                return;
            const auto clamped = static_cast<uint32_t>(std::clamp(index, 0, static_cast<int>(source) - 1));
            if (result.taps.size() > first && result.taps.back().index == clamped)
                result.taps.back().weight += weight;
            else
                result.taps.push_back({clamped, weight});
        };
        if (method == DisplaySimple) {
            add(static_cast<int>((2ull * at + 1) * source / (2ull * destination)), 1);
        } else if (method == DisplayIntegra || simple_average) {
            const double left = at * ratio, right = (at + 1) * ratio;
            const auto start =
                simple_average
                    ? static_cast<uint32_t>((2ull * at * source + destination) / (2ull * destination))
                    : static_cast<uint32_t>(static_cast<uint64_t>(at) * source / destination);
            const auto end =
                simple_average
                    ? static_cast<uint32_t>((2ull * (at + 1) * source + destination) / (2ull * destination))
                    : static_cast<uint32_t>((static_cast<uint64_t>(at + 1) * source + destination - 1) /
                                            destination);
            for (auto i = start; i < end; ++i)
                add(i,
                    simple_average ? 1 : std::max(0.0, std::min(right, i + 1.0) - std::max(left, double(i))));
        } else if (method == DisplayLinear) {
            // The reference's bilinear path aligns the first and last pixel centers.
            const double position = destination > 1 ? double(at) * (source - 1) / (destination - 1) : 0;
            const auto lo = static_cast<int>(std::floor(position));
            add(lo, 1 - (position - lo));
            add(lo + 1, position - lo);
        } else {
            const double center = (at + .5) * ratio - .5;
            const int start = static_cast<int>(std::ceil(center - radius / rate));
            const int end = static_cast<int>(std::floor(center + radius / rate));
            for (int i = start; i <= end; ++i)
                add(i, kernel((i - center) * rate, method));
        }
        const auto last = result.taps.size();
        double total{};
        for (auto i = first; i < last; ++i)
            total += result.taps[i].weight;
        if (std::abs(total) < 1e-12)
            throw std::runtime_error("Degenerate display weights");
        if (!simple_average)
            for (auto i = first; i < last; ++i)
                result.taps[i].weight /= total;
        result.spans.push_back({first, last});
    }
    return result;
}
std::array<uint8_t, 4> quantized(const std::array<double, 4> &pixel) {
    std::array<uint8_t, 4> result{};
    const auto alpha = detail::display_round(pixel[3], 255);
    result[3] = static_cast<uint8_t>(alpha);
    for (size_t channel = 0; channel < 3; ++channel)
        result[channel] =
            detail::display_round(pixel[channel], alpha);
    return result;
}
void horizontal_first(detail::DisplayRows &source, uint32_t left, uint32_t right, const Axis &horizontal, const Axis &vertical,
                      DisplayTile &tile) {
    struct Row {
        uint32_t source{};
        std::vector<uint8_t> values;
    };
    const size_t stride = static_cast<size_t>(tile.width) * 4;
    size_t needed_rows = 1;
    for (const auto span : vertical.spans)
        needed_rows = std::max(needed_rows, span.last - span.first);
    const size_t capacity =
        std::max<size_t>(1, std::min({needed_rows, size_t(512), size_t(16ull * 1024 * 1024 / stride)}));
    // Expand a source row once instead of unpacking the same byte channels at
    // every Lanczos tap. Only this row is double precision, not the full image.
    std::vector<double> expanded(static_cast<size_t>(right - left) * 4);
    std::list<Row> cache;
    std::unordered_map<uint32_t, std::list<Row>::iterator> lookup;
    const auto row = [&](uint32_t source_y) -> const std::vector<uint8_t> & {
        if (const auto found = lookup.find(source_y); found != lookup.end()) {
            cache.splice(cache.begin(), cache, found->second);
            return cache.front().values;
        }
        if (cache.size() == capacity) {
            lookup.erase(cache.back().source);
            cache.splice(cache.begin(), cache, std::prev(cache.end()));
        } else
            cache.emplace_front();
        auto &result = cache.front();
        result.source = source_y;
        result.values.resize(stride);
        detail::expand_display_row(source.get(source_y), expanded.data(), expanded.size());
        const auto source_pixels = expanded.data();
        for (size_t x = 0; x < tile.width; ++x) {
            detail::DisplaySum sum;
            const auto span = horizontal.spans[x];
            for (auto i = span.first; i < span.last; ++i) {
                const auto tap = horizontal.taps[i];
                const auto pixel = source_pixels + static_cast<size_t>(tap.index - left) * 4;
                sum.add(pixel, tap.weight);
            }
            const auto pixel = quantized(sum.value());
            std::copy(pixel.begin(), pixel.end(), result.values.begin() + x * 4);
        }
        lookup[source_y] = cache.begin();
        return result.values;
    };
    std::vector<double> sum(stride);
    for (size_t y = 0; y < tile.height; ++y) {
        std::fill(sum.begin(), sum.end(), 0);
        const auto span = vertical.spans[y];
        for (auto i = span.first; i < span.last; ++i) {
            const auto tap = vertical.taps[i];
            const auto &pixels = row(tap.index);
            detail::add_display_row(sum.data(), pixels.data(), stride, tap.weight);
        }
        for (size_t x = 0; x < tile.width; ++x) {
            const auto pixel = quantized({sum[x * 4], sum[x * 4 + 1], sum[x * 4 + 2], sum[x * 4 + 3]});
            std::copy(pixel.begin(), pixel.end(), tile.bgra.begin() + y * stride + x * 4);
        }
    }
    tile.workspace_bytes =
        horizontal.bytes() + vertical.bytes() + cache.size() * stride +
        (sum.capacity() + expanded.capacity()) * sizeof(double);
}
void average_tile(detail::DisplayRows &rows, uint32_t left, uint32_t right,
                  const Axis &horizontal, const Axis &vertical, DisplayTile &tile) {
    std::vector<uint32_t> columns(static_cast<size_t>(right - left) * 4);
    for (size_t y = 0; y < tile.height; ++y) {
        std::fill(columns.begin(), columns.end(), 0);
        const auto span = vertical.spans[y];
        for (auto i = span.first; i < span.last; ++i)
            detail::add_average_row(columns.data(), rows.get(vertical.taps[i].index), columns.size());
        for (size_t x = 0; x < tile.width; ++x) {
            std::array<uint64_t, 4> sum{};
            const auto horizontal_span = horizontal.spans[x];
            for (auto i = horizontal_span.first; i < horizontal_span.last; ++i) {
                const auto pixel = columns.data() + static_cast<size_t>(horizontal.taps[i].index - left) * 4;
                for (size_t c = 0; c < 4; ++c) sum[c] += pixel[c];
            }
            const auto divisor = uint64_t(horizontal_span.last - horizontal_span.first) * (span.last - span.first);
            const auto alpha = static_cast<uint8_t>(sum[3] / divisor);
            auto output = tile.bgra.data() + (y * tile.width + x) * 4;
            output[3] = alpha;
            for (size_t c = 0; c < 3; ++c) output[c] = static_cast<uint8_t>(std::min<uint64_t>(sum[c] / divisor, alpha));
        }
    }
    tile.conversion_bytes = rows.bytes();
    tile.workspace_bytes = horizontal.bytes() + vertical.bytes() + columns.capacity() * sizeof(uint32_t) + tile.conversion_bytes;
}
} // namespace
void detail::resample_source_tile(DisplaySource source, uint32_t scaled_width, uint32_t scaled_height,
                                  uint32_t x, uint32_t y, int method, DisplayTile &tile) {
    const auto width = source.width, height = source.height;
    if (method < DisplaySimple || method > DisplayLanczos3 || !scaled_width || !scaled_height ||
        source.bytes.size() != checked_image_pixels(width, height) * 4 ||
        tile.bgra.size() != checked_pixels(tile.width, tile.height, 1, 64ull * 1024 * 1024) * 4 ||
        static_cast<uint64_t>(x) + tile.width > scaled_width ||
        static_cast<uint64_t>(y) + tile.height > scaled_height)
        throw std::invalid_argument("Invalid native display tile");
    if (method == DisplaySimple) {
        std::vector<uint32_t> columns(tile.width);
        for (size_t column = 0; column < tile.width; ++column)
            columns[column] = simple_position(width, scaled_width, x + static_cast<uint32_t>(column));
        const auto [low, high] = std::minmax_element(columns.begin(), columns.end());
        const auto left = *low;
        DisplayRows rows(source, left, *high - left + 1);
        for (uint32_t line = 0; line < tile.height; ++line) {
            const auto source_y =
                height - 1 - simple_position(height, scaled_height, scaled_height - 1 - y - line);
            const auto source_pixels = rows.get(source_y);
            for (size_t column = 0; column < tile.width; ++column) {
                const auto input = source_pixels + static_cast<size_t>(columns[column] - left) * 4;
                const auto output = tile.bgra.data() + (static_cast<size_t>(line) * tile.width + column) * 4;
                std::copy_n(input, 4, output);
            }
        }
        tile.conversion_bytes = rows.bytes();
        tile.workspace_bytes = columns.capacity() * sizeof(uint32_t) + tile.conversion_bytes;
        return;
    }
    if (method >= DisplayHermite && method <= DisplayLanczos3 &&
        (scaled_width * 100ull < width || scaled_height * 100ull < height))
        method = DisplayIntegra;
    const bool average = method == DisplayLinear && (scaled_width - 1) * 2ull <= width - 1 &&
                         (scaled_height - 1) * 2ull <= height - 1;
    const auto horizontal = axis(width, scaled_width, x, tile.width, method, average);
    const auto vertical = axis(height, scaled_height, y, tile.height, method, average);
    uint32_t left = width, right{};
    for (const auto tap : horizontal.taps) {
        left = std::min(left, tap.index);
        right = std::max(right, tap.index + 1);
    }
    DisplayRows rows(source, left, right - left);
    if (average) {
        average_tile(rows, left, right, horizontal, vertical, tile);
        return;
    }
    const bool horizontal_shrink_first =
        scaled_width < width &&
        static_cast<uint64_t>(scaled_width) * height <= static_cast<uint64_t>(scaled_height) * width;
    if (method >= DisplayHermite && horizontal_shrink_first) {
        horizontal_first(rows, left, right, horizontal, vertical, tile);
        tile.conversion_bytes = rows.bytes();
        tile.workspace_bytes += tile.conversion_bytes;
        return;
    }
    const size_t stride = static_cast<size_t>(tile.width) * 4;
    std::vector<double> intermediate(static_cast<size_t>(right - left) * 4);
    for (size_t destination_y = 0; destination_y < tile.height; ++destination_y) {
        std::fill(intermediate.begin(), intermediate.end(), 0);
        const auto span = vertical.spans[destination_y];
        for (auto i = span.first; i < span.last; ++i) {
            const auto tap = vertical.taps[i];
            const auto input = rows.get(tap.index);
            detail::add_display_row(intermediate.data(), input, intermediate.size(), tap.weight);
        }
        // Reconstruct Y before X and clip between passes, retaining premultiplied colors.
        if (method >= DisplayHermite)
            for (size_t at = 0; at < intermediate.size(); at += 4) {
                const auto pixel = quantized(
                    {intermediate[at], intermediate[at + 1], intermediate[at + 2], intermediate[at + 3]});
                for (size_t channel = 0; channel < 4; ++channel)
                    intermediate[at + channel] = pixel[channel];
            }
        auto output = tile.bgra.data() + destination_y * stride;
        for (size_t column = 0; column < tile.width; ++column) {
            detail::DisplaySum accumulation;
            const auto horizontal_span = horizontal.spans[column];
            for (auto i = horizontal_span.first; i < horizontal_span.last; ++i) {
                const auto tap = horizontal.taps[i];
                const auto offset = static_cast<size_t>(tap.index - left) * 4;
                accumulation.add(intermediate.data() + offset, tap.weight);
            }
            const auto pixel = quantized(accumulation.value());
            std::copy(pixel.begin(), pixel.end(), output + column * 4);
        }
    }
    tile.conversion_bytes = rows.bytes();
    tile.workspace_bytes = horizontal.bytes() + vertical.bytes() + intermediate.capacity() * sizeof(double) +
                           tile.conversion_bytes;
}
void resample_native_tile(std::span<const uint8_t> data, uint32_t width, uint32_t height,
                          uint32_t scaled_width, uint32_t scaled_height, uint32_t x, uint32_t y, int method,
                          DisplayTile &tile) {
    detail::resample_source_tile({data, width, height, false}, scaled_width, scaled_height, x, y, method,
                                 tile);
}
void resample_edit_rgba(std::span<const Pixel> source, uint32_t sw, uint32_t sh,
                        std::span<Pixel> destination, uint32_t width, uint32_t height,
                        int horizontal_method, int vertical_method) {
    if (source.size() != checked_image_pixels(sw, sh) ||
        destination.size() != checked_image_pixels(width, height) ||
        horizontal_method < DisplaySimple || horizontal_method > DisplayLanczos3 ||
        vertical_method < DisplaySimple || vertical_method > DisplayLanczos3)
        throw std::invalid_argument("Invalid image resize input");
    if (horizontal_method == vertical_method &&
        std::all_of(source.begin(), source.end(), [](Pixel p) { return p.a == 255; })) {
        // Reuse the reference-checked opaque arithmetic, including inter-pass rounding.
        detail::DisplaySource input{{reinterpret_cast<const uint8_t *>(source.data()), source.size_bytes()}, sw, sh, true};
        DisplayTile tile; tile.width = width;
        const auto rows = std::max(1u, std::min(64u, 1024u * 1024u / width / 4));
        for (uint32_t y = 0; y < height; y += tile.height) {
            tile.height = std::min(rows, height - y); tile.bgra.resize(size_t(width) * tile.height * 4);
            detail::resample_source_tile(input, width, height, 0, y, horizontal_method, tile);
            for (size_t i = 0; i < size_t(width) * tile.height; ++i) {
                const auto p = tile.bgra.data() + i * 4;
                destination[size_t(y) * width + i] = {p[2], p[1], p[0], p[3]};
            }
        }
        return;
    }
    auto effective = [](int method, uint32_t source, uint32_t target) {
        return method >= DisplayHermite && uint64_t(target) * 100 < source ? DisplayIntegra : method;
    };
    horizontal_method = effective(horizontal_method, sw, width);
    vertical_method = effective(vertical_method, sh, height);
    const bool same_linear = horizontal_method == DisplayLinear && vertical_method == DisplayLinear;
    const bool average_x = horizontal_method == DisplayLinear && (width - 1) * 2ull <= sw - 1 &&
                           (!same_linear || (height - 1) * 2ull <= sh - 1);
    const bool average_y = vertical_method == DisplayLinear && (height - 1) * 2ull <= sh - 1 &&
                           (!same_linear || (width - 1) * 2ull <= sw - 1);
    auto horizontal = axis(sw, width, 0, width, horizontal_method, average_x);
    auto vertical = axis(sh, height, 0, height, vertical_method, average_y);
    const auto normalize = [](Axis &table) {
        for (const auto span : table.spans) {
            double sum{};
            for (auto i = span.first; i < span.last; ++i) sum += table.taps[i].weight;
            for (auto i = span.first; i < span.last; ++i) table.taps[i].weight /= sum;
        }
    };
    normalize(horizontal); normalize(vertical);
    using Float = std::array<double, 4>; // r*a,g*a,b*a,a: no8bit premultiplication loss.
    const auto sample = [](Pixel p) -> Float { return {double(p.r)*p.a,double(p.g)*p.a,double(p.b)*p.a,double(p.a)}; };
    const auto sanitize = [](Float &p) {
        if (p[3] <= 1e-12) { p = {}; return; }
        for (size_t c=0;c<3;++c) p[c]=std::clamp(p[c],0.0,255*p[3]);
    };
    const auto output = [](const Float &p) -> Pixel {
        const auto a=static_cast<uint8_t>(std::clamp(std::lround(p[3]),0l,255l));
        if (!a || p[3] <= 1e-12) return {};
        const auto channel=[&](size_t c){return static_cast<uint8_t>(std::clamp(std::lround(p[c]/p[3]),0l,255l));};
        return {channel(0),channel(1),channel(2),a};
    };
    size_t needed_rows=1;
    for (const auto span:vertical.spans) needed_rows=std::max(needed_rows,span.last-span.first);
    const size_t capacity=std::max<size_t>(1,std::min<size_t>(512,4ull*1024*1024/(size_t(width)*sizeof(Float))));
    const bool horizontal_first = needed_rows <= capacity;
    if (horizontal_first) {
        struct Row { uint32_t source{}; std::vector<Float> values; };
        std::list<Row> cache;
        std::unordered_map<uint32_t,std::list<Row>::iterator> lookup;
        const auto row = [&](uint32_t sy) -> const std::vector<Float> & {
            if (const auto found=lookup.find(sy);found!=lookup.end()) {
                cache.splice(cache.begin(),cache,found->second);return cache.front().values;
            }
            if (cache.size()==needed_rows) {
                lookup.erase(cache.back().source);cache.splice(cache.begin(),cache,std::prev(cache.end()));
            } else cache.emplace_front();
            auto &next=cache.front();next.source=sy;next.values.assign(width,{});
            for (uint32_t x=0;x<width;++x) {
                const auto span=horizontal.spans[x];auto &sum=next.values[x];
                for (auto i=span.first;i<span.last;++i) {
                    const auto tap=horizontal.taps[i];const auto p=sample(source[size_t(sy)*sw+tap.index]);
                    for(size_t c=0;c<4;++c)sum[c]+=p[c]*tap.weight;
                }
                sanitize(sum);
            }
            lookup[sy]=cache.begin();return next.values;
        };
        std::vector<Float> sums(width);
        for(uint32_t y=0;y<height;++y) {
            std::fill(sums.begin(),sums.end(),Float{});const auto span=vertical.spans[y];
            for(auto i=span.first;i<span.last;++i) {
                const auto tap=vertical.taps[i];const auto &values=row(tap.index);
                for(uint32_t x=0;x<width;++x)for(size_t c=0;c<4;++c)sums[x][c]+=values[x][c]*tap.weight;
            }
            for(uint32_t x=0;x<width;++x)destination[size_t(y)*width+x]=output(sums[x]);
        }
    } else {
        // A large vertical support need not retain a full intermediate image.
        std::vector<Float> intermediate(sw);
        for(uint32_t y=0;y<height;++y) {
            std::fill(intermediate.begin(),intermediate.end(),Float{});const auto span=vertical.spans[y];
            for(auto i=span.first;i<span.last;++i) {
                const auto tap=vertical.taps[i];
                for(uint32_t x=0;x<sw;++x) {
                    const auto p=sample(source[size_t(tap.index)*sw+x]);
                    for(size_t c=0;c<4;++c)intermediate[x][c]+=p[c]*tap.weight;
                }
            }
            for(auto &p:intermediate)sanitize(p);
            for(uint32_t x=0;x<width;++x) {
                Float sum{};const auto h=horizontal.spans[x];
                for(auto i=h.first;i<h.last;++i) {
                    const auto tap=horizontal.taps[i];for(size_t c=0;c<4;++c)sum[c]+=intermediate[tap.index][c]*tap.weight;
                }
                destination[size_t(y)*width+x]=output(sum);
            }
        }
    }
}
} // namespace gvn
