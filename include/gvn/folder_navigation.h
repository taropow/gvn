#pragma once
#include "core.h"
#include <functional>
#include <optional>

namespace gvn {
enum class FolderMove { Next, Previous, AdjacentNext, AdjacentPrevious, Parent };
struct FolderQuery {
    std::filesystem::path directory;
    FolderMove move{FolderMove::Next};
    FileVisibility visibility;
    bool natural{true};
    // Empty means the current volume/share root. A narrower root bounds browsing or a test tree.
    std::filesystem::path root;
    bool collect_files{};
    Sort sort{Sort::Name};
    bool reverse{};
    uint32_t seed{1};
    // At the forward boundary, revisit this root (including its own images) before its children.
    bool wrap{};
};
struct FolderMatch {
    std::filesystem::path directory;
    size_t examined{};
    bool cancelled{};
    // A present empty list is a successfully visited folder with no visible supported files.
    std::optional<std::vector<FileEntry>> files;
};
// Extension/attribute discovery and optional ordered metadata, without decoding pixels.
// Next/Previous traverse directories in depth-first name order, skipping empty folders and reparse
// points. Adjacent moves stay at the same level and may select empty folders or explicit junctions.
FolderMatch find_folder(const FolderQuery &, const std::function<bool()> &cancel = {});
} // namespace gvn
