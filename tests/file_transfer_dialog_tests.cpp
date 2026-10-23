#include "gvn/file_transfer_dialog.h"
#include "gvn/transfer.h"
#include "gvn/ui.h"
#include "test.h"
#include "test_desktop.h"
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstring>
#include <deque>
#include <fstream>

using namespace gvn;
namespace id = gvn::transfer_control;
namespace {
std::wstring window_text(HWND window) {
    std::wstring value(static_cast<size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
    value.pop_back();
    return value;
}
bool full_control(HWND window) {
    RECT bounds{};
    GetWindowRect(window, &bounds);
    for (auto current = window;; current = GetParent(current)) {
        const auto style = GetWindowLongPtrW(current, GWL_STYLE);
        if (!(style & WS_VISIBLE))
            return false;
        if (!(style & WS_CHILD))
            return true;
        RECT area{};
        const auto parent = GetParent(current);
        GetClientRect(parent, &area);
        MapWindowPoints(parent, nullptr, reinterpret_cast<POINT *>(&area), 2);
        if (bounds.left < area.left || bounds.top < area.top || bounds.right > area.right ||
            bounds.bottom > area.bottom)
            return false;
    }
}
struct Fixture {
    std::filesystem::path temporary, source;
    HWND owner{};
    std::vector<std::wstring> errors;
    std::unique_ptr<FileTransferDialog> dialog;
    Fixture() {
        GUID guid{};
        CHECK(SUCCEEDED(CoCreateGuid(&guid)));
        wchar_t unique[40]{};
        StringFromGUID2(guid, unique, 40);
        temporary = std::filesystem::temp_directory_path() / (std::wstring(L"GVN-transfer-ui-") + unique);
        std::filesystem::create_directories(temporary / L"source");
        for (int i = 0; i < 10; ++i)
            std::filesystem::create_directory(folder(i));
        source = temporary / L"source" / L"a&b.png";
        std::ofstream(source, std::ios::binary) << "source-owned-file";
        owner = CreateWindowExW(0, L"STATIC", L"Transfer UI test owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0,
                                0, 700, 700, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        CHECK(owner);
    }
    ~Fixture() {
        dialog.reset();
        if (IsWindow(owner))
            DestroyWindow(owner);
        std::error_code error;
        const auto base = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(), error);
        const auto resolved = std::filesystem::weakly_canonical(temporary, error);
        if (!error && resolved.parent_path() == base &&
            resolved.filename().wstring().starts_with(L"GVN-transfer-ui-"))
            std::filesystem::remove_all(resolved, error);
    }
    std::filesystem::path folder(int index) const {
        return temporary / (L"folder-" + std::to_wstring(index));
    }
    FileTransferOptions registered() const {
        FileTransferOptions options;
        options.registered = true;
        for (int i = 0; i < 10; ++i)
            options.destinations[i] = folder(i);
        return options;
    }
    void pump() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (dialog && IsWindow(dialog->window()) && IsDialogMessageW(dialog->window(), &message))
                continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    void open(const FileTransferOptions &options = {}, bool move = false, int initial = -1,
              FileTransferDialog::Browse browse = {}) {
        dialog = std::make_unique<FileTransferDialog>(
            move, source, options, initial, std::move(browse),
            [&](HWND, const std::wstring &message) { errors.push_back(message); });
        CHECK(dialog->create(owner));
        ShowWindow(dialog->window(), SW_SHOW);
        SetActiveWindow(dialog->window());
        pump();
    }
    void set(int key, const std::wstring &value) {
        const auto h = dialog->control(key);
        CHECK(h);
        SetWindowTextW(h, value.c_str());
    }
    void click(int key) {
        const auto h = dialog->control(key);
        CHECK(h && full_control(h));
        SendMessageW(h, BM_CLICK, 0, 0);
        pump();
    }
    void wheel(int delta) {
        const auto h = dialog->control(id::EditBase + dialog->active_slot());
        SendMessageW(h, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(delta)), 0);
        pump();
    }
};
HGLOBAL clone_drop(IDataObject *object) {
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    CHECK(SUCCEEDED(object->GetData(&format, &medium)));
    const auto size = GlobalSize(medium.hGlobal);
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, size);
    CHECK(memory);
    const auto from = GlobalLock(medium.hGlobal);
    const auto to = GlobalLock(memory);
    CHECK(from && to);
    memcpy(to, from, size);
    GlobalUnlock(memory);
    GlobalUnlock(medium.hGlobal);
    ReleaseStgMedium(&medium);
    return memory;
}
void raw_drop(HWND edit, const std::vector<unsigned char> &bytes) {
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    CHECK(memory);
    const auto value = GlobalLock(memory);
    CHECK(value);
    memset(value, 0x7f, GlobalSize(memory));
    memcpy(value, bytes.data(), bytes.size());
    GlobalUnlock(memory);
    SendMessageW(edit, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0);
}
} // namespace

TEST(transfer_basic_rejects_empty_and_files_accepts_relative_folder) {
    Fixture f;
    FileTransferOptions initial;
    f.open(initial);
    CHECK(!f.dialog->registered_mode() && full_control(f.dialog->control(id::BasicEdit)));
    CHECK(f.dialog->ready() && !f.dialog->control(id::EditBase));
    CHECK(GetWindowLongPtrW(f.dialog->control(id::Source), GWL_STYLE) & SS_NOPREFIX);
    f.click(IDOK);
    CHECK(f.errors.size() == 1 && IsWindow(f.dialog->window()) && !f.dialog->result());
    f.set(id::BasicEdit, f.source);
    f.click(IDOK);
    CHECK(f.errors.size() == 2 && !f.dialog->result());
    f.set(id::BasicEdit, L"../folder-3");
    f.click(id::Confirm);
    f.click(IDOK);
    const auto result = f.dialog->result();
    CHECK(result && result->directory == L"../folder-3" && result->slot == -1);
    CHECK(!result->options.confirm && !result->options.registered &&
          result->options.basic_path == L"../folder-3");
    CHECK(initial == FileTransferOptions{} && std::filesystem::exists(f.source));
}
TEST(transfer_modes_preserve_all_edit_drafts_and_cancel_is_not_commit) {
    Fixture f;
    FileTransferOptions initial;
    f.open(initial);
    const auto quoted = L"\"" + f.folder(2).wstring() + L"\"";
    f.set(id::BasicEdit, quoted);
    f.click(id::Registered);
    CHECK(f.dialog->registered_mode() && !full_control(f.dialog->control(id::BasicEdit)));
    for (int i = 0; i < 10; ++i)
        f.set(id::EditBase + i, L"\"" + f.folder(i).wstring() + L"\"");
    f.click(id::Registered);
    CHECK(window_text(f.dialog->control(id::BasicEdit)) == quoted);
    f.click(IDOK);
    const auto accepted = f.dialog->result();
    CHECK(accepted && accepted->options.destinations == f.registered().destinations &&
          accepted->options.basic_path == f.folder(2).wstring() && !accepted->options.registered);
    f.open(initial);
    f.set(id::BasicEdit, f.folder(7));
    f.click(id::Registered);
    f.set(id::EditBase + 5, f.folder(5));
    f.click(id::Confirm);
    f.click(IDCANCEL);
    CHECK(!f.dialog->result() && initial == FileTransferOptions{});
    f.open(initial);
    f.set(id::BasicEdit, f.folder(8));
    SendMessageW(f.dialog->window(), WM_CLOSE, 0, 0);
    f.pump();
    CHECK(!f.dialog->result() && !IsWindow(f.dialog->window()));
}
TEST(transfer_every_number_button_and_forced_initial_slot) {
    Fixture f;
    const auto initial = f.registered();
    for (int i = 0; i < 10; ++i) {
        f.open(initial, i % 2 != 0, i);
        CHECK(f.dialog->active_slot() == i && GetFocus() == f.dialog->control(id::EditBase + i));
        CHECK(full_control(f.dialog->control(id::EditBase + i)));
        const int clicked = (i + 4) % 10;
        f.click(id::ExecuteBase + clicked);
        const auto accepted = f.dialog->result();
        CHECK(accepted && accepted->slot == clicked && accepted->directory == f.folder(clicked));
        CHECK(accepted->options == initial);
    }
    THROWS(FileTransferDialog(false, f.source, initial, -2));
    THROWS(FileTransferDialog(false, f.source, initial, 10));
}
TEST(transfer_button_focus_and_leave_do_not_execute_or_validate) {
    Fixture f;
    for (bool valid : {false, true}) {
        auto initial = f.registered();
        if (!valid)
            initial.destinations.fill({});
        f.open(initial, false, 9);
        for (int i = 0; i < 10; ++i) {
            SetFocus(f.dialog->control(id::ExecuteBase + i));
            f.pump();
            CHECK(f.dialog->active_slot() == i && !f.dialog->result());
            CHECK(full_control(f.dialog->control(id::ExecuteBase + i)));
            SetFocus(f.dialog->control(id::EditBase + i));
            f.pump();
            CHECK(IsWindow(f.dialog->window()) && !f.dialog->result() && f.errors.empty());
        }
        SetFocus(f.dialog->control(id::ExecuteBase + 9));
        SetFocus(f.dialog->control(IDCANCEL));
        f.pump();
        CHECK(IsWindow(f.dialog->window()) && !f.dialog->result() && f.errors.empty());
        f.click(IDCANCEL);
        CHECK(!f.dialog->result());
    }
}
TEST(transfer_wheel_accumulates_clamps_reveals_and_enter_uses_current_row) {
    Fixture f;
    f.open(f.registered(), false, 0);
    SetWindowPos(f.dialog->window(), nullptr, 0, 0, 520, 390, SWP_NOMOVE | SWP_NOZORDER);
    f.wheel(-60);
    CHECK(f.dialog->active_slot() == 0);
    f.wheel(-60);
    CHECK(f.dialog->active_slot() == 1 && GetFocus() == f.dialog->control(id::EditBase + 1));
    f.wheel(120);
    f.wheel(120);
    CHECK(f.dialog->active_slot() == 0);
    f.wheel(-1200);
    CHECK(f.dialog->active_slot() == 9 && full_control(f.dialog->control(id::EditBase + 9)));
    SetFocus(f.dialog->control(id::ExecuteBase));
    f.pump();
    CHECK(f.dialog->active_slot() == 0 && full_control(f.dialog->control(id::ExecuteBase)));
    SetFocus(f.dialog->control(id::EditBase + 9));
    f.pump();
    CHECK(f.dialog->active_slot() == 9 && full_control(f.dialog->control(id::EditBase + 9)));
    MSG key{};
    key.hwnd = f.dialog->control(id::EditBase + 9);
    key.message = WM_KEYDOWN;
    key.wParam = VK_RETURN;
    if (!IsDialogMessageW(f.dialog->window(), &key))
        DispatchMessageW(&key);
    f.pump();
    const auto result = f.dialog->result();
    CHECK(result && result->slot == 9 && result->directory == f.folder(9));
}
TEST(transfer_browse_tracks_last_focused_row_and_cancel_keeps_text) {
    Fixture f;
    std::deque<std::filesystem::path> replies{{}, f.folder(8)};
    f.open(f.registered(), true, 3, [&](HWND, const wchar_t *title) {
        CHECK(std::wstring(title) == L"移動先フォルダ" && !replies.empty());
        auto result = replies.front();
        replies.pop_front();
        return result;
    });
    f.click(id::Browse);
    CHECK(window_text(f.dialog->control(id::EditBase + 3)) == f.folder(3).wstring());
    f.click(id::Browse);
    CHECK(window_text(f.dialog->control(id::EditBase + 3)) == f.folder(8).wstring());
    CHECK(window_text(f.dialog->control(id::EditBase + 8)) == f.folder(8).wstring());
    f.click(id::ExecuteBase + 3);
    CHECK(f.dialog->result()->slot == 3 && f.dialog->result()->directory == f.folder(8));
    f.open({}, false, -1, [&](HWND, const wchar_t *title) {
        CHECK(std::wstring(title) == L"コピー先フォルダ");
        return f.folder(2);
    });
    f.click(id::Browse);
    f.click(IDOK);
    CHECK(f.dialog->result()->options.basic_path == f.folder(2).wstring());
}
TEST(transfer_shell_folder_data_to_actual_ole_targets_and_legacy_hdrop) {
    Fixture f;
    for (int field = -1; field < 10; ++field) {
        f.open(field < 0 ? FileTransferOptions{} : f.registered(), false, field);
        const int key = field < 0 ? id::BasicEdit : id::EditBase + field;
        const auto edit = f.dialog->control(key);
        auto target = f.dialog->folder_drop_target(key);
        CHECK(target);
        const auto object = file_data_object(f.folder((field + 4) % 10));
        DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        CHECK(target->DragEnter(object.Get(), 0, {}, &effect) == S_OK && effect == DROPEFFECT_COPY);
        effect = DROPEFFECT_MOVE;
        CHECK(target->DragOver(0, {}, &effect) == S_OK && effect == DROPEFFECT_NONE);
        CHECK(target->DragLeave() == S_OK);
        effect = DROPEFFECT_COPY;
        CHECK(target->Drop(object.Get(), 0, {}, &effect) == S_OK && effect == DROPEFFECT_COPY);
        f.pump();
        CHECK(window_text(edit) == f.folder((field + 4) % 10).wstring() && GetFocus() == edit);
        const auto file = file_data_object(f.source);
        const auto previous = window_text(edit);
        const auto errors = f.errors.size();
        effect = DROPEFFECT_COPY;
        CHECK(target->Drop(file.Get(), 0, {}, &effect) == S_OK && effect == DROPEFFECT_NONE);
        CHECK(f.errors.size() == errors + 1 && window_text(edit) == previous);
        const auto legacy = file_data_object(f.folder(1));
        const auto memory = clone_drop(legacy.Get());
        SendMessageW(edit, WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0);
        f.pump();
        CHECK(window_text(edit) == f.folder(1).wstring());
        if (field >= 0) {
            auto hidden = f.dialog->folder_drop_target(id::BasicEdit);
            effect = DROPEFFECT_COPY;
            CHECK(hidden->DragEnter(object.Get(), 0, {}, &effect) == S_OK && effect == DROPEFFECT_NONE);
        }
        f.click(IDCANCEL);
    }
}
TEST(transfer_rejects_malformed_multiple_hdrop_and_detaches_stale_targets) {
    Fixture f;
    f.open();
    const auto edit = f.dialog->control(id::BasicEdit);
    f.set(id::BasicEdit, L"unchanged");
    raw_drop(edit, {1, 2});
    CHECK(f.errors.size() == 1 && window_text(edit) == L"unchanged");
    DROPFILES header{};
    header.pFiles = sizeof(header);
    header.fWide = TRUE;
    const auto paths = f.folder(0).wstring() + L'\0' + f.folder(1).wstring() + L'\0' + L'\0';
    std::vector<unsigned char> bytes(sizeof(header) + paths.size() * sizeof(wchar_t));
    memcpy(bytes.data(), &header, sizeof(header));
    memcpy(bytes.data() + sizeof(header), paths.data(), paths.size() * sizeof(wchar_t));
    raw_drop(edit, bytes);
    CHECK(f.errors.size() == 2 && window_text(edit) == L"unchanged");
    header.pFiles = 0xffffffff;
    memcpy(bytes.data(), &header, sizeof(header));
    raw_drop(edit, bytes);
    CHECK(f.errors.size() == 3 && window_text(edit) == L"unchanged");
    auto target = f.dialog->folder_drop_target(id::BasicEdit);
    const auto object = file_data_object(f.folder(0));
    f.dialog.reset();
    DWORD effect = DROPEFFECT_COPY;
    CHECK(target->DragEnter(object.Get(), 0, {}, &effect) == S_OK && effect == DROPEFFECT_NONE);
    effect = DROPEFFECT_COPY;
    CHECK(target->Drop(object.Get(), 0, {}, &effect) == S_OK && effect == DROPEFFECT_NONE);
}
TEST(transfer_dpi_and_small_windows_keep_selected_row_visible) {
    Fixture f;
    f.open(f.registered(), false, 9);
    for (const auto dpi : {144u, 192u, 96u}) {
        RECT bounds{20, 20, 800, 580};
        SendMessageW(f.dialog->window(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi),
                     reinterpret_cast<LPARAM>(&bounds));
        f.pump();
        CHECK(full_control(f.dialog->control(id::EditBase + 9)));
        LOGFONTW font{};
        CHECK(GetObjectW(
            reinterpret_cast<HFONT>(SendMessageW(f.dialog->control(id::BasicEdit), WM_GETFONT, 0, 0)),
            sizeof(font), &font));
        CHECK(font.lfHeight == -MulDiv(9, dpi, 72));
    }
    f.click(id::Registered);
    CHECK(full_control(f.dialog->control(id::BasicEdit)) && !f.dialog->registered_mode());
    f.click(id::Registered);
    CHECK(full_control(f.dialog->control(id::EditBase + 9)) && f.dialog->registered_mode());
    f.click(IDCANCEL);
}
TEST(transfer_legacy_ansi_folder_drop) {
    Fixture f;
    f.open();
    const auto path = f.folder(4).wstring();
    const auto length = WideCharToMultiByte(CP_ACP, 0, path.c_str(), static_cast<int>(path.size()), nullptr,
                                            0, nullptr, nullptr);
    CHECK(length > 0);
    DROPFILES header{};
    header.pFiles = sizeof(header);
    std::vector<unsigned char> bytes(sizeof(header) + length + 2, 0);
    memcpy(bytes.data(), &header, sizeof(header));
    WideCharToMultiByte(CP_ACP, 0, path.c_str(), static_cast<int>(path.size()),
                        reinterpret_cast<char *>(bytes.data() + sizeof(header)), length, nullptr, nullptr);
    raw_drop(f.dialog->control(id::BasicEdit), bytes);
    CHECK(f.errors.empty() && window_text(f.dialog->control(id::BasicEdit)) == path);
    f.click(IDCANCEL);
}
int main() {
    gvn_test::ClipboardLock clipboard_lock;
    if (!clipboard_lock)
        return 2;
    auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !gvn_test::noninteractive(station) || !SetProcessWindowStation(station))
        return 2;
    auto desktop = CreateDesktopW(L"GVN-transfer-dialog-test", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop))
        return 3;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com = OleInitialize(nullptr);
    if (FAILED(com)) {
        std::cerr << "OleInitialize failed: " << std::hex << static_cast<unsigned long>(com) << '\n';
        return 4;
    }
    if (!initialize_controls()) {
        std::cerr << "InitCommonControlsEx failed: " << GetLastError() << '\n';
        OleUninitialize();
        return 4;
    }
    const auto result = run_tests();
    OleUninitialize();
    return result;
}
