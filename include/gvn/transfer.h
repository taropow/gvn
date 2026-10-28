#pragma once
#include "codecs.h"
#include "selection_clipboard.h"
#include "saving.h"
#include <windows.h>
#include <objidl.h>
#include <wrl/client.h>
#include <variant>
#include <functional>
#include <memory>

namespace gvn {
// Owns prepared HGLOBAL payloads until the UI transfers them to the clipboard.
class PreparedClipboardImage {
    struct Data;
    std::unique_ptr<Data> data;
    friend PreparedClipboardImage prepare_clipboard_image(const Image &,size_t,std::optional<SelectionOrigin>,const ImageMetadata *,const CancelCheck &);
    friend bool publish_clipboard_image(HWND,PreparedClipboardImage &&,std::optional<DWORD>);
    friend bool render_clipboard_image(PreparedClipboardImage &,UINT);
public:
    PreparedClipboardImage();~PreparedClipboardImage();
    PreparedClipboardImage(PreparedClipboardImage &&) noexcept;
    PreparedClipboardImage &operator=(PreparedClipboardImage &&) noexcept;
};
PreparedClipboardImage prepare_clipboard_image(const Image &,size_t,std::optional<SelectionOrigin> origin={},const ImageMetadata *metadata=nullptr,const CancelCheck &cancel={});
bool publish_clipboard_image(HWND,PreparedClipboardImage &&,std::optional<DWORD> expected_sequence={});
void announce_clipboard_image(HWND,bool positioned);
// WM_RENDERFORMAT already has the receiver's clipboard lock; do not reopen it.
bool render_clipboard_image(PreparedClipboardImage &,UINT format=0);
bool publish_delayed_clipboard_image(HWND,PreparedClipboardImage &);
// Packed DIB payloads use straight alpha. PNG remains the preferred interchange.
std::vector<uint8_t> make_dibv5(const Image &image, size_t frame);
Image read_dib(std::span<const uint8_t> data);
void copy_image(HWND owner, const Image &image, size_t frame,std::optional<SelectionOrigin> origin=std::nullopt,const ImageMetadata *metadata=nullptr);
Image paste_image(HWND owner);
// Pixel data retains alpha; file references go through the viewer's normal open path.
using ClipboardContent = std::variant<Image, std::filesystem::path>;
ClipboardContent paste_clipboard(HWND owner,const std::function<void()> &decoding={});
SelectionClipboard paste_selection_clipboard(HWND owner,const std::function<void()> &decoding={});
std::filesystem::path drag_path(const Image &image, size_t frame, bool modified,const ImageMetadata *metadata=nullptr);
std::filesystem::path export_drag_path(const Image &,size_t,const std::wstring &name,SaveFormat,const SaveOptions &,const ImageMetadata *metadata=nullptr);
Microsoft::WRL::ComPtr<IDataObject> file_data_object(const std::filesystem::path &path);
DWORD drag_file(const std::filesystem::path &path);
} // namespace gvn
