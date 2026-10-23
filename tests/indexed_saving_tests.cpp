#include "gvn/saving.h"
#include "gvn/codecs.h"
#include "test.h"
#include <windows.h>
#include <array>
#include <fstream>
#include <span>
using namespace gvn;
namespace {
uint32_t le32(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
IndexedRaster indexed(unsigned bits, uint32_t width = 37, uint32_t height = 23, bool repeats = false) {
    IndexedRaster raster{width, height, bits};
    for (unsigned i = 0; i < (1u << bits); ++i)
        raster.palette.push_back(
            {static_cast<uint8_t>(i * 37), static_cast<uint8_t>(i * 71), static_cast<uint8_t>(i * 109), 255});
    if (bits > 1)
        raster.palette[1] = raster.palette[0]; // Same RGB, different palette indices.
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            raster.indices.push_back(static_cast<uint8_t>((repeats ? x / 128 + y : x * 7 + y * 13 + x * y) %
                                                          raster.palette.size()));
    return raster;
}
Image rgba(const IndexedRaster &indexed) {
    Image image;
    image.width = indexed.width;
    image.height = indexed.height;
    image.frames.resize(1);
    for (auto index : indexed.indices)
        image.frames[0].pixels.push_back(indexed.palette[index]);
    return image;
}
Image gradient(uint32_t width = 67, uint32_t height = 41) {
    auto source = rgba(indexed(8, width, height));
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            source.frames[0].pixels[y * size_t(width) + x] = {
                static_cast<uint8_t>(x * 19 + y * 7), static_cast<uint8_t>(x * 5 + y * 11),
                static_cast<uint8_t>(x * 3 + y * 31), static_cast<uint8_t>(x * 13 + y * 17)};
    return source;
}
uint64_t error(const Image &a, const Image &b) {
    CHECK(a.width == b.width && a.height == b.height);
    uint64_t result{};
    for (size_t i = 0; i < a.frames[0].pixels.size(); ++i) {
        const auto x = a.frames[0].pixels[i], y = b.frames[0].pixels[i];
        for (const int delta : {int(x.r) - y.r, int(x.g) - y.g, int(x.b) - y.b})
            result += delta * delta;
    }
    return result;
}
struct Directory {
    const std::filesystem::path root = std::filesystem::path(GVN_SOURCE_ROOT) / L".cache/save-file-tests";
    std::filesystem::path path;
    Directory() {
        GUID guid{};
        CHECK(SUCCEEDED(CoCreateGuid(&guid)));
        wchar_t name[64]{};
        StringFromGUID2(guid, name, 64);
        path = root / (std::wstring(L"run-") + name);
        CHECK(!std::filesystem::exists(path));
        std::filesystem::create_directories(path);
    }
    ~Directory() {
        if (path.parent_path() == root && path.filename().wstring().starts_with(L"run-")) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
    void no_temporary_files() const {
        for (const auto &entry : std::filesystem::directory_iterator(path))
            CHECK(!entry.path().filename().wstring().ends_with(L".gvn-writing"));
    }
};
FILETIME time_value(uint64_t value) {
    return {static_cast<DWORD>(value), static_cast<DWORD>(value >> 32)};
}
WIN32_FILE_ATTRIBUTE_DATA file_times(const std::filesystem::path &path) {
    WIN32_FILE_ATTRIBUTE_DATA result{};
    CHECK(GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &result));
    return result;
}
void set_times(const std::filesystem::path &path, const FILETIME &time) {
    const auto handle =
        CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(handle != INVALID_HANDLE_VALUE);
    const auto ok = SetFileTime(handle, &time, &time, &time);
    CloseHandle(handle);
    CHECK(ok);
}
struct GifData {
    bool interlaced{};
    int transparent{-1};
    unsigned minimum_bits{};
    std::vector<uint8_t> compressed;
};
GifData gif_data(std::span<const uint8_t> bytes) {
    CHECK(bytes.size() >= 13 && std::string(reinterpret_cast<const char *>(bytes.data()), 6) == "GIF89a");
    GifData result;
    size_t offset = 13 + ((bytes[10] & 128) ? 3ull * (2u << (bytes[10] & 7)) : 0);
    while (true) {
        CHECK(offset < bytes.size());
        const auto marker = bytes[offset++];
        if (marker == 0x21) {
            CHECK(offset < bytes.size());
            const auto label = bytes[offset++];
            if (label == 0xf9) {
                CHECK(bytes.size() - offset >= 6 && bytes[offset] == 4);
                result.transparent = (bytes[offset + 1] & 1) ? bytes[offset + 4] : -1;
            }
            while (true) {
                CHECK(offset < bytes.size());
                const auto size = bytes[offset++];
                if (!size)
                    break;
                CHECK(size <= bytes.size() - offset);
                offset += size;
            }
        } else if (marker == 0x2c) {
            CHECK(bytes.size() - offset >= 10);
            const auto flags = bytes[offset + 8];
            result.interlaced = (flags & 64) != 0;
            offset += 9 + ((flags & 128) ? 3ull * (2u << (flags & 7)) : 0);
            CHECK(offset < bytes.size());
            result.minimum_bits = bytes[offset++];
            while (true) {
                CHECK(offset < bytes.size());
                const auto size = bytes[offset++];
                if (!size)
                    break;
                CHECK(size <= bytes.size() - offset);
                result.compressed.insert(result.compressed.end(), bytes.begin() + offset,
                                         bytes.begin() + offset + size);
                offset += size;
            }
            CHECK(offset + 1 == bytes.size() && bytes[offset] == 0x3b);
            return result;
        } else
            throw std::runtime_error("Unexpected GIF record");
    }
}
void check_gif(const IndexedRaster &source, const GifSaveOptions &options) {
    const auto bytes = encode_gif(source, options);
    const auto wire = gif_data(bytes);
    CHECK(wire.interlaced == options.interlaced);
    const auto loaded = decode(bytes);
    CHECK(loaded.width == source.width && loaded.height == source.height && loaded.frames.size() == 1);
    int transparent = -1;
    if (options.transparency == GifTransparency::Index)
        transparent = static_cast<int>(options.transparent_index);
    else if (options.transparency >= GifTransparency::TopLeft) {
        static constexpr unsigned x[]{0, 1, 0, 1}, y[]{0, 0, 1, 1};
        const auto corner = static_cast<unsigned>(options.transparency) - 2;
        transparent = source.indices[(source.height - 1ull) * y[corner] * source.width +
                                     (source.width - 1ull) * x[corner]];
    }
    CHECK(wire.transparent == transparent);
    for (size_t i = 0; i < source.indices.size(); ++i) {
        const auto expected = source.palette[source.indices[i]], actual = loaded.frames[0].pixels[i];
        CHECK(actual.a == (source.indices[i] == transparent ? 0 : 255));
        if (actual.a)
            CHECK(actual == expected);
    }
    if (options.uncompressed) {
        std::vector<uint8_t> actual;
        size_t bit{};
        const unsigned clear = 1u << wire.minimum_bits, code_bits = wire.minimum_bits + 1;
        bool ended{};
        unsigned clears{};
        while (bit + code_bits <= wire.compressed.size() * 8) {
            unsigned code{};
            for (unsigned i = 0; i < code_bits; ++i, ++bit)
                code |= ((wire.compressed[bit / 8] >> (bit % 8)) & 1) << i;
            if (code == clear + 1) {
                ended = true;
                break;
            }
            if (code == clear)
                ++clears;
            else {
                CHECK(code < clear); // A dictionary code would contradict uncompressed mode.
                actual.push_back(static_cast<uint8_t>(code));
            }
        }
        std::vector<uint8_t> expected;
        if (options.interlaced) {
            static constexpr unsigned start[]{0, 4, 2, 1}, step[]{8, 8, 4, 2};
            for (unsigned pass = 0; pass < 4; ++pass)
                for (uint32_t y = start[pass]; y < source.height; y += step[pass])
                    expected.insert(expected.end(), source.indices.begin() + size_t(y) * source.width,
                                    source.indices.begin() + (size_t(y) + 1) * source.width);
        } else
            expected = source.indices;
        CHECK(ended && clears && actual == expected);
    }
}
} // namespace
TEST(bmp_all_indexed_depths_odd_rows_orientation_and_palette_indices) {
    for (unsigned bits : {1, 4, 8})
        for (auto size : {std::pair{1u, 1u}, std::pair{7u, 13u}, std::pair{37u, 23u}}) {
            const auto source = indexed(bits, size.first, size.second), unchanged = source;
            for (bool top : {false, true}) {
                BmpSaveOptions options;
                options.top_down = top;
                const auto bytes = encode_bmp(source, options);
                CHECK(bytes[28] == bits && le32(bytes.data() + 30) == 0);
                CHECK(static_cast<int32_t>(le32(bytes.data() + 22)) ==
                      (top ? -int32_t(source.height) : int32_t(source.height)));
                CHECK(decode(bytes).frames[0].pixels == rgba(source).frames[0].pixels);
                const size_t offset = le32(bytes.data() + 10);
                const size_t stride = ((source.width * bits + 31ull) / 32) * 4;
                for (uint32_t y = 0; y < source.height; ++y)
                    for (uint32_t x = 0; x < source.width; ++x) {
                        const size_t row = top ? y : source.height - 1ull - y;
                        const unsigned shift = 8 - bits - (x * bits) % 8;
                        CHECK(((bytes[offset + row * stride + x * bits / 8] >> shift) & ((1 << bits) - 1)) ==
                              source.indices[y * size_t(source.width) + x]);
                    }
            }
            CHECK(source == unchanged);
        }
}
TEST(bmp_rle4_rle8_compress_when_useful_and_fall_back_when_not) {
    for (unsigned bits : {4, 8}) {
        auto source = indexed(bits, 512, 27, true);
        const auto unchanged = source;
        BmpSaveOptions options;
        const auto ordinary = encode_bmp(source, options);
        options.rle = options.top_down = true;
        const auto compressed = encode_bmp(source, options);
        CHECK(le32(compressed.data() + 30) == (bits == 4 ? 2 : 1));
        CHECK(le32(compressed.data() + 22) == source.height && compressed.size() < ordinary.size() / 4);
        CHECK(decode(compressed).frames[0].pixels == rgba(source).frames[0].pixels);
        CHECK(source == unchanged);
        source = indexed(bits, 1, 1);
        const auto tiny_bytes = encode_bmp(source, options);
        CHECK(le32(tiny_bytes.data() + 30) == 0 && static_cast<int32_t>(le32(tiny_bytes.data() + 22)) == -1);
    }
    BmpSaveOptions options;
    options.rle = true;
    CHECK(le32(encode_bmp(indexed(1, 512, 20, true), options).data() + 30) == 0);
}
TEST(bmp_explicit_four_bit_depth_is_kept_with_a_two_color_palette) {
    auto source = indexed(1, 512, 20, true);
    source.bits = 4;
    BmpSaveOptions options;
    options.rle = true;
    const auto compressed = encode_bmp(source, options);
    CHECK(compressed[28] == 4 && le32(compressed.data() + 30) == 2);
    CHECK(decode(compressed).frames[0].pixels == rgba(source).frames[0].pixels);
}
TEST(bmp_truecolor_current_frame_matte_and_top_down_keep_source) {
    auto source = gradient();
    const auto first = source.frames[0].pixels;
    source.frames.push_back(source.frames[0]);
    std::fill(source.frames[1].pixels.begin(), source.frames[1].pixels.end(), Pixel{250, 70, 3, 128});
    const auto unchanged = source.frames[1].pixels;
    for (bool top : {false, true})
        for (bool rle : {false, true}) {
            BmpSaveOptions options;
            options.top_down = top;
            options.rle = rle;
            options.matte = 0x123456;
            const auto bytes = encode_bmp(source, 1, options);
            CHECK(bytes[28] == 24 && le32(bytes.data() + 30) == 0);
            const auto loaded = decode(bytes);
            for (auto pixel : loaded.frames[0].pixels)
                CHECK(pixel == over(unchanged[0], {18, 52, 86, 255}));
        }
    CHECK(source.frames[0].pixels == first && source.frames[1].pixels == unchanged);
}
TEST(gif_all_interlace_compression_transparency_options_and_small_heights) {
    for (unsigned bits : {1, 4, 8})
        for (auto size : {std::pair{1u, 1u}, std::pair{7u, 3u}, std::pair{37u, 23u}}) {
            const auto source = indexed(bits, size.first, size.second), unchanged = source;
            for (bool interlaced : {false, true})
                for (bool raw : {false, true})
                    for (unsigned mode = 0; mode <= 5; ++mode) {
                        GifSaveOptions options;
                        options.interlaced = interlaced;
                        options.uncompressed = raw;
                        options.transparency = static_cast<GifTransparency>(mode);
                        options.transparent_index = 1;
                        check_gif(source, options);
                    }
            CHECK(source == unchanged);
        }
}
TEST(gif_unused_transparent_index_is_valid_and_lzw_mode_changes_actual_stream) {
    const auto source = indexed(4, 512, 27, true);
    GifSaveOptions options;
    options.transparency = GifTransparency::Index;
    options.transparent_index = 255;
    check_gif(source, options);
    const auto compressed = encode_gif(source, options);
    options.uncompressed = true;
    check_gif(source, options);
    CHECK(encode_gif(source, options).size() > compressed.size() * 4);
}
TEST(indexing_preserves_exact_flattened_colors_and_quantizes_higher_depth_more_accurately) {
    auto source = rgba(indexed(1, 13, 7));
    source.frames[0].pixels[0] = {10, 20, 30, 0};
    const auto unchanged = source.frames[0].pixels;
    const auto exact = index_image(source, 0, 4, 0x123456);
    for (size_t i = 0; i < unchanged.size(); ++i)
        CHECK(exact.palette[exact.indices[i]] == over(unchanged[i], {18, 52, 86, 255}));
    CHECK(source.frames[0].pixels == unchanged);
    source = gradient();
    auto expected = source;
    for (auto &pixel : expected.frames[0].pixels)
        pixel = over(pixel, {18, 52, 86, 255});
    std::array<uint64_t, 3> errors{};
    unsigned position{};
    for (unsigned bits : {1, 4, 8}) {
        const auto reduced = index_image(source, 0, bits, 0x123456, false);
        CHECK(reduced.palette.size() > 1 && reduced.palette.size() <= (1u << bits));
        const auto decoded = decode(encode_bmp(reduced));
        errors[position++] = error(expected, decoded);
        CHECK(decoded.frames[0].pixels == rgba(reduced).frames[0].pixels);
        CHECK(index_image(source, 0, bits, 0x123456, true).indices != reduced.indices);
    }
    CHECK(errors[2] < errors[1] && errors[1] < errors[0]);
    source.frames.push_back(source.frames[0]);
    std::fill(source.frames[1].pixels.begin(), source.frames[1].pixels.end(), Pixel{1, 2, 3, 0});
    GifSaveOptions options;
    options.matte = 0x123456;
    const auto loaded = decode(encode_gif(source, 1, options));
    for (auto pixel : loaded.frames[0].pixels)
        CHECK(pixel == Pixel{18, 52, 86, 255});
}
TEST(indexed_savers_reject_invalid_depth_palette_index_frame_dimensions_and_options) {
    for (unsigned bits : {0, 2, 16, 24}) {
        auto source = indexed(4);
        source.bits = bits;
        THROWS(encode_bmp(source));
        THROWS(encode_gif(source));
        THROWS(index_image(gradient(), 0, bits));
    }
    for (unsigned invalid = 0; invalid < 5; ++invalid) {
        auto source = indexed(4);
        if (invalid == 0)
            source.palette.clear();
        if (invalid == 1)
            source.palette[0].a = 0;
        if (invalid == 2)
            source.indices[0] = 16;
        if (invalid == 3)
            source.indices.pop_back();
        if (invalid == 4)
            source.palette.push_back({0, 0, 0, 255});
        THROWS(encode_bmp(source));
        THROWS(encode_gif(source));
    }
    GifSaveOptions gif;
    gif.transparent_index = 256;
    THROWS(encode_gif(indexed(4), gif));
    gif = {};
    gif.transparency = static_cast<GifTransparency>(99);
    THROWS(encode_gif(indexed(4), gif));
    gif = {};
    gif.matte = 0x1000000;
    THROWS(encode_gif(gradient(), 0, gif));
    BmpSaveOptions bmp;
    bmp.matte = 0x1000000;
    THROWS(encode_bmp(gradient(), 0, bmp));
    THROWS(encode_bmp(indexed(4), bmp));
    THROWS(encode_bmp(gradient(), 1));
    THROWS(encode_gif(gradient(), 1));
    THROWS(encode_gif(indexed(1, 65536, 1)));
    THROWS(encode_gif(gradient(65536, 1), 0));
}
TEST(format_save_target_automatic_extension_preserves_explicit_names) {
    SaveOptions options;
    CHECK(save_target(L"日本語 画像", options) == L"日本語 画像.png");
    options.uppercase_extension = true;
    const std::array<const wchar_t *, 4> extensions{L".PNG", L".JPG", L".BMP", L".GIF"};
    for (unsigned i = 0; i < extensions.size(); ++i) {
        options.last_format = static_cast<SaveFormat>(i);
        CHECK(save_target(L"name", options) == std::wstring(L"name") + extensions[i]);
        CHECK(save_target(L"explicit.pNg", options) == L"explicit.pNg");
        CHECK(save_target(L"explicit.JpEg", options) == L"explicit.JpEg");
    }
    THROWS(save_target(L"", options));
    THROWS(save_target(L"unsupported.psd", options));
    options.last_format = static_cast<SaveFormat>(99);
    THROWS(save_target(L"missing", options));
    THROWS(encode_for_save(gradient(), 0, static_cast<SaveFormat>(99), options));
    options = {};
    options.bmp_bits = 2;
    THROWS(encode_for_save(gradient(), 0, SaveFormat::Bmp, options));
}
TEST(real_file_save_routes_format_options_and_keeps_source_rgba) {
    Directory directory;
    const auto source = rgba(indexed(4, 512, 27, true));
    const auto unchanged = source.frames[0].pixels;
    SaveOptions options;
    options.png.interlaced = true;
    options.jpeg.progressive = true;
    options.bmp_bits = 4;
    options.bmp.rle = true;
    options.gif.interlaced = options.gif.uncompressed = true;
    for (auto extension : {L".png", L".jpeg", L".bmp", L".gif"}) {
        const auto path = directory.path / (std::wstring(L"保存 画像") + extension);
        save_image(source, 0, path, options, false);
        const auto bytes = read_file(path);
        if (extension == std::wstring(L".png"))
            CHECK(bytes[28] == 1 && load_image(path).frames[0].pixels == unchanged);
        else if (extension == std::wstring(L".jpeg")) {
            const std::array<uint8_t, 2> marker{255, 0xc2};
            CHECK(std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end()) != bytes.end());
        } else if (extension == std::wstring(L".bmp"))
            CHECK(bytes[28] == 4 && le32(bytes.data() + 30) == 2 &&
                  load_image(path).frames[0].pixels == unchanged);
        else
            CHECK(gif_data(bytes).interlaced && load_image(path).frames[0].pixels == unchanged);
        CHECK(source.frames[0].pixels == unchanged);
    }
    directory.no_temporary_files();
}
TEST(real_file_save_preserves_original_times_on_new_and_existing_targets) {
    Directory directory;
    auto source = gradient();
    source.path = directory.path / L"original.png";
    save_image(source, 0, source.path);
    const auto selected = time_value(132224123456789000ull);
    set_times(source.path, selected);
    SaveOptions options;
    options.keep_times = true;
    for (bool existing : {false, true}) {
        const auto target = directory.path / (existing ? L"existing.png" : L"new.png");
        if (existing) {
            save_image(source, 0, target);
            set_times(target, time_value(132555123456789000ull));
        }
        save_image(source, 0, target, options, existing);
        const auto actual = file_times(target);
        CHECK(CompareFileTime(&actual.ftLastWriteTime, &selected) == 0);
        CHECK(CompareFileTime(&actual.ftCreationTime, &selected) == 0);
        CHECK(load_image(target).frames[0].pixels == source.frames[0].pixels);
    }
    options.keep_times = false;
    save_image(source, 0, directory.path / L"current.png", options);
    const auto current = file_times(directory.path / L"current.png");
    CHECK(CompareFileTime(&current.ftLastWriteTime, &selected) != 0);
    options.keep_times = true;
    save_image(source, 0, source.path, options);
    const auto overwritten = file_times(source.path);
    CHECK(CompareFileTime(&overwritten.ftLastWriteTime, &selected) == 0);
    source.path.clear();
    save_image(source, 0, directory.path / L"clipboard.png", options);
    directory.no_temporary_files();
}
TEST(real_file_save_rejects_overwrite_locks_invalid_encoding_and_animation_originals) {
    Directory directory;
    auto source = gradient();
    const auto target = directory.path / L"target.png";
    save_image(source, 0, target);
    const auto original = read_file(target);
    source.frames[0].pixels[0] = {123, 45, 6, 255};
    SaveOptions options;
    THROWS(save_image(source, 0, target, options, false));
    CHECK(read_file(target) == original);
    const auto lock =
        CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(lock != INVALID_HANDLE_VALUE);
    bool rejected{};
    try {
        save_image(source, 0, target, options);
    } catch (const std::exception &) {
        rejected = true;
    }
    CloseHandle(lock);
    CHECK(rejected && read_file(target) == original);
    options.png.compression = 10;
    THROWS(save_image(source, 0, target, options));
    CHECK(read_file(target) == original);
    options = {};
    THROWS(save_image(source, 0, directory.path / L"missing/target.png", options));
    source.frames.push_back(source.frames[0]);
    source.path = target;
    THROWS(save_image(source, 0, target, options));
    const auto alias = directory.path / L"alias.png";
    CHECK(CreateHardLinkW(alias.c_str(), target.c_str(), nullptr));
    THROWS(save_image(source, 0, alias, options));
    CHECK(read_file(target) == original && read_file(alias) == original);
    save_image(source, 1, directory.path / L"frame.png", options);
    CHECK(load_image(directory.path / L"frame.png").frames[0].pixels == source.frames[1].pixels);
    directory.no_temporary_files();
}
int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--oracle") {
        try {
            const auto root =
                (std::filesystem::path(GVN_SOURCE_ROOT) / L".cache/indexed-saving-oracle").lexically_normal();
            const auto output = std::filesystem::weakly_canonical(argv[2]);
            CHECK(output.parent_path() == root && output.filename().wstring().starts_with(L"run-"));
            CHECK(std::filesystem::is_directory(output) && std::filesystem::is_empty(output));
            const auto write = [&](const std::string &name, const auto &bytes) {
                const auto path = output / name;
                CHECK(!std::filesystem::exists(path));
                std::ofstream stream(path, std::ios::binary);
                stream.write(reinterpret_cast<const char *>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size() * sizeof(bytes[0])));
                stream.close();
                CHECK(stream.good());
            };
            unsigned bmps{}, gifs{};
            for (unsigned bits : {1, 4, 8})
                for (unsigned kind = 0; kind < 3; ++kind) {
                    const auto source = kind == 0   ? indexed(bits)
                                        : kind == 1 ? indexed(bits, 512, 27, true)
                                                    : indexed(bits, 7, 3);
                    const auto unchanged = source;
                    const auto prefix = std::to_string(bits) + "-" + std::to_string(kind);
                    write(prefix + ".palette", source.palette);
                    write(prefix + ".indices", source.indices);
                    for (bool top : {false, true})
                        for (bool rle : {false, true}) {
                            BmpSaveOptions options;
                            options.top_down = top;
                            options.rle = rle;
                            write(prefix + "-bmp-" + std::to_string(top) + "-" + std::to_string(rle) + ".bmp",
                                  encode_bmp(source, options));
                            ++bmps;
                        }
                    for (bool interlaced : {false, true})
                        for (bool raw : {false, true})
                            for (unsigned mode = 0; mode <= 5; ++mode) {
                                GifSaveOptions options;
                                options.interlaced = interlaced;
                                options.uncompressed = raw;
                                options.transparency = static_cast<GifTransparency>(mode);
                                options.transparent_index = 1;
                                write(prefix + "-gif-" + std::to_string(interlaced) + "-" +
                                          std::to_string(raw) + "-" + std::to_string(mode) + ".gif",
                                      encode_gif(source, options));
                                ++gifs;
                            }
                    CHECK(source == unchanged);
                }
            const auto source = gradient();
            const auto unchanged = source.frames[0].pixels;
            write("gradient.rgba", unchanged);
            for (bool top : {false, true}) {
                BmpSaveOptions options;
                options.top_down = top;
                options.matte = 0x123456;
                write("gradient-" + std::to_string(top) + ".bmp", encode_bmp(source, 0, options));
                ++bmps;
            }
            for (unsigned bits : {1, 4, 8})
                for (bool dither : {false, true}) {
                    const auto reduced = index_image(source, 0, bits, 0x123456, dither);
                    const auto prefix = "quantized-" + std::to_string(bits) + "-" + std::to_string(dither);
                    write(prefix + ".palette", reduced.palette);
                    write(prefix + ".indices", reduced.indices);
                    write(prefix + ".bmp", encode_bmp(reduced));
                    ++bmps;
                }
            CHECK(source.frames[0].pixels == unchanged);
            std::cout << "{\"passed\":true,\"source_unchanged\":true,\"bmp_outputs\":" << bmps
                      << ",\"gif_outputs\":" << gifs << "}\n";
            return 0;
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    return run_tests();
}
