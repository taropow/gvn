#include "gvn/renderer.h"
#include "pixel_conversion.h"
#include <chrono>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;
namespace gvn {
namespace {
class DrawTimer {
    double *output;
    std::chrono::steady_clock::time_point started;

  public:
    explicit DrawTimer(double *value) : output(value) {
        if (output)
            started = std::chrono::steady_clock::now();
    }
    ~DrawTimer() {
        if (output)
            *output +=
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    }
};
// DirectWrite does not expose bitmap fonts or GDI's @-prefixed vertical faces.
// Keep their native glyph mapping, metrics, decorations and discrete bitmap sizes.
class CaptionFont {
    HDC dc_{CreateCompatibleDC(nullptr)};
    HFONT font_{};
    HGDIOBJ previous_{};
  public:
    explicit CaptionFont(const CaptionOverlay &value) {
        LOGFONTW font{};
        font.lfHeight = -static_cast<LONG>(std::clamp(std::round(double(value.font_size)), 1.0,
                                                    double(INT_MAX / 4)));
        font.lfWeight = value.weight;
        font.lfItalic = value.italic;
        font.lfUnderline = value.underline;
        font.lfStrikeOut = value.strikeout;
        font.lfCharSet = static_cast<BYTE>(value.charset);
        font.lfQuality = ANTIALIASED_QUALITY;
        wcsncpy_s(font.lfFaceName, value.family.c_str(), _TRUNCATE);
        font_ = CreateFontIndirectW(&font);
        if (dc_ && font_)
            previous_ = SelectObject(dc_, font_);
    }
    ~CaptionFont() {
        if (previous_) SelectObject(dc_, previous_);
        if (font_) DeleteObject(font_);
        if (dc_) DeleteDC(dc_);
    }
    HDC dc() const { return dc_; }
    CaptionFont(const CaptionFont &) = delete;
    CaptionFont &operator=(const CaptionFont &) = delete;
    bool native(const CaptionOverlay &value) const {
        TEXTMETRICW metrics{};
        if (!dc_ || !font_ || !GetTextMetricsW(dc_, &metrics))
            throw std::runtime_error("Caption font unavailable");
        return value.family.starts_with(L"@") || !(metrics.tmPitchAndFamily & TMPF_TRUETYPE);
    }
    SIZE measure(const std::wstring &text) const {
        SIZE size{};
        if (!GetTextExtentPoint32W(dc_, text.c_str(), static_cast<int>(text.size()), &size))
            throw std::runtime_error("Caption font measurement failed");
        return size;
    }
    std::vector<uint32_t> mask(const std::wstring &text, int w, int h, int x, int y) const {
        if (w <= 0 || h <= 0 || uint64_t(w) * h > kDecodeLimit / sizeof(uint32_t))
            throw std::runtime_error("Caption viewport exceeds bitmap limit");
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        void *pixels{};
        const auto bitmap = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!bitmap || !pixels)
            throw std::runtime_error("Caption bitmap unavailable");
        const auto previous = SelectObject(dc_, bitmap);
        std::memset(pixels, 0, size_t(w) * h * sizeof(uint32_t));
        SetTextColor(dc_, RGB(255, 255, 255));
        SetBkMode(dc_, TRANSPARENT);
        const bool drawn = TextOutW(dc_, x, y, text.c_str(), static_cast<int>(text.size())) != FALSE;
        GdiFlush();
        std::vector<uint32_t> result;
        try {
            if (drawn) {
                const auto first = static_cast<const uint32_t *>(pixels);
                result.assign(first, first + size_t(w) * h);
                for (auto &pixel : result) {
                    const auto alpha = std::max({pixel & 255, (pixel >> 8) & 255, (pixel >> 16) & 255});
                    pixel = alpha * 0x01010101u; // premultiplied white coverage; never source image pixels.
                }
            }
        } catch (...) {
            SelectObject(dc_, previous);
            DeleteObject(bitmap);
            throw;
        }
        SelectObject(dc_, previous);
        DeleteObject(bitmap);
        if (!drawn)
            throw std::runtime_error("Caption text rendering failed");
        return result;
    }
};
} // namespace
static void verify(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error("Direct2D initialization failed");
}
void Renderer::ensure(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const auto pixels = static_cast<uint64_t>(std::max(1L, client.right)) * std::max(1L, client.bottom);
    const bool large = pixels > 2ull * 1024 * 1024;
    if (target) {
        // Keep an existing system target when shrinking. During live sizing, defer a costly upgrade.
        if (backend == applied_backend &&
            !(backend == RenderBackend::Automatic && software_target() && large && !resizing))
            return;
        reset_background();
    }
    if (!factory) {
        DrawTimer timer(trace ? &trace->factory_ms : nullptr);
        verify(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()));
    }
    active_type = backend == RenderBackend::Software || (backend == RenderBackend::Automatic && !large)
                      ? D2D1_RENDER_TARGET_TYPE_SOFTWARE
                      : D2D1_RENDER_TARGET_TYPE_DEFAULT;
    {
        DrawTimer timer(trace ? &trace->target_ms : nullptr);
        verify(factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(active_type),
            D2D1::HwndRenderTargetProperties(
                hwnd, D2D1::SizeU(std::max(1L, client.right), std::max(1L, client.bottom))),
            &target));
    }
    target->SetDpi(96, 96);
    {
        DrawTimer timer(trace ? &trace->brush_ms : nullptr);
        verify(target->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.9f, 0.9f), &brush));
    }
    uploaded = nullptr;
    uploaded_frame = SIZE_MAX;
    applied_backend = backend;
}
ComPtr<ID2D1BitmapBrush> Renderer::checker_for(ID2D1RenderTarget *context, bool viewport) {
    const int cell = viewport ? 8 << std::clamp(background_pattern, 0, 4) : std::clamp(checker_size, 2, 64);
    const int side = cell * 2;
    const auto first = viewport ? background_first : checker_light;
    const auto second = viewport ? background_second : checker_dark;
    std::vector<uint32_t> pattern_pixels(side * side);
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            pattern_pixels[y * side + x] = 0xff000000 | (((x / cell + y / cell) % 2) ? second : first);
    ComPtr<ID2D1Bitmap> pattern;
    verify(context->CreateBitmap(
        D2D1::SizeU(side, side), pattern_pixels.data(), side * 4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
        &pattern));
    ComPtr<ID2D1BitmapBrush> result;
    verify(context->CreateBitmapBrush(
        pattern.Get(),
        D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP,
                                    D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),
        &result));
    return result;
}
void Renderer::prepare(const Frame &frame) {
    premultiplied.resize(frame.pixels.size() * 4);
    detail::premultiply_rgba(reinterpret_cast<const uint8_t *>(frame.pixels.data()), premultiplied.data(),
                             frame.pixels.size());
}
void Renderer::prepare_image(const Image *image, size_t frame) {
    if (prepared != image || prepared_frame != frame) {
        prepare(image->frames.at(frame));
        prepared = image;
        prepared_frame = frame;
    }
}
bool Renderer::sampling_limited(const View &view) const {
    const double w = std::max(0.0, std::min(view.width, view.left() + view.canvas_w * view.scale) -
                                       std::max(0.0, view.left()));
    const double h = std::max(0.0, std::min(view.height, view.top() + view.canvas_h * view.scale) -
                                       std::max(0.0, view.top()));
    return w > 0 && h > 0 && ((w + 4) * (h + 4) > 64.0 * 1024 * 1024 / 4 || w + 4 > 16384 || h + 4 > 16384);
}
int Renderer::filter(const View &view) const {
    if (nearest || std::abs(view.scale-1)<1e-9)return DisplaySimple;
    const auto method=std::clamp(view.scale<1?down:up,0,kLastDisplayFilter);
    // Selected quality applies to fit, manual zoom and selection tools alike.
    // Interactive previews may use Linear temporarily, then restore the chosen method.
    if(dragging_low||resizing)return method==DisplaySimple?DisplaySimple:DisplayLinear;
    return method>1&&sampling_limited(view)?DisplayLinear:method;
}
bool Renderer::sample_current(const Image *image, size_t frame, const View &view) const {
    const int method = std::abs(view.scale - 1) < 1e-9 ? DisplaySimple : filter(view);
    return image && !tile.bgra.empty() && sampled == image && sampled_frame == frame &&
           sampled_method == method && sampled_view.scale == view.scale &&
           sampled_view.left() == view.left() && sampled_view.top() == view.top() &&
           sampled_view.width == view.width && sampled_view.height == view.height;
}
bool Renderer::prepare_sample(const Image *image, size_t frame, const View &view, uint32_t bitmap_limit) {
    if (!image) {
        // A viewer left open without an image must not retain the previous CPU/GPU image buffers.
        std::vector<uint8_t>().swap(premultiplied);
        prepared = uploaded = nullptr;
        prepared_frame = uploaded_frame = SIZE_MAX;
        bitmap.Reset();
    }
    const int method = std::abs(view.scale - 1) < 1e-9 ? DisplaySimple : filter(view);
    // DC and HWND targets can have different limits. Very wide sources still use visible tiles.
    bitmap_limit = std::min(16384u, bitmap_limit);
    const bool direct_actual = image && std::abs(view.scale - 1) < 1e-9 && image->width <= bitmap_limit &&
                               image->height <= bitmap_limit;
    if (!image || sampling_limited(view) || direct_actual) {
        // Release a potentially large old tile when leaving filtered fit mode.
        tile = {};
        sampled = nullptr;
        sampled_bitmap.Reset();
        return false;
    }
    if(resizing&&image->width<=bitmap_limit&&image->height<=bitmap_limit){
        // A size gesture changes the destination every frame. Reuse one source bitmap
        // and let Direct2D scale it; rebuild the selected-quality tile after the gesture.
        tile={};sampled=nullptr;sampled_bitmap.Reset();return false;
    }
    if (!sample_current(image, frame, view)) {
        if (view.manual || resizing) {
            prepare_image(image, frame);
            tile = resample_display(premultiplied, image->width, image->height, view, method);
        } else
            tile = resample_rgba_display(image->frames.at(frame).pixels, image->width, image->height, view,
                                         method);
        sampled = image;
        sampled_frame = frame;
        sampled_view = view;
        sampled_method = method;
        sampled_bitmap.Reset();
    }
    if (!view.manual && !resizing) {
        std::vector<uint8_t>().swap(premultiplied);
        prepared = nullptr;
        prepared_frame = SIZE_MAX;
    }
    uploaded = nullptr;
    uploaded_frame = SIZE_MAX;
    bitmap.Reset();
    return true;
}
ComPtr<ID2D1Bitmap> Renderer::tile_bitmap(ID2D1RenderTarget *context) {
    ComPtr<ID2D1Bitmap> result;
    if (!tile.bgra.empty())
        verify(context->CreateBitmap(D2D1::SizeU(tile.width, tile.height), tile.bgra.data(), tile.width * 4,
                                     D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                              D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                     &result));
    return result;
}
void Renderer::resize(UINT width, UINT height) {
    if (target){const auto before=target->GetPixelSize(),next=D2D1::SizeU(std::max(1u,width),std::max(1u,height));
        if(before.width!=next.width||before.height!=next.height)target->Resize(next);}
}
HRESULT Renderer::draw(HWND hwnd, const Image *image, size_t frame, const View &view, int top, int bottom,
                       const Rect *selection) {
    ensure(hwnd);
    bool filtered{};
    {
        DrawTimer timer(trace ? &trace->sampling_ms : nullptr);
        filtered = prepare_sample(image, frame, view, target->GetMaximumBitmapSize());
    }
    {
        DrawTimer timer(trace ? &trace->upload_ms : nullptr);
        if (filtered) {
            // Filtered fit display only uploads its visible tile to the GPU.
            bitmap.Reset();
            uploaded = nullptr;
        }
        if (image && !filtered && (uploaded != image || uploaded_frame != frame)) {
            prepare_image(image, frame);
            if (bitmap) {
                const auto size = bitmap->GetPixelSize();
                if (size.width != image->width || size.height != image->height)
                    bitmap.Reset();
            }
            if (!bitmap)
                verify(target->CreateBitmap(D2D1::SizeU(image->width, image->height), premultiplied.data(),
                                            image->width * 4,
                                            D2D1::BitmapProperties(D2D1::PixelFormat(
                                                DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                            &bitmap));
            else
                verify(bitmap->CopyFromMemory(nullptr, premultiplied.data(), image->width * 4));
            uploaded = image;
            uploaded_frame = frame;
        }
        if (filtered && !sampled_bitmap)
            sampled_bitmap = tile_bitmap(target.Get());
    }
    if ((background_tiled && !background_checker) || (image && checkerboard && !checker)) {
        DrawTimer timer(trace ? &trace->background_ms : nullptr);
        if (background_tiled && !background_checker)
            background_checker = checker_for(target.Get(), true);
        if (image && checkerboard && !checker)
            checker = checker_for(target.Get(), false);
    }
    DrawTimer timer(trace ? &trace->scene_ms : nullptr);
    const HRESULT result = scene(target.Get(), filtered ? sampled_bitmap.Get() : bitmap.Get(), checker.Get(),
                                 background_checker.Get(), brush.Get(), image, view, top, selection);
    if (result == D2DERR_RECREATE_TARGET)
        reset_background();
    (void)bottom;
    return result;
}
HRESULT Renderer::scene(ID2D1RenderTarget *context, ID2D1Bitmap *pixels, ID2D1BitmapBrush *pattern,
                        ID2D1BitmapBrush *viewport_pattern,
                        ID2D1SolidColorBrush *drawing_brush, const Image *image, const View &view, int top,
                        const Rect *selection) {
    context->BeginDraw();
    overlay_rect = {};
    context->Clear(D2D1::ColorF(background));
    context->SetTransform(D2D1::Matrix3x2F::Translation(float(viewport_left),0));
    const auto clip = D2D1::RectF(0, static_cast<float>(top), static_cast<float>(view.width),
                                  static_cast<float>(top + view.height));
    context->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    if (background_tiled) {
        viewport_pattern->SetTransform(D2D1::Matrix3x2F::Translation(0, static_cast<float>(top)));
        context->FillRectangle(clip, viewport_pattern);
    } else {
        drawing_brush->SetColor(D2D1::ColorF(background_first));
        context->FillRectangle(clip, drawing_brush);
    }
    if (image) {
        // At 1:1, a half-pixel center must not soften or shift source pixels.
        const bool actual = std::abs(view.scale - 1) < 1e-9;
        const double left = actual ? std::floor(view.left()) : view.left();
        const double image_top = actual ? std::floor(view.top()) : view.top();
        const auto destination =
            D2D1::RectF(static_cast<float>(left), static_cast<float>(top + image_top),
                        static_cast<float>(left + image->width * view.scale),
                        static_cast<float>(top + image_top + image->height * view.scale));
        if (checkerboard)
            context->FillRectangle(destination, pattern);
        else {
            drawing_brush->SetColor(D2D1::ColorF(background));
            context->FillRectangle(destination, drawing_brush);
        }
        auto sampled_destination = destination;
        const int method = filter(view);
        const bool sampled_tile = sampled == image && !tile.bgra.empty();
        if (sampled_tile)
            sampled_destination =
                D2D1::RectF(static_cast<float>(actual ? std::floor(tile.left) : tile.left),
                            static_cast<float>(top + (actual ? std::floor(tile.top) : tile.top)),
                            static_cast<float>(actual ? std::floor(tile.right) : tile.right),
                            static_cast<float>(top + (actual ? std::floor(tile.bottom) : tile.bottom)));
        if (pixels)
            context->DrawBitmap(pixels, sampled_destination, 1,
                                sampled_tile || method == 0 || std::abs(view.scale - 1) < 1e-9
                                    ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                    : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (selection && !selection->empty()) {
            if(selection_region&&selection_region->outline())draw_selection_lines(context,drawing_brush,*selection_region->outline(),view,top);
            else {
                const double x=selection->x,y=selection->y,r=x+selection->w,b=y+selection->h;
                const SelectionLine lines[]{{{x,y},{r,y}},{{r,y},{r,b}},{{r,b},{x,b}},{{x,b},{x,y}}};
                draw_selection_lines(context,drawing_brush,lines,view,top);
            }
        }
        if(selection_path.size()>1){std::vector<SelectionLine> lines;lines.reserve(selection_path.size());
            for(size_t n=1;n<selection_path.size();++n)lines.push_back({selection_path[n-1],selection_path[n]});
            draw_selection_lines(context,drawing_brush,lines,view,top);}
        if(floating_source)draw_floating(context,drawing_brush,view,top,destination);
        draw_editor_overlay(context,drawing_brush,view,top,destination);
    }
    if (image && overlay_visible && !overlay.text.empty())
        draw_overlay(context, drawing_brush, view, top);
    context->PopAxisAlignedClip();
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    return context->EndDraw();
}
void Renderer::draw_editor_overlay(ID2D1RenderTarget *context,ID2D1SolidColorBrush *drawing_brush,const View &view,int top,const D2D1_RECT_F &canvas){
    const bool actual=std::abs(view.scale-1)<1e-9;
    const double left=actual?std::floor(view.left()):view.left(),y=actual?std::floor(view.top()):view.top();
    const auto screen=[&](FloatPoint p){return D2D1::Point2F(float(left+p.x*view.scale),float(top+y+p.y*view.scale));};
    Microsoft::WRL::ComPtr<ID2D1Factory> owner;context->GetFactory(&owner);
    if(!pen_points.empty()){
        context->PushAxisAlignedClip(canvas,D2D1_ANTIALIAS_MODE_ALIASED);
        ComPtr<ID2D1PathGeometry> path;ComPtr<ID2D1StrokeStyle> style;
        if(pen_points.size()>1){ComPtr<ID2D1GeometrySink> sink;
            verify(owner->CreatePathGeometry(&path));verify(path->Open(&sink));sink->BeginFigure(screen(pen_points[0]),D2D1_FIGURE_BEGIN_HOLLOW);
            for(size_t n=1;n<pen_points.size();++n)sink->AddLine(screen(pen_points[n]));sink->EndFigure(D2D1_FIGURE_END_OPEN);verify(sink->Close());
            auto props=D2D1::StrokeStyleProperties();props.startCap=props.endCap=props.dashCap=D2D1_CAP_STYLE_ROUND;props.lineJoin=D2D1_LINE_JOIN_ROUND;
            verify(owner->CreateStrokeStyle(props,nullptr,0,&style));}
        const auto stroke=[&](Pixel c,double pixels,double offset){
            drawing_brush->SetColor(D2D1::ColorF(c.r/255.f,c.g/255.f,c.b/255.f,c.a/255.f));
            const float width=float(pixels*view.scale),shift=float(offset*view.scale);
            D2D1_MATRIX_3X2_F original;context->GetTransform(&original);
            context->SetTransform(D2D1::Matrix3x2F::Translation(shift,shift)*original);
            if(path)context->DrawGeometry(path.Get(),drawing_brush,width,style.Get());
            else context->FillEllipse(D2D1::Ellipse(screen(pen_points[0]),width/2,width/2),drawing_brush);
            context->SetTransform(original);};
        const auto &e=pen_style.effects;const double outside=pen_style.width+(e.outline?2*e.outline_width:0);
        if(e.shadow){auto c=e.shadow_color;
            if(e.shadow_blur){c.a=uint8_t(c.a/6);for(int n=5;n>=0;--n)stroke(c,outside+n*e.shadow_blur*.4,e.shadow_offset);}
            else stroke(c,outside,e.shadow_offset);}
        if(e.outline)stroke(e.outline_color,outside,0);stroke(pen_style.color,pen_style.width,0);
        context->PopAxisAlignedClip();
    }
    if(crop_box){
        const auto handles=crop_box->handles();ComPtr<ID2D1PathGeometry> path;ComPtr<ID2D1GeometrySink> sink;
        verify(owner->CreatePathGeometry(&path));verify(path->Open(&sink));sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
        sink->BeginFigure(D2D1::Point2F(canvas.left,canvas.top),D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(D2D1::Point2F(canvas.right,canvas.top));sink->AddLine(D2D1::Point2F(canvas.right,canvas.bottom));sink->AddLine(D2D1::Point2F(canvas.left,canvas.bottom));sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->BeginFigure(screen(handles[0]),D2D1_FIGURE_BEGIN_FILLED);for(int n:{2,4,6})sink->AddLine(screen(handles[n]));sink->EndFigure(D2D1_FIGURE_END_CLOSED);verify(sink->Close());
        drawing_brush->SetColor(D2D1::ColorF(0.f,0.f,0.f,.58f));context->PushAxisAlignedClip(canvas,D2D1_ANTIALIAS_MODE_ALIASED);context->FillGeometry(path.Get(),drawing_brush);context->PopAxisAlignedClip();
        drawing_brush->SetColor(D2D1::ColorF(1.f,1.f,1.f));
        for(int n=0;n<4;++n)context->DrawLine(screen(handles[n*2]),screen(handles[(n*2+2)%8]),drawing_brush,1);
        for(auto p:handles){const auto at=screen(p);context->FillRectangle(D2D1::RectF(at.x-4,at.y-4,at.x+4,at.y+4),drawing_brush);}
    }
}
void Renderer::draw_selection_lines(ID2D1RenderTarget *context,ID2D1SolidColorBrush *drawing_brush,std::span<const SelectionLine> lines,const View &view,int top){
    const auto previous=context->GetAntialiasMode();context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    const bool actual=std::abs(view.scale-1)<1e-9;
    const double left=actual?std::floor(view.left()):view.left(),image_top=actual?std::floor(view.top()):view.top();
    // Project first; the stroke itself always uses the target's physical pixel space (96 dpi).
    const auto screen=[&](FloatPoint p){return D2D1::Point2F(float(std::floor(left+p.x*view.scale)+.5),float(std::floor(top+image_top+p.y*view.scale)+.5));};
    auto &style=ants_styles[ants_phase%ants_styles.size()];
    if(!style){const float pattern[]{4,4};auto properties=D2D1::StrokeStyleProperties();
        properties.dashStyle=D2D1_DASH_STYLE_CUSTOM;properties.dashOffset=float(ants_phase%8);
        Microsoft::WRL::ComPtr<ID2D1Factory> owner;context->GetFactory(&owner);
        verify(owner->CreateStrokeStyle(properties,pattern,2,&style));}
    for(auto line:lines){auto a=screen(line.a),b=screen(line.b);if(a.x>b.x||(a.x==b.x&&a.y>b.y))std::swap(a,b);
        drawing_brush->SetColor(D2D1::ColorF(1.f,1.f,1.f));context->DrawLine(a,b,drawing_brush,1);
        drawing_brush->SetColor(D2D1::ColorF(0.f,0.f,0.f));context->DrawLine(a,b,drawing_brush,1,style.Get());}
    context->SetAntialiasMode(previous);
}
void Renderer::draw_floating(ID2D1RenderTarget *context,ID2D1SolidColorBrush *drawing_brush,const View &view,int top,const D2D1_RECT_F &canvas) {
    if(!floating_source||!floating_box.valid())return;
    if(floating_prepared!=floating_source.get()){
        floating_bitmap.Reset();floating_pixels.clear();
        const double ratio=std::min(1.,2048./std::max(floating_source->width,floating_source->height));
        Image preview;const Image *source=floating_source.get();
        if(ratio<1){preview=resize_image(*source,std::max(1u,uint32_t(source->width*ratio)),std::max(1u,uint32_t(source->height*ratio)));source=&preview;}
        floating_width=source->width;floating_height=source->height;floating_pixels.resize(size_t(floating_width)*floating_height*4);
        for(size_t i=0;i<source->frames[0].pixels.size();++i){const auto p=source->frames[0].pixels[i];
            floating_pixels[i*4]=uint8_t((unsigned(p.b)*p.a+127)/255);floating_pixels[i*4+1]=uint8_t((unsigned(p.g)*p.a+127)/255);
            floating_pixels[i*4+2]=uint8_t((unsigned(p.r)*p.a+127)/255);floating_pixels[i*4+3]=p.a;}
        floating_prepared=floating_source.get();
    }
    ComPtr<ID2D1Bitmap> local;
    const bool cached=context==target.Get();auto &pixels=cached?floating_bitmap:local;
    if(!pixels)verify(context->CreateBitmap(D2D1::SizeU(floating_width,floating_height),floating_pixels.data(),floating_width*4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&pixels));
    const bool actual=std::abs(view.scale-1)<1e-9;
    const double left=actual?std::floor(view.left()):view.left(),image_top=actual?std::floor(view.top()):view.top();
    const float cx=float(left+floating_box.cx*view.scale),cy=float(top+image_top+floating_box.cy*view.scale);
    const float width=float(floating_box.width*view.scale),height=float(floating_box.height*view.scale);
    context->PushAxisAlignedClip(canvas,D2D1_ANTIALIAS_MODE_ALIASED);
    D2D1_MATRIX_3X2_F previous_transform;context->GetTransform(&previous_transform);
    context->SetTransform(D2D1::Matrix3x2F::Rotation(float(floating_box.angle),D2D1::Point2F(cx,cy))*previous_transform);
    context->DrawBitmap(pixels.Get(),D2D1::RectF(cx-width/2,cy-height/2,cx+width/2,cy+height/2),1,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    context->SetTransform(previous_transform);context->PopAxisAlignedClip();
    const auto handles=floating_box.handles();auto screen=[&](FloatPoint p){return D2D1::Point2F(float(left+p.x*view.scale),float(top+image_top+p.y*view.scale));};
    std::array<SelectionLine,4> lines;for(int n=0;n<4;++n)lines[n]={handles[n*2],handles[(n*2+2)%8]};
    draw_selection_lines(context,drawing_brush,lines,view,top);
    const float radius=3.5f;
    for(auto p:handles){const auto q=screen(p);const auto r=D2D1::RectF(q.x-radius,q.y-radius,q.x+radius,q.y+radius);
        drawing_brush->SetColor(D2D1::ColorF(1,1,1));context->FillRectangle(r,drawing_brush);
        drawing_brush->SetColor(D2D1::ColorF(0.05f,0.45f,0.8f));context->DrawRectangle(r,drawing_brush,1);}
}
void Renderer::draw_overlay(ID2D1RenderTarget *context, ID2D1SolidColorBrush *drawing_brush, const View &view,
                            int top) {
    const auto antialias = context->GetTextAntialiasMode();
    context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    const bool changed = !overlay_font_known || cached_overlay != overlay;
    if (changed && (!overlay_font_known || cached_overlay.family != overlay.family ||
                    cached_overlay.charset != overlay.charset || cached_overlay.weight != overlay.weight ||
                    cached_overlay.italic != overlay.italic)) {
        overlay_native_font = CaptionFont(overlay).native(overlay);
        overlay_font_known = true;
    }
    if (changed && overlay_native_font) {
        const auto size = CaptionFont(overlay).measure(overlay.text);
        overlay_text_width = static_cast<float>(size.cx);
        overlay_text_height = static_cast<float>(size.cy);
        overlay_layout.Reset();
        overlay_mask.clear();
        overlay_mask_bitmap.Reset();
        cached_overlay = overlay;
        ++overlay_layout_builds;
    } else if (changed || (!overlay_native_font && !overlay_layout)) {
        if (!text_factory)
            verify(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown **>(text_factory.GetAddressOf())));
        ComPtr<IDWriteTextFormat> format;
        verify(text_factory->CreateTextFormat(
            overlay.family.c_str(), nullptr, static_cast<DWRITE_FONT_WEIGHT>(overlay.weight),
            overlay.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            overlay.font_size, L"ja-jp", &format));
        verify(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
        overlay_layout.Reset();
        verify(text_factory->CreateTextLayout(overlay.text.c_str(), static_cast<UINT32>(overlay.text.size()),
                                              format.Get(), 100000000.0f,
                                              std::max(1.0f, overlay.font_size * 4), &overlay_layout));
        const DWRITE_TEXT_RANGE range{0, static_cast<UINT32>(overlay.text.size())};
        verify(overlay_layout->SetUnderline(overlay.underline, range));
        verify(overlay_layout->SetStrikethrough(overlay.strikeout, range));
        DWRITE_TEXT_METRICS metrics{};
        verify(overlay_layout->GetMetrics(&metrics));
        overlay_text_width = metrics.widthIncludingTrailingWhitespace;
        overlay_text_height = metrics.height;
        std::vector<uint32_t>().swap(overlay_mask);
        overlay_mask_bitmap.Reset();
        cached_overlay = overlay;
        ++overlay_layout_builds;
    }
    const float pad = std::max(1.0f, 2 * overlay.dpi / 96);
    overlay_rect = place_overlay({0, top, static_cast<int>(view.width), static_cast<int>(view.height)},
                                 static_cast<int>(std::min(double(INT_MAX / 4), std::ceil(double(overlay_text_width) + 2 * pad))),
                                 static_cast<int>(std::min(double(INT_MAX / 4), std::ceil(double(overlay_text_height) + 2 * pad))), overlay.position,
                                 static_cast<int>(std::ceil(4 * overlay.dpi / 96)));
    const auto box = D2D1::RectF(static_cast<float>(overlay_rect.x), static_cast<float>(overlay_rect.y),
                                 static_cast<float>(overlay_rect.x + overlay_rect.w),
                                 static_cast<float>(overlay_rect.y + overlay_rect.h));
    context->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
    if (overlay.background == 1) {
        drawing_brush->SetColor(D2D1::ColorF(overlay.backcolor));
        context->FillRectangle(box, drawing_brush);
    }
    // Long captions expose the filename at the tail instead of clipping it away behind the path.
    const float x = overlay_text_width > overlay_rect.w - 2 * pad || overlay.position % 3 == 2
                        ? box.right - pad - overlay_text_width
                    : overlay.position % 3 == 1 ? (box.left + box.right - overlay_text_width) / 2
                                                : box.left + pad;
    const float y = box.top + pad;
    if (overlay_native_font) {
        if (overlay_mask.empty() || overlay_mask_size.cx != overlay_rect.w || overlay_mask_size.cy != overlay_rect.h) {
            overlay_mask = CaptionFont(overlay).mask(overlay.text, overlay_rect.w, overlay_rect.h,
                                                     static_cast<int>(std::round(x - box.left)),
                                                     static_cast<int>(std::round(pad)));
            overlay_mask_size = {overlay_rect.w, overlay_rect.h};
            overlay_mask_bitmap.Reset();
        }
        ComPtr<ID2D1Bitmap> mask;
        if (context == target.Get() && overlay_mask_bitmap)
            mask = overlay_mask_bitmap;
        else {
            verify(context->CreateBitmap(D2D1::SizeU(overlay_rect.w, overlay_rect.h), overlay_mask.data(),
                                         overlay_rect.w * 4,
                                         D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                                  D2D1_ALPHA_MODE_PREMULTIPLIED)), &mask));
            if (context == target.Get()) overlay_mask_bitmap = mask;
        }
        const auto aa = context->GetAntialiasMode();
        context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        const auto source = D2D1::RectF(0, 0, static_cast<float>(overlay_rect.w), static_cast<float>(overlay_rect.h));
        if (overlay.background == 2) {
            const float shift = std::max(1.0f, overlay.dpi / 96);
            const auto shadow = D2D1::RectF(box.left + shift, box.top + shift, box.right + shift, box.bottom + shift);
            drawing_brush->SetColor(D2D1::ColorF(overlay.backcolor));
            context->FillOpacityMask(mask.Get(), drawing_brush, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, shadow, source);
        }
        drawing_brush->SetColor(D2D1::ColorF(overlay.foreground));
        context->FillOpacityMask(mask.Get(), drawing_brush, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, box, source);
        context->SetAntialiasMode(aa);
        context->PopAxisAlignedClip();
        context->SetTextAntialiasMode(antialias);
        return;
    }
    if (overlay.background == 2) {
        const float shadow = std::max(1.0f, overlay.dpi / 96);
        drawing_brush->SetColor(D2D1::ColorF(overlay.backcolor));
        context->DrawTextLayout(D2D1::Point2F(x + shadow, y + shadow), overlay_layout.Get(), drawing_brush);
    }
    drawing_brush->SetColor(D2D1::ColorF(overlay.foreground));
    context->DrawTextLayout(D2D1::Point2F(x, y), overlay_layout.Get(), drawing_brush);
    context->PopAxisAlignedClip();
    context->SetTextAntialiasMode(antialias);
}
HRESULT Renderer::print(HWND hwnd, HDC dc, const Image *image, size_t frame, const View &view, int top,
                        const Rect *selection) {
    ensure(hwnd);
    ComPtr<ID2D1DCRenderTarget> context;
    const auto properties =
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                     D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                                     96, 96, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
    verify(factory->CreateDCRenderTarget(&properties, &context));
    RECT client{};
    GetClientRect(hwnd, &client);
    verify(context->BindDC(dc, &client));
    ComPtr<ID2D1Bitmap> pixels;
    const bool filtered = prepare_sample(image, frame, view, context->GetMaximumBitmapSize());
    if (filtered)
        pixels = tile_bitmap(context.Get());
    else if (image) {
        prepare_image(image, frame);
        verify(context->CreateBitmap(D2D1::SizeU(image->width, image->height), premultiplied.data(),
                                     image->width * 4,
                                     D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                              D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                     &pixels));
    }
    auto viewport_pattern = background_tiled ? checker_for(context.Get(), true) : nullptr;
    auto pattern = image && checkerboard ? checker_for(context.Get(), false) : nullptr;
    ComPtr<ID2D1SolidColorBrush> drawing_brush;
    verify(context->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &drawing_brush));
    return scene(context.Get(), pixels.Get(), pattern.Get(), viewport_pattern.Get(), drawing_brush.Get(), image, view, top,
                 selection);
}
} // namespace gvn
