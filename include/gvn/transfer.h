#pragma once
#include "codecs.h"
#include "selection_clipboard.h"
#include <windows.h>
#include <objidl.h>
#include <wrl/client.h>
#include <variant>

namespace gvn {
// Packed DIB payloads use straight alpha. PNG remains the preferred interchange.
std::vector<uint8_t> make_dibv5(const Image &image, size_t frame);
Image read_dib(std::span<const uint8_t> data);
void copy_image(HWND owner, const Image &image, size_t frame,std::optional<SelectionOrigin> origin=std::nullopt);
Image paste_image(HWND owner);
// Pixel data retains alpha; file references go through the viewer's normal open path.
using ClipboardContent = std::variant<Image, std::filesystem::path>;
ClipboardContent paste_clipboard(HWND owner);
SelectionClipboard paste_selection_clipboard(HWND owner);
std::filesystem::path drag_path(const Image &image, size_t frame, bool modified,const ImageMetadata *metadata=nullptr);
Microsoft::WRL::ComPtr<IDataObject> file_data_object(const std::filesystem::path &path);
DWORD drag_file(const std::filesystem::path &path);
} // namespace gvn
