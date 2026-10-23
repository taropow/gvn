#include "gvn/jump_dialog.h"
#include "gvn/loader.h"
#include "gvn/renderer.h"
#include "gvn/ui.h"
#include <commctrl.h>
#include <windowsx.h>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <mutex>
#include <utility>

namespace gvn {
namespace {
namespace id = jump_control;
struct DialogTemplate {
    DLGTEMPLATE dialog{};
    WORD menu{}, cls{}, title{}, points{9};
    wchar_t face[13]{L"Yu Gothic UI"};
    DialogTemplate() {
        dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | DS_MODALFRAME | DS_SETFONT;
        dialog.cx = 410;
        dialog.cy = 160;
    }
};
std::wstring text(HWND window) {
    std::wstring value(static_cast<size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
    value.pop_back();
    return value;
}
} // namespace
struct JumpDialog::Impl {
    std::vector<FileEntry> entries; // A stable snapshot while the owner's modal message loop runs.
    size_t index{};
    bool reversed{}, modal{}, initialized{}, syncing{}, valid{true}, pending{};
    HWND hwnd{};
    HFONT font{};
    UINT dpi{96};
    JumpOptions draft;
    LoadingOptions loading_options;
    std::shared_ptr<JumpPreview> preview;
    JumpPreview::Mailbox inbox;
    std::optional<JumpChoice> selected;
    uint64_t sequence{};
    std::shared_ptr<Image> image;
    Renderer renderer;
    View view;
    Impl(std::span<const FileEntry> files, size_t current, bool reverse, JumpOptions options,
         LoadingOptions loading, Decoder decode, std::shared_ptr<JumpPreview> service)
        : entries(files.begin(), files.end()), index(current), reversed(reverse), draft(options),
          loading_options(loading) {
        if (entries.empty() || entries.size() > INT_MAX)
            throw std::invalid_argument("Jump requires 1..INT_MAX entries");
        if (service && decode)
            throw std::invalid_argument("The preview service owns its decoder");
        preview = service ? std::move(service) : std::make_shared<JumpPreview>(std::move(decode));
        inbox = preview->mailbox();
        index = std::min(index, entries.size() - 1);
        draft.quality = std::clamp(draft.quality, 0, 2);
        view.shrink_only = true;
        view.fit_limit = 1;
    }
    HWND control(int key) const {
        return GetDlgItem(hwnd, key);
    }
    int px(int value) const {
        return MulDiv(value, dpi, 96);
    }
    HWND add(int key, const wchar_t *cls, const wchar_t *label, DWORD style, DWORD ex = 0) {
        const bool combo = _wcsicmp(cls, WC_COMBOBOXW) == 0;
        if (combo)
            style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
        auto h = CreateWindowExW(ex, cls, label, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(key)),
                                 GetModuleHandleW(nullptr), nullptr);
        if (!h)
            throw std::runtime_error("Cannot create jump control");
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (combo)
            SetWindowSubclass(h, combo_print_proc, 1, 0);
        return h;
    }
    // Use the same native owner-draw/print path as the settings and save dialogs.
    static LRESULT CALLBACK combo_print_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
        const auto result = DefSubclassProc(h, m, w, l);
        if (m == WM_PRINT || m == WM_PRINTCLIENT) {
            const auto item = SendMessageW(h, CB_GETCURSEL, 0, 0);
            COMBOBOXINFO info{sizeof(info)};
            if (item >= 0 && GetComboBoxInfo(h, &info)) {
                DRAWITEMSTRUCT draw{};
                draw.CtlType = ODT_COMBOBOX;
                draw.CtlID = GetDlgCtrlID(h);
                draw.itemID = static_cast<UINT>(item);
                draw.itemAction = ODA_DRAWENTIRE;
                draw.itemState = ODS_COMBOBOXEDIT | (IsWindowEnabled(h) ? 0 : ODS_DISABLED);
                draw.hwndItem = h;
                draw.hDC = reinterpret_cast<HDC>(w);
                draw.rcItem = info.rcItem;
                SendMessageW(GetParent(h), WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
            }
        }
        return result;
    }
    void set_font() {
        if (font)
            DeleteObject(font);
        font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH,
                           L"Yu Gothic UI");
        if (!font)
            throw std::runtime_error("Cannot create jump font");
        EnumChildWindows(
            hwnd,
            [](HWND h, LPARAM f) -> BOOL {
                SendMessageW(h, WM_SETFONT, f, TRUE);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(font));
    }
    void layout() {
        RECT r{};
        GetClientRect(hwnd, &r);
        const int w = MulDiv(r.right, 96, dpi), h = MulDiv(r.bottom, 96, dpi);
        const int left = std::max(120, (w - 36) * 38 / 100), x = left + 24, width = w - x - 12;
        auto place = [&](int key, int x0, int y, int cx, int cy) {
            SetWindowPos(control(key), nullptr, px(x0), px(y), std::max(1, px(cx)), std::max(1, px(cy)),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        };
        place(id::Preview, 12, 12, left, h - 60);
        place(201, x, 12, width, 20);
        place(id::Name, x, 34, width, 24);
        place(202, x, 68, 55, 24);
        place(id::Order, x + 60, 68, 85, 24);
        place(id::Total, x + 155, 68, width - 155, 24);
        place(id::Scroll, x, 105, width, 20);
        place(id::Direction, x, 132, width, 20);
        place(id::Enabled, x, 158, width, 24);
        place(203, x, 192, 140, 24);
        place(id::Quality, x + 145, 190, std::max(80, width - 145), 140);
        place(id::Error, 12, h - 41, std::max(1, w - 230), 28);
        place(IDOK, w - 208, h - 40, 90, 28);
        place(IDCANCEL, w - 108, h - 40, 96, 28);
        RECT area{};
        GetClientRect(control(id::Preview), &area);
        view.resize(std::max(1L, area.right), std::max(1L, area.bottom));
        renderer.resize(area.right, area.bottom);
        InvalidateRect(control(id::Preview), nullptr, FALSE);
    }
    void cancel_preview() {
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        preview->cancel(inbox);
        pending = false;
        image.reset();
        renderer.invalidate();
    }
    void schedule() {
        cancel_preview();
        if (valid && draft.preview) {
            pending = true;
            SetTimer(hwnd, 2, 60, nullptr); // Coalesce rapid edit/scroll messages before filesystem I/O.
        }
        EnableWindow(control(id::Quality), draft.preview);
        InvalidateRect(control(id::Preview), nullptr, FALSE);
    }
    void start_preview() {
        KillTimer(hwnd, 2);
        if (!valid || !draft.preview)
            return;
        sequence = preview->request(entries[index].path, loading_options, inbox);
        SetTimer(hwnd, 1, 20, nullptr);
    }
    void receive() {
        auto result = preview->take(inbox);
        if (!result || result->sequence != sequence || !pending || !draft.preview || !valid)
            return;
        pending = false;
        KillTimer(hwnd, 1);
        image = std::move(result->image);
        if (image && !image->frames.empty()) {
            // A preview is a still image. Release all other animation frames promptly.
            image->frames.resize(1);
            image->frames.shrink_to_fit();
            view.set_image(image->width, image->height);
        }
        renderer.invalidate();
        InvalidateRect(control(id::Preview), nullptr, FALSE);
    }
    void synchronize(bool set_order = true) {
        syncing = true;
        if (set_order)
            SetWindowTextW(control(id::Order), std::to_wstring(index + 1).c_str());
        const auto &entry = entries[index];
        const auto name = entry.display_name.empty() ? entry.path.filename().wstring() : entry.display_name;
        SetWindowTextW(control(id::Name), name.c_str());
        SendMessageW(control(id::Name), EM_SETSEL, static_cast<WPARAM>(-1), -1);
        SendMessageW(control(id::Name), EM_SCROLLCARET, 0, 0);
        SCROLLINFO info{sizeof(info),
                        SIF_RANGE | SIF_PAGE | SIF_POS,
                        1,
                        static_cast<int>(entries.size()),
                        1,
                        static_cast<int>(reversed ? entries.size() - index : index + 1)};
        SetScrollInfo(control(id::Scroll), SB_CTL, &info, TRUE);
        SetWindowTextW(control(id::Error), L"");
        EnableWindow(control(IDOK), TRUE);
        valid = true;
        syncing = false;
        schedule();
    }
    void number_changed() {
        if (syncing)
            return;
        const auto value = text(control(id::Order));
        wchar_t *end{};
        errno = 0;
        const auto number = std::wcstoull(value.c_str(), &end, 10);
        if (value.empty() || value.front() < L'0' || value.front() > L'9' || errno == ERANGE || !end ||
            *end || number < 1 || number > entries.size()) {
            valid = false;
            EnableWindow(control(IDOK), FALSE);
            SetWindowTextW(control(id::Error), L"範囲内の番号を入力してください。");
            cancel_preview();
            InvalidateRect(control(id::Preview), nullptr, FALSE);
            return;
        }
        index = static_cast<size_t>(number - 1);
        synchronize(false);
    }
    void scroll(int code) {
        const int count = static_cast<int>(entries.size());
        int64_t position = static_cast<int64_t>(reversed ? entries.size() - index : index + 1);
        if (code == SB_LINELEFT)
            --position;
        else if (code == SB_LINERIGHT)
            ++position;
        else if (code == SB_PAGELEFT)
            position -= std::max(1, count / 10);
        else if (code == SB_PAGERIGHT)
            position += std::max(1, count / 10);
        else if (code == SB_LEFT)
            position = 1;
        else if (code == SB_RIGHT)
            position = count;
        else if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION) {
            SCROLLINFO info{sizeof(info), SIF_TRACKPOS};
            GetScrollInfo(control(id::Scroll), SB_CTL, &info);
            position = info.nTrackPos;
        } else
            return;
        position = std::clamp<int64_t>(position, 1, count);
        index = reversed ? entries.size() - position : static_cast<size_t>(position - 1);
        synchronize();
    }
    void finish(bool accept = false) {
        if (accept && !valid)
            return;
        if (accept)
            selected = JumpChoice{index, entries[index].path, draft};
        cancel_preview();
        KillTimer(hwnd, 1);
        if (modal)
            EndDialog(hwnd, accept ? IDOK : IDCANCEL);
        else
            DestroyWindow(hwnd);
    }
    void paint(HWND window, HDC dc) {
        renderer.nearest = draft.quality == 0;
        renderer.down = draft.quality;
        renderer.up = 0;
        if (dc)
            renderer.print(window, dc, image.get(), 0, view, 0, nullptr);
        else
            renderer.draw(window, image.get(), 0, view, 0, 0);
        if (!image) {
            HDC overlay = dc ? dc : GetDC(window);
            RECT r{};
            GetClientRect(window, &r);
            SetBkMode(overlay, TRANSPARENT);
            SetTextColor(overlay, RGB(220, 220, 220));
            auto old = SelectObject(overlay, font);
            const wchar_t *label = !valid           ? L"番号が範囲外です"
                                   : !draft.preview ? L"プレビュー OFF"
                                   : pending        ? L"読み込み中…"
                                                    : L"プレビューできません";
            DrawTextW(overlay, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(overlay, old);
            if (!dc)
                ReleaseDC(window, overlay);
        }
    }
    static LRESULT CALLBACK preview_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto &self = *reinterpret_cast<Impl *>(data);
        if (m == WM_ERASEBKGND)
            return 1;
        if (m == WM_PAINT) {
            PAINTSTRUCT ps{};
            BeginPaint(h, &ps);
            self.paint(h, nullptr);
            EndPaint(h, &ps);
            return 0;
        }
        if (m == WM_PRINT || m == WM_PRINTCLIENT) {
            self.paint(h, reinterpret_cast<HDC>(w));
            return 0;
        }
        return DefSubclassProc(h, m, w, l);
    }
    INT_PTR message(UINT m, WPARAM w, LPARAM l) {
        if (m == WM_MEASUREITEM && reinterpret_cast<MEASUREITEMSTRUCT *>(l)->CtlType == ODT_COMBOBOX) {
            reinterpret_cast<MEASUREITEMSTRUCT *>(l)->itemHeight = px(20);
            return TRUE;
        }
        if (m == WM_DRAWITEM && reinterpret_cast<DRAWITEMSTRUCT *>(l)->CtlType == ODT_COMBOBOX) {
            const auto draw = reinterpret_cast<DRAWITEMSTRUCT *>(l);
            const bool highlighted = (draw->itemState & ODS_SELECTED) != 0;
            const auto saved = SaveDC(draw->hDC);
            SelectObject(draw->hDC, font);
            FillRect(draw->hDC, &draw->rcItem,
                     GetSysColorBrush(highlighted ? COLOR_HIGHLIGHT : COLOR_WINDOW));
            if (draw->itemID != static_cast<UINT>(-1)) {
                const auto length = SendMessageW(draw->hwndItem, CB_GETLBTEXTLEN, draw->itemID, 0);
                if (length >= 0) {
                    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
                    SendMessageW(draw->hwndItem, CB_GETLBTEXT, draw->itemID,
                                 reinterpret_cast<LPARAM>(value.data()));
                    SetTextColor(draw->hDC, GetSysColor((draw->itemState & ODS_DISABLED) ? COLOR_GRAYTEXT
                                                        : highlighted                    ? COLOR_HIGHLIGHTTEXT
                                                                                         : COLOR_WINDOWTEXT));
                    SetBkMode(draw->hDC, TRANSPARENT);
                    auto bounds = draw->rcItem;
                    bounds.left += px(3);
                    DrawTextW(draw->hDC, value.data(), static_cast<int>(length), &bounds,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }
            if (draw->itemState & ODS_FOCUS)
                DrawFocusRect(draw->hDC, &draw->rcItem);
            RestoreDC(draw->hDC, saved);
            return TRUE;
        }
        if (m == WM_INITDIALOG) {
            dpi = GetDpiForWindow(hwnd);
            set_font();
            SetWindowTextW(hwnd, L"GVN - ジャンプ");
            auto canvas = add(id::Preview, L"STATIC", L"", SS_NOTIFY, WS_EX_CLIENTEDGE);
            SetWindowSubclass(canvas, preview_proc, 1, reinterpret_cast<DWORD_PTR>(this));
            add(201, L"STATIC", L"名前(&N)", SS_LEFT);
            add(id::Name, L"EDIT", L"", WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE);
            add(202, L"STATIC", L"順番(&X)", SS_LEFT);
            add(id::Order, L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL | ES_RIGHT,
                WS_EX_CLIENTEDGE);
            SendMessageW(control(id::Order), EM_SETLIMITTEXT, 20, 0);
            add(id::Total, L"STATIC", (L"総数: " + std::to_wstring(entries.size())).c_str(), SS_LEFT);
            add(id::Scroll, L"SCROLLBAR", L"", SBS_HORZ | WS_TABSTOP);
            add(id::Direction, L"STATIC", reversed ? L"末尾 ← → 先頭" : L"先頭 ← → 末尾", SS_RIGHT);
            add(id::Enabled, L"BUTTON", L"プレビュー(&P)", WS_TABSTOP | BS_AUTOCHECKBOX);
            SendMessageW(control(id::Enabled), BM_SETCHECK, draft.preview ? BST_CHECKED : BST_UNCHECKED, 0);
            add(203, L"STATIC", L"縮小表示の画質(&Q)", SS_LEFT);
            add(id::Quality, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
            for (const auto label : {L"低", L"中", L"高"})
                SendMessageW(control(id::Quality), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
            SendMessageW(control(id::Quality), CB_SETCURSEL, draft.quality, 0);
            add(id::Error, L"STATIC", L"", SS_LEFT);
            add(IDOK, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON);
            add(IDCANCEL, L"BUTTON", L"キャンセル", WS_TABSTOP | BS_PUSHBUTTON);
            RECT bounds{0, 0, px(680), px(280)}, owner{};
            AdjustWindowRectExForDpi(&bounds, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                                     static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), dpi);
            GetWindowRect(GetParent(hwnd), &owner);
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
            const auto placed = place_window(
                {owner.left + (owner.right - owner.left - bounds.right + bounds.left) / 2,
                 owner.top + (owner.bottom - owner.top - bounds.bottom + bounds.top) / 2,
                 bounds.right - bounds.left, bounds.bottom - bounds.top},
                {monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.right - monitor.rcWork.left,
                 monitor.rcWork.bottom - monitor.rcWork.top},
                false);
            SetWindowPos(hwnd, nullptr, placed.x, placed.y, placed.w, placed.h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            synchronize();
            initialized = true;
            SetFocus(control(id::Order));
            SendMessageW(control(id::Order), EM_SETSEL, 0, -1);
            return FALSE;
        }
        if (m == WM_SIZE && initialized) {
            layout();
            return TRUE;
        }
        if (m == WM_GETMINMAXINFO) {
            const auto info = reinterpret_cast<MINMAXINFO *>(l);
            info->ptMinTrackSize = {px(640), px(300)};
            return TRUE;
        }
        if (m == WM_DPICHANGED) {
            dpi = HIWORD(w);
            set_font();
            auto r = reinterpret_cast<RECT *>(l);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            return TRUE;
        }
        if (m == WM_TIMER) {
            if (w == 2)
                start_preview();
            else if (w == 1)
                receive();
            return TRUE;
        }
        if (m == WM_HSCROLL && reinterpret_cast<HWND>(l) == control(id::Scroll)) {
            scroll(LOWORD(w));
            return TRUE;
        }
        if (m == WM_CLOSE) {
            finish();
            return TRUE;
        }
        if (m == WM_DESTROY) {
            KillTimer(hwnd, 1);
            cancel_preview();
            initialized = false;
            return TRUE;
        }
        if (m == WM_COMMAND) {
            const int key = LOWORD(w), code = HIWORD(w);
            if (key == IDCANCEL || key == IDOK) {
                finish(key == IDOK);
                return TRUE;
            }
            if (!initialized)
                return FALSE;
            if (key == id::Order && code == EN_CHANGE) {
                number_changed();
                return TRUE;
            }
            if (key == id::Enabled && code == BN_CLICKED) {
                draft.preview = SendMessageW(control(key), BM_GETCHECK, 0, 0) == BST_CHECKED;
                schedule();
                return TRUE;
            }
            if (key == id::Quality && code == CBN_SELCHANGE) {
                draft.quality = static_cast<int>(SendMessageW(control(key), CB_GETCURSEL, 0, 0));
                renderer.invalidate();
                InvalidateRect(control(id::Preview), nullptr, FALSE);
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
            return self->message(m, w, l);
        } catch (const std::exception &) {
            self->finish();
            return TRUE;
        }
    }
};
JumpDialog::JumpDialog(std::span<const FileEntry> files, size_t current, bool reversed, JumpOptions options,
                       LoadingOptions loading, Decoder decode, std::shared_ptr<JumpPreview> service)
    : impl(std::make_unique<Impl>(files, current, reversed, options, loading, std::move(decode),
                                  std::move(service))) {}
JumpDialog::~JumpDialog() {
    if (IsWindow(impl->hwnd))
        DestroyWindow(impl->hwnd);
    impl->preview->cancel(impl->inbox);
    if (impl->font)
        DeleteObject(impl->font);
}
std::optional<JumpChoice> JumpDialog::show(HWND owner) {
    DialogTemplate dialog;
    impl->modal = true;
    if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &dialog.dialog, owner, Impl::procedure,
                                reinterpret_cast<LPARAM>(impl.get())) == -1)
        throw std::runtime_error("Cannot open jump dialog");
    return impl->selected;
}
HWND JumpDialog::create(HWND owner) {
    DialogTemplate dialog;
    const auto window = CreateDialogIndirectParamW(GetModuleHandleW(nullptr), &dialog.dialog, owner,
                                                   Impl::procedure, reinterpret_cast<LPARAM>(impl.get()));
    if (!IsWindow(window))
        throw std::runtime_error("Cannot create jump dialog");
    return window;
}
HWND JumpDialog::window() const {
    return impl->hwnd;
}
HWND JumpDialog::control(int key) const {
    return impl->control(key);
}
bool JumpDialog::ready() const {
    return impl->initialized;
}
bool JumpDialog::loading() const {
    return impl->pending;
}
bool JumpDialog::valid() const {
    return impl->valid;
}
size_t JumpDialog::selected_index() const {
    return impl->index;
}
JumpOptions JumpDialog::options() const {
    return impl->draft;
}
std::shared_ptr<const Image> JumpDialog::preview_image() const {
    return impl->image;
}
std::optional<JumpChoice> JumpDialog::result() const {
    return impl->selected;
}
std::optional<JumpChoice> choose_jump(HWND owner, std::span<const FileEntry> files, size_t current,
                                      bool reversed, JumpOptions options, LoadingOptions loading,
                                      std::shared_ptr<JumpPreview> service) {
    if (!service)
        throw std::invalid_argument("The viewer must own the preview service");
    JumpDialog dialog(files, current, reversed, options, loading, {}, std::move(service));
    return dialog.show(owner);
}
} // namespace gvn
