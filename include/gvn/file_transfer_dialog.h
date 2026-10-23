#pragma once
#include <windows.h>
#include <objidl.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace gvn {
struct FileTransferOptions {
    std::wstring basic_path;
    std::array<std::wstring, 10> destinations;
    bool registered{}, confirm{true};
    bool operator==(const FileTransferOptions &) const = default;
};
struct FileTransferChoice {
    FileTransferOptions options;
    std::filesystem::path directory;
    int slot{-1};
};
namespace transfer_control {
constexpr int BasicEdit = 100, Confirm = 101, Registered = 102, Browse = 103, Source = 104, Base = 105,
              BasicPane = 110, RegisteredPane = 111, Scroll = 112, BasicLabel = 113, EditBase = 200,
              ExecuteBase = 300;
}
class FileTransferDialog {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    using Browse = std::function<std::filesystem::path(HWND, const wchar_t *)>;
    using Error = std::function<void(HWND, const std::wstring &)>;
    FileTransferDialog(bool move, const std::filesystem::path &source, const FileTransferOptions &,
                       int initial_slot = -1, Browse = {}, Error = {});
    ~FileTransferDialog();
    std::optional<FileTransferChoice> show(HWND owner);
    HWND create(HWND owner);
    HWND window() const;
    HWND control(int id) const;
    int active_slot() const;
    bool registered_mode() const;
    bool ready() const;
    std::optional<FileTransferChoice> result() const;
    // The registered target of this dialog's own edit control; also used by the
    // dedicated in-process tests with a real Shell IDataObject.
    Microsoft::WRL::ComPtr<IDropTarget> folder_drop_target(int id) const;
};
std::optional<FileTransferChoice> choose_file_transfer(HWND, bool move, const std::filesystem::path &source,
                                                       const FileTransferOptions &, int initial_slot = -1);
} // namespace gvn
