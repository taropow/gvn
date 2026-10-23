#include "gvn/ui_theme.h"
#include "gvn/file_transfer_dialog.h"
#include "gvn/ui.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <vector>

namespace gvn {
namespace {
namespace id = transfer_control;
struct DialogTemplate {
    DLGTEMPLATE dialog{};
    WORD menu{}, cls{}, title{}, points{9};
    wchar_t face[13]{L"Yu Gothic UI"};
    DialogTemplate() {
        dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | DS_MODALFRAME | DS_SETFONT;
        dialog.cx = 400;
        dialog.cy = 180;
    }
};
std::wstring text(HWND window) {
    std::wstring value(static_cast<size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
    value.pop_back();
    return value;
}
std::wstring path_text(HWND window) {
    auto value = text(window);
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
        value = value.substr(1, value.size() - 2);
    return value;
}
bool visible(HWND window) {
    if (!IsWindow(window))
        return false;
    for (;;) {
        const auto style = GetWindowLongPtrW(window, GWL_STYLE);
        if (!(style & WS_VISIBLE))
            return false;
        if (!(style & WS_CHILD))
            return true;
        window = GetParent(window);
    }
}
// Decode only one bounded CF_HDROP path. Do not ask DragQueryFile to parse a
// potentially malformed, unbounded block supplied by another application.
std::filesystem::path dropped_folder(HGLOBAL memory) {
    const auto size = GlobalSize(memory);
    if (size < sizeof(DROPFILES) || size > 128 * 1024)
        return {};
    const auto bytes = static_cast<const unsigned char *>(GlobalLock(memory));
    if (!bytes)
        return {};
    struct Unlock {
        HGLOBAL memory;
        ~Unlock() {
            GlobalUnlock(memory);
        }
    } unlock{memory};
    DROPFILES header{};
    memcpy(&header, bytes, sizeof(header));
    if (header.pFiles < sizeof(header) || header.pFiles >= size)
        return {};
    const auto available = size - header.pFiles;
    std::wstring value;
    if (header.fWide) {
        if (header.pFiles % sizeof(wchar_t) || available < 2 * sizeof(wchar_t))
            return {};
        const auto begin = reinterpret_cast<const wchar_t *>(bytes + header.pFiles);
        const auto count = available / sizeof(wchar_t);
        const auto end = std::find(begin, begin + count, L'\0');
        if (end == begin || end == begin + count || end + 1 == begin + count || end[1] != L'\0' ||
            end - begin > 32760)
            return {};
        value.assign(begin, end);
    } else {
        const auto begin = reinterpret_cast<const char *>(bytes + header.pFiles);
        const auto end = std::find(begin, begin + available, '\0');
        if (end == begin || end == begin + available || end + 1 == begin + available || end[1] != '\0')
            return {};
        const int length = MultiByteToWideChar(CP_ACP, 0, begin, static_cast<int>(end - begin), nullptr, 0);
        if (length <= 0 || length > 32760)
            return {};
        value.resize(length);
        MultiByteToWideChar(CP_ACP, 0, begin, static_cast<int>(end - begin), value.data(), length);
    }
    std::filesystem::path path(value);
    std::error_code error;
    if (!path.is_absolute() || !std::filesystem::is_directory(path, error))
        return {};
    return path;
}
} // namespace

struct FileTransferDialog::Impl {
    struct FolderTarget final : IDropTarget {
        LONG references{1};
        Impl *owner{};
        HWND edit{};
        bool offered{};
        FolderTarget(Impl *owner, HWND edit) : owner(owner), edit(edit) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (iid != IID_IUnknown && iid != IID_IDropTarget)
                return E_NOINTERFACE;
            *out = static_cast<IDropTarget *>(this);
            AddRef();
            return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef() override {
            return InterlockedIncrement(&references);
        }
        ULONG STDMETHODCALLTYPE Release() override {
            const auto count = InterlockedDecrement(&references);
            if (!count)
                delete this;
            return count;
        }
        HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *data, DWORD, POINTL, DWORD *effect) override {
            if (!effect)
                return E_POINTER;
            FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            offered = owner && visible(edit) && data && data->QueryGetData(&format) == S_OK;
            *effect = offered && (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD *effect) override {
            if (!effect)
                return E_POINTER;
            *effect = owner && visible(edit) && offered && (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY
                                                                                       : DROPEFFECT_NONE;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE DragLeave() override {
            offered = false;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE Drop(IDataObject *data, DWORD, POINTL, DWORD *effect) override {
            if (!effect)
                return E_POINTER;
            const bool allowed = (*effect & DROPEFFECT_COPY) != 0;
            *effect = DROPEFFECT_NONE;
            offered = false;
            if (!owner || !visible(edit) || !data || !allowed)
                return S_OK;
            FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM medium{};
            if (FAILED(data->GetData(&format, &medium)))
                return S_OK;
            struct Release {
                STGMEDIUM *medium;
                ~Release() {
                    ReleaseStgMedium(medium);
                }
            } release{&medium};
            try {
                if (medium.tymed == TYMED_HGLOBAL && owner->drop(edit, medium.hGlobal))
                    *effect = DROPEFFECT_COPY;
            } catch (...) {
                return E_UNEXPECTED;
            }
            return S_OK;
        }
    };
    bool move{}, modal{}, building{}, initialized{}, ten_ready{};
    HWND hwnd{}, basic{}, ten{}, scrollbar{};
    HFONT font{};
    UINT dpi{96};
    int active{}, scroll{}, wheel_remainder{};
    std::filesystem::path source;
    FileTransferOptions draft;
    std::optional<FileTransferChoice> selected;
    Browse browse;
    Error error;
    std::map<int, HWND> controls;
    std::map<int, Microsoft::WRL::ComPtr<FolderTarget>> targets;

    Impl(bool move, const std::filesystem::path &source, const FileTransferOptions &options, int initial_slot,
         Browse browse, Error error)
        : move(move), source(std::filesystem::absolute(source).lexically_normal()), draft(options),
          browse(std::move(browse)), error(std::move(error)) {
        if (initial_slot < -1 || initial_slot > 9)
            throw std::invalid_argument("Invalid initial transfer slot");
        if (initial_slot >= 0) {
            active = initial_slot;
            draft.registered = true;
        }
        if (!this->browse)
            this->browse = choose_folder;
    }
    int px(int value) const {
        return MulDiv(value, dpi, 96);
    }
    HWND control(int key) const {
        const auto entry = controls.find(key);
        return entry == controls.end() ? nullptr : entry->second;
    }
    void notify(const wchar_t *message) {
        if (error)
            error(hwnd, message);
        else
            MessageBoxW(hwnd, message, L"送り先を確認してください", MB_OK | MB_ICONWARNING);
    }
    HWND make(HWND parent, const wchar_t *cls, const wchar_t *label, DWORD style, int key) {
        const auto h = CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, label,
                                       WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(key)),
                                       GetModuleHandleW(nullptr), nullptr);
        if (!h)
            throw std::runtime_error("Cannot create transfer control");
        controls[key] = h;
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        return h;
    }
    void set_font() {
        const auto previous = font;
        font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                           0, CLEARTYPE_QUALITY, 0, L"Yu Gothic UI");
        if (!font)
            throw std::runtime_error("Cannot create transfer font");
        for (const auto &[key, window] : controls)
            SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (previous)
            DeleteObject(previous);
    }
    static LRESULT CALLBACK input_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Impl *>(data);
        if (m == WM_MOUSEWHEEL) {
            if (self->draft.registered)
                self->wheel(GET_WHEEL_DELTA_WPARAM(w));
            return 0;
        }
        if (m == WM_GETDLGCODE && l) {
            const auto message = reinterpret_cast<const MSG *>(l);
            if (message->message == WM_KEYDOWN && message->wParam == VK_RETURN)
                return DefSubclassProc(h, m, w, l) | DLGC_WANTMESSAGE;
        }
        if (m == WM_KEYDOWN && w == VK_RETURN) {
            SendMessageW(self->hwnd, WM_COMMAND, IDOK, 0);
            return 0;
        }
        if (m == WM_DROPFILES) {
            const auto memory = reinterpret_cast<HGLOBAL>(w);
            try {
                self->drop(h, memory);
            } catch (...) {
                self->notify(L"フォルダを読み取れませんでした。");
            }
            DragFinish(static_cast<HDROP>(memory));
            return 0;
        }
        return DefSubclassProc(h, m, w, l);
    }
    static LRESULT CALLBACK pane_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Impl *>(data);
        if (m == WM_COMMAND || m == WM_VSCROLL)
            return SendMessageW(self->hwnd, m, w, l);
        if (m == WM_MOUSEWHEEL) {
            if (self->draft.registered)
                self->wheel(GET_WHEEL_DELTA_WPARAM(w));
            return 0;
        }
        return DefSubclassProc(h, m, w, l);
    }
    void input(HWND parent, int key, const std::wstring &value) {
        const auto h = make(parent, L"EDIT", value.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, key);
        SendMessageW(h, EM_SETLIMITTEXT, 32760, 0);
        SetWindowSubclass(h, input_proc, 1, reinterpret_cast<DWORD_PTR>(this));
        DragAcceptFiles(h, TRUE);
        Microsoft::WRL::ComPtr<FolderTarget> target;
        target.Attach(new FolderTarget(this, h));
        if (FAILED(RegisterDragDrop(h, target.Get())))
            throw std::runtime_error("Cannot register folder drop target");
        targets.emplace(key, std::move(target));
    }
    void cleanup_targets() {
        for (auto &[key, target] : targets) {
            target->owner = nullptr;
            RevokeDragDrop(control(key));
        }
        targets.clear();
    }
    void collect() {
        draft.basic_path = path_text(control(id::BasicEdit));
        for (int i = 0; i < 10; ++i)
            if (const auto edit = control(id::EditBase + i))
                draft.destinations[i] = path_text(edit);
        draft.confirm = SendMessageW(control(id::Confirm), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void place(HWND child, int x, int y, int width, int height) {
        if (!child)
            return;
        MoveWindow(child, px(x), px(y), px(std::max(1, width)), px(std::max(1, height)), TRUE);
    }
    void layout() {
        if (building || !basic || !ten)
            return;
        RECT client{};
        GetClientRect(hwnd, &client);
        const int width = MulDiv(client.right, 96, dpi), height = MulDiv(client.bottom, 96, dpi);
        place(control(id::Source), 16, 12, width - 32, 22);
        place(control(id::Base), 16, 38, width - 32, 22);
        const int body_height = std::max(32, height - 190), body_width = std::max(100, width - 32);
        place(basic, 16, 70, body_width, body_height);
        place(ten, 16, 70, body_width, body_height);
        ShowWindow(basic, draft.registered ? SW_HIDE : SW_SHOW);
        ShowWindow(ten, draft.registered ? SW_SHOW : SW_HIDE);
        place(control(id::BasicLabel), 0, 0, body_width, 22);
        place(control(id::BasicEdit), 0, 26, body_width, 28);
        if (ten_ready) {
            const bool scrolling = body_height < 340;
            const int bar = scrolling ? MulDiv(GetSystemMetricsForDpi(SM_CXVSCROLL, dpi), 96, dpi) : 0;
            scroll = std::clamp(scroll, 0, std::max(0, 340 - body_height));
            SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS, 0,
                            339,          static_cast<UINT>(body_height), scroll};
            SetScrollInfo(scrollbar, SB_CTL, &info, TRUE);
            place(scrollbar, body_width - bar, 0, std::max(1, bar), body_height);
            ShowWindow(scrollbar, scrolling ? SW_SHOW : SW_HIDE);
            for (int i = 0; i < 10; ++i) {
                place(control(id::ExecuteBase + i), 0, i * 34 - scroll, 48, 28);
                place(control(id::EditBase + i), 58, i * 34 - scroll, body_width - bar - 62, 28);
            }
        }
        place(control(id::Confirm), 16, height - 112, width - 32, 24);
        place(control(id::Registered), 16, height - 78, width - 32, 24);
        place(control(IDOK), width - 296, height - 44, 88, 28);
        place(control(id::Browse), width - 200, height - 44, 88, 28);
        place(control(IDCANCEL), width - 104, height - 44, 88, 28);
        ShowWindow(control(IDOK), draft.registered ? SW_HIDE : SW_SHOW);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    void reveal(int slot) {
        active = std::clamp(slot, 0, 9);
        RECT rect{};
        GetClientRect(ten, &rect);
        const int height = MulDiv(rect.bottom, 96, dpi), top = active * 34;
        if (top < scroll)
            scroll = top;
        else if (top + 28 > scroll + height)
            scroll = top + 28 - height;
        layout();
        for (int i = 0; i < 10; ++i)
            SendMessageW(control(id::ExecuteBase + i), BM_SETSTYLE,
                         (i == active ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON) | BS_NOTIFY, TRUE);
        SendMessageW(hwnd, DM_SETDEFID, id::ExecuteBase + active, 0);
    }
    void focus_input() {
        const auto edit = control(draft.registered ? id::EditBase + active : id::BasicEdit);
        if (draft.registered)
            reveal(active);
        else
            SendMessageW(hwnd, DM_SETDEFID, IDOK, 0);
        SetFocus(edit);
        PostMessageW(hwnd, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(edit), TRUE);
    }
    void wheel(int delta) {
        wheel_remainder += delta;
        while (std::abs(wheel_remainder) >= WHEEL_DELTA) {
            const int direction = wheel_remainder > 0 ? -1 : 1;
            wheel_remainder += direction * WHEEL_DELTA;
            reveal(active + direction);
        }
        SetFocus(control(id::EditBase + active));
    }
    void resize_to_mode(bool first = false) {
        RECT work{}, current{}, client{};
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
        work = monitor.rcWork;
        GetWindowRect(hwnd, &current);
        GetClientRect(hwnd, &client);
        RECT bounds{0, 0, first ? px(640) : client.right, px(draft.registered ? 540 : 248)};
        AdjustWindowRectExForDpi(&bounds, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                                 static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), dpi);
        const int width = std::min(bounds.right - bounds.left, work.right - work.left);
        const int height = std::min(bounds.bottom - bounds.top, work.bottom - work.top);
        const int x = first ? work.left + (work.right - work.left - width) / 2 : current.left;
        const int y = first ? work.top + (work.bottom - work.top - height) / 2 : current.top;
        SetWindowPos(hwnd, nullptr,
                     std::clamp(x, static_cast<int>(work.left), static_cast<int>(work.right) - width),
                     std::clamp(y, static_cast<int>(work.top), static_cast<int>(work.bottom) - height), width,
                     height, SWP_NOZORDER);
        layout();
    }
    void ensure_ten() {
        if (ten_ready)
            return;
        const bool previous = building;
        building = true;
        for (int i = 0; i < 10; ++i) {
            const auto label = i == 9 ? std::wstring(L"1&0") : L"&" + std::to_wstring(i + 1);
            make(ten, L"BUTTON", label.c_str(), WS_TABSTOP | BS_NOTIFY, id::ExecuteBase + i);
            input(ten, id::EditBase + i, draft.destinations[i]);
        }
        scrollbar = make(ten, L"SCROLLBAR", L"", SBS_VERT | WS_TABSTOP, id::Scroll);
        ten_ready = true;
        building = previous;
    }
    void initialize() {
        building = true;
        dpi = GetDpiForWindow(hwnd);
        set_font();
        SetWindowTextW(hwnd, move ? L"ファイルを移動" : L"ファイルをコピー");
        make(hwnd, L"STATIC", (L"対象: " + source.wstring()).c_str(), SS_PATHELLIPSIS | SS_NOPREFIX,
             id::Source);
        make(hwnd, L"STATIC", (L"相対パスの基準: " + source.parent_path().wstring()).c_str(),
             SS_PATHELLIPSIS | SS_NOPREFIX, id::Base);
        basic = make(hwnd, L"STATIC", L"", WS_CLIPCHILDREN, id::BasicPane);
        ten = make(hwnd, L"STATIC", L"", WS_CLIPCHILDREN, id::RegisteredPane);
        for (auto pane : {basic, ten}) {
            SetWindowLongPtrW(pane, GWL_EXSTYLE, GetWindowLongPtrW(pane, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
            SetWindowSubclass(pane, pane_proc, 1, reinterpret_cast<DWORD_PTR>(this));
        }
        make(basic, L"STATIC", move ? L"移動先フォルダ" : L"コピー先フォルダ", 0, id::BasicLabel);
        input(basic, id::BasicEdit, draft.basic_path);
        if (draft.registered)
            ensure_ten();
        make(hwnd, L"BUTTON", L"上書き時に確認する", BS_AUTOCHECKBOX | WS_TABSTOP, id::Confirm);
        make(hwnd, L"BUTTON", L"10 個 登録", BS_AUTOCHECKBOX | WS_TABSTOP, id::Registered);
        make(hwnd, L"BUTTON", move ? L"移動" : L"コピー", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
        make(hwnd, L"BUTTON", L"参照…", WS_TABSTOP, id::Browse);
        make(hwnd, L"BUTTON", L"キャンセル", WS_TABSTOP, IDCANCEL);
        SendMessageW(control(id::Confirm), BM_SETCHECK, draft.confirm ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(control(id::Registered), BM_SETCHECK, draft.registered ? BST_CHECKED : BST_UNCHECKED, 0);
        building = false;
        initialized = true;
        resize_to_mode(true);
        focus_input();
    }
    bool drop(HWND edit, HGLOBAL memory) {
        const auto folder = dropped_folder(memory);
        if (folder.empty()) {
            notify(L"フォルダを1個ドロップしてください。");
            return false;
        }
        SetWindowTextW(edit, folder.c_str());
        SetFocus(edit);
        return true;
    }
    void finish() {
        if (modal)
            EndDialog(hwnd, selected ? IDOK : IDCANCEL);
        else
            DestroyWindow(hwnd);
    }
    void accept(int slot) {
        collect();
        auto value = slot < 0 ? draft.basic_path : draft.destinations.at(slot);
        if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
            value = value.substr(1, value.size() - 2);
        const std::filesystem::path directory(value);
        std::error_code filesystem_error;
        const auto absolute = directory.is_relative() ? source.parent_path() / directory : directory;
        if (value.empty() || !std::filesystem::is_directory(absolute, filesystem_error)) {
            notify(L"存在する送り先フォルダを入力してください。相対パスも使用できます。");
            if (slot >= 0)
                active = slot;
            focus_input();
            return;
        }
        (slot < 0 ? draft.basic_path : draft.destinations[slot]) = value;
        selected = FileTransferChoice{draft, directory, slot};
        finish();
    }
    INT_PTR message(UINT m, WPARAM w, LPARAM l) {
        if (m == WM_INITDIALOG) {
            initialize();
            return FALSE;
        }
        if (m == WM_DESTROY) {
            cleanup_targets();
            return TRUE;
        }
        if (m == WM_CLOSE) {
            finish();
            return TRUE;
        }
        if (m == WM_SIZE) {
            layout();
            return TRUE;
        }
        if (m == WM_DPICHANGED) {
            dpi = HIWORD(w);
            set_font();
            const auto rect = reinterpret_cast<RECT *>(l);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                         rect->bottom - rect->top, SWP_NOZORDER);
            layout();
            if (draft.registered)
                reveal(active);
            return TRUE;
        }
        if (m == WM_GETMINMAXINFO) {
            RECT bounds{0, 0, px(360), px(draft.registered ? 280 : 248)};
            AdjustWindowRectExForDpi(&bounds, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                                     static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), dpi);
            auto info = reinterpret_cast<MINMAXINFO *>(l);
            info->ptMinTrackSize = {bounds.right - bounds.left, bounds.bottom - bounds.top};
            return TRUE;
        }
        if (m == WM_MOUSEWHEEL) {
            if (draft.registered)
                wheel(GET_WHEEL_DELTA_WPARAM(w));
            return TRUE;
        }
        if (m == WM_VSCROLL && reinterpret_cast<HWND>(l) == scrollbar) {
            SCROLLINFO info{sizeof(info), SIF_ALL};
            GetScrollInfo(scrollbar, SB_CTL, &info);
            const int code = LOWORD(w), page = static_cast<int>(info.nPage);
            if (code == SB_LINEUP)
                scroll -= 34;
            else if (code == SB_LINEDOWN)
                scroll += 34;
            else if (code == SB_PAGEUP)
                scroll -= page;
            else if (code == SB_PAGEDOWN)
                scroll += page;
            else if (code == SB_TOP)
                scroll = 0;
            else if (code == SB_BOTTOM)
                scroll = 340;
            else if (code == SB_THUMBPOSITION)
                scroll = HIWORD(w);
            else if (code == SB_THUMBTRACK)
                scroll = info.nTrackPos;
            layout();
            return TRUE;
        }
        if (m == WM_COMMAND) {
            const int key = LOWORD(w), code = HIWORD(w);
            if (key == IDCANCEL) {
                finish();
                return TRUE;
            }
            if (key == id::Registered && code == BN_CLICKED) {
                collect();
                draft.registered = SendMessageW(control(id::Registered), BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (draft.registered)
                    ensure_ten();
                wheel_remainder = 0;
                resize_to_mode();
                focus_input();
                return TRUE;
            }
            if (code == EN_SETFOCUS && key >= id::EditBase && key < id::EditBase + 10) {
                reveal(key - id::EditBase);
                return TRUE;
            }
            if (code == BN_SETFOCUS && key >= id::ExecuteBase && key < id::ExecuteBase + 10) {
                reveal(key - id::ExecuteBase);
                return TRUE;
            }
            if (key == id::Browse && code == BN_CLICKED) {
                const auto directory = browse(hwnd, move ? L"移動先フォルダ" : L"コピー先フォルダ");
                if (!directory.empty()) {
                    SetWindowTextW(control(draft.registered ? id::EditBase + active : id::BasicEdit),
                                   directory.c_str());
                    focus_input();
                }
                return TRUE;
            }
            if (key == IDOK) {
                accept(draft.registered ? active : -1);
                return TRUE;
            }
            if (draft.registered && code == BN_CLICKED && key >= id::ExecuteBase &&
                key < id::ExecuteBase + 10) {
                accept(key - id::ExecuteBase);
                return TRUE;
            }
        }
        return FALSE;
    }
    static INT_PTR CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<Impl *>(GetWindowLongPtrW(h, DWLP_USER));
        if (m == WM_INITDIALOG) {
            self = reinterpret_cast<Impl *>(l);
            self->hwnd = h;
            SetWindowLongPtrW(h, DWLP_USER, l);
        }
        if (!self)
            return FALSE;
        try {
            const auto result=self->message(m, w, l);if(m==WM_INITDIALOG||m==WM_DPICHANGED)apply_ui_theme(h);return result;
        } catch (const std::exception &) {
            self->notify(L"ファイル操作画面の処理に失敗しました。");
            self->finish();
            return TRUE;
        }
    }
};
FileTransferDialog::FileTransferDialog(bool move, const std::filesystem::path &source,
                                       const FileTransferOptions &options, int initial_slot, Browse browse,
                                       Error error)
    : impl(std::make_unique<Impl>(move, source, options, initial_slot, std::move(browse), std::move(error))) {
}
FileTransferDialog::~FileTransferDialog() {
    if (IsWindow(impl->hwnd))
        DestroyWindow(impl->hwnd);
    impl->cleanup_targets();
    if (impl->font)
        DeleteObject(impl->font);
}
std::optional<FileTransferChoice> FileTransferDialog::show(HWND owner) {
    DialogTemplate dialog;
    impl->modal = true;
    if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &dialog.dialog, owner, Impl::procedure,
                                reinterpret_cast<LPARAM>(impl.get())) == -1)
        throw std::runtime_error("Cannot open file transfer dialog");
    return impl->selected;
}
HWND FileTransferDialog::create(HWND owner) {
    DialogTemplate dialog;
    const auto window = CreateDialogIndirectParamW(GetModuleHandleW(nullptr), &dialog.dialog, owner,
                                                   Impl::procedure, reinterpret_cast<LPARAM>(impl.get()));
    if (!IsWindow(window))
        throw std::runtime_error("Cannot create file transfer dialog");
    return window;
}
HWND FileTransferDialog::window() const {
    return impl->hwnd;
}
HWND FileTransferDialog::control(int key) const {
    return impl->control(key);
}
int FileTransferDialog::active_slot() const {
    return impl->active;
}
bool FileTransferDialog::registered_mode() const {
    return impl->draft.registered;
}
bool FileTransferDialog::ready() const {
    return impl->initialized && !impl->building;
}
std::optional<FileTransferChoice> FileTransferDialog::result() const {
    return impl->selected;
}
Microsoft::WRL::ComPtr<IDropTarget> FileTransferDialog::folder_drop_target(int key) const {
    const auto entry = impl->targets.find(key);
    if (entry == impl->targets.end())
        return {};
    return entry->second;
}
std::optional<FileTransferChoice> choose_file_transfer(HWND owner, bool move,
                                                       const std::filesystem::path &source,
                                                       const FileTransferOptions &options, int initial_slot) {
    FileTransferDialog dialog(move, source, options, initial_slot);
    return dialog.show(owner);
}
} // namespace gvn
