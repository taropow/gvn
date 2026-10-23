#pragma once
#include "core.h"
#include "loading.h"
#include "jump_preview.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace gvn {
struct JumpOptions {
    bool preview{true};
    int quality{1}; // Simple, Linear, Integra
    bool operator==(const JumpOptions &) const = default;
};
struct JumpChoice {
    size_t index{};
    std::filesystem::path path;
    JumpOptions options;
};
namespace jump_control {
constexpr int Preview = 100, Name = 101, Order = 102, Total = 103, Scroll = 104, Enabled = 105, Quality = 106,
              Direction = 107, Error = 108;
}
class JumpDialog {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    using Decoder = JumpPreview::Decoder;
    JumpDialog(std::span<const FileEntry>, size_t index, bool reversed, JumpOptions = {}, LoadingOptions = {},
               Decoder = {}, std::shared_ptr<JumpPreview> = {});
    ~JumpDialog();
    std::optional<JumpChoice> show(HWND owner);
    HWND create(HWND owner);
    HWND window() const;
    HWND control(int id) const;
    bool ready() const;
    bool loading() const;
    bool valid() const;
    size_t selected_index() const;
    JumpOptions options() const;
    std::shared_ptr<const Image> preview_image() const;
    std::optional<JumpChoice> result() const;
};
std::optional<JumpChoice> choose_jump(HWND, std::span<const FileEntry>, size_t index, bool reversed,
                                      JumpOptions, LoadingOptions, std::shared_ptr<JumpPreview>);
} // namespace gvn
