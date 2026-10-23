// Command integration tests: real App, HWND, worker, codecs, files and isolated OS clipboard.
// Native dialogs and new-viewer launch are substituted boundaries; real child startup is tested separately.
#include "../src/viewer.cpp"
#include "test.h"
#include "rotation_formats_fixture.h"
#include "zoom_reference.h"
#include "mask_reference.h"
#include "fl_reference.h"
#include "cp_reference.h"
#include "test_desktop.h"
#include <deque>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include <imm.h>

namespace gvn {
struct AppAudit {
    App app;
    std::filesystem::path temporary;
    std::deque<int> answers;
    std::deque<std::optional<std::wstring>> texts;
    std::deque<std::filesystem::path> paths, directories;
    std::deque<BoundaryChoice> boundary_answers;
    std::deque<std::optional<JumpChoice>> jumps;
    std::deque<std::optional<ResizeChoice>> resizes;
    std::deque<std::optional<CanvasChoice>> canvases;
    std::deque<std::optional<SelectionUnits>> selection_units;
    std::deque<std::optional<Rect>> selection_rects;
    std::vector<bool> jump_directions;
    std::vector<JumpOptions> jump_options;
    std::vector<bool> boundary_directions;
    size_t boundary_beeps{},command_pips{};
    std::vector<std::wstring> notices;
    std::vector<std::filesystem::path> save_folders;
    std::vector<NewWindowContent> new_windows;
    std::vector<ExternalLaunch> external_launches;
    WallpaperSnapshot wallpaper_state;
    std::vector<std::string> wallpaper_events;
    size_t wallpaper_factories{};
    bool wallpaper_fail_apply{};
    inline static std::set<UINT> commands;
    inline static std::set<UINT> case_commands;
    AppAudit() {
        GUID guid{};
        CoCreateGuid(&guid);
        wchar_t unique[40]{};
        StringFromGUID2(guid, unique, 40);
        temporary = std::filesystem::temp_directory_path() / (std::wstring(L"GVN-app-audit-") + unique);
        std::filesystem::create_directory(temporary);
        app.set_config(temporary / L"settings.ini");
        app.navigation_root = temporary;
        // Exercise real encoding/file management while isolating every desktop setter.
        app.dialogs.wallpaper_system = [&, mutex = std::wstring(L"Local\\GVN.Wallpaper.AppAudit.") + unique] {
            ++wallpaper_factories;
            WallpaperPlatform platform;
            platform.mutex_name = mutex;
            platform.read = [&] { wallpaper_events.push_back("read"); return wallpaper_state; };
            platform.write = [&](const WallpaperValues &values) {
                wallpaper_events.push_back("write"); wallpaper_state.values = values;
            };
            platform.apply = [&](const std::filesystem::path &path) {
                wallpaper_events.push_back("apply");
                CHECK(path.empty() || std::filesystem::is_regular_file(path));
                wallpaper_state.image = path;
                if (std::exchange(wallpaper_fail_apply, false))
                    throw std::runtime_error("Injected wallpaper apply failure");
            };
            return platform;
        };
        app.dialogs.boundary = [&](HWND, bool forward, const auto &) {
            boundary_directions.push_back(forward);
            CHECK(!boundary_answers.empty());
            const auto answer = boundary_answers.front();
            boundary_answers.pop_front();
            return answer;
        };
        app.dialogs.boundary_beep = [&] { ++boundary_beeps; };
        app.dialogs.command_pip=[&]{++command_pips;};
        app.dialogs.new_viewer = [&](const auto &executable, const auto &config, const auto &content) {
            CHECK(executable == viewer_executable() && config == app.config);
            new_windows.push_back(decode_new_window(encode_new_window(content)));
            return ViewerProcess(PROCESS_INFORMATION{});
        };
        app.dialogs.external_launch = [&](HWND owner, const ExternalLaunch &request) {
            CHECK(owner == app.hwnd); external_launches.push_back(request); return !request.file.empty();
        };
        app.dialogs.executable = [&](HWND owner) {
            CHECK(owner == app.hwnd && !paths.empty()); const auto result = paths.front(); paths.pop_front(); return result;
        };
        app.dialogs.jump = [&](HWND, std::span<const FileEntry> entries, size_t current, bool reversed,
                               JumpOptions options, LoadingOptions loading,
                               std::shared_ptr<JumpPreview> preview) {
            CHECK(!entries.empty() && current < entries.size() && !app.loading);
            CHECK(preview && preview == app.preview_service);
            CHECK(loading == app.settings.loading_options());
            jump_directions.push_back(reversed);
            jump_options.push_back(options);
            CHECK(!jumps.empty());
            auto choice = jumps.front();
            jumps.pop_front();
            return choice;
        };
        app.dialogs.ask = [&](HWND, const wchar_t *message, const wchar_t *, UINT flags) {
            notices.push_back(message);
            if ((flags & MB_TYPEMASK) == MB_OK)
                return IDOK;
            CHECK(!answers.empty());
            const int result = answers.front();
            answers.pop_front();
            return result;
        };
        app.dialogs.text = [&](HWND, const wchar_t *, const wchar_t *, const std::wstring &) {
            CHECK(!texts.empty());
            auto result = texts.front();
            texts.pop_front();
            return result;
        };
        app.dialogs.spatial_filter=[&](HWND,std::shared_ptr<const Image>,size_t,Rect,SpatialFilter choice,const SpatialFilterDialog::Current &)->std::optional<SpatialFilter>{
            // Preserve prior numeric-input case bodies; native controls have a separate UI test.
            CHECK(!texts.empty());const auto value=texts.front();texts.pop_front();if(!value)return {};std::wistringstream in(*value);double radius{};if(!(in>>radius))throw std::invalid_argument("Invalid filter radius");choice.radius=int(std::lround(radius*10));
            if(choice.sharpen){wchar_t a{},b{};if(!(in>>a>>choice.strength>>b>>choice.threshold)||a!=L','||b!=L',')throw std::invalid_argument("Invalid unsharp parameters");}
            if(!(in>>std::ws).eof()||!valid_spatial_filter(choice))throw std::invalid_argument("Invalid filter parameters");return choice;
        };
        app.dialogs.brightness=[&](HWND,BrightnessChoice choice,const BrightnessDialog::Process &process,const BrightnessDialog::Preview &,const BrightnessDialog::Current &)->std::optional<BrightnessResult>{
            // Preserve existing numeric-input case bodies while testing native controls separately.
            CHECK(!texts.empty());const auto value=texts.front();texts.pop_front();if(!value)return {};std::wistringstream in(*value);if(!(in>>choice.adjustment.brightness)||!(in>>std::ws).eof()||!valid_brightness(choice.adjustment))throw std::invalid_argument("Invalid brightness value");return BrightnessResult{choice,process(choice.adjustment)};
        };
        app.dialogs.levels=[&](HWND,LevelsChoice choice,const LevelsHistogram &,const LevelsDialog::Process &process,const LevelsDialog::Preview &,const LevelsDialog::Current &)->std::optional<LevelsResult>{
            // Keep old numeric cases at the boundary; the real dialog has separate native proof.
            CHECK(!texts.empty());const auto value=texts.front();texts.pop_front();if(!value)return {};std::wistringstream in(*value);double black{},white{},gamma{},out_black{},out_white{};int channel{};wchar_t a{},b{},c{},d{},e{};
            if(!(in>>black>>a>>white>>b>>gamma>>c>>out_black>>d>>out_white>>e>>channel)||a!=L','||b!=L','||c!=L','||d!=L','||e!=L','||!(in>>std::ws).eof()||channel<0||channel>3)throw std::invalid_argument("Invalid levels values");
            choice.adjustment.individual=channel!=0;choice.adjustment.curves[channel]={int(black),int(std::lround(gamma*1000)),int(white),int(out_black),int(out_white)};if(!valid_levels(choice.adjustment))throw std::invalid_argument("Invalid levels curve");return LevelsResult{choice,process(choice.adjustment)};
        };
        app.dialogs.selection_rect=[&](HWND owner,uint32_t width,uint32_t height,Rect)->std::optional<Rect>{
            CHECK(owner==app.hwnd);
            if(!selection_rects.empty()){auto value=selection_rects.front();selection_rects.pop_front();return value;}
            // Keep all pre-existing numeric-range case bodies and their pixel assertions.
            CHECK(!texts.empty());auto text=texts.front();texts.pop_front();if(!text)return {};
            std::wistringstream in(*text);Rect rect;wchar_t a{},b{},c{};
            if(!(in>>rect.x>>a>>rect.y>>b>>rect.w>>c>>rect.h)||a!=L','||b!=L','||c!=L','||!(in>>std::ws).eof())throw std::runtime_error("Enter left, top, width, height");
            return intersect(rect,{0,0,int(width),int(height)});
        };
        app.dialogs.selection_units=[&](HWND owner,SelectionUnits)->std::optional<SelectionUnits>{
            CHECK(owner==app.hwnd);
            if(!selection_units.empty()){auto value=selection_units.front();selection_units.pop_front();return value;}
            // Preserve the older common-grid numeric boundary and every case assertion.
            CHECK(!texts.empty());const auto text=texts.front();texts.pop_front();if(!text)return {};
            const int n=std::stoi(*text);return SelectionUnits{n,n,n,n};
        };
        app.dialogs.resize = [&](HWND, uint32_t w, uint32_t h, ResizeChoice choice, const ResizeDialog::Validator &validate) -> std::optional<ResizeChoice> {
            CHECK(!app.player.playing);
            if (!resizes.empty()) {
                auto result = resizes.front(); resizes.pop_front();
                if (result && validate && result->resample) validate(result->width,result->height);
                return result;
            }
            // Preserve the pre-existing command cases' text boundary and every assertion.
            const auto size = app.dimensions(L"リサイズ",w,h);
            if (!size) return {};
            choice.width=size->first;choice.height=size->second;return choice;
        };
        app.dialogs.canvas = [&](HWND,uint32_t w,uint32_t h,CanvasChoice choice,const ResizeDialog::Validator &validate)->std::optional<CanvasChoice> {
            CHECK(!app.player.playing);
            if(!canvases.empty()){auto result=canvases.front();canvases.pop_front();if(result&&validate)validate(result->width,result->height);return result;}
            const auto size=app.dimensions(L"キャンバスサイズ",w,h);if(!size)return {};
            choice.width=size->first;choice.height=size->second;return choice;
        };
        app.dialogs.image_info=[&](HWND,const ImageInfoData &data,bool automatic,const ImageInfoDialog::Counter &){notices.push_back(data.text());return automatic;};
        app.dialogs.image_file = [&](HWND, bool save, const auto &initial, const auto &) {
            if (save)
                save_folders.push_back(initial);
            CHECK(!paths.empty());
            auto result = paths.front();
            paths.pop_front();
            return result;
        };
        app.dialogs.save_file = [&](HWND owner, const auto &initial, const auto &name, SaveOptions &) {
            return app.dialogs.image_file(owner, true, initial, name);
        };
        app.dialogs.folder = [&](HWND, const wchar_t *) {
            CHECK(!directories.empty());
            auto result = directories.front();
            directories.pop_front();
            return result;
        };
        app.dialogs.file_transfer = [&](HWND, bool, const auto &, const FileTransferOptions &initial,
                                        int) -> std::optional<FileTransferChoice> {
            CHECK(!directories.empty());
            const auto directory = directories.front();
            directories.pop_front();
            if (directory.empty())
                return {};
            auto next = initial;
            next.basic_path = directory;
            next.registered = false;
            return FileTransferChoice{next, directory, -1};
        };
        CHECK(CreateWindowExW(0, L"GVN.AppAudit", L"GVN command audit", WS_OVERLAPPEDWINDOW, 0, 0, 900, 700,
                              nullptr, nullptr, GetModuleHandleW(nullptr), &app));
        // Legacy confirmation/cancel cases explicitly exercise the optional confirmation mode.
        app.settings.confirm_unsaved = true;
        // Existing source-depth contracts explicitly use the supported retention mode.
        app.settings.bitfield_to24 = false;
        app.settings.save_dialog = false; // Legacy direct-save cases; new policy case explicitly enables it.
        app.settings.save_as_retarget = true; // Legacy cases explicitly retain the prior binding policy.
        app.settings.set_end_action(0);   // Legacy contracts stop at the list boundary.
        app.settings.slide_loop = false; // Existing slideshow cases explicitly exercise stop-at-end.
    }
    ~AppAudit() {
        if (IsWindow(app.hwnd))
            DestroyWindow(app.hwnd);
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored); // Only this constructor's unique test directory.
    }
    static auto fixture(const wchar_t *name) {
        return std::filesystem::path(GVN_FIXTURES) / name;
    }
    void use_legacy_wide_policy() {
        // These navigation contracts predate the reference default which permits landscape pairs.
        app.settings.comic_single_wide = true;
    }
    auto copy(const wchar_t *source, const std::filesystem::path &name) {
        auto path = temporary / name;
        std::filesystem::create_directories(path.parent_path());
        std::filesystem::copy_file(fixture(source), path);
        return path;
    }
    void pump() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT)
                continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    void wait() {
        const auto until = GetTickCount64() + 5000;
        do {
            pump();
            CHECK(GetTickCount64() < until);
            if (!app.loading)
                return;
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        } while (true);
    }
    void open(const std::filesystem::path &path) {
        app.open(path);
        wait();
        CHECK(app.image);
    }
    void command(UINT id) {
        commands.insert(id);
        case_commands.insert(id);
        app.message(WM_COMMAND, id, 0);
    }
    std::wstring status(int part = 4) {
        wchar_t text[1024]{};
        SendMessageW(app.status, SB_GETTEXTW, part, reinterpret_cast<LPARAM>(text));
        return text;
    }
    void clipboard_text(const std::wstring &text, bool clear = true) {
        const auto bytes = (text.size() + 1) * sizeof(wchar_t);
        const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        CHECK(memory);
        const auto data = GlobalLock(memory);
        CHECK(data);
        memcpy(data, text.c_str(), bytes);
        GlobalUnlock(memory);
        CHECK(OpenClipboard(app.hwnd));
        if (clear)
            CHECK(EmptyClipboard());
        const bool published = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        CloseClipboard();
        if (!published)
            GlobalFree(memory);
        CHECK(published);
    }
    LRESULT forward(const std::filesystem::path &path, bool foreign = false) {
        auto identity = instance_identity(foreign ? temporary / L"other.ini" : app.config);
        auto bytes = instance_request(identity, path);
        COPYDATASTRUCT copy{kInstanceCopyData, static_cast<DWORD>(bytes.size()), bytes.data()};
        return SendMessageW(app.hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copy));
    }
    void drop(const std::filesystem::path &path) {
        auto object = file_data_object(path);
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        CHECK(SUCCEEDED(object->GetData(&format, &medium)));
        const auto copied = OleDuplicateData(medium.hGlobal, CF_HDROP, 0);
        ReleaseStgMedium(&medium);
        CHECK(copied);
        SendMessageW(app.hwnd, WM_DROPFILES, reinterpret_cast<WPARAM>(copied), 0);
    }
    void configured_ime_disable_changes_actual_main_input_context() {
        auto policy = [&](const wchar_t *value) {
            CHECK(WritePrivateProfileStringW(L"Viewer", L"disable_ime", value, app.config.c_str()));
            Settings next;
            next.load(app.config);
            app.apply_settings(next);
        };
        app.settings.save(app.config);
        policy(L"0");
        struct Context {
            AppAudit &audit;
            HIMC owned{ImmCreateContext()}, previous{};
            explicit Context(AppAudit &value) : audit(value) {
                CHECK(owned);
                previous = ImmAssociateContext(audit.app.hwnd, owned);
            }
            ~Context() {
                // The policy must release its saved association before this test-owned HIMC dies.
                try {
                    if (!WritePrivateProfileStringW(L"Viewer", L"disable_ime", L"0",
                                                    audit.app.config.c_str()))
                        throw std::runtime_error("IME cleanup profile");
                    Settings next;
                    next.load(audit.app.config);
                    audit.app.apply_settings(next);
                } catch (...) {
                    if (IsWindow(audit.app.hwnd))
                        DestroyWindow(audit.app.hwnd);
                }
                if (IsWindow(audit.app.hwnd))
                    ImmAssociateContext(audit.app.hwnd, previous);
                ImmDestroyContext(owned);
            }
        } context(*this);
        auto actual = ImmGetContext(app.hwnd);
        if (actual)
            ImmReleaseContext(app.hwnd, actual);
        CHECK(actual == context.owned);
        policy(L"1");
        actual = ImmGetContext(app.hwnd);
        if (actual)
            ImmReleaseContext(app.hwnd, actual);
        CHECK(actual == nullptr);
        policy(L"0");
        actual = ImmGetContext(app.hwnd);
        if (actual)
            ImmReleaseContext(app.hwnd, actual);
        CHECK(actual == context.owned);
    }
    void ime_default_new_children_language_focus_and_destroy_restore() {
        const auto current = [](HWND window) {
            const auto context = ImmGetContext(window);
            if (context)
                ImmReleaseContext(window, context);
            return context;
        };
        CHECK(app.settings.disable_ime && !current(app.hwnd) && !current(app.toolbar.hwnd));
        const auto child = CreateWindowExW(0, L"Static", L"later readonly control", WS_CHILD, 0, 0, 1, 1,
                                           app.hwnd, nullptr, nullptr, nullptr);
        CHECK(child && !current(child)); // The real App receives WM_PARENTNOTIFY for this future child.
        auto next = app.settings;
        next.disable_ime = false;
        app.apply_settings(next);
        struct OwnedContexts {
            AppAudit &audit;
            HWND child;
            HIMC root_context{}, child_context{}, previous_root{}, previous_child{};
            OwnedContexts(AppAudit &owner, HWND window) : audit(owner), child(window) {
                root_context = ImmCreateContext();
                child_context = ImmCreateContext();
                if (!root_context || !child_context) {
                    if (root_context)
                        ImmDestroyContext(root_context);
                    if (child_context)
                        ImmDestroyContext(child_context);
                    throw std::runtime_error("Cannot create private IME audit contexts");
                }
                previous_root = ImmAssociateContext(audit.app.hwnd, root_context);
                previous_child = ImmAssociateContext(child, child_context);
            }
            ~OwnedContexts() {
                if (IsWindow(audit.app.hwnd)) {
                    try {
                        auto next = audit.app.settings;
                        next.disable_ime = false;
                        audit.app.apply_settings(next);
                    } catch (...) {
                        DestroyWindow(audit.app.hwnd);
                    }
                }
                if (IsWindow(audit.app.hwnd))
                    ImmAssociateContext(audit.app.hwnd, previous_root);
                if (IsWindow(child))
                    ImmAssociateContext(child, previous_child);
                ImmDestroyContext(root_context);
                ImmDestroyContext(child_context);
            }
        } contexts(*this, child);
        const auto original_root = current(app.hwnd), original_child = current(child);
        CHECK(original_root && original_child);
        struct DestroyProbe {
            HIMC expected{};
            bool destroyed{}, restored{};
        } state{original_child};
        const auto callback =
            +[](HWND window, UINT msg, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) -> LRESULT {
            if (msg == WM_DESTROY) {
                auto &state = *reinterpret_cast<DestroyProbe *>(data);
                const auto context = ImmGetContext(window);
                if (context)
                    ImmReleaseContext(window, context);
                state.destroyed = true;
                state.restored = context == state.expected;
            }
            return DefSubclassProc(window, msg, w, l);
        };
        CHECK(SetWindowSubclass(child, callback, 70, reinterpret_cast<DWORD_PTR>(&state)));
        struct RemoveProbe {
            HWND window;
            SUBCLASSPROC callback;
            ~RemoveProbe() {
                if (IsWindow(window))
                    RemoveWindowSubclass(window, callback, 70);
            }
        } cleanup{child, callback};
        next.disable_ime = true;
        app.apply_settings(next);
        CHECK(!current(child));
        ImmAssociateContext(child, original_child);
        SendMessageW(app.hwnd, WM_INPUTLANGCHANGE, 0, reinterpret_cast<LPARAM>(GetKeyboardLayout(0)));
        CHECK(!current(child));
        ImmAssociateContext(app.hwnd, original_root);
        SendMessageW(app.hwnd, WM_SETFOCUS, 0, 0);
        CHECK(!current(app.hwnd));
        CHECK(DestroyWindow(app.hwnd));
        CHECK(state.destroyed && state.restored);
    }
    void editing_dimensions_allow_more_than_decode_limit_without_allocating_output() {
        app.settings.save(app.config);
        CHECK(WritePrivateProfileStringW(L"Viewer", L"allow_physical_overflow", L"1", app.config.c_str()));
        Settings proposed;
        proposed.load(app.config);
        app.apply_settings(proposed);
        texts.push_back(L"8193, 8192");
        const auto size = app.dimensions(L"編集寸法の境界検査", 3, 2);
        CHECK(size && size->first == 8193 && size->second == 8192);
        CHECK(!app.image && !app.loading); // This is dimension validation, not a giant allocation test.
    }
    void edit_memory_small_budget_controls_real_commands_without_touching_source() {
        CHECK(!app.settings.allow_physical_overflow);
        app.settings.confirm_unsaved = false;
        const auto file = copy(L"alpha.png", L"memory/source.png");
        const auto disk = read_file(file);
        open(file);
        command(Negative);
        const auto original = app.image, original_undo = app.undo_image;
        const auto pixels = original->frames[0].pixels;
        app.selection = {1, 0, 1, 2};
        size_t allocations{};
        app.editing.physical_bytes = [] { return uint64_t{23}; };
        app.editing.allocating = [&](size_t bytes) {
            CHECK(bytes == 24);
            ++allocations;
        };
        const auto unchanged = [&] {
            CHECK(app.image == original && app.undo_image == original_undo && app.modified);
            CHECK(app.selection.x == 1 && app.selection.w == 1 && app.selection.h == 2);
            CHECK(app.image->frames[0].pixels == pixels && app.requested == file && !app.loading);
        };
        for (const auto id : {Resize, Canvas, NewImage}) {
            texts.push_back(L"3, 2");
            THROWS(command(id));
            unchanged();
        }
        for (const auto text : {L"0, 2", L"65536, 1", L"-1, 2", L"18446744073709551615, 2", L"3, 2 extra"}) {
            texts.push_back(text);
            THROWS(command(Canvas));
            unchanged();
        }
        CHECK(!allocations);
        auto next = app.settings;
        next.allow_physical_overflow = next.nearest = true;
        app.apply_settings(next);
        unchanged();
        for (const auto id : {Resize, Canvas, NewImage}) {
            texts.push_back(L"3, 2");
            command(id);
            CHECK(app.image != original && app.undo_image == original && app.modified);
            if (id == NewImage)
                CHECK(app.image->frames[0].pixels == std::vector<Pixel>(6) && app.image->path.empty());
            else
                CHECK(app.image->frames[0].pixels == pixels);
            command(Undo);
            CHECK(app.image == original && app.requested == file);
        }
        CHECK(allocations == 3 && read_file(file) == disk);
        next.allow_physical_overflow = false;
        app.apply_settings(next);
        CHECK(app.image == original); // This option does not evict or reload an existing image.
        command(Negative);            // Same-size processing does not use the new-geometry physical budget.
        CHECK(app.image->frames[0].pixels == load_image(file).frames[0].pixels);
    }
    void edit_allocation_faults_preserve_undo_selection_and_animation() {
        app.settings.confirm_unsaved = false;
        open(fixture(L"alpha.png"));
        command(Negative);
        const auto original = app.image, original_undo = app.undo_image;
        const auto original_requested = app.requested;
        app.selection = {1, 0, 1, 2};
        app.editing.physical_bytes = [] { return uint64_t{1024}; };
        size_t attempts{};
        app.editing.allocating = [&](size_t bytes) {
            CHECK(bytes == 80);
            ++attempts;
            throw std::bad_alloc();
        };
        for (const auto id : {Resize, Canvas, NewImage}) {
            texts.push_back(L"5, 4");
            THROWS(command(id));
            CHECK(app.image == original && app.undo_image == original_undo && app.modified);
            CHECK(app.selection.x == 1 && app.selection.w == 1 && app.selection.h == 2);
            CHECK(app.requested == original_requested && app.player.frame == 0 && !app.loading);
        }
        CHECK(attempts == 3);
        app.editing.physical_bytes = []() -> uint64_t {
            throw std::runtime_error("Synthetic memory query failure");
        };
        texts.push_back(L"5, 4");
        THROWS(command(Resize));
        CHECK(attempts == 3 && app.image == original && app.undo_image == original_undo);
        auto next = app.settings;
        next.allow_physical_overflow = true;
        app.apply_settings(next);
        texts.push_back(L"5, 4");
        THROWS(command(NewImage)); // ON bypasses the physical-memory query, but not allocation failure.
        CHECK(attempts == 4 && app.image == original && app.undo_image == original_undo);
        open(fixture(L"disposal.gif")); // Read/decode is independent of the editing boundary.
        const auto animation = app.image;
        app.player.frame = 1;
        app.player.playing = false;
        texts.push_back(L"5, 4");
        answers.push_back(IDCANCEL);
        command(Resize);
        CHECK(app.image == animation && attempts == 4 && app.player.frame == 1);
        texts.push_back(L"5, 4");
        answers.push_back(IDOK);
        THROWS(command(Resize));
        CHECK(app.image == animation && attempts == 5 && app.player.frame == 1 && !app.player.playing);
        CHECK(!app.modified && !app.undo_image && !app.loading);
    }
    void created_maximum_axis_images_save_copy_and_undo_with_small_buffers() {
        app.settings.confirm_unsaved = false;
        for (const auto &[width, height] : {std::pair{65535u, 1u}, {1u, 65535u}}) {
            open(fixture(L"alpha.png"));
            const auto original = app.image;
            texts.push_back(std::to_wstring(width) + L", " + std::to_wstring(height));
            command(Canvas);
            CHECK(app.image->width == width && app.image->height == height);
            const auto pixels = app.image->frames[0].pixels;
            const auto target =
                temporary / (std::to_wstring(width) + L"x" + std::to_wstring(height) + L".png");
            paths.push_back(target);
            command(SaveAs);
            CHECK(!app.modified && load_image(target).frames[0].pixels == pixels);
            command(Copy);
            CHECK(paste_image(app.hwnd).frames[0].pixels == pixels);
            command(Undo);
            // Save As intentionally gives Undo the new file identity, without mutating the cached source.
            CHECK(app.image->frames[0].pixels == original->frames[0].pixels && app.image->path == target &&
                  app.modified);
        }
    }
    void new_image_undo_keeps_displayed_location_after_metadata_during_blocked_decode() {
        app.settings.confirm_unsaved = false;
        app.settings.prefetch_enabled = false;
        const auto first = copy(L"alpha.png", L"commit/a/source.png");
        const auto second = copy(L"rgb.bmp", L"commit/b/pending.bmp");
        open(first);
        const auto original = app.image;
        std::promise<void> entered, release;
        const auto gate = release.get_future().share();
        std::once_flag released;
        const auto unblock = [&] { std::call_once(released, [&] { release.set_value(); }); };
        struct Cleanup {
            std::function<void()> run;
            ~Cleanup() {
                run();
            }
        } cleanup{[&] {
            unblock();
            app.loader.reset();
        }};
        app.loader = std::make_unique<Loader>(
            [this](Loaded loaded) {
                auto value = std::make_unique<Loaded>(std::move(loaded));
                if (PostMessageW(app.hwnd, kLoaded, 0, reinterpret_cast<LPARAM>(value.get())))
                    value.release();
            },
            kDefaultCacheLimit,
            [&, second, gate](const auto &path) {
                if (path == second) {
                    entered.set_value();
                    gate.wait();
                }
                return load_image(path);
            });
        app.loader->configure(kDefaultCacheLimit, false);
        auto started = entered.get_future();
        app.open(second);
        const auto deadline = GetTickCount64() + 5000;
        while (started.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            pump();
            CHECK(GetTickCount64() < deadline);
            MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        CHECK(app.loading && app.before_load && app.before_load->replaced && app.image == original);
        CHECK(app.requested == second && app.folder == second.parent_path());
        texts.push_back(L"7, 9");
        command(NewImage);
        CHECK(!app.loading && app.image->width == 7 && app.requested.empty());
        command(Undo);
        CHECK(app.image == original && app.requested == first && app.folder == first.parent_path());
        CHECK(app.files.size() == 1 && app.files[0].path == first && app.index == 0);
    }
    void forwarded_requests_acknowledge_before_open_and_keep_latest_file() {
        app.settings.confirm_unsaved = app.settings.activate_reused = false;
        auto first = copy(L"alpha.png", L"01.png"), second = copy(L"rgb.bmp", L"02.bmp"),
             third = copy(L"alpha.png", L"03.png");
        open(first);
        const auto original = app.image;
        CHECK(forward(second) == 1 && !app.loading && app.image == original);
        CHECK(forward(third) == 1 && forward({}) == 1);
        CHECK(app.forwarded_file && app.forwarded_file->path == third);
        CHECK(forward(second, true) == 0 && app.forwarded_file->path == third);
        wait();
        CHECK(app.image->path == third && !app.forwarded_file && !app.before_load);
        CHECK(notices.empty());
    }
    void forwarded_file_waits_for_modal_owner_and_loads_after_enable() {
        app.settings.confirm_unsaved = app.settings.activate_reused = false;
        auto first = copy(L"alpha.png", L"01.png"), second = copy(L"rgb.bmp", L"02.bmp");
        open(first);
        const auto original = app.image;
        EnableWindow(app.hwnd, FALSE);
        CHECK(forward(second) == 1);
        pump();
        CHECK(app.forwarded_file && !app.loading && app.image == original);
        EnableWindow(app.hwnd, TRUE);
        wait();
        CHECK(app.image->path == second && !app.forwarded_file && notices.empty());
    }
    void reused_activation_off_keeps_minimized_through_metadata_and_image_policy() {
        app.settings.confirm_unsaved = app.settings.activate_reused = false;
        app.settings.display_mode = 0;
        const auto first = copy(L"alpha.png", L"01.png");
        open(first);
        const auto second = copy(L"rgb.bmp", L"new-folder/02.bmp");
        ShowWindow(app.hwnd, SW_MINIMIZE);
        SetActiveWindow(app.hwnd);
        CHECK(IsIconic(app.hwnd));
        CHECK(forward(second) == 1);
        app.message(kForwarded, 0, 0);
        CHECK(app.pending_open && app.before_load && app.before_load->no_activate);
        wait();
        CHECK(app.image->path == second && IsIconic(app.hwnd) && app.deferred_display_policy);
        app.settings.activate_reused = true;
        CHECK(forward(first) == 1);
        wait();
        CHECK(app.image->path == first && !IsIconic(app.hwnd) && !app.deferred_display_policy);
        CHECK(forward({}) == 1);
        pump();
        CHECK(app.image->path == first && !app.loading && notices.empty());
        // A thread can retain its active HWND while another process is in the foreground.
        app.settings.activate_reused = false;
        ShowWindow(app.hwnd, SW_MAXIMIZE);
        SetActiveWindow(app.hwnd);
        CHECK(IsZoomed(app.hwnd) && GetForegroundWindow() != app.hwnd);
        CHECK(forward(second) == 1);
        wait();
        CHECK(IsZoomed(app.hwnd) && app.deferred_display_policy && app.image->path == second);
        app.settings.activate_reused = true;
        CHECK(forward(first) == 1);
        wait();
        CHECK(!IsZoomed(app.hwnd) && !app.deferred_display_policy && app.image->path == first);
    }
    void dropped_file_activation_controls_restore_and_preserves_source_alpha() {
        app.settings.confirm_unsaved = false;
        app.settings.display_mode = 0;
        auto first = copy(L"rgb.bmp", L"01.bmp"), second = copy(L"alpha.png", L"dropped/02.png");
        open(first);
        ShowWindow(app.hwnd, SW_SHOWMINNOACTIVE);
        SetActiveWindow(nullptr);
        app.settings.activate_dropped = false;
        drop(second);
        wait();
        CHECK(IsIconic(app.hwnd) && app.deferred_display_policy && app.image->path == second);
        const auto decoded = load_image(second);
        CHECK(app.image->frames[0].pixels == decoded.frames[0].pixels);
        app.settings.activate_dropped = true;
        drop(first);
        wait();
        CHECK(!IsIconic(app.hwnd) && !app.deferred_display_policy && app.image->path == first);
        CHECK(notices.empty());
    }
    void startup_reopen_default_off_explicit_priority_fallback_and_quiet_empty() {
        app.settings.confirm_unsaved = false;
        // This case deliberately deletes an old source, independently of background prefetch.
        auto next = app.settings;
        next.prefetch_enabled = false;
        app.apply_settings(next);
        const auto first = copy(L"alpha.png", L"saved/01.png"), second = copy(L"rgb.bmp", L"saved/02.bmp");
        app.settings.last_file = second.wstring();
        app.open_startup({});
        CHECK(!app.image && !app.loading && !app.settings.reopen_last);
        app.settings.reopen_last = true;
        app.open_startup({});
        CHECK(app.pending_open && app.pending_open->allow_empty);
        wait();
        CHECK(app.image->path == second);
        command(CloseImage);
        app.open_startup(first);
        wait();
        CHECK(app.image->path == first);
        std::filesystem::remove(second);
        app.open_startup({});
        CHECK(app.pending_open);
        wait();
        CHECK(app.image->path == first);
        command(CloseImage);
        std::filesystem::create_directory(temporary / L"empty");
        app.settings.last_file = (temporary / L"empty/missing.png").wstring();
        app.open_startup({});
        wait();
        CHECK(!app.image && !app.loading && notices.empty());
        app.settings.last_file = (temporary / L"missing-directory/missing.png").wstring();
        app.open_startup({});
        wait();
        CHECK(!app.image && notices.empty());
    }
    void closing_remembers_displayed_source_instead_of_pending_file_or_clipboard() {
        app.settings.confirm_unsaved = false;
        app.settings.reopen_last = true;
        const auto first = copy(L"alpha.png", L"01.png"), second = copy(L"rgb.bmp", L"02.bmp");
        open(first);
        app.open(second);
        CHECK(app.loading && app.image->path == first);
        app.message(WM_CLOSE, 0, 0);
        Settings saved;
        saved.load(app.config);
        CHECK(saved.reopen_last && saved.last_file == first.wstring() && saved.last_archive_entry.empty());
        {
            AppAudit empty;
            empty.app.settings.last_file = first.wstring();
            empty.app.message(WM_CLOSE, 0, 0);
            Settings state;
            state.load(empty.app.config);
            CHECK(state.last_file.empty() && state.last_archive_entry.empty());
        }
        {
            AppAudit clipboard;
            clipboard.app.settings.confirm_unsaved = false;
            clipboard.open(first);
            copy_image(clipboard.app.hwnd, *clipboard.app.image, 0);
            clipboard.command(NewPaste);
            CHECK(clipboard.app.requested.empty() && clipboard.app.image->path.empty());
            clipboard.app.message(WM_CLOSE, 0, 0);
            Settings state;
            state.load(clipboard.app.config);
            CHECK(state.last_file.empty() && state.last_archive_entry.empty());
        }
    }
    void startup_archive_remembers_member_name_and_reloads_original_not_unsaved_edits() {
        app.settings.confirm_unsaved = false;
        app.settings.reopen_last = true;
        const auto book = copy(L"images.zip", L"book.zip");
        open(book);
        const auto first_token = app.files.front().path;
        command(Last);
        wait();
        const auto original = app.image->frames[0].pixels;
        const auto last_token = app.requested;
        app.remember_startup_file();
        CHECK(app.settings.last_file == last_token.wstring() &&
              app.settings.last_archive_entry == app.files.back().display_name);
        command(Negative);
        CHECK(app.modified && app.image->frames[0].pixels != original);
        command(CloseImage);
        // The saved index can refer to another entry after the archive is rewritten.
        app.settings.last_file = first_token.wstring();
        app.open_startup({});
        wait();
        CHECK(app.requested == last_token && app.image->frames[0].pixels == original && !app.modified);
        CHECK(notices.empty());
    }
    void folder_adoption_uses_worker_snapshot_without_rescanning_or_resorting() {
        const auto a = copy(L"alpha.png", L"snapshot/a/1.png");
        const auto b1 = copy(L"alpha.png", L"snapshot/b.zip/1.png").lexically_normal();
        const auto b2 = copy(L"alpha.png", L"snapshot/b.zip/2.png").lexically_normal();
        app.navigation_root = temporary / L"snapshot";
        open(a);
        command(NextFolder);
        auto result = std::make_unique<Loaded>();
        result->sequence = app.loader->sequence();
        result->path = b1.parent_path();
        result->folder_match = FolderMatch{result->path, 1, false};
        result->folder_match->files = std::vector<FileEntry>{{b2, 20, 200}, {b1, 10, 100}};
        copy(L"alpha.png", L"snapshot/b.zip/3.png"); // Added after the supplied snapshot.
        app.accept(std::move(result));
        wait();
        CHECK(same_path(app.requested, b2) && app.files.size() == 2 && app.index == 0);
        CHECK(app.files[0].bytes == 20 && app.files[1].time == 100 && app.archive.empty());
        command(Next);
        wait();
        CHECK(same_path(app.requested, b1) && app.files.size() == 2 && app.index == 1);
    }
    void folder_adoption_missing_list_or_disappearing_file_keeps_original_document() {
        const auto a = copy(L"alpha.png", L"snapshot-fail/a/1.png");
        const auto b = copy(L"alpha.png", L"snapshot-fail/b/1.png").lexically_normal();
        app.navigation_root = temporary / L"snapshot-fail";
        open(a);
        const auto source = app.image;
        const auto history = app.settings.history;
        command(NextFolder);
        auto missing = std::make_unique<Loaded>();
        missing->sequence = app.loader->sequence();
        missing->path = b.parent_path();
        missing->folder_match = FolderMatch{missing->path, 1, false};
        THROWS(app.accept(std::move(missing)));
        CHECK(app.image == source && same_path(app.requested, a) && !app.loading);
        CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
        pump();
        command(NextFolder);
        auto deleted = std::make_unique<Loaded>();
        deleted->sequence = app.loader->sequence();
        deleted->path = b.parent_path();
        deleted->folder_match = FolderMatch{deleted->path, 1, false};
        deleted->folder_match->files = std::vector<FileEntry>{{b.parent_path() / L"gone.png", 1, 0}};
        app.accept(std::move(deleted));
        wait();
        CHECK(app.image == source && same_path(app.requested, a) && app.files.size() == 1);
        CHECK(app.settings.history == history && !app.navigation_folder);
    }
    void changing_sort_or_visibility_cancels_pending_folder_snapshot() {
        const auto a = copy(L"alpha.png", L"changing/a/1.png");
        copy(L"alpha.png", L"changing/b/1.png");
        app.navigation_root = temporary / L"changing";
        open(a);
        const auto source = app.image;
        for (const UINT command_id : {SortTime, SortReverse, NaturalSort, ShowHidden}) {
            command(NextFolder);
            CHECK(app.pending_folder && app.loading);
            command(command_id);
            CHECK(!app.pending_folder && !app.loading);
            CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
            pump();
            CHECK(app.image == source && same_path(app.requested, a));
        }
    }
    void folder_commands_depth_first_empty_adjacent_and_independent_head_choices() {
        const auto a1 = copy(L"alpha.png", L"nav/a/1.png"), a2 = copy(L"alpha.png", L"nav/a/2.png");
        const auto child1 = copy(L"alpha.png", L"nav/a/child/1.png"),
                   child2 = copy(L"alpha.png", L"nav/a/child/2.png");
        const auto c1 = copy(L"alpha.png", L"nav/c/1.png");
        const auto root1 = copy(L"alpha.png", L"nav/1.png"), root2 = copy(L"alpha.png", L"nav/2.png");
        const auto empty = temporary / L"nav/b-empty";
        std::filesystem::create_directory(empty);
        app.navigation_root = temporary / L"nav";
        open(a1);
        command(NextFolder);
        wait();
        CHECK(same_path(app.requested, child1));
        command(PreviousFolder);
        wait();
        CHECK(same_path(app.requested, a2));
        app.settings.previous_folder_first = true;
        command(NextFolder);
        wait();
        command(PreviousFolder);
        wait();
        CHECK(same_path(app.requested, a1));
        open(c1);
        app.settings.previous_folder_first = false;
        command(PreviousFolder);
        wait();
        CHECK(same_path(app.requested, child2));
        open(c1);
        command(PreviousAdjacentFolder);
        wait();
        CHECK(!app.image && app.files.empty() && app.requested.empty() && !app.modified);
        CHECK(same_path(app.folder, empty) && !app.original_file() && !app.undo_image);
        wchar_t caption[1024]{};
        GetWindowTextW(app.hwnd, caption, 1024);
        CHECK(std::wstring(caption).find(L"b-empty") != std::wstring::npos);
        command(PreviousAdjacentFolder);
        wait();
        CHECK(same_path(app.requested, a2));
        app.settings.adjacent_previous_first = true;
        command(NextAdjacentFolder);
        wait();
        CHECK(!app.image && same_path(app.folder, empty));
        command(PreviousAdjacentFolder);
        wait();
        CHECK(same_path(app.requested, a1));
        open(child1);
        command(ParentFolder);
        wait();
        CHECK(same_path(app.requested, a2));
        app.settings.parent_first = true;
        open(child1);
        command(ParentFolder);
        wait();
        CHECK(same_path(app.requested, a1));
        app.settings.parent_first = false;
        command(ParentFolder);
        wait();
        CHECK(same_path(app.requested, root2));
        app.settings.parent_first = true;
        open(a1);
        command(ParentFolder);
        wait();
        CHECK(same_path(app.requested, root1));
    }
    void folder_history_suppression_explicit_departure_and_global_disable() {
        const auto a = copy(L"alpha.png", L"hist/a/1.png");
        const auto b1 = copy(L"alpha.png", L"hist/b/1.png"), b2 = copy(L"alpha.png", L"hist/b/2.png");
        const auto c = copy(L"alpha.png", L"hist/c/1.png");
        app.navigation_root = temporary / L"hist";
        open(a);
        const auto contains = [&](const auto &path) {
            return std::any_of(app.settings.history.begin(), app.settings.history.end(),
                               [&](const auto &entry) { return same_path(entry, path); });
        };
        command(NextFolder);
        wait();
        CHECK(same_path(app.requested, b1) && app.navigation_folder);
        CHECK(!contains(b1.parent_path()));
        command(Next);
        wait();
        CHECK(same_path(app.requested, b2) && !contains(b1.parent_path()));
        command(First);
        wait();
        CHECK(!contains(b1.parent_path()));
        open(c);
        CHECK(contains(b1.parent_path()) && contains(c.parent_path()) && !app.navigation_folder);
        app.settings.folder_history = true;
        command(PreviousFolder);
        wait();
        CHECK(same_path(app.requested, b2) && contains(b1.parent_path()));
        auto next = app.settings;
        next.remember_history = false;
        app.apply_settings(next);
        command(NextFolder);
        wait();
        CHECK(app.settings.history.empty());
        open(a);
        CHECK(app.settings.history.empty());
    }
    void folder_navigation_preserves_source_on_corrupt_target_and_cancelled_discard() {
        const auto a = copy(L"alpha.png", L"fail/a/1.png");
        const auto b = copy(L"alpha.png", L"fail/b/1.png");
        app.navigation_root = temporary / L"fail";
        open(a);
        command(Negative);
        const auto edited = app.image;
        answers.push_back(IDCANCEL);
        command(NextFolder);
        CHECK(app.image == edited && app.modified && !app.loading && !app.pending_folder);
        app.settings.save_dialog = true;
        answers.push_back(IDYES);
        paths.emplace_back();
        command(NextFolder);
        CHECK(app.image == edited && app.modified && !app.loading);
        answers.push_back(IDNO);
        command(NextFolder);
        wait();
        CHECK(same_path(app.requested, b) && !app.modified);
        CHECK(read_file(a) == read_file(fixture(L"alpha.png")));
        open(a);
        const auto original = app.image;
        const auto history = app.settings.history;
        std::ofstream corrupt(b, std::ios::binary | std::ios::trunc);
        corrupt << "not an image";
        corrupt.close();
        command(NextFolder);
        wait();
        CHECK(app.image == original && same_path(app.requested, a) && !app.navigation_folder);
        CHECK(app.settings.history == history && !app.pending_folder && !app.loading);
    }
    void movement_boundary_modes_clamp_skip_confirm_loop_and_folder() {
        const auto a1 = copy(L"alpha.png", L"end/a/1.png"), a2 = copy(L"alpha.png", L"end/a/2.png");
        const auto b1 = copy(L"alpha.png", L"end/b/1.png"), b2 = copy(L"alpha.png", L"end/b/2.png");
        app.navigation_root = temporary / L"end";
        open(a1);
        app.settings.skip_count = 10;
        command(SkipNext);
        wait();
        CHECK(same_path(app.requested, a2) && !boundary_beeps);
        command(Next);
        CHECK(same_path(app.requested, a2) && boundary_directions.empty());
        app.settings.boundary_sound = true;
        command(Next);
        CHECK(boundary_beeps == 1 && !app.loading);
        app.settings.set_end_action(2);
        command(SkipNext);
        wait();
        CHECK(same_path(app.requested, a1) && boundary_beeps == 2);
        command(Previous);
        wait();
        CHECK(same_path(app.requested, a2) && boundary_beeps == 3);
        command(SkipBack);
        wait();
        CHECK(same_path(app.requested, a1) && boundary_beeps == 3);
        app.settings.set_end_action(1);
        boundary_answers.push_back(BoundaryChoice::Cancel);
        command(Previous);
        CHECK(same_path(app.requested, a1) && boundary_directions.back() == false);
        boundary_answers.push_back(BoundaryChoice::FolderEnd);
        command(Previous);
        wait();
        CHECK(same_path(app.requested, a2));
        boundary_answers.push_back(BoundaryChoice::FolderEnd);
        command(Next);
        wait();
        CHECK(same_path(app.requested, a1) && boundary_directions.back());
        open(b1);
        app.settings.previous_folder_first = true;
        boundary_answers.push_back(BoundaryChoice::Folder);
        command(Previous);
        wait();
        CHECK(same_path(app.requested, a2));
        app.settings.set_end_action(3);
        command(Next);
        wait();
        CHECK(same_path(app.requested, b1));
        command(Previous);
        wait();
        CHECK(same_path(app.requested, a2));
        open(b2);
        const auto source = app.image;
        command(Next);
        wait();
        CHECK(app.image == source && same_path(app.requested, b2));
        CHECK(!app.slideshow && boundary_answers.empty());
        command(ListLoop);
        CHECK(app.settings.end_action == 2 && app.settings.list_loop);
        command(ListLoop);
        CHECK(app.settings.end_action == 0 && !app.settings.list_loop);
    }
    void movement_toolbar_reverse_keeps_menu_shortcuts_and_button_bitmaps() {
        for (int i = 1; i <= 4; ++i)
            copy(L"alpha.png", L"bar/" + std::to_wstring(i) + L".png");
        open(temporary / L"bar/2.png");
        const auto bitmap = SendMessageW(app.toolbar.hwnd, TB_GETBITMAP, Previous, 0);
        app.comic = true;
        app.settings.comic_rtl = app.settings.comic_toolbar_reverse = true;
        app.update();
        app.message(WM_COMMAND, Previous, reinterpret_cast<LPARAM>(app.toolbar.hwnd));
        wait();
        CHECK(app.index == 2);
        command(Previous);
        wait();
        CHECK(app.index == 1);
        app.message(WM_KEYDOWN, VK_LEFT, 0);
        wait();
        CHECK(app.index == 0);
        CHECK(SendMessageW(app.toolbar.hwnd, TB_GETBITMAP, Previous, 0) == bitmap);
        auto next = app.settings;
        next.comic_toolbar_reverse = false;
        app.apply_settings(next);
        app.message(WM_COMMAND, Next, reinterpret_cast<LPARAM>(app.toolbar.hwnd));
        wait();
        CHECK(app.index == 1);
        next.comic_toolbar_reverse = true;
        next.comic_rtl = false;
        app.apply_settings(next);
        wait();
        app.message(WM_COMMAND, Previous, reinterpret_cast<LPARAM>(app.toolbar.hwnd));
        wait();
        CHECK(app.index == 0);
    }
    void folder_search_escape_stale_result_and_changed_document_are_rejected() {
        const auto a = copy(L"alpha.png", L"race/a/1.png"), b = copy(L"alpha.png", L"race/b/1.png");
        const auto c = copy(L"alpha.png", L"race/c/1.png");
        app.navigation_root = temporary / L"race";
        open(a);
        const auto source = app.image;
        command(NextFolder);
        const auto old = app.loader->sequence();
        CHECK(app.loading && app.pending_folder);
        app.message(WM_KEYDOWN, VK_ESCAPE, 0);
        CHECK(!app.loading && !app.pending_folder && app.image == source);
        CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
        pump();
        CHECK(app.image == source);
        auto stale = std::make_unique<Loaded>();
        stale->sequence = old;
        stale->path = b.parent_path();
        stale->folder_match = FolderMatch{b.parent_path(), 1, false};
        open(c);
        const auto current = app.image;
        app.accept(std::move(stale));
        CHECK(app.image == current && same_path(app.requested, c));
        open(a);
        command(NextFolder);
        auto result = std::make_unique<Loaded>();
        result->sequence = app.loader->sequence();
        result->path = b.parent_path();
        result->folder_match = FolderMatch{b.parent_path(), 1, false};
        app.image = std::make_shared<Image>(*app.image); // A document change without a worker request.
        const auto changed = app.image;
        app.accept(std::move(result));
        CHECK(app.image == changed && same_path(app.requested, a) && !app.loading && !app.pending_folder);
        CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
        pump();
        CHECK(app.image == changed);
    }
    void folder_history_is_saved_on_close_even_when_navigation_visits_are_suppressed() {
        const auto a = copy(L"alpha.png", L"closing/a/1.png"), b = copy(L"alpha.png", L"closing/b/1.png");
        app.navigation_root = temporary / L"closing";
        open(a);
        command(NextFolder);
        wait();
        CHECK(same_path(app.requested, b));
        CHECK(std::none_of(app.settings.history.begin(), app.settings.history.end(),
                           [&](const auto &entry) { return same_path(entry, b.parent_path()); }));
        app.message(WM_CLOSE, 0, 0);
        Settings restored;
        restored.load(app.config);
        CHECK(!IsWindow(app.hwnd));
        CHECK(!restored.history.empty() && same_path(restored.history.front(), b.parent_path()));
    }
    void save_dialog_overwrite_and_remembered_folder_policy() {
        const auto source = copy(L"alpha.png", L"source/a.png");
        const auto existing = copy(L"alpha.png", L"saved/existing.png");
        const auto original = read_file(source);
        open(source);
        app.settings.confirm_unsaved = false;
        app.settings.save_dialog = true;
        command(Negative);
        const auto edited = app.image->frames[0].pixels;
        paths.push_back({});
        CHECK(!app.save(false));
        CHECK(read_file(source) == original && app.modified);
        paths.push_back(source);
        answers.push_back(IDNO);
        CHECK(!app.save(false));
        CHECK(read_file(source) == original);
        paths.push_back(source);
        answers.push_back(IDYES);
        CHECK(app.save(false));
        CHECK(load_image(source).frames[0].pixels == edited && !app.modified);
        CHECK(save_folders.front() == source.parent_path());
        app.settings.save_dialog = false;
        app.settings.confirm_overwrite = false;
        command(Negative);
        CHECK(app.save(false) &&
              load_image(source).frames[0].pixels == load_image(existing).frames[0].pixels);
        const auto existing_bytes = read_file(existing);
        command(Negative);
        paths.push_back(existing);
        answers.push_back(IDNO); // Save As always confirms, even with overwrite confirmation OFF.
        CHECK(!app.save(true));
        CHECK(read_file(existing) == existing_bytes && app.settings.last_save_folder.empty());
        paths.push_back(existing);
        answers.push_back(IDYES);
        CHECK(app.save(true));
        CHECK(load_image(existing).frames[0].pixels == edited);
        CHECK(app.settings.last_save_folder == existing.parent_path());
        open(source);
        paths.push_back({});
        CHECK(!app.save(true));
        CHECK(save_folders.back() == existing.parent_path());
        app.settings.remember_save_folder = false;
        paths.push_back({});
        CHECK(!app.save(true));
        CHECK(save_folders.back() == source.parent_path());
        app.settings.save(app.config);
        Settings restored;
        restored.load(app.config);
        CHECK(!restored.save_dialog && !restored.confirm_overwrite && !restored.remember_save_folder);
        CHECK(restored.last_save_folder == existing.parent_path());
    }
    void save_format_proposals_commit_only_after_success() {
        const auto source = copy(L"alpha.png", L"source/a.png");
        const auto existing = copy(L"alpha.png", L"saved/existing.png");
        const auto original = read_file(source), destination = read_file(existing);
        open(source);
        app.settings.confirm_unsaved = false;
        command(Negative);
        const auto rgba = app.image->frames[0].pixels;
        const auto before = app.settings.saving;
        app.dialogs.save_file = [&](HWND owner, const auto &initial, const auto &name,
                                    SaveOptions &proposed) {
            proposed.png.interlaced = true;
            proposed.png.compression = 0;
            proposed.keep_times = proposed.uppercase_extension = true;
            return app.dialogs.image_file(owner, true, initial, name);
        };
        paths.push_back({});
        CHECK(!app.save(true) && app.settings.saving == before && app.modified);
        paths.push_back(existing);
        answers.push_back(IDNO);
        CHECK(!app.save(true) && app.settings.saving == before && read_file(existing) == destination);
        paths.push_back(temporary / L"missing/no.png");
        THROWS(app.save(true));
        CHECK(app.settings.saving == before && app.image->path == source && app.modified);
        const auto target = temporary / L"saved/new.png";
        paths.push_back(target);
        CHECK(app.save(true));
        CHECK(app.settings.saving.png.interlaced && app.settings.saving.png.compression == 0 &&
              app.settings.saving.keep_times && app.settings.saving.uppercase_extension);
        CHECK(read_file(target)[28] == 1 && load_image(target).frames[0].pixels == rgba);
        CHECK(read_file(source) == original && !app.modified);
        app.settings.save(app.config);
        Settings restored;
        restored.load(app.config);
        CHECK(restored.saving == app.settings.saving);
    }
    void save_command_routes_each_format_options_to_actual_files() {
        const auto source = copy(L"alpha.png", L"source.png");
        const auto original = read_file(source);
        open(source);
        app.settings.confirm_unsaved = false;
        const auto rgba = app.image->frames[0].pixels;
        app.settings.saving.png.interlaced = true;
        app.settings.saving.jpeg.progressive = true;
        app.settings.saving.jpeg.quality = 50;
        app.settings.saving.jpeg.matte = app.settings.saving.bmp.matte = app.settings.saving.gif.matte =
            0x123456;
        app.settings.saving.bmp_bits = 8;
        app.settings.saving.bmp.top_down = true;
        app.settings.saving.gif.interlaced = app.settings.saving.gif.uncompressed = true;
        for (auto extension : {L".png", L".jpg", L".bmp", L".gif"}) {
            const auto target = temporary / (std::wstring(L"output") + extension);
            paths.push_back(target);
            if (extension != std::wstring(L".png"))
                answers.push_back(IDOK);
            CHECK(app.save(true));
            const auto bytes = read_file(target);
            const auto loaded = load_image(target);
            CHECK(app.image->frames[0].pixels == rgba &&
                  app.settings.saving.last_format == save_format(target));
            if (extension == std::wstring(L".png"))
                CHECK(bytes[28] == 1 && loaded.frames[0].pixels == rgba);
            else if (extension == std::wstring(L".jpg")) {
                const std::array<uint8_t, 2> marker{255, 0xc2};
                CHECK(std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end()) != bytes.end());
            } else if (extension == std::wstring(L".bmp")) {
                CHECK(bytes[28] == 8 && (bytes[25] & 128));
                for (size_t i = 0; i < rgba.size(); ++i)
                    CHECK(loaded.frames[0].pixels[i] == over(rgba[i], {18, 52, 86, 255}));
            } else {
                const auto offset = 13 + 3 * (2u << (bytes[10] & 7));
                CHECK(bytes[offset] == ',' && (bytes[offset + 9] & 64));
                for (size_t i = 0; i < rgba.size(); ++i)
                    CHECK(loaded.frames[0].pixels[i] == over(rgba[i], {18, 52, 86, 255}));
            }
        }
        CHECK(answers.empty() && read_file(source) == original);
    }
    void rename_resort_preserves_pixels_view_dirty_and_undo() {
        copy(L"alpha.png", L"rename/a.png");
        const auto source = copy(L"alpha.png", L"rename/b.png");
        copy(L"alpha.png", L"rename/c.png");
        open(source);
        app.settings.confirm_unsaved = false;
        app.settings.rename_resort = false;
        command(Negative);
        command(Actual);
        const auto rgba = app.image->frames[0].pixels;
        const auto scale = app.view.scale;
        texts.push_back(L"z.png");
        command(RenameFile);
        CHECK(!app.loading && app.index == 1 && app.files[1].path.filename() == L"z.png");
        CHECK(app.image->frames[0].pixels == rgba && app.view.scale == scale && app.modified);
        CHECK(app.undo_image && app.undo_image->path.filename() == L"z.png");
        command(Undo);
        CHECK(!app.modified && app.image->path.filename() == L"z.png");
        app.settings.rename_resort = true;
        texts.push_back(L"0.png");
        command(RenameFile);
        CHECK(!app.loading && app.index == 0 && app.files.front().path.filename() == L"0.png");
        command(Next);
        wait();
        CHECK(app.image->path.filename() == L"a.png");
        texts.push_back(L"a.png:stream");
        THROWS(app.command(RenameFile));
        CHECK(std::filesystem::exists(app.image->path));
    }
    void copy_move_independent_confirmation_relative_paths_and_failures() {
        const auto source = copy(L"alpha.png", L"source/a.png");
        const auto target = copy(L"rgb.bmp", L"dest/a.png");
        open(source);
        const auto original = read_file(source), old_target = read_file(target);
        app.settings.copy_destinations[0] = L"../dest";
        app.settings.move_destinations[0] = L"../dest";
        answers.push_back(IDNO);
        command(CopyDestBase);
        CHECK(read_file(target) == old_target && read_file(source) == original);
        app.settings.confirm_copy = false;
        const auto messages = notices.size();
        command(CopyDestBase);
        CHECK(read_file(target) == original && notices.size() == messages);
        answers.push_back(IDNO);
        command(MoveDestBase);
        CHECK(std::filesystem::exists(source) && same_path(app.image->path, source));
        app.settings.confirm_move = false;
        command(Actual);
        const auto rgba = app.image->frames[0].pixels;
        command(MoveDestBase);
        CHECK(!app.loading && !std::filesystem::exists(source) && same_path(app.image->path, target));
        CHECK(app.image->frames[0].pixels == rgba && app.view.scale == 1);
        app.settings.copy_destinations[1] = target.parent_path();
        app.settings.move_destinations[1] = target.parent_path();
        THROWS(app.command(CopyDestBase + 1));
        THROWS(app.command(MoveDestBase + 1));
        const auto locked = copy(L"rgb.bmp", L"locked/a.png");
        const auto lock =
            CreateFileW(locked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(lock != INVALID_HANDLE_VALUE);
        app.settings.copy_destinations[2] = locked.parent_path();
        bool failed = false;
        try {
            app.command(CopyDestBase + 2);
        } catch (...) {
            failed = true;
        }
        CloseHandle(lock);
        CHECK(failed && read_file(locked) == old_target && same_path(app.image->path, target));
    }
    void move_advance_uses_source_order_and_last_file_boundary() {
        copy(L"alpha.png", L"source/a.png");
        const auto middle = copy(L"alpha.png", L"source/b.png");
        const auto last = copy(L"alpha.png", L"source/c.png");
        std::filesystem::create_directory(temporary / L"dest");
        app.settings.move_destinations[0] = temporary / L"dest";
        app.settings.next_after_fileop = true;
        open(middle);
        command(MoveDestBase);
        wait();
        CHECK(same_path(app.image->path, last) && app.files.size() == 2 && app.index == 1);
        CHECK(std::filesystem::exists(temporary / L"dest/b.png") && !std::filesystem::exists(middle));
        command(MoveDestBase);
        wait();
        CHECK(app.image->path.filename() == L"a.png" && app.files.size() == 1);
        command(MoveDestBase);
        CHECK(!app.image && app.files.empty());
        CHECK(std::filesystem::exists(temporary / L"dest/a.png"));
    }
    void delete_without_advance_keeps_alpha_and_navigation_gap() {
        const auto first = copy(L"alpha.png", L"delete/a.png");
        const auto middle = copy(L"alpha.png", L"delete/b.png");
        const auto last = copy(L"alpha.png", L"delete/c.png");
        open(middle);
        const auto rgba = app.image->frames[0].pixels;
        app.settings.confirm_delete = false;
        app.settings.recycle_deleted = false;
        app.settings.next_after_fileop = false;
        command(FileDelete);
        CHECK(!std::filesystem::exists(middle) && app.image && app.image->path.empty());
        CHECK(app.removed_current && app.index == 1 && app.files.size() == 2 && status(1) == L"—");
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == rgba);
        command(Next);
        wait();
        CHECK(same_path(app.image->path, last) && !app.removed_current);
        command(Previous);
        wait();
        CHECK(same_path(app.image->path, first));
        command(FileDelete);
        command(Next);
        wait();
        CHECK(same_path(app.image->path, last));
        command(FileDelete);
        CHECK(app.image && app.removed_current && app.files.empty());
        paths.push_back(temporary / L"restored.png");
        command(SaveAs);
        CHECK(!app.removed_current && load_image(temporary / L"restored.png").frames[0].pixels == rgba);
    }
    void delete_confirm_modes_failure_advance_and_gap_rollback() {
        const auto first = copy(L"alpha.png", L"delete/a.png");
        const auto middle = copy(L"alpha.png", L"delete/b.png");
        const auto last = temporary / L"delete/c.png";
        std::ofstream(last) << "corrupt PNG";
        open(middle);
        answers.push_back(IDNO);
        command(FileDelete);
        CHECK(std::filesystem::exists(middle) && !app.removed_current);
        bool recycled = false;
        app.dialogs.delete_file = [&](HWND owner, const auto &path, bool recycle) {
            CHECK(path == middle);
            recycled = recycle; // Routing only; actual Shell recycle/restore has its separate native probe.
            delete_file_checked(owner, path, false);
        };
        answers.push_back(IDYES);
        command(FileDelete);
        CHECK(recycled && !std::filesystem::exists(middle));
        command(Next);
        wait();
        CHECK(app.removed_current && app.requested == middle && app.index == 1 && app.image->path.empty());
        command(Previous);
        wait();
        CHECK(app.requested == first && !app.removed_current);
        app.dialogs.delete_file = gvn::delete_file_checked;
        app.settings.recycle_deleted = false;
        const auto lock =
            CreateFileW(first.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(lock != INVALID_HANDLE_VALUE);
        answers.push_back(IDYES);
        bool failed = false;
        try {
            app.command(FileDelete);
        } catch (...) {
            failed = true;
        }
        CloseHandle(lock);
        CHECK(failed && std::filesystem::exists(first) && app.requested == first && !app.removed_current);
        std::filesystem::remove(last);
        app.scan_folder(first.parent_path());
        app.settings.next_after_fileop = true;
        app.settings.confirm_delete = false;
        command(FileDelete);
        CHECK(!app.image && app.files.empty());
    }
    void direct_delete_permanent_routing_confirmation_failure_and_document_guards() {
        const auto first = copy(L"alpha.png", L"direct/a.png");
        const auto second = copy(L"alpha.png", L"direct/b.png");
        open(first);
        const auto original = app.image;
        const auto rgba = original->frames[0].pixels;
        app.settings.recycle_deleted = true;
        app.settings.confirm_delete = true;
        app.settings.next_after_fileop = false;
        std::vector<std::pair<std::filesystem::path, bool>> deletions;
        app.dialogs.delete_file = [&](HWND owner, const auto &path, bool recycle) {
            CHECK(owner == app.hwnd && path.parent_path() == first.parent_path());
            deletions.emplace_back(path, recycle);
            // Observe ordinary recycle routing, then remove only our fixture without filling the user bin.
            delete_file_checked(owner, path, false);
        };
        answers.push_back(IDNO);
        command(FileDeleteDirect);
        CHECK(deletions.empty() && std::filesystem::exists(first) && app.image == original);
        answers.push_back(IDYES);
        command(FileDeleteDirect);
        CHECK(deletions.size() == 1 && deletions[0].first == first && !deletions[0].second);
        CHECK(!std::filesystem::exists(first) && app.settings.recycle_deleted && app.image->path.empty());
        CHECK(app.removed_current && app.image->frames[0].pixels == rgba);
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == rgba);
        command(FileDeleteDirect);
        CHECK(deletions.size() == 1);
        open(second);
        answers.push_back(IDYES);
        command(FileDelete);
        CHECK(deletions.size() == 2 && deletions[1].second && app.settings.recycle_deleted);
        app.dialogs.delete_file = delete_file_checked;
        const auto locked_file = copy(L"alpha.png", L"direct/locked.png");
        open(locked_file);
        const auto before_failure = app.image;
        app.settings.confirm_delete = false;
        const auto handle = CreateFileW(locked_file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(handle != INVALID_HANDLE_VALUE);
        bool failed = false;
        try { app.command(FileDeleteDirect); } catch (...) { failed = true; }
        CloseHandle(handle);
        CHECK(failed && std::filesystem::exists(locked_file) && app.image == before_failure);
        CHECK(same_path(app.image->path, locked_file) && !app.removed_current && app.settings.recycle_deleted);
        const auto advance_first = copy(L"alpha.png", L"direct-advance/a.png");
        const auto advance_second = copy(L"alpha.png", L"direct-advance/b.png");
        open(advance_first);
        app.settings.next_after_fileop = true;
        command(FileDeleteDirect);
        wait();
        CHECK(!std::filesystem::exists(advance_first) && same_path(app.image->path, advance_second));
        command(Copy);
        command(NewPaste);
        command(FileDeleteDirect);
        CHECK(std::filesystem::exists(advance_second) && app.image->path.empty());
        CHECK(app.image->frames[0].pixels == rgba && app.settings.recycle_deleted);
        const auto zip = copy(L"images.zip", L"direct-archive/images.zip");
        answers.push_back(IDNO); // The audit fixture enables optional unsaved confirmation.
        open(zip);
        const auto archive_pixels = app.image->frames[0].pixels;
        command(FileDeleteDirect);
        CHECK(std::filesystem::exists(zip) && app.image->frames[0].pixels == archive_pixels);
        command(CloseImage);
        command(FileDeleteDirect);
        CHECK(!app.image && app.settings.recycle_deleted);
    }
    void cache_policy_applies_without_replacing_image_and_close_releases_retained_files() {
        const auto a = copy(L"alpha.png", L"cache/a.png");
        copy(L"rgb.bmp", L"cache/b.bmp");
        copy(L"disposal.gif", L"cache/c.gif");
        open(a);
        CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
        const auto original = app.image;
        const auto rgba = original->frames[0].pixels;
        CHECK(app.loader->stats().cache_bytes >= original->bytes());
        auto settings = app.settings;
        settings.cache_mib = 0;
        settings.prefetch_enabled = false;
        app.apply_settings(settings);
        CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
        CHECK(app.image == original && app.image->frames[0].pixels == rgba);
        CHECK(app.loader->stats().cache_bytes == 0 && app.loader->stats().cache_limit == 0);
        settings.cache_mib = 1;
        settings.prefetch_enabled = true;
        app.apply_settings(settings);
        CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
        CHECK(app.image == original && app.image->frames[0].pixels == rgba);
        CHECK(app.loader->stats().cache_bytes > 0 && app.loader->stats().cache_bytes <= 1024 * 1024);
        const auto hits = app.loader->stats().cache_hits;
        command(Next);
        wait();
        CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
        CHECK(app.loader->stats().cache_hits > hits && app.image->path.filename() == L"b.bmp");
        command(CloseImage);
        CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
        CHECK(!app.image && !app.undo_image && app.loader->stats().cache_bytes == 0);
        CHECK(app.loader->stats().foreground_bytes == 0);
    }
    void unusable_clipboard_is_silent_before_cancelling_pending_edits() {
        app.settings.confirm_unsaved=false;app.replace(selection_fixture(48,32),false,true);command(Negative);
        const auto original=app.image,undo=app.undo_image;const auto source=selection_fixture(12,8);
        copy_image(app.hwnd,source,0);command(PasteSelection);CHECK(app.floating);
        const auto floating_source=app.floating->source;const auto notices_before=notices.size();
        clipboard_text(L"ordinary text, not an image");const auto sequence=GetClipboardSequenceNumber();
        for(auto id:{Paste,NewPaste,PasteSelection,NewWindowPaste}){command(id);CHECK(app.floating&&app.floating->source==floating_source&&app.image==original&&app.undo_image==undo&&app.modified);}
        CHECK(GetClipboardSequenceNumber()==sequence&&notices.size()==notices_before&&new_windows.empty());
        command(CancelSelection);command(ToolCrop);CHECK(app.cropping);const auto crop=app.cropping->box;
        for(auto id:{Paste,NewPaste,PasteSelection,NewWindowPaste}){command(id);CHECK(app.cropping&&app.cropping->box.cx==crop.cx&&app.cropping->box.cy==crop.cy&&app.image==original&&app.undo_image==undo);}
        command(CancelSelection);
        CHECK(OpenClipboard(app.hwnd));CHECK(EmptyClipboard());const auto broken=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,8);CHECK(broken);CHECK(SetClipboardData(RegisterClipboardFormatW(L"PNG"),broken));CHECK(CloseClipboard());
        for(auto id:{Paste,NewPaste,PasteSelection,NewWindowPaste}){command(id);CHECK(app.image==original&&app.undo_image==undo&&app.modified&&!app.floating&&!app.loading);}
        CHECK(notices.size()==notices_before&&new_windows.empty());command(CloseImage);CHECK(!app.image);
        const POINT point{100,app.top+100};app.message(WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(point.x,point.y));app.message(WM_LBUTTONUP,0,MAKELPARAM(point.x,point.y));
        CHECK(!app.image&&!app.loading&&notices.size()==notices_before);
        copy_image(app.hwnd,source,0);command(Paste);CHECK(app.image&&app.image->frames[0].pixels==source.frames[0].pixels);
    }
    void empty_canvas_tap_pastes_rgba_or_path_and_restores_status_parts() {
        app.settings.confirm_unsaved = false;
        app.update();
        CHECK(!app.image && status(0).find(L"タップするとクリップボード") != std::wstring::npos);
        CHECK(SendMessageW(app.status, SB_GETPARTS, 0, 0) == 1);
        const auto source = load_image(fixture(L"alpha.png"));
        copy_image(app.hwnd, source, 0);
        const POINT point{100, app.top + 100};
        const auto tap = [&] {
            app.message(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
            app.message(WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
        };
        app.message(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        CHECK(!app.image); // Paste only after a completed tap.
        app.message(WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
        CHECK(app.image && app.image->frames[0].pixels == source.frames[0].pixels && app.modified);
        CHECK(SendMessageW(app.status, SB_GETPARTS, 0, 0) == 5);
        const auto pasted = app.image;
        tap();
        CHECK(app.image == pasted); // An ordinary click on a displayed image never pastes again.
        command(CloseImage);
        CHECK(!app.image && SendMessageW(app.status, SB_GETPARTS, 0, 0) == 1);
        const auto path = copy(L"alpha.png", L"tap/日本語 画像.png");
        clipboard_text(L"\"" + path.wstring() + L"\"");
        tap();wait();
        CHECK(app.image && same_path(app.image->path, path) && !app.modified);
        CHECK(app.image->frames[0].pixels == source.frames[0].pixels);
    }
    void empty_canvas_tap_ignores_drag_cancel_outside_and_pending_image() {
        const auto source = load_image(fixture(L"alpha.png"));
        copy_image(app.hwnd, source, 0);
        const POINT point{100, app.top + 100};
        const auto down = [&] { app.message(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y)); };
        const auto up = [&] { app.message(WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y)); };
        down();app.message(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(point.x + 40, point.y));up();
        CHECK(!app.image);
        down();app.message(WM_CANCELMODE, 0, 0);up();CHECK(!app.image);
        down();app.message(WM_LBUTTONUP, 0, MAKELPARAM(point.x, app.top - 1));CHECK(!app.image);
        app.loading = true;down();up();CHECK(!app.image);app.loading = false;
        app.message(WM_LBUTTONDOWN, MK_LBUTTON | MK_CONTROL, MAKELPARAM(point.x, point.y));up();
        CHECK(!app.image);
        down();open(fixture(L"display-alpha.png"));const auto opened = app.image;up();
        CHECK(app.image == opened && !app.modified);
    }
    void clipboard_full_paths_open_files_and_keep_alpha() {
        const auto first = copy(L"alpha.png", L"画像 フォルダ/透過 画像.PNG");
        const auto source = load_image(first);
        clipboard_text(L" \r\n\"" + first.wstring() + L"\"\r\n ");
        command(Paste);
        wait();
        CHECK(app.image && same_path(app.requested, first) && same_path(app.image->path, first));
        CHECK(!app.modified && app.image->frames[0].pixels == source.frames[0].pixels);
        CHECK(app.folder == first.parent_path() && app.files.size() == 1);
        command(SelectAll);
        app.settings.paste_new = false;
        for (const auto name : {L"psd-16-3.psd", L"disposal.gif", L"blend.apng"}) {
            const auto next = copy(name, name);
            clipboard_text(next.wstring());
            command(Paste);
            wait();
            const auto expected = load_image(next);
            CHECK(same_path(app.requested, next) && !app.modified);
            CHECK(app.image->frames.size() == expected.frames.size());
            CHECK(app.image->frames[0].pixels == expected.frames[0].pixels);
        }
        clipboard_text(first.wstring());
        command(NewPaste);
        wait();
        copy_image(app.hwnd, source, 0);
        clipboard_text((temporary / L"missing.png").wstring(), false);
        command(NewPaste);
        CHECK(app.requested.empty() && app.image->path.empty());
        CHECK(app.image->frames[0].pixels == source.frames[0].pixels);
    }
    void clipboard_path_cancel_invalid_and_failed_load_preserve_document() {
        const auto first = copy(L"alpha.png", L"first.png");
        const auto next = copy(L"page1.png", L"next.png");
        open(first);
        command(Negative);
        const auto edited = app.image->frames[0].pixels;
        clipboard_text(L"\"" + next.wstring() + L"\"");
        answers.push_back(IDCANCEL);
        command(Paste);
        CHECK(app.modified && app.image->frames[0].pixels == edited && same_path(app.requested, first));
        CHECK(!app.loading);
        answers.push_back(IDNO);
        command(Paste);
        wait();
        CHECK(same_path(app.requested, next) && !app.modified);
        const auto current = app.image;
        const auto folder = app.folder;
        const auto before_notices = notices.size();
        clipboard_text((temporary / L"missing.png").wstring());
        command(Paste);
        wait();
        CHECK(app.image == current && app.folder == folder && same_path(app.requested, next));
        CHECK(notices.size() == before_notices);
        const auto corrupt = temporary / L"corrupt.png";
        std::ofstream(corrupt) << "not an image";
        clipboard_text(corrupt.wstring());
        command(Paste);
        wait();
        CHECK(app.image == current && same_path(app.requested, next));
        for (const auto &invalid : {L"", L"relative.png", L"C:relative.png", L"https://example.org/a.png",
                                    L"C:\\first.png\r\nC:\\second.png", L"C:\\unsupported.exe"}) {
            clipboard_text(invalid);
            app.command(Paste);
            CHECK(app.image == current && same_path(app.requested, next) && !app.loading);
        }
        CHECK(notices.size()==before_notices);
        app.open(corrupt);wait();CHECK(notices.size()==before_notices+1); // Explicit opens still report real errors.
        CHECK(read_file(first) == read_file(fixture(L"alpha.png")));
    }
    void viewer_defaults_skip_unsaved_prompt_and_zoom_wheel() {
        app.settings.confirm_unsaved = Settings{}.confirm_unsaved;
        CHECK(!app.settings.confirm_unsaved);
        const auto first = copy(L"display-alpha.png", L"a.png");
        const auto next = copy(L"alpha.png", L"b.png");
        open(first);
        command(Actual);
        POINT cursor{100, app.top + 100};
        ClientToScreen(app.hwnd, &cursor);
        const auto at = MAKELPARAM(cursor.x, cursor.y);
        app.message(WM_MOUSEWHEEL, MAKEWPARAM(0, 120), at);
        CHECK(app.view.scale > 1 && app.index == 0 && !app.loading);
        app.message(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-120)), at);
        CHECK(std::abs(app.view.scale - 1) < 1e-8 && app.index == 0);
        command(Negative);
        CHECK(app.modified);
        const auto before = notices.size();
        open(next);
        CHECK(notices.size() == before && same_path(app.requested, next) && !app.modified);
        command(Negative);
        command(CloseImage);
        CHECK(!app.image && notices.size() == before);
        CHECK(read_file(first) == read_file(fixture(L"display-alpha.png")));
        CHECK(read_file(next) == read_file(fixture(L"alpha.png")));
    }
    void bottom_export_drag_original_edited_clipboard_and_animation() {
        skitch_bottom_export_moves_status_icon_names_formats_and_ole_payload();
        app.settings.confirm_unsaved=false;open(fixture(L"disposal.gif"));command(FrameNext);app.player.playing=true;
        app.dialogs.file_drag=[&](const auto &path){CHECK(!app.player.playing);CHECK(load_image(path).frames[0].pixels==app.image->frames[app.player.frame].pixels);std::filesystem::remove(path);};
        app.begin_file_drag();CHECK(app.player.playing);
    }
    void bitfield_loading_toggle_reloads_cached_file_and_archive_but_preserves_edits_clipboard_and_alpha() {
        CHECK(Settings{}.bitfield_to24&&!LoadingOptions{}.bitfield_to24);app.settings.confirm_unsaved=false;app.settings.bitfield_to24=true;
        const auto path=copy(L"auto-bmp/rgb565.bmp",L"source/rgb565.bmp");const auto bytes=read_file(path);open(path);CHECK(!app.image->rgb16&&!app.modified);const auto reference=load_image(fixture(L"bitfield-loading/on-rgb565.png"));CHECK(app.image->frames[0].pixels==reference.frames[0].pixels);
        auto next=app.settings;next.bitfield_to24=false;app.apply_settings(next);wait();CHECK(app.image->rgb16&&!app.modified);CHECK(encode_for_save(*app.image,0,SaveFormat::Bmp,{}).at(28)==16);
        next.bitfield_to24=true;app.apply_settings(next);wait();CHECK(!app.image->rgb16&&app.image->frames[0].pixels==reference.frames[0].pixels);CHECK(encode_for_save(*app.image,0,SaveFormat::Bmp,{}).at(28)==24);command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==reference.frames[0].pixels);
        command(Negative);const auto edited=app.image;next.bitfield_to24=false;app.apply_settings(next);CHECK(app.image==edited&&app.modified&&!app.loading);command(Undo);CHECK(app.image->frames[0].pixels==reference.frames[0].pixels);
        const auto archive=copy(L"bitfield-loading/one565.zip",L"source/one565.zip");next.bitfield_to24=true;app.apply_settings(next);open(archive);CHECK(!app.image->rgb16&&app.image->frames[0].pixels==reference.frames[0].pixels);next.bitfield_to24=false;app.apply_settings(next);wait();CHECK(app.image->rgb16&&!app.modified);
        const auto alpha=load_image(fixture(L"loading/alpha-v5.bmp"));copy_image(app.hwnd,alpha,0);command(NewPaste);const auto clipboard=app.image;next.bitfield_to24=true;app.apply_settings(next);CHECK(app.image==clipboard&&!app.loading&&app.image->frames[0].pixels==alpha.frames[0].pixels);command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==alpha.frames[0].pixels);CHECK(read_file(path)==bytes);
        Settings saved;saved.load(app.config);CHECK(saved.bitfield_to24);
    }
    void loading_options_reload_without_changing_clipboard_or_edited_images() {
        const auto path = fixture(L"loading/rgba-background.png");
        open(path);
        const auto original = app.image->frames[0].pixels;
        auto options = app.settings;
        options.preserve_alpha = false;
        options.transparency_background = 0x123456;
        app.apply_settings(options);
        wait();
        CHECK(app.image->frames[0].pixels[2] == Pixel{18, 52, 86, 255});
        options.preserve_alpha = true;
        app.apply_settings(options);
        wait();
        CHECK(app.image->frames[0].pixels == original);
        command(Negative);
        const auto edited = app.image;
        options.preserve_alpha = false;
        app.apply_settings(options);
        CHECK(app.image == edited && app.modified && !app.loading);
        command(Undo);
        copy_image(app.hwnd, load_image(path), 0);
        command(NewPaste);
        CHECK(app.image->frames[0].pixels == original && app.requested.empty());
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == original);
        options.png_transparency = false;
        app.apply_settings(options);
        CHECK(app.image->frames[0].pixels == original && !app.loading);
    }
    void loading_animation_orientation_archive_and_attribute_settings() {
        open(fixture(L"loading/animated-background.gif"));
        CHECK(app.image->frames.size() == 2);
        auto options = app.settings;
        options.gif_animation = false;
        options.animation_autoplay = false;
        app.apply_settings(options);
        wait();
        // The audit owner is hidden; inspect the player's own visible style.
        CHECK(app.image->frames.size() == 1 &&
              !(GetWindowLongPtrW(app.playback.hwnd, GWL_STYLE) & WS_VISIBLE));
        options.gif_animation = true;
        app.apply_settings(options);
        wait();
        CHECK(app.image->frames.size() == 2 &&
              (GetWindowLongPtrW(app.playback.hwnd, GWL_STYLE) & WS_VISIBLE) && !app.player.playing);
        open(fixture(L"loading/exif-6.jpg"));
        CHECK(app.image->width == 2 && app.image->height == 3);
        options.exif_orientation = false;
        app.apply_settings(options);
        wait();
        CHECK(app.image->width == 3 && app.image->height == 2);
        options.preserve_alpha = false;
        options.transparency_background = 0x123456;
        app.apply_settings(options);
        wait();
        open(fixture(L"images.zip"));
        CHECK(app.image->frames[0].pixels[2] == Pixel{18, 52, 86, 255});
        options.preserve_alpha = true;
        app.apply_settings(options);
        wait();
        CHECK(app.image->frames[0].pixels[2].a == 0);
        const auto normal = copy(L"alpha.png", L"attributes/0.png");
        const auto hidden = copy(L"alpha.png", L"attributes/1.png");
        const auto system = copy(L"alpha.png", L"attributes/2.png");
        const auto protected_file = copy(L"alpha.png", L"attributes/3.png");
        CHECK(SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_HIDDEN));
        CHECK(SetFileAttributesW(system.c_str(), FILE_ATTRIBUTE_SYSTEM));
        CHECK(SetFileAttributesW(protected_file.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
        open(normal);
        CHECK(app.files.size() == 1);
        options.hidden = true;
        app.apply_settings(options);
        CHECK(app.files.size() == 2);
        options.system_files = true;
        app.apply_settings(options);
        CHECK(app.files.size() == 3);
        options.protected_files = true;
        app.apply_settings(options);
        CHECK(app.files.size() == 4);
        options.hidden = options.system_files = false;
        app.apply_settings(options);
        CHECK(app.files.size() == 2);
        CHECK(same_path(app.files.back().path, protected_file));
    }
    void about_version_preserves_empty_edited_alpha_and_animation_document() {
        CHECK(!app.image && GetMenuState(app.main_menu, About, MF_BYCOMMAND) != static_cast<UINT>(-1));
        command(About);
        CHECK(!app.image && notices.size() == 1 && notices.back().find(version_string) != std::wstring::npos);
        CHECK(notices.back().find(build_architecture) != std::wstring::npos);
        open(fixture(L"alpha.png"));
        command(Negative);
        const auto edited = app.image, undo = app.undo_image;
        CHECK(app.modified && undo);
        command(About);
        CHECK(app.image == edited && app.undo_image == undo && app.modified && notices.size() == 2);
        command(Undo);
        CHECK(app.image == undo);
        open(fixture(L"disposal.gif"));
        app.player.playing = false;
        command(FrameNext);
        const auto animation = app.image;
        const auto frame = app.player.frame;
        command(About);
        CHECK(app.image == animation && app.player.frame == frame && !app.player.playing && notices.size() == 3);
        CHECK(answers.empty());
    }
    void resize_dialog_cancel_commit_preferences_alpha_and_animation_undo() {
        app.settings.confirm_unsaved=false;
        open(fixture(L"alpha.png"));command(Negative);
        const auto before=app.image,old_undo=app.undo_image;
        resizes.push_back(std::nullopt);command(Resize);
        CHECK(app.image==before && app.undo_image==old_undo && app.modified);
        CHECK(app.settings.resize_keep_aspect && app.settings.resize_smooth);
        resizes.push_back(ResizeChoice{6,4,false,false});command(Resize);
        CHECK(app.image!=before && app.image->width==6 && app.image->height==4 && app.undo_image==before);
        CHECK(app.image->frames[0].pixels[1]==before->frames[0].pixels[0] &&
              app.image->frames[0].pixels[5]==before->frames[0].pixels[2] &&
              app.image->frames[0].pixels[23]==before->frames[0].pixels[5]);
        Settings saved;saved.load(app.config);
        CHECK(!saved.resize_keep_aspect && !saved.resize_smooth && !saved.nearest);
        command(Undo);CHECK(app.image==before && app.modified);
        open(fixture(L"disposal.gif"));
        const auto animation=app.image;app.player.frame=1;app.player.playing=true;
        resizes.push_back(std::nullopt);command(Resize);
        CHECK(app.image==animation && app.player.frame==1 && app.player.playing);
        resizes.push_back(ResizeChoice{animation->width,animation->height,true,true});answers.push_back(IDOK);
        command(Resize);
        CHECK(app.image!=animation && !app.image->animated() && app.image->path.empty());
        CHECK(app.image->frames[0].pixels==animation->frames[1].pixels && app.undo_player.frame==1);
        command(Undo);
        CHECK(app.image==animation && app.player.frame==1 && app.player.playing);
        CHECK(app.settings.resize_keep_aspect && app.settings.resize_smooth && answers.empty() && resizes.empty());
    }
    void resize_filters_route_saved_choices_and_commit_only_on_success() {
        app.settings.confirm_unsaved=false;open(fixture(L"alpha.png"));
        const auto original=app.image;
        resizes.push_back(ResizeChoice{6,4,false,true,10,7,true});command(Resize);
        const auto expected=resize_image(*original,6,4,ResizeFilters{10,7,true});
        CHECK(app.image->frames[0].pixels==expected.frames[0].pixels&&app.undo_image==original);
        Settings saved;saved.load(app.config);
        CHECK(saved.resize_down==10&&saved.resize_up==7&&saved.resize_sharpen&&!saved.resize_keep_aspect);
        command(Undo);CHECK(app.image==original);
        resizes.push_back(std::nullopt);command(Resize);
        CHECK(app.image==original&&app.settings.resize_down==10&&app.settings.resize_up==7&&app.settings.resize_sharpen);
        app.editing.allocating=[](size_t){throw std::bad_alloc();};
        resizes.push_back(ResizeChoice{2,1,false,true,2,3,false});THROWS(command(Resize));
        saved.load(app.config);CHECK(app.image==original&&saved.resize_down==10&&saved.resize_up==7&&saved.resize_sharpen);
        app.editing.allocating={};
        resizes.push_back(ResizeChoice{2,1,false,true,2,3,false});command(Resize);
        CHECK(app.image->frames[0].pixels==resize_image(*original,2,1,ResizeFilters{2,3,false}).frames[0].pixels);
        saved.load(app.config);CHECK(saved.resize_down==2&&saved.resize_up==3&&!saved.resize_sharpen);
        command(Undo);CHECK(app.image==original&&resizes.empty());
    }
    void canvas_options_cancel_failure_alpha_commit_and_animation_undo() {
        app.settings.confirm_unsaved=false;open(fixture(L"alpha.png"));const auto original=app.image;
        canvases.push_back(std::nullopt);command(Canvas);CHECK(app.image==original&&!app.modified);
        canvases.push_back(CanvasChoice{5,4,false,8,false,0x11273d,33});command(Canvas);
        CHECK(app.image->frames[0].pixels[0]==Pixel{17,39,61,33}&&app.image->frames[0].pixels[12]==original->frames[0].pixels[0]);
        Settings saved;saved.load(app.config);CHECK(saved.canvas_anchor==8&&!saved.canvas_transparent&&saved.canvas_alpha==33&&saved.canvas_color==0x11273d);
        command(Undo);CHECK(app.image==original);
        app.editing.allocating=[](size_t){throw std::bad_alloc();};canvases.push_back(CanvasChoice{5,4,true,0,true,0xffffff,255});THROWS(command(Canvas));
        saved.load(app.config);CHECK(app.image==original&&saved.canvas_anchor==8&&!saved.canvas_transparent&&saved.canvas_alpha==33);
        app.editing.allocating={};open(fixture(L"disposal.gif"));const auto animation=app.image;app.player.frame=1;app.player.playing=true;
        canvases.push_back(std::nullopt);command(Canvas);CHECK(app.image==animation&&app.player.playing&&app.player.frame==1);
        canvases.push_back(CanvasChoice{animation->width+1,animation->height+1,false,0,true});answers.push_back(IDOK);command(Canvas);
        CHECK(!app.image->animated()&&app.image->path.empty()&&app.image->frames[0].pixels[0]==animation->frames[1].pixels[0]);
        command(Undo);CHECK(app.image==animation&&app.player.playing&&app.player.frame==1&&canvases.empty()&&answers.empty());
    }
    void resolution_only_uses_document_overlay_preserves_pixels_animation_and_undo() {
        app.settings.confirm_unsaved=false;open(fixture(L"metadata/rgba-300x150.png"));
        const auto original=app.image;const auto metadata=original->metadata;const auto pixels=original->frames[0].pixels.data();
        ResizeChoice choice{3,2};choice.resample=false;choice.dpi_x=144;choice.dpi_y=288;choice.print_unit=4;choice.density_unit=1;
        app.editing.allocating=[](size_t){throw std::bad_alloc();};
        resizes.push_back(choice);command(Resize);
        CHECK(app.image==original&&app.image->frames[0].pixels.data()==pixels&&original->metadata==metadata&&app.modified);
        CHECK(app.current_metadata().dpi_x==144&&app.current_metadata().dpi_y==288&&app.current_metadata().dpi_defined);
        CHECK(app.undo_image==original&&!app.undo_modified&&app.copied_image().metadata.dpi_x==144);
        command(Info);CHECK(notices.back().find(L"144.000 × 288.000 DPI")!=std::wstring::npos);
        Settings saved;saved.load(app.config);CHECK(!saved.resize_resample&&saved.resize_print_unit==4&&saved.resize_density_unit==1);
        command(Undo);CHECK(app.image==original&&!app.modified&&app.current_metadata()==metadata);
        command(Redo);CHECK(app.current_metadata().dpi_x==144&&app.modified);
        resizes.push_back(std::nullopt);command(Resize);CHECK(app.current_metadata().dpi_x==144);
        auto invalid=choice;invalid.dpi_x=0;resizes.push_back(invalid);THROWS(command(Resize));
        CHECK(app.image==original&&app.current_metadata().dpi_x==144&&app.undo_metadata==std::nullopt);
        app.editing.allocating={};command(Negative);CHECK(app.image!=original&&app.image->metadata.dpi_x==144&&app.image->metadata.dpi_y==288);
        command(Undo);CHECK(app.image==original&&app.current_metadata().dpi_x==144);
        command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==original->frames[0].pixels);
        open(fixture(L"disposal.gif"));const auto animation=app.image;const auto animation_metadata=animation->metadata;
        app.player.frame=1;app.player.playing=true;app.selection={0,0,1,1};
        choice.width=animation->width;choice.height=animation->height;resizes.push_back(choice);command(Resize);
        CHECK(app.image==animation&&app.image->animated()&&app.player.frame==1&&app.player.playing&&app.selection.w==1);
        CHECK(app.current_metadata().dpi_y==288&&animation->metadata==animation_metadata&&answers.empty());
        command(Undo);CHECK(app.image==animation&&app.current_metadata()==animation_metadata&&app.player.frame==1&&app.player.playing);
        command(Redo);CHECK(app.current_metadata().dpi_x==144);
        open(fixture(L"alpha.png"));CHECK(!app.image_metadata&&app.current_metadata()==app.image->metadata);
        app.settings.undo_enabled=false;choice.width=3;choice.height=2;resizes.push_back(choice);command(Resize);
        CHECK(!app.undo_image&&!app.undo_metadata&&app.current_metadata().dpi_x==144&&resizes.empty());
    }
    void resolution_save_clipboard_drag_and_animation_frame_use_document_metadata() {
        app.settings.confirm_unsaved=false;const auto source=copy(L"alpha.png",L"resolution/source.png");const auto disk=read_file(source);open(source);
        const auto original=app.image;const auto rgba=original->frames[0].pixels;ResizeChoice choice{3,2};choice.resample=false;choice.dpi_x=100.5;choice.dpi_y=200.25;
        resizes.push_back(choice);command(Resize);CHECK(app.image==original&&app.current_metadata().dpi_x==100.5);
        const auto inspect=[&](const Image &value){CHECK(value.metadata.dpi_defined&&std::abs(value.metadata.dpi_x-100.5)<.013&&std::abs(value.metadata.dpi_y-200.25)<.013);};
        command(Copy);const auto clipboard=paste_image(app.hwnd);inspect(clipboard);CHECK(clipboard.frames[0].pixels==rgba);
        std::filesystem::path exported;
        app.dialogs.file_drag=[&](const auto &path){exported=path;const auto image=load_image(path);inspect(image);CHECK(image.frames[0].pixels==rgba);};
        app.begin_file_drag();CHECK(!exported.empty()&&exported!=source&&std::filesystem::remove(exported));
        const auto png=temporary/L"resolution/out.png";app.settings.save_as_retarget=false;paths.push_back(png);CHECK(app.save(true));inspect(load_image(png));
        CHECK(app.image==original&&app.modified&&load_image(png).frames[0].pixels==rgba&&!original->metadata.dpi_defined);
        const auto bmp=temporary/L"resolution/out.bmp";app.settings.save_as_retarget=true;paths.push_back(bmp);answers.push_back(IDOK);CHECK(app.save(true));inspect(load_image(bmp));
        CHECK(!app.modified&&same_path(app.image->path,bmp)&&app.current_metadata().dpi_x==100.5&&read_file(source)==disk);
        command(Undo);CHECK(app.current_metadata().dpi_x==72&&app.modified);command(Redo);CHECK(app.current_metadata().dpi_x==100.5&&!app.modified);
        open(fixture(L"disposal.gif"));const auto animation=app.image;app.player.frame=1;app.player.playing=false;choice.width=animation->width;choice.height=animation->height;
        resizes.push_back(choice);command(Resize);const auto frame=temporary/L"resolution/frame.png";paths.push_back(frame);CHECK(app.save(true));
        const auto saved=load_image(frame);inspect(saved);CHECK(saved.frames[0].pixels==animation->frames[1].pixels&&app.image==animation&&app.image->animated()&&app.player.frame==1);
        CHECK(answers.empty()&&paths.empty()&&resizes.empty());
    }
    void new_window_send_and_paste_preserve_effective_dpi_alpha_undo_and_animation_frame() {
        app.settings.confirm_unsaved=false;open(fixture(L"alpha.png"));const auto original=app.image;
        ResizeChoice choice{original->width,original->height};choice.resample=false;choice.dpi_x=144.5;choice.dpi_y=288.25;resizes.push_back(choice);command(Resize);const auto undo=app.undo_image;
        command(NewWindowSend);const auto &sent=std::get<Image>(new_windows.back());
        CHECK(sent.metadata.dpi_defined&&sent.metadata.dpi_x==144.5&&sent.metadata.dpi_y==288.25&&sent.frames[0].pixels==original->frames[0].pixels&&sent.path.empty());
        command(NewWindowPaste);const auto &pasted=std::get<Image>(new_windows.back());
        CHECK(pasted.metadata.dpi_defined&&std::abs(pasted.metadata.dpi_x-144.5)<.02&&std::abs(pasted.metadata.dpi_y-288.25)<.02&&pasted.frames[0].pixels==original->frames[0].pixels);
        CHECK(app.image==original&&app.undo_image==undo&&app.modified);
        open(fixture(L"disposal.gif"));const auto animation=app.image;app.player.frame=1;app.player.playing=false;choice.width=animation->width;choice.height=animation->height;resizes.push_back(choice);command(Resize);command(NewWindowSend);
        const auto &frame=std::get<Image>(new_windows.back());CHECK(frame.frames.size()==1&&frame.frames[0].pixels==animation->frames[1].pixels&&frame.metadata.dpi_defined&&frame.metadata.dpi_x==144.5&&frame.metadata.dpi_y==288.25);
        CHECK(app.image==animation&&app.image->animated()&&app.player.frame==1&&!app.player.playing&&app.modified);
    }
    void selection_numeric_dialog_preloads_current_preserves_alpha_crop_undo_and_animation() {
        app.settings.confirm_unsaved=false;open(fixture(L"selection-units.png"));command(Negative);const auto source=app.image,undo=app.undo_image;const auto rgba=source->frames[0].pixels;
        app.selection={8,8,24,20};app.settings.selection_custom=true;app.settings.custom_selection_units={8,4,6,5};
        app.dialogs.selection_rect=[&](HWND owner,uint32_t w,uint32_t h,Rect current)->std::optional<Rect>{CHECK(owner==app.hwnd&&w==96&&h==80&&current.x==8&&current.y==8&&current.w==24&&current.h==20);return {};};
        command(SelectNumbers);CHECK(app.image==source&&app.undo_image==undo&&app.selection.w==24&&app.modified);
        app.dialogs.selection_rect=[&](HWND,uint32_t,uint32_t,Rect)->std::optional<Rect>{return Rect{12,10,18,17};};command(SelectNumbers);
        CHECK(app.selection.x==12&&app.selection.y==10&&app.selection.w==18&&app.selection.h==17&&app.image==source&&app.undo_image==undo);
        const auto expected=crop(*source,app.selection);command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==expected.frames[0].pixels);
        command(Crop);CHECK(app.image->frames[0].pixels==expected.frames[0].pixels);command(Undo);CHECK(app.image==source&&source->frames[0].pixels==rgba);
        open(fixture(L"disposal.gif"));const auto animation=app.image;app.player.frame=1;app.player.playing=false;
        app.dialogs.selection_rect=[&](HWND,uint32_t,uint32_t,Rect)->std::optional<Rect>{return Rect{0,0,1,1};};command(SelectNumbers);
        CHECK(app.image==animation&&app.image->animated()&&app.player.frame==1&&!app.player.playing&&!app.modified);
    }
    void selection_numeric_dialog_ignores_result_after_displayed_document_changes() {
        app.settings.confirm_unsaved=false;open(fixture(L"selection-units.png"));const auto original=app.image;const auto rgba=original->frames[0].pixels;std::shared_ptr<Image> replacement;
        app.dialogs.selection_rect=[&](HWND,uint32_t w,uint32_t h,Rect)->std::optional<Rect>{CHECK(w==96&&h==80);open(fixture(L"display-alpha.png"));replacement=app.image;app.selection={0,0,1,1};return Rect{12,10,18,17};};
        command(SelectNumbers);CHECK(app.image==replacement&&replacement!=original&&app.selection.x==0&&app.selection.y==0&&app.selection.w==1&&app.selection.h==1&&original->frames[0].pixels==rgba);
    }
    void selection_units_custom_presets_persist_cancel_and_preserve_document() {
        app.settings.confirm_unsaved=false;open(fixture(L"selection-units.png"));command(Negative);const auto image=app.image,undo=app.undo_image;const auto rgba=image->frames[0].pixels;
        app.selection={3,4,8,9};app.settings.save(app.config);const auto config=read_file(app.config);selection_units.push_back(std::nullopt);command(Grid);CHECK(read_file(app.config)==config&&app.selection.w==8);
        selection_units.push_back(SelectionUnits{8,4,6,5});command(Grid);Settings saved;saved.load(app.config);
        CHECK(saved.selection_custom&&(saved.selection_units()==SelectionUnits{8,4,6,5})&&app.checked(Grid));
        for(int n=0;n<6;++n){const int values[]{1,2,4,8,16,32};command(GridFree+n);saved.load(app.config);CHECK((!saved.selection_custom&&saved.grid==values[n]&&saved.custom_selection_units==SelectionUnits{8,4,6,5}));CHECK(app.checked(GridFree+n));CHECK((GetMenuState(app.main_menu,GridFree+n,MF_BYCOMMAND)&MF_CHECKED)!=0);}
        selection_units.push_back(saved.custom_selection_units);command(Grid);CHECK(app.settings.selection_custom);
        CHECK(app.image==image&&app.undo_image==undo&&app.modified&&image->frames[0].pixels==rgba&&app.selection.x==3&&app.selection.w==8&&selection_units.empty());
    }
    void selection_pointer_matches_reference_reverse_edge_and_alpha_copy_crop_undo() {
        app.settings.confirm_unsaved=false;open(fixture(L"selection-units.png"));const auto original=app.image;const auto rgba=original->frames[0].pixels;
        selection_units.push_back(SelectionUnits{8,4,6,5});command(Grid);command(SelectMode);command(Actual);
        const auto drag=[&](int x1,int y1,int x2,int y2){const auto at=[&](int x,int y){return MAKELPARAM(app.left+int(std::floor((std::abs(app.view.scale-1)<1e-9?std::floor(app.view.left()):app.view.left())+(x+.5)*app.view.scale)),app.top+int(std::floor((std::abs(app.view.scale-1)<1e-9?std::floor(app.view.top()):app.view.top())+(y+.5)*app.view.scale)));};app.message(WM_LBUTTONDOWN,MK_LBUTTON,at(x1,y1));app.message(WM_MOUSEMOVE,MK_LBUTTON,at(x2,y2));app.message(WM_LBUTTONUP,0,at(x2,y2));};
        for(bool reverse:{false,true}){command(ClearSelection);drag(reverse?30:13,reverse?27:11,reverse?13:30,reverse?11:27);CHECK(app.selection.x==8&&app.selection.y==8&&app.selection.w==24&&app.selection.h==20);}
        command(Copy);const auto copied=std::get<Image>(paste_clipboard(app.hwnd));const auto expected=crop(*original,{8,8,24,20});CHECK(copied.width==24&&copied.height==20&&copied.frames[0].pixels==expected.frames[0].pixels);
        command(Crop);CHECK(app.image->frames[0].pixels==expected.frames[0].pixels&&app.undo_image==original);command(Undo);CHECK(app.image==original&&original->frames[0].pixels==rgba);
        drag(90,74,110,100);CHECK(app.selection.x==88&&app.selection.y==72&&app.selection.w==6&&app.selection.h==5);
        drag(95,79,96,80);CHECK(app.selection.empty());
        open(fixture(L"disposal.gif"));const auto animation=app.image;app.player.frame=1;app.player.playing=false;selection_units.push_back(SelectionUnits{1,1,1,1});command(Grid);command(Grid8);
        CHECK(app.image==animation&&app.player.frame==1&&!app.player.playing&&!app.modified);
    }
    void image_info_save_retarget_and_undo_report_the_actual_saved_file_format() {
        app.settings.confirm_unsaved=false;app.settings.save_as_retarget=true;open(fixture(L"info/rgb.png"));const auto original=app.image;
        ResizeChoice choice{original->width,original->height};choice.resample=false;choice.dpi_x=144;choice.dpi_y=288;resizes.push_back(choice);command(Resize);
        command(Negative);const auto target=temporary/L"saved-info.bmp";paths.push_back(target);CHECK(app.save(true));command(Info);
        const auto inspect=[&]{const auto bytes=std::filesystem::file_size(target);CHECK(notices.back().find(L"ファイル形式: BMP")!=std::wstring::npos&&notices.back().find(std::to_wstring(bytes)+L" bytes")!=std::wstring::npos);};inspect();
        CHECK(app.image->path==target&&app.current_metadata().dpi_x==144&&original->metadata.source_format==L"PNG");
        command(Undo);command(Info);inspect();CHECK(app.image->path==target&&app.current_metadata().dpi_x==144);
        command(Redo);command(Info);inspect();CHECK(paths.empty()&&answers.empty());
    }
    void image_info_preserves_source_format_bytes_and_load_time_after_editing() {
        app.settings.confirm_unsaved=false;open(fixture(L"palette/png-4-alpha.png"));
        const auto original=app.image;original->metadata.source_load_ms=77;
        command(Negative);CHECK(app.image!=original&&app.image->format==L"RGBA");command(Info);
        const auto bytes=std::filesystem::file_size(fixture(L"palette/png-4-alpha.png"));
        CHECK(notices.back().find(L"ファイル形式: PNG")!=std::wstring::npos&&notices.back().find(std::to_wstring(bytes)+L" bytes")!=std::wstring::npos&&notices.back().find(L"読み込み時間: 77 ms")!=std::wstring::npos);
        command(Undo);command(Info);CHECK(notices.back().find(L"読み込み時間: 77 ms")!=std::wstring::npos);
        open(temporary/L"missing-source.png"); // Failed open must keep the displayed source information.
        CHECK(app.image==original);command(Info);CHECK(notices.back().find(L"ファイル形式: PNG")!=std::wstring::npos);
    }
    void print_command_uses_immutable_current_frame_and_effective_dpi_without_changing_document() {
        app.settings.confirm_unsaved=false;open(fixture(L"alpha.png"));const auto original=app.image;size_t calls{};
        ResizeChoice choice{original->width,original->height};choice.resample=false;choice.dpi_x=144;choice.dpi_y=288;resizes.push_back(choice);command(Resize);
        app.dialogs.print=[&](HWND owner,const Image &i,size_t frame,const ImageMetadata *m){++calls;CHECK(owner==app.hwnd&&&i==original.get()&&frame==0&&m&&m->dpi_x==144&&m->dpi_y==288);};
        const auto undo=app.undo_image;command(Print);CHECK(calls==1&&app.image==original&&app.undo_image==undo&&app.modified);
        open(fixture(L"disposal.gif"));const auto animation=app.image;app.player.frame=1;app.player.playing=false;
        app.dialogs.print=[&](HWND,const Image &i,size_t frame,const ImageMetadata *m){++calls;CHECK(&i==animation.get()&&frame==1&&m);};
        command(Print);CHECK(calls==2&&app.image==animation&&app.player.frame==1&&!app.player.playing&&!app.modified);
    }
    void image_info_origin_color_type_survives_metadata_overlay_and_pixel_edits() {
        app.settings.confirm_unsaved=false;open(fixture(L"palette/png-4-alpha.png"));const auto original=app.image;
        command(Info);CHECK(notices.back().find(L"4 BPP (パレット)")!=std::wstring::npos);
        ResizeChoice choice{original->width,original->height};choice.resample=false;choice.dpi_x=144;choice.dpi_y=288;resizes.push_back(choice);command(Resize);
        CHECK(app.image==original&&app.current_metadata().original_color_type==ImageColorType::Palette);
        command(Negative);const auto edited=app.image;command(Info);CHECK(notices.back().find(L"4 BPP (パレット)")!=std::wstring::npos&&notices.back().find(L"RGBA 32 BPP")!=std::wstring::npos);
        command(Undo);CHECK(app.image==original&&app.current_metadata().dpi_x==144&&app.current_metadata().original_color_type==ImageColorType::Palette);
        command(Redo);CHECK(app.image==edited&&app.current_metadata().original_color_type==ImageColorType::Palette);
        open(fixture(L"loading/alpha-v4.bmp"));command(Info);CHECK(notices.back().find(L"32 BPP (ビットフィールド)")!=std::wstring::npos);
        open(fixture(L"metadata/gray16-100x50.tiff"));command(Info);CHECK(notices.back().find(L"16 BPP (グレースケール)")!=std::wstring::npos);
        open(fixture(L"info/cmyk.tiff"));command(Info);CHECK(notices.back().find(L"32 BPP (CMYK)")!=std::wstring::npos);
        CHECK(resizes.empty());
    }
    void image_info_fields_preferences_and_counter_failure_preserve_document() {
        app.settings.confirm_unsaved=false;copy(L"info/rgb.png",L"info/01.png");const auto path=copy(L"info/rgba-low.png",L"info/02.png");copy(L"info/palette-unused.png",L"info/03.png");open(path);
        const auto original=app.image;const auto rgba=original->frames[0].pixels;
        app.dialogs.image_info=[&](HWND owner,const ImageInfoData &data,bool automatic,const ImageInfoDialog::Counter &counter){
            CHECK(owner==app.hwnd&&data.image==original&&!automatic);const auto text=data.text();
            const auto normalized=std::filesystem::absolute(path).lexically_normal();
            CHECK(text.find(normalized.wstring())!=std::wstring::npos);CHECK(text.find(L"フォルダ内の順番: 2 / 3")!=std::wstring::npos);
            CHECK(text.find(L"ファイルの場所: "+normalized.parent_path().wstring())!=std::wstring::npos&&text.find(L"更新日時: —")==std::wstring::npos);
            CHECK(counter({})==3);return true;
        };
        command(Info);Settings saved;saved.load(app.config);CHECK(saved.info_auto_colors&&app.image==original&&!app.modified&&app.image->frames[0].pixels==rgba);
        command(Negative);const auto edited=app.image,undo=app.undo_image;app.editing.allocating=[](size_t){throw std::bad_alloc();};
        app.dialogs.image_info=[&](HWND,const ImageInfoData &data,bool automatic,const ImageInfoDialog::Counter &counter){CHECK(data.image==edited&&automatic);THROWS(counter({}));return false;};
        command(Info);saved.load(app.config);CHECK(!saved.info_auto_colors&&app.image==edited&&app.undo_image==undo&&app.modified&&original->frames[0].pixels==rgba);
        app.editing.allocating={};command(Copy);command(NewPaste);
        app.dialogs.image_info=[&](HWND,const ImageInfoData &data,bool automatic,const ImageInfoDialog::Counter &){CHECK(data.image==app.image&&data.text().find(L"フルパス: （未保存）")!=std::wstring::npos&&data.text().find(L"フォルダ内の順番: —")!=std::wstring::npos);return automatic;};
        command(Info);open(fixture(L"disposal.gif"));const auto animation=app.image;app.player.frame=1;app.player.playing=true;app.settings.info_auto_colors=true;
        app.dialogs.image_info=[&](HWND,const ImageInfoData &data,bool automatic,const ImageInfoDialog::Counter &){CHECK(data.image==animation&&data.image->animated()&&automatic);return automatic;};
        command(Info);CHECK(app.image==animation&&app.player.frame==1&&app.player.playing&&app.settings.info_auto_colors);
    }
    void image_info_reports_source_depth_dpi_profile_and_retains_undo_origin() {
        app.settings.confirm_unsaved=false;open(fixture(L"metadata/rgba-300x150.png"));const auto original=app.image;
        command(Info);CHECK(app.image==original&&notices.back().find(L"299.999")!=std::wstring::npos&&notices.back().find(L"埋め込みあり")!=std::wstring::npos);
        canvases.push_back(CanvasChoice{5,4,false,0,true});command(Canvas);const auto edited=app.image;
        command(Info);CHECK(app.image==edited&&app.undo_image==original&&notices.back().find(L"読み込み時: 3 × 2")!=std::wstring::npos&&notices.back().find(L"5 × 4")!=std::wstring::npos);
        command(Undo);CHECK(app.image==original&&app.image->metadata.dpi_x>299.9);
        open(fixture(L"metadata/rgb-300x150.bmp"));command(Info);CHECK(notices.back().find(L"元の色深度: 24 BPP")!=std::wstring::npos);
        open(fixture(L"alpha.png"));command(Info);CHECK(notices.back().find(L"未指定のため72 DPI")!=std::wstring::npos);
    }
    void native_controls_and_menu() {
        CHECK(SendMessageW(app.toolbar.hwnd, TB_BUTTONCOUNT, 0, 0) == 27);
        CHECK(!SendMessageW(app.toolbar.hwnd, TB_ISBUTTONENABLED, Save, 0));
        CHECK(!GetMenu(app.hwnd));
        paths.push_back(fixture(L"alpha.png"));
        command(Open);
        wait();
        CHECK(app.image);
        CHECK(SendMessageW(app.toolbar.hwnd, TB_ISBUTTONENABLED, Save, 0));
        command(ToggleMenu);
        CHECK(GetMenu(app.hwnd));
        command(ToggleMenu);
        CHECK(!GetMenu(app.hwnd));
        const auto top = app.top, bottom = app.bottom;
        command(ToggleToolbar);
        CHECK(app.top < top);
        command(ToggleToolbar);
        CHECK(app.top == top);
        command(ToggleStatus);
        CHECK(app.bottom == 0); // Export controls are embedded in the hidden status bar.
        command(ToggleStatus);
        CHECK(app.bottom == bottom);
    }
    void navigation_and_sorting() {
        auto first = copy(L"alpha.png", L"pages/2.png");
        auto second = copy(L"display-alpha.png", L"pages/10.png");
        open(first);
        CHECK(app.index == 0 && app.files.size() == 2);
        command(Next);
        wait();
        CHECK(same_path(app.image->path, second) && app.index == 1);
        command(First);
        wait();
        CHECK(app.index == 0);
        command(Last);
        wait();
        CHECK(app.index == 1);
        command(Next);
        CHECK(!app.loading && app.index == 1);
        command(ListLoop);
        command(Next);
        wait();
        CHECK(app.index == 0);
        command(Previous);
        wait();
        CHECK(app.index == 1);
        command(SortReverse);
        CHECK(app.index == 0);
        command(SortReverse);
        CHECK(app.index == 1);
        command(SortSize);
        CHECK(app.files.front().bytes <= app.files.back().bytes);
        command(SortTime);
        CHECK(app.files.front().time <= app.files.back().time);
        command(SortExtension);
        CHECK(app.files.size() == 2);
        command(SortRandom);
        CHECK(app.files.size() == 2);
        command(SortName);
        command(NaturalSort);
        CHECK(app.files.front().path.filename() == L"10.png");
        auto hidden = copy(L"alpha.png", L"pages/hidden.png");
        SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_HIDDEN);
        command(RefreshList);
        CHECK(app.files.size() == 2);
        command(ShowHidden);
        CHECK(app.files.size() == 3);
        jumps.push_back(JumpChoice{0, app.files.front().path, {true, 1}});
        command(Jump);
        wait();
        CHECK(app.index == 0);
        SetFileAttributesW(hidden.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    void jump_commits_only_accept_and_keeps_cancelled_modified_image() {
        const auto first = copy(L"alpha.png", L"jump/a.png"), second = copy(L"rgb.bmp", L"jump/b.bmp");
        open(first);
        command(Negative);
        const auto edited = app.image;
        jumps.push_back(std::nullopt);
        command(Jump);
        CHECK(app.image == edited && app.modified && !app.loading && app.settings.jump_preview &&
              app.settings.jump_quality == 1);
        CHECK(jump_options.back() == (JumpOptions{true, 1}));
        jumps.push_back(JumpChoice{1, second, {false, 2}});
        answers.push_back(IDNO);
        command(Jump);
        wait();
        CHECK(same_path(app.image->path, second) && !app.modified && !app.settings.jump_preview &&
              app.settings.jump_quality == 2);
        Settings saved;
        saved.load(app.config);
        CHECK(!saved.jump_preview && saved.jump_quality == 2);
        // A malformed boundary result cannot select a different entry or change preferences.
        jumps.push_back(JumpChoice{0, second, {true, 0}});
        command(Jump);
        CHECK(same_path(app.image->path, second) && !app.settings.jump_preview &&
              app.settings.jump_quality == 2);
        jumps.push_back(JumpChoice{99, first, {true, 0}});
        command(Jump);
        CHECK(!app.loading && !app.settings.jump_preview);
        const auto zip = copy(L"images.zip", L"jump/archive.zip");
        open(zip);
        CHECK(app.files.size() > 1 && !app.files[1].display_name.empty());
        const auto member = app.files[1].path;
        jumps.push_back(JumpChoice{1, member, {false, 1}});
        command(Jump);
        wait();
        CHECK(app.index == 1 && same_path(app.requested, member) && !app.archive.empty());
    }
    void jump_cancels_pending_load_and_honors_all_comic_direction_flags() {
        const auto first = copy(L"alpha.png", L"jump/a.png"), second = copy(L"rgb.bmp", L"jump/b.bmp");
        open(first);
        const auto original = app.image;
        app.open(second);
        CHECK(app.loading);
        jumps.push_back(std::nullopt);
        command(Jump);
        wait();
        CHECK(app.image == original && same_path(app.requested, first) && !app.loading);
        app.settings.jump_preview = false;
        app.settings.jump_quality = 2;
        for (bool comic : {false, true})
            for (bool rtl : {false, true})
                for (bool reversed : {false, true}) {
                    app.comic = comic;
                    app.settings.comic_rtl = rtl;
                    app.settings.comic_jump_reverse = reversed;
                    jumps.push_back(std::nullopt);
                    command(Jump);
                    CHECK(jump_directions.back() == (comic && rtl && reversed));
                    CHECK(jump_options.back() == (JumpOptions{false, 2}));
                    CHECK(app.image == original);
                }
    }
    void jump_changed_document_does_not_apply_old_choice_or_settings() {
        const auto first = copy(L"alpha.png", L"jump/a.png"), second = copy(L"rgb.bmp", L"jump/b.bmp");
        open(first);
        app.dialogs.jump = [&](HWND, std::span<const FileEntry>, size_t, bool, JumpOptions, LoadingOptions,
                               std::shared_ptr<JumpPreview>) -> std::optional<JumpChoice> {
            open(second);
            return JumpChoice{0, first, {false, 0}};
        };
        command(Jump);
        CHECK(same_path(app.image->path, second) && app.settings.jump_preview &&
              app.settings.jump_quality == 1);
        CHECK(!app.loading);
    }
    void jump_retires_busy_preview_without_blocking_main_loader_or_accumulating_services() {
        const auto first = copy(L"alpha.png", L"preview-retirement/a.png"),
                   second = copy(L"rgb.bmp", L"preview-retirement/b.bmp");
        open(first);
        CHECK(!app.preview_service);
        std::promise<void> entered, release;
        const auto gate = release.get_future().share();
        std::once_flag flag;
        const auto unblock = [&] { std::call_once(flag, [&] { release.set_value(); }); };
        struct Cleanup {
            std::function<void()> run;
            ~Cleanup() {
                run();
            }
        } cleanup{[&] {
            unblock();
            app.preview_service.reset();
        }};
        app.preview_service = std::make_shared<JumpPreview>([&](const auto &path) {
            entered.set_value();
            gate.wait();
            return load_image(path);
        });
        const auto weak = std::weak_ptr<JumpPreview>(app.preview_service);
        auto inbox = app.preview_service->mailbox();
        app.preview_service->request(first, app.settings.loading_options(), inbox);
        auto started = entered.get_future();
        CHECK(started.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
        jumps.push_back(std::nullopt);
        command(Jump);
        CHECK(app.preview_service && !app.preview_service->idle());
        SendMessageW(app.hwnd, WM_TIMER, kPreviewCleanupTimer, 0);
        CHECK(app.preview_service && !weak.expired());
        // Main decoding uses its own existing loader and remains usable while preview is held.
        open(second);
        CHECK(same_path(app.image->path, second) && !app.preview_service->idle());
        app.preview_service->cancel(inbox);
        unblock();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (app.preview_service && !app.preview_service->idle() &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        CHECK(app.preview_service && app.preview_service->idle());
        SendMessageW(app.hwnd, WM_TIMER, kPreviewCleanupTimer, 0);
        CHECK(!app.preview_service && weak.expired());
    }
    void explicit_open_returns_before_directory_listing_and_keeps_source_until_adoption() {
        const auto first = copy(L"alpha.png", L"open-responsive/a/1.png");
        const auto second = copy(L"rgb.bmp", L"open-responsive/b/2.bmp");
        open(first);
        const auto original = app.image;
        const auto original_files = app.files;
        app.settings.prefetch_enabled = false;
        app.loader->configure(0, false);
        CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
        std::promise<void> entered, release;
        auto started = entered.get_future();
        auto gate = release.get_future().share();
        std::atomic<bool> released{}, forced{};
        std::once_flag once;
        const auto unblock = [&] {
            std::call_once(once, [&] {
                released = true;
                release.set_value();
            });
        };
        struct Cleanup {
            App &app;
            std::function<void()> unblock;
            ~Cleanup() {
                unblock();
                app.loader->wait_idle(std::chrono::seconds(3));
                app.open_lister = {};
            }
        } cleanup{app, unblock};
        app.open_lister = [&](const auto &directory, auto visibility, const auto &) {
            entered.set_value();
            gate.wait();
            return list_files(directory, visibility);
        };
        std::jthread watchdog([&] {
            if (gate.wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
                forced = true;
                unblock();
            }
        });
        const auto begin = std::chrono::steady_clock::now();
        app.open(second);
        const auto elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        const bool returned_before_listing = !released.load();
        const bool worker_entered = started.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
        const bool source_intact = app.image == original && same_path(app.folder, first.parent_path()) &&
                                   same_path(app.requested, first) &&
                                   app.files.size() == original_files.size();
        command(ZoomIn);
        pump();
        const bool ui_returned_while_blocked = !released.load();
        unblock();
        watchdog.join();
        wait();
        std::cout << "CHECKPOINT explicit_open returned_before_listing=" << returned_before_listing
                  << " elapsed_ms=" << elapsed << '\n';
        CHECK(returned_before_listing && worker_entered && source_intact && ui_returned_while_blocked &&
              !forced);
        CHECK(app.image != original && same_path(app.requested, second) && app.image->width == 3);
        CHECK(app.files.size() == 1 && same_path(app.files[0].path, second));
    }
    void explicit_open_adopts_worker_order_and_metadata_without_a_second_scan() {
        const auto a = copy(L"alpha.png", L"open-snapshot/a/1.png");
        const auto b1 = copy(L"alpha.png", L"open-snapshot/b/1.png");
        const auto b2 = copy(L"rgb.bmp", L"open-snapshot/b/2.bmp");
        open(a);
        app.open(b1);
        CHECK(app.pending_open && app.before_load && same_path(app.requested, a));
        auto result = std::make_unique<Loaded>();
        result->sequence = app.loader->sequence();
        result->path = b2;
        result->open_match = OpenMatch{};
        result->open_match->path = b2;
        result->open_match->folder = b2.parent_path();
        result->open_match->files = {{b2, 20, 200}, {b1, 10, 100}};
        copy(L"alpha.png", L"open-snapshot/b/3.png");
        app.accept(std::move(result));
        wait();
        CHECK(same_path(app.requested, b2) && app.files.size() == 2 && app.index == 0);
        CHECK(app.files[0].bytes == 20 && app.files[1].time == 100 && !app.pending_open);
        command(Next);
        wait();
        CHECK(same_path(app.requested, b1) && app.index == 1 && app.files.size() == 2);
    }
    void sort_visibility_and_loading_settings_cancel_pending_open_snapshot() {
        const auto a = copy(L"alpha.png", L"open-policy/a/1.png");
        const auto b = copy(L"rgb.bmp", L"open-policy/b/2.bmp");
        open(a);
        const auto original = app.image;
        for (const auto id : {SortName, SortReverse, NaturalSort, ShowHidden}) {
            app.open(b);
            CHECK(app.pending_open);
            const auto old = app.loader->sequence();
            command(id);
            CHECK(!app.pending_open && !app.before_load && !app.loading);
            auto stale = std::make_unique<Loaded>();
            stale->sequence = old;
            stale->open_match = prepare_open(OpenQuery{b});
            app.accept(std::move(stale));
            CHECK(app.image == original && same_path(app.requested, a));
        }
        app.open(b);
        auto settings = app.settings;
        settings.preserve_alpha = !settings.preserve_alpha;
        app.apply_settings(settings);
        wait();
        CHECK(!app.pending_open && same_path(app.requested, a));
    }
    void failed_load_keeps_document_and_location() {
        auto original = copy(L"alpha.png", L"first/a.png");
        auto invalid = temporary / L"other/b.png";
        std::filesystem::create_directory(invalid.parent_path());
        std::ofstream(invalid) << "corrupt image";
        open(original);
        auto pixels = app.image->frames[0].pixels;
        app.open(invalid);
        wait();
        CHECK(app.image->frames[0].pixels == pixels);
        CHECK(same_path(app.requested, original));
        CHECK(same_path(app.folder, original.parent_path()));
        CHECK(app.files.size() == 1 && same_path(app.files[0].path, original));
        CHECK(!notices.empty());
    }
    void empty_folder_keeps_location() {
        auto original = copy(L"alpha.png", L"first/a.png");
        open(original);
        auto empty = temporary / L"empty";
        std::filesystem::create_directory(empty);
        app.open(empty);
        wait();
        CHECK(same_path(app.folder, original.parent_path()));
        CHECK(same_path(app.requested, original));
        CHECK(app.files.size() == 1);
    }
    void cancelled_open_keeps_edit() {
        open(copy(L"alpha.png", L"a.png"));
        command(Negative);
        auto changed = app.image;
        answers.push_back(IDCANCEL);
        app.open(fixture(L"page1.png"));
        CHECK(app.image == changed && app.modified && !app.loading);
    }
    void undo_after_save_is_dirty() {
        auto original = copy(L"alpha.png", L"a.png");
        open(original);
        auto pixels = app.image->frames[0].pixels;
        command(Negative);
        answers.push_back(IDYES);
        command(Save);
        CHECK(!app.modified && load_image(original).frames[0].pixels != pixels);
        command(Undo);
        CHECK(app.image->frames[0].pixels == pixels);
        CHECK(app.modified);
    }
    void clipboard_new_document_undo_identity() {
        auto original = copy(L"alpha.png", L"a.png");
        open(original);
        const auto pasted = load_image(fixture(L"page1.png"));
        copy_image(app.hwnd, pasted, 0);
        command(NewPaste);
        CHECK(app.image->width == 120 && app.requested.empty());
        command(Undo);
        CHECK(app.image->width == 3);
        CHECK(same_path(app.requested, original));
        CHECK(app.current_name == L"a.png" && app.files.size() == 1);
        command(Redo);
        CHECK(app.image->width == 120 && app.requested.empty());
    }
    void clipboard_selection_cut_paste_alpha() {
        open(fixture(L"alpha.png"));
        const auto source = app.image->frames[0].pixels;
        command(SelectAll);
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == source);
        command(ClearSelection);
        texts.push_back(L"1, 0, 1, 1");
        command(SelectNumbers);
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels[0] == source[1]);
        command(Cut);
        CHECK(app.image->frames[0].pixels[1] == Pixel{});
        CHECK(app.image->frames[0].pixels[0] == source[0]);
        command(Undo);
        CHECK(app.image->frames[0].pixels == source);
        texts.push_back(L"1, 0, 1, 1");
        command(SelectNumbers);
        command(Paste);
        CHECK(app.image->frames[0].pixels[1] == over(source[1], source[1]));
        CHECK(app.image->frames[0].pixels[0] == source[0]);
    }
    void configured_cut_color_preserves_clipboard_alpha_and_outside_pixels() {
        Settings proposed = app.settings;
        proposed.confirm_unsaved = false;
        proposed.save(app.config);
        CHECK(WritePrivateProfileStringW(L"Viewer", L"cut_transparent", L"0", app.config.c_str()));
        CHECK(WritePrivateProfileStringW(L"Viewer", L"cut_color", L"1193046", app.config.c_str()));
        proposed.load(app.config);
        app.apply_settings(proposed);
        const auto file = copy(L"alpha.png", L"cut/source.png");
        open(file);
        const auto original = app.image->frames[0].pixels;
        app.selection = {1, 0, 1, 2};
        command(Cut);
        CHECK(app.image->frames[0].pixels[1] == Pixel{18, 52, 86, 255});
        CHECK(app.image->frames[0].pixels[4] == Pixel{18, 52, 86, 255});
        for (const size_t index : {0u, 2u, 3u, 5u})
            CHECK(app.image->frames[0].pixels[index] == original[index]);
        const auto copied = paste_image(app.hwnd);
        CHECK(copied.width == 1 && copied.height == 2 &&
              copied.frames[0].pixels == std::vector<Pixel>{original[1], original[4]});
        CHECK(app.modified && app.requested == file && app.image->path == file);
        CHECK(load_image(file).frames[0].pixels == original);
        command(Undo);
        CHECK(app.image->frames[0].pixels == original && !app.modified);
        command(Redo);
        CHECK(app.image->frames[0].pixels[1] == Pixel{18, 52, 86, 255} && app.modified);
    }
    void cut_transparent_default_and_runtime_switch_do_not_reload_source() {
        CHECK(app.settings.cut_transparent && app.settings.cut_color == 0);
        open(copy(L"alpha.png", L"cut/switch.png"));
        const auto original = app.image->frames[0].pixels;
        const auto document = app.image;
        const auto sequence = GetClipboardSequenceNumber();
        command(Cut);
        CHECK(app.image == document && !app.modified && !app.undo_image &&
              GetClipboardSequenceNumber() == sequence);
        auto proposed = app.settings;
        proposed.cut_color = 0xabcdef;
        app.apply_settings(proposed);
        CHECK(app.image == document && !app.loading);
        app.selection = {1, 0, 1, 1};
        command(Cut);
        CHECK(app.image->frames[0].pixels[1] == Pixel{});
        CHECK(paste_image(app.hwnd).frames[0].pixels == std::vector<Pixel>{original[1]});
        command(Undo);
        proposed.cut_transparent = false;
        const auto restored = app.image;
        app.apply_settings(proposed);
        CHECK(app.image == restored && app.settings.cut_color == 0xabcdef && !app.loading);
        app.selection = {1, 0, 1, 1};
        command(Cut);
        CHECK(app.image->frames[0].pixels[1] == Pixel{171, 205, 239, 255});
    }
    void cut_color_animation_cancel_current_frame_and_palette_undo() {
        app.settings.confirm_unsaved = false;
        app.settings.cut_transparent = false;
        app.settings.cut_color = 0x102030;
        const auto animation = copy(L"disposal.gif", L"cut/animation.gif");
        open(animation);
        app.player.frame = 1;
        app.player.playing = false;
        const auto original = app.image;
        const auto frame = app.image->frames[1].pixels;
        app.selection = {0, 0, 1, 1};
        answers.push_back(IDCANCEL);
        command(Cut);
        CHECK(app.image == original && !app.modified && app.player.frame == 1);
        CHECK(paste_image(app.hwnd).frames[0].pixels == std::vector<Pixel>{frame[0]});
        answers.push_back(IDOK);
        command(Cut);
        CHECK(app.image->frames.size() == 1 && app.image->path.empty() && app.modified);
        CHECK(app.image->frames[0].pixels[0] == Pixel{16, 32, 48, 255});
        for (size_t i = 1; i < frame.size(); ++i)
            CHECK(app.image->frames[0].pixels[i] == frame[i]);
        command(Undo);
        CHECK(app.image == original && app.player.frame == 1 && !app.modified);
        CHECK(load_image(animation).frames[1].pixels == frame);
        const auto palette_file = copy(L"palette/png-4-alpha.png", L"cut/palette.png");
        open(palette_file);
        const auto indexed = app.image->frames[0].indexed;
        const auto pixels = app.image->frames[0].pixels;
        CHECK(indexed && indexed->matches(pixels));
        app.selection = {3, 2, 7, 9};
        const auto expected_clipboard = crop(*app.image, app.selection);
        command(Cut);
        CHECK(!app.image->frames[0].indexed && indexed->matches(pixels));
        CHECK(app.image->frames[0].pixels[2 * app.image->width + 3] == Pixel{16, 32, 48, 255});
        CHECK(paste_image(app.hwnd).frames[0].pixels == expected_clipboard.frames[0].pixels);
        command(Undo);
        CHECK(app.image->frames[0].indexed == indexed && app.image->frames[0].pixels == pixels);
        CHECK(load_image(palette_file).frames[0].pixels == pixels);
    }
    void selection_crop_resize_canvas_and_undo() {
        open(fixture(L"alpha.png"));
        auto original = app.image->frames[0].pixels;
        texts.push_back(L"1, 0, 2, 1");
        command(SelectNumbers);
        command(Crop);
        CHECK(app.image->width == 2 && app.image->height == 1);
        CHECK(app.image->frames[0].pixels[0] == original[1]);
        command(Undo);
        texts.push_back(L"6, 4");
        command(Resize);
        CHECK(app.image->width == 6 && app.image->height == 4);
        command(Undo);
        texts.push_back(L"5, 4");
        command(Canvas);
        CHECK(app.image->frames[0].pixels[6] == original[0]);
        CHECK(app.image->frames[0].pixels[0] == Pixel{});
        command(Undo);
        CHECK(app.image->frames[0].pixels == original);
        texts.push_back(L"8");
        command(Grid);
        CHECK(app.settings.grid == 8);
    }
    void spatial_filter_six_presets_menu_pixels_copy_and_undo_match_native_reference() {
        const UINT ids[]{Sharpen,SharpenStrong,SharpenEdges,Blur,BlurStrong,BlurSoft};const wchar_t *names[]{L"sharpen",L"sharpen-strong",L"sharpen-edges",L"blur",L"blur-strong",L"blur-soft"};
        app.settings.confirm_unsaved=false;open(fixture(L"spatial-filter/colors/source.png"));const auto source=app.image;const auto original=source->frames[0].pixels;
        for(int n=0;n<6;++n){CHECK(GetMenuState(app.main_menu,ids[n],MF_BYCOMMAND)!=UINT(-1)&&std::wstring(command_name(ids[n]))!=L"不明な機能");command(ids[n]);CHECK(app.modified&&app.undo_image==source);const auto expected=load_image(fixture((L"spatial-filter/colors/"+std::wstring(names[n])+L".png").c_str()));
            for(size_t k=0;k<original.size();++k){const auto a=app.image->frames[0].pixels[k],b=expected.frames[0].pixels[k];CHECK(a.a==255&&std::abs(int(a.r)-b.r)<=2&&std::abs(int(a.g)-b.g)<=2&&std::abs(int(a.b)-b.b)<=2);}command(Copy);const auto copied=paste_image(app.hwnd);CHECK(copied.frames[0].pixels==app.image->frames[0].pixels);command(Undo);CHECK(app.image==source&&!app.modified&&app.image->frames[0].pixels==original);}
        app.selection={2,3,11,9};const auto rect=app.selection;app.dialogs.spatial_filter=[&](HWND owner,std::shared_ptr<const Image> image,size_t frame,Rect selection,SpatialFilter choice,const SpatialFilterDialog::Current &current)->std::optional<SpatialFilter>{CHECK(owner==app.hwnd&&image==source&&frame==0&&selection.x==rect.x&&selection.y==rect.y&&selection.w==rect.w&&selection.h==rect.h&&current()&&!choice.sharpen);return SpatialFilter{false,50,100,0,65};};
        command(Gaussian);CHECK(app.selection.x==rect.x&&app.selection.y==rect.y&&app.selection.w==rect.w&&app.selection.h==rect.h&&app.settings.gaussian_radius==50&&app.settings.gaussian_transparency==65);const auto edited=app.image;command(Copy);const auto copied=paste_image(app.hwnd);CHECK(copied.width==uint32_t(rect.w)&&copied.height==uint32_t(rect.h));Settings saved;saved.load(app.config);CHECK(saved.gaussian_radius==50&&saved.gaussian_transparency==65);
        command(Undo);CHECK(app.image==source&&app.image->frames[0].pixels==original&&edited!=source);
    }
    void spatial_filter_animation_cancel_late_result_and_failure_preserve_document() {
        app.settings.confirm_unsaved=false;open(fixture(L"disposal.gif"));const auto source=app.image;CHECK(source->animated());app.player.frame=1;app.player.playing=true;const auto previous=source->frames[1].pixels;
        app.dialogs.spatial_filter=[&](HWND,std::shared_ptr<const Image> image,size_t frame,Rect,SpatialFilter choice,const SpatialFilterDialog::Current &current)->std::optional<SpatialFilter>{CHECK(image==source&&frame==1&&current()&&!app.player.playing&&choice.sharpen);return {};};
        answers.push_back(IDOK);command(Unsharp);CHECK(app.image==source&&app.player.frame==1&&app.player.playing&&!app.modified);
        app.dialogs.spatial_filter=[&](HWND,std::shared_ptr<const Image>,size_t,Rect,SpatialFilter,const SpatialFilterDialog::Current &)->std::optional<SpatialFilter>{return SpatialFilter{true,8,200,0,0};};
        app.editing.allocating=[](size_t){throw std::bad_alloc();};answers.push_back(IDOK);THROWS(app.command(Unsharp));CHECK(app.image==source&&app.player.frame==1&&app.player.playing&&!app.modified);app.editing.allocating={};
        const auto config=app.config;app.config=temporary;answers.push_back(IDOK);THROWS(app.command(Unsharp));app.config=config;CHECK(app.image==source&&app.settings.unsharp_radius==6&&app.player.playing);
        answers.push_back(IDOK);command(Unsharp);CHECK(app.image!=source&&app.image->frames.size()==1&&app.image->path.empty()&&app.undo_image==source&&app.modified);for(size_t n=0;n<previous.size();++n)CHECK(app.image->frames[0].pixels[n].a==previous[n].a);command(Undo);CHECK(app.image==source&&app.player.frame==1&&app.player.playing);
        app.dialogs.spatial_filter=[&](HWND,std::shared_ptr<const Image>,size_t,Rect,SpatialFilter choice,const SpatialFilterDialog::Current &current)->std::optional<SpatialFilter>{open(fixture(L"alpha.png"));CHECK(!current());return choice;};answers.push_back(IDOK);command(Unsharp);CHECK(app.image!=source&&app.image->width==3&&!app.modified);
    }
    void brightness_main_preview_alpha_palette_roi_copy_save_and_single_undo_are_transactional() {
        app.settings.confirm_unsaved=false;open(fixture(L"palette/png-4-alpha.png"));const auto original=app.image;const auto pixels=original->frames[0].pixels;const auto indexed=original->frames[0].indexed;CHECK(indexed);const BrightnessAdjustment parameters{20,35,1250};std::shared_ptr<Image> accepted;unsigned calls{};
        app.dialogs.brightness=[&](HWND owner,BrightnessChoice choice,const BrightnessDialog::Process &process,const BrightnessDialog::Preview &preview,const BrightnessDialog::Current &current)->std::optional<BrightnessResult>{CHECK(owner==app.hwnd&&choice.adjustment.neutral()&&choice.preview&&current()&&!app.player.playing);++calls;accepted=process(parameters);CHECK(accepted&&accepted->frames[0].indexed&&accepted->frames[0].indexed->packed==indexed->packed);preview(accepted);CHECK(app.mask_preview_image==accepted&&app.presented_image()==accepted.get()&&app.image==original&&app.image->frames[0].pixels==pixels);preview({});CHECK(!app.mask_preview_image&&app.image==original);return BrightnessResult{{parameters,false},accepted};};
        command(Brightness);CHECK(calls==1&&app.image==accepted&&app.undo_image==original&&app.modified&&!app.settings.brightness_preview&&!app.mask_preview_image);command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==accepted->frames[0].pixels);const auto output=temporary/L"brightness.png";save_image(*accepted,0,output,SaveOptions{});CHECK(load_image(output).frames[0].pixels==accepted->frames[0].pixels);Settings saved;saved.load(app.config);CHECK(!saved.brightness_preview);command(Undo);CHECK(app.image==original&&!app.modified&&app.image->frames[0].pixels==pixels);
        app.selection={1,1,2,1};const auto rect=app.selection;app.dialogs.brightness=[&](HWND,BrightnessChoice,const BrightnessDialog::Process &process,const BrightnessDialog::Preview &preview,const BrightnessDialog::Current &)->std::optional<BrightnessResult>{accepted=process(parameters);CHECK(!accepted->frames[0].indexed);preview(accepted);return BrightnessResult{{parameters,false},accepted};};command(Brightness);for(int y=0;y<int(original->height);++y)for(int x=0;x<int(original->width);++x){const auto at=size_t(y)*original->width+x;CHECK(app.image->frames[0].pixels[at].a==pixels[at].a);if(y!=1||x<1||x>=3)CHECK(app.image->frames[0].pixels[at]==pixels[at]);}CHECK(app.selection.x==rect.x&&app.selection.y==rect.y&&app.selection.w==rect.w&&app.selection.h==rect.h);command(Undo);CHECK(app.image==original);
    }
    void brightness_cancel_neutral_failure_animation_and_late_source_keep_original_state() {
        app.settings.confirm_unsaved=false;open(fixture(L"disposal.gif"));const auto original=app.image;app.player.frame=1;app.player.playing=true;const auto undo=app.undo_image;const BrightnessAdjustment parameters{-20,-30,750};std::shared_ptr<Image> accepted;
        app.dialogs.brightness=[&](HWND,BrightnessChoice,const BrightnessDialog::Process &process,const BrightnessDialog::Preview &preview,const BrightnessDialog::Current &current)->std::optional<BrightnessResult>{CHECK(current()&&!app.player.playing);accepted=process(parameters);preview(accepted);CHECK(app.mask_preview_frame==1&&app.image==original);return {};};answers.push_back(IDOK);command(Brightness);CHECK(app.image==original&&app.undo_image==undo&&!app.modified&&!app.mask_preview_image&&app.player.frame==1&&app.player.playing&&app.settings.brightness_preview);
        app.dialogs.brightness=[&](HWND,BrightnessChoice choice,const BrightnessDialog::Process &,const BrightnessDialog::Preview &,const BrightnessDialog::Current &)->std::optional<BrightnessResult>{return BrightnessResult{choice,{}};};answers.push_back(IDOK);command(Brightness);CHECK(app.image==original&&app.undo_image==undo&&!app.modified&&app.player.playing);
        app.dialogs.brightness=[&](HWND,BrightnessChoice,const BrightnessDialog::Process &process,const BrightnessDialog::Preview &,const BrightnessDialog::Current &)->std::optional<BrightnessResult>{accepted=process(parameters);return BrightnessResult{{parameters,false},accepted};};app.editing.allocating=[](size_t){throw std::bad_alloc();};answers.push_back(IDOK);THROWS(app.command(Brightness));app.editing.allocating={};CHECK(app.image==original&&app.undo_image==undo&&!app.modified&&app.player.playing);const auto config=app.config;app.config=temporary;answers.push_back(IDOK);THROWS(app.command(Brightness));app.config=config;CHECK(app.image==original&&app.settings.brightness_preview&&app.player.playing);
        answers.push_back(IDOK);command(Brightness);CHECK(app.image==accepted&&app.image->frames.size()==1&&app.image->path.empty()&&app.undo_image==original);for(size_t n=0;n<accepted->frames[0].pixels.size();++n)CHECK(accepted->frames[0].pixels[n].a==original->frames[1].pixels[n].a);command(Undo);CHECK(app.image==original&&app.player.frame==1&&app.player.playing);
        app.dialogs.brightness=[&](HWND,BrightnessChoice,const BrightnessDialog::Process &process,const BrightnessDialog::Preview &preview,const BrightnessDialog::Current &current)->std::optional<BrightnessResult>{auto late=process(parameters);open(fixture(L"alpha.png"));CHECK(!current());preview(late);CHECK(!app.mask_preview_image);return BrightnessResult{{parameters,true},late};};answers.push_back(IDOK);command(Brightness);CHECK(app.image!=original&&app.image->width==3&&!app.modified&&!app.mask_preview_image&&!app.settings.brightness_preview);
    }
        void levels_main_preview_alpha_palette_roi_copy_save_and_single_undo_are_transactional() {
        app.settings.confirm_unsaved=false;open(fixture(L"palette/png-4-alpha.png"));const auto original=app.image;const auto pixels=original->frames[0].pixels;const auto indexed=original->frames[0].indexed;CHECK(indexed);LevelsAdjustment parameters;parameters.curves[0]={20,1250,225,10,240};std::shared_ptr<Image> accepted;unsigned calls{};
        app.dialogs.levels=[&](HWND owner,LevelsChoice choice,const LevelsHistogram &histogram,const LevelsDialog::Process &process,const LevelsDialog::Preview &preview,const LevelsDialog::Current &current)->std::optional<LevelsResult>{CHECK(owner==app.hwnd&&choice.adjustment.neutral()&&choice.preview&&current()&&!app.player.playing);CHECK(histogram==levels_histogram(*original));++calls;accepted=process(parameters);CHECK(accepted&&accepted->frames[0].indexed&&accepted->frames[0].indexed->packed==indexed->packed);preview(accepted);CHECK(app.mask_preview_image==accepted&&app.presented_image()==accepted.get()&&app.image==original&&app.image->frames[0].pixels==pixels);preview({});CHECK(!app.mask_preview_image&&app.image==original);return LevelsResult{{parameters,false},accepted};};
        command(Levels);CHECK(calls==1&&app.image==accepted&&app.undo_image==original&&app.modified&&!app.settings.levels_preview&&!app.mask_preview_image);command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==accepted->frames[0].pixels);const auto output=temporary/L"levels.png";save_image(*accepted,0,output,SaveOptions{});CHECK(load_image(output).frames[0].pixels==accepted->frames[0].pixels);Settings saved;saved.load(app.config);CHECK(!saved.levels_preview);command(Undo);CHECK(app.image==original&&!app.modified&&app.image->frames[0].pixels==pixels);
        app.selection={1,1,2,1};const auto rect=app.selection;app.dialogs.levels=[&](HWND,LevelsChoice,const LevelsHistogram &histogram,const LevelsDialog::Process &process,const LevelsDialog::Preview &preview,const LevelsDialog::Current &)->std::optional<LevelsResult>{CHECK(histogram==levels_histogram(*original,rect));accepted=process(parameters);CHECK(!accepted->frames[0].indexed);preview(accepted);return LevelsResult{{parameters,false},accepted};};command(Levels);for(int y=0;y<int(original->height);++y)for(int x=0;x<int(original->width);++x){const auto at=size_t(y)*original->width+x;CHECK(app.image->frames[0].pixels[at].a==pixels[at].a);if(y!=1||x<1||x>=3)CHECK(app.image->frames[0].pixels[at]==pixels[at]);}CHECK(app.selection.x==rect.x&&app.selection.y==rect.y&&app.selection.w==rect.w&&app.selection.h==rect.h);command(Undo);CHECK(app.image==original);
    }
        void levels_cancel_neutral_failure_animation_and_late_source_keep_original_state() {
        app.settings.confirm_unsaved=false;open(fixture(L"disposal.gif"));const auto original=app.image;app.player.frame=1;app.player.playing=true;const auto undo=app.undo_image;LevelsAdjustment parameters;parameters.curves[0]={20,750,225,230,15};std::shared_ptr<Image> accepted;
        app.dialogs.levels=[&](HWND,LevelsChoice,const LevelsHistogram &,const LevelsDialog::Process &process,const LevelsDialog::Preview &preview,const LevelsDialog::Current &current)->std::optional<LevelsResult>{CHECK(current()&&!app.player.playing);accepted=process(parameters);preview(accepted);CHECK(app.mask_preview_frame==1&&app.image==original);return {};};answers.push_back(IDOK);command(Levels);CHECK(app.image==original&&app.undo_image==undo&&!app.modified&&!app.mask_preview_image&&app.player.frame==1&&app.player.playing&&app.settings.levels_preview);
        app.dialogs.levels=[&](HWND,LevelsChoice choice,const LevelsHistogram &,const LevelsDialog::Process &,const LevelsDialog::Preview &,const LevelsDialog::Current &)->std::optional<LevelsResult>{return LevelsResult{choice,{}};};answers.push_back(IDOK);command(Levels);CHECK(app.image==original&&app.undo_image==undo&&!app.modified&&app.player.playing);
        app.dialogs.levels=[&](HWND,LevelsChoice,const LevelsHistogram &,const LevelsDialog::Process &process,const LevelsDialog::Preview &,const LevelsDialog::Current &)->std::optional<LevelsResult>{accepted=process(parameters);return LevelsResult{{parameters,false},accepted};};app.editing.allocating=[](size_t){throw std::bad_alloc();};answers.push_back(IDOK);THROWS(app.command(Levels));app.editing.allocating={};CHECK(app.image==original&&app.undo_image==undo&&!app.modified&&app.player.playing);const auto config=app.config;app.config=temporary;answers.push_back(IDOK);THROWS(app.command(Levels));app.config=config;CHECK(app.image==original&&app.settings.levels_preview&&app.player.playing);
        answers.push_back(IDOK);command(Levels);CHECK(app.image==accepted&&app.image->frames.size()==1&&app.image->path.empty()&&app.undo_image==original);for(size_t n=0;n<accepted->frames[0].pixels.size();++n)CHECK(accepted->frames[0].pixels[n].a==original->frames[1].pixels[n].a);command(Undo);CHECK(app.image==original&&app.player.frame==1&&app.player.playing);
        app.dialogs.levels=[&](HWND,LevelsChoice,const LevelsHistogram &,const LevelsDialog::Process &process,const LevelsDialog::Preview &preview,const LevelsDialog::Current &current)->std::optional<LevelsResult>{auto late=process(parameters);open(fixture(L"alpha.png"));CHECK(!current());preview(late);CHECK(!app.mask_preview_image);return LevelsResult{{parameters,true},late};};answers.push_back(IDOK);command(Levels);CHECK(app.image!=original&&app.image->width==3&&!app.modified&&!app.mask_preview_image&&!app.settings.levels_preview);
    }
    void edits_are_routed_and_undoable() {
        open(fixture(L"alpha.png"));
        auto original = app.image->frames[0].pixels;
        for (auto id :
             {RotateRight, RotateLeft, Rotate180, FlipH,       FlipV,        Negative,   Gray,
              Sepia,       Blur,       Sharpen,   Edge,        Emboss,       AutoLevels, AutoContrast,
              SwapRG,      SwapRB,     SwapGB,    SwapForward, SwapBackward, Xor80,      WinMask}) {
            command(id);
            CHECK(app.modified && app.undo_image);
            command(Undo);
            CHECK(!app.modified && app.image->width == 3 && app.image->frames[0].pixels == original);
        }
        for (auto [id, input] :
             std::initializer_list<std::pair<UINT, const wchar_t *>>{{RotateAngle, L"30"},
                                                                     {Brightness, L"20"},
                                                                     {Contrast, L"1.2"},
                                                                     {Gamma, L"1.5"},
                                                                     {Hue, L"30"},
                                                                     {Saturation, L"0.5"},
                                                                     {Threshold, L"128"},
                                                                     {Posterize, L"4"},
                                                                     {Mosaic, L"2"},
                                                                     {Noise, L"0.2"},
                                                                     {Gaussian, L"1"},
                                                                     {Unsharp, L"1, 100, 0"},
                                                                     {Levels, L"0, 255, 1, 0, 255, 0"},
                                                                     {GlassV, L"2"},
                                                                     {GlassH, L"2"},
                                                                     {ReplaceColor, L"FF0000, 00FF00"}}) {
            if (id == GlassV || id == GlassH)
                app.settings.glass_size = 2; // Official glass commands apply the configured size immediately.
            else
                texts.push_back(input);
            const auto previous_image=app.image,previous_undo=app.undo_image;
            command(id);
            if(id==Levels){
                // Actual reference identityOK leaves pixels and Undo unchanged (0.58 stage4).
                CHECK(!app.modified&&app.image==previous_image&&app.undo_image==previous_undo);
            }else{
                CHECK(app.modified && app.undo_image);
                command(Undo);
            }
            CHECK(app.image->frames[0].pixels == original);
        }
        texts.push_back(L"0, 4");
        THROWS(app.command(Resize));
        CHECK(app.image->frames[0].pixels == original);
        texts.push_back(std::nullopt);
        command(Resize);
        CHECK(app.image->frames[0].pixels == original);
    }

    // Command numbers deliberately derive from the last pre-rotation command. These cases also
    // compile against the unchanged product before the new commands are implemented.
    void view_rotation_commands_match_reference_pixels_and_exclusive_menu_checks() {
        const UINT first_rotation = NextAdjacentFolder + 1;
        for (int shape = 0; shape < 3; ++shape) {
            const auto source = copy(shape == 0 ? L"view-rotation/single/portrait.png"
                                     : shape == 1 ? L"view-rotation/single/landscape.png"
                                                  : L"view-rotation/single/square.png",
                                     L"rotation/source.png");
            const auto bytes = read_file(source);
            open(source);
            for (int mode = 0; mode < 8; ++mode) {
                command(first_rotation + mode);
                wait();
                CHECK(app.settings.view_rotation == mode && !app.modified && !app.undo_image);
                wchar_t name[128]{};
                swprintf_s(name, L"view-rotation/single/expected-%03d.png", shape * 8 + mode);
                const auto expected = load_image(fixture(name));
                CHECK(app.image->width == expected.width && app.image->height == expected.height &&
                      app.image->frames[0].pixels == expected.frames[0].pixels);
                for (int candidate = 0; candidate < 8; ++candidate) {
                    CHECK(app.checked(first_rotation + candidate) == (candidate == mode));
                    const auto state = GetMenuState(app.main_menu, first_rotation + candidate, MF_BYCOMMAND);
                    CHECK(state != static_cast<UINT>(-1) && ((state & MF_CHECKED) != 0) == (candidate == mode));
                }
                command(Copy);
                CHECK(paste_image(app.hwnd).frames[0].pixels == expected.frames[0].pixels);
                CHECK(read_file(source) == bytes && app.requested == source);
            }
            command(CloseImage);
            std::filesystem::remove(source);
        }
    }
    void view_rotation_discards_file_edits_without_prompt_and_clears_undo() {
        const auto source = copy(L"view-rotation/single/portrait.png", L"rotation/edited.png");
        const auto bytes = read_file(source);
        open(source);
        command(RotateRight);
        CHECK(app.modified && app.undo_image && app.settings.confirm_unsaved);
        app.selection = {1, 1, 2, 2};
        command(NextAdjacentFolder + 2); // Display-left, not image-edit-left.
        wait();
        const auto expected = load_image(fixture(L"view-rotation/single/expected-001.png"));
        CHECK(app.settings.view_rotation == 1 && app.image->width == expected.width &&
              app.image->height == expected.height &&
              app.image->frames[0].pixels == expected.frames[0].pixels);
        CHECK(!app.modified && !app.undo_image && app.selection.empty() && notices.empty() && answers.empty());
        const auto displayed = app.image;
        command(Undo);
        CHECK(app.image == displayed && read_file(source) == bytes);
    }
    void view_rotation_option_keeps_clipboard_alpha_selection_and_undo_identity() {
        open(copy(L"alpha.png", L"rotation/clipboard-source.png"));
        command(NextAdjacentFolder + 2);
        wait();
        CHECK(app.settings.view_rotation == 1);
        const auto original = load_image(fixture(L"alpha.png"));
        copy_image(app.hwnd, original, 0);
        command(NewPaste);
        CHECK(app.requested.empty() && app.image->path.empty() &&
              app.image->width == original.width && app.image->height == original.height &&
              app.image->frames[0].pixels == original.frames[0].pixels);
        const auto displayed = app.image, undo = app.undo_image;
        app.selection = {0, 0, 1, 1};
        const auto sequence = GetClipboardSequenceNumber();
        command(NextAdjacentFolder + 3);
        wait();
        CHECK(app.settings.view_rotation == 2 && app.image == displayed && app.undo_image == undo &&
              app.modified && app.selection.x == 0 && app.selection.y == 0 && app.selection.w == 1 &&
              app.selection.h == 1 && notices.empty() &&
              GetClipboardSequenceNumber() == sequence);
        command(ClearSelection);
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == original.frames[0].pixels);
    }
    void comic_wide_single_command_reloads_current_pair_and_preserves_sources() {
        const auto first = copy(L"view-rotation/single/landscape.png", L"rotation/pair/01.png");
        const auto second = copy(L"view-rotation/single/landscape.png", L"rotation/pair/02.png");
        const auto bytes = read_file(first);
        open(first);
        command(Comic);
        wait();
        CHECK(app.image->width == 14 && app.image->height == 5 && app.page_step() == 2);
        const UINT wide_command = NextAdjacentFolder + 9;
        command(wide_command);
        wait();
        CHECK(app.settings.comic_single_wide && app.checked(wide_command) && app.image->width == 7 &&
              app.image->height == 5 && app.page_step() == 1 && app.image->path == first);
        command(wide_command);
        wait();
        CHECK(!app.settings.comic_single_wide && !app.checked(wide_command) && app.image->width == 14 &&
              app.image->height == 5 && app.page_step() == 2);
        CHECK(read_file(first) == bytes && read_file(second) == bytes && notices.empty());
    }
    void view_rotation_same_option_keeps_edits_and_custom_keys_mouse_popup_dispatch() {
        const auto source = copy(L"alpha.png", L"rotation/input.png");
        open(source);
        command(RotateRight);
        const auto edited = app.image, undo = app.undo_image;
        command(ViewRotateNone);
        CHECK(app.image == edited && app.undo_image == undo && app.modified && !app.loading);
        auto next = app.settings;
        next.set_keys(ViewRotateLeft, {L"F12"});
        next.mouse.assign(MouseGesture::MiddleClick, 0, 0, ViewRotateHalf);
        app.apply_settings(next);
        app.message(WM_KEYDOWN, VK_F12, 0);
        wait();
        CHECK(app.settings.view_rotation == 1 && !app.modified);
        const auto left = rotate_quarters(load_image(source), 3);
        CHECK(app.image->frames[0].pixels == left.frames[0].pixels);
        wchar_t label[128]{};
        CHECK(GetMenuStringW(app.main_menu, ViewRotateLeft, label, 128, MF_BYCOMMAND) > 0);
        CHECK(std::wstring(label).find(L"F12") != std::wstring::npos);
        unsigned popups{};
        app.dialogs.popup = [&](HWND, HMENU menu, POINT) {
            CHECK(GetMenuItemCount(menu) == 8 && GetMenuItemID(menu, 0) == ViewRotateNone);
            CHECK((GetMenuState(menu, ViewRotateLeft, MF_BYCOMMAND) & MF_CHECKED) != 0);
            ++popups;
            return static_cast<UINT>(ViewRotateRight);
        };
        command(ViewRotate);
        wait();
        CHECK(popups == 1 && app.settings.view_rotation == 2);
        app.message(WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(100, app.top + 80));
        app.message(WM_MBUTTONUP, 0, MAKELPARAM(100, app.top + 80));
        wait();
        CHECK(app.settings.view_rotation == 7 && notices.empty());
        const auto half = rotate_quarters(load_image(source), 2);
        CHECK(app.image->frames[0].pixels == half.frames[0].pixels);
        Settings persisted;
        app.settings.save(app.config);
        persisted.load(app.config);
        CHECK(persisted.view_rotation == 7 && persisted.first_key(ViewRotateLeft) == L"F12" &&
              persisted.mouse.action(MouseGesture::MiddleClick, 0, 0) == ViewRotateHalf);
    }
    void view_rotation_real_formats_preserve_every_frame_metadata_and_source_bytes() {
        app.settings.animation_autoplay = false;
        const wchar_t *names[]{L"alpha.png",          L"disposal.gif",       L"blend.apng",
                               L"psd-8-2.psd",        L"loading/exif-1.jpg", L"loading/exif-2.jpg",
                               L"loading/exif-3.jpg", L"loading/exif-4.jpg", L"loading/exif-5.jpg",
                               L"loading/exif-6.jpg", L"loading/exif-7.jpg", L"loading/exif-8.jpg"};
        for (const auto name : names) {
            const auto path = copy(name, std::filesystem::path(L"rotation/formats") /
                                             std::filesystem::path(name).filename());
            const auto bytes = read_file(path);
            const auto raw = load_source(path);
            open(path);
            for (const auto [mode, quarters] : {std::pair{1, 3}, {2, 1}, {7, 2}, {0, 0}}) {
                command(ViewRotateNone + mode);
                wait();
                CHECK(app.image->frames.size() == raw.frames.size() && app.image->format == raw.format &&
                      app.image->file_bytes == raw.file_bytes && app.image->file_loops == raw.file_loops &&
                      app.image->path == path && !app.modified && !app.undo_image);
                for (size_t i = 0; i < raw.frames.size(); ++i) {
                    const auto expected = rotate_quarters(still_frame(raw, i), quarters);
                    CHECK(app.image->width == expected.width && app.image->height == expected.height &&
                          app.image->frames[i].pixels == expected.frames[0].pixels &&
                          app.image->frames[i].duration_ms == raw.frames[i].duration_ms);
                }
                app.player.frame = app.image->frames.size() - 1;
                command(Copy);
                CHECK(paste_image(app.hwnd).frames[0].pixels == app.image->frames.back().pixels);
                CHECK(read_file(path) == bytes && !app.player.playing);
            }
        }
    }
    void view_rotation_selection_png_save_and_archive_use_displayed_pixels() {
        const auto source = copy(L"alpha.png", L"rotation/save-source.png");
        const auto bytes = read_file(source);
        open(source);
        command(ViewRotateLeft);
        wait();
        const auto displayed = app.image->frames[0].pixels;
        app.selection = {1, 0, 1, 1};
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == std::vector<Pixel>{displayed[1]});
        command(ClearSelection);
        const auto target = temporary / L"rotation/saved.png";
        paths.push_back(target);
        command(SaveAs);
        const auto saved = load_image(target);
        CHECK(saved.width == 2 && saved.height == 3 && saved.frames[0].pixels == displayed &&
              read_file(source) == bytes);
        command(ViewRotateNone);
        wait();
        open(fixture(L"images.zip"));
        const auto original = *app.image;
        const auto member = app.requested;
        command(ViewRotateRight);
        wait();
        const auto expected = rotate_quarters(original, 1);
        CHECK(!app.archive.empty() && app.requested == member && app.image->path.empty() &&
              app.image->width == expected.width && app.image->height == expected.height &&
              app.image->frames[0].pixels == expected.frames[0].pixels && !app.modified);
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == expected.frames[0].pixels);
    }
    void with_rotation_worker_gate(
        const std::filesystem::path &blocked, bool fail, bool folder_stage,
        const std::function<void(const std::function<void()> &, const std::function<void()> &)> &actions) {
        app.settings.prefetch_enabled = false;
        app.loader->configure(0, false);
        CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
        std::promise<void> entered, release;
        auto started = entered.get_future();
        const auto gate = release.get_future().share();
        std::once_flag entered_once, released_once;
        const auto unblock = [&] { std::call_once(released_once, [&] { release.set_value(); }); };
        const auto hold = [&] {
            std::call_once(entered_once, [&] { entered.set_value(); });
            gate.wait();
        };
        struct Cleanup {
            std::function<void()> run;
            ~Cleanup() { run(); }
        } cleanup{[&] {
            unblock();
            app.loader.reset();
        }};
        const auto callback = [this](Loaded loaded) {
            auto value = std::make_unique<Loaded>(std::move(loaded));
            if (PostMessageW(app.hwnd, kLoaded, 0, reinterpret_cast<LPARAM>(value.get())))
                value.release();
        };
        if (folder_stage)
            app.loader = std::make_unique<Loader>(callback, 0, nullptr,
                [&](const FolderQuery &query, const std::function<bool()> &cancelled) {
                    hold();
                    return find_folder(query, cancelled);
                });
        else
            app.loader = std::make_unique<Loader>(callback, 0, [&](const auto &path) {
                if (same_path(path, blocked)) {
                    hold();
                    if (fail)
                        throw std::runtime_error("Synthetic gated decode failure");
                }
                return load_source(path);
            });
        app.loader->configure(0, false);
        const auto await_entered = [&] {
            const auto deadline = GetTickCount64() + 5000;
            while (started.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                pump();
                CHECK(GetTickCount64() < deadline);
                MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        };
        actions(await_entered, unblock);
    }
    void view_rotation_during_open_discovery_keeps_target_and_uses_last_mode() {
        const auto first = copy(L"alpha.png", L"rotation-race/a/source.png");
        const auto second = copy(L"view-rotation/single/landscape.png", L"rotation-race/b/target.png");
        const auto expected = load_image(fixture(L"view-rotation/single/expected-009.png"));
        app.settings.prefetch_enabled = false;
        app.loader->configure(0, false);
        for (bool has_source : {false, true}) {
            if (has_source) {
                command(ViewRotateNone);
                open(first);
                command(Negative);
            }
            CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
            const auto source = app.image, undo = app.undo_image;
            std::promise<void> entered, release;
            auto started = entered.get_future();
            const auto gate = release.get_future().share();
            std::once_flag entered_once, released_once;
            const auto unblock = [&] { std::call_once(released_once, [&] { release.set_value(); }); };
            struct Cleanup {
                std::function<void()> run;
                ~Cleanup() { run(); }
            } cleanup{[&] {
                unblock();
                app.loader->wait_idle(std::chrono::seconds(3));
                app.open_lister = {};
            }};
            app.open_lister = [&](const auto &directory, auto visibility, const auto &) {
                std::call_once(entered_once, [&] { entered.set_value(); });
                gate.wait();
                return list_files(directory, visibility);
            };
            app.open(second, false, false);
            const auto deadline = GetTickCount64() + 5000;
            while (started.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                pump();
                CHECK(GetTickCount64() < deadline);
                MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
            CHECK(app.pending_open && app.loading);
            const auto sequence = app.loader->sequence();
            command(ViewRotateRight);
            command(ViewRotateHalf);
            command(ViewRotateLeft);
            CHECK(app.pending_open && app.loader->sequence() == sequence && app.image == source &&
                  app.undo_image == undo && app.modified == has_source);
            unblock();
            wait();
            CHECK(app.settings.view_rotation == 1 && app.requested == second &&
                  app.folder == second.parent_path() && app.files.size() == 1 &&
                  app.image->width == expected.width && app.image->height == expected.height &&
                  app.image->frames[0].pixels == expected.frames[0].pixels &&
                  !app.modified && !app.undo_image && notices.empty());
            command(CloseImage);
        }
    }
    void view_rotation_replaces_inflight_decode_and_rejects_older_completion() {
        const auto first = copy(L"alpha.png", L"rotation-race/a/source.png");
        const auto second = copy(L"view-rotation/single/landscape.png", L"rotation-race/b/target.png");
        const auto bytes = read_file(second);
        open(first);
        command(Negative);
        const auto source = app.image;
        with_rotation_worker_gate(second, false, false, [&](const auto &entered, const auto &release) {
            app.open(second, false, false);
            entered();
            CHECK(app.loading && !app.pending_open && app.requested == second && app.image == source);
            const auto stale_sequence = app.loader->sequence();
            command(ViewRotateLeft);
            command(ViewRotateRight);
            CHECK(app.loading && app.loader->sequence() > stale_sequence && app.image == source);
            auto stale = std::make_unique<Loaded>();
            stale->sequence = stale_sequence;
            stale->path = second;
            stale->image = std::make_shared<Image>(load_image(first));
            app.accept(std::move(stale));
            CHECK(app.loading && app.image == source && app.modified && app.requested == second);
            release();
            wait();
            const auto expected = load_image(fixture(L"view-rotation/single/expected-010.png"));
            CHECK(app.image->width == expected.width && app.image->height == expected.height &&
                  app.image->frames[0].pixels == expected.frames[0].pixels &&
                  app.image->path == second && app.requested == second && !app.modified &&
                  !app.undo_image && !app.before_load && notices.empty());
            CHECK(app.loader->wait_idle(std::chrono::seconds(3)) && app.loader->stats().discarded >= 1);
            CHECK(read_file(second) == bytes);
        });
    }
    void view_rotation_failed_pending_decode_restores_edited_source_selection_and_undo() {
        const auto first = copy(L"alpha.png", L"rotation-race/a/source.png");
        const auto second = copy(L"rgb.bmp", L"rotation-race/b/target.bmp");
        const auto first_bytes = read_file(first), second_bytes = read_file(second);
        open(first);
        command(Negative);
        const auto source = app.image, undo = app.undo_image;
        app.selection = {1, 0, 1, 1};
        with_rotation_worker_gate(second, true, false, [&](const auto &entered, const auto &release) {
            app.open(second, false, false);
            entered();
            command(ViewRotateLeft);
            release();
            wait();
            CHECK(app.image == source && app.undo_image == undo && app.modified &&
                  app.requested == first && app.folder == first.parent_path() && app.files.size() == 1 &&
                  app.files[0].path == first && !app.before_load && app.selection.x == 1 &&
                  app.selection.y == 0 && app.selection.w == 1 && app.selection.h == 1);
            CHECK(app.settings.view_rotation == 1 && notices.size() == 1 &&
                  notices.front().find(L"Synthetic gated decode failure") != std::wstring::npos);
            command(ViewRotateLeft);
            CHECK(!app.loading && app.image == source && app.undo_image == undo);
            command(ViewRotateNone);
            wait();
            CHECK(app.image->frames[0].pixels == load_image(first).frames[0].pixels &&
                  !app.modified && !app.undo_image && app.selection.empty() && notices.size() == 1);
            CHECK(read_file(first) == first_bytes && read_file(second) == second_bytes);
        });
    }
    void view_rotation_paste_during_pending_decode_cancels_target_and_keeps_undo_location() {
        const auto first = copy(L"alpha.png", L"rotation-race/a/source.png");
        const auto second = copy(L"rgb.bmp", L"rotation-race/b/target.bmp");
        open(first);
        const auto source = app.image;
        const auto clipboard = load_image(fixture(L"alpha.png"));
        copy_image(app.hwnd, clipboard, 0);
        with_rotation_worker_gate(second, false, false, [&](const auto &entered, const auto &release) {
            app.open(second);
            entered();
            command(ViewRotateLeft);
            command(NewPaste);
            CHECK(!app.loading && app.requested.empty() && app.image->path.empty() &&
                  app.image->width == clipboard.width && app.image->height == clipboard.height &&
                  app.image->frames[0].pixels == clipboard.frames[0].pixels && app.modified);
            const auto pasted = app.image;
            release();
            CHECK(app.loader->wait_idle(std::chrono::seconds(3)));
            pump();
            CHECK(app.image == pasted && app.requested.empty() && !app.loading);
            command(Undo);
            CHECK(app.image == source && app.requested == first && app.folder == first.parent_path() &&
                  app.files.size() == 1 && app.files[0].path == first && !app.modified &&
                  app.settings.view_rotation == 1 && notices.empty());
        });
    }
    void view_rotation_during_folder_search_keeps_request_and_rotates_adopted_file() {
        const auto first = copy(L"alpha.png", L"rotation-race/a/source.png");
        const auto second = copy(L"view-rotation/single/landscape.png", L"rotation-race/b/target.png");
        app.navigation_root = temporary / L"rotation-race";
        open(first);
        const auto source = app.image;
        with_rotation_worker_gate(second, false, true, [&](const auto &entered, const auto &release) {
            command(NextFolder);
            entered();
            const auto sequence = app.loader->sequence();
            CHECK(app.pending_folder && app.loading && app.requested == first);
            command(ViewRotateLeft);
            CHECK(app.pending_folder && app.loader->sequence() == sequence && app.image == source);
            release();
            wait();
            const auto expected = load_image(fixture(L"view-rotation/single/expected-009.png"));
            CHECK(app.requested == second && app.folder == second.parent_path() &&
                  app.image->width == expected.width && app.image->height == expected.height &&
                  app.image->frames[0].pixels == expected.frames[0].pixels &&
                  !app.pending_folder && !app.modified && notices.empty());
        });
    }
    void use_save_copy_policy() {
        auto next = app.settings;
        next.save(app.config);
        CHECK(WritePrivateProfileStringW(L"Viewer", L"save_as_retarget", L"0", app.config.c_str()));
        next.load(app.config);
        app.apply_settings(next);
    }
    void save_as_copy_keeps_edited_original_source_selection_and_undo() {
        use_save_copy_policy();
        const auto source = copy(L"alpha.png", L"save-copy/source.png");
        const auto bytes = read_file(source);
        open(source);
        command(Negative);
        app.selection = {1, 0, 1, 1};
        const auto edited = app.image, undo = app.undo_image;
        const auto pixels = edited->frames[0].pixels;
        const auto target = temporary / L"save-copy/export.png";
        paths.push_back(target);
        command(SaveAs);
        CHECK(load_image(target).frames[0].pixels == pixels && read_file(source) == bytes);
        // Saving a copy does not write the original backing file or change the displayed document.
        CHECK(app.image == edited && app.undo_image == undo && app.modified && app.requested == source &&
              app.image->path == source && app.current_name == L"source.png" &&
              app.selection.x == 1 && app.selection.y == 0 && app.selection.w == 1 && app.selection.h == 1);
        command(ClearSelection);
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == pixels);
        command(Undo);
        CHECK(app.image == undo && !app.modified && app.requested == source);
        command(Redo);
        CHECK(app.image == edited && app.modified && app.requested == source);
    }
    void save_as_copy_exports_current_animation_frame_without_rebinding_or_flattening() {
        use_save_copy_policy();
        app.settings.animation_autoplay = false;
        const auto source = copy(L"disposal.gif", L"save-copy/animation.gif");
        const auto bytes = read_file(source);
        open(source);
        command(FrameNext);
        const auto document = app.image;
        CHECK(document->animated() && app.player.frame == 1);
        const auto target = temporary / L"save-copy/frame.png";
        paths.push_back(target);
        command(SaveAs);
        CHECK(load_image(target).frames[0].pixels == document->frames[1].pixels && read_file(source) == bytes);
        CHECK(app.image == document && app.image->path == source && app.requested == source &&
              !app.modified && app.player.frame == 1 && !app.player.playing && !app.undo_image);
        command(FrameNext);
        CHECK(app.image == document && app.player.frame == 2);
        command(Reload);
        wait();
        CHECK(app.image->animated() && app.image->format == L"GIF" && app.requested == source);
    }
    void save_as_copy_keeps_clipboard_alpha_document_and_previous_undo_location() {
        use_save_copy_policy();
        const auto source = copy(L"rgb.bmp", L"save-copy/original.bmp");
        open(source);
        const auto original = app.image;
        const auto alpha = load_image(fixture(L"alpha.png"));
        copy_image(app.hwnd, alpha, 0);
        command(NewPaste);
        const auto pasted = app.image;
        const auto target = temporary / L"save-copy/clipboard.png";
        paths.push_back(target);
        command(SaveAs);
        CHECK(load_image(target).frames[0].pixels == alpha.frames[0].pixels);
        CHECK(app.image == pasted && app.image->path.empty() && app.requested.empty() && app.modified &&
              app.undo_image == original && app.undo_location && app.files.empty());
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == alpha.frames[0].pixels);
        command(Undo);
        CHECK(app.image == original && app.requested == source && app.image->path == source);
    }
    void save_copy_policy_still_commits_real_overwrites_and_can_enable_legacy_binding() {
        use_save_copy_policy();
        const auto source = copy(L"alpha.png", L"save-copy/overwrite.png");
        open(source);
        const auto original = app.image->frames[0].pixels;
        command(Negative);
        const auto edited = app.image->frames[0].pixels;
        app.settings.confirm_overwrite = false;
        command(Save);
        CHECK(!app.modified && app.requested == source && load_image(source).frames[0].pixels == edited);
        command(Undo);
        CHECK(app.modified && app.image->frames[0].pixels == original && app.requested == source);
        const auto displayed = app.image, undo = app.undo_image;
        auto next = app.settings;
        next.save_as_retarget = true;
        app.apply_settings(next);
        CHECK(app.image == displayed && app.undo_image == undo && app.modified && !app.loading);
        const auto target = temporary / L"save-copy/legacy.png";
        paths.push_back(target);
        command(SaveAs);
        CHECK(!app.modified && app.image->path == target && app.requested == target &&
              load_image(target).frames[0].pixels == original &&
              load_image(source).frames[0].pixels == edited);
    }
    void gif_style_change_preserves_static_and_edited_image_identity() {
        open(fixture(L"alpha.png"));
        auto original = app.image;
        const auto pixels = original->frames[0].pixels;
        auto value = app.settings;
        value.gif_speed = 1;
        app.apply_settings(value);
        CHECK(!app.loading && app.image == original && app.image->frames[0].pixels == pixels);
        command(Negative);
        const auto edited = app.image;
        const auto undo = app.undo_image;
        value = app.settings;
        value.gif_speed = 4;
        app.apply_settings(value);
        CHECK(!app.loading && app.image == edited && app.undo_image == undo && app.modified);
        command(Undo);
        CHECK(app.image == original && app.image->frames[0].pixels == pixels);
        open(fixture(L"blend.apng"));
        original = app.image;
        command(StopAnimation);
        value = app.settings;
        value.gif_speed = 2;
        app.apply_settings(value);
        CHECK(!app.loading && app.image == original && app.player.frame == 0 && !app.player.playing);
    }

    void gif_style_reload_finite_timer_replay_seek_copy_and_undo() {
        auto options = app.settings;
        options.animation_autoplay = false;
        options.prefetch_enabled = false;
        app.apply_settings(options);
        const auto source =
            copy(L"../../docs/verification/gif-reference-0.45/loops/assets/loops-1.gif", L"gif/source.gif");
        const auto bytes = read_file(source);
        open(source);
        for (int style = 0; style < 5; ++style) {
            auto value = app.settings;
            value.gif_speed = style;
            app.apply_settings(value);
            wait();
            CHECK(!app.player.playing && !app.animation_timer);
            CHECK(app.image->file_loops == ((style == 2 || style == 3) ? 1 : 2));
            CHECK(app.player.durations[0] == (style == 1 ? 100 : 50));
            CHECK(app.player.compensate == (style != 2 && style != 3));
            command(Play);
            CHECK(app.player.playing && app.animation_timer);
            for (unsigned step = 0; app.player.playing && step < 12; ++step) {
                const auto duration = app.player.durations.at(app.player.frame);
                app.last_tick = std::chrono::steady_clock::now() - std::chrono::milliseconds(duration);
                app.message(WM_TIMER, 1, 0);
            }
            CHECK(app.player.ended && !app.player.playing && !app.animation_timer && app.player.frame == 2);
            command(Copy);
            CHECK(paste_image(app.hwnd).frames[0].pixels == app.image->frames[2].pixels);
            command(Play);
            CHECK(app.player.playing && app.player.frame == 0 && !app.player.ended);
            command(StopAnimation);
            SendMessageW(app.slider, TBM_SETPOS, TRUE, 1);
            app.message(WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(app.slider));
            CHECK(app.player.frame == 1 && !app.player.playing && !app.player.ended &&
                  app.player.completed_passes == 0);
            CHECK(read_file(source) == bytes);
        }
        const auto original = app.image;
        answers.push_back(IDOK);
        command(Negative);
        CHECK(app.modified && !app.image->animated() && app.undo_image == original);
        command(Undo);
        CHECK(app.image == original && app.player.frame == 1 && !app.player.playing);
        CHECK(read_file(source) == bytes);
    }

    void explicit_loop_on_repeats_finite_gif_without_changing_source_metadata() {
        auto value = app.settings;
        value.animation_autoplay = false;
        value.prefetch_enabled = false;
        app.apply_settings(value);
        const auto path =
            copy(L"../../docs/verification/gif-reference-0.45/loops/assets/loops-1.gif", L"loop/source.gif");
        const auto bytes = read_file(path);
        open(path);
        const auto original = app.image;
        CHECK(app.image->file_loops == 2 && !app.player.playing);
        command(Loop);
        CHECK(!app.player.loop);
        command(Loop);
        CHECK(app.player.loop);
        command(Play);
        app.last_tick = std::chrono::steady_clock::now() - std::chrono::hours(1);
        app.message(WM_TIMER, 1, 0);
        CHECK(app.player.playing && app.animation_timer && !app.player.ended);
        CHECK(app.image == original && app.image->file_loops == 2 && read_file(path) == bytes);
        command(StopAnimation);
        CHECK(app.player.frame == 0 && !app.player.playing);
    }

    void file_loop_setting_changes_runtime_without_reload_and_restores_after_undo() {
        auto value = app.settings;
        value.animation_autoplay = false;
        value.prefetch_enabled = false;
        app.apply_settings(value);
        open(fixture(L"../../docs/verification/gif-reference-0.45/loops/assets/loops-1.gif"));
        auto original = app.image;
        CHECK(app.player.honor_file_loops && app.image->file_loops == 2);
        value = app.settings;
        value.animation_file_loops = false;
        app.apply_settings(value);
        CHECK(app.image == original && !app.loading && !app.player.honor_file_loops);
        command(Play);
        app.last_tick = std::chrono::steady_clock::now() - std::chrono::hours(1);
        app.message(WM_TIMER, 1, 0);
        CHECK(app.player.playing);
        value = app.settings;
        value.animation_file_loops = true;
        app.apply_settings(value);
        CHECK(app.image == original && !app.loading && app.player.honor_file_loops);
        for (unsigned step = 0; app.player.playing && step < 12; ++step) {
            app.last_tick = std::chrono::steady_clock::now() -
                            std::chrono::milliseconds(app.player.durations.at(app.player.frame));
            app.message(WM_TIMER, 1, 0);
        }
        CHECK(!app.player.playing && app.player.ended);
        command(FirstFrame);
        command(FrameNext);
        answers.push_back(IDOK);
        command(Negative);
        CHECK(app.modified && app.undo_image == original);
        command(Loop);
        command(Loop);
        CHECK(!app.settings.animation_file_loops);
        command(Undo);
        CHECK(app.image == original && app.player.frame == 1 && !app.player.honor_file_loops &&
              !app.player.playing);
        Settings persisted;
        persisted.load(app.config);
        // Toolbar choices are persisted by the normal settings/save boundary.
        app.settings.save(app.config);
        persisted.load(app.config);
        CHECK(!persisted.animation_file_loops && persisted.animation_loop);
    }

    void gif_late_timer_displays_next_frame_without_skipping_to_the_end() {
        auto value = app.settings;
        value.animation_autoplay = false;
        value.prefetch_enabled = false;
        app.apply_settings(value);
        open(fixture(L"../../docs/verification/gif-reference-0.45/loops/assets/loops-1.gif"));
        command(Play);
        app.last_tick = std::chrono::steady_clock::now() - std::chrono::hours(1);
        app.message(WM_TIMER, 1, 0);
        CHECK(app.player.frame == 1 && app.player.playing && !app.player.ended);
        command(StopAnimation);
    }
    void gif_finite_and_forced_infinite_modes_are_distinguished_by_nominal_timer_ticks() {
        auto value = app.settings;
        value.animation_autoplay = false;
        value.prefetch_enabled = false;
        app.apply_settings(value);
        open(fixture(L"../../docs/verification/gif-reference-0.45/loops/assets/loops-1.gif"));
        const auto tick = [&] {
            app.last_tick = std::chrono::steady_clock::now() -
                            std::chrono::milliseconds(app.player.durations.at(app.player.frame));
            app.message(WM_TIMER, 1, 0);
        };
        command(Play);
        for (int i = 0; i < 6; ++i)
            tick();
        CHECK(!app.player.playing && app.player.ended && app.player.frame == 2);
        command(Loop);
        command(Loop);
        command(Play);
        for (int i = 0; i < 18; ++i) {
            tick();
            CHECK(app.player.playing && !app.player.ended);
        }
        command(StopAnimation);
    }

    void animation_clock_retains_fractional_milliseconds_across_timer_callbacks() {
        auto value = app.settings;
        value.animation_autoplay = false;
        value.prefetch_enabled = false;
        app.apply_settings(value);
        open(fixture(L"disposal.gif"));
        CHECK(app.player.cycle_ms == 300);
        auto previous = app.animation_now;
        struct Restore {
            std::function<void()> action;
            ~Restore() {
                action();
            }
        } restore{[&] { app.animation_now = std::move(previous); }};
        std::chrono::steady_clock::time_point now{};
        app.animation_now = [&] { return now; };
        command(Play);
        for (int i = 0; i < 19; ++i) {
            now += std::chrono::microseconds(15900);
            app.message(WM_TIMER, 1, 0);
        }
        CHECK(app.player.playing && app.player.frame == 0 && app.player.elapsed == 2);
        CHECK(std::chrono::duration_cast<std::chrono::microseconds>(now - app.last_tick).count() == 100);
        command(StopAnimation);
    }

    void animation_commands_copy_and_status() {
        open(fixture(L"blend.apng"));
        CHECK(app.image->frames.size() == 4 && app.player.playing && app.animation_timer);
        command(Play);
        CHECK(!app.player.playing && !app.animation_timer);
        command(FirstFrame); // Autoplay may have advanced while the real load/message pump was running.
        command(FrameNext);
        CHECK(app.player.frame == 1 && !app.player.playing);
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == app.image->frames[1].pixels);
        command(LastFrame);
        CHECK(app.player.frame == 3);
        command(FrameNext);
        CHECK(app.player.frame == 0);
        command(FramePrevious);
        CHECK(app.player.frame == 3);
        command(FirstFrame);
        CHECK(app.player.frame == 0);
        command(Play);
        app.last_tick = std::chrono::steady_clock::now() - std::chrono::milliseconds(50);
        app.message(WM_TIMER, 1, 0);
        CHECK(app.player.frame == 1);
        CHECK(status().find(L"2/4") != std::wstring::npos);
        command(Loop);
        CHECK(!app.player.loop && !app.settings.animation_loop);
        command(StopAnimation);
        CHECK(app.player.frame == 0 && !app.player.playing);
    }
    void animation_seek_and_edit_undo() {
        open(fixture(L"disposal.gif"));
        SendMessageW(app.slider, TBM_SETPOS, TRUE, 2);
        app.message(WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(app.slider));
        CHECK(app.player.frame == 2 && !app.player.playing);
        auto original = app.image;
        command(Loop);
        CHECK(!app.player.loop);
        answers.push_back(IDOK);
        command(RotateRight);
        CHECK(!app.image->animated() && app.image->path.empty());
        command(Undo);
        CHECK(app.image == original && app.player.frame == 2 && !app.player.loop);
    }
    void close_stops_timers_and_drag() {
        open(fixture(L"blend.apng"));
        command(Slideshow);
        command(ArmDrag);
        command(CloseImage);
        CHECK(!app.image && !app.loading && !app.animation_timer);
        CHECK(!app.slideshow && !app.drag_armed && app.files.empty());
        app.message(WM_TIMER, 2, 0); // A queued timer must not reopen the previous list.
        CHECK(!app.image && !app.loading);
    }
    void save_as_reopen_and_cancel() {
        open(copy(L"alpha.png", L"a.png"));
        command(RotateRight);
        auto original = app.image->frames[0].pixels;
        paths.push_back(temporary / L"日本語 空白.png");
        command(SaveAs);
        CHECK(!app.modified && app.image->path.filename() == L"日本語 空白.png");
        CHECK(load_image(app.image->path).frames[0].pixels == original);
        command(Reload);
        wait();
        CHECK(app.image->frames[0].pixels == original && !app.undo_image);
        command(Negative);
        paths.push_back({});
        command(SaveAs);
        CHECK(app.modified);
        answers.push_back(IDNO);
        command(Save);
        CHECK(app.modified);
    }
    std::wstring file_name_clipboard_value() {
        CHECK(OpenClipboard(app.hwnd));
        struct Close {
            ~Close() { CloseClipboard(); }
        } close;
        const auto handle = GetClipboardData(CF_UNICODETEXT);
        CHECK(handle);
        const auto value = static_cast<const wchar_t *>(GlobalLock(handle));
        CHECK(value);
        const std::wstring result(value);
        GlobalUnlock(handle);
        return result;
    }
    void check_file_name_clipboard(const std::filesystem::path &source) {
        auto native_source = source;
        native_source.make_preferred();
        wchar_t short_name[32768]{};
        CHECK(GetShortPathNameW(native_source.c_str(), short_name, 32768) > 0);
        const std::pair<UINT, std::wstring> expected[]{
            {CopyPath, native_source.wstring()}, {CopyFileName, native_source.filename().wstring()},
            {CopyFolderName, native_source.parent_path().filename().wstring()},
            {CopyFolderPath, native_source.parent_path().wstring()}, {CopyShortPath, short_name}};
        const auto image = app.image, undo = app.undo_image;
        const auto dirty = app.modified;
        const auto requested = app.requested;
        for (const auto &[id, text] : expected) {
            clipboard_text(L"GVN clipboard-name sentinel");
            command(id);
            CHECK(file_name_clipboard_value() == text);
            CHECK(app.image == image && app.undo_image == undo && app.modified == dirty &&
                  app.requested == requested);
        }
    }
    void file_name_clipboard_variants_keep_unicode_source_after_edit_and_save_copy() {
        use_save_copy_policy();
        const auto source = copy(L"alpha.png", L"Reference folder 日本語 space/many.parts_日本語 image.png");
        const auto bytes = read_file(source);
        open(source);
        check_file_name_clipboard(source);
        command(Negative);
        app.selection = {1, 0, 1, 1};
        check_file_name_clipboard(source);
        CHECK(app.selection.x == 1 && app.selection.w == 1);
        const auto target = temporary / L"saved-copy.png";
        paths.push_back(target);
        command(SaveAs);
        check_file_name_clipboard(source);
        CHECK(app.modified && read_file(source) == bytes && std::filesystem::exists(target));
    }
    void file_name_clipboard_uses_displayed_source_during_pending_decode() {
        const auto first = copy(L"alpha.png", L"name-source/current.png");
        const auto second = copy(L"rgb.bmp", L"name-target/pending.bmp");
        open(first);
        with_rotation_worker_gate(second, false, false, [&](const auto &entered, const auto &release) {
            app.open(second, false, false);
            entered();
            CHECK(app.loading && app.requested == second && app.image->path == first);
            const auto sequence = app.loader->sequence();
            check_file_name_clipboard(first);
            CHECK(app.loading && app.loader->sequence() == sequence);
            release();
            wait();
            CHECK(app.image->path == second);
            check_file_name_clipboard(second);
        });
    }
    void file_name_clipboard_virtual_name_folder_and_empty_short_path_keep_alpha_and_undo() {
        const UINT ids[]{CopyPath, CopyFileName, CopyFolderName, CopyFolderPath, CopyShortPath};
        clipboard_text(L"no-image clipboard");
        for (auto id : ids) {
            command(id);
            CHECK(file_name_clipboard_value() == L"no-image clipboard");
        }
        app.settings.confirm_unsaved = false;
        const auto source = copy(L"rgb.bmp", L"virtual-folder 日本語/original.bmp");
        const auto alpha = load_image(fixture(L"alpha.png"));
        for (const bool undo_enabled : {true, false}) {
            app.settings.undo_enabled = undo_enabled;
            open(source);
            const auto previous = app.image;
            copy_image(app.hwnd, alpha, 0);
            command(NewPaste);
            const auto document = app.image;
            CHECK(document->path.empty() && app.requested.empty());
            app.selection = {0, 0, 3, 2};
            const std::pair<UINT, std::wstring> expected[]{
                {CopyFileName, L"クリップボード画像"}, {CopyPath, L"クリップボード画像"},
                {CopyFolderName, source.parent_path().filename().wstring()},
                {CopyFolderPath, source.parent_path().wstring()}};
            for (const auto &[id, text] : expected) {
                command(id);
                CHECK(file_name_clipboard_value() == text);
                CHECK(app.image == document && app.modified && app.selection.w == 3 &&
                      app.selection.h == 2 && document->frames[0].pixels == alpha.frames[0].pixels);
            }
            command(CopyShortPath);
            CHECK(CountClipboardFormats() == 0 && app.image == document);
            command(Copy);
            CHECK(paste_image(app.hwnd).frames[0].pixels == alpha.frames[0].pixels);
            if (undo_enabled) {
                command(Undo);
                CHECK(app.image == previous && app.requested == source);
                command(Redo);
                CHECK(app.image == document && app.requested.empty());
                command(CopyFolderPath);
                CHECK(file_name_clipboard_value() == source.parent_path().wstring());
            } else
                CHECK(!app.undo_image);
        }
    }
    void file_name_clipboard_archive_source_and_missing_short_path() {
        const auto archive_file = copy(L"images.zip", L"archive folder 日本語/pages.zip");
        open(archive_file);
        CHECK(!app.archive.empty() && source_file(app.requested) == archive_file);
        check_file_name_clipboard(archive_file);
        const auto image = app.image;
        const auto pixels = image->frames[0].pixels;
        CHECK(std::filesystem::remove(archive_file));
        clipboard_text(L"short-path failure sentinel");
        command(CopyShortPath);
        CHECK(CountClipboardFormats() == 0 && app.image == image);
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == pixels);
    }
    void new_window_empty_file_and_send_keep_edited_selection_undo_and_alpha() {
        const auto file = copy(L"alpha.png", L"new window 日本語/source.png");
        const auto disk = read_file(file);
        open(file);
        command(Negative);
        app.selection = {1, 0, 1, 2};
        const auto image = app.image, undo = app.undo_image;
        const auto pixels = image->frames[0].pixels;
        const auto selected = crop(still_frame(*image, 0), app.selection);
        clipboard_text(L"new-window sentinel");
        const auto sequence = GetClipboardSequenceNumber();
        command(NewWindow);
        CHECK(new_windows.size() == 1 && std::holds_alternative<std::monostate>(new_windows.back()));
        CHECK(GetClipboardSequenceNumber() == sequence);
        command(NewWindowFile);
        CHECK(new_windows.size() == 2 && std::get<std::filesystem::path>(new_windows.back()) == file);
        CHECK(GetClipboardSequenceNumber() == sequence);
        command(NewWindowSend);
        CHECK(new_windows.size() == 3);
        const auto &sent = std::get<Image>(new_windows.back());
        CHECK(sent.width == 1 && sent.height == 2 && sent.path.empty() && sent.frames.size() == 1);
        CHECK(sent.frames[0].pixels == selected.frames[0].pixels);
        CHECK(paste_image(app.hwnd).frames[0].pixels == selected.frames[0].pixels);
        CHECK(app.image == image && app.undo_image == undo && app.modified);
        CHECK(app.image->frames[0].pixels == pixels && app.requested == file && !app.loading);
        CHECK(app.selection.x == 1 && app.selection.w == 1 && app.selection.h == 2);
        CHECK(read_file(file) == disk && notices.empty());
    }
    void new_window_file_and_send_keep_displayed_source_during_pending_decode() {
        const auto first = copy(L"alpha.png", L"window-source/current.png");
        const auto second = copy(L"rgb.bmp", L"window-target/pending.bmp");
        open(first);
        const auto image = app.image;
        with_rotation_worker_gate(second, false, false, [&](const auto &entered, const auto &release) {
            app.open(second, false, false);
            entered();
            const auto sequence = app.loader->sequence();
            command(NewWindowFile);
            CHECK(std::get<std::filesystem::path>(new_windows.back()) == first);
            command(NewWindowSend);
            CHECK(std::get<Image>(new_windows.back()).frames[0].pixels == image->frames[0].pixels);
            command(NewWindow);
            CHECK(std::holds_alternative<std::monostate>(new_windows.back()));
            CHECK(app.loading && app.requested == second && app.image == image &&
                  app.loader->sequence() == sequence);
            release();
            wait();
            command(NewWindowFile);
            CHECK(std::get<std::filesystem::path>(new_windows.back()) == second);
        });
    }
    void new_window_paste_freezes_image_or_path_without_writing_clipboard_or_parent() {
        open(fixture(L"rgb.bmp"));
        command(Negative);
        const auto image = app.image, undo = app.undo_image;
        auto source = load_image(fixture(L"alpha.png"));
        copy_image(app.hwnd, source, 0);
        auto sequence = GetClipboardSequenceNumber();
        command(NewWindowPaste);
        CHECK(new_windows.size() == 1 && GetClipboardSequenceNumber() == sequence);
        CHECK(std::get<Image>(new_windows.back()).frames[0].pixels == source.frames[0].pixels);
        clipboard_text(fixture(L"alpha.png").wstring());
        sequence = GetClipboardSequenceNumber();
        command(NewWindowPaste);
        CHECK(new_windows.size() == 2 && GetClipboardSequenceNumber() == sequence);
        CHECK(std::get<std::filesystem::path>(new_windows.back()) == fixture(L"alpha.png"));
        clipboard_text(L"ordinary unsupported text");
        sequence = GetClipboardSequenceNumber();
        command(NewWindowPaste);
        CHECK(new_windows.size() == 2 && GetClipboardSequenceNumber() == sequence);
        CHECK(app.image == image && app.undo_image == undo && app.modified && notices.empty());
    }
    void new_window_current_frame_archive_and_anonymous_source_rules() {
        for (const auto file : {L"disposal.gif", L"blend.apng", L"psd-16-3.psd"}) {
            open(fixture(file));
            command(LastFrame);
            app.selection = {0, 0, 1, 1};
            const auto expected = crop(still_frame(*app.image, app.player.frame), app.selection);
            const auto image = app.image;
            command(NewWindowFile);
            CHECK(std::get<std::filesystem::path>(new_windows.back()) == fixture(file));
            command(NewWindowSend);
            CHECK(std::get<Image>(new_windows.back()).frames[0].pixels == expected.frames[0].pixels);
            CHECK(app.image == image && app.player.frame == image->frames.size() - 1);
        }
        open(fixture(L"images.zip"));
        const auto member = app.requested;
        command(NewWindowFile);
        CHECK(std::get<std::filesystem::path>(new_windows.back()) == member);
        copy_image(app.hwnd, load_image(fixture(L"alpha.png")), 0);
        command(NewPaste);
        const auto count = new_windows.size();
        command(NewWindowFile);
        CHECK(new_windows.size() == count && app.requested.empty());
        command(NewWindowSend);
        CHECK(new_windows.size() == count + 1 && std::get<Image>(new_windows.back()).path.empty());
    }
    void new_window_received_empty_and_image_skip_previous_file_reopen() {
        app.settings.reopen_last = true;
        app.settings.last_file = fixture(L"rgb.bmp").wstring();
        app.settings.paste_new = false;
        app.open_new_window(std::monostate{});
        CHECK(!app.image && !app.loading);
        const auto source = load_image(fixture(L"alpha.png"));
        app.open_new_window(decode_new_window(encode_new_window(source)));
        CHECK(app.image && !app.loading && app.modified && app.image->path.empty());
        CHECK(app.requested.empty() && app.folder.empty() && app.archive.empty());
        CHECK(app.current_name == L"クリップボード画像" && !app.undo_image);
        CHECK(app.image->frames[0].pixels == source.frames[0].pixels);
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == source.frames[0].pixels);
        app.settings.confirm_unsaved = false;
        command(CloseImage);
        const auto count = new_windows.size();
        command(NewWindowFile);
        command(NewWindowSend);
        CHECK(new_windows.size() == count);
        app.open_new_window(fixture(L"alpha.png"));
        wait();
        CHECK(app.image && app.image->path == fixture(L"alpha.png"));
    }
    void new_window_launch_failure_keeps_parent_selection_undo_and_clipboard_reads() {
        open(fixture(L"alpha.png"));
        command(Negative);
        app.selection = {0, 0, 1, 1};
        const auto image = app.image, undo = app.undo_image;
        app.dialogs.new_viewer = [](const auto &, const auto &, const auto &) -> ViewerProcess {
            throw std::runtime_error("owned launch failure");
        };
        copy_image(app.hwnd, *image, 0);
        const auto sequence = GetClipboardSequenceNumber();
        for (const auto id : {NewWindow, NewWindowFile, NewWindowPaste}) {
            THROWS(command(id));
            CHECK(GetClipboardSequenceNumber() == sequence);
        }
        THROWS(command(NewWindowSend));
        CHECK(paste_image(app.hwnd).frames[0].pixels == crop(still_frame(*image, 0), app.selection).frames[0].pixels);
        CHECK(app.image == image && app.undo_image == undo && app.modified && app.selection.w == 1);
    }
    ExternalApplication external_entry(ExternalSend send = ExternalSend::File) {
        ExternalApplication entry; entry.name = L"Editor&A"; entry.executable = (temporary / L"Editor 日本語 space" / L"app.exe").wstring(); entry.send = send;
        app.settings.set_external_application(0, entry); return entry;
    }
    static std::vector<std::wstring> external_arguments(const ExternalLaunch &request) {
        int count{}; const auto args = CommandLineToArgvW((L"receiver.exe " + request.parameters).c_str(), &count); CHECK(args);
        std::vector<std::wstring> result; for (int i = 1; i < count; ++i) result.emplace_back(args[i]); LocalFree(args); return result;
    }
    void inactive_display_policy_changes_refresh_cached_views_without_resetting_active_manual_zoom() {
        open(fixture(L"alpha.png"));
        command(Fullscreen);
        CHECK(app.view.scale > 1);
        command(Fullscreen);
        command(ZoomIn);
        const double normal_zoom = app.view.scale;
        CHECK(app.view.manual && normal_zoom > 1);
        auto next = app.settings;
        next.full_display_mode = 2;
        app.apply_settings(next);
        CHECK(!app.full && app.view.manual && app.view.scale == normal_zoom);
        command(Fullscreen);
        CHECK(app.view.scale == 1 && app.view.shrink_only && !app.view.manual);
        command(ZoomIn);
        const double full_zoom = app.view.scale;
        next = app.settings;
        next.display_mode = 1;
        app.apply_settings(next);
        CHECK(app.full && app.view.manual && app.view.scale == full_zoom);
        command(Fullscreen);
        CHECK(app.view.scale > 1 && !app.view.shrink_only && !app.view.manual);
        command(ZoomIn);
        command(Fullscreen);
        CHECK(app.view.manual && app.view.scale == full_zoom);
        next = app.settings;
        next.initial_fit = static_cast<int>(Fit::Width);
        app.apply_settings(next);
        CHECK(app.full && app.view.manual && app.view.scale == full_zoom);
        command(Fullscreen);
        CHECK(!app.view.manual && app.view.fit == Fit::Width);
    }
    void first_fullscreen_policy_honors_one_pixel_image_when_cached_dimensions_already_match() {
        auto next = app.settings;
        next.full_display_mode = 2;
        app.apply_settings(next);
        Image image;
        image.width = image.height = 1;
        Frame frame; frame.pixels.push_back({17, 31, 59, 127});
        image.frames.push_back(std::move(frame));
        const auto path = temporary / L"one pixel.png";
        save_image(image, 0, path, SaveOptions{});
        open(path);
        CHECK(app.full_view.canvas_w == 1 && app.full_view.canvas_h == 1);
        command(Fullscreen);
        CHECK(app.view.scale == 1 && app.view.shrink_only && app.checked(DisplayLargeWindow));
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == image.frames[0].pixels);
        command(Fullscreen);
        CHECK(app.view.scale == 1 && app.view.shrink_only);
    }
    void display_default_shrinks_large_images_and_preserves_small_alpha_in_normal_mode() {
        CHECK(app.settings.display_mode == 2 && app.settings.full_display_mode == 1);
        open(fixture(L"display-alpha.png"));
        const auto pixels = app.image->frames[0].pixels;
        CHECK(app.view.scale == 1 && app.view.shrink_only && app.checked(DisplayLargeWindow));
        CHECK(SetWindowPos(app.hwnd, nullptr, 0, 0, 1200, 800, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        CHECK(app.view.scale == 1 && app.view.shrink_only);
        command(Fullscreen);
        CHECK(app.full && app.view.scale > 1 && !app.view.shrink_only && app.checked(DisplayImageWindow));
        command(Fullscreen);
        CHECK(!app.full && app.view.scale == 1 && app.view.shrink_only);
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == pixels);
        Image large;
        large.width = 2048; large.height = 1536;
        Frame frame; frame.pixels.assign(size_t(large.width) * large.height, {21, 39, 71, 255});
        large.frames.push_back(std::move(frame));
        const auto path = temporary / L"large policy.png";
        save_image(large, 0, path, SaveOptions{});
        open(path);
        CHECK(app.view.scale < 1 && app.view.scale > 0 && app.view.shrink_only);
        CHECK(app.view.canvas_w * app.view.scale <= app.view.width + 1e-6);
        CHECK(app.view.canvas_h * app.view.scale <= app.view.height + 1e-6);
        CHECK(!app.modified && load_source(path).frames[0].pixels == large.frames[0].pixels);
    }
    void display_explicit_policy_changes_survive_save_without_crossing_screen_modes() {
        auto next = app.settings;
        next.display_mode = 1; next.full_display_mode = 2;
        app.apply_settings(next);
        open(fixture(L"alpha.png"));
        CHECK(app.view.scale > 1 && app.checked(DisplayImageWindow));
        command(Fullscreen);
        CHECK(app.view.scale == 1 && app.checked(DisplayLargeWindow));
        command(DisplayImageWindow);
        CHECK(app.view.scale > 1 && app.settings.display_mode == 1 && app.settings.full_display_mode == 1);
        command(Fullscreen);
        command(DisplayLargeWindow);
        CHECK(app.view.scale == 1 && app.settings.display_mode == 2 && app.settings.full_display_mode == 1);
        app.settings.save(app.config);
        Settings restored; restored.load(app.config);
        CHECK(restored.display_mode == 2 && restored.full_display_mode == 1);
        command(Fullscreen);
        CHECK(app.view.scale > 1 && app.checked(DisplayImageWindow));
        command(Fullscreen);
        CHECK(app.view.scale == 1 && app.checked(DisplayLargeWindow));
    }
    void external_menu_names_hidden_slots_separators_keys_and_configuration() {
        auto entry = external_entry(); entry.separator = true;
        app.settings.set_external_application(0, entry);
        auto hidden = entry; hidden.name.clear(); app.settings.set_external_application(1, hidden);
        app.settings.set_keys(ExternalBase, {L"Ctrl+Alt+E"}); app.refresh_menu();
        const auto find = [&](auto &&self, HMENU menu, UINT id) -> std::pair<HMENU, int> {
            for (int i = 0; i < GetMenuItemCount(menu); ++i) {
                if (GetMenuItemID(menu, i) == id) return {menu, i};
                if (const auto child = GetSubMenu(menu, i)) { const auto found = self(self, child, id); if (found.first) return found; }
            }
            return {};
        };
        const auto item = find(find, app.main_menu, ExternalBase); CHECK(item.first);
        wchar_t text[256]{}; GetMenuStringW(item.first, item.second, text, 256, MF_BYPOSITION);
        CHECK(std::wstring(text) == L"(&1) Editor&&A\tCtrl+Alt+E");
        CHECK(GetMenuState(item.first, item.second + 1, MF_BYPOSITION) & MF_SEPARATOR);
        CHECK(!find(find, app.main_menu, ExternalBase + 1).first);
        CHECK(find(find, app.main_menu, ExternalBase + 7).first && find(find, app.main_menu, ExternalBase + 8).first);
        bool opened{};
        app.dialogs.preferences_ui = [&](HWND owner, const Settings &value, const PreferencesDialog::Apply &apply, PreferencePage page, int) {
            CHECK(owner == app.hwnd && page == PreferencePage::External); opened = true;
            auto next = value; auto changed = next.external_application(0); changed.name = L"Changed name";
            next.set_external_application(0, changed); apply(next); return true;
        };
        command(ConfigureExternal); CHECK(opened && app.settings.external_application(0).name == L"Changed name");
        Settings saved; saved.load(app.config); CHECK(saved.external_applications() == app.settings.external_applications());
    }
    void external_file_folder_none_and_explicit_arguments_keep_edited_source() {
        const auto source = copy(L"display-alpha.png", L"External source 日本語\\image with spaces.png"); open(source); command(Negative);
        app.selection = {0, 0, 1, 1}; const auto image = app.image, undo = app.undo_image;
        const auto bytes = read_file(source); clipboard_text(L"leave this clipboard"); const auto sequence = GetClipboardSequenceNumber();
        for (const auto send : {ExternalSend::None, ExternalSend::File, ExternalSend::Folder}) {
            auto entry = external_entry(send); entry.show = ExternalShow::Maximized; entry.verb = L"edit";
            app.settings.set_external_application(0, entry); command(ExternalBase);
            const auto &request = external_launches.back();
            CHECK(request.file == entry.executable && request.directory == viewer_executable().parent_path() && request.show == SW_SHOWMAXIMIZED && request.verb == L"edit");
            CHECK(external_arguments(request) == (send == ExternalSend::File ? std::vector{source.wstring()} : send == ExternalSend::Folder ? std::vector{source.parent_path().wstring()} : std::vector<std::wstring>{}));
            CHECK(GetClipboardSequenceNumber() == sequence && app.image == image && app.undo_image == undo && app.modified && app.selection.w == 1);
        }
        auto entry = external_entry(ExternalSend::Folder); entry.executable = L"\"" + entry.executable + L"\" /open \"($f)\" \"($d)\" \"\"";
        app.settings.set_external_application(0, entry); command(ExternalBase);
        CHECK(external_arguments(external_launches.back()) == std::vector<std::wstring>{L"/open", source.wstring(), source.parent_path().wstring(), L""});
        CHECK(read_file(source) == bytes && notices.empty());
    }
    void external_clipboard_has_full_alpha_selection_and_current_animation_frame_before_launch() {
        app.settings.confirm_unsaved = false;
        for (const auto file : {L"display-alpha.png", L"disposal.gif", L"blend.apng", L"psd-16-3.psd"}) {
            open(fixture(file)); command(LastFrame);
            if (std::wstring(file) != L"display-alpha.png") app.selection = {0, 0, 1, 1};
            const auto expected = app.copied_image(); const auto image = app.image, undo = app.undo_image; const auto frame = app.player.frame;
            external_entry(ExternalSend::Clipboard);
            app.dialogs.external_launch = [&](HWND owner, const ExternalLaunch &request) {
                CHECK(owner == app.hwnd && request.copy_image && request.parameters.empty());
                const auto pasted = paste_image(app.hwnd);
                CHECK(pasted.width == expected.width && pasted.height == expected.height && pasted.frames[0].pixels == expected.frames[0].pixels);
                external_launches.push_back(request); return true;
            };
            const auto sequence = GetClipboardSequenceNumber(); command(ExternalBase);
            CHECK(GetClipboardSequenceNumber() != sequence && IsClipboardFormatAvailable(CF_DIBV5) && IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG")));
            CHECK(app.image == image && app.undo_image == undo && app.player.frame == frame);
        }
    }
    void external_empty_and_anonymous_images_keep_launch_paths_and_copy_only_semantics() {
        clipboard_text(L"empty viewer marker"); const auto sequence = GetClipboardSequenceNumber();
        for (const auto send : {ExternalSend::None, ExternalSend::File, ExternalSend::Folder, ExternalSend::Clipboard}) {
            external_entry(send); command(ExternalBase); const auto &request = external_launches.back();
            CHECK(!request.copy_image && !app.image && GetClipboardSequenceNumber() == sequence);
            CHECK(external_arguments(request) == (send == ExternalSend::Folder ? std::vector{viewer_executable().parent_path().wstring()} : std::vector<std::wstring>{}));
        }
        const auto alpha = load_image(fixture(L"display-alpha.png")); copy_image(app.hwnd, alpha, 0); command(NewPaste);
        external_entry(ExternalSend::File); command(ExternalBase); CHECK(external_launches.back().parameters.empty());
        auto entry = default_external_applications()[7]; entry.quit_after_launch = true;
        app.settings.set_external_application(0, entry); command(ExternalBase);
        CHECK(external_launches.back().file.empty()); MSG message{};
        CHECK(!PeekMessageW(&message, app.hwnd, WM_CLOSE, WM_CLOSE, PM_REMOVE));
        entry = {}; entry.send = ExternalSend::Clipboard; app.settings.set_external_application(0, entry); command(ExternalBase);
        CHECK(external_launches.back().file.empty() && paste_image(app.hwnd).frames[0].pixels == alpha.frames[0].pixels);
    }
    void external_archive_file_folder_and_macro_policies_use_named_displayed_member() {
        const auto source = copy(L"images.zip", L"External archive 日本語\\images with spaces.zip"); open(source);
        const auto identity = app.requested.wstring(); const auto number = std::stoul(identity.substr(identity.find(L'|') + 1));
        const auto entries = Archive(read_file(source)).entries();
        const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto &entry) { return entry.index == number; }); CHECK(found != entries.end());
        auto name = found->name; std::replace(name.begin(), name.end(), L'/', L'\\'); const auto member = source.wstring() + L"\\" + name;
        const auto image = app.image;
        for (const bool include : {false, true}) for (const bool archive_folder : {false, true}) {
            for (const auto send : {ExternalSend::File, ExternalSend::Folder}) {
                auto entry = external_entry(send); entry.include_archive_member = include; entry.archive_file_for_folder = archive_folder;
                app.settings.set_external_application(0, entry); command(ExternalBase);
                CHECK(external_arguments(external_launches.back()) == std::vector<std::wstring>{send == ExternalSend::File ? (include ? member : source.wstring()) : (archive_folder ? source.wstring() : source.parent_path().wstring())});
                CHECK(app.image == image && app.requested.wstring() == identity);
            }
        }
        auto entry = external_entry(ExternalSend::None); entry.include_archive_member = entry.archive_file_for_folder = true;
        entry.executable = L"\"" + entry.executable + L"\" /archive \"($f)\" \"($d)\""; app.settings.set_external_application(0, entry); command(ExternalBase);
        CHECK(external_arguments(external_launches.back()) == std::vector<std::wstring>{L"/archive", member, source.wstring()});
    }
    void external_pending_regular_decode_keeps_displayed_source_selection_and_undo() {
        const auto first = copy(L"display-alpha.png", L"External pending\\current.png"), second = copy(L"rgb.bmp", L"External pending\\target.bmp");
        open(first); command(Negative); app.selection = {0, 0, 1, 1}; const auto image = app.image, undo = app.undo_image;
        with_rotation_worker_gate(second, false, false, [&](const auto &entered, const auto &release) {
            app.open(second, false, false); entered(); const auto sequence = app.loader->sequence();
            external_entry(ExternalSend::File); command(ExternalBase); CHECK(external_arguments(external_launches.back()) == std::vector{first.wstring()});
            external_entry(ExternalSend::Folder); command(ExternalBase); CHECK(external_arguments(external_launches.back()) == std::vector{first.parent_path().wstring()});
            external_entry(ExternalSend::Clipboard); command(ExternalBase);
            CHECK(paste_image(app.hwnd).frames[0].pixels == app.copied_image().frames[0].pixels);
            CHECK(app.loading && app.requested == second && app.loader->sequence() == sequence && app.image == image && app.undo_image == undo && app.selection.w == 1);
            release(); wait(); CHECK(app.image->path == second);
        });
    }
    void external_pending_archive_replacement_uses_source_archive_metadata() {
        const auto first = copy(L"images.zip", L"External pending archive\\source.zip"), second = copy(L"rgb.bmp", L"External pending elsewhere\\target.bmp");
        open(first); const auto displayed = app.requested; const auto before = app.image;
        auto entry = external_entry(ExternalSend::File); entry.include_archive_member = true; app.settings.set_external_application(0, entry);
        const auto expected = app.external_context(entry).file_path;
        with_rotation_worker_gate(second, false, false, [&](const auto &entered, const auto &release) {
            app.open(second, false, false); entered(); command(ExternalBase);
            CHECK(external_arguments(external_launches.back()) == std::vector{expected});
            CHECK(app.image == before && app.loading && app.before_load && displayed != app.requested);
            release(); wait();
        });
    }
    void external_legacy_registration_and_unregistered_picker_keep_original_or_rendered_image() {
        const auto source = copy(L"psd-16-3.psd", L"External legacy\\source.psd"); open(source);
        app.settings.external[0] = (temporary / L"old editor.exe").wstring(); command(ExternalBase);
        CHECK(external_arguments(external_launches.back()) == std::vector{source.wstring()});
        command(Negative); app.selection = {0, 0, 1, 1}; const auto image = app.image, undo = app.undo_image;
        command(ExternalBase); const auto exported = std::filesystem::path(external_arguments(external_launches.back()).at(0));
        CHECK(exported != source && exported.extension() == L".png");
        auto decoded = load_image(exported); CHECK(decoded.width == image->width && decoded.height == image->height && decoded.frames[0].pixels == image->frames[0].pixels);
        CHECK(std::filesystem::remove(exported));
        paths.push_back(temporary / L"picked editor.exe"); command(External);
        const auto picked = std::filesystem::path(external_arguments(external_launches.back()).at(0)); CHECK(picked != source);
        CHECK(load_image(picked).frames[0].pixels == image->frames[0].pixels); CHECK(std::filesystem::remove(picked));
        const auto count = external_launches.size(); paths.push_back({}); command(External); CHECK(external_launches.size() == count);
        CHECK(app.image == image && app.undo_image == undo && app.selection.w == 1 && app.modified);
    }
    void external_launch_failure_and_malformed_arguments_keep_source_and_no_quit() {
        open(fixture(L"display-alpha.png")); command(Negative); app.selection = {0, 0, 1, 1}; const auto image = app.image, undo = app.undo_image;
        auto entry = external_entry(); entry.quit_after_launch = true; app.settings.set_external_application(0, entry);
        clipboard_text(L"failure marker"); const auto sequence = GetClipboardSequenceNumber();
        app.dialogs.external_launch = [](HWND, const ExternalLaunch &) -> bool { throw std::runtime_error("owned launch failure"); };
        THROWS(command(ExternalBase)); CHECK(GetClipboardSequenceNumber() == sequence);
        entry.send = ExternalSend::Clipboard; app.settings.set_external_application(0, entry); THROWS(command(ExternalBase));
        CHECK(paste_image(app.hwnd).frames[0].pixels == app.copied_image().frames[0].pixels);
        const auto copied = GetClipboardSequenceNumber(); entry.send = ExternalSend::DisplayedPng; entry.executable = L"\"unclosed";
        app.settings.set_external_application(0, entry); THROWS(command(ExternalBase)); CHECK(GetClipboardSequenceNumber() == copied);
        MSG message{}; CHECK(!PeekMessageW(&message, app.hwnd, WM_CLOSE, WM_CLOSE, PM_REMOVE));
        CHECK(app.image == image && app.undo_image == undo && app.modified && app.selection.w == 1 && notices.empty());
    }
    void external_successful_launch_quit_honors_optional_unsaved_prompt_and_copy_only_does_not_close() {
        open(fixture(L"display-alpha.png")); command(Negative); const auto image = app.image;
        auto entry = external_entry(); entry.quit_after_launch = true; app.settings.set_external_application(0, entry);
        command(ExternalBase); MSG message{}; CHECK(PeekMessageW(&message, app.hwnd, WM_CLOSE, WM_CLOSE, PM_REMOVE));
        answers.push_back(IDCANCEL); app.message(message.message, message.wParam, message.lParam);
        CHECK(IsWindow(app.hwnd) && app.image == image && app.modified && notices.size() == 1);
        entry.executable.clear(); entry.send = ExternalSend::Clipboard; app.settings.set_external_application(0, entry); command(ExternalBase);
        CHECK(!PeekMessageW(&message, app.hwnd, WM_CLOSE, WM_CLOSE, PM_REMOVE));
        app.settings.confirm_unsaved = false; entry = external_entry(); entry.quit_after_launch = true;
        app.settings.set_external_application(0, entry); command(ExternalBase);
        CHECK(PeekMessageW(&message, app.hwnd, WM_CLOSE, WM_CLOSE, PM_REMOVE)); app.message(message.message, message.wParam, message.lParam);
        CHECK(!IsWindow(app.hwnd) && notices.size() == 1);
    }
    void file_copy_move_rename_no_overwrite() {
        auto original = copy(L"alpha.png", L"source/a.png");
        open(original);
        auto destination = temporary / L"destination";
        std::filesystem::create_directory(destination);
        const auto expected = read_file(original);
        directories.push_back(destination);
        command(FileCopy);
        CHECK(read_file(destination / L"a.png") == expected && std::filesystem::exists(original));
        directories.push_back(destination);
        answers.push_back(IDNO);
        command(FileCopy);
        CHECK(read_file(destination / L"a.png") == expected);
        texts.push_back(L"../outside.png");
        THROWS(app.command(RenameFile));
        texts.push_back(L"renamed.png");
        command(RenameFile);
        wait();
        CHECK(app.image->path.filename() == L"renamed.png" && !std::filesystem::exists(original));
        directories.push_back(destination);
        command(FileMove);
        wait();
        CHECK(same_path(app.image->path, destination / L"renamed.png"));
        CHECK(read_file(app.image->path) == expected);
        command(CopyPath);
        CHECK(OpenClipboard(app.hwnd));
        const auto text = static_cast<const wchar_t *>(GlobalLock(GetClipboardData(CF_UNICODETEXT)));
        CHECK(text && std::wstring(text) == app.image->path.wstring());
        GlobalUnlock(GetClipboardData(CF_UNICODETEXT));
        CloseClipboard();
    }
    void comic_default_matches_reference_centered_black_padding() {
        Image first, second;
        first.width = 7;
        first.height = 11;
        first.frames.push_back({std::vector<Pixel>(77, {200, 0, 20, 255}), 100});
        second.width = 5;
        second.height = 17;
        second.frames.push_back({std::vector<Pixel>(85, {10, 0, 220, 255}), 100});
        const auto directory = temporary / L"comic-reference";
        std::filesystem::create_directory(directory);
        const auto a = directory / L"01.png", b = directory / L"02.png";
        save_image(first, 0, a, SaveOptions{});
        save_image(second, 0, b, SaveOptions{});
        open(a);
        command(Comic);
        wait();
        CHECK(app.image->width == 12 && app.image->height == 17 && app.image->format == L"見開き");
        const auto &pixels = app.image->frames[0].pixels;
        CHECK(pixels[0] == Pixel{10, 0, 220, 255});
        CHECK(pixels[5] == Pixel{0, 0, 0, 255});
        CHECK(pixels[3 * 12 + 5] == Pixel{200, 0, 20, 255});
        CHECK(pixels[13 * 12 + 5] == Pixel{200, 0, 20, 255});
        CHECK(pixels[14 * 12 + 5] == Pixel{0, 0, 0, 255});
        CHECK(pixels.back() == Pixel{0, 0, 0, 255});
    }
    void comic_and_zip_navigation() {
        app.settings.comic_mode = 3; // Preserve the explicit old-GVN first-page policy contract.
        auto first = copy(L"page1.png", L"pages/1.png");
        copy(L"page2.png", L"pages/2.png");
        open(first);
        command(Comic);
        wait();
        CHECK(app.image->format == L"見開き" && app.image->path.empty() && app.page_step() == 2);
        const auto pixels = app.image->frames[0].pixels;
        command(ComicRTL);
        wait();
        CHECK(app.image->frames[0].pixels != pixels);
        CHECK(app.image->height == 180);
        command(ComicHeight);
        wait();
        CHECK(app.image->height == 200);
        command(ComicOffset);
        wait();
        CHECK(app.index == 1);
        command(ComicOffsetBack);
        wait();
        CHECK(app.index == 0);
        command(ComicOffset);
        wait();
        command(Comic);
        wait();
        CHECK(app.image->width == 100);
        open(fixture(L"images.zip"));
        CHECK(app.files.size() == 3 && !app.archive.empty());
        auto previous = app.current_name;
        command(Next);
        wait();
        CHECK(app.current_name != previous && app.image->path.empty() && !app.original_file());
        command(Copy);
        CHECK(paste_image(app.hwnd).width == app.image->width);
    }
    void view_toggles_zoom_and_wheel() {
        open(fixture(L"display-alpha.png"));
        command(Actual);
        CHECK(app.view.scale == 1);
        command(ZoomIn);
        CHECK(app.view.scale == 1.25);
        command(ZoomOut);
        CHECK(std::abs(app.view.scale - 1) < 1e-8);
        command(FitWidth);
        CHECK(std::abs(app.view.scale - app.view.width / 512) < 1e-8);
        command(FitHeight);
        CHECK(std::abs(app.view.scale - app.view.height / 384) < 1e-8);
        command(FitCover);
        CHECK(app.view.fit == Fit::Cover);
        command(FitImage);
        CHECK(app.view.fit == Fit::Contain);
        texts.push_back(L"250");
        command(ZoomNumber);
        CHECK(app.view.scale == 2.5);
        command(KeepZoom);
        CHECK(app.settings.keep_zoom);
        command(Checker);
        CHECK(!app.settings.checker && !app.renderer.checkerboard);
        command(Nearest);
        CHECK(app.checked(Nearest)&&app.renderer.filter(app.view)==DisplaySimple);
        command(ArmDrag);
        CHECK(app.drag_armed && status().find(L"左ドラッグ") != std::wstring::npos);
        app.message(WM_KEYDOWN, VK_ESCAPE, 0);
        CHECK(!app.drag_armed);
        const auto before = app.view.scale;
        app.message(WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, WHEEL_DELTA), MAKELPARAM(100, 100));
        CHECK(app.view.scale > before);
        command(SelectMode);
        CHECK(app.selection_active());
    }
    void folders_slideshow_and_settings() {
        auto first = copy(L"alpha.png", L"parent/1/a.png");
        auto second = copy(L"alpha.png", L"parent/2/b.png");
        copy(L"alpha.png", L"parent/root.png");
        open(first);
        command(NextFolder);
        wait();
        CHECK(same_path(app.image->path, second));
        command(PreviousFolder);
        wait();
        CHECK(same_path(app.image->path, first));
        command(ParentFolder);
        wait();
        CHECK(app.image->path.filename() == L"root.png");
        directories.push_back(first.parent_path());
        command(ChildFolder);
        wait();
        CHECK(same_path(app.image->path, first));
        command(HistoryBase);
        wait();
        CHECK(app.image);
        texts.push_back(L"0.5");
        command(SlideInterval);
        CHECK(app.settings.interval == 500);
        command(Slideshow);
        CHECK(app.slideshow);
        app.message(WM_KEYDOWN, VK_ESCAPE, 0);
        CHECK(!app.slideshow);
        app.settings.save(app.config);
        Settings restored;
        restored.load(app.config);
        CHECK(restored.interval == 500 && restored.history == app.settings.history);
    }
    void pending_load_cannot_replace_undo_or_new_image() {
        auto first = copy(L"alpha.png", L"first/a.png");
        auto second = copy(L"page1.png", L"second/b.png");
        open(first);
        const auto original = app.image->frames[0].pixels;
        command(Negative);
        answers.push_back(IDNO);
        app.open(second);
        command(Undo);
        pump();
        CHECK(!app.loading && same_path(app.requested, first));
        CHECK(app.image->frames[0].pixels == original);
        app.open(second);
        texts.push_back(L"7, 9");
        command(NewImage);
        pump();
        CHECK(app.image->width == 7 && app.image->height == 9 && app.requested.empty());
        CHECK(app.files.empty() && app.folder.empty() && !app.loading && app.modified);
        command(Undo);
        CHECK(same_path(app.requested, first) && app.image->frames[0].pixels == original);
    }
    void file_metadata_and_registered_destinations() {
        auto original = copy(L"alpha.png", L"source/a.png");
        open(original);
        command(Info);
        CHECK(notices.back().find(L"更新日時:") != std::wstring::npos);
        CHECK(notices.back().find(L"3 × 2") != std::wstring::npos);
        command(Help);
        CHECK(notices.back().find(L"Ctrl+O") != std::wstring::npos);
        auto destination = temporary / L"dest";
        std::filesystem::create_directory(destination);
        app.dialogs.preferences_ui = [&](HWND, const Settings &initial, const PreferencesDialog::Apply &apply,
                                         PreferencePage page, int row) {
            CHECK(page == PreferencePage::FileOperations && row == 0);
            auto next = initial;
            next.copy_destinations[9] = destination;
            apply(next);
            return true;
        };
        command(ConfigureDestinations);
        CHECK(app.settings.copy_destinations[9] == destination.wstring());
        command(CopyDestBase + 9);
        CHECK(read_file(destination / L"a.png") == read_file(original));
        auto move = temporary / L"move";
        std::filesystem::create_directory(move);
        app.settings.move_destinations[0] = move;
        command(MoveDestBase);
        wait();
        CHECK(same_path(app.image->path, move / L"a.png"));
    }
    void independent_twenty_destinations_transfer_original_files() {
        for (int i = 0; i < 10; ++i) {
            const auto name = std::to_wstring(i);
            const auto source = copy(L"alpha.png", std::filesystem::path(L"sources") / name / L"a.png");
            const auto copied = temporary / L"copied" / name;
            const auto moved = temporary / L"moved" / name;
            std::filesystem::create_directories(copied);
            std::filesystem::create_directories(moved);
            app.settings.copy_destinations[i] = copied;
            app.settings.move_destinations[i] = moved;
            open(source);
            const auto bytes = read_file(source);
            const auto pixels = app.image->frames[0].pixels;
            command(CopyDestBase + i);
            CHECK(read_file(copied / L"a.png") == bytes && std::filesystem::exists(source));
            CHECK(same_path(app.image->path, source));
            command(MoveDestBase + i);
            CHECK(!std::filesystem::exists(source) && read_file(moved / L"a.png") == bytes);
            CHECK(read_file(copied / L"a.png") == bytes && same_path(app.image->path, moved / L"a.png"));
            CHECK(!app.loading && app.image->frames[0].pixels == pixels);
        }
    }
    void unregistered_destination_accept_cancel_and_empty() {
        for (bool move : {false, true}) {
            const auto source =
                copy(L"alpha.png", move ? L"unregistered-move/a.png" : L"unregistered-copy/a.png");
            const auto target = temporary / (move ? L"move-registered" : L"copy-registered");
            std::filesystem::create_directory(target);
            open(source);
            int called{};
            app.dialogs.file_transfer = [&](HWND, bool operation, const auto &shown,
                                            const FileTransferOptions &initial,
                                            int slot) -> std::optional<FileTransferChoice> {
                CHECK(operation == move && same_path(shown, source) && slot == 9 && initial.registered);
                ++called;
                if (called == 1)
                    return {};
                if (called == 2)
                    return FileTransferChoice{initial, {}, slot}; // Invalid injected response is ignored.
                auto next = initial;
                next.destinations[9] = target;
                return FileTransferChoice{next, target, slot};
            };
            const auto id = (move ? MoveDestBase : CopyDestBase) + 9;
            command(id);
            CHECK(called == 1 && std::filesystem::exists(source) &&
                  !std::filesystem::exists(target / L"a.png"));
            command(id);
            CHECK(called == 2 && std::filesystem::exists(source) &&
                  !std::filesystem::exists(target / L"a.png"));
            command(id);
            CHECK(called == 3 && read_file(target / L"a.png") == read_file(fixture(L"alpha.png")));
            CHECK(std::filesystem::exists(source) == !move);
            open(copy(L"alpha.png", move ? L"unregistered-move/b.png" : L"unregistered-copy/b.png"));
            command(id);
            CHECK(called == 3); // A registered slot bypasses registration.
        }
    }
    void registered_destination_cancels_pending_load_before_registration() {
        for (bool move : {false, true}) {
            const auto first = copy(L"alpha.png", move ? L"pending-move/a.png" : L"pending-copy/a.png");
            const auto other =
                copy(L"rgb.bmp", move ? L"pending-next-move/b.bmp" : L"pending-next-copy/b.bmp");
            const auto target = temporary / (move ? L"pending-move-target" : L"pending-copy-target");
            std::filesystem::create_directory(target);
            open(first);
            const auto pixels = app.image->frames[0].pixels;
            app.open(other);
            CHECK(app.loading && same_path(app.image->path, first));
            std::filesystem::path during_registration;
            app.dialogs.file_transfer = [&](HWND, bool operation, const auto &shown,
                                            const FileTransferOptions &initial,
                                            int slot) -> std::optional<FileTransferChoice> {
                CHECK(operation == move && same_path(shown, first) && slot == 6 && initial.registered);
                // A real modal dialog pumps loader completion messages while the user edits a path.
                wait();
                during_registration = app.image->path;
                auto next = initial;
                next.destinations[6] = target;
                return FileTransferChoice{next, target, slot};
            };
            command((move ? MoveDestBase : CopyDestBase) + 6);
            CHECK(same_path(during_registration, first));
            CHECK(read_file(target / L"a.png") == read_file(fixture(L"alpha.png")));
            CHECK(!std::filesystem::exists(target / L"b.bmp") && std::filesystem::exists(other));
            CHECK(std::filesystem::exists(first) == !move);
            pump();
            CHECK(app.image->frames[0].pixels == pixels && !app.loading);
        }
    }
    void transfer_dialog_proposal_options_and_rejected_overwrite() {
        for (bool move : {false, true}) {
            const auto source = copy(L"alpha.png", move ? L"proposal-move/a.png" : L"proposal-copy/a.png");
            const auto target =
                copy(L"rgb.bmp", move ? L"proposal-move-target/a.png" : L"proposal-copy-target/a.png");
            const auto old_target = read_file(target);
            open(source);
            const auto rgba = app.image->frames[0].pixels;
            const auto initial = app.transfer_options(move), other = app.transfer_options(!move);
            int called{};
            app.dialogs.file_transfer = [&](HWND, bool operation, const auto &shown,
                                            const FileTransferOptions &options,
                                            int slot) -> std::optional<FileTransferChoice> {
                CHECK(operation == move && same_path(shown, source) && slot == -1);
                ++called;
                if (called == 1)
                    return {};
                auto next = options;
                next.registered = true;
                next.confirm = true;
                next.basic_path = move ? L"../last-basic-move" : L"../last-basic-copy";
                next.destinations[4] = target.parent_path();
                return FileTransferChoice{next, target.parent_path(), 4};
            };
            command(move ? FileMove : FileCopy);
            CHECK(called == 1 && app.transfer_options(move) == initial && read_file(target) == old_target);
            answers.push_back(IDNO);
            command(move ? FileMove : FileCopy);
            CHECK(called == 2 && read_file(target) == old_target && std::filesystem::exists(source));
            CHECK(app.transfer_options(move).registered && app.transfer_options(move).confirm);
            CHECK(app.transfer_options(!move) == other);
            Settings saved;
            saved.load(temporary / L"settings.ini");
            CHECK((move ? saved.move_destinations : saved.copy_destinations)[4] ==
                  target.parent_path().wstring());
            CHECK((move ? saved.move_registered : saved.copy_registered));
            CHECK((move ? saved.move_basic_path : saved.copy_basic_path) ==
                  (move ? L"../last-basic-move" : L"../last-basic-copy"));
            answers.push_back(IDYES);
            command((move ? MoveDestBase : CopyDestBase) + 4);
            CHECK(called == 2 && read_file(target) == read_file(fixture(L"alpha.png")));
            CHECK(std::filesystem::exists(source) == !move && app.image->frames[0].pixels == rgba);
        }
    }
    void transfer_dialog_settings_failure_keeps_source_and_target() {
        const auto source = copy(L"alpha.png", L"proposal-failure/a.png");
        const auto target = temporary / L"proposal-failure-target";
        std::filesystem::create_directory(target);
        const auto invalid = temporary / L"settings-is-a-directory";
        std::filesystem::create_directory(invalid);
        app.set_config(invalid);
        open(source);
        const auto initial = app.transfer_options(false);
        app.dialogs.file_transfer = [&](HWND, bool, const auto &, const FileTransferOptions &options,
                                        int) -> std::optional<FileTransferChoice> {
            auto next = options;
            next.basic_path = target;
            next.confirm = false;
            return FileTransferChoice{next, target, -1};
        };
        THROWS(app.command(FileCopy));
        CHECK(app.transfer_options(false) == initial && same_path(app.image->path, source));
        CHECK(std::filesystem::exists(source) && !std::filesystem::exists(target / L"a.png"));
    }
    void transfer_dialog_changed_document_aborts_before_options_and_file() {
        const auto source = copy(L"alpha.png", L"proposal-original/a.png");
        const auto other = copy(L"rgb.bmp", L"proposal-other/b.bmp");
        const auto target = temporary / L"proposal-changed-target";
        std::filesystem::create_directory(target);
        open(source);
        const auto initial = app.transfer_options(false);
        app.dialogs.file_transfer = [&](HWND, bool, const auto &, const FileTransferOptions &options,
                                        int) -> std::optional<FileTransferChoice> {
            open(other); // Simulate an explicit external document change while a modal dialog is open.
            auto next = options;
            next.registered = true;
            return FileTransferChoice{next, target, -1};
        };
        THROWS(app.command(FileCopy));
        CHECK(app.transfer_options(false) == initial && same_path(app.image->path, other));
        CHECK(std::filesystem::exists(source) && std::filesystem::exists(other));
        CHECK(std::filesystem::is_empty(target));
    }
    void full_screen_preserves_separate_view_settings() {
        open(fixture(L"display-alpha.png"));
        command(Actual);
        app.message(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, app.top + 100));
        app.message(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(100, app.top + 100));
        app.message(WM_LBUTTONUP, 0, MAKELPARAM(100, app.top + 100));
        CHECK(!app.view.manual); // A stationary click must not cancel automatic fit.
        app.message(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, app.top + 100));
        app.message(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(117, app.top + 100));
        app.message(WM_LBUTTONUP, 0, MAKELPARAM(117, app.top + 100));
        const auto initial = app.view;
        command(Fullscreen);
        CHECK(app.full && app.top == 0 && app.bottom == 0);
        CHECK(!(GetWindowLongPtrW(app.hwnd, GWL_STYLE) & WS_CAPTION));
        texts.push_back(L"250");
        command(ZoomNumber);
        command(Fullscreen);
        CHECK(!app.full && app.view.scale == initial.scale && app.view.pan_x == initial.pan_x);
        CHECK(GetWindowLongPtrW(app.hwnd, GWL_STYLE) & WS_CAPTION);
        command(Fullscreen);
        CHECK(app.view.scale == 2.5);
        app.message(WM_KEYDOWN, VK_ESCAPE, 0);
        CHECK(!app.full);
        command(Topmost);
        CHECK(GetWindowLongPtrW(app.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST);
        command(Topmost);
        CHECK(!(GetWindowLongPtrW(app.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST));
    }
    void skip_and_slideshow_advance_without_wrap() {
        for (int i = 1; i <= 12; ++i)
            copy(L"alpha.png", std::to_wstring(i) + L".png");
        open(temporary / L"1.png");
        command(SkipNext);
        wait();
        CHECK(app.index == 10);
        command(SkipBack);
        wait();
        CHECK(app.index == 0);
        command(Slideshow);
        app.message(WM_TIMER, 2, 0);
        wait();
        CHECK(app.index == 1);
        command(Last);
        wait();
        CHECK(app.index == 11);
        app.message(WM_TIMER, 2, 0);
        CHECK(!app.slideshow && app.index == 11);
    }
    void zip_date_sort_and_close_settings() {
        open(fixture(L"dates.zip"));
        command(SortTime);
        CHECK(app.files[0].display_name == L"old.png" && app.files[1].display_name == L"new.png");
        command(Exit);
        pump();
        CHECK(!IsWindow(app.hwnd) && std::filesystem::exists(app.config));
    }
    void customization_changes_real_paste_undo_and_shortcut_dispatch() {
        const auto file = copy(L"alpha.png", L"base.png");
        open(file);
        auto next = app.settings;
        next.undo_enabled = next.remember_history = next.paste_new = false;
        next.set_keys(ArmDrag, {});
        next.set_keys(FlipH, {L"F12"});
        app.apply_settings(next);
        CHECK(app.settings.history.empty());
        command(History);
        CHECK(notices.back().find(L"ありません") != std::wstring::npos);
        Image source;
        source.width = source.height = 1;
        source.frames.push_back({{{0, 0, 255, 128}}, 100});
        copy_image(app.hwnd, source, 0);
        command(Paste);
        CHECK(app.image->width == 3 && app.image->height == 2 && same_path(app.image->path, file));
        CHECK(app.image->frames[0].pixels[0] == Pixel({127, 0, 128, 255}));
        CHECK(app.image->frames[0].pixels[1] == Pixel({0, 255, 0, 128}));
        CHECK(app.image->frames[0].pixels[2] == Pixel({0, 0, 255, 0}));
        CHECK(!app.undo_image && app.modified);
        const auto pasted = app.image->frames[0].pixels;
        command(Undo);
        CHECK(app.image->frames[0].pixels == pasted);
        app.message(WM_KEYDOWN, VK_F6, 0);
        CHECK(!app.drag_armed);
        app.message(WM_KEYDOWN, VK_F12, 0);
        CHECK(app.image->frames[0].pixels[0] == pasted[2]);
        app.message(WM_KEYDOWN, VK_F12, 0);
        CHECK(app.image->frames[0].pixels == pasted);
        next = app.settings;
        next.undo_enabled = next.paste_new = true;
        app.apply_settings(next);
        answers.push_back(IDNO); // Explicitly discard the modified current document when starting a new one.
        command(Paste);
        CHECK(app.image->width == 1 && app.image->height == 1 && app.image->path.empty());
        CHECK(app.image->frames[0].pixels[0] == Pixel({0, 0, 255, 128}));
        CHECK(app.undo_image != nullptr);
        command(Undo);
        CHECK(app.image->frames[0].pixels == pasted && same_path(app.image->path, file));
        Settings saved;
        saved.load(app.config);
        CHECK(saved.history.empty() && saved.keys_for(ArmDrag).empty() && saved.first_key(FlipH) == L"F12");
    }
    void mouse_click_drag_select_zoom_and_capture_cancel() {
        using G = MouseGesture;
        open(fixture(L"display-alpha.png"));
        texts.push_back(L"400");
        command(ZoomNumber);
        const auto original = app.image->frames[0].pixels;
        app.settings.mouse.assign(G::LeftClick, 0, 0, FlipH);
        app.settings.mouse.assign(G::RightClick, 0, 0, MouseNone);
        app.settings.mouse.assign(G::RightDrag, 0, 0, MouseZoom);
        app.settings.mouse.assign(G::X1Drag, 0, 0, MouseSelect);
        app.settings.mouse.drag_scroll = 200;
        app.settings.mouse.drag_threshold = 5;
        app.settings.mouse.drag_axis = 2;
        const auto at = [&](int x, int y) { return MAKELPARAM(x, app.top + y); };
        app.message(WM_LBUTTONDOWN, MK_LBUTTON, at(100, 100));
        app.message(WM_MOUSEMOVE, MK_LBUTTON, at(103, 100));
        app.message(WM_LBUTTONUP, 0, at(103, 100));
        CHECK(app.image->frames[0].pixels != original);
        command(Undo);
        CHECK(app.image->frames[0].pixels == original);
        texts.push_back(L"400");
        command(ZoomNumber);
        app.message(WM_LBUTTONDOWN, MK_LBUTTON, at(100, 100));
        app.message(WM_MOUSEMOVE, MK_LBUTTON, at(112, 100));
        CHECK(app.view.pan_x == 24);
        app.message(WM_LBUTTONUP, 0, at(112, 100));
        CHECK(app.image->frames[0].pixels == original);
        app.message(WM_RBUTTONDOWN, MK_RBUTTON, at(200, 100));
        const auto anchor = (200 - app.view.left()) / app.view.scale;
        app.message(WM_MOUSEMOVE, MK_RBUTTON, at(250, 100));
        CHECK(std::abs(app.view.scale - 4 * std::exp(.5)) < 1e-6);
        CHECK(std::abs((200 - app.view.left()) / app.view.scale - anchor) < 1e-6);
        app.message(WM_RBUTTONUP, 0, at(250, 100));
        app.message(WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), at(240, 180));
        app.message(WM_MOUSEMOVE, MK_XBUTTON1, at(310, 230));
        app.message(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), at(310, 230));
        CHECK(!app.selection.empty() && app.image->frames[0].pixels == original);
        for (int axis = 0; axis < 4; ++axis) {
            app.settings.mouse.drag_axis = axis;
            texts.push_back(L"400");
            command(ZoomNumber);
            const POINT end = axis == 0   ? POINT{200, 150}
                              : axis == 1 ? POINT{200, 250}
                              : axis == 2 ? POINT{250, 200}
                                          : POINT{150, 200};
            app.message(WM_RBUTTONDOWN, MK_RBUTTON, at(200, 200));
            app.message(WM_MOUSEMOVE, MK_RBUTTON, at(end.x, end.y));
            app.message(WM_RBUTTONUP, 0, at(end.x, end.y));
            CHECK(std::abs(app.view.scale - 4 * std::exp(.5)) < 1e-6);
        }
        app.message(WM_LBUTTONDOWN, MK_LBUTTON, at(120, 130));
        app.message(WM_MOUSEMOVE, MK_LBUTTON, at(150, 160));
        app.message(WM_CANCELMODE, 0, 0);
        CHECK(!app.mouse_buttons && !app.active_drag_button && !app.panning && GetCapture() != app.hwnd);
    }
    void mouse_chords_wheel_double_click_nonclient_and_activation() {
        using G = MouseGesture;
        for (int i = 1; i <= 3; ++i)
            copy(L"alpha.png", std::to_wstring(i) + L".png");
        open(temporary / L"1.png");
        int popups{};
        app.dialogs.popup = [&](HWND, HMENU menu, POINT) {
            CHECK(IsMenu(menu));
            ++popups;
            return 0u;
        };
        app.settings.mouse.assign(G::LeftClick, mouse_right, 0, Next);
        app.settings.mouse.assign(G::X1Click, 0, 0, Last);
        app.settings.mouse.assign(G::X2Double, 0, 0, Previous);
        const auto at = MAKELPARAM(100, app.top + 100);
        app.message(WM_RBUTTONDOWN, MK_RBUTTON, at);
        CHECK(app.mouse_buttons == mouse_right);
        app.message(WM_LBUTTONDOWN, MK_LBUTTON | MK_RBUTTON, at);
        CHECK(app.mouse_buttons == (mouse_left | mouse_right));
        app.message(WM_LBUTTONUP, MK_RBUTTON, at);
        wait();
        CHECK(app.index == 1);
        app.message(WM_RBUTTONUP, 0, at);
        CHECK(popups == 0);
        app.message(WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), at);
        app.message(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), at);
        wait();
        CHECK(app.index == 2);
        app.message(WM_NCXBUTTONUP, MAKEWPARAM(0, XBUTTON1), at);
        wait();
        CHECK(app.index == 1); // Nonclient X1 ignores the custom Last action.
        app.message(WM_NCXBUTTONUP, MAKEWPARAM(0, XBUTTON2), at);
        wait();
        CHECK(app.index == 2);
        command(Previous);
        wait();
        app.message(WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON2, XBUTTON2), at);
        app.message(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), at);
        wait();
        CHECK(app.index == 2);
        app.message(WM_XBUTTONDBLCLK, MAKEWPARAM(MK_XBUTTON2, XBUTTON2), at);
        app.message(WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), at);
        wait();
        CHECK(app.index == 1);
        app.settings.mouse.basic_wheel(false); // This case explicitly tests page navigation with the wheel.
        app.settings.mouse.slow_wheel = true;
        app.message(WM_MOUSEWHEEL, MAKEWPARAM(0, 120), at);
        CHECK(app.index == 1 && !app.loading);
        app.message(WM_MOUSEWHEEL, MAKEWPARAM(0, 120), at);
        wait();
        CHECK(app.index == 0);
        app.settings.mouse.ignore_activation = true;
        SetActiveWindow(nullptr);
        CHECK(app.message(WM_MOUSEACTIVATE, reinterpret_cast<WPARAM>(app.hwnd),
                          MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) == MA_ACTIVATEANDEAT);
    }
    void mouse_hover_scroll_and_ole_drag_path() {
        using G = MouseGesture;
        open(fixture(L"display-alpha.png"));
        texts.push_back(L"400");
        command(ZoomNumber);
        command(ToggleHover);
        CHECK(app.checked(ToggleHover));
        app.settings.mouse.hover_margin = 25;
        const auto y = app.top + static_cast<int>(app.view.height / 2);
        app.message(WM_MOUSEMOVE, 0, MAKELPARAM(0, y));
        app.message(WM_MOUSEMOVE, 0, MAKELPARAM(static_cast<int>(app.view.width * .25), y));
        CHECK(std::abs(app.view.left()) < 2);
        app.message(WM_MOUSEMOVE, 0, MAKELPARAM(static_cast<int>(app.view.width * .75), y));
        CHECK(std::abs(app.view.left() + app.view.canvas_w * app.view.scale - app.view.width) < 2);
        const auto pan = app.view.pan_x;
        app.settings.mouse.hover_margin = 50;
        app.message(WM_MOUSEMOVE, 0, MAKELPARAM(100, y));
        CHECK(app.view.pan_x == pan);
        app.settings.mouse.hover_margin = 0;
        app.settings.mouse.hover_min = 1;
        app.view.scale = (app.view.width + 2) / app.image->width;
        app.hover_seen = false;
        const auto center = static_cast<int>(app.view.width / 2);
        app.message(WM_MOUSEMOVE, 0, MAKELPARAM(center, y));
        app.message(WM_MOUSEMOVE, 0, MAKELPARAM(center + 1, y));
        CHECK(app.view.pan_x == -1);
        app.message(WM_MOUSEMOVE, 0, MAKELPARAM(center + 2, y));
        CHECK(app.view.pan_x == -1); // Minimum movement stays monotonic at the edge.
        app.settings.mouse.hover_min = 0;
        app.hover_seen = false;
        app.message(WM_MOUSEMOVE, 0, MAKELPARAM(center, y));
        app.message(WM_MOUSEMOVE, 0, MAKELPARAM(center + 1, y));
        CHECK(std::abs(app.view.pan_x + 2 / app.view.width) < 1e-6);
        std::filesystem::path exported;
        app.dialogs.file_drag = [&](const auto &path) { exported = path; };
        app.settings.mouse.assign(G::MiddleDrag, 0, 0, MouseFile);
        app.message(WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(100, y));
        app.message(WM_MOUSEMOVE, MK_MBUTTON, MAKELPARAM(120, y));
        app.message(WM_MBUTTONUP, 0, MAKELPARAM(120, y));
        CHECK(same_path(exported, app.image->path));
        command(FlipH);
        app.message(WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(100, y));
        app.message(WM_MOUSEMOVE, MK_MBUTTON, MAKELPARAM(120, y));
        app.message(WM_MBUTTONUP, 0, MAKELPARAM(120, y));
        CHECK(!same_path(exported, app.image->path) && exported.extension() == L".png");
        CHECK(load_image(exported).frames[0].pixels == app.image->frames[0].pixels);
    }
    void cp_native_107_seeded_commands_99_alpha_selection_undo_copy_save_and_ole() {
        app.settings.confirm_unsaved=false;app.settings.save_as_retarget=false;
        app.settings.rainbow_size=32;app.settings.rainbow_preview=false;
        const auto old_drag=app.dialogs.file_drag;size_t checked{},alpha{},exports{};
        for(const auto &reference:gvn_test::cp_references()) {
            const auto &item=reference.pixels;auto source=item.source,expected=item.expected;
            if(item.alpha) {
                for(size_t i=0;i<source.frames[0].pixels.size();++i){source.frames[0].pixels[i].a=static_cast<uint8_t>(i);expected.frames[0].pixels[i].a=item.expected_alpha[i];}
                ++alpha;
            }
            app.replace(std::make_shared<Image>(std::move(source)),false);const auto original=app.image,previous_undo=app.undo_image;
            if(item.selected)app.selection=item.rect;
            const auto rect=app.selection;const auto selected=[&]{return app.selection.x==rect.x&&app.selection.y==rect.y&&app.selection.w==rect.w&&app.selection.h==rect.h;};
            const auto empty=normalize_cp_code(reference.code).empty();std::shared_ptr<Image> candidate;
            app.dialogs.cp=[&](HWND owner,CpChoice,const auto &process,const auto &preview,const auto &current)->std::optional<CpResult> {
                CHECK(owner==app.hwnd&&current());const auto shown=app.image;
                if(!empty){candidate=process(reference.code);preview(candidate);CHECK(app.presented_image()==candidate.get());}
                CHECK(app.image==shown&&selected()&&!app.player.playing);
                return CpResult{{reference.code,checked%2==0},candidate};
            };
            command(CPMask);CHECK(selected()&&!app.mask_preview_image&&app.image->frames[0].pixels==expected.frames[0].pixels);
            CHECK(empty?(app.image==original&&app.undo_image==previous_undo&&!app.modified):(app.image==candidate&&app.undo_image==original&&app.modified));
            gvn_test::fl_check_indexed(*app.image,expected);
            if(item.selected||checked%23==0) {
                command(Copy);const auto copied=rect.empty()?expected:crop(expected,rect);CHECK(paste_image(app.hwnd).frames[0].pixels==copied.frames[0].pixels);
                const auto saved=temporary/(L"CP-"+std::to_wstring(checked)+L".png");paths.push_back(saved);command(SaveAs);CHECK(load_image(saved).frames[0].pixels==expected.frames[0].pixels);
                std::filesystem::path dragged;
                app.dialogs.file_drag=[&](const auto &file) {
                    dragged=file;CHECK(file.extension()==L".png"&&load_image(file).frames[0].pixels==expected.frames[0].pixels);
                    const auto object=file_data_object(file);FORMATETC f{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};CHECK(object->QueryGetData(&f)==S_OK);
                };
                app.begin_file_drag();app.dialogs.file_drag=old_drag;CHECK(!dragged.empty()&&std::filesystem::remove(dragged));++exports;
            }
            Settings saved;saved.load(app.config);CHECK(saved.cp_code==normalize_cp_code(reference.code)&&saved.cp_preview==(checked%2==0)&&saved.rainbow_size==32&&!saved.rainbow_preview);
            if(!empty){command(Undo);CHECK(app.image==original&&!app.modified&&selected());}
            ++checked;
        }
        CHECK(checked==107&&alpha==99&&exports>=8);
    }
    void cp_preview_cancel_and_empty_accept_preserve_dirty_undo_selection_and_rendering() {
        app.settings.confirm_unsaved=false;app.settings.nearest=true;open(fixture(L"cp-reference/png/full-a-source.png"));command(Negative);
        const auto original=app.image,undo=app.undo_image;const auto pixels=original->frames[0].pixels;app.selection={3,2,29,19};app.renderer.nearest=true;
        const auto printed=[&](const Image *expected=nullptr) {
            RECT r{};GetClientRect(app.hwnd,&r);const auto dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};
            info.bmiHeader={sizeof(BITMAPINFOHEADER),r.right,-r.bottom,1,32,BI_RGB};void *data{};
            const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);CHECK(dc&&bitmap&&data);const auto old=SelectObject(dc,bitmap);
            std::fill_n(static_cast<uint32_t*>(data),static_cast<size_t>(r.right)*r.bottom,0x12345678u);
            if(expected)CHECK(SUCCEEDED(app.renderer.print(app.hwnd,dc,expected,0,app.view,app.top,&app.selection)));
            else SendMessageW(app.hwnd,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
            GdiFlush();std::vector<uint32_t> result(static_cast<size_t>(r.right)*r.bottom);memcpy(result.data(),data,result.size()*4);
            SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return result;
        };
        const auto baseline=printed();std::weak_ptr<Image> released;
        app.dialogs.cp=[&](HWND,CpChoice initial,const auto &process,const auto &preview,const auto &current)->std::optional<CpResult> {
            CHECK(initial==(CpChoice{L"",true})&&current());const auto image=process(L"AB");released=image;preview(image);
            CHECK(app.image==original&&app.undo_image==undo&&app.modified&&app.selection.w==29);
            CHECK(printed()==printed(image.get())&&printed()!=baseline);preview({});CHECK(printed()==baseline);preview(image);return {};
        };
        command(CPMask);CHECK(released.expired()&&app.image==original&&app.undo_image==undo&&app.modified&&!app.mask_preview_image&&printed()==baseline);
        CHECK(app.settings.cp_code.empty()&&app.settings.cp_preview&&app.image->frames[0].pixels==pixels);
        app.dialogs.cp=[](HWND,CpChoice,const auto &,const auto &,const auto &)->std::optional<CpResult>{return CpResult{{L"",false},{}};};
        command(CPMask);CHECK(app.image==original&&app.undo_image==undo&&app.modified&&app.selection.w==29&&printed()==baseline);
        Settings saved;saved.load(app.config);CHECK(saved.cp_code.empty()&&!saved.cp_preview);
        command(Undo);CHECK(app.image==undo&&!app.modified&&app.selection.w==29);
    }
    void cp_actual_formats_current_frame_cancel_accept_and_empty_undo_keep_playing() {
        app.settings.confirm_unsaved=false;app.settings.animation_autoplay=false;
        for(const auto name:{L"alpha.png",L"psd-16-3.psd",L"disposal.gif",L"blend.apng"}) {
            open(fixture(name));if(app.image->animated()){command(FrameNext);command(Play);}
            const auto original=app.image;const auto frame=app.player.frame;const bool playing=app.player.playing;
            const auto expected=cp_image(*original,L"A",{},frame);size_t calls{};
            app.dialogs.cp=[&](HWND,CpChoice,const auto &process,const auto &preview,const auto &)->std::optional<CpResult> {
                ++calls;CHECK(!app.player.playing);const auto image=process(L"A");preview(image);CHECK(image->frames[0].pixels==expected.frames[0].pixels);return {};
            };
            if(original->animated()){answers.push_back(IDCANCEL);command(CPMask);CHECK(!calls);answers.push_back(IDOK);}
            command(CPMask);CHECK(calls==1&&app.image==original&&app.player.frame==frame&&app.player.playing==playing&&!app.modified);
            const auto prior=app.undo_image;
            app.dialogs.cp=[](HWND,CpChoice,const auto &,const auto &,const auto &)->std::optional<CpResult>{return CpResult{{L"",true},{}};};
            if(original->animated())answers.push_back(IDOK);
            command(CPMask);CHECK(app.image==original&&app.undo_image==prior&&app.player.frame==frame&&app.player.playing==playing&&!app.modified);
            app.dialogs.cp=[&](HWND,CpChoice,const auto &process,const auto &preview,const auto &)->std::optional<CpResult> {
                const auto image=process(L"A");preview(image);return CpResult{{L"a",true},image};
            };
            if(original->animated())answers.push_back(IDOK);
            command(CPMask);CHECK(app.image->frames.size()==1&&!app.player.playing&&app.image->frames[0].pixels==expected.frames[0].pixels);
            CHECK(!original->animated()||app.image->path.empty());command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==expected.frames[0].pixels);
            command(Undo);CHECK(app.image==original&&app.player.frame==frame&&app.player.playing==playing);app.player.playing=false;
        }
    }
    void cp_allocation_and_document_changes_preserve_transaction_boundaries() {
        app.settings.confirm_unsaved=false;open(fixture(L"alpha.png"));command(Negative);const auto original=app.image,undo=app.undo_image;
        app.selection={1,0,1,2};size_t allocations{};app.editing.allocating=[&](size_t){++allocations;throw std::bad_alloc();};
        app.dialogs.cp=[](HWND,CpChoice,const auto &process,const auto &,const auto &)->std::optional<CpResult>{return CpResult{{L"A",true},process(L"A")};};
        THROWS(command(CPMask));CHECK(allocations==1&&app.image==original&&app.undo_image==undo&&app.modified&&!app.mask_preview_image&&app.selection.w==1);
        app.editing.allocating={};app.editing.physical_bytes=[]{return uint64_t{1};};THROWS(command(CPMask));CHECK(app.image==original&&app.undo_image==undo&&!app.mask_preview_image);
        app.editing.physical_bytes=physical_memory_bytes;const auto next=std::make_shared<Image>(load_image(fixture(L"rgb.bmp")));
        app.dialogs.cp=[&](HWND,CpChoice,const auto &process,const auto &preview,const auto &current)->std::optional<CpResult> {
            const auto value=process(L"A");preview(value);CHECK(current());app.replace(next,false);CHECK(!current());preview(value);CHECK(!app.previewing_mask());THROWS(process(L"A"));return CpResult{{L"A",false},value};
        };
        command(CPMask);CHECK(app.image==next&&!app.modified&&!app.mask_preview_image&&app.settings.cp_code.empty()&&app.settings.cp_preview);
    }
    void cp_configured_keyboard_and_mouse_dispatch_same_code_and_alpha() {
        app.settings.confirm_unsaved=false;open(fixture(L"cp-reference/png/full-a-source.png"));const auto original=app.image;
        const auto expected=load_image(fixture(L"cp-reference/png/full-a-expected.png"));auto next=app.settings;
        next.cp_code=L"A";next.set_keys(CPMask,{L"F12"});next.mouse.assign(MouseGesture::MiddleClick,0,0,CPMask);app.apply_settings(next);
        app.dialogs.cp=[](HWND,CpChoice choice,const auto &process,const auto &preview,const auto &)->std::optional<CpResult> {
            CHECK(choice.code==L"A");const auto image=process(choice.code);preview(image);return CpResult{choice,image};
        };
        app.message(WM_KEYDOWN,VK_F12,0);CHECK(app.image->frames[0].pixels==expected.frames[0].pixels);command(Undo);CHECK(app.image==original);
        app.message(WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(100,app.top+100));app.message(WM_MBUTTONUP,0,MAKELPARAM(100,app.top+100));
        CHECK(app.image->frames[0].pixels==expected.frames[0].pixels);Settings saved;saved.load(app.config);CHECK(saved.first_key(CPMask)==L"F12"&&saved.cp_code==L"A");
    }
    void rainbow_native_27_cases_alpha_selection_undo_copy_save_and_ole() {
        app.settings.confirm_unsaved=false;app.settings.save_as_retarget=false;
        size_t checked{},alpha{};const auto old_drag=app.dialogs.file_drag;
        for(const auto &item:gvn_test::fl_references(L"rainbow-reference",27)) {
            auto source=item.source,expected=item.expected;
            if(item.alpha) {
                for(size_t i=0;i<source.frames[0].pixels.size();++i) {
                    source.frames[0].pixels[i].a=static_cast<uint8_t>(i);
                    expected.frames[0].pixels[i].a=item.expected_alpha[i];
                }
                ++alpha;
            }
            const auto path=temporary/(L"Rainbow-"+std::to_wstring(checked)+L".png");
            save_image(source,0,path,SaveOptions{});open(path);const auto original=app.image;
            if(item.selected)app.selection=item.rect;
            const auto rect=app.selection;const auto selected=[&] {
                return app.selection.x==rect.x&&app.selection.y==rect.y&&app.selection.w==rect.w&&app.selection.h==rect.h;
            };
            const int size=item.options.glass_size;std::shared_ptr<Image> candidate;
            app.dialogs.rainbow=[&](HWND owner,RainbowChoice,const auto &process,const auto &preview,const auto &current) {
                CHECK(owner==app.hwnd&&current());candidate=process(size);preview(candidate);
                CHECK(app.image==original&&!app.modified&&app.presented_image()==candidate.get());
                CHECK(app.presented_frame()==0&&selected());
                return std::optional<RainbowResult>{{{size,checked%2==0},candidate}};
            };
            command(RainbowMask);
            CHECK(app.image==candidate&&app.modified&&selected()&&!app.mask_preview_image);
            CHECK(app.image->frames[0].pixels==expected.frames[0].pixels);
            gvn_test::fl_check_indexed(*app.image,expected);
            command(Copy);const auto copied=rect.empty()?expected:crop(expected,rect);
            CHECK(paste_image(app.hwnd).frames[0].pixels==copied.frames[0].pixels);
            const auto saved=temporary/(L"Rainbow-saved-"+std::to_wstring(checked)+L".png");
            paths.push_back(saved);command(SaveAs);CHECK(load_image(saved).frames[0].pixels==expected.frames[0].pixels);
            app.dialogs.file_drag=[&](const auto &file) {
                CHECK(file!=path&&file.extension()==L".png"&&load_image(file).frames[0].pixels==expected.frames[0].pixels);
                const auto object=file_data_object(file);FORMATETC f{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
                CHECK(object->QueryGetData(&f)==S_OK);CHECK(std::filesystem::remove(file));
            };
            app.begin_file_drag();app.dialogs.file_drag=old_drag;
            Settings persisted;persisted.load(app.config);
            CHECK(persisted.rainbow_size==size&&persisted.rainbow_preview==(checked%2==0));
            command(Undo);CHECK(app.image==original&&selected()&&!app.mask_preview_image);++checked;
        }
        CHECK(checked==27&&alpha==13);
    }
    void rainbow_preview_print_cancel_and_shared_candidate_commit_are_transactional() {
        app.settings.confirm_unsaved=false;app.settings.nearest=true;
        open(fixture(L"rainbow-reference/png/full-4-source.png"));
        command(Negative);const auto original=app.image,undo=app.undo_image;
        const auto source=original->frames[0].pixels;app.selection={3,2,29,19};
        const auto settings=app.settings;
        // Compare actual WM_PRINTCLIENT output with the same renderer given an explicit image.
        const auto printed=[&](const Image *expected=nullptr) {
            RECT r{};GetClientRect(app.hwnd,&r);const auto dc=CreateCompatibleDC(nullptr);
            BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),r.right,-r.bottom,1,32,BI_RGB};
            void *data{};const auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);
            CHECK(dc&&bitmap&&data);const auto old=SelectObject(dc,bitmap);
            std::fill_n(static_cast<uint32_t*>(data),static_cast<size_t>(r.right)*r.bottom,0x12345678u);
            if(expected)CHECK(SUCCEEDED(app.renderer.print(app.hwnd,dc,expected,0,app.view,app.top,&app.selection)));
            else SendMessageW(app.hwnd,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
            GdiFlush();std::vector<uint32_t> result(static_cast<size_t>(r.right)*r.bottom);
            memcpy(result.data(),data,result.size()*4);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return result;
        };
        app.renderer.nearest=true;const auto baseline=printed();const auto baseline_view=app.view;
        std::weak_ptr<Image> released;
        app.dialogs.rainbow=[&](HWND,RainbowChoice initial,const auto &process,const auto &preview,const auto &current)->std::optional<RainbowResult> {
            CHECK(initial==(RainbowChoice{settings.rainbow_size,settings.rainbow_preview})&&current());
            const auto value=process(4);released=value;preview(value);
            CHECK(app.image==original&&app.undo_image==undo&&app.modified&&app.previewing_mask());
            CHECK(app.image->frames[0].pixels==source&&app.selection.w==29&&app.selection.h==19);
            const auto visible=printed();CHECK(visible==printed(value.get())&&visible!=baseline);
            preview({});CHECK(printed()==baseline&&!app.previewing_mask());
            preview(value);return {};
        };
        command(RainbowMask);
        CHECK(released.expired()&&app.image==original&&app.undo_image==undo&&app.modified);
        CHECK(!app.mask_preview_image&&printed()==baseline&&app.selection.w==29&&app.selection.h==19);
        CHECK(app.settings.rainbow_size==settings.rainbow_size&&app.settings.rainbow_preview==settings.rainbow_preview);
        std::shared_ptr<Image> accepted;
        app.dialogs.rainbow=[&](HWND,RainbowChoice,const auto &process,const auto &preview,const auto &)->std::optional<RainbowResult> {
            accepted=process(4);preview(accepted);return RainbowResult{{4,false},accepted};
        };
        command(RainbowMask);
        CHECK(app.image==accepted&&app.undo_image==original&&!app.mask_preview_image&&app.selection.w==29);
        CHECK(app.image->frames[0].pixels==rainbow_image(*original,4,{3,2,29,19}).frames[0].pixels);
        Settings saved;saved.load(app.config);CHECK(saved.rainbow_size==4&&!saved.rainbow_preview);
        command(Undo);CHECK(app.image==original);CHECK(app.modified);CHECK(app.selection.w==29);
        const auto restored=printed();
        if(restored!=baseline)std::cout<<"Rainbow Undo view: scale "<<baseline_view.scale<<" -> "<<app.view.scale
            <<", left "<<baseline_view.left()<<" -> "<<app.view.left()<<", top "<<baseline_view.top()<<" -> "<<app.view.top()
            <<", nearest "<<app.renderer.nearest<<", dragging "<<app.renderer.dragging_low<<'\n';
        CHECK(restored==baseline);
    }
    void rainbow_actual_formats_cancel_accept_and_undo_restore_playback_frame() {
        app.settings.confirm_unsaved=false;app.settings.animation_autoplay=false;
        for(const auto name:{L"alpha.png",L"psd-16-3.psd",L"disposal.gif",L"blend.apng"}) {
            open(fixture(name));if(app.image->animated()){command(FrameNext);command(Play);}
            const auto original=app.image;const auto frame=app.player.frame;const bool playing=app.player.playing;
            const auto expected=rainbow_image(*original,1,{},frame);
            size_t calls{};app.dialogs.rainbow=[&](HWND,RainbowChoice,const auto &process,const auto &preview,const auto &)->std::optional<RainbowResult> {
                ++calls;CHECK(!app.player.playing);const auto value=process(1);preview(value);
                CHECK(value->frames.size()==1&&value->frames[0].pixels==expected.frames[0].pixels);
                CHECK(!original->animated()||value->path.empty());return {};
            };
            if(original->animated()){answers.push_back(IDCANCEL);command(RainbowMask);CHECK(calls==0);answers.push_back(IDOK);}
            command(RainbowMask);
            CHECK(calls==1&&app.image==original&&app.player.frame==frame&&app.player.playing==playing&&!app.modified);
            app.dialogs.rainbow=[&](HWND,RainbowChoice,const auto &process,const auto &preview,const auto &)->std::optional<RainbowResult> {
                const auto value=process(1);preview(value);return RainbowResult{{1,true},value};
            };
            if(original->animated())answers.push_back(IDOK);
            command(RainbowMask);
            CHECK(app.image->frames.size()==1&&!app.player.playing&&app.image->frames[0].pixels==expected.frames[0].pixels);
            command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==expected.frames[0].pixels);
            command(Undo);CHECK(app.image==original&&app.player.frame==frame&&app.player.playing==playing);
            app.player.playing=false;
        }
    }
    void rainbow_memory_failure_and_changed_document_preserve_transaction_boundaries() {
        app.settings.confirm_unsaved=false;open(fixture(L"alpha.png"));command(Negative);
        const auto original=app.image,undo=app.undo_image;app.selection={1,0,1,2};
        size_t allocated{};app.editing.allocating=[&](size_t){++allocated;throw std::bad_alloc();};
        app.dialogs.rainbow=[&](HWND,RainbowChoice,const auto &process,const auto &,const auto &)->std::optional<RainbowResult> {
            return RainbowResult{{1,true},process(1)};
        };
        THROWS(command(RainbowMask));CHECK(allocated==1&&app.image==original&&app.undo_image==undo&&app.modified&&!app.mask_preview_image);
        CHECK(app.selection.x==1&&app.selection.w==1&&app.selection.h==2);
        app.editing.allocating={};app.editing.physical_bytes=[]{return uint64_t{1};};
        THROWS(command(RainbowMask));CHECK(app.image==original&&app.undo_image==undo&&!app.mask_preview_image);
        app.editing.physical_bytes=physical_memory_bytes;
        const auto next=std::make_shared<Image>(load_image(fixture(L"rgb.bmp")));
        app.dialogs.rainbow=[&](HWND,RainbowChoice,const auto &process,const auto &preview,const auto &current)->std::optional<RainbowResult> {
            const auto value=process(1);preview(value);CHECK(current());
            app.replace(next,false);CHECK(!current());preview(value);CHECK(!app.previewing_mask());
            THROWS(process(1));return RainbowResult{{1,true},value};
        };
        command(RainbowMask);CHECK(app.image==next&&!app.modified&&!app.mask_preview_image);
        CHECK(app.settings.rainbow_size==8&&app.settings.rainbow_preview);
    }
    void rainbow_and_meko_commands_support_configured_keyboard_and_mouse() {
        app.settings.confirm_unsaved=false;
        open(fixture(L"meko-reference/png/meko-forward-source.png"));
        app.dialogs.rainbow=[&](HWND,RainbowChoice,const auto &process,const auto &preview,const auto &)->std::optional<RainbowResult> {
            const auto value=process(4);preview(value);return RainbowResult{{4,true},value};
        };
        for(const auto id:{MekoForward,MekoReverse,RainbowMask}) {
            const auto original=app.image;auto next=app.settings;next.set_keys(id,{L"F12"});
            next.mouse.assign(MouseGesture::MiddleClick,0,0,id);app.apply_settings(next);
            const auto expected=id==RainbowMask?rainbow_image(*original,4):mask_image(*original,id==MekoForward?Mask::MekoForward:Mask::MekoReverse);
            app.message(WM_KEYDOWN,VK_F12,0);CHECK(app.image->frames[0].pixels==expected.frames[0].pixels);
            command(Undo);CHECK(app.image==original);
            app.message(WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(100,app.top+100));
            app.message(WM_MBUTTONUP,0,MAKELPARAM(100,app.top+100));CHECK(app.image->frames[0].pixels==expected.frames[0].pixels);
            Settings saved;saved.load(app.config);CHECK(saved.first_key(id)==L"F12");command(Undo);
            next=app.settings;next.set_keys(id,{});app.apply_settings(next);
        }
    }
    void meko_native_commands_alpha_selection_undo_copy_save_and_ole() {
        app.settings.confirm_unsaved=false;app.settings.save_as_retarget=false;
        app.settings.glass_size=999;app.settings.mask_swap=4;app.settings.fl_negative=false;
        app.settings.mask_negative_red=app.settings.mask_xor_red=false;
        size_t checked{}, alpha{};const auto old_drag=app.dialogs.file_drag;
        for(const auto &item:gvn_test::fl_references(L"meko-reference",56)) {
            auto source=item.source,expected=item.expected;
            if(item.alpha) {
                for(size_t i=0;i<source.frames[0].pixels.size();++i) {
                    source.frames[0].pixels[i].a=static_cast<uint8_t>(i);expected.frames[0].pixels[i].a=item.expected_alpha[i];
                }
                ++alpha;
            }
            const auto path=temporary/(L"Meko-"+std::to_wstring(checked)+L".png");
            save_image(source,0,path,SaveOptions{});open(path);const auto original=app.image;
            if(item.selected) app.selection=item.rect;
            const auto selection=app.selection;
            const auto selected=[&] {const auto r=app.selection;return r.x==selection.x&&r.y==selection.y&&r.w==selection.w&&r.h==selection.h;};
            const auto id=item.kind=="meko-reverse"?MekoReverse:MekoForward;
            const auto inverse=id==MekoForward?MekoReverse:MekoForward;
            command(id);CHECK(app.modified && selected() && app.image->frames[0].pixels==expected.frames[0].pixels);
            gvn_test::fl_check_indexed(*app.image,expected);
            if(item.selected || (source.width==81&&source.height==73)) {
                command(Copy);const auto copied=selection.empty()?expected:crop(expected,selection);
                CHECK(paste_image(app.hwnd).frames[0].pixels==copied.frames[0].pixels);
                const auto saved=temporary/(L"Meko-copy-"+std::to_wstring(checked)+L".png");paths.push_back(saved);command(SaveAs);
                CHECK(load_image(saved).frames[0].pixels==expected.frames[0].pixels);
                std::filesystem::path dragged;
                app.dialogs.file_drag=[&](const auto &file) {
                    dragged=file;CHECK(file.extension()==L".png"&&load_image(file).frames[0].pixels==expected.frames[0].pixels);
                    const auto object=file_data_object(file);FORMATETC f{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
                    CHECK(object->QueryGetData(&f)==S_OK);
                };
                app.begin_file_drag();app.dialogs.file_drag=old_drag;CHECK(!dragged.empty()&&dragged!=path);CHECK(std::filesystem::remove(dragged));
            }
            command(inverse);CHECK(selected()&&app.image->frames[0].pixels==original->frames[0].pixels);
            command(Undo);CHECK(selected()&&app.image->frames[0].pixels==expected.frames[0].pixels);
            command(Undo);CHECK(selected()&&app.image->frames[0].pixels==original->frames[0].pixels);++checked;
        }
        CHECK(checked==56&&alpha==42);
    }
    void meko_actual_formats_current_frame_and_cancel() {
        app.settings.animation_autoplay=false;app.settings.confirm_unsaved=false;app.settings.save_as_retarget=false;
        for(const auto name:{L"alpha.png",L"psd-16-3.psd",L"disposal.gif",L"blend.apng"}) {
            open(fixture(name));if(app.image->animated())command(FrameNext);
            const auto original=app.image;const auto frame=app.player.frame;
            const auto expected=mask_image(still_frame(*original,frame),Mask::MekoForward);
            if(original->animated()) {
                answers.push_back(IDCANCEL);command(MekoForward);CHECK(app.image==original&&app.player.frame==frame&&!app.modified);
                answers.push_back(IDOK);
            }
            command(MekoForward);CHECK(app.image->frames.size()==1&&!app.player.playing&&app.image->frames[0].pixels==expected.frames[0].pixels);
            command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==expected.frames[0].pixels);
            command(Undo);CHECK(app.image==original&&app.player.frame==frame&&!app.player.playing);
        }
    }
    void fl_actual_formats_current_frame_cancel_keyboard_and_mouse() {
        app.settings.animation_autoplay=false; app.settings.confirm_unsaved=false; app.settings.save_as_retarget=false;
        for (const auto name : {L"alpha.png", L"psd-16-3.psd", L"disposal.gif", L"blend.apng"}) {
            open(fixture(name));
            if (app.image->animated()) command(FrameNext);
            const auto original=app.image; const auto frame=app.player.frame;
            const auto pixels=original->frames[frame].pixels;
            const auto expected=mask_image(still_frame(*original,frame),Mask::FL);
            if (original->animated()) {
                answers.push_back(IDCANCEL); command(FLMask);
                CHECK(app.image==original && app.player.frame==frame && !app.modified);
                answers.push_back(IDOK);
            }
            command(FLMask); CHECK(app.image->frames.size()==1 && app.modified && !app.player.playing);
            CHECK(app.image->frames[0].pixels==expected.frames[0].pixels && original->frames[frame].pixels==pixels);
            command(Copy); CHECK(paste_image(app.hwnd).frames[0].pixels==expected.frames[0].pixels);
            const auto saved=temporary/(std::wstring(name)+L"-FL.png"); paths.push_back(saved); command(SaveAs);
            CHECK(load_image(saved).frames[0].pixels==expected.frames[0].pixels);
            command(Undo); CHECK(app.image==original && app.player.frame==frame && !app.player.playing);
        }
        open(fixture(L"fl-reference/png/full-rgb-49x43-invert-source.png"));
        auto next=app.settings; next.set_keys(FLMask,{L"F12"});
        next.mouse.assign(MouseGesture::MiddleClick,0,0,FLMask); app.apply_settings(next);
        const auto original=app.image;
        const auto expected=load_image(fixture(L"fl-reference/png/full-rgb-49x43-invert-expected.png"));
        app.message(WM_KEYDOWN,VK_F12,0); CHECK(app.image->frames[0].pixels==expected.frames[0].pixels);
        command(Undo); CHECK(app.image==original);
        app.message(WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(100,app.top+100));
        app.message(WM_MBUTTONUP,0,MAKELPARAM(100,app.top+100));
        CHECK(app.image->frames[0].pixels==expected.frames[0].pixels);
        Settings saved; saved.load(app.config); CHECK(saved.first_key(FLMask)==L"F12" && saved.fl_negative);
    }
    void fl_native_reference_commands_copy_save_drag_and_undo() {
        app.settings.confirm_unsaved=false; app.settings.save_as_retarget=false;
        const auto old_drag=app.dialogs.file_drag;
        size_t checked{}, alpha{};
        for (const auto &item : gvn_test::fl_references()) {
            if (item.kind!="fl") continue;
            auto source=item.source, expected=item.expected;
            if (item.alpha) {
                for (size_t i=0;i<source.frames[0].pixels.size();++i) {
                    source.frames[0].pixels[i].a=static_cast<uint8_t>(i);
                    expected.frames[0].pixels[i].a=item.expected_alpha[i];
                }
                ++alpha;
            }
            const auto file=temporary/(L"FL-source-"+std::to_wstring(checked)+L".png");
            save_image(source,0,file,SaveOptions{}); open(file);
            const auto original=app.image; const auto pixels=original->frames[0].pixels;
            app.settings.fl_negative=item.negative;
            app.settings.glass_size=item.options.glass_size; app.settings.mask_swap=item.options.swap;
            if (item.selected) app.selection=item.rect;
            const auto selection=app.selection;
            const auto selected=[&] { const auto r=app.selection; return r.x==selection.x && r.y==selection.y && r.w==selection.w && r.h==selection.h; };
            command(FLMask); CHECK(app.modified && app.image!=original && selected());
            CHECK(app.image->frames[0].pixels==expected.frames[0].pixels && original->frames[0].pixels==pixels);
            gvn_test::fl_check_indexed(*app.image,expected);
            if ((item.source.width==49 && item.source.height==43) || item.selected) {
                command(Copy);
                const auto copied=selection.empty()?expected:crop(expected,selection);
                CHECK(paste_image(app.hwnd).frames[0].pixels==copied.frames[0].pixels);
                const auto saved=temporary/(L"FL-copy-"+std::to_wstring(checked)+L".png");
                paths.push_back(saved); command(SaveAs); CHECK(load_image(saved).frames[0].pixels==expected.frames[0].pixels);
                size_t drags{};
                app.dialogs.file_drag=[&](const auto &path) {
                    CHECK(path.extension()==L".png" && load_image(path).frames[0].pixels==expected.frames[0].pixels);
                    const auto object=file_data_object(path); CHECK(object);
                    FORMATETC format{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
                    CHECK(object->QueryGetData(&format)==S_OK); ++drags;
                };
                app.begin_file_drag(); CHECK(drags==1);
                app.dialogs.file_drag=old_drag;
            }
            command(FLMask); CHECK(app.image->frames[0].pixels==pixels && selected());
            command(Undo); CHECK(app.image->frames[0].pixels==expected.frames[0].pixels && selected());
            command(Undo); CHECK(app.image->frames[0].pixels==pixels && selected());
            ++checked;
        }
        CHECK(checked==92 && alpha==72);
    }
    void selected_mask_commands_and_undo_keep_official_selection_rectangle() {
        app.settings.confirm_unsaved=false; app.settings.glass_size=5; app.settings.mask_swap=0;
        app.settings.mask_negative_green=app.settings.mask_negative_blue=false;
        app.settings.mask_xor_red=app.settings.mask_xor_blue=false;
        app.settings.mask_q0_xor=app.settings.mask_q0_flip_vertical=app.settings.mask_q0_flip_horizontal=true;
        app.settings.glass_remainder=true;
        size_t checked{};
        for (const auto &item : gvn_test::fl_references()) {
            if (item.kind=="fl") continue;
            const auto file=temporary/(L"selection-reference-"+std::to_wstring(checked)+L".png");
            save_image(item.source,0,file,SaveOptions{}); open(file); app.selection=item.rect;
            const auto original=app.image;
            const auto selected=[&] { const auto r=app.selection; return r.x==item.rect.x && r.y==item.rect.y && r.w==item.rect.w && r.h==item.rect.h; };
            const auto id=item.kind=="vertical"?GlassV:item.kind=="horizontal"?GlassH:item.kind=="negative"?MaskNegative
                         :item.kind=="xor"?Xor80:item.kind=="swap"?MaskSwap:item.kind=="q0"?Q0Mask:WinMask;
            command(id); CHECK(app.image->frames[0].pixels==item.expected.frames[0].pixels);
            CHECK(selected());
            command(Copy); CHECK(paste_image(app.hwnd).frames[0].pixels==crop(item.expected,item.rect).frames[0].pixels);
            command(id); CHECK(app.image->frames[0].pixels==original->frames[0].pixels && selected());
            command(Undo); CHECK(app.image->frames[0].pixels==item.expected.frames[0].pixels && selected());
            command(Undo); CHECK(app.image->frames[0].pixels==original->frames[0].pixels && selected());
            ++checked;
        }
        CHECK(checked==14);
    }
    void mask_psd_gif_apng_current_frame_and_selected_alpha_restore_with_undo() {
        app.settings.animation_autoplay = false; app.settings.confirm_unsaved = false;
        app.settings.save_as_retarget = false; app.settings.glass_size = 2;
        app.settings.mask_q0_xor = true; app.settings.glass_remainder = true;
        size_t count{};
        for (const auto name : {L"alpha.png", L"psd-16-3.psd", L"disposal.gif", L"blend.apng"}) {
            open(fixture(name));
            if (app.image->animated()) command(FrameNext);
            const auto original = app.image; const auto frame = app.player.frame;
            const auto pixels = original->frames.at(frame).pixels;
            const auto expected = mask_image(still_frame(*original, frame), Mask::Q0, app.settings.mask_options());
            if (original->animated()) {
                answers.push_back(IDCANCEL); command(Q0Mask);
                CHECK(app.image == original && app.player.frame == frame && !app.modified);
                answers.push_back(IDOK);
            }
            command(Q0Mask);
            CHECK(app.image->frames.size() == 1 && !app.player.playing && app.modified);
            CHECK(app.image->frames[0].pixels == expected.frames[0].pixels && original->frames.at(frame).pixels == pixels);
            command(Copy); CHECK(paste_image(app.hwnd).frames[0].pixels == expected.frames[0].pixels);
            const auto path = temporary / (L"mask-format-" + std::to_wstring(count++) + L".png");
            paths.push_back(path); command(SaveAs);
            CHECK(load_image(path).frames[0].pixels == expected.frames[0].pixels);
            command(Undo); CHECK(app.image == original && app.player.frame == frame && !app.player.playing);
        }
        const auto source = gvn_test::mask_reference_alpha_source();
        Image canvas; canvas.width = 35; canvas.height = 25;
        canvas.frames.push_back({std::vector<Pixel>(35 * 25, {17,39,61,33}), 100});
        for (uint32_t y = 0; y < 19; ++y)
            std::copy_n(source.frames[0].pixels.begin() + y * 29, 29, canvas.frames[0].pixels.begin() + (y + 2) * 35 + 3);
        const auto path = temporary / L"mask-selected.png"; save_image(canvas,0,path,SaveOptions{}); open(path);
        app.settings.glass_size = 3; app.settings.mask_q0_flip_vertical = app.settings.mask_q0_flip_horizontal = true;
        app.selection = {3,2,29,19}; const auto original = app.image;
        const auto native = load_image(fixture(L"mask-reference/png/q0-all.png"));
        auto expected = canvas;
        for (uint32_t y = 0; y < 19; ++y)
            std::copy_n(native.frames[0].pixels.begin() + y * 29, 29, expected.frames[0].pixels.begin() + (y + 2) * 35 + 3);
        command(Q0Mask); CHECK(app.image->frames[0].pixels == expected.frames[0].pixels);
        command(Undo); CHECK(app.image == original && app.selection.x==3 && app.selection.y==2 && app.selection.w==29 && app.selection.h==19);
    }
    void mask_reference_commands_preserve_alpha_undo_copy_save_and_ole() {
        const auto source = gvn_test::mask_reference_alpha_source();
        const auto path = temporary / L"mask-source Alpha.png";
        save_image(source, 0, path, SaveOptions{}); open(path);
        app.settings.confirm_unsaved = false; app.settings.save_as_retarget = false;
        const auto references = gvn_test::mask_references();
        size_t checked{}; bool exported{};
        for (const auto &item : references) {
            if (item.kind == "fl") continue;
            app.settings.glass_size = item.size; app.settings.glass_remainder = item.remainder;
            app.settings.mask_swap = item.swap;
            app.settings.mask_negative_red = (item.negative & 1) != 0;
            app.settings.mask_negative_green = (item.negative & 2) != 0;
            app.settings.mask_negative_blue = (item.negative & 4) != 0;
            app.settings.mask_xor_red = (item.xor_channels & 1) != 0;
            app.settings.mask_xor_green = (item.xor_channels & 2) != 0;
            app.settings.mask_xor_blue = (item.xor_channels & 4) != 0;
            app.settings.mask_q0_vertical = (item.q0 & 1) != 0;
            app.settings.mask_q0_horizontal = (item.q0 & 2) != 0;
            app.settings.mask_q0_negative = (item.q0 & 4) != 0;
            app.settings.mask_q0_xor = (item.q0 & 8) != 0;
            app.settings.mask_q0_flip_vertical = (item.q0 & 16) != 0;
            app.settings.mask_q0_flip_horizontal = (item.q0 & 32) != 0;
            const auto expected = gvn_test::mask_reference_alpha_expected(item, references);
            const UINT id = item.kind == "vertical" ? GlassV : item.kind == "horizontal" ? GlassH
                          : item.kind == "negative" ? MaskNegative : item.kind == "xor" ? Xor80
                          : item.kind == "swap" ? MaskSwap : item.kind == "q0" ? Q0Mask
                          : item.kind == "win" ? WinMask : Negative;
            const auto original = app.image;
            command(id);
            CHECK(app.image->frames[0].pixels == expected.frames[0].pixels && app.modified && app.undo_image == original);
            if (!exported && item.kind == "q0" && item.q0 == 63) {
                command(Copy); CHECK(paste_image(app.hwnd).frames[0].pixels == expected.frames[0].pixels);
                const auto saved = temporary / L"masked.png"; paths.push_back(saved); command(SaveAs);
                CHECK(load_image(saved).frames[0].pixels == expected.frames[0].pixels && app.image->path == path);
                std::filesystem::path dragged;
                const auto old_drag = app.dialogs.file_drag;
                app.dialogs.file_drag = [&](const auto &value) {
                    dragged = value;
                    const auto object = file_data_object(value);
                    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
                    CHECK(object->QueryGetData(&format) == S_OK);
                };
                app.begin_file_drag(); app.dialogs.file_drag = old_drag;
                CHECK(dragged != path && load_image(dragged).frames[0].pixels == expected.frames[0].pixels);
                CHECK(std::filesystem::remove(dragged)); exported = true;
            }
            command(Undo); CHECK(app.image == original && app.image->frames[0].pixels == source.frames[0].pixels);
            ++checked;
        }
        CHECK(checked == 560 && exported && texts.empty());
        app.dialogs.preferences_ui = [&](HWND owner, const Settings &, const PreferencesDialog::Apply &,
                                         PreferencePage page, int row) {
            CHECK(owner == app.hwnd && page == PreferencePage::Mask && row == -1); return false;
        };
        command(ConfigureMask);
    }
    void zoom_reference_transitions_through_numeric_input_and_app_commands_preserve_alpha() {
        open(fixture(L"alpha.png"));
        const auto image = app.image;
        const auto pixels = image->frames[0].pixels;
        size_t cases{};
        for (const auto &item : gvn_test::zoom_references()) {
            if (app.settings.zoom_mode != item.mode || app.settings.zoom_below != item.below ||
                app.settings.zoom_above != item.above) {
                auto next = app.settings;
                next.zoom_mode = item.mode; next.zoom_below = item.below; next.zoom_above = item.above;
                app.apply_settings(next);
            }
            texts.push_back(std::to_wstring(item.before * 100));
            command(ZoomNumber);
            CHECK(std::abs(app.view.scale - item.before) < 1e-9);
            command(item.enlarge ? ZoomIn : ZoomOut);
            if (std::abs(app.view.scale - item.after) >= 1e-9)
                std::cout << "APP_ZOOM_REFERENCE_MISMATCH " << item.label << '\n';
            CHECK(std::abs(app.view.scale - item.after) < 1e-9 && app.view.manual);
            CHECK(app.image == image && !app.modified && app.image->frames[0].pixels == pixels);
            ++cases;
        }
        CHECK(cases == 474 && texts.empty());
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == pixels);
    }
    void display_alignment_zoom_policies_and_fullscreen_bounds() {
        open(fixture(L"display-alpha.png"));
        auto next = app.settings;
        next.center_image = false;
        next.fit_max = 100;
        next.display_down = 3;
        next.display_up = 4;
        next.full_down = 2;
        next.full_up = 3;
        next.zoom_mode = 1;
        app.apply_settings(next);
        command(FitImage);
        CHECK(app.view.scale <= 1 && app.view.left() == 0 && app.view.top() == 0);
        command(Actual);
        command(ZoomIn);
        CHECK(app.view.scale == 1.25 && app.renderer.filter(app.view) == DisplayLanczos3);
        command(ZoomOut);
        command(ZoomOut);
        CHECK(std::abs(app.view.scale - .9) < 1e-9);
        next = app.settings;
        next.zoom_reduce = false;
        app.apply_settings(next);
        CHECK(app.renderer.filter(app.view) == DisplayLanczos3);
        command(Actual);
        texts.push_back(L"400");
        command(ZoomNumber);
        command(ScrollBottomRight);
        CHECK(app.view.left() == app.view.width - app.image->width * app.view.scale);
        CHECK(app.view.top() == app.view.height - app.image->height * app.view.scale);
        command(ScrollCenter);
        CHECK(app.view.left() == (app.view.width - app.image->width * app.view.scale) / 2);
        command(ScrollTopLeft);
        CHECK(app.view.left() == 0 && app.view.top() == 0);
        command(Fullscreen);
        CHECK(app.view.centered && app.view.fit_limit == 1);
        const auto check_bounds = [&] {
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
            const RECT expected = app.settings.full_hide_taskbar ? monitor.rcMonitor : monitor.rcWork;
            RECT actual{};
            GetWindowRect(app.hwnd, &actual);
            CHECK(EqualRect(&expected, &actual));
        };
        check_bounds();
        next = app.settings;
        next.full_hide_taskbar = false;
        app.apply_settings(next);
        check_bounds();
        next = app.settings;
        next.full_hide_cursor = true;
        app.apply_settings(next);
        const POINT cursor{50, app.top + 50};
        CHECK(app.pointer_cursor(cursor, false) == nullptr);
        CHECK(app.pointer_cursor(cursor, true) == LoadCursorW(nullptr, IDC_ARROW));
        app.loading = true;
        CHECK(app.pointer_cursor(cursor, false) == LoadCursorW(nullptr, IDC_WAIT));
        next.full_hide_wait = true;
        app.apply_settings(next);
        CHECK(app.pointer_cursor(cursor, false) == nullptr);
        CHECK(app.pointer_cursor(cursor, true) == LoadCursorW(nullptr, IDC_WAIT));
        app.loading = false;
        command(Fullscreen);
        CHECK(app.pointer_cursor(cursor, false) == LoadCursorW(nullptr, IDC_ARROW));
        CHECK(!app.view.centered && app.view.scale == 4 && app.view.left() == 0);
    }
    void window_startup_position_state_and_reopen() {
        struct Probe {
            App app;
            ~Probe() {
                if (IsWindow(app.hwnd))
                    DestroyWindow(app.hwnd);
            }
        };
        const auto path = temporary / L"startup.ini";
        const auto launch = [&] {
            auto probe = std::make_unique<Probe>();
            probe->app.set_config(path);
            CHECK(CreateWindowExW(0, L"GVN.AppAudit", L"startup probe", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                  20, 20, 900, 700, nullptr, nullptr, GetModuleHandleW(nullptr),
                                  &probe->app));
            probe->app.show_initial(SW_SHOWNORMAL);
            probe->app.show_initial(SW_SHOWNORMAL);
            return probe;
        };
        Settings initial;
        const auto work = App::monitor_info(app.hwnd).rcWork;
        initial.startup_position = 2;
        initial.window_x = work.left + 30;
        initial.window_y = work.top + 40;
        initial.window_width = 620;
        initial.window_height = 420;
        initial.save(path);
        auto probe = launch();
        RECT first{};
        GetWindowRect(probe->app.hwnd, &first);
        CHECK(first.left == initial.window_x && first.top == initial.window_y &&
              first.right - first.left == 620);
        WINDOWPLACEMENT saved{sizeof(saved)};
        GetWindowPlacement(probe->app.hwnd, &saved);
        probe->app.message(WM_CLOSE, 0, 0);
        probe.reset();
        pump();
        Settings stored;
        stored.load(path);
        CHECK(stored.last_window_valid && stored.last_state == 0 &&
              stored.last_x == saved.rcNormalPosition.left);
        stored.startup_position = 1;
        stored.save(path);
        probe = launch();
        RECT restored{};
        GetWindowRect(probe->app.hwnd, &restored);
        CHECK(EqualRect(&first, &restored));
        probe.reset();
        pump();
        stored.startup_state = 2;
        stored.save(path);
        probe = launch();
        CHECK(IsZoomed(probe->app.hwnd));
        probe->app.message(WM_CLOSE, 0, 0);
        probe.reset();
        pump();
        stored.load(path);
        CHECK(stored.last_state == 1);
        stored.startup_state = 0;
        stored.restore_maximized = true;
        stored.save(path);
        probe = launch();
        CHECK(IsZoomed(probe->app.hwnd));
        probe.reset();
        pump();
        stored.startup_state = 1;
        stored.save(path);
        probe = launch();
        CHECK(IsIconic(probe->app.hwnd));
        probe.reset();
        pump();
        stored.startup_state = 3;
        stored.save(path);
        probe = launch();
        CHECK(probe->app.full && !(GetWindowLongPtrW(probe->app.hwnd, GWL_STYLE) & WS_CAPTION));
        probe->app.message(WM_CLOSE, 0, 0);
        probe.reset();
        pump();
        stored.load(path);
        CHECK(stored.last_state == 2);
        stored.startup_state = 0;
        stored.restore_fullscreen = true;
        stored.save(path);
        probe = launch();
        CHECK(probe->app.full);
        probe.reset();
        pump();
        stored.startup_position = 2;
        stored.window_x = stored.window_y = 1000000;
        stored.restore_fullscreen = stored.restore_maximized = false;
        stored.window_center = false;
        stored.save(path);
        probe = launch();
        RECT actual{};
        GetWindowRect(probe->app.hwnd, &actual);
        const auto actual_work = App::monitor_info(probe->app.hwnd).rcWork;
        CHECK(actual.left >= actual_work.left && actual.top >= actual_work.top &&
              actual.right <= actual_work.right && actual.bottom <= actual_work.bottom);
    }
    void fit_window_measures_chrome_at_target_width_and_preserves_edited_alpha() {
        auto next=app.settings;
        next.confirm_unsaved=false;
        next.display_mode=5;
        next.toolbar=true;next.status=true;next.menu=false;
        next.toolbar_large=true;next.toolbar_line=true;
        next.window_toolbar_min=false;next.scrollbars=false;next.keep_zoom=false;
        app.apply_settings(next);
        open(fixture(L"selection-units.png"));
        command(RotateRight);
        const auto edited=app.image,undo=app.undo_image;
        const auto pixels=edited->frames[0].pixels;
        SIZE fitted{};
        for(int start_width:{900,136}) {
            SetWindowPos(app.hwnd,nullptr,0,0,start_width,600,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            command(DisplayWindowImage);
            RECT client{},outer{};
            CHECK(GetClientRect(app.hwnd,&client)&&GetWindowRect(app.hwnd,&outer));
            CHECK(app.view.scale==1 && app.view.width>=edited->width && app.view.height>=edited->height);
            CHECK(client.bottom-app.top-app.bottom>=static_cast<LONG>(edited->height));
            CHECK(app.image==edited&&app.undo_image==undo&&app.modified&&app.image->frames[0].pixels==pixels);
            if(!fitted.cx)fitted={outer.right-outer.left,outer.bottom-outer.top};
            else CHECK(fitted.cx==outer.right-outer.left&&fitted.cy==outer.bottom-outer.top);
        }
        next=app.settings;next.window_toolbar_min=true;app.apply_settings(next);
        command(DisplayWindowImage);
        RECT client{};CHECK(GetClientRect(app.hwnd,&client));
        CHECK(client.right>=app.toolbar.ideal_width()&&app.view.height>=edited->height&&app.view.scale==1);
        next=app.settings;next.toolbar=false;next.status=false;app.apply_settings(next);
        command(DisplayWindowImage);
        CHECK(app.top==0&&app.bottom==0&&app.view.width>=edited->width&&app.view.height>=edited->height);
        command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==pixels);
        CHECK(app.image==edited&&app.undo_image==undo&&app.modified&&notices.empty());
    }
    void fit_large_desktop_uses_target_chrome_and_preserves_pixels_in_both_branches() {
        auto next=app.settings;
        next.confirm_unsaved=false;next.keep_zoom=false;next.display_mode=5;
        next.initial_fit=static_cast<int>(Fit::Contain);
        next.toolbar=false;next.status=false;next.menu=false;next.scrollbars=true;
        next.window_toolbar_min=false;app.apply_settings(next);
        open(fixture(L"selection-units.png"));
        const auto still_image=app.image;
        command(DisplayLargeDesktop);
        CHECK(app.settings.display_mode==4&&app.view.scale==1&&app.image==still_image);
        const auto work=app.monitor_info(app.hwnd).rcWork;
        for(bool wide:{true,false}) {
            Image source;
            source.width=wide?static_cast<uint32_t>(work.right-work.left+64):20;
            source.height=wide?20:static_cast<uint32_t>(work.bottom-work.top+64);
            source.frames.push_back({std::vector<Pixel>(checked_pixels(source.width,source.height),Pixel{7,11,13,127}),100});
            source.frames[0].pixels.front()={31,59,61,0};
            source.frames[0].pixels.back()={67,71,73,255};
            app.replace(std::move(source));
            const auto image=app.image,undo=app.undo_image;
            const auto pixels=image->frames[0].pixels;
            command(DisplayLargeDesktop);
            RECT actual{};CHECK(GetWindowRect(app.hwnd,&actual));
            CHECK(EqualRect(&actual,&work)&&app.view.scale<1&&app.view.fit==Fit::Contain);
            CHECK(app.settings.display_mode==4&&app.image==image&&app.undo_image==undo&&app.modified);
            command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==pixels);
        }
        CHECK(notices.empty());
    }
    void window_styles_display_options_and_toolbar_rebuild() {
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        command(WindowMaximize);
        CHECK(IsZoomed(app.hwnd));
        command(WindowMaximize);
        CHECK(!IsZoomed(app.hwnd));
        command(WindowMinimize);
        CHECK(IsIconic(app.hwnd));
        command(WindowRestore);
        CHECK(!IsIconic(app.hwnd));
        command(WindowMaximize);
        command(WindowMinimize);
        app.remember_window();
        CHECK(app.settings.last_state == 1);
        command(WindowRestore);
        command(ToggleCaption);
        CHECK(!(GetWindowLongPtrW(app.hwnd, GWL_STYLE) & WS_CAPTION));
        command(ToggleOuterFrame);
        CHECK(!(GetWindowLongPtrW(app.hwnd, GWL_STYLE) & WS_THICKFRAME));
        command(ToggleInnerFrame);
        CHECK(GetWindowLongPtrW(app.hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE);
        command(ToggleCaption);
        CHECK((GetWindowLongPtrW(app.hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION);
        open(fixture(L"alpha.png"));
        command(DisplayLargeWindow);
        CHECK(app.view.scale == 1 && app.view.shrink_only);
        app.message(WM_SIZE, SIZE_RESTORED, 0);
        CHECK(app.view.scale == 1);
        command(DisplayUnchanged);
        CHECK(app.view.scale == 1);
        command(DisplayImageWindow);
        CHECK(app.view.scale > 1);
        auto centered_settings = app.settings;
        centered_settings.window_center = true;
        app.apply_settings(centered_settings);
        RECT before_fit{};
        GetWindowRect(app.hwnd, &before_fit);
        command(DisplayWindowImage);
        CHECK(app.view.scale == 1);
        RECT after_fit{};
        GetWindowRect(app.hwnd, &after_fit);
        CHECK(std::abs(before_fit.left + before_fit.right - after_fit.left - after_fit.right) <= 1);
        CHECK(std::abs(before_fit.top + before_fit.bottom - after_fit.top - after_fit.bottom) <= 1);
        auto smaller_window = app.settings;
        smaller_window.window_toolbar_min = false;
        app.apply_settings(smaller_window);
        command(DisplayWindowImage);
        RECT without_min{};
        GetWindowRect(app.hwnd, &without_min);
        CHECK(without_min.right - without_min.left < after_fit.right - after_fit.left);
        smaller_window.window_toolbar_min = true;
        app.apply_settings(smaller_window);
        command(DisplayWindowImage);
        smaller_window.menu = true;
        app.apply_settings(smaller_window);
        command(DisplayWindowImage);
        int menu_width = 0;
        for (int i = 0; i < GetMenuItemCount(GetMenu(app.hwnd)); ++i) {
            RECT item{};
            CHECK(GetMenuItemRect(app.hwnd, GetMenu(app.hwnd), i, &item));
            menu_width += item.right - item.left;
        }
        RECT menu_client{};
        GetClientRect(app.hwnd, &menu_client);
        CHECK(menu_client.right >= menu_width);
        command(DisplayLargeDesktop);
        CHECK(app.view.scale == 1);
        command(DisplayImageDesktop);
        CHECK(app.view.fit == Fit::Contain);
        auto next = app.settings;
        next.toolbar_large = next.toolbar_center = next.toolbar_line = true;
        next.topmost = true;
        next.full_topmost = false;
        app.apply_settings(next);
        const auto toolbar_bounds = [&] {
            RECT r{};
            GetWindowRect(app.toolbar.hwnd, &r);
            return r;
        }();
        const auto host_bounds = [&] {
            RECT r{};
            GetWindowRect(app.toolbar.bar_window(), &r);
            return r;
        }();
        CHECK(toolbar_bounds.left > host_bounds.left && std::abs(toolbar_bounds.left + toolbar_bounds.right -
                                                                 host_bounds.left - host_bounds.right) <= 1);
        CHECK(GetWindowLongPtrW(app.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST);
        command(Fullscreen);
        CHECK(!(GetWindowLongPtrW(app.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST));
        command(WindowRestore);
        CHECK(GetWindowLongPtrW(app.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST);
        auto independent_fit = app.settings;
        independent_fit.initial_fit = static_cast<int>(Fit::Width);
        independent_fit.full_initial_fit = static_cast<int>(Fit::Height);
        app.apply_settings(independent_fit);
        command(Fullscreen);
        CHECK(app.view.fit == Fit::Height);
        command(FitCover);
        CHECK(app.settings.full_initial_fit == static_cast<int>(Fit::Cover));
        command(Fullscreen);
        CHECK(app.view.fit == Fit::Width);
        CHECK(app.settings.initial_fit == static_cast<int>(Fit::Width));
        command(CloseImage);
        CHECK(!SendMessageW(app.toolbar.hwnd, TB_ISBUTTONENABLED, Copy, 0));
        app.toolbar.layout(1000, 192, true);
        int w{}, h{};
        ImageList_GetIconSize(
            reinterpret_cast<HIMAGELIST>(SendMessageW(app.toolbar.hwnd, TB_GETIMAGELIST, 0, 0)), &w, &h);
        CHECK(w == 48 && h == 48 && !SendMessageW(app.toolbar.hwnd, TB_ISBUTTONENABLED, Copy, 0));
        app.layout();
        app.update();
    }
    void bars_autohide_fullscreen_and_animation_layout() {
        auto next = app.settings;
        next.toolbar_auto = next.status_auto = true;
        next.animation_autoplay = false;
        next.toolbar_large = true;
        app.apply_settings(next);
        CHECK(!app.toolbar_shown && !app.status_shown);
        app.observe_bars({50, 1}, 0);
        CHECK(app.toolbar_shown && !app.status_shown);
        app.observe_bars({50, 1}, 1);
        CHECK(!app.toolbar_shown);
        app.observe_bars({50, 100}, 0);
        CHECK(!app.toolbar_shown);
        RECT client{};
        GetClientRect(app.hwnd, &client);
        app.observe_bars({50, client.bottom - 1}, 0);
        CHECK(app.status_shown);
        app.observe_bars({50, client.bottom - 1}, 2);
        CHECK(!app.status_shown);
        open(fixture(L"blend.apng"));
        command(Fullscreen);
        CHECK(!app.toolbar_shown && !app.status_shown);
        app.observe_bars({50, 1}, 0);
        CHECK(app.toolbar_shown);
        command(ToggleToolbar);
        CHECK(!app.settings.full_toolbar && app.settings.toolbar);
        command(Fullscreen);
        CHECK(app.settings.toolbar && !app.settings.full_toolbar);
        next = app.settings;
        next.toolbar_auto = next.status_auto = false;
        app.apply_settings(next);
        SetWindowPos(app.hwnd, nullptr, 0, 0, 360, 350, SWP_NOMOVE | SWP_NOZORDER);
        CHECK(app.play_height >= 90);
        RECT player{}, counter{}, icons{};
        GetClientRect(app.playback.hwnd, &player);
        GetWindowRect(app.playback.counter, &counter);
        MapWindowPoints(nullptr, app.playback.hwnd, reinterpret_cast<POINT *>(&counter), 2);
        GetWindowRect(app.playback.toolbar.hwnd, &icons);
        MapWindowPoints(nullptr, app.playback.hwnd, reinterpret_cast<POINT *>(&icons), 2);
        CHECK(counter.top >= icons.bottom && counter.right <= player.right &&
              counter.bottom <= player.bottom);
    }
    void minimum_window_large_bars_animation_and_visibility() {
        auto next = app.settings;
        next.animation_autoplay = false;
        next.toolbar_large = next.toolbar_line = next.scrollbars = true;
        next.window_toolbar_min = false;
        app.apply_settings(next);
        open(fixture(L"canvas/scroll.apng"));
        command(Actual);
        const auto verify = [&] {
            RECT area{}, bounds{}, player{}, status{};
            GetClientRect(app.hwnd, &area);
            GetWindowRect(app.hwnd, &bounds);
            GetWindowRect(app.playback.hwnd, &player);
            GetWindowRect(app.status, &status);
            MapWindowPoints(nullptr, app.hwnd, reinterpret_cast<POINT *>(&player), 2);
            MapWindowPoints(nullptr, app.hwnd, reinterpret_cast<POINT *>(&status), 2);
            CHECK(area.bottom - app.top - app.bottom >= MulDiv(64, GetDpiForWindow(app.hwnd), 96));
            CHECK(player.top >= app.top && player.bottom <= status.top);
            MINMAXINFO minimum{};
            SendMessageW(app.hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&minimum));
            CHECK(bounds.right - bounds.left >= minimum.ptMinTrackSize.x);
            CHECK(bounds.bottom - bounds.top >= minimum.ptMinTrackSize.y);
        };
        for (int width : {340, 360, 420, 560, 900}) {
            SetWindowPos(app.hwnd, nullptr, 0, 0, width, 240, SWP_NOMOVE | SWP_NOZORDER);
            verify();
        }
        command(ToggleCaption);
        command(ToggleInnerFrame);
        SetWindowPos(app.hwnd, nullptr, 0, 0, 340, 240, SWP_NOMOVE | SWP_NOZORDER);
        verify();
        command(ToggleMenu);
        verify();
        command(CloseImage);
        SetWindowPos(app.hwnd, nullptr, 0, 0, 340, 240, SWP_NOMOVE | SWP_NOZORDER);
        open(fixture(L"disposal.gif"));
        verify(); // An animation opened into an existing short window also expands.
        command(Fullscreen);
        const auto monitor = app.monitor_info(app.hwnd).rcMonitor;
        RECT full_bounds{};
        GetWindowRect(app.hwnd, &full_bounds);
        CHECK(EqualRect(&full_bounds, &monitor));
    }
    void captionless_control_drag_respects_customization_and_cleans_capture() {
        unsigned requests{};
        const SUBCLASSPROC probe = [](HWND window, UINT message, WPARAM hit, LPARAM point, UINT_PTR,
                                      DWORD_PTR data) -> LRESULT {
            if (message == WM_NCLBUTTONDOWN) {
                CHECK(hit == HTCAPTION);
                ++*reinterpret_cast<unsigned *>(data);
                return 0; // The real OS move loop is a separately documented test boundary.
            }
            return DefSubclassProc(window, message, hit, point);
        };
        CHECK(SetWindowSubclass(app.hwnd, probe, 91, reinterpret_cast<DWORD_PTR>(&requests)));
        const auto press = [&](WPARAM flags = MK_CONTROL) {
            const auto point = MAKELPARAM(50, app.top + 30);
            app.message(WM_LBUTTONDOWN, flags | MK_LBUTTON, point);
            app.message(WM_LBUTTONUP, flags, point);
            CHECK(app.mouse_buttons == 0 && app.active_drag_action == 0 && GetCapture() != app.hwnd);
        };
        press();
        CHECK(requests == 0); // Caption present.
        command(ToggleCaption);
        press();
        CHECK(requests == 1); // Works with no image loaded.
        press(0);
        press(MK_CONTROL | MK_SHIFT);
        press(MK_CONTROL | MK_RBUTTON);
        CHECK(requests == 1);
        open(fixture(L"display-alpha.png"));
        press();
        CHECK(requests == 2);
        for (unsigned action : {MouseNone, MousePan, MouseZoom, MouseSelect}) {
            app.settings.mouse.assign(MouseGesture::LeftDrag, 0, 1, action);
            press();
            CHECK(requests == 2); // Explicit Ctrl binding wins, including explicit pan and none.
        }
        app.settings.mouse.assign(MouseGesture::LeftDrag, 0, 1, MouseInherit);
        app.settings.mouse.assign(MouseGesture::LeftDrag, 0, 0, MouseZoom);
        press();
        CHECK(requests == 2); // A customized base action is inherited.
        app.settings.mouse.assign(MouseGesture::LeftDrag, 0, 0, MousePan);
        command(SelectMode);
        press();
        command(SelectMode);
        command(ArmDrag);
        press();
        app.message(WM_KEYDOWN, VK_ESCAPE, 0);
        CHECK(requests == 2);
        command(Fullscreen);
        press();
        CHECK(requests == 2);
        command(Fullscreen);
        press();
        CHECK(requests == 3);
        RemoveWindowSubclass(app.hwnd, probe, 91);
    }
    void native_canvas_scrollbars_ranges_keyboard_and_fullscreen() {
        open(fixture(L"display-alpha.png"));
        CHECK(!app.canvas_bars.horizontal && !app.canvas_bars.vertical);
        command(ToggleScrollbars);
        texts.push_back(L"400");
        command(ZoomNumber);
        CHECK(app.canvas_bars.horizontal && app.canvas_bars.vertical);
        const auto native_key = [&](HWND target, UINT key) {
            MSG message{};
            message.hwnd = target;
            message.message = WM_KEYDOWN;
            message.wParam = key;
            CHECK(!app.route_key(message));
            SendMessageW(target, WM_KEYDOWN, key, 0);
            SendMessageW(target, WM_KEYUP, key, 0);
        };
        native_key(app.canvas_horizontal, VK_HOME);
        native_key(app.canvas_vertical, VK_HOME);
        CHECK(app.view.left() == 0 && app.view.top() == 0);
        native_key(app.canvas_horizontal, VK_RIGHT);
        CHECK(app.view.left() == -MulDiv(16, GetDpiForWindow(app.hwnd), 96));
        native_key(app.canvas_horizontal, VK_END);
        native_key(app.canvas_vertical, VK_END);
        CHECK(std::abs(app.view.left() + app.image->width * app.view.scale - app.view.width) < 1);
        CHECK(std::abs(app.view.top() + app.image->height * app.view.scale - app.view.height) < 1);
        command(Fullscreen);
        CHECK(!app.canvas_bars.horizontal && !app.canvas_bars.vertical);
        command(ToggleScrollbars);
        CHECK(app.settings.full_scrollbars && app.settings.scrollbars);
        texts.push_back(L"5000");
        command(ZoomNumber);
        CHECK(app.canvas_bars.horizontal && app.canvas_bars.vertical);
        command(Fullscreen);
        command(FitImage);
        CHECK(!app.canvas_bars.horizontal && !app.canvas_bars.vertical);
        // A 32-bit range must work beyond WM_HSCROLL's 16-bit message payload.
        app.image = std::make_shared<Image>();
        app.image->width = 2000;
        app.image->height = 1;
        app.image->frames = {{{std::vector<Pixel>(2000, {1, 2, 3, 255})}, 100}};
        app.view.set_image(2000, 1);
        app.view.apply_fit(Fit::Actual);
        app.view.zoom_at(50, 0, 0);
        app.update();
        native_key(app.canvas_horizontal, VK_END);
        const int end = GetScrollPos(app.canvas_horizontal, SB_CTL);
        CHECK(end > 65535 && std::abs(app.view.left() + end) < 1);
        native_key(app.canvas_horizontal, VK_PRIOR);
        CHECK(GetScrollPos(app.canvas_horizontal, SB_CTL) < end);
        native_key(app.canvas_horizontal, VK_HOME);
        CHECK(app.view.left() == 0);
        CHECK(!app.canvas_bars.vertical);
    }
    void caption_root_and_unc_match_reference_and_keep_edited_frame_undo() {
        open(fixture(L"alpha.png"));command(RotateRight);
        const auto edited=app.image,undo=app.undo_image;
        const auto pixels=edited->frames[0].pixels;
        const auto original_path=edited->path,original_requested=app.requested;
        const auto original_name=app.current_name;
        const int original_mode=app.settings.title_mode;
        std::ifstream input(fixture(L"caption-paths.tsv"));CHECK(input.good());
        std::string row;unsigned count{};
        while(std::getline(input,row)) {
            if(row.empty()||row[0]=='#')continue;
            std::istringstream columns(row);std::vector<std::wstring> fields;std::string field;
            while(std::getline(columns,field,'\t'))fields.push_back(widen(field));
            CHECK(fields.size()==4);
            const std::filesystem::path source(fields[0]);
            const int modes[]{1,2,0};
            // These are recorded source identities, not files to open or modify.
            // The normal-process probe separately decodes actual root/UNC files.
            edited->path=source;app.current_name=source.filename().wstring();app.archive.clear();
            for(int i=0;i<3;++i) {app.settings.title_mode=modes[i];CHECK(app.caption_name()==fields[i+1]);}
            app.settings.title_mode=3;CHECK(app.caption_name()==source.stem().wstring());
            app.settings.title_mode=4;CHECK(app.caption_name().empty());
            auto archive_path=source;archive_path.replace_extension(L".zip");
            edited->path.clear();app.archive=archive_path;
            app.requested=std::filesystem::path(archive_path.wstring()+L"|1");
            app.current_name=archive_path.filename().wstring()+L" / nested/page.v2.png";
            for(int i=0;i<3;++i) {
                auto expected=fields[i+1];const auto old_name=source.filename().wstring();
                const auto at=expected.rfind(old_name);CHECK(at!=std::wstring::npos);
                expected.replace(at,old_name.size(),archive_path.filename().wstring());
                app.settings.title_mode=modes[i];CHECK(app.caption_name()==expected+L" / nested/page.v2.png");
            }
            CHECK(app.image==edited&&app.undo_image==undo&&app.modified&&edited->frames[0].pixels==pixels);
            ++count;
        }
        CHECK(count==4);
        edited->path=original_path;app.requested=original_requested;app.current_name=original_name;
        app.archive.clear();app.settings.title_mode=original_mode;
        command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==pixels);
        CHECK(app.image==edited&&app.undo_image==undo&&app.modified&&notices.empty());
    }
    void caption_overlay_visibility_interactions_and_raw_clipboard() {
        open(fixture(L"display-alpha.png"));
        auto next = app.settings;
        next.overlay_full = next.overlay_window = true;
        next.overlay_animation = false;
        next.animation_autoplay = false;
        next.overlay_position = 8;
        next.overlay_background = 1;
        next.overlay_family = L"Arial";
        next.overlay_points = 180;
        next.overlay_weight = 700;
        next.overlay_italic = next.overlay_underline = next.overlay_strikeout = true;
        next.overlay_foreground = 0x123456;
        next.overlay_backcolor = 0xabcdef;
        app.apply_settings(next);
        CHECK(!app.renderer.overlay_visible);
        command(ToggleCaption);
        CHECK(app.renderer.overlay_visible);
        wchar_t title[4096]{};
        GetWindowTextW(app.hwnd, title, 4096);
        CHECK(app.renderer.overlay.text == title);
        CHECK(app.renderer.overlay.family == L"Arial" && app.renderer.overlay.weight == 700);
        CHECK(app.renderer.overlay.italic && app.renderer.overlay.underline &&
              app.renderer.overlay.strikeout);
        CHECK(std::abs(app.renderer.overlay.font_size - 180 * GetDpiForWindow(app.hwnd) / 720.0) < .001);
        command(SelectMode);
        CHECK(!app.renderer.overlay_visible);
        command(Fullscreen);
        CHECK(app.renderer.overlay_visible);
        command(Fullscreen);
        CHECK(!app.renderer.overlay_visible);
        command(SelectMode);
        texts.push_back(L"400");
        command(ZoomNumber);
        app.pointer_down(0, {100, app.top + 100}, 0, false);
        app.pointer_move({130, app.top + 110}, MK_LBUTTON);
        CHECK(!app.renderer.overlay_visible && app.active_drag_action == MousePan);
        app.pointer_up(0, {130, app.top + 110});
        CHECK(app.renderer.overlay_visible);
        command(ScrollRight);
        CHECK(!app.renderer.overlay_visible);
        const auto until = GetTickCount64() + 1500;
        while (!app.renderer.overlay_visible && GetTickCount64() < until) {
            pump();
            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
        CHECK(app.renderer.overlay_visible);
        const auto source = app.image->frames[0].pixels;
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == source);
        open(fixture(L"canvas/scroll.apng"));
        CHECK(!app.renderer.overlay_visible && !app.player.playing);
        next = app.settings;
        next.overlay_animation = true;
        app.apply_settings(next);
        CHECK(app.renderer.overlay_visible);
        command(Play);
        CHECK(app.renderer.overlay_visible && app.player.playing);
        command(Play);
        command(FrameNext);
        command(Copy);
        CHECK(paste_image(app.hwnd).frames[0].pixels == app.image->frames[app.player.frame].pixels);
    }
    void configurable_scroll_fit_read_random_and_popups() {
        for (int i = 1; i <= 3; ++i)
            copy(L"display-alpha.png", std::to_wstring(i) + L".png");
        open(temporary / L"2.png");
        texts.push_back(L"400");
        command(ZoomNumber);
        command(ScrollTopLeft);
        CHECK(app.view.left() == 0 && app.view.top() == 0);
        command(ScrollRight);
        CHECK(app.view.left() == -60);
        command(ScrollDown);
        CHECK(app.view.top() == -60);
        command(ScrollLeft);
        command(ScrollUp);
        CHECK(app.view.left() == 0 && app.view.top() == 0);
        for (const UINT id : {ScrollTop, ScrollBottom, ScrollLeftEdge, ScrollRightEdge, ScrollTopRight,
                              ScrollBottomLeft, ScrollBottomRight, ScrollCenter})
            command(id);
        CHECK(app.view.pan_x == 0 && app.view.pan_y == 0);
        command(ReadScrollLeft);
        command(ReadScrollRight);
        CHECK(app.view.pan_x == 0);
        command(ToggleActualFit);
        CHECK(app.view.scale == 1 && !app.view.manual);
        command(ToggleActualFit);
        CHECK(app.view.fit == Fit::Contain && !app.view.manual);
        command(ToggleFitActual);
        CHECK(app.view.scale == 1 && !app.view.manual);
        command(ToggleFitActual);
        CHECK(app.view.fit == Fit::Contain && !app.view.manual);
        command(ReadNext);
        wait();
        CHECK(app.index == 2);
        app.comic = true;
        command(ReadNext);
        wait();
        CHECK(app.index == 1);
        command(ReadPrevious);
        wait();
        CHECK(app.index == 2);
        app.comic = false;
        const auto before = app.index;
        command(RandomJump);
        wait();
        CHECK(app.index != before);
        int popups{};
        app.dialogs.popup = [&](HWND, HMENU menu, POINT) {
            CHECK(IsMenu(menu) && GetMenuItemCount(menu) > 0);
            ++popups;
            return 0u;
        };
        for (UINT id = PopupAll; id <= PopupExternal; ++id)
            command(id);
        CHECK(popups == 13);
        const auto checker = app.settings.checker;
        app.dialogs.popup = [](HWND, HMENU, POINT) { return static_cast<UINT>(Checker); };
        command(PopupView);
        CHECK(app.settings.checker != checker);
    }
void check_rotation_frame(const gvn::Image &actual, const gvn::Image &expected, size_t frame) {
    CHECK(actual.width == expected.width && actual.height == expected.height && actual.frames.size() == 1);
    CHECK(actual.frames[0].pixels == expected.frames.at(frame).pixels);
}
void check_rotation_shell_file(const std::filesystem::path &path) {
    const auto object = gvn::file_data_object(path);
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    CHECK(object->QueryGetData(&format) == S_OK);
    STGMEDIUM medium{};
    CHECK(object->GetData(&format, &medium) == S_OK);
    const auto drop = static_cast<HDROP>(medium.hGlobal);
    wchar_t name[32768]{};
    const auto count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
    const auto length = DragQueryFileW(drop, 0, name, 32768);
    const auto received = std::filesystem::path(name);
    ReleaseStgMedium(&medium);
    CHECK(count == 1 && length && gvn::same_path(received, path));
}
// Original-file drag is still available from the main toolbar/F6. The new
// filename/format export panel has its own native-control and CF_HDROP coverage.
void rotation_status_drag(gvn::AppAudit &audit) { audit.app.begin_file_drag(); }
std::filesystem::path open_rotation_case(gvn::AppAudit &audit, const gvn_test::RotationFormatCase &item) {
    using namespace gvn;
    audit.command(ViewRotateNone);
    audit.wait();
    const auto source = std::filesystem::path(L"view-rotation-formats") / item.source;
    const auto relative = std::filesystem::path(L"rotation-reference") /
                          (item.kind + "-" + item.shape + "-" + std::to_string(item.mode)) /
                          ("source" + source.extension().string());
    const auto path = audit.copy(source.c_str(), relative);
    audit.open(path);
    audit.command(ViewRotateNone + item.mode);
    audit.wait();
    return path;
}
void copy_save_rotation_frame(gvn::AppAudit &audit, size_t frame, const std::filesystem::path &target) {
    using namespace gvn;
    const auto displayed = audit.app.image;
    CHECK(displayed && audit.app.player.frame == frame && !audit.app.player.playing);
    audit.command(Copy);
    check_rotation_frame(paste_image(audit.app.hwnd), *displayed, frame);
    audit.paths.push_back(target);
    audit.command(SaveAs);
    check_rotation_frame(load_image(target), *displayed, frame);
    CHECK(audit.app.image == displayed && audit.app.player.frame == frame && !audit.app.player.playing);
}
void rotation_reference_static_copy_save_and_original_file_ole_contract() {
    using namespace gvn;
    auto &audit = *this;
    auto &app = audit.app;
    app.settings.animation_autoplay = false;
    app.settings.confirm_unsaved = false;
    app.settings.save_as_retarget = false; // Exercise the new-profile copy-save contract.
    size_t count{}, drags{};
    for (const auto &item : gvn_test::rotation_format_cases()) {
        if (item.frames != 1)
            continue;
        const auto path = open_rotation_case(audit, item);
        const auto bytes = read_file(path);
        const auto decoded = load_source(path);
        gvn_test::check_rotation_format(*app.image, decoded, item, path);
        copy_save_rotation_frame(audit, 0, path.parent_path() / L"displayed.png");
        app.dialogs.file_drag = [&](const auto &exported) {
            CHECK(same_path(exported, path) && !app.player.playing);
            check_rotation_shell_file(exported);
            CHECK(read_file(exported) == bytes);
            ++drags;
        };
        rotation_status_drag(audit);
        CHECK(read_file(path) == bytes && !app.modified);
        ++count;
    }
    CHECK(count == 72 && drags == 72 && audit.notices.empty());
}
void rotation_reference_animation_current_frame_alpha_copy_save_and_png_ole() {
    using namespace gvn;
    auto &audit = *this;
    auto &app = audit.app;
    app.settings.animation_autoplay = false;
    app.settings.confirm_unsaved = false;
    app.settings.save_as_retarget = false; // Exercise the new-profile copy-save contract.
    size_t count{}, drags{};
    for (const auto &item : gvn_test::rotation_format_cases()) {
        if (item.frames != 2)
            continue;
        const auto path = open_rotation_case(audit, item);
        const auto bytes = read_file(path);
        const auto decoded = load_source(path);
        gvn_test::check_rotation_format(*app.image, decoded, item, path);
        CHECK(app.player.frame == 0 && !app.player.playing);
        for (size_t frame = 0; frame < 2; ++frame) {
            if (frame)
                audit.command(FrameNext);
            copy_save_rotation_frame(audit, frame, path.parent_path() / (L"displayed-" + std::to_wstring(frame) + L".png"));
            app.dialogs.file_drag = [&](const auto &exported) {
                CHECK(!same_path(exported, path) && exported.extension() == L".png" && !app.player.playing);
                check_rotation_shell_file(exported);
                check_rotation_frame(load_image(exported), *app.image, frame);
                CHECK(std::filesystem::remove(exported)); // Only this invocation's fresh generated PNG.
                ++drags;
            };
            rotation_status_drag(audit);
            CHECK(app.player.frame == frame && !app.player.playing);
        }
        gvn_test::check_rotation_format(*app.image, decoded, item, path);
        CHECK(read_file(path) == bytes && !app.modified);
        ++count;
    }
    CHECK(count == 48 && drags == 96 && audit.notices.empty());
}
void rotation_reference_edited_and_clipboard_alpha_ole_uses_displayed_png() {
    using namespace gvn;
    auto &audit = *this;
    auto &app = audit.app;
    app.settings.confirm_unsaved = false;
    size_t count{}, drags{};
    for (const auto &item : gvn_test::rotation_format_cases()) {
        if (item.kind != "png-alpha")
            continue;
        const auto path = open_rotation_case(audit, item);
        const auto bytes = read_file(path);
        auto expected = *app.image;
        for (auto &pixel : expected.frames[0].pixels) {
            pixel.r = 255 - pixel.r;
            pixel.g = 255 - pixel.g;
            pixel.b = 255 - pixel.b;
        }
        audit.command(Negative);
        CHECK(app.modified && app.image->frames[0].pixels == expected.frames[0].pixels);
        app.dialogs.file_drag = [&](const auto &exported) {
            CHECK(!same_path(exported, path) && exported.extension() == L".png");
            check_rotation_shell_file(exported);
            check_rotation_frame(load_image(exported), expected, 0);
            CHECK(std::filesystem::remove(exported)); // Only this invocation's fresh generated PNG.
            ++drags;
        };
        rotation_status_drag(audit);
        audit.command(Copy);
        audit.command(NewPaste);
        CHECK(app.image->path.empty());
        check_rotation_frame(*app.image, expected, 0);
        const auto pasted = app.image;
        audit.command(ViewRotateNone + ((item.mode + 1) % 8));
        CHECK(app.image == pasted && app.image->frames[0].pixels == expected.frames[0].pixels);
        rotation_status_drag(audit);
        CHECK(read_file(path) == bytes);
        ++count;
    }
    CHECK(count == 24 && drags == 48 && audit.notices.empty());
}

void background_changes_preserve_image_animation_copy_save_and_ole() {
    size_t conditions{}, drags{};
    for (const auto name : {L"alpha.png", L"disposal.gif", L"blend.apng"}) {
        const auto path = copy(name, std::filesystem::path(L"background") / name);
        open(path);
        app.player.playing = false;
        app.player.frame = app.image->frames.size() > 1 ? 1 : 0;
        app.player.elapsed = 3;
        app.view.manual = true; app.view.scale = 1.5; app.view.pan_x = 7; app.view.pan_y = -3;
        app.settings.save_as_retarget = false;
        const auto source = app.image;
        const auto original = *source;
        const auto original_bytes = read_file(path);
        const auto player = app.player;
        const auto view = app.view;
        for (int preset = 0; preset < 5; ++preset) for (bool tiled : {true, false}) {
            auto value = app.settings;
            value.background_tiled = tiled; value.background_pattern = preset;
            value.background_first = 0x172d43; value.background_second = 0x596571;
            value.checker = !tiled; value.checker_light = 0x12ef34;
            value.checker_dark = 0xef1234; value.checker_size = 13;
            const auto sequence = app.loader->sequence();
            app.apply_settings(value);
            CHECK(app.image == source && !app.loading && app.loader->sequence() == sequence && !app.modified);
            CHECK(app.player.frame == player.frame && app.player.elapsed == player.elapsed &&
                  app.player.playing == player.playing && app.player.loop == player.loop &&
                  app.player.completed_passes == player.completed_passes);
            CHECK(app.view.manual == view.manual && app.view.scale == view.scale &&
                  app.view.pan_x == view.pan_x && app.view.pan_y == view.pan_y);
            CHECK(app.renderer.background_tiled == tiled && app.renderer.background_pattern == preset &&
                  app.renderer.background_first == value.background_first &&
                  app.renderer.background_second == value.background_second);
            for (size_t frame = 0; frame < original.frames.size(); ++frame)
                CHECK(app.image->frames[frame].pixels == original.frames[frame].pixels);
            command(Copy);
            check_rotation_frame(paste_image(app.hwnd), original, player.frame);
            const auto saved = temporary / (L"background-save-" + std::to_wstring(conditions) + L".png");
            paths.push_back(saved); command(SaveAs);
            check_rotation_frame(load_image(saved), original, player.frame);
            CHECK(app.image == source && same_path(app.image->path, path));
            app.dialogs.file_drag = [&](const auto &exported) {
                check_rotation_shell_file(exported);
                check_rotation_frame(load_image(exported), original, player.frame);
                if (!same_path(exported, path)) CHECK(std::filesystem::remove(exported));
                ++drags;
            };
            rotation_status_drag(*this);
            CHECK(read_file(path) == original_bytes);
            ++conditions;
        }
    }
    CHECK(conditions == 30 && drags == 30 && notices.empty());
}

    HMENU wallpaper_menu(HMENU root) {
        for (int i = 0; i < GetMenuItemCount(root); ++i) {
            if (GetMenuItemID(root, i) == WallpaperCenter) return root;
            if (const auto child = GetSubMenu(root, i))
                if (const auto found = wallpaper_menu(child)) return found;
        }
        return nullptr;
    }
    void check_wallpaper_frame(const Image &source, size_t frame) {
        const auto actual = load_image(wallpaper_state.image);
        CHECK(actual.width == source.width && actual.height == source.height && actual.frames.size() == 1);
        auto expected = source.frames[frame].pixels;
        for (auto &pixel : expected) {
            const auto channel = [&](uint8_t value) { return static_cast<uint8_t>((unsigned(value) * pixel.a + 128u * (255u-pixel.a)) / 255u); };
            pixel.r = channel(pixel.r); pixel.g = channel(pixel.g); pixel.b = channel(pixel.b); pixel.a = 255;
        }
        CHECK(actual.frames[0].pixels == expected);
    }
    void wallpaper_menu_empty_close_cancel_and_other_instance_changes() {
        app.settings.confirm_unsaved = false;
        const auto root = app.menu(); const auto menu = wallpaper_menu(root);
        CHECK(menu && GetMenuItemCount(menu) == 7);
        for (UINT id = WallpaperCenter; id <= WallpaperDelete; ++id) {
            const auto state = GetMenuState(menu, id, MF_BYCOMMAND);
            CHECK(state != UINT(-1) && (state & MF_GRAYED) && !(state & MF_CHECKED));
            command(id);
        }
        CHECK(wallpaper_factories == 0 && wallpaper_events.empty() && notices.empty());
        open(fixture(L"alpha.png"));
        app.message(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(menu), 0);
        for (UINT id = WallpaperCenter; id <= WallpaperHeight; ++id)
            CHECK(!(GetMenuState(menu, id, MF_BYCOMMAND) & (MF_GRAYED | MF_CHECKED)));
        CHECK(GetMenuState(menu, WallpaperDelete, MF_BYCOMMAND) & MF_GRAYED);
        command(WallpaperWidth);
        const auto file = wallpaper_state.image;
        const auto bytes = read_file(file);
        app.message(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(menu), 0);
        CHECK(!(GetMenuState(menu, WallpaperDelete, MF_BYCOMMAND) & MF_GRAYED));
        const auto calls = wallpaper_factories;
        for (const auto answer : {IDNO, IDCANCEL}) {
            answers.push_back(answer); command(WallpaperDelete);
            CHECK(wallpaper_factories == calls && wallpaper_state.image == file && read_file(file) == bytes);
        }
        command(CloseImage); CHECK(!app.image);
        app.message(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(menu), 0);
        CHECK(GetMenuState(menu, WallpaperCenter, MF_BYCOMMAND) & MF_GRAYED);
        CHECK(!(GetMenuState(menu, WallpaperDelete, MF_BYCOMMAND) & MF_GRAYED));
        // Simulate another instance deleting the shared generated file while this menu exists.
        CHECK(std::filesystem::remove(file));
        app.message(WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(menu), 0);
        CHECK(GetMenuState(menu, WallpaperDelete, MF_BYCOMMAND) & MF_GRAYED);
        DestroyMenu(root);
        CHECK(wallpaper_factories == calls && answers.empty());
    }
    void wallpaper_all_forty_native_cases_match_full_image_and_palette() {
        app.settings.confirm_unsaved = false;
        std::ifstream table(fixture(L"wallpaper-reference/cases.tsv")); CHECK(table.good());
        std::string line; CHECK(std::getline(table, line)); size_t count{}, pixels{};
        while (std::getline(table, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::string> cells; std::istringstream row(line); std::string cell;
            while (std::getline(row, cell, '\t')) cells.push_back(cell); CHECK(cells.size() == 11);
            const auto source = std::filesystem::path(L"wallpaper-reference") / cells[2];
            const auto file = copy(source.c_str(), std::filesystem::path(L"wallpaper-native") / cells[0] / L"source.png");
            open(file); const auto image = app.image; const auto original = *image; const auto source_bytes = read_file(file);
            app.selection = {1,1,2,2}; const auto sequence = app.loader->sequence();
            wallpaper_events.clear(); command(WallpaperCenter + std::stoi(cells[1]));
            CHECK(wallpaper_events == std::vector<std::string>{"read","write","apply"});
            CHECK(wallpaper_state.values == wallpaper_values(static_cast<WallpaperMode>(std::stoi(cells[1]))));
            CHECK(wallpaper_state.image.parent_path() == wallpaper_directory(app.config));
            const auto actual = load_image(wallpaper_state.image);
            const auto expected = load_image(fixture(L"wallpaper-reference") / cells[3]);
            CHECK(actual.width == expected.width && actual.height == expected.height && actual.frames[0].pixels == expected.frames[0].pixels);
            if (expected.frames[0].indexed) {
                CHECK(actual.frames[0].indexed && actual.frames[0].indexed->palette == expected.frames[0].indexed->palette);
                for (uint32_t y=0; y<actual.height; ++y) for (uint32_t x=0; x<actual.width; ++x)
                    CHECK(actual.frames[0].indexed->index(x,y) == expected.frames[0].indexed->index(x,y));
            }
            CHECK(app.image == image && app.image->frames[0].pixels == original.frames[0].pixels && !app.modified);
            CHECK(app.selection.x == 1 && app.selection.y == 1 && app.selection.w == 2 && app.selection.h == 2);
            CHECK(app.loader->sequence() == sequence && read_file(file) == source_bytes);
            CHECK(managed_wallpaper_files(app.config).size() == 1);
            ++count; pixels += size_t(actual.width) * actual.height;
        }
        CHECK(count == 40 && pixels == 19950 && wallpaper_factories == 40 && notices.empty());
    }
    void wallpaper_png_psd_gif_apng_current_frame_preserves_playback_and_alpha() {
        app.settings.confirm_unsaved = false; app.settings.animation_autoplay = false;
        size_t checked{};
        for (const auto name : {L"alpha.png", L"psd-16-3.psd", L"disposal.gif", L"blend.apng"}) {
            const auto path = copy(name, std::filesystem::path(L"wallpaper-formats") / name); open(path);
            const auto image = app.image; const auto original = *image; const auto bytes = read_file(path);
            for (size_t frame = 0; frame < image->frames.size(); ++frame) {
                app.player.frame = frame; app.player.elapsed = 3; app.player.playing = image->animated();
                app.player.loop = false; app.player.completed_passes = 1;
                app.selection = {0,0,1,1}; const auto player = app.player;
                for (UINT id = WallpaperCenter; id <= WallpaperHeight; ++id) {
                    command(id); check_wallpaper_frame(original, frame);
                    CHECK(app.image == image && !app.modified && app.player.frame == frame);
                    CHECK(app.player.playing == player.playing && app.player.elapsed == player.elapsed &&
                          app.player.loop == player.loop && app.player.completed_passes == player.completed_passes);
                    CHECK(app.selection.w == 1 && app.selection.h == 1);
                    CHECK(app.image->file_loops == original.file_loops && app.image->format == original.format);
                    for (size_t f=0; f<image->frames.size(); ++f)
                        CHECK(image->frames[f].pixels == original.frames[f].pixels && image->frames[f].duration_ms == original.frames[f].duration_ms);
                    ++checked;
                }
            }
            CHECK(read_file(path) == bytes);
        }
        CHECK(checked >= 30 && notices.empty());
    }
    void wallpaper_edited_clipboard_undo_copy_and_confirmed_delete_preserve_document() {
        app.settings.confirm_unsaved = false;
        const auto file = copy(L"alpha.png", L"wallpaper-edited/source.png"); open(file);
        const auto original = app.image; const auto bytes = read_file(file); command(Negative);
        app.selection = {0,0,1,1}; const auto edited = app.image, undo = app.undo_image;
        command(WallpaperTile); check_wallpaper_frame(*edited, 0);
        CHECK(app.modified && app.image == edited && app.undo_image == undo && undo == original);
        CHECK(app.selection.w == 1 && read_file(file) == bytes);
        command(Undo); CHECK(app.image == original);
        copy_image(app.hwnd, *original, 0); command(NewPaste);
        const auto anonymous = app.image; CHECK(anonymous->path.empty());
        command(WallpaperStretch); check_wallpaper_frame(*anonymous, 0);
        command(Copy); CHECK(paste_image(app.hwnd).frames[0].pixels == anonymous->frames[0].pixels);
        const auto clipboard = GetClipboardSequenceNumber();
        const auto previous_undo = app.undo_image;
        answers.push_back(IDYES); command(WallpaperDelete);
        CHECK(wallpaper_state.image.empty() && !has_wallpaper_files(app.config));
        CHECK(app.image == anonymous && app.undo_image == previous_undo && app.image->path.empty());
        CHECK(GetClipboardSequenceNumber() == clipboard && read_file(file) == bytes && answers.empty());
    }
    void wallpaper_during_pending_decode_uses_displayed_image_without_cancelling_request() {
        const auto first = copy(L"alpha.png", L"wallpaper-current/source.png");
        const auto second = copy(L"rgb.bmp", L"wallpaper-pending/target.bmp"); open(first);
        const auto image = app.image;
        with_rotation_worker_gate(second, false, false, [&](const auto &entered, const auto &release) {
            app.open(second, false, false); entered(); const auto sequence = app.loader->sequence();
            command(WallpaperHeight); check_wallpaper_frame(*image, 0);
            CHECK(app.loading && app.requested == second && app.image == image && app.loader->sequence() == sequence);
            release(); wait(); CHECK(app.image->path == second);
            command(WallpaperCenter); check_wallpaper_frame(*app.image, 0);
        });
    }
    void wallpaper_failure_restores_previous_output_and_preserves_edited_selection_undo() {
        app.settings.confirm_unsaved = false; open(fixture(L"alpha.png")); command(WallpaperWidth);
        const auto before = wallpaper_state; const auto generated = read_file(before.image);
        command(Negative); app.selection = {0,0,1,1}; const auto image = app.image, undo = app.undo_image;
        wallpaper_fail_apply = true; wallpaper_events.clear();
        THROWS(command(WallpaperTile));
        CHECK(wallpaper_state == before && read_file(before.image) == generated);
        CHECK(wallpaper_events == std::vector<std::string>{"read","write","apply","write","apply"});
        CHECK(managed_wallpaper_files(app.config) == std::vector<std::filesystem::path>{before.image});
        CHECK(app.image == image && app.undo_image == undo && app.modified && app.selection.w == 1);
        wallpaper_events.clear(); wallpaper_fail_apply = true; answers.push_back(IDYES);
        THROWS(command(WallpaperDelete));
        CHECK(wallpaper_state == before && read_file(before.image) == generated);
        CHECK(app.image == image && app.undo_image == undo && app.modified && app.selection.w == 1);
        CHECK(answers.empty());
    }
    void wallpaper_generated_input_retention_is_silent_but_cleanup_failure_warns() {
        app.settings.confirm_unsaved = false; open(fixture(L"alpha.png")); command(WallpaperCenter);
        const auto generated = wallpaper_state.image; const auto original = read_file(generated);
        open(generated); const auto image = app.image;
        command(WallpaperWidth);
        CHECK(notices.empty() && read_file(generated) == original && managed_wallpaper_files(app.config).size() == 2);
        struct OwnedLock {
            HANDLE handle{INVALID_HANDLE_VALUE};
            ~OwnedLock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
        } lock{CreateFileW(wallpaper_state.image.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        CHECK(lock.handle != INVALID_HANDLE_VALUE);
        command(WallpaperHeight);
        CHECK(notices.size() == 1 && notices[0].find(L"削除できませんでした") != std::wstring::npos);
        CHECK(read_file(generated) == original && app.image == image && !app.modified);
        CHECK(managed_wallpaper_files(app.config).size() == 3);
    }
    void wallpaper_custom_keys_mouse_and_legacy_popup_dispatch_all_six_commands() {
        app.settings.confirm_unsaved = false; open(fixture(L"alpha.png")); const auto image = app.image;
        for (UINT id = WallpaperCenter; id <= WallpaperDelete; ++id) {
            auto next = app.settings; next.set_keys(id, {L"F12"});
            next.mouse.assign(MouseGesture::MiddleClick, 0, 0, id); app.apply_settings(next);
            if (id == WallpaperDelete) answers.push_back(IDYES);
            const auto previous = wallpaper_factories;
            app.message(WM_KEYDOWN, VK_F12, 0); CHECK(wallpaper_factories == previous + 1);
            CHECK(id == WallpaperDelete ? wallpaper_state.image.empty() : wallpaper_state.values == wallpaper_values(static_cast<WallpaperMode>(id-WallpaperCenter)));
            if (id == WallpaperDelete) { command(WallpaperCenter); answers.push_back(IDYES); }
            app.message(WM_MBUTTONDOWN, MK_MBUTTON, MAKELPARAM(100, app.top+100));
            app.message(WM_MBUTTONUP, 0, MAKELPARAM(100, app.top+100));
            CHECK(wallpaper_factories == previous + (id == WallpaperDelete ? 3 : 2));
            Settings saved; saved.load(app.config); CHECK(saved.first_key(id) == L"F12");
            if (id == WallpaperDelete) command(WallpaperCenter);
            next = app.settings; next.set_keys(id, {}); app.apply_settings(next);
        }
        auto next = app.settings; next.set_keys(Wallpaper, {L"F12"}); app.apply_settings(next);
        const POINT position{321,654}; app.cursor_screen = [=] { return position; }; size_t popups{};
        app.dialogs.popup = [&](HWND owner, HMENU menu, POINT point) {
            CHECK(owner == app.hwnd && menu && GetMenuItemCount(menu) == 7 && point.x == position.x && point.y == position.y);
            CHECK(GetMenuItemID(menu, 0) == WallpaperCenter && GetMenuItemID(menu, 6) == WallpaperDelete);
            for (UINT id=WallpaperCenter; id<=WallpaperHeight; ++id) CHECK(!(GetMenuState(menu,id,MF_BYCOMMAND)&MF_CHECKED));
            ++popups; return UINT(WallpaperHeight);
        };
        app.message(WM_KEYDOWN, VK_F12, 0); CHECK(popups == 1 && wallpaper_state.values == wallpaper_values(WallpaperMode::Height));
        CHECK(app.image == image && !app.modified && answers.empty() && notices.size() == 2);
    }
    // BEGIN slideshow-window reference contracts (existing cases remain unchanged).
    void slideshow_window_start_stop_matches_native_six_cases() {
        open(fixture(L"alpha.png"));
        app.settings.interval = 600000;
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        std::ifstream reference(fixture(L"window-startup-reference/native-stop.tsv"));
        CHECK(reference);
        std::string line, name;
        std::getline(reference, line);
        int automatic{}, prior{}, started{}, stopped{};
        size_t cases{};
        for (; reference >> name >> automatic >> prior >> started >> stopped; ++cases) {
            if (app.full) command(Fullscreen);
            ShowWindow(app.hwnd, SW_RESTORE);
            const auto work = App::monitor_info(app.hwnd).rcWork;
            CHECK(SetWindowPos(app.hwnd, nullptr, work.left + 110, work.top + 120, 700, 500,
                               SWP_NOZORDER | SWP_NOACTIVATE));
            if (prior == 1) command(WindowMaximize);
            if (prior == 2) command(Fullscreen);
            WINDOWPLACEMENT before{sizeof(before)};
            if (app.full) before = app.placement;
            else CHECK(GetWindowPlacement(app.hwnd, &before));
            const auto image = app.image;
            app.settings.slide_fullscreen = automatic != 0;
            command(Slideshow);
            CHECK(app.slideshow && (app.full ? 2 : IsZoomed(app.hwnd) ? 1 : 0) == started);
            app.remember_window();
            CHECK(app.settings.last_state == prior);
            CHECK(app.settings.last_x == before.rcNormalPosition.left &&
                  app.settings.last_y == before.rcNormalPosition.top);
            command(Slideshow);
            CHECK(!app.slideshow && (app.full ? 2 : IsZoomed(app.hwnd) ? 1 : 0) == stopped);
            WINDOWPLACEMENT after{sizeof(after)};
            if (app.full) after = app.placement;
            else CHECK(GetWindowPlacement(app.hwnd, &after));
            CHECK(EqualRect(&before.rcNormalPosition, &after.rcNormalPosition));
            CHECK(app.image == image && !app.modified);
            app.stop_slideshow();
            CHECK((app.full ? 2 : IsZoomed(app.hwnd) ? 1 : 0) == stopped);
        }
        CHECK(cases == 6);
    }
    void slideshow_close_active_and_reopen_excludes_only_automatic_fullscreen() {
        struct Probe {
            App app;
            ~Probe() { if (IsWindow(app.hwnd)) DestroyWindow(app.hwnd); }
        };
        const auto path = temporary / L"slide-startup.ini";
        const auto launch = [&] {
            auto probe = std::make_unique<Probe>();
            probe->app.set_config(path);
            CHECK(CreateWindowExW(0, L"GVN.AppAudit", L"slideshow restart", WS_OVERLAPPEDWINDOW,
                                  20, 20, 900, 700, nullptr, nullptr, GetModuleHandleW(nullptr), &probe->app));
            probe->app.show_initial(SW_SHOWNORMAL);
            probe->app.show_initial(SW_SHOWNORMAL);
            return probe;
        };
        for (bool automatic : {false, true}) for (int prior : {0, 1, 2}) for (int toggles : {0, 1, 2}) {
            Settings initial;
            initial.startup_position = 2;
            initial.startup_state = prior == 1 ? 2 : prior == 2 ? 3 : 0;
            initial.window_width = 700; initial.window_height = 500;
            initial.restore_maximized = initial.restore_fullscreen = true;
            initial.slide_fullscreen = automatic; initial.interval = 600000;
            initial.save(path);
            auto probe = launch();
            probe->app.command(Slideshow);
            CHECK(probe->app.slideshow && (!automatic || probe->app.full));
            for (int toggle = 0; toggle < toggles; ++toggle) probe->app.command(Fullscreen);
            const auto expected = automatic ? prior : probe->app.full ? 2 : IsZoomed(probe->app.hwnd) ? 1 : 0;
            probe->app.message(WM_CLOSE, 0, 0);
            probe.reset(); pump();
            Settings saved; saved.load(path);
            CHECK(saved.last_window_valid && saved.last_state == expected && saved.slide_fullscreen == automatic);
            saved.startup_position = 1; saved.startup_state = 0; saved.save(path);
            probe = launch();
            CHECK(!probe->app.slideshow && (probe->app.full ? 2 : IsZoomed(probe->app.hwnd) ? 1 : 0) == expected);
            probe.reset(); pump();
        }
    }
    void slideshow_window_option_changes_take_effect_on_next_start_only() {
        open(fixture(L"alpha.png"));
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE); ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        app.settings.interval = 600000;
        const auto source = app.image;
        command(Slideshow); CHECK(app.slideshow && app.full);
        auto next = app.settings; next.slide_fullscreen = false; app.apply_settings(next);
        CHECK(app.slideshow && app.full);
        app.remember_window(); CHECK(app.settings.last_state == 0);
        command(Slideshow); CHECK(!app.slideshow && !app.full);
        command(Slideshow); CHECK(app.slideshow && !app.full);
        command(Fullscreen); CHECK(app.full);
        next = app.settings; next.slide_fullscreen = true; app.apply_settings(next);
        app.remember_window(); CHECK(app.settings.last_state == 2);
        command(Slideshow); CHECK(!app.slideshow && app.full && app.image == source);
        command(Slideshow); CHECK(app.slideshow && app.full);
        command(Fullscreen); CHECK(!app.full);
        command(Slideshow); CHECK(!app.slideshow && app.full);
    }
    void slideshow_load_error_and_image_close_restore_original_window() {
        open(fixture(L"alpha.png"));
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE); ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        app.settings.interval = 600000;
        const auto source = app.image;
        command(Slideshow); CHECK(app.slideshow && app.full);
        const auto broken = temporary / L"broken.png";
        { std::ofstream file(broken, std::ios::binary); file << "not a PNG"; }
        const auto notices_before = notices.size();
        app.open(broken); wait();
        CHECK(!app.slideshow && !app.full && app.image == source && answers.empty() &&
              notices.size() == notices_before + 1);
        command(Slideshow); CHECK(app.slideshow && app.full);
        command(CloseImage);
        CHECK(!app.slideshow && !app.full && !app.image);
    }
    void slideshow_window_preserves_animation_alpha_selection_and_undo() {
        open(fixture(L"blend.apng"));
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE); ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        app.settings.interval = 600000;
        answers.push_back(IDOK);
        command(FlipH);
        command(Undo);
        CHECK(app.image->animated() && app.undo_image && !app.undo_image->animated() && answers.empty());
        app.player.seek(1);
        app.selection = {1, 1, 2, 2};
        const auto image = app.image, undo = app.undo_image;
        const auto pixels = image->frames[app.player.frame].pixels;
        const auto frame = app.player.frame; const auto playing = app.player.playing;
        const bool modified = app.modified;
        command(Slideshow); CHECK(app.slideshow && app.full);
        command(Slideshow); CHECK(!app.slideshow && !app.full);
        CHECK(app.image == image && app.undo_image == undo && app.modified == modified &&
              app.player.frame == frame && app.player.playing == playing &&
              app.selection.x == 1 && app.selection.y == 1 && app.selection.w == 2 && app.selection.h == 2 &&
              app.image->frames[frame].pixels == pixels);
        command(Undo); CHECK(app.image == undo);
    }
    // END slideshow-window reference contracts.
    // BEGIN startup-default reference contracts.
    void startup_defaults_fresh_empty_and_image_match_native_then_restore_position() {
        struct Probe {
            App app;
            ~Probe() { if (IsWindow(app.hwnd)) DestroyWindow(app.hwnd); }
        };
        std::ifstream reference(fixture(L"window-startup-reference/fresh-window.tsv"));
        CHECK(reference);
        std::string line; std::getline(reference, line);
        int with_image{}, x{}, y{}, width{}, height{}; size_t cases{};
        while (reference >> with_image >> x >> y >> width >> height) {
            const auto path = temporary / (L"fresh-" + std::to_wstring(with_image) + L".ini");
            const auto launch = [&] {
                auto probe = std::make_unique<Probe>(); probe->app.set_config(path);
                CHECK(CreateWindowExW(0, L"GVN.AppAudit", L"fresh defaults", WS_OVERLAPPEDWINDOW,
                                      20, 20, 900, 700, nullptr, nullptr, GetModuleHandleW(nullptr), &probe->app));
                probe->app.show_initial(SW_SHOWNORMAL); probe->app.show_initial(SW_SHOWNORMAL);
                if (with_image) {
                    probe->app.open(fixture(L"window-startup-reference/fresh-image.png"));
                    const auto until = GetTickCount64() + 5000;
                    do {
                        pump(); CHECK(GetTickCount64() < until);
                        if (probe->app.loading)
                            MsgWaitForMultipleObjectsEx(0, nullptr, 10, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    }
                    while (probe->app.loading);
                    CHECK(probe->app.image && probe->app.image->width == 64 && probe->app.image->height == 48);
                }
                return probe;
            };
            CHECK(!std::filesystem::exists(path));
            auto probe = launch(); RECT actual{};
            CHECK(GetWindowRect(probe->app.hwnd, &actual));
            CHECK(actual.left == x && actual.top == y && actual.right - actual.left == width &&
                  actual.bottom - actual.top == height && !probe->app.full && !IsZoomed(probe->app.hwnd));
            CHECK(probe->app.settings.startup_position == 1 && probe->app.settings.restore_maximized &&
                  !probe->app.settings.restore_fullscreen && probe->app.settings.interval == 5000);
            CHECK(SetWindowPos(probe->app.hwnd, nullptr, 110, 120, 700, 500, SWP_NOZORDER | SWP_NOACTIVATE));
            probe->app.message(WM_CLOSE, 0, 0); probe.reset(); pump();
            Settings saved; saved.load(path);
            CHECK(saved.last_window_valid && saved.last_state == 0 && saved.last_width == 700 && saved.last_height == 500);
            probe = launch(); CHECK(GetWindowRect(probe->app.hwnd, &actual));
            CHECK(actual.left == 110 && actual.top == 120 && actual.right - actual.left == 700 && actual.bottom - actual.top == 500);
            probe->app.message(WM_CLOSE, 0, 0); probe.reset(); pump(); ++cases;
        }
        CHECK(cases == 2);
    }
    void startup_defaults_restore_maximized_and_keep_explicit_legacy_windows_mode() {
        struct Probe {
            App app;
            ~Probe() { if (IsWindow(app.hwnd)) DestroyWindow(app.hwnd); }
        };
        const auto path = temporary / L"fresh-maximized.ini";
        const auto launch = [&] {
            auto probe = std::make_unique<Probe>(); probe->app.set_config(path);
            CHECK(CreateWindowExW(0, L"GVN.AppAudit", L"max defaults", WS_OVERLAPPEDWINDOW,
                                  20, 20, 900, 700, nullptr, nullptr, GetModuleHandleW(nullptr), &probe->app));
            probe->app.show_initial(SW_SHOWNORMAL); probe->app.show_initial(SW_SHOWNORMAL); return probe;
        };
        auto probe = launch(); CHECK(probe->app.settings.restore_maximized && !IsZoomed(probe->app.hwnd));
        probe->app.command(WindowMaximize); CHECK(IsZoomed(probe->app.hwnd));
        probe->app.message(WM_CLOSE, 0, 0); probe.reset(); pump();
        probe = launch(); CHECK(IsZoomed(probe->app.hwnd) && !probe->app.full);
        probe->app.message(WM_CLOSE, 0, 0); probe.reset(); pump();
        Settings legacy; legacy.load(path);
        legacy.startup_position = 0; legacy.restore_maximized = false;
        legacy.window_x = legacy.window_y = 100; legacy.window_width = 1080; legacy.window_height = 760;
        legacy.interval = 3000; legacy.save(path);
        probe = launch(); RECT rect{}; CHECK(GetWindowRect(probe->app.hwnd, &rect));
        CHECK(!probe->app.full && !IsZoomed(probe->app.hwnd) && rect.right - rect.left == 900 && rect.bottom - rect.top == 700);
        CHECK(probe->app.settings.startup_position == 0 && !probe->app.settings.restore_maximized &&
              probe->app.settings.window_width == 1080 && probe->app.settings.window_height == 760 && probe->app.settings.interval == 3000);
    }
    // END startup-default reference contracts.
    // BEGIN slideshow-policy reference contracts.
    void slideshow_policy_loop_and_last_frame_dwell_are_independent_of_navigation() {
        for (int i = 0; i < 4; ++i) copy(L"alpha.png", L"slide-flat/" + std::to_wstring(i) + L".png");
        app.settings.interval = 600000; app.settings.slide_fullscreen = false;
        app.settings.boundary_sound = true; app.settings.set_end_action(1);
        for (bool loop : {false, true}) {
            open(temporary / L"slide-flat/0.png"); app.settings.slide_loop = loop;
            command(Slideshow);
            for (size_t i = 1; i <= 3; ++i) {
                app.message(WM_TIMER, 2, 0); wait();
                CHECK(app.slideshow && app.index == i);
            }
            const auto last_image = app.image;
            app.message(WM_TIMER, 2, 0); wait();
            CHECK(app.slideshow == loop && app.index == (loop ? 0 : 3));
            CHECK(loop || app.image == last_image);
            const auto menu_state=GetMenuState(app.main_menu,Slideshow,MF_BYCOMMAND);
            CHECK(menu_state!=UINT(-1) && bool(menu_state&MF_CHECKED)==loop);
            CHECK(boundary_directions.empty() && !boundary_beeps && notices.empty());
            if (loop) command(Slideshow);
        }
        open(temporary / L"slide-flat/3.png"); app.settings.slide_loop = false;
        command(Slideshow); CHECK(app.slideshow);
        const auto source = app.image; app.message(WM_TIMER, 2, 0);
        CHECK(!app.slideshow && app.image == source && app.index == 3 && notices.empty());
    }
    void slideshow_policy_random_is_a_complete_permutation_and_restores_sort() {
        for (int i = 0; i < 4; ++i) copy(L"alpha.png", L"slide-random/" + std::to_wstring(i) + L".png");
        open(temporary / L"slide-random/0.png");
        app.settings.interval = 600000; app.settings.slide_fullscreen = false;
        app.settings.slide_loop = true; app.settings.slide_random = true;
        app.random.seed(0x71b9);
        const int original_sort = app.settings.sort; const auto source = app.image;
        command(Slideshow);
        CHECK(app.settings.sort == static_cast<int>(Sort::Random) && app.index == 0 && app.image == source);
        std::set<std::vector<std::filesystem::path>> orders;
        for (int cycle = 0; cycle < 5; ++cycle) {
            std::set<std::filesystem::path> visited; std::vector<std::filesystem::path> order;
            for (int frame = 0; frame < 4; ++frame) {
                CHECK(app.slideshow); visited.insert(app.requested); order.push_back(app.requested);
                if (frame < 3) { app.message(WM_TIMER, 2, 0); wait(); }
            }
            CHECK(visited.size() == 4); orders.insert(order);
            if (cycle < 4) { app.message(WM_TIMER, 2, 0); wait(); }
        }
        CHECK(orders.size() > 1);
        app.apply_settings(app.settings); Settings persisted; persisted.load(app.config);
        CHECK(persisted.sort == original_sort && persisted.slide_random && persisted.slide_loop);
        const auto last_image = app.image; const auto last_path = app.requested;
        command(Slideshow);
        CHECK(!app.slideshow && app.settings.sort == original_sort && app.image == last_image &&
              same_path(app.requested, last_path) && same_path(app.files[app.index].path, last_path));
        for (size_t i = 0; i < app.files.size(); ++i)
            CHECK(app.files[i].path.filename() == std::to_wstring(i) + L".png");
        command(SortRandom); app.settings.slide_random = false;
        command(Slideshow); CHECK(app.settings.sort == static_cast<int>(Sort::Random));
        command(Slideshow); CHECK(app.settings.sort == static_cast<int>(Sort::Random));
    }
    void slideshow_policy_subtree_and_nested_root_match_native_depth_first_order() {
        for (const auto name : {L"root-0.png",L"a/a-0.png",L"a/a-1.png",L"a/c/c-0.png",L"b/b-0.png"})
            copy(L"alpha.png", std::filesystem::path(L"slide-tree") / name);
        app.navigation_root = temporary / L"slide-tree";
        app.settings.interval = 600000; app.settings.slide_fullscreen = false;
        app.settings.slide_next_folder = app.settings.slide_subtree = true;
        app.settings.set_end_action(1); app.settings.boundary_sound = true;
        for (bool nested : {false,true}) for (bool loop : {false,true}) {
            const auto start = app.navigation_root / (nested ? L"a/a-0.png" : L"root-0.png");
            const std::vector<std::wstring> names = nested
                ? std::vector<std::wstring>{L"a-0.png",L"a-1.png",L"c-0.png"}
                : std::vector<std::wstring>{L"root-0.png",L"a-0.png",L"a-1.png",L"c-0.png",L"b-0.png"};
            open(start); app.settings.slide_loop = loop; command(Slideshow);
            for (size_t i = 0; i < names.size() * (loop ? 2 : 1); ++i) {
                CHECK(app.slideshow && app.requested.filename() == names[i % names.size()]);
                app.message(WM_TIMER,2,0); wait();
            }
            CHECK(app.slideshow == loop && notices.empty() && boundary_directions.empty() && !boundary_beeps);
            if (loop) { CHECK(same_path(app.requested,start)); command(Slideshow); }
            else CHECK(app.requested.filename() == names.back());
        }
    }
    void slideshow_policy_stop_cancels_owned_worker_and_restores_displayed_list() {
        const auto source = copy(L"alpha.png",L"slide-cancel/a/source.png");
        const auto target = copy(L"alpha.png",L"slide-cancel/b/target.png");
        open(source); app.settings.interval=600000; app.settings.slide_fullscreen=false;
        app.settings.slide_random=true; app.settings.slide_next_folder=true;
        app.settings.slide_subtree=false; app.settings.slide_loop=false;
        app.navigation_root=temporary/L"slide-cancel";
        with_rotation_worker_gate(target,false,true,[&](const auto &entered,const auto &release) {
            const auto image=app.image; const auto old_sort=app.settings.sort;
            command(Slideshow); app.message(WM_TIMER,2,0); entered();
            CHECK(app.loading && app.pending_folder && app.slideshow);
            const auto sequence=app.loader->sequence(); command(Slideshow);
            CHECK(!app.slideshow && !app.loading && !app.pending_folder && app.image==image &&
                  same_path(app.requested,source) && app.settings.sort==old_sort && app.loader->sequence()!=sequence);
            release(); CHECK(app.loader->wait_idle(std::chrono::seconds(3))); pump();
            CHECK(app.image==image && same_path(app.requested,source) && notices.empty());
        });
    }
    void slideshow_policy_stop_during_decode_keeps_alpha_selection_animation_and_undo() {
        const auto source=copy(L"blend.apng",L"slide-decode/0.apng");
        const auto target=copy(L"alpha.png",L"slide-decode/1.png");
        open(source); app.settings.interval=600000; app.settings.slide_fullscreen=false;
        app.settings.confirm_unsaved=false; app.settings.slide_random=true;
        answers.push_back(IDOK); command(FlipH); command(Undo); CHECK(answers.empty());
        CHECK(notices.size()==1); notices.clear(); // The setup's animation-edit prompt precedes the slideshow.
        app.player.seek(1); app.selection={1,1,2,2};
        const auto image=app.image,undo=app.undo_image; const auto pixels=image->frames[1].pixels;
        const auto playing=app.player.playing; const auto modified=app.modified;
        with_rotation_worker_gate(target,false,false,[&](const auto &entered,const auto &release) {
            command(Slideshow); CHECK(app.image==image && app.player.frame==1);
            app.message(WM_TIMER,2,0); entered(); CHECK(app.loading && app.image==image);
            command(Slideshow);
            CHECK(!app.loading && !app.slideshow && app.image==image && app.undo_image==undo &&
                  same_path(app.requested,source) && app.index==0 && app.settings.sort==0 &&
                  app.player.frame==1 && app.player.playing==playing && app.modified==modified &&
                  app.selection.x==1 && app.selection.y==1 && app.selection.w==2 && app.selection.h==2 &&
                  app.image->frames[1].pixels==pixels);
            release(); CHECK(app.loader->wait_idle(std::chrono::seconds(3))); pump();
            CHECK(app.image==image && app.undo_image==undo && notices.empty());
        });
    }
    void slideshow_policy_decode_failure_and_declined_discard_restore_sort_and_window() {
        const auto source=copy(L"alpha.png",L"slide-error/0.png");
        std::ofstream(temporary/L"slide-error/1.png",std::ios::binary)<<"synthetic invalid PNG";
        open(source); ShowWindow(app.hwnd,SW_SHOWNOACTIVATE);
        app.settings.interval=600000; app.settings.slide_fullscreen=true; app.settings.slide_random=true;
        const auto image=app.image; command(Slideshow); CHECK(app.full && app.settings.sort==4);
        app.message(WM_TIMER,2,0); wait();
        CHECK(!app.slideshow && !app.full && app.image==image && app.settings.sort==0 &&
              same_path(app.requested,source) && notices.size()==1);
        notices.clear(); command(Negative); const auto edited=app.image,undo=app.undo_image;
        CHECK(app.modified); app.selection={0,0,1,1}; answers.push_back(IDCANCEL);
        command(Slideshow); app.message(WM_TIMER,2,0); wait();
        CHECK(!app.slideshow && !app.full && !app.loading && app.settings.sort==0 &&
              app.image==edited && app.undo_image==undo && app.modified && app.selection.w==1 &&
              answers.empty() && notices.size()==1);
    }
    void slideshow_policy_options_apply_next_run_and_unrestricted_folder_end_is_quiet() {
        const auto source=copy(L"alpha.png",L"slide-options/empty/a/0.png");
        const auto next=copy(L"alpha.png",L"slide-options/empty/b/0.png");
        app.navigation_root=temporary/L"slide-options/empty";
        open(source); app.settings.interval=600000; app.settings.slide_fullscreen=false;
        app.settings.slide_loop=false; app.settings.slide_next_folder=false;
        command(Slideshow);
        auto settings=app.settings; settings.slide_loop=true; settings.slide_next_folder=true;
        settings.slide_subtree=false; app.apply_settings(settings);
        app.message(WM_TIMER,2,0); wait();
        CHECK(!app.slideshow && same_path(app.requested,source) && notices.empty());
        command(Slideshow); app.message(WM_TIMER,2,0); wait();
        CHECK(app.slideshow && same_path(app.requested,next));
        app.message(WM_TIMER,2,0); wait();
        CHECK(!app.slideshow && same_path(app.requested,next) && notices.empty());
    }
    void slideshow_policy_start_during_manual_decode_keeps_displayed_source() {
        const auto source=copy(L"alpha.png",L"slide-start-pending/0.png");
        const auto target=copy(L"alpha.png",L"slide-start-pending/1.png");
        open(source); app.settings.interval=600000; app.settings.slide_fullscreen=false;
        app.settings.slide_random=true; const auto image=app.image;
        with_rotation_worker_gate(target,false,false,[&](const auto &entered,const auto &release) {
            app.open(target); entered(); CHECK(app.loading);
            command(Slideshow);
            CHECK(app.slideshow && !app.loading && app.image==image && app.settings.sort==4 &&
                  same_path(app.requested,source) && app.index==0);
            release(); CHECK(app.loader->wait_idle(std::chrono::seconds(3))); pump();
            CHECK(app.image==image && same_path(app.requested,source));
            command(Slideshow); CHECK(app.settings.sort==0 && notices.empty());
        });
    }
    void slideshow_policy_stop_during_manual_decode_restores_displayed_index() {
        const auto source=copy(L"alpha.png",L"slide-stop-pending/0.png");
        const auto target=copy(L"alpha.png",L"slide-stop-pending/1.png");
        open(source); app.settings.interval=600000; app.settings.slide_fullscreen=false;
        app.settings.slide_random=true; const auto image=app.image;
        with_rotation_worker_gate(target,false,false,[&](const auto &entered,const auto &release) {
            command(Slideshow); app.open(target); entered(); CHECK(app.loading);
            command(Slideshow);
            CHECK(!app.slideshow && !app.loading && app.image==image && app.settings.sort==0 &&
                  same_path(app.requested,source) && app.index==0);
            release(); CHECK(app.loader->wait_idle(std::chrono::seconds(3))); pump();
            CHECK(app.image==image && same_path(app.requested,source) && notices.empty());
        });
    }
    void slideshow_policy_natural_stop_updates_native_menu_for_all_prior_sort_modes() {
        const auto source=copy(L"alpha.png",L"slide-menu/0.png");
        copy(L"alpha.png",L"slide-menu/1.png");
        open(source); app.settings.interval=600000; app.settings.slide_loop=false;
        app.settings.slide_random=true;
        const auto find_menu=[&](auto &&self,HMENU root)->HMENU {
            for(int i=0;i<GetMenuItemCount(root);++i) {
                if(GetMenuItemID(root,i)==SortName)return root;
                if(const auto child=GetSubMenu(root,i))
                    if(const auto found=self(self,child))return found;
            }
            return nullptr;
        };
        for(int previous=0;previous<=4;++previous)for(bool fullscreen:{false,true}) {
            command(SortName+previous); open(source); app.settings.slide_fullscreen=fullscreen;
            command(Slideshow); CHECK(app.slideshow && app.settings.sort==4);
            auto menu=find_menu(find_menu,app.main_menu); CHECK(menu);
            CHECK(GetMenuState(menu,SortRandom,MF_BYCOMMAND)&MF_CHECKED);
            app.message(WM_TIMER,2,0); wait(); CHECK(app.slideshow);
            const auto image=app.image; app.message(WM_TIMER,2,0); wait();
            CHECK(!app.slideshow && !app.full && app.settings.sort==previous && app.image==image &&
                  same_path(app.files[app.index].path,app.requested));
            menu=find_menu(find_menu,app.main_menu); CHECK(menu);
            for(UINT id=SortName;id<=SortRandom;++id) {
                const auto state=GetMenuState(menu,id,MF_BYCOMMAND); CHECK(state!=UINT(-1));
                CHECK(bool(state&MF_CHECKED)==(id==SortName+previous));
            }
        }
    }
    // END slideshow-policy reference contracts.
    // BEGIN custom-window reference contracts.
    void custom_window_small_sizes_keep_alpha_and_use_system_tracking_minimum() {
        open(fixture(L"alpha.png"));
        const auto image=app.image; const auto pixels=image->frames[0].pixels;
        app.settings.startup_position=2; app.settings.startup_state=0;
        app.settings.window_x=37; app.settings.window_y=49; app.settings.window_center=false;
        for(const auto size:{SIZE{100,80},SIZE{1,1}}) {
            app.settings.window_width=size.cx; app.settings.window_height=size.cy;
            app.show_initial(SW_SHOWNOACTIVATE); pump();
            RECT bounds{}; CHECK(GetWindowRect(app.hwnd,&bounds));
            const auto dpi=GetDpiForWindow(app.hwnd);
            CHECK(bounds.left==37 && bounds.top==49 &&
                  bounds.right-bounds.left==std::max(size.cx,LONG(GetSystemMetricsForDpi(SM_CXMINTRACK,dpi))) &&
                  bounds.bottom-bounds.top==std::max(size.cy,LONG(GetSystemMetricsForDpi(SM_CYMINTRACK,dpi))));
            CHECK(app.image==image && app.image->frames[0].pixels==pixels && !app.loading &&
                  std::isfinite(app.view.scale) && app.view.width>=1 && app.view.height>=1);
            CHECK(SUCCEEDED(app.renderer.draw(app.hwnd,app.image.get(),app.player.frame,app.view,
                                               app.top,app.bottom,&app.selection)));
        }
        CHECK(SetWindowPos(app.hwnd,nullptr,37,49,700,500,SWP_NOZORDER|SWP_NOACTIVATE));
        CHECK(app.image==image && app.image->frames[0].pixels==pixels);
    }
    void custom_window_spans_present_monitors_and_centers_actual_minimum_size() {
        open(fixture(L"alpha.png"));
        app.settings.startup_position=2; app.settings.startup_state=0;
        app.settings.window_width=640; app.settings.window_height=480; app.settings.window_center=false;
        if(MonitorFromPoint(POINT{-1,-1},MONITOR_DEFAULTTONULL)) {
            app.settings.window_x=-1; app.settings.window_y=-1; app.show_initial(SW_SHOWNOACTIVATE);
            RECT bounds{}; CHECK(GetWindowRect(app.hwnd,&bounds));
            CHECK(bounds.left==-1 && bounds.top==-1 && bounds.right-bounds.left==640 && bounds.bottom-bounds.top==480);
        }
        app.settings.window_x=37; app.settings.window_y=49; app.settings.window_width=100;
        app.settings.window_height=80; app.settings.window_center=true; app.show_initial(SW_SHOWNOACTIVATE);
        const auto work=App::monitor_info(app.hwnd).rcWork; RECT bounds{}; CHECK(GetWindowRect(app.hwnd,&bounds));
        const auto dpi=GetDpiForWindow(app.hwnd); const int width=std::max(100,GetSystemMetricsForDpi(SM_CXMINTRACK,dpi));
        const int height=std::max(80,GetSystemMetricsForDpi(SM_CYMINTRACK,dpi));
        CHECK(bounds.right-bounds.left==width && bounds.bottom-bounds.top==height &&
              bounds.left==work.left+(work.right-work.left-width)/2 &&
              bounds.top==work.top+(work.bottom-work.top-height)/2);
    }
    void custom_window_large_saved_integers_keep_positive_geometry_without_endpoint_overflow() {
        open(fixture(L"alpha.png")); const auto image=app.image;
        app.settings.startup_position=2; app.settings.startup_state=0; app.settings.window_center=false;
        app.settings.window_x=INT_MAX; app.settings.window_y=0;
        app.settings.window_width=INT_MAX; app.settings.window_height=480; app.settings.save(app.config);
        Settings saved; saved.load(app.config); CHECK(saved.window_x==INT_MAX && saved.window_width==INT_MAX);
        app.settings=saved; app.show_initial(SW_SHOWNOACTIVATE); pump();
        RECT bounds{}; CHECK(GetWindowRect(app.hwnd,&bounds));
        CHECK(bounds.right>bounds.left && bounds.bottom>bounds.top &&
              bounds.right-bounds.left<=GetSystemMetrics(SM_CXVIRTUALSCREEN) &&
              bounds.bottom-bounds.top<=GetSystemMetrics(SM_CYVIRTUALSCREEN) &&
              app.image==image && std::isfinite(app.view.scale));
        CHECK(app.settings.window_width==INT_MAX && app.settings.window_x==INT_MAX);
    }
    // END custom-window reference contracts.
    void palette_apply_preview_cancel_copy_and_undo_preserve_original_numbers_and_alpha() {
        open(fixture(L"palette/png-4-alpha.png"));const auto original=app.image;const auto before=original->frames[0].pixels;
        const auto indices=original->frames[0].indexed;const auto prior_undo=app.undo_image;
        app.selection={1,1,3,2};const auto rect=app.selection;
        app.dialogs.palette=[&](HWND,const std::vector<Pixel>&colors,bool enabled,const PaletteDialog::Process&process,
            const PaletteDialog::Preview&preview,const PaletteDialog::Commit&commit,const PaletteDialog::Current&current) {
            CHECK(enabled&&current()&&colors==indices->palette);auto draft=colors;draft[1].r=101;draft[1].g=103;draft[1].b=107;
            auto candidate=process(draft);preview(candidate);
            CHECK(app.image==original&&app.undo_image==prior_undo&&app.presented_image()==candidate.get());
            commit(candidate,false);CHECK(current()&&app.image==candidate&&app.modified&&app.undo_image==original);
            CHECK(!app.mask_preview_image&&!app.settings.palette_preview&&app.selection.x==rect.x&&app.selection.w==rect.w);
            auto later=draft;later[1].r=211;preview(process(later));CHECK(app.image==candidate);preview({});
        };
        command(PaletteEdit);const auto edited=app.image;CHECK(edited!=original&&!app.mask_preview_image);
        CHECK(edited->frames[0].indexed->packed==indices->packed);
        for(uint32_t y=0;y<edited->height;++y)for(uint32_t x=0;x<edited->width;++x) {
            const auto at=size_t(y)*edited->width+x;const auto prior=before[at];const auto value=edited->frames[0].pixels[at];
            CHECK(value==(indices->index(x,y)==1?Pixel{101,103,107,prior.a}:prior));
        }
        Settings saved;saved.load(app.config);CHECK(!saved.palette_preview);
        command(ClearSelection);command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==edited->frames[0].pixels);
        command(Undo);CHECK(app.image==original&&app.image->frames[0].indexed==indices&&!app.modified);
        command(Redo);CHECK(app.image==edited&&app.modified);
        const auto path=temporary/L"palette-edited.png";paths.push_back(path);command(SaveAs);const auto decoded=load_image(path);
        CHECK(decoded.frames[0].indexed);
        CHECK(decoded.frames[0].pixels==edited->frames[0].pixels&&decoded.frames[0].indexed->palette==edited->frames[0].indexed->palette);
        command(Undo);CHECK(app.image->frames[0].pixels==before&&app.image->frames[0].indexed==indices&&app.modified);
    }
    void palette_cancel_without_apply_and_nonindexed_commands_preserve_document() {
        unsigned opened{};app.dialogs.palette=[&](HWND,const std::vector<Pixel>&colors,bool,const PaletteDialog::Process&process,
            const PaletteDialog::Preview&preview,const PaletteDialog::Commit&,const PaletteDialog::Current&current) {
            ++opened;const auto original=app.image;const auto undo=app.undo_image;auto draft=colors;draft[0].r=199;
            preview(process(draft));CHECK(current()&&app.image==original&&app.undo_image==undo&&app.mask_preview_image);preview({});
        };
        command(PaletteEdit);open(fixture(L"display-alpha.png"));const auto rgba=app.image;command(PaletteEdit);CHECK(opened==0&&app.image==rgba);
        open(fixture(L"disposal.gif"));const auto animation=app.image;command(PaletteEdit);CHECK(opened==0&&app.image==animation);
        open(fixture(L"palette/png-4-alpha.png"));const auto original=app.image;const auto undo=app.undo_image;command(PaletteEdit);
        CHECK(opened==1&&app.image==original&&app.undo_image==undo&&!app.modified&&!app.mask_preview_image);
    }
    void palette_allocation_and_settings_save_failure_leave_image_undo_and_preference_unchanged() {
        open(fixture(L"palette/png-4-alpha.png"));const auto original=app.image;const auto undo=app.undo_image;
        app.dialogs.palette=[&](HWND,const std::vector<Pixel>&colors,bool,const PaletteDialog::Process&process,
            const PaletteDialog::Preview&,const PaletteDialog::Commit&commit,const PaletteDialog::Current&current) {
            auto draft=colors;draft[1].r=199;app.editing.allocating=[](size_t){throw std::bad_alloc();};
            THROWS(process(draft));app.editing.allocating={};CHECK(current()&&app.image==original&&app.undo_image==undo);
            auto candidate=process(draft);const auto config=app.config;app.config=temporary;
            THROWS(commit(candidate,false));app.config=config;
            CHECK(app.image==original&&app.undo_image==undo&&app.settings.palette_preview);
        };
        command(PaletteEdit);CHECK(app.image==original&&app.undo_image==undo&&!app.mask_preview_image);
    }
    void palette_late_preview_and_commit_cannot_replace_a_newly_loaded_document() {
        open(fixture(L"palette/png-4-alpha.png"));
        app.dialogs.palette=[&](HWND,const std::vector<Pixel>&colors,bool,const PaletteDialog::Process&process,
            const PaletteDialog::Preview&preview,const PaletteDialog::Commit&commit,const PaletteDialog::Current&current) {
            auto draft=colors;draft[1].r=199;auto candidate=process(draft);open(fixture(L"display-alpha.png"));
            const auto replacement=app.image;const auto undo=app.undo_image;CHECK(!current());preview(candidate);
            CHECK(!app.mask_preview_image);THROWS(commit(candidate,false));THROWS(process(draft));
            CHECK(app.image==replacement&&app.undo_image==undo&&app.settings.palette_preview);
        };
        command(PaletteEdit);CHECK(app.image->width==512&&!app.mask_preview_image);
    }
    void color_depth_command_choices_alpha_copy_save_and_undo_preserve_source() {
        app.settings.confirm_unsaved=false;app.settings.save_as_retarget=false;unsigned number{};
        for(const auto depth:{ColorDepth::Indexed8,ColorDepth::Rgb555,ColorDepth::Rgb565,ColorDepth::Rgb24,ColorDepth::Optimal}) {
            open(fixture(L"palette/png-4-alpha.png"));const auto original=app.image;const auto before=original->frames[0].pixels;const auto undo=app.undo_image;
            app.selection={1,1,3,2};const auto rect=app.selection;app.view.zoom_to(3.,10,10);const auto scale=app.view.scale;
            app.dialogs.color_depth=[&](HWND,unsigned bits,std::optional<Rgb16Layout> layout,bool,const ColorDepthDialog::Current &current)->std::optional<ColorDepthChoice>{CHECK(bits==4&&!layout&&current()&&!app.player.playing);return ColorDepthChoice{depth,false};};
            command(ConvertColorDepth);CHECK(!app.settings.color_depth_dither);
            Settings saved;saved.load(app.config);CHECK(!saved.color_depth_dither);
            if(depth==ColorDepth::Optimal){CHECK(app.image==original&&app.undo_image==undo&&!app.modified);continue;}
            const auto edited=app.image;CHECK(edited!=original&&app.undo_image==original&&app.modified&&app.view.scale==scale&&app.selection.x==rect.x&&app.selection.w==rect.w);
            CHECK(edited->metadata==original->metadata&&edited->path==original->path);
            for(size_t i=0;i<before.size();++i)CHECK(edited->frames[0].pixels[i].a==before[i].a);
            if(depth==ColorDepth::Indexed8)CHECK(edited->frames[0].indexed->bits==8&&edited->frames[0].indexed->palette==original->frames[0].indexed->palette);
            if(depth==ColorDepth::Rgb24)CHECK(!edited->frames[0].indexed&&edited->frames[0].pixels==before);
            command(ClearSelection);command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==edited->frames[0].pixels);
            const auto path=temporary/(L"depth-"+std::to_wstring(number++)+L".png");paths.push_back(path);command(SaveAs);CHECK(load_image(path).frames[0].pixels==edited->frames[0].pixels);
            command(Undo);CHECK(app.image==original&&!app.modified&&app.image->frames[0].pixels==before);
        }
    }
    void color_depth_cancel_late_source_and_failures_preserve_image_undo_and_preference() {
        unsigned opened{};app.dialogs.color_depth=[&](HWND,unsigned,std::optional<Rgb16Layout>,bool,const ColorDepthDialog::Current &)->std::optional<ColorDepthChoice>{++opened;return {};};
        command(ConvertColorDepth);CHECK(opened==0);open(fixture(L"palette/png-4-alpha.png"));const auto original=app.image;const auto undo=app.undo_image;
        command(ConvertColorDepth);CHECK(opened==1&&app.image==original&&app.undo_image==undo&&app.settings.color_depth_dither);
        app.dialogs.color_depth=[](HWND,unsigned,std::optional<Rgb16Layout>,bool,const ColorDepthDialog::Current &)->std::optional<ColorDepthChoice>{return ColorDepthChoice{ColorDepth::Indexed8,false};};
        app.editing.allocating=[](size_t){throw std::bad_alloc();};THROWS(command(ConvertColorDepth));app.editing.allocating={};
        CHECK(app.image==original&&app.undo_image==undo&&app.settings.color_depth_dither&&!app.modified);
        const auto config=app.config;app.config=temporary;THROWS(command(ConvertColorDepth));app.config=config;
        CHECK(app.image==original&&app.undo_image==undo&&app.settings.color_depth_dither&&!app.modified);
        app.dialogs.color_depth=[&](HWND,unsigned,std::optional<Rgb16Layout>,bool,const ColorDepthDialog::Current &current)->std::optional<ColorDepthChoice>{open(fixture(L"display-alpha.png"));CHECK(!current());return ColorDepthChoice{ColorDepth::Indexed8,false};};
        command(ConvertColorDepth);CHECK(app.image!=original&&app.image->width==512&&app.settings.color_depth_dither);
    }
    void color_depth_animation_confirm_current_frame_alpha_and_undo_keep_original_animation() {
        app.settings.save_as_retarget=false;
        open(fixture(L"disposal.gif"));const auto original=app.image;app.player.seek(1);app.player.playing=true;const auto expected=original->frames[1].pixels;const auto undo=app.undo_image;
        unsigned opened{};bool cancel=true;app.dialogs.color_depth=[&](HWND,unsigned,std::optional<Rgb16Layout>,bool,const ColorDepthDialog::Current &current)->std::optional<ColorDepthChoice>{++opened;CHECK(current()&&!app.player.playing);if(cancel)return {};return ColorDepthChoice{ColorDepth::Indexed8,false};};
        answers.push_back(IDCANCEL);command(ConvertColorDepth);CHECK(opened==0&&app.image==original&&app.player.frame==1&&app.player.playing);
        answers.push_back(IDOK);command(ConvertColorDepth);CHECK(opened==1&&app.image==original&&app.undo_image==undo&&app.player.frame==1&&app.player.playing&&app.settings.color_depth_dither);
        cancel=false;answers.push_back(IDOK);command(ConvertColorDepth);CHECK(opened==2&&app.image!=original&&!app.image->animated()&&app.image->path.empty()&&app.modified&&app.undo_image==original);
        CHECK(app.image->frames[0].pixels==expected&&app.player.frame==0&&!app.player.playing&&!app.settings.color_depth_dither);
        command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==expected);const auto path=temporary/L"depth-frame.png";paths.push_back(path);command(SaveAs);CHECK(load_image(path).frames[0].pixels==expected);
        command(Undo);CHECK(app.image==original&&app.player.frame==1&&app.player.playing&&!app.modified);
    }
    void color_depth_effective_metadata_and_failed_preference_save_keep_previous_undo() {
        open(fixture(L"palette/png-4-alpha.png"));const auto original=app.image;const auto pixels=original->frames[0].pixels;
        app.change_resolution(123.45,234.56);const auto overlay=app.image_metadata;const auto previous_undo=app.undo_image;const auto previous_undo_metadata=app.undo_metadata;
        CHECK(overlay&&app.modified);app.dialogs.color_depth=[](HWND,unsigned,std::optional<Rgb16Layout>,bool,const ColorDepthDialog::Current &)->std::optional<ColorDepthChoice>{return ColorDepthChoice{ColorDepth::Indexed8,false};};
        const auto config=app.config;app.config=temporary;THROWS(command(ConvertColorDepth));app.config=config;
        CHECK(app.image==original&&app.undo_image==previous_undo&&app.undo_metadata==previous_undo_metadata&&app.image_metadata==overlay&&app.settings.color_depth_dither&&app.modified);
        command(ConvertColorDepth);CHECK(app.image!=original&&app.current_metadata()==*overlay&&app.undo_metadata==overlay&&!app.settings.color_depth_dither);
        CHECK(app.image->frames[0].pixels==pixels&&app.image->frames[0].indexed->bits==8);
        command(Undo);CHECK(app.image==original&&app.image_metadata==overlay&&app.current_metadata().dpi_x==123.45&&app.current_metadata().dpi_y==234.56&&app.modified);
    }
    void color_adjustment_whole_palette_and_selection_keep_alpha_zoom_copy_save_and_undo() {
        app.settings.confirm_unsaved=false;app.settings.save_as_retarget=false;
        const ColorAdjustment choice{75,-40,15,false,GrayMethod::Luma};
        for(const bool selected:{false,true}) {
            open(fixture(L"palette/png-4-alpha.png"));const auto original=app.image;const auto before=original->frames[0].pixels;
            const auto indices=original->frames[0].indexed;app.selection=selected?Rect{1,1,3,2}:Rect{};const auto rect=app.selection;
            app.view.zoom_to(3.,10,10);const auto scale=app.view.scale;const auto expected=gvn::color_adjustment(*original,choice,rect);CHECK(expected);
            app.dialogs.color_adjustment=[&](HWND owner,std::shared_ptr<const Image> source,size_t frame,Rect area,ColorAdjustment initial,const ColorAdjustmentDialog::Current &current)->std::optional<ColorAdjustment>{
                CHECK(owner==app.hwnd&&source==original&&frame==0&&current()&&!app.player.playing&&initial==ColorAdjustment{});
                CHECK(area.x==rect.x&&area.y==rect.y&&area.w==rect.w&&area.h==rect.h&&app.image==original);return choice;
            };
            command(ColorAdjust);const auto edited=app.image;
            CHECK(edited!=original&&app.undo_image==original&&app.modified&&app.view.scale==scale);
            CHECK(app.selection.x==rect.x&&app.selection.y==rect.y&&app.selection.w==rect.w&&app.selection.h==rect.h);
            CHECK(edited->frames[0].pixels==expected->frames[0].pixels&&edited->metadata==original->metadata&&edited->path==original->path);
            CHECK(original->frames[0].pixels==before&&original->frames[0].indexed==indices);
            for(size_t i=0;i<before.size();++i)CHECK(edited->frames[0].pixels[i].a==before[i].a);
            if(selected)CHECK(!edited->frames[0].indexed);else CHECK(edited->frames[0].indexed&&edited->frames[0].indexed->packed==indices->packed);
            command(ClearSelection);command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==expected->frames[0].pixels);
            const auto path=temporary/(selected?L"adjust-selected.png":L"adjust-whole.png");paths.push_back(path);command(SaveAs);
            CHECK(load_image(path).frames[0].pixels==expected->frames[0].pixels);
            command(Undo);CHECK(app.image==original&&!app.modified&&app.image->frames[0].pixels==before&&app.image->frames[0].indexed==indices);
        }
    }
    void color_adjustment_cancel_neutral_saved_unified_values_and_late_source_are_transactional() {
        unsigned opened{};app.dialogs.color_adjustment=[&](HWND,std::shared_ptr<const Image>,size_t,Rect,ColorAdjustment,const ColorAdjustmentDialog::Current &)->std::optional<ColorAdjustment>{++opened;return {};};
        command(ColorAdjust);CHECK(opened==0);open(fixture(L"palette/png-4-alpha.png"));command(Negative);
        const auto source=app.image;const auto undo=app.undo_image;const auto pixels=source->frames[0].pixels;
        command(ColorAdjust);CHECK(opened==1&&app.image==source&&app.undo_image==undo&&app.modified&&!std::filesystem::exists(app.config));
        app.dialogs.color_adjustment=[&](HWND,std::shared_ptr<const Image> image,size_t,Rect,ColorAdjustment initial,const ColorAdjustmentDialog::Current &current)->std::optional<ColorAdjustment>{CHECK(image==source&&current()&&initial==ColorAdjustment{});return ColorAdjustment{0,0,0,false,GrayMethod::Average};};
        command(ColorAdjust);CHECK(app.image==source&&app.undo_image==undo&&app.modified&&app.image->frames[0].pixels==pixels);
        Settings saved;saved.load(app.config);CHECK(saved.color_adjustment_choice()==ColorAdjustment{0,0,0,false,GrayMethod::Average});
        const ColorAdjustment unified{45,60,20,true,GrayMethod::Average};
        app.dialogs.color_adjustment=[&](HWND,std::shared_ptr<const Image>,size_t,Rect,ColorAdjustment initial,const ColorAdjustmentDialog::Current &)->std::optional<ColorAdjustment>{CHECK(initial==saved.color_adjustment_choice());return unified;};
        command(ColorAdjust);CHECK(app.image!=source&&app.undo_image==source);saved=Settings{};saved.load(app.config);CHECK(saved.color_adjustment_choice()==unified);
        const auto unified_image=app.image;const auto unified_undo=app.undo_image;
        app.dialogs.color_adjustment=[&](HWND,std::shared_ptr<const Image>,size_t,Rect,ColorAdjustment initial,const ColorAdjustmentDialog::Current &)->std::optional<ColorAdjustment>{CHECK(initial==unified);return {};};
        command(ColorAdjust);CHECK(app.image==unified_image&&app.undo_image==unified_undo&&app.settings.color_adjustment_choice()==unified);
        app.dialogs.color_adjustment=[](HWND,std::shared_ptr<const Image>,size_t,Rect,ColorAdjustment,const ColorAdjustmentDialog::Current &)->std::optional<ColorAdjustment>{return ColorAdjustment{90,0,0,false,GrayMethod::Middle};};
        command(ColorAdjust);saved=Settings{};saved.load(app.config);CHECK(saved.color_adjustment_choice()==ColorAdjustment{0,0,0,false,GrayMethod::Middle});
        CHECK(saved.color_adjust_hue==45&&saved.color_adjust_saturation==60&&saved.color_adjust_lightness==20);
        app.settings.confirm_unsaved=false;
        app.dialogs.color_adjustment=[&](HWND,std::shared_ptr<const Image>,size_t,Rect,ColorAdjustment initial,const ColorAdjustmentDialog::Current &current)->std::optional<ColorAdjustment>{
            CHECK(initial==saved.color_adjustment_choice());open(fixture(L"display-alpha.png"));CHECK(!current());return unified;
        };
        command(ColorAdjust);CHECK(app.image->width==512&&!app.modified&&app.settings.color_adjustment_choice()==saved.color_adjustment_choice());
    }
    void color_adjustment_allocation_and_preference_failure_keep_effective_metadata_and_previous_undo() {
        open(fixture(L"palette/png-4-alpha.png"));const auto source=app.image;const auto before=source->frames[0].pixels;
        app.change_resolution(123.45,234.56);const auto overlay=app.image_metadata;const auto prior_undo=app.undo_image;const auto prior_metadata=app.undo_metadata;
        const ColorAdjustment choice{45,60,20,true,GrayMethod::Average};
        app.dialogs.color_adjustment=[&](HWND,std::shared_ptr<const Image>,size_t,Rect,ColorAdjustment,const ColorAdjustmentDialog::Current &)->std::optional<ColorAdjustment>{return choice;};
        app.editing.allocating=[](size_t){throw std::bad_alloc();};THROWS(command(ColorAdjust));app.editing.allocating={};
        CHECK(app.image==source&&app.undo_image==prior_undo&&app.image_metadata==overlay&&app.undo_metadata==prior_metadata&&app.modified&&app.settings.color_adjustment_choice()==ColorAdjustment{});
        const auto config=app.config;app.config=temporary;THROWS(command(ColorAdjust));app.config=config;
        CHECK(app.image==source&&app.undo_image==prior_undo&&app.image_metadata==overlay&&app.undo_metadata==prior_metadata&&app.modified&&app.settings.color_adjustment_choice()==ColorAdjustment{});
        CHECK(source->frames[0].pixels==before);
        command(ColorAdjust);CHECK(app.image!=source&&app.current_metadata()==*overlay&&app.undo_metadata==overlay&&app.settings.color_adjustment_choice()==choice);
        command(Undo);CHECK(app.image==source&&app.image_metadata==overlay&&app.current_metadata().dpi_x==123.45&&app.current_metadata().dpi_y==234.56&&app.modified&&app.image->frames[0].pixels==before);
    }
    void color_adjustment_animation_cancel_current_frame_alpha_save_undo_and_late_frame_guard() {
        app.settings.save_as_retarget=false;open(fixture(L"disposal.gif"));const auto source=app.image;app.player.seek(1);app.player.playing=true;const auto prior_undo=app.undo_image;
        const auto still=still_frame(*source,1);const ColorAdjustment choice{90,0,0,false,GrayMethod::Luma};const auto expected=gvn::color_adjustment(still,choice);CHECK(expected);
        unsigned opened{};bool cancel=true;
        app.dialogs.color_adjustment=[&](HWND,std::shared_ptr<const Image> image,size_t frame,Rect,ColorAdjustment,const ColorAdjustmentDialog::Current &current)->std::optional<ColorAdjustment>{
            ++opened;CHECK(image==source&&frame==1&&current()&&!app.player.playing);if(cancel)return {};return choice;
        };
        answers.push_back(IDCANCEL);command(ColorAdjust);CHECK(opened==0&&app.image==source&&app.player.frame==1&&app.player.playing);
        answers.push_back(IDOK);command(ColorAdjust);CHECK(opened==1&&app.image==source&&app.undo_image==prior_undo&&app.player.frame==1&&app.player.playing);
        cancel=false;answers.push_back(IDOK);command(ColorAdjust);CHECK(opened==2&&app.image!=source&&!app.image->animated()&&app.image->path.empty()&&app.modified&&app.undo_image==source);
        CHECK(app.image->frames[0].pixels==expected->frames[0].pixels&&app.player.frame==0&&!app.player.playing);
        for(size_t i=0;i<still.frames[0].pixels.size();++i)CHECK(app.image->frames[0].pixels[i].a==still.frames[0].pixels[i].a);
        command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==expected->frames[0].pixels);const auto path=temporary/L"adjust-frame.png";paths.push_back(path);command(SaveAs);CHECK(load_image(path).frames[0].pixels==expected->frames[0].pixels);
        command(Undo);CHECK(app.image==source&&app.player.frame==1&&app.player.playing&&!app.modified);
        app.dialogs.color_adjustment=[&](HWND,std::shared_ptr<const Image>,size_t,Rect,ColorAdjustment,const ColorAdjustmentDialog::Current &current)->std::optional<ColorAdjustment>{app.player.seek(0);CHECK(!current());return ColorAdjustment{45,60,20,true,GrayMethod::Average};};
        answers.push_back(IDOK);command(ColorAdjust);CHECK(app.image==source&&app.player.frame==0&&app.settings.color_adjustment_choice()==ColorAdjustment{});
    }
    void animated_gif_current_frame_copy_save_crop_and_undo_keep_source_numbers_and_alpha() {
        app.settings.confirm_unsaved=false;app.settings.save_as_retarget=false;
        app.settings.animation_autoplay=false;app.settings.save_dialog=false;
        const auto source=copy(L"animated-saving/source.gif",L"numbers.gif");const auto bytes=read_file(source);
        open(source);const auto document=app.image;CHECK(document->animated()&&document->indexed_source);
        for(size_t n=0;n<2;++n) {
            CHECK(app.player.frame==n&&!app.player.playing);const auto expected=image_indices(*document,n);CHECK(expected);
            command(Copy);const auto pasted=paste_image(app.hwnd);
            CHECK(pasted.frames[0].pixels==document->frames[n].pixels&&pasted.frames[0].indexed);
            CHECK(pasted.frames[0].indexed->packed==expected->packed&&pasted.frames[0].indexed->palette==expected->palette);
            const auto target=temporary/(L"frame-"+std::to_wstring(n)+L".png");paths.push_back(target);command(SaveAs);
            const auto saved=load_image(target);CHECK(saved.frames[0].indexed&&saved.frames[0].pixels==document->frames[n].pixels);
            CHECK(saved.frames[0].indexed->packed==expected->packed&&saved.frames[0].indexed->palette==expected->palette);
            CHECK(app.image==document&&!app.modified&&app.player.frame==n&&!app.player.playing&&read_file(source)==bytes);
            if(!n)command(FrameNext);
        }
        app.selection={1,0,2,2};answers.push_back(IDOK);command(Crop);
        CHECK(!app.image->animated()&&app.image->frames[0].indexed&&!app.image->indexed_source);
        CHECK(app.image->frames[0].indexed->index(0,0)==81&&app.image->frames[0].indexed->palette.size()==256);
        command(Undo);CHECK(app.image==document&&app.player.frame==1&&!app.player.playing&&read_file(source)==bytes);
        open(fixture(L"disposal.gif"));command(LastFrame);const auto alpha=app.image;const auto expected=image_indices(*alpha,3);CHECK(expected);
        command(Copy);const auto pasted=paste_image(app.hwnd);
        CHECK(pasted.frames[0].pixels==alpha->frames[3].pixels&&pasted.frames[0].indexed);
        CHECK(pasted.frames[0].indexed->packed==expected->packed&&pasted.frames[0].indexed->palette==expected->palette);
        const auto target=temporary/L"alpha.png";paths.push_back(target);command(SaveAs);const auto saved=load_image(target);
        CHECK(saved.frames[0].pixels==alpha->frames[3].pixels&&saved.frames[0].indexed&&saved.frames[0].indexed->packed==expected->packed);
        CHECK(app.image==alpha&&app.player.frame==3&&!app.player.playing&&answers.empty()&&paths.empty());
    }
    void static_gif_loading_and_save_dialog_option_keep_original_indices_and_source_animation() {
        app.settings.confirm_unsaved=false;app.settings.save_as_retarget=false;
        const auto source=copy(L"animated-saving/source.gif",L"source.gif");const auto bytes=read_file(source);
        open(source);CHECK(app.image->frames.size()==2);auto options=app.settings;options.gif_animation=false;options.animation_autoplay=false;
        app.apply_settings(options);wait();CHECK(app.image->frames.size()==1&&!(GetWindowLongPtrW(app.playback.hwnd,GWL_STYLE)&WS_VISIBLE));
        const auto expected=load_image(fixture(L"animated-saving/first-frame.png"));const auto still=app.image;const auto indices=still->frames[0].indexed;
        CHECK(indices&&expected.frames[0].indexed&&indices->bits==8&&indices->palette==expected.frames[0].indexed->palette);
        CHECK(indices->packed==expected.frames[0].indexed->packed&&still->frames[0].pixels==expected.frames[0].pixels);
        CHECK(indices->palette[0]==indices->palette[1]&&indices->index(0,0)==0&&indices->index(1,0)==1);
        const auto target=temporary/L"first-frame.png";unsigned dialogs{};bool cancel=true;const auto saving=app.settings.saving;
        app.settings.save_dialog=true;
        app.dialogs.save_file=[&](HWND owner,const auto &folder,const auto &,SaveOptions &proposed){
            ++dialogs;CHECK(owner==app.hwnd&&same_path(folder,temporary)&&app.image==still&&proposed==saving);
            return cancel?std::filesystem::path{}:target;
        };
        command(Save);CHECK(dialogs==1&&app.image==still&&!std::filesystem::exists(target)&&read_file(source)==bytes&&app.settings.saving==saving);
        cancel=false;command(Save);CHECK(dialogs==2&&app.image==still&&read_file(source)==bytes);
        const auto saved=load_image(target);CHECK(saved.frames[0].indexed&&saved.frames[0].indexed->palette==indices->palette&&saved.frames[0].indexed->packed==indices->packed);
        CHECK(saved.frames[0].pixels==still->frames[0].pixels);command(Copy);CHECK(paste_image(app.hwnd).frames[0].pixels==still->frames[0].pixels);
        app.settings.save_dialog=false;app.settings.confirm_overwrite=false;open(target);command(Negative);const auto edited=app.image->frames[0].pixels;
        command(Save);CHECK(dialogs==2&&!app.modified&&load_image(target).frames[0].pixels==edited);
        options=app.settings;options.gif_animation=true;app.apply_settings(options);wait();open(source);
        CHECK(app.image->frames.size()==2&&read_file(source)==bytes&&app.image->frames[0].pixels==expected.frames[0].pixels);
    }
#include "selection_app_cases.inc"
#include "editor_app_cases.inc"
#include "skitch_app_cases.inc"
#include "modern_app_cases.inc"
#include "responsive_app_cases.inc"
#include "feedback_app_cases.inc"
#include "daily_hotpath_app_cases.inc"
};
} // namespace gvn

#define APP_TEST(name)                                                                                       \
    TEST(name) {                                                                                             \
        gvn::AppAudit audit;                                                                                 \
        audit.name();                                                                                        \
    }
#define LEGACY_WIDE_TEST(name)                                                                               \
    TEST(name) {                                                                                             \
        gvn::AppAudit audit;                                                                                 \
        audit.use_legacy_wide_policy();                                                                      \
        audit.name();                                                                                        \
    }
APP_TEST(native_controls_and_menu)
APP_TEST(resize_dialog_cancel_commit_preferences_alpha_and_animation_undo)
APP_TEST(resize_filters_route_saved_choices_and_commit_only_on_success)
APP_TEST(canvas_options_cancel_failure_alpha_commit_and_animation_undo)
APP_TEST(image_info_reports_source_depth_dpi_profile_and_retains_undo_origin)
APP_TEST(image_info_fields_preferences_and_counter_failure_preserve_document)
APP_TEST(image_info_origin_color_type_survives_metadata_overlay_and_pixel_edits)
APP_TEST(image_info_preserves_source_format_bytes_and_load_time_after_editing)
APP_TEST(image_info_save_retarget_and_undo_report_the_actual_saved_file_format)
APP_TEST(selection_units_custom_presets_persist_cancel_and_preserve_document)
APP_TEST(selection_numeric_dialog_preloads_current_preserves_alpha_crop_undo_and_animation)
APP_TEST(new_window_send_and_paste_preserve_effective_dpi_alpha_undo_and_animation_frame)
APP_TEST(selection_numeric_dialog_ignores_result_after_displayed_document_changes)
APP_TEST(selection_pointer_matches_reference_reverse_edge_and_alpha_copy_crop_undo)
APP_TEST(print_command_uses_immutable_current_frame_and_effective_dpi_without_changing_document)
APP_TEST(resolution_only_uses_document_overlay_preserves_pixels_animation_and_undo)
APP_TEST(resolution_save_clipboard_drag_and_animation_frame_use_document_metadata)
APP_TEST(about_version_preserves_empty_edited_alpha_and_animation_document)
APP_TEST(configured_ime_disable_changes_actual_main_input_context)
APP_TEST(ime_default_new_children_language_focus_and_destroy_restore)
APP_TEST(editing_dimensions_allow_more_than_decode_limit_without_allocating_output)
APP_TEST(edit_memory_small_budget_controls_real_commands_without_touching_source)
APP_TEST(edit_allocation_faults_preserve_undo_selection_and_animation)
APP_TEST(created_maximum_axis_images_save_copy_and_undo_with_small_buffers)
APP_TEST(new_image_undo_keeps_displayed_location_after_metadata_during_blocked_decode)
APP_TEST(forwarded_requests_acknowledge_before_open_and_keep_latest_file)
APP_TEST(forwarded_file_waits_for_modal_owner_and_loads_after_enable)
APP_TEST(reused_activation_off_keeps_minimized_through_metadata_and_image_policy)
APP_TEST(dropped_file_activation_controls_restore_and_preserves_source_alpha)
APP_TEST(startup_reopen_default_off_explicit_priority_fallback_and_quiet_empty)
APP_TEST(closing_remembers_displayed_source_instead_of_pending_file_or_clipboard)
APP_TEST(startup_archive_remembers_member_name_and_reloads_original_not_unsaved_edits)
APP_TEST(folder_adoption_uses_worker_snapshot_without_rescanning_or_resorting)
APP_TEST(folder_adoption_missing_list_or_disappearing_file_keeps_original_document)
APP_TEST(changing_sort_or_visibility_cancels_pending_folder_snapshot)
APP_TEST(folder_commands_depth_first_empty_adjacent_and_independent_head_choices)
APP_TEST(folder_history_suppression_explicit_departure_and_global_disable)
APP_TEST(folder_navigation_preserves_source_on_corrupt_target_and_cancelled_discard)
APP_TEST(movement_boundary_modes_clamp_skip_confirm_loop_and_folder)
LEGACY_WIDE_TEST(movement_toolbar_reverse_keeps_menu_shortcuts_and_button_bitmaps)
APP_TEST(folder_search_escape_stale_result_and_changed_document_are_rejected)
APP_TEST(folder_history_is_saved_on_close_even_when_navigation_visits_are_suppressed)
APP_TEST(save_dialog_overwrite_and_remembered_folder_policy)
APP_TEST(save_format_proposals_commit_only_after_success)
APP_TEST(save_command_routes_each_format_options_to_actual_files)
APP_TEST(rename_resort_preserves_pixels_view_dirty_and_undo)
APP_TEST(copy_move_independent_confirmation_relative_paths_and_failures)
APP_TEST(move_advance_uses_source_order_and_last_file_boundary)
APP_TEST(delete_without_advance_keeps_alpha_and_navigation_gap)
APP_TEST(delete_confirm_modes_failure_advance_and_gap_rollback)
APP_TEST(direct_delete_permanent_routing_confirmation_failure_and_document_guards)
APP_TEST(viewer_defaults_skip_unsaved_prompt_and_zoom_wheel)
APP_TEST(cache_policy_applies_without_replacing_image_and_close_releases_retained_files)
APP_TEST(bottom_export_drag_original_edited_clipboard_and_animation)
APP_TEST(loading_options_reload_without_changing_clipboard_or_edited_images)
APP_TEST(loading_animation_orientation_archive_and_attribute_settings)
APP_TEST(navigation_and_sorting)
APP_TEST(explicit_open_returns_before_directory_listing_and_keeps_source_until_adoption)
APP_TEST(explicit_open_adopts_worker_order_and_metadata_without_a_second_scan)
APP_TEST(sort_visibility_and_loading_settings_cancel_pending_open_snapshot)
APP_TEST(failed_load_keeps_document_and_location)
APP_TEST(empty_folder_keeps_location)
APP_TEST(cancelled_open_keeps_edit)
APP_TEST(undo_after_save_is_dirty)
APP_TEST(clipboard_new_document_undo_identity)
APP_TEST(clipboard_selection_cut_paste_alpha)
APP_TEST(configured_cut_color_preserves_clipboard_alpha_and_outside_pixels)
APP_TEST(cut_transparent_default_and_runtime_switch_do_not_reload_source)
APP_TEST(cut_color_animation_cancel_current_frame_and_palette_undo)
APP_TEST(jump_commits_only_accept_and_keeps_cancelled_modified_image)
APP_TEST(jump_cancels_pending_load_and_honors_all_comic_direction_flags)
APP_TEST(jump_changed_document_does_not_apply_old_choice_or_settings)
APP_TEST(jump_retires_busy_preview_without_blocking_main_loader_or_accumulating_services)
APP_TEST(unusable_clipboard_is_silent_before_cancelling_pending_edits)
APP_TEST(clipboard_full_paths_open_files_and_keep_alpha)
APP_TEST(clipboard_path_cancel_invalid_and_failed_load_preserve_document)
APP_TEST(selection_crop_resize_canvas_and_undo)
APP_TEST(edits_are_routed_and_undoable)
APP_TEST(gif_style_change_preserves_static_and_edited_image_identity)
APP_TEST(view_rotation_commands_match_reference_pixels_and_exclusive_menu_checks)
APP_TEST(view_rotation_discards_file_edits_without_prompt_and_clears_undo)
APP_TEST(view_rotation_option_keeps_clipboard_alpha_selection_and_undo_identity)
APP_TEST(comic_wide_single_command_reloads_current_pair_and_preserves_sources)
APP_TEST(view_rotation_same_option_keeps_edits_and_custom_keys_mouse_popup_dispatch)
APP_TEST(view_rotation_real_formats_preserve_every_frame_metadata_and_source_bytes)
APP_TEST(view_rotation_selection_png_save_and_archive_use_displayed_pixels)
APP_TEST(view_rotation_during_open_discovery_keeps_target_and_uses_last_mode)
APP_TEST(view_rotation_replaces_inflight_decode_and_rejects_older_completion)
APP_TEST(view_rotation_failed_pending_decode_restores_edited_source_selection_and_undo)
APP_TEST(view_rotation_paste_during_pending_decode_cancels_target_and_keeps_undo_location)
APP_TEST(view_rotation_during_folder_search_keeps_request_and_rotates_adopted_file)
APP_TEST(save_as_copy_keeps_edited_original_source_selection_and_undo)
APP_TEST(save_as_copy_exports_current_animation_frame_without_rebinding_or_flattening)
APP_TEST(save_as_copy_keeps_clipboard_alpha_document_and_previous_undo_location)
APP_TEST(save_copy_policy_still_commits_real_overwrites_and_can_enable_legacy_binding)
APP_TEST(gif_style_reload_finite_timer_replay_seek_copy_and_undo)
APP_TEST(explicit_loop_on_repeats_finite_gif_without_changing_source_metadata)
APP_TEST(file_loop_setting_changes_runtime_without_reload_and_restores_after_undo)
APP_TEST(gif_late_timer_displays_next_frame_without_skipping_to_the_end)
APP_TEST(gif_finite_and_forced_infinite_modes_are_distinguished_by_nominal_timer_ticks)
APP_TEST(animation_clock_retains_fractional_milliseconds_across_timer_callbacks)
APP_TEST(animation_commands_copy_and_status)
APP_TEST(animation_seek_and_edit_undo)
APP_TEST(close_stops_timers_and_drag)
APP_TEST(save_as_reopen_and_cancel)
APP_TEST(file_copy_move_rename_no_overwrite)
APP_TEST(file_name_clipboard_variants_keep_unicode_source_after_edit_and_save_copy)
APP_TEST(file_name_clipboard_uses_displayed_source_during_pending_decode)
APP_TEST(file_name_clipboard_virtual_name_folder_and_empty_short_path_keep_alpha_and_undo)
APP_TEST(file_name_clipboard_archive_source_and_missing_short_path)
APP_TEST(new_window_empty_file_and_send_keep_edited_selection_undo_and_alpha)
APP_TEST(new_window_file_and_send_keep_displayed_source_during_pending_decode)
APP_TEST(new_window_paste_freezes_image_or_path_without_writing_clipboard_or_parent)
APP_TEST(new_window_current_frame_archive_and_anonymous_source_rules)
APP_TEST(new_window_received_empty_and_image_skip_previous_file_reopen)
APP_TEST(new_window_launch_failure_keeps_parent_selection_undo_and_clipboard_reads)
APP_TEST(inactive_display_policy_changes_refresh_cached_views_without_resetting_active_manual_zoom)
APP_TEST(first_fullscreen_policy_honors_one_pixel_image_when_cached_dimensions_already_match)
APP_TEST(display_default_shrinks_large_images_and_preserves_small_alpha_in_normal_mode)
APP_TEST(display_explicit_policy_changes_survive_save_without_crossing_screen_modes)
APP_TEST(external_menu_names_hidden_slots_separators_keys_and_configuration)
APP_TEST(external_file_folder_none_and_explicit_arguments_keep_edited_source)
APP_TEST(external_clipboard_has_full_alpha_selection_and_current_animation_frame_before_launch)
APP_TEST(external_empty_and_anonymous_images_keep_launch_paths_and_copy_only_semantics)
APP_TEST(external_archive_file_folder_and_macro_policies_use_named_displayed_member)
APP_TEST(external_pending_regular_decode_keeps_displayed_source_selection_and_undo)
APP_TEST(external_pending_archive_replacement_uses_source_archive_metadata)
APP_TEST(external_legacy_registration_and_unregistered_picker_keep_original_or_rendered_image)
APP_TEST(external_launch_failure_and_malformed_arguments_keep_source_and_no_quit)
APP_TEST(external_successful_launch_quit_honors_optional_unsaved_prompt_and_copy_only_does_not_close)
APP_TEST(comic_and_zip_navigation)
APP_TEST(comic_default_matches_reference_centered_black_padding)
APP_TEST(view_toggles_zoom_and_wheel)
APP_TEST(folders_slideshow_and_settings)
APP_TEST(pending_load_cannot_replace_undo_or_new_image)
APP_TEST(file_metadata_and_registered_destinations)
APP_TEST(independent_twenty_destinations_transfer_original_files)
APP_TEST(unregistered_destination_accept_cancel_and_empty)
APP_TEST(registered_destination_cancels_pending_load_before_registration)
APP_TEST(transfer_dialog_proposal_options_and_rejected_overwrite)
APP_TEST(transfer_dialog_settings_failure_keeps_source_and_target)
APP_TEST(transfer_dialog_changed_document_aborts_before_options_and_file)
APP_TEST(full_screen_preserves_separate_view_settings)
APP_TEST(skip_and_slideshow_advance_without_wrap)
APP_TEST(zip_date_sort_and_close_settings)
APP_TEST(customization_changes_real_paste_undo_and_shortcut_dispatch)
APP_TEST(mouse_click_drag_select_zoom_and_capture_cancel)
APP_TEST(mouse_chords_wheel_double_click_nonclient_and_activation)
APP_TEST(mouse_hover_scroll_and_ole_drag_path)
LEGACY_WIDE_TEST(configurable_scroll_fit_read_random_and_popups)
APP_TEST(display_alignment_zoom_policies_and_fullscreen_bounds)
APP_TEST(window_startup_position_state_and_reopen)
APP_TEST(fit_window_measures_chrome_at_target_width_and_preserves_edited_alpha)
APP_TEST(fit_large_desktop_uses_target_chrome_and_preserves_pixels_in_both_branches)
APP_TEST(window_styles_display_options_and_toolbar_rebuild)
APP_TEST(bars_autohide_fullscreen_and_animation_layout)
APP_TEST(minimum_window_large_bars_animation_and_visibility)
APP_TEST(captionless_control_drag_respects_customization_and_cleans_capture)
APP_TEST(native_canvas_scrollbars_ranges_keyboard_and_fullscreen)
APP_TEST(caption_root_and_unc_match_reference_and_keep_edited_frame_undo)
APP_TEST(caption_overlay_visibility_interactions_and_raw_clipboard)

int main(int argc, char **argv) {
    gvn_test::ClipboardLock clipboard_lock;
    if (!clipboard_lock)
        return 2;
    // Separate OS clipboard and desktop; never touch the user's active clipboard or windows.
    auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !gvn_test::noninteractive(station) || !SetProcessWindowStation(station))
        return 2;
    auto desktop = CreateDesktopW(L"GVN-AppAudit", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop))
        return 3;
    if (FAILED(OleInitialize(nullptr)))
        return 4;
    if (!gvn::initialize_controls())
        return 5;
    WNDCLASSEXW window{sizeof(window)};
    window.lpfnWndProc = gvn::procedure;
    window.hInstance = GetModuleHandleW(nullptr);
    window.lpszClassName = L"GVN.AppAudit";
    if (!RegisterClassExW(&window))
        return 5;
    int failures = 0;
    std::ofstream report;
    if (argc > 1) {
        std::filesystem::create_directories(std::filesystem::path(argv[1]).parent_path());
        report.open(argv[1]);
        report << "{\"method\":\"native command integration; private clipboard; dialog answers "
                  "substituted\",\"cases\":[";
    }
    bool comma = false;
    unsigned isolated_input_resets{}, discarded_thread_messages{}, inherited_modifiers{};
    const std::string filter=argc>2?argv[2]:"";
    size_t executed{};
    for (const auto &test : all_tests()) {
        if(!filter.empty()&&filter!=test.name)continue;
        ++executed;
        gvn::AppAudit::case_commands.clear();
        bool passed = true;
        try {
            // This thread owns a private station/desktop. Discard only its prior case's messages
            // and establish neutral keyboard state; never inject input into the user's desktop.
            MSG stale{};
            while (PeekMessageW(&stale, nullptr, 0, 0, PM_REMOVE)) ++discarded_thread_messages;
            inherited_modifiers |= ((GetKeyState(VK_CONTROL) & 0x8000) ? 1u : 0u) |
                                   ((GetKeyState(VK_SHIFT) & 0x8000) ? 2u : 0u) |
                                   ((GetKeyState(VK_MENU) & 0x8000) ? 4u : 0u);
            BYTE neutral[256]{}; CHECK(SetKeyboardState(neutral)); ++isolated_input_resets;
            test.run();
        } catch (const std::exception &error) {
            passed = false;
            ++failures;
            std::cout << "FAIL " << test.name << ": " << error.what() << '\n';
        }
        if (passed)
            std::cout << "PASS " << test.name << '\n';
        if (report) {
            if (comma)
                report << ',';
            comma = true;
            report << "{\"name\":\"" << test.name << "\",\"passed\":" << (passed ? "true" : "false")
                   << ",\"commands\":[";
            bool command_comma = false;
            for (auto id : gvn::AppAudit::case_commands) {
                if (command_comma)
                    report << ',';
                command_comma = true;
                report << id;
            }
            report << "]}";
        }
    }
    if (report) {
        report << "],\"commands\":[";
        comma = false;
        for (const auto id : gvn::AppAudit::commands) {
            if (comma)
                report << ',';
            comma = true;
            report << id;
        }
        report << "],\"failures\":" << failures << ",\"isolated_input_resets\":" << isolated_input_resets
               << ",\"discarded_private_thread_messages\":" << discarded_thread_messages
               << ",\"inherited_modifier_mask\":" << inherited_modifiers << "}\n";
    }
    std::cout << executed << " application cases, " << failures << " failures\n";
    OleUninitialize();
    return failures ? 1 : executed ? 0 : 2;
}

APP_TEST(rotation_reference_static_copy_save_and_original_file_ole_contract)
APP_TEST(rotation_reference_animation_current_frame_alpha_copy_save_and_png_ole)
APP_TEST(rotation_reference_edited_and_clipboard_alpha_ole_uses_displayed_png)
APP_TEST(background_changes_preserve_image_animation_copy_save_and_ole)
APP_TEST(zoom_reference_transitions_through_numeric_input_and_app_commands_preserve_alpha)
APP_TEST(mask_reference_commands_preserve_alpha_undo_copy_save_and_ole)
APP_TEST(mask_psd_gif_apng_current_frame_and_selected_alpha_restore_with_undo)
APP_TEST(selected_mask_commands_and_undo_keep_official_selection_rectangle)
APP_TEST(fl_native_reference_commands_copy_save_drag_and_undo)
APP_TEST(fl_actual_formats_current_frame_cancel_keyboard_and_mouse)
APP_TEST(meko_native_commands_alpha_selection_undo_copy_save_and_ole)
APP_TEST(meko_actual_formats_current_frame_and_cancel)
APP_TEST(rainbow_native_27_cases_alpha_selection_undo_copy_save_and_ole)
APP_TEST(rainbow_preview_print_cancel_and_shared_candidate_commit_are_transactional)
APP_TEST(rainbow_actual_formats_cancel_accept_and_undo_restore_playback_frame)
APP_TEST(rainbow_memory_failure_and_changed_document_preserve_transaction_boundaries)
APP_TEST(rainbow_and_meko_commands_support_configured_keyboard_and_mouse)
APP_TEST(cp_native_107_seeded_commands_99_alpha_selection_undo_copy_save_and_ole)
APP_TEST(cp_preview_cancel_and_empty_accept_preserve_dirty_undo_selection_and_rendering)
APP_TEST(cp_actual_formats_current_frame_cancel_accept_and_empty_undo_keep_playing)
APP_TEST(cp_allocation_and_document_changes_preserve_transaction_boundaries)
APP_TEST(cp_configured_keyboard_and_mouse_dispatch_same_code_and_alpha)
APP_TEST(wallpaper_menu_empty_close_cancel_and_other_instance_changes)
APP_TEST(wallpaper_all_forty_native_cases_match_full_image_and_palette)
APP_TEST(wallpaper_png_psd_gif_apng_current_frame_preserves_playback_and_alpha)
APP_TEST(wallpaper_edited_clipboard_undo_copy_and_confirmed_delete_preserve_document)
APP_TEST(wallpaper_during_pending_decode_uses_displayed_image_without_cancelling_request)
APP_TEST(wallpaper_failure_restores_previous_output_and_preserves_edited_selection_undo)
APP_TEST(wallpaper_custom_keys_mouse_and_legacy_popup_dispatch_all_six_commands)
APP_TEST(wallpaper_generated_input_retention_is_silent_but_cleanup_failure_warns)
APP_TEST(slideshow_window_start_stop_matches_native_six_cases)
APP_TEST(slideshow_close_active_and_reopen_excludes_only_automatic_fullscreen)
APP_TEST(slideshow_window_option_changes_take_effect_on_next_start_only)
APP_TEST(slideshow_load_error_and_image_close_restore_original_window)
APP_TEST(slideshow_window_preserves_animation_alpha_selection_and_undo)
APP_TEST(slideshow_policy_loop_and_last_frame_dwell_are_independent_of_navigation)
APP_TEST(slideshow_policy_random_is_a_complete_permutation_and_restores_sort)
APP_TEST(slideshow_policy_subtree_and_nested_root_match_native_depth_first_order)
APP_TEST(slideshow_policy_stop_cancels_owned_worker_and_restores_displayed_list)
APP_TEST(slideshow_policy_stop_during_decode_keeps_alpha_selection_animation_and_undo)
APP_TEST(slideshow_policy_decode_failure_and_declined_discard_restore_sort_and_window)
APP_TEST(slideshow_policy_options_apply_next_run_and_unrestricted_folder_end_is_quiet)
APP_TEST(slideshow_policy_start_during_manual_decode_keeps_displayed_source)
APP_TEST(slideshow_policy_stop_during_manual_decode_restores_displayed_index)
APP_TEST(slideshow_policy_natural_stop_updates_native_menu_for_all_prior_sort_modes)
APP_TEST(custom_window_small_sizes_keep_alpha_and_use_system_tracking_minimum)
APP_TEST(custom_window_spans_present_monitors_and_centers_actual_minimum_size)
APP_TEST(custom_window_large_saved_integers_keep_positive_geometry_without_endpoint_overflow)
APP_TEST(startup_defaults_fresh_empty_and_image_match_native_then_restore_position)
APP_TEST(startup_defaults_restore_maximized_and_keep_explicit_legacy_windows_mode)
APP_TEST(palette_apply_preview_cancel_copy_and_undo_preserve_original_numbers_and_alpha)
APP_TEST(palette_cancel_without_apply_and_nonindexed_commands_preserve_document)
APP_TEST(palette_allocation_and_settings_save_failure_leave_image_undo_and_preference_unchanged)
APP_TEST(palette_late_preview_and_commit_cannot_replace_a_newly_loaded_document)
APP_TEST(color_depth_command_choices_alpha_copy_save_and_undo_preserve_source)
APP_TEST(color_depth_cancel_late_source_and_failures_preserve_image_undo_and_preference)
APP_TEST(color_depth_animation_confirm_current_frame_alpha_and_undo_keep_original_animation)
APP_TEST(color_depth_effective_metadata_and_failed_preference_save_keep_previous_undo)
APP_TEST(color_adjustment_whole_palette_and_selection_keep_alpha_zoom_copy_save_and_undo)
APP_TEST(color_adjustment_cancel_neutral_saved_unified_values_and_late_source_are_transactional)
APP_TEST(color_adjustment_allocation_and_preference_failure_keep_effective_metadata_and_previous_undo)
APP_TEST(color_adjustment_animation_cancel_current_frame_alpha_save_undo_and_late_frame_guard)
APP_TEST(static_gif_loading_and_save_dialog_option_keep_original_indices_and_source_animation)
APP_TEST(animated_gif_current_frame_copy_save_crop_and_undo_keep_source_numbers_and_alpha)
APP_TEST(spatial_filter_six_presets_menu_pixels_copy_and_undo_match_native_reference)
APP_TEST(spatial_filter_animation_cancel_late_result_and_failure_preserve_document)
APP_TEST(bitfield_loading_toggle_reloads_cached_file_and_archive_but_preserves_edits_clipboard_and_alpha)
APP_TEST(brightness_main_preview_alpha_palette_roi_copy_save_and_single_undo_are_transactional)
APP_TEST(brightness_cancel_neutral_failure_animation_and_late_source_keep_original_state)

APP_TEST(levels_main_preview_alpha_palette_roi_copy_save_and_single_undo_are_transactional)

APP_TEST(levels_cancel_neutral_failure_animation_and_late_source_keep_original_state)

APP_TEST(empty_canvas_tap_pastes_rgba_or_path_and_restores_status_parts)
APP_TEST(empty_canvas_tap_ignores_drag_cancel_outside_and_pending_image)
APP_TEST(floating_paste_origin_transform_cancel_commit_and_undo)
APP_TEST(selection_tools_native_input_boolean_alpha_delete_move_and_undo)
APP_TEST(marching_ants_animate_one_device_pixel_and_leave_rgba_unchanged)
APP_TEST(plain_inside_drag_starts_masked_move_transform_and_cancel_undo)
APP_TEST(selection_modifiers_and_mask_holes_never_start_automatic_move)
APP_TEST(space_and_hand_pan_small_images_restore_tools_and_custom_keys)
APP_TEST(left_held_wheel_default_navigates_up_next_down_previous)
APP_TEST(selection_outline_and_transform_focused_performance)
APP_TEST(floating_invalid_metadata_and_allocation_failure_preserve_document)
APP_TEST(polygon_effects_and_cut_preserve_unselected_pixels_and_alpha)
APP_TEST(editor_toggle_native_sidebar_and_viewport_preserve_document)
APP_TEST(editor_unifies_legacy_keys_mouse_tools_units_and_cancel)
APP_TEST(editor_short_window_tools_scroll_and_animation_layout)
APP_TEST(live_resize_restores_quality_preserves_document_and_removes_empty_toolbar_row)

APP_TEST(three_quality_choices_drive_actual_zoom_editor_fullscreen_without_changing_rgba)
APP_TEST(skitch_pen_text_native_controls_alpha_apply_cancel_undo)
APP_TEST(annotation_effect_controls_and_multiple_undo_redo_are_transactional)
APP_TEST(multiple_undo_save_retarget_and_document_boundaries)
APP_TEST(skitch_crop_handles_numeric_bounds_buttons_cancel_apply_undo)
APP_TEST(skitch_bottom_export_moves_status_icon_names_formats_and_ole_payload)
APP_TEST(compact_editor_options_and_embedded_status_export)
APP_TEST(timestamp_export_name_on_open_new_and_every_successful_paste_keeps_manual_edits)

APP_TEST(modern_preferences_search_reset_categories_and_theme_apply)
APP_TEST(modern_theme_overflow_density_and_stable_native_resources)
APP_TEST(modern_details_state_and_command_shortcuts_preserve_edits)

APP_TEST(responsive_native_update_and_layout_benchmark)
APP_TEST(responsive_native_updates_preserve_status_theme_and_document)
APP_TEST(responsive_animation_controls_only_write_changed_values)

APP_TEST(daily_hotpaths_benchmark)
APP_TEST(daily_reused_menu_matches_rebuilt_state_and_navigation)

APP_TEST(feedback_copy_paste_alpha_shortcuts_and_timer)
APP_TEST(feedback_silent_paste_failures_keep_document_and_pending_edit)
APP_TEST(feedback_settings_independent_sound_throttle_and_reused_window)
APP_TEST(feedback_async_clipboard_file_complete_failure_and_cancel)
APP_TEST(feedback_edit_save_transfer_success_and_cancel)
APP_TEST(feedback_preferences_native_controls_apply_reset)
