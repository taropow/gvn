#include "gvn/save_dialog.h"
#include <commdlg.h>
#include <commctrl.h>
#include <dlgs.h>
#include <array>
#include <cstring>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>

namespace gvn {
namespace {
namespace id = save_control;
struct Validation {
    std::wstring message;
};
std::wstring hex_color(uint32_t value) {
    std::wostringstream text;
    text << std::hex << std::uppercase << std::setfill(L'0') << std::setw(6) << value;
    return text.str();
}
class Dialog {
    SaveOptions initial;
    SaveFormat current;
    HWND window{}, parent{}, common{};
    std::array<HWND, 4> panes{};
    std::map<DWORD, HWND> widgets;
    std::wstring extension;
    OPENFILENAMEW file{};
    std::array<wchar_t, 32768> filename{};
    int minimum_width{};
    static constexpr UINT apply_format = WM_APP + 273;
    std::optional<SaveFormat> pending_format;
    std::filesystem::path pending_path;
    void apply_pending_format() {
        if (!pending_format)
            return;
        file.nFilterIndex = static_cast<DWORD>(*pending_format) + 1;
        pending_format.reset();
        const auto types = GetDlgItem(parent, cmb1);
        SendMessageW(types, CB_SETCURSEL, file.nFilterIndex - 1, 0);
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(cmb1, CBN_SELCHANGE), reinterpret_cast<LPARAM>(types));
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(cmb1, CBN_SELENDOK), reinterpret_cast<LPARAM>(types));
        refresh();
        auto name = pending_path.wstring();
        const auto folder = std::filesystem::path(dialog_text(CDM_GETFOLDERPATH));
        if (_wcsicmp(folder.lexically_normal().c_str(),
                     pending_path.parent_path().lexically_normal().c_str()) == 0)
            name = pending_path.filename().wstring();
        auto input = GetDlgItem(parent, edt1);
        if (!input)
            input =
                reinterpret_cast<HWND>(SendMessageW(GetDlgItem(parent, cmb13), CBEM_GETEDITCONTROL, 0, 0));
        if (!input || !SetWindowTextW(input, name.c_str()))
            throw std::runtime_error("Cannot retain typed save filename");
    }
    static LRESULT draw_combo(const DRAWITEMSTRUCT &item) {
        if (item.CtlType != ODT_COMBOBOX)
            return FALSE;
        const int saved = SaveDC(item.hDC);
        const bool highlight = (item.itemState & ODS_SELECTED) && !(item.itemState & ODS_COMBOBOXEDIT);
        FillRect(item.hDC, &item.rcItem, GetSysColorBrush(highlight ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        SetTextColor(item.hDC, GetSysColor((item.itemState & ODS_DISABLED) ? COLOR_GRAYTEXT
                                           : highlight                     ? COLOR_HIGHLIGHTTEXT
                                                                           : COLOR_WINDOWTEXT));
        SetBkMode(item.hDC, TRANSPARENT);
        SelectObject(item.hDC, reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)));
        if (item.itemID != static_cast<UINT>(-1)) {
            const auto length = SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, item.itemID, 0);
            if (length >= 0 && length < 4096) {
                std::wstring text(static_cast<size_t>(length) + 1, L'\0');
                SendMessageW(item.hwndItem, CB_GETLBTEXT, item.itemID, reinterpret_cast<LPARAM>(text.data()));
                RECT area = item.rcItem;
                area.left += 3;
                DrawTextW(item.hDC, text.c_str(), static_cast<int>(length), &area,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
        }
        if (item.itemState & ODS_FOCUS)
            DrawFocusRect(item.hDC, &item.rcItem);
        RestoreDC(item.hDC, saved);
        return TRUE;
    }
    static LRESULT CALLBACK combo_print(HWND control, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                        DWORD_PTR) {
        const auto result = DefSubclassProc(control, message, wparam, lparam);
        if (message == WM_PRINT || message == WM_PRINTCLIENT) {
            COMBOBOXINFO info{sizeof(info)};
            const auto index = SendMessageW(control, CB_GETCURSEL, 0, 0);
            if (index >= 0 && GetComboBoxInfo(control, &info)) {
                DRAWITEMSTRUCT item{};
                item.CtlID = GetDlgCtrlID(control);
                item.CtlType = ODT_COMBOBOX;
                item.itemID = static_cast<UINT>(index);
                item.itemState = ODS_COMBOBOXEDIT | (IsWindowEnabled(control) ? 0 : ODS_DISABLED);
                item.hwndItem = control;
                item.hDC = reinterpret_cast<HDC>(wparam);
                item.rcItem = info.rcItem;
                draw_combo(item);
            }
        }
        return result;
    }
    static LRESULT CALLBACK parent_proc(HWND parent, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                        DWORD_PTR data) {
        const auto self = reinterpret_cast<Dialog *>(data);
        if (message == WM_DRAWITEM && reinterpret_cast<DRAWITEMSTRUCT *>(lparam)->CtlID == cmb1)
            return draw_combo(*reinterpret_cast<DRAWITEMSTRUCT *>(lparam));
        const auto result = DefSubclassProc(parent, message, wparam, lparam);
        if (message == WM_GETMINMAXINFO && self->minimum_width)
            reinterpret_cast<MINMAXINFO *>(lparam)->ptMinTrackSize.x =
                std::max<LONG>(reinterpret_cast<MINMAXINFO *>(lparam)->ptMinTrackSize.x, self->minimum_width);
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(parent, parent_proc, 1);
        return result;
    }
    void fit_width() {
        RECT bounds{}, client{}, needed{};
        GetWindowRect(parent, &bounds);
        GetClientRect(parent, &client);
        GetWindowRect(widget(id::Error), &needed);
        minimum_width = needed.right - bounds.left + (bounds.right - bounds.left - client.right) + 8;
        if (bounds.right - bounds.left < minimum_width)
            SetWindowPos(parent, nullptr, 0, 0, minimum_width, bounds.bottom - bounds.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (!SetWindowSubclass(parent, parent_proc, 1, reinterpret_cast<DWORD_PTR>(this)))
            throw std::runtime_error("Cannot preserve save dialog minimum width");
        if (const auto type = GetDlgItem(parent, cmb1)) {
            SetWindowSubclass(type, combo_print, 1, 0);
            InvalidateRect(type, nullptr, TRUE);
        }
    }
    static LRESULT CALLBACK panel_proc(HWND panel, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                       DWORD_PTR data) {
        if (message == WM_COMMAND || message == WM_NOTIFY || message == WM_DRAWITEM ||
            message == WM_MEASUREITEM)
            return SendMessageW(reinterpret_cast<HWND>(data), message, wparam, lparam);
        return DefSubclassProc(panel, message, wparam, lparam);
    }
    HWND add(HWND host, DWORD control, const wchar_t *type, const wchar_t *text, DWORD style, int x, int y,
             int width, int height) {
        RECT r{x, y, x + width, y + height};
        MapDialogRect(window, &r);
        const DWORD extra = _wcsicmp(type, L"Edit") == 0 ? WS_EX_CLIENTEDGE : 0;
        if (_wcsicmp(type, WC_COMBOBOXW) == 0)
            style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
        const auto value =
            CreateWindowExW(extra, type, text, WS_CHILD | WS_VISIBLE | style, r.left, r.top, r.right - r.left,
                            r.bottom - r.top, host, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(control)),
                            GetModuleHandleW(nullptr), nullptr);
        if (!value)
            throw std::runtime_error("Cannot create image save control");
        SendMessageW(value, WM_SETFONT, SendMessageW(window, WM_GETFONT, 0, 0), TRUE);
        if (_wcsicmp(type, WC_COMBOBOXW) == 0)
            SetWindowSubclass(value, combo_print, 1, 0);
        if (control)
            widgets[control] = value;
        return value;
    }
    void label(HWND host, const wchar_t *text, int y) {
        add(host, 0, L"Static", text, SS_LEFT | SS_NOPREFIX, 10, y + 2, 77, 12);
    }
    void checkbox(HWND host, DWORD control, const wchar_t *text, bool checked, int y, int width = 222) {
        const auto value =
            add(host, control, L"Button", text, BS_AUTOCHECKBOX | WS_TABSTOP, 10, y, width, 13);
        SendMessageW(value, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    void edit(HWND host, DWORD control, const wchar_t *text, const std::wstring &value, int y) {
        label(host, text, y);
        const auto input =
            add(host, control, L"Edit", value.c_str(), ES_AUTOHSCROLL | WS_TABSTOP, 90, y, 139, 14);
        SendMessageW(input, EM_SETLIMITTEXT, 32, 0);
    }
    void combo(HWND host, DWORD control, const wchar_t *text, const std::vector<std::wstring> &choices,
               unsigned index, int y) {
        label(host, text, y);
        const auto input =
            add(host, control, L"ComboBox", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 90, y, 139, 160);
        for (const auto &choice : choices)
            if (SendMessageW(input, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.c_str())) < 0)
                throw std::runtime_error("Cannot add image save choice");
        if (SendMessageW(input, CB_SETCURSEL, index, 0) < 0)
            throw std::runtime_error("Invalid initial image save choice");
    }
    HWND pane(DWORD control, const wchar_t *title, bool shared = false) {
        const auto panel = add(window, control, L"Static", L"", WS_CLIPCHILDREN | SS_LEFT, shared ? 4 : 177,
                               3, shared ? 168 : 243, 124);
        SetWindowLongPtrW(panel, GWL_EXSTYLE, GetWindowLongPtrW(panel, GWL_EXSTYLE) | WS_EX_CONTROLPARENT);
        if (!SetWindowSubclass(panel, panel_proc, 1, reinterpret_cast<DWORD_PTR>(window)))
            throw std::runtime_error("Cannot route save pane input");
        add(panel, 0, L"Button", title, BS_GROUPBOX, 0, 0, shared ? 168 : 243, 124);
        return panel;
    }
    HWND widget(DWORD control) const {
        return widgets.at(control);
    }
    bool checked(DWORD control) const {
        return SendMessageW(widget(control), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    unsigned selected(DWORD control) const {
        const auto value = SendMessageW(widget(control), CB_GETCURSEL, 0, 0);
        if (value < 0)
            throw std::runtime_error("No image save choice selected");
        return static_cast<unsigned>(value);
    }
    std::wstring text(DWORD control) const {
        const auto input = widget(control);
        std::wstring result(static_cast<size_t>(GetWindowTextLengthW(input)) + 1, L'\0');
        GetWindowTextW(input, result.data(), static_cast<int>(result.size()));
        result.pop_back();
        return result;
    }
    unsigned number(DWORD control, unsigned maximum, const wchar_t *label, bool color = false) const {
        auto value = text(control);
        const auto begin = value.find_first_not_of(L" \t\r\n"), end = value.find_last_not_of(L" \t\r\n");
        value = begin == std::wstring::npos ? L"" : value.substr(begin, end - begin + 1);
        if (color && value.starts_with(L"#"))
            value.erase(0, 1);
        const auto invalid = [&] {
            throw Validation{std::wstring(label) +
                             (color ? L"は6桁の16進数で入力してください。"
                                    : L"は0～" + std::to_wstring(maximum) + L"で入力してください。")};
        };
        if (value.empty() ||
            value.find_first_not_of(color ? L"0123456789abcdefABCDEF" : L"0123456789") !=
                std::wstring::npos ||
            (color && value.size() != 6))
            invalid();
        unsigned long result{};
        try {
            result = std::stoul(value, nullptr, color ? 16 : 10);
        } catch (...) {
            invalid();
        }
        if (result > maximum)
            invalid();
        return static_cast<unsigned>(result);
    }
    unsigned optional_number(DWORD control, unsigned previous, unsigned maximum, const wchar_t *label,
                             bool color, bool required) const {
        try {
            return number(control, maximum, label, color);
        } catch (const Validation &) {
            if (required)
                throw;
            return previous;
        }
    }
    void refresh() {
        if (file.nFilterIndex < 1 || file.nFilterIndex > 4)
            throw std::runtime_error("Invalid image save format");
        current = static_cast<SaveFormat>(file.nFilterIndex - 1);
        for (unsigned i = 0; i < panes.size(); ++i)
            ShowWindow(panes[i], i == file.nFilterIndex - 1 ? SW_SHOW : SW_HIDE);
        EnableWindow(widget(id::PngColor),
                     selected(id::PngTransparency) == static_cast<unsigned>(PngTransparency::Color));
        EnableWindow(widget(id::PngMatte),
                     selected(id::PngTransparency) != static_cast<unsigned>(PngTransparency::Alpha));
        EnableWindow(widget(id::GifIndex),
                     selected(id::GifTransparency) == static_cast<unsigned>(GifTransparency::Index));
        EnableWindow(widget(id::BmpRle),
                     selected(id::BmpBits) == 1 || selected(id::BmpBits) == 2 || selected(id::BmpBits) == 4);
        EnableWindow(widget(id::Dither), current == SaveFormat::Gif ||
                                             (current == SaveFormat::Bmp && selected(id::BmpBits) != 0 &&
                                              selected(id::BmpBits) != 5));
        static constexpr const wchar_t *lower[]{L"png", L"jpg", L"bmp", L"gif"};
        static constexpr const wchar_t *upper[]{L"PNG", L"JPG", L"BMP", L"GIF"};
        extension = (checked(id::Uppercase) ? upper : lower)[static_cast<unsigned>(current)];
        file.lpstrDefExt = extension.c_str();
        if (parent)
            SendMessageW(parent, CDM_SETDEFEXT, 0, reinterpret_cast<LPARAM>(extension.c_str()));
    }
    SaveOptions values() const {
        auto result = initial;
        result.keep_times = checked(id::KeepTimes);
        result.uppercase_extension = checked(id::Uppercase);
        result.dither = checked(id::Dither);
        result.last_format = current;
        const auto read_pane = [&](SaveFormat format, const auto &read) {
            try {
                read();
            } catch (const Validation &) {
                if (current == format)
                    throw;
            }
        };
        read_pane(SaveFormat::Png, [&] {
            auto value = initial.png;
            value.interlaced = checked(id::PngInterlaced);
            value.compression = selected(id::PngCompression);
            value.filter = static_cast<PngFilter>(selected(id::PngFilter));
            value.transparency = static_cast<PngTransparency>(selected(id::PngTransparency));
            value.transparent_color =
                optional_number(id::PngColor, value.transparent_color, 0xffffff, L"透過色", true,
                                value.transparency == PngTransparency::Color);
            value.matte = optional_number(id::PngMatte, value.matte, 0xffffff, L"背景色", true,
                                          value.transparency != PngTransparency::Alpha);
            result.png = value;
        });
        read_pane(SaveFormat::Jpeg, [&] {
            auto value = initial.jpeg;
            value.progressive = checked(id::JpegProgressive);
            value.quality = number(id::JpegQuality, 100, L"JPEG画質");
            value.matte = number(id::JpegMatte, 0xffffff, L"背景色", true);
            result.jpeg = value;
        });
        read_pane(SaveFormat::Bmp, [&] {
            auto value = initial.bmp;
            value.top_down = checked(id::BmpTopDown);
            value.rle = checked(id::BmpRle);
            value.matte = number(id::BmpMatte, 0xffffff, L"背景色", true);
            static constexpr unsigned depths[]{24, 8, 4, 1, 0, 16};
            const auto index = selected(id::BmpBits);
            if (index >= std::size(depths))
                throw std::runtime_error("Invalid BMP color depth");
            result.bmp_bits = depths[index];
            result.bmp = value;
        });
        read_pane(SaveFormat::Gif, [&] {
            auto value = initial.gif;
            value.interlaced = checked(id::GifInterlaced);
            value.uncompressed = checked(id::GifUncompressed);
            value.transparency = static_cast<GifTransparency>(selected(id::GifTransparency));
            value.transparent_index =
                optional_number(id::GifIndex, value.transparent_index, 255, L"透過色番号", false,
                                value.transparency == GifTransparency::Index);
            value.matte = number(id::GifMatte, 0xffffff, L"背景色", true);
            result.gif = value;
        });
        return result;
    }
    void create() {
        common = pane(id::Common, L"共通", true);
        checkbox(common, id::KeepTimes, L"元ファイルの日時で保存", initial.keep_times, 15, 150);
        checkbox(common, id::Uppercase, L"拡張子は大文字で付ける", initial.uppercase_extension, 34, 150);
        checkbox(common, id::Dither, L"減色時にディザを使う", initial.dither, 53, 150);
        panes[0] = pane(id::Png, L"PNG 保存設定");
        checkbox(panes[0], id::PngInterlaced, L"インターレース", initial.png.interlaced, 14);
        std::vector<std::wstring> levels;
        for (unsigned i = 0; i <= 9; ++i)
            levels.push_back(std::to_wstring(i));
        combo(panes[0], id::PngCompression, L"圧縮レベル", levels, initial.png.compression, 32);
        combo(panes[0], id::PngFilter, L"フィルタ",
              {L"圧縮率優先（自動）", L"なし", L"Sub", L"Up", L"Average", L"Paeth", L"速度優先"},
              static_cast<unsigned>(initial.png.filter), 49);
        combo(panes[0], id::PngTransparency, L"透過色",
              {L"アルファを維持", L"なし（背景合成）", L"RGBを指定", L"左上", L"右上", L"左下", L"右下"},
              static_cast<unsigned>(initial.png.transparency), 66);
        edit(panes[0], id::PngColor, L"透過色 RGB", hex_color(initial.png.transparent_color), 83);
        edit(panes[0], id::PngMatte, L"背景色 RGB", hex_color(initial.png.matte), 100);
        panes[1] = pane(id::Jpeg, L"JPEG 保存設定");
        checkbox(panes[1], id::JpegProgressive, L"プログレッシブ", initial.jpeg.progressive, 14);
        edit(panes[1], id::JpegQuality, L"画質 (0～100)", std::to_wstring(initial.jpeg.quality), 34);
        edit(panes[1], id::JpegMatte, L"背景色 RGB", hex_color(initial.jpeg.matte), 54);
        panes[2] = pane(id::Bmp, L"BMP 保存設定");
        checkbox(panes[2], id::BmpTopDown, L"トップダウン", initial.bmp.top_down, 14);
        checkbox(panes[2], id::BmpRle, L"4 か 8 BPP なら RLE 圧縮", initial.bmp.rle, 33);
        combo(panes[2], id::BmpBits, L"色深度",
              {L"24 BPP", L"8 BPP（256色）", L"4 BPP（16色）", L"1 BPP（2色）", L"元の色深度（自動）",
               L"16 BPP（RGB555）"},
              initial.bmp_bits == 8   ? 1
              : initial.bmp_bits == 4 ? 2
              : initial.bmp_bits == 1 ? 3
              : initial.bmp_bits == 0 ? 4
              : initial.bmp_bits == 16 ? 5
                                      : 0,
              54);
        edit(panes[2], id::BmpMatte, L"背景色 RGB", hex_color(initial.bmp.matte), 76);
        panes[3] = pane(id::Gif, L"GIF 保存設定（現在のコマ）");
        checkbox(panes[3], id::GifInterlaced, L"インターレース", initial.gif.interlaced, 14);
        checkbox(panes[3], id::GifUncompressed, L"圧縮しない", initial.gif.uncompressed, 33);
        combo(panes[3], id::GifTransparency, L"透過色",
              {L"なし", L"色番号を指定", L"左上", L"右上", L"左下", L"右下"},
              static_cast<unsigned>(initial.gif.transparency), 54);
        edit(panes[3], id::GifIndex, L"透過色番号", std::to_wstring(initial.gif.transparent_index), 75);
        edit(panes[3], id::GifMatte, L"背景色 RGB", hex_color(initial.gif.matte), 96);
        add(window, id::Error, L"Static", L"", SS_LEFT | SS_NOPREFIX, 7, 130, 410, 26);
        refresh();
    }
    std::wstring dialog_text(UINT message) const {
        std::array<wchar_t, 32768> value{};
        const auto count =
            SendMessageW(parent, message, value.size(), reinterpret_cast<LPARAM>(value.data()));
        if (count <= 0 || static_cast<size_t>(count) > value.size())
            throw std::runtime_error("Cannot read save dialog path");
        return value.data();
    }
    bool accept() {
        try {
            auto proposed = values();
            auto target = std::filesystem::path(dialog_text(CDM_GETFILEPATH));
            const auto typed = std::filesystem::path(dialog_text(CDM_GETSPEC));
            if (!target.has_extension())
                target = save_target(target, proposed);
            SaveFormat actual;
            try {
                actual = save_format(target);
            } catch (const std::invalid_argument &) {
                throw Validation{L"拡張子はPNG、JPEG、BMP、GIFを指定してください。"};
            }
            if (actual != current) {
                pending_format = actual;
                pending_path = target;
                if (!PostMessageW(window, apply_format, 0, 0))
                    throw std::runtime_error("Cannot defer image save type change");
                throw Validation{
                    L"拡張子に合わせて保存設定を切り替えました。設定を確認して保存してください。"};
            }
            if (proposed.uppercase_extension && !typed.has_extension()) {
                target.replace_extension();
                target = save_target(target, proposed);
            }
            proposed.last_format = actual;
            accepted = proposed;
            path = std::move(target);
            return true;
        } catch (const Validation &error) {
            SetWindowTextW(widget(id::Error), error.message.c_str());
            return false;
        }
    }
    static UINT_PTR CALLBACK hook(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        auto self = reinterpret_cast<Dialog *>(GetWindowLongPtrW(window, DWLP_USER));
        if (message == WM_INITDIALOG) {
            self = reinterpret_cast<Dialog *>(reinterpret_cast<OPENFILENAMEW *>(lparam)->lCustData);
            SetWindowLongPtrW(window, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
            self->window = window;
            self->parent = GetParent(window);
        }
        if (!self)
            return 0;
        try {
            if (message == apply_format) {
                self->apply_pending_format();
                return 1;
            }
            if (message == WM_DRAWITEM)
                return draw_combo(*reinterpret_cast<DRAWITEMSTRUCT *>(lparam));
            if (message == WM_MEASUREITEM) {
                const auto measure = reinterpret_cast<MEASUREITEMSTRUCT *>(lparam);
                if (measure->CtlType == ODT_COMBOBOX) {
                    measure->itemHeight = MulDiv(18, GetDpiForWindow(window), 96);
                    return 1;
                }
            }
            if (message == WM_INITDIALOG)
                self->create();
            else if (message == WM_NOTIFY) {
                const auto notice = reinterpret_cast<OFNOTIFYW *>(lparam);
                if (notice->hdr.code == CDN_INITDONE) {
                    ShowWindow(GetDlgItem(window, 30500), SW_HIDE);
                    self->fit_width();
                    const auto index = SendMessageW(GetDlgItem(self->parent, cmb1), CB_GETCURSEL, 0, 0);
                    if (index >= 0)
                        self->file.nFilterIndex = static_cast<DWORD>(index) + 1;
                    self->refresh();
                } else if (notice->hdr.code == CDN_TYPECHANGE) {
                    const auto index = SendMessageW(GetDlgItem(self->parent, cmb1), CB_GETCURSEL, 0, 0);
                    self->file.nFilterIndex =
                        index >= 0 ? static_cast<DWORD>(index) + 1 : notice->lpOFN->nFilterIndex;
                    self->refresh();
                } else if (notice->hdr.code == CDN_FILEOK && !self->accept()) {
                    SetWindowLongPtrW(window, DWLP_MSGRESULT, 1);
                    return 1;
                }
            } else if (message == WM_COMMAND &&
                       (HIWORD(wparam) == CBN_SELCHANGE || HIWORD(wparam) == BN_CLICKED))
                self->refresh();
        } catch (const std::exception &error) {
            self->failure = error.what();
            PostMessageW(self->parent, WM_COMMAND, IDCANCEL, 0);
        }
        return 0;
    }

  public:
    std::string failure;
    SaveOptions accepted;
    std::filesystem::path path;
    explicit Dialog(const SaveOptions &value) : initial(value), current(value.last_format), accepted(value) {}
    bool show(HWND owner, const std::filesystem::path &folder, const std::wstring &suggested) {
        if (suggested.size() >= filename.size())
            throw std::invalid_argument("Suggested filename too long");
        std::copy(suggested.begin(), suggested.end(), filename.begin());
        if (std::filesystem::path(suggested).has_extension())
            current = save_format(suggested);
        if (static_cast<unsigned>(current) > 3)
            throw std::invalid_argument("Invalid initial save format");
        DLGTEMPLATE header{};
        header.style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | DS_CONTROL | DS_SETFONT;
        header.dwExtendedStyle = WS_EX_CONTROLPARENT;
        header.cx = 425;
        header.cy = 158;
        header.cdit = 1;
        std::vector<WORD> resource((sizeof(header) + 1) / 2);
        std::memcpy(resource.data(), &header, sizeof(header));
        resource.insert(resource.end(), {0, 0, 0, 9});
        for (auto c : std::wstring(L"MS Shell Dlg 2"))
            resource.push_back(c);
        resource.push_back(0);
        // The Explorer dialog measures template controls before laying out the extension.
        // Reserve its complete area, then hide this placeholder after initialization.
        if (resource.size() % 2)
            resource.push_back(0);
        DLGITEMTEMPLATE placeholder{};
        placeholder.style = WS_CHILD | WS_VISIBLE | SS_LEFT;
        placeholder.cx = header.cx;
        placeholder.cy = header.cy;
        placeholder.id = 30500;
        const auto offset = resource.size();
        resource.resize(offset + (sizeof(placeholder) + 1) / 2);
        std::memcpy(resource.data() + offset, &placeholder, sizeof(placeholder));
        resource.insert(resource.end(), {0xffff, 0x0082, 0, 0}); // STATIC, empty title, no creation data.
        const auto memory = GlobalAlloc(GMEM_FIXED, resource.size() * sizeof(WORD));
        if (!memory)
            throw std::bad_alloc();
        const std::unique_ptr<std::remove_pointer_t<HGLOBAL>, decltype(&GlobalFree)> release(memory,
                                                                                             GlobalFree);
        const auto data = GlobalLock(memory);
        if (!data)
            throw std::runtime_error("Cannot access save dialog template");
        std::memcpy(data, resource.data(), resource.size() * sizeof(WORD));
        GlobalUnlock(memory);
        const auto initial_folder = folder.wstring();
        static constexpr const wchar_t filter[] =
            L"PNG（透明度維持）\0*.png\0JPEG\0*.jpg;*.jpeg\0BMP\0*.bmp\0GIF（静止画）\0*.gif\0\0";
        static constexpr const wchar_t *extensions[]{L"png", L"jpg", L"bmp", L"gif"};
        extension = extensions[static_cast<unsigned>(current)];
        file = {sizeof(file)};
        file.hwndOwner = owner;
        file.hInstance = reinterpret_cast<HINSTANCE>(memory);
        file.lpstrFilter = filter;
        file.nFilterIndex = static_cast<DWORD>(current) + 1;
        file.lpstrFile = filename.data();
        file.nMaxFile = static_cast<DWORD>(filename.size());
        file.lpstrInitialDir = initial_folder.empty() ? nullptr : initial_folder.c_str();
        file.lpstrTitle = L"画像を保存";
        file.lpstrDefExt = extension.c_str();
        file.Flags = OFN_EXPLORER | OFN_ENABLEHOOK | OFN_ENABLETEMPLATEHANDLE | OFN_ENABLESIZING |
                     OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOREADONLYRETURN;
        file.lCustData = reinterpret_cast<LPARAM>(this);
        file.lpfnHook = hook;
        const bool result = GetSaveFileNameW(&file) != FALSE;
        if (!failure.empty())
            throw std::runtime_error(failure);
        const auto error = result ? 0u : CommDlgExtendedError();
        if (error)
            throw std::runtime_error("Image save dialog failed: " + std::to_string(error));
        if (result && path.empty())
            throw std::runtime_error("Image save dialog returned no validated file");
        return result;
    }
};
} // namespace
std::filesystem::path choose_save_file(HWND owner, const std::filesystem::path &folder,
                                       const std::wstring &suggested, SaveOptions &options) {
    Dialog dialog(options);
    if (!dialog.show(owner, folder, suggested))
        return {};
    options = dialog.accepted;
    return dialog.path;
}
} // namespace gvn
