#include "gvn/core.h"
#include <cmath>
#include <climits>
#include <cwctype>
#include <numeric>
#include <random>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlwapi.h>
#endif

namespace gvn {
const wchar_t *image_color_type_name(ImageColorType type) {
    switch(type){case ImageColorType::Palette:return L"パレット";case ImageColorType::Rgb:return L"RGB";
    case ImageColorType::Bitfields:return L"ビットフィールド";case ImageColorType::Gray:return L"グレースケール";
    case ImageColorType::Cmyk:return L"CMYK";default:return L"不明";}
}
Rect place_overlay(Rect area, int width, int height, int position, int margin) {
    if (area.empty())
        return {};
    position = std::clamp(position, 0, 8);
    const int mx = std::min(std::max(0, margin), (area.w - 1) / 2);
    const int my = std::min(std::max(0, margin), (area.h - 1) / 2);
    width = std::clamp(width, 1, area.w - 2 * mx);
    height = std::clamp(height, 1, area.h - 2 * my);
    const auto axis = [](int length, int size, int margin, int alignment) {
        return alignment == 0 ? margin : alignment == 1 ? (length - size) / 2 : length - size - margin;
    };
    return {area.x + axis(area.w, width, mx, position % 3), area.y + axis(area.h, height, my, position / 3),
            width, height};
}
CanvasScrollbars arrange_scrollbars(View &view, int width, int height, int thickness, bool enabled) {
    width = std::max(1, width);
    height = std::max(1, height);
    thickness = std::max(1, thickness);
    CanvasScrollbars bars;
    const View original = view;
    for (int attempt = 0; attempt < 3; ++attempt) {
        view = original;
        const int w = std::max(1, width - (bars.vertical ? thickness : 0));
        const int h = std::max(1, height - (bars.horizontal ? thickness : 0));
        if (view.width != w || view.height != h)
            view.resize(w, h);
        if (!enabled)
            break;
        const bool x = bars.horizontal || view.canvas_w * view.scale > w + .01;
        const bool y = bars.vertical || view.canvas_h * view.scale > h + .01;
        if (x == bars.horizontal && y == bars.vertical)
            break;
        bars = {x, y};
    }
    if (enabled) {
        const auto [xmin, xmax] = view.x_range();
        const auto [ymin, ymax] = view.y_range();
        view.pan_x = std::clamp(view.pan_x, xmin, xmax);
        view.pan_y = std::clamp(view.pan_y, ymin, ymax);
    }
    return bars;
}
size_t checked_pixels(uint64_t w, uint64_t h, uint64_t n, size_t limit) {
    if (!w || !h || !n || w > 65535 || h > 65535 || n > 100000)
        throw std::runtime_error("Invalid image dimensions or frame count");
    const uint64_t pixels = w * h;
    if (pixels > limit / sizeof(Pixel) || n > limit / sizeof(Pixel) / pixels)
        throw std::runtime_error("Image data exceeds the memory limit");
    return static_cast<size_t>(pixels * n);
}
size_t checked_image_pixels(uint64_t width, uint64_t height) {
    return checked_pixels(width, height, 1, kImageLimit);
}
size_t IndexedPixels::bytes() const {
    return packed.size() + palette.size() * sizeof(Pixel);
}
bool IndexedPixels::valid() const {
    if (!width || !height || width > 65535 || height > 65535 ||
        (bits != 1 && bits != 2 && bits != 4 && bits != 8) || palette.empty() ||
        palette.size() > (size_t{1} << bits) || packed.size() != stride() * height)
        return false;
    if (palette.size() == (size_t{1} << bits))
        return true; // Every representable bit pattern names an existing palette entry.
    if (bits == 8)
        return std::all_of(packed.begin(), packed.end(),
                           [&](uint8_t value) { return value < palette.size(); });
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            if (index(x, y) >= palette.size())
                return false;
    return true;
}
bool IndexedPixels::matches(const std::vector<Pixel> &pixels) const {
    if (!valid() || pixels.size() != size_t(width) * height)
        return false;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            if (pixels[size_t(y) * width + x] != palette[index(x, y)])
                return false;
    return true;
}
Rgb16Colors::Rgb16Colors() {
    red.fill(255); green.fill(255); blue.fill(255);
}
bool Rgb16Colors::matches(const std::vector<Pixel> &pixels) const {
    if (pixels.empty() || (layout != Rgb16Layout::Rgb555 && layout != Rgb16Layout::Rgb565))
        return false;
    const auto green_limit = layout == Rgb16Layout::Rgb565 ? 64 : 32;
    return std::all_of(pixels.begin(), pixels.end(), [&](Pixel p) {
        return p.a == 255 && red[p.r] < 32 && green[p.g] < green_limit && blue[p.b] < 32;
    });
}
size_t Image::bytes() const {
    size_t total{};
    for (const auto &frame : frames) {
        total += frame.pixels.size() * sizeof(Pixel);
        if (frame.indexed)
            total += frame.indexed->bytes();
    }
    return total + (metadata.prompt_source ? metadata.prompt_source->byte_size : 0) + (rgb16 ? sizeof(Rgb16Colors) : 0) + (indexed_source ? indexed_source->bytes() : 0);
}
std::shared_ptr<const IndexedPixels> image_indices(const Image &image, size_t frame) {
    const auto &displayed = image.frames.at(frame);
    auto indices = displayed.indexed;
    if (!indices && image.indexed_source) {
        try {
            indices = image.indexed_source->read(frame);
            const auto quarters = image.indexed_quarters % 4;
            if (indices && quarters) {
                if (!indices->valid())
                    return {};
                auto rotated = std::make_shared<IndexedPixels>();
                rotated->width = quarters % 2 ? indices->height : indices->width;
                rotated->height = quarters % 2 ? indices->width : indices->height;
                rotated->bits = indices->bits;
                rotated->palette = indices->palette;
                rotated->packed.resize(rotated->stride() * rotated->height);
                for (uint32_t y = 0; y < rotated->height; ++y)
                    for (uint32_t x = 0; x < rotated->width; ++x) {
                        const auto [sx, sy] = quarters == 1 ? std::pair{y, indices->height - 1 - x}
                            : quarters == 2 ? std::pair{indices->width - 1 - x, indices->height - 1 - y}
                                            : std::pair{indices->width - 1 - y, x};
                        rotated->set_index(x, y, indices->index(sx, sy));
                    }
                indices = std::move(rotated);
            }
        } catch (...) {
            return {};
        }
    }
    if (!indices || indices->width != image.width || indices->height != image.height ||
        !indices->matches(displayed.pixels))
        return {};
    return indices;
}
Pixel over(Pixel s, Pixel d) {
    const uint32_t a = s.a * 255u + d.a * (255u - s.a);
    if (!a)
        return {};
    auto channel = [&](uint8_t c, uint8_t e) {
        return static_cast<uint8_t>((c * s.a * 255u + e * d.a * (255u - s.a) + a / 2) / a);
    };
    return {channel(s.r, d.r), channel(s.g, d.g), channel(s.b, d.b), static_cast<uint8_t>((a + 127) / 255)};
}
Rect intersect(Rect a, Rect b) {
    const int x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    const auto right = std::min(static_cast<int64_t>(a.x) + a.w, static_cast<int64_t>(b.x) + b.w),
               bottom = std::min(static_cast<int64_t>(a.y) + a.h, static_cast<int64_t>(b.y) + b.h);
    return {x, y, static_cast<int>(std::clamp<int64_t>(right - x, 0, INT_MAX)),
            static_cast<int>(std::clamp<int64_t>(bottom - y, 0, INT_MAX))};
}
Rect place_window(Rect requested, Rect work, bool centered) {
    if (requested.w <= 0 || requested.h <= 0 || work.w <= 0 || work.h <= 0)
        throw std::invalid_argument("Invalid window rectangle");
    const int width = std::min(requested.w, work.w), height = std::min(requested.h, work.h);
    const int64_t right = static_cast<int64_t>(work.x) + work.w - width,
                  bottom = static_cast<int64_t>(work.y) + work.h - height;
    const auto x = centered ? static_cast<int64_t>(work.x) + (work.w - width) / 2
                            : std::clamp<int64_t>(requested.x, work.x, right);
    const auto y = centered ? static_cast<int64_t>(work.y) + (work.h - height) / 2
                            : std::clamp<int64_t>(requested.y, work.y, bottom);
    return {static_cast<int>(std::clamp<int64_t>(x, INT_MIN, INT_MAX)),
            static_cast<int>(std::clamp<int64_t>(y, INT_MIN, INT_MAX)), width, height};
}
void View::resize(double w, double h) {
    width = std::max(1.0, w);
    height = std::max(1.0, h);
    if (!manual)
        apply_fit(fit, shrink_only);
}
void View::set_image(double w, double h, bool keep) {
    free_pan = false;
    canvas_w = std::max(1.0, w);
    canvas_h = std::max(1.0, h);
    pan_x = pan_y = 0;
    if (keep)
        manual = true;
    else
        apply_fit(fit);
}
void View::apply_fit(Fit value, bool only_shrink) {
    free_pan = false;
    fit = value;
    manual = false;
    shrink_only = only_shrink;
    pan_x = pan_y = 0;
    const double sx = width / canvas_w, sy = height / canvas_h;
    switch (value) {
    case Fit::Contain:
        scale = std::min(sx, sy);
        break;
    case Fit::Width:
        scale = sx;
        break;
    case Fit::Height:
        scale = sy;
        break;
    case Fit::Cover:
        scale = std::max(sx, sy);
        break;
    case Fit::Actual:
        scale = 1;
        break;
    }
    if (value != Fit::Actual)
        scale = std::min(scale, fit_limit);
    if (value != Fit::Actual && shrink_only)
        scale = std::min(scale, 1.0);
    scale = std::clamp(scale, 0.00001, 50.0);
}
static double snap_actual_zoom(double current, double target) {
    return (current < 1 && target > 1) || (current > 1 && target < 1) ? 1.0 : target;
}
void View::zoom_at(double factor, double x, double y) {
    if (!std::isfinite(factor) || factor <= 0)
        return;
    zoom_to(snap_actual_zoom(scale, std::clamp(scale * factor, .00001, 50.0)), x, y);
}
void View::zoom_to(double target, double x, double y) {
    if (!std::isfinite(target) || target <= 0)
        return;
    const double ix = (x - left()) / scale, iy = (y - top()) / scale;
    scale = std::clamp(target, .00001, 50.0);
    manual = true;
    pan_x = x - ix * scale - base_x();
    pan_y = y - iy * scale - base_y();
}
std::pair<double, double> View::x_range() const {
    if (free_pan) {
        const double visible = std::min({32.0, width, canvas_w * scale});
        return {visible - canvas_w * scale - base_x(), width - visible - base_x()};
    }
    const double extent = std::max(0.0, canvas_w * scale - width);
    return centered ? std::pair{-extent / 2, extent / 2} : std::pair{-extent, 0.0};
}
std::pair<double, double> View::y_range() const {
    if (free_pan) {
        const double visible = std::min({32.0, height, canvas_h * scale});
        return {visible - canvas_h * scale - base_y(), height - visible - base_y()};
    }
    const double extent = std::max(0.0, canvas_h * scale - height);
    return centered ? std::pair{-extent / 2, extent / 2} : std::pair{-extent, 0.0};
}
double stepped_zoom(double scale, bool enlarge, int mode, int relative, int below, int above) {
    if (mode != 1 && mode != 2) {
        const double factor = 1 + relative / 100.0;
        return snap_actual_zoom(scale, std::clamp(scale * (enlarge ? factor : 1 / factor), .00001, 50.0));
    }
    // MassiGra rounds the current percentage before finding a strictly adjacent step.
    // Region selection and the 100% crossing limit use the unrounded percentage.
    const double original = scale * 100;
    const double current = std::floor(original + .5);
    double result;
    if (mode == 2) {
        constexpr double stops[]{1,   2,   5,   10,  15,   20,   25,   30,   40,   50,   60,
                                 75,  100, 125, 150, 175,  200,  250,  300,  350,  400,  500,
                                 600, 800, 1000,1300,1600, 2000, 2500, 3000, 3500, 4000, 5000};
        result = enlarge ? 5000 : 1;
        if (enlarge) {
            for (double stop : stops)
                if (stop > current) {
                    result = stop;
                    break;
                }
        } else {
            for (double stop : stops)
                if (stop < current)
                    result = stop;
        }
    } else {
        const double step = std::max(1, original < 100 || (!enlarge && original <= 100) ? below : above);
        const double position = (current - 100) / step;
        result = 100 + (enlarge ? std::floor(position) + 1 : std::ceil(position) - 1) * step;
    }
    if (enlarge && original < 100)
        result = std::min(result, 100.0);
    if (!enlarge && original > 100)
        result = std::max(result, 100.0);
    return std::clamp(result, 1.0, 5000.0) / 100;
}
void Player::reset(const Image &image) {
    std::vector<uint64_t> next_durations, next_boundaries;
    next_durations.reserve(image.frames.size());
    next_boundaries.reserve(image.frames.size());
    uint64_t total = 0;
    for (const auto &f : image.frames) {
        const auto duration = std::clamp<uint64_t>(f.duration_ms, 10, 3600000);
        if (duration > UINT64_MAX - total)
            throw std::overflow_error("Animation duration overflow");
        next_durations.push_back(duration);
        next_boundaries.push_back(total += duration);
    }
    durations = std::move(next_durations);
    boundaries = std::move(next_boundaries);
    cycle_ms = total;
    file_loops = image.file_loops;
    completed_passes = 0;
    ended = false;
    compensate = image.animation_compensate;
    gif_clock = image.format == L"GIF";
    frame = 0;
    elapsed = 0;
    playing = durations.size() > 1;
}
void Player::seek(size_t value) {
    frame = durations.empty() ? 0 : std::min(value, durations.size() - 1);
    elapsed = 0;
    completed_passes = 0;
    ended = false;
}
void Player::play() {
    if (ended)
        seek(0);
    playing = durations.size() > 1;
}
void Player::set_loop(bool enabled, bool honor_file_count) {
    if (loop != enabled || honor_file_loops != honor_file_count)
        completed_passes = 0;
    loop = enabled;
    honor_file_loops = honor_file_count;
}
void Player::step(int direction) {
    playing = false;
    if (durations.empty())
        return;
    const auto count = static_cast<int64_t>(durations.size());
    seek(static_cast<size_t>((static_cast<int64_t>(frame) + direction % count + count) % count));
}
bool Player::advance(uint64_t ms) {
    if (!playing || ended || durations.size() < 2)
        return false;
    const auto before = frame;
    uint64_t credit = 0;
    if (gif_clock || !compensate) {
        const auto remaining = durations[frame] - elapsed;
        if (ms < remaining) {
            elapsed += ms;
            return false;
        }
        // GIF advances one frame at a time even after a long rendering delay.
        // Compensated styles may carry credit only inside the next frame's duration.
        if (compensate)
            credit = ms - remaining;
        ms = remaining;
    }
    uint64_t passes = ms / cycle_ms, remainder = ms % cycle_ms;
    uint64_t position = (frame ? boundaries[frame - 1] : 0) + elapsed;
    if (remainder >= cycle_ms - position) {
        remainder -= cycle_ms - position;
        position = remainder;
        ++passes;
    } else
        position += remainder;
    const bool limited = loop && honor_file_loops && file_loops;
    const uint64_t left = !loop ? 1 : limited ? file_loops - completed_passes : 0;
    if (left && passes >= left) {
        frame = durations.size() - 1;
        elapsed = 0;
        playing = false;
        ended = true;
        if (limited)
            completed_passes = file_loops;
        return before != frame;
    }
    if (limited)
        completed_passes += static_cast<uint32_t>(passes);
    frame = static_cast<size_t>(std::upper_bound(boundaries.begin(), boundaries.end(), position) -
                                boundaries.begin());
    elapsed = position - (frame ? boundaries[frame - 1] : 0);
    if (gif_clock && compensate && credit < durations[frame])
        elapsed = credit;
    return before != frame;
}
static std::wstring lower(std::wstring value) {
    for (auto &c : value)
        c = static_cast<wchar_t>(std::towlower(c));
    return value;
}
bool supported(const std::filesystem::path &path) {
    const auto ext = lower(path.extension().wstring());
    return ext == L".png" || ext == L".apng" || ext == L".gif" || ext == L".jpg" || ext == L".jpeg" ||
           ext == L".jpe" || ext == L".bmp" || ext == L".psd" || ext == L".tif" || ext == L".tiff" ||
           ext == L".ico";
}
void sort_files(std::vector<FileEntry> &files, Sort mode, bool reverse, bool natural, uint32_t seed) {
    auto by_name = [&](const FileEntry &a, const FileEntry &b) {
        const auto left = a.display_name.empty() ? a.path.filename().wstring() : a.display_name;
        const auto right = b.display_name.empty() ? b.path.filename().wstring() : b.display_name;
#ifdef _WIN32
        if (natural)
            return StrCmpLogicalW(left.c_str(), right.c_str()) < 0;
#endif
        return lower(left) < lower(right);
    };
    std::stable_sort(files.begin(), files.end(), by_name);
    if (mode == Sort::Random) {
        std::mt19937 rng(seed);
        std::shuffle(files.begin(), files.end(), rng);
    } else if (mode != Sort::Name)
        std::stable_sort(files.begin(), files.end(), [&](const auto &a, const auto &b) {
            if (mode == Sort::Time)
                return a.time < b.time;
            if (mode == Sort::Size)
                return a.bytes < b.bytes;
            return lower(std::filesystem::path(a.display_name.empty() ? a.path.wstring() : a.display_name)
                             .extension()
                             .wstring()) <
                   lower(std::filesystem::path(b.display_name.empty() ? b.path.wstring() : b.display_name)
                             .extension()
                             .wstring());
        });
    if (reverse)
        std::reverse(files.begin(), files.end());
}
std::vector<FileEntry> list_files(const std::filesystem::path &folder, bool hidden) {
    return list_files(folder, FileVisibility{hidden, hidden, hidden});
}
std::vector<FileEntry> list_files(const std::filesystem::path &folder, FileVisibility visibility) {
    std::vector<FileEntry> result;
    std::error_code ec;
    for (std::filesystem::directory_iterator
             it(folder, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !supported(it->path()))
            continue;
#ifdef _WIN32
        const DWORD attrs = GetFileAttributesW(it->path().c_str());
        const bool hidden = (attrs & FILE_ATTRIBUTE_HIDDEN) != 0,
                   system = (attrs & FILE_ATTRIBUTE_SYSTEM) != 0;
        if (attrs == INVALID_FILE_ATTRIBUTES || (hidden && system ? !visibility.protected_system
                                                 : hidden         ? !visibility.hidden
                                                                  : system && !visibility.system))
            continue;
#endif
        const auto size = it->file_size(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        const auto time = it->last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        result.push_back({it->path(), size, time.time_since_epoch().count()});
    }
    sort_files(result, Sort::Name, false, true);
    return result;
}
} // namespace gvn
