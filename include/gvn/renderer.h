#pragma once
#include "core.h"
#include "resample.h"
#include "floating_selection.h"
#include "selection_region.h"
#include "annotation.h"
#include <optional>
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

namespace gvn {
struct CaptionOverlay {
    std::wstring text, family{L"MS UI Gothic"};
    float font_size{40.0f / 3}, dpi{96};
    int weight{400}, charset{DEFAULT_CHARSET}, position{}, background{2};
    bool italic{}, underline{}, strikeout{};
    uint32_t foreground{0xffffff}, backcolor{};
    bool operator==(const CaptionOverlay &) const = default;
};
struct DrawMetrics {
    double factory_ms{}, target_ms{}, brush_ms{}, sampling_ms{}, upload_ms{}, background_ms{}, scene_ms{};
};
enum class RenderBackend { Automatic, Software, SystemDefault };
class Renderer {
    friend struct RendererAudit;
    bool surface_valid{};
    RenderBackend applied_backend{RenderBackend::Automatic};
    D2D1_RENDER_TARGET_TYPE active_type{D2D1_RENDER_TARGET_TYPE_DEFAULT};
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    std::array<Microsoft::WRL::ComPtr<ID2D1StrokeStyle>,8> ants_styles;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> target;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> checker;
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> background_checker;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    Microsoft::WRL::ComPtr<IDWriteFactory> text_factory;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> overlay_layout;
    CaptionOverlay cached_overlay;
    float overlay_text_width{}, overlay_text_height{};
    bool overlay_font_known{}, overlay_native_font{};
    SIZE overlay_mask_size{};
    std::vector<uint32_t> overlay_mask;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> overlay_mask_bitmap;
    void draw_overlay(ID2D1RenderTarget *, ID2D1SolidColorBrush *, const View &, int top);
    void draw_floating(ID2D1RenderTarget *,ID2D1SolidColorBrush *,const View &,int,const D2D1_RECT_F &);
    void draw_selection_lines(ID2D1RenderTarget *,ID2D1SolidColorBrush *,std::span<const SelectionLine>,const View &,int);
    void draw_editor_overlay(ID2D1RenderTarget *,ID2D1SolidColorBrush *,const View &,int,const D2D1_RECT_F &);
    const Image *floating_prepared{};
    std::vector<uint8_t> floating_pixels;
    uint32_t floating_width{},floating_height{};
    Microsoft::WRL::ComPtr<ID2D1Bitmap> floating_bitmap;
    const Image *uploaded{};
    size_t uploaded_frame{SIZE_MAX};
    std::vector<uint8_t> premultiplied;
    const Image *prepared{}, *sampled{};
    size_t prepared_frame{SIZE_MAX}, sampled_frame{SIZE_MAX};
    View sampled_view;
    int sampled_method{-1};
    DisplayTile tile;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> sampled_bitmap;
    void ensure(HWND hwnd);
    void prepare(const Frame &frame);
    void prepare_image(const Image *image, size_t frame);
    bool prepare_sample(const Image *image, size_t frame, const View &view, uint32_t bitmap_limit);
    Microsoft::WRL::ComPtr<ID2D1Bitmap> tile_bitmap(ID2D1RenderTarget *context);
    Microsoft::WRL::ComPtr<ID2D1BitmapBrush> checker_for(ID2D1RenderTarget *context, bool viewport);
    HRESULT scene(ID2D1RenderTarget *context, ID2D1Bitmap *pixels, ID2D1BitmapBrush *pattern,
                  ID2D1BitmapBrush *viewport_pattern,
                  ID2D1SolidColorBrush *drawing_brush, const Image *image, const View &view, int top,
                  const Rect *selection, std::span<const RECT> damage = {},bool tile_allowed=true);
    void scene_content(ID2D1RenderTarget *, ID2D1Bitmap *, ID2D1BitmapBrush *, ID2D1BitmapBrush *,
                       ID2D1SolidColorBrush *, const Image *, const View &, int, const Rect *,bool);

  public:
    // Optional diagnostics; ordinary painting does not query the clock.
    DrawMetrics *trace{};
    RenderBackend backend{RenderBackend::Automatic};
    bool software_target() const {
        return target && active_type == D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    }
    CaptionOverlay overlay;
    std::shared_ptr<const Image> floating_source;
    FloatingBox floating_box;
    const SelectionRegion *selection_region{};
    std::vector<FloatPoint> selection_path;
    int viewport_left{};
    unsigned ants_phase{};
    std::optional<FloatingBox> crop_box;
    std::vector<FloatPoint> pen_points;
    PenStyle pen_style;
    void set_floating(std::shared_ptr<const Image> source,FloatingBox box={}) {
        if(floating_source!=source){floating_prepared=nullptr;floating_pixels.clear();floating_bitmap.Reset();}
        floating_source=std::move(source);floating_box=box;
    }
    bool overlay_visible{};
    Rect overlay_rect;
    size_t overlay_layout_builds{};
    uint64_t last_damage_pixels{};
    size_t last_damage_regions{};
    size_t sample_builds{}; // Diagnostics; includes only actual tile reconstructions.
    bool overlay_uses_gdi() const { return overlay_native_font; }
    size_t overlay_mask_bytes() const { return overlay_mask.size() * sizeof(uint32_t); }
    bool checkerboard{true}, nearest{};
    bool resizing{};
    bool zoom_reduce{true}, zoom_enlarge{true}, selection_mode{}, dragging_low{};
    int down{1}, up{1}, fit_simple{}, zoom_simple{};
    bool sampling_limited(const View &view) const;
    int filter(const View &view) const;
    bool sample_current(const Image *image, size_t frame, const View &view) const;
    size_t sample_bytes() const {
        return tile.bgra.size();
    }
    size_t source_bytes() const {
        return premultiplied.capacity();
    }
    size_t sample_workspace_bytes() const {
        return tile.workspace_bytes;
    }
    uint32_t background{0x202226};
    bool background_tiled{true};
    int background_pattern{2};
    uint32_t background_first{0x202020}, background_second{0x303030};
    uint32_t checker_light{0xeeeeee}, checker_dark{0xc4c4c4};
    int checker_size{8};
    void reset_patterns() {
        checker.Reset();
        background_checker.Reset();
    }
    void reset_background() {
        surface_valid = false;
        reset_patterns();
        target.Reset();
        bitmap.Reset();
        sampled_bitmap.Reset();
        brush.Reset();
        overlay_mask_bitmap.Reset();
        floating_bitmap.Reset();
        // Device resources depend on the target; the CPU source/tile do not.
        uploaded = nullptr;
        uploaded_frame = SIZE_MAX;
    }
    HRESULT draw(HWND hwnd, const Image *image, size_t frame, const View &view, int top, int bottom,
                 const Rect *selection = nullptr, std::span<const RECT> damage = {});
    bool can_draw_cached(const Image *image,size_t frame) const {
        return image&&((sampled==image&&sampled_frame==frame&&!tile.bgra.empty()) ||
            (uploaded==image&&uploaded_frame==frame&&bitmap));
    }
    void adopt_sample(const Image *,size_t,const View &,int,DisplayTile);
    HRESULT draw_cached(HWND,const Image *,size_t,const View &,int,int,const Rect * = nullptr,std::span<const RECT> = {});
    void resize(UINT width, UINT height);
    HRESULT print(HWND hwnd, HDC dc, const Image *image, size_t frame, const View &view, int top,
                  const Rect *selection);
    void invalidate() {
        surface_valid = false;
        uploaded = nullptr;
        uploaded_frame = SIZE_MAX;
        prepared = sampled = nullptr;
        prepared_frame = sampled_frame = SIZE_MAX;
        sampled_method = -1;
        tile = {};
        sampled_bitmap.Reset();
    }
};
} // namespace gvn
