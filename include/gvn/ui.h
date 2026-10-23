#pragma once
#include "commands.h"
#include "ui_theme.h"
#include "core.h"
#include "printing.h"
#include <windows.h>
#include <commctrl.h>
#include <optional>
#include <map>
namespace gvn {
bool initialize_controls();
const wchar_t *command_label(UINT command);
HICON window_icon(int size);
HICON command_icon(UINT command, int size, bool disabled = false);
class IconToolbar {
    HIMAGELIST images{};
    int dpi{};
    bool player{}, is_playing{}, tools{};
    int tool_scroll{},tool_content{},tool_height{},tool_width{};
    bool large{}, built_large{}, centered{}, line{};
    bool floating_mode{},built_floating{};
    HWND host{},tools_scroll{};
    int bar_height{};
    int measured_width{-1},measured_height{},measured_extent{};
    std::map<UINT,std::wstring> texts;
    uint64_t palette_generation{};
    bool overflow{true};
    std::vector<UINT> overflow_commands;
    void arrange_overflow(int);
    int play_bitmap{-1}, pause_bitmap{-1};
    bool playing_bitmap_current{};
    void rebuild();

  public:
    HWND hwnd{};
    ~IconToolbar();
    void create(HWND parent, bool playback = false,bool editing_tools=false);
    int layout_tools(UINT dpi,int top,int height,bool visible);
    void scroll_tools(UINT action,int delta=0);
    HWND scroll_window() const{return tools_scroll;}
    void use_overflow(bool value){if(overflow!=value){overflow=value;measured_width=-1;}}
    const std::vector<UINT> &hidden_commands() const{return overflow_commands;}
    void show_more(HWND owner) const;
    void floating(bool value){floating_mode=value;}
    void configure(bool center, bool top_line, bool large_buttons) {
        centered = center;
        line = top_line;
        large = large_buttons;
    }
    int measure_height(int width, UINT new_dpi);
    int layout(int width, UINT new_dpi, bool visible, int x = 0, int y = 0);
    void enable(UINT command, bool enabled);
    void check(UINT command, bool checked);
    void playing(bool playing);
    void text(UINT command, const wchar_t *text);
    int content_width() const;
    int ideal_width() const;
    int height() const {
        return bar_height;
    }
    bool large_buttons() const {
        return large;
    }
    HWND bar_window() const {
        return host ? host : hwnd;
    }
};
class AnimationBar {
    std::optional<size_t> displayed_frame,displayed_count;
  public:
    HWND hwnd{}, slider{}, counter{};
    IconToolbar toolbar;
    void create(HWND parent);
    int measure_height(int width, UINT dpi);
    int layout(int width, int client_height, int status_height, UINT dpi, bool visible);
    void update(size_t frame, size_t count, bool playing, bool loop, bool honor_file_loops = true,
                uint32_t file_loops = 0);
};
std::optional<std::wstring> prompt(HWND owner, const wchar_t *title, const wchar_t *description,
                                   const std::wstring &initial);
enum class BoundaryChoice { Cancel, FolderEnd, Folder };
BoundaryChoice choose_boundary(HWND owner, bool forward, const std::filesystem::path &folder);
std::filesystem::path choose_folder(HWND owner, const wchar_t *title);
std::filesystem::path choose_executable(HWND owner);
std::filesystem::path choose_image_file(HWND owner, bool save, const std::filesystem::path &folder = {},
                                        const std::wstring &suggested = L"image.png");
void recycle_file(HWND owner, const std::filesystem::path &path);
} // namespace gvn
