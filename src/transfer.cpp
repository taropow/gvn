#include "gvn/transfer.h"
#include <shlobj.h>
#include <shlguid.h>
#include <bit>
#include <cstring>
#include "pixel_conversion.h"

using Microsoft::WRL::ComPtr;
namespace gvn {
namespace {
void require(bool ok, const char *error) {
    if (!ok)
        throw std::runtime_error(error);
}
uint16_t u16(std::span<const uint8_t> d, size_t p) {
    require(p <= d.size() && d.size() - p >= 2, "Truncated DIB");
    return static_cast<uint16_t>(d[p] | d[p + 1] << 8);
}
uint32_t u32(std::span<const uint8_t> d, size_t p) {
    return u16(d, p) | static_cast<uint32_t>(u16(d, p + 2)) << 16;
}
struct Clipboard {
    explicit Clipboard(HWND owner) {
        require(OpenClipboard(owner) != 0, "Clipboard is busy; try again");
    }
    ~Clipboard() {
        CloseClipboard();
    }
};
struct Global {
    HGLOBAL value{};uint8_t *data{};size_t size{};
    explicit Global(size_t bytes):size(bytes) {
        value=GlobalAlloc(GMEM_MOVEABLE,bytes);require(value!=nullptr,"Clipboard allocation failed");
        data=static_cast<uint8_t *>(GlobalLock(value));
        if(!data){GlobalFree(value);throw std::runtime_error("Clipboard allocation failed");}
    }
    explicit Global(std::span<const uint8_t> bytes):Global(bytes.size()){std::memcpy(data,bytes.data(),bytes.size());}
    Global(const Global &)=delete;
    ~Global(){if(data)GlobalUnlock(value);if(value)GlobalFree(value);}
    std::span<uint8_t> writable(){return {data,size};}
    void publish(UINT format){
        if(data){GlobalUnlock(value);data=nullptr;}
        require(SetClipboardData(format,value)!=nullptr,"Cannot publish clipboard image");value=nullptr;
    }
};
// The caller holds Clipboard open for this entire lifetime. Decoders consume
// borrowed spans synchronously, so a second full encoded-image allocation is unnecessary.
class ClipboardData {
    HGLOBAL handle{};
    const uint8_t *data{};
    size_t length{};
public:
    explicit ClipboardData(UINT format) {
        handle = GetClipboardData(format);
        if (!handle) return;
        length = GlobalSize(handle);
        require(length <= 512ull * 1024 * 1024, "Clipboard image exceeds limit");
        data = static_cast<const uint8_t *>(GlobalLock(handle));
        if (!data) length = 0;
    }
    ClipboardData(const ClipboardData &) = delete;
    ClipboardData &operator=(const ClipboardData &) = delete;
    ~ClipboardData() { if (data) GlobalUnlock(handle); }
    std::span<const uint8_t> bytes() const { return {data, length}; }
};
std::filesystem::path clipboard_file() {
    auto valid = [](const std::filesystem::path &path) { return path.is_absolute() && supported(path); };
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        const auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
        const auto count = std::min(DragQueryFileW(drop, 0xffffffff, nullptr, 0), 4096u);
        for (UINT i = 0; i < count; ++i) {
            const auto length = DragQueryFileW(drop, i, nullptr, 0);
            if (!length || length >= 32768)
                continue;
            std::wstring text(length + 1, L'\0');
            if (!DragQueryFileW(drop, i, text.data(), length + 1))
                continue;
            text.resize(length);
            if (valid(text))
                return text;
        }
    }
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        const auto handle = GetClipboardData(CF_UNICODETEXT);
        const auto length = GlobalSize(handle);
        if (!length || length > 128 * 1024 || length % sizeof(wchar_t))
            return {};
        const ClipboardData memory(CF_UNICODETEXT);
        const auto data = memory.bytes();
        std::wstring text(data.size() / sizeof(wchar_t), L'\0');
        memcpy(text.data(), data.data(), data.size());
        const auto end = text.find(L'\0');
        if (end == std::wstring::npos)
            return {};
        text.resize(end);
        const auto first = text.find_first_not_of(L" \t\r\n");
        if (first == std::wstring::npos)
            return {};
        text = text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
        if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"')
            text = text.substr(1, text.size() - 2);
        if (text.find_first_of(L"\r\n\t\"") == std::wstring::npos && valid(text))
            return text;
    }
    return {};
}
} // namespace
static void fill_dibv5(std::span<uint8_t> result,const Image &image,size_t frame) {
    const size_t count = checked_image_pixels(image.width, image.height);
    const auto &pixels = image.frames.at(frame).pixels;
    require(pixels.size() == count, "Invalid frame");
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = static_cast<LONG>(image.width);
    header.bV5Height = -static_cast<LONG>(image.height);
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5SizeImage = static_cast<DWORD>(count * 4);
    header.bV5RedMask = 0x00ff0000;
    header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff;
    header.bV5AlphaMask = 0xff000000;
    header.bV5CSType = LCS_sRGB;
    header.bV5Intent = LCS_GM_IMAGES;
    require(result.size()==sizeof(header)+count*4,"Invalid DIB output buffer");
    memcpy(result.data(), &header, sizeof(header));
    detail::swizzle_rgba(reinterpret_cast<const uint8_t *>(pixels.data()),result.data()+sizeof(header),count);
}
std::vector<uint8_t> make_dibv5(const Image &image,size_t frame) {
    std::vector<uint8_t> result(sizeof(BITMAPV5HEADER)+checked_image_pixels(image.width,image.height)*4);
    fill_dibv5(result,image,frame);return result;
}
Image read_dib(std::span<const uint8_t> data) {
    const uint32_t header = u32(data, 0);
    require(header >= 40 && header <= 124 && header <= data.size(), "Unsupported DIB header");
    const int32_t w = static_cast<int32_t>(u32(data, 4)), h = static_cast<int32_t>(u32(data, 8));
    require(w > 0 && h != INT_MIN && h != 0 && u16(data, 12) == 1, "Invalid DIB dimensions");
    const uint16_t bits = u16(data, 14);
    const uint32_t compression = u32(data, 16);
    require(bits == 1 || bits == 4 || bits == 8 || bits == 16 || bits == 24 || bits == 32,
            "Unsupported DIB pixel depth");
    require(compression == BI_RGB || (compression == BI_BITFIELDS && (bits == 16 || bits == 32)),
            "Unsupported compressed DIB");
    const uint32_t height = static_cast<uint32_t>(h < 0 ? -h : h);
    const size_t count = checked_pixels(w, height);
    uint32_t masks[4]{bits == 16 ? 0x7c00u : 0xff0000u, bits == 16 ? 0x3e0u : 0xff00u,
                      bits == 16 ? 0x1fu : 0xffu, 0};
    size_t offset = header;
    if (compression == BI_BITFIELDS) {
        if (header >= 56) {
            for (int i = 0; i < 4; ++i)
                masks[i] = u32(data, 40 + i * 4);
        } else {
            require(header == 40, "Invalid bitfield header");
            for (int i = 0; i < 3; ++i)
                masks[i] = u32(data, offset + i * 4);
            offset += 12;
        }
    } else if (bits == 32 && header >= 56 && u32(data, 52)) {
        for (int i = 0; i < 4; ++i)
            masks[i] = u32(data, 40 + i * 4);
    }
    if (bits == 16 || bits == 32) {
        uint32_t used{};
        for (int i = 0; i < 4; ++i) {
            const auto mask = masks[i];
            if (i < 3)
                require(mask != 0, "Missing color mask");
            if (!mask)
                continue;
            const auto shifted = mask >> std::countr_zero(mask);
            require((shifted & (shifted + 1)) == 0 && (used & mask) == 0, "Invalid color masks");
            if (bits == 16)
                require(mask <= 0xffff, "Invalid 16-bit mask");
            used |= mask;
        }
    }
    const uint32_t colors = bits <= 8 ? (u32(data, 32) ? u32(data, 32) : 1u << bits) : u32(data, 32);
    require(colors <= 256, "Invalid DIB palette");
    const size_t palette = offset;
    offset += static_cast<size_t>(colors) * 4;
    const size_t stride = ((static_cast<size_t>(w) * bits + 31) / 32) * 4;
    // CSP 5.0.1 publishes a V5 header with the three RGB masks repeated before the pixels.
    // Accept only the observed, exact-size layout; ordinary V5 pixels can have the same RGB values.
    if (header == 124 && bits == 32 && compression == BI_BITFIELDS && colors == 0 &&
        u32(data, 20) == stride * height && u32(data, 112) == 0 && u32(data, 116) == 0 &&
        data.size() == offset + 12 + stride * height && u32(data, offset) == masks[0] &&
        u32(data, offset + 4) == masks[1] && u32(data, offset + 8) == masks[2])
        offset += 12;
    require(offset <= data.size() && stride * height <= data.size() - offset, "Truncated DIB pixels");
    auto component = [](uint32_t value, uint32_t mask, uint8_t fallback) {
        if (!mask)
            return fallback;
        const int shift = std::countr_zero(mask);
        const uint64_t maximum = mask >> shift;
        return static_cast<uint8_t>((((value & mask) >> shift) * 255ull + maximum / 2) / maximum);
    };
    Image image;
    image.width = w;
    image.height = height;
    image.format = L"Clipboard DIB";
    image.metadata.original_width=image.width;image.metadata.original_height=image.height;image.metadata.original_bpp=bits;
    image.metadata.original_color_type=bits<=8?ImageColorType::Palette:compression==BI_BITFIELDS?ImageColorType::Bitfields:ImageColorType::Rgb;
    image.frames.resize(1);
    auto &pixels = image.frames[0].pixels;
    pixels.resize(count);
    // Common Windows/CSP BGRA/BGRX: header, masks, palette and full byte range
    // have already been validated. Keep straight alpha and transparent RGB intact.
    if (bits == 32 && masks[0] == 0xff0000 && masks[1] == 0xff00 && masks[2] == 0xff &&
        (masks[3] == 0 || masks[3] == 0xff000000)) {
        const bool alpha = masks[3] != 0;
        for (size_t y = 0; y < height; ++y) {
            const auto *source = data.data() + offset + (h > 0 ? height - 1 - y : y) * stride;
            auto *target = pixels.data() + y * w;
            for (size_t x = 0; x < static_cast<size_t>(w); ++x)
                target[x] = {source[x * 4 + 2], source[x * 4 + 1], source[x * 4],
                             alpha ? source[x * 4 + 3] : uint8_t(255)};
        }
        return image;
    }
    for (size_t y = 0; y < height; ++y) {
        const auto row = data.subspan(offset + (h > 0 ? height - 1 - y : y) * stride, stride);
        for (size_t x = 0; x < static_cast<size_t>(w); ++x) {
            Pixel p{};
            if (bits <= 8) {
                const uint32_t index = bits == 8   ? row[x]
                                       : bits == 4 ? ((row[x / 2] >> ((1 - x % 2) * 4)) & 15)
                                                   : ((row[x / 8] >> (7 - x % 8)) & 1);
                require(index < colors, "DIB palette index out of range");
                p = {data[palette + index * 4 + 2], data[palette + index * 4 + 1], data[palette + index * 4],
                     255};
            } else if (bits == 24) {
                p = {row[x * 3 + 2], row[x * 3 + 1], row[x * 3], 255};
            } else {
                const uint32_t value = bits == 16 ? u16(row, x * 2) : u32(row, x * 4);
                p = {component(value, masks[0], 0), component(value, masks[1], 0),
                     component(value, masks[2], 0), component(value, masks[3], 255)};
            }
            pixels[y * w + x] = p;
        }
    }
    return image;
}
void copy_image(HWND owner, const Image &image, size_t frame,std::optional<SelectionOrigin> origin,const ImageMetadata *metadata) {
    PngSaveOptions options;options.compression=1; // Lossless interchange, prioritize command latency.
    const auto png = encode_png(image,frame,options,metadata);
    Global png_memory(png), dib_memory(sizeof(BITMAPV5HEADER)+checked_image_pixels(image.width,image.height)*4);
    fill_dibv5(dib_memory.writable(),image,frame);
    std::unique_ptr<Global> positioned;
    UINT position_format{};
    if(origin){
        const auto header=selection_clipboard_header(png.size(),image.width,image.height,*origin);
        positioned=std::make_unique<Global>(header.size()+png.size());const auto bytes=positioned->writable();
        std::copy(header.begin(),header.end(),bytes.begin());std::copy(png.begin(),png.end(),bytes.begin()+header.size());
        position_format=RegisterClipboardFormatW(selection_clipboard_format);require(position_format!=0,"Cannot register selection format");}
    const UINT png_format = RegisterClipboardFormatW(L"PNG");
    require(png_format != 0, "Cannot register PNG clipboard format");
    Clipboard clipboard(owner);
    require(EmptyClipboard() != 0, "Cannot clear clipboard");
    png_memory.publish(png_format);
    dib_memory.publish(CF_DIBV5);
    if(positioned)positioned->publish(position_format);
}
static ClipboardContent read_open_clipboard(const std::function<void()> &decoding={}) {
    // Some producers publish a broken optional format. Continue to the next advertised representation.
    std::string error;
    struct Format {
        UINT id;
        bool dib;
    };
    const Format formats[] = {{RegisterClipboardFormatW(L"PNG"), false},
                              {RegisterClipboardFormatW(L"image/png"), false},
                              {CF_DIBV5, true},
                              {CF_TIFF, false},
                              {RegisterClipboardFormatW(L"TIFF"), false},
                              {RegisterClipboardFormatW(L"image/tiff"), false},
                              {RegisterClipboardFormatW(L"GIF"), false},
                              {RegisterClipboardFormatW(L"image/gif"), false},
                              {RegisterClipboardFormatW(L"BMP"), false},
                              {RegisterClipboardFormatW(L"image/bmp"), false},
                              {RegisterClipboardFormatW(L"JPEG"), false},
                              {RegisterClipboardFormatW(L"JFIF"), false},
                              {RegisterClipboardFormatW(L"image/jpeg"), false},
                              {CF_DIB, true}};
    for (const auto format : formats)
        if (format.id && IsClipboardFormatAvailable(format.id)) {
            try {
                const ClipboardData memory(format.id);
                const auto data = memory.bytes();
                if(!data.empty()) {
                    if(decoding)decoding();
                    auto image = format.dib ? read_dib(data) : decode(data, L"");
                    image.path.clear();
                    return image;
                }
            } catch (const std::exception &e) {
                error = e.what();
            }
        }
    if (IsClipboardFormatAvailable(CF_BITMAP)) {
        auto bitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
        BITMAP bm{};
        require(GetObjectW(bitmap, sizeof(bm), &bm) != 0, "Cannot read clipboard bitmap");
        const size_t count=checked_pixels(bm.bmWidth,bm.bmHeight);
        if(decoding)decoding();
        std::vector<uint8_t> data(sizeof(BITMAPINFOHEADER) + count * 4);
        auto *info = reinterpret_cast<BITMAPINFO *>(data.data());
        info->bmiHeader = {sizeof(BITMAPINFOHEADER),
                           bm.bmWidth,
                           -bm.bmHeight,
                           1,
                           32,
                           BI_RGB,
                           static_cast<DWORD>(count * 4),
                           0,
                           0,
                           0,
                           0};
        HDC dc = GetDC(nullptr);
        const int lines = GetDIBits(dc, bitmap, 0, bm.bmHeight, data.data() + sizeof(BITMAPINFOHEADER), info,
                                    DIB_RGB_COLORS);
        ReleaseDC(nullptr, dc);
        require(lines == bm.bmHeight, "Cannot read clipboard bitmap pixels");
        return read_dib(data);
    }
    if(const auto path=clipboard_file();!path.empty()){
        if(decoding)decoding();return path;
    }
    throw std::runtime_error(error.empty() ? "Clipboard does not contain an image or a supported image path"
                                           : error);
}
ClipboardContent paste_clipboard(HWND owner,const std::function<void()> &decoding) {
    Clipboard clipboard(owner);
    return read_open_clipboard(decoding);
}
Image paste_image(HWND owner) {
    auto content = paste_clipboard(owner);
    if (auto image = std::get_if<Image>(&content))
        return std::move(*image);
    throw std::runtime_error("Clipboard contains a file path rather than image pixels");
}
SelectionClipboard paste_selection_clipboard(HWND owner,const std::function<void()> &decoding) {
    bool invalid{};
    ClipboardContent content;
    {
        Clipboard clipboard(owner);
        const auto format=RegisterClipboardFormatW(selection_clipboard_format);
        if(format&&IsClipboardFormatAvailable(format))try {
            const ClipboardData memory(format);
            const auto data=memory.bytes();
            if(data.size()>=64&&memcmp(data.data(),"GVNSEL1",7)==0&&decoding)decoding();
            return decode_selection_clipboard(data);
        }catch(const std::exception &){invalid=true;}
        content=read_open_clipboard(decoding);
    }
    if(auto image=std::get_if<Image>(&content))return {std::move(*image),{},invalid};
    // A file reference remains image input for this explicit selection command.
    return {load_image(std::get<std::filesystem::path>(content)),{},invalid};
}
std::filesystem::path drag_path(const Image &image, size_t frame, bool modified,const ImageMetadata *metadata) {
    if (!modified && !image.animated() && !image.path.empty() && std::filesystem::is_regular_file(image.path))
        return std::filesystem::absolute(image.path);
    wchar_t temp[32768]{};
    require(GetTempPathW(32768, temp) > 0, "Cannot find temporary folder");
    const auto directory = std::filesystem::path(temp) / L"GVN" / L"drag";
    std::filesystem::create_directories(directory);
    GUID guid{};
    require(SUCCEEDED(CoCreateGuid(&guid)), "Cannot create temporary name");
    wchar_t name[64]{};
    StringFromGUID2(guid, name, 64);
    const auto path = directory / (L"GVN-" + std::wstring(name) + L".png");
    save_image(image, frame, path,0.93f,true,metadata);
    return path;
}
std::filesystem::path export_drag_path(const Image &image,size_t frame,const std::wstring &name,SaveFormat format,const SaveOptions &options,const ImageMetadata *metadata){
    std::wstring safe=name;
    for(auto &c:safe)if(c<32||std::wstring_view(L"<>:\"/\\|?*").find(c)!=std::wstring_view::npos)c=L'_';
    while(!safe.empty()&&(safe.back()==L'.'||safe.back()==L' '))safe.pop_back();
    if(safe.empty())safe=L"画像";if(safe.size()>120)safe.resize(120);
    auto base=safe.substr(0,safe.find(L'.'));std::transform(base.begin(),base.end(),base.begin(),[](wchar_t c){return wchar_t(towupper(c));});
    if(base==L"CON"||base==L"PRN"||base==L"AUX"||base==L"NUL"||(base.size()==4&&(base.starts_with(L"COM")||base.starts_with(L"LPT"))&&base[3]>=L'1'&&base[3]<=L'9'))safe=L"_"+safe;
    // Each export has its own directory; an application may open the dropped file later.
    wchar_t temp[32768]{},guid_text[64]{};GUID guid{};
    require(GetTempPathW(32768,temp)>0&&SUCCEEDED(CoCreateGuid(&guid)),"Cannot create drag export");StringFromGUID2(guid,guid_text,64);
    const auto directory=std::filesystem::path(temp)/L"GVN"/L"drag"/guid_text;std::filesystem::create_directories(directory);
    const auto ext=format==SaveFormat::Jpeg?L".jpg":format==SaveFormat::Bmp?L".bmp":format==SaveFormat::Gif?L".gif":L".png";
    auto target=directory/(safe+ext);auto configured=options;
    // PNG is the lossless alpha interchange choice, independent of the save dialog's matte settings.
    if(format==SaveFormat::Png)configured.png.transparency=PngTransparency::Alpha;
    save_image(image,frame,target,configured,false,metadata);return target;
}
ComPtr<IDataObject> file_data_object(const std::filesystem::path &path) {
    ComPtr<IShellItem> item;
    require(SUCCEEDED(SHCreateItemFromParsingName(std::filesystem::absolute(path).c_str(), nullptr,
                                                  IID_PPV_ARGS(&item))),
            "Cannot create shell file item");
    ComPtr<IDataObject> data;
    require(SUCCEEDED(item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data))),
            "Cannot create Explorer file drag data");
    return data;
}
class DropSource final : public IDropSource {
    LONG references{1};

  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDropSource) {
            *out = static_cast<IDropSource *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&references);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto count = InterlockedDecrement(&references);
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape)
            return DRAGDROP_S_CANCEL;
        if (!(keys & MK_LBUTTON))
            return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }
};
DWORD drag_file(const std::filesystem::path &path) {
    auto data = file_data_object(path);
    ComPtr<IDropSource> source;
    source.Attach(new DropSource);
    DWORD effect{};
    const auto result = DoDragDrop(data.Get(), source.Get(), DROPEFFECT_COPY, &effect);
    require(SUCCEEDED(result), "OLE drag failed");
    return result == DRAGDROP_S_DROP ? effect : 0;
}
} // namespace gvn
