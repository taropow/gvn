#pragma once
#include "codecs.h"
#include <span>
#include <functional>
namespace gvn {
struct ArchiveEntry {
    std::wstring name;
    uint64_t bytes{};
    uint32_t index{};
    int64_t time{};
};
class Archive {
    std::vector<uint8_t> bytes;

  public:
    explicit Archive(std::vector<uint8_t> data) : bytes(std::move(data)) {}
    std::vector<ArchiveEntry> entries(const std::function<bool()> &cancel = {}) const;
    Image read(uint32_t index, const LoadingOptions &options = {}, size_t limit = kDecodeLimit,
               size_t source_limit = kSourceLimit,const CancelCheck &cancel = {}) const;
};
std::filesystem::path source_file(const std::filesystem::path &path);
Image load_source(const std::filesystem::path &path, const LoadingOptions &options = {},
                  size_t limit = kDecodeLimit, size_t source_limit = kSourceLimit,const CancelCheck &cancel = {});
} // namespace gvn
