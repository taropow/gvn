#pragma once
#include "archive.h"
#include <functional>

namespace gvn {
using OpenLister = std::function<std::vector<FileEntry>(const std::filesystem::path &, FileVisibility,
                                                        const std::function<bool()> &)>;
struct OpenQuery {
    std::filesystem::path path;
    FileVisibility visibility;
    Sort sort{Sort::Name};
    bool reverse{}, natural{true};
    uint32_t seed{1};
    bool last{};
    bool missing_fallback{};
    std::wstring archive_entry;
};
struct OpenMatch {
    std::filesystem::path path, folder, archive;
    std::vector<FileEntry> files;
    std::vector<ArchiveEntry> entries;
    size_t index{};
    bool cancelled{};
};
// File type/metadata, directory and ZIP enumeration and ordering; no pixel decoding.
OpenMatch prepare_open(const OpenQuery &, const std::function<bool()> &cancel = {},
                       const OpenLister &lister = {});
} // namespace gvn
