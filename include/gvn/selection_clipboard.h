#pragma once
#include "codecs.h"
#include <optional>
#include <span>
namespace gvn {
inline constexpr wchar_t selection_clipboard_format[]=L"GVN.Selection.v1";
struct SelectionOrigin {int32_t x{},y{};uint32_t canvas_width{},canvas_height{};};
struct SelectionClipboard {Image image;std::optional<SelectionOrigin> origin;bool invalid_metadata{};};
SelectionClipboard decode_selection_clipboard(std::span<const uint8_t>);
std::array<uint8_t,64> selection_clipboard_header(size_t png_bytes,uint32_t width,uint32_t height,
                                                 SelectionOrigin,bool known=true);
std::vector<uint8_t> encode_selection_clipboard(std::span<const uint8_t> png,uint32_t width,uint32_t height,
                                               SelectionOrigin,bool known=true);
} // namespace gvn
