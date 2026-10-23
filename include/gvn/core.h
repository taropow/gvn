#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace gvn {
constexpr size_t kDecodeLimit = 256ull * 1024 * 1024;
constexpr size_t kImageLimit = 1024ull * 1024 * 1024;
struct Pixel {
    uint8_t r{}, g{}, b{}, a{};
    bool operator==(const Pixel &) const = default;
};
static_assert(sizeof(Pixel) == 4);
struct Rect {
    int x{}, y{}, w{}, h{};
    bool empty() const {
        return w <= 0 || h <= 0;
    }
};
// Immutable source palette and tightly packed, most-significant-bit-first rows.
// Sharing this data avoids another index-buffer copy for Undo/cache/frame extraction.
struct IndexedPixels {
    uint32_t width{}, height{};
    unsigned bits{8};
    std::vector<Pixel> palette;
    std::vector<uint8_t> packed;
    size_t stride() const {
        return (size_t(width) * bits + 7) / 8;
    }
    size_t bytes() const;
    uint8_t index(uint32_t x, uint32_t y) const {
        const size_t position = size_t(x) * bits;
        return (packed[size_t(y) * stride() + position / 8] >> (8 - bits - position % 8)) &
               ((1u << bits) - 1);
    }
    void set_index(uint32_t x, uint32_t y, uint8_t value) {
        const size_t position = size_t(x) * bits;
        const unsigned shift = 8 - bits - position % 8, mask = ((1u << bits) - 1) << shift;
        auto &output = packed[size_t(y) * stride() + position / 8];
        output = static_cast<uint8_t>((output & ~mask) | ((unsigned(value) << shift) & mask));
    }
    bool valid() const;
    bool matches(const std::vector<Pixel> &pixels) const;
};
struct Frame {
    std::vector<Pixel> pixels;
    uint64_t duration_ms{100};
    std::shared_ptr<const IndexedPixels> indexed;
};
// Optional immutable encoded provenance. Decoders can reconstruct one indexed
// frame when an edit/export needs it without retaining a second movie raster.
struct IndexedSource {
    virtual ~IndexedSource() = default;
    virtual std::shared_ptr<const IndexedPixels> read(size_t frame) const = 0;
    virtual size_t bytes() const = 0;
};
enum class Rgb16Layout { Rgb555, Rgb565 };
// Immutable observed RGB-to-component codes from a decoded 16-bit BMP.
// A candidate survives edits only when every current opaque pixel still matches.
struct Rgb16Colors {
    Rgb16Layout layout{Rgb16Layout::Rgb555};
    std::array<uint8_t, 256> red, green, blue;
    Rgb16Colors();
    bool matches(const std::vector<Pixel> &) const;
};
enum class ImageColorType : uint8_t { Unknown,Palette,Rgb,Bitfields,Gray,Cmyk };
const wchar_t *image_color_type_name(ImageColorType);
// Immutable UTF-8 PNG text, shared by edits/Undo. byte_size is cached for loader budgets.
struct PromptSource {
    std::vector<std::pair<std::string,std::string>> text;
    std::string issue;
    size_t byte_size{};
};
struct ImageMetadata {
    std::shared_ptr<const PromptSource> prompt_source;
    uint32_t original_width{},original_height{},original_bpp{};
    double dpi_x{72},dpi_y{72};
    bool operator==(const ImageMetadata &) const = default;
    bool dpi_defined{},icc_profile{};
    uint32_t icc_bytes{};
    ImageColorType original_color_type{ImageColorType::Unknown};
    std::wstring source_format;
    uint64_t source_bytes{},source_load_ms{};
};
struct Image {
    uint32_t width{}, height{};
    std::vector<Frame> frames;
    std::wstring format;
    std::filesystem::path path;
    uint64_t file_bytes{}, load_ms{};
    uint32_t file_loops{};
    bool animation_compensate{true};
    ImageMetadata metadata;
    std::shared_ptr<const Rgb16Colors> rgb16;
    std::shared_ptr<const IndexedSource> indexed_source;
    unsigned indexed_quarters{};
    bool animated() const {
        return frames.size() > 1;
    }
    size_t bytes() const;
};
// Reject stale/unrepresentable numbers. Optional reconstruction failure leaves
// the already-decoded RGBA usable; an invalid frame number is still an error.
std::shared_ptr<const IndexedPixels> image_indices(const Image &, size_t frame);
size_t checked_pixels(uint64_t width, uint64_t height, uint64_t frames = 1, size_t limit = kDecodeLimit);
// Existing, validated in-memory images may be larger than the decoder's independent input budget.
size_t checked_image_pixels(uint64_t width, uint64_t height);
Pixel over(Pixel source, Pixel destination);
Rect intersect(Rect a, Rect b);
Rect place_window(Rect requested, Rect work, bool centered);
Rect place_overlay(Rect area, int width, int height, int position, int margin);
enum class Fit { Contain, Width, Height, Cover, Actual };
struct View {
    double width{1}, height{1}, canvas_w{1}, canvas_h{1};
    double scale{1}, pan_x{}, pan_y{};
    Fit fit{Fit::Contain};
    bool manual{}, centered{true}, shrink_only{}, free_pan{};
    double fit_limit{50};
    void resize(double w, double h);
    void set_image(double w, double h, bool keep_zoom = false);
    void apply_fit(Fit value, bool only_shrink = false);
    void zoom_at(double factor, double x, double y);
    void zoom_to(double target, double x, double y);
    double base_x() const {
        return centered ? (width - canvas_w * scale) / 2 : 0;
    }
    double base_y() const {
        return centered ? (height - canvas_h * scale) / 2 : 0;
    }
    std::pair<double, double> x_range() const;
    std::pair<double, double> y_range() const;
    double left() const {
        return base_x() + pan_x;
    }
    double top() const {
        return base_y() + pan_y;
    }
};
double stepped_zoom(double scale, bool enlarge, int mode, int relative, int below, int above);
struct CanvasScrollbars {
    bool horizontal{}, vertical{};
};
// Resolve the two dependent scrollbar axes, refit if needed, and clamp visible content.
CanvasScrollbars arrange_scrollbars(View &view, int width, int height, int thickness, bool enabled);
struct Player {
    std::vector<uint64_t> durations;
    std::vector<uint64_t> boundaries;
    size_t frame{};
    uint64_t elapsed{}, cycle_ms{};
    uint32_t file_loops{}, completed_passes{};
    bool playing{true}, loop{true}, honor_file_loops{true}, ended{}, compensate{true}, gif_clock{};
    void reset(const Image &image);
    void seek(size_t value);
    void step(int direction);
    void play();
    void set_loop(bool enabled, bool honor_file_count);
    bool advance(uint64_t milliseconds);
};
enum class Sort { Name, Time, Size, Extension, Random };
struct FileEntry {
    std::filesystem::path path;
    uint64_t bytes{};
    int64_t time{};
    std::wstring display_name;
};
bool supported(const std::filesystem::path &path);
void sort_files(std::vector<FileEntry> &files, Sort mode, bool reverse, bool natural, uint32_t seed = 1);
std::vector<FileEntry> list_files(const std::filesystem::path &folder, bool hidden = false);
struct FileVisibility {
    bool hidden{}, system{}, protected_system{};
};
std::vector<FileEntry> list_files(const std::filesystem::path &folder, FileVisibility visibility);
} // namespace gvn
