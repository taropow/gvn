#include "gvn/loader.h"
#include "gvn/archive.h"
#include "gvn/renderer.h"
#include "gvn/transfer.h"
#include "gvn/edit.h"
#include "gvn/settings.h"
#include "gvn/ui.h"
#include "gvn/preferences.h"
#include "gvn/file_operations.h"
#include "gvn/file_transfer_dialog.h"
#include "gvn/save_dialog.h"
#include "gvn/jump_dialog.h"
#include "gvn/instance.h"
#include "gvn/new_window.h"
#include "gvn/ime.h"
#include "gvn/rainbow_dialog.h"
#include "gvn/cp_dialog.h"
#include "gvn/wallpaper.h"
#include "gvn/version.h"
#include "gvn/display_quality.h"
#include "gvn/resize_dialog.h"
#include "gvn/image_info_dialog.h"
#include "gvn/selection_units_dialog.h"
#include "gvn/selection_rect_dialog.h"
#include "gvn/palette_dialog.h"
#include "gvn/color_depth_dialog.h"
#include "gvn/color_adjustment_dialog.h"
#include "gvn/spatial_filter_dialog.h"
#include "gvn/brightness_dialog.h"
#include "gvn/levels_dialog.h"
#include "gvn/floating_selection.h"
#include "gvn/editor_chrome.h"
#include <windowsx.h>
#include <commdlg.h>
#include <pathcch.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <functional>
#include <cmath>
#include <psapi.h>
#include <utility>

namespace gvn {
constexpr UINT kLoaded = WM_APP + 1;
constexpr UINT kForwarded = WM_APP + 2;
constexpr UINT_PTR kPreviewCleanupTimer = 37;
constexpr UINT_PTR kForwardRetryTimer = 38;
constexpr UINT kKeyBase = 2000;
static std::wstring widen(const std::string &value) {
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}
static bool same_path(const std::filesystem::path &a, const std::filesystem::path &b) {
    return _wcsicmp(a.lexically_normal().c_str(), b.lexically_normal().c_str()) == 0;
}
static std::wstring size_text(uint64_t bytes) {
    std::wostringstream s;
    if (bytes < 1024)
        s << bytes << L" B";
    else if (bytes < 1024 * 1024)
        s << std::fixed << std::setprecision(1) << bytes / 1024.0 << L" KB";
    else
        s << std::fixed << std::setprecision(2) << bytes / (1024.0 * 1024) << L" MB";
    return s.str();
}
class App {
    friend struct AppAudit;
    friend struct Autopilot;
    enum class OpenOrigin { Explicit, WithinFolder, Folder, Startup };
    // Keep native dialogs at the boundary so command tests can supply deterministic answers.
    struct Dialogs {
        decltype(std::function{gvn::choose_resize}) resize = gvn::choose_resize;
        decltype(std::function{gvn::choose_canvas}) canvas = gvn::choose_canvas;
        decltype(std::function{gvn::show_image_info}) image_info = gvn::show_image_info;
        decltype(std::function{gvn::print_image}) print = gvn::print_image;
        decltype(std::function{gvn::choose_selection_units}) selection_units = gvn::choose_selection_units;
        decltype(std::function{gvn::choose_selection_rect}) selection_rect = gvn::choose_selection_rect;
        std::function<int(HWND, const wchar_t *, const wchar_t *, UINT)> ask = MessageBoxW;
        decltype(std::function{gvn::prompt}) text = gvn::prompt;
        decltype(std::function{gvn::choose_image_file}) image_file = gvn::choose_image_file;
        decltype(std::function{gvn::choose_save_file}) save_file = gvn::choose_save_file;
        decltype(std::function{gvn::choose_folder}) folder = gvn::choose_folder;
        decltype(std::function{gvn::choose_file_transfer}) file_transfer = gvn::choose_file_transfer;
        decltype(std::function{gvn::choose_boundary}) boundary = gvn::choose_boundary;
        decltype(std::function{gvn::choose_jump}) jump = gvn::choose_jump;
        decltype(std::function{gvn::choose_rainbow}) rainbow = gvn::choose_rainbow;
        decltype(std::function{gvn::choose_cp}) cp = gvn::choose_cp;
        decltype(std::function{gvn::choose_palette}) palette = gvn::choose_palette;
        decltype(std::function{gvn::choose_color_depth}) color_depth = gvn::choose_color_depth;
        decltype(std::function{gvn::choose_color_adjustment}) color_adjustment = gvn::choose_color_adjustment;
        decltype(std::function{gvn::choose_spatial_filter}) spatial_filter = gvn::choose_spatial_filter;
        decltype(std::function{gvn::choose_brightness}) brightness = gvn::choose_brightness;
        decltype(std::function{gvn::choose_levels}) levels = gvn::choose_levels;
        std::function<void()> boundary_beep = [] { MessageBeep(MB_OK); };
        decltype(std::function{gvn::show_preferences}) preferences_ui = gvn::show_preferences;
        std::function<UINT(HWND, HMENU, POINT)> popup = [](HWND owner, HMENU menu, POINT point) {
            return TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, owner, nullptr);
        };
        std::function<void(const std::filesystem::path &)> file_drag = gvn::drag_file;
        decltype(std::function{gvn::delete_file_checked}) delete_file = gvn::delete_file_checked;
        decltype(std::function{gvn::launch_new_viewer}) new_viewer = gvn::launch_new_viewer;
        decltype(std::function{gvn::launch_external_application}) external_launch = gvn::launch_external_application;
        decltype(std::function{gvn::choose_executable}) executable = gvn::choose_executable;
        std::function<WallpaperPlatform()> wallpaper_system = native_wallpaper_platform;
    } dialogs;
    std::function<POINT()> cursor_screen = [] {
        POINT point{};
        GetCursorPos(&point);
        return point;
    };
    POINT cursor_client() const {
        auto point = cursor_screen();
        ScreenToClient(hwnd, &point);
        return point;
    }
    HWND status{}, slider{}, canvas_horizontal{}, canvas_vertical{}, canvas_corner{};
    CanvasScrollbars canvas_bars;
    int canvas_width{}, canvas_height{}, canvas_top{-1}, canvas_left{-1},canvas_thickness{};
    int status_min_height{};
    bool canvas_tracking{};
    uint64_t overlay_scroll_until{};
    AnimationBar playback;
    IconToolbar toolbar,editor_tools;
    EditorChrome editor_chrome;
    ViewerIme ime;
    struct Editing {
        std::function<uint64_t()> physical_bytes = physical_memory_bytes;
        std::function<void(size_t)> allocating;
    } editing;
    EditMemory edit_memory() const {
        return {settings.allow_physical_overflow ? UINT64_MAX : editing.physical_bytes(),
                settings.allow_physical_overflow, editing.allocating};
    }
    std::unique_ptr<Loader> loader;
    OpenLister open_lister;
    std::shared_ptr<JumpPreview> preview_service;
    std::shared_ptr<Image> image, undo_image;
    std::optional<ImageMetadata> image_metadata,undo_metadata;
    std::shared_ptr<const Image> mask_preview_image, mask_preview_origin;
    size_t mask_preview_frame{SIZE_MAX};
    bool undo_modified{};
    Player undo_player;
    Renderer renderer;
    View view, normal_view, full_view;
    bool normal_display_policy_pending{true}, full_display_policy_pending{true};
    Fit normal_fit{Fit::Contain}, full_fit{Fit::Contain};
    Player player;
    Settings settings;
    std::filesystem::path config;
    bool settings_ready{}, deferred_display_policy{};
    std::optional<InstanceIdentity> published_instance;
    struct ForwardedFile {
        std::filesystem::path path;
        bool activated{};
    };
    std::optional<ForwardedFile> forwarded_file;
    std::vector<FileEntry> files;
    size_t index{};
    bool removed_current{};
    bool navigation_folder{};
    // Empty uses the volume/share boundary. A bounded browsing session can supply a subtree.
    std::filesystem::path navigation_root;
    std::filesystem::path folder, requested, archive, clipboard_folder;
    std::vector<ArchiveEntry> archive_entries;
    std::wstring current_name;
    struct Location {
        std::vector<FileEntry> files;
        size_t index{};
        bool removed{};
        std::filesystem::path folder, requested, archive;
        std::vector<ArchiveEntry> entries;
        std::wstring name;
        bool navigation{};
        std::filesystem::path clipboard_folder;
    };
    struct LoadRollback {
        std::filesystem::path requested;
        size_t index{};
        bool removed{};
        std::optional<Location> replaced;
        bool navigation_before{}, next_navigation{}, no_activate{};
        std::filesystem::path source_folder, remember_departed;
    };
    std::optional<LoadRollback> before_load;
    std::optional<Location> undo_location;
    struct UndoEntry {
        std::shared_ptr<Image> image;
        std::optional<ImageMetadata> metadata;
        bool modified{};
        Player player;
        std::optional<Location> location;
        std::filesystem::path retarget;
    };
    std::vector<UndoEntry> undo_history,redo_history;
    void clear_undo(){undo_image.reset();undo_metadata.reset();undo_location.reset();undo_player={};undo_history.clear();redo_history.clear();}
    void trim_undo(){
        const uint64_t limit=std::clamp(editing.physical_bytes()/8,uint64_t(16*1024*1024),uint64_t(256*1024*1024));
        uint64_t bytes=undo_image?undo_image->bytes():0;for(const auto &e:undo_history)bytes+=e.image->bytes();for(const auto &e:redo_history)bytes+=e.image->bytes();
        while(!undo_history.empty()&&(undo_history.size()+redo_history.size()+1>32||bytes>limit)){
            bytes-=undo_history.front().image->bytes();undo_history.erase(undo_history.begin());}
        while(redo_history.size()>1&&bytes>limit){bytes-=redo_history.front().image->bytes();redo_history.erase(redo_history.begin());}
    }
    void remember_undo(Player previous,std::optional<ImageMetadata> metadata,std::optional<Location> where={}){
        if(!settings.undo_enabled){clear_undo();return;}
        // Only references are retained. Reserve before moving any existing state so failure is atomic.
        undo_history.reserve(32);redo_history.reserve(32);
        if(undo_image)undo_history.push_back({std::move(undo_image),std::move(undo_metadata),undo_modified,std::move(undo_player),std::move(undo_location),{}});
        redo_history.clear();undo_image=image;undo_metadata=std::move(metadata);undo_modified=modified;
        undo_player=std::move(previous);undo_location=std::move(where);trim_undo();
    }
    void rebind_older_history(const std::filesystem::path &target,bool saved){
        auto bind=[&](std::vector<UndoEntry> &entries){for(auto i=entries.rbegin();i!=entries.rend();++i){if(i->location)break;i->retarget=target;if(saved){i->modified=true;
            auto metadata=i->metadata.value_or(i->image->metadata);metadata.source_format=current_metadata().source_format;metadata.source_bytes=current_metadata().source_bytes;i->metadata=std::move(metadata);}}};
        if(!undo_location)bind(undo_history);bind(redo_history);
    }
    void history_step(bool redo){
        if(redo?redo_history.empty():!undo_image)return;
        cancel_load();stop_slideshow();
        const auto &entry=redo?redo_history.back():UndoEntry{undo_image,undo_metadata,undo_modified,undo_player,undo_location,{}};
        // Location/player/string copies and optional retarget allocation precede stack mutation.
        UndoEntry target=entry,current{image,image_metadata,modified,player,entry.location?std::optional<Location>(location()):std::nullopt,{}};
        if(!target.retarget.empty()){auto rebound=std::make_shared<Image>(*target.image);rebound->path=target.retarget;target.image=std::move(rebound);}
        std::optional<UndoEntry> older;
        if(!redo&&!undo_history.empty()){older=undo_history.back();if(!older->retarget.empty()){
            auto rebound=std::make_shared<Image>(*older->image);rebound->path=older->retarget;older->image=std::move(rebound);}}
        undo_history.reserve(32);redo_history.reserve(32);
        if(redo){
            if(undo_image)undo_history.push_back({std::move(undo_image),std::move(undo_metadata),undo_modified,std::move(undo_player),std::move(undo_location),{}});
            undo_image=std::move(current.image);undo_metadata=std::move(current.metadata);undo_modified=current.modified;undo_player=std::move(current.player);undo_location=std::move(current.location);
            redo_history.pop_back();
        }else{
            redo_history.push_back(std::move(current));
            if(undo_history.empty()){undo_image.reset();undo_metadata.reset();undo_location.reset();undo_player={};}
            else {auto old=std::move(*older);undo_history.pop_back();
                undo_image=std::move(old.image);undo_metadata=std::move(old.metadata);undo_modified=old.modified;undo_player=std::move(old.player);undo_location=std::move(old.location);}
        }
        image=std::move(target.image);image_metadata=std::move(target.metadata);modified=target.modified;player=std::move(target.player);
        if(target.location)restore_location(std::move(*target.location));
        player.set_loop(settings.animation_loop,settings.animation_file_loops);reset_animation_clock();renderer.invalidate();
        view.set_image(image->width,image->height,settings.keep_zoom);selection=selection.clipped(image->width,image->height);layout();apply_display_policy();
    }
    struct PendingFolder {
        uint64_t sequence{};
        bool last{};
        std::shared_ptr<Image> source;
        std::filesystem::path folder, requested;
        bool slideshow{};
    };
    std::optional<PendingFolder> pending_folder;
    struct PendingOpen {
        uint64_t sequence{};
        bool force{}, allow_empty{};
    };
    std::optional<PendingOpen> pending_open;
    Location location() const {
        return {files,   index,           removed_current, folder,           requested,
                archive, archive_entries, current_name,    navigation_folder, clipboard_folder};
    }
    Location take_location() {
        return {std::move(files),
                index,
                removed_current,
                std::move(folder),
                std::move(requested),
                std::move(archive),
                std::move(archive_entries),
                std::move(current_name),
                navigation_folder,
                std::move(clipboard_folder)};
    }
    void restore_location(Location value) {
        files = std::move(value.files);
        index = value.index;
        removed_current = value.removed;
        folder = std::move(value.folder);
        requested = std::move(value.requested);
        archive = std::move(value.archive);
        archive_entries = std::move(value.entries);
        current_name = std::move(value.name);
        navigation_folder = value.navigation;
        clipboard_folder = std::move(value.clipboard_folder);
    }
    void checkpoint_list() {
        // Move a list only when replacing it; ordinary next/previous navigation never copies it.
        if (before_load && !before_load->replaced) {
            before_load->replaced = take_location();
            before_load->replaced->requested = before_load->requested;
            before_load->replaced->index = before_load->index;
            before_load->replaced->removed = before_load->removed;
            current_name = before_load->replaced->name;
        }
    }
    void rollback_load() {
        if (before_load) {
            if (before_load->replaced)
                restore_location(std::move(*before_load->replaced));
            requested = std::move(before_load->requested);
            index = before_load->index;
            removed_current = before_load->removed;
            navigation_folder = before_load->navigation_before;
            before_load.reset();
        }
        loading = false;
    }
    void cancel_load(bool release_cache = false) {
        slideshow_loading = false;
        pending_folder.reset();
        pending_open.reset();
        loader->cancel(release_cache);
        rollback_load();
    }
    void retire_jump_preview() {
        if (!preview_service || preview_service->idle()) {
            KillTimer(hwnd, kPreviewCleanupTimer);
            preview_service.reset();
        } else if (IsWindow(hwnd)) {
            SetTimer(hwnd, kPreviewCleanupTimer, 250, nullptr);
        }
    }
    struct SlideshowWindowRestore {
        bool was_full{}, was_maximized{};
        WINDOWPLACEMENT saved{sizeof(WINDOWPLACEMENT)};
    };
    std::optional<SlideshowWindowRestore> slideshow_window;
    struct SlideshowRun {
        bool loop{}, next_folder{}, subtree{}, temporary_random{};
        int previous_sort{};
        std::filesystem::path starting_folder;
    };
    std::optional<SlideshowRun> slideshow_run;
    bool slideshow_loading{};
    Settings persisted_settings(const Settings &value) const {
        auto saved = value;
        if (slideshow_run && slideshow_run->temporary_random)
            saved.sort = slideshow_run->previous_sort;
        return saved;
    }
    void save_settings() const {
        persisted_settings(settings).save(config);
    }
    void start_slideshow() {
        if (slideshow)
            return;
        if (loading)
            cancel_load();
        if (settings.slide_fullscreen) {
            SlideshowWindowRestore previous;
            previous.was_full = full;
            previous.was_maximized = was_maximized;
            if (full)
                previous.saved = placement;
            else if (!GetWindowPlacement(hwnd, &previous.saved))
                throw std::runtime_error("Could not remember the slideshow window placement");
            slideshow_window = previous;
        }
        slideshow_run = SlideshowRun{settings.slide_loop, settings.slide_next_folder, settings.slide_subtree,
                                     settings.slide_random, settings.sort, folder};
        slideshow = true;
        try {
            if (settings.slide_random)
                settings.sort = static_cast<int>(Sort::Random);
            if (settings.sort == static_cast<int>(Sort::Random)) {
                sort_list();
                // Starting a random run keeps the displayed image at the beginning of its
                // first complete cycle. The pixels and animation player remain untouched.
                if (!removed_current && index < files.size()) {
                    std::rotate(files.begin(), files.begin() + index, files.end());
                    index = 0;
                }
            }
            if (slideshow_window && !full)
                fullscreen();
            if (!SetTimer(hwnd, 2, settings.interval, nullptr))
                throw std::runtime_error("Could not start the slideshow timer");
        } catch (...) {
            stop_slideshow();
            throw;
        }
    }
    void stop_slideshow() {
        slideshow = false;
        KillTimer(hwnd, 2);
        if (loading && (slideshow_loading || (slideshow_run && slideshow_run->temporary_random)))
            cancel_load();
        slideshow_loading = false;
        const auto run = std::exchange(slideshow_run, std::nullopt);
        if (run && run->temporary_random) {
            settings.sort = run->previous_sort;
            sort_list();
        }
        if (run)
            refresh_menu();
        // Freeze the start option for this run. Changes in Preferences apply next time.
        const auto previous = std::exchange(slideshow_window, std::nullopt);
        if (!previous)
            return;
        if (full != previous->was_full)
            fullscreen();
        if (full)
            placement = previous->saved;
        else
            SetWindowPlacement(hwnd, &previous->saved);
        was_maximized = previous->was_maximized;
    }
    void reset_animation_clock() {
        KillTimer(hwnd, 1);
        animation_timer = false;
    }
    HMENU main_menu{};
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    DWORD old_style{};
    bool was_maximized{}, bar_seen{}, toolbar_shown{true}, status_shown{true};
    POINT bar_point{};
    unsigned bar_modifiers{};
    bool full{}, topmost{}, panning{}, loading{}, modified{}, slideshow{}, comic{}, editor_mode{},
        selecting{}, drag_armed{}, drag_pending{};
    POINT last{}, drag_start{}, selection_start{};
    SelectionUnits selection_drag_units;
    struct MousePress {
        bool down{}, consumed{}, moved{};
        POINT origin{};
        unsigned held{}, modifiers{};
        bool empty_paste{}, selection_move_pending{};
    };
    std::array<MousePress, 5> mouse_presses;
    unsigned mouse_buttons{}, active_drag_button{}, active_drag_action{};
    POINT zoom_anchor{};
    bool hover_seen{};
    POINT hover_last{};
    int wheel_vertical{}, wheel_horizontal{};
    std::array<unsigned, 2> wheel_context{UINT_MAX, UINT_MAX};
    SelectionRegion selection,selection_before_drag;
    enum class SelectionTool { Rectangle,Freehand,Polygon,Move,Browse,Pan,Pen,Text,Crop };
    SelectionTool selection_tool{SelectionTool::Browse},previous_editor_tool{SelectionTool::Rectangle},selection_drag_tool{SelectionTool::Rectangle};
    bool selection_active() const { return selection_tool<=SelectionTool::Move; }
    SelectionCombine selection_combine{SelectionCombine::Replace};
    std::vector<FloatPoint> selection_points;
    bool polygon_drawing{}, space_pan{}, ants_timer{};
    struct FloatingPaste {
        std::shared_ptr<Image> origin,canvas,source;
        FloatingBox box;
        Player previous_player;
        View previous_view;
        std::optional<SelectionOrigin> original_position;
        bool slideshow{},invalid_metadata{};
        SelectionRegion previous_selection;
        bool text_annotation{};
    };
    std::optional<FloatingPaste> floating;
    std::optional<FloatingDrag> floating_drag;
    unsigned floating_drag_button{};
    struct CropPreview {std::shared_ptr<Image> origin;FloatingBox box;SelectionRegion selection;Player player;double scale{1};};
    std::optional<CropPreview> cropping;
    std::optional<FloatingDrag> crop_drag;
    std::optional<Player> pen_player;
    int left{},top{}, bottom{}, play_height{28};
    std::chrono::steady_clock::time_point last_tick;
#ifdef GVN_APP_TESTS
    std::function<std::chrono::steady_clock::time_point()> animation_now{
        [] { return std::chrono::steady_clock::now(); }};
#else
    static auto animation_now() {
        return std::chrono::steady_clock::now();
    }
#endif
    std::mt19937 random{std::random_device{}()};
    int paints{};
    uint64_t ready_ms{}, idle_cpu_start{}, idle_cpu_used{};
    uint64_t display_ready_ms{};
    double first_draw_ms{};
    DrawMetrics first_draw_phases;
    bool first_draw_software{};
    bool first_draw_succeeded{};
    static uint64_t cpu_ticks() {
        FILETIME created{}, exited{}, kernel{}, user{};
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
        ULARGE_INTEGER k{}, u{};
        k.LowPart = kernel.dwLowDateTime;
        k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime;
        u.HighPart = user.dwHighDateTime;
        return k.QuadPart + u.QuadPart;
    }

    bool checked(UINT id) const {
        if(id==ToggleEditor)return editor_mode;
        if(id==ToolPan)return space_pan||(editor_mode&&selection_tool==SelectionTool::Pan);
        if(id==ToolPen)return editor_mode&&selection_tool==SelectionTool::Pen;
        if(id==ToolText)return editor_mode&&selection_tool==SelectionTool::Text;
        if(id==ToolCrop)return cropping.has_value();
        if(id>=SelectRectangle&&id<=SelectMove)return selection_active()&&int(selection_tool)==int(id-SelectRectangle);
        if(id>=GridFree&&id<=Grid32){const int units[]{1,2,4,8,16,32};return !settings.selection_custom&&settings.grid==units[id-GridFree];}
        if(id==Grid)return settings.selection_custom||(settings.grid!=1&&settings.grid!=2&&settings.grid!=4&&settings.grid!=8&&settings.grid!=16&&settings.grid!=32);
        switch (id) {
        case ToggleHover:
            return settings.mouse.action(MouseGesture::Hover, 0, 0) == MouseHover;
        case Fullscreen:
            return full;
        case Topmost:
            return topmost;
        case ToggleCaption:
            return settings.caption;
        case ToggleOuterFrame:
            return settings.outer_frame;
        case ToggleInnerFrame:
            return settings.inner_frame;
        case ToggleToolbar:
            return full ? settings.full_toolbar : settings.toolbar;
        case ToggleStatus:
            return full ? settings.full_status : settings.status;
        case ToggleMenu:
            return full ? settings.full_menu : settings.menu;
        case ToggleScrollbars:
            return full ? settings.full_scrollbars : settings.scrollbars;
        case Checker:
            return settings.checker;
        case Nearest:
            return (full?settings.full_down:settings.display_down)==DisplaySimple&&(full?settings.full_up:settings.display_up)==DisplaySimple;
        case Loop:
            return player.loop;
        case Slideshow:
            return slideshow;
        case ListLoop:
            return settings.list_loop;
        case SortReverse:
            return settings.reverse;
        case NaturalSort:
            return settings.natural;
        case ShowHidden:
            return settings.hidden;
        case KeepZoom:
            return settings.keep_zoom;
        case Comic:
            return comic;
        case ComicRTL:
            return settings.comic_rtl;
        case ComicSingleWide:
            return settings.comic_single_wide;
        case ComicHeight:
            return settings.comic_mode >= 1 && settings.comic_mode <= 3;
        case SelectMode:
            return editor_mode&&selection_tool==SelectionTool::Rectangle;
        default:
            if (id >= ViewRotateNone && id <= ViewRotateHalf)
                return settings.view_rotation == static_cast<int>(id - ViewRotateNone);
            if (id >= DisplayWindowImage && id <= DisplayUnchanged)
                return (full ? settings.full_display_mode : settings.display_mode) ==
                       static_cast<int>(id - DisplayWindowImage);
            return id >= SortName && id <= SortRandom && settings.sort == static_cast<int>(id - SortName);
        }
    }
    std::optional<std::wstring> file_clipboard_text(UINT id) const {
        const auto source = source_file(!image->path.empty() ? image->path
                                        : before_load ? before_load->requested : requested);
        const auto previous = before_load && before_load->replaced ? &*before_load->replaced : nullptr;
        const auto name = !source.empty() ? source.filename().wstring()
                          : previous ? previous->name : current_name;
        if (id == CopyFileName)
            return name.empty() ? L"クリップボード画像" : name;
        if (id == CopyPath)
            return source.empty() ? (name.empty() ? L"クリップボード画像" : name) : source.wstring();
        if (id == CopyShortPath) {
            if (source.empty())
                return std::nullopt;
            const auto size = GetShortPathNameW(source.c_str(), nullptr, 0);
            if (!size)
                return std::nullopt;
            std::wstring result(size, L'\0');
            const auto length = GetShortPathNameW(source.c_str(), result.data(), size);
            if (!length || length >= size)
                return std::nullopt;
            result.resize(length);
            return result;
        }
        auto source_folder = source.empty() ? (previous ? previous->folder : folder) : source.parent_path();
        if (source_folder.empty())
            source_folder = previous ? previous->clipboard_folder : clipboard_folder;
        if (source_folder.empty())
            source_folder = std::filesystem::current_path();
        return id == CopyFolderName ? source_folder.filename().wstring() : source_folder.wstring();
    }
    void add(HMENU menu, UINT id, const std::wstring &text) {
        auto label = text.substr(0, text.find(L'\t'));
        if (id == SkipBack || id == SkipNext)
            label = std::to_wstring(settings.skip_count) + (id == SkipBack ? L"枚戻る" : L"枚進む");
        const auto key = settings.first_key(id);
        if (!key.empty())
            label += L"\t" + key;
        AppendMenuW(menu,
                    MF_STRING | (checked(id) ? MF_CHECKED : 0) |
                        ((id == ToggleOuterFrame && settings.caption) ||
                         (id >= WallpaperCenter && id <= WallpaperHeight && !image) ||
                         (id == PaletteEdit && (!image || image->frames.size()!=1 || !image->frames[0].indexed)) ||
                         (id == ConvertColorDepth && !image) ||
                         (id == ColorAdjust && !image) ||
                         (id == WallpaperDelete && !has_wallpaper_files(config)) ? MF_GRAYED : 0),
                    id, label.c_str());
    }
    static void separator(HMENU menu) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    static void submenu(HMENU root, HMENU child, const wchar_t *label) {
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(child), label);
    }
    void update_wallpaper_menu(HMENU target) {
        // Refresh only this popup's entries; other instances may have changed the files.
        // Never rebuild a menu while Windows is tracking it, or query the desktop here.
        for (int i = 0; i < GetMenuItemCount(target); ++i) {
            const auto id = GetMenuItemID(target, i);
            if (id >= WallpaperCenter && id <= WallpaperDelete) {
                const bool enabled = id == WallpaperDelete ? has_wallpaper_files(config) : bool(image);
                EnableMenuItem(target, static_cast<UINT>(i), MF_BYPOSITION | (enabled ? MF_ENABLED : MF_GRAYED));
            }
        }
    }
    HMENU menu(bool popup = false) {
        HMENU root = popup ? CreatePopupMenu() : CreateMenu(), m = CreatePopupMenu();
        add(m, Open, L"開く…\tCtrl+O");
        add(m, Reload, L"再読み込み\tF5");
        add(m, NewImage, L"新規画像…");
        separator(m);
        add(m, Save, L"保存\tCtrl+S");
        add(m, SaveAs, L"名前を付けて保存…\tCtrl+Shift+S");
        separator(m);
        add(m, RenameFile, L"名前を変更…\tF2");
        add(m, FileCopy, L"ファイルをコピー…");
        add(m, FileMove, L"ファイルを移動…");
        add(m, FileDelete, settings.recycle_deleted ? L"ごみ箱へ…\tDelete" : L"ファイルを削除…\tDelete");
        add(m, CopyPath, L"パスをコピー");
        HMENU destinations = CreatePopupMenu();
        for (size_t i = 0; i < 10; ++i) {
            const auto name = [&](bool move) {
                const auto &path = (move ? settings.move_destinations : settings.copy_destinations)[i];
                return path.empty() ? std::wstring(L"未登録") : path;
            };
            add(destinations, CopyDestBase + static_cast<UINT>(i),
                L"コピー " + std::to_wstring(i + 1) + L": " + name(false));
            add(destinations, MoveDestBase + static_cast<UINT>(i),
                L"移動 " + std::to_wstring(i + 1) + L": " + name(true));
        }
        separator(destinations);
        add(destinations, ConfigureDestinations, L"登録先を設定…");
        submenu(m, destinations, L"登録先へ振り分け");
        separator(m);
        HMENU recent = CreatePopupMenu();
        for (size_t i = 0; i < settings.history.size(); ++i)
            add(recent, HistoryBase + static_cast<UINT>(i), settings.history[i]);
        submenu(m, recent, L"最近開いたフォルダ");
        HMENU external = CreatePopupMenu();
        add(external, External, L"アプリを選んで開く…");
        for (size_t i = 0; i < 9; ++i) {
            const auto entry = settings.external_application(i);
            if (!entry.name.empty()) {
                std::wstring label = L"(&" + std::to_wstring(i + 1) + L") ";
                for (const auto ch : entry.name) {
                    if (ch == L'&') label += L'&';
                    label += ch == L'\t' || ch == L'\r' || ch == L'\n' ? L' ' : ch;
                }
                add(external, ExternalBase + static_cast<UINT>(i), label);
                if (entry.separator) separator(external);
            }
        }
        add(external, ConfigureExternal, L"外部アプリを登録…");
        submenu(m, external, L"他のアプリで開く");
        HMENU new_windows = CreatePopupMenu();
        add(new_windows, NewWindow, L"空のウィンドウ");
        add(new_windows, NewWindowFile, L"現在のファイルを開く");
        add(new_windows, NewWindowSend, L"画像をクリップボード経由で送る");
        add(new_windows, NewWindowPaste, L"クリップボードの画像を貼り付け");
        submenu(m, new_windows, L"もう1つ起動");
        add(m, Print, L"印刷…\tCtrl+P");
        HMENU wallpaper = CreatePopupMenu();
        add(wallpaper, WallpaperCenter, L"中央に表示");
        add(wallpaper, WallpaperTile, L"並べて表示");
        add(wallpaper, WallpaperStretch, L"伸縮して表示");
        add(wallpaper, WallpaperWidth, L"横幅に合わせる");
        add(wallpaper, WallpaperHeight, L"縦幅に合わせる");
        separator(wallpaper);
        add(wallpaper, WallpaperDelete, L"壁紙ファイルを削除…");
        submenu(m, wallpaper, L"壁紙にする");
        separator(m);
        add(m, Exit, L"終了");
        submenu(root, m, L"ファイル");
        m = CreatePopupMenu();
        add(m, Undo, L"元に戻す\tCtrl+Z");
        add(m, Redo, L"やり直し\tCtrl+Shift+Z");
        separator(m);
        add(m,ToggleEditor,L"画像編集モード");
        HMENU selection_tools=CreatePopupMenu();
        add(selection_tools,ToolPan,L"手のひら（表示を移動）");
        add(selection_tools,SelectRectangle,L"矩形選択\tS");add(selection_tools,SelectFreehand,L"自由選択");
        add(selection_tools,SelectPolygon,L"折れ線選択（ダブルクリック / Enterで完了）");
        add(selection_tools,SelectMove,L"移動・変形");
        submenu(m,selection_tools,L"選択ツール（Shift:追加 / Alt:除外 / Ctrl:交差）");
        add(m, SelectNumbers, L"範囲を数値で指定…");
        HMENU units=CreatePopupMenu();
        const wchar_t *unit_labels[]{L"フリー",L"2 x 2",L"4 x 4",L"8 x 8",L"16 x 16",L"32 x 32"};
        for(UINT n=0;n<6;++n)add(units,GridFree+n,unit_labels[n]);
        separator(units);add(units,Grid,L"カスタム…");
        int unit_position=7;const int common_units[]{1,2,4,8,16,32};
        if(!settings.selection_custom)for(int n=0;n<6;++n)if(settings.grid==common_units[n])unit_position=n;
        CheckMenuRadioItem(units,0,7,unit_position,MF_BYPOSITION);submenu(m,units,L"矩形選択の単位");
        add(m, SelectAll, L"すべて選択\tCtrl+A");
        add(m, ClearSelection, L"選択を解除\tEsc");
        add(m, InvertSelection,L"選択範囲を反転\tCtrl+Shift+I");
        add(m, DeleteSelection,L"選択範囲を削除\tDelete（選択中）");
        separator(m);
        add(m, Copy, L"コピー（アルファ維持）\tCtrl+C");
        add(m, Cut, L"切り取り\tCtrl+X");
        add(m, Paste, L"貼り付け\tCtrl+V");
        add(m, PasteSelection,L"選択範囲でペースト");
        if(floating){add(m,CommitSelection,L"貼り付けを確定\tEnter");add(m,CancelSelection,L"貼り付けを中止\tEsc");add(m,TransformSelection,L"位置・サイズ・角度…");}
        add(m, NewPaste, L"新規画像として貼り付け");
        add(m, CloseImage, L"画像を閉じる\tCtrl+W");
        add(m, Crop, L"選択範囲を切り出す\tCtrl+Y");
        separator(m);
        add(m, ArmDrag, L"次の左ドラッグでファイルを渡す\tF6 / Ctrl+Shift+D");
        HMENU file_names = CreatePopupMenu();
        add(file_names, CopyFileName, L"現在のファイル名");
        add(file_names, CopyPath, L"現在のファイルのフルパス");
        add(file_names, CopyFolderName, L"現在のフォルダ名");
        add(file_names, CopyFolderPath, L"現在のフォルダのパス");
        add(file_names, CopyShortPath, L"現在のファイルのショートパス");
        submenu(m, file_names, L"ファイル名をクリップボードへ");
        submenu(root, m, L"編集");
        m = CreatePopupMenu();
        add(m,ToolPen,L"ペン");add(m,ToolText,L"文字入れ");add(m,ToolCrop,L"トリミング…");
        add(m, RotateRight, L"右90度回転\tR");
        add(m, RotateLeft, L"左90度回転\tShift+R");
        add(m, Rotate180, L"180度回転");
        add(m, RotateAngle, L"角度を指定して回転…");
        add(m, FlipH, L"左右反転");
        add(m, FlipV, L"上下反転");
        separator(m);
        add(m, Resize, L"リサイズ…");
        add(m, Canvas, L"キャンバスサイズ…");
        separator(m);
        add(m, AutoContrast, L"自動コントラスト");
        add(m, AutoLevels, L"自動レベル調整");
        add(m, Levels, L"レベル調整…");
        for (auto [id, label] :
             std::initializer_list<std::pair<UINT, const wchar_t *>>{{Brightness, L"明るさ…"},
                                                                     {Contrast, L"コントラスト…"},
                                                                     {Gamma, L"ガンマ…"},
                                                                     {ColorAdjust, L"色合い調整…"},
                                                                     {Hue, L"色相…"},
                                                                     {Saturation, L"彩度…"},
                                                                     {Negative, L"ネガポジ反転"},
                                                                     {Gray, L"グレースケール"},
                                                                     {Sepia, L"セピア"},
                                                                     {Threshold, L"2値化…"},
                                                                     {Posterize, L"階調削減（ポスタライズ）…"},
                                                                     {ConvertColorDepth, L"減色/増色…"},
                                                                     {PaletteEdit, L"パレット編集…"},
                                                                     {ReplaceColor, L"色を置き換える…"},
                                                                     {Edge, L"輪郭抽出"},
                                                                     {Emboss, L"エンボス"},
                                                                     {Mosaic, L"モザイク…"},
                                                                     {Noise, L"ノイズ…"}})
            add(m, id, label);
        HMENU filters=CreatePopupMenu();
        for(auto [id,label]:std::initializer_list<std::pair<UINT,const wchar_t *>>{{Sharpen,L"シャープ"},{SharpenStrong,L"シャープ <強>"},{SharpenEdges,L"シャープ <輪郭だけ>"},{Unsharp,L"アンシャープマスク…"},{Blur,L"ぼかし"},{BlurStrong,L"ぼかし <強>"},{BlurSoft,L"ぼかし <ソフト>"},{Gaussian,L"ガウスぼかし…"}})add(filters,id,label);
        submenu(m,filters,L"シャープ / ぼかし");
        HMENU swaps = CreatePopupMenu();
        add(swaps, SwapRG, L"R ↔ G");
        add(swaps, SwapRB, L"R ↔ B");
        add(swaps, SwapGB, L"G ↔ B");
        add(swaps, SwapForward, L"R → G → B → R");
        add(swaps, SwapBackward, L"R ← G ← B ← R");
        submenu(m, swaps, L"色の入れ替え");
        HMENU masks = CreatePopupMenu();
        add(masks, GlassV, L"縦ガラス");
        add(masks, GlassH, L"横ガラス");
        add(masks, MaskNegative, L"ネガポジ反転");
        add(masks, Xor80, L"XOR 0x80");
        add(masks, MaskSwap, L"RGB入れ替え");
        add(masks, Q0Mask, L"Q0マスク");
        add(masks, WinMask, L"Winマスク");
        add(masks, FLMask, L"FLマスク");
        add(masks, CPMask, L"CPマスク…");
        add(masks, MekoForward, L"Mekoマスク 正順");
        add(masks, MekoReverse, L"Mekoマスク 逆順");
        add(masks, RainbowMask, L"虹色マスク…");
        AppendMenuW(masks, MF_SEPARATOR, 0, nullptr);
        add(masks, ConfigureMask, L"マスクオプション…");
        submenu(m, masks, L"マスク");
        submenu(root, m, L"画像");
        m = CreatePopupMenu();
        add(m, Previous, L"前の画像\t←");
        add(m, Next, L"次の画像\t→");
        add(m, First, L"先頭\tHome");
        add(m, Last, L"末尾\tEnd");
        add(m, SkipBack, L"10枚戻る\tPageUp");
        add(m, SkipNext, L"10枚進む\tPageDown");
        add(m, Jump, L"番号へジャンプ…\tJ");
        separator(m);
        add(m, ParentFolder, L"親フォルダ\tBackspace");
        add(m, ChildFolder, L"子フォルダ…");
        add(m, PreviousFolder, L"前のフォルダ\tCtrl+PageUp");
        add(m, NextFolder, L"次のフォルダ\tCtrl+PageDown");
        add(m, PreviousAdjacentFolder, L"前隣のフォルダ");
        add(m, NextAdjacentFolder, L"次隣のフォルダ");
        add(m, RefreshList, L"リストを更新");
        separator(m);
        HMENU sorting = CreatePopupMenu();
        for (UINT i = SortName; i <= SortRandom; ++i)
            add(sorting, i,
                std::array<const wchar_t *, 5>{L"ファイル名", L"更新日時", L"サイズ", L"拡張子",
                                               L"ランダム"}[i - SortName]);
        separator(sorting);
        add(sorting, SortReverse, L"逆順");
        add(sorting, NaturalSort, L"数字を自然順に並べる");
        add(sorting, ShowHidden, L"隠しファイルも含める");
        submenu(m, sorting, L"並び順");
        add(m, ListLoop, L"末尾から先頭に戻る");
        add(m, Slideshow, L"スライドショー\tF8");
        add(m, SlideInterval, L"スライド間隔…");
        submenu(root, m, L"リスト");
        m = CreatePopupMenu();
        add(m, Fullscreen, L"全画面\tF11");
        add(m, Actual, L"原寸\t1");
        add(m, FitImage, L"画面に収める\t0");
        add(m, FitWidth, L"幅に合わせる");
        add(m, FitHeight, L"高さに合わせる");
        add(m, FitCover, L"画面を埋める");
        add(m, ZoomIn, L"拡大\t+");
        add(m, ZoomOut, L"縮小\t-");
        add(m, ZoomNumber, L"倍率を指定…");
        add(m, KeepZoom, L"画像切り替え時に倍率を維持");
        separator(m);
        add(m, Comic, L"漫画の見開き表示");
        add(m, ComicRTL, L"右から左に読む");
        add(m, ComicSingleWide, L"横長の画像は単独で表示");
        add(m, ComicHeight, L"見開きの高さを合わせる");
        add(m, ComicOffset, L"見開きの組を1枚ずらす");
        add(m, ComicOffsetBack, L"見開きの組を1枚戻す");
        separator(m);
        add(m, Checker, L"透明部分を市松模様で表示");
        add(m, Background, L"背景色…");
        add(m, Nearest, L"無補正表示");
        add(m, ToggleHover, L"ホバースクロール");
        add(m, Topmost, L"常に手前");
        add(m, ToggleToolbar, L"ツールバー");
        add(m, ToggleStatus, L"ステータスバー");
        add(m, ToggleMenu, L"メニューバー\tF10");
        add(m, ToggleScrollbars, L"スクロールバー");
        auto window_menu = CreatePopupMenu();
        for (UINT id : {WindowRestore, WindowMinimize, WindowMaximize, ToggleCaption, ToggleOuterFrame,
                        ToggleInnerFrame})
            add(window_menu, id, command_name(id));
        submenu(m, window_menu, L"ウィンドウ");
        auto display_menu = CreatePopupMenu();
        for (UINT id = DisplayWindowImage; id <= DisplayUnchanged; ++id)
            add(display_menu, id, command_name(id));
        submenu(m, display_menu, L"表示オプション");
        auto rotation_menu = CreatePopupMenu();
        const wchar_t *rotation_labels[]{
            L"回転しない",       L"左へ90度",         L"右へ90度",         L"縦長だけ左へ90度",
            L"縦長だけ右へ90度", L"横長だけ左へ90度", L"横長だけ右へ90度", L"180度"};
        for (UINT id = ViewRotateNone; id <= ViewRotateHalf; ++id)
            add(rotation_menu, id, rotation_labels[id - ViewRotateNone]);
        CheckMenuRadioItem(rotation_menu, ViewRotateNone, ViewRotateHalf,
                           ViewRotateNone + settings.view_rotation, MF_BYCOMMAND);
        submenu(m, rotation_menu, L"表示用回転");
        submenu(root, m, L"表示");
        m = CreatePopupMenu();
        add(m, Play, L"再生 / 一時停止\tSpace");
        add(m, StopAnimation, L"停止して先頭へ");
        add(m, FramePrevious, L"前のフレーム\t[");
        add(m, FrameNext, L"次のフレーム\t]");
        add(m, FirstFrame, L"先頭フレーム");
        add(m, LastFrame, L"末尾フレーム");
        add(m, Loop, L"アニメーションをループ");
        submenu(root, m, L"アニメーション");
        m = CreatePopupMenu();
        add(m, Preferences, L"設定…");
        add(m, Info, L"画像情報\tI");
        add(m, Association, L"既定のアプリを設定…");
        add(m, Help, L"操作説明\tF1");
        add(m, About, L"バージョン情報…");
        submenu(root, m, L"その他");
        return root;
    }
    void refresh_menu() {
        auto next = menu();
        SetMenu(hwnd, (full ? settings.full_menu : settings.menu) ? next : nullptr);
        if (main_menu)
            DestroyMenu(main_menu);
        main_menu = next;
        for (auto bar : {toolbar.hwnd, playback.toolbar.hwnd,editor_tools.hwnd})
            for (int i = 0; i < SendMessageW(bar, TB_BUTTONCOUNT, 0, 0); ++i) {
                TBBUTTON button{};
                SendMessageW(bar, TB_GETBUTTON, i, reinterpret_cast<LPARAM>(&button));
                if (button.idCommand && button.idCommand != Play && button.idCommand != Loop) {
                    auto label = std::wstring(command_name(button.idCommand));
                    const auto key = settings.first_key(button.idCommand);
                    if (!key.empty())
                        label += L" (" + key + L")";
                    (bar == toolbar.hwnd ? toolbar : bar==editor_tools.hwnd?editor_tools:playback.toolbar).text(button.idCommand, label.c_str());
                }
            }
        DrawMenuBar(hwnd);
    }
    void apply_window_style() {
        DWORD style = full ? old_style : static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        style &= ~WS_OVERLAPPEDWINDOW;
        if (settings.caption)
            style |= WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        if (settings.caption || settings.outer_frame)
            style |= WS_THICKFRAME;
        if (full)
            old_style = style;
        else
            SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        ex = !full && settings.inner_frame ? ex | WS_EX_CLIENTEDGE : ex & ~WS_EX_CLIENTEDGE;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    static MONITORINFO monitor_info(HWND window) {
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
        return monitor;
    }
    void position_window(RECT wanted, bool center) {
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromRect(&wanted, MONITOR_DEFAULTTONEAREST), &monitor);
        const auto &work = monitor.rcWork;
        const auto minimum = minimum_window_size(wanted.right - wanted.left);
        const auto placed =
            place_window({wanted.left, wanted.top, std::max(minimum.cx, wanted.right - wanted.left),
                          std::max(minimum.cy, wanted.bottom - wanted.top)},
                         {work.left, work.top, work.right - work.left, work.bottom - work.top}, center);
        SetWindowPos(hwnd, nullptr, placed.x, placed.y, placed.w, placed.h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void position_custom_window() {
        const auto minimum = minimum_window_size(settings.window_width);
        const Rect wanted{settings.window_x, settings.window_y,
                          std::max<int>(minimum.cx, settings.window_width),
                          std::max<int>(minimum.cy, settings.window_height)};
        const auto endpoint = [](int origin, int size) {
            return static_cast<LONG>(std::clamp<int64_t>(static_cast<int64_t>(origin) + size, LONG_MIN, LONG_MAX));
        };
        RECT nearest{wanted.x, wanted.y, endpoint(wanted.x, wanted.w), endpoint(wanted.y, wanted.h)};
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromRect(&nearest, MONITOR_DEFAULTTONEAREST), &monitor))
            throw std::runtime_error("Could not read the custom window monitor");
        const auto &work = monitor.rcWork;
        if (settings.window_center) {
            const auto placed = place_window(wanted,
                {work.left, work.top, work.right - work.left, work.bottom - work.top}, true);
            SetWindowPos(hwnd, nullptr, placed.x, placed.y, placed.w, placed.h, SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }
        const Rect desktop{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                           GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        const auto placed = place_window(wanted, desktop, false);
        const RECT screen{placed.x, placed.y, endpoint(placed.x, placed.w), endpoint(placed.y, placed.h)};
        if (!GetMonitorInfoW(MonitorFromRect(&screen, MONITOR_DEFAULTTONEAREST), &monitor))
            throw std::runtime_error("Could not read the placed window monitor");
        // Controls and GetWindowRect use screen coordinates; WINDOWPLACEMENT uses workspace coordinates.
        const auto dx = monitor.rcWork.left - monitor.rcMonitor.left;
        const auto dy = monitor.rcWork.top - monitor.rcMonitor.top;
        WINDOWPLACEMENT saved{sizeof(saved)};
        if (!GetWindowPlacement(hwnd, &saved))
            throw std::runtime_error("Could not read the custom window placement");
        saved.flags = 0;
        saved.showCmd = IsWindowVisible(hwnd) ? SW_SHOWNOACTIVATE : SW_HIDE;
        saved.rcNormalPosition = {screen.left - dx, screen.top - dy, screen.right - dx, screen.bottom - dy};
        if (!SetWindowPlacement(hwnd, &saved))
            throw std::runtime_error("Could not apply the custom window placement");
    }
    void remember_window() {
        WINDOWPLACEMENT saved{sizeof(saved)};
        const bool saved_full = slideshow_window ? slideshow_window->was_full : full;
        const bool saved_maximized = slideshow_window ? slideshow_window->was_maximized : was_maximized;
        if (slideshow_window)
            saved = slideshow_window->saved;
        else if (full)
            saved = placement;
        else if (!GetWindowPlacement(hwnd, &saved))
            return;
        const auto rect = saved.rcNormalPosition;
        if (rect.right <= rect.left || rect.bottom <= rect.top)
            return;
        settings.last_x = rect.left;
        settings.last_y = rect.top;
        settings.last_width = rect.right - rect.left;
        settings.last_height = rect.bottom - rect.top;
        settings.last_state = saved_full ? 2
                              : (saved.showCmd == SW_SHOWMAXIMIZED || (IsIconic(hwnd) && saved_maximized)) ? 1
                                                                                                          : 0;
        settings.last_window_valid = true;
    }
    void remember_startup_file() {
        settings.last_file = image ? requested.wstring() : std::wstring{};
        settings.last_archive_entry.clear();
        if (!image || archive.empty() || requested.empty())
            return;
        for (const auto &entry : archive_entries)
            if (same_path(requested,
                          std::filesystem::path(archive.wstring() + L"|" + std::to_wstring(entry.index)))) {
                settings.last_archive_entry = entry.name;
                break;
            }
    }
    std::wstring caption_name() const {
        const auto previous = before_load && before_load->replaced ? &*before_load->replaced : nullptr;
        const auto &displayed_name = previous ? previous->name : current_name;
        const auto name = displayed_name.empty() ? L"クリップボード画像" : displayed_name;
        if (settings.title_mode == 4)
            return L"";
        if (settings.title_mode == 1 || settings.title_mode == 2) {
            const auto source = source_file(image && !image->path.empty() ? image->path
                                            : before_load ? before_load->requested : requested);
            if (!source.empty()) {
                auto text = source.wstring();
                if (settings.title_mode == 2) {
                    PCWSTR root_end{};
                    const auto base_name = source.filename();
                    // The reference retains the full drive/share path for a file
                    // directly in a volume root. This is a lexical check only.
                    if (FAILED(PathCchSkipRoot(text.c_str(), &root_end)) || !root_end ||
                        base_name.native() != root_end)
                        text = L"\\" + (source.parent_path().filename() / base_name).wstring();
                }
                const auto &displayed_archive = previous ? previous->archive : archive;
                if (!displayed_archive.empty()) {
                    const auto separator = name.find(L" / ");
                    if (separator != std::wstring::npos)
                        text += name.substr(separator);
                }
                return text;
            }
        }
        if (settings.title_mode == 3)
            return std::filesystem::path(name).stem().wstring();
        return name;
    }
    void apply_display_policy(bool no_activate = false) {
        if (!no_activate)
            deferred_display_policy = false;
        if (!image || settings.keep_zoom)
            return;
        (full ? full_display_policy_pending : normal_display_policy_pending) = false;
        int mode = full ? settings.full_display_mode : settings.display_mode;
        if (full) {
            if (mode == 0 || mode == 5)
                view.apply_fit(Fit::Actual);
            else
                view.apply_fit(full_fit, mode == 2 || mode == 4);
            return;
        }
        if (mode == 0 || mode == 3 || mode == 4) {
            if (no_activate && (IsIconic(hwnd) || (IsZoomed(hwnd) && GetForegroundWindow() != hwnd))) {
                deferred_display_policy = true;
                view.apply_fit(normal_fit);
                return;
            }
            const auto work = monitor_info(hwnd).rcWork;
            RECT outer{};
            GetWindowRect(hwnd, &outer);
            if (IsZoomed(hwnd) || IsIconic(hwnd))
                ShowWindow(hwnd, SW_RESTORE);
            int width = work.right - work.left, height = work.bottom - work.top;
            if (mode != 3) {
                const UINT dpi = GetDpiForWindow(hwnd);
                RECT frame{};
                if (!AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_STYLE)),
                        GetMenu(hwnd)!=nullptr, static_cast<DWORD>(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)), dpi))
                    throw std::runtime_error("Could not measure the image window frame");
                const int extra_x=frame.right-frame.left, extra_y=frame.bottom-frame.top;
                const int image_w=static_cast<int>(image->width), image_h=static_cast<int>(image->height);
                width=image_w+left+extra_x;
                if (settings.window_toolbar_min && settings.toolbar)
                    width=std::max(width,toolbar.ideal_width()+extra_x);
                if (const auto menu=GetMenu(hwnd)) {
                    int menu_width=0;
                    for(int i=0;i<GetMenuItemCount(menu);++i) {
                        RECT item{};
                        if(GetMenuItemRect(hwnd,menu,i,&item))menu_width+=item.right-item.left;
                    }
                    width=std::max(width,menu_width+extra_x);
                }
                const bool wider_than_desktop=width>work.right-work.left;
                width=std::min(width,static_cast<int>(work.right-work.left));
                const auto minimum=minimum_window_size(width);
                width=std::max(width,static_cast<int>(minimum.cx));
                const int client_width=std::max(1,width-extra_x);
                // Bar wrapping belongs to the destination width. Using top/bottom
                // from the previous window can hide part of a small image or leave
                // extra empty space when the toolbar changes its number of rows.
                toolbar.configure(settings.toolbar_center,settings.toolbar_line,settings.toolbar_large);
                const auto panel=cropping?EditorPanel::Crop:floating?(floating->text_annotation?EditorPanel::Text:EditorPanel::Transform):
                    selection_tool==SelectionTool::Pen?EditorPanel::Pen:selection_tool==SelectionTool::Text?EditorPanel::Text:EditorPanel::None;
                const int upper=(toolbar_shown?toolbar.measure_height(client_width,dpi):0)+
                    ((editor_mode||floating||cropping)?EditorChrome::top_height(client_width,dpi,panel,floating||cropping):0);
                const int lower=(status_shown?measure_status_height(dpi,true):0)+
                    (image->animated()?playback.measure_height(client_width,dpi):0);
                const int horizontal=settings.scrollbars&&image_w>client_width?
                    GetSystemMetricsForDpi(SM_CYHSCROLL,dpi):0;
                height=std::max(image_h+extra_y+upper+lower+horizontal,static_cast<int>(minimum.cy));
                if(mode==4)
                    mode=wider_than_desktop||height>work.bottom-work.top?3:0;
            }
            RECT wanted{outer.left, outer.top, outer.left + width, outer.top + height};
            if (mode == 3)
                wanted = work;
            else if (settings.window_center) {
                wanted.left = outer.left + (outer.right - outer.left - width) / 2;
                wanted.top = outer.top + (outer.bottom - outer.top - height) / 2;
                wanted.right = wanted.left + width;
                wanted.bottom = wanted.top + height;
            }
            position_window(wanted, false);
            layout();
        }
        if (mode == 0 || mode == 5)
            view.apply_fit(Fit::Actual);
        else
            view.apply_fit(normal_fit, mode == 2);
    }
    void prepare_settings() {
        if (!smoke && !settings_ready) {
            if (config.empty())
                config = settings_path();
            settings.load(config);
            settings_ready = true;
        }
    }
    void initialize() {
        prepare_settings();
        render_settings();
        view.fit = normal_view.fit = full_view.fit = static_cast<Fit>(settings.initial_fit);
        normal_fit = static_cast<Fit>(settings.initial_fit);
        full_fit = full_view.fit = static_cast<Fit>(settings.full_initial_fit);
        apply_window_style();
        topmost = settings.topmost;
        SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        toolbar.create(hwnd);
        editor_tools.create(hwnd,false,true);
        status =
            CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | SBARS_SIZEGRIP | SBARS_TOOLTIPS,
                            0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        playback.create(hwnd);
        slider = playback.slider;
        canvas_horizontal = CreateWindowExW(0, L"SCROLLBAR", L"", WS_CHILD | WS_TABSTOP | SBS_HORZ, 0, 0, 0,
                                            0, hwnd, nullptr, nullptr, nullptr);
        canvas_vertical = CreateWindowExW(0, L"SCROLLBAR", L"", WS_CHILD | WS_TABSTOP | SBS_VERT, 0, 0, 0, 0,
                                          hwnd, nullptr, nullptr, nullptr);
        canvas_corner =
            CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, hwnd, nullptr, nullptr, nullptr);
        watch_bar_controls(hwnd);
        loader = std::make_unique<Loader>(
            [this](Loaded value) {
                auto result = std::make_unique<Loaded>(std::move(value));
                if (PostMessageW(hwnd, kLoaded, 0, reinterpret_cast<LPARAM>(result.get())))
                    result.release();
            },
            static_cast<size_t>(settings.cache_mib) * 1024 * 1024);
        loader->configure(static_cast<size_t>(settings.cache_mib) * 1024 * 1024, settings.prefetch_enabled);
        DragAcceptFiles(hwnd, TRUE);
        ime.apply(hwnd, settings.disable_ime);
        layout();
        refresh_menu();
        update();
    }
    bool bar_visible(bool upper) const {
        const bool enabled = upper ? (full ? settings.full_toolbar : settings.toolbar)
                                   : (full ? settings.full_status : settings.status);
        if (!enabled)
            return false;
        if (!(full || (upper ? settings.toolbar_auto : settings.status_auto)))
            return true;
        if (!bar_seen || (bar_modifiers & 3))
            return false;
        const bool previous = upper ? toolbar_shown : status_shown;
        if (mouse_buttons || GetCapture())
            return previous;
        RECT area{};
        GetClientRect(hwnd, &area);
        const int edge = MulDiv(6, GetDpiForWindow(hwnd), 96);
        const int extent = upper ? toolbar.height() : EditorChrome::status_height(GetDpiForWindow(hwnd),image!=nullptr);
        return upper ? bar_point.y < (previous ? extent + edge : edge)
                     : bar_point.y >= area.bottom - (previous ? extent + edge : edge);
    }
    void observe_bars(POINT point, unsigned mods) {
        RECT area{};
        GetClientRect(hwnd, &area);
        bar_seen = PtInRect(&area, point) != FALSE;
        bar_point = point;
        bar_modifiers = mods;
        if (toolbar_shown != bar_visible(true) || status_shown != bar_visible(false)) {
            layout();
            update();
        }
    }
    static LRESULT CALLBACK bar_control_proc(HWND child, UINT message, WPARAM w, LPARAM l, UINT_PTR,
                                             DWORD_PTR data) {
        auto self = reinterpret_cast<App *>(data);
        if (message == WM_CAPTURECHANGED && self->canvas_tracking &&
            (child == self->canvas_horizontal || child == self->canvas_vertical))
            self->finish_overlay_scroll();
        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP ||
             message == WM_SYSKEYUP) &&
            (w == VK_CONTROL || w == VK_SHIFT) && self->bar_seen)
            self->observe_bars(self->bar_point, pointer_modifiers());
        if (message == WM_MOUSEMOVE) {
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            MapWindowPoints(child, self->hwnd, &point, 1);
            self->observe_bars(point, pointer_modifiers(w));
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, child, 0};
            TrackMouseEvent(&track);
        } else if (message == WM_MOUSELEAVE) {
            const POINT point = self->cursor_client();
            RECT area{};
            GetClientRect(self->hwnd, &area);
            if (!PtInRect(&area, point))
                self->observe_bars(point, pointer_modifiers());
        }
        return DefSubclassProc(child, message, w, l);
    }
    void watch_bar_controls(HWND parent) {
        for (auto child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            SetWindowSubclass(child, bar_control_proc, 17, reinterpret_cast<DWORD_PTR>(this));
            watch_bar_controls(child);
        }
    }
    int measure_status_height(UINT dpi,bool has_image) {
        const int wanted=EditorChrome::status_height(dpi,has_image);if(!status)return wanted;
        if(status_min_height!=wanted){
            // Themed status padding and the native outer border have different sizes.
            // Measure the actual control rather than subtracting its text-part padding.
            SendMessageW(status,SB_SETMINHEIGHT,wanted,0);SendMessageW(status,WM_SIZE,0,0);
            RECT bounds{};GetWindowRect(status,&bounds);const int actual=bounds.bottom-bounds.top;
            if(actual!=wanted)SendMessageW(status,SB_SETMINHEIGHT,std::max(0,2*wanted-actual),0);
            status_min_height=wanted;
        }
        SendMessageW(status,WM_SIZE,0,0);RECT bounds{};GetWindowRect(status,&bounds);return bounds.bottom-bounds.top;
    }
    SIZE minimum_window_size(int outer_width) {
        const UINT dpi = GetDpiForWindow(hwnd);
        auto px = [&](int value) { return MulDiv(value, dpi ? dpi : 96, 96); };
        if (!image || !image->animated())
            return {GetSystemMetricsForDpi(SM_CXMINTRACK, dpi ? dpi : 96),
                    GetSystemMetricsForDpi(SM_CYMINTRACK, dpi ? dpi : 96)};
        SIZE result{px(340), px(240)};
        if (!toolbar.hwnd || full)
            return result;
        RECT frame{};
        AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)),
                                 GetMenu(hwnd) != nullptr,
                                 static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), dpi);
        const int extra_x = frame.right - frame.left;
        int extra_y = frame.bottom - frame.top;
        RECT current_outer{}, current_client{};
        GetWindowRect(hwnd, &current_outer);
        GetClientRect(hwnd, &current_client);
        // AdjustWindowRectExForDpi accounts for one menu row. A narrow window may have more.
        if (outer_width == current_outer.right - current_outer.left)
            extra_y = std::max(
                extra_y, static_cast<int>(current_outer.bottom - current_outer.top - current_client.bottom));
        toolbar.configure(settings.toolbar_center, settings.toolbar_line, settings.toolbar_large);
        playback.toolbar.configure(false, false, settings.toolbar_large);
        const bool animated = image && image->animated();
        if (animated) {
            playback.measure_height(std::max(1, outer_width - extra_x), dpi);
            result.cx =
                std::max(result.cx, static_cast<LONG>(playback.toolbar.ideal_width() + px(6) + extra_x));
        }
        const int width = std::max(static_cast<int>(result.cx), outer_width) - extra_x;
        const int upper = settings.toolbar ? toolbar.measure_height(width, dpi) : 0;
        const int lower =
            (settings.status ? measure_status_height(dpi,true) : 0) + (animated ? playback.measure_height(width, dpi) : 0);
        result.cy = std::max(
            result.cy,
            static_cast<LONG>(upper + lower + px(64) + extra_y +
                              (settings.scrollbars ? GetSystemMetricsForDpi(SM_CXVSCROLL, dpi) : 0)));
        const auto work = monitor_info(hwnd).rcWork;
        result.cx = std::min(result.cx, work.right - work.left);
        result.cy = std::min(result.cy, work.bottom - work.top);
        return result;
    }
    void layout() {
        if (!toolbar.hwnd)
            return;
        if (!full && !IsZoomed(hwnd) && !IsIconic(hwnd)) {
            RECT outer{};
            GetWindowRect(hwnd, &outer);
            const auto minimum = minimum_window_size(outer.right - outer.left);
            if (outer.right - outer.left < minimum.cx || outer.bottom - outer.top < minimum.cy) {
                SetWindowPos(hwnd, nullptr, 0, 0, std::max(outer.right - outer.left, minimum.cx),
                             std::max(outer.bottom - outer.top, minimum.cy),
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                return;
            }
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const UINT dpi = GetDpiForWindow(hwnd);
        const bool centered = full || settings.center_image;
        const double limit = settings.fit_max / 100.0;
        if (view.centered != centered || view.fit_limit != limit) {
            view.centered = centered;
            view.fit_limit = limit;
            if (!view.manual)
                view.apply_fit(view.fit, view.shrink_only);
        }
        toolbar.configure(settings.toolbar_center, settings.toolbar_line, settings.toolbar_large);
        playback.toolbar.configure(false, false, settings.toolbar_large);
        toolbar_shown = bar_visible(true);
        status_shown = bar_visible(false);
        top = toolbar.layout(rc.right, dpi, toolbar_shown);
        ShowWindow(status,status_shown?SW_SHOW:SW_HIDE);
        const int measured_status=measure_status_height(dpi,image!=nullptr);
        const int status_height=status_shown?measured_status:0;
        const bool animated = image && image->animated();
        play_height = playback.layout(rc.right, rc.bottom, status_height, dpi, animated);
        bottom = status_height + play_height;
        const auto panel=cropping?EditorPanel::Crop:floating?(floating->text_annotation?EditorPanel::Text:EditorPanel::Transform):
            selection_tool==SelectionTool::Pen?EditorPanel::Pen:selection_tool==SelectionTool::Text?EditorPanel::Text:EditorPanel::None;
        top+=editor_chrome.layout_top(hwnd,rc.right,top,dpi,editor_mode||floating||cropping,panel,floating||cropping);
        editor_tools.configure(false,false,settings.toolbar_large);
        left=editor_tools.layout_tools(dpi,top,std::max(1L,rc.bottom-top-bottom),editor_mode);
        renderer.viewport_left=left;
        layout_status_parts();
        sync_canvas_scrollbars();
        renderer.resize(rc.right, rc.bottom);
        ime.apply(hwnd, settings.disable_ime);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void status_text(int part, const std::wstring &text) {
        SendMessageW(status, SB_SETTEXTW, part, reinterpret_cast<LPARAM>(text.c_str()));
    }
    void layout_status_parts() {
        const auto dpi=GetDpiForWindow(status);const auto px=[&](int n){return MulDiv(n,dpi,96);};
        if(!image){const int edge=-1;SendMessageW(status,SB_SETPARTS,1,reinterpret_cast<LPARAM>(&edge));editor_chrome.layout_status(hwnd,status,{},dpi,false,false);return;}
        RECT area{};GetClientRect(status,&area);const int right=std::max(1L,area.right-px(20));
        const int export_width=std::min(right,std::clamp(int(right*.42),px(260),px(420))),information=std::max(0,right-export_width-px(6));
        int edges[5]{};
        if(information>=px(520)){edges[0]=px(160);edges[1]=px(210);edges[2]=px(270);edges[3]=px(410);}
        else if(information>=px(280)){edges[0]=edges[1]=px(145);edges[2]=edges[3]=px(200);}
        else if(information>=px(145)){edges[0]=edges[1]=information-px(45);edges[2]=edges[3]=information;}
        else {edges[0]=edges[1]=edges[2]=edges[3]=information;}
        edges[4]=information;SendMessageW(status,SB_SETPARTS,5,reinterpret_cast<LPARAM>(edges));
        const RECT panel{right-export_width,px(2),right,area.bottom-px(2)};
        editor_chrome.layout_status(hwnd,status,panel,dpi,status_shown,image&&!loading&&!floating&&!cropping);
    }
    void sync_canvas_scrollbars() {
        if (!canvas_horizontal)
            return;
        RECT client{};
        GetClientRect(hwnd, &client);
        const int width = std::max(1L, client.right-left);
        const int height = std::max(1L, client.bottom - top - bottom);
        const int thickness = GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(hwnd));
        const auto bars = arrange_scrollbars(
            view, width, height, thickness, image && (full ? settings.full_scrollbars : settings.scrollbars));
        if (canvas_width != width || canvas_height != height || canvas_top != top ||canvas_left!=left||
            canvas_thickness != thickness || bars.horizontal != canvas_bars.horizontal ||
            bars.vertical != canvas_bars.vertical) {
            canvas_width = width;
            canvas_height = height;
            canvas_top = top;
            canvas_left=left;
            canvas_thickness = thickness;
            canvas_bars = bars;
            SetWindowPos(canvas_horizontal, nullptr, left, top + static_cast<int>(view.height),
                         static_cast<int>(view.width), thickness, SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(canvas_vertical, nullptr, left+static_cast<int>(view.width), top, thickness,
                         static_cast<int>(view.height), SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(canvas_corner, nullptr, left+static_cast<int>(view.width),
                         top + static_cast<int>(view.height), thickness, thickness,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(canvas_horizontal, bars.horizontal ? SW_SHOWNA : SW_HIDE);
            ShowWindow(canvas_vertical, bars.vertical ? SW_SHOWNA : SW_HIDE);
            ShowWindow(canvas_corner, bars.horizontal && bars.vertical ? SW_SHOWNA : SW_HIDE);
        }
        const auto range = [](HWND control, double content, double page, double offset) {
            SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
            info.nMax = std::max(0, static_cast<int>(std::ceil(content)) - 1);
            info.nPage = static_cast<UINT>(page);
            info.nPos = static_cast<int>(std::lround(-offset));
            SetScrollInfo(control, SB_CTL, &info, TRUE);
        };
        if (bars.horizontal)
            range(canvas_horizontal, view.free_pan?view.x_range().second-view.x_range().first+view.width:view.canvas_w * view.scale, view.width, view.free_pan?view.pan_x-view.x_range().second:view.left());
        if (bars.vertical)
            range(canvas_vertical, view.free_pan?view.y_range().second-view.y_range().first+view.height:view.canvas_h * view.scale, view.height, view.free_pan?view.pan_y-view.y_range().second:view.top());
    }
    void canvas_scroll(HWND control, UINT action) {
        if (!image)
            return;
        if (action == SB_ENDSCROLL) {
            finish_overlay_scroll();
            return;
        }
        const bool horizontal = control == canvas_horizontal;
        SCROLLINFO info{sizeof(info), SIF_ALL};
        if (!GetScrollInfo(control, SB_CTL, &info))
            return;
        const int maximum = std::max(0, info.nMax - static_cast<int>(info.nPage) + 1);
        const int line = MulDiv(16, GetDpiForWindow(hwnd), 96);
        int next = info.nPos;
        switch (action) {
        case SB_LINELEFT:
            next -= line;
            break;
        case SB_LINERIGHT:
            next += line;
            break;
        case SB_PAGELEFT:
            next -= std::max(line, static_cast<int>(info.nPage) - line);
            break;
        case SB_PAGERIGHT:
            next += std::max(line, static_cast<int>(info.nPage) - line);
            break;
        case SB_LEFT:
            next = 0;
            break;
        case SB_RIGHT:
            next = maximum;
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            next = info.nTrackPos;
            break;
        default:
            return;
        }
        next = std::clamp(next, 0, maximum);
        canvas_tracking = action == SB_THUMBTRACK;
        mark_overlay_scroll();
        const double pan = view.free_pan ? (horizontal?view.x_range():view.y_range()).second-next
            : -next - (horizontal ? view.base_x() : view.base_y());
        (horizontal ? view.pan_x : view.pan_y) = pan;
        view.manual = true;
        update();
    }
    void sync_ants_timer() {
        const bool needed=(image||floating)&&(!selection.empty()||renderer.selection_path.size()>1||floating)&&!IsIconic(hwnd);
        if(needed&&!ants_timer)ants_timer=SetTimer(hwnd,39,80,nullptr)!=0;
        else if(!needed&&ants_timer){KillTimer(hwnd,39);ants_timer=false;}
    }
    void update() {
        sync_ants_timer();
        sync_canvas_scrollbars();
        render_settings();
        layout_status_parts();
        std::wostringstream title;
        if (image) {
            title << (modified ? L"* " : L"") << caption_name() << L" <" << (view.manual ? L"" : L"Fit ")
                  << std::fixed << std::setprecision(0) << view.scale * 100 << L" %> — GVN";
            if (!status_shown && !files.empty() && !removed_current)
                title << L" (" << index + 1 << L" / " << files.size() << L")";
            status_text(0,
                        std::to_wstring(image->width) + L" × " + std::to_wstring(image->height) + L"  RGBA");
            status_text(1, files.empty() || removed_current
                               ? L"—"
                               : std::to_wstring(index + 1) + L" / " + std::to_wstring(files.size()));
            std::wostringstream scale;
            scale << std::fixed << std::setprecision(1) << view.scale * 100 << L" %";
            status_text(2, scale.str());
            status_text(3, size_text(image->file_bytes) + L" / " + size_text(image->bytes()));
            std::wstring detail = image->format + L"  " + std::to_wstring(image->load_ms) + L" ms";
            if (image->animated())
                detail +=
                    L"  " + std::to_wstring(player.frame + 1) + L"/" + std::to_wstring(image->frames.size());
            if (!selection.empty())
                detail += L"  選択 " + std::to_wstring(selection.w) + L"×" + std::to_wstring(selection.h);
            if (loading)
                detail += L"  読み込み中…";
            if (renderer.filter(view) != DisplaySimple && renderer.sampling_limited(view))
                detail += L"  画質: 高速Linear（表示領域の上限）";
            if (drag_armed)
                detail = L"左ドラッグでファイルを渡す（Escで解除）";
            status_text(4, detail);
        } else {
            title << (folder.empty() ? L"GVN Image Viewer" : folder.wstring() + L" — GVN");
            status_text(0, loading ? L"画像を読み込み中です…"
                                  : L"画像を開いてください。タップするとクリップボードの内容がペーストされます");
        }
        renderer.overlay.text = title.str();
        SetWindowTextW(hwnd, renderer.overlay.text.c_str());
        refresh_overlay();
        if (image && image->animated()) {
            playback.update(player.frame, image->frames.size(), player.playing, player.loop,
                            player.honor_file_loops, image->file_loops);
        }
        if (image && image->animated() && player.playing) {
            if (!animation_timer) {
                last_tick = animation_now();
                SetTimer(hwnd, 1, 16, nullptr);
                animation_timer = true;
            }
        } else {
            KillTimer(hwnd, 1);
            animation_timer = false;
        }
        for (UINT id : {Save, Copy, ArmDrag, Info, ZoomIn, ZoomOut, FitImage, Actual, Fullscreen})
            toolbar.enable(id, image != nullptr);
        toolbar.enable(Undo, undo_image != nullptr);
        toolbar.enable(Redo,!redo_history.empty());
        toolbar.enable(FileDelete, original_file());
        toolbar.text(FileDelete, settings.recycle_deleted ? L"ごみ箱へ" : L"ファイルを削除");
        toolbar.enable(Previous, !files.empty());
        toolbar.enable(Next, !files.empty());
        const bool reversed = comic && settings.comic_rtl && settings.comic_toolbar_reverse;
        toolbar.text(Previous, reversed ? L"次の画像" : L"前の画像");
        toolbar.text(Next, reversed ? L"前の画像" : L"次の画像");
        toolbar.check(Slideshow, slideshow);
        toolbar.check(ToggleEditor,editor_mode);
        toolbar.text(ToggleEditor,editor_mode?L"画像編集モードを終了":L"画像編集モードに切り替え");
        if(editor_mode){
            for(UINT id:{SelectRectangle,SelectFreehand,SelectPolygon,SelectMove,ToolPan,ToolPen,ToolText,ToolCrop})editor_tools.check(id,checked(id));
            for(UINT id:{SelectRectangle,SelectFreehand,SelectPolygon,SelectAll,InvertSelection,ToolCrop,ToolPen,ToolText,Resize,Canvas,RotateRight,FlipH,FlipV,Brightness,ColorAdjust,Levels})editor_tools.enable(id,image&&!floating&&!loading);
            for(UINT id:{SelectMove,DeleteSelection,ClearSelection})editor_tools.enable(id,image&&!selection.empty()&&!floating&&!loading);
            editor_tools.enable(PasteSelection,!loading&&!floating);
        }
        if(cropping)editor_chrome.bounds(cropping->box,cropping->scale);
        else if(floating&&!floating->text_annotation)editor_chrome.bounds(floating->box);
        if(floating){
            for(UINT id:{Save,Copy,ArmDrag,FileDelete,Previous,Next,Undo,Redo})toolbar.enable(id,false);
            std::wostringstream hint;hint<<L"仮置き：ドラッグで移動／ハンドルで拡縮／枠外で回転  Enter:確定  Esc:中止  ";
            hint<<std::fixed<<std::setprecision(1)<<L"X:"<<floating->box.cx-floating->box.width/2<<L" Y:"<<floating->box.cy-floating->box.height/2<<L"  "<<floating->box.width<<L"×"<<floating->box.height<<L"  "<<floating->box.angle<<L"°";
            if(!floating->original_position)hint<<L"（元位置情報なし）";
            if(floating->invalid_metadata)hint<<L"（位置データ不正のため標準画像を使用）";
            status_text(image?4:0,hint.str());
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    bool animation_timer{};
    bool original_file() const {
        return image && !image->path.empty() && archive.empty() &&
               std::filesystem::is_regular_file(image->path);
    }
    bool discard() {
        if (!modified || !settings.confirm_unsaved)
            return true;
        const int result = dialogs.ask(hwnd, L"画像に保存していない変更があります。保存しますか？", L"GVN",
                                       MB_YESNOCANCEL | MB_ICONQUESTION);
        if (result == IDCANCEL)
            return false;
        if (result == IDYES)
            return save(false);
        return true;
    }
    void sort_list() {
        const auto current = removed_current
                                 ? (index < files.size() ? files[index].path : std::filesystem::path{})
                                 : requested;
        index = 0;
        sort_files(files, static_cast<Sort>(settings.sort), settings.reverse, settings.natural, random());
        for (size_t i = 0; i < files.size(); ++i)
            if (same_path(files[i].path, current)) {
                index = i;
                break;
            }
        if (removed_current && current.empty())
            index = files.size();
    }
    void scan_folder(const std::filesystem::path &directory) {
        folder = std::filesystem::absolute(directory).lexically_normal();
        const FileVisibility visibility{settings.hidden, settings.system_files, settings.protected_files};
        files = open_lister ? open_lister(folder, visibility, {}) : list_files(folder, visibility);
        sort_list();
    }
    void commit_open_history() {
        if (before_load) {
            navigation_folder = before_load->next_navigation;
            if (!smoke)
                settings.remember(before_load->remember_departed);
        }
        before_load.reset();
        if (!smoke && (!navigation_folder || settings.folder_history))
            settings.remember(folder);
    }
    void request_image(std::filesystem::path path, bool force) {
        requested = std::move(path);
        removed_current = false;
        loading = true;
        std::filesystem::path second;
        if (comic && index + 1 < files.size())
            second = files[index + 1].path;
        loader->request(requested, force, second, settings.comic_rtl, [&]{auto options=settings.comic_options();options.down=canonical_display_filter(options.down);options.up=canonical_display_filter(options.up);return options;}(),
                        settings.loading_options());
        update();
    }
    void open(std::filesystem::path path, bool force = false, bool ask = true,
              OpenOrigin origin = OpenOrigin::Explicit, bool open_last = false,
              std::vector<FileEntry> *discovered = nullptr) {
        if (ask && !discard())
            return;
        if(floating)cancel_floating();
        cancel_pointer();
        const bool next_navigation = origin == OpenOrigin::Folder ||
                                     (origin == OpenOrigin::WithinFolder &&
                                      (before_load ? before_load->next_navigation : navigation_folder));
        if (!before_load) {
            before_load = LoadRollback{requested, index, removed_current};
            before_load->navigation_before = navigation_folder;
            before_load->source_folder = folder;
        }
        before_load->next_navigation = next_navigation;
        before_load->no_activate = false;
        if (origin == OpenOrigin::Explicit)
            before_load->remember_departed = before_load->source_folder;
        pending_folder.reset();
        pending_open.reset();
        loader->cancel();
        try {
            if (discovered) {
                checkpoint_list();
                archive.clear();
                archive_entries.clear();
                folder = std::move(path);
                files = std::move(*discovered);
                index = 0;
                if (files.empty()) {
                    commit_open_history();
                    loader->cancel(true);
                    stop_slideshow();
                    reset_animation_clock();
                    image.reset();image_metadata.reset();undo_metadata.reset();
                    clear_undo();
                    undo_location.reset();
                    modified = loading = removed_current = drag_armed = false;
                    selection = {};
                    requested.clear();
                    current_name.clear();
                    player = Player{};
                    renderer.invalidate();
                    layout();
                    refresh_menu();
                    update();
                    return;
                }
                index = open_last ? files.size() - 1 : 0;
                request_image(files[index].path, force);
                return;
            }
            const auto known = std::find_if(files.begin(), files.end(),
                                            [&](const auto &entry) { return same_path(entry.path, path); });
            const bool archive_member = source_file(path) != path && same_path(source_file(path), archive);
            if (origin != OpenOrigin::Startup && known != files.end() && (!force || archive_member)) {
                index = static_cast<size_t>(known - files.begin());
                request_image(known->path, force);
                return;
            }
            OpenQuery query{path,
                            {settings.hidden, settings.system_files, settings.protected_files},
                            static_cast<Sort>(settings.sort),
                            settings.reverse,
                            settings.natural,
                            random(),
                            open_last};
            query.missing_fallback = origin == OpenOrigin::Startup;
            if (query.missing_fallback)
                query.archive_entry = settings.last_archive_entry;
            const auto sequence = loader->request_open(std::move(query), open_lister);
            pending_open = PendingOpen{sequence, force, origin == OpenOrigin::Startup};
            loading = true;
            update();
        } catch (...) {
            rollback_load();
            update();
            throw;
        }
    }
    void accept(std::unique_ptr<Loaded> result) {
        if (result->sequence != loader->sequence())
            return;
        loading = false;
        if (!result->error.empty()) {
            const bool folder_error = pending_folder.has_value();
            pending_folder.reset();
            pending_open.reset();
            rollback_load();
            if (smoke) {
                smoke_error = result->error;
                finish_smoke();
                return;
            }
            stop_slideshow();
            dialogs.ask(hwnd, widen(result->error).c_str(),
                        folder_error ? L"フォルダへ移動できません" : L"画像を開けません",
                        MB_OK | MB_ICONERROR);
            update();
            return;
        }
        if (result->open_match) {
            const auto intent = std::exchange(pending_open, std::nullopt);
            if (!intent || intent->sequence != result->sequence || result->open_match->cancelled) {
                rollback_load();
                update();
                return;
            }
            auto &match = *result->open_match;
            if (match.path.empty()) {
                rollback_load();
                if (!intent->allow_empty)
                    dialogs.ask(hwnd, L"このフォルダには対応画像がありません。", L"GVN", MB_OK);
                update();
                return;
            }
            checkpoint_list();
            folder = std::move(match.folder);
            archive = std::move(match.archive);
            files = std::move(match.files);
            archive_entries = std::move(match.entries);
            index = match.index;
            request_image(std::move(match.path), intent->force);
            return;
        }
        if (result->folder_match) {
            const auto intent = std::exchange(pending_folder, std::nullopt);
            if (!intent || intent->sequence != result->sequence || intent->source != image ||
                intent->folder != folder || intent->requested != requested) {
                stop_slideshow();
                update();
                return;
            }
            if (result->folder_match->cancelled) {
                if (intent->slideshow)
                    stop_slideshow();
                update();
                return;
            }
            if (result->path.empty()) {
                stop_slideshow();
                if (!intent->slideshow)
                    dialogs.ask(hwnd, L"これ以上移動できるフォルダがありません。", L"GVN", MB_OK);
                update();
                return;
            }
            if (!result->folder_match->files)
                throw std::runtime_error("Folder worker did not return the requested file list");
            open(result->path, false, false, OpenOrigin::Folder, intent->last, &*result->folder_match->files);
            return;
        }
        slideshow_loading = false;
        const bool no_activate = before_load && before_load->no_activate;
        commit_open_history();
        removed_current = false;
        image = std::move(result->image);
        editor_chrome.reset_export_name();
        image_metadata.reset();undo_metadata.reset();
        modified = false;
        clear_undo();
        undo_location.reset();
        selection = {};
        reset_animation_clock();
        player.reset(*image);
        player.playing = player.playing && settings.animation_autoplay;
        player.set_loop(settings.animation_loop, settings.animation_file_loops);
        renderer.invalidate();
        view.set_image(image->width, image->height, settings.keep_zoom);
        current_name = requested.filename().wstring();
        if (!archive.empty()) {
            const auto separator = requested.wstring().find(L'|');
            const auto number = std::stoul(requested.wstring().substr(separator + 1));
            for (const auto &entry : archive_entries)
                if (entry.index == number)
                    current_name = archive.filename().wstring() + L" / " + entry.name;
        }
        layout();
        apply_display_policy(no_activate);
        refresh_menu();
        update();
        prefetch_neighbors();
        if (slideshow)
            SetTimer(hwnd, 2, settings.interval, nullptr);
        ready_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                .count();
        if (profile)
            profile_initial_draw();
        if (smoke)
            SetTimer(hwnd, 9, profile ? 2000 : 80, nullptr);
    }
    void prefetch_neighbors() {
        std::vector<std::filesystem::path> neighbors;
        if (image && !modified && !removed_current && !requested.empty() && index < files.size()) {
            if (index + 1 < files.size())
                neighbors.push_back(files[index + 1].path);
            if (index > 0)
                neighbors.push_back(files[index - 1].path);
        }
        loader->prefetch(std::move(neighbors), settings.loading_options(), image ? image->bytes() : 0);
    }
    void profile_initial_draw() {
        first_draw_phases = {};
        renderer.trace = &first_draw_phases;
        const auto draw_started = std::chrono::steady_clock::now();
        HRESULT draw_status{};
        try {
            draw_status = renderer.draw(hwnd, image.get(), player.frame, view, top, bottom, &selection);
            if (draw_status == D2DERR_RECREATE_TARGET)
                draw_status = renderer.draw(hwnd, image.get(), player.frame, view, top, bottom, &selection);
        } catch (...) {
            renderer.trace = nullptr;
            throw;
        }
        renderer.trace = nullptr;
        const auto finished = std::chrono::steady_clock::now();
        first_draw_ms = std::chrono::duration<double, std::milli>(finished - draw_started).count();
        display_ready_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
        first_draw_succeeded = SUCCEEDED(draw_status);
        first_draw_software = renderer.software_target();
        if (!first_draw_succeeded)
            smoke_error = "Initial profile rendering failed";
        else
            ++paints;
        idle_cpu_start = cpu_ticks();
    }
    void navigate(int delta) {
        if (files.empty())
            return;
        int64_t target = static_cast<int64_t>(index) + delta - (removed_current && delta > 0 ? 1 : 0);
        if (target < 0 || target >= static_cast<int64_t>(files.size())) {
            if (loading) {
                cancel_load();
                navigate(delta);
                return;
            }
            const bool forward = delta > 0;
            const bool at_edge =
                forward ? index + static_cast<size_t>(page_step()) >= files.size() : index == 0;
            if (at_edge) {
                end_of_folder(forward);
                return;
            }
            target = forward ? static_cast<int64_t>(files.size() - 1) : 0;
        }
        open(files[static_cast<size_t>(target)].path, false, true, OpenOrigin::WithinFolder);
    }
    int page_step() const {
        return comic && image && image->format == L"見開き" ? 2 : 1;
    }
    void advance_slideshow() {
        if (!slideshow || !slideshow_run || loading)
            return;
        KillTimer(hwnd, 2);
        try {
            if (files.empty()) {
                stop_slideshow();
                update();
                return;
            }
            const auto target = static_cast<int64_t>(index) + page_step() - (removed_current ? 1 : 0);
            if (target < static_cast<int64_t>(files.size())) {
                slideshow_loading = true;
                open(files[static_cast<size_t>(target)].path, false, true, OpenOrigin::WithinFolder);
            } else if (slideshow_run->next_folder && archive.empty() && !folder.empty()) {
                if (!discard()) {
                    stop_slideshow();
                    update();
                    return;
                }
                FolderQuery query{folder, FolderMove::Next,
                                  {settings.hidden, settings.system_files, settings.protected_files},
                                  settings.natural,
                                  slideshow_run->subtree ? slideshow_run->starting_folder : navigation_root};
                query.collect_files = true;
                query.sort = static_cast<Sort>(settings.sort);
                query.reverse = settings.reverse;
                query.seed = random();
                // The official help stops at the drive's end when subtree restriction is OFF.
                query.wrap = slideshow_run->subtree && slideshow_run->loop;
                PendingFolder intent{0, false, image, folder, requested, true};
                intent.sequence = loader->request_folder(std::move(query));
                pending_folder = std::move(intent);
                slideshow_loading = loading = true;
                update();
            } else if (slideshow_run->loop) {
                if (settings.sort == static_cast<int>(Sort::Random))
                    sort_list();
                slideshow_loading = true;
                open(files.front().path, false, true, OpenOrigin::WithinFolder);
            } else {
                stop_slideshow();
                update();
            }
            // A declined discard confirmation must not leave a checked, timerless slideshow.
            if (slideshow && !loading) {
                stop_slideshow();
                update();
            }
        } catch (...) {
            stop_slideshow();
            update();
            throw;
        }
    }
    void move_folder(FolderMove move, std::optional<bool> last_override = {}) {
        cancel_load();
        if (folder.empty())
            return;
        if (!discard())
            return;
        const bool open_last =
            last_override.value_or(move == FolderMove::Previous           ? !settings.previous_folder_first
                                   : move == FolderMove::AdjacentPrevious ? !settings.adjacent_previous_first
                                   : move == FolderMove::Parent           ? !settings.parent_first
                                                                          : false);
        FolderQuery query{folder,
                          move,
                          {settings.hidden, settings.system_files, settings.protected_files},
                          settings.natural,
                          navigation_root};
        query.collect_files = true;
        query.sort = static_cast<Sort>(settings.sort);
        query.reverse = settings.reverse;
        query.seed = random();
        PendingFolder intent{0, open_last, image, folder, requested};
        intent.sequence = loader->request_folder(std::move(query));
        pending_folder = std::move(intent);
        loading = true;
        KillTimer(hwnd, 2);
        update();
    }
    void end_of_folder(bool forward) {
        cancel_load();
        if (settings.boundary_sound)
            dialogs.boundary_beep();
        const int action = settings.effective_end_action();
        BoundaryChoice choice = action == 2   ? BoundaryChoice::FolderEnd
                                : action == 3 ? BoundaryChoice::Folder
                                              : BoundaryChoice::Cancel;
        if (action == 1) {
            stop_slideshow();
            const auto source = image;
            const auto source_folder = folder, source_path = requested;
            const auto source_index = index;
            choice = dialogs.boundary(hwnd, forward, archive.empty() ? folder : archive);
            if (image != source || folder != source_folder || requested != source_path ||
                index != source_index)
                return;
        }
        if (choice == BoundaryChoice::FolderEnd && !files.empty())
            open((forward ? files.front() : files.back()).path, false, true, OpenOrigin::WithinFolder);
        else if (choice == BoundaryChoice::Folder)
            move_folder(forward ? FolderMove::Next : FolderMove::Previous, !forward);
        else
            stop_slideshow();
    }
    void choose() {
        const auto path = dialogs.image_file(hwnd, false, folder, L"image.png");
        if (!path.empty())
            open(path);
    }
    void replace(Image value, bool dirty = true, bool new_document = false) {
        // Complete allocations before changing the source, selection or the previous Undo entry.
        auto next_image = std::make_shared<Image>(std::move(value));
        replace(std::move(next_image),dirty,new_document);
    }
    void replace(std::shared_ptr<Image> next_image, bool dirty = true, bool new_document = false) {
        if(!next_image)throw std::invalid_argument("Empty edited image");
        if(cropping)cancel_crop();
        if(floating)cancel_floating();
        Player next_player;
        next_player.reset(*next_image);
        next_player.playing = next_player.playing && settings.animation_autoplay;
        next_player.set_loop(settings.animation_loop, settings.animation_file_loops);
        auto next_undo_player = settings.undo_enabled ? player : Player{};
        cancel_pointer();
        // Undo describes the displayed source after cancelling a pending replacement, not its target.
        cancel_load(true);
        auto next_undo_location =
            new_document && settings.undo_enabled ? std::optional<Location>(location()) : std::nullopt;
        if (new_document && !folder.empty())
            clipboard_folder = folder;
        remember_undo(std::move(next_undo_player),image_metadata,std::move(next_undo_location));
        if (new_document)
            removed_current = false;
        image = std::move(next_image);
        if(new_document)editor_chrome.reset_export_name();
        image_metadata.reset();
        modified = dirty;
        selection = {};
        reset_animation_clock();
        player = std::move(next_player);
        renderer.invalidate();
        view.set_image(image->width, image->height, settings.keep_zoom);
        layout();
        apply_display_policy();
        update();
    }
    bool begin_edit() {
        if (!image)
            return false;
        if (image->animated() && dialogs.ask(hwnd,
                                             L"表示中のフレームを静止画像として加工します。元のアニメーション"
                                             L"は上書きしません。続けますか？",
                                             L"GVN", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
            return false;
        return true;
    }
    void edit(const std::function<Image(const Image &)> &operation,bool use_selection=true) {
        if (!begin_edit())
            return;
        auto frame = still_frame(*image, player.frame);
        frame.metadata=current_metadata();
        if (image->animated() || image->format == L"見開き")
            frame.path.clear();
        const auto old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        try {
            auto result=operation(frame);
            const auto previous=selection;
            if(use_selection)restrict_to_selection(result,frame,previous);
            const bool retain=result.width==frame.width&&result.height==frame.height;
            replace(std::move(result));
            if(retain){selection=previous;update();}
        } catch (...) {
            SetCursor(old);
            throw;
        }
        SetCursor(old);
    }
    bool previewing_mask() const {
        return mask_preview_image && image==mask_preview_origin && player.frame==mask_preview_frame;
    }
    const Image *presented_image() const {return floating?floating->canvas.get():previewing_mask()?mask_preview_image.get():image.get();}
    size_t presented_frame() const {return previewing_mask()?0:player.frame;}
    HRESULT render_scene(HDC dc=nullptr) {
        renderer.selection_region=&selection;
        renderer.crop_box=cropping?std::optional<FloatingBox>(cropping->box):std::nullopt;
        renderer.set_floating(floating?floating->source:nullptr,floating?floating->box:FloatingBox{});
        const auto previous=renderer.dragging_low;renderer.dragging_low=previous||previewing_mask();
        struct Restore {bool &value;bool previous;~Restore(){value=previous;}} restore{renderer.dragging_low,previous};
        const auto frame=floating?size_t(0):presented_frame();
        return dc?renderer.print(hwnd,dc,presented_image(),frame,view,top,floating?nullptr:&selection)
                 :renderer.draw(hwnd,presented_image(),frame,view,top,bottom,floating?nullptr:&selection);
    }
    void begin_floating() {
        if(cropping)cancel_crop();
        if(loading)throw std::runtime_error("Wait for the current image to finish loading");
        auto content=paste_selection_clipboard(hwnd);
        auto source=std::make_shared<Image>(still_frame(content.image,0,false));
        if(floating)cancel_floating();
        auto canvas=image?std::make_shared<Image>(still_frame(*image,player.frame,false)):
            std::make_shared<Image>(new_image(content.origin?content.origin->canvas_width:source->width,
                                              content.origin?content.origin->canvas_height:source->height,edit_memory()));
        FloatingPaste next{image,canvas,source,{},player,view,content.origin,slideshow,content.invalid_metadata};
        next.previous_selection=selection;
        next.box={double(content.origin?content.origin->x:0)+source->width/2.,double(content.origin?content.origin->y:0)+source->height/2.,double(source->width),double(source->height),0};
        floating=std::move(next);cancel_pointer();selection={};renderer.selection_path.clear();player.playing=false;reset_animation_clock();KillTimer(hwnd,2);
        drag_armed=false;toolbar.floating(true);
        if(!image){view.set_image(canvas->width,canvas->height,false);view.apply_fit(Fit::Contain);}
        refresh_menu();layout();update();
    }
    void cancel_floating() {
        if(!floating)return;
        auto old=std::move(*floating);floating.reset();floating_drag.reset();renderer.set_floating({});
        toolbar.floating(false);cancel_pointer();
        if(image==old.origin){player=std::move(old.previous_player);selection=old.previous_selection;reset_animation_clock();if(!image)view=old.previous_view;}
        if(old.slideshow&&slideshow)SetTimer(hwnd,2,settings.interval,nullptr);
        refresh_menu();layout();update();
    }
    void commit_floating() {
        if(!floating)return;
        if(image!=floating->origin){cancel_floating();return;}
        if(image&&!begin_edit())return;
        auto result=paste_floating(*floating->canvas,*floating->source,floating->box,edit_memory());
        if(image){result.metadata=current_metadata();if(image->animated()||image->format==L"見開き")result.path.clear();}
        // Finish every allocation before discarding the uncommitted placement.
        auto prepared=std::make_shared<Image>(std::move(result));const bool synthetic=!floating->origin;
        cancel_floating();
        if(synthetic)replace_clipboard_document(std::move(*prepared));else replace(std::move(prepared));
    }
    FloatPoint floating_point(POINT p) const {
        const bool actual=std::abs(view.scale-1)<1e-9;
        return {(p.x-(actual?std::floor(view.left()):view.left()))/view.scale,
                (p.y-top-(actual?std::floor(view.top()):view.top()))/view.scale};
    }
    void floating_numbers() {
        if(!floating)return;const auto source=floating->source;const auto box=floating->box;
        std::wostringstream initial;initial<<box.cx-box.width/2<<L' '<<box.cy-box.height/2<<L' '<<box.width<<L' '<<box.height<<L' '<<box.angle;
        const auto text=dialogs.text(hwnd,L"貼り付けの位置・サイズ・角度",L"X Y 幅 高さ 角度（px / 度、半角空白区切り）",initial.str());
        if(!text||!floating||floating->source!=source)return;
        double x{},y{},width{},height{},angle{};std::wistringstream in(*text);
        if(!(in>>x>>y>>width>>height>>angle)||!(in>>std::ws).eof())throw std::invalid_argument("Enter X Y width height angle");
        FloatingBox next{x+width/2,y+height/2,width,height,angle};
        if(!next.valid())throw std::invalid_argument("Invalid floating selection bounds");floating->box=next;update();
    }
    void move_selection() {
        if(!image||selection.empty()||loading)return;
        auto frame=still_frame(*image,player.frame,false);
        auto source=std::make_shared<Image>(selection_image(frame,selection,edit_memory()));
        auto canvas=std::make_shared<Image>(erase_selection(frame,selection,{},edit_memory()));
        FloatingPaste next{image,canvas,source,{},player,view,SelectionOrigin{selection.x,selection.y,image->width,image->height},slideshow,false};
        next.previous_selection=selection;
        next.box={selection.x+source->width/2.,selection.y+source->height/2.,double(source->width),double(source->height),0};
        floating=std::move(next);cancel_pointer();selection={};renderer.selection_path.clear();
        player.playing=false;reset_animation_clock();KillTimer(hwnd,2);drag_armed=false;toolbar.floating(true);
        refresh_menu();layout();update();
    }
    static SelectionCombine selection_modifier(WPARAM flags) {
        if(GetKeyState(VK_MENU)&0x8000)return SelectionCombine::Subtract;
        if(flags&MK_CONTROL)return SelectionCombine::Intersect;
        if(flags&MK_SHIFT)return SelectionCombine::Add;
        return SelectionCombine::Replace;
    }
    void finish_selection_path() {
        if(!image)return;
        auto next=SelectionRegion::polygon(selection_points,image->width,image->height);
        next=selection_before_drag.combined(next,selection_combine,image->width,image->height);
        selecting=polygon_drawing=false;selection_points.clear();renderer.selection_path.clear();
        selection=std::move(next);if(GetCapture()==hwnd)ReleaseCapture();update();
    }
    void cancel_selection_path() {
        if(selecting||polygon_drawing)selection=selection_before_drag;
        selecting=polygon_drawing=false;selection_points.clear();renderer.selection_path.clear();
    }
    void set_edit_mode(bool enabled) {
        if(editor_mode==enabled)return;
        cancel_pointer();if(floating)cancel_floating();if(cropping)cancel_crop();
        if(!enabled){previous_editor_tool=selection_tool;selection_tool=SelectionTool::Browse;}
        else selection_tool=previous_editor_tool;
        editor_mode=enabled;drag_armed=false;refresh_menu();layout();update();
    }
    void choose_tool(SelectionTool tool) {
        cancel_pointer();if(floating)cancel_floating();if(cropping)cancel_crop();
        set_edit_mode(true);selection_tool=previous_editor_tool=tool;
        if(tool==SelectionTool::Move)move_selection();
        refresh_menu();layout();update();
    }
    POINT canvas_point(POINT p) const {p.x-=left;return p;}
    POINT client_point(POINT p) const {p.x+=left;return p;}
    void start_selection(unsigned button,POINT point,WPARAM flags,bool double_click) {
        const auto tool=selection_active()&&!(selection_tool==SelectionTool::Move&&selection_modifier(flags)!=SelectionCombine::Replace)?selection_tool:SelectionTool::Rectangle;
        auto &press=mouse_presses[button];press.consumed=true;
        const auto p=image_point(point);const FloatPoint next{double(p.x),double(p.y)};
        if(tool==SelectionTool::Move){
            if(selection.contains(p.x,p.y)){move_selection();if(floating){const auto at=floating_point(point);floating_drag_button=button;floating_drag=FloatingDrag{floating->box,at,floating_hit(floating->box,at,7./view.scale)};SetCapture(hwnd);}}
            return;
        }
        if(tool==SelectionTool::Polygon){
            if(!polygon_drawing){selection_before_drag=selection;selection_combine=selection_modifier(flags);selection_points.clear();polygon_drawing=true;}
            if(selection_points.empty()||selection_points.back().x!=next.x||selection_points.back().y!=next.y)selection_points.push_back(next);
            if(selection_points.size()>65536)throw std::runtime_error("Too many selection points");
            if(double_click){finish_selection_path();return;}
            renderer.selection_path=selection_points;renderer.selection_path.push_back(next);update();return;
        }
        selection_before_drag=selection;selection_combine=selection_modifier(flags);selection_drag_tool=tool;
        active_drag_button=button+1;active_drag_action=MouseSelect;selecting=true;selection_start=p;
        selection_drag_units=settings.selection_units();selection_points.clear();
        if(tool==SelectionTool::Freehand){selection_points.push_back(next);renderer.selection_path=selection_points;}
        else selection={};
        SetCapture(hwnd);update();
    }
    void rainbow_edit() {
        if(!begin_edit())return;
        const auto source=image;const auto frame=player.frame;const auto rect=selection;
        auto previous_player=player;const bool was_slideshow=slideshow;bool committed{};
        const auto current=[&]{return image==source&&player.frame==frame;};
        const auto preview=[&](std::shared_ptr<const Image> value) {
            if(!current())value.reset();
            if(mask_preview_image==value && (!value || (mask_preview_origin==source&&mask_preview_frame==frame)))return;
            mask_preview_image=std::move(value);mask_preview_origin=mask_preview_image?source:nullptr;
            mask_preview_frame=mask_preview_image?frame:SIZE_MAX;
            renderer.invalidate();InvalidateRect(hwnd,nullptr,FALSE);
            if(mask_preview_image)UpdateWindow(hwnd);
        };
        const auto cleanup=[&] {
            if(!committed&&current()) {
                player=std::move(previous_player);player.set_loop(settings.animation_loop,settings.animation_file_loops);reset_animation_clock();
            }
            preview({});
            if(was_slideshow&&slideshow)SetTimer(hwnd,2,settings.interval,nullptr);
            try {update();}catch(...) {}
        };
        struct OnExit {const decltype(cleanup) &action;~OnExit(){action();}} restore{cleanup};
        player.playing=false;KillTimer(hwnd,2);reset_animation_clock();update();
        const auto process=[&](int size) {
            if(!current())throw std::runtime_error("Displayed image changed during Rainbow preview");
            const auto memory=edit_memory();const auto count=memory.pixels(source->width,source->height);
            if(memory.before_allocate)memory.before_allocate(count*sizeof(Pixel));
            auto result=rainbow_image(*source,size,rect,frame);restrict_to_selection(result,*source,rect,frame);
            if(source->animated()||source->format==L"見開き")result.path.clear();
            return std::make_shared<Image>(std::move(result));
        };
        const auto result=dialogs.rainbow(hwnd,{settings.rainbow_size,settings.rainbow_preview},process,preview,current);
        if(!result||!current())return;
        if(!result->image)throw std::runtime_error("Empty Rainbow result");
        auto next=settings;next.rainbow_size=result->choice.size;next.rainbow_preview=result->choice.preview;
        auto resume_player=previous_player;player=std::move(resume_player);preview({});
        result->image->metadata=current_metadata();
        replace(result->image);committed=true;selection=rect;apply_settings(next);
    }
    void palette_edit() {
        if(!image||image->frames.size()!=1||!image->frames[0].indexed)return;
        const auto &indices=image->frames[0].indexed;
        if(indices->width!=image->width||indices->height!=image->height||!indices->matches(image->frames[0].pixels))return;
        auto source=image;const auto frame=player.frame;const auto rect=selection;
        auto previous_player=player;const bool was_slideshow=slideshow;bool committed{};
        const auto current=[&]{return image==source&&player.frame==frame;};
        const auto preview=[&](std::shared_ptr<const Image> value) {
            if(!current())value.reset();
            mask_preview_image=std::move(value);mask_preview_origin=mask_preview_image?source:nullptr;
            mask_preview_frame=mask_preview_image?frame:SIZE_MAX;
            renderer.invalidate();InvalidateRect(hwnd,nullptr,FALSE);if(mask_preview_image)UpdateWindow(hwnd);
        };
        const auto cleanup=[&] {
            preview({});
            if(!committed&&current())player=std::move(previous_player);
            if(was_slideshow&&slideshow)SetTimer(hwnd,2,settings.interval,nullptr);
            reset_animation_clock();try{update();}catch(...) {}
        };
        struct OnExit{const decltype(cleanup)&action;~OnExit(){action();}}restore{cleanup};
        player.playing=false;KillTimer(hwnd,2);reset_animation_clock();
        const auto process=[&](const std::vector<Pixel> &colors) {
            if(!current())throw std::runtime_error("Displayed image changed during palette preview");
            auto result=gvn::edit_palette(*source,colors,edit_memory());result.metadata=current_metadata();
            return std::make_shared<Image>(std::move(result));
        };
        const auto commit=[&](std::shared_ptr<Image> candidate,bool enabled) {
            if(!current())throw std::runtime_error("Displayed image changed during palette commit");
            if(candidate&&(candidate->frames.size()!=1||!candidate->frames[0].indexed||!candidate->frames[0].indexed->matches(candidate->frames[0].pixels)))
                throw std::runtime_error("Invalid palette result");
            if(enabled!=settings.palette_preview){auto next=settings;next.palette_preview=enabled;next.save(config);settings.palette_preview=enabled;}
            preview({});
            if(candidate){candidate->metadata=current_metadata();replace(std::move(candidate));source=image;selection=rect;committed=true;}
        };
        dialogs.palette(hwnd,source->frames[0].indexed->palette,settings.palette_preview,process,preview,commit,current);
    }
    void color_depth_edit() {
        if(!image||!begin_edit()||!image)return;
        const auto source=image;const auto frame=player.frame;const auto rect=selection;
        auto previous_player=player;const bool was_slideshow=slideshow;bool committed{};
        const auto current=[&]{return image==source&&player.frame==frame;};
        const auto cleanup=[&] {
            if(!committed&&current())player=std::move(previous_player);
            if(was_slideshow&&slideshow)SetTimer(hwnd,2,settings.interval,nullptr);
            reset_animation_clock();try{update();}catch(...){}
        };
        struct OnExit{const decltype(cleanup)&action;~OnExit(){action();}}restore{cleanup};
        player.playing=false;KillTimer(hwnd,2);reset_animation_clock();
        unsigned bits=24;std::optional<Rgb16Layout> layout16;
        const auto &displayed=source->frames.at(frame);
        if(const auto &indices=displayed.indexed;indices&&indices->width==source->width&&indices->height==source->height&&indices->matches(displayed.pixels))bits=indices->bits;
        else if(source->rgb16&&source->rgb16->matches(displayed.pixels)){bits=16;layout16=source->rgb16->layout;}
        const auto choice=dialogs.color_depth(hwnd,bits,layout16,settings.color_depth_dither,current);
        if(!choice||!current())return;
        std::optional<Image> converted;
        if(source->animated()) {
            auto still=still_frame(*source,frame);still.metadata=current_metadata();
            converted=convert_color_depth(still,*choice,edit_memory());
        }else converted=convert_color_depth(*source,*choice,edit_memory());
        if(!current())return;
        std::shared_ptr<Image> candidate;Player next_player;
        if(converted) {
            converted->metadata=current_metadata();if(source->animated()||source->format==L"見開き")converted->path.clear();
            candidate=std::make_shared<Image>(std::move(*converted));next_player.reset(*candidate);
            next_player.set_loop(settings.animation_loop,settings.animation_file_loops);
        }
        auto next_undo_metadata=candidate&&settings.undo_enabled?image_metadata:std::nullopt;
        // Complete image/player/metadata allocations before persisting the option or changing Undo.
        if(choice->dither!=settings.color_depth_dither){auto next=settings;next.color_depth_dither=choice->dither;persisted_settings(next).save(config);}
        if(candidate) {
            cancel_load(true);remember_undo(std::move(previous_player),std::move(next_undo_metadata));
            image=std::move(candidate);image_metadata.reset();modified=true;player=std::move(next_player);selection=rect;
            renderer.invalidate();layout();committed=true;
        }
        settings.color_depth_dither=choice->dither;
    }
    void color_adjustment_edit() {
        if(!image||!begin_edit()||!image)return;
        const auto source=image;const auto frame=player.frame;const auto rect=selection;
        auto previous_player=player;const bool was_slideshow=slideshow;bool committed{};
        const auto current=[&]{return image==source&&player.frame==frame;};
        const auto cleanup=[&] {
            if(!committed&&current())player=std::move(previous_player);
            if(was_slideshow&&slideshow)SetTimer(hwnd,2,settings.interval,nullptr);
            reset_animation_clock();try{update();}catch(...){}
        };
        struct OnExit{const decltype(cleanup)&action;~OnExit(){action();}}restore{cleanup};
        player.playing=false;KillTimer(hwnd,2);reset_animation_clock();update();
        const auto choice=dialogs.color_adjustment(hwnd,source,frame,rect,settings.color_adjustment_choice(),current);
        if(!choice||!current())return;if(!valid_color_adjustment(*choice))throw std::invalid_argument("Invalid color adjustment choice");
        std::optional<Image> converted;
        if(!choice->neutral()) {
            if(source->animated()){auto still=still_frame(*source,frame);still.metadata=current_metadata();converted=gvn::color_adjustment(still,*choice,rect,edit_memory());}
            else converted=gvn::color_adjustment(*source,*choice,rect,edit_memory());
        }
        if(!current())return;
        std::shared_ptr<Image> candidate;Player next_player;
        if(converted) {
            restrict_to_selection(*converted,*source,rect,frame);converted->metadata=current_metadata();if(source->animated()||source->format==L"見開き")converted->path.clear();
            candidate=std::make_shared<Image>(std::move(*converted));next_player.reset(*candidate);next_player.set_loop(settings.animation_loop,settings.animation_file_loops);
        }
        auto next_undo_metadata=candidate&&settings.undo_enabled?image_metadata:std::nullopt;
        auto next=settings;next.color_adjust_unified=choice->unified;next.color_adjust_gray=int(choice->gray);
        if(choice->unified){next.color_adjust_hue=choice->hue;next.color_adjust_saturation=choice->saturation;next.color_adjust_lightness=choice->lightness;}
        if(next.color_adjust_unified!=settings.color_adjust_unified||next.color_adjust_hue!=settings.color_adjust_hue||next.color_adjust_saturation!=settings.color_adjust_saturation||
           next.color_adjust_lightness!=settings.color_adjust_lightness||next.color_adjust_gray!=settings.color_adjust_gray)persisted_settings(next).save(config);
        settings.color_adjust_unified=next.color_adjust_unified;settings.color_adjust_hue=next.color_adjust_hue;settings.color_adjust_saturation=next.color_adjust_saturation;
        settings.color_adjust_lightness=next.color_adjust_lightness;settings.color_adjust_gray=next.color_adjust_gray;
        if(candidate) {
            cancel_load(true);remember_undo(std::move(previous_player),std::move(next_undo_metadata));image=std::move(candidate);image_metadata.reset();modified=true;
            player=std::move(next_player);selection=rect;renderer.invalidate();layout();committed=true;
        }
    }
    void brightness_edit() {
        if(!begin_edit())return;const auto source=image;const auto frame=player.frame;const auto rect=selection;auto previous_player=player;const bool was_slideshow=slideshow;bool committed{};
        const auto current=[&]{return image==source&&player.frame==frame;};
        const auto preview=[&](std::shared_ptr<const Image> value){if(!current())value.reset();mask_preview_image=std::move(value);mask_preview_origin=mask_preview_image?source:nullptr;mask_preview_frame=mask_preview_image?frame:SIZE_MAX;renderer.invalidate();InvalidateRect(hwnd,nullptr,FALSE);if(mask_preview_image)UpdateWindow(hwnd);};
        const auto cleanup=[&]{preview({});if(!committed&&current())player=std::move(previous_player);if(was_slideshow&&slideshow)SetTimer(hwnd,2,settings.interval,nullptr);reset_animation_clock();try{update();}catch(...) {}};
        struct OnExit{const decltype(cleanup)&action;~OnExit(){action();}}restore{cleanup};player.playing=false;KillTimer(hwnd,2);reset_animation_clock();update();
        const auto process=[&](const BrightnessAdjustment &choice)->std::shared_ptr<Image>{
            if(!current())throw std::runtime_error("Displayed image changed during brightness preview");std::optional<Image> result;
            if(source->animated()){auto still=still_frame(*source,frame);still.metadata=current_metadata();result=adjust_brightness(still,choice,rect,edit_memory());}else result=adjust_brightness(*source,choice,rect,edit_memory());
            if(!result)return {};restrict_to_selection(*result,*source,rect,frame);result->metadata=current_metadata();if(source->animated()||source->format==L"見開き")result->path.clear();return std::make_shared<Image>(std::move(*result));
        };
        const auto choice=dialogs.brightness(hwnd,{{},settings.brightness_preview},process,preview,current);if(!choice||!current())return;
        if(!valid_brightness(choice->choice.adjustment)||(!choice->choice.adjustment.neutral()&&!choice->image))throw std::runtime_error("Invalid brightness result");
        auto candidate=choice->image;if(candidate&&(candidate->width!=source->width||candidate->height!=source->height||candidate->frames.size()!=1||candidate->frames[0].pixels.size()!=checked_image_pixels(source->width,source->height)))throw std::runtime_error("Invalid brightness result image");
        Player next_player;if(candidate){next_player.reset(*candidate);next_player.set_loop(settings.animation_loop,settings.animation_file_loops);}auto next_undo_metadata=candidate&&settings.undo_enabled?image_metadata:std::nullopt;
        if(choice->choice.preview!=settings.brightness_preview){auto next=settings;next.brightness_preview=choice->choice.preview;persisted_settings(next).save(config);settings.brightness_preview=next.brightness_preview;}
        preview({});if(candidate){cancel_load(true);remember_undo(std::move(previous_player),std::move(next_undo_metadata));image=std::move(candidate);image_metadata.reset();modified=true;player=std::move(next_player);selection=rect;renderer.invalidate();layout();committed=true;}
    }
    void levels_edit() {
        if(!begin_edit())return;const auto source=image;const auto frame=player.frame;const auto rect=selection;auto previous_player=player;const bool was_slideshow=slideshow;bool committed{};
        const auto current=[&]{return image==source&&player.frame==frame;};
        const auto preview=[&](std::shared_ptr<const Image> value){if(!current())value.reset();mask_preview_image=std::move(value);mask_preview_origin=mask_preview_image?source:nullptr;mask_preview_frame=mask_preview_image?frame:SIZE_MAX;renderer.invalidate();InvalidateRect(hwnd,nullptr,FALSE);if(mask_preview_image)UpdateWindow(hwnd);};
        const auto cleanup=[&]{preview({});if(!committed&&current())player=std::move(previous_player);if(was_slideshow&&slideshow)SetTimer(hwnd,2,settings.interval,nullptr);reset_animation_clock();try{update();}catch(...) {}};
        struct OnExit{const decltype(cleanup)&action;~OnExit(){action();}}restore{cleanup};player.playing=false;KillTimer(hwnd,2);reset_animation_clock();update();
        std::shared_ptr<const Image> frame_source=source;
        if(source->animated()){auto still=still_frame(*source,frame);still.metadata=current_metadata();frame_source=std::make_shared<Image>(std::move(still));}
        const auto histogram=levels_histogram(*frame_source,rect);
        const auto process=[&](const LevelsAdjustment &choice)->std::shared_ptr<Image>{
            if(!current())throw std::runtime_error("Displayed image changed during levels preview");auto result=adjust_levels(*frame_source,choice,rect,edit_memory());
            if(!result)return {};restrict_to_selection(*result,*source,rect,frame);result->metadata=current_metadata();if(source->animated()||source->format==L"見開き")result->path.clear();return std::make_shared<Image>(std::move(*result));
        };
        const auto choice=dialogs.levels(hwnd,{{},settings.levels_preview},histogram,process,preview,current);if(!choice||!current())return;
        if(!valid_levels(choice->choice.adjustment)||(!choice->choice.adjustment.neutral()&&!choice->image))throw std::runtime_error("Invalid levels result");
        auto candidate=choice->image;if(candidate&&(candidate->width!=source->width||candidate->height!=source->height||candidate->frames.size()!=1||candidate->frames[0].pixels.size()!=checked_image_pixels(source->width,source->height)))throw std::runtime_error("Invalid levels result image");
        Player next_player;if(candidate){next_player.reset(*candidate);next_player.set_loop(settings.animation_loop,settings.animation_file_loops);}auto next_undo_metadata=candidate&&settings.undo_enabled?image_metadata:std::nullopt;
        if(choice->choice.preview!=settings.levels_preview){auto next=settings;next.levels_preview=choice->choice.preview;persisted_settings(next).save(config);settings.levels_preview=next.levels_preview;}
        preview({});if(candidate){cancel_load(true);remember_undo(std::move(previous_player),std::move(next_undo_metadata));image=std::move(candidate);image_metadata.reset();modified=true;player=std::move(next_player);selection=rect;renderer.invalidate();layout();committed=true;}
    }
    void spatial_filter_edit(bool sharp) {
        if(!begin_edit())return;const auto source=image;const auto frame=player.frame;const auto rect=selection;auto previous_player=player;const bool was_slideshow=slideshow;bool committed{};
        const auto current=[&]{return image==source&&player.frame==frame;};
        const auto cleanup=[&]{if(!committed&&current())player=std::move(previous_player);if(was_slideshow&&slideshow)SetTimer(hwnd,2,settings.interval,nullptr);reset_animation_clock();try{update();}catch(...) {}};
        struct OnExit{const decltype(cleanup)&action;~OnExit(){action();}}restore{cleanup};player.playing=false;KillTimer(hwnd,2);reset_animation_clock();update();
        const auto choice=dialogs.spatial_filter(hwnd,source,frame,rect,settings.filter_choice(sharp),current);if(!choice||!current())return;if(choice->sharpen!=sharp||!valid_spatial_filter(*choice))throw std::invalid_argument("Invalid spatial filter choice");
        auto converted=gvn::spatial_filter(*source,*choice,rect,frame,edit_memory());restrict_to_selection(converted,*source,rect,frame);converted.metadata=current_metadata();if(source->animated()||source->format==L"見開き")converted.path.clear();
        auto candidate=std::make_shared<Image>(std::move(converted));Player next_player;next_player.reset(*candidate);next_player.set_loop(settings.animation_loop,settings.animation_file_loops);auto next_undo_metadata=settings.undo_enabled?image_metadata:std::nullopt;
        auto next=settings;if(sharp){next.unsharp_radius=choice->radius;next.unsharp_strength=choice->strength;next.unsharp_threshold=choice->threshold;}else {next.gaussian_radius=choice->radius;next.gaussian_transparency=choice->transparency;}
        if(next.unsharp_radius!=settings.unsharp_radius||next.unsharp_strength!=settings.unsharp_strength||next.unsharp_threshold!=settings.unsharp_threshold||next.gaussian_radius!=settings.gaussian_radius||next.gaussian_transparency!=settings.gaussian_transparency)persisted_settings(next).save(config);
        settings.unsharp_radius=next.unsharp_radius;settings.unsharp_strength=next.unsharp_strength;settings.unsharp_threshold=next.unsharp_threshold;settings.gaussian_radius=next.gaussian_radius;settings.gaussian_transparency=next.gaussian_transparency;
        cancel_load(true);remember_undo(std::move(previous_player),std::move(next_undo_metadata));image=std::move(candidate);image_metadata.reset();modified=true;player=std::move(next_player);selection=rect;renderer.invalidate();layout();committed=true;
    }
    void cp_edit() {
        if(!begin_edit())return;
        const auto source=image;const auto frame=player.frame;const auto rect=selection;
        auto previous_player=player;const bool was_slideshow=slideshow;bool committed{};
        const auto current=[&]{return image==source&&player.frame==frame;};
        const auto preview=[&](std::shared_ptr<const Image> value) {
            if(!current())value.reset();
            if(mask_preview_image==value && (!value || (mask_preview_origin==source&&mask_preview_frame==frame)))return;
            mask_preview_image=std::move(value);mask_preview_origin=mask_preview_image?source:nullptr;
            mask_preview_frame=mask_preview_image?frame:SIZE_MAX;
            renderer.invalidate();InvalidateRect(hwnd,nullptr,FALSE);
            if(mask_preview_image)UpdateWindow(hwnd);
        };
        const auto cleanup=[&] {
            if(!committed&&current()) {
                player=std::move(previous_player);player.set_loop(settings.animation_loop,settings.animation_file_loops);reset_animation_clock();
            }
            preview({});
            if(was_slideshow&&slideshow)SetTimer(hwnd,2,settings.interval,nullptr);
            try {update();}catch(...) {}
        };
        struct OnExit {const decltype(cleanup) &action;~OnExit(){action();}} restore{cleanup};
        player.playing=false;KillTimer(hwnd,2);reset_animation_clock();update();
        const auto process=[&](std::wstring_view code) {
            if(!current())throw std::runtime_error("Displayed image changed during CP preview");
            const auto memory=edit_memory();const auto count=memory.pixels(source->width,source->height);
            if(memory.before_allocate)memory.before_allocate(count*sizeof(Pixel));
            auto result=cp_image(*source,code,rect,frame);restrict_to_selection(result,*source,rect,frame);
            if(source->animated()||source->format==L"見開き")result.path.clear();
            return std::make_shared<Image>(std::move(result));
        };
        const auto result=dialogs.cp(hwnd,{settings.cp_code,settings.cp_preview},process,preview,current);
        if(!result||!current())return;
        auto next=settings;next.cp_code=normalize_cp_code(result->choice.code);next.cp_preview=result->choice.preview;
        if(next.cp_code.empty()){apply_settings(next);return;}
        if(!result->image)throw std::runtime_error("Empty CP result");
        auto resume_player=previous_player;player=std::move(resume_player);preview({});
        result->image->metadata=current_metadata();
        replace(result->image);committed=true;selection=rect;apply_settings(next);
    }
    ImageMetadata current_metadata() const {return image?image_metadata.value_or(image->metadata):ImageMetadata{};}
    void change_resolution(double x,double y) {
        if(!image)return;
        if(!std::isfinite(x)||!std::isfinite(y)||x<.001||y<.001||x>65535||y>65535)throw std::invalid_argument("Invalid image resolution");
        auto next=current_metadata();next.dpi_x=x;next.dpi_y=y;next.dpi_defined=true;
        if(next==current_metadata())return;
        auto previous_player=settings.undo_enabled?player:Player{};
        cancel_load(true);
        remember_undo(std::move(previous_player),image_metadata);
        image_metadata=next;modified=true;update();
    }
    Image copied_image() const {
        auto frame = still_frame(*image, player.frame);
        frame.metadata=current_metadata();
        return selection.empty() ? frame : selection_image(frame, selection);
    }
    void copy_current_selection(const Image &frame) {
        std::optional<SelectionOrigin> origin;
        if(image&&!selection.empty())origin=SelectionOrigin{selection.x,selection.y,image->width,image->height};
        copy_image(hwnd,frame,0,origin);
    }
    void replace_clipboard_document(Image pasted) {
        replace(std::move(pasted), true, true);
        stop_slideshow();
        drag_armed = false;
        files.clear();
        folder.clear();
        navigation_folder = false;
        archive.clear();
        requested.clear();
        current_name = L"クリップボード画像";
        update();
    }
    std::optional<double> number(const wchar_t *title, const wchar_t *label, double initial, double minimum,
                                 double maximum) {
        auto text = dialogs.text(hwnd, title, label, std::to_wstring(initial));
        if (!text)
            return {};
        size_t used{};
        double value = std::stod(*text, &used);
        if (used != text->size() || !std::isfinite(value) || value < minimum || value > maximum)
            throw std::runtime_error("Value is outside the allowed range");
        return value;
    }
    std::optional<std::pair<uint32_t, uint32_t>> dimensions(const wchar_t *title, uint32_t w, uint32_t h) {
        auto text = dialogs.text(hwnd, title, L"幅, 高さ（ピクセル）。例: 1920, 1080",
                                 std::to_wstring(w) + L", " + std::to_wstring(h));
        if (!text)
            return {};
        std::wistringstream in(*text);
        uint64_t width{}, height{};
        wchar_t comma{};
        if (!(in >> width >> comma >> height) || comma != L',' || !(in >> std::ws).eof())
            throw std::runtime_error("Enter width, height");
        edit_memory().pixels(width, height);
        return std::pair{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    }
    bool save(bool as) {
        if (!image)
            return false;
        cancel_load();
        stop_slideshow();
        player.playing = false;
        update();
        std::filesystem::path target = image->path;
        auto ext = target.extension().wstring();
        for (auto &c : ext)
            c = static_cast<wchar_t>(towlower(c));
        const bool save_as =
            as || target.empty() || image->animated() ||
            (ext != L".png" && ext != L".jpg" && ext != L".jpeg" && ext != L".bmp" && ext != L".gif");
        auto proposed = settings.saving;
        if (save_as || settings.save_dialog) {
            auto base = (!current_name.empty() && current_name.find(L'/') == std::wstring::npos)
                            ? std::filesystem::path(current_name).stem().wstring() + (save_as ? L"" : ext)
                            : L"image";
            if (image->format == L"見開き")
                base = L"spread";
            auto initial = target.empty() ? folder : target.parent_path();
            std::error_code ec;
            if (save_as && settings.remember_save_folder &&
                std::filesystem::is_directory(settings.last_save_folder, ec))
                initial = settings.last_save_folder;
            target = dialogs.save_file(hwnd, initial, base, proposed);
            if (target.empty())
                return false;
        }
        target = gvn::save_target(target, proposed);
        target = std::filesystem::absolute(target).lexically_normal();
        if (image->animated() && same_disk_file(target, image->path))
            throw std::runtime_error("Save an animation frame to a different file");
        const bool exists = std::filesystem::exists(target);
        if (exists && (save_as || settings.confirm_overwrite || !same_path(target, image->path)) &&
            dialogs.ask(hwnd, L"保存先のファイルを上書きしますか？", target.c_str(),
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
            return false;
        auto extension = target.extension().wstring();
        for (auto &c : extension)
            c = static_cast<wchar_t>(towlower(c));
        if (extension != L".png") {
            const bool alpha =
                std::any_of(image->frames.at(player.frame).pixels.begin(),
                            image->frames.at(player.frame).pixels.end(), [](Pixel p) { return p.a != 255; });
            if (alpha &&
                dialogs.ask(hwnd, L"PNG以外では半透明や色が失われる場合があります。この形式で保存しますか？",
                            L"保存形式", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
                return false;
        }
        const auto metadata=current_metadata();
        save_image(*image, player.frame, target, proposed, exists,&metadata);
        proposed.last_format = save_format(target);
        const bool persist_saving =
            proposed != settings.saving || (save_as && settings.remember_save_folder &&
                                            settings.last_save_folder != target.parent_path().wstring());
        settings.saving = proposed;
        if (save_as && settings.remember_save_folder)
            settings.last_save_folder = target.parent_path().wstring();
        const bool saved_current_source = !image->path.empty() && same_path(target, image->path);
        if (!image->animated() && (settings.save_as_retarget || saved_current_source)) {
            cancel_load(true);
            auto copy = std::make_shared<Image>(*image);
            copy->path = target;
            copy->file_bytes = std::filesystem::file_size(target);
            const auto saved_format=proposed.last_format==SaveFormat::Png?L"PNG":proposed.last_format==SaveFormat::Jpeg?L"JPEG":proposed.last_format==SaveFormat::Bmp?L"BMP":L"GIF";
            copy->metadata.source_format=saved_format;copy->metadata.source_bytes=copy->file_bytes;
            if(image_metadata){image_metadata->source_format=saved_format;image_metadata->source_bytes=copy->file_bytes;}
            image = std::move(copy);
            requested = target;
            current_name = target.filename().wstring();
            modified = false;
            removed_current = false;
            // Undo now differs from the bytes just saved, even when the earlier image was clean.
            if (undo_image && !undo_location) {
                auto previous = std::make_shared<Image>(*undo_image);
                previous->path = target;
                previous->metadata.source_format=saved_format;previous->metadata.source_bytes=image->file_bytes;
                if(undo_metadata){undo_metadata->source_format=saved_format;undo_metadata->source_bytes=image->file_bytes;}
                undo_image = std::move(previous);
                undo_modified = true;
            }
            rebind_older_history(target,true);
            archive.clear();
            scan_folder(target.parent_path());
            sort_list();
        }
        update();
        if (persist_saving && !config.empty())
            save_settings();
        return true;
    }
    void bind_file(const std::filesystem::path &target) {
        // Loader images are shared immutable cache entries; do not change their source metadata.
        cancel_load(true);
        auto copy = std::make_shared<Image>(*image);
        copy->path = target;
        image = std::move(copy);
        if (undo_image && !undo_location) {
            auto previous = std::make_shared<Image>(*undo_image);
            previous->path = target;
            undo_image = std::move(previous);
        }
        rebind_older_history(target,false);
        requested = target;
        current_name = target.filename().wstring();
    }
    void remove_list_entry(const std::filesystem::path &source) {
        auto found = std::find_if(files.begin(), files.end(),
                                  [&](const FileEntry &entry) { return same_path(entry.path, source); });
        if (found != files.end()) {
            index = static_cast<size_t>(found - files.begin());
            files.erase(found);
        } else
            index = std::min(index, files.size());
        removed_current = true;
    }
    void advance_after_fileop() {
        if (!settings.next_after_fileop)
            return;
        if (!files.empty())
            open(files[std::min(index, files.size() - 1)].path, false, false, OpenOrigin::WithinFolder);
        else {
            image.reset();image_metadata.reset();undo_metadata.reset();
            clear_undo();
            undo_location.reset();
            modified = loading = drag_armed = removed_current = false;
            selection = {};
            requested.clear();
            current_name.clear();
            renderer.invalidate();
            layout();
        }
    }
    void fullscreen() {
        bar_seen = false;
        bool new_dimensions{};
        if (!full) {
            normal_view = view;
            view = full_view;
            new_dimensions =
                view.canvas_w != (image ? image->width : 1) || view.canvas_h != (image ? image->height : 1);
            if (view.canvas_w != (image ? image->width : 1) || view.canvas_h != (image ? image->height : 1))
                view.set_image(image ? image->width : 1, image ? image->height : 1, view.manual);
            GetWindowPlacement(hwnd, &placement);
            old_style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
            MONITORINFO m{sizeof(m)};
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &m);
            SetWindowLongPtrW(hwnd, GWL_STYLE, old_style & ~WS_OVERLAPPEDWINDOW);
            SetMenu(hwnd, nullptr);
            full = true;
            topmost = settings.full_topmost;
            apply_window_style();
            SetMenu(hwnd, settings.full_menu ? main_menu : nullptr);
            const RECT rect = settings.full_hide_taskbar ? m.rcMonitor : m.rcWork;
            SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, rect.left, rect.top,
                         rect.right - rect.left, rect.bottom - rect.top, SWP_FRAMECHANGED);
        } else {
            full_view = view;
            view = normal_view;
            new_dimensions =
                view.canvas_w != (image ? image->width : 1) || view.canvas_h != (image ? image->height : 1);
            if (view.canvas_w != (image ? image->width : 1) || view.canvas_h != (image ? image->height : 1))
                view.set_image(image ? image->width : 1, image ? image->height : 1, view.manual);
            full = false;
            SetWindowLongPtrW(hwnd, GWL_STYLE, old_style);
            topmost = settings.topmost;
            apply_window_style();
            SetMenu(hwnd, settings.menu ? main_menu : nullptr);
            SetWindowPlacement(hwnd, &placement);
            SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
        }
        render_settings();
        layout();
        if ((new_dimensions && !view.manual) ||
            (full ? full_display_policy_pending : normal_display_policy_pending))
            apply_display_policy();
        update();
    }
    FileTransferOptions transfer_options(bool move) const {
        return {move ? settings.move_basic_path : settings.copy_basic_path,
                move ? settings.move_destinations : settings.copy_destinations,
                move ? settings.move_registered : settings.copy_registered,
                move ? settings.confirm_move : settings.confirm_copy};
    }
    void apply_transfer_options(bool move, const FileTransferOptions &options) {
        auto next = settings;
        (move ? next.move_destinations : next.copy_destinations) = options.destinations;
        (move ? next.move_basic_path : next.copy_basic_path) = options.basic_path;
        (move ? next.move_registered : next.copy_registered) = options.registered;
        (move ? next.confirm_move : next.confirm_copy) = options.confirm;
        apply_settings(next);
    }
    void transfer_file(bool move, const std::filesystem::path &destination = {}, int initial_slot = -1) {
        cancel_load();
        if (!original_file())
            return;
        stop_slideshow();
        if (!discard())
            return;
        const auto source = image->path;
        auto directory = destination;
        if (directory.empty()) {
            auto options = transfer_options(move);
            if (initial_slot >= 0)
                options.registered = true;
            const auto choice = dialogs.file_transfer(hwnd, move, source, options, initial_slot);
            if (!choice || choice->directory.empty())
                return;
            if (!image || !same_path(source, image->path))
                throw std::runtime_error("Image changed while choosing the file transfer destination");
            apply_transfer_options(move, choice->options);
            directory = choice->directory;
        }
        if (directory.is_relative())
            directory = source.parent_path() / directory;
        directory = std::filesystem::absolute(directory).lexically_normal();
        const auto target = directory / source.filename();
        if (same_disk_file(source, target))
            throw std::runtime_error("Source and destination are the same file");
        const bool exists = std::filesystem::exists(target);
        if (exists && (move ? settings.confirm_move : settings.confirm_copy) &&
            dialogs.ask(hwnd, L"保存先の同名ファイルを上書きしますか？", target.c_str(),
                        MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        if (move) {
            move_file_checked(source, target, exists);
            bind_file(target);
            if (settings.next_after_fileop) {
                remove_list_entry(source);
                advance_after_fileop();
            } else {
                removed_current = false;
                scan_folder(directory);
            }
            update();
        } else
            copy_file_checked(source, target, exists);
    }
    ExternalContext external_context(const ExternalApplication &entry) const {
        ExternalContext result;
        result.viewer_directory = viewer_executable().parent_path();
        result.image_available = image != nullptr;
        const auto displayed = !image ? std::filesystem::path{} : !image->path.empty() ? image->path :
                               before_load ? before_load->requested : requested;
        const auto source = source_file(displayed);
        const auto previous = before_load && before_load->replaced ? &*before_load->replaced : nullptr;
        result.file_path = source.wstring();
        if (!source.empty()) result.folder_path = source.parent_path().wstring();
        else {
            auto directory = previous ? previous->folder : folder;
            if (directory.empty()) directory = previous ? previous->clipboard_folder : clipboard_folder;
            result.folder_path = directory.wstring();
        }
        if (!source.empty() && displayed != source) {
            if (entry.archive_file_for_folder) result.folder_path = source.wstring();
            if (entry.include_archive_member) {
                const auto text = displayed.wstring(); const auto bar = text.find_last_of(L'|');
                size_t used{}; const auto number = std::stoul(text.substr(bar + 1), &used);
                if (bar == std::wstring::npos || used != text.size() - bar - 1)
                    throw std::runtime_error("Invalid displayed archive member");
                const auto &entries = previous ? previous->entries : archive_entries;
                const auto member = std::find_if(entries.begin(), entries.end(), [&](const auto &value) { return value.index == number; });
                if (member == entries.end()) throw std::runtime_error("Cannot identify displayed archive member");
                auto name = member->name; std::replace(name.begin(), name.end(), L'/', L'\\');
                result.file_path += L"\\" + name;
            }
        }
        return result;
    }
    void external(const ExternalApplication &entry) {
        if (entry.send == ExternalSend::DisplayedPng && !image) return;
        auto context = external_context(entry);
        ExternalLaunch launch;
        if (entry.send == ExternalSend::DisplayedPng) {
            auto preview = entry; preview.send = ExternalSend::None;
            launch = plan_external_application(preview, context);
            if (!launch.explicit_arguments) {
                const auto metadata=current_metadata();
                context.displayed_png = drag_path(*image, player.frame, modified || image->format == L"見開き",&metadata);
                launch = plan_external_application(entry, context);
            }
        } else launch = plan_external_application(entry, context);
        if (launch.copy_image) {
            auto frame = copied_image();
            copy_current_selection(frame);
        }
        if (dialogs.external_launch(hwnd, launch) && launch.quit_after_launch)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    void render_settings() {
        const bool background_changed = renderer.checker_light != settings.checker_light ||
                                        renderer.checker_dark != settings.checker_dark ||
                                        renderer.checker_size != settings.checker_size ||
                                        renderer.background_tiled != settings.background_tiled ||
                                        renderer.background_pattern != settings.background_pattern ||
                                        renderer.background_first != settings.background_first ||
                                        renderer.background_second != settings.background_second;
        renderer.checkerboard = settings.checker;
        renderer.nearest = false; // Legacy override switches no longer mask the quality choice.
        renderer.down = canonical_display_filter(full ? settings.full_down : settings.display_down);
        renderer.up = canonical_display_filter(full ? settings.full_up : settings.display_up);
        renderer.fit_simple = settings.fit_simple;
        renderer.zoom_simple = settings.zoom_simple;
        renderer.zoom_reduce = settings.zoom_reduce;
        renderer.zoom_enlarge = settings.zoom_enlarge;
        renderer.selection_mode = selection_active();
        renderer.dragging_low = settings.drag_simple && active_drag_action == MouseZoom;
        renderer.overlay.family = settings.overlay_family;
        renderer.overlay.font_size = settings.overlay_points * (GetDpiForWindow(hwnd) / 720.0f);
        renderer.overlay.dpi = static_cast<float>(GetDpiForWindow(hwnd));
        renderer.overlay.weight = settings.overlay_weight;
        renderer.overlay.charset = settings.overlay_charset;
        renderer.overlay.italic = settings.overlay_italic;
        renderer.overlay.underline = settings.overlay_underline;
        renderer.overlay.strikeout = settings.overlay_strikeout;
        renderer.overlay.position = settings.overlay_position;
        renderer.overlay.background = settings.overlay_background;
        renderer.overlay.foreground = settings.overlay_foreground;
        renderer.overlay.backcolor = settings.overlay_backcolor;
        refresh_overlay();
        renderer.background = settings.background;
        renderer.background_tiled = settings.background_tiled;
        renderer.background_pattern = settings.background_pattern;
        renderer.background_first = settings.background_first;
        renderer.background_second = settings.background_second;
        renderer.checker_light = settings.checker_light;
        renderer.checker_dark = settings.checker_dark;
        renderer.checker_size = settings.checker_size;
        if (background_changed)
            renderer.reset_patterns();
    }
    void apply_settings(const Settings &value) {
        Settings next = value;
        next.set_end_action(next.effective_end_action());
        next.title_path = next.title_mode == 1;
        if (!next.mouse.valid())
            throw std::invalid_argument("Invalid mouse configuration");
        next.history = settings.history;
        next.last_file = settings.last_file;
        next.last_archive_entry = settings.last_archive_entry;
        if (!next.remember_history)
            next.history.clear();
        if (!smoke)
            persisted_settings(next).save(config);
        const bool rescan = next.hidden != settings.hidden || next.system_files != settings.system_files ||
                            next.protected_files != settings.protected_files;
        const bool resort = next.sort != settings.sort || next.reverse != settings.reverse ||
                            next.natural != settings.natural;
        auto next_loading = next.loading_options();
        const auto old_loading = settings.loading_options();
        const bool gif_style_changed = next_loading.gif_speed != old_loading.gif_speed;
        next_loading.gif_speed = old_loading.gif_speed;
        const bool reload_decode = next_loading != old_loading ||
                                   (gif_style_changed && (loading || (image && image->format == L"GIF")));
        const bool cache_changed =
            next.cache_mib != settings.cache_mib || next.prefetch_enabled != settings.prefetch_enabled;
        if ((pending_folder && (rescan || resort)) || (pending_open && (rescan || resort || reload_decode)))
            cancel_load();
        const bool normal_refit = next.initial_fit != settings.initial_fit;
        const bool full_refit = next.full_initial_fit != settings.full_initial_fit;
        const bool refit = full ? full_refit : normal_refit;
        const bool normal_redisplay = next.display_mode != settings.display_mode;
        const bool full_redisplay = next.full_display_mode != settings.full_display_mode;
        const bool redisplay = full ? full_redisplay : normal_redisplay;
        normal_display_policy_pending |= normal_redisplay || normal_refit;
        full_display_policy_pending |= full_redisplay || full_refit;
        const bool full_bounds = full && next.full_hide_taskbar != settings.full_hide_taskbar;
        const bool recombine = next.comic_rtl != settings.comic_rtl ||
                               next.comic_options().key() != settings.comic_options().key();
        settings = std::move(next);
        ime.apply(hwnd, settings.disable_ime);
        if (cache_changed) {
            loader->configure(static_cast<size_t>(settings.cache_mib) * 1024 * 1024,
                              settings.prefetch_enabled);
            prefetch_neighbors();
        }
        cancel_pointer();
        if (full_bounds) {
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
            const RECT rect = settings.full_hide_taskbar ? monitor.rcMonitor : monitor.rcWork;
            SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        topmost = full ? settings.full_topmost : settings.topmost;
        apply_window_style();
        SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!settings.undo_enabled) {
            clear_undo();
            undo_location.reset();
        }
        player.set_loop(settings.animation_loop, settings.animation_file_loops);
        render_settings();
        if (normal_refit)
            normal_fit = normal_view.fit = static_cast<Fit>(settings.initial_fit);
        if (full_refit)
            full_fit = full_view.fit = static_cast<Fit>(settings.full_initial_fit);
        if (refit)
            view.apply_fit(full ? full_fit : normal_fit);
        if (rescan && archive.empty() && !folder.empty())
            scan_folder(folder);
        else if (resort)
            sort_list();
        if (slideshow)
            SetTimer(hwnd, 2, settings.interval, nullptr);
        refresh_menu();
        layout();
        if (redisplay || refit)
            apply_display_policy();
        update();
        if (((comic && recombine) || reload_decode) && !modified && !requested.empty() &&
            (!image || !image->path.empty() || !archive.empty() || comic))
            open(requested, false, false, OpenOrigin::WithinFolder);
    }
    bool preferences(PreferencePage page = PreferencePage::General, int destination_row = -1) {
        return dialogs.preferences_ui(
            hwnd, settings, [this](const Settings &value) { apply_settings(value); }, page, destination_row);
    }
    void registered_transfer(bool move, size_t slot) {
        const auto &destination = (move ? settings.move_destinations : settings.copy_destinations).at(slot);
        transfer_file(move, destination, static_cast<int>(slot));
    }
    void color() {
        static COLORREF custom[16]{};
        CHOOSECOLORW c{sizeof(c)};
        c.hwndOwner = hwnd;
        c.lpCustColors = custom;
        c.rgbResult =
            RGB(settings.background_first >> 16 & 255, settings.background_first >> 8 & 255,
                settings.background_first & 255);
        c.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&c)) {
            settings.background =
                (GetRValue(c.rgbResult) << 16) | (GetGValue(c.rgbResult) << 8) | GetBValue(c.rgbResult);
            settings.background_first = settings.background;
            render_settings();
        }
    }
    ImageInfoData image_information() const {
        if(!image)return {};
        ImageInfoData data;data.image=image;const auto metadata=current_metadata();
        const auto dimensions=[](uint32_t w,uint32_t h){return std::to_wstring(w)+L" × "+std::to_wstring(h);};
        const auto path=image->path;const auto disk=source_file(path);
        data.fields={{L"ファイル名",current_name},{L"ファイルの場所",path.empty()?L"（未保存）":path.parent_path().wstring()},
            {L"フルパス",path.empty()?L"（未保存）":path.wstring()},{L"ファイル形式",metadata.source_format.empty()?image->format:metadata.source_format},
            {L"読み込み時",dimensions(metadata.original_width?metadata.original_width:image->width,metadata.original_height?metadata.original_height:image->height)+L" / 元の色深度: "+(metadata.original_bpp?std::to_wstring(metadata.original_bpp):L"不明")+L" BPP ("+image_color_type_name(metadata.original_color_type)+L")"},
            {L"現在のサイズ",dimensions(image->width,image->height)+L" RGBA 32 BPP"}};
        std::wostringstream dpi,size;dpi<<std::fixed<<std::setprecision(3)<<metadata.dpi_x<<L" × "<<metadata.dpi_y<<L" DPI"<<(metadata.dpi_defined?L"":L"（未指定のため72 DPI）");
        size<<std::fixed<<std::setprecision(3)<<image->width*25.4/metadata.dpi_x<<L" × "<<image->height*25.4/metadata.dpi_y<<L" mm";
        data.fields.emplace_back(L"解像度",dpi.str());data.fields.emplace_back(L"印刷サイズ",size.str());
        data.fields.emplace_back(L"ICCプロファイル",metadata.icc_profile?L"埋め込みあり":L"なし");
        data.fields.emplace_back(L"フレーム",std::to_wstring(image->frames.size()));
        WIN32_FILE_ATTRIBUTE_DATA attributes{};
        const bool has_attributes=!disk.empty()&&GetFileAttributesExW(disk.c_str(),GetFileExInfoStandard,&attributes);
        uint64_t disk_bytes=path.empty()?0:metadata.source_bytes?metadata.source_bytes:image->file_bytes;
        if(has_attributes&&disk==path)disk_bytes=(uint64_t(attributes.nFileSizeHigh)<<32)|attributes.nFileSizeLow;
        data.fields.emplace_back(L"ディスク上のサイズ",size_text(disk_bytes)+L" ("+std::to_wstring(disk_bytes)+L" bytes)");
        data.fields.emplace_back(L"メモリ上のサイズ",size_text(image->bytes())+L" ("+std::to_wstring(image->bytes())+L" bytes)");
        auto found=files.end();if(!path.empty()){if(index<files.size()&&same_path(files[index].path,path))found=files.begin()+index;else found=std::find_if(files.begin(),files.end(),[&](const auto &entry){return same_path(entry.path,path);});}
        data.fields.emplace_back(L"フォルダ内の順番",found==files.end()?L"—":std::to_wstring(found-files.begin()+1)+L" / "+std::to_wstring(files.size()));
        std::wstring modified_time=L"—";
        if (has_attributes) {
            FILETIME local{};
            SYSTEMTIME time{};
            if (FileTimeToLocalFileTime(&attributes.ftLastWriteTime, &local) &&
                FileTimeToSystemTime(&local, &time)) {
                wchar_t formatted[64]{};
                swprintf_s(formatted, L"%04u-%02u-%02u %02u:%02u:%02u", time.wYear, time.wMonth,
                           time.wDay, time.wHour, time.wMinute, time.wSecond);
                modified_time=formatted;
            }
        }
        data.fields.emplace_back(L"更新日時",modified_time);data.fields.emplace_back(L"読み込み時間",std::to_wstring(metadata.source_load_ms?metadata.source_load_ms:image->load_ms)+L" ms");return data;
    }
    void show_info() {
        if(!image)return;
        const auto data=image_information();const auto frame=player.frame;const auto physical=editing.physical_bytes;const auto allocation=editing.allocating;
        const bool overflow=settings.allow_physical_overflow;
        const auto automatic=dialogs.image_info(hwnd,data,settings.info_auto_colors,[snapshot=data.image,frame,physical,allocation,overflow](std::stop_token stop){
            return count_image_colors(snapshot->frames.at(frame).pixels,stop,ColorCountMemory{overflow?UINT64_MAX:physical(),allocation});
        });
        if(automatic!=settings.info_auto_colors){settings.info_auto_colors=automatic;settings.save(config);}
    }
#include "viewer_editor.inc"
    void command(UINT id) {
        if(id==ToolPen||id==ToolText){choose_tool(id==ToolPen?SelectionTool::Pen:SelectionTool::Text);return;}
        if(id==ToolCrop){begin_crop();return;}
        if(cropping){if(id==CommitSelection){commit_crop();return;}if(id==CancelSelection||id==Undo||id==ClearSelection){cancel_crop();return;}
            const bool viewing=(id>=ZoomIn&&id<=Nearest)||(id>=ScrollUp&&id<=ScrollCenter)||id==ToggleMenu||id==ToggleToolbar||id==ToggleStatus;
            if(!viewing&&id!=ToggleEditor)cancel_crop();}
        if(id==ToggleEditor){set_edit_mode(!editor_mode);return;}
        if(floating){
            if(id==CommitSelection){commit_floating();return;}
            if(id==CancelSelection||id==Undo||id==ClearSelection){cancel_floating();return;}
            if(id==TransformSelection){floating_numbers();return;}
            const bool viewing=(id>=ZoomIn&&id<=Nearest)||id==ToggleToolbar||id==ToggleStatus||id==ToggleMenu||
                id==ToggleCaption||id==ToggleOuterFrame||id==ToggleInnerFrame||id==ToggleScrollbars||
                id==ZoomNumber||(id>=ScrollUp&&id<=ScrollCenter)||id==Info||id==Help;
            const bool leaving=id==Open||id==Reload||id==Exit||id==CloseImage||(id>=Previous&&id<=Last)||
                (id>=ParentFolder&&id<=NextFolder)||id==Paste||id==NewPaste;
            if(leaving)cancel_floating();
            else if(!viewing&&id!=PasteSelection){status_text(image?4:0,L"先に貼り付けを確定するか、中止してください");return;}
        }
        if(id==PasteSelection){begin_floating();return;}
        if(id==CommitSelection||id==CancelSelection||id==TransformSelection)return;
        if ((id >= PopupAll && id <= PopupExternal) || id == ViewRotate || id == Wallpaper) {
            popup_menu(id, cursor_screen());
            return;
        }
        if (id >= WallpaperCenter && id <= WallpaperDelete) {
            WallpaperResult result;
            if (id == WallpaperDelete) {
                if (!has_wallpaper_files(config) ||
                    dialogs.ask(hwnd, L"GVNが生成した壁紙ファイルを削除し、壁紙の設定を解除しますか？",
                                L"壁紙ファイルを削除", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
                    return;
                result = delete_wallpaper_images(config, dialogs.wallpaper_system());
            } else {
                if (!image)
                    return;
                const auto source = image;
                result = set_wallpaper_image(*source, player.frame, config,
                    static_cast<WallpaperMode>(id - WallpaperCenter), dialogs.wallpaper_system());
            }
            refresh_menu();
            update();
            if (result.cleanup_incomplete)
                dialogs.ask(hwnd, L"壁紙の設定は更新しましたが、一部の以前の壁紙ファイルを削除できませんでした。"
                                  L"使用中のファイルは保持しています。", L"壁紙", MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (id >= ViewRotateNone && id <= ViewRotateHalf) {
            const int mode = static_cast<int>(id - ViewRotateNone);
            if (mode == settings.view_rotation)
                return; // Reselecting the active option preserves edits and Undo, as in the reference.
            settings.view_rotation = mode;
            // Metadata/folder work will request pixels using the new settings when it completes.
            // A decode already in flight must be replaced. Clipboard/new documents stay untouched.
            if (!pending_open && !pending_folder && !requested.empty() &&
                (loading || !image || !image->path.empty() || !archive.empty() || comic))
                open(requested, false, false, OpenOrigin::WithinFolder);
            refresh_menu();
            update();
            return;
        }
        if (id >= SwapRG && id <= SwapBackward) {
            const auto rect = selection;
            edit([&](const Image &source) { return swap_channels(source, id - SwapRG, rect); });
            update();
            return;
        }
        if (id >= HistoryBase && id < HistoryBase + settings.history.size()) {
            open(settings.history[id - HistoryBase]);
            return;
        }
        if (id >= ExternalBase && id < ExternalBase + 9) {
            external(settings.external_application(id - ExternalBase));
            return;
        }
        if (id >= CopyDestBase && id < CopyDestBase + 10) {
            registered_transfer(false, id - CopyDestBase);
            return;
        }
        if (id >= MoveDestBase && id < MoveDestBase + 10) {
            registered_transfer(true, id - MoveDestBase);
            return;
        }
        if (id >= SortName && id <= SortRandom) {
            if (pending_folder || pending_open)
                cancel_load();
            settings.sort = id - SortName;
            sort_list();
            refresh_menu();
            update();
            return;
        }
        switch (id) {
        case ToggleHover:
            settings.mouse.assign(MouseGesture::Hover, 0, 0, checked(ToggleHover) ? MouseNone : MouseHover);
            hover_seen = false;
            break;
        case ScrollUp:
            scroll(0, settings.mouse.wheel_scroll);
            break;
        case ScrollDown:
            scroll(0, -settings.mouse.wheel_scroll);
            break;
        case ScrollLeft:
            scroll(settings.mouse.wheel_scroll, 0);
            break;
        case ScrollRight:
            scroll(-settings.mouse.wheel_scroll, 0);
            break;
        case ReadScrollLeft:
            scroll(comic && settings.comic_rtl ? -settings.mouse.wheel_scroll : settings.mouse.wheel_scroll,
                   0);
            break;
        case ReadScrollRight:
            scroll(comic && settings.comic_rtl ? settings.mouse.wheel_scroll : -settings.mouse.wheel_scroll,
                   0);
            break;
        case ScrollTop:
        case ScrollBottom:
        case ScrollLeftEdge:
        case ScrollRightEdge:
        case ScrollTopLeft:
        case ScrollTopRight:
        case ScrollBottomLeft:
        case ScrollBottomRight:
        case ScrollCenter:
            position_scroll(id);
            break;
        case ToggleActualFit:
            view.apply_fit(!view.manual && view.fit == Fit::Actual ? Fit::Contain : Fit::Actual);
            break;
        case ToggleFitActual:
            view.apply_fit(!view.manual && view.fit == Fit::Contain ? Fit::Actual : Fit::Contain);
            break;
        case ReadPrevious:
            navigate(comic && settings.comic_rtl ? page_step() : -page_step());
            break;
        case ReadNext:
            navigate(comic && settings.comic_rtl ? -page_step() : page_step());
            break;
        case RandomJump:
            if (files.size() > 1 || (removed_current && !files.empty())) {
                std::uniform_int_distribution<size_t> choose(0, files.size() - (removed_current ? 1 : 2));
                auto target = choose(random);
                if (!removed_current && target >= index)
                    ++target;
                open(files[target].path, false, true, OpenOrigin::WithinFolder);
            }
            break;
        case Open:
            choose();
            break;
        case Reload:
            if (!requested.empty())
                open(requested, true, true, OpenOrigin::WithinFolder);
            break;
        case Exit:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        case Previous:
            navigate(-page_step());
            break;
        case Next:
            navigate(page_step());
            break;
        case First:
            if (!files.empty())
                open(files.front().path, false, true, OpenOrigin::WithinFolder);
            break;
        case Last:
            if (!files.empty())
                open(files.back().path, false, true, OpenOrigin::WithinFolder);
            break;
        case SkipBack:
            navigate(-settings.skip_count);
            break;
        case SkipNext:
            navigate(settings.skip_count);
            break;
        case Jump: {
            cancel_load();
            stop_slideshow();
            if (files.empty())
                break;
            KillTimer(hwnd, kPreviewCleanupTimer);
            if (!preview_service)
                preview_service = std::make_shared<JumpPreview>();
            struct Retirement {
                App &app;
                ~Retirement() {
                    app.retire_jump_preview();
                }
            } retirement{*this};
            const auto source = image;
            const auto source_path = requested, source_folder = folder;
            const auto count = files.size();
            const auto choice = dialogs.jump(hwnd, files, std::min(index, files.size() - 1),
                                             comic && settings.comic_rtl && settings.comic_jump_reverse,
                                             {settings.jump_preview, settings.jump_quality},
                                             settings.loading_options(), preview_service);
            if (!choice || source != image || source_path != requested || source_folder != folder ||
                count != files.size() || choice->index >= files.size() ||
                !same_path(choice->path, files[choice->index].path) || choice->options.quality < 0 ||
                choice->options.quality > 2)
                break;
            auto next = settings;
            next.jump_preview = choice->options.preview;
            next.jump_quality = choice->options.quality;
            apply_settings(next);
            open(choice->path, false, true, OpenOrigin::WithinFolder);
            break;
        }
        case ParentFolder:
            move_folder(FolderMove::Parent);
            break;
        case ChildFolder: {
            auto path = dialogs.folder(hwnd, L"開くフォルダ");
            if (!path.empty())
                open(path);
            break;
        }
        case PreviousFolder:
            move_folder(FolderMove::Previous);
            break;
        case NextFolder:
            move_folder(FolderMove::Next);
            break;
        case PreviousAdjacentFolder:
            move_folder(FolderMove::AdjacentPrevious);
            break;
        case NextAdjacentFolder:
            move_folder(FolderMove::AdjacentNext);
            break;
        case RefreshList:
            if (pending_open)
                cancel_load();
            if (archive.empty() && !folder.empty()) {
                scan_folder(folder);
                update();
            }
            break;
        case SortReverse:
            if (pending_folder || pending_open)
                cancel_load();
            settings.reverse = !settings.reverse;
            sort_list();
            break;
        case NaturalSort:
            if (pending_folder || pending_open)
                cancel_load();
            settings.natural = !settings.natural;
            sort_list();
            break;
        case ShowHidden:
            if (pending_folder || pending_open)
                cancel_load();
            settings.hidden = !settings.hidden;
            if (archive.empty() && !folder.empty())
                scan_folder(folder);
            break;
        case ListLoop:
            settings.set_end_action(settings.list_loop ? 0 : 2);
            break;
        case KeepZoom:
            settings.keep_zoom = !settings.keep_zoom;
            break;
        case Slideshow:
            if (slideshow)
                stop_slideshow();
            else
                start_slideshow();
            break;
        case SlideInterval:
            if (auto value =
                    number(L"スライド間隔", L"秒（0.25～3600）", settings.interval / 1000.0, .25, 3600)) {
                settings.interval = static_cast<int>(*value * 1000);
                if (slideshow)
                    SetTimer(hwnd, 2, settings.interval, nullptr);
            }
            break;
        case WindowRestore:
            if (full)
                fullscreen();
            ShowWindow(hwnd, SW_RESTORE);
            break;
        case WindowMinimize:
            ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_MINIMIZE);
            break;
        case WindowMaximize:
            if (full)
                fullscreen();
            ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            break;
        case ToggleCaption:
            settings.caption = !settings.caption;
            apply_window_style();
            layout();
            break;
        case ToggleOuterFrame:
            if (!settings.caption) {
                settings.outer_frame = !settings.outer_frame;
                apply_window_style();
                layout();
            }
            break;
        case ToggleInnerFrame:
            settings.inner_frame = !settings.inner_frame;
            apply_window_style();
            layout();
            break;
        case DisplayWindowImage:
        case DisplayImageWindow:
        case DisplayLargeWindow:
        case DisplayImageDesktop:
        case DisplayLargeDesktop:
        case DisplayUnchanged:
            (full ? settings.full_display_mode : settings.display_mode) =
                static_cast<int>(id - DisplayWindowImage);
            apply_display_policy();
            update();
            break;
        case ZoomIn:
            step_zoom(true, view.width / 2, view.height / 2);
            break;
        case ZoomOut:
            step_zoom(false, view.width / 2, view.height / 2);
            break;
        case Actual:
            view.apply_fit(Fit::Actual);
            break;
        case FitImage:
            (full ? full_fit : normal_fit) = Fit::Contain;
            (full ? settings.full_initial_fit : settings.initial_fit) = static_cast<int>(Fit::Contain);
            view.apply_fit(Fit::Contain);
            break;
        case FitWidth:
            (full ? full_fit : normal_fit) = Fit::Width;
            (full ? settings.full_initial_fit : settings.initial_fit) = static_cast<int>(Fit::Width);
            view.apply_fit(Fit::Width);
            break;
        case FitHeight:
            (full ? full_fit : normal_fit) = Fit::Height;
            (full ? settings.full_initial_fit : settings.initial_fit) = static_cast<int>(Fit::Height);
            view.apply_fit(Fit::Height);
            break;
        case FitCover:
            (full ? full_fit : normal_fit) = Fit::Cover;
            (full ? settings.full_initial_fit : settings.initial_fit) = static_cast<int>(Fit::Cover);
            view.apply_fit(Fit::Cover);
            break;
        case ZoomNumber:
            if (auto value = number(L"表示倍率", L"倍率（0.001～5000 %）", view.scale * 100, .001, 5000))
                view.zoom_to(*value / 100, view.width / 2, view.height / 2);
            break;
        case Fullscreen:
            fullscreen();
            break;
        case Topmost:
            topmost = !topmost;
            (full ? settings.full_topmost : settings.topmost) = topmost;
            SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            break;
        case Checker:
            settings.checker = !settings.checker;
            renderer.checkerboard = settings.checker;
            break;
        case Nearest:
            {auto &down=full?settings.full_down:settings.display_down;auto &up=full?settings.full_up:settings.display_up;
                down=up=down==DisplaySimple&&up==DisplaySimple?DisplayLanczos3:DisplaySimple;}
            render_settings();
            break;
        case Background:
            color();
            break;
        case ToggleToolbar:
            (full ? settings.full_toolbar : settings.toolbar) =
                !(full ? settings.full_toolbar : settings.toolbar);
            layout();
            break;
        case ToggleStatus:
            (full ? settings.full_status : settings.status) =
                !(full ? settings.full_status : settings.status);
            layout();
            break;
        case ToggleMenu:
            (full ? settings.full_menu : settings.menu) = !(full ? settings.full_menu : settings.menu);
            refresh_menu();
            layout();
            break;
        case ToggleScrollbars:
            (full ? settings.full_scrollbars : settings.scrollbars) =
                !(full ? settings.full_scrollbars : settings.scrollbars);
            layout();
            break;
        case Comic:
            comic = !comic;
            if (!requested.empty())
                open(requested, false, true, OpenOrigin::WithinFolder);
            break;
        case ComicRTL:
            settings.comic_rtl = !settings.comic_rtl;
            if (comic && !requested.empty())
                open(requested, false, true, OpenOrigin::WithinFolder);
            break;
        case ComicSingleWide:
            settings.comic_single_wide = !settings.comic_single_wide;
            if (comic && !requested.empty())
                open(requested, false, true, OpenOrigin::WithinFolder);
            break;
        case ComicHeight:
            settings.comic_mode =
                settings.comic_mode >= 3 ? (settings.comic_mode == 3 ? 4 : 3) : (settings.comic_mode + 1) % 3;
            if (comic && !requested.empty())
                open(requested, false, true, OpenOrigin::WithinFolder);
            break;
        case ComicOffset:
            navigate(1);
            break;
        case ComicOffsetBack:
            navigate(-1);
            break;
        case Play:
            if (image && image->animated()) {
                if (player.playing)
                    player.playing = false;
                else
                    player.play();
            } else
                navigate(page_step());
            break;
        case FramePrevious:
            player.step(-1);
            break;
        case FrameNext:
            player.step(1);
            break;
        case StopAnimation:
            player.playing = false;
            player.seek(0);
            break;
        case FirstFrame:
            player.playing = false;
            player.seek(0);
            break;
        case LastFrame:
            player.playing = false;
            if (image)
                player.seek(image->frames.size() - 1);
            break;
        case Loop:
            settings.animation_loop = !player.loop;
            if (settings.animation_loop)
                settings.animation_file_loops = false;
            player.set_loop(settings.animation_loop, settings.animation_file_loops);
            break;
        case Copy:
            if (image) {
                auto frame = copied_image();
                copy_current_selection(frame);
            }
            break;
        case Paste:
        case NewPaste: {
            auto content = paste_clipboard(hwnd);
            if (auto path = std::get_if<std::filesystem::path>(&content)) {
                open(*path);
                break;
            }
            auto pasted = std::move(std::get<Image>(content));
            if (id == Paste && image && (!selection.empty() || !settings.paste_new)) {
                const auto rect = selection.empty() ? Rect{0, 0, static_cast<int>(image->width),
                                                           static_cast<int>(image->height)}
                                                    : selection;
                const auto before=image;
                edit([&](const Image &source) { return paste_region(source, pasted, rect); });
                if(image!=before)editor_chrome.reset_export_name();
                break;
            }
            if (discard()) {
                replace_clipboard_document(std::move(pasted));
            }
            break;
        }
        case CloseImage:
            if (discard()) {
                cancel_pointer();
                cancel_load(true);
                if (!smoke)
                    settings.remember(folder);
                stop_slideshow();
                drag_armed = false;
                image.reset();image_metadata.reset();undo_metadata.reset();
                clear_undo();
                undo_location.reset();
                modified = loading = false;
                removed_current = false;
                selection = {};
                files.clear();
                folder.clear();
                requested.clear();
                archive.clear();
                current_name.clear();
                renderer.invalidate();
                layout();
            }
            break;
        case Cut:
            if (image && !selection.empty()) {
                auto frame = copied_image();
                copy_current_selection(frame);
                const auto rect = selection;
                const auto color = settings.cut_color;
                const auto fill = settings.cut_transparent ? Pixel{}
                                                           : Pixel{static_cast<uint8_t>(color >> 16),
                                                                   static_cast<uint8_t>(color >> 8),
                                                                   static_cast<uint8_t>(color), 255};
                edit([&](const Image &source) {
                    auto out = source;
                    out.frames[0].indexed.reset();
                    for (int y = rect.y; y < rect.y + rect.h; ++y)
                        for (int x = rect.x; x < rect.x + rect.w; ++x)
                            out.frames[0].pixels[static_cast<size_t>(y) * out.width + x] = fill;
                    return out;
                });
            }
            break;
        case ArmDrag:
            drag_armed = true;
            SetFocus(hwnd);
            break;
        case Undo:history_step(false);break;
        case Redo:history_step(true);break;
        case SelectMode:
            if(editor_mode&&selection_tool==SelectionTool::Rectangle)set_edit_mode(false);
            else choose_tool(SelectionTool::Rectangle);
            break;
        case ToolPan:choose_tool(SelectionTool::Pan);break;
        case SelectRectangle:case SelectFreehand:case SelectPolygon:case SelectMove:
            choose_tool(id==SelectRectangle?SelectionTool::Rectangle:id==SelectFreehand?SelectionTool::Freehand:id==SelectPolygon?SelectionTool::Polygon:SelectionTool::Move);break;
        case InvertSelection:
            cancel_selection_path();if(image)selection=selection.inverted(image->width,image->height);break;
        case DeleteSelection:
            if(image&&!selection.empty()){const auto region=selection;edit([&](const Image &source){return erase_selection(source,region,{},edit_memory());});}break;
        case ClearSelection:
            cancel_selection_path();
            selection = {};
            break;
        case SelectAll:
            cancel_selection_path();
            if (image)
                selection = {0, 0, static_cast<int>(image->width), static_cast<int>(image->height)};
            break;
        case Grid:
            if(auto choice=dialogs.selection_units(hwnd,settings.custom_selection_units)) {
                if(!valid_selection_units(*choice))throw std::invalid_argument("Invalid selection units");
                auto next=settings;next.custom_selection_units=*choice;next.selection_custom=true;
                if(choice->start_x==choice->start_y&&choice->start_x==choice->size_x&&choice->start_x==choice->size_y&&choice->start_x<=256)next.grid=choice->start_x;
                persisted_settings(next).save(config);settings=std::move(next);
            }
            break;
        case GridFree:case Grid2:case Grid4:case Grid8:case Grid16:case Grid32: {
            const int units[]{1,2,4,8,16,32};auto next=settings;next.grid=units[id-GridFree];next.selection_custom=false;
            persisted_settings(next).save(config);settings=std::move(next);
            break;
        }
        case SelectNumbers:
            if (image) {
                const auto source=image;
                const auto chosen=dialogs.selection_rect(hwnd,source->width,source->height,selection);
                if(chosen&&image==source)selection=intersect(*chosen,{0,0,int(source->width),int(source->height)});
            }
            break;
        case Crop:
            if (!selection.empty()) {
                const auto rect = selection;
                edit([&](const Image &source) { return selection_image(source, rect,edit_memory()); },false);
            }
            break;
        case RotateRight:
            edit([](const Image &source) { return rotate_quarters(source, 1); },false);
            break;
        case RotateLeft:
            edit([](const Image &source) { return rotate_quarters(source, -1); },false);
            break;
        case Rotate180:
            edit([](const Image &source) { return rotate_quarters(source, 2); },false);
            break;
        case FlipH:
            edit([](const Image &source) { return flip(source, true); },false);
            break;
        case FlipV:
            edit([](const Image &source) { return flip(source, false); },false);
            break;
        case RotateAngle:
            if (image)
                if (auto angle = number(L"角度指定回転", L"時計回りの角度（-360～360度）", 0, -360, 360))
                    edit([&](const Image &source) { return rotate_image(source, *angle, edit_memory()); },false);
            break;
        case AutoLevels:
        case AutoContrast: {
            const auto rect = selection;
            edit([&](const Image &source) { return auto_levels(source, id == AutoLevels, rect); });
            break;
        }
        case Levels:
            levels_edit();
            break;
        case Gaussian:
            spatial_filter_edit(false);
            break;
        case Unsharp:
            spatial_filter_edit(true);
            break;
        case Sharpen:case SharpenStrong:case SharpenEdges:case Blur:case BlurStrong:case BlurSoft: {
            const auto preset=id==Sharpen?FilterPreset::Sharpen:id==SharpenStrong?FilterPreset::SharpenStrong:id==SharpenEdges?FilterPreset::SharpenEdges:id==Blur?FilterPreset::Blur:id==BlurStrong?FilterPreset::BlurStrong:FilterPreset::BlurSoft;
            const auto rect=selection;edit([&](const Image &source){return gvn::spatial_filter(source,filter_preset(preset),rect,0,edit_memory());});selection=rect;
            break;
        }
        case GlassV:
        case GlassH:
        case Xor80:
        case WinMask:
        case MaskNegative:
        case MaskSwap:
        case Q0Mask:
        case FLMask:
        case MekoForward:
        case MekoReverse: {
            const auto rect = selection;
            const auto original = image;
            const auto options = settings.mask_options();
            const auto operation = id == GlassV ? Mask::Vertical : id == GlassH ? Mask::Horizontal
                                 : id == Xor80 ? Mask::Xor : id == WinMask ? Mask::Win
                                 : id == MaskNegative ? Mask::Negative : id == MaskSwap ? Mask::Swap
                                 : id == FLMask ? Mask::FL : id == MekoForward ? Mask::MekoForward
                                 : id == MekoReverse ? Mask::MekoReverse : Mask::Q0;
            edit([&](const Image &source) { return mask_image(source, operation, options, rect); });
            if (image != original) { selection = rect; update(); }
            break;
        }
        case ConfigureMask:
            preferences(PreferencePage::Mask);
            break;
        case RainbowMask:
            rainbow_edit();
            break;
        case CPMask:
            cp_edit();
            break;
        case PaletteEdit:
            palette_edit();
            break;
        case ConvertColorDepth:
            color_depth_edit();
            break;
        case ColorAdjust:
            color_adjustment_edit();
            break;
        case Brightness:
            brightness_edit();
            break;
        case ReplaceColor:
            if (image) {
                auto text = dialogs.text(
                    hwnd, L"色の置き換え",
                    L"元のRGB色, 変更後のRGB色（各6桁の16進数）\n透明度は変更しません。例: FF0000, 00FF00",
                    L"FF0000, 00FF00");
                if (text) {
                    std::wistringstream in(*text);
                    uint32_t from{}, to{};
                    wchar_t comma{};
                    if (!(in >> std::hex >> from >> comma >> to) || comma != L',' || from > 0xffffff ||
                        to > 0xffffff || !(in >> std::ws).eof())
                        throw std::runtime_error("Enter two RGB colors");
                    auto pixel = [](uint32_t rgb) {
                        return Pixel{static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8),
                                     static_cast<uint8_t>(rgb), 255};
                    };
                    const auto rect = selection;
                    edit([&](const Image &source) {
                        return replace_color(source, pixel(from), pixel(to), rect);
                    });
                }
            }
            break;
        case Resize:
        case Canvas:
            if (image) {
                const auto original = image;
                const auto previous_player = player;
                const bool was_slideshow = slideshow;
                player.playing = false;
                reset_animation_clock();
                KillTimer(hwnd,2);
                struct Restore {
                    App &app; std::shared_ptr<Image> image; Player player; bool slideshow;
                    ~Restore() {
                        if(app.image==image){app.player=std::move(player);app.reset_animation_clock();}
                        if(slideshow && app.slideshow)SetTimer(app.hwnd,2,app.settings.interval,nullptr);
                        try{app.update();}catch(...) {}
                    }
                } restore{*this,original,previous_player,was_slideshow};
                const auto metadata=current_metadata();
                if(id==Canvas) {
                    const auto choice=dialogs.canvas(hwnd,image->width,image->height,
                        {image->width,image->height,settings.canvas_keep_aspect,settings.canvas_anchor,
                         settings.canvas_transparent,settings.canvas_color,settings.canvas_alpha,metadata.dpi_x,metadata.dpi_y,settings.canvas_unit},
                        [&](uint32_t w,uint32_t h){edit_memory().pixels(w,h);});
                    if(choice&&image==original) {
                        player=previous_player;
                        const Pixel background=choice->transparent?Pixel{}:Pixel{uint8_t(choice->color>>16),uint8_t(choice->color>>8),uint8_t(choice->color),uint8_t(choice->alpha)};
                        edit([&](const Image &source){return canvas_image(source,choice->width,choice->height,CanvasOptions{choice->anchor,background},edit_memory());});
                        if(image!=original){settings.canvas_keep_aspect=choice->keep_aspect;settings.canvas_anchor=choice->anchor;
                            settings.canvas_transparent=choice->transparent;settings.canvas_color=choice->color;settings.canvas_alpha=choice->alpha;settings.canvas_unit=choice->unit;settings.save(config);}
                    }
                } else {
                const auto choice = dialogs.resize(hwnd,image->width,image->height,
                    {image->width,image->height,settings.resize_keep_aspect,settings.resize_smooth,
                     settings.resize_down,settings.resize_up,settings.resize_sharpen,metadata.dpi_x,metadata.dpi_y,metadata.dpi_defined,
                     settings.resize_resample,settings.resize_print_unit,settings.resize_density_unit},
                    [&](uint32_t w,uint32_t h){edit_memory().pixels(w,h);});
                if(choice && image==original) {
                    // Undo must restore playback as it was before the modal dialog.
                    player=previous_player;
                    const ResizeFilters filters{choice->smooth?choice->down:0,choice->smooth?choice->up:0,
                                                choice->smooth&&choice->sharpen};
                    if(!choice->resample)change_resolution(choice->dpi_x,choice->dpi_y);
                    else edit([&](const Image &source){auto out=resize_image(source,choice->width,choice->height,filters,edit_memory());
                        out.metadata.dpi_x=choice->dpi_x;out.metadata.dpi_y=choice->dpi_y;out.metadata.dpi_defined=choice->dpi_defined;return out;});
                    if(!choice->resample||image!=original) {
                        settings.resize_keep_aspect=choice->keep_aspect;settings.resize_smooth=choice->smooth;
                        settings.resize_down=choice->down;settings.resize_up=choice->up;
                        settings.resize_sharpen=choice->sharpen;
                        settings.resize_resample=choice->resample;settings.resize_print_unit=choice->print_unit;settings.resize_density_unit=choice->density_unit;
                        settings.save(config);
                    }
                }
                }
            }
            break;
        case NewImage:
            if (discard())
                if (auto size = dimensions(L"新規の透明画像", 1024, 1024)) {
                    replace(new_image(size->first, size->second, edit_memory()), true, true);
                    stop_slideshow();
                    drag_armed = false;
                    requested.clear();
                    folder.clear();
                    current_name = L"新規画像";
                    archive.clear();
                    files.clear();
                }
            break;
        case Negative:
        case Gray:
        case Sepia:
        case Threshold:
        case Contrast:
        case Gamma:
        case Saturation:
        case Hue:
        case Edge:
        case Emboss:
        case Mosaic:
        case Noise:
        case Posterize: {
            if (!image)
                break;
            Effect type{};
            double amount = 1;
            const wchar_t *label = nullptr;
            double low = -255, high = 255;
            switch (id) {
            case Negative:
                type = Effect::Negative;
                break;
            case Gray:
                type = Effect::Grayscale;
                break;
            case Sepia:
                type = Effect::Sepia;
                break;
            case Threshold:
                type = Effect::Threshold;
                amount = 128;
                label = L"しきい値（0～255）";
                low = 0;
                break;
            case Contrast:
                type = Effect::Contrast;
                label = L"コントラスト（0～5、通常1）";
                low = 0;
                high = 5;
                break;
            case Gamma:
                type = Effect::Gamma;
                label = L"ガンマ（0.01～10、通常1）";
                low = .01;
                high = 10;
                break;
            case Saturation:
                type = Effect::Saturation;
                label = L"彩度（0～5、通常1）";
                low = 0;
                high = 5;
                break;
            case Hue:
                type = Effect::Hue;
                amount = 0;
                label = L"色相の回転（-180～180度）";
                low = -180;
                high = 180;
                break;
            case Edge:
                type = Effect::Edge;
                break;
            case Emboss:
                type = Effect::Emboss;
                break;
            case Mosaic:
                type = Effect::Mosaic;
                amount = 8;
                label = L"ブロックサイズ（2～256）";
                low = 2;
                high = 256;
                break;
            case Noise:
                type = Effect::Noise;
                amount = .2;
                label = L"強さ（0～2）";
                low = 0;
                high = 2;
                break;
            case Posterize:
                type = Effect::Posterize;
                amount = 8;
                label = L"各色の階調数（2～256）";
                low = 2;
                high = 256;
                break;
            }
            if (label) {
                auto value = number(L"画像加工", label, amount, low, high);
                if (!value)
                    break;
                amount = *value;
            }
            const auto rect = selection;
            edit([&](const Image &source) { return effect(source, type, amount, rect); });
            break;
        }
        case Save:
            save(false);
            break;
        case SaveAs:
            save(true);
            break;
        case RenameFile:
            cancel_load();
            stop_slideshow();
            if (original_file() && discard()) {
                auto name =
                    dialogs.text(hwnd, L"名前変更", L"新しいファイル名", image->path.filename().wstring());
                if (name) {
                    const std::filesystem::path leaf(*name);
                    if (leaf.empty() || leaf.has_parent_path() || *name == L"." || *name == L".." ||
                        name->find_first_of(L"<>:\"/\\|?*") != std::wstring::npos || name->back() == L'.' ||
                        name->back() == L' ' ||
                        std::any_of(name->begin(), name->end(), [](wchar_t c) { return c < 32; }))
                        throw std::runtime_error("Enter a filename only");
                    const auto source = image->path, target = source.parent_path() / leaf;
                    if (std::filesystem::exists(target) && !same_path(source, target))
                        throw std::runtime_error("File already exists");
                    std::filesystem::rename(source, target);
                    for (auto &entry : files)
                        if (same_path(entry.path, source)) {
                            entry.path = target;
                            entry.display_name = target.filename().wstring();
                        }
                    bind_file(target);
                    if (settings.rename_resort && !removed_current)
                        sort_list();
                }
            }
            break;
        case FileCopy:
            transfer_file(false);
            break;
        case FileMove:
            transfer_file(true);
            break;
        case FileDelete:
        case FileDeleteDirect: {
            cancel_load();
            stop_slideshow();
            if (!original_file() || !discard())
                break;
            const bool recycle = id == FileDelete && settings.recycle_deleted;
            if (!settings.confirm_delete ||
                dialogs.ask(hwnd, recycle ? L"このファイルをごみ箱に移しますか？"
                                          : L"このファイルを完全に削除しますか？",
                            current_name.c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES) {
                const auto path = image->path;
                dialogs.delete_file(hwnd, path, recycle);
                bind_file({});
                requested = path;
                current_name = path.filename().wstring();
                clear_undo();
                undo_location.reset();
                remove_list_entry(path);
                advance_after_fileop();
            }
            break;
        }
        case CopyFileName:
        case CopyFolderName:
        case CopyFolderPath:
        case CopyShortPath:
        case CopyPath:
            if (image) {
                const auto text = file_clipboard_text(id);
                struct Memory {
                    HGLOBAL value{};
                    ~Memory() {
                        if (value)
                            GlobalFree(value);
                    }
                } memory;
                if (text) {
                    memory.value = GlobalAlloc(GMEM_MOVEABLE, (text->size() + 1) * sizeof(wchar_t));
                    if (!memory.value)
                        throw std::bad_alloc();
                    const auto ptr = GlobalLock(memory.value);
                    if (!ptr)
                        throw std::bad_alloc();
                    memcpy(ptr, text->c_str(), (text->size() + 1) * sizeof(wchar_t));
                    GlobalUnlock(memory.value);
                }
                if (!OpenClipboard(hwnd))
                    throw std::runtime_error("Clipboard is busy");
                struct Close {
                    ~Close() { CloseClipboard(); }
                } close;
                if (!EmptyClipboard())
                    throw std::runtime_error("Cannot empty the clipboard");
                if (memory.value && !SetClipboardData(CF_UNICODETEXT, memory.value))
                    throw std::runtime_error("Cannot copy the file name to the clipboard");
                memory.value = nullptr;
            }
            break;
        case ConfigureDestinations:
            preferences(PreferencePage::FileOperations, 0);
            break;
        case ConfigureExternal:
            preferences(PreferencePage::External);
            break;
        case External:
            if (const auto path = dialogs.executable(hwnd); !path.empty())
                external(migrate_external_application(0, path.wstring()));
            break;
        case NewWindow:
        case NewWindowFile:
        case NewWindowSend:
        case NewWindowPaste: {
            NewWindowContent content;
            if (id == NewWindowFile) {
                if (!image)
                    break;
                const auto source = !image->path.empty() ? image->path
                                  : before_load ? before_load->requested : requested;
                if (source.empty())
                    break;
                content = source;
            } else if (id == NewWindowSend) {
                if (!image)
                    break;
                auto frame = copied_image();
                copy_current_selection(frame);
                content = std::move(frame);
            } else if (id == NewWindowPaste) {
                auto pasted = paste_clipboard(hwnd);
                if (auto path = std::get_if<std::filesystem::path>(&pasted))
                    content = std::move(*path);
                else
                    content = std::move(std::get<Image>(pasted));
            }
            dialogs.new_viewer(viewer_executable(), config, content);
            break;
        }
        case Print:
            if (image)
                {const auto snapshot=image;const auto metadata=current_metadata();dialogs.print(hwnd,*snapshot,player.frame,&metadata);}
            break;
        case Preferences:
            preferences();
            break;
        case History: {
            if (settings.history.empty()) {
                dialogs.ask(hwnd, L"最近開いたフォルダはありません。", L"最近開いたフォルダ", MB_OK);
                break;
            }
            auto recent = CreatePopupMenu();
            for (size_t i = 0; i < settings.history.size(); ++i)
                add(recent, HistoryBase + static_cast<UINT>(i), settings.history[i]);
            POINT point{};
            GetCursorPos(&point);
            const auto chosen =
                TrackPopupMenu(recent, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
            DestroyMenu(recent);
            if (chosen)
                command(chosen);
            break;
        }
        case Info:
            show_info();
            break;
        case Association:
            ShellExecuteW(hwnd, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case About: {
            const auto text = std::wstring(L"GVN Image Viewer\nバージョン ") + version_string + L" (" +
                              build_architecture + L")\n\n使用しているライブラリのライセンスは、\n"
                                                   L"GVN.exeと同じ場所のlicensesフォルダーにあります。";
            dialogs.ask(hwnd, text.c_str(), L"GVN バージョン情報", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case Help: {
            std::wstring text = L"設定 → キーで割り当てを変更できます。現在の主な割り当て:\n\n";
            for (const UINT help_command : {Open, Previous, Next, Copy, Paste, Save, Undo, Redo, ArmDrag,
                                            Fullscreen, Play, FramePrevious, FrameNext}) {
                text += command_name(help_command);
                text += L": ";
                const auto keys = settings.keys_for(help_command);
                if (keys.empty())
                    text += L"割り当てなし";
                for (size_t i = 0; i < keys.size(); ++i)
                    text += (i ? L" / " : L"") + keys[i];
                text += L"\n";
            }
            text += L"\n左ドラッグ: 移動 / Shift+左ドラッグ: 範囲選択\nAlt+左ドラッグ: "
                    L"ファイルを渡す\n右クリック: 全メニュー\nEsc: "
                    L"選択解除・スライド停止・全画面解除\n\nPSDは保存済み合成画像を表示します。\nGIF/"
                    L"APNGの加工・コピー・ドラッグは現在フレームです。";
            dialogs.ask(hwnd, text.c_str(), L"GVN 操作説明", MB_OK);
            break;
        }
        }
        if (!((id >= ScrollUp && id <= ReadScrollRight) || id == ZoomIn || id == ZoomOut))
            refresh_menu();
        update();
    }
    void refresh_overlay() {
        const bool visible = image &&
                             (full ? settings.overlay_full
                                   : settings.overlay_window && !settings.caption && !selection_active()) &&
                             (!image->animated() || settings.overlay_animation) &&
                             active_drag_action != MousePan && active_drag_action != MouseZoom &&
                             !canvas_tracking && GetTickCount64() >= overlay_scroll_until;
        if (visible != renderer.overlay_visible) {
            renderer.overlay_visible = visible;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    void mark_overlay_scroll() {
        overlay_scroll_until = GetTickCount64() + 150;
        SetTimer(hwnd, 6, 150, nullptr);
        refresh_overlay();
    }
    void finish_overlay_scroll() {
        canvas_tracking = false;
        overlay_scroll_until = 0;
        KillTimer(hwnd, 6);
        refresh_overlay();
    }
    void scroll(double dx, double dy) {
        if (!image)
            return;
        const auto [xmin, xmax] = view.x_range();
        const auto [ymin, ymax] = view.y_range();
        const auto next_x = std::clamp(view.pan_x + dx, xmin, xmax),
                   next_y = std::clamp(view.pan_y + dy, ymin, ymax);
        if (next_x == view.pan_x && next_y == view.pan_y)
            return;
        view.pan_x = next_x;
        view.pan_y = next_y;
        view.manual = true;
        mark_overlay_scroll();
        sync_canvas_scrollbars();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void position_scroll(UINT action) {
        if (!image)
            return;
        const auto [xmin, xmax] = view.x_range();
        const auto [ymin, ymax] = view.y_range();
        if (action == ScrollLeftEdge || action == ScrollTopLeft || action == ScrollBottomLeft)
            view.pan_x = xmax;
        if (action == ScrollRightEdge || action == ScrollTopRight || action == ScrollBottomRight)
            view.pan_x = xmin;
        if (action == ScrollTop || action == ScrollTopLeft || action == ScrollTopRight)
            view.pan_y = ymax;
        if (action == ScrollBottom || action == ScrollBottomLeft || action == ScrollBottomRight)
            view.pan_y = ymin;
        if (action == ScrollCenter)
            view.pan_x = (xmin + xmax) / 2, view.pan_y = (ymin + ymax) / 2;
        view.manual = true;
    }
    void popup_menu(UINT action, POINT point) {
        auto root = menu(true);
        HMENU selected = root;
        if (action >= PopupFile && action <= PopupOptions)
            selected = GetSubMenu(root, action - PopupFile);
        else if ((action >= PopupSort && action <= PopupExternal) || action == ViewRotate || action == Wallpaper) {
            const UINT marker = action == PopupSort           ? SortName
                                : action == ViewRotate        ? ViewRotateNone
                                : action == Wallpaper         ? WallpaperCenter
                                : action == PopupRGB          ? SwapRG
                                : action == PopupMask         ? GlassV
                                : action == PopupDestinations ? CopyDestBase
                                                              : External;
            const auto find = [&](auto &&self, HMENU current) -> HMENU {
                for (int i = 0; i < GetMenuItemCount(current); ++i) {
                    if (GetMenuItemID(current, i) == marker)
                        return current;
                    if (auto child = GetSubMenu(current, i))
                        if (auto found = self(self, child))
                            return found;
                }
                return nullptr;
            };
            selected = find(find, root);
        }
        const UINT chosen = selected ? dialogs.popup(hwnd, selected, point) : 0;
        DestroyMenu(root);
        if (chosen)
            command(chosen);
    }
    static unsigned pointer_modifiers(WPARAM flags = 0) {
        return (((flags & MK_CONTROL) || (GetKeyState(VK_CONTROL) & 0x8000)) ? 1u : 0u) |
               (((flags & MK_SHIFT) || (GetKeyState(VK_SHIFT) & 0x8000)) ? 2u : 0u) |
               ((GetKeyState(VK_MENU) & 0x8000) ? 4u : 0u);
    }
    static unsigned pointer_flags(WPARAM flags) {
        return ((flags & MK_LBUTTON) ? mouse_left : 0) | ((flags & MK_RBUTTON) ? mouse_right : 0) |
               ((flags & MK_MBUTTON) ? mouse_middle : 0) | ((flags & MK_XBUTTON1) ? mouse_x1 : 0) |
               ((flags & MK_XBUTTON2) ? mouse_x2 : 0);
    }
    HCURSOR pointer_cursor(POINT point, bool button_down) const {
        if(space_pan||(editor_mode&&selection_tool==SelectionTool::Pan))return LoadCursorW(nullptr,IDC_HAND);
        if(floating&&point.y>=top&&point.y<top+view.height){
            const auto hit=floating_hit(floating->box,floating_point(point),7./view.scale);
            return LoadCursorW(nullptr,hit==FloatMove?IDC_SIZEALL:hit==FloatRotate?IDC_CROSS:
                hit==1||hit==5?IDC_SIZENS:hit==3||hit==7?IDC_SIZEWE:hit==0||hit==4?IDC_SIZENWSE:IDC_SIZENESW);
        }
        if (full && settings.full_hide_cursor && point.y >= top && point.y < top + view.height &&
            !button_down && (!loading || settings.full_hide_wait))
            return nullptr;
        return LoadCursorW(nullptr, loading          ? IDC_WAIT
                                    : drag_armed     ? IDC_HAND
                                    : selection_active() ? IDC_CROSS
                                                     : IDC_ARROW);
    }
    void begin_file_drag() {
        if (!image)
            return;
        auto source = image;
        const auto metadata=current_metadata();
        const auto path = drag_path(*source, player.frame, modified || source->format == L"見開き",&metadata);
        const bool was_playing = player.playing;
        drag_armed = false;
        cancel_pointer();
        player.playing = false;
        update();
        try {
            dialogs.file_drag(path);
        } catch (...) {
            if (image == source)
                player.playing = was_playing;
            update();
            throw;
        }
        if (image == source)
            player.playing = was_playing;
        update();
    }
    void cancel_pointer() {
        crop_drag.reset();
        if(pen_player){player=std::move(*pen_player);pen_player.reset();renderer.pen_points.clear();}
        floating_drag.reset();
        cancel_selection_path();
        editor_chrome.cancel_drag();
        for (auto &press : mouse_presses)
            press = {};
        mouse_buttons = active_drag_button = active_drag_action = 0;
        panning = selecting = drag_pending = false;
        hover_seen = false;
        wheel_vertical = wheel_horizontal = 0;
        wheel_context = {UINT_MAX, UINT_MAX};
        renderer.dragging_low = false;
        finish_overlay_scroll();
        InvalidateRect(hwnd, nullptr, FALSE);
        if (GetCapture() == hwnd)
            ReleaseCapture();
    }
    void consume_pointer(unsigned buttons) {
        for (unsigned i = 0; i < 5; ++i)
            if (buttons & (1u << i))
                mouse_presses[i].consumed = true;
    }
    void step_zoom(bool enlarge, double x, double y) {
        const auto target = stepped_zoom(view.scale, enlarge, settings.zoom_mode, settings.zoom_step,
                                         settings.zoom_below, settings.zoom_above);
        view.zoom_to(target, x, y);
    }
    void pointer_action(unsigned action, POINT point) {
        if (action == MouseNone)
            return;
        if (action == MouseZoomIn || action == MouseZoomOut) {
            step_zoom(action == MouseZoomIn, point.x, point.y - top);
            update();
        } else if ((action >= PopupAll && action <= PopupExternal) || action == ViewRotate)
            popup_menu(action, [&] {
                auto p = client_point(point);
                ClientToScreen(hwnd, &p);
                return p;
            }());
        else
            command(action);
    }
    unsigned drag_action(unsigned button, const MousePress &press) const {
        auto action =
            settings.mouse.action(static_cast<MouseGesture>(10 + button), press.held, press.modifiers);
        if(button==0&&space_pan)return MousePan;
        if (button == 0 && image && drag_armed)
            return MouseFile;
        if (button == 0 && image && selection_active())
            return MouseSelect;
        if(button==0&&editor_mode&&selection_tool==SelectionTool::Pan)return MousePan;
        // A captionless window retains the reference viewer's Ctrl+left fallback.
        // An explicit Ctrl assignment or a customized base drag keeps its meaning.
        if (button == 0 && !full && !settings.caption && press.held == 0 && press.modifiers == 1 &&
            action == MousePan && settings.mouse.exact(MouseGesture::LeftDrag, 0, 1) == MouseInherit &&
            settings.mouse.exact(MouseGesture::LeftDrag, 0, 0) == MousePan)
            return MouseWindow;
        return action;
    }
    void pointer_down(unsigned button, POINT point, WPARAM flags, bool double_click = false) {
        if (point.y < top || point.y >= top + view.height||point.x<0||point.x>=view.width)
            return;
        if(!space_pan&&image&&button==0&&editor_mode){
            if(cropping){SetFocus(hwnd);const auto p=floating_point(point);crop_drag=FloatingDrag{cropping->box,p,floating_hit(cropping->box,p,8./view.scale)};SetCapture(hwnd);return;}
            if(selection_tool==SelectionTool::Pen&&!floating){start_pen(point);return;}
            if(selection_tool==SelectionTool::Text&&!floating){start_text(point);return;}
        }
        if(floating&&!space_pan){
            if(button==0&&point.x>=0&&point.x<view.width){SetFocus(hwnd);const auto p=floating_point(point);
                floating_drag_button=button;floating_drag=FloatingDrag{floating->box,p,floating_hit(floating->box,p,7./view.scale)};SetCapture(hwnd);}
            return;
        }
        SetFocus(hwnd);
        hover_seen = false;
        auto &press = mouse_presses.at(button);
        press = {true,
                 false,
                 false,
                 point,
                 (mouse_buttons | pointer_flags(flags)) & ~(1u << button),
                 pointer_modifiers(flags)};
        mouse_buttons |= 1u << button;
        press.empty_paste = button == 0 && !image && !loading && !double_click && !press.held &&
                            !press.modifiers && point.x >= 0 && point.x < view.width;
        const auto action = drag_action(button, press);
        if(image&&action==MouseSelect){
            const auto pixel=image_point(point);
            if(button==0&&!press.held&&!press.modifiers&&!double_click&&!polygon_drawing&&selection.contains(pixel.x,pixel.y)){
                press.selection_move_pending=true;SetCapture(hwnd);return;
            }
            start_selection(button,point,flags,double_click);return;
        }
        if (double_click) {
            press.consumed = true;
            const auto click_action =
                settings.mouse.action(static_cast<MouseGesture>(5 + button), press.held, press.modifiers);
            if (click_action)
                consume_pointer(press.held | (1u << button));
            pointer_action(click_action, point);
            return;
        }
        drag_pending = image && action == MouseFile;
        if (action == MouseWindow) {
            cancel_pointer();
            point=client_point(point);ClientToScreen(hwnd, &point);
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
            return;
        }
        if (GetCapture() != hwnd)
            SetCapture(hwnd);
    }
    void pointer_up(unsigned button, POINT point) {
        if(button==0&&pen_player){finish_pen(point);return;}
        if(button==0&&crop_drag){cropping->box=crop_drag->moved(floating_point(point),(GetKeyState(VK_SHIFT)&0x8000)!=0);crop_drag.reset();if(GetCapture()==hwnd)ReleaseCapture();update();return;}
        if(floating&&!space_pan){
            if(button==floating_drag_button&&floating_drag){floating->box=floating_drag->moved(floating_point(point),(GetKeyState(VK_SHIFT)&0x8000)!=0);
                floating_drag.reset();if(GetCapture()==hwnd)ReleaseCapture();update();}
            return;
        }
        if(polygon_drawing&&mouse_presses[button].consumed){mouse_presses[button]={};mouse_buttons&=~(1u<<button);return;}
        if(selecting&&active_drag_button==button+1&&selection_drag_tool==SelectionTool::Freehand){
            const auto p=image_point(point);selection_points.push_back({double(p.x),double(p.y)});finish_selection_path();return;
        }
        const bool was_view_drag = active_drag_action == MouseZoom || active_drag_action == MousePan;
        auto press = mouse_presses.at(button);
        if (active_drag_button == button + 1) {
            if (selecting) {
                select_to(point);
                selection=selection_before_drag.combined(selection,selection_combine,image->width,image->height);
                press.moved = press.moved || point.x != press.origin.x || point.y != press.origin.y;
            }
            press.consumed = press.consumed || active_drag_action != MouseSelect || press.moved;
            if (press.consumed)
                consume_pointer(press.held);
            active_drag_button = active_drag_action = 0;
            panning = selecting = drag_pending = false;
        }
        mouse_presses[button] = {};
        mouse_buttons &= ~(1u << button);
        if (!mouse_buttons && GetCapture() == hwnd)
            ReleaseCapture();
        if (press.down && !press.consumed && !press.moved && !press.selection_move_pending && !space_pan) {
            if (press.empty_paste) {
                const auto threshold = std::max(1, MulDiv(settings.mouse.drag_threshold, GetDpiForWindow(hwnd), 96));
                if (!image && !loading && point.x >= 0 && point.x < view.width &&
                    point.y >= top && point.y < top + view.height &&
                    std::abs(point.x - press.origin.x) <= threshold &&
                    std::abs(point.y - press.origin.y) <= threshold)
                    command(Paste);
                return;
            }
            const auto action =
                settings.mouse.action(static_cast<MouseGesture>(button), press.held, press.modifiers);
            if (action)
                consume_pointer(press.held);
            pointer_action(action, point);
        }
        if (was_view_drag) {
            finish_overlay_scroll();
            update();
        }
    }
    void hover_scroll(POINT point, WPARAM flags) {
        const auto action = settings.mouse.action(MouseGesture::Hover, mouse_buttons | pointer_flags(flags),
                                                  pointer_modifiers(flags));
        if (action != MouseHover || !image || settings.mouse.hover_margin == 50) {
            hover_seen = false;
            return;
        }
        const double margin = settings.mouse.hover_margin / 100.0;
        const auto position = [&](double coordinate, double viewport, std::pair<double, double> range) {
            const auto fraction =
                std::clamp((coordinate - viewport * margin) / (viewport * (1 - 2 * margin)), 0.0, 1.0);
            return range.second + (range.first - range.second) * fraction;
        };
        if (!hover_seen) {
            hover_seen = true;
            hover_last = point;
            scroll(position(point.x, view.width, view.x_range()) - view.pan_x,
                   position(point.y - top, view.height, view.y_range()) - view.pan_y);
            return;
        }
        const auto movement = [&](double before, double now, double viewport, double content) {
            const auto extent = std::max(0.0, content - viewport);
            const auto effective = viewport * (1 - 2 * margin);
            const auto delta = std::clamp(now, viewport * margin, viewport * (1 - margin)) -
                               std::clamp(before, viewport * margin, viewport * (1 - margin));
            if (!delta || !extent)
                return 0.0;
            auto amount = -delta * extent / effective;
            if (std::abs(amount) < settings.mouse.hover_min)
                amount = std::copysign(static_cast<double>(settings.mouse.hover_min), amount);
            return amount;
        };
        const auto x = movement(hover_last.x, point.x, view.width, view.canvas_w * view.scale);
        const auto y = movement(hover_last.y - top, point.y - top, view.height, view.canvas_h * view.scale);
        hover_last = point;
        scroll(x, y);
    }
    void pointer_move(POINT point, WPARAM flags) {
        if(pen_player){if(flags&MK_LBUTTON){const auto p=floating_point(point);auto &points=renderer.pen_points;
            if(points.size()<65536&&std::hypot(p.x-points.back().x,p.y-points.back().y)>=.25){points.push_back(p);InvalidateRect(hwnd,nullptr,FALSE);}}return;}
        if(crop_drag&&cropping){if(flags&MK_LBUTTON){cropping->box=crop_drag->moved(floating_point(point),(flags&MK_SHIFT)!=0);update();}return;}
        auto &pending=mouse_presses[0];
        if(pending.down&&pending.selection_move_pending){
            const auto threshold=MulDiv(settings.mouse.drag_threshold,GetDpiForWindow(hwnd),96);
            if(!(flags&MK_LBUTTON))return;
            if(std::abs(point.x-pending.origin.x)<=threshold&&std::abs(point.y-pending.origin.y)<=threshold)return;
            if(pointer_modifiers(flags)){const auto origin=pending.origin;pending.selection_move_pending=false;
                start_selection(0,origin,flags,false);pointer_move(point,flags);return;}
            const auto start=floating_point(pending.origin),now=floating_point(point);
            choose_tool(SelectionTool::Move);
            if(floating){floating_drag_button=0;floating_drag=FloatingDrag{floating->box,start,FloatMove};
                floating->box=floating_drag->moved(now,false);SetCapture(hwnd);update();}
            return;
        }
        if(floating&&!space_pan){
            if(floating_drag&&(pointer_flags(flags)&(1u<<floating_drag_button))){floating->box=floating_drag->moved(floating_point(point),(flags&MK_SHIFT)!=0);update();}
            return;
        }
        if(polygon_drawing){const auto p=image_point(point);renderer.selection_path=selection_points;
            renderer.selection_path.push_back({double(p.x),double(p.y)});update();return;}
        if(selecting&&selection_drag_tool==SelectionTool::Freehand){
            const auto p=image_point(point);const FloatPoint next{double(p.x),double(p.y)};
            if(selection_points.empty()||std::hypot(next.x-selection_points.back().x,next.y-selection_points.back().y)>=.5){
                if(selection_points.size()>=65536)throw std::runtime_error("Too many selection points");selection_points.push_back(next);}
            renderer.selection_path=selection_points;if(!selection_points.empty())renderer.selection_path.push_back(selection_points.front());update();return;}
        if (!active_drag_button) {
            for (unsigned button = 0; button < 5; ++button) {
                auto &press = mouse_presses[button];
                if (!press.down || press.consumed)
                    continue;
                const auto threshold = MulDiv(settings.mouse.drag_threshold, GetDpiForWindow(hwnd), 96);
                if (std::abs(point.x - press.origin.x) <= threshold &&
                    std::abs(point.y - press.origin.y) <= threshold)
                    continue;
                press.moved = true;
                const auto action = drag_action(button, press);
                if (!image || action == MouseNone)
                    continue;
                active_drag_button = button + 1;
                active_drag_action = action;
                last = zoom_anchor = press.origin;
                consume_pointer(press.held | (1u << button));
                break;
            }
        }
        if (!active_drag_button) {
            hover_scroll(point, flags);
            return;
        }
        refresh_overlay();
        if (active_drag_action == MouseFile) {
            begin_file_drag();
        } else if (active_drag_action == MouseSelect) {
            auto &press = mouse_presses[active_drag_button - 1];
            if (point.x != press.origin.x || point.y != press.origin.y) {
                press.moved = true;
                consume_pointer(press.held | (1u << (active_drag_button - 1)));
            }
            select_to(point);
        } else if (active_drag_action == MousePan) {
            panning = true;
            if(space_pan||(editor_mode&&selection_tool==SelectionTool::Pan))view.free_pan=true;
            scroll((point.x - last.x) * settings.mouse.drag_scroll / 100.0,
                   (point.y - last.y) * settings.mouse.drag_scroll / 100.0);
            last = point;
        } else if (active_drag_action == MouseZoom) {
            const auto delta = settings.mouse.drag_axis == 0   ? last.y - point.y
                               : settings.mouse.drag_axis == 1 ? point.y - last.y
                               : settings.mouse.drag_axis == 2 ? point.x - last.x
                                                               : last.x - point.x;
            const auto factor = std::exp(std::clamp(delta * settings.mouse.drag_zoom / 10000.0, -8.0, 8.0));
            view.zoom_at(factor, zoom_anchor.x, zoom_anchor.y - top);
            last = point;
            update();
        }
    }
    void pointer_wheel(bool horizontal, int delta, WPARAM flags, POINT point) {
        const auto held = mouse_buttons | pointer_flags(flags);
        const auto mods = pointer_modifiers(flags);
        auto &accumulator = horizontal ? wheel_horizontal : wheel_vertical;
        auto &context = wheel_context[horizontal ? 1 : 0];
        const auto current = held | (mods << 5);
        if (context != current) {
            accumulator = 0;
            context = current;
        }
        accumulator += delta;
        const int threshold = settings.mouse.slow_wheel ? WHEEL_DELTA * 2 : WHEEL_DELTA;
        while (std::abs(accumulator) >= threshold) {
            const bool positive = accumulator > 0;
            accumulator += positive ? -threshold : threshold;
            const auto gesture = horizontal ? (positive ? MouseGesture::WheelRight : MouseGesture::WheelLeft)
                                            : (positive ? MouseGesture::WheelUp : MouseGesture::WheelDown);
            const auto action = settings.mouse.action(gesture, held, mods);
            if (action)
                consume_pointer(held);
            pointer_action(action, point);
        }
    }
    POINT image_point(POINT point) const {
        if (!image)
            return {};
        const auto projected=floating_point(point);
        return {std::clamp(static_cast<int>(std::floor(projected.x)), 0,
                           static_cast<int>(image->width)),
                std::clamp(static_cast<int>(std::floor(projected.y)), 0,
                           static_cast<int>(image->height))};
    }
    void select_to(POINT point) {
        auto p = image_point(point);
        selection=selection_rectangle(selection_start.x,selection_start.y,p.x,p.y,int(image->width),int(image->height),selection_drag_units);
        update();
    }
    void finish_smoke() {
        if (image) {
            try {
                if (SUCCEEDED(renderer.draw(hwnd, image.get(), player.frame, view, top, bottom, &selection)))
                    ++paints;
                else
                    smoke_error = "Rendering failed";
            } catch (const std::exception &e) {
                smoke_error = e.what();
            }
        }
        std::ofstream report(smoke_report);
        report << "{\"rendered\":" << (paints > 0 ? "true" : "false")
               << ",\"width\":" << (image ? image->width : 0) << ",\"height\":" << (image ? image->height : 0)
               << ",\"frames\":" << (image ? image->frames.size() : 0)
               << ",\"toolbar_buttons\":" << SendMessageW(toolbar.hwnd, TB_BUTTONCOUNT, 0, 0)
               << ",\"ready_ms\":" << ready_ms << ",\"idle_cpu_ms\":" << idle_cpu_used / 10000.0
               << ",\"display_ready_ms\":" << display_ready_ms << ",\"first_draw_ms\":" << first_draw_ms
               << ",\"first_draw_succeeded\":" << (first_draw_succeeded ? "true" : "false")
               << ",\"software_target\":" << (first_draw_software ? "true" : "false")
               << ",\"draw_phases_ms\":{\"factory\":" << first_draw_phases.factory_ms
               << ",\"target\":" << first_draw_phases.target_ms << ",\"brush\":" << first_draw_phases.brush_ms
               << ",\"sampling\":" << first_draw_phases.sampling_ms
               << ",\"upload\":" << first_draw_phases.upload_ms
               << ",\"background\":" << first_draw_phases.background_ms
               << ",\"scene\":" << first_draw_phases.scene_ms << "}";
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                             sizeof(memory));
        const auto cache = loader->stats();
        report << ",\"cache_bytes\":" << cache.cache_bytes << ",\"cache_limit\":" << cache.cache_limit
               << ",\"cache_idle\":" << (cache.idle ? "true" : "false")
               << ",\"cache_hits\":" << cache.cache_hits << ",\"prefetch_loads\":" << cache.prefetch_loads
               << ",\"prefetch_skips\":" << cache.prefetch_skips
               << ",\"working_set\":" << memory.WorkingSetSize << ",\"private_bytes\":" << memory.PrivateUsage
               << ",\"conversion_cache_bytes\":" << renderer.source_bytes()
               << ",\"display_tile_bytes\":" << renderer.sample_bytes()
               << ",\"display_workspace_bytes\":" << renderer.sample_workspace_bytes()
               << ",\"error\":" << (smoke_error.empty() ? "false" : "true") << "}\n";
        report.close();
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }

  public:
    HWND hwnd{};
    bool smoke{};
    bool profile{};
    std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    std::filesystem::path smoke_report;
    std::string smoke_error;
    void profile_empty() {
        ready_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
                .count();
        profile_initial_draw();
        SetTimer(hwnd, 9, 2000, nullptr);
    }
    void report_exception(const std::wstring &message) {
        smoke_error = "Unhandled application exception";
        dialogs.ask(hwnd, message.c_str(), L"GVN", MB_OK | MB_ICONERROR);
    }
    void set_config(std::filesystem::path path) {
        config = std::move(path);
        settings_ready = false;
    }
    std::pair<InstanceIdentity, bool> instance_options() {
        prepare_settings();
        return {instance_identity(config), settings.allow_multiple};
    }
    bool register_instance(const InstanceIdentity &identity) {
        if (!publish_instance(hwnd, identity))
            return false;
        published_instance = identity;
        return true;
    }
    void open_external(const std::filesystem::path &path, bool activate) {
        if (activate)
            activate_viewer(hwnd);
        const auto sequence = loader->sequence();
        open(path);
        if (before_load && loader->sequence() != sequence)
            before_load->no_activate = !activate;
    }
    void process_forwarded_file() {
        if (!forwarded_file)
            return;
        if (settings.activate_reused && !forwarded_file->activated) {
            activate_viewer(hwnd);
            forwarded_file->activated = true;
        }
        if (!IsWindowEnabled(hwnd)) {
            SetTimer(hwnd, kForwardRetryTimer, 50, nullptr);
            return;
        }
        KillTimer(hwnd, kForwardRetryTimer);
        const auto request = std::exchange(forwarded_file, std::nullopt);
        if (!request->path.empty())
            open_external(request->path, false);
        if (before_load && settings.activate_reused)
            before_load->no_activate = false;
    }
    void show_initial(int requested_show) {
        if (smoke) {
            ShowWindow(hwnd, SW_HIDE);
            return;
        }
        if (full)
            fullscreen();
        if (settings.startup_position == 1 && settings.last_window_valid) {
            WINDOWPLACEMENT saved{sizeof(saved)};
            GetWindowPlacement(hwnd, &saved);
            saved.showCmd = SW_SHOWNORMAL;
            saved.flags = 0;
            saved.rcNormalPosition = {settings.last_x, settings.last_y, settings.last_x + settings.last_width,
                                      settings.last_y + settings.last_height};
            // Workspace coordinates are restored only with SetWindowPlacement.
            SetWindowPlacement(hwnd, &saved);
        } else if (settings.startup_position == 1) {
            // No saved position yet: use the reference's initial rectangle before first show.
            const auto rect = Settings::initial_window_rect;
            position_window({rect.x, rect.y, rect.x + rect.w, rect.y + rect.h}, false);
        } else if (settings.startup_position == 2) {
            position_custom_window();
        }
        if (settings.window_center && settings.startup_position != 2) {
            RECT current{};
            GetWindowRect(hwnd, &current);
            position_window(current, true);
        }
        int state = settings.startup_state;
        if (state == 0 && settings.last_window_valid) {
            if (settings.last_state == 2 && settings.restore_fullscreen)
                state = 3;
            else if (settings.last_state == 1 && settings.restore_maximized)
                state = 2;
        }
        const int show = state == 1   ? SW_SHOWMINIMIZED
                         : state == 2 ? SW_SHOWMAXIMIZED
                         : state == 3 ? SW_SHOWNORMAL
                                      : requested_show;
        ShowWindow(hwnd, show);
        if (state == 3)
            fullscreen();
        layout();
        update();
    }
    bool route_key(const MSG &message) {
        if(editor_chrome.owns(message.hwnd)){
            if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE){command(CancelSelection);SetFocus(hwnd);return true;}
            if(message.message==WM_KEYDOWN&&message.wParam==VK_TAB){const auto host=IsChild(editor_chrome.top,message.hwnd)?editor_chrome.top:editor_chrome.bottom;
                SetFocus(GetNextDlgTabItem(host,message.hwnd,(GetKeyState(VK_SHIFT)&0x8000)!=0));return true;}
            if(message.message==WM_KEYDOWN&&message.wParam==VK_RETURN&&GetDlgCtrlID(message.hwnd)!=editor_id::Text){
                const int id=GetDlgCtrlID(message.hwnd);
                if(id>=editor_id::X&&id<=editor_id::Scale)
                    SendMessageW(editor_chrome.top,WM_COMMAND,MAKEWPARAM(id,EN_KILLFOCUS),reinterpret_cast<LPARAM>(message.hwnd));
                SetFocus(hwnd);return true;}
            return false;
        }
        if ((message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN &&
             !((message.message==WM_KEYUP||message.message==WM_SYSKEYUP)&&message.wParam==VK_SPACE&&space_pan)) ||
            !IsChild(hwnd, message.hwnd))
            return false;
        if ((message.hwnd == canvas_horizontal || message.hwnd == canvas_vertical) &&
            (message.wParam == VK_LEFT || message.wParam == VK_RIGHT || message.wParam == VK_UP ||
             message.wParam == VK_DOWN || message.wParam == VK_PRIOR || message.wParam == VK_NEXT ||
             message.wParam == VK_HOME || message.wParam == VK_END) &&
            !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000) &&
            !(GetKeyState(VK_MENU) & 0x8000))
            return false;
        if (message.hwnd == slider && (message.wParam == VK_LEFT || message.wParam == VK_RIGHT) &&
            !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_SHIFT) & 0x8000) &&
            !(GetKeyState(VK_MENU) & 0x8000))
            return false;
        SendMessageW(hwnd, message.message, message.wParam, message.lParam);
        return true;
    }
    void open_new_window(NewWindowContent content) {
        try {
            if (auto path = std::get_if<std::filesystem::path>(&content))
                open_initial(*path);
            else if (auto pasted = std::get_if<Image>(&content))
                replace_clipboard_document(std::move(*pasted));
        } catch (const std::exception &error) {
            report_exception(widen(error.what()));
        }
    }
    void open_startup(const std::filesystem::path &explicit_path) {
        if (!explicit_path.empty())
            open_initial(explicit_path);
        else if (profile)
            profile_empty();
        else if (!smoke && settings.reopen_last && !settings.last_file.empty())
            open_initial(settings.last_file, true);
    }
    void open_initial(const std::filesystem::path &path, bool resume = false) {
        try {
            open(path, false, true, resume ? OpenOrigin::Startup : OpenOrigin::Explicit);
        } catch (const std::exception &e) {
            if (smoke) {
                smoke_error = e.what();
                finish_smoke();
            } else
                dialogs.ask(hwnd, widen(e.what()).c_str(), L"画像を開けません", MB_OK | MB_ICONERROR);
        }
    }
    LRESULT message(UINT msg, WPARAM w, LPARAM l) {
        switch (msg) {
        case WM_CREATE:
            initialize();
            return 0;
        case WM_INPUTLANGCHANGE: {
            const auto result = DefWindowProcW(hwnd, msg, w, l);
            ime.apply(hwnd, settings.disable_ime);
            return result;
        }
        case WM_SETFOCUS:
            ime.apply(hwnd, settings.disable_ime);
            break;
        case WM_PARENTNOTIFY:
            if (LOWORD(w) == WM_CREATE && settings_ready)
                ime.apply(hwnd, settings.disable_ime);
            break;
        case WM_ACTIVATE:
            if (LOWORD(w) != WA_INACTIVE)
                ime.apply(hwnd, settings.disable_ime);
            if (LOWORD(w) != WA_INACTIVE && deferred_display_policy && IsWindowEnabled(hwnd)) {
                deferred_display_policy = false;
                apply_display_policy();
            }
            break;
        case WM_ENABLE:
            if (w && forwarded_file)
                PostMessageW(hwnd, kForwarded, 0, 0);
            break;
        case WM_COPYDATA:
            if (smoke || config.empty() || !l)
                return 0;
            try {
                const auto path = read_instance_request(instance_identity(config),
                                                        *reinterpret_cast<const COPYDATASTRUCT *>(l));
                if (!path)
                    return 0;
                if (!forwarded_file || !path->empty())
                    forwarded_file = ForwardedFile{*path};
                else
                    forwarded_file->activated = false;
                if (!PostMessageW(hwnd, kForwarded, 0, 0)) {
                    forwarded_file.reset();
                    return 0;
                }
                return 1;
            } catch (...) {
                return 0;
            }
        case kForwarded:
            process_forwarded_file();
            return 0;
        case WM_SIZE:
            if (!full && w != SIZE_MINIMIZED)
                was_maximized = w == SIZE_MAXIMIZED;
            layout();
            if (status)
                update();
            return 0;
        case WM_DPICHANGED: {
            const auto *rc = reinterpret_cast<RECT *>(l);
            SetWindowPos(hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                         SWP_NOZORDER);
            layout();
            update();
            return 0;
        }
        case WM_ENTERSIZEMOVE:
            renderer.resizing = true;
            return 0;
        case WM_EXITSIZEMOVE:
            renderer.resizing = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            try {
                const auto result = render_scene();
                if (SUCCEEDED(result))
                    ++paints;
                else if (result != D2DERR_RECREATE_TARGET)
                    smoke_error = "Rendering failed";
            } catch (const std::exception &e) {
                smoke_error = e.what();
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            render_scene(reinterpret_cast<HDC>(w));
            return 0;
        case WM_GETMINMAXINFO: {
            auto info = reinterpret_cast<MINMAXINFO *>(l);
            RECT bounds{};
            GetWindowRect(hwnd, &bounds);
            const auto minimum = minimum_window_size(bounds.right - bounds.left);
            info->ptMinTrackSize = {minimum.cx, minimum.cy};
            return 0;
        }
        case WM_WINDOWPOSCHANGING: {
            auto position = reinterpret_cast<WINDOWPOS *>(l);
            if (!(position->flags & SWP_NOSIZE) && !full && !IsZoomed(hwnd) && !IsIconic(hwnd)) {
                const auto minimum = minimum_window_size(position->cx);
                position->cx = std::max(position->cx, static_cast<int>(minimum.cx));
                position->cy = std::max(position->cy, static_cast<int>(minimum.cy));
                return 0;
            }
            break;
        }
        case WM_INITMENUPOPUP:
            update_wallpaper_menu(reinterpret_cast<HMENU>(w));
            return 0;
        case WM_COMMAND: {
            UINT id = LOWORD(w);
            if(id>=editor_id::Color&&id<editor_id::Last){editor_notification(id,HIWORD(w));return 0;}
            if (reinterpret_cast<HWND>(l) == toolbar.hwnd && comic && settings.comic_rtl &&
                settings.comic_toolbar_reverse && (id == Previous || id == Next))
                id = id == Previous ? Next : Previous;
            command(id);
            SetFocus(hwnd);
            return 0;
        }
        case kLoaded:
            accept(std::unique_ptr<Loaded>(reinterpret_cast<Loaded *>(l)));
            return 0;
        case WM_NOTIFY: {
            auto *header = reinterpret_cast<NMHDR *>(l);
            if (header->hwndFrom == toolbar.hwnd && header->code == NM_CUSTOMDRAW) {
                auto *draw = reinterpret_cast<NMTBCUSTOMDRAW *>(l);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                    FillRect(draw->nmcd.hdc, &draw->nmcd.rc, GetSysColorBrush(COLOR_BTNFACE));
                    return CDRF_DODEFAULT;
                }
            }
            break;
        }
        case WM_DROPFILES: {
            auto drop = reinterpret_cast<HDROP>(w);
            if (!DragQueryFileW(drop, 0xffffffff, nullptr, 0)) {
                DragFinish(drop);
                return 0;
            }
            const auto count = DragQueryFileW(drop, 0, nullptr, 0);
            std::wstring path(count + 1, L'\0');
            DragQueryFileW(drop, 0, path.data(), count + 1);
            path.resize(count);
            DragFinish(drop);
            if (!path.empty())
                open_external(path, settings.activate_dropped);
            return 0;
        }
        case WM_TIMER:
            if(w==39){
                sync_ants_timer();
                if(ants_timer){renderer.ants_phase=(renderer.ants_phase+1)%8;
                    RECT canvas{left,top,left+LONG(view.width),top+LONG(view.height)};InvalidateRect(hwnd,&canvas,FALSE);}
            } else if (w == kForwardRetryTimer) {
                process_forwarded_file();
            } else if (w == kPreviewCleanupTimer) {
                retire_jump_preview();
            } else if (w == 6) {
                if (GetTickCount64() >= overlay_scroll_until &&
                    (!canvas_tracking ||
                     (GetCapture() != canvas_horizontal && GetCapture() != canvas_vertical)))
                    finish_overlay_scroll();
            } else if (w == 1 && image) {
                const auto now = animation_now();
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick).count();
                if (elapsed > 0)
                    last_tick += std::chrono::milliseconds(elapsed);
                else if (elapsed < 0)
                    last_tick = now;
                if (player.advance(static_cast<uint64_t>(std::max<int64_t>(0, elapsed)))) {
                    update();
                }
                if (!player.playing)
                    update();
            } else if (w == 2) {
                advance_slideshow();
            } else if (w == 9) {
                KillTimer(hwnd, 9);
                if (profile)
                    idle_cpu_used = cpu_ticks() - idle_cpu_start;
                command(Actual);
                command(ZoomIn);
                command(FitImage);
                if (image && image->animated()) {
                    player.step(1);
                    player.seek(0);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateWindow(hwnd);
                finish_smoke();
            }
            return 0;
        case WM_HSCROLL:
        case WM_VSCROLL:
            if (reinterpret_cast<HWND>(l) == canvas_horizontal ||
                reinterpret_cast<HWND>(l) == canvas_vertical) {
                canvas_scroll(reinterpret_cast<HWND>(l), LOWORD(w));
                return 0;
            }
            if (reinterpret_cast<HWND>(l) == slider) {
                player.playing = false;
                player.seek(static_cast<size_t>(SendMessageW(slider, TBM_GETPOS, 0, 0)));
                update();
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(l) == canvas_corner) {
                SetBkColor(reinterpret_cast<HDC>(w), GetSysColor(COLOR_BTNFACE));
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            }
            break;
        case WM_MOUSEACTIVATE:
            // This message itself denotes a click on an inactive window.
            if (settings.mouse.ignore_activation)
                return MA_ACTIVATEANDEAT;
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDBLCLK: {
            const bool double_click = msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDBLCLK ||
                                      msg == WM_MBUTTONDBLCLK || msg == WM_XBUTTONDBLCLK;
            const unsigned button = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK)   ? 0
                                    : (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) ? 1
                                    : (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK) ? 2
                                    : GET_XBUTTON_WPARAM(w) == XBUTTON1                  ? 3
                                                                                         : 4;
            pointer_down(button, canvas_point({GET_X_LPARAM(l), GET_Y_LPARAM(l)}), GET_KEYSTATE_WPARAM(w), double_click);
            return button >= 3 ? TRUE : 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP: {
            const unsigned button = msg == WM_LBUTTONUP                 ? 0
                                    : msg == WM_RBUTTONUP               ? 1
                                    : msg == WM_MBUTTONUP               ? 2
                                    : GET_XBUTTON_WPARAM(w) == XBUTTON1 ? 3
                                                                        : 4;
            pointer_up(button, canvas_point({GET_X_LPARAM(l), GET_Y_LPARAM(l)}));
            return button >= 3 ? TRUE : 0;
        }
        case WM_NCXBUTTONUP:
            navigate(GET_XBUTTON_WPARAM(w) == XBUTTON1 ? -page_step() : page_step());
            return TRUE;
        case WM_MOUSEMOVE:
            observe_bars({GET_X_LPARAM(l), GET_Y_LPARAM(l)}, pointer_modifiers(w));
            {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
            }
            pointer_move(canvas_point({GET_X_LPARAM(l), GET_Y_LPARAM(l)}), w);
            return 0;
        case WM_MOUSELEAVE: {
            const POINT point = cursor_client();
            RECT area{};
            GetClientRect(hwnd, &area);
            if (!PtInRect(&area, point))
                observe_bars(point, pointer_modifiers());
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if(w==VK_SPACE&&space_pan){cancel_pointer();space_pan=false;update();return 0;}
            if (bar_seen)
                observe_bars(bar_point, pointer_modifiers());
            break;
        case WM_CAPTURECHANGED:
            if (reinterpret_cast<HWND>(l) == hwnd)
                return 0;
            cancel_pointer();
            return 0;
        case WM_CANCELMODE:
            space_pan=false;
            renderer.resizing = false;
            cancel_pointer();
            return 0;
        case WM_KILLFOCUS:
            space_pan=false;
            cancel_pointer();
            break;
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT && reinterpret_cast<HWND>(w) == hwnd) {
                const POINT point = cursor_client();
                bool button_down = mouse_buttons != 0;
                for (int key : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2})
                    button_down = button_down || (GetKeyState(key) & 0x8000);
                SetCursor(pointer_cursor(canvas_point(point), button_down));
                return TRUE;
            }
            break;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(hwnd, &point);
            pointer_wheel(msg == WM_MOUSEHWHEEL, GET_WHEEL_DELTA_WPARAM(w), GET_KEYSTATE_WPARAM(w), canvas_point(point));
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (point.x == -1) {
                GetCursorPos(&point);
                popup_menu(PopupAll, point);
            } else {
                ScreenToClient(hwnd, &point);
                pointer_action(
                    settings.mouse.action(MouseGesture::RightClick, mouse_buttons, pointer_modifiers()),
                    canvas_point(point));
            }
            return 0;
        }
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN: {
            if(w==VK_SPACE&&space_pan)return 0;
            const UINT key_mods=((GetKeyState(VK_CONTROL)&0x8000)?1:0)|((GetKeyState(VK_SHIFT)&0x8000)?2:0)|((GetKeyState(VK_MENU)&0x8000)?4:0);
            if(w==VK_SPACE&&settings.command_for(VK_SPACE,key_mods)==ToolPan){
                if(!mouse_buttons&&!selecting&&!polygon_drawing){space_pan=true;update();}return 0;
            }
            if(polygon_drawing&&w==VK_RETURN){finish_selection_path();return 0;}
            if((polygon_drawing||selecting)&&w==VK_ESCAPE){cancel_selection_path();cancel_pointer();update();return 0;}
            if(cropping){if(w==VK_RETURN){commit_crop();return 0;}if(w==VK_ESCAPE){cancel_crop();return 0;}}
            if(floating){
                if(w==VK_RETURN){commit_floating();return 0;}
                if(w==VK_ESCAPE){cancel_floating();return 0;}
                const double step=(GetKeyState(VK_SHIFT)&0x8000)?10:1;
                if(w==VK_LEFT||w==VK_RIGHT||w==VK_UP||w==VK_DOWN){
                    floating->box.cx+=(w==VK_RIGHT?step:w==VK_LEFT?-step:0);floating->box.cy+=(w==VK_DOWN?step:w==VK_UP?-step:0);update();return 0;}
            }
            if (bar_seen && (w == VK_CONTROL || w == VK_SHIFT))
                observe_bars(bar_point, pointer_modifiers());
            if (w == VK_ESCAPE) {
                cancel_pointer();
                if (pending_folder || pending_open) {
                    cancel_load();
                    stop_slideshow();
                    update();
                    return 0;
                }
                drag_armed = false;
                selection = {};
                stop_slideshow();
                if (full)
                    fullscreen();
                update();
                return 0;
            }
            const UINT mods = ((GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0) |
                              ((GetKeyState(VK_SHIFT) & 0x8000) ? 2 : 0) |
                              ((GetKeyState(VK_MENU) & 0x8000) ? 4 : 0);
            if(w==VK_DELETE&&!mods&&!selection.empty()){command(DeleteSelection);return 0;}
            if(w=='I'&&mods==3){command(InvertSelection);return 0;}
            if(w=='D'&&mods==1){command(ClearSelection);return 0;}
            if (auto id = settings.command_for(static_cast<UINT>(w), mods))
                command(id);
            else if (msg == WM_SYSKEYDOWN)
                return DefWindowProcW(hwnd, msg, w, l);
            return 0;
        }
        case WM_CLOSE:
            cancel_load();
            if (!smoke && !discard())
                return 0;
            if (!smoke) {
                remember_window();
                remember_startup_file();
                settings.remember(folder);
                save_settings();
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            ime.restore();
            KillTimer(hwnd, kForwardRetryTimer);
            forwarded_file.reset();
            if (published_instance)
                unpublish_instance(hwnd, *published_instance);
            KillTimer(hwnd, kPreviewCleanupTimer);
            if (preview_service)
                preview_service->cancel();
            loader.reset();
            SetMenu(hwnd, nullptr);
            if (main_menu) {
                DestroyMenu(main_menu);
                main_menu = nullptr;
            }
            while (true) {
                MSG pending{};
                if (!PeekMessageW(&pending, hwnd, kLoaded, kLoaded, PM_REMOVE))
                    break;
                delete reinterpret_cast<Loaded *>(pending.lParam);
            }
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, w, l);
    }
};
static LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto *app = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        app = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        app->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (app)
        try {
            return app->message(msg, w, l);
        } catch (const std::exception &e) {
            if (app->smoke) {
                app->smoke_error = e.what();
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            } else
                app->report_exception(widen(e.what()));
            return 0;
        }
    return DefWindowProcW(hwnd, msg, w, l);
}
} // namespace gvn
#ifndef GVN_APP_TESTS
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole))
        return 1;
    struct OleScope {
        ~OleScope() {
            OleUninitialize();
        }
    } ole_scope;
    int count{};
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    gvn::App app;
    std::filesystem::path initial;
    std::optional<gvn::NewWindowStartup> new_window;
    if (args) {
        try {
            new_window = gvn::read_new_window_arguments(count, args);
        } catch (const std::exception &error) {
            LocalFree(args);
            MessageBoxW(nullptr, gvn::widen(error.what()).c_str(), L"GVN", MB_OK | MB_ICONERROR);
            return 1;
        }
        if (new_window) {
            app.set_config(new_window->settings);
        } else if (count >= 4 && (std::wstring(args[1]) == L"--smoke" || std::wstring(args[1]) == L"--profile")) {
            app.smoke = true;
            app.profile = std::wstring(args[1]) == L"--profile";
            initial = args[2];
            app.smoke_report = args[3];
        } else if (count >= 3 && std::wstring(args[1]) == L"--profile-empty") {
            app.smoke = app.profile = true;
            app.smoke_report = args[2];
        } else if (count >= 3 && std::wstring(args[1]) == L"--settings") {
            app.set_config(args[2]);
            if (count > 3)
                initial = args[3];
        } else if (count > 1)
            initial = args[1];
        LocalFree(args);
    }
    std::unique_ptr<gvn::InstanceStartup> startup;
    std::optional<gvn::InstanceIdentity> identity;
    try {
        if (!app.smoke) {
            const auto options = app.instance_options();
            identity = options.first;
            startup = std::make_unique<gvn::InstanceStartup>(*identity);
            const auto decision = startup->decide(options.second || new_window.has_value(), initial);
            if (decision == gvn::InstanceDecision::Forwarded)
                return 0;
            if (decision == gvn::InstanceDecision::Busy) {
                startup->ready();
                MessageBoxW(nullptr,
                            L"既に起動しているGVNが応答しません。少し待ってから、もう一度開いてください。",
                            L"GVN", MB_OK | MB_ICONERROR);
                return 2;
            }
        }
    } catch (const std::exception &error) {
        MessageBoxW(nullptr, gvn::widen(error.what()).c_str(), L"GVN", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!gvn::initialize_controls())
        return 1;
    WNDCLASSEXW c{sizeof(c)};
    c.style = CS_DBLCLKS;
    c.lpfnWndProc = gvn::procedure;
    c.hInstance = instance;
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    c.hIcon = gvn::window_icon(32);
    c.hIconSm = gvn::window_icon(16);
    c.lpszClassName = gvn::kViewerWindowClass;
    RegisterClassExW(&c);
    auto hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, c.lpszClassName, L"GVN Image Viewer",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1080,
                                760, nullptr, nullptr, instance, &app);
    if (!hwnd) {
        DestroyIcon(c.hIcon);
        DestroyIcon(c.hIconSm);
        return 1;
    }
    if (identity) {
        if (!app.register_instance(*identity)) {
            DestroyWindow(hwnd);
            DestroyIcon(c.hIcon);
            DestroyIcon(c.hIconSm);
            return 1;
        }
        startup->ready();
    }
    app.show_initial(show);
    if (!app.smoke)
        SetFocus(hwnd);
    UpdateWindow(hwnd);
    if (new_window)
        app.open_new_window(std::move(new_window->content));
    else
        app.open_startup(initial);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (app.route_key(message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DestroyIcon(c.hIcon);
    DestroyIcon(c.hIconSm);
    return app.smoke_error.empty() ? 0 : 1;
}

#endif // GVN_APP_TESTS
