#include "gvn/settings.h"
#include "settings_profile.h"
#include <shlobj.h>
#include <cwctype>
#include <map>
#include <set>
#include <sstream>
namespace gvn {
#define GVN_WIDEN_INNER(x) L##x
#define GVN_WIDEN(x) GVN_WIDEN_INNER(x)
std::vector<Binding> default_bindings() {
    return {{Open, L"Ctrl+O"},
            {CloseImage, L"Ctrl+W"},
            {Save, L"Ctrl+S"},
            {SaveAs, L"Ctrl+Shift+S"},
            {Copy, L"Ctrl+C"},
            {Paste, L"Ctrl+V"},
            {Cut, L"Ctrl+X"},
            {Undo, L"Ctrl+Z"},
            {SelectAll, L"Ctrl+A"},
            {ArmDrag, L"Ctrl+Shift+D"},
            {Previous, L"Left"},
            {Next, L"Right"},
            {SkipBack, L"PageUp"},
            {SkipNext, L"PageDown"},
            {First, L"Home"},
            {Last, L"End"},
            {ParentFolder, L"Backspace"},
            {PreviousFolder, L"Ctrl+PageUp"},
            {NextFolder, L"Ctrl+PageDown"},
            {Reload, L"F5"},
            {Fullscreen, L"F11"},
            {Slideshow, L"F8"},
            {Info, L"I"},
            {Help, L"F1"},
            {Actual, L"1"},
            {FitImage, L"0"},
            {ZoomIn, L"Plus"},
            {ZoomOut, L"Minus"},
            {ToolPan, L"Space"},
            {FramePrevious, L"["},
            {FrameNext, L"]"},
            {FileDelete, L"Delete"},
            {FileDeleteDirect, L"Shift+Delete"},
            {RenameFile, L"F2"},
            {Jump, L"J"},
            {SelectMode, L"S"},
            {Crop, L"Ctrl+Y"},
            {RotateRight, L"R"},
            {RotateLeft, L"Shift+R"},
            {ToggleMenu, L"F10"},
            {Print, L"Ctrl+P"},
            {ArmDrag, L"F6"}};
}
bool parse_key(const std::wstring &text, UINT &key, UINT &modifiers) {
    std::wstring token;
    key = 0;
    modifiers = 0;
    for (wchar_t c : text)
        if (!iswspace(c))
            token.push_back(static_cast<wchar_t>(towupper(c)));
    size_t start{};
    while (true) {
        auto end = token.find(L'+', start);
        const auto part = token.substr(start, end == std::wstring::npos ? end : end - start);
        if (part == L"CTRL" || part == L"CONTROL")
            modifiers |= 1;
        else if (part == L"SHIFT")
            modifiers |= 2;
        else if (part == L"ALT")
            modifiers |= 4;
        else {
            if (key)
                return false;
            if (part.size() == 1 &&
                ((part[0] >= L'A' && part[0] <= L'Z') || (part[0] >= L'0' && part[0] <= L'9')))
                key = part[0];
            else if (part == L"[")
                key = VK_OEM_4;
            else if (part == L"]")
                key = VK_OEM_6;
            else if (part.size() >= 2 && part[0] == L'F') {
                try {
                    size_t used{};
                    const int n = std::stoi(part.substr(1), &used);
                    if (used != part.size() - 1 || n < 1 || n > 24)
                        return false;
                    key = VK_F1 + n - 1;
                } catch (...) {
                    return false;
                }
            } else {
                const std::pair<const wchar_t *, UINT> keys[]{
                    {L"LEFT", VK_LEFT},         {L"RIGHT", VK_RIGHT},       {L"UP", VK_UP},
                    {L"DOWN", VK_DOWN},         {L"HOME", VK_HOME},         {L"END", VK_END},
                    {L"PAGEUP", VK_PRIOR},      {L"PAGEDOWN", VK_NEXT},     {L"SPACE", VK_SPACE},
                    {L"DELETE", VK_DELETE},     {L"BACKSPACE", VK_BACK},    {L"ENTER", VK_RETURN},
                    {L"PLUS", VK_OEM_PLUS},     {L"MINUS", VK_OEM_MINUS},   {L"TAB", VK_TAB},
                    {L"ESC", VK_ESCAPE},        {L"ESCAPE", VK_ESCAPE},     {L"INSERT", VK_INSERT},
                    {L"COMMA", VK_OEM_COMMA},   {L"PERIOD", VK_OEM_PERIOD}, {L"SEMICOLON", VK_OEM_1},
                    {L"SLASH", VK_OEM_2},       {L"BACKSLASH", VK_OEM_5},   {L"QUOTE", VK_OEM_7},
                    {L"GRAVE", VK_OEM_3},       {L"ADD", VK_OEM_PLUS},      {L"SUBTRACT", VK_OEM_MINUS},
                    {L"MULTIPLY", VK_MULTIPLY}, {L"DIVIDE", VK_DIVIDE},     {L"DECIMAL", VK_DECIMAL},
                    {L"CONVERT", VK_CONVERT},   {L"NONCONVERT", VK_NONCONVERT},
                    {L"変換", VK_CONVERT},     {L"無変換", VK_NONCONVERT},
                    {L"NUM0", VK_NUMPAD0},      {L"NUM1", VK_NUMPAD1},      {L"NUM2", VK_NUMPAD2},
                    {L"NUM3", VK_NUMPAD3},      {L"NUM4", VK_NUMPAD4},      {L"NUM5", VK_NUMPAD5},
                    {L"NUM6", VK_NUMPAD6},      {L"NUM7", VK_NUMPAD7},      {L"NUM8", VK_NUMPAD8},
                    {L"NUM9", VK_NUMPAD9}};
                for (const auto &entry : keys)
                    if (part == entry.first)
                        key = entry.second;
                if (!key)
                    return false;
            }
        }
        if (end == std::wstring::npos)
            break;
        start = end + 1;
    }
    return key != 0;
}
bool assignable_key(const std::wstring &text) {
    UINT key{}, mods{};
    if (!parse_key(text, key, mods))
        return false;
    return !(key == VK_ESCAPE && (mods == 0 || mods == 1 || (mods & 4))) &&
           !(key == VK_TAB && ((mods & 4) || mods == 7)) && !(key == VK_DELETE && (mods & 5) == 5);
}
std::wstring format_key(UINT key, UINT modifiers) {
    std::wstring name;
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
        name.push_back(static_cast<wchar_t>(key));
    else if (key >= VK_F1 && key <= VK_F24)
        name = L"F" + std::to_wstring(key - VK_F1 + 1);
    else {
        if (key == VK_ADD)
            key = VK_OEM_PLUS;
        if (key == VK_SUBTRACT)
            key = VK_OEM_MINUS;
        for (const auto candidate :
             {L"Left",      L"Right", L"Up",        L"Down",      L"Home",  L"End",      L"PageUp",
              L"PageDown",  L"Space", L"Delete",    L"Backspace", L"Enter", L"Plus",     L"Minus",
              L"Tab",       L"Esc",   L"Insert",    L"[",         L"]",     L"Comma",    L"Period",
              L"Semicolon", L"Slash", L"Backslash", L"Quote",     L"Grave", L"Multiply", L"Divide",
              L"Decimal",   L"Num0",  L"Num1",      L"Num2",      L"Num3",  L"Num4",     L"Num5",
              L"Num6",      L"Num7",  L"Num8",      L"Num9",      L"Convert", L"NonConvert"}) {
            UINT vk{}, mods{};
            if (parse_key(candidate, vk, mods) && vk == key) {
                name = candidate;
                break;
            }
        }
    }
    if (name.empty() || modifiers > 7)
        return {};
    return std::wstring(modifiers & 1 ? L"Ctrl+" : L"") + (modifiers & 2 ? L"Shift+" : L"") +
           (modifiers & 4 ? L"Alt+" : L"") + name;
}
namespace {
bool valid_bindings(const std::vector<Binding> &bindings) {
    std::set<std::pair<UINT, UINT>> used;
    std::map<UINT, int> counts;
    const auto &catalog = command_catalog();
    for (const auto &binding : bindings) {
        UINT key{}, mods{};
        if (!assignable_key(binding.key) ||
            std::none_of(catalog.begin(), catalog.end(),
                         [&](const auto &c) { return c.id == binding.command; }) ||
            !parse_key(binding.key, key, mods) || !used.emplace(key, mods).second ||
            ++counts[binding.command] > 8)
            return false;
    }
    return true;
}
std::wstring join_keys(const std::vector<std::wstring> &keys) {
    std::wstring result;
    for (const auto &key : keys) {
        if (!result.empty())
            result += L"|";
        result += key;
    }
    return result.empty() ? L"@none" : result;
}
std::vector<std::wstring> split_keys(const std::wstring &text) {
    if (text == L"@none")
        return {};
    std::vector<std::wstring> result;
    size_t start{};
    for (;;) {
        auto end = text.find(L'|', start);
        result.push_back(text.substr(start, end == std::wstring::npos ? end : end - start));
        if (end == std::wstring::npos)
            return result;
        start = end + 1;
    }
}
} // namespace
std::vector<std::wstring> Settings::keys_for(UINT command) const {
    std::vector<std::wstring> result;
    for (const auto &binding : bindings)
        if (binding.command == command)
            result.push_back(binding.key);
    return result;
}
std::wstring Settings::first_key(UINT command) const {
    const auto keys = keys_for(command);
    return keys.empty() ? L"" : keys.front();
}
void Settings::set_keys(UINT command, const std::vector<std::wstring> &keys) {
    auto next = bindings;
    const auto first =
        std::find_if(next.begin(), next.end(), [=](const auto &b) { return b.command == command; });
    const auto position = static_cast<size_t>(std::distance(next.begin(), first));
    std::erase_if(next, [=](const auto &b) { return b.command == command; });
    std::vector<Binding> replacement;
    for (const auto &key : keys)
        replacement.push_back({command, key});
    next.insert(next.begin() + std::min(position, next.size()), replacement.begin(), replacement.end());
    if (!valid_bindings(next))
        throw std::invalid_argument("Invalid, reserved, duplicate shortcut or more than eight keys");
    bindings = std::move(next);
}
std::filesystem::path settings_path() {
    PWSTR local{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)))
        throw std::runtime_error("Cannot find settings folder");
    const auto directory = std::filesystem::path(local) / L"GVN";
    CoTaskMemFree(local);
    return directory / L"settings.ini";
}
void Settings::remember(const std::filesystem::path &path) {
    if (!remember_history)
        return;
    const auto folder = path.wstring();
    if (folder.empty())
        return;
    history.erase(std::remove(history.begin(), history.end(), folder), history.end());
    history.insert(history.begin(), folder);
    if (history.size() > 20)
        history.resize(20);
}
void Settings::load(const std::filesystem::path &path) {
    detail::ProfileReader profile(path);
    auto read = [&](const wchar_t *section, const std::wstring &key, const std::wstring &initial) {
        return profile.read(section, key, initial);
    };
    auto integer = [&](const wchar_t *name, int initial, int low, int high) {
        try {
            return std::clamp(std::stoi(read(L"Viewer", name, std::to_wstring(initial))), low, high);
        } catch (...) {
            return initial;
        }
    };
#define GVN_BOOL(name) name = integer(GVN_WIDEN(#name), name ? 1 : 0, 0, 1) != 0
    GVN_BOOL(allow_multiple);
    GVN_BOOL(activate_reused);
    GVN_BOOL(activate_dropped);
    GVN_BOOL(reopen_last);
    GVN_BOOL(disable_ime);
    GVN_BOOL(allow_physical_overflow);
    GVN_BOOL(cut_transparent);
    GVN_BOOL(toolbar);
    GVN_BOOL(status);
    GVN_BOOL(menu);
    GVN_BOOL(checker);
    GVN_BOOL(background_tiled);
    GVN_BOOL(nearest);
    GVN_BOOL(resize_keep_aspect);
    resize_smooth = integer(L"resize_smooth", nearest ? 0 : 1, 0, 1) != 0;
    resize_down = integer(L"resize_down",1,0,10);
    resize_up = integer(L"resize_up",1,0,10);
    GVN_BOOL(resize_sharpen);
    GVN_BOOL(resize_resample);
    GVN_BOOL(info_auto_colors);
    resize_print_unit=integer(L"resize_print_unit",3,2,4);
    resize_density_unit=integer(L"resize_density_unit",0,0,1);
    canvas_unit=integer(L"canvas_unit",0,0,4);
    GVN_BOOL(canvas_keep_aspect);
    GVN_BOOL(canvas_transparent);
    canvas_anchor=integer(L"canvas_anchor",4,0,8);
    canvas_alpha=integer(L"canvas_alpha",255,0,255);
    canvas_color=static_cast<uint32_t>(integer(L"canvas_color",0xffffff,0,0xffffff));
    GVN_BOOL(natural);
    GVN_BOOL(reverse);
    GVN_BOOL(hidden);
    GVN_BOOL(list_loop);
    const auto action_key = read(L"Viewer", L"end_action", L"");
    const auto legacy_loop = read(L"Viewer", L"list_loop", L"");
    set_end_action(action_key.empty() && !legacy_loop.empty() ? (list_loop ? 2 : 0)
                                                              : integer(L"end_action", end_action, 0, 3));
    GVN_BOOL(folder_history);
    GVN_BOOL(previous_folder_first);
    GVN_BOOL(adjacent_previous_first);
    GVN_BOOL(parent_first);
    GVN_BOOL(boundary_sound);
    GVN_BOOL(comic_toolbar_reverse);
    GVN_BOOL(comic_jump_reverse);
    GVN_BOOL(jump_preview);
    jump_quality = integer(L"jump_quality", jump_quality, 0, 2);
    GVN_BOOL(keep_zoom);
    GVN_BOOL(comic_rtl);
    const auto comic_mode_key = read(L"Viewer", L"comic_mode", L"");
    const auto old_comic_height = read(L"Viewer", L"comic_height", L"");
    comic_mode = comic_mode_key.empty() && !old_comic_height.empty()
                     ? (integer(L"comic_height", 1, 0, 1) ? 3 : 4)
                     : integer(L"comic_mode", comic_mode, 0, 4);
    comic_down = integer(L"comic_down", comic_down, 0, 10);
    comic_up = integer(L"comic_up", comic_up, 0, 10);
    comic_fill = static_cast<uint32_t>(integer(L"comic_fill", comic_fill, 0, 0xffffff));
    view_rotation = integer(L"view_rotation", view_rotation, 0, 7);
    GVN_BOOL(comic_rotate_before);
    const auto single_wide_key = read(L"Viewer", L"comic_single_wide", L"");
    if (single_wide_key.empty() && (!comic_mode_key.empty() || !old_comic_height.empty()))
        comic_single_wide = true; // Legacy GVN always kept landscape pages standalone.
    else
        GVN_BOOL(comic_single_wide);
    GVN_BOOL(animation_loop);
    const auto file_loop_key = read(L"Viewer", L"animation_file_loops", L"");
    if (file_loop_key.empty() && !read(L"Viewer", L"animation_loop", L"").empty())
        animation_file_loops = false; // Existing profiles used ON to mean infinite replay.
    else
        GVN_BOOL(animation_file_loops);
    gif_speed = integer(L"gif_speed", gif_speed, 0, 4);
    GVN_BOOL(slide_random);
    GVN_BOOL(slide_fullscreen);
    GVN_BOOL(slide_loop);
    GVN_BOOL(slide_next_folder);
    GVN_BOOL(slide_subtree);
    GVN_BOOL(undo_enabled);
    GVN_BOOL(remember_history);
    GVN_BOOL(paste_new);
    GVN_BOOL(topmost);
    GVN_BOOL(title_path);
    GVN_BOOL(full_hide_cursor);
    GVN_BOOL(full_nearest);
    GVN_BOOL(animation_autoplay);
    GVN_BOOL(confirm_overwrite);
    GVN_BOOL(confirm_delete);
    GVN_BOOL(confirm_unsaved);
    GVN_BOOL(save_dialog);
    if (read(L"Viewer", L"save_as_retarget", L"").empty() &&
        !read(L"Viewer", L"save_dialog", L"").empty())
        save_as_retarget = true; // Preserve the previous GVN policy in existing file-operation profiles.
    else
        GVN_BOOL(save_as_retarget);
    GVN_BOOL(remember_save_folder);
    GVN_BOOL(rename_resort);
    GVN_BOOL(recycle_deleted);
    GVN_BOOL(next_after_fileop);
    GVN_BOOL(confirm_move);
    GVN_BOOL(confirm_copy);
    GVN_BOOL(copy_registered);
    GVN_BOOL(move_registered);
    GVN_BOOL(center_image);
    GVN_BOOL(zoom_reduce);
    GVN_BOOL(zoom_enlarge);
    GVN_BOOL(drag_simple);
    GVN_BOOL(glass_remainder);
    GVN_BOOL(mask_negative_red);
    GVN_BOOL(mask_negative_green);
    GVN_BOOL(mask_negative_blue);
    GVN_BOOL(mask_xor_red);
    GVN_BOOL(mask_xor_green);
    GVN_BOOL(mask_xor_blue);
    GVN_BOOL(mask_q0_vertical);
    GVN_BOOL(mask_q0_horizontal);
    GVN_BOOL(mask_q0_negative);
    GVN_BOOL(mask_q0_xor);
    GVN_BOOL(mask_q0_flip_vertical);
    GVN_BOOL(mask_q0_flip_horizontal);
    GVN_BOOL(fl_negative);
    GVN_BOOL(rainbow_preview);
    GVN_BOOL(cp_preview);
    GVN_BOOL(palette_preview);
    GVN_BOOL(color_depth_dither);
    GVN_BOOL(color_adjust_unified);
    GVN_BOOL(brightness_preview);
    GVN_BOOL(levels_preview);
    color_adjust_hue=integer(L"color_adjust_hue",color_adjust_hue,0,360);
    color_adjust_saturation=integer(L"color_adjust_saturation",color_adjust_saturation,0,100);
    color_adjust_lightness=integer(L"color_adjust_lightness",color_adjust_lightness,-100,100);
    color_adjust_gray=integer(L"color_adjust_gray",color_adjust_gray,0,2);
    unsharp_radius=integer(L"unsharp_radius",unsharp_radius,1,100);
    unsharp_strength=integer(L"unsharp_strength",unsharp_strength,1,400);
    unsharp_threshold=integer(L"unsharp_threshold",unsharp_threshold,0,255);
    gaussian_radius=integer(L"gaussian_radius",gaussian_radius,1,100);
    gaussian_transparency=integer(L"gaussian_transparency",gaussian_transparency,0,99);
    GVN_BOOL(full_hide_taskbar);
    GVN_BOOL(full_hide_wait);
    GVN_BOOL(window_center);
    GVN_BOOL(window_toolbar_min);
    GVN_BOOL(restore_maximized);
    GVN_BOOL(restore_fullscreen);
    GVN_BOOL(caption);
    GVN_BOOL(outer_frame);
    GVN_BOOL(inner_frame);
    GVN_BOOL(full_topmost);
    GVN_BOOL(full_toolbar);
    GVN_BOOL(full_status);
    GVN_BOOL(full_menu);
    GVN_BOOL(toolbar_center);
    GVN_BOOL(toolbar_line);
    GVN_BOOL(toolbar_large);
    GVN_BOOL(toolbar_auto);
    GVN_BOOL(status_auto);
    GVN_BOOL(last_window_valid);
    GVN_BOOL(scrollbars);
    GVN_BOOL(full_scrollbars);
    GVN_BOOL(overlay_full);
    GVN_BOOL(overlay_window);
    GVN_BOOL(overlay_animation);
    GVN_BOOL(overlay_italic);
    GVN_BOOL(overlay_underline);
    GVN_BOOL(overlay_strikeout);
    GVN_BOOL(preserve_alpha);
    GVN_BOOL(bmp_transparency);
    GVN_BOOL(gif_transparency);
    GVN_BOOL(png_transparency);
    GVN_BOOL(gif_background);
    GVN_BOOL(png_background);
    GVN_BOOL(gif_animation);
    GVN_BOOL(bitfield_to24);
    GVN_BOOL(exif_orientation);
    GVN_BOOL(prefetch_enabled);
#undef GVN_BOOL
    cache_mib = integer(L"cache_mib", cache_mib, 0, 4096);
    cut_color = static_cast<uint32_t>(integer(L"cut_color", static_cast<int>(cut_color), 0, 0xffffff));
    last_save_folder = read(L"Viewer", L"last_save_folder", last_save_folder);
    last_file = read(L"Viewer", L"last_file", last_file);
    const auto entry_parts = read(L"Viewer", L"last_archive_entry_parts", L"");
    if (entry_parts.empty())
        last_archive_entry = read(L"Viewer", L"last_archive_entry", last_archive_entry);
    else {
        last_archive_entry.clear();
        try {
            size_t consumed{};
            const int parts = std::stoi(entry_parts, &consumed);
            if (consumed != entry_parts.size() || parts < 0 || parts > 16)
                throw std::invalid_argument("Invalid previous ZIP name part count");
            const auto nibble = [](wchar_t value) -> unsigned {
                if (value >= L'0' && value <= L'9')
                    return value - L'0';
                if (value >= L'a' && value <= L'f')
                    return value - L'a' + 10;
                if (value >= L'A' && value <= L'F')
                    return value - L'A' + 10;
                throw std::invalid_argument("Invalid previous ZIP name encoding");
            };
            for (int part = 0; part < parts; ++part) {
                const auto hex = read(L"Viewer", L"last_archive_entry_" + std::to_wstring(part), L"");
                if (hex.empty() || hex.size() > 16384 || hex.size() % 4)
                    throw std::invalid_argument("Invalid previous ZIP name part");
                for (size_t offset = 0; offset < hex.size(); offset += 4) {
                    unsigned value{};
                    for (size_t digit = 0; digit < 4; ++digit)
                        value = (value << 4) | nibble(hex[offset + digit]);
                    last_archive_entry.push_back(static_cast<wchar_t>(value));
                }
            }
            if (last_archive_entry.size() > 65535)
                throw std::invalid_argument("Previous ZIP name is too long");
        } catch (const std::exception &) {
            last_archive_entry.clear();
        }
    }
    const auto saved_number = [&](const wchar_t *name, int initial, int low, int high) {
        try {
            const auto value = read(L"Saving", name, std::to_wstring(initial));
            size_t end{};
            const auto number = std::stoi(value, &end);
            if (value.find_first_not_of(L" \t\r\n", end) != std::wstring::npos)
                return initial;
            return std::clamp(number, low, high);
        } catch (...) {
            return initial;
        }
    };
#define GVN_SAVE_BOOL(key, field) saving.field = saved_number(L##key, saving.field, 0, 1) != 0
#define GVN_SAVE_INT(key, field, low, high) saving.field = saved_number(L##key, saving.field, low, high)
    GVN_SAVE_BOOL("keep_times", keep_times);
    GVN_SAVE_BOOL("uppercase_extension", uppercase_extension);
    GVN_SAVE_BOOL("dither", dither);
    GVN_SAVE_BOOL("bmp_top_down", bmp.top_down);
    GVN_SAVE_BOOL("bmp_rle", bmp.rle);
    GVN_SAVE_BOOL("gif_interlaced", gif.interlaced);
    GVN_SAVE_BOOL("gif_uncompressed", gif.uncompressed);
    GVN_SAVE_BOOL("jpeg_progressive", jpeg.progressive);
    GVN_SAVE_BOOL("png_interlaced", png.interlaced);
    GVN_SAVE_INT("png_compression", png.compression, 0, 9);
    GVN_SAVE_INT("jpeg_quality", jpeg.quality, 0, 100);
    GVN_SAVE_INT("gif_transparent_index", gif.transparent_index, 0, 255);
    GVN_SAVE_INT("bmp_matte", bmp.matte, 0, 0xffffff);
    GVN_SAVE_INT("gif_matte", gif.matte, 0, 0xffffff);
    GVN_SAVE_INT("jpeg_matte", jpeg.matte, 0, 0xffffff);
    GVN_SAVE_INT("png_matte", png.matte, 0, 0xffffff);
    GVN_SAVE_INT("png_transparent_color", png.transparent_color, 0, 0xffffff);
#undef GVN_SAVE_BOOL
#undef GVN_SAVE_INT
    saving.last_format =
        static_cast<SaveFormat>(saved_number(L"last_format", static_cast<int>(saving.last_format), 0, 3));
    saving.bmp_bits = saved_number(L"bmp_bits", saving.bmp_bits, 0, 24);
    if (saving.bmp_bits != 0 && saving.bmp_bits != 1 && saving.bmp_bits != 4 && saving.bmp_bits != 8 &&
        saving.bmp_bits != 16 && saving.bmp_bits != 24)
        saving.bmp_bits = 0;
    saving.png.filter =
        static_cast<PngFilter>(saved_number(L"png_filter", static_cast<int>(saving.png.filter), 0, 6));
    saving.png.transparency = static_cast<PngTransparency>(
        saved_number(L"png_transparency", static_cast<int>(saving.png.transparency), 0, 6));
    saving.gif.transparency = static_cast<GifTransparency>(
        saved_number(L"gif_transparency", static_cast<int>(saving.gif.transparency), 0, 5));
    system_files = integer(L"system_files", hidden, 0, 1) != 0;
    protected_files = integer(L"protected_files", hidden, 0, 1) != 0;
    transparency_background =
        static_cast<uint32_t>(integer(L"transparency_background", transparency_background, 0, 0xffffff));
    sort = integer(L"sort", sort, 0, 4);
    interval = integer(L"interval", interval, 250, 3600000);
    grid = integer(L"grid", grid, 1, 256);
    selection_custom=integer(L"selection_custom",0,0,1)!=0;
    custom_selection_units={integer(L"selection_start_x",grid,1,999),integer(L"selection_start_y",grid,1,999),integer(L"selection_size_x",grid,1,999),integer(L"selection_size_y",grid,1,999)};
    wheel = integer(L"wheel", wheel, 0, 1);
    middle = integer(L"middle", middle, 0, 2);
    background = static_cast<uint32_t>(integer(L"background", background, 0, 0xffffff));
    background_pattern = integer(L"background_pattern", background_pattern, 0, 4);
    if (read(L"Viewer", L"background_first", L"").empty() &&
        !read(L"Viewer", L"background", L"").empty())
        background_first = background;
    background_first = static_cast<uint32_t>(integer(L"background_first", background_first, 0, 0xffffff));
    background_second = static_cast<uint32_t>(integer(L"background_second", background_second, 0, 0xffffff));
    checker_light = static_cast<uint32_t>(integer(L"checker_light", checker_light, 0, 0xffffff));
    checker_dark = static_cast<uint32_t>(integer(L"checker_dark", checker_dark, 0, 0xffffff));
    checker_size = integer(L"checker_size", checker_size, 2, 64);
    zoom_step = integer(L"zoom_step", zoom_step, 1, 400);
    skip_count = integer(L"skip_count", skip_count, 1, 100000);
    glass_size = integer(L"glass_size", glass_size, 1, 999);
    mask_swap = integer(L"mask_swap", mask_swap, 0, 4);
    rainbow_size = integer(L"rainbow_size", rainbow_size, 1, 32);
    if (rainbow_size & (rainbow_size-1)) rainbow_size=8;
    cp_code=normalize_cp_code(read(L"Viewer",L"cp_code",cp_code));
    initial_fit = integer(L"initial_fit", initial_fit, 0, 4);
    startup_position = integer(L"startup_position", startup_position, 0, 2);
    startup_state = integer(L"startup_state", startup_state, 0, 3);
    window_x = integer(L"window_x", window_x, INT_MIN, INT_MAX);
    window_y = integer(L"window_y", window_y, INT_MIN, INT_MAX);
    window_width = integer(L"window_width", window_width, INT_MIN, INT_MAX);
    window_height = integer(L"window_height", window_height, INT_MIN, INT_MAX);
    // Preserve the existing recovery rule for damaged INI dimensions while accepting
    // every valid positive size. Invalid dialog input separately retains its draft value.
    if (window_width <= 0)
        window_width = 340;
    if (window_height <= 0)
        window_height = 240;
    last_x = integer(L"last_x", last_x, -1000000, 1000000);
    last_y = integer(L"last_y", last_y, -1000000, 1000000);
    last_width = integer(L"last_width", last_width, 1, 32768);
    last_height = integer(L"last_height", last_height, 1, 32768);
    last_state = integer(L"last_state", last_state, 0, 2);
    title_mode = integer(L"title_mode", title_path ? 1 : 0, 0, 4);
    title_path = title_mode == 1;
    display_mode = integer(L"display_mode", display_mode, 0, 5);
    full_display_mode = integer(L"full_display_mode", full_display_mode, 0, 5);
    full_initial_fit = integer(L"full_initial_fit", initial_fit, 0, 4);
    fit_max = integer(L"fit_max", fit_max, 1, 5000);
    fit_simple = integer(L"fit_simple", fit_simple, 0, 5000);
    const bool legacy_filters = integer(L"display_filter_version", 0, 0, 1) == 0;
    const auto filter_setting = [&](const wchar_t *key, int initial) {
        if (read(L"Viewer", key, L"").empty())
            return initial;
        const int value = integer(key, -1, 0, legacy_filters ? 4 : 13);
        if (value < 0)
            return initial;
        return legacy_filters && value >= 2 ? value + 9 : value;
    };
    display_down = filter_setting(L"display_down", display_down);
    display_up = filter_setting(L"display_up", display_up);
    full_down = filter_setting(L"full_down", full_down);
    full_up = filter_setting(L"full_up", full_up);
    zoom_simple = integer(L"zoom_simple", zoom_simple, 0, 5000);
    zoom_mode = integer(L"zoom_mode", zoom_mode, 0, 2);
    zoom_below = integer(L"zoom_below", zoom_below, 1, 400);
    zoom_above = integer(L"zoom_above", zoom_above, 1, 5000);
    overlay_position = integer(L"overlay_position", overlay_position, 0, 8);
    overlay_background = integer(L"overlay_background", overlay_background, 0, 2);
    overlay_points = integer(L"overlay_points", overlay_points, 10, INT_MAX);
    overlay_weight = integer(L"overlay_weight", overlay_weight, 1, 999);
    overlay_charset = integer(L"overlay_charset", overlay_charset, 0, 255);
    overlay_foreground =
        static_cast<uint32_t>(integer(L"overlay_foreground", overlay_foreground, 0, 0xffffff));
    overlay_backcolor = static_cast<uint32_t>(integer(L"overlay_backcolor", overlay_backcolor, 0, 0xffffff));
    overlay_family = read(L"Viewer", L"overlay_family", overlay_family).substr(0, LF_FACESIZE - 1);
    if (overlay_family.empty())
        overlay_family = L"MS UI Gothic";
    mouse = MouseSettings{};
    mouse.wheel_scroll = integer(L"mouse.wheel_scroll", 60, 1, 2000);
    mouse.drag_scroll = integer(L"mouse.drag_scroll", 100, 1, 1000);
    mouse.drag_zoom = integer(L"mouse.drag_zoom", 100, 1, 1000);
    mouse.drag_axis = integer(L"mouse.drag_axis", 0, 0, 3);
    mouse.drag_threshold = integer(L"mouse.drag_threshold", 4, 0, 100);
    mouse.hover_margin = integer(L"mouse.hover_margin", 10, 0, 50);
    mouse.hover_min = integer(L"mouse.hover_min", 0, 0, 100);
    mouse.ignore_activation = integer(L"mouse.ignore_activation", 0, 0, 1) != 0;
    mouse.slow_wheel = integer(L"mouse.slow_wheel", 0, 0, 1) != 0;
    mouse.basic_wheel(wheel != 0);
    mouse.basic_middle(middle);
    if (read(L"MouseBindings", L"Version", L"") == L"1") {
        auto loaded = mouse;
        loaded.bindings.clear();
        bool good = true;
        try {
            const auto text = read(L"MouseBindings", L"Count", L"-1");
            size_t used{};
            const auto count = std::stoi(text, &used);
            if (used != text.size() || count < 0 || count > 4096)
                throw std::runtime_error("Mouse binding count");
            for (int i = 0; i < count; ++i) {
                std::wistringstream row(read(L"MouseBindings", std::to_wstring(i), L""));
                unsigned gesture{}, held{}, mods{}, action{};
                std::wstring extra;
                if (!(row >> gesture >> held >> mods >> action) || (row >> extra))
                    throw std::runtime_error("Mouse binding row");
                loaded.bindings.push_back({static_cast<MouseGesture>(gesture), held, mods, action});
            }
        } catch (...) {
            good = false;
        }
        if (good && loaded.valid())
            mouse = std::move(loaded);
    }
    // Adopt the requested zoom default once; preserve all other mouse customizations.
    const auto mouse_defaults = read(L"MouseBindings", L"DefaultsVersion", L"0");
    if ((mouse_defaults == L"0" || mouse_defaults == L"1") &&
        mouse.exact(MouseGesture::WheelUp, 0, 0) == Previous &&
        mouse.exact(MouseGesture::WheelDown, 0, 0) == Next)
        mouse.basic_wheel(true);
    if (mouse_defaults != L"3" &&
        mouse.exact(MouseGesture::WheelUp, mouse_left, 0) == ScrollUp &&
        mouse.exact(MouseGesture::WheelDown, mouse_left, 0) == ScrollDown) {
        mouse.assign(MouseGesture::WheelUp, mouse_left, 0, Next);
        mouse.assign(MouseGesture::WheelDown, mouse_left, 0, Previous);
    }
    wheel = mouse.wheel_preset();
    middle = mouse.middle_preset();
    copy_basic_path = read(L"CopyDestinations", L"Basic", L"");
    move_basic_path = read(L"MoveDestinations", L"Basic", L"");
    for (size_t i = 0; i < copy_destinations.size(); ++i) {
        const auto key = std::to_wstring(i + 1);
        const auto legacy = read(L"Destinations", key, L"");
        copy_destinations[i] = read(L"CopyDestinations", key, legacy);
        move_destinations[i] = read(L"MoveDestinations", key, legacy);
    }
    for (size_t i = 0; i < external.size(); ++i)
        external[i] = read(L"External", std::to_wstring(i + 1), L"");
    const bool external_modern = read(L"ExternalOptions", L"Version", L"") == L"1";
    for (size_t i = 0; i < external.size(); ++i) {
        auto entry = migrate_external_application(i, external[i]);
        const auto prefix = std::to_wstring(i + 1) + L".";
        if (external_modern && read(L"ExternalOptions", prefix + L"present", L"") == L"1") {
            const auto field = [&](const wchar_t *key, const std::wstring &initial) {
                return read(L"ExternalOptions", prefix + key, initial);
            };
            const auto number = [&](const wchar_t *key, int initial, int high) {
                const auto value = field(key, std::to_wstring(initial));
                try {
                    size_t used{}; const auto result = std::stoi(value, &used);
                    if (used == value.size() && result >= 0 && result <= high) return result;
                } catch (...) {}
                return initial;
            };
            entry.name = external_application_name(field(L"name", entry.name));
            entry.executable = field(L"command", entry.executable);
            entry.verb = field(L"verb", entry.verb);
            entry.send = static_cast<ExternalSend>(number(L"send", static_cast<int>(entry.send), 4));
            entry.show = static_cast<ExternalShow>(number(L"show", 0, 2));
            entry.quit_after_launch = number(L"quit", 0, 1) != 0;
            entry.separator = number(L"separator", 0, 1) != 0;
            entry.include_archive_member = number(L"archive_member", 0, 1) != 0;
            entry.archive_file_for_folder = number(L"archive_folder", 0, 1) != 0;
        }
        set_external_application(i, std::move(entry));
    }
    history.clear();
    for (int i = 0; i < 20; ++i) {
        auto folder = read(L"History", std::to_wstring(i), L"");
        if (remember_history && !folder.empty())
            history.push_back(std::move(folder));
    }
    bindings = default_bindings();
    const bool modern = read(L"KeyBindings", L"Version", L"") == L"2";
    const bool existing_profile = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    // Existing profiles opt into this newly added destructive shortcut explicitly.
    // Also keep it disabled if a corrupt map falls back to the standard bindings.
    if (existing_profile)
        std::erase_if(bindings, [](const auto &binding) { return binding.command == FileDeleteDirect; });
    const bool old_space = existing_profile && read(L"KeyBindings", L"DefaultsVersion", L"") != L"1";
    std::vector<Binding> loaded;
    for (const auto &entry : command_catalog()) {
        const auto fallback = old_space && entry.id == Play ? std::vector<std::wstring>{L"Space"}
            : old_space && entry.id == ToolPan ? std::vector<std::wstring>{} : keys_for(entry.id);
        auto value = read(modern ? L"KeyBindings" : L"Keys", std::to_wstring(entry.id), L"@missing");
        auto keys = value == L"@missing" ? fallback
                    : modern             ? split_keys(value)
                                         : std::vector<std::wstring>{value};
        for (const auto &key : keys)
            loaded.push_back({entry.id, key});
    }
    // Older versions kept F6 outside the configurable map. Preserve that alias
    // during migration unless the user had already assigned F6 to another action.
    if (!modern && std::none_of(loaded.begin(), loaded.end(), [](const auto &binding) {
            UINT key{}, mods{};
            return parse_key(binding.key, key, mods) && key == VK_F6 && mods == 0;
        }))
        loaded.push_back({ArmDrag, L"F6"});
    // Migrate only the exact former default; retain an explicitly cleared or customized Space.
    if (old_space) {
        std::vector<std::wstring> play_keys;bool pan_bound=false;
        for (const auto &b : loaded) {if(b.command==Play)play_keys.push_back(b.key);if(b.command==ToolPan)pan_bound=true;}
        if (play_keys == std::vector<std::wstring>{L"Space"} && !pan_bound)
            for(auto &b:loaded)if(b.command==Play)b.command=ToolPan;
    }
    // A corrupt hand-edited map must never create ambiguous dispatch.
    if (valid_bindings(loaded))
        bindings = std::move(loaded);
}
void Settings::save(const std::filesystem::path &path) const {
    const auto external_state = external_applications();
    for (const auto &entry : external_state) {
        for (const auto *text : {&entry.name, &entry.executable, &entry.verb})
            if (text->size() > 32760 || text->find_first_of(std::wstring(L"\0\r\n", 3)) != std::wstring::npos)
                throw std::runtime_error("Invalid external application setting text");
        if (static_cast<unsigned>(entry.send) > 4 || static_cast<unsigned>(entry.show) > 2)
            throw std::runtime_error("Invalid external application setting choice");
    }
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    if (!std::filesystem::exists(path)) {
        HANDLE h =
            CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot create settings");
        const WORD bom = 0xfeff;
        DWORD count{};
        WriteFile(h, &bom, 2, &count, nullptr);
        CloseHandle(h);
    }
    std::map<std::wstring, std::vector<wchar_t>> sections;
    auto write = [&](const wchar_t *section, const std::wstring &key, const std::wstring &value) {
        auto &data = sections[section];
        const auto entry = key + L"=" + value;
        data.insert(data.end(), entry.begin(), entry.end());
        data.push_back(L'\0');
    };
    auto integer = [&](const wchar_t *key, int value) { write(L"Viewer", key, std::to_wstring(value)); };
#define GVN_SAVE(name) integer(GVN_WIDEN(#name), name)
    GVN_SAVE(allow_multiple);
    GVN_SAVE(activate_reused);
    GVN_SAVE(activate_dropped);
    GVN_SAVE(reopen_last);
    GVN_SAVE(disable_ime);
    GVN_SAVE(allow_physical_overflow);
    GVN_SAVE(cut_transparent);
    GVN_SAVE(cut_color);
    GVN_SAVE(toolbar);
    GVN_SAVE(status);
    GVN_SAVE(menu);
    GVN_SAVE(checker);
    GVN_SAVE(background_tiled);
    GVN_SAVE(background_pattern);
    integer(L"background_first", static_cast<int>(background_first));
    integer(L"background_second", static_cast<int>(background_second));
    GVN_SAVE(nearest);
    GVN_SAVE(resize_keep_aspect);
    GVN_SAVE(resize_smooth);
    GVN_SAVE(resize_down);
    GVN_SAVE(resize_up);
    GVN_SAVE(resize_sharpen);
    GVN_SAVE(resize_resample);
    GVN_SAVE(info_auto_colors);
    GVN_SAVE(resize_print_unit);
    GVN_SAVE(resize_density_unit);
    GVN_SAVE(canvas_unit);
    GVN_SAVE(canvas_keep_aspect);
    GVN_SAVE(canvas_transparent);
    GVN_SAVE(canvas_anchor);
    GVN_SAVE(canvas_alpha);
    GVN_SAVE(canvas_color);
    GVN_SAVE(natural);
    GVN_SAVE(reverse);
    GVN_SAVE(hidden);
    GVN_SAVE(list_loop);
    write(L"Viewer", L"end_action", std::to_wstring(effective_end_action()));
    GVN_SAVE(folder_history);
    GVN_SAVE(previous_folder_first);
    GVN_SAVE(adjacent_previous_first);
    GVN_SAVE(parent_first);
    GVN_SAVE(boundary_sound);
    GVN_SAVE(comic_toolbar_reverse);
    GVN_SAVE(comic_jump_reverse);
    GVN_SAVE(jump_preview);
    GVN_SAVE(jump_quality);
    GVN_SAVE(keep_zoom);
    GVN_SAVE(comic_rtl);
    GVN_SAVE(comic_mode);
    GVN_SAVE(comic_down);
    GVN_SAVE(comic_up);
    GVN_SAVE(comic_fill);
    GVN_SAVE(view_rotation);
    GVN_SAVE(comic_rotate_before);
    GVN_SAVE(comic_single_wide);
    GVN_SAVE(animation_loop);
    GVN_SAVE(animation_file_loops);
    GVN_SAVE(gif_speed);
    GVN_SAVE(slide_random);
    GVN_SAVE(slide_fullscreen);
    GVN_SAVE(slide_loop);
    GVN_SAVE(slide_next_folder);
    GVN_SAVE(slide_subtree);
    GVN_SAVE(undo_enabled);
    GVN_SAVE(remember_history);
    GVN_SAVE(paste_new);
    GVN_SAVE(topmost);
    GVN_SAVE(title_path);
    GVN_SAVE(full_hide_cursor);
    GVN_SAVE(full_nearest);
    GVN_SAVE(animation_autoplay);
    GVN_SAVE(confirm_overwrite);
    GVN_SAVE(confirm_delete);
    GVN_SAVE(confirm_unsaved);
    GVN_SAVE(save_dialog);
    GVN_SAVE(save_as_retarget);
    GVN_SAVE(remember_save_folder);
    GVN_SAVE(rename_resort);
    GVN_SAVE(recycle_deleted);
    GVN_SAVE(next_after_fileop);
    GVN_SAVE(confirm_move);
    GVN_SAVE(confirm_copy);
    GVN_SAVE(copy_registered);
    GVN_SAVE(move_registered);
    auto save_folder = last_save_folder.substr(0, 32760);
    for (auto &c : save_folder)
        if (c == L'\r' || c == L'\n')
            c = L' ';
    write(L"Viewer", L"last_save_folder", save_folder);
    {
        auto clean = last_file.substr(0, 32760);
        for (auto &c : clean)
            if (c == L'\r' || c == L'\n')
                c = L' ';
        write(L"Viewer", L"last_file", clean);
    }
    if (last_archive_entry.size() > 65535)
        throw std::invalid_argument("Previous ZIP name is too long");
    const size_t entry_parts = (last_archive_entry.size() + 4095) / 4096;
    integer(L"last_archive_entry_parts", static_cast<int>(entry_parts));
    constexpr wchar_t digits[] = L"0123456789abcdef";
    for (size_t part = 0; part < entry_parts; ++part) {
        std::wstring hex;
        const size_t end = std::min(last_archive_entry.size(), (part + 1) * 4096);
        hex.reserve((end - part * 4096) * 4);
        for (size_t i = part * 4096; i < end; ++i)
            for (int shift = 12; shift >= 0; shift -= 4)
                hex.push_back(digits[(static_cast<unsigned>(last_archive_entry[i]) >> shift) & 15]);
        write(L"Viewer", L"last_archive_entry_" + std::to_wstring(part), hex);
    }
    const auto save_number = [&](const wchar_t *name, int value) {
        write(L"Saving", name, std::to_wstring(value));
    };
#define GVN_FORMAT_SAVE(key, field) save_number(L##key, saving.field)
    GVN_FORMAT_SAVE("keep_times", keep_times);
    GVN_FORMAT_SAVE("uppercase_extension", uppercase_extension);
    GVN_FORMAT_SAVE("dither", dither);
    GVN_FORMAT_SAVE("bmp_top_down", bmp.top_down);
    GVN_FORMAT_SAVE("bmp_rle", bmp.rle);
    GVN_FORMAT_SAVE("gif_interlaced", gif.interlaced);
    GVN_FORMAT_SAVE("gif_uncompressed", gif.uncompressed);
    GVN_FORMAT_SAVE("jpeg_progressive", jpeg.progressive);
    GVN_FORMAT_SAVE("png_interlaced", png.interlaced);
    GVN_FORMAT_SAVE("png_compression", png.compression);
    GVN_FORMAT_SAVE("jpeg_quality", jpeg.quality);
    GVN_FORMAT_SAVE("gif_transparent_index", gif.transparent_index);
    GVN_FORMAT_SAVE("bmp_matte", bmp.matte);
    GVN_FORMAT_SAVE("gif_matte", gif.matte);
    GVN_FORMAT_SAVE("jpeg_matte", jpeg.matte);
    GVN_FORMAT_SAVE("png_matte", png.matte);
    GVN_FORMAT_SAVE("png_transparent_color", png.transparent_color);
    GVN_FORMAT_SAVE("bmp_bits", bmp_bits);
#undef GVN_FORMAT_SAVE
    save_number(L"last_format", static_cast<int>(saving.last_format));
    save_number(L"png_filter", static_cast<int>(saving.png.filter));
    save_number(L"png_transparency", static_cast<int>(saving.png.transparency));
    save_number(L"gif_transparency", static_cast<int>(saving.gif.transparency));
    GVN_SAVE(zoom_step);
    GVN_SAVE(skip_count);
    GVN_SAVE(glass_size);
    GVN_SAVE(mask_swap);
    GVN_SAVE(glass_remainder);
    GVN_SAVE(mask_negative_red);
    GVN_SAVE(mask_negative_green);
    GVN_SAVE(mask_negative_blue);
    GVN_SAVE(mask_xor_red);
    GVN_SAVE(mask_xor_green);
    GVN_SAVE(mask_xor_blue);
    GVN_SAVE(mask_q0_vertical);
    GVN_SAVE(mask_q0_horizontal);
    GVN_SAVE(mask_q0_negative);
    GVN_SAVE(mask_q0_xor);
    GVN_SAVE(mask_q0_flip_vertical);
    GVN_SAVE(mask_q0_flip_horizontal);
    GVN_SAVE(fl_negative);
    GVN_SAVE(rainbow_preview);
    GVN_SAVE(rainbow_size);
    GVN_SAVE(cp_preview);
    GVN_SAVE(palette_preview);
    GVN_SAVE(color_depth_dither);
    GVN_SAVE(color_adjust_unified);
    GVN_SAVE(brightness_preview);
    GVN_SAVE(levels_preview);
    GVN_SAVE(color_adjust_hue);
    GVN_SAVE(color_adjust_saturation);
    GVN_SAVE(color_adjust_lightness);
    GVN_SAVE(color_adjust_gray);
    GVN_SAVE(unsharp_radius);
    GVN_SAVE(unsharp_strength);
    GVN_SAVE(unsharp_threshold);
    GVN_SAVE(gaussian_radius);
    GVN_SAVE(gaussian_transparency);
    write(L"Viewer",L"cp_code",normalize_cp_code(cp_code));
    GVN_SAVE(initial_fit);
    GVN_SAVE(center_image);
    GVN_SAVE(zoom_reduce);
    GVN_SAVE(zoom_enlarge);
    GVN_SAVE(drag_simple);
    GVN_SAVE(full_hide_taskbar);
    GVN_SAVE(full_hide_wait);
    GVN_SAVE(window_center);
    GVN_SAVE(window_toolbar_min);
    GVN_SAVE(restore_maximized);
    GVN_SAVE(restore_fullscreen);
    GVN_SAVE(caption);
    GVN_SAVE(outer_frame);
    GVN_SAVE(inner_frame);
    GVN_SAVE(full_topmost);
    GVN_SAVE(full_toolbar);
    GVN_SAVE(full_status);
    GVN_SAVE(full_menu);
    GVN_SAVE(toolbar_center);
    GVN_SAVE(toolbar_line);
    GVN_SAVE(toolbar_large);
    GVN_SAVE(toolbar_auto);
    GVN_SAVE(status_auto);
    GVN_SAVE(last_window_valid);
    GVN_SAVE(scrollbars);
    GVN_SAVE(full_scrollbars);
    GVN_SAVE(overlay_full);
    GVN_SAVE(overlay_window);
    GVN_SAVE(overlay_animation);
    GVN_SAVE(overlay_italic);
    GVN_SAVE(overlay_underline);
    GVN_SAVE(overlay_strikeout);
    GVN_SAVE(preserve_alpha);
    GVN_SAVE(bmp_transparency);
    GVN_SAVE(gif_transparency);
    GVN_SAVE(png_transparency);
    GVN_SAVE(gif_background);
    GVN_SAVE(png_background);
    GVN_SAVE(gif_animation);
    GVN_SAVE(bitfield_to24);
    GVN_SAVE(exif_orientation);
    GVN_SAVE(prefetch_enabled);
    GVN_SAVE(cache_mib);
    GVN_SAVE(system_files);
    GVN_SAVE(protected_files);
    GVN_SAVE(transparency_background);
    GVN_SAVE(overlay_position);
    GVN_SAVE(overlay_background);
    GVN_SAVE(overlay_points);
    GVN_SAVE(overlay_weight);
    GVN_SAVE(overlay_charset);
    GVN_SAVE(overlay_foreground);
    GVN_SAVE(overlay_backcolor);
    auto family = overlay_family.substr(0, LF_FACESIZE - 1);
    for (auto &c : family)
        if (c == L'\r' || c == L'\n')
            c = L' ';
    write(L"Viewer", L"overlay_family", family);
    GVN_SAVE(startup_position);
    GVN_SAVE(startup_state);
    GVN_SAVE(window_x);
    GVN_SAVE(window_y);
    GVN_SAVE(window_width);
    GVN_SAVE(window_height);
    GVN_SAVE(last_x);
    GVN_SAVE(last_y);
    GVN_SAVE(last_width);
    GVN_SAVE(last_height);
    GVN_SAVE(last_state);
    integer(L"title_mode", title_mode == 0 && title_path ? 1 : title_mode);
    GVN_SAVE(display_mode);
    GVN_SAVE(full_display_mode);
    GVN_SAVE(full_initial_fit);
    GVN_SAVE(fit_max);
    GVN_SAVE(fit_simple);
    integer(L"display_filter_version", 1);
    GVN_SAVE(display_down);
    GVN_SAVE(display_up);
    GVN_SAVE(full_down);
    GVN_SAVE(full_up);
    GVN_SAVE(zoom_simple);
    GVN_SAVE(zoom_mode);
    GVN_SAVE(zoom_below);
    GVN_SAVE(zoom_above);
    GVN_SAVE(checker_size);
    integer(L"checker_light", static_cast<int>(checker_light));
    integer(L"checker_dark", static_cast<int>(checker_dark));
    GVN_SAVE(sort);
    GVN_SAVE(interval);
    GVN_SAVE(grid);
    GVN_SAVE(selection_custom);
    integer(L"selection_start_x",custom_selection_units.start_x);integer(L"selection_start_y",custom_selection_units.start_y);
    integer(L"selection_size_x",custom_selection_units.size_x);integer(L"selection_size_y",custom_selection_units.size_y);
    integer(L"wheel", mouse.wheel_preset());
    integer(L"middle", mouse.middle_preset());
    write(L"MouseBindings", L"DefaultsVersion", L"3");
    GVN_SAVE(mouse.wheel_scroll);
    GVN_SAVE(mouse.drag_scroll);
    GVN_SAVE(mouse.drag_zoom);
    GVN_SAVE(mouse.drag_axis);
    GVN_SAVE(mouse.drag_threshold);
    GVN_SAVE(mouse.hover_margin);
    GVN_SAVE(mouse.hover_min);
    GVN_SAVE(mouse.ignore_activation);
    GVN_SAVE(mouse.slow_wheel);
    integer(L"background", static_cast<int>(background));
#undef GVN_SAVE
    write(L"CopyDestinations", L"Basic", copy_basic_path);
    write(L"MoveDestinations", L"Basic", move_basic_path);
    for (size_t i = 0; i < copy_destinations.size(); ++i) {
        const auto key = std::to_wstring(i + 1);
        write(L"CopyDestinations", key, copy_destinations[i]);
        write(L"MoveDestinations", key, move_destinations[i]);
    }
    write(L"ExternalOptions", L"Version", L"1");
    for (size_t i = 0; i < external_state.size(); ++i) {
        const auto &entry = external_state[i];
        const auto prefix = std::to_wstring(i + 1) + L".";
        write(L"External", std::to_wstring(i + 1), entry.executable);
        const auto field = [&](const wchar_t *key, const std::wstring &value) {
            write(L"ExternalOptions", prefix + key, value);
        };
        // An extra outer quote pair preserves the command's own quotes and edge spaces in Win32 INI.
        field(L"name", L"\"" + entry.name + L"\"");
        field(L"command", L"\"" + entry.executable + L"\"");
        field(L"verb", L"\"" + entry.verb + L"\"");
        field(L"present", L"1");
        field(L"send", std::to_wstring(static_cast<int>(entry.send)));
        field(L"show", std::to_wstring(static_cast<int>(entry.show)));
        field(L"quit", std::to_wstring(entry.quit_after_launch));
        field(L"separator", std::to_wstring(entry.separator));
        field(L"archive_member", std::to_wstring(entry.include_archive_member));
        field(L"archive_folder", std::to_wstring(entry.archive_file_for_folder));
    }
    sections[L"History"] = {};
    for (size_t i = 0; remember_history && i < history.size(); ++i)
        write(L"History", std::to_wstring(i), history[i]);
    write(L"KeyBindings", L"Version", L"2");
    write(L"KeyBindings", L"DefaultsVersion", L"1");
    for (const auto &entry : command_catalog())
        write(L"KeyBindings", std::to_wstring(entry.id), join_keys(keys_for(entry.id)));
    write(L"MouseBindings", L"Version", L"1");
    write(L"MouseBindings", L"Count", std::to_wstring(mouse.bindings.size()));
    for (size_t i = 0; i < mouse.bindings.size(); ++i) {
        const auto &b = mouse.bindings[i];
        write(L"MouseBindings", std::to_wstring(i),
              std::to_wstring(static_cast<unsigned>(b.gesture)) + L" " + std::to_wstring(b.held) + L" " +
                  std::to_wstring(b.modifiers) + L" " + std::to_wstring(b.action));
    }
    for (auto &[name, data] : sections) {
        data.push_back(L'\0');
        if (data.size() == 1)
            data.push_back(L'\0');
        if (!WritePrivateProfileSectionW(name.c_str(), data.data(), path.c_str()))
            throw std::runtime_error("Cannot save settings");
    }
}
ExternalApplication Settings::external_application(size_t index) const {
    const auto &entry = external_options.at(index);
    return external.at(index) != external_loaded_paths.at(index)
               ? migrate_external_application(index, external.at(index)) : entry;
}
ExternalApplications Settings::external_applications() const {
    ExternalApplications result;
    for (size_t i = 0; i < result.size(); ++i) result[i] = external_application(i);
    return result;
}
void Settings::set_external_application(size_t index, ExternalApplication entry) {
    entry.name = external_application_name(entry.name);
    external_options.at(index) = std::move(entry);
    external.at(index) = external_options[index].executable;
    external_loaded_paths.at(index) = external[index];
}
void Settings::set_external_applications(const ExternalApplications &entries) {
    for (size_t i = 0; i < entries.size(); ++i) set_external_application(i, entries[i]);
}
UINT Settings::command_for(UINT key, UINT modifiers) const {
    if (key == VK_ADD)
        key = VK_OEM_PLUS;
    if (key == VK_SUBTRACT)
        key = VK_OEM_MINUS;
    for (const auto &binding : bindings) {
        UINT vk{}, mods{};
        if (parse_key(binding.key, vk, mods) && key == vk && mods == modifiers)
            return binding.command;
    }
    return 0;
}
} // namespace gvn
