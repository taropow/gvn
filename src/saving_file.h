#pragma once
#include <filesystem>
#include <span>

namespace gvn::detail {
void write_encoded_image(std::span<const uint8_t> bytes, const std::filesystem::path &path, bool overwrite,
                         const std::filesystem::path &timestamp_source = {});
}
