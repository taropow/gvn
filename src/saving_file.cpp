#include "gvn/saving.h"
#include "saving_file.h"
#include "saving_indexed.h"
#include <windows.h>
#include <cwctype>
#include <optional>

namespace gvn {
SaveFormat save_format(const std::filesystem::path &path) {
    auto extension = path.extension().wstring();
    for (auto &c : extension)
        c = static_cast<wchar_t>(std::towlower(c));
    if (extension == L".png")
        return SaveFormat::Png;
    if (extension == L".jpg" || extension == L".jpeg")
        return SaveFormat::Jpeg;
    if (extension == L".bmp")
        return SaveFormat::Bmp;
    if (extension == L".gif")
        return SaveFormat::Gif;
    throw std::invalid_argument("Save supports PNG, JPEG, BMP and GIF");
}
namespace {
struct File {
    HANDLE handle{INVALID_HANDLE_VALUE};
    ~File() {
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
};
} // namespace
namespace detail {
void write_encoded_image(std::span<const uint8_t> bytes, const std::filesystem::path &path, bool overwrite,
                         const std::filesystem::path &timestamp_source) {
    if (bytes.empty() || bytes.size() > 512ull * 1024 * 1024)
        throw std::invalid_argument("Invalid encoded image size");
    std::optional<WIN32_FILE_ATTRIBUTE_DATA> times;
    if (!timestamp_source.empty()) {
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        if (GetFileAttributesExW(timestamp_source.c_str(), GetFileExInfoStandard, &attributes)) {
            if (!(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                times = attributes;
        } else if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)
            throw std::runtime_error("Cannot read original image timestamps");
    }
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid)))
        throw std::runtime_error("Cannot create output name");
    wchar_t suffix[64]{};
    StringFromGUID2(guid, suffix, 64);
    auto temporary = path;
    temporary += std::wstring(L".") + suffix + L".gvn-writing";
    try {
        {
            File output{CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr)};
            if (output.handle == INVALID_HANDLE_VALUE)
                throw std::runtime_error("Cannot create output file");
            DWORD written{};
            if (!WriteFile(output.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
                           nullptr) ||
                written != bytes.size())
                throw std::runtime_error("Image write failed");
            if (times && !SetFileTime(output.handle, &times->ftCreationTime, &times->ftLastAccessTime,
                                      &times->ftLastWriteTime))
                throw std::runtime_error("Cannot preserve original image timestamps");
            if (!FlushFileBuffers(output.handle))
                throw std::runtime_error("Image flush failed");
        }
        if (!MoveFileExW(temporary.c_str(), path.c_str(),
                         (overwrite ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace output file");
    } catch (...) {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
}
} // namespace detail
std::vector<uint8_t> encode_for_save(const Image &image, size_t frame, SaveFormat format,
                                     const SaveOptions &options,const ImageMetadata *metadata) {
    switch (format) {
    case SaveFormat::Png:
        return encode_png(image, frame, options.png,metadata);
    case SaveFormat::Jpeg:
        return encode_jpeg(image, frame, options.jpeg,metadata);
    case SaveFormat::Bmp: {
        unsigned bits = options.bmp_bits;
        const Rgb16Colors *preserve{};
        std::shared_ptr<const IndexedPixels> indexed;
        if (!bits) {
            const auto &current = image.frames.at(frame);
            indexed = image_indices(image, frame);
            bits = indexed
                       ? (indexed->bits == 2 ? 4 : indexed->bits)
                       : 24;
            if (bits == 24 && image.rgb16 && image.rgb16->matches(current.pixels)) {
                bits = 16;
                preserve = image.rgb16.get();
            }
        } else if (bits == 1 || bits == 4 || bits == 8)
            indexed = image_indices(image, frame);
        if (bits == 16 || bits == 24)
            return encode_bmp(image, frame, bits, options.bmp, metadata, preserve);
        return encode_bmp(detail::index_image(image, frame, bits, options.bmp.matte, options.dither, indexed), options.bmp,metadata?metadata:&image.metadata);
    }
    case SaveFormat::Gif:
        return encode_gif(image, frame, options.gif, options.dither);
    default:
        throw std::invalid_argument("Invalid save format");
    }
}
std::filesystem::path save_target(std::filesystem::path path, const SaveOptions &options) {
    if (path.empty() || path.filename().empty())
        throw std::invalid_argument("An image filename is required");
    if (!path.has_extension()) {
        static constexpr const wchar_t *extensions[]{L".png", L".jpg", L".bmp", L".gif"};
        if (static_cast<unsigned>(options.last_format) >= std::size(extensions))
            throw std::invalid_argument("Invalid default save format");
        std::wstring extension = extensions[static_cast<unsigned>(options.last_format)];
        if (options.uppercase_extension)
            for (auto &c : extension)
                c = static_cast<wchar_t>(std::towupper(c));
        path += extension;
    }
    (void)save_format(path);
    return path;
}
void save_image(const Image &image, size_t frame, const std::filesystem::path &path,
                const SaveOptions &options, bool overwrite,const ImageMetadata *metadata) {
    const auto target = save_target(path, options);
    const auto format = save_format(target);
    // A selected animation frame must never replace the original animated file.
    if (image.animated() && !image.path.empty()) {
        std::error_code error;
        const bool same = std::filesystem::equivalent(target, image.path, error);
        if (same || _wcsicmp(std::filesystem::absolute(target).lexically_normal().c_str(),
                             std::filesystem::absolute(image.path).lexically_normal().c_str()) == 0)
            throw std::invalid_argument("Save an animation frame to a different file");
    }
    const auto bytes = encode_for_save(image, frame, format, options,metadata);
    detail::write_encoded_image(bytes, target, overwrite,
                                options.keep_times ? image.path : std::filesystem::path{});
}
} // namespace gvn
