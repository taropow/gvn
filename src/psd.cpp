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
#include <array>
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
    std::array<std::span<const uint8_t>, 3> parts{};
    size_t size{};
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
        if (offset > size || count > size - offset)
            throw std::runtime_error("PSD read outside source");
        auto *out = static_cast<uint8_t *>(target);
        for (auto part : parts) {
            if (offset >= part.size()) {
                offset -= part.size();
                continue;
            }
            const size_t used = std::min<size_t>(count, part.size() - static_cast<size_t>(offset));
            std::memcpy(out, part.data() + offset, used);
            out += used;
            count -= static_cast<uint32_t>(used);
            offset = 0;
            if (!count)
                break;
        }
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
        return size;
    }

  public:
    BoundedFile(psd::Allocator *allocator, std::span<const uint8_t> data,
                std::span<const uint8_t> table = {}, std::span<const uint8_t> pixels = {})
        : File(allocator), parts{data, table, pixels}, size(data.size() + table.size() + pixels.size()) {}
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
    const size_t plane_size = pixels * (bits / 8);
    std::array<std::pair<size_t, size_t>, 56> channel_ranges{};
    if (compression == 0) {
        if (planar_size > bytes.size() - start)
            throw std::runtime_error("PSD has no complete merged image");
        for (size_t c = 0; c < channels; ++c)
            channel_ranges[c] = {start + c * plane_size, plane_size};
    } else if (compression == 1) {
        const size_t rows = static_cast<size_t>(channels) * h;
        if (rows > (bytes.size() - start) / 2)
            throw std::runtime_error("Truncated PSD RLE table");
        size_t at = start + rows * 2;
        for (size_t c = 0; c < channels; ++c) {
            const size_t first = at;
            for (size_t y = 0; y < h; ++y) {
                const auto length = be16(bytes, start + (c * h + y) * 2);
                if (length > bytes.size() - at)
                    throw std::runtime_error("Truncated PSD RLE data");
                validate_rle(bytes.subspan(at, length), static_cast<size_t>(w) * (bits / 8));
                at += length;
            }
            channel_ranges[c] = {first, at - first};
        }
    } else if (compression != 2 && compression != 3)
        throw std::runtime_error("Unknown PSD compression");
    Arena arena(limit + 1024 * 1024);
    BoundedFile file(&arena, bytes);
    auto *document = psd::CreateDocument(&file, &arena);
    if (!document)
        throw std::runtime_error("PSD header could not be decoded");
    Image image;
    image.width = w;
    image.height = h;
    image.format = L"PSD";
    image.frames.resize(1);
    image.frames[0].pixels.assign(pixels, Pixel{0, 0, 0, 255});
    const size_t colors = mode == 3 ? 3 : 1, needed = std::min<size_t>(channels, colors + 1);
    const auto apply = [&](size_t channel, const void *data, bool big_endian) {
        const auto *raw = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < pixels; ++i) {
            const auto value = bits == 8 ? raw[i]
                : big_endian ? uint16_t((uint16_t(raw[i * 2]) << 8) | raw[i * 2 + 1])
                             : static_cast<const uint16_t *>(data)[i];
            const auto sample = static_cast<uint8_t>(bits == 8 ? value : (value + 128u) / 257u);
            auto &p = image.frames[0].pixels[i];
            if (channel == colors)
                p.a = sample;
            else if (mode == 1)
                p.r = p.g = p.b = sample;
            else if (channel == 0)
                p.r = sample;
            else if (channel == 1)
                p.g = sample;
            else
                p.b = sample;
        }
    };
    if (compression < 2) {
        // Give the SDK a bounded view of one channel, retaining its RLE/16-bit decoder.
        // No encoded plane or whole planar image is copied into a normalized PSD.
        auto single = *document;
        single.channelCount = 1;
        single.imageDataSection.offset = 0;
        const auto prefix = bytes.subspan(offset, 2);
        for (size_t c = 0; c < needed; ++c) {
            const auto [at, size] = channel_ranges[c];
            const auto table = compression == 1 ? bytes.subspan(start + c * h * 2, h * 2)
                                                 : std::span<const uint8_t>{};
            const auto data = bytes.subspan(at, size);
            BoundedFile channel(&arena, prefix, table, data);
            single.imageDataSection.length = prefix.size() + table.size() + data.size();
            auto *section = psd::ParseImageDataSection(&single, &channel, &arena);
            if (!section || !section->images || section->imageCount != 1 || !section->images[0].data)
                throw std::runtime_error("PSD merged image unavailable");
            apply(c, section->images[0].data, false);
            psd::DestroyImageDataSection(section, &arena);
        }
    } else {
        struct Inflate {
            mz_stream stream{};
            Inflate() {
                if (mz_inflateInit(&stream) != MZ_OK)
                    throw std::runtime_error("PSD ZIP initialization failed");
            }
            ~Inflate() { mz_inflateEnd(&stream); }
        } inflater;
        auto &z = inflater.stream;
        z.next_in = bytes.data() + start;
        z.avail_in = static_cast<unsigned>(bytes.size() - start);
        std::vector<uint8_t> plane(plane_size);
        int status = MZ_OK;
        for (size_t c = 0; c < channels; ++c) {
            z.next_out = plane.data();
            z.avail_out = static_cast<unsigned>(plane.size());
            while (z.avail_out) {
                if (status == MZ_STREAM_END)
                    throw std::runtime_error("Incomplete PSD ZIP channel");
                const auto old_in = z.avail_in, old_out = z.avail_out;
                status = mz_inflate(&z, MZ_NO_FLUSH);
                if ((status != MZ_OK && status != MZ_STREAM_END) ||
                    (old_in == z.avail_in && old_out == z.avail_out))
                    throw std::runtime_error("PSD ZIP decompression failed");
            }
            if (c >= needed)
                continue;
            if (compression == 3) {
                const size_t stride = static_cast<size_t>(w) * (bits / 8);
                for (size_t y = 0; y < h; ++y) {
                    uint16_t previous = 0;
                    for (size_t x = 0; x < w; ++x) {
                        const size_t at = y * stride + x * (bits / 8);
                        if (bits == 8) {
                            previous = static_cast<uint8_t>(plane[at] + previous);
                            plane[at] = static_cast<uint8_t>(previous);
                        } else {
                            previous = static_cast<uint16_t>(be16(plane, at) + previous);
                            plane[at] = static_cast<uint8_t>(previous >> 8);
                            plane[at + 1] = static_cast<uint8_t>(previous);
                        }
                    }
                }
            }
            apply(c, plane.data(), true);
        }
        // Consume the zlib trailer and reject output beyond the declared channels.
        uint8_t extra{};
        while (status != MZ_STREAM_END) {
            z.next_out = &extra;
            z.avail_out = 1;
            const auto old_in = z.avail_in;
            status = mz_inflate(&z, MZ_NO_FLUSH);
            if (!z.avail_out || (status != MZ_OK && status != MZ_STREAM_END) ||
                (status != MZ_STREAM_END && old_in == z.avail_in))
                throw std::runtime_error("PSD ZIP length mismatch");
        }
    }
    psd::DestroyDocument(document, &arena);
    read_container_metadata(image,bytes);
    return image;
}
} // namespace gvn
