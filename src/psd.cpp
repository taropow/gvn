#include "gvn/codecs.h"
#include "image_metadata.h"
#include "Psd.h"
#include "PsdAllocator.h"
#include "PsdFile.h"
#include "PsdDocument.h"
#include "PsdPlanarImage.h"
#include "PsdImageDataSection.h"
#include "PsdParseDocument.h"
#include "PsdParseImageDataSection.h"
#include "miniz.h"
#include <cstring>
#include <map>
#include <malloc.h>

namespace gvn {
namespace {
uint16_t be16(std::span<const uint8_t> data, size_t offset) {
    if (offset > data.size() || data.size() - offset < 2)
        throw std::runtime_error("Truncated PSD");
    return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
}
uint32_t be32(std::span<const uint8_t> data, size_t offset) {
    return (uint32_t(be16(data, offset)) << 16) | be16(data, offset + 2);
}
class Arena final : public psd::Allocator {
    std::map<void *, size_t> blocks;
    size_t total{}, maximum;
    void *DoAllocate(size_t size, size_t alignment) override {
        if (size > maximum - total)
            throw std::runtime_error("PSD allocation limit exceeded");
        void *p = _aligned_malloc(std::max<size_t>(1, size), std::max<size_t>(alignment, 8));
        if (!p)
            throw std::bad_alloc();
        blocks.emplace(p, size);
        total += size;
        std::memset(p, 0, size);
        return p;
    }
    void DoFree(void *p) override {
        if (!p)
            return;
        auto it = blocks.find(p);
        if (it != blocks.end()) {
            total -= it->second;
            blocks.erase(it);
            _aligned_free(p);
        }
    }

  public:
    explicit Arena(size_t limit) : maximum(limit) {}
    ~Arena() {
        for (auto [p, size] : blocks) {
            (void)size;
            _aligned_free(p);
        }
    }
};
class BoundedFile final : public psd::File {
    std::span<const uint8_t> bytes;
    bool DoOpenRead(const wchar_t *) override {
        return false;
    }
    bool DoOpenWrite(const wchar_t *) override {
        return false;
    }
    bool DoClose() override {
        return true;
    }
    ReadOperation DoRead(void *target, uint32_t count, uint64_t offset) override {
        if (offset > bytes.size() || count > bytes.size() - offset)
            throw std::runtime_error("PSD read outside source");
        std::memcpy(target, bytes.data() + offset, count);
        return nullptr;
    }
    bool DoWaitForRead(ReadOperation &) override {
        return true;
    }
    WriteOperation DoWrite(const void *, uint32_t, uint64_t) override {
        throw std::runtime_error("PSD is read only");
    }
    bool DoWaitForWrite(WriteOperation &) override {
        return false;
    }
    uint64_t DoGetSize() const override {
        return bytes.size();
    }

  public:
    BoundedFile(psd::Allocator *allocator, std::span<const uint8_t> data) : File(allocator), bytes(data) {}
};
void validate_rle(std::span<const uint8_t> row, size_t expected) {
    size_t input = 0, output = 0;
    while (input < row.size()) {
        const uint8_t code = row[input++];
        if (code == 128)
            continue;
        const size_t count = code > 128 ? 257u - code : code + 1u;
        const size_t used = code > 128 ? 1 : count;
        if (used > row.size() - input || count > expected - output)
            throw std::runtime_error("Malformed PSD RLE row");
        input += used;
        output += count;
    }
    if (output != expected)
        throw std::runtime_error("Incomplete PSD RLE row");
}
} // namespace
Image decode_psd(std::span<const uint8_t> bytes, size_t limit) {
    if (bytes.size() < 40 || be16(bytes, 4) != 1)
        throw std::runtime_error("PSD version unsupported (PSB is not PSD)");
    const auto channels = be16(bytes, 12), bits = be16(bytes, 22), mode = be16(bytes, 24);
    const auto h = be32(bytes, 14), w = be32(bytes, 18);
    const size_t pixels = checked_pixels(w, h, 1, limit);
    if ((mode != 1 && mode != 3) || (bits != 8 && bits != 16) || channels < (mode == 3 ? 3 : 1) ||
        channels > 56)
        throw std::runtime_error("PSD requires 8/16-bit RGB or grayscale merged data");
    const uint64_t planar_size = static_cast<uint64_t>(pixels) * channels * (bits / 8);
    if (planar_size > limit)
        throw std::runtime_error("PSD channels exceed the memory limit");
    size_t offset = 26;
    for (int section = 0; section < 3; ++section) {
        const size_t length = be32(bytes, offset);
        offset += 4;
        if (length > bytes.size() - offset)
            throw std::runtime_error("Truncated PSD section");
        offset += length;
    }
    const auto compression = be16(bytes, offset);
    const size_t start = offset + 2;
    std::vector<uint8_t> normalized;
    if (compression == 0) {
        if (planar_size > bytes.size() - start)
            throw std::runtime_error("PSD has no complete merged image");
    } else if (compression == 1) {
        const size_t rows = static_cast<size_t>(channels) * h;
        if (rows > (bytes.size() - start) / 2)
            throw std::runtime_error("Truncated PSD RLE table");
        size_t at = start + rows * 2;
        for (size_t row = 0; row < rows; ++row) {
            const auto length = be16(bytes, start + row * 2);
            if (length > bytes.size() - at)
                throw std::runtime_error("Truncated PSD RLE data");
            validate_rle(bytes.subspan(at, length), static_cast<size_t>(w) * (bits / 8));
            at += length;
        }
    } else if (compression == 2 || compression == 3) {
        std::vector<uint8_t> decoded(static_cast<size_t>(planar_size));
        if (tinfl_decompress_mem_to_mem(decoded.data(), decoded.size(), bytes.data() + start,
                                        bytes.size() - start, TINFL_FLAG_PARSE_ZLIB_HEADER) != decoded.size())
            throw std::runtime_error("PSD ZIP decompression failed");
        if (compression == 3) {
            const size_t stride = static_cast<size_t>(w) * (bits / 8);
            for (size_t row = 0; row < static_cast<size_t>(h) * channels; ++row) {
                uint16_t previous = 0;
                for (size_t x = 0; x < w; ++x) {
                    const size_t at = row * stride + x * (bits / 8);
                    if (bits == 8) {
                        previous = static_cast<uint8_t>(decoded[at] + previous);
                        decoded[at] = static_cast<uint8_t>(previous);
                    } else {
                        previous = static_cast<uint16_t>(be16(decoded, at) + previous);
                        decoded[at] = static_cast<uint8_t>(previous >> 8);
                        decoded[at + 1] = static_cast<uint8_t>(previous);
                    }
                }
            }
        }
        normalized.assign(bytes.begin(), bytes.begin() + 26);
        normalized.resize(40, 0);
        normalized.insert(normalized.end(), decoded.begin(), decoded.end());
        bytes = normalized;
    } else
        throw std::runtime_error("Unknown PSD compression");
    Arena arena(limit + 1024 * 1024);
    BoundedFile file(&arena, bytes);
    auto *document = psd::CreateDocument(&file, &arena);
    if (!document)
        throw std::runtime_error("PSD header could not be decoded");
    auto *section = psd::ParseImageDataSection(document, &file, &arena);
    if (!section || !section->images || section->imageCount < channels)
        throw std::runtime_error("PSD merged image unavailable");
    Image image;
    image.width = w;
    image.height = h;
    image.format = L"PSD";
    image.frames.resize(1);
    image.frames[0].pixels.resize(pixels);
    auto sample = [&](size_t channel, size_t index) -> uint8_t {
        if (bits == 8)
            return static_cast<const uint8_t *>(section->images[channel].data)[index];
        return static_cast<uint8_t>(
            (static_cast<const uint16_t *>(section->images[channel].data)[index] + 128u) / 257u);
    };
    for (size_t i = 0; i < pixels; ++i) {
        auto &p = image.frames[0].pixels[i];
        p.r = sample(0, i);
        p.g = mode == 3 ? sample(1, i) : p.r;
        p.b = mode == 3 ? sample(2, i) : p.r;
        p.a = channels > (mode == 3 ? 3 : 1) ? sample(mode == 3 ? 3 : 1, i) : 255;
    }
    psd::DestroyImageDataSection(section, &arena);
    psd::DestroyDocument(document, &arena);
    read_container_metadata(image,bytes);
    return image;
}
} // namespace gvn
