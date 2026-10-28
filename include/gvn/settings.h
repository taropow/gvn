#pragma once
#include "commands.h"
#include "toast_duration.h"
#include "context_menu.h"
#include "mouse.h"
#include "core.h"
#include "loading.h"
#include "saving.h"
#include "comic.h"
#include "external.h"
#include "masks.h"
#include "color_adjustment.h"
#include "spatial_filter.h"
#include "selection_units.h"
#include <windows.h>
#include <array>
namespace gvn {
struct Binding {
    UINT command;
    std::wstring key;
};
std::vector<Binding> default_bindings();
bool parse_key(const std::wstring &text, UINT &key, UINT &modifiers);
bool assignable_key(const std::wstring &text);
std::wstring format_key(UINT key, UINT modifiers);
struct Settings {
    static constexpr Rect initial_window_rect{0, 0, 480, 360};
    int ui_theme{},ui_density{};
    bool toolbar_overflow{true};
    bool toast_enabled{true},command_sound{true},toast_view_commands{};
    int toast_duration{kToastDefaultDuration};
    int context_menu_mode{}; // compact, legacy full menu, custom
    std::vector<unsigned> context_menu_items=default_context_items();
    bool allow_multiple{true}, activate_reused{true}, activate_dropped{true};
    bool reopen_last{};
    bool disable_ime{true};
    bool allow_physical_overflow{};
    bool resize_keep_aspect{true}, resize_smooth{true};
    int resize_down{1}, resize_up{1};
    bool resize_sharpen{};
    bool resize_resample{true};
    bool info_auto_colors{};
    int resize_print_unit{3},resize_density_unit{},canvas_unit{};
    bool canvas_keep_aspect{},canvas_transparent{true};
    int canvas_anchor{4},canvas_alpha{255};
    uint32_t canvas_color{0xffffff};
    bool cut_transparent{true};
    uint32_t cut_color{};
    std::wstring last_file, last_archive_entry;
    bool toolbar{true}, status{true}, menu{}, checker{true}, nearest{}, natural{true}, reverse{}, hidden{},
        list_loop{}, keep_zoom{}, comic_rtl{true}, animation_loop{true}, slide_random{};
    bool slide_fullscreen{true};
    bool slide_loop{true}, slide_next_folder{}, slide_subtree{};
    int comic_mode{}, comic_down{10}, comic_up{1};
    uint32_t comic_fill{};
    bool comic_rotate_before{}, comic_single_wide{};
    ComicOptions comic_options() const {
        return {comic_mode, comic_down, comic_up, comic_fill, comic_rotate_before, comic_single_wide};
    }
    int sort{}, interval{5000}, grid{1}, wheel{1}, middle{1};
    bool selection_custom{};
    SelectionUnits custom_selection_units;
    SelectionUnits selection_units() const {
        return selection_custom?custom_selection_units:SelectionUnits{grid,grid,grid,grid};
    }
    // 0 stop, 1 confirm, 2 folder loop, 3 next/previous folder.
    int end_action{1};
    bool folder_history{}, previous_folder_first{}, adjacent_previous_first{}, parent_first{},
        boundary_sound{}, comic_toolbar_reverse{true}, comic_jump_reverse{true}, jump_preview{true};
    int jump_quality{1};
    int effective_end_action() const {
        return list_loop ? 2 : end_action == 2 ? 0 : end_action;
    }
    void set_end_action(int value) {
        end_action = value;
        list_loop = value == 2;
    }
    uint32_t background{0x202226};
    bool background_tiled{true};
    int background_pattern{2};
    uint32_t background_first{0x202020}, background_second{0x303030};
    bool undo_enabled{true}, remember_history{true}, paste_new{true}, topmost{}, title_path{},
        full_hide_cursor{}, full_nearest{}, animation_autoplay{true}, confirm_overwrite{true},
        confirm_delete{true}, confirm_unsaved{};
    bool save_dialog{true}, remember_save_folder{true}, rename_resort{true}, recycle_deleted{true},
        next_after_fileop{}, confirm_move{true}, confirm_copy{true};
    bool save_as_retarget{};
    bool copy_registered{}, move_registered{};
    std::wstring copy_basic_path, move_basic_path;
    std::wstring last_save_folder;
    SaveOptions saving;
    int zoom_step{25}, skip_count{10}, glass_size{8}, initial_fit{};
    int mask_swap{3};
    int rainbow_size{8};
    bool rainbow_preview{true};
    std::wstring cp_code;
    bool cp_preview{true};
    bool palette_preview{true};
    bool color_depth_dither{true};
    bool brightness_preview{true};
    bool levels_preview{true};
    bool color_adjust_unified{};
    int color_adjust_hue{},color_adjust_saturation{25},color_adjust_lightness{},color_adjust_gray{2};
    ColorAdjustment color_adjustment_choice() const {
        return {color_adjust_unified?color_adjust_hue:0,color_adjust_unified?color_adjust_saturation:0,
                color_adjust_unified?color_adjust_lightness:0,color_adjust_unified,static_cast<GrayMethod>(color_adjust_gray)};
    }
    int unsharp_radius{6},unsharp_strength{100},unsharp_threshold{},gaussian_radius{6},gaussian_transparency{};
    SpatialFilter filter_choice(bool sharp) const {
        return {sharp,sharp?unsharp_radius:gaussian_radius,unsharp_strength,unsharp_threshold,gaussian_transparency};
    }
    bool glass_remainder{}, mask_negative_red{true}, mask_negative_green{true}, mask_negative_blue{true},
        mask_xor_red{true}, mask_xor_green{true}, mask_xor_blue{true}, mask_q0_vertical{true},
        mask_q0_horizontal{true}, mask_q0_negative{true}, mask_q0_xor{}, mask_q0_flip_vertical{},
        mask_q0_flip_horizontal{}, fl_negative{true};
    MaskOptions mask_options() const {
        return {glass_size, mask_swap,
                unsigned(mask_negative_red) | unsigned(mask_negative_green) << 1 | unsigned(mask_negative_blue) << 2,
                unsigned(mask_xor_red) | unsigned(mask_xor_green) << 1 | unsigned(mask_xor_blue) << 2,
                unsigned(mask_q0_vertical) | unsigned(mask_q0_horizontal) << 1 | unsigned(mask_q0_negative) << 2 |
                    unsigned(mask_q0_xor) << 3 | unsigned(mask_q0_flip_vertical) << 4 | unsigned(mask_q0_flip_horizontal) << 5,
                glass_remainder, fl_negative, rainbow_size};
    }
    bool center_image{true}, zoom_reduce{true}, zoom_enlarge{true}, drag_simple{},
        full_hide_taskbar{true}, full_hide_wait{};
    int fit_max{5000}, fit_simple{}, display_down{10}, display_up{1}, full_down{1}, full_up{1}, zoom_simple{},
        zoom_mode{2}, zoom_below{10}, zoom_above{25};
    uint32_t checker_light{0xeeeeee}, checker_dark{0xc4c4c4};
    bool preserve_alpha{true}, bmp_transparency{true}, gif_transparency{true}, png_transparency{true},
        gif_background{}, png_background{}, gif_animation{true}, exif_orientation{true}, system_files{},
        protected_files{};
    uint32_t transparency_background{0xffffff};
    int gif_speed{};
    int view_rotation{};
    bool bitfield_to24{true};
    bool animation_file_loops{true};
    bool prefetch_enabled{true};
    int cache_mib{64};
    LoadingOptions loading_options() const {
        return {preserve_alpha,          bmp_transparency, gif_transparency, png_transparency,
                gif_background,          png_background,   gif_animation,    exif_orientation,
                transparency_background, gif_speed,        view_rotation,bitfield_to24};
    }
    int checker_size{8};
    bool window_center{}, window_toolbar_min{true}, restore_maximized{true}, restore_fullscreen{}, caption{true},
        outer_frame{true}, inner_frame{}, full_topmost{}, full_toolbar{true}, full_status{true}, full_menu{},
        toolbar_center{}, toolbar_line{}, toolbar_large{}, toolbar_auto{}, status_auto{}, last_window_valid{};
    int startup_position{1}, startup_state{}, window_x{initial_window_rect.x}, window_y{initial_window_rect.y},
        window_width{initial_window_rect.w}, window_height{initial_window_rect.h}, last_x{}, last_y{},
        last_width{initial_window_rect.w}, last_height{initial_window_rect.h}, last_state{},
        title_mode{}, display_mode{2}, full_display_mode{1}, full_initial_fit{};
    bool scrollbars{}, full_scrollbars{};
    bool overlay_full{true}, overlay_window{true}, overlay_animation{}, overlay_italic{}, overlay_underline{},
        overlay_strikeout{};
    int overlay_position{}, overlay_background{2}, overlay_points{100}, overlay_weight{400},
        overlay_charset{DEFAULT_CHARSET};
    uint32_t overlay_foreground{0xffffff}, overlay_backcolor{};
    std::wstring overlay_family{L"MS UI Gothic"};
    MouseSettings mouse;
    std::array<std::wstring, 10> copy_destinations, move_destinations;
    std::array<std::wstring, 9> external;
    // Retain legacy path assignments from existing callers; edits to the structured model use setters.
    ExternalApplications external_options{default_external_applications()};
    std::array<std::wstring, 9> external_loaded_paths;
    ExternalApplication external_application(size_t index) const;
    ExternalApplications external_applications() const;
    void set_external_application(size_t index, ExternalApplication entry);
    void set_external_applications(const ExternalApplications &entries);
    std::vector<std::wstring> history;
    std::vector<Binding> bindings{default_bindings()};
    void load(const std::filesystem::path &path);
    void save(const std::filesystem::path &path) const;
    void remember(const std::filesystem::path &folder);
    UINT command_for(UINT key, UINT modifiers) const;
    std::vector<std::wstring> keys_for(UINT command) const;
    // Validates the whole proposed map, including aliases and cross-command conflicts.
    void set_keys(UINT command, const std::vector<std::wstring> &keys);
    std::wstring first_key(UINT command) const;
};
std::filesystem::path settings_path();
} // namespace gvn
