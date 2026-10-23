#pragma once
#include "core.h"
#include "loading.h"
#include <span>

namespace gvn {
constexpr size_t kSourceLimit = 512ull * 1024 * 1024;
std::vector<uint8_t> read_file(const std::filesystem::path &path, size_t source_limit = kSourceLimit);
Image decode(std::span<const uint8_t> bytes, const std::wstring &hint = L"", size_t limit = kDecodeLimit,
             const LoadingOptions &options = {});
Image load_image(const std::filesystem::path &path, size_t limit = kDecodeLimit,
                 const LoadingOptions &options = {}, size_t source_limit = kSourceLimit);
Image decode_psd(std::span<const uint8_t> bytes, size_t limit);
Image decode_animation(std::span<const uint8_t> bytes, size_t limit, const LoadingOptions &options = {});
bool is_apng(std::span<const uint8_t> bytes);
std::vector<uint8_t> encode(const Image &image, size_t frame, const std::wstring &extension,
                            float quality = 0.93f,const ImageMetadata *metadata=nullptr);
void save_image(const Image &image, size_t frame, const std::filesystem::path &path, float quality = 0.93f,
                bool overwrite = true,const ImageMetadata *metadata=nullptr);
} // namespace gvn
