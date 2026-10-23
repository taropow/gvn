#include "gvn/archive.h"
#include "gvn/view_rotation.h"
#include "miniz.h"
#include <windows.h>
namespace gvn {
namespace {
struct Zip {
    mz_zip_archive zip{};
    explicit Zip(std::span<const uint8_t> bytes) {
        if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
            throw std::runtime_error("Cannot read ZIP archive");
        if (mz_zip_reader_get_num_files(&zip) > 100000) {
            mz_zip_reader_end(&zip);
            throw std::runtime_error("ZIP has too many entries");
        }
    }
    ~Zip() {
        mz_zip_reader_end(&zip);
    }
};
std::wstring filename(mz_zip_archive &zip, uint32_t index, bool utf8) {
    const auto size = mz_zip_reader_get_filename(&zip, index, nullptr, 0);
    if (size == 0 || size > 32768)
        throw std::runtime_error("Invalid ZIP filename");
    std::vector<char> name(size);
    mz_zip_reader_get_filename(&zip, index, name.data(), size);
    const UINT page = utf8 ? CP_UTF8 : 932;
    int length = MultiByteToWideChar(page, 0, name.data(), -1, nullptr, 0);
    if (length <= 0)
        throw std::runtime_error("Invalid ZIP filename encoding");
    std::wstring result(length, L'\0');
    MultiByteToWideChar(page, 0, name.data(), -1, result.data(), length);
    result.pop_back();
    return result;
}
} // namespace
std::vector<ArchiveEntry> Archive::entries(const std::function<bool()> &cancel) const {
    Zip reader(bytes);
    std::vector<ArchiveEntry> out;
    for (uint32_t i = 0; i < mz_zip_reader_get_num_files(&reader.zip); ++i) {
        if (cancel && cancel())
            return {};
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&reader.zip, i, &stat))
            throw std::runtime_error("Invalid ZIP directory");
        if (stat.m_is_directory)
            continue;
        auto name = filename(reader.zip, i, (stat.m_bit_flag & 0x800) != 0);
        if (supported(name))
            out.push_back({std::move(name), stat.m_uncomp_size, i, static_cast<int64_t>(stat.m_time)});
    }
    return out;
}
Image Archive::read(uint32_t index, const LoadingOptions &options, size_t limit, size_t source_limit) const {
    Zip reader(bytes);
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&reader.zip, index, &stat))
        throw std::runtime_error("ZIP entry not found");
    if (stat.m_is_directory || stat.m_is_encrypted || !stat.m_is_supported)
        throw std::runtime_error("Unsupported or encrypted ZIP entry");
    if (stat.m_uncomp_size > std::min(kDecodeLimit, source_limit) || stat.m_uncomp_size == 0)
        throw std::runtime_error("ZIP entry exceeds image limit");
    std::vector<uint8_t> data(static_cast<size_t>(stat.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(&reader.zip, index, data.data(), data.size(), 0))
        throw std::runtime_error("ZIP decompression or CRC check failed");
    auto image = decode(data, filename(reader.zip, index, (stat.m_bit_flag & 0x800) != 0), limit, options);
    image.file_bytes = stat.m_uncomp_size;
    return image;
}
std::filesystem::path source_file(const std::filesystem::path &path) {
    const auto text = path.wstring();
    const auto separator = text.find(L'|');
    return separator == std::wstring::npos ? path : std::filesystem::path(text.substr(0, separator));
}
Image load_source(const std::filesystem::path &path, const LoadingOptions &options, size_t limit,
                  size_t source_limit) {
    const auto text = path.wstring();
    const auto separator = text.find(L'|');
    (void)view_rotation_quarters(options.view_rotation, 1, 1);
    if (separator == std::wstring::npos) {
        auto image = load_image(path, limit, options, source_limit);
        apply_view_rotation(image, options.view_rotation);
        return image;
    }
    const auto start = std::chrono::steady_clock::now();
    const auto part = text.substr(separator + 1);
    size_t used{};
    const auto index = std::stoul(part, &used);
    if (used != part.size() || index > UINT32_MAX)
        throw std::runtime_error("Invalid ZIP item");
    Archive zip(read_file(text.substr(0, separator), source_limit));
    auto image = zip.read(static_cast<uint32_t>(index), options, limit, source_limit);
    apply_view_rotation(image, options.view_rotation);
    image.load_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
            .count();
    image.metadata.source_load_ms=image.load_ms;
    return image;
}
} // namespace gvn
