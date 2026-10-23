#include "gvn/jump_dialog.h"
#include "gvn/codecs.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <chrono>
#include <future>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>

using namespace gvn;
namespace id = gvn::jump_control;
namespace {
std::wstring text(HWND h) {
    std::wstring value(static_cast<size_t>(GetWindowTextLengthW(h)) + 1, L'\0');
    GetWindowTextW(h, value.data(), static_cast<int>(value.size()));
    value.pop_back();
    return value;
}
Image synthetic(int value = 0) {
    Image image;
    image.width = 617;
    image.height = 379;
    image.frames.push_back({std::vector<Pixel>(image.width * image.height), 100});
    for (uint32_t y = 0; y < image.height; ++y)
        for (uint32_t x = 0; x < image.width; ++x)
            image.frames[0].pixels[y * image.width + x] = {
                static_cast<uint8_t>((x * x + y * 19 + value) % 256),
                static_cast<uint8_t>((x * 17 + y * y) % 256), static_cast<uint8_t>((x * y + 97) % 256),
                static_cast<uint8_t>((x + 5 * y) % 256)};
    return image;
}
struct Fixture {
    HWND owner{};
    std::vector<FileEntry> files;
    std::shared_ptr<JumpPreview> preview;
    std::unique_ptr<JumpDialog> dialog;
    Fixture() {
        owner = CreateWindowExW(0, L"STATIC", L"Owned jump tests", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0,
                                900, 700, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        CHECK(owner);
        for (const auto name : {L"alpha.png", L"rgb.bmp", L"disposal.gif"})
            files.push_back({std::filesystem::path(GVN_FIXTURES) / name});
    }
    ~Fixture() {
        cleanup();
        DestroyWindow(owner);
    }
    void cleanup() {
        dialog.reset();
        preview.reset();
    }
    void pump() {
        MSG m{};
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            if (dialog && IsWindow(dialog->window()) && IsDialogMessageW(dialog->window(), &m))
                continue;
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    template <typename T> void until(T condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        do {
            pump();
            if (condition())
                return;
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
        } while (std::chrono::steady_clock::now() < deadline);
        CHECK(condition());
    }
    void open(bool reverse = false, JumpOptions options = {false, 1}, size_t index = 1,
              JumpDialog::Decoder decode = {}) {
        if (!preview)
            preview = std::make_shared<JumpPreview>(std::move(decode));
        dialog = std::make_unique<JumpDialog>(files, index, reverse, options, LoadingOptions{},
                                              JumpDialog::Decoder{}, preview);
        CHECK(dialog->create(owner));
        ShowWindow(dialog->window(), SW_SHOW);
        SetActiveWindow(dialog->window());
        pump();
        CHECK(dialog->ready());
    }
    void set(const wchar_t *value) {
        SetWindowTextW(dialog->control(id::Order), value);
        pump();
    }
    void click(int key) {
        SendMessageW(dialog->control(key), BM_CLICK, 0, 0);
        pump();
    }
    void scroll(int code) {
        SendMessageW(dialog->window(), WM_HSCROLL, code,
                     reinterpret_cast<LPARAM>(dialog->control(id::Scroll)));
        pump();
    }
    void quality(int value) {
        SendMessageW(dialog->control(id::Quality), CB_SETCURSEL, value, 0);
        SendMessageW(dialog->window(), WM_COMMAND, MAKEWPARAM(id::Quality, CBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(dialog->control(id::Quality)));
        pump();
    }
    std::vector<uint32_t> pixels() {
        auto window = dialog->control(id::Preview);
        RECT r{};
        GetClientRect(window, &r);
        const auto dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), r.right, -r.bottom, 1, 32, BI_RGB};
        void *data{};
        const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        CHECK(bitmap && data);
        const auto old = SelectObject(dc, bitmap);
        SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        GdiFlush();
        std::vector<uint32_t> copy(static_cast<size_t>(r.right) * r.bottom);
        memcpy(copy.data(), data, copy.size() * 4);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        return copy;
    }
};
} // namespace
TEST(jump_snapshot_labels_order_validation_and_cancel) {
    Fixture f;
    const auto path = f.files[1].path;
    f.files[1].display_name = L"sub/長い名前 & image.bmp";
    f.open();
    CHECK(f.dialog->selected_index() == 1 && text(f.dialog->control(id::Order)) == L"2");
    CHECK(text(f.dialog->control(id::Name)) == f.files[1].display_name);
    CHECK(text(f.dialog->control(id::Total)) == L"総数: 3");
    CHECK(SendMessageW(f.dialog->control(id::Quality), CB_GETCOUNT, 0, 0) == 3);
    CHECK(!IsWindowEnabled(f.dialog->control(id::Quality)) && !f.dialog->loading());
    for (const auto value : {L"", L"0", L"4", L"-1", L"2.5", L"18446744073709551616", L"1x"}) {
        f.set(value);
        CHECK(!f.dialog->valid() && !IsWindowEnabled(f.dialog->control(IDOK)));
        SendMessageW(f.dialog->window(), WM_COMMAND, IDOK, 0);
        CHECK(IsWindow(f.dialog->window()) && !f.dialog->result());
    }
    f.set(L"0002");
    CHECK(f.dialog->valid() && IsWindowEnabled(f.dialog->control(IDOK)));
    f.files[1].path = L"changed-after-dialog-snapshot.png";
    f.click(IDOK);
    CHECK(f.dialog->result() && f.dialog->result()->path == path && f.dialog->result()->index == 1);
    f.open();
    f.click(id::Enabled);
    f.quality(2);
    f.click(IDCANCEL);
    CHECK(!f.dialog->result());
    THROWS(JumpDialog(std::span<const FileEntry>{}, 0, false));
}
TEST(jump_scroll_normal_and_reverse_with_32bit_order_range) {
    Fixture f;
    for (bool reverse : {false, true}) {
        f.open(reverse);
        f.scroll(SB_LEFT);
        CHECK(f.dialog->selected_index() == (reverse ? 2u : 0u));
        f.scroll(SB_LINERIGHT);
        CHECK(f.dialog->selected_index() == 1);
        f.scroll(SB_RIGHT);
        CHECK(f.dialog->selected_index() == (reverse ? 0u : 2u));
        f.scroll(SB_PAGELEFT);
        CHECK(f.dialog->selected_index() == 1);
        f.click(IDCANCEL);
    }
    f.files.assign(70001, f.files.front());
    f.open(false, {false, 1}, 65536);
    SCROLLINFO info{sizeof(info), SIF_ALL};
    CHECK(GetScrollInfo(f.dialog->control(id::Scroll), SB_CTL, &info));
    CHECK(info.nMax == 70001 && info.nPos == 65537);
    f.scroll(SB_RIGHT);
    CHECK(f.dialog->selected_index() == 70000 && text(f.dialog->control(id::Order)) == L"70001");
    f.set(L"65538");
    CHECK(GetScrollInfo(f.dialog->control(id::Scroll), SB_CTL, &info) && info.nPos == 65538);
}
TEST(jump_preview_is_lazy_and_preserves_actual_rgba_and_first_animation_frame) {
    Fixture f;
    std::atomic<int> decoded{};
    f.open(false, {false, 1}, 0, [&](const auto &path) {
        ++decoded;
        return load_image(path);
    });
    CHECK(decoded == 0 && !f.dialog->preview_image());
    f.click(id::Enabled);
    f.until([&] { return !f.dialog->loading(); });
    const auto expected = load_image(f.files[0].path);
    CHECK(decoded == 1 && f.dialog->preview_image());
    CHECK(f.dialog->preview_image()->frames[0].pixels == expected.frames[0].pixels);
    f.set(L"3");
    f.until([&] { return !f.dialog->loading(); });
    const auto animation = load_image(f.files[2].path);
    CHECK(animation.animated() && f.dialog->preview_image()->frames.size() == 1);
    CHECK(f.dialog->preview_image()->frames[0].pixels == animation.frames[0].pixels);
    f.click(id::Enabled);
    CHECK(!f.dialog->preview_image() && !f.dialog->loading());
}
TEST(jump_three_quality_options_change_actual_preview_pixels_without_decoding_again) {
    Fixture f;
    std::atomic<int> decoded{};
    f.open(false, {true, 1}, 0, [&](const auto &) {
        ++decoded;
        return synthetic();
    });
    f.until([&] { return !f.dialog->loading(); });
    CHECK(f.dialog->preview_image());
    f.quality(0);
    const auto low = f.pixels();
    f.quality(1);
    const auto middle = f.pixels();
    f.quality(2);
    const auto high = f.pixels();
    CHECK(low != middle && middle != high && low != high && decoded == 1);
    f.click(IDOK);
    CHECK(f.dialog->result() && f.dialog->result()->options == (JumpOptions{true, 2}));
}
TEST(jump_preview_replaces_started_decode_and_does_not_publish_old_image) {
    Fixture f;
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    std::atomic<int> decoded{};
    // Release and join before decoder captures go out of scope, also on assertion failures.
    struct Release {
        std::promise<void> &p;
        Fixture &fixture;
        ~Release() {
            try {
                p.set_value();
            } catch (...) {
            }
            fixture.cleanup();
        }
    } cleanup{release, f};
    f.open(false, {true, 1}, 0, [&](const auto &path) {
        ++decoded;
        if (path == f.files[0].path) {
            entered.set_value();
            gate.wait();
        }
        auto image = synthetic(path == f.files[0].path ? 1 : 2);
        image.path = path;
        return image;
    });
    auto started = entered.get_future();
    f.until([&] { return started.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
    f.set(L"2");
    CHECK(f.dialog->loading() && !f.dialog->preview_image());
    // Submit the replacement before allowing the first decoder to finish.
    SendMessageW(f.dialog->window(), WM_TIMER, 2, 0);
    release.set_value();
    f.until([&] { return !f.dialog->loading(); });
    CHECK(f.dialog->selected_index() == 1 && decoded == 2 && f.dialog->preview_image());
    CHECK(f.dialog->preview_image()->path == f.files[1].path);
    CHECK(f.dialog->preview_image()->frames[0].pixels == synthetic(2).frames[0].pixels);
}
TEST(jump_disabling_preview_during_decode_rejects_completion_and_can_enable_again) {
    Fixture f;
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::atomic<int> decoded{};
    struct Cleanup {
        Fixture &fixture;
        std::promise<void> &release;
        ~Cleanup() {
            try {
                release.set_value();
            } catch (...) {
            }
            fixture.cleanup();
        }
    } cleanup{f, release};
    f.open(false, {true, 1}, 0, [&](const auto &) {
        if (++decoded == 1) {
            entered.set_value();
            gate.wait();
        }
        return synthetic();
    });
    auto started = entered.get_future();
    f.until([&] { return started.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
    f.click(id::Enabled);
    CHECK(!f.dialog->loading() && !f.dialog->preview_image() && !f.dialog->options().preview);
    release.set_value();
    f.click(id::Enabled);
    f.until([&] { return !f.dialog->loading(); });
    CHECK(decoded == 2 && f.dialog->preview_image() && f.dialog->options().preview);
}
TEST(jump_close_returns_while_decoder_is_blocked) {
    Fixture f;
    std::promise<void> entered, release, closed;
    auto gate = release.get_future().share();
    std::once_flag release_flag;
    const auto release_decoder = [&] { std::call_once(release_flag, [&] { release.set_value(); }); };
    std::atomic<bool> forced_release{};
    struct Cleanup {
        std::function<void()> run;
        ~Cleanup() {
            run();
        }
    } cleanup{[&] {
        release_decoder();
        f.cleanup();
    }};
    auto returned = closed.get_future();
    auto watchdog = std::async(std::launch::async, [&] {
        if (returned.wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
            forced_release = true;
            release_decoder();
        }
    });
    f.open(false, {true, 1}, 0, [&](const auto &) {
        entered.set_value();
        gate.wait();
        return synthetic();
    });
    auto started = entered.get_future();
    f.until([&] { return started.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
    const auto begin = std::chrono::steady_clock::now();
    f.click(IDCANCEL);
    f.dialog.reset();
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    closed.set_value();
    watchdog.get();
    release_decoder();
    f.cleanup();
    std::cout << "CHECKPOINT jump_close returned_before_decoder_release=" << !forced_release.load()
              << " elapsed_ms=" << elapsed << '\n';
    CHECK(!forced_release);
}
TEST(jump_decode_failure_is_nonmodal_and_next_selection_recovers) {
    Fixture f;
    f.open(false, {true, 1}, 0, [&](const auto &path) {
        if (path == f.files[0].path)
            throw std::runtime_error("owned corrupt source");
        return synthetic();
    });
    f.until([&] { return !f.dialog->loading(); });
    CHECK(!f.dialog->preview_image() && f.dialog->valid() && IsWindowEnabled(f.dialog->control(IDOK)));
    f.set(L"2");
    f.until([&] { return !f.dialog->loading(); });
    CHECK(f.dialog->preview_image());
    SendMessageW(f.dialog->window(), WM_CLOSE, 0, 0);
    CHECK(!f.dialog->result() && !IsWindow(f.dialog->window()));
}
TEST(jump_preview_service_reuses_one_worker_and_only_latest_live_mailbox) {
    Fixture f;
    std::promise<void> entered, release, queued;
    const auto gate = release.get_future().share();
    std::once_flag release_flag;
    const auto release_decoder = [&] { std::call_once(release_flag, [&] { release.set_value(); }); };
    std::atomic<bool> forced_release{};
    std::atomic<int> jobs{};
    std::vector<std::thread::id> threads;
    struct Cleanup {
        std::function<void()> run;
        ~Cleanup() {
            run();
        }
    } cleanup{[&] {
        release_decoder();
        f.cleanup();
    }};
    f.preview = std::make_shared<JumpPreview>([&](const auto &path) {
        const int job = ++jobs;
        threads.push_back(std::this_thread::get_id());
        if (job == 1) {
            entered.set_value();
            gate.wait();
        }
        auto image = synthetic(job);
        image.path = path;
        return image;
    });
    CHECK(!f.preview->started() && f.preview->idle());
    auto old = f.preview->mailbox(), next = f.preview->mailbox();
    const auto expired = std::weak_ptr<JumpPreview::Inbox>(old);
    f.preview->request(f.files[0].path, {}, old);
    auto started = entered.get_future();
    CHECK(started.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    CHECK(f.preview->started() && !f.preview->idle());
    auto completed = queued.get_future();
    auto watchdog = std::async(std::launch::async, [&] {
        if (completed.wait_for(std::chrono::seconds(1)) != std::future_status::ready) {
            forced_release = true;
            release_decoder();
        }
    });
    f.preview->cancel(old);
    f.preview->request(f.files[1].path, {}, next);
    const auto latest = f.preview->request(f.files[2].path, {}, next);
    // An old dialog's cancellation must not cancel its replacement's work.
    f.preview->cancel(old);
    old.reset();
    CHECK(expired.expired());
    queued.set_value();
    watchdog.get();
    release_decoder();
    f.until([&] { return f.preview->idle(); });
    const auto result = f.preview->take(next);
    CHECK(!forced_release && result && result->sequence == latest && result->image);
    CHECK(result->path == f.files[2].path && result->image->path == f.files[2].path);
    CHECK(jobs == 2 && threads.size() == 2 && threads[0] == threads[1]);
    CHECK(!f.preview->take(next));
    f.preview->cancel();
    f.until([&] { return f.preview->idle(); });
    f.cleanup();
}
TEST(jump_dpi_relayouts_real_controls_and_updates_preview_target) {
    Fixture f;
    f.open();
    for (const auto dpi : {144u, 192u, 96u}) {
        RECT bounds{20, 20, 20 + MulDiv(710, dpi, 96), 20 + MulDiv(340, dpi, 96)};
        SendMessageW(f.dialog->window(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi),
                     reinterpret_cast<LPARAM>(&bounds));
        f.pump();
        RECT client{};
        GetClientRect(f.dialog->window(), &client);
        for (const int key : {id::Preview, id::Name, id::Order, id::Scroll, id::Enabled, IDOK, IDCANCEL}) {
            RECT area{};
            GetWindowRect(f.dialog->control(key), &area);
            MapWindowPoints(nullptr, f.dialog->window(), reinterpret_cast<POINT *>(&area), 2);
            CHECK(area.left >= 0 && area.top >= 0 && area.right <= client.right &&
                  area.bottom <= client.bottom);
        }
        LOGFONTW font{};
        CHECK(
            GetObjectW(reinterpret_cast<HFONT>(SendMessageW(f.dialog->control(id::Order), WM_GETFONT, 0, 0)),
                       sizeof(font), &font));
        CHECK(font.lfHeight == -MulDiv(9, dpi, 72));
    }
}
int main() {
    gvn_test::ClipboardLock clipboard_lock;
    if (!clipboard_lock)
        return 2;
    const auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !gvn_test::noninteractive(station) || !SetProcessWindowStation(station))
        return 2;
    const auto desktop = CreateDesktopW(L"GVN-jump-dialog-test", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop))
        return 3;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(OleInitialize(nullptr)) || !initialize_controls())
        return 4;
    const auto result = run_tests();
    OleUninitialize();
    return result;
}
