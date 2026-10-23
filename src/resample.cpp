#include "gvn/resample.h"
#include "display_source.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <cstring>
namespace gvn {
namespace {
using Microsoft::WRL::ComPtr;
void verify(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("WIC display resampling failed");
}
// The scaler borrows the source synchronously and converts requested RGBA rows.
// No second full source image or full enlarged image is allocated.
class MemorySource final : public IWICBitmapSource {
    std::atomic<ULONG> references{1};
    detail::DisplaySource pixels;
    UINT width, height;

  public:
    explicit MemorySource(detail::DisplaySource data)
        : pixels(data), width(data.width), height(data.height) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(IWICBitmapSource))
            return E_NOINTERFACE;
        *out = static_cast<IWICBitmapSource *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++references;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references;
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE GetSize(UINT *w, UINT *h) override {
        if (!w || !h)
            return E_POINTER;
        *w = width;
        *h = height;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelFormat(WICPixelFormatGUID *format) override {
        if (!format)
            return E_POINTER;
        *format = GUID_WICPixelFormat32bppPBGRA;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetResolution(double *x, double *y) override {
        if (!x || !y)
            return E_POINTER;
        *x = *y = 96;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyPalette(IWICPalette *) override {
        return WINCODEC_ERR_PALETTEUNAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE CopyPixels(const WICRect *rect, UINT stride, UINT size, BYTE *out) override {
        if (!out)
            return E_POINTER;
        const WICRect area = rect ? *rect : WICRect{0, 0, static_cast<INT>(width), static_cast<INT>(height)};
        if (area.X < 0 || area.Y < 0 || area.Width <= 0 || area.Height <= 0 ||
            static_cast<uint64_t>(area.X) + area.Width > width ||
            static_cast<uint64_t>(area.Y) + area.Height > height ||
            stride < static_cast<uint64_t>(area.Width) * 4 ||
            static_cast<uint64_t>(stride) * (area.Height - 1) + area.Width * 4ull > size)
            return E_INVALIDARG;
        for (int y = 0; y < area.Height; ++y)
            pixels.copy_row(area.Y + y, area.X, area.Width, out + static_cast<size_t>(y) * stride);
        return S_OK;
    }
};
} // namespace
static DisplayTile resample_source(detail::DisplaySource source, const View &view, int method) {
    const auto width = source.width, height = source.height;
    if (source.bytes.size() != checked_image_pixels(width, height) * 4 || method < 0 ||
        method > kLastDisplayFilter || !std::isfinite(view.scale) || view.scale <= 0 || view.scale > 50 ||
        !std::isfinite(view.left()) || !std::isfinite(view.top()) || !std::isfinite(view.width) ||
        !std::isfinite(view.height) || view.width <= 0 || view.height <= 0)
        throw std::invalid_argument("Invalid display resampling input");
    const UINT w = static_cast<UINT>(std::max(1ll, std::llround(width * view.scale))),
               h = static_cast<UINT>(std::max(1ll, std::llround(height * view.scale)));
    const double sx = width * view.scale / w, sy = height * view.scale / h;
    const auto bound = [](double value, UINT limit, bool upper) {
        return static_cast<INT>(std::clamp(upper ? std::ceil(value) + 1 : std::floor(value) - 1, 0.0,
                                           static_cast<double>(limit)));
    };
    const int x0 = bound(-view.left() / sx, w, false), y0 = bound(-view.top() / sy, h, false),
              x1 = bound((view.width - view.left()) / sx, w, true),
              y1 = bound((view.height - view.top()) / sy, h, true);
    DisplayTile tile;
    if (x1 <= x0 || y1 <= y0)
        return tile;
    tile.width = x1 - x0;
    tile.height = y1 - y0;
    tile.left = view.left() + x0 * sx;
    tile.top = view.top() + y0 * sy;
    tile.right = view.left() + x1 * sx;
    tile.bottom = view.top() + y1 * sy;
    // A separate 64 MiB budget bounds the visible resampling cache.
    tile.bgra.resize(checked_pixels(tile.width, tile.height, 1, 64ull * 1024 * 1024) * 4);
    if (method <= DisplayLanczos3) {
        detail::resample_source_tile(source, w, h, x0, y0, method, tile);
        return tile;
    }
    ComPtr<IWICImagingFactory> factory;
    verify(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapSource> input;
    input.Attach(new MemorySource(source));
    ComPtr<IWICBitmapScaler> scaler;
    verify(factory->CreateBitmapScaler(&scaler));
    const auto wic_method = method == DisplayWicCubic  ? WICBitmapInterpolationModeCubic
                            : method == DisplayWicFant ? WICBitmapInterpolationModeFant
                                                       : WICBitmapInterpolationModeHighQualityCubic;
    verify(scaler->Initialize(input.Get(), w, h, wic_method));
    const WICRect area{x0, y0, x1 - x0, y1 - y0};
    verify(scaler->CopyPixels(&area, tile.width * 4, static_cast<UINT>(tile.bgra.size()), tile.bgra.data()));
    return tile;
}
DisplayTile resample_display(std::span<const uint8_t> data, uint32_t width, uint32_t height, const View &view,
                             int method) {
    return resample_source({data, width, height, false}, view, method);
}
DisplayTile resample_rgba_display(std::span<const Pixel> data, uint32_t width, uint32_t height,
                                  const View &view, int method) {
    return resample_source(
        {{reinterpret_cast<const uint8_t *>(data.data()), data.size_bytes()}, width, height, true}, view,
        method);
}
} // namespace gvn
