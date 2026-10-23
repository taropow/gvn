#include "gvn/transfer.h"
#include "test.h"
#include "test_desktop.h"
#include <shellapi.h>
#include <cstring>
using namespace gvn;
const auto fixtures = std::filesystem::path(GVN_FIXTURES);
TEST(dib_alpha_roundtrip) {
    const auto image = load_image(fixtures / L"alpha.png");
    const auto data = make_dibv5(image, 0);
    auto result = read_dib(data);
    CHECK(result.frames[0].pixels == image.frames[0].pixels);
    auto flipped = data;
    auto *header = reinterpret_cast<BITMAPV5HEADER *>(flipped.data());
    header->bV5Height = -header->bV5Height;
    for (size_t x = 0; x < 12; ++x)
        std::swap(flipped[124 + x], flipped[136 + x]);
    CHECK(read_dib(flipped).frames[0].pixels == image.frames[0].pixels);
}
TEST(dib_legacy_opaque_and_corrupt) {
    const auto image = load_image(fixtures / L"alpha.png");
    auto data = make_dibv5(image, 0);
    BITMAPINFOHEADER header{40, 3, -2, 1, 32, BI_RGB, 24, 0, 0, 0, 0};
    std::vector<uint8_t> legacy(64);
    memcpy(legacy.data(), &header, 40);
    memcpy(legacy.data() + 40, data.data() + 124, 24);
    auto read = read_dib(legacy);
    for (auto p : read.frames[0].pixels)
        CHECK(p.a == 255);
    for (size_t n : {0u, 8u, 39u, 60u, 147u}) {
        bool failed{};
        try {
            read_dib(std::span(data).first(n));
        } catch (...) {
            failed = true;
        }
        CHECK(failed);
    }
    reinterpret_cast<BITMAPV5HEADER *>(data.data())->bV5GreenMask = 0xff0000;
    bool failed{};
    try {
        read_dib(data);
    } catch (...) {
        failed = true;
    }
    CHECK(failed);
}
TEST(dib_csp_v5_duplicate_masks_preserve_all_rgba_and_row_positions) {
    const auto image = load_image(fixtures / L"alpha.png");
    auto data = make_dibv5(image, 0);
    std::vector<uint8_t> masks(data.begin() + 40, data.begin() + 52);
    data.insert(data.begin() + 124, masks.begin(), masks.end());
    CHECK(read_dib(data).frames[0].pixels == image.frames[0].pixels);
    auto *header = reinterpret_cast<BITMAPV5HEADER *>(data.data());
    header->bV5Height = -header->bV5Height;
    const auto stride = image.width * 4;
    for (size_t x = 0; x < stride; ++x)
        std::swap(data[136 + x], data[136 + stride + x]);
    CHECK(read_dib(data).frames[0].pixels == image.frames[0].pixels);
    // A canonical V5 whose first three pixels equal RGB masks is still ordinary pixel data.
    Image canonical;
    canonical.width = 3;
    canonical.height = 1;
    canonical.frames.push_back({{{255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}}, 0});
    CHECK(read_dib(make_dibv5(canonical, 0)).frames[0].pixels == canonical.frames[0].pixels);
}
TEST(dib_standard32_all_alpha_values_and_opaque_legacy_rows) {
    Image image; image.width = 257; image.height = 7; image.frames.resize(1);
    auto &pixels = image.frames[0].pixels; pixels.resize(image.width * image.height);
    for (size_t n = 0; n < pixels.size(); ++n)
        pixels[n] = {uint8_t(n * 19), uint8_t(n * 71 + 3), uint8_t(n * 131 + 5), uint8_t(n)};
    const auto original = make_dibv5(image, 0);
    for (bool bottom_up : {false, true}) for (bool csp : {false, true}) {
        auto data = original;
        if (bottom_up) {
            reinterpret_cast<BITMAPV5HEADER *>(data.data())->bV5Height = image.height;
            const size_t stride = image.width * 4;
            for (size_t y = 0; y < image.height / 2; ++y)
                for (size_t x = 0; x < stride; ++x)
                    std::swap(data[124 + y * stride + x],data[124 + (image.height - 1 - y) * stride + x]);
        }
        if (csp) {
            const std::vector<uint8_t> masks(data.begin() + 40,data.begin() + 52);
            data.insert(data.begin() + 124,masks.begin(),masks.end());
        }
        const auto result = read_dib(data);
        CHECK(result.width == image.width && result.height == image.height && result.frames[0].pixels == pixels);
        auto opaque = data;
        reinterpret_cast<BITMAPV5HEADER *>(opaque.data())->bV5AlphaMask = 0;
        auto expected = pixels; for (auto &p : expected) p.a = 255;
        CHECK(read_dib(opaque).frames[0].pixels == expected);
        // The CSP prefix has no marker: a one-byte-short CSP payload can also
        // be a canonical V5 with trailing padding. Test unambiguous truncation.
        THROWS(read_dib(std::span(data).first(original.size() - 1)));
    }
}
TEST(dib_nonstandard32_masks_keep_generic_scaling) {
    for (bool bottom_up : {false, true}) for (bool ten_bit : {false, true}) {
        BITMAPV5HEADER header{}; header.bV5Size = sizeof(header); header.bV5Width = 7;
        header.bV5Height = bottom_up ? 3 : -3; header.bV5Planes = 1; header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask = ten_bit ? 0x3ff00000 : 0xff;
        header.bV5GreenMask = ten_bit ? 0x000ffc00 : 0xff00;
        header.bV5BlueMask = ten_bit ? 0x000003ff : 0xff0000;
        header.bV5AlphaMask = ten_bit ? 0xc0000000 : 0xff000000;
        std::vector<uint8_t> data(sizeof(header) + 7 * 3 * 4); memcpy(data.data(),&header,sizeof(header));
        std::vector<Pixel> expected(21);
        for (unsigned y = 0; y < 3; ++y) for (unsigned x = 0; x < 7; ++x) {
            const unsigned n = y * 7 + x;
            const unsigned maximum = ten_bit ? 1023 : 255;
            const unsigned r = n * 47 % (maximum + 1), g = n * 197 % (maximum + 1), b = n * 23 % (maximum + 1);
            const unsigned a = n % (ten_bit ? 4 : 256);
            const uint32_t value = ten_bit ? (r << 20) | (g << 10) | b | (a << 30) : r | (g << 8) | (b << 16) | (a << 24);
            memcpy(data.data() + sizeof(header) + ((bottom_up ? 2 - y : y) * 7 + x) * 4,&value,4);
            const auto channel = [maximum](unsigned v) { return uint8_t((v * 255 + maximum / 2) / maximum); };
            expected[n] = {channel(r),channel(g),channel(b),uint8_t(ten_bit ? a * 85 : a)};
        }
        CHECK(read_dib(data).frames[0].pixels == expected);
    }
}
TEST(shell_file_and_export) {
    auto image = load_image(fixtures / L"alpha.png");
    CHECK(std::filesystem::equivalent(drag_path(image, 0, false), image.path));
    const auto path = drag_path(image, 0, true);
    CHECK(path.extension() == L".png");
    CHECK(load_image(path).frames[0].pixels == image.frames[0].pixels);
    auto data = file_data_object(path);
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    CHECK(data->QueryGetData(&format) == S_OK);
    STGMEDIUM medium{};
    CHECK(SUCCEEDED(data->GetData(&format, &medium)));
    auto drop = static_cast<HDROP>(medium.hGlobal);
    CHECK(DragQueryFileW(drop, 0xffffffff, nullptr, 0) == 1);
    wchar_t file[32768]{};
    CHECK(DragQueryFileW(drop, 0, file, 32768) > 0);
    CHECK(std::filesystem::equivalent(path, file));
    ReleaseStgMedium(&medium);
    std::filesystem::remove(path);
}
void clipboard_encoded_formats_and_file_references(HWND owner) {
    const auto source = load_image(fixtures / L"alpha.png");
    auto publish = [&](UINT format, const std::vector<uint8_t> &bytes, bool clear = true) {
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
        CHECK(memory);
        auto data = GlobalLock(memory);
        CHECK(data);
        memcpy(data, bytes.data(), bytes.size());
        GlobalUnlock(memory);
        CHECK(OpenClipboard(owner));
        if (clear)
            CHECK(EmptyClipboard());
        const bool published = SetClipboardData(format, memory) != nullptr;
        CloseClipboard();
        if (!published)
            GlobalFree(memory);
        CHECK(published);
    };
    for (const auto &[name, ext] :
         std::vector<std::pair<std::wstring, std::wstring>>{{L"PNG", L".png"},
                                                            {L"image/png", L".png"},
                                                            {L"TIFF", L".tiff"},
                                                            {L"image/tiff", L".tiff"},
                                                            {L"GIF", L".gif"},
                                                            {L"image/gif", L".gif"},
                                                            {L"BMP", L".bmp"},
                                                            {L"image/bmp", L".bmp"},
                                                            {L"JPEG", L".jpg"},
                                                            {L"JFIF", L".jpg"},
                                                            {L"image/jpeg", L".jpg"},
                                                            {L"CF_TIFF", L".tiff"}}) {
        const auto bytes =
            ext == L".tiff" ? read_file(fixtures / L"clipboard/alpha.tiff") : encode(source, 0, ext);
        publish(name == L"CF_TIFF" ? CF_TIFF : RegisterClipboardFormatW(name.c_str()), bytes);
        const auto actual = paste_image(owner), expected = decode(bytes, ext);
        CHECK(actual.path.empty() && actual.frames[0].pixels == expected.frames[0].pixels);
        if (ext == L".png" || ext == L".tiff")
            CHECK(actual.frames[0].pixels == source.frames[0].pixels);
    }
    copy_image(owner, source, 0);
    publish(RegisterClipboardFormatW(L"PNG"), {1, 2, 3}, false);
    CHECK(paste_image(owner).frames[0].pixels == source.frames[0].pixels);
    const auto path = std::filesystem::absolute(fixtures / L"alpha.png");
    const auto text = L" \r\n\"" + path.wstring() + L"\"\r\n";
    std::vector<uint8_t> text_bytes((text.size() + 1) * sizeof(wchar_t));
    memcpy(text_bytes.data(), text.c_str(), text_bytes.size());
    publish(CF_UNICODETEXT, text_bytes);
    CHECK(std::get<std::filesystem::path>(paste_clipboard(owner)) == path);
    auto object = file_data_object(path);
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    CHECK(SUCCEEDED(object->GetData(&format, &medium)));
    const auto size = GlobalSize(medium.hGlobal);
    auto memory = GlobalLock(medium.hGlobal);
    CHECK(memory);
    std::vector<uint8_t> drop_bytes(static_cast<uint8_t *>(memory), static_cast<uint8_t *>(memory) + size);
    GlobalUnlock(medium.hGlobal);
    ReleaseStgMedium(&medium);
    publish(CF_HDROP, drop_bytes);
    CHECK(std::get<std::filesystem::path>(paste_clipboard(owner)) == path);
    copy_image(owner, source, 0);
    publish(CF_HDROP, drop_bytes, false);
    CHECK(std::get<Image>(paste_clipboard(owner)).frames[0].pixels == source.frames[0].pixels);
    // Borrowed clipboard spans must unlock on both a failed preferred decoder
    // and a successful DIB fallback. Clipboard memory remains owned by Windows.
    publish(CF_DIBV5,make_dibv5(source,0));
    const auto png_format=RegisterClipboardFormatW(L"PNG");
    publish(png_format,std::vector<uint8_t>{1,2,3},false);
    CHECK(paste_image(owner).frames[0].pixels==source.frames[0].pixels);
    CHECK(OpenClipboard(owner));
    const auto png_locks=GlobalFlags(GetClipboardData(png_format));
    const auto dib_locks=GlobalFlags(GetClipboardData(CF_DIBV5));
    CHECK(CloseClipboard());
    CHECK(png_locks!=GMEM_INVALID_HANDLE&&dib_locks!=GMEM_INVALID_HANDLE);
    CHECK((png_locks&GMEM_LOCKCOUNT)==0&&(dib_locks&GMEM_LOCKCOUNT)==0);
    // Reject malformed text without reading beyond HGLOBAL bounds.
    for (const auto &bytes : {std::vector<uint8_t>{'C', 0, ':', 0}, std::vector<uint8_t>{0, 0, 1}}) {
        publish(CF_UNICODETEXT, bytes);
        THROWS(paste_clipboard(owner));
    }
}
int main(int argc, char **) {
    if (argc > 1) {
        gvn_test::ClipboardLock clipboard_lock;
        if (!clipboard_lock)
            return 2;
        // A private station has its own clipboard: never overwrite the user's clipboard during tests.
        HWINSTA station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        if (!station || !gvn_test::noninteractive(station) || !SetProcessWindowStation(station)) {
            std::cerr << "Cannot isolate clipboard\n";
            return 2;
        }
        HDESK desktop = CreateDesktopW(L"GVN-test", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        if (!desktop || !SetThreadDesktop(desktop))
            return 3;
        OleInitialize(nullptr);
        HWND owner = CreateWindowExW(0, L"STATIC", L"GVN clipboard test", 0, 0, 0, 1, 1, nullptr, nullptr,
                                     nullptr, nullptr);
        if (!owner)
            return 4;
        try {
            const auto image = load_image(fixtures / L"alpha.png");
            copy_image(owner, image, 0);
            auto pasted = paste_image(owner);
            if (pasted.frames[0].pixels != image.frames[0].pixels)
                return 5;
            const auto bytes = make_dibv5(image, 0);
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
            auto ptr = GlobalLock(memory);
            memcpy(ptr, bytes.data(), bytes.size());
            GlobalUnlock(memory);
            if (!OpenClipboard(owner))
                return 6;
            EmptyClipboard();
            SetClipboardData(CF_DIBV5, memory);
            CloseClipboard();
            pasted = paste_image(owner);
            if (pasted.frames[0].pixels != image.frames[0].pixels)
                return 7;
            clipboard_encoded_formats_and_file_references(owner);
            DestroyWindow(owner);
            OleUninitialize();
            std::cout << "PNG + DIBV5, 12 encoded aliases, fallback, full paths and CF_HDROP isolated "
                         "clipboard passed\n";
            return 0;
        } catch (const std::exception &e) {
            std::cerr << e.what() << "\n";
            return 8;
        }
    }
    OleInitialize(nullptr);
    const auto result = run_tests();
    OleUninitialize();
    return result;
}
