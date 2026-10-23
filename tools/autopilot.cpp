// GVN-owned native UI autopilot. No global input injection or other application handles.
#include "../src/viewer.cpp"
#include "../tests/test_desktop.h"
#include "../tests/rotation_formats_fixture.h"
#include <dlgs.h>
#include <ColorDlg.h>
#include <map>
#include <cstring>
#include <iostream>
#include <deque>

namespace gvn {
namespace {
std::string utf8(const std::wstring &s) {
    const int n =
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n, nullptr, nullptr);
    return result;
}
std::string quote(const std::string &s) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\')
            out << '\\' << c;
        else if (c == '\n')
            out << "\\n";
        else if (c == '\r')
            out << "\\r";
        else if (c == '\t')
            out << "\\t";
        else if (c < 32)
            out << '?';
        else
            out << c;
    }
    out << '"';
    return out.str();
}
std::vector<std::string> split(const std::string &line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        auto end = line.find('\t', start);
        out.push_back(line.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
            return out;
        start = end + 1;
    }
}
std::wstring window_text(HWND h) {
    std::wstring s(static_cast<size_t>(GetWindowTextLengthW(h)) + 1, L'\0');
    GetWindowTextW(h, s.data(), static_cast<int>(s.size()));
    s.pop_back();
    return s;
}
void require(bool ok, const char *reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
void own(HWND h) {
    DWORD pid{};
    GetWindowThreadProcessId(h, &pid);
    require(IsWindow(h) && pid == GetCurrentProcessId(), "Autopilot only accepts its own process's HWNDs");
}
bool locally_visible(HWND h) {
    // Noninteractive window stations have an invisible desktop ancestor. Check
    // the app's actual WS_VISIBLE flags up to its top-level window instead.
    own(h);
    for (;;) {
        if (!(GetWindowLongPtrW(h, GWL_STYLE) & WS_VISIBLE))
            return false;
        if (!(GetWindowLongPtrW(h, GWL_STYLE) & WS_CHILD))
            return true;
        h = GetParent(h);
        own(h);
    }
}
bool fully_visible(HWND h) {
    if (!locally_visible(h))
        return false;
    RECT bounds{};
    GetWindowRect(h, &bounds);
    for (auto current = h; GetWindowLongPtrW(current, GWL_STYLE) & WS_CHILD;) {
        current = GetParent(current);
        RECT area{};
        GetClientRect(current, &area);
        MapWindowPoints(current, nullptr, reinterpret_cast<POINT *>(&area), 2);
        if (bounds.left < area.left || bounds.top < area.top || bounds.right > area.right ||
            bounds.bottom > area.bottom)
            return false;
    }
    return true;
}
RECT relative_rect(HWND h, HWND parent) {
    RECT r{};
    GetWindowRect(h, &r);
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT *>(&r), 2);
    return r;
}
bool overlaps(RECT a, RECT b) {
    RECT out{};
    return IntersectRect(&out, &a, &b) != FALSE;
}
void native_click(HWND h, int x, int y) {
    own(h);
    SetFocus(h);
    SendMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
    SendMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
}
void render_client(HWND hwnd, const Rect *empty_canvas = nullptr, int cell = 32,
                   uint32_t first = 0x202020, uint32_t second = 0x303030,
                   bool tiled = true, const Rect *image_bounds = nullptr, const Rect *overlay_bounds = nullptr) {
    own(hwnd);
    RECT client{};
    GetClientRect(hwnd, &client);
    require(client.right > 0 && client.bottom > 0, "Empty client area");
    HDC dc = CreateCompatibleDC(nullptr);
    require(dc != nullptr, "Render DC creation failed");
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), client.right, -client.bottom, 1, 32, BI_RGB};
    void *pixels{};
    const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) {
        DeleteDC(dc);
        throw std::runtime_error("Render bitmap creation failed");
    }
    const size_t count = static_cast<size_t>(client.right) * client.bottom;
    std::fill_n(static_cast<uint32_t *>(pixels), count, 0x12345678u);
    const auto previous = SelectObject(dc, bitmap);
    SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    GdiFlush();
    const bool painted = std::any_of(static_cast<uint32_t *>(pixels), static_cast<uint32_t *>(pixels) + count,
                                     [](uint32_t p) { return p != 0x12345678u; });
    bool empty_matches = true;
    size_t compared_background{};
    if (empty_canvas) {
        const auto &r = *empty_canvas;
        require(!r.empty() && r.x >= 0 && r.y >= 0 && r.x + r.w <= client.right && r.y + r.h <= client.bottom,
                "Empty canvas is outside client area");
        const auto actual = static_cast<const uint32_t *>(pixels);
        for (int y = 0; y < r.h; ++y)
            for (int x = 0; x < r.w; ++x) {
                if (image_bounds && x >= image_bounds->x && y >= image_bounds->y &&
                    x < image_bounds->x + image_bounds->w && y < image_bounds->y + image_bounds->h)
                    continue;
                if (overlay_bounds && x + r.x >= overlay_bounds->x && y + r.y >= overlay_bounds->y &&
                    x + r.x < overlay_bounds->x + overlay_bounds->w &&
                    y + r.y < overlay_bounds->y + overlay_bounds->h)
                    continue;
                const auto expected = tiled && ((x / cell + y / cell) % 2) ? second : first;
                empty_matches &= (actual[(r.y + y) * client.right + r.x + x] & 0xffffff) == expected;
                ++compared_background;
            }
    }
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    require(painted, "Native WM_PRINTCLIENT did not paint");
    require(!empty_canvas || compared_background > 0, "No visible background pixels were compared");
    require(empty_matches, "Empty viewport differs from the dark checkerboard (including text or gaps)");
}
void print_native_tree(HWND hwnd, HDC dc, const RECT &root) {
    own(hwnd);
    if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE))
        return;
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    const auto saved = SaveDC(dc);
    SetViewportOrgEx(dc, rect.left - root.left, rect.top - root.top, nullptr);
    IntersectClipRect(dc, 0, 0, rect.right - rect.left, rect.bottom - rect.top);
    if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CLIPSIBLINGS) {
        for (auto sibling = GetWindow(hwnd, GW_HWNDPREV); sibling;
             sibling = GetWindow(sibling, GW_HWNDPREV)) {
            if (!(GetWindowLongPtrW(sibling, GWL_STYLE) & WS_VISIBLE))
                continue;
            RECT area{};
            GetWindowRect(sibling, &area);
            ExcludeClipRect(dc, area.left - rect.left, area.top - rect.top, area.right - rect.left,
                            area.bottom - rect.top);
        }
    }
    // Recurse only through visible controls. Some combo boxes include hidden
    // list windows which WM_PRINT with PRF_CHILDREN can paint over the selection.
    SendMessageW(hwnd, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_NONCLIENT | PRF_CLIENT | PRF_ERASEBKGND);
    wchar_t cls[128]{};
    GetClassNameW(hwnd, cls, 128);
    const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const bool owner_combo =
        _wcsicmp(cls, WC_COMBOBOXW) == 0 && (style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE));
    const bool owner_static = _wcsicmp(cls, L"STATIC") == 0 && (style & SS_TYPEMASK) == SS_OWNERDRAW;
    if (owner_combo || owner_static) {
        const auto owner_saved = SaveDC(dc);
        POINT client_origin{};
        ClientToScreen(hwnd, &client_origin);
        SetViewportOrgEx(dc, client_origin.x - root.left, client_origin.y - root.top, nullptr);
        DRAWITEMSTRUCT item{};
        item.CtlID = GetDlgCtrlID(hwnd);
        item.CtlType = owner_combo ? ODT_COMBOBOX : ODT_STATIC;
        item.hwndItem = hwnd;
        item.hDC = dc;
        item.itemAction = ODA_DRAWENTIRE;
        item.itemState = IsWindowEnabled(hwnd) ? 0 : ODS_DISABLED;
        if (owner_combo) {
            COMBOBOXINFO info{sizeof(info)};
            GetComboBoxInfo(hwnd, &info);
            item.rcItem = info.rcItem;
            item.itemID = static_cast<UINT>(SendMessageW(hwnd, CB_GETCURSEL, 0, 0));
            item.itemData = SendMessageW(hwnd, CB_GETITEMDATA, item.itemID, 0);
            item.itemState |= ODS_COMBOBOXEDIT;
        } else
            GetClientRect(hwnd, &item.rcItem);
        SendMessageW(GetParent(hwnd), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
        RestoreDC(dc, owner_saved);
    }
    std::vector<HWND> children;
    for (auto child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        children.push_back(child);
    RECT client{};
    POINT origin{};
    GetClientRect(hwnd, &client);
    ClientToScreen(hwnd, &origin);
    SetViewportOrgEx(dc, origin.x - root.left, origin.y - root.top, nullptr);
    IntersectClipRect(dc, client.left, client.top, client.right, client.bottom);
    for (auto child : children)
        print_native_tree(child, dc, root);
    RestoreDC(dc, saved);
}
void capture(HWND hwnd, const std::filesystem::path &path) {
    own(hwnd);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    RECT rc{};
    GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    require(w > 0 && h > 0, "Invalid capture dimensions");
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void *pixels{};
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require(bitmap && pixels, "Cannot allocate window capture");
    auto old = SelectObject(dc, bitmap);
    std::memset(pixels, 0x5a, static_cast<size_t>(w) * h * 4);
    PrintWindow(hwnd, dc, 2);
    GdiFlush();
    const auto untouched = [&] {
        const auto data = static_cast<const uint8_t *>(pixels);
        return std::all_of(data, data + static_cast<size_t>(w) * h * 4,
                           [](uint8_t value) { return value == 0x5a; });
    };
    // PrintWindow can return a partially painted surface on a private station.
    // Always repaint the actual window/control tree, including visible native bars.
    print_native_tree(hwnd, dc, rc);
    GdiFlush();
    const bool rendered = !untouched();
    Image image;
    image.width = w;
    image.height = h;
    image.frames.push_back({std::vector<Pixel>(checked_pixels(w, h)), 100});
    auto input = static_cast<const uint8_t *>(pixels);
    for (size_t i = 0; i < image.frames[0].pixels.size(); ++i)
        image.frames[0].pixels[i] = {input[4 * i + 2], input[4 * i + 1], input[4 * i], 255};
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    require(rendered, "The native window did not render into its capture DC");
    save_image(image, 0, path);
    const auto verify = [&](auto &&self, HWND control) -> void {
        if (!locally_visible(control))
            return;
        wchar_t cls[80]{};
        GetClassNameW(control, cls, 80);
        if(window_text(hwnd)==L"画像情報"&&_wcsicmp(cls,L"EDIT")==0&&GetDlgCtrlID(control)==image_info_control::Field&&!window_text(control).empty()) {
            require(fully_visible(control),"Image Info filename field is clipped");
            RECT field{};GetWindowRect(control,&field);field.left+=3;field.right-=3;field.top+=3;field.bottom-=3;
            std::map<uint32_t,size_t> colors;
            for(int y=field.top-rc.top;y<field.bottom-rc.top;++y)for(int x=field.left-rc.left;x<field.right-rc.left;++x){const auto p=image.frames[0].pixels[size_t(y)*image.width+x];++colors[(uint32_t(p.r)<<16)|(uint32_t(p.g)<<8)|p.b];}
            require(!colors.empty(),"Filename capture rectangle is empty");const auto background=std::max_element(colors.begin(),colors.end(),[](const auto &a,const auto &b){return a.second<b.second;})->first;size_t ink{};
            for(const auto &[rgb,count]:colors)if(std::abs(int(rgb>>16&255)-int(background>>16&255))+std::abs(int(rgb>>8&255)-int(background>>8&255))+std::abs(int(rgb&255)-int(background&255))>=90)ink+=count;
            require(ink>=2,"Image Info filename text is present but not painted");
        }
        if (_wcsicmp(cls, WC_COMBOBOXW) == 0 && fully_visible(control) &&
            SendMessageW(control, CB_GETCURSEL, 0, 0) >= 0) {
            COMBOBOXINFO info{sizeof(info)};
            require(GetComboBoxInfo(control, &info) != FALSE, "Cannot inspect captured combo");
            POINT a{info.rcItem.left + 2, info.rcItem.top + 2},
                b{info.rcItem.right - 2, info.rcItem.bottom - 2};
            ClientToScreen(control, &a);
            ClientToScreen(control, &b);
            a.x -= rc.left;
            b.x -= rc.left;
            a.y -= rc.top;
            b.y -= rc.top;
            std::map<uint32_t, size_t> colors;
            for (int y = std::max(0L, a.y); y < std::min(static_cast<LONG>(image.height), b.y); ++y)
                for (int x = std::max(0L, a.x); x < std::min(static_cast<LONG>(image.width), b.x); ++x) {
                    const auto p = image.frames[0].pixels[static_cast<size_t>(y) * image.width + x];
                    ++colors[(uint32_t(p.r) << 16) | (uint32_t(p.g) << 8) | p.b];
                }
            require(!colors.empty(), "Captured combo text rectangle is empty");
            const auto background = std::max_element(colors.begin(), colors.end(), [](const auto &left, const auto &right) {
                return left.second < right.second;
            })->first;
            int ink{};
            for (int y = std::max(0L, a.y); y < std::min(static_cast<LONG>(image.height), b.y); ++y)
                for (int x = std::max(0L, a.x); x < std::min(static_cast<LONG>(image.width), b.x); ++x) {
                    const auto p = image.frames[0].pixels[static_cast<size_t>(y) * image.width + x];
                    // Thin ClearType glyphs can have no pixels near the solid foreground color.
                    // Require actual strokes against the observed background, including disabled text.
                    if (std::abs(int(p.r) - int(background >> 16 & 255)) +
                        std::abs(int(p.g) - int(background >> 8 & 255)) +
                        std::abs(int(p.b) - int(background & 255)) >= 90) ++ink;
                }
            if (ink < 2)
                throw std::runtime_error(
                    "Native combo text missing from capture: id=" + std::to_string(GetDlgCtrlID(control)) +
                    " text=" + utf8(window_text(control)) + " rect=" + std::to_string(a.x) + "," +
                    std::to_string(a.y) + "," + std::to_string(b.x) + "," + std::to_string(b.y));
        }
        if (_wcsicmp(cls, TOOLBARCLASSNAMEW) == 0 && fully_visible(control)) {

            const int inset = MulDiv(3, GetDpiForWindow(control), 96);
            const auto count = SendMessageW(control, TB_BUTTONCOUNT, 0, 0);
            for (int index = 0; index < count; ++index) {
                TBBUTTON button{};
                RECT bounds{};
                SendMessageW(control, TB_GETBUTTON, index, reinterpret_cast<LPARAM>(&button));
                if ((button.fsStyle & BTNS_SEP) || (button.fsState & TBSTATE_HIDDEN))
                    continue;
                SendMessageW(control, TB_GETITEMRECT, index, reinterpret_cast<LPARAM>(&bounds));
                MapWindowPoints(control, nullptr, reinterpret_cast<POINT *>(&bounds), 2);
                OffsetRect(&bounds, -rc.left, -rc.top);
                std::map<uint32_t,size_t> colors;
                for(int y=std::max(0L,bounds.top+inset);y<std::min(LONG(image.height),bounds.bottom-inset);++y)for(int x=std::max(0L,bounds.left+inset);x<std::min(LONG(image.width),bounds.right-inset);++x){const auto p=image.frames[0].pixels[size_t(y)*image.width+x];++colors[(uint32_t(p.r)<<16)|(uint32_t(p.g)<<8)|p.b];}
                require(!colors.empty(),"Captured toolbar button rectangle is empty");const auto face=std::max_element(colors.begin(),colors.end(),[](const auto &a,const auto &b){return a.second<b.second;})->first;
                size_t background{},ink{};for(const auto &[color,count]:colors){const auto distance=std::abs(int((color>>16)&255)-int((face>>16)&255))+std::abs(int((color>>8)&255)-int((face>>8)&255))+std::abs(int(color&255)-int(face&255));if(distance<75)background+=count;else ink+=count;}
                if (background < 8 || ink < 2)
                    throw std::runtime_error("Native toolbar icon/background missing from capture: command=" +
                                             std::to_string(button.idCommand));
            }
        }
        for (auto child = GetWindow(control, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            self(self, child);
    };
    verify(verify, hwnd);
}
const std::map<std::string, UINT> command_ids{
#define C(x) {#x, x}
    C(Open),
    C(Reload),
    C(Save),
    C(SaveAs),
    C(FileDelete),
    C(FileDeleteDirect),
    C(About),
    C(FileCopy),
    C(FileMove),
    C(RenameFile),
    C(Resize),
    C(Canvas),
    C(NewImage),
    C(ToggleEditor),C(ToolPan),C(ToolPen),C(ToolText),C(ToolCrop),C(SelectRectangle),C(SelectFreehand),C(SelectPolygon),C(SelectMove),
    C(PasteSelection),C(CommitSelection),C(CancelSelection),C(TransformSelection),C(InvertSelection),C(DeleteSelection),
    C(SelectMode),
    C(Preferences),
    C(Wallpaper),
    C(WallpaperCenter),
    C(WallpaperTile),
    C(WallpaperStretch),
    C(WallpaperWidth),
    C(WallpaperHeight),
    C(WallpaperDelete),
    C(Play),
    C(FramePrevious),
    C(FrameNext),
    C(FirstFrame),
    C(LastFrame),
    C(StopAnimation),
    C(Loop),
    C(Copy),
    C(Paste),
    C(NewPaste),
    C(ArmDrag),
    C(Fullscreen),
    C(Actual),
    C(FitImage),
    C(ZoomIn),
    C(ZoomOut),
    C(Next),
    C(Previous),
    C(SkipNext),
    C(SkipBack),
    C(First),
    C(Last),
    C(Jump),
    C(Slideshow),
    C(SelectAll),
    C(SelectNumbers),
    C(Crop),
    C(SelectMode),
    C(Grid),C(GridFree),C(Grid2),C(Grid4),C(Grid8),C(Grid16),C(Grid32),
    C(ClearSelection),
    C(Undo),
    C(Redo),
    C(Negative),
    C(Gray),
    C(RotateRight),
    C(RotateLeft),
    C(CloseImage),
    C(ToggleToolbar),
    C(ToggleStatus),
    C(ToggleMenu),
    C(Comic),
    C(Print),
    C(FlipH),
    C(FlipV),
    C(Checker),
    C(Nearest),
    C(Exit),
    C(CopyDestBase),
    C(ExternalBase),
    C(ScrollTopLeft),
    C(ScrollBottomRight),
    C(ScrollCenter),
    C(ScrollUp),
    C(ScrollDown),
    C(ScrollLeft),
    C(ScrollRight),
    C(PopupAll),
    C(PopupView),
    C(ZoomNumber),
    C(Last),
    C(First),
    C(WindowRestore),
    C(WindowMinimize),
    C(WindowMaximize),
    C(ToggleCaption),
    C(ToggleOuterFrame),
    C(ToggleInnerFrame),
    C(DisplayWindowImage),
    C(DisplayImageWindow),
    C(DisplayLargeWindow),
    C(DisplayImageDesktop),
    C(DisplayLargeDesktop),
    C(DisplayUnchanged),
    C(ToggleScrollbars),
    C(ParentFolder),
    C(PreviousFolder),
    C(NextFolder),
    C(PreviousAdjacentFolder),
    C(NextAdjacentFolder),
    C(ComicRTL),
    C(ComicHeight),
    C(ComicSingleWide),
    C(CopyFileName),
    C(CopyPath),
    C(CopyFolderName),
    C(CopyFolderPath),
    C(CopyShortPath),
    C(NewWindow),
    C(NewWindowFile),
    C(NewWindowSend),
    C(NewWindowPaste),
    C(GlassV), C(GlassH), C(Xor80), C(WinMask),
    C(MaskNegative), C(MaskSwap), C(Q0Mask), C(ConfigureMask),
    C(FLMask),
    C(MekoForward), C(MekoReverse),
    C(RainbowMask),
    C(CPMask),
    C(PaletteEdit),
    C(ConvertColorDepth),
    C(ColorAdjust),
    C(ViewRotate),
    C(ViewRotateNone),
    C(ViewRotateLeft),
    C(ViewRotateRight),
    C(ViewRotatePortraitLeft),
    C(ViewRotatePortraitRight),
    C(ViewRotateLandscapeLeft),
    C(ViewRotateLandscapeRight),
    C(ViewRotateHalf),
    C(SortName),
    C(SortTime),
    C(SortReverse),
    C(NaturalSort),
    C(ShowHidden)
#undef C
};
const std::map<std::string, int> preference_ids{
#define P(x) {#x, preference_id::x}
    P(AllowMultiple),
    P(ActivateReused),
    P(ActivateDropped),
    P(ReopenLast),
    P(CutTransparent),
    P(CutColor),
    P(DisableIme),
    P(AllowPhysicalOverflow),
    P(GifSpeed),
    P(ComicRotateBefore),
    P(SaveAsRetarget),
    {"CutColorPick", preference_id::CutColor + 5000},
    P(Undo),
    P(History),
    P(PasteNew),
    P(Grid),
    P(Checker),
    P(Background),
    P(Light),
    P(Dark),
    P(Cell),
    P(BackgroundTiled),
    P(BackgroundPattern),
    P(BackgroundFirst),
    P(BackgroundSecond),
    P(Preview),
    {"BackgroundFirstPick", preference_id::BackgroundFirst + 5000},
    {"BackgroundSecondPick", preference_id::BackgroundSecond + 5000},
    P(Topmost),
    P(TitlePath),
    P(InitialFit),
    P(Nearest),
    P(ComicRTL),
    P(ComicHeightMode),
    P(ComicFill),
    P(ComicDown),
    P(ComicUp),
    {"ComicFillPick", preference_id::ComicFill + 5000},
    P(KeepZoom),
    P(ZoomStep),
    P(HideCursor),
    P(FullNearest),
    P(CenterImage),
    P(FitMax),
    P(FitSimple),
    P(DisplayDown),
    P(DisplayUp),
    P(ZoomReduce),
    P(ZoomEnlarge),
    P(DragSimple),
    P(ZoomSimple),
    P(ZoomMode),
    P(ZoomBelow),
    P(ZoomAbove),
    P(FullDown),
    P(FullUp),
    P(HideTaskbar),
    P(HideWait),
    P(Tabs),P(Groups),P(Search),P(SearchResults),P(ResetField),P(Related),P(UiTheme),P(UiDensity),P(ToolbarOverflow),
    P(WindowCenter),
    P(WindowToolbarMin),
    P(StartupPosition),
    P(StartupState),
    P(WindowX),
    P(WindowY),
    P(WindowWidth),
    P(WindowHeight),
    P(RestoreMaximized),
    P(RestoreFullscreen),
    P(Caption),
    P(OuterFrame),
    P(InnerFrame),
    P(FullTopmost),
    P(FullToolbar),
    P(FullStatus),
    P(FullMenu),
    P(ToolbarCenter),
    P(ToolbarLine),
    P(ToolbarLarge),
    P(ToolbarAuto),
    P(StatusAuto),
    P(TitleMode),
    P(DisplayMode),
    P(FullDisplayMode),
    P(FullFit),
    P(Scrollbars),
    P(FullScrollbars),
    P(OverlayFull),
    P(OverlayWindow),
    P(OverlayAnimation),
    P(OverlayPosition),
    P(OverlayBackground),
    P(OverlayFont),
    P(OverlayForeground),
    P(OverlayBackcolor),
    {"OverlayForegroundPick", preference_id::OverlayForeground + 5000},
    {"OverlayBackcolorPick", preference_id::OverlayBackcolor + 5000},
    P(WindowCurrent),
    P(Toolbar),
    P(Status),
    P(Menu),
    P(Sort),
    P(Natural),
    P(Reverse),
    P(ListLoop),
    P(FolderHistory),
    P(PreviousFolderFirst),
    P(AdjacentPreviousFirst),
    P(ParentFirst),
    P(BoundarySound),
    P(ComicToolbarReverse),
    P(ComicJumpReverse),
    P(SkipCount),
    P(ConfirmOverwrite),
    P(ConfirmDelete),
    P(ConfirmUnsaved),
    P(SaveDialog),
    P(RememberSaveFolder),
    P(RenameResort),
    P(RecycleDeleted),
    P(NextAfterFileop),
    P(ConfirmMove),
    P(ConfirmCopy),
    P(Hidden),
    P(PreserveAlpha),
    P(BmpTransparency),
    P(GifTransparency),
    P(PngTransparency),
    P(GifBackground),
    P(PngBackground),
    P(GifAnimation),
    P(ExifOrientation),
    P(BitfieldTo24),
    P(TransparencyBackground),
    {"TransparencyBackgroundPick", preference_id::TransparencyBackground + 5000},
    P(SystemFiles),
    P(ProtectedFiles),
    P(PrefetchEnabled),
    P(CacheMiB),
    P(Autoplay),
    P(AnimationLoop),
    P(AnimationFileLoops),
    P(GlassSize),
    P(GlassRemainder), P(MaskSwapChoice),
    P(FLNegative),
    P(MaskNegativeRed), P(MaskNegativeGreen), P(MaskNegativeBlue),
    P(MaskXorRed), P(MaskXorGreen), P(MaskXorBlue),
    P(Q0Vertical), P(Q0Horizontal), P(Q0Negative), P(Q0Xor), P(Q0FlipVertical), P(Q0FlipHorizontal),
    P(Wheel),
    P(Middle),
    P(Interval),
    P(SlideRandom),
    P(SlideFullscreen),
    P(SlideLoop),
    P(SlideNextFolder),
    P(SlideSubtree),
    P(WheelScroll),
    P(DragScroll),
    P(DragZoom),
    P(DragAxis),
    P(DragThreshold),
    P(HoverMargin),
    P(HoverMin),
    P(IgnoreActivation),
    P(SlowWheel),
    P(MouseList),
    P(MouseGestureChoice),
    P(MouseHeld),
    P(MouseModifiers),
    P(MouseActionChoice),
    P(MouseAssign),
    P(MouseClear),
    P(MouseInherit),
    P(KeyList),
    P(KeyEdit),
    P(KeyAssign),
    P(KeySlots),
    P(KeyAdd),
    P(KeyRemove),
    P(KeyFirst),
    P(KeyCapture),
    P(DestinationList),
    P(DestinationEdit),
    P(ExternalList),
    P(ExternalEdit),
    P(ExternalBrowse),
    P(ExternalName),
    P(ExternalSend),
    P(ExternalShow),
    P(ExternalVerb),
    P(ExternalQuit),
    P(ExternalSeparator),
    P(ExternalArchiveMember),
    P(ExternalArchiveFolder),
    P(ExternalUp),
    P(ExternalDown),
    P(ExternalReset),
    P(Reset),
    P(ApplyButton),
#undef P
    {"OK", IDOK},
    {"Cancel", IDCANCEL}};
const std::map<std::string, int> page_ids{
#define T(x) {#x, static_cast<int>(PreferencePage::x)}
    T(General), T(Background),  T(Window),         T(Display), T(Zoom), T(Fullscreen),
    T(Bars),    T(Move),        T(FileOperations), T(Loading), T(Mask), T(Keys),
    T(Mouse),   T(MouseCustom), T(Slideshow),      T(External)
#undef T
};
const std::map<std::string, DWORD> save_ids{
#define C(x) {#x, save_control::x}
    C(Common),    C(KeepTimes),     C(Uppercase),      C(Dither),          C(Error),
    C(Png),       C(PngInterlaced), C(PngCompression), C(PngFilter),       C(PngTransparency),
    C(PngColor),  C(PngMatte),      C(Jpeg),           C(JpegProgressive), C(JpegQuality),
    C(JpegMatte), C(Bmp),           C(BmpTopDown),     C(BmpRle),          C(BmpBits),
    C(BmpMatte),  C(Gif),           C(GifInterlaced),  C(GifUncompressed), C(GifTransparency),
    C(GifIndex),  C(GifMatte)
#undef C
};
} // namespace
const std::map<std::string, MouseGesture> gesture_ids{
#define G(x) {#x, MouseGesture::x}
    G(LeftClick),   G(RightClick),   G(MiddleClick), G(X1Click),  G(X2Click),   G(LeftDouble),
    G(RightDouble), G(MiddleDouble), G(X1Double),    G(X2Double), G(LeftDrag),  G(RightDrag),
    G(MiddleDrag),  G(X1Drag),       G(X2Drag),      G(WheelUp),  G(WheelDown), G(WheelLeft),
    G(WheelRight),  G(Hover)
#undef G
};
unsigned mouse_action_id(const std::string &name) {
    const std::map<std::string, unsigned> actions{
#define A(x) {#x, x}
        A(MouseNone),  A(MousePan),    A(MouseZoom),    A(MouseSelect), A(MouseFile), A(MouseWindow),
        A(MouseHover), A(MouseZoomIn), A(MouseZoomOut), A(MouseInherit)
#undef A
    };
    return actions.contains(name) ? actions.at(name) : command_ids.at(name);
}

struct Autopilot {
    struct PreviewGate {
        std::mutex mutex;
        std::condition_variable ready;
        std::atomic<bool> entered{}, released{}, forced{};
        std::atomic<unsigned> jobs{};
        void release() {
            {
                std::lock_guard lock(mutex);
                released = true;
            }
            ready.notify_all();
        }
        void hold() {
            ++jobs;
            entered = true;
            {
                std::unique_lock lock(mutex);
                if (!ready.wait_for(lock, std::chrono::seconds(8), [&] { return released.load(); })) {
                    forced = true;
                    released = true;
                }
            }
        }
        Image decode(const std::filesystem::path &path, LoadingOptions options) {
            hold();
            return load_source(path, options);
        }
    };
    POINT scripted_cursor{200, 200};
    App app;
    HIMC ime_probe_context{};
    std::shared_ptr<PreviewGate> preview_gate;
    std::shared_ptr<PreviewGate> open_gate;
    std::weak_ptr<JumpPreview> held_preview_service;
    PreferencesDialog *preferences{};
    FileTransferDialog *file_transfer{};
    JumpDialog *jump{};
    ResizeDialog *resize{};
    ResizeDialog *canvas{};
    ImageInfoDialog *information{};
    SelectionUnitsDialog *selection_units_dialog{};
    SelectionRectDialog *selection_rect_dialog{};
    uint64_t rect_wait_started{};
    uint64_t grid_wait_started{};
    PrintDialog *printing{};
    uint64_t print_wait_started{};
    std::optional<PrintOptions> print_choice;
    unsigned print_calls{},print_commits{},print_setup_calls{};

    uint64_t canvas_wait_started{};
    uint64_t resize_wait_started{};
    RainbowDialog *rainbow{};
    CpDialog *cp{};
    PaletteDialog *palette_editor{};
    ColorDepthDialog *depth_dialog{};
    uint64_t depth_wait_started{};
    ColorAdjustmentDialog *adjustment_dialog{};
    uint64_t adjustment_wait_started{};
    uint64_t palette_wait_started{};
    std::vector<std::vector<std::string>> steps;
    struct Result {
        std::string action;
        std::string error;
        bool finished{};
    };
    std::vector<Result> results;
    size_t position{};
    uint64_t wait_started{}, settings_wait_started{}, transfer_wait_started{}, jump_wait_started{}, rainbow_wait_started{},cp_wait_started{};
    bool done{}, failed{};
    bool showing_about{};
    bool native_info{};
    std::wstring notice_title{L"GVN バージョン情報"};
    uint64_t about_wait_started{};
    uint64_t info_wait_started{};
    std::vector<std::wstring> errors;
    unsigned acknowledged_application_errors{};
    std::deque<int> answers;
    std::vector<std::wstring> confirmations;
    unsigned completed_deletions{};
    bool last_delete_recycle{};
    std::map<std::string, std::vector<Pixel>> remembered;
    std::vector<NewWindowContent> captured_new_windows;
    bool new_window_capture_used{};
    std::map<std::string, SaveOptions> remembered_saving;
    std::map<std::string, ExternalApplication> remembered_external;
    unsigned external_browse_substitutions{}, incoming_executable_drops{};
    unsigned native_executable_pickers{}, native_executable_accepts{}, native_executable_cancels{};
    std::filesystem::path executable_picker_cwd;
    std::vector<std::string> native_picker_capture_warnings;
    unsigned missing_combo_capture_rejections{};
    std::filesystem::path output, fixtures;
    inline static Autopilot *current{};
    UINT_PTR timer{};
    unsigned scripted_buttons{}, popup_count{}, export_count{};
    bool export_panel_drag{};
    UINT popup_choice{};
    WallpaperSnapshot wallpaper_state;
    size_t wallpaper_apply_calls{}, wallpaper_captures{};
    bool wallpaper_fail_apply{};
    unsigned window_move_requests{};
    unsigned settings_visibility_waits{};
    unsigned transfer_visibility_waits{}, folder_browse_substitutions{}, incoming_folder_drops{};
    bool boundary_open{};
    unsigned boundary_dialogs{}, boundary_beeps{}, boundary_visibility_waits{};
    ULONGLONG boundary_wait_started{};
    std::deque<std::filesystem::path> folder_browse_replies;
    bool probing_window_move{};
    bool window_move_coordinates{true};
    static LRESULT CALLBACK window_move_probe(HWND window, UINT message, WPARAM hit, LPARAM position,
                                              UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Autopilot *>(data);
        if (message == WM_NCLBUTTONDOWN) {
            auto expected = self->scripted_cursor;
            ClientToScreen(window, &expected);
            self->window_move_coordinates = self->window_move_coordinates && hit == HTCAPTION &&
                                            GET_X_LPARAM(position) == expected.x &&
                                            GET_Y_LPARAM(position) == expected.y;
            ++self->window_move_requests;
            return 0; // Report the OS move loop boundary; never inject input into the user's desktop.
        }
        return DefSubclassProc(window, message, hit, position);
    }
    std::filesystem::path exported;
    std::filesystem::path save_target;
    bool save_open{};
    HWND save_window() {
        HWND found{};
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                if (window_text(window) == L"画像を保存" && GetDlgItem(window, IDOK)) {
                    *reinterpret_cast<HWND *>(data) = window;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&found));
        require(found != nullptr, "Actual image save dialog is not open");
        own(found);
        return found;
    }
    HWND save_widget(const std::string &name) {
        struct Search {
            DWORD id;
            HWND found{};
        } search{save_ids.at(name)};
        EnumChildWindows(
            save_window(),
            [](HWND control, LPARAM data) -> BOOL {
                auto &search = *reinterpret_cast<Search *>(data);
                if (GetDlgCtrlID(control) == static_cast<int>(search.id)) {
                    search.found = control;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        require(search.found != nullptr, "Image save option is missing");
        own(search.found);
        return search.found;
    }
    HWND jump_widget(const std::string &name) {
        require(jump && jump->ready() && IsWindow(jump->window()), "Jump dialog is not open and ready");
        const std::map<std::string, int> ids{{"Preview", jump_control::Preview},
                                             {"Name", jump_control::Name},
                                             {"Order", jump_control::Order},
                                             {"Total", jump_control::Total},
                                             {"Scroll", jump_control::Scroll},
                                             {"Enabled", jump_control::Enabled},
                                             {"Quality", jump_control::Quality},
                                             {"Direction", jump_control::Direction},
                                             {"Error", jump_control::Error},
                                             {"OK", IDOK},
                                             {"Cancel", IDCANCEL}};
        const auto window = jump->control(ids.at(name));
        own(window);
        return window;
    }
    HWND transfer_widget(const std::string &name) {
        require(file_transfer && IsWindow(file_transfer->window()), "File transfer dialog is not open");
        const std::map<std::string, int> fields{{"BasicEdit", transfer_control::BasicEdit},
                                                {"Confirm", transfer_control::Confirm},
                                                {"Registered", transfer_control::Registered},
                                                {"Browse", transfer_control::Browse},
                                                {"Source", transfer_control::Source},
                                                {"Base", transfer_control::Base},
                                                {"OK", IDOK},
                                                {"Cancel", IDCANCEL}};
        int key{};
        if (fields.contains(name))
            key = fields.at(name);
        else if (name.starts_with("Edit") || name.starts_with("Execute")) {
            const bool edit = name.starts_with("Edit");
            const int slot = std::stoi(name.substr(edit ? 4 : 7)) - 1;
            require(slot >= 0 && slot < 10, "Transfer widget slot is out of range");
            key = (edit ? transfer_control::EditBase : transfer_control::ExecuteBase) + slot;
        } else
            throw std::runtime_error("Unknown file transfer widget");
        const auto window = file_transfer->control(key);
        require(window != nullptr, "File transfer widget is not created");
        own(window);
        return window;
    }
    HWND boundary_window(bool required = true) {
        HWND found{};
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                const auto title = window_text(window);
                if (title == L"GVN - 次へ" || title == L"GVN - 前へ") {
                    DWORD pid{};
                    GetWindowThreadProcessId(window, &pid);
                    if (pid == GetCurrentProcessId()) {
                        *reinterpret_cast<HWND *>(data) = window;
                        return FALSE;
                    }
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&found));
        if (required)
            require(found != nullptr, "Actual native boundary dialog is not open");
        if (found)
            own(found);
        return found;
    }
    HWND boundary_button(const std::string &name) {
        const auto root = boundary_window();
        const bool forward = window_text(root) == L"GVN - 次へ";
        const std::wstring label =
            name == "End"      ? (forward ? L"フォルダの先頭へ(&F)" : L"フォルダの末尾へ(&F)")
            : name == "Folder" ? (forward ? L"次のフォルダへ(&D)" : L"前のフォルダへ(&D)")
            : name == "Cancel" ? L"キャンセル(&C)"
                               : L"";
        require(!label.empty(), "Unknown boundary button");
        struct Search {
            std::wstring label;
            HWND found{};
        } search{label};
        EnumChildWindows(
            root,
            [](HWND control, LPARAM data) -> BOOL {
                auto &search = *reinterpret_cast<Search *>(data);
                wchar_t cls[80]{};
                GetClassNameW(control, cls, 80);
                if (window_text(control) == search.label && _wcsicmp(cls, L"Button") == 0 &&
                    locally_visible(control)) {
                    search.found = control;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        require(search.found != nullptr, "Native boundary button is missing");
        own(search.found);
        return search.found;
    }
    Autopilot(std::filesystem::path directory, std::filesystem::path assets)
        : output(std::move(directory)), fixtures(std::move(assets)) {
        std::filesystem::create_directories(output);
        app.set_config(output / L"settings.ini");
        app.navigation_root = output; // Never browse outside the scenario's private fixture tree.
        app.dialogs.resize = [&](HWND owner,uint32_t w,uint32_t h,ResizeChoice choice,const ResizeDialog::Validator &validate) {
            ResizeDialog dialog(w,h,choice,validate);resize=&dialog;
            struct Reset { ResizeDialog *&value; ~Reset(){value=nullptr;} } reset{resize};
            return dialog.show(owner);
        };
        app.dialogs.canvas = [&](HWND owner,uint32_t w,uint32_t h,CanvasChoice choice,const ResizeDialog::Validator &validate) {
            auto dialog=ResizeDialog::for_canvas(w,h,choice,validate);canvas=dialog.get();
            struct Reset {ResizeDialog *&value;~Reset(){value=nullptr;}} reset{canvas};
            dialog->show(owner);return dialog->canvas_result();
        };
        app.dialogs.cp = [&](HWND owner,CpChoice options,const auto &process,const auto &preview,const auto &current) {
            CpDialog dialog(std::move(options),process,preview,current);cp=&dialog;
            struct Reset {CpDialog *&value;~Reset(){value=nullptr;}} reset{cp};return dialog.show(owner);
        };
        app.dialogs.palette = [&](HWND owner,const std::vector<Pixel> &colors,bool enabled,const auto &process,
                                  const auto &preview,const auto &commit,const auto &current) {
            PaletteDialog dialog(colors,enabled,process,preview,commit,current);palette_editor=&dialog;
            struct Reset{PaletteDialog *&value;~Reset(){value=nullptr;}}reset{palette_editor};
            dialog.show(owner);
        };
        app.dialogs.color_depth = [&](HWND owner,unsigned bits,std::optional<Rgb16Layout> layout,bool dither,const auto &current) {
            ColorDepthDialog dialog(bits,layout,dither,current);depth_dialog=&dialog;
            struct Reset{ColorDepthDialog *&value;~Reset(){value=nullptr;}}reset{depth_dialog};return dialog.show(owner);
        };
        app.dialogs.color_adjustment = [&](HWND owner,std::shared_ptr<const Image> source,size_t frame,Rect roi,ColorAdjustment initial,const auto &current) {
            ColorAdjustmentDialog dialog(std::move(source),frame,roi,initial,current);adjustment_dialog=&dialog;
            struct Reset{ColorAdjustmentDialog *&value;~Reset(){value=nullptr;}}reset{adjustment_dialog};return dialog.show(owner);
        };
        app.dialogs.rainbow = [&](HWND owner,RainbowChoice options,const auto &process,const auto &preview,const auto &current) {
            RainbowDialog dialog(options,process,preview,current);rainbow=&dialog;
            struct Reset {RainbowDialog *&value;~Reset(){value=nullptr;}} reset{rainbow};
            return dialog.show(owner);
        };
        app.dialogs.jump = [&](HWND owner, std::span<const FileEntry> files, size_t index, bool reversed,
                               JumpOptions options, LoadingOptions loading,
                               std::shared_ptr<JumpPreview> preview) {
            JumpDialog dialog(files, index, reversed, options, loading, {}, std::move(preview));
            jump = &dialog;
            struct Reset {
                JumpDialog *&value;
                ~Reset() {
                    value = nullptr;
                }
            } reset{jump};
            return dialog.show(owner);
        };
        app.dialogs.boundary = [&](HWND owner, bool forward, const auto &folder) {
            boundary_open = true;
            ++boundary_dialogs;
            struct Reset {
                bool &value;
                ~Reset() {
                    value = false;
                }
            } reset{boundary_open};
            return choose_boundary(owner, forward, folder);
        };
        app.dialogs.boundary_beep = [&] { ++boundary_beeps; };
        app.dialogs.save_file = [&](HWND owner, const auto &initial, const auto &name, SaveOptions &options) {
            if (!save_target.empty()) {
                const auto target = std::exchange(save_target, {});
                save_open = true;
                try {
                    const auto selected =
                        choose_save_file(owner, target.parent_path(), target.filename().wstring(), options);
                    save_open = false;
                    if (!selected.empty())
                        (void)output_path(utf8(selected.wstring()));
                    return selected;
                } catch (...) {
                    save_open = false;
                    throw;
                }
            }
            const auto selected = choose_save_file(owner, initial, name, options);
            if (!selected.empty())
                (void)output_path(utf8(selected.wstring()));
            return selected;
        };
        app.dialogs.delete_file = [&](HWND owner, const auto &path, bool recycle) {
            require(
                path.lexically_normal().wstring().starts_with(output.lexically_normal().wstring() + L"\\"),
                "Deletion is restricted to scenario-owned fixture copies");
            delete_file_checked(owner, path, recycle);
            last_delete_recycle = recycle;
            ++completed_deletions;
        };
        app.dialogs.preferences_ui = [&](HWND owner, const Settings &settings,
                                         const PreferencesDialog::Apply &apply, PreferencePage page,
                                         int destination_row) {
            PreferencesDialog dialog(
                settings, apply, [&](HWND, const std::wstring &s) { errors.push_back(s); }, page,
                destination_row);
            preferences = &dialog;
            const bool accepted = dialog.show(owner);
            preferences = nullptr;
            return accepted;
        };
        app.dialogs.file_transfer = [&](HWND owner, bool move, const auto &source,
                                        const FileTransferOptions &options, int initial_slot) {
            FileTransferDialog dialog(
                move, source, options, initial_slot,
                [&](HWND, const wchar_t *) {
                    require(!folder_browse_replies.empty(), "No scripted folder browser reply");
                    ++folder_browse_substitutions;
                    auto result = folder_browse_replies.front();
                    folder_browse_replies.pop_front();
                    return result;
                },
                [&](HWND, const std::wstring &message) { errors.push_back(message); });
            file_transfer = &dialog;
            struct Reset {
                FileTransferDialog *&value;
                ~Reset() {
                    value = nullptr;
                }
            } reset{file_transfer};
            return dialog.show(owner);
        };
        app.dialogs.ask = [&](HWND owner, const wchar_t *message, const wchar_t *title, UINT flags) {
            if (std::wstring_view(title) == L"GVN バージョン情報") {
                own(owner);
                notice_title=title;
                showing_about = true;
                struct Reset { bool &value; ~Reset() { value = false; } } reset{showing_about};
                return MessageBoxW(owner, message, title, flags);
            }
            if (!answers.empty()) {
                const int answer = answers.front();
                answers.pop_front();
                confirmations.push_back(message);
                return answer;
            }
            errors.push_back(message);
            return (flags & MB_TYPEMASK) == MB_OK ? IDOK : IDCANCEL;
        };
        app.dialogs.selection_rect=[&](HWND owner,uint32_t width,uint32_t height,Rect current)->std::optional<Rect>{
            SelectionRectDialog dialog(width,height,current);selection_rect_dialog=&dialog;
            struct Reset{SelectionRectDialog *&p;~Reset(){p=nullptr;}} reset{selection_rect_dialog};return dialog.run(owner);
        };
        app.dialogs.selection_units=[&](HWND owner,SelectionUnits units)->std::optional<SelectionUnits>{
            SelectionUnitsDialog dialog(units);selection_units_dialog=&dialog;
            struct Reset {SelectionUnitsDialog *&p;~Reset(){p=nullptr;}} reset{selection_units_dialog};return dialog.run(owner);
        };
        app.dialogs.print=[&](HWND owner,const Image &image,size_t frame,const ImageMetadata *metadata) {
            ++print_calls;
            PrintDialog dialog(image,frame,*metadata,{},PrintDevice{L"GVN dedicated synthetic printer",L"A4",{210,297,3,4,5,6},true},[&](HWND window)->std::optional<PrintDevice>{own(window);++print_setup_calls;return PrintDevice{L"GVN synthetic landscape",L"A4",{297,210,5,5,5,5},true};});
            printing=&dialog;struct Reset{PrintDialog *&p;~Reset(){p=nullptr;}} reset{printing};
            print_choice=dialog.run(owner);if(print_choice)++print_commits;
        };
        app.dialogs.image_info=[&](HWND owner,const ImageInfoData &data,bool automatic,const ImageInfoDialog::Counter &counter){
            if(!native_info){errors.push_back(data.text());return automatic;}
            ImageInfoDialog dialog(data,automatic,counter);information=&dialog;
            struct Reset {ImageInfoDialog *&pointer;~Reset(){pointer=nullptr;}} reset{information};return dialog.show(owner);
        };
        app.dialogs.wallpaper_system = [&] {
            WallpaperPlatform platform;
            platform.mutex_name = L"Local\\GVN.Wallpaper.Autopilot." + std::to_wstring(GetCurrentProcessId());
            platform.read = [&] { return wallpaper_state; };
            platform.write = [&](const WallpaperValues &values) { wallpaper_state.values = values; };
            platform.apply = [&](const std::filesystem::path &path) {
                require(path.empty() || (path.parent_path() == wallpaper_directory(app.config) &&
                                        std::filesystem::is_regular_file(path)), "Wallpaper file escaped the owned config directory");
                ++wallpaper_apply_calls; wallpaper_state.image = path;
                if (std::exchange(wallpaper_fail_apply, false)) throw std::runtime_error("Injected wallpaper apply failure");
            };
            return platform;
        };
        app.dialogs.popup = [&](HWND owner, HMENU menu, POINT) {
            own(owner);
            require(IsMenu(menu) != FALSE, "Popup tree is not a real native menu");
            ++popup_count;
            const auto choice = std::exchange(popup_choice, 0u);
            if (choice) {
                const auto state = GetMenuState(menu, choice, MF_BYCOMMAND);
                require(state != UINT(-1) && !(state & (MF_GRAYED | MF_DISABLED)), "Chosen popup command is missing or disabled");
            }
            return choice;
        };
        app.dialogs.file_drag = [&](const std::filesystem::path &path) {
            // Validate the actual transfer object without entering a cross-desktop OLE modal loop.
            const auto object = file_data_object(path);
            FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM medium{};
            require(SUCCEEDED(object->GetData(&format, &medium)), "Drag did not expose CF_HDROP");
            wchar_t received[32768]{};
            const auto count =
                DragQueryFileW(reinterpret_cast<HDROP>(medium.hGlobal), 0xffffffff, nullptr, 0);
            DragQueryFileW(reinterpret_cast<HDROP>(medium.hGlobal), 0, received, 32768);
            ReleaseStgMedium(&medium);
            require(count == 1 && same_path(received, path), "Drag object contains a different file");
            const auto read = load_image(path);
            const bool original = !export_panel_drag && !app.modified && !app.image->animated() &&
                                  app.image->format != L"見開き" && !app.image->path.empty() &&
                                  std::filesystem::is_regular_file(app.image->path);
            if (original) {
                require(same_path(path, app.image->path), "Unmodified still-image drag did not use its original file");
                if (app.settings.view_rotation == 0)
                    require(read.frames[0].pixels == app.image->frames.at(app.player.frame).pixels,
                            "Exported original RGBA differs");
            } else {
                require(path.extension() == L".png" && read.width == app.image->width && read.height == app.image->height &&
                            read.frames[0].pixels == app.image->frames.at(app.player.frame).pixels,
                        "Exported PNG RGBA differs");
            }
            exported = path;
            ++export_count;
        };
        WNDCLASSEXW cls{sizeof(cls)};
        cls.style = CS_DBLCLKS;
        cls.lpfnWndProc = procedure;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"GVN.Autopilot";
        RegisterClassExW(&cls);
        require(CreateWindowExW(0, cls.lpszClassName, L"GVN autopilot", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                0, 0, 1000, 720, nullptr, nullptr, cls.hInstance, &app) != nullptr,
                "Cannot create the real GVN window");
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        // The first ShowWindow may honor the launcher's STARTUPINFO (SW_HIDE).
        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        require(locally_visible(app.hwnd), "The autopilot's own root window stayed hidden");
        app.cursor_screen = [this] {
            auto point = scripted_cursor;
            ClientToScreen(app.hwnd, &point);
            return point;
        };
        UpdateWindow(app.hwnd);
    }
    ~Autopilot() {
        if (open_gate)
            open_gate->release();
        if (preview_gate)
            preview_gate->release();
        if (timer)
            KillTimer(nullptr, timer);
        if (IsWindow(app.hwnd))
            DestroyWindow(app.hwnd);
        if (ime_probe_context)
            ImmDestroyContext(ime_probe_context); // Only after the owned root has lost its associations.
    }
    std::filesystem::path path(std::string value) {
        const auto replace = [&](const std::string &token, const std::filesystem::path &p) {
            size_t at{};
            while ((at = value.find(token, at)) != std::string::npos) {
                auto text = utf8(p.wstring());
                value.replace(at, token.size(), text);
                at += text.size();
            }
        };
        replace("${output}", output);
        replace("${fixtures}", fixtures);
        return std::filesystem::path(widen(value));
    }
    std::filesystem::path output_path(const std::string &value) {
        const auto target = std::filesystem::weakly_canonical(output / path(value));
        const auto relative = target.lexically_relative(std::filesystem::weakly_canonical(output));
        require(!relative.empty() && !relative.is_absolute() && *relative.begin() != L"..",
                "Autopilot writes must stay inside the scenario output directory");
        return target;
    }
    HWND control(const std::string &name) {
        require(preferences && IsWindow(preferences->window()), "Settings dialog is not open");
        auto h = preferences->control(preference_ids.at(name));
        own(h);
        return h;
    }
    HWND font_dialog() {
        struct Search {
            HWND owner{}, found{};
        } search{preferences ? preferences->window() : nullptr};
        require(search.owner != nullptr, "Font dialog requires the settings owner");
        EnumWindows(
            [](HWND candidate, LPARAM data) -> BOOL {
                auto &search = *reinterpret_cast<Search *>(data);
                DWORD process{};
                GetWindowThreadProcessId(candidate, &process);
                if (process == GetCurrentProcessId() && candidate != search.owner &&
                    GetDlgItem(candidate, cmb1) && GetDlgItem(candidate, cmb2) &&
                    GetDlgItem(candidate, cmb3)) {
                    search.found = candidate;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        if (!search.found)
            dump_owned_windows();
        require(search.found != nullptr, "Actual native font dialog is not open");
        own(search.found);
        return search.found;
    }
    HWND color_dialog() {
        HWND found{};
        EnumWindows(
            [](HWND window, LPARAM data) -> BOOL {
                DWORD process{};
                GetWindowThreadProcessId(window, &process);
                if (process == GetCurrentProcessId() && GetDlgItem(window, 706) && GetDlgItem(window, 707) &&
                    GetDlgItem(window, 708)) {
                    *reinterpret_cast<HWND *>(data) = window;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&found));
        if (!found)
            dump_owned_windows();
        require(found != nullptr, "Actual native color dialog is not open");
        own(found);
        return found;
    }
    void dump_owned_windows() {
        std::ofstream report(output / L"owned-modal-windows.json");
        report << '[';
        struct Dump {
            std::ofstream *report;
            bool comma{};
        } dump{&report};
        EnumWindows(
            [](HWND root, LPARAM data) -> BOOL {
                DWORD process{};
                GetWindowThreadProcessId(root, &process);
                if (process != GetCurrentProcessId())
                    return TRUE;
                auto emit = [](HWND window, LPARAM data) -> BOOL {
                    auto &dump = *reinterpret_cast<Dump *>(data);
                    if (dump.comma)
                        *dump.report << ',';
                    dump.comma = true;
                    wchar_t cls[128]{};
                    GetClassNameW(window, cls, 128);
                    RECT rect{};
                    GetWindowRect(window, &rect);
                    *dump.report << "{\"id\":" << GetDlgCtrlID(window) << ",\"class\":" << quote(utf8(cls))
                                 << ",\"text\":" << quote(utf8(window_text(window)))
                                 << ",\"root\":" << reinterpret_cast<uintptr_t>(GetAncestor(window, GA_ROOT))
                                 << ",\"visible\":" << (locally_visible(window) ? "true" : "false")
                                 << ",\"focused\":" << (GetFocus() == window ? "true" : "false")
                                 << ",\"rect\":[" << rect.left << ',' << rect.top << ',' << rect.right << ','
                                 << rect.bottom << ']' << '}';
                    return TRUE;
                };
                emit(root, data);
                EnumChildWindows(root, emit, data);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&dump));
        report << "]\n";
    }
    HWND about_window(bool required = true) {
        struct Search { HWND owner{}, found{}; std::wstring title; } search{app.hwnd,nullptr,notice_title};
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
            auto &search = *reinterpret_cast<Search *>(data);
            if (GetWindow(window, GW_OWNER) == search.owner &&
                window_text(window) == search.title) {
                search.found = window;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        if (required)
            require(search.found != nullptr, "Actual About dialog is not open");
        if (search.found)
            own(search.found);
        return search.found;
    }
    void close_owned_modals() {
        if (open_gate)
            open_gate->release();
        if (preview_gate)
            preview_gate->release();
        struct Close {
            HWND root{}, preferences{}, transfer{};
            std::vector<HWND> dialogs;
        } close{app.hwnd, preferences ? preferences->window() : nullptr,
                file_transfer ? file_transfer->window() : nullptr};
        EnumWindows(
            [](HWND window, LPARAM data) -> BOOL {
                auto &close = *reinterpret_cast<Close *>(data);
                DWORD process{};
                GetWindowThreadProcessId(window, &process);
                if (process == GetCurrentProcessId() && window != close.root && window != close.preferences &&
                    window != close.transfer &&
                    (GetDlgItem(window, IDCANCEL) || GetDlgItem(window, IDOK) ||
                     window_text(window) == L"GVN - 次へ" || window_text(window) == L"GVN - 前へ"))
                    close.dialogs.push_back(window);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&close));
        for (auto window : close.dialogs) {
            const auto title = window_text(window);
            if (title == L"GVN - 次へ" || title == L"GVN - 前へ")
                PostMessageW(window, WM_CLOSE, 0, 0);
            else
                PostMessageW(window, WM_COMMAND, GetDlgItem(window, IDCANCEL) ? IDCANCEL : IDOK, 0);
        }
    }
    HWND palette_picker_window(bool required=true) {
        struct Search{HWND owner{},found{};}search{palette_editor?palette_editor->window():nullptr};
        EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM data)->BOOL{
            auto &s=*reinterpret_cast<Search*>(data);wchar_t cls[64]{};GetClassNameW(window,cls,64);
            if(s.owner&&GetWindow(window,GW_OWNER)==s.owner&&wcscmp(cls,L"#32770")==0&&
               GetDlgItem(window,COLOR_RED)&&GetDlgItem(window,COLOR_GREEN)&&GetDlgItem(window,COLOR_BLUE)){
                s.found=window;return FALSE;
            }return TRUE;
        },reinterpret_cast<LPARAM>(&search));
        if(required)require(search.found!=nullptr,"Actual owned Windows color picker is not open");
        if(search.found)own(search.found);return search.found;
    }
    HWND executable_picker_window(bool required = true) {
        struct Search { HWND owner{}, found{}; } search{preferences ? preferences->window() : nullptr};
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
            auto &s = *reinterpret_cast<Search *>(data);
            wchar_t cls[64]{}; GetClassNameW(window, cls, 64);
            if (s.owner && GetWindow(window, GW_OWNER) == s.owner && wcscmp(cls, L"#32770") == 0 &&
                GetDlgItem(window, IDOK) && GetDlgItem(window, IDCANCEL) &&
                (GetDlgItem(window, edt1) || GetDlgItem(window, cmb13))) {
                s.found = window;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        if (required) require(search.found != nullptr, "Actual owned OS executable picker is not open");
        if (search.found) own(search.found);
        return search.found;
    }
    void toolbar(UINT id, bool player = true) {
        const auto target = player ? app.playback.toolbar.hwnd : app.toolbar.hwnd;
        RECT r{};const auto state=SendMessageW(target,TB_GETSTATE,id,0);
        if(!player&&state>=0&&(state&TBSTATE_HIDDEN)){
            require(std::find(app.toolbar.hidden_commands().begin(),app.toolbar.hidden_commands().end(),id)!=app.toolbar.hidden_commands().end(),"Hidden command is not present in the overflow menu");
            require((state&TBSTATE_ENABLED)!=0,"Overflow command disabled");++popup_count;SendMessageW(app.hwnd,WM_COMMAND,id,0);return;
        }
        require(SendMessageW(target, TB_GETRECT, id, reinterpret_cast<LPARAM>(&r)) != FALSE,
                "Toolbar button missing");
        require(SendMessageW(target, TB_ISBUTTONENABLED, id, 0) != FALSE, "Toolbar button disabled");
        native_click(target, (r.left + r.right) / 2, (r.top + r.bottom) / 2);
    }
    double value(const std::string &name) {
        if(name=="rect.open")return selection_rect_dialog&&IsWindow(selection_rect_dialog->window());
        if(name=="rect.valid"){require(selection_rect_dialog,"Numeric selection dialog is not open");return selection_rect_dialog->bounds().has_value();}
        if(name=="rect.ok_enabled"){require(selection_rect_dialog,"Numeric selection dialog is not open");return IsWindowEnabled(selection_rect_dialog->control(IDOK))!=FALSE;}

        if(name=="grid.open")return selection_units_dialog&&IsWindow(selection_units_dialog->window());
        if(name=="grid.custom")return app.settings.selection_custom;
        if(name=="grid.start_x")return app.settings.selection_units().start_x;
        if(name=="grid.start_y")return app.settings.selection_units().start_y;
        if(name=="grid.size_x")return app.settings.selection_units().size_x;
        if(name=="grid.size_y")return app.settings.selection_units().size_y;
        if(name=="grid.ok_enabled"){require(selection_units_dialog,"Grid dialog is not open");return IsWindowEnabled(selection_units_dialog->control(IDOK))!=FALSE;}

        if(name=="print.calls")return print_calls;
        if(name=="print.commits")return print_commits;
        if(name=="print.setup_calls")return print_setup_calls;
        if(name=="print.open")return printing&&IsWindow(printing->window());
        if(name=="print.result.density")return print_choice?print_choice->density:99;
        if(name=="print.result.copies")return print_choice?print_choice->copies:0;
        if(name.starts_with("print.")) {
            require(printing,"Actual print dialog is not open");const auto plan=printing->layout();
            if(name=="print.valid")return plan.has_value();
            if(name=="print.ok_enabled")return IsWindowEnabled(printing->control(IDOK))!=FALSE;
            if(name=="print.specified_enabled")return IsWindowEnabled(printing->control(print_control::Width))!=FALSE;
            require(plan.has_value(),"Print layout is invalid");
            if(name=="print.x")return plan->image.x;if(name=="print.y")return plan->image.y;
            if(name=="print.width")return plan->image.width;if(name=="print.height")return plan->image.height;
            if(name=="print.area_x")return plan->area.x;if(name=="print.area_y")return plan->area.y;
            if(name=="print.area_width")return plan->area.width;if(name=="print.area_height")return plan->area.height;
            if(name=="print.turns")return plan->quarter_turns;
        }

        if (name == "delete.calls") return completed_deletions;
        if (name == "delete.recycle") return last_delete_recycle;
        if (name == "wallpaper.applies") return static_cast<double>(wallpaper_apply_calls);
        if (name == "wallpaper.files") return static_cast<double>(managed_wallpaper_files(app.config).size());
        if (name == "wallpaper.active") return !wallpaper_state.image.empty();
        const auto has_ime = [&](HWND window) {
            own(window);
            const auto context = ImmGetContext(window);
            if (context)
                ImmReleaseContext(window, context);
            return context != nullptr;
        };
        if (name == "ime.main_enabled")
            return has_ime(app.hwnd);
        if (name == "ime.bars_disabled") {
            for (const auto window :
                 {app.toolbar.bar_window(), app.toolbar.hwnd, app.status, app.playback.hwnd,
                  app.playback.toolbar.hwnd, app.slider, app.playback.counter, app.canvas_horizontal,
                  app.canvas_vertical, app.canvas_corner})
                if (has_ime(window))
                    return false;
            return true;
        }
        if (name == "ime.settings_edit_enabled") {
            require(preferences, "Preferences must be open");
            return has_ime(preferences->control(preference_id::Grid));
        }
        if (name == "ime.key_edit_enabled") {
            require(preferences, "Preferences must be open");
            return has_ime(control("KeyEdit"));
        }
        if (name == "ime.settings_is_owned") {
            require(preferences, "Preferences must be open");
            return GetWindow(preferences->window(), GW_OWNER) == app.hwnd &&
                   !IsChild(app.hwnd, preferences->window());
        }
        if (name == "open.pending")
            return app.pending_open.has_value();
        if (name == "open.loading")
            return app.loading;
        if (name == "open.blocked")
            return open_gate && open_gate->entered && !open_gate->released;
        if (name == "open.forced_release")
            return open_gate && open_gate->forced;
        if (name == "open.list_count")
            return open_gate ? open_gate->jobs.load() : 0;
        if (name == "boundary_open")
            return boundary_open;
        if (name == "jump_open")
            return jump && IsWindow(jump->window());
        if (name == "about_open")
            return notice_title==L"GVN バージョン情報" && about_window(false) != nullptr;
        if(name=="info_open")return information&&IsWindow(information->window());
        if(name.starts_with("info.")) {
            require(information&&information->ready(),"Actual image info dialog is not open");
            if(name=="info.pending")return information->pending();
            if(name=="info.count")return information->count()?static_cast<double>(*information->count()):-1;
            if(name=="info.automatic")return information->automatic();
            if(name=="info.colors_enabled")return IsWindowEnabled(information->control(image_info_control::Auto))!=FALSE;
            if(name=="info.checked")return SendMessageW(information->control(image_info_control::Auto),BM_GETCHECK,0,0)==BST_CHECKED;
            if(name=="info.filename_first_visible")return static_cast<double>(SendMessageW(information->control(image_info_control::Field),EM_GETFIRSTVISIBLELINE,0,0));
        }
        if(name=="canvas.open")return canvas&&IsWindow(canvas->window());
        if(name.starts_with("canvas.")) {
            require(canvas&&canvas->ready(),"Actual canvas dialog is not open");const auto choice=canvas->canvas_options();
            if(name=="canvas.width")return choice.width;
            if(name=="canvas.height")return choice.height;
            if(name=="canvas.keep_aspect")return choice.keep_aspect;
            if(name=="canvas.anchor")return choice.anchor;
            if(name=="canvas.transparent")return choice.transparent;
            if(name=="canvas.color")return choice.color;
            if(name=="canvas.alpha")return choice.alpha;
            if(name=="canvas.unit")return choice.unit;
            if(name=="canvas.dpi_x")return choice.dpi_x;
            if(name=="canvas.dpi_y")return choice.dpi_y;
            if(name=="canvas.valid")return canvas->valid();
            if(name=="canvas.ok_enabled")return IsWindowEnabled(canvas->control(IDOK))!=FALSE;
            if(name=="canvas.colors_enabled")return IsWindowEnabled(canvas->control(resize_control::Red))&&IsWindowEnabled(canvas->control(resize_control::Alpha));
        }
        if (name == "resize.open") return resize && IsWindow(resize->window());
        if (name.starts_with("resize.")) {
            require(resize && resize->ready(), "Actual resize dialog is not open");
            const auto choice=resize->options();
            if(name=="resize.width")return choice.width;
            if(name=="resize.height")return choice.height;
            if(name=="resize.keep_aspect")return choice.keep_aspect;
            if(name=="resize.smooth")return choice.smooth;
            if(name=="resize.down")return choice.down;
            if(name=="resize.up")return choice.up;
            if(name=="resize.sharpen")return choice.sharpen;
            if(name=="resize.dpi_x")return choice.dpi_x;
            if(name=="resize.dpi_y")return choice.dpi_y;
            if(name=="resize.resample")return choice.resample;
            if(name=="resize.print_unit")return choice.print_unit;
            if(name=="resize.density_unit")return choice.density_unit;
            if(name=="resize.pixel_controls_enabled")return IsWindowEnabled(resize->control(resize_control::Width)) && IsWindowEnabled(resize->control(resize_control::KeepAspect));
            if(name=="resize.down_count")return SendMessageW(resize->control(resize_control::Down),CB_GETCOUNT,0,0);
            if(name=="resize.up_count")return SendMessageW(resize->control(resize_control::Up),CB_GETCOUNT,0,0);
            if(name=="resize.filters_enabled")return IsWindowEnabled(resize->control(resize_control::Down)) &&
                IsWindowEnabled(resize->control(resize_control::Up)) && IsWindowEnabled(resize->control(resize_control::Sharpen));
            if(name=="resize.valid")return resize->valid();
            if(name=="resize.ok_enabled")return IsWindowEnabled(resize->control(IDOK))!=FALSE;
        }
        if(name=="rainbow.open")return rainbow&&IsWindow(rainbow->window());
        if(name=="rainbow.size"){require(rainbow,"Rainbow dialog must be open");return rainbow->options().size;}
        if(name=="rainbow.preview"){require(rainbow,"Rainbow dialog must be open");return rainbow->options().preview;}
        if(name=="rainbow.valid"){require(rainbow,"Rainbow dialog must be open");return rainbow->valid();}
        if(name=="rainbow.presented")return app.previewing_mask();
        if(name=="cp.open")return cp&&IsWindow(cp->window());
        if(name=="cp.preview"){require(cp,"CP dialog must be open");return cp->options().preview;}
        if(name=="cp.valid"){require(cp,"CP dialog must be open");return cp->valid();}
        if(name=="cp.presented")return app.previewing_mask();
        if(name=="palette.open")return palette_editor&&IsWindow(palette_editor->window());
        if(name=="depth.open")return depth_dialog&&IsWindow(depth_dialog->window());
        if(name=="adjust.open")return adjustment_dialog&&IsWindow(adjustment_dialog->window());
        if(name.starts_with("adjust.")) {
            require(adjustment_dialog,"Color adjustment dialog must be open");const auto choice=adjustment_dialog->options();
            if(name=="adjust.hue")return choice.hue;if(name=="adjust.saturation")return choice.saturation;if(name=="adjust.lightness")return choice.lightness;
            if(name=="adjust.unified")return choice.unified;if(name=="adjust.gray")return int(choice.gray);
            if(name=="adjust.valid")return adjustment_dialog->valid();
            if(name=="adjust.ok_enabled")return IsWindowEnabled(adjustment_dialog->control(IDOK))!=FALSE;
        }
        if(name=="depth.dither"){require(depth_dialog,"Color depth dialog must be open");return depth_dialog->options().dither;}
        if(name=="depth.choice") {
            require(depth_dialog,"Color depth dialog must be open");const auto depth=depth_dialog->options().depth;
            return depth==ColorDepth::Indexed8?0:depth==ColorDepth::Rgb555?1:depth==ColorDepth::Rgb565?2:depth==ColorDepth::Rgb24?3:4;
        }
        if(name=="palette.index"){require(palette_editor,"Palette dialog must be open");return palette_editor->selected_index();}
        if(name=="palette.preview"){require(palette_editor,"Palette dialog must be open");return palette_editor->preview_enabled();}
        if(name=="palette.valid"){require(palette_editor,"Palette dialog must be open");return palette_editor->valid();}
        if(name=="palette.apply"){require(palette_editor,"Palette dialog must be open");return IsWindowEnabled(palette_editor->control(palette_control::Apply))!=FALSE;}
        if(name=="palette.presented")return app.previewing_mask();
        if (name == "preview.present")
            return app.preview_service != nullptr;
        if (name == "preview.started")
            return app.preview_service && app.preview_service->started();
        if (name == "preview.idle")
            return !app.preview_service || app.preview_service->idle();
        if (name == "preview.blocked")
            return preview_gate && preview_gate->entered && !preview_gate->released;
        if (name == "preview.forced_release")
            return preview_gate && preview_gate->forced;
        if (name == "preview.decode_count")
            return preview_gate ? preview_gate->jobs.load() : 0;
        if (name == "preview.same_service")
            return app.preview_service && app.preview_service == held_preview_service.lock();
        if (name == "preview.expired")
            return held_preview_service.expired();
        if (name.starts_with("jump.")) {
            require(jump && jump->ready(), "Jump dialog is not open");
            if (name == "jump.index")
                return static_cast<double>(jump->selected_index());
            if (name == "jump.valid")
                return jump->valid();
            if (name == "jump.loading")
                return jump->loading();
            if (name == "jump.has_image")
                return jump->preview_image() != nullptr;
            if (name == "jump.preview")
                return jump->options().preview;
            if (name == "jump.quality")
                return jump->options().quality;
            if (name == "jump.position")
                return GetScrollPos(jump_widget("Scroll"), SB_CTL);
            if (name == "jump.ok_enabled")
                return IsWindowEnabled(jump_widget("OK")) != FALSE;
        }
        if (name == "boundary.count")
            return boundary_dialogs;
        if (name == "boundary.beeps")
            return boundary_beeps;
        if (name == "navigation_folder")
            return app.navigation_folder;
        if (name == "folder_search")
            return app.pending_folder.has_value();
        if (name == "has_image")
            return app.image != nullptr;
        if (name == "slideshow")
            return app.slideshow;
        if (name == "transfer_open")
            return file_transfer && IsWindow(file_transfer->window());
        if (name == "export.drag_pending")return app.editor_chrome.dragging();
        if (name == "crop.active")return app.cropping.has_value();
        if (name == "crop.width")return app.cropping?app.cropping->box.width:0;
        if (name == "crop.height")return app.cropping?app.cropping->box.height:0;
        if (name == "pen.active")return app.pen_player.has_value();
        if (name == "floating.active")return app.floating.has_value();
        if (name == "status.drag_icon")
            return SendMessageW(app.status, SB_GETICON, 0, 0) > 0;
        if (name == "settings.confirm_unsaved")
            return app.settings.confirm_unsaved;
#define FILE_OPTION(field)                                                                                   \
    if (name == "settings." #field)                                                                          \
        return app.settings.field;
        FILE_OPTION(save_dialog)
        FILE_OPTION(save_as_retarget)
        FILE_OPTION(confirm_overwrite)
        FILE_OPTION(remember_save_folder)
        FILE_OPTION(rename_resort)
        FILE_OPTION(confirm_delete)
        FILE_OPTION(recycle_deleted)
        FILE_OPTION(next_after_fileop)
        FILE_OPTION(confirm_move)
        FILE_OPTION(confirm_copy)
#undef FILE_OPTION
        if (name == "removed_current")
            return app.removed_current;
        if (name == "overlay.visible")
            return app.renderer.overlay_visible;
        if (name == "overlay.caption")
            return app.renderer.overlay.text == window_text(app.hwnd);
        if (name == "overlay.layouts")
            return static_cast<double>(app.renderer.overlay_layout_builds);
        if (name == "overlay.in_canvas") {
            const auto r = app.renderer.overlay_rect;
            return !r.empty() && r.x >= 0 && r.y >= app.top && r.x + r.w <= app.view.width &&
                   r.y + r.h <= app.top + app.view.height;
        }
        if (name == "overlay.family_arial")
            return app.settings.overlay_family == L"Arial";
        if (name == "overlay.native_font")
            return app.renderer.overlay_uses_gdi();
        if (name == "overlay.family_vertical")
            return app.settings.overlay_family.starts_with(L"@");
        if (name == "overlay.family_system")
            return app.settings.overlay_family == L"System";
        RECT outer{};
        GetWindowRect(app.hwnd, &outer);
        if (name == "window.width")
            return outer.right - outer.left;
        if (name == "window.height")
            return outer.bottom - outer.top;
        if (name == "window.minimized")
            return IsIconic(app.hwnd) != FALSE;
        if (name == "incoming.pending")
            return app.forwarded_file.has_value();
        if (name == "window.maximized")
            return IsZoomed(app.hwnd) != FALSE;
        if (name == "window.centered") {
            const auto area = App::monitor_info(app.hwnd).rcWork;
            return std::abs(outer.left + outer.right - area.left - area.right) <= 1 &&
                   std::abs(outer.top + outer.bottom - area.top - area.bottom) <= 1;
        }
        if (name == "window.caption")
            return (GetWindowLongPtrW(app.hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
        if (name == "window.outer")
            return (GetWindowLongPtrW(app.hwnd, GWL_STYLE) & WS_THICKFRAME) != 0;
        if (name == "window.inner")
            return (GetWindowLongPtrW(app.hwnd, GWL_EXSTYLE) & WS_EX_CLIENTEDGE) != 0;
        if (name == "window.topmost")
            return (GetWindowLongPtrW(app.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        if (name == "window.settings_match")
            return app.settings.window_x == outer.left && app.settings.window_y == outer.top &&
                   app.settings.window_width == outer.right - outer.left &&
                   app.settings.window_height == outer.bottom - outer.top;
        if (name == "toolbar.visible")
            return locally_visible(app.toolbar.hwnd);
        if (name == "status.visible")
            return locally_visible(app.status);
        if (name == "toolbar.centered") {
            const auto r = relative_rect(app.toolbar.hwnd, app.toolbar.bar_window());
            RECT area{};
            GetClientRect(app.toolbar.bar_window(), &area);
            return r.left > 0 && std::abs(r.left + r.right - area.right) <= 1;
        }
        if (name == "toolbar.icon_size") {
            const auto list =
                reinterpret_cast<HIMAGELIST>(SendMessageW(app.toolbar.hwnd, TB_GETIMAGELIST, 0, 0));
            int w{}, h{};
            ImageList_GetIconSize(list, &w, &h);
            return MulDiv(w, 96, GetDpiForWindow(app.hwnd));
        }
        if (name == "settings.scroll") {
            const auto page =
                preferences->control(preference_id::ScrollBase + preferences->selected_page());
            return GetScrollPos(page, SB_CTL);
        }
        if (name == "settings.scrollbar") {
            const auto page =
                preferences->control(preference_id::ScrollBase + preferences->selected_page());
            return fully_visible(page);
        }
        if (name.starts_with("text_limit."))
            return SendMessageW(control(name.substr(11)), EM_GETLIMITTEXT, 0, 0);
        if (name.starts_with("number_only."))
            return (GetWindowLongPtrW(control(name.substr(12)), GWL_STYLE) & ES_NUMBER) != 0;
        if (name.starts_with("enabled."))
            return IsWindowEnabled(control(name.substr(8))) != FALSE;
        if (name.starts_with("visible."))
            return locally_visible(control(name.substr(8)));
        if (name == "view.left")
            return app.view.left();
        if (name == "view.actual_fits")
            return app.image && app.view.scale==1 && app.view.width>=app.image->width &&
                   app.view.height>=app.image->height;
        if (name == "scroll.horizontal")
            return locally_visible(app.canvas_horizontal);
        if (name == "scroll.vertical")
            return locally_visible(app.canvas_vertical);
        if (name == "scroll.x")
            return GetScrollPos(app.canvas_horizontal, SB_CTL);
        if (name == "scroll.y")
            return GetScrollPos(app.canvas_vertical, SB_CTL);
        if (name == "scroll.left")
            return std::abs(app.view.left()) < 1;
        if (name == "scroll.top")
            return std::abs(app.view.top()) < 1;
        if (name == "scroll.right")
            return std::abs(app.view.left() + app.view.canvas_w * app.view.scale - app.view.width) < 1;
        if (name == "scroll.bottom")
            return std::abs(app.view.top() + app.view.canvas_h * app.view.scale - app.view.height) < 1;
        if (name == "scroll.no_overlap") {
            const auto h = relative_rect(app.canvas_horizontal, app.hwnd);
            const auto v = relative_rect(app.canvas_vertical, app.hwnd);
            const auto p = relative_rect(app.playback.hwnd, app.hwnd);
            return (!app.canvas_bars.horizontal ||
                    (h.top >= app.top && h.left == app.left && h.right == app.left+static_cast<LONG>(app.view.width) &&
                     (!app.image->animated() || h.bottom <= p.top))) &&
                   (!app.canvas_bars.vertical ||
                    (v.top == app.top && v.left == app.left+static_cast<LONG>(app.view.width) &&
                     (!app.image->animated() || v.bottom <= p.top)));
        }
        if(name=="editor.mode")return app.editor_mode;
        if(name=="editor.left")return app.left;
        if (name == "view.top")
            return app.view.top();
        if (name == "view.centered")
            return app.view.centered;
        if (name == "view.manual")
            return app.view.manual;
        if (name == "renderer.filter")
            return app.renderer.filter(app.view);
        if (name == "renderer.sample_bytes")
            return static_cast<double>(app.renderer.sample_bytes());
        if (name == "renderer.source_bytes")
            return static_cast<double>(app.renderer.source_bytes());
        if (name == "renderer.resizing")
            return app.renderer.resizing;
        if (name == "loader.cache_bytes")
            return static_cast<double>(app.loader->stats().cache_bytes);
        if (name == "loader.cache_limit")
            return static_cast<double>(app.loader->stats().cache_limit);
        if (name == "loader.cache_hits")
            return static_cast<double>(app.loader->stats().cache_hits);
        if (name == "loader.cache_budget")
            return app.loader->stats().cache_bytes <= app.loader->stats().cache_limit;
        if (name == "settings.cache_mib")
            return app.settings.cache_mib;
        if (name == "settings.prefetch_enabled")
            return app.settings.prefetch_enabled;
        if (name == "renderer.software_target")
            return app.renderer.software_target();
        if (name == "renderer.sample_current")
            return app.renderer.sample_current(app.image.get(), app.player.frame, app.view);
        if (name == "renderer.sample_budget")
            return app.renderer.sample_bytes() <= 64u * 1024 * 1024 &&
                   app.renderer.sample_workspace_bytes() <= 35u * 1024 * 1024;
        if (name == "full.bounds") {
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(app.hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
            RECT actual{};
            GetWindowRect(app.hwnd, &actual);
            const RECT expected = app.settings.full_hide_taskbar ? monitor.rcMonitor : monitor.rcWork;
            return EqualRect(&actual, &expected) != FALSE;
        }
        if (name == "pan_x")
            return app.view.pan_x;
        if (name == "pan_y")
            return app.view.pan_y;
        if (name == "selection_w")
            return app.selection.w;
        if (name == "selection_h")
            return app.selection.h;
        if (name == "pointer_buttons")
            return app.mouse_buttons;
        if (name == "drag_active")
            return app.active_drag_button != 0;
        if (name == "popup_count")
            return popup_count;
        if (name == "window_move.requests")
            return window_move_requests;
        if (name == "window_move.coordinates")
            return window_move_coordinates;
        if (name == "export_count")
            return export_count;
        if (name == "export_temporary")
            return app.image && !same_path(exported, app.image->path);
        if (name == "mouse_assign_enabled")
            return IsWindowEnabled(control("MouseAssign")) != FALSE;
        if (name == "mouse.wheel_scroll")
            return app.settings.mouse.wheel_scroll;
        if (name == "mouse.drag_scroll")
            return app.settings.mouse.drag_scroll;
        if (name == "mouse.drag_zoom")
            return app.settings.mouse.drag_zoom;
        if (name == "mouse.drag_axis")
            return app.settings.mouse.drag_axis;
        if (name == "mouse.drag_threshold")
            return app.settings.mouse.drag_threshold;
        if (name == "mouse.hover_margin")
            return app.settings.mouse.hover_margin;
        if (name == "mouse.hover_min")
            return app.settings.mouse.hover_min;
        if (name == "mouse.ignore_activation")
            return app.settings.mouse.ignore_activation;
        if (name == "mouse.slow_wheel")
            return app.settings.mouse.slow_wheel;
        if (name == "frame")
            return static_cast<double>(app.player.frame);
        if (name == "frame.duration")
            return app.image ? static_cast<double>(app.image->frames.at(app.player.frame).duration_ms) : 0;
        if (name == "file_loops")
            return app.image ? app.image->file_loops : 0;
        if (name == "animation.honor_file_loops")
            return app.player.honor_file_loops;
        if (name == "animation.ended")
            return app.player.ended;
        if (name == "frames")
            return app.image ? static_cast<double>(app.image->frames.size()) : 0;
        if (name == "list.count")
            return static_cast<double>(app.files.size());
        if (name == "width")
            return app.image ? app.image->width : 0;
        if(name=="dpi_x")return app.current_metadata().dpi_x;
        if(name=="dpi_y")return app.current_metadata().dpi_y;
        if (name == "height")
            return app.image ? app.image->height : 0;
        if (name == "playing")
            return app.player.playing;
        if (name == "loop")
            return app.player.loop;
        if (name == "modified")
            return app.modified;
        if (name == "comic")
            return app.comic;
        if (name == "undo_available")
            return app.undo_image != nullptr;
        if (name == "index")
            return static_cast<double>(app.index);
        if (name == "scale")
            return app.view.scale;
        if (name == "settings_open")
            return preferences && IsWindow(preferences->window());
        if (name == "tabs")
            return preferences ? int(PreferencePage::Count) : 0;
        if (name == "key_functions")
            return ListView_GetItemCount(control("KeyList"));
        if (name == "key_slots")
            return static_cast<double>(SendMessageW(control("KeySlots"), LB_GETCOUNT, 0, 0));
        if (name == "key_capture_focused")
            return GetFocus() == control("KeyCapture");
        if (name == "error_count")
            return static_cast<double>(errors.size());
        if (name == "apply_enabled")
            return preferences && IsWindowEnabled(preferences->control(preference_id::ApplyButton));
        if (name == "renderer.checker")
            return app.renderer.checkerboard;
        if (name == "renderer.nearest")
            return app.renderer.nearest;
        if (name == "renderer.background")
            return app.renderer.background;
        if (name == "renderer.cell")
            return app.renderer.checker_size;
        if (name == "renderer.background_tiled")
            return app.renderer.background_tiled;
        if (name == "renderer.background_pattern")
            return app.renderer.background_pattern;
        if (name == "renderer.background_first")
            return app.renderer.background_first;
        if (name == "renderer.background_second")
            return app.renderer.background_second;
        if (name == "full")
            return app.full;
        if (name == "player.visible")
            return locally_visible(app.playback.hwnd);
        if (name == "layout.no_overlap") {
            RECT area{};
            GetClientRect(app.hwnd, &area);
            const auto player = relative_rect(app.playback.hwnd, app.hwnd);
            const auto status = relative_rect(app.status, app.hwnd);
            return app.top >= 0 &&
                   app.top + app.bottom + MulDiv(64, GetDpiForWindow(app.hwnd), 96) <= area.bottom &&
                   (!locally_visible(app.playback.hwnd) ||
                    (player.top >= app.top && player.bottom <= area.bottom &&
                     (!locally_visible(app.status) || player.bottom <= status.top))) &&
                   (!locally_visible(app.status) || status.top >= app.top);
        }
        if (name == "toolbar.no_clip") {
            RECT area{};
            GetClientRect(app.toolbar.hwnd, &area);
            for (int i = 0; i < SendMessageW(app.toolbar.hwnd, TB_BUTTONCOUNT, 0, 0); ++i) {
                RECT r{};
                SendMessageW(app.toolbar.hwnd, TB_GETITEMRECT, i, reinterpret_cast<LPARAM>(&r));
                if (r.left < 0 || r.top < 0 || r.right > area.right || r.bottom > area.bottom)
                    return false;
            }
            return true;
        }
        if (name == "player.loop_checked")
            return SendMessageW(app.playback.toolbar.hwnd, TB_ISBUTTONCHECKED, Loop, 0) != FALSE;
        if (name == "player.buttons")
            return static_cast<double>(SendMessageW(app.playback.toolbar.hwnd, TB_BUTTONCOUNT, 0, 0));
        if (name == "player.pause_icon") {
            TBBUTTONINFOW info{sizeof(info)};
            info.dwMask = TBIF_IMAGE;
            SendMessageW(app.playback.toolbar.hwnd, TB_GETBUTTONINFOW, Play, reinterpret_cast<LPARAM>(&info));
            auto list =
                reinterpret_cast<HIMAGELIST>(SendMessageW(app.playback.toolbar.hwnd, TB_GETIMAGELIST, 0, 0));
            return info.iImage == ImageList_GetImageCount(list) - 1;
        }
        if (name == "player.bottom") {
            RECT client{};
            GetClientRect(app.hwnd, &client);
            auto p = relative_rect(app.playback.hwnd, app.hwnd);
            const int status = app.status_shown ? int(relative_rect(app.status,app.hwnd).bottom-relative_rect(app.status,app.hwnd).top) : 0;
            return p.bottom == client.bottom - status && p.top >= app.top &&
                   std::abs(
                       (app.top + app.view.height + (app.canvas_bars.horizontal ? app.canvas_thickness : 0)) -
                       p.top) < 1;
        }
        if (name == "player.no_overlap") {
            auto parent = app.playback.hwnd;
            auto a = relative_rect(app.playback.toolbar.hwnd, parent), b = relative_rect(app.slider, parent),
                 c = relative_rect(app.playback.counter, parent);
            RECT area{};
            GetClientRect(parent, &area);
            return !overlaps(a, b) && !overlaps(b, c) && !overlaps(a, c) && a.left >= 0 && b.left >= 0 &&
                   c.left >= 0 && a.right <= area.right && b.right <= area.right && c.right <= area.right &&
                   a.top >= 0 && b.top >= 0 && c.top >= 0 && a.bottom <= area.bottom &&
                   b.bottom <= area.bottom && c.bottom <= area.bottom;
        }
#define S(x)                                                                                                 \
    if (name == "settings." #x)                                                                              \
        return app.settings.x;
        S(allow_multiple)
        S(activate_reused)
        S(activate_dropped)
        S(reopen_last)
        S(disable_ime)
        S(allow_physical_overflow)
        S(cut_transparent)
        S(cut_color)
        S(scrollbars)
        S(preserve_alpha)
        S(bmp_transparency)
        S(gif_transparency)
        S(png_transparency)
        S(gif_background)
        S(png_background)
        S(gif_animation)
        S(bitfield_to24)
        S(gif_speed)
        S(view_rotation)
        S(comic_rotate_before)
        S(comic_single_wide)
        S(animation_file_loops)
        S(exif_orientation)
        S(transparency_background)
        S(system_files)
        S(protected_files)
        S(overlay_full)
        S(overlay_window)
        S(overlay_animation)
        S(overlay_position)
        S(overlay_background)
        S(overlay_points)
        S(overlay_weight)
        S(overlay_charset)
        S(overlay_italic)
        S(overlay_underline)
        S(overlay_strikeout)
        S(overlay_foreground)
        S(overlay_backcolor)
        S(full_scrollbars)
        S(undo_enabled)
        S(window_center)
        S(window_toolbar_min)
        S(restore_maximized)
        S(restore_fullscreen)
        S(caption)
        S(outer_frame)
        S(inner_frame)
        S(full_topmost)
        S(full_toolbar)
        S(full_status)
        S(full_menu)
        S(toolbar_center)
        S(toolbar_line)
        S(toolbar_large)
        S(toolbar_auto)
        S(status_auto)
        S(startup_position)
        S(startup_state)
        S(window_x)
        S(window_y)
        S(window_width)
        S(window_height)
        S(last_state)
        S(last_window_valid)
        S(title_mode)
        S(display_mode)
        S(full_display_mode)
        S(full_initial_fit)
        S(center_image)
        S(fit_max)
        S(fit_simple)
        S(display_down)
        S(display_up)
        S(full_down)
        S(full_up)
        S(zoom_reduce)
        S(zoom_enlarge)
        S(drag_simple)
        S(zoom_simple)
        S(zoom_mode)
        S(mask_swap)
        S(glass_remainder)
        S(mask_negative_red) S(mask_negative_green) S(mask_negative_blue)
        S(mask_xor_red) S(mask_xor_green) S(mask_xor_blue)
        S(mask_q0_vertical) S(mask_q0_horizontal) S(mask_q0_negative)
        S(mask_q0_xor) S(mask_q0_flip_vertical) S(mask_q0_flip_horizontal)
        S(fl_negative)
        S(rainbow_size) S(rainbow_preview)
        S(cp_preview)
        S(palette_preview)
        S(color_depth_dither)
        S(color_adjust_unified) S(color_adjust_hue) S(color_adjust_saturation) S(color_adjust_lightness) S(color_adjust_gray)
        S(zoom_below)
        S(zoom_above)
        S(full_hide_taskbar)
        S(full_hide_wait)
        S(remember_history)
        S(paste_new)
        S(grid)
        S(checker)
        S(nearest)
        S(resize_keep_aspect) S(resize_smooth) S(resize_down) S(resize_up) S(resize_sharpen)
        S(resize_resample) S(resize_print_unit) S(resize_density_unit) S(canvas_unit)
        S(info_auto_colors)
        S(canvas_keep_aspect) S(canvas_anchor) S(canvas_transparent) S(canvas_color) S(canvas_alpha)
        S(background)
        S(background_tiled)
        S(background_pattern)
        S(background_first)
        S(background_second)
        S(checker_light)
        S(checker_dark)
        S(checker_size)
        S(topmost)
        S(title_path) S(initial_fit) S(comic_rtl) S(comic_mode) S(comic_down) S(comic_up) S(comic_fill)
            S(keep_zoom) S(zoom_step) S(full_hide_cursor) S(full_nearest) S(toolbar) S(status) S(menu) S(sort)
                S(natural) S(reverse) S(list_loop) S(skip_count) S(end_action) S(folder_history)
                    S(previous_folder_first) S(adjacent_previous_first) S(parent_first) S(boundary_sound)
                        S(comic_toolbar_reverse) S(comic_jump_reverse) S(jump_preview) S(jump_quality)
                            S(confirm_overwrite) S(confirm_delete) S(hidden) S(animation_autoplay)
                                S(animation_loop) S(glass_size) S(wheel) S(middle) S(interval) S(slide_random) S(slide_fullscreen) S(slide_loop) S(slide_next_folder) S(slide_subtree)
#undef S
                                    throw std::runtime_error("Unknown state: " + name);
    }
    void execute(const std::vector<std::string> &s) {
        if(s[0]=="resize_benchmark"){
            using Clock=std::chrono::steady_clock;
            const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
            std::ofstream report(output_path(s.at(1)));report<<"{\"method\":\"Native WM_SIZE (SetWindowPos) + explicit WM_PAINT on private desktop; 2 warm-up and 20 measured sizes per case\",\"cases\":[";
            auto large_image=std::make_shared<Image>(new_image(3840,2160));
            for(unsigned y=0;y<large_image->height;++y)for(unsigned x=0;x<large_image->width;++x)large_image->frames[0].pixels[size_t(y)*large_image->width+x]={uint8_t(x*7+y),uint8_t(x+y*3),uint8_t(x*3+y*5),uint8_t((x+y)%256)};
            const auto small_image=std::make_shared<Image>(new_image(512,384));
            app.settings.confirm_unsaved=false;app.command(DisplayUnchanged);
            struct Case{const char *name;std::shared_ptr<Image> image;int method;bool editor,actual;};
            const Case cases[]{{"empty",{},DisplayLinear,false,false},{"fit-512-fast",small_image,DisplayLinear,false,false},
                {"fit-4k-fast",large_image,DisplayLinear,false,false},{"fit-4k-lanczos3",large_image,DisplayLanczos3,false,false},
                {"editor-4k-lanczos3",large_image,DisplayLanczos3,true,false},{"actual-4k",large_image,DisplayLanczos3,false,true}};
            bool comma=false;
            for(const auto &c:cases){
                app.set_edit_mode(false);if(c.image)app.replace(c.image,false,true);else app.command(CloseImage);app.clear_undo();
                app.settings.display_down=app.settings.display_up=c.method;app.render_settings();
                if(c.editor)app.command(ToolPen);app.command(c.actual?Actual:FitImage);
                SetWindowPos(app.hwnd,nullptr,0,0,900,650,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);app.update();
                app.message(WM_ENTERSIZEMOVE,0,0);if(comma)report<<',';comma=true;
                report<<"{\"name\":"<<quote(c.name)<<",\"frames\":[";
                for(int n=-2;n<20;++n){const int k=n+2;DrawMetrics metrics;app.renderer.trace=&metrics;
                    const auto begin=Clock::now();SetWindowPos(app.hwnd,nullptr,0,0,900+(k*29)%240,650+(k*17)%160,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
                    const auto arranged=Clock::now();SendMessageW(app.hwnd,WM_PAINT,0,0);const auto end=Clock::now();app.renderer.trace=nullptr;
                    require(app.smoke_error.empty(),"Resize benchmark failed to render");
                    if(n>=0)report<<(n?",":"")<<"{\"total_ms\":"<<ms(begin,end)<<",\"layout_ms\":"<<ms(begin,arranged)<<",\"paint_ms\":"<<ms(arranged,end)
                        <<",\"sample_ms\":"<<metrics.sampling_ms<<",\"upload_ms\":"<<metrics.upload_ms<<",\"scene_ms\":"<<metrics.scene_ms<<'}';
                }
                app.message(WM_EXITSIZEMOVE,0,0);const auto begin=Clock::now();SendMessageW(app.hwnd,WM_PAINT,0,0);
                report<<"],\"settled_ms\":"<<ms(begin,Clock::now())<<",\"toolbar_height\":"<<app.toolbar.height()<<",\"toolbar_item_bottom\":";
                LONG bottom{};for(int n=0;n<SendMessageW(app.toolbar.hwnd,TB_BUTTONCOUNT,0,0);++n){RECT r{};SendMessageW(app.toolbar.hwnd,TB_GETITEMRECT,n,reinterpret_cast<LPARAM>(&r));bottom=std::max(bottom,r.bottom);}report<<bottom<<'}';report.flush();
            }
            report<<"]}\n";return;
        }
        if(s[0]=="rect_dialog"){SendMessageW(app.hwnd,WM_COMMAND,SelectNumbers,0);return;}
        if(s[0].starts_with("rect_")||(selection_rect_dialog&&s[0].starts_with("prompt_"))) {
            require(selection_rect_dialog&&selection_rect_dialog->ready(),"Actual numeric selection dialog is not open");own(selection_rect_dialog->window());
            const std::map<std::string,int> ids{{"Left",selection_rect_control::Left},{"Top",selection_rect_control::Top},{"Right",selection_rect_control::Right},{"Bottom",selection_rect_control::Bottom},{"Width",selection_rect_control::Width},{"Height",selection_rect_control::Height},{"OK",IDOK},{"Cancel",IDCANCEL}};
            if(s[0]=="rect_snapshot"||s[0]=="prompt_snapshot")capture(selection_rect_dialog->window(),output_path(s.at(1)));
            else if(s[0]=="prompt_accept"||s[0]=="prompt_cancel")SendMessageW(selection_rect_dialog->control(s[0]=="prompt_accept"?IDOK:IDCANCEL),BM_CLICK,0,0);
            else if(s[0]=="prompt_text") {
                // The existing four-value DSL now operates real coordinate/size controls.
                std::wistringstream in(widen(s.at(1)));Rect r;wchar_t a{},b{},c{};
                require(bool(in>>r.x>>a>>r.y>>b>>r.w>>c>>r.h)&&a==L','&&b==L','&&c==L','&&(in>>std::ws).eof(),"Invalid legacy selection text");
                r=intersect(r,{0,0,int(app.image->width),int(app.image->height)});require(!r.empty(),"Legacy selection text must identify a nonempty rectangle");
                for(const auto [key,value]:{std::pair{selection_rect_control::Left,r.x},{selection_rect_control::Top,r.y},{selection_rect_control::Width,r.w},{selection_rect_control::Height,r.h}}) {
                    const auto h=selection_rect_dialog->control(key);own(h);require(fully_visible(h)&&IsWindowEnabled(h),"Numeric selection field unavailable");SetWindowTextW(h,std::to_wstring(value).c_str());
                }
                const auto actual=selection_rect_dialog->bounds();require(actual&&actual->x==r.x&&actual->y==r.y&&actual->w==r.w&&actual->h==r.h,"Legacy selection DSL did not reach requested native fields");
            } else {
                const int key=ids.at(s.at(1));const auto h=selection_rect_dialog->control(key);own(h);require(fully_visible(h)&&IsWindowEnabled(h),"Numeric selection control unavailable");
                if(s[0]=="rect_set")SetWindowTextW(h,s.at(2)=="empty"?L"":widen(s.at(2)).c_str());
                else if(s[0]=="rect_expect")require(window_text(h)==widen(s.at(2)),"Numeric selection field text differs");
                else if(s[0]=="rect_click")SendMessageW(h,BM_CLICK,0,0);
                else if(s[0]=="rect_spin") {
                    require(key>=selection_rect_control::Left&&key<=selection_rect_control::Height,"Numeric selection spinner field required");
                    const int n=key-selection_rect_control::Left;const auto spin=selection_rect_dialog->control(selection_rect_control::Spin+n);own(spin);RECT r{};require(GetClientRect(spin,&r),"Spinner bounds unavailable");require(s.at(2)=="increase"||s.at(2)=="decrease","Invalid spinner direction");const bool increase=s.at(2)=="increase";
                    native_click(spin,n%2?r.right/2:increase?3*r.right/4:r.right/4,n%2?(increase?3*r.bottom/4:r.bottom/4):r.bottom/2);
                } else throw std::runtime_error("Unknown numeric selection operation");
            }
            return;
        }

        if(s[0]=="expect_grid_saved") {
            Settings saved;saved.load(app.config);require(saved.selection_custom==app.settings.selection_custom&&saved.custom_selection_units==app.settings.custom_selection_units&&saved.selection_units()==app.settings.selection_units(),"Persisted selection units differ");return;
        }
        if(s[0]=="grid_dialog"){SendMessageW(app.hwnd,WM_COMMAND,Grid,0);return;}
        if(s[0].starts_with("grid_")) {
            require(selection_units_dialog&&selection_units_dialog->ready(),"Actual selection unit dialog is not open");own(selection_units_dialog->window());
            const std::map<std::string,int> ids{{"StartX",selection_control::StartX},{"StartY",selection_control::StartY},{"SizeX",selection_control::SizeX},{"SizeY",selection_control::SizeY},{"OK",IDOK},{"Cancel",IDCANCEL}};
            if(s[0]=="grid_snapshot")capture(selection_units_dialog->window(),output_path(s.at(1)));
            else {
                int key=ids.at(s.at(1));
                if(s[0]=="grid_quick"){const int n=std::stoi(s.at(2));require(n==1||n==8||n==16,"Invalid grid shortcut");key=selection_control::Quick+(key-selection_control::StartX)*3+(n==1?0:n==8?1:2);}
                const auto h=selection_units_dialog->control(key);own(h);require(fully_visible(h)&&IsWindowEnabled(h),"Grid control unavailable");
                if(s[0]=="grid_set")SetWindowTextW(h,widen(s.at(2)).c_str());
                else if(s[0]=="grid_expect")require(window_text(h)==widen(s.at(2)),"Grid field text differs");
                else if(s[0]=="grid_click"||s[0]=="grid_quick")SendMessageW(h,BM_CLICK,0,0);
                else throw std::runtime_error("Unknown grid operation");
            }
            return;
        }
        if(s[0]=="editor_set"||s[0]=="editor_click"||s[0]=="editor_expect") {
            const std::map<std::string,int> ids{{"Outline",editor_id::Outline},{"OutlineWidth",editor_id::OutlineWidth},{"Shadow",editor_id::Shadow},{"ShadowOffset",editor_id::ShadowOffset},{"ShadowBlur",editor_id::ShadowBlur},{"ShadowOpacity",editor_id::ShadowOpacity},{"Details",editor_id::Details},{"Width",editor_id::Width},{"Opacity",editor_id::Opacity},{"Font",editor_id::Font},{"TextSize",editor_id::TextSize},{"Text",editor_id::Text},
                {"X",editor_id::X},{"Y",editor_id::Y},{"W",editor_id::W},{"H",editor_id::H},{"Angle",editor_id::Angle},{"Scale",editor_id::Scale},{"Anchor",editor_id::Anchor},
                {"Apply",editor_id::Apply},{"Cancel",editor_id::Cancel},{"Name",editor_id::Name},{"Format",editor_id::Format}};
            const auto id=ids.at(s.at(1));const auto h=app.editor_chrome.control(id);own(h);
            if(s[0]=="editor_click")SendMessageW(h,BM_CLICK,0,0);
            else if(s[0]=="editor_expect")require(window_text(h)==widen(s.at(2)),"Editor field differs");
            else {if(id==editor_id::Anchor||id==editor_id::Format){SendMessageW(h,CB_SETCURSEL,std::stoi(s.at(2)),0);SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(h));}
                else {auto value=widen(s.at(2));for(size_t at=0;(at=value.find(L"\\n",at))!=std::wstring::npos;at+=2)value.replace(at,2,L"\r\n");
                    SetWindowTextW(h,value.c_str());if(id!=editor_id::Name&&id!=editor_id::Text)SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(id,EN_KILLFOCUS),reinterpret_cast<LPARAM>(h));}}
            return;
        }
        if(s[0]=="editor_layout") {
            const auto host=app.editor_chrome.top;own(host);RECT area{};GetClientRect(host,&area);std::vector<RECT> occupied;
            for(auto h=GetWindow(host,GW_CHILD);h;h=GetWindow(h,GW_HWNDNEXT))if(locally_visible(h)){
                RECT bounds{};GetWindowRect(h,&bounds);MapWindowPoints(nullptr,host,reinterpret_cast<POINT *>(&bounds),2);
                require(bounds.left>=0&&bounds.top>=0&&bounds.right<=area.right&&bounds.bottom<=area.bottom,"Editor control extends outside its panel");
                for(const auto &other:occupied){RECT overlap{};require(!IntersectRect(&overlap,&bounds,&other),"Editor controls overlap");}occupied.push_back(bounds);
            }return;
        }
        if(s[0]=="paint_pixels") {
            const auto at=[&](int x,int y){return POINT{LONG(app.left+std::floor(app.view.left())+x*app.view.scale),LONG(app.top+std::floor(app.view.top())+y*app.view.scale)};};
            const auto a=at(std::stoi(s.at(1)),std::stoi(s.at(2))),b=at(std::stoi(s.at(3)),std::stoi(s.at(4)));
            own(app.hwnd);SendMessageW(app.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(a.x,a.y));SendMessageW(app.hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(b.x,b.y));SendMessageW(app.hwnd,WM_LBUTTONUP,0,MAKELPARAM(b.x,b.y));return;
        }
        if(s[0]=="select_pixels") {
            require(app.image&&app.selection_active()&&app.view.scale>=1,"Pixel selection needs an image, selection mode and Actual scale or higher");
            const auto at=[&](int x,int y){return POINT{LONG(app.left+std::floor((std::abs(app.view.scale-1)<1e-9?std::floor(app.view.left()):app.view.left())+(x+.5)*app.view.scale)),LONG(app.top+std::floor((std::abs(app.view.scale-1)<1e-9?std::floor(app.view.top()):app.view.top())+(y+.5)*app.view.scale))};};
            const auto start=at(std::stoi(s.at(1)),std::stoi(s.at(2))),end=at(std::stoi(s.at(3)),std::stoi(s.at(4)));
            own(app.hwnd);SendMessageW(app.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(start.x,start.y));SendMessageW(app.hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));SendMessageW(app.hwnd,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));return;
        }
        if(s[0]=="expect_selection_rect") {
            const auto &r=app.selection;require(r.x==std::stoi(s.at(1))&&r.y==std::stoi(s.at(2))&&r.w==std::stoi(s.at(3))&&r.h==std::stoi(s.at(4)),"Actual selection rectangle differs");return;
        }

        if(s.at(0)=="print_image"){SendMessageW(app.hwnd,WM_COMMAND,Print,0);return;}
        if(s.at(0).starts_with("print_")) {
            require(printing&&IsWindow(printing->window()),"Actual print dialog is not open");own(printing->window());
            const std::map<std::string,int> ids{{"Left",print_control::Left},{"Top",print_control::Top},{"Right",print_control::Right},{"Bottom",print_control::Bottom},
                {"Width",print_control::Width},{"Height",print_control::Height},{"Sizing",print_control::Sizing},{"Unit",print_control::Unit},{"Density",print_control::Density},
                {"Rotation",print_control::Rotation},{"Copies",print_control::Copies},{"Aspect",print_control::Aspect},{"Setup",print_control::Setup},{"OK",IDOK},{"Cancel",IDCANCEL}};
            if(s[0]=="print_snapshot")capture(printing->window(),output_path(s.at(1)));
            else {
                const int key=s[0]=="print_position"?print_control::Position+std::stoi(s.at(1)):ids.at(s.at(1));const auto h=printing->control(key);
                own(h);require(fully_visible(h)&&IsWindowEnabled(h),"Print control unavailable");
                if(s[0]=="print_set")SetWindowTextW(h,widen(s.at(2)).c_str());
                else if(s[0]=="print_select"){SendMessageW(h,CB_SETCURSEL,std::stoi(s.at(2)),0);SendMessageW(printing->window(),WM_COMMAND,MAKEWPARAM(key,CBN_SELCHANGE),LPARAM(h));}
                else if(s[0]=="print_click"||s[0]=="print_position")SendMessageW(h,BM_CLICK,0,0);
                else throw std::runtime_error("Unknown print operation");
            }
            return;
        }

        if(s.at(0)=="image_info") {
            native_info=true;struct Reset {bool &value;~Reset(){value=false;}} reset{native_info};
            SendMessageW(app.hwnd,WM_COMMAND,Info,0);return;
        }
        if (s.at(0).starts_with("resize_")||s.at(0).starts_with("canvas_")) {
            auto dialog=s[0].starts_with("canvas_")?canvas:resize;
            require(dialog && dialog->ready(), "Actual geometry dialog is not open");
            own(dialog->window());
            const std::map<std::string,int> ids{{"Width",resize_control::Width},{"Height",resize_control::Height},
                {"WidthPercent",resize_control::WidthPercent},{"HeightPercent",resize_control::HeightPercent},
                {"KeepAspect",resize_control::KeepAspect},{"Smooth",resize_control::Smooth},
                {"Down",resize_control::Down},{"Up",resize_control::Up},{"Sharpen",resize_control::Sharpen},
                {"DpiX",resize_control::DpiX},{"DpiY",resize_control::DpiY},{"PrintWidth",resize_control::PrintWidth},
                {"PrintHeight",resize_control::PrintHeight},{"PrintUnit",resize_control::PrintUnit},{"DpiUnit",resize_control::DpiUnit},
                {"Resample",resize_control::Resample},{"Unit",resize_control::CanvasUnit},
                {"Anchor",resize_control::Anchor},{"Transparent",resize_control::Transparent},{"Red",resize_control::Red},
                {"Green",resize_control::Green},{"Blue",resize_control::Blue},{"Alpha",resize_control::Alpha},{"Color",resize_control::Color},
                {"Half",resize_control::Half},{"Double",resize_control::Double},{"OK",IDOK},{"Cancel",IDCANCEL}};
            if(s[0]=="resize_snapshot"||s[0]=="canvas_snapshot")capture(dialog->window(),output_path(s.at(1)));
            else {
                auto key=ids.at(s.at(1));
                if(key==resize_control::Anchor){const auto at=std::stoi(s.at(2));require(at>=0&&at<9,"Invalid canvas anchor");key+=at;}
                const auto h=dialog->control(key);own(h);
                if(s[0]=="resize_click"||s[0]=="canvas_click"||(key>=resize_control::Anchor&&key<resize_control::Anchor+9))SendMessageW(h,BM_CLICK,0,0);
                else if(s[0]=="resize_set"||s[0]=="canvas_set") {
                    require(IsWindowEnabled(h), "Resize control is disabled");
                    if(key==resize_control::Down||key==resize_control::Up||key==resize_control::PrintUnit||key==resize_control::DpiUnit||key==resize_control::CanvasUnit) {
                        const auto selected=std::stoi(s.at(2));require(selected>=0&&selected<SendMessageW(h,CB_GETCOUNT,0,0),"Invalid geometry choice");
                        require(SendMessageW(h,CB_SETCURSEL,selected,0)!=CB_ERR,"Cannot select resize filter");
                        SendMessageW(dialog->window(),WM_COMMAND,MAKEWPARAM(key,CBN_SELCHANGE),reinterpret_cast<LPARAM>(h));
                    } else if(key==resize_control::KeepAspect||key==resize_control::Smooth||key==resize_control::Sharpen||key==resize_control::Transparent||key==resize_control::Resample) {
                        require(s.at(2)=="true"||s.at(2)=="false", "Invalid resize checkbox value");
                        const bool checked=SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED;
                        if(checked!=(s.at(2)=="true"))SendMessageW(h,BM_CLICK,0,0);
                    } else {
                        require((key>=resize_control::Width && key<=resize_control::HeightPercent)||(key>=resize_control::Red&&key<=resize_control::Alpha)||(key>=resize_control::DpiX&&key<=resize_control::PrintHeight), "Geometry field is not editable");
                        SetWindowTextW(h,s.at(2)=="empty"?L"":widen(s.at(2)).c_str());
                    }
                } else throw std::runtime_error("Unknown resize operation");
            }
            return;
        }
        if(s.at(0).starts_with("info_")) {
            require(information&&information->ready(),"Actual image info dialog is not open");own(information->window());
            if(s[0]=="info_close")SendMessageW(information->control(IDOK),BM_CLICK,0,0);
            else if(s[0]=="info_snapshot")capture(information->window(),output_path(s.at(1)));
            else if(s[0]=="info_expect") {
                const auto expected=s.at(1).find("${")!=std::string::npos?std::filesystem::absolute(path(s.at(1))).lexically_normal().wstring():widen(s.at(1));
                if(information->text().find(expected)==std::wstring::npos)throw std::runtime_error("Image information text differs: "+utf8(information->text()));
            } else if(s[0]=="info_set") {
                require(s.at(1)=="Auto"&&(s.at(2)=="true"||s.at(2)=="false"),"Invalid info checkbox");const auto h=information->control(image_info_control::Auto);own(h);require(IsWindowEnabled(h),"Color count is disabled for animations");
                if((SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED)!=(s.at(2)=="true"))SendMessageW(h,BM_CLICK,0,0);
            } else if(s[0]=="info_copy") {
                const std::map<std::string,int> fields{{"Name",0},{"Folder",1},{"FullPath",2}};const auto h=information->control(image_info_control::Field+fields.at(s.at(1)));own(h);require(fully_visible(h),"Info copy field is clipped");SetFocus(h);
                {
                    struct Restore {BYTE saved[256]{};Restore(){require(GetKeyboardState(saved),"Cannot read private keyboard state");}~Restore(){SetKeyboardState(saved);}} restore;
                    BYTE keys[256]{};for(size_t n=0;n<256;++n)keys[n]=restore.saved[n];keys[VK_CONTROL]|=0x80;require(SetKeyboardState(keys),"Cannot set private keyboard state");SendMessageW(h,WM_KEYDOWN,'A',0);
                    DWORD first{},last{};SendMessageW(h,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));require(first==0&&last==window_text(h).size(),"Ctrl+A did not select the complete info field");
                }
                SendMessageW(h,WM_COPY,0,0);
            } else if(s[0]=="info_wait")require(!information->pending(),"Color count is still pending");
            else throw std::runtime_error("Unknown image info operation");return;
        }
        if (s.at(0).starts_with("about_")) {
            const auto window = about_window();
            if (s[0] == "about_close"||s[0]=="info_close") {
                // Modern MessageBox implementations need not expose IDOK as a direct child.
                // This MB_OK dialog has exactly one visible acknowledgement button.
                struct Search { HWND button{}; unsigned count{}; } search;
                EnumChildWindows(window, [](HWND child, LPARAM data) -> BOOL {
                    auto &search = *reinterpret_cast<Search *>(data);
                    wchar_t cls[80]{};
                    GetClassNameW(child, cls, 80);
                    if (_wcsicmp(cls, L"Button") == 0 && locally_visible(child)) {
                        search.button = child;
                        ++search.count;
                    }
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&search));
                require(search.count == 1, "Expected one actual About acknowledgement button");
                own(search.button);
                require(IsWindowEnabled(search.button) && fully_visible(search.button), "About button is unavailable");
                SendMessageW(search.button, BM_CLICK, 0, 0);
            } else if (s[0] == "about_snapshot"||s[0]=="info_snapshot") {
                capture(window, output_path(s.at(1)));
            } else if (s[0] == "about_expect"||s[0]=="info_expect") {
                struct Text { std::wstring combined; } text;
                EnumChildWindows(window, [](HWND child, LPARAM data) -> BOOL {
                    auto &text = *reinterpret_cast<Text *>(data);
                    text.combined += window_text(child) + L"\n";
                    return TRUE;
                }, reinterpret_cast<LPARAM>(&text));
                require(text.combined.find(widen(s.at(1))) != std::wstring::npos, "About text differs");
            } else
                throw std::runtime_error("Unknown About operation");
            return;
        }
        const auto &op = s.at(0);
        if (op == "wallpaper_fail_next_apply") { wallpaper_fail_apply = true; return; }
        if (op == "popup_choice") { popup_choice = command_ids.at(s.at(1)); return; }
        if (op == "wallpaper_expect") {
            const auto mode = std::stoi(s.at(1)); require(mode >= 0 && mode < 5 && app.image, "Invalid wallpaper assertion");
            require(wallpaper_state.values == wallpaper_values(static_cast<WallpaperMode>(mode)), "Wallpaper placement differs");
            require(wallpaper_state.image.parent_path() == wallpaper_directory(app.config), "Wallpaper not in owned managed folder");
            const auto actual = load_image(wallpaper_state.image); const auto &source = *app.image;
            require(actual.width == source.width && actual.height == source.height && actual.frames.size() == 1, "Wallpaper dimensions/frame differ");
            auto expected = source.frames.at(app.player.frame).pixels;
            for (auto &pixel : expected) {
                const auto channel = [&](uint8_t c) { return static_cast<uint8_t>((unsigned(c)*pixel.a+128u*(255u-pixel.a))/255u); };
                pixel.r=channel(pixel.r); pixel.g=channel(pixel.g); pixel.b=channel(pixel.b); pixel.a=255;
            }
            require(actual.frames[0].pixels == expected, "Wallpaper is not the complete current frame flattened over gray");
            std::filesystem::copy_file(wallpaper_state.image, output/(L"wallpaper-capture-"+std::to_wstring(++wallpaper_captures)+L".bmp"));
            return;
        }
        if (op == "remember_startup_file") {
            app.remember_startup_file();
            app.save_settings();
            return;
        }
        if (op == "resume_startup") {
            app.open_startup(s.size() > 1 ? path(s.at(1)) : std::filesystem::path{});
            return;
        }
        if (op == "incoming_launch") {
            auto bytes = instance_request(instance_identity(app.config),
                                          s.at(1) == "empty" ? std::filesystem::path{} : path(s.at(1)));
            COPYDATASTRUCT copy{kInstanceCopyData, static_cast<DWORD>(bytes.size()), bytes.data()};
            require(SendMessageW(app.hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copy)) == 1,
                    "Incoming launch was not acknowledged");
            return;
        }
        if (op == "incoming_file") {
            const auto object = file_data_object(path(s.at(1)));
            FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
            STGMEDIUM medium{};
            require(SUCCEEDED(object->GetData(&format, &medium)), "Incoming file has no CF_HDROP");
            const auto copied = OleDuplicateData(medium.hGlobal, CF_HDROP, 0);
            ReleaseStgMedium(&medium);
            require(copied != nullptr, "Cannot duplicate owned file drop");
            SendMessageW(app.hwnd, WM_DROPFILES, reinterpret_cast<WPARAM>(copied), 0);
            return;
        }
        if (op == "open_hold") {
            require(!app.loading && (!open_gate || open_gate->released),
                    "Open hold requires an idle owned source");
            open_gate = std::make_shared<PreviewGate>();
            app.open_lister = [gate = open_gate](const auto &folder, auto visibility, const auto &cancel) {
                gate->hold();
                return cancel && cancel() ? std::vector<FileEntry>{} : list_files(folder, visibility);
            };
            return;
        }
        if (op == "open_detach") {
            app.open_lister = {};
            return;
        }
        if (op == "open_release") {
            require(open_gate != nullptr, "No owned open gate exists");
            open_gate->release();
            app.open_lister = {};
            return;
        }
        if (op == "preview_hold") {
            require(!jump && !app.preview_service && !preview_gate,
                    "Preview hold requires a fresh owned service");
            preview_gate = std::make_shared<PreviewGate>();
            const auto options = app.settings.loading_options();
            app.preview_service = std::make_shared<JumpPreview>(
                [gate = preview_gate, options](const auto &path) { return gate->decode(path, options); });
            held_preview_service = app.preview_service;
            return;
        }
        if (op == "preview_release") {
            require(preview_gate != nullptr, "No owned preview gate exists");
            preview_gate->release();
            return;
        }
        if(op=="expect_cp_code") {
            require(app.settings.cp_code==(s.size()>1?widen(s.at(1)):L""),"Applied CP code differs");return;
        }
        if(op.starts_with("adjust_")) {
            namespace id=color_adjust_control;require(adjustment_dialog&&adjustment_dialog->ready(),"Actual color adjustment dialog is not ready");own(adjustment_dialog->window());
            const auto control=[&](int key){const auto h=adjustment_dialog->control(key);own(h);require(fully_visible(h),"Color adjustment control is clipped");return h;};
            const auto click=[&](int key){const auto h=control(key);require(IsWindowEnabled(h),"Color adjustment control is disabled");SendMessageW(h,BM_CLICK,0,0);};
            const std::map<std::string,int> axes{{"Hue",0},{"Saturation",1},{"Lightness",2}};
            if(op=="adjust_set")SetWindowTextW(control(id::Hue+axes.at(s.at(1))),s.at(2)=="empty"?L"":widen(s.at(2)).c_str());
            else if(op=="adjust_scroll") {
                const auto axis=axes.at(s.at(1)),value=std::stoi(s.at(2));require(value>=-1000&&value<=1000,"Invalid adjustment scroll value");
                SendMessageW(adjustment_dialog->window(),WM_HSCROLL,MAKEWPARAM(SB_THUMBPOSITION,WORD(value)),reinterpret_cast<LPARAM>(control(id::ScrollHue+axis)));
            }else if(op=="adjust_reset")click(id::ResetHue+axes.at(s.at(1)));
            else if(op=="adjust_unified") {
                require(s.at(1)=="true"||s.at(1)=="false","Invalid color adjustment mode");if(adjustment_dialog->options().unified!=(s.at(1)=="true"))click(id::Unified);
            }else if(op=="adjust_gray") {
                const auto h=control(id::Gray),dialog=adjustment_dialog->window();const auto value=std::stoi(s.at(1));require(value>=0&&value<=2,"Invalid adjustment gray method");
                SendMessageW(h,CB_SETCURSEL,value,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(id::Gray,CBN_SELCHANGE),reinterpret_cast<LPARAM>(h));
            }else if(op=="adjust_range") {
                const auto axis=axes.at(s.at(1));SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_POS};require(GetScrollInfo(control(id::ScrollHue+axis),SB_CTL,&info),"Cannot read actual adjustment scroll");
                require(info.nMin==std::stoi(s.at(2))&&info.nMax==std::stoi(s.at(3))&&info.nPos==std::stoi(s.at(4)),"Actual adjustment range/position differs");
                require(int(GetDlgItemInt(adjustment_dialog->window(),id::Hue+axis,nullptr,TRUE))==info.nPos,"Adjustment edit and scroll positions differ");
            }else if(op=="adjust_preview") {
                const auto target=output_path(s.at(1));std::filesystem::create_directories(target.parent_path());save_image(adjustment_dialog->adjusted_preview(),0,target);
            }
            else if(op=="adjust_accept")click(IDOK);
            else if(op=="adjust_cancel")click(IDCANCEL);
            else if(op=="adjust_snapshot")capture(adjustment_dialog->window(),output_path(s.at(1)));
            else throw std::runtime_error("Unknown color adjustment operation");return;
        }
        if(op.starts_with("depth_")) {
            namespace id=color_depth_control;require(depth_dialog&&depth_dialog->ready(),"Actual color depth dialog is not ready");own(depth_dialog->window());
            const auto control=[&](int key){const auto h=depth_dialog->control(key);own(h);require(fully_visible(h),"Color depth control is clipped");return h;};
            const auto click=[&](int key){const auto h=control(key);require(IsWindowEnabled(h),"Color depth control is disabled");SendMessageW(h,BM_CLICK,0,0);};
            const std::map<std::string,int> radios{{"Indexed8",0},{"Rgb555",1},{"Rgb565",2},{"Rgb24",3},{"Optimal",4}};
            if(op=="depth_set")click(id::ChoiceBase+radios.at(s.at(1)));
            else if(op=="depth_dither") {
                require(s.at(1)=="true"||s.at(1)=="false","Invalid color depth dither flag");if(depth_dialog->options().dither!=(s.at(1)=="true"))click(id::Dither);
            }else if(op=="depth_wheel") {
                const auto h=s.size()>2?control(id::ChoiceBase+radios.at(s.at(2))):depth_dialog->window();
                const auto delta=std::stoi(s.at(1));require(delta>=-32768&&delta<=32767,"Invalid native wheel delta");SendMessageW(h,WM_MOUSEWHEEL,MAKEWPARAM(0,WORD(delta)),0);
            }else if(op=="depth_enabled")require(bool(IsWindowEnabled(control(id::ChoiceBase+radios.at(s.at(1)))))==(s.at(2)=="true"),"Color depth native enabled state differs");
            else if(op=="depth_current")require(window_text(control(id::Current))==widen(s.at(1)),"Current color depth label differs");
            else if(op=="depth_accept")click(IDOK);
            else if(op=="depth_cancel")click(IDCANCEL);
            else if(op=="depth_snapshot")capture(depth_dialog->window(),output_path(s.at(1)));
            else throw std::runtime_error("Unknown color depth operation");return;
        }
        if(op.starts_with("pal_")) {
            namespace id=palette_control;
            require(palette_editor&&palette_editor->ready(),"Actual palette editor is not ready");own(palette_editor->window());
            const auto control=[&](int key){const auto h=palette_editor->control(key);own(h);require(fully_visible(h),"Palette control is clipped");return h;};
            const auto click=[&](int key){const auto h=control(key);require(IsWindowEnabled(h),"Palette control is disabled");SendMessageW(h,BM_CLICK,0,0);};
            const std::map<std::string,int> fields{{"Index",id::Index},{"Red",id::Red},{"Green",id::Green},{"Blue",id::Blue}};
            if(op=="pal_set")SetWindowTextW(control(fields.at(s.at(1))),widen(s.at(2)).c_str());
            else if(op=="pal_slide") {
                const auto field=fields.at(s.at(1));require(field>=id::Red&&field<=id::Blue,"Invalid RGB slider");
                const auto value=std::stoi(s.at(2));require(value>=0&&value<=255,"Invalid RGB slider value");
                const auto slider=control(id::SlideRed+field-id::Red);require(IsWindowEnabled(slider),"Palette slider is disabled");
                SendMessageW(slider,TBM_SETPOS,TRUE,value);
                SendMessageW(palette_editor->window(),WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(slider));
            }else if(op=="pal_grid") {
                const auto index=std::stoul(s.at(1));const auto count=palette_editor->palette().size();require(index<count,"Invalid palette cell");
                const auto grid=control(id::Grid);RECT area{};GetClientRect(grid,&area);const int cols=count<=4?2:count<=16?4:16;
                const auto rows=(count+cols-1)/cols;const auto x=(int(index%cols)*area.right/cols+(int(index%cols)+1)*area.right/cols)/2;
                const auto y=(int(index/cols)*area.bottom/int(rows)+(int(index/cols)+1)*area.bottom/int(rows))/2;native_click(grid,x,y);
            }else if(op=="pal_rgb") {
                const auto value=palette_editor->palette().at(palette_editor->selected_index());
                require(value==Pixel{uint8_t(std::stoul(s.at(1))),uint8_t(std::stoul(s.at(2))),uint8_t(std::stoul(s.at(3))),uint8_t(std::stoul(s.at(4)))},"Selected palette RGBA differs");
                const int values[]{value.r,value.g,value.b};
                for(int n=0;n<3;++n)require(GetDlgItemInt(palette_editor->window(),id::Red+n,nullptr,FALSE)==values[n]&&
                    SendMessageW(control(id::SlideRed+n),TBM_GETPOS,0,0)==values[n],"Palette RGB edit/slider synchronization differs");
            }else if(op=="pal_preview") {
                require(s.at(1)=="true"||s.at(1)=="false","Invalid palette preview flag");
                if(palette_editor->preview_enabled()!=(s.at(1)=="true"))click(id::Preview);
            }else if(op=="pal_apply")click(id::Apply);
            else if(op=="pal_accept")click(IDOK);
            else if(op=="pal_cancel")click(IDCANCEL);
            else if(op=="pal_pick")click(id::Pick);
            else if(op=="pal_picker_rgb") {
                const auto picker=palette_picker_window();require(!IsWindowEnabled(palette_editor->window()),"OS picker did not disable palette owner");
                const int ids[]{COLOR_RED,COLOR_GREEN,COLOR_BLUE};
                for(int n=0;n<3;++n){const auto h=GetDlgItem(picker,ids[n]);own(h);require(fully_visible(h)&&IsWindowEnabled(h),"OS RGB edit is clipped or disabled");
                    const auto value=std::stoul(s.at(n+1));require(value<=255,"Invalid OS RGB value");SetWindowTextW(h,std::to_wstring(value).c_str());}
                for(int n=0;n<3;++n)require(GetDlgItemInt(picker,ids[n],nullptr,FALSE)==std::stoul(s.at(n+1)),"OS picker RGB text differs");
            }else if(op=="pal_picker_accept"||op=="pal_picker_cancel") {
                const auto picker=palette_picker_window();const auto h=GetDlgItem(picker,op=="pal_picker_accept"?IDOK:IDCANCEL);own(h);
                require(fully_visible(h)&&IsWindowEnabled(h),"OS color picker button is clipped or disabled");SendMessageW(h,BM_CLICK,0,0);
            }else if(op=="pal_snapshot")capture(palette_editor->window(),output_path(s.at(1)));
            else if(op=="pal_picker_snapshot")capture(palette_picker_window(),output_path(s.at(1)));
            else if(op=="pal_expect") {
                const auto expected=load_source(path(s.at(1)));const auto actual=app.presented_image();
                require(actual&&actual->width==expected.width&&actual->height==expected.height&&actual->frames.at(app.presented_frame()).pixels==expected.frames.front().pixels,
                    "Palette displayed RGBA differs from independent reference");render_client(app.hwnd);
            }else throw std::runtime_error("Unknown palette dialog operation");return;
        }
        if(op.starts_with("cp_")) {
            require(cp&&cp->ready(),"Actual CP dialog is not ready");own(cp->window());
            const auto control=[&](int key){const auto h=cp->control(key);own(h);require(fully_visible(h),"CP control is clipped");return h;};
            const auto click=[&](int key){const auto h=control(key);require(IsWindowEnabled(h),"CP control is disabled");SendMessageW(h,BM_CLICK,0,0);};
            if(op=="cp_code"||op=="cp_clear")SetWindowTextW(control(cp_control::Code),op=="cp_clear"?L"":widen(s.at(1)).c_str());
            else if(op=="cp_preview") {
                require(s.at(1)=="true"||s.at(1)=="false","Invalid CP preview flag");
                if(cp->options().preview!=(s.at(1)=="true"))click(cp_control::Preview);
            }else if(op=="cp_accept")click(IDOK);
            else if(op=="cp_cancel")click(IDCANCEL);
            else if(op=="cp_snapshot")capture(cp->window(),output_path(s.at(1)));
            else if(op=="cp_expect_text")require(window_text(control(cp_control::Code))==(s.size()>1?widen(s.at(1)):L""),"CP edit text differs from native filter");
            else if(op=="cp_expect") {
                const auto expected=load_source(path(s.at(1)));const auto actual=app.presented_image();
                require(actual&&actual->width==expected.width&&actual->height==expected.height&&actual->frames.at(app.presented_frame()).pixels==expected.frames.front().pixels,"CP displayed RGBA differs from independent reference");
                render_client(app.hwnd);
            }else throw std::runtime_error("Unknown CP dialog operation");return;
        }
        if(op.starts_with("rainbow_")) {
            require(rainbow&&rainbow->ready(),"Actual Rainbow dialog is not ready");own(rainbow->window());
            const auto click=[&](int id) {
                const auto h=rainbow->control(id);own(h);
                require(fully_visible(h)&&IsWindowEnabled(h),"Rainbow control is clipped or disabled");
                SendMessageW(h,BM_CLICK,0,0);
            };
            if(op=="rainbow_size") {
                constexpr int sizes[]{1,2,4,8,16,32};const auto size=std::stoi(s.at(1));
                const auto found=std::find(std::begin(sizes),std::end(sizes),size);
                require(found!=std::end(sizes),"Invalid Rainbow block size");
                click(rainbow_control::SizeBase+static_cast<int>(found-std::begin(sizes)));
            }else if(op=="rainbow_preview") {
                require(s.at(1)=="true"||s.at(1)=="false","Invalid Rainbow preview flag");
                if(rainbow->options().preview!=(s.at(1)=="true"))click(rainbow_control::Preview);
            }else if(op=="rainbow_accept")click(IDOK);
            else if(op=="rainbow_cancel")click(IDCANCEL);
            else if(op=="rainbow_snapshot")capture(rainbow->window(),output_path(s.at(1)));
            else if(op=="rainbow_expect") {
                const auto expected=load_source(path(s.at(1)));const auto actual=app.presented_image();
                require(actual&&actual->width==expected.width&&actual->height==expected.height&&
                            actual->frames.at(app.presented_frame()).pixels==expected.frames.front().pixels,
                        "Rainbow displayed RGBA differs from independent reference");
                render_client(app.hwnd);
            }else throw std::runtime_error("Unknown Rainbow dialog operation");
            return;
        }
        if (op.starts_with("jump_")) {
            require(jump && jump->ready(), "Actual jump dialog is not open");
            own(jump->window());
            if (op == "jump_set") {
                const auto h = jump_widget(s.at(1));
                require(fully_visible(h) && IsWindowEnabled(h), "Jump input is clipped or disabled");
                if (s.at(1) == "Enabled") {
                    const auto checked = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    if (checked != (s.at(2) == "true"))
                        SendMessageW(h, BM_CLICK, 0, 0);
                } else if (s.at(1) == "Quality") {
                    const auto value = std::stoi(s.at(2));
                    require(value >= 0 && value < SendMessageW(h, CB_GETCOUNT, 0, 0), "Invalid jump quality");
                    SendMessageW(h, CB_SETCURSEL, value, 0);
                    SendMessageW(jump->window(), WM_COMMAND, MAKEWPARAM(jump_control::Quality, CBN_SELCHANGE),
                                 reinterpret_cast<LPARAM>(h));
                } else {
                    require(s.at(1) == "Order", "Only the jump order is editable");
                    SetWindowTextW(h, s.at(2) == "empty" ? L"" : widen(s.at(2)).c_str());
                }
            } else if (op == "jump_click") {
                const auto h = jump_widget(s.at(1));
                require(fully_visible(h) && IsWindowEnabled(h), "Jump button is clipped or disabled");
                SendMessageW(h, BM_CLICK, 0, 0);
            } else if (op == "jump_scroll") {
                const std::map<std::string, int> codes{
                    {"Left", SB_LEFT},           {"Right", SB_RIGHT},       {"LineLeft", SB_LINELEFT},
                    {"LineRight", SB_LINERIGHT}, {"PageLeft", SB_PAGELEFT}, {"PageRight", SB_PAGERIGHT}};
                SendMessageW(jump->window(), WM_HSCROLL, codes.at(s.at(1)),
                             reinterpret_cast<LPARAM>(jump_widget("Scroll")));
            } else if (op == "jump_expect") {
                require(window_text(jump_widget(s.at(1))) == widen(s.at(2)), "Jump control text differs");
            } else if (op == "jump_rgba") {
                const auto expected = load_image(fixtures / widen(s.at(1)));
                const auto actual = jump->preview_image();
                require(actual && actual->width == expected.width && actual->height == expected.height &&
                            actual->frames.size() == 1 &&
                            actual->frames[0].pixels == expected.frames[0].pixels,
                        "Jump preview first-frame RGBA differs");
            } else if (op == "jump_snapshot") {
                capture(jump->window(), output_path(s.at(1)));
            } else if (op == "jump_close") {
                SendMessageW(jump->window(), WM_CLOSE, 0, 0);
            } else if (op == "jump_key") {
                const auto h = jump_widget(s.at(1));
                SetFocus(h);
                UINT key{}, modifiers{};
                require(parse_key(widen(s.at(2)), key, modifiers) && modifiers == 0, "Invalid jump key");
                MSG message{};
                message.hwnd = h;
                message.message = WM_KEYDOWN;
                message.wParam = key;
                if (!IsDialogMessageW(jump->window(), &message))
                    DispatchMessageW(&message);
            } else {
                throw std::runtime_error("Unknown jump operation");
            }
            return;
        }
        if (op == "navigation_root") {
            const auto root = output_path(s.at(1));
            require(std::filesystem::is_directory(root), "Navigation root must be an owned folder");
            app.navigation_root = root;
            return;
        }
        if (op == "expect_location") {
            const auto actual = s.at(1) == "folder"               ? app.folder
                                : s.at(1) == "requested"          ? app.requested
                                : s.at(1) == "image" && app.image ? app.image->path
                                                                  : std::filesystem::path{};
            require(s.at(1) == "folder" || s.at(1) == "requested" || s.at(1) == "image",
                    "Unknown location field");
            require(s.at(2) == "empty" ? actual.empty() : same_path(actual, path(s.at(2))),
                    "Displayed location differs");
            return;
        }
        if (op == "expect_history") {
            const auto expected = path(s.at(1));
            const bool found = std::any_of(app.settings.history.begin(), app.settings.history.end(),
                                           [&](const auto &entry) { return same_path(entry, expected); });
            require(found == (s.at(2) == "true"), "Visited folder history differs");
            return;
        }
        if (op == "expect_missing_combo_capture_rejected") {
            const auto combo = control(s.at(1));
            const auto row = SendMessageW(combo, CB_GETCURSEL, 0, 0);
            require(row >= 0, "Negative capture requires a selected native combo");
            const auto length = SendMessageW(combo, CB_GETLBTEXTLEN, row, 0);
            std::wstring text(static_cast<size_t>(length) + 1, L'\0');
            SendMessageW(combo, CB_GETLBTEXT, row, reinterpret_cast<LPARAM>(text.data()));
            text.resize(static_cast<size_t>(length));
            const auto replace = [&](const wchar_t *value) {
                SendMessageW(combo, CB_DELETESTRING, row, 0);
                SendMessageW(combo, CB_INSERTSTRING, row, reinterpret_cast<LPARAM>(value));
                SendMessageW(combo, CB_SETCURSEL, row, 0);
            };
            replace(L""); bool rejected{};
            try { capture(preferences->window(), output_path(s.at(2))); }
            catch (const std::exception &error) {
                rejected = std::string(error.what()).starts_with("Native combo text missing from capture: id=" + std::to_string(GetDlgCtrlID(combo)) + " ");
            }
            replace(text.c_str());
            require(rejected, "A blank selected combo incorrectly passed capture verification");
            ++missing_combo_capture_rejections;
            return;
        }
        if (op == "preferences_resize") {
            require(preferences && IsWindow(preferences->window()), "Settings are not open");
            own(preferences->window());
            require(SetWindowPos(preferences->window(), nullptr, 0, 0, std::stoi(s.at(1)), std::stoi(s.at(2)),
                                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE, "Cannot resize owned settings");
            return;
        }
        if (op == "copy_owned_executable") {
            const auto target = output_path(s.at(1));
            std::filesystem::create_directories(target.parent_path());
            require(std::filesystem::copy_file(viewer_executable(), target), "Cannot copy owned executable");
            return;
        }
        if (op == "external_native_browse") {
            require(preferences && !executable_picker_window(false), "Settings must own a new OS picker");
            preferences->set_executable_picker({}); // Restore the product's actual GetOpenFileNameW call.
            executable_picker_cwd = std::filesystem::current_path();
            const auto button = control("ExternalBrowse");
            require(fully_visible(button) && IsWindowEnabled(button), "Executable browse button is clipped");
            ++native_executable_pickers;
            SendMessageW(button, BM_CLICK, 0, 0);
            return;
        }
        if (op == "picker_filename") {
            const auto chosen = output_path(s.at(1));
            require(chosen.extension() == L".exe" && std::filesystem::is_regular_file(chosen),
                    "Native picker may only select an existing owned executable");
            const auto dialog = executable_picker_window();
            auto edit = GetDlgItem(dialog, edt1);
            if (!edit) {
                const auto combo = GetDlgItem(dialog, cmb13); own(combo);
                edit = reinterpret_cast<HWND>(SendMessageW(combo, CBEM_GETEDITCONTROL, 0, 0));
            }
            own(edit);
            require(fully_visible(edit) && IsWindowEnabled(edit), "Actual picker filename edit is clipped");
            require(SetWindowTextW(edit, chosen.c_str()) && window_text(edit) == chosen.wstring(),
                    "Native executable filename input differs");
            return;
        }
        if (op == "picker_snapshot") {
            const auto dialog = executable_picker_window();
            const auto filter = GetDlgItem(dialog, cmb1); own(filter);
            require(window_text(filter) == L"アプリケーション" &&
                        SendMessageW(filter, CB_GETCOUNT, 0, 0) == 1,
                    "Actual executable filter differs");
            try { capture(dialog, output_path(s.at(1))); }
            catch (const std::exception &error) {
                // The OS-owned themed filter can return text but leave it unpainted on a private
                // station. Retain the diagnostic image and report that limitation explicitly.
                // Existing GVN capture assertions still reject every missing control stroke.
                const auto prefix = "Native combo text missing from capture: id=" +
                                    std::to_string(cmb1) + " text=" + utf8(window_text(filter)) + " rect=";
                if (!std::string(error.what()).starts_with(prefix)) throw;
                native_picker_capture_warnings.push_back(error.what());
            }
            return;
        }
        if (op == "picker_close") {
            require(s.at(1) == "OK" || s.at(1) == "Cancel", "Unknown executable picker button");
            const auto dialog = executable_picker_window();
            const int id = s.at(1) == "OK" ? IDOK : IDCANCEL;
            const auto button = GetDlgItem(dialog, id); own(button);
            require(fully_visible(button) && IsWindowEnabled(button), "Picker button is clipped");
            if (id == IDOK) ++native_executable_accepts; else ++native_executable_cancels;
            PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(button));
            return;
        }
        if (op == "external_browse") {
            const auto chosen = s.at(1) == "cancel" ? std::filesystem::path{} : output_path(s.at(1));
            require(chosen.empty() || std::filesystem::is_regular_file(chosen), "Browse choice is not an owned file");
            preferences->set_executable_picker([&, chosen](HWND owner) {
                own(owner); ++external_browse_substitutions; return chosen;
            });
            const auto button = control("ExternalBrowse"); SetFocus(button);
            require(fully_visible(button) && IsWindowEnabled(button), "Executable browse button is clipped");
            SendMessageW(button, BM_CLICK, 0, 0);
            return;
        }
        if (op == "external_drop") {
            const auto object = file_data_object(path(s.at(1)));
            FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}; STGMEDIUM medium{};
            require(SUCCEEDED(object->GetData(&format, &medium)), "Owned executable drop lacks CF_HDROP");
            const auto copied = OleDuplicateData(medium.hGlobal, CF_HDROP, 0); ReleaseStgMedium(&medium);
            require(copied != nullptr, "Cannot duplicate owned executable drop");
            const auto page = preferences->control(preference_id::PageBase + static_cast<int>(PreferencePage::External));
            own(page);
            require(GetWindowLongPtrW(page, GWL_EXSTYLE) & WS_EX_ACCEPTFILES,
                    "Actual external settings page must accept Shell file drops");
            SendMessageW(page, WM_DROPFILES, reinterpret_cast<WPARAM>(copied), 0);
            ++incoming_executable_drops;
            return;
        }
        if (op == "expect_value" || op == "expect_selection") {
            const auto widget = control(s.at(1));
            if (op == "expect_selection")
                require(SendMessageW(widget, CB_GETCURSEL, 0, 0) == std::stoi(s.at(2)), "Native selection differs");
            else {
                const auto expected = s.at(2) == "empty" ? std::wstring{} :
                                      s.at(2).find("${") != std::string::npos ? path(s.at(2)).wstring() : widen(s.at(2));
                require(window_text(widget) == expected, ("Exact control text differs: " + s.at(1)).c_str());
            }
            return;
        }
        if (op == "remember_external" || op == "expect_external_equal") {
            const auto index = std::stoul(s.at(2)) - 1; require(index < 9, "External slot out of range");
            const auto entry = app.settings.external_application(index);
            if (op == "remember_external") remembered_external[s.at(1)] = entry;
            else require(entry == remembered_external.at(s.at(1)), "Whole external entry differs");
            return;
        }
        if (op == "expect_external") {
            const auto index = std::stoul(s.at(1)) - 1; require(index < 9, "External slot out of range");
            const auto entry = app.settings.external_application(index); const auto key = s.at(2);
            const auto expected = s.at(3) == "empty" ? std::wstring{} :
                                  s.at(3).find("${") != std::string::npos ? path(s.at(3)).wstring() : widen(s.at(3));
            if (key == "name" || key == "executable" || key == "verb" || key == "path") {
                const auto actual = key == "name" ? entry.name : key == "verb" ? entry.verb : entry.executable;
                require(key == "path" ? same_path(actual, expected) : actual == expected, "Applied external text differs");
            } else {
                int actual{};
                if (key == "send") actual = static_cast<int>(entry.send);
                else if (key == "show") actual = static_cast<int>(entry.show);
                else if (key == "quit") actual = entry.quit_after_launch;
                else if (key == "separator") actual = entry.separator;
                else if (key == "archive_member") actual = entry.include_archive_member;
                else { require(key == "archive_folder", "Unknown external field"); actual = entry.archive_file_for_folder; }
                require(actual == std::stoi(s.at(3)), "Applied external choice differs");
            }
            return;
        }
        if (op == "reload_external_settings") {
            require(!preferences, "Close settings before reloading its saved file");
            const auto before = app.settings.external_applications(); Settings loaded; loaded.load(app.config);
            require(loaded.external_applications() == before, "Reloaded external entries differ");
            app.settings = std::move(loaded);
            return;
        }
        if (op == "expect_choice_count") {
            require(SendMessageW(control(s.at(1)), CB_GETCOUNT, 0, 0) == std::stoi(s.at(2)),
                    "Native choice count differs");
            return;
        }
        if (op == "boundary_expect") {
            const bool forward = s.at(1) == "forward";
            require(s.at(1) == "forward" || s.at(1) == "backward", "Unknown boundary direction");
            require(window_text(boundary_window()) == (forward ? L"GVN - 次へ" : L"GVN - 前へ"),
                    "Boundary title differs");
            require(window_text(boundary_button("End")).find(forward ? L"先頭" : L"末尾") !=
                        std::wstring::npos,
                    "Boundary endpoint label differs");
            require(window_text(boundary_button("Folder")).find(forward ? L"次の" : L"前の") !=
                        std::wstring::npos,
                    "Boundary folder label differs");
            return;
        }
        if (op == "boundary_click") {
            const auto button = boundary_button(s.at(1));
            require(fully_visible(button) && IsWindowEnabled(button),
                    "Boundary button is clipped or disabled");
            SendMessageW(button, BM_CLICK, 0, 0);
            return;
        }
        if (op == "boundary_snapshot") {
            capture(boundary_window(), output_path(s.at(1)));
            return;
        }
        if (op == "color_set" || op == "color_expect") {
            const auto dialog = color_dialog();
            for (int index = 0; index < 3; ++index) {
                const int id = 706 + index;
                const auto edit = GetDlgItem(dialog, id);
                own(edit);
                require(fully_visible(edit), "Color component edit must be visible");
                if (op == "color_expect")
                    require(std::stoi(window_text(edit)) == std::stoi(s.at(index + 1)),
                            "Color picker initial value differs");
                else {
                    SetWindowTextW(edit, widen(s.at(index + 1)).c_str());
                    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, EN_CHANGE),
                                 reinterpret_cast<LPARAM>(edit));
                }
            }
            return;
        }
        if (op == "color_snapshot") {
            capture(color_dialog(), output_path(s.at(1)));
            return;
        }
        if (op == "color_close") {
            const auto button = GetDlgItem(color_dialog(), s.at(1) == "OK" ? IDOK : IDCANCEL);
            own(button);
            SendMessageW(button, BM_CLICK, 0, 0);
            return;
        }
        if (op == "font_script_available") {
            const auto control = GetDlgItem(font_dialog(), cmb5);
            own(control);
            require(locally_visible(control) && IsWindowEnabled(control) &&
                        SendMessageW(control, CB_GETCOUNT, 0, 0) > 0,
                    "Native font script selector must be available");
            return;
        }
        if (op == "font_set") {
            const auto dialog = font_dialog();
            const auto family = GetDlgItem(dialog, cmb1), size = GetDlgItem(dialog, cmb3),
                       style = GetDlgItem(dialog, cmb2);
            const auto face = widen(s.at(1));
            const auto family_index = SendMessageW(family, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                                   reinterpret_cast<LPARAM>(face.c_str()));
            require(family_index != CB_ERR, "Requested installed font missing");
            SendMessageW(family, CB_SETCURSEL, family_index, 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(cmb1, CBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(family));
            const auto index = std::stoi(s.at(3));
            require(index >= 0 && index < SendMessageW(style, CB_GETCOUNT, 0, 0),
                    "Requested font style missing");
            SendMessageW(style, CB_SETCURSEL, index, 0);
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(cmb2, CBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(style));
            SetWindowTextW(size, widen(s.at(2)).c_str());
            SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(cmb3, CBN_EDITCHANGE),
                         reinterpret_cast<LPARAM>(size));
            SetFocus(GetDlgItem(dialog, IDOK));
            return;
        }
        if (op == "font_effect") {
            const auto id = s.at(1) == "underline" ? chx2 : chx1;
            const auto checkbox = GetDlgItem(font_dialog(), id);
            own(checkbox);
            require(fully_visible(checkbox), "Font effect checkbox must be visible");
            if ((SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED) != (s.at(2) == "true"))
                SendMessageW(checkbox, BM_CLICK, 0, 0);
            return;
        }
        if (op == "font_snapshot") {
            capture(font_dialog(), output_path(s.at(1)));
            return;
        }
        if (op == "font_close") {
            const auto button = GetDlgItem(font_dialog(), s.at(1) == "OK" ? IDOK : IDCANCEL);
            own(button);
            SendMessageW(button, BM_CLICK, 0, 0);
            return;
        }
        if (op == "canvas_key") {
            const auto target = s.at(1) == "horizontal" ? app.canvas_horizontal : app.canvas_vertical;
            own(target);
            require(fully_visible(target), "Image scrollbar must be visible");
            const std::map<std::string, UINT> keys{
                {"Home", VK_HOME}, {"End", VK_END},   {"Left", VK_LEFT},    {"Right", VK_RIGHT},
                {"Up", VK_UP},     {"Down", VK_DOWN}, {"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT}};
            SetFocus(target);
            MSG message{};
            message.hwnd = target;
            message.message = WM_KEYDOWN;
            message.wParam = keys.at(s.at(2));
            require(!app.route_key(message), "App intercepted native scrollbar navigation");
            SendMessageW(target, message.message, message.wParam, 0);
            SendMessageW(target, WM_KEYUP, message.wParam, 0);
            return;
        }
        if (op == "startup") {
            app.show_initial(SW_SHOWNOACTIVATE);
            return;
        }
        if (op == "remember_window") {
            app.remember_window();
            app.save_settings();
            return;
        }
        if (op == "transfer_browse_reply") {
            folder_browse_replies.push_back(s.at(1) == "cancel" ? std::filesystem::path{}
                                                                : output_path(s.at(1)));
            return;
        } else if (op == "transfer_set") {
            const auto window = transfer_widget(s.at(1));
            SetFocus(window);
            require(fully_visible(window) && IsWindowEnabled(window),
                    "Transfer widget is clipped or disabled");
            if (s.at(1) == "Confirm" || s.at(1) == "Registered") {
                const bool checked = s.at(2) == "true" || s.at(2) == "1";
                if ((SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED) != checked)
                    SendMessageW(window, BM_CLICK, 0, 0);
            } else {
                const auto value = s.at(2) == "empty"                       ? std::wstring{}
                                   : s.at(2).find('$') != std::string::npos ? path(s.at(2)).wstring()
                                                                            : widen(s.at(2));
                SetWindowTextW(window, value.c_str());
            }
            return;
        } else if (op == "transfer_click") {
            const auto window = transfer_widget(s.at(1));
            SetFocus(window);
            require(fully_visible(window) && IsWindowEnabled(window),
                    "Transfer button is clipped or disabled");
            SendMessageW(window, BM_CLICK, 0, 0);
            return;
        } else if (op == "transfer_focus") {
            const auto window = transfer_widget(s.at(1));
            SetFocus(window);
            require(fully_visible(window) && GetFocus() == window, "Transfer focus did not reveal the input");
            return;
        } else if (op == "transfer_wheel") {
            require(file_transfer != nullptr, "Transfer dialog is not open");
            const auto window = transfer_widget(
                file_transfer->registered_mode() ? "Edit" + std::to_string(file_transfer->active_slot() + 1)
                                                 : "BasicEdit");
            SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(std::stoi(s.at(1)))), 0);
            return;
        } else if (op == "transfer_enter") {
            require(file_transfer != nullptr, "Transfer dialog is not open");
            MSG message{};
            message.hwnd = GetFocus();
            own(message.hwnd);
            message.message = WM_KEYDOWN;
            message.wParam = VK_RETURN;
            if (!IsDialogMessageW(file_transfer->window(), &message))
                DispatchMessageW(&message);
            return;
        } else if (op == "transfer_close") {
            require(file_transfer != nullptr, "Transfer dialog is not open");
            SendMessageW(file_transfer->window(), WM_CLOSE, 0, 0);
            return;
        } else if (op == "transfer_resize") {
            require(file_transfer != nullptr, "Transfer dialog is not open");
            SetWindowPos(file_transfer->window(), nullptr, 0, 0, std::stoi(s.at(1)), std::stoi(s.at(2)),
                         SWP_NOMOVE | SWP_NOZORDER);
            return;
        } else if (op == "transfer_snapshot") {
            require(file_transfer != nullptr, "Transfer dialog is not open");
            capture(file_transfer->window(), output_path(s.at(1)));
            return;
        } else if (op == "transfer_expect") {
            require(file_transfer != nullptr, "Transfer dialog is not open");
            if (s.at(1) == "Focus")
                require(GetFocus() == transfer_widget(s.at(2)), "Transfer input focus differs");
            else if (s.at(1) == "Slot")
                require(file_transfer->active_slot() == std::stoi(s.at(2)) - 1,
                        "Transfer selected slot differs");
            else if (s.at(1) == "Mode")
                require(file_transfer->registered_mode() == (s.at(2) == "true"), "Transfer mode differs");
            else if (s.at(1) == "Confirm" || s.at(1) == "Registered")
                require((SendMessageW(transfer_widget(s.at(1)), BM_GETCHECK, 0, 0) == BST_CHECKED) ==
                            (s.at(2) == "true"),
                        "Transfer checkbox differs");
            else {
                const auto actual = window_text(transfer_widget(s.at(1)));
                const auto expected = s.at(2) == "empty"                       ? std::wstring{}
                                      : s.at(2).find('$') != std::string::npos ? path(s.at(2)).wstring()
                                                                               : widen(s.at(2));
                require(s.at(2).find('$') != std::string::npos ? same_path(actual, expected)
                                                               : actual == expected,
                        "Transfer text differs");
            }
            return;
        } else if (op == "expect_transfer_option") {
            require(s.at(1) == "copy" || s.at(1) == "move", "Unknown transfer operation");
            const auto options = app.transfer_options(s.at(1) == "move");
            if (s.at(2) == "registered")
                require(options.registered == (s.at(3) == "true"), "Applied transfer mode differs");
            else if (s.at(2) == "confirm")
                require(options.confirm == (s.at(3) == "true"), "Applied transfer confirmation differs");
            else {
                require(s.at(2) == "basic", "Unknown transfer option");
                const auto expected = s.at(3) == "empty"                       ? std::wstring{}
                                      : s.at(3).find('$') != std::string::npos ? path(s.at(3)).wstring()
                                                                               : widen(s.at(3));
                require(s.at(3).find('$') != std::string::npos ? same_path(options.basic_path, expected)
                                                               : options.basic_path == expected,
                        "Applied basic folder differs");
            }
            return;
        } else if (op == "transfer_drop") {
            const auto edit = transfer_widget(s.at(1));
            require(fully_visible(edit), "Cannot drop onto a clipped input");
            const auto object = file_data_object(output_path(s.at(2)));
            auto target = file_transfer->folder_drop_target(GetDlgCtrlID(edit));
            require(target != nullptr, "Actual registered folder target is missing");
            const auto old_text = window_text(edit);
            DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
            require(target->DragEnter(object.Get(), 0, {}, &effect) == S_OK, "Folder DragEnter failed");
            effect = DROPEFFECT_COPY;
            require(target->Drop(object.Get(), 0, {}, &effect) == S_OK, "Folder Drop failed");
            const bool expected = s.at(3) == "true";
            const DWORD expected_effect = expected ? DROPEFFECT_COPY : DROPEFFECT_NONE;
            require(effect == expected_effect, "Folder drop effect differs");
            if (!expected)
                require(window_text(edit) == old_text, "Rejected drop changed the path");
            ++incoming_folder_drops;
            return;
        } else if (op == "scroll_page") {
            require(s.at(1) == "top" || s.at(1) == "bottom", "Unknown scroll destination");
            const auto scrollbar =
                preferences->control(preference_id::ScrollBase + preferences->selected_page());
            own(scrollbar);
            require(fully_visible(scrollbar), "Settings scrollbar is not visible");
            SetFocus(scrollbar);
            const UINT key = s.at(1) == "bottom" ? VK_END : VK_HOME;
            SendMessageW(scrollbar, WM_KEYDOWN, key, 0);
            SendMessageW(scrollbar, WM_KEYUP, key, 0);
            return;
        }
        if (op == "bar_hover") {
            const auto mods = std::stoul(s.at(3));
            BYTE previous[256]{}, keys[256]{};
            GetKeyboardState(previous);
            keys[VK_CONTROL] = (mods & 1) ? 0x80 : 0;
            keys[VK_SHIFT] = (mods & 2) ? 0x80 : 0;
            SetKeyboardState(keys);
            const WPARAM flags = ((mods & 1) ? MK_CONTROL : 0) | ((mods & 2) ? MK_SHIFT : 0);
            RECT area{};
            GetClientRect(app.hwnd, &area);
            const int y = s.at(2) == "bottom" ? area.bottom - 1 : std::stoi(s.at(2));
            scripted_cursor = {std::stoi(s.at(1)), y};
            SendMessageW(app.hwnd, WM_MOUSEMOVE, flags, MAKELPARAM(std::stoi(s.at(1)), y));
            SetKeyboardState(previous);
            return;
        }
        if (op == "bar_leave") {
            scripted_cursor = {-10, -10};
            SendMessageW(app.hwnd, WM_MOUSELEAVE, 0, 0);
            return;
        }
        if (op == "bar_child_hover") {
            const auto child = s.at(1) == "Toolbar" ? app.toolbar.hwnd : app.status;
            own(child);
            require(fully_visible(child), "Bar control is not visible");
            POINT point{std::stoi(s.at(2)), std::stoi(s.at(3))};
            scripted_cursor = point;
            MapWindowPoints(child, app.hwnd, &scripted_cursor, 1);
            const auto mods = std::stoul(s.at(4));
            BYTE previous[256]{}, keys[256]{};
            GetKeyboardState(previous);
            keys[VK_CONTROL] = (mods & 1) ? 0x80 : 0;
            keys[VK_SHIFT] = (mods & 2) ? 0x80 : 0;
            SetKeyboardState(keys);
            SendMessageW(child, WM_MOUSEMOVE, ((mods & 1) ? MK_CONTROL : 0) | ((mods & 2) ? MK_SHIFT : 0),
                         MAKELPARAM(point.x, point.y));
            SetKeyboardState(previous);
            return;
        }
        if (op == "mouse_context" || op == "mouse_assign") {
            const auto choose = [&](const std::string &name, unsigned selected, bool data) {
                const auto c = control(name);
                LRESULT index = selected;
                if (data) {
                    index = -1;
                    for (LRESULT i = 0; i < SendMessageW(c, CB_GETCOUNT, 0, 0); ++i)
                        if (static_cast<unsigned>(SendMessageW(c, CB_GETITEMDATA, i, 0)) == selected) {
                            index = i;
                            break;
                        }
                }
                require(index >= 0 && SendMessageW(c, CB_SETCURSEL, index, 0) != CB_ERR,
                        "Mouse choice unavailable");
                SendMessageW(GetParent(c), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(c), CBN_SELCHANGE),
                             reinterpret_cast<LPARAM>(c));
            };
            choose("MouseGestureChoice", static_cast<unsigned>(gesture_ids.at(s.at(1))), false);
            choose("MouseHeld", std::stoul(s.at(2)), true);
            choose("MouseModifiers", std::stoul(s.at(3)), false);
            if (op == "mouse_assign")
                choose("MouseActionChoice", mouse_action_id(s.at(4)), true);
        } else if (op == "expect_mouse") {
            require(app.settings.mouse.action(gesture_ids.at(s.at(1)), std::stoul(s.at(2)),
                                              std::stoul(s.at(3))) == mouse_action_id(s.at(4)),
                    "Applied mouse binding differs");
        } else if (op == "window_move_probe") {
            own(app.hwnd);
            probing_window_move = s.at(1) == "on";
            if (probing_window_move)
                require(SetWindowSubclass(app.hwnd, window_move_probe, 91,
                                          reinterpret_cast<DWORD_PTR>(this)) != FALSE,
                        "Cannot install own-window move boundary probe");
            else
                RemoveWindowSubclass(app.hwnd, window_move_probe, 91);
        } else if (op == "pointer") {
            own(app.hwnd);
            const auto event = s.at(1);
            require(event == "down" || event == "up" || event == "double" || event == "move" ||
                        event == "wheel" || event == "hwheel",
                    "Unknown pointer event");
            POINT point{std::stoi(s.at(3)), app.top + std::stoi(s.at(4))};
            scripted_cursor = point;
            const unsigned mods = std::stoul(s.at(5));
            BYTE before[256]{}, state[256]{};
            GetKeyboardState(before);
            state[VK_CONTROL] = (mods & 1) ? 0x80 : 0;
            state[VK_SHIFT] = (mods & 2) ? 0x80 : 0;
            state[VK_MENU] = (mods & 4) ? 0x80 : 0;
            SetKeyboardState(state);
            unsigned button = 0;
            if (event == "down" || event == "up" || event == "double") {
                const std::map<std::string, unsigned> buttons{
                    {"Left", 0}, {"Right", 1}, {"Middle", 2}, {"X1", 3}, {"X2", 4}};
                button = buttons.at(s.at(2));
                if (event == "up")
                    scripted_buttons &= ~(1u << button);
                else
                    scripted_buttons |= 1u << button;
            }
            WPARAM flags = ((scripted_buttons & mouse_left) ? MK_LBUTTON : 0) |
                           ((scripted_buttons & mouse_right) ? MK_RBUTTON : 0) |
                           ((scripted_buttons & mouse_middle) ? MK_MBUTTON : 0) |
                           ((scripted_buttons & mouse_x1) ? MK_XBUTTON1 : 0) |
                           ((scripted_buttons & mouse_x2) ? MK_XBUTTON2 : 0) | ((mods & 1) ? MK_CONTROL : 0) |
                           ((mods & 2) ? MK_SHIFT : 0);
            UINT message = WM_MOUSEMOVE;
            if (event == "wheel" || event == "hwheel") {
                message = event == "wheel" ? WM_MOUSEWHEEL : WM_MOUSEHWHEEL;
                flags = MAKEWPARAM(flags, std::stoi(s.at(2)));
                ClientToScreen(app.hwnd, &point);
            } else if (event != "move") {
                static const UINT down[]{WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, WM_XBUTTONDOWN,
                                         WM_XBUTTONDOWN};
                static const UINT up[]{WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP, WM_XBUTTONUP};
                static const UINT twice[]{WM_LBUTTONDBLCLK, WM_RBUTTONDBLCLK, WM_MBUTTONDBLCLK,
                                          WM_XBUTTONDBLCLK, WM_XBUTTONDBLCLK};
                message = event == "down" ? down[button] : event == "up" ? up[button] : twice[button];
                if (button >= 3)
                    flags = MAKEWPARAM(flags, button == 3 ? XBUTTON1 : XBUTTON2);
            }
            SendMessageW(app.hwnd, message, flags, MAKELPARAM(point.x, point.y));
            SetKeyboardState(before);
        } else if (op == "answer")
            answers.push_back(s.at(1) == "OK"    ? IDOK
                              : s.at(1) == "Yes" ? IDYES
                              : s.at(1) == "No"  ? IDNO
                                                 : IDCANCEL);
        else if (op == "new_window_capture") {
            new_window_capture_used = true;
            app.dialogs.new_viewer = [&](const auto &executable, const auto &config, const auto &content) {
                require(executable == viewer_executable() && config == app.config,
                        "New viewer did not use this process's executable and isolated profile");
                captured_new_windows.push_back(decode_new_window(encode_new_window(content)));
                return ViewerProcess(PROCESS_INFORMATION{});
            };
        } else if (op == "expect_new_window") {
            const auto kind = s.at(1);
            if (kind == "count") {
                require(captured_new_windows.size() == std::stoul(s.at(2)),
                        "Captured new-window request count differs");
            } else {
                require(!captured_new_windows.empty(), "No new-window request captured");
                const auto &content = captured_new_windows.back();
                if (kind == "empty")
                    require(std::holds_alternative<std::monostate>(content), "Expected an empty window");
                else if (kind == "file") {
                    const auto file = std::get_if<std::filesystem::path>(&content);
                    require(file && same_path(*file, path(s.at(2))), "New-window source path differs");
                } else {
                    const auto image = std::get_if<Image>(&content);
                    require(image && image->frames.size() == 1 && image->path.empty(),
                            "New-window image must be one detached frame");
                    if (kind == "image") {
                        auto expected = load_image(path(s.at(2)));
                        if (s.size() == 7)
                            expected = crop(expected, {std::stoi(s[3]), std::stoi(s[4]),
                                                       std::stoi(s[5]), std::stoi(s[6])});
                        require(image->width == expected.width && image->height == expected.height &&
                                    image->frames[0].pixels == expected.frames[0].pixels,
                                "New-window image RGBA differs from the expected fixture");
                    } else {
                        require(kind == "pixels", "Unknown new-window expectation");
                        require(image->width == std::stoul(s.at(3)) && image->height == std::stoul(s.at(4)) &&
                                    image->frames[0].pixels == remembered.at(s.at(2)),
                                "New-window image differs from the remembered pixels");
                    }
                }
            }
        } else if (op == "open")
            app.open(path(s.at(1)));
        else if (op == "command")
            app.message(WM_COMMAND, command_ids.at(s.at(1)), 0);
        else if (op == "slideshow_timer") {
            require(app.slideshow && !app.loading, "Slideshow timer requires an active, settled slide");
            SendMessageW(app.hwnd, WM_TIMER, 2, 0);
        }
        else if (op == "menu") {
            const auto id = command_ids.at(s.at(1));
            const auto find = [&](auto &&self, HMENU menu) -> HMENU {
                for (int i = 0; i < GetMenuItemCount(menu); ++i) {
                    if (GetMenuItemID(menu, i) == id)
                        return menu;
                    if (const auto child = GetSubMenu(menu, i))
                        if (const auto found = self(self, child))
                            return found;
                }
                return nullptr;
            };
            const auto menu = find(find, app.main_menu);
            require(menu != nullptr, "Native menu command is missing");
            const auto state = GetMenuState(menu, id, MF_BYCOMMAND);
            require(state != static_cast<UINT>(-1) && !(state & (MF_GRAYED | MF_DISABLED)),
                    "Native menu command is disabled");
            SendMessageW(app.hwnd, WM_COMMAND, MAKEWPARAM(id, 0), 0);
        }
        else if (op == "native_command")
            SendMessageW(app.hwnd, WM_COMMAND, command_ids.at(s.at(1)), 0);
        else if (op == "player")
            toolbar(command_ids.at(s.at(1)));
        else if (op == "main_button")
            toolbar(command_ids.at(s.at(1)), false);
        else if (op == "settings")
            app.message(WM_COMMAND, Preferences, 0);
        else if (op == "tab") {
            auto tab = preferences ? preferences->control(preference_id::Tabs) : nullptr;
            own(tab);
            preferences->select_page(static_cast<PreferencePage>(page_ids.at(s.at(1))));
            RECT r{};
            require(TabCtrl_GetItemRect(tab, preferences->page_tab_index(static_cast<PreferencePage>(page_ids.at(s.at(1)))), &r) != FALSE, "Tab missing");
            native_click(tab, (r.left + r.right) / 2, (r.top + r.bottom) / 2);
            require(preferences->selected_page() == page_ids.at(s.at(1)),
                    "Native tab click did not select requested page");
        } else if(op=="search_result"){
            require(preferences!=nullptr,"Settings dialog required");auto list=preferences->control(preference_id::SearchResults);own(list);const int row=std::stoi(s.at(1));require(row>=0&&row<SendMessageW(list,LB_GETCOUNT,0,0),"Search row missing");SendMessageW(list,LB_SETCURSEL,row,0);SetFocus(list);SendMessageW(list,WM_KEYDOWN,VK_RETURN,0);require(IsChild(preferences->window(),GetFocus()),"Search did not focus an owned setting");
        } else if (op == "expect_checked") {
            const auto widget = control(s.at(1));
            require(s.at(2) == "true" || s.at(2) == "false", "Invalid expected checkbox state");
            require((SendMessageW(widget, BM_GETCHECK, 0, 0) == BST_CHECKED) == (s.at(2) == "true"),
                    ("Native checkbox differs: " + s.at(1)).c_str());
        } else if (op == "set") {
            auto c = control(s.at(1));
            SetFocus(c);
            require(fully_visible(c) && IsWindowEnabled(c),
                    "Cannot edit a clipped, hidden or disabled control");
            wchar_t cls[80]{};
            GetClassNameW(c, cls, 80);
            if (_wcsicmp(cls, L"Button") == 0) {
                const bool wanted = s.at(2) == "true" || s.at(2) == "1";
                if ((SendMessageW(c, BM_GETCHECK, 0, 0) == BST_CHECKED) != wanted)
                    SendMessageW(c, BM_CLICK, 0, 0);
            } else if (_wcsicmp(cls, L"ComboBox") == 0) {
                SendMessageW(c, CB_SETCURSEL, std::stoi(s.at(2)), 0);
                SendMessageW(GetParent(c), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(c), CBN_SELCHANGE),
                             reinterpret_cast<LPARAM>(c));
            } else
                SetWindowTextW(
                    c, (s.at(2).find("${") != std::string::npos ? path(s.at(2)).wstring() : widen(s.at(2)))
                           .c_str());
        } else if (op == "function") {
            const auto list = control("KeyList");
            const auto command = command_ids.at(s.at(1));
            int row = -1;
            for (int i = 0; i < ListView_GetItemCount(list); ++i) {
                LVITEMW item{};
                item.mask = LVIF_PARAM;
                item.iItem = i;
                ListView_GetItem(list, &item);
                if (item.lParam == command) {
                    row = i;
                    break;
                }
            }
            require(row >= 0, "Command absent from the settings function list");
            ListView_EnsureVisible(list, row, FALSE);
            ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            require(ListView_GetNextItem(list, -1, LVNI_SELECTED) == row, "Function selection failed");
        } else if (op == "slot") {
            const auto list = control("KeySlots");
            const int index = std::stoi(s.at(1));
            require(index >= 0 && index < SendMessageW(list, LB_GETCOUNT, 0, 0), "Key slot missing");
            SendMessageW(list, LB_SETCURSEL, index, 0);
            SendMessageW(GetParent(list), WM_COMMAND, MAKEWPARAM(preference_id::KeySlots, LBN_SELCHANGE),
                         reinterpret_cast<LPARAM>(list));
        } else if (op == "row") {
            auto c = control(s.at(1));
            const int row = std::stoi(s.at(2));
            require(row >= 0 && row < ListView_GetItemCount(c), "Requested list row is out of range");
            ListView_EnsureVisible(c, row, FALSE);
            ListView_SetItemState(c, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            require(ListView_GetNextItem(c, -1, LVNI_SELECTED) == row,
                    "Requested native list row was not selected");
        } else if (op == "expect_destination" || op == "destination_command") {
            require(s.at(1) == "copy" || s.at(1) == "move", "Unknown destination operation");
            const bool move = s.at(1) == "move";
            const int slot = std::stoi(s.at(2)) - 1;
            require(slot >= 0 && slot < 10, "Destination slot is out of range");
            if (op == "destination_command")
                app.message(WM_COMMAND, (move ? MoveDestBase : CopyDestBase) + slot, 0);
            else {
                const auto expected = s.at(3) == "empty"                        ? std::wstring{}
                                      : s.at(3).find("${") != std::string::npos ? path(s.at(3)).wstring()
                                                                                : widen(s.at(3));
                require((move ? app.settings.move_destinations : app.settings.copy_destinations)[slot] ==
                            expected,
                        "Applied destination differs from expected path");
            }
        } else if (op == "expect_destination_selected") {
            const auto list = control("DestinationList");
            require(ListView_GetItemCount(list) == 20, "Expected ten copy and ten move rows");
            require(ListView_GetNextItem(list, -1, LVNI_SELECTED) == std::stoi(s.at(1)),
                    "Registration did not select the requested destination row");
            const auto edit = control("DestinationEdit");
            require(fully_visible(edit), "Requested destination editor is clipped");
            const auto focus_error = "Requested destination editor does not have focus; actual ID=" +
                                     std::to_string(GetDlgCtrlID(GetFocus()));
            require(GetFocus() == edit, focus_error.c_str());
        } else if (op == "click") {
            const auto target = control(s.at(1));
            SetFocus(target);
            require(fully_visible(target) && IsWindowEnabled(target),
                    "Cannot click a clipped, hidden or disabled control");
            SendMessageW(target, BM_CLICK, 0, 0);
        } else if (op == "seek") {
            own(app.slider);
            SendMessageW(app.slider, TBM_SETPOS, TRUE, std::stoi(s.at(1)));
            SendMessageW(GetParent(app.slider), WM_HSCROLL, TB_THUMBPOSITION,
                         reinterpret_cast<LPARAM>(app.slider));
        } else if (op == "resize")
            SetWindowPos(app.hwnd, nullptr, 0, 0, std::stoi(s.at(1)), std::stoi(s.at(2)),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        else if (op == "sizing")
            app.message(s.at(1) == "begin" ? WM_ENTERSIZEMOVE
                        : s.at(1) == "end" ? WM_EXITSIZEMOVE
                                           : WM_CANCELMODE,
                        0, 0);
        else if (op == "render")
            render_client(app.hwnd);
        else if (op == "expect_empty_checker") {
            require(!app.image, "Expected an empty image viewport");
            const Rect canvas{0, app.top, static_cast<int>(app.view.width),
                              static_cast<int>(app.view.height)};
            render_client(app.hwnd, &canvas);
        } else if (op == "expect_view_background") {
            const Rect canvas{0, app.top, static_cast<int>(app.view.width), static_cast<int>(app.view.height)};
            Rect image_bounds{};
            if (app.image) {
                image_bounds.x = static_cast<int>(std::floor(app.view.left())) - 1;
                image_bounds.y = static_cast<int>(std::floor(app.view.top())) - 1;
                image_bounds.w = static_cast<int>(std::ceil(app.image->width * app.view.scale)) + 3;
                image_bounds.h = static_cast<int>(std::ceil(app.image->height * app.view.scale)) + 3;
                require(image_bounds.w < canvas.w || image_bounds.h < canvas.h,
                        "Background check requires visible space around the image");
            }
            const auto &settings = app.settings;
            require(settings.background_pattern >= 0 && settings.background_pattern <= 4,
                    "Invalid background preset in App settings");
            render_client(app.hwnd, &canvas, std::array{8,16,32,64,128}[settings.background_pattern],
                          settings.background_first, settings.background_second, settings.background_tiled,
                          app.image ? &image_bounds : nullptr, app.image ? &app.renderer.overlay_rect : nullptr);
        } else if (op == "clipboard_path") {
            const auto text = s.size() > 2 && s.at(2) == "quoted"
                                  ? L"\"" + path(s.at(1)).wstring() + L"\"\r\n"
                                  : path(s.at(1)).wstring();
            const auto bytes = (text.size() + 1) * sizeof(wchar_t);
            const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
            require(memory != nullptr, "Cannot allocate test clipboard text");
            const auto data = GlobalLock(memory);
            require(data != nullptr, "Cannot lock test clipboard text");
            memcpy(data, text.c_str(), bytes);
            GlobalUnlock(memory);
            require(OpenClipboard(app.hwnd) != FALSE, "Cannot open private test clipboard");
            EmptyClipboard();
            const bool published = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
            CloseClipboard();
            if (!published)
                GlobalFree(memory);
            require(published, "Cannot publish test clipboard text");
        } else if (op == "expect_clipboard_empty") {
            require(CountClipboardFormats() == 0, "Expected an empty private clipboard");
        } else if (op == "expect_clipboard_text") {
            std::wstring expected = widen(s.at(1));
            if (s.size() > 2) {
                auto expected_path = path(s.at(1));
                expected_path.make_preferred();
                expected = expected_path.wstring();
                if (s.at(2) == "short") {
                    wchar_t short_name[32768]{};
                    require(GetShortPathNameW(expected_path.c_str(), short_name, 32768) > 0,
                            "Cannot obtain the expected Windows short path");
                    expected = short_name;
                } else
                    require(s.at(2) == "path", "Unknown clipboard-text expectation mode");
            }
            require(OpenClipboard(app.hwnd) != FALSE, "Cannot read private clipboard text");
            struct Close {
                ~Close() { CloseClipboard(); }
            } close;
            const auto handle = GetClipboardData(CF_UNICODETEXT);
            require(handle != nullptr, "Private clipboard does not contain Unicode text");
            const auto bytes = GlobalSize(handle);
            require(bytes >= sizeof(wchar_t) && bytes <= 131072 && bytes % sizeof(wchar_t) == 0,
                    "Invalid clipboard text size");
            const auto text = static_cast<const wchar_t *>(GlobalLock(handle));
            require(text != nullptr, "Cannot lock private clipboard text");
            struct Unlock {
                HGLOBAL handle;
                ~Unlock() { GlobalUnlock(handle); }
            } unlock{handle};
            const auto end = std::find(text, text + bytes / sizeof(wchar_t), L'\0');
            require(end != text + bytes / sizeof(wchar_t), "Unterminated clipboard text");
            require(std::wstring(text, end) == expected, "Private clipboard text differs");
        } else if (op == "clipboard_data") {
            const auto data = read_file(path(s.at(2)));
            const UINT format = s.at(1) == "DIBV5" ? CF_DIBV5
                                : s.at(1) == "DIB" ? CF_DIB
                                                   : RegisterClipboardFormatW(widen(s.at(1)).c_str());
            require(format != 0, "Cannot register test clipboard format");
            const auto memory = GlobalAlloc(GMEM_MOVEABLE, data.size());
            require(memory != nullptr, "Cannot allocate test clipboard data");
            const auto destination = GlobalLock(memory);
            require(destination != nullptr, "Cannot lock test clipboard data");
            memcpy(destination, data.data(), data.size());
            GlobalUnlock(memory);
            require(OpenClipboard(app.hwnd) != FALSE, "Cannot open private test clipboard");
            EmptyClipboard();
            const bool published = SetClipboardData(format, memory) != nullptr;
            CloseClipboard();
            if (!published)
                GlobalFree(memory);
            require(published, "Cannot publish test clipboard data");
        } else if (op == "expect_no_overlap") {
            const auto first = control(s.at(1)), second = control(s.at(2));
            require(fully_visible(first) && fully_visible(second), "Controls must both be visible");
            RECT a{}, b{};
            GetWindowRect(first, &a);
            GetWindowRect(second, &b);
            require(!overlaps(a, b), "Native controls overlap");
        } else if (op == "expect_positive") {
            require(value(s.at(1)) > 0, "Expected a positive state value");
        } else if (op == "expect") {
            double expected = s.at(2) == "true" ? 1 : s.at(2) == "false" ? 0 : std::stod(s.at(2));
            const double actual = value(s.at(1));
            if (std::abs(actual - expected) > 1e-6)
                throw std::runtime_error(s.at(1) + " expected " + s.at(2) + ", actual " +
                                         std::to_string(actual));
        } else if (op == "expect_caption") {
            const auto expected = s.at(1) == "@none" ? std::wstring{} : path(s.at(1)).wstring();
            require(app.caption_name() == expected, "Caption filename text differs");
            require(window_text(app.hwnd).find(expected) != std::wstring::npos, "Window title omits caption filename");
        } else if (op == "expect_text") {
            std::wstring actual;
            if (s.at(1) == "player.play" || s.at(1) == "player.loop") {
                wchar_t text[256]{};
                SendMessageW(app.playback.toolbar.hwnd, TB_GETBUTTONTEXTW,
                             s.at(1) == "player.play" ? Play : Loop, reinterpret_cast<LPARAM>(text));
                actual = text;
            } else
                actual = s.at(1) == "title" ? window_text(app.hwnd) : window_text(control(s.at(1)));
            require(actual.find(widen(s.at(2))) != std::wstring::npos, "Expected control text not found");
        } else if (op == "fit_snapshot") {
            require(value("view.actual_fits")==1,"Image does not fit at its actual size");
            capture(app.hwnd,output_path(s.at(1)));
        } else if (op == "snapshot_small") {
            require(!preferences && s.size() == 4, "Small snapshot requires main window and explicit dimensions");
            RECT bounds{};
            require(GetWindowRect(app.hwnd, &bounds) && bounds.right - bounds.left == std::stoi(s.at(2)) &&
                        bounds.bottom - bounds.top == std::stoi(s.at(3)), "Small snapshot dimensions differ");
            capture(app.hwnd, output_path(s.at(1)));
        } else if (op == "snapshot")
            capture(preferences ? preferences->window() : app.hwnd, output_path(s.at(1)));
        else if (op == "pixel_copy") {
            require(app.image != nullptr, "No image");
            auto reference = app.image->frames.at(app.player.frame).pixels;
            auto width = app.image->width, height = app.image->height;
            if (!app.selection.empty()) {
                const auto rect = intersect(app.selection, {0,0,static_cast<int>(width),static_cast<int>(height)});
                require(!rect.empty(), "Selected clipboard rectangle is outside the image");
                std::vector<Pixel> selected(static_cast<size_t>(rect.w) * rect.h);
                for (int y = 0; y < rect.h; ++y)
                    std::copy_n(reference.begin() + static_cast<size_t>(y + rect.y) * width + rect.x,
                                rect.w, selected.begin() + static_cast<size_t>(y) * rect.w);
                reference = std::move(selected); width = rect.w; height = rect.h;
            }
            toolbar(Copy, app.image->animated());
            auto pasted = paste_image(app.hwnd);
            require(pasted.width == width && pasted.height == height && pasted.frames.size() == 1,
                    "Clipboard frame dimensions mismatch");
            require(pasted.frames[0].pixels == reference, "Clipboard frame RGBA mismatch");
        } else if (op == "pixel_save") {
            require(app.image != nullptr, "No image");
            save_image(*app.image, app.player.frame, output_path(s.at(1)));
        } else if (op == "expect_rotation_format") {
            require(app.image && !app.image->path.empty(), "No file image for rotation-format comparison");
            const auto mode = std::stoi(s.at(3));
            const auto &cases = gvn_test::rotation_format_cases();
            const auto item = std::find_if(cases.begin(), cases.end(), [&](const auto &value) {
                return value.kind == s.at(1) && value.shape == s.at(2) && value.mode == mode;
            });
            require(item != cases.end() && app.settings.view_rotation == mode, "Rotation-format case or setting differs");
            const auto decoded = load_source(app.image->path);
            gvn_test::check_rotation_format(*app.image, decoded, *item, app.image->path);
        } else if (op == "expect_image") {
            require(app.image != nullptr, "No image for full-pixel comparison");
            const auto expected = load_source(path(s.at(1)));
            const auto tolerance = std::stoi(s.at(2));
            require(tolerance >= 0 && tolerance <= 2 && expected.width == app.image->width &&
                        expected.height == app.image->height,
                    "Reference image dimensions or tolerance differ");
            const auto &actual = app.image->frames.at(app.player.frame).pixels;
            const auto &reference = expected.frames.front().pixels;
            require(actual.size() == reference.size(), "Reference pixel count differs");
            for (size_t i = 0; i < actual.size(); ++i) {
                const auto a = actual[i], b = reference[i];
                require(std::abs(int(a.r) - b.r) <= tolerance && std::abs(int(a.g) - b.g) <= tolerance &&
                            std::abs(int(a.b) - b.b) <= tolerance && a.a == b.a,
                        "Actual App pixels differ from independent reference");
            }
        } else if (op == "expect_indexed") {
            require(app.image != nullptr, "No image for palette assertion");
            const auto &frame = app.image->frames.at(app.player.frame);
            const auto bits = std::stoul(s.at(1));
            if (!bits)
                require(!frame.indexed, "Expected RGBA without old palette metadata");
            else
                require(frame.indexed && frame.indexed->bits == bits &&
                            frame.indexed->width == app.image->width &&
                            frame.indexed->height == app.image->height &&
                            frame.indexed->matches(frame.pixels),
                        "Current palette/depth/indices do not match displayed RGBA");
        } else if (op == "expect_index") {
            require(app.image != nullptr, "No image for palette index assertion");
            const auto &indexed = app.image->frames.at(app.player.frame).indexed;
            const auto x = std::stoul(s.at(1)), y = std::stoul(s.at(2));
            require(indexed && x < indexed->width && y < indexed->height &&
                        indexed->index(x, y) == std::stoul(s.at(3)),
                    "Unexpected current palette index");
        } else if (op == "expect_pixel") {
            require(app.image != nullptr, "No image");
            const auto x = std::stoul(s.at(1)), y = std::stoul(s.at(2));
            require(x < app.image->width && y < app.image->height, "Pixel outside image");
            const auto actual = app.image->frames.at(app.player.frame).pixels[y * app.image->width + x];
            require(actual == Pixel{static_cast<uint8_t>(std::stoul(s.at(3))),
                                    static_cast<uint8_t>(std::stoul(s.at(4))),
                                    static_cast<uint8_t>(std::stoul(s.at(5))),
                                    static_cast<uint8_t>(std::stoul(s.at(6)))},
                    "Image RGBA differs from expected pixel");
        } else if (op == "export_mouse") {
            const auto child=app.editor_chrome.control(editor_id::Drag);own(child);
            const auto message = s.at(1) == "down"     ? WM_LBUTTONDOWN
                                 : s.at(1) == "move"   ? WM_MOUSEMOVE
                                 : s.at(1) == "cancel" ? WM_CANCELMODE
                                                       : WM_LBUTTONUP;
            const auto flags = message == WM_LBUTTONDOWN || message == WM_MOUSEMOVE ? MK_LBUTTON : 0;
            export_panel_drag=true;
            SendMessageW(child, message, flags, MAKELPARAM(std::stoi(s.at(2)), std::stoi(s.at(3))));
            export_panel_drag=false;
        } else if (op == "persisted") {
            Settings saved;
            saved.load(app.config);
            require(saved.external_applications() == app.settings.external_applications(), "INI external applications differ");
            require(saved.disable_ime == app.settings.disable_ime, "INI IME policy differs");
            require(saved.allow_physical_overflow == app.settings.allow_physical_overflow,
                    "INI edit memory policy differs");
            require(saved.cut_transparent == app.settings.cut_transparent &&
                        saved.cut_color == app.settings.cut_color,
                    "INI cut background differs");
            require(saved.reopen_last == app.settings.reopen_last &&
                        saved.last_file == app.settings.last_file &&
                        saved.last_archive_entry == app.settings.last_archive_entry,
                    "INI startup image differs");
            require(saved.allow_multiple == app.settings.allow_multiple &&
                        saved.activate_reused == app.settings.activate_reused &&
                        saved.activate_dropped == app.settings.activate_dropped,
                    "INI instance/activation options differ");
            require(saved.comic_options() == app.settings.comic_options(), "INI comic options differ");
            require(saved.gif_speed == app.settings.gif_speed, "INI GIF style differs");
            require(saved.view_rotation == app.settings.view_rotation, "INI view rotation differs");
            require(saved.animation_file_loops == app.settings.animation_file_loops,
                    "INI animation repetition policy differs");
            require(saved.saving == app.settings.saving, "INI save format options differ");
            require(saved.background_tiled == app.settings.background_tiled &&
                        saved.background_pattern == app.settings.background_pattern &&
                        saved.background_first == app.settings.background_first &&
                        saved.background_second == app.settings.background_second,
                    "INI viewport background differs from applied settings");
#define FILE_OPTION(field)                                                                                   \
    require(saved.field == app.settings.field, "INI file operation option differs: " #field);
            FILE_OPTION(save_dialog)
            FILE_OPTION(save_as_retarget)
            FILE_OPTION(confirm_overwrite)
            FILE_OPTION(remember_save_folder)
            FILE_OPTION(rename_resort)
            FILE_OPTION(confirm_delete)
            FILE_OPTION(recycle_deleted)
            FILE_OPTION(next_after_fileop)
            FILE_OPTION(confirm_move)
            FILE_OPTION(confirm_copy)
            FILE_OPTION(last_save_folder)
#undef FILE_OPTION
            require(saved.cache_mib == app.settings.cache_mib &&
                        saved.prefetch_enabled == app.settings.prefetch_enabled,
                    "INI cache policy differs from applied settings");
            require(saved.confirm_unsaved == app.settings.confirm_unsaved, "INI confirmation option differs");
            require(saved.end_action == app.settings.effective_end_action() &&
                        saved.list_loop == app.settings.list_loop &&
                        saved.folder_history == app.settings.folder_history &&
                        saved.previous_folder_first == app.settings.previous_folder_first &&
                        saved.adjacent_previous_first == app.settings.adjacent_previous_first &&
                        saved.parent_first == app.settings.parent_first &&
                        saved.boundary_sound == app.settings.boundary_sound &&
                        saved.comic_toolbar_reverse == app.settings.comic_toolbar_reverse &&
                        saved.comic_jump_reverse == app.settings.comic_jump_reverse &&
                        saved.jump_preview == app.settings.jump_preview &&
                        saved.jump_quality == app.settings.jump_quality,
                    "INI movement options differ");
            require(saved.copy_destinations == app.settings.copy_destinations &&
                        saved.move_destinations == app.settings.move_destinations,
                    "INI registered copy/move paths differ");
            require(saved.copy_basic_path == app.settings.copy_basic_path &&
                        saved.move_basic_path == app.settings.move_basic_path &&
                        saved.copy_registered == app.settings.copy_registered &&
                        saved.move_registered == app.settings.move_registered,
                    "INI transfer dialog options differ");
            require(saved.grid == app.settings.grid && saved.background == app.settings.background &&
                        saved.interval == app.settings.interval && saved.slide_fullscreen == app.settings.slide_fullscreen &&
                        saved.slide_loop == app.settings.slide_loop && saved.slide_next_folder == app.settings.slide_next_folder &&
                        saved.slide_subtree == app.settings.slide_subtree && saved.slide_random == app.settings.slide_random,
                    "INI roundtrip differs from applied settings");
            require(saved.sort == app.persisted_settings(app.settings).sort,
                    "INI contains a temporary slideshow sort instead of the persistent order");
            for (const auto &entry : command_catalog())
                require(saved.keys_for(entry.id) == app.settings.keys_for(entry.id),
                        "INI shortcut lists/order differ");
            require(saved.mouse == app.settings.mouse, "INI mouse options/bindings differ");
            require(saved.loading_options() == app.settings.loading_options() &&
                        saved.hidden == app.settings.hidden &&
                        saved.system_files == app.settings.system_files &&
                        saved.protected_files == app.settings.protected_files,
                    "INI loading options differ");
            require(saved.overlay_family == app.settings.overlay_family, "INI overlay font differs");
            for (auto member : {&Settings::overlay_position, &Settings::overlay_background,
                                &Settings::overlay_points, &Settings::overlay_weight, &Settings::overlay_charset})
                require(saved.*member == app.settings.*member, "INI overlay numeric values differ");
            for (auto member : {&Settings::overlay_foreground, &Settings::overlay_backcolor})
                require(saved.*member == app.settings.*member, "INI overlay colors differ");
            for (auto member :
                 {&Settings::overlay_full, &Settings::overlay_window, &Settings::overlay_animation,
                  &Settings::overlay_italic, &Settings::overlay_underline, &Settings::overlay_strikeout})
                require(saved.*member == app.settings.*member, "INI overlay flags differ");
            for (auto member : {&Settings::startup_position, &Settings::startup_state, &Settings::window_x,
                                &Settings::window_y, &Settings::window_width, &Settings::window_height,
                                &Settings::title_mode, &Settings::display_mode, &Settings::full_display_mode,
                                &Settings::full_initial_fit, &Settings::last_state, &Settings::last_x,
                                &Settings::last_y, &Settings::last_width, &Settings::last_height})
                require(saved.*member == app.settings.*member, "INI window/bars values differ");
            for (auto member :
                 {&Settings::scrollbars, &Settings::full_scrollbars, &Settings::window_center,
                  &Settings::window_toolbar_min, &Settings::restore_maximized, &Settings::restore_fullscreen,
                  &Settings::caption, &Settings::outer_frame, &Settings::inner_frame, &Settings::full_topmost,
                  &Settings::full_toolbar, &Settings::full_status, &Settings::full_menu,
                  &Settings::toolbar_center, &Settings::toolbar_line, &Settings::toolbar_large,
                  &Settings::toolbar_auto, &Settings::status_auto, &Settings::last_window_valid})
                require(saved.*member == app.settings.*member, "INI window/bars flags differ");
            for (auto member :
                 {&Settings::fit_max, &Settings::fit_simple, &Settings::display_down, &Settings::display_up,
                  &Settings::full_down, &Settings::full_up, &Settings::zoom_simple, &Settings::zoom_mode,
                  &Settings::zoom_below, &Settings::zoom_above})
                require(saved.*member == app.settings.*member, "INI display/zoom options differ");
            for (auto member :
                 {&Settings::center_image, &Settings::zoom_reduce, &Settings::zoom_enlarge,
                  &Settings::drag_simple, &Settings::full_hide_taskbar, &Settings::full_hide_wait})
                require(saved.*member == app.settings.*member, "INI display/zoom flags differ");
            require(saved.mask_options() == app.settings.mask_options(), "INI mask options differ");
            require(saved.rainbow_preview==app.settings.rainbow_preview,"INI Rainbow preview flag differs");
            require(saved.cp_code==app.settings.cp_code&&saved.cp_preview==app.settings.cp_preview,"INI CP code or preview differs");
            require(saved.palette_preview==app.settings.palette_preview,"INI palette preview flag differs");
            require(saved.color_depth_dither==app.settings.color_depth_dither,"INI color depth dither flag differs");
            require(saved.color_adjust_unified==app.settings.color_adjust_unified&&saved.color_adjust_hue==app.settings.color_adjust_hue&&
                saved.color_adjust_saturation==app.settings.color_adjust_saturation&&saved.color_adjust_lightness==app.settings.color_adjust_lightness&&
                saved.color_adjust_gray==app.settings.color_adjust_gray,"INI color adjustment preferences differ");
        } else if (op == "expect_binding") {
            UINT key{}, mods{};
            require(parse_key(widen(s.at(2)), key, mods), "Invalid expected key");
            require(app.settings.command_for(key, mods) == command_ids.at(s.at(1)),
                    "Applied shortcut does not dispatch the requested command");
        } else if (op == "expect_unbound") {
            UINT key{}, mods{};
            require(parse_key(widen(s.at(1)), key, mods), "Invalid expected key");
            require(app.settings.command_for(key, mods) == 0, "Shortcut should be unassigned");
        } else if (op == "expect_first_key") {
            require(app.settings.first_key(command_ids.at(s.at(1))) == widen(s.at(2)),
                    "Primary shortcut differs");
        } else if (op == "expect_menu_checked" || op == "expect_menu_enabled") {
            const auto state = GetMenuState(app.main_menu, command_ids.at(s.at(1)), MF_BYCOMMAND);
            require(state != static_cast<UINT>(-1), "Expected menu command is missing");
            const bool actual = op == "expect_menu_checked" ? (state & MF_CHECKED) != 0 : !(state & (MF_GRAYED | MF_DISABLED));
            require(actual == (s.at(2) == "true"), "Native menu state differs");
        } else if (op == "expect_menu_key") {
            const UINT id = command_ids.at(s.at(1));
            std::wstring label;
            const auto find = [&](auto &&self, HMENU menu) -> void {
                for (int i = 0; i < GetMenuItemCount(menu); ++i) {
                    wchar_t text[512]{};
                    MENUITEMINFOW item{sizeof(item)};
                    item.fMask = MIIM_ID | MIIM_STRING | MIIM_SUBMENU;
                    item.dwTypeData = text;
                    item.cch = 512;
                    GetMenuItemInfoW(menu, i, TRUE, &item);
                    if (item.wID == id)
                        label = text;
                    if (item.hSubMenu)
                        self(self, item.hSubMenu);
                }
            };
            find(find, app.main_menu);
            require(!label.empty(), "Menu command missing");
            const auto tab = label.find(L'\t');
            require((tab == std::wstring::npos ? L"" : label.substr(tab + 1)) == widen(s.at(2)),
                    "Menu primary shortcut is stale");
        } else if (op == "remember") {
            require(app.image != nullptr, "No pixels to remember");
            remembered[s.at(1)] = app.image->frames.at(app.player.frame).pixels;
        } else if (op == "expect_same") {
            require(app.image != nullptr, "No image");
            require((app.image->frames.at(app.player.frame).pixels == remembered.at(s.at(1))) ==
                        (s.at(2) == "true"),
                    "Current frame pixels differ from the requested comparison");
        } else if (op == "attributes") {
            const auto target = output_path(s.at(1));
            const auto attributes = std::stoul(s.at(2));
            require(attributes == FILE_ATTRIBUTE_NORMAL || attributes == FILE_ATTRIBUTE_HIDDEN ||
                        attributes == FILE_ATTRIBUTE_SYSTEM ||
                        attributes == (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM),
                    "Only normal/hidden/system fixture attributes are supported");
            require(std::filesystem::is_regular_file(target) &&
                        SetFileAttributesW(target.c_str(), attributes),
                    "Cannot set scenario-owned image attributes");
        } else if (op == "copy_fixture") {
            const auto target = output_path(s.at(2));
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(fixtures / widen(s.at(1)), target);
        } else if (op == "mkdir") {
            std::filesystem::create_directories(output_path(s.at(1)));
        } else if (op == "expect_file") {
            const auto target = output_path(s.at(1));
            if (s.at(2) == "absent")
                require(!std::filesystem::exists(target), "Unexpected scenario-owned file exists");
            else
                require(read_file(target) == read_file(fixtures / widen(s.at(2))),
                        "Scenario-owned file differs from independent fixture bytes");
        } else if (op == "save_target") {
            save_target = output_path(s.at(1));
            std::filesystem::create_directories(save_target.parent_path());
        } else if (op == "remember_saving") {
            remembered_saving[s.at(1)] = app.settings.saving;
        } else if (op == "expect_saving") {
            require(app.settings.saving == remembered_saving.at(s.at(1)),
                    "Save settings changed unexpectedly");
        } else if (op == "save_set" || op == "save_expect_control") {
            const auto control = save_widget(s.at(1));
            wchar_t type[80]{};
            GetClassNameW(control, type, 80);
            if (op == "save_set") {
                require(fully_visible(control) && IsWindowEnabled(control), "Save option cannot be operated");
                if (_wcsicmp(type, L"Button") == 0) {
                    const bool wanted = s.at(2) == "true" || s.at(2) == "1";
                    if ((SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED) != wanted)
                        SendMessageW(control, BM_CLICK, 0, 0);
                } else if (_wcsicmp(type, WC_COMBOBOXW) == 0) {
                    require(SendMessageW(control, CB_SETCURSEL, std::stoul(s.at(2)), 0) >= 0,
                            "Save option choice is invalid");
                    SendMessageW(GetParent(control), WM_COMMAND,
                                 MAKEWPARAM(GetDlgCtrlID(control), CBN_SELCHANGE),
                                 reinterpret_cast<LPARAM>(control));
                } else {
                    require(_wcsicmp(type, L"Edit") == 0, "Save option is not editable");
                    require(SetWindowTextW(control, widen(s.at(2)).c_str()) != FALSE,
                            "Save option edit failed");
                }
            } else {
                auto expected = widen(s.at(2));
                std::wstring actual;
                if (_wcsicmp(type, L"Button") == 0) {
                    actual = SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0";
                    if (expected == L"true")
                        expected = L"1";
                    if (expected == L"false")
                        expected = L"0";
                } else if (_wcsicmp(type, WC_COMBOBOXW) == 0)
                    actual = std::to_wstring(SendMessageW(control, CB_GETCURSEL, 0, 0));
                else
                    actual = window_text(control);
                require(actual == expected, "Save control value differs");
            }
        } else if (op == "save_type" || op == "save_expect_type") {
            const auto dialog = save_window();
            const auto combo = GetDlgItem(dialog, cmb1);
            own(combo);
            const std::map<std::string, unsigned> formats{{"Png", 0}, {"Jpeg", 1}, {"Bmp", 2}, {"Gif", 3}};
            if (op == "save_type") {
                SetFocus(combo);
                SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
                SendMessageW(combo, WM_KEYDOWN, VK_HOME, 0);
                for (unsigned i = 0; i < formats.at(s.at(1)); ++i)
                    SendMessageW(combo, WM_KEYDOWN, VK_DOWN, 0);
                SendMessageW(combo, WM_KEYDOWN, VK_RETURN, 0);
            }
            require(SendMessageW(combo, CB_GETCURSEL, 0, 0) == formats.at(s.at(1)),
                    "Cannot choose save format");
        } else if (op == "save_enabled" || op == "save_visible") {
            const auto control = save_widget(s.at(1));
            const bool actual =
                op == "save_enabled" ? IsWindowEnabled(control) != FALSE : locally_visible(control);
            require(actual == (s.at(2) == "true"), "Save control visibility/enabled state differs");
        } else if (op == "save_error_contains") {
            require(window_text(save_widget("Error")).find(widen(s.at(1))) != std::wstring::npos,
                    "Save validation message differs");
        } else if (op == "save_layout") {
            for (const auto &[name, id] : save_ids) {
                const auto control = save_widget(name);
                if (locally_visible(control))
                    require(fully_visible(control), "Save option is clipped by its window");
            }
        } else if (op == "save_filename") {
            const auto filename = std::filesystem::path(widen(s.at(1)));
            require(!filename.empty() && !filename.has_parent_path() && !filename.is_absolute() &&
                        filename != L"." && filename != L"..",
                    "Scripted save names must stay in the scenario-owned save folder");
            const auto dialog = save_window();
            auto control = GetDlgItem(dialog, edt1);
            if (!control) {
                const auto combo = GetDlgItem(dialog, cmb13);
                own(combo);
                control = reinterpret_cast<HWND>(SendMessageW(combo, CBEM_GETEDITCONTROL, 0, 0));
            }
            own(control);
            require(SetWindowTextW(control, widen(s.at(1)).c_str()) != FALSE, "Cannot edit save filename");
        } else if (op == "save_accept" || op == "save_snapshot" || op == "save_cancel") {
            HWND dialog{};
            EnumThreadWindows(
                GetCurrentThreadId(),
                [](HWND window, LPARAM data) -> BOOL {
                    if (window_text(window) == L"画像を保存" && GetDlgItem(window, IDOK)) {
                        *reinterpret_cast<HWND *>(data) = window;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&dialog));
            require(dialog != nullptr, "Actual native image save dialog is not open");
            own(dialog);
            if (op == "save_snapshot")
                capture(dialog, output_path(s.at(1)));
            else {
                const auto id = op == "save_cancel" ? IDCANCEL : IDOK;
                PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
                             reinterpret_cast<LPARAM>(GetDlgItem(dialog, id)));
            }
        } else if (op == "expect_saved") {
            const auto saved = load_image(output_path(s.at(1)));
            require(app.image && saved.frames[0].pixels == app.image->frames[app.player.frame].pixels,
                    "Native save dialog output RGBA differs");
        } else if (op == "ack_error") {
            require(app.smoke_error == "Unhandled application exception" && !errors.empty() &&
                        errors.back().find(widen(s.at(1))) != std::wstring::npos,
                    "No matching expected application error to acknowledge");
            ++acknowledged_application_errors;
            app.smoke_error.clear();
        } else if (op == "edit_budget") {
            if (s.at(1) == "real")
                app.editing.physical_bytes = physical_memory_bytes;
            else {
                const auto bytes = std::stoull(s.at(1));
                app.editing.physical_bytes = [bytes] { return bytes; };
            }
        } else if (op == "edit_fail_alloc") {
            require(s.at(1) == "true" || s.at(1) == "false", "Invalid edit allocation fault option");
            if (s.at(1) == "true")
                app.editing.allocating = [](size_t) { throw std::bad_alloc(); };
            else
                app.editing.allocating = {};
        } else if (op == "ime_seed") {
            require(!app.settings.disable_ime && !ime_probe_context, "IME probe must start with policy off");
            ime_probe_context = ImmCreateContext();
            require(ime_probe_context != nullptr, "Cannot create a private test IME context");
            ImmAssociateContext(app.hwnd, ime_probe_context);
        } else if (op.starts_with("prompt_")) {
            HWND dialog{};
            EnumThreadWindows(
                GetCurrentThreadId(),
                [](HWND window, LPARAM data) -> BOOL {
                    const auto title = window_text(window);
                    if ((title == L"名前変更" || title == L"リサイズ" || title == L"キャンバスサイズ" ||
                         title == L"新規の透明画像" || title == L"範囲選択") &&
                        GetDlgItem(window, 101) && GetDlgItem(window, IDOK)) {
                        *reinterpret_cast<HWND *>(data) = window;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&dialog));
            require(dialog && GetWindow(dialog, GW_OWNER) == app.hwnd && !IsChild(app.hwnd, dialog),
                    "Actual owned text input prompt is not open");
            own(dialog);
            const auto edit = GetDlgItem(dialog, 101);
            own(edit);
            if (op == "prompt_ime") {
                const auto context = ImmGetContext(edit);
                if (context)
                    ImmReleaseContext(edit, context);
                require(context != nullptr, "Rename edit has lost its IME input context");
            } else if (op == "prompt_text") {
                SetWindowTextW(edit, widen(s.at(1)).c_str());
                require(window_text(edit) == widen(s.at(1)), "Rename Unicode text differs");
            } else if (op == "prompt_snapshot")
                capture(dialog, output_path(s.at(1)));
            else if (op == "prompt_accept" || op == "prompt_cancel")
                PostMessageW(dialog, WM_COMMAND, op == "prompt_accept" ? IDOK : IDCANCEL, 0);
            else
                throw std::runtime_error("Unknown prompt operation");
        } else if (op == "focus") {
            const auto target = s.at(1) == "Slider"          ? app.slider
                                : s.at(1) == "PlayerToolbar" ? app.playback.toolbar.hwnd
                                                             : app.hwnd;
            own(target);
            SetFocus(target);
        } else if (op == "ime_processed") {
            require(preferences, "Preferences must be open");
            const auto edit = control("KeyEdit");
            require(GetFocus() == edit, "IME processed key target must have focus");
            SendMessageW(edit, WM_KEYDOWN, VK_PROCESSKEY, 0);
        } else if (op == "capture_vk") {
            require(preferences,"Preferences must be open");
            const auto edit=control("KeyEdit");own(edit);
            const auto key=static_cast<UINT>(std::stoul(s.at(1)));
            const auto mods=static_cast<UINT>(std::stoul(s.at(2)));
            require(key>=VK_VOLUME_MUTE&&key<=VK_LAUNCH_APP2&&mods<=7,
                    "Only multimedia/application keys are allowed in this owned capture probe");
            require(GetFocus()==edit&&window_text(control("KeyCapture"))==L"入力待ち（Escで中止）",
                    "Actual owned shortcut edit must be capturing");
            BYTE before[256]{},state[256]{};GetKeyboardState(before);
            state[VK_CONTROL]=(mods&1)?0x80:0;state[VK_SHIFT]=(mods&2)?0x80:0;
            state[VK_MENU]=(mods&4)?0x80:0;state[key]=0x80;SetKeyboardState(state);
            // Dispatch only to the actual captured edit. Do not generate global
            // input, WM_APPCOMMAND, sound or launch requests.
            SendMessageW(edit,WM_KEYDOWN,key,0);SendMessageW(edit,WM_KEYUP,key,0);
            SetKeyboardState(before);
        } else if (op == "key" || op == "key_ime") {
            UINT key{}, mods{};
            require(parse_key(widen(s.at(1)), key, mods), "Invalid scripted key");
            BYTE before[256]{};
            GetKeyboardState(before);
            BYTE state[256]{};
            state[VK_CONTROL] = (mods & 1) ? 0x80 : 0;
            state[VK_SHIFT] = (mods & 2) ? 0x80 : 0;
            state[VK_MENU] = (mods & 4) ? 0x80 : 0;
            state[key] = 0x80;
            SetKeyboardState(state);
            const auto focus = GetFocus();
            MSG message{};
            DWORD pid{};
            if (focus)
                GetWindowThreadProcessId(focus, &pid);
            message.hwnd = focus && pid == GetCurrentProcessId() ? focus : app.hwnd;
            if (op == "key_ime")
                require(preferences && message.hwnd == control("KeyEdit"), "IME key target must be owned shortcut edit");
            message.message = op == "key_ime" ? WM_IME_KEYDOWN : WM_KEYDOWN;
            message.wParam = key;
            if (!app.route_key(message))
                DispatchMessageW(&message);
            message.message = op == "key_ime" ? WM_IME_KEYUP : WM_KEYUP;
            DispatchMessageW(&message);
            SetKeyboardState(before);
        } else
            throw std::runtime_error("Unknown autopilot operation: " + op);
    }
    void tick() {
        if (done)
            return;
        if (position >= steps.size()) {
            if(selection_rect_dialog){failed=true;results.back().error="Native numeric selection dialog was left open";close_owned_modals();}
            if(selection_units_dialog){failed=true;results.back().error="Native selection units dialog was left open";close_owned_modals();}
            if(printing){failed=true;results.back().error="Native print dialog was left open";close_owned_modals();}
            if(information){failed=true;results.back().error="Native image info dialog was left open";close_owned_modals();}
            if (resize) {failed=true;results.back().error="Native resize dialog was left open";close_owned_modals();}
            if (canvas) {failed=true;results.back().error="Native canvas dialog was left open";close_owned_modals();}
            if (showing_about) {
                failed = true;
                results.back().error = "Native About dialog was left open";
                close_owned_modals();
            }
            if (open_gate && (!open_gate->released || open_gate->forced)) {
                failed = true;
                results.back().error = "Open listing gate was left blocked or required forced release";
                open_gate->release();
                close_owned_modals();
            }
            if (preview_gate && (!preview_gate->released || preview_gate->forced)) {
                failed = true;
                results.back().error = "Preview decoder gate was left blocked or required forced release";
                preview_gate->release();
                close_owned_modals();
            }
            if(cp){failed=true;results.back().error="Native CP dialog was left open";close_owned_modals();}
            if(palette_editor){failed=true;results.back().error="Native palette dialog was left open";close_owned_modals();}
            if(depth_dialog){failed=true;results.back().error="Native color depth dialog was left open";close_owned_modals();}
            if(adjustment_dialog){failed=true;results.back().error="Native color adjustment dialog was left open";close_owned_modals();}
            if (rainbow) {
                failed=true;results.back().error="Native Rainbow dialog was left open";close_owned_modals();
            }
            if (jump) {
                failed = true;
                results.back().error = "Native jump dialog was left open";
                close_owned_modals();
            }
            if (boundary_open) {
                failed = true;
                results.back().error = "Native boundary dialog was left open";
                close_owned_modals();
            }
            if (!app.smoke_error.empty()) {
                failed = true;
                results.back().error = app.smoke_error;
            }
            if (!answers.empty()) {
                failed = true;
                results.back().error = "An explicitly scripted dialog answer was not consumed";
            }
            if (!folder_browse_replies.empty()) {
                failed = true;
                results.back().error = "A scripted folder browser reply was not consumed";
            }
            done = true;
            return;
        }
        const auto step = steps[position];
        if(selection_rect_dialog) {
            if(!selection_rect_dialog->ready()||!IsWindow(selection_rect_dialog->window())||!locally_visible(selection_rect_dialog->window())) {
                if(!rect_wait_started)rect_wait_started=GetTickCount64();if(GetTickCount64()-rect_wait_started<10000)return;
                failed=done=true;results[position].error="Actual numeric selection dialog did not become ready";close_owned_modals();return;
            }
            if(!step[0].starts_with("rect_")&&!step[0].starts_with("prompt_")&&!step[0].starts_with("expect")){failed=done=true;results[position].error="Unexpected numeric selection dialog blocks operation";close_owned_modals();return;}
        }
        rect_wait_started=0;
        if(selection_units_dialog) {
            if(!selection_units_dialog->ready()||!IsWindow(selection_units_dialog->window())||!locally_visible(selection_units_dialog->window())) {
                if(!grid_wait_started)grid_wait_started=GetTickCount64();if(GetTickCount64()-grid_wait_started<10000)return;
                failed=done=true;results[position].error="Actual selection units dialog did not become ready";close_owned_modals();return;
            }
            if(!step[0].starts_with("grid_")&&!step[0].starts_with("expect")){failed=done=true;results[position].error="Unexpected selection units dialog blocks operation";close_owned_modals();return;}
        }
        grid_wait_started=0;
        if(printing) {
            if(!IsWindow(printing->window())||!locally_visible(printing->window())||!printing->control(print_control::Sizing)) {
                if(!print_wait_started)print_wait_started=GetTickCount64();if(GetTickCount64()-print_wait_started<10000)return;
                failed=done=true;results[position].error="Actual print dialog did not become ready";close_owned_modals();return;
            }
            if(!step[0].starts_with("print_")&&!step[0].starts_with("expect")){failed=done=true;results[position].error="Unexpected print dialog blocks operation";close_owned_modals();return;}
        }
        print_wait_started=0;
        if(information) {
            if(!information->ready()||!IsWindow(information->window())||!locally_visible(information->window())) {
                if(!info_wait_started)info_wait_started=GetTickCount64();if(GetTickCount64()-info_wait_started<10000)return;
                failed=done=true;results[position].error="Actual info dialog did not become ready";close_owned_modals();return;
            }
            if(!step[0].starts_with("info_")&&!step[0].starts_with("expect")) {failed=done=true;results[position].error="Unexpected info dialog blocks operation";close_owned_modals();return;}
            if(step[0]=="info_wait"&&information->pending()) {
                if(!info_wait_started)info_wait_started=GetTickCount64();if(GetTickCount64()-info_wait_started<20000)return;
                failed=done=true;results[position].error="Image color count timed out";close_owned_modals();return;
            }
        }
        info_wait_started=0;
        if(resize) {
            if(!resize->ready()||!IsWindow(resize->window())||!locally_visible(resize->window())) {
                if(!resize_wait_started)resize_wait_started=GetTickCount64();
                if(GetTickCount64()-resize_wait_started<10000)return;
                failed=done=true;results[position].error="Actual resize dialog did not become ready";close_owned_modals();return;
            }
            if(!step[0].starts_with("resize_")&&!step[0].starts_with("expect")) {
                failed=done=true;results[position].error="Unexpected resize dialog blocks operation";close_owned_modals();return;
            }
        }
        resize_wait_started=0;
        if(canvas) {
            if(!canvas->ready()||!IsWindow(canvas->window())||!locally_visible(canvas->window())) {
                if(!canvas_wait_started)canvas_wait_started=GetTickCount64();
                if(GetTickCount64()-canvas_wait_started<10000)return;
                failed=done=true;results[position].error="Actual canvas dialog did not become ready";close_owned_modals();return;
            }
            if(!step[0].starts_with("canvas_")&&!step[0].starts_with("color_")&&!step[0].starts_with("expect")) {
                failed=done=true;results[position].error="Unexpected canvas dialog blocks operation";close_owned_modals();return;
            }
        }
        canvas_wait_started=0;
        if (showing_about) {
            const auto window = about_window(false);
            if (!window || !locally_visible(window)) {
                if (!about_wait_started)
                    about_wait_started = GetTickCount64();
                if (GetTickCount64() - about_wait_started < 10000)
                    return;
                failed = done = true;
                results[position].error = "Actual About dialog did not become ready";
                close_owned_modals();
                return;
            }
            if (!step[0].starts_with("about_") && !step[0].starts_with("info_") && !step[0].starts_with("expect")) {
                failed = done = true;
                results[position].error = "Unexpected About dialog blocks operation";
                close_owned_modals();
                return;
            }
        }
        about_wait_started = 0;
        if (boundary_open) {
            const auto window = boundary_window(false);
            bool ready = window && locally_visible(window);
            if (ready)
                try {
                    for (const auto name : {"End", "Folder", "Cancel"}) {
                        const auto button = boundary_button(name);
                        ready = ready && fully_visible(button) && IsWindowEnabled(button);
                    }
                } catch (...) {
                    ready = false;
                }
            if (!ready) {
                ++boundary_visibility_waits;
                if (!boundary_wait_started)
                    boundary_wait_started = GetTickCount64();
                if (GetTickCount64() - boundary_wait_started < 10000)
                    return;
                failed = done = true;
                results[position].error = "Native boundary dialog did not become visible";
                dump_owned_windows();
                if (window)
                    try {
                        capture(window, output / L"failure-boundary.png");
                    } catch (...) {
                    }
                close_owned_modals();
                return;
            }
            if (!step[0].starts_with("boundary_") && !step[0].starts_with("expect")) {
                failed = done = true;
                results[position].error = "Unexpected native boundary dialog blocks the requested operation";
                dump_owned_windows();
                close_owned_modals();
                return;
            }
        }
        boundary_wait_started = 0;
        if(adjustment_dialog) {
            if(!adjustment_dialog->ready()||!IsWindow(adjustment_dialog->window())||!locally_visible(adjustment_dialog->window())) {
                if(!adjustment_wait_started)adjustment_wait_started=GetTickCount64();if(GetTickCount64()-adjustment_wait_started<10000)return;
                failed=done=true;results[position].error="Actual color adjustment dialog did not become ready";dump_owned_windows();close_owned_modals();return;
            }
            if(!step[0].starts_with("adjust_")&&!step[0].starts_with("expect")) {
                failed=done=true;results[position].error="Unexpected color adjustment dialog blocks operation";dump_owned_windows();close_owned_modals();return;
            }
        }
        adjustment_wait_started=0;
        if(depth_dialog) {
            if(!depth_dialog->ready()||!IsWindow(depth_dialog->window())||!locally_visible(depth_dialog->window())) {
                if(!depth_wait_started)depth_wait_started=GetTickCount64();if(GetTickCount64()-depth_wait_started<10000)return;
                failed=done=true;results[position].error="Actual color depth dialog did not become ready";dump_owned_windows();close_owned_modals();return;
            }
            if(!step[0].starts_with("depth_")&&!step[0].starts_with("expect")) {
                failed=done=true;results[position].error="Unexpected color depth dialog blocks operation";dump_owned_windows();close_owned_modals();return;
            }
        }
        depth_wait_started=0;
        if(palette_editor) {
            bool ready=palette_editor->ready()&&IsWindow(palette_editor->window())&&locally_visible(palette_editor->window());
            if(step[0].starts_with("pal_picker_")){const auto picker=palette_picker_window(false);ready=ready&&picker&&locally_visible(picker);}
            if(!ready) {
                if(!palette_wait_started)palette_wait_started=GetTickCount64();if(GetTickCount64()-palette_wait_started<10000)return;
                failed=done=true;results[position].error="Actual palette editor/color picker did not become ready";dump_owned_windows();close_owned_modals();return;
            }
            if(!step[0].starts_with("pal_")&&!step[0].starts_with("expect")) {
                failed=done=true;results[position].error="Unexpected palette editor blocks operation";dump_owned_windows();close_owned_modals();return;
            }
        }
        palette_wait_started=0;
        if(cp) {
            if(!cp->ready()||!IsWindow(cp->window())||!locally_visible(cp->window())) {
                if(!cp_wait_started)cp_wait_started=GetTickCount64();if(GetTickCount64()-cp_wait_started<10000)return;
                failed=done=true;results[position].error="Actual CP dialog did not become ready";dump_owned_windows();close_owned_modals();return;
            }
            if(!step[0].starts_with("cp_")&&!step[0].starts_with("expect")) {
                failed=done=true;results[position].error="Unexpected CP dialog blocks operation";dump_owned_windows();close_owned_modals();return;
            }
        }
        cp_wait_started=0;
        if(rainbow) {
            if(!rainbow->ready()||!IsWindow(rainbow->window())||!locally_visible(rainbow->window())) {
                if(!rainbow_wait_started)rainbow_wait_started=GetTickCount64();
                if(GetTickCount64()-rainbow_wait_started<10000)return;
                failed=done=true;results[position].error="Actual Rainbow dialog did not become ready";
                dump_owned_windows();close_owned_modals();return;
            }
            if(!step[0].starts_with("rainbow_")&&!step[0].starts_with("expect")) {
                failed=done=true;results[position].error="Unexpected Rainbow dialog blocks operation";
                dump_owned_windows();close_owned_modals();return;
            }
        }
        rainbow_wait_started=0;
        if (jump) {
            if (!jump->ready() || !IsWindow(jump->window()) || !locally_visible(jump->window())) {
                if (!jump_wait_started)
                    jump_wait_started = GetTickCount64();
                if (GetTickCount64() - jump_wait_started < 10000)
                    return;
                failed = done = true;
                results[position].error = "Actual jump dialog did not become ready";
                dump_owned_windows();
                close_owned_modals();
                return;
            }
            if (!step[0].starts_with("jump_") && !step[0].starts_with("expect") && step[0] != "wait_jump" &&
                step[0] != "wait_preview_blocked") {
                failed = done = true;
                results[position].error = "Unexpected jump dialog blocks the requested operation";
                dump_owned_windows();
                close_owned_modals();
                return;
            }
        }
        jump_wait_started = 0;
        if (file_transfer && (!IsWindow(file_transfer->window()) || !file_transfer->ready() ||
                              !locally_visible(file_transfer->window()))) {
            ++transfer_visibility_waits;
            if (!transfer_wait_started)
                transfer_wait_started = GetTickCount64();
            if (GetTickCount64() - transfer_wait_started < 10000)
                return;
            failed = done = true;
            results[position].error = "Native file transfer dialog did not become ready";
            dump_owned_windows();
            close_owned_modals();
            if (IsWindow(file_transfer->window()))
                SendMessageW(file_transfer->window(), WM_COMMAND, IDCANCEL, 0);
            return;
        }
        transfer_wait_started = 0;
        // Dialog creation can pump this timer before WM_INITDIALOG returns.
        // Wait only for the actual top-level visibility, then retain strict
        // per-control clipping, focus and enabled-state assertions below.
        if (preferences && (!IsWindow(preferences->window()) || !locally_visible(preferences->window()))) {
            ++settings_visibility_waits;
            if (!settings_wait_started)
                settings_wait_started = GetTickCount64();
            if (GetTickCount64() - settings_wait_started < 10000)
                return;
            failed = done = true;
            results[position].error = "Native settings dialog did not become visible";
            dump_owned_windows();
            if (IsWindow(preferences->window()))
                SendMessageW(preferences->window(), WM_COMMAND, IDCANCEL, 0);
            return;
        }
        settings_wait_started = 0;
        if (step[0] == "picker_wait_closed") {
            if (executable_picker_window(false) || !preferences || !IsWindowEnabled(preferences->window())) {
                if (!wait_started) wait_started = GetTickCount64();
                if (GetTickCount64() - wait_started < 10000) return;
                results[position].error = "Actual OS executable picker did not close";
                failed = done = true; close_owned_modals(); return;
            }
            if (std::filesystem::current_path() != executable_picker_cwd) {
                results[position].error = "OS executable picker changed the process working directory";
                failed = done = true; close_owned_modals(); return;
            }
            wait_started = 0; results[position++].finished = true; return;
        }
        if (step[0].starts_with("picker_")) {
            const auto dialog = executable_picker_window(false);
            const auto button = dialog ? GetDlgItem(dialog, IDOK) : nullptr;
            if (!dialog || !locally_visible(dialog) || !button || !fully_visible(button) || !IsWindowEnabled(button)) {
                if (!wait_started) wait_started = GetTickCount64();
                if (GetTickCount64() - wait_started < 10000) return;
                results[position].error = "Actual OS executable picker did not become ready";
                failed = done = true; dump_owned_windows(); close_owned_modals(); return;
            }
            wait_started = 0;
        }
        if (step[0].starts_with("save_") && step[0] != "save_target") {
            bool ready{};
            EnumThreadWindows(
                GetCurrentThreadId(),
                [](HWND window, LPARAM data) -> BOOL {
                    const auto button = GetDlgItem(window, IDOK);
                    if (window_text(window) == L"画像を保存" && button && fully_visible(button) &&
                        IsWindowEnabled(button)) {
                        *reinterpret_cast<bool *>(data) = true;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&ready));
            if (!ready) {
                if (!wait_started)
                    wait_started = GetTickCount64();
                if (GetTickCount64() - wait_started < 10000)
                    return;
                failed = done = true;
                results[position].error = "Native save dialog did not become ready";
                dump_owned_windows();
                close_owned_modals();
                return;
            }
            wait_started = 0;
        }
        if (step[0].starts_with("font_") || step[0].starts_with("color_")) {
            struct Ready {
                bool color{}, ready{};
            } state{step[0].starts_with("color_")};
            EnumWindows(
                [](HWND window, LPARAM data) -> BOOL {
                    auto &state = *reinterpret_cast<Ready *>(data);
                    DWORD process{};
                    GetWindowThreadProcessId(window, &process);
                    if (process != GetCurrentProcessId() || !locally_visible(window) ||
                        !IsWindowEnabled(window))
                        return TRUE;
                    const int first = state.color ? 706 : cmb1;
                    for (int i = 0; i < 3; ++i) {
                        const auto control = GetDlgItem(window, first + i);
                        if (!control || !fully_visible(control))
                            return TRUE;
                    }
                    state.ready = true;
                    return FALSE;
                },
                reinterpret_cast<LPARAM>(&state));
            if (!state.ready) {
                if (!wait_started)
                    wait_started = GetTickCount64();
                if (GetTickCount64() - wait_started < 10000)
                    return;
                failed = done = true;
                results[position].error = "Native modal dialog did not become visible and ready";
                dump_owned_windows();
                close_owned_modals();
                if (preferences)
                    SendMessageW(preferences->window(), WM_COMMAND, IDCANCEL, 0);
                return;
            }
            wait_started = 0;
        }

        if (step[0] == "wait_ms") {
            uint64_t delay{};
            try {
                size_t used{};
                delay = std::stoull(step.at(1), &used);
                if (used != step.at(1).size() || !delay || delay > 10000)
                    throw std::runtime_error("wait_ms requires 1..10000 milliseconds");
            } catch (const std::exception &error) {
                results[position].error = error.what();
                failed = done = true;
                close_owned_modals();
                return;
            }
            if (!wait_started)
                wait_started = GetTickCount64();
            if (GetTickCount64() - wait_started < delay)
                return;
            wait_started = 0;
            results[position].finished = true;
            ++position;
            return;
        }

        if (step[0] == "wait" || step[0] == "wait_overlay" || step[0] == "wait_cache" ||
            step[0] == "wait_jump" || step[0] == "wait_save" || step[0] == "wait_preview" ||
            step[0] == "wait_preview_blocked" || step[0] == "wait_open_blocked" ||
            step[0] == "wait_animation") {
            const bool waiting = step[0] == "wait_animation"         ? app.loading || app.player.playing
                                 : step[0] == "wait_open_blocked"    ? !open_gate || !open_gate->entered
                                 : step[0] == "wait_preview"         ? app.preview_service != nullptr
                                 : step[0] == "wait_preview_blocked" ? !preview_gate || !preview_gate->entered
                                 : step[0] == "wait_jump"            ? jump && jump->loading()
                                 : step[0] == "wait_save"            ? save_open
                                 : step[0] == "wait_cache" ? app.loading || !app.loader->stats().idle
                                 : step[0] == "wait"       ? app.loading || app.forwarded_file.has_value()
                                                           : !app.renderer.overlay_visible;
            if (waiting) {
                if (!wait_started)
                    wait_started = GetTickCount64();
                if (GetTickCount64() - wait_started < 10000)
                    return;
                failed = true;
                results[position].error = "Timed out waiting for the actual loader";
                done = true;
                close_owned_modals();
                return;
            }
            wait_started = 0;
            results[position].finished = true;
            ++position;
            return;
        }
        const auto i = position++; // Advance before entering the real modal dialog's nested message loop.
        try {
            execute(step);
            results[i].finished = true;
        } catch (const std::exception &e) {
            failed = true;
            results[i].error = e.what();
            done = true;
            dump_owned_windows();
            close_owned_modals();
            if (preferences) {
                try {
                    const auto dialog = font_dialog();
                    capture(dialog, output / L"failure-font.png");
                    SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
                } catch (...) {
                }
                close_owned_modals();
            }
            try {
                capture(adjustment_dialog ? adjustment_dialog->window()
                        : depth_dialog    ? depth_dialog->window()
                        : palette_editor  ? palette_editor->window()
                        : cp              ? cp->window()
                        : rainbow       ? rainbow->window()
                        : jump          ? jump->window()
                        : file_transfer ? file_transfer->window()
                        : preferences   ? preferences->window()
                                        : app.hwnd,
                        output / L"failure.png");
            } catch (...) {
            }
            if (preferences)
                SendMessageW(preferences->window(), WM_COMMAND, IDCANCEL, 0);
            if (file_transfer)
                SendMessageW(file_transfer->window(), WM_COMMAND, IDCANCEL, 0);
        }
    }
    int run(const std::filesystem::path &script) {
        std::ifstream input(script);
        require(input.is_open(), "Scenario missing");
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            steps.push_back(split(line));
            results.push_back({line});
        }
        require(!steps.empty(), "Scenario empty");
        current = this;
        timer = SetTimer(nullptr, 0, 30, [](HWND, UINT, UINT_PTR, DWORD) {
            if (current)
                current->tick();
        });
        require(timer != 0, "Cannot start autopilot timer");
        const auto budget = std::clamp<ULONGLONG>(30000 + steps.size() * 300, 90000, 240000);
        const auto deadline = GetTickCount64() + budget;
        MSG message{};
        while (!done && GetTickCount64() < deadline) {
            if (GetMessageW(&message, nullptr, 0, 0) <= 0)
                break;
            if (app.route_key(message))
                continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        KillTimer(nullptr, timer);
        timer = 0;
        current = nullptr;
        if (!done) {
            failed = true;
            results[std::min(position, results.size() - 1)].error =
                "Scenario deadline exceeded after " + std::to_string(budget) + " ms";
        }
        std::ofstream report(output / L"result.json");
        report << "{\"method\":\"GVN native controls; real modal dialogs; private station; timer-driven "
                  "scenario; PrintWindow/WM_PRINT + shared Direct2D scene; scripted confirmation "
                  "replies; scripted private-desktop cursor position; popup selection, outgoing OLE loop, "
                  "optional new-viewer process launch and folder browser replies substituted and reported; incoming Shell folder data "
                  "delivered to actual registered IDropTarget; real native boundary dialog buttons; "
                  "audio requests counted without playing system sound; navigation bounded to owned "
                  "folders; optional preview decoder gate reported and real fixtures decoded after "
                  "release\",\"passed\":"
               << (failed ? "false" : "true") << ",\"steps\":[";
        for (size_t i = 0; i < results.size(); ++i) {
            if (i)
                report << ',';
            const auto &r = results[i];
            report << "{\"action\":" << quote(r.action)
                   << ",\"completed\":" << (r.finished ? "true" : "false") << ",\"error\":" << quote(r.error)
                   << '}';
        }
        report << "],\"validation_errors\":[";
        for (size_t i = 0; i < errors.size(); ++i) {
            if (i)
                report << ',';
            report << quote(utf8(errors[i]));
        }
        report << "],\"scripted_confirmations\":[";
        for (size_t i = 0; i < confirmations.size(); ++i) {
            if (i)
                report << ',';
            report << quote(utf8(confirmations[i]));
        }
        report << "],\"acknowledged_application_errors\":" << acknowledged_application_errors
               << ",\"popup_count\":" << popup_count << ",\"export_count\":" << export_count
               << ",\"system_window_move_requests_intercepted\":" << window_move_requests
               << ",\"new_viewer_launch_intercepted\":" << (new_window_capture_used ? "true" : "false")
               << ",\"captured_new_viewer_requests\":" << captured_new_windows.size()
               << ",\"settings_visibility_waits\":" << settings_visibility_waits
               << ",\"transfer_visibility_waits\":" << transfer_visibility_waits
               << ",\"folder_browse_substitutions\":" << folder_browse_substitutions
               << ",\"incoming_folder_drops_to_registered_target\":" << incoming_folder_drops
               << ",\"executable_picker_selection_substitutions\":" << external_browse_substitutions
               << ",\"native_executable_pickers\":" << native_executable_pickers
               << ",\"native_executable_accepts\":" << native_executable_accepts
               << ",\"native_executable_cancels\":" << native_executable_cancels
               << ",\"native_picker_capture_warnings\":[";
        for (size_t i = 0; i < native_picker_capture_warnings.size(); ++i) {
            if (i) report << ',';
            report << quote(native_picker_capture_warnings[i]);
        }
        report << ']'
               << ",\"incoming_executable_wm_dropfiles\":" << incoming_executable_drops
               << ",\"missing_combo_capture_rejections\":" << missing_combo_capture_rejections
               << ",\"boundary_dialogs\":" << boundary_dialogs
               << ",\"boundary_beep_requests\":" << boundary_beeps
               << ",\"boundary_visibility_waits\":" << boundary_visibility_waits
               << ",\"preview_decoder_gate_used\":" << (preview_gate ? "true" : "false")
               << ",\"preview_decoder_gate_forced_release\":"
               << (preview_gate && preview_gate->forced ? "true" : "false")
               << ",\"preview_decoder_gate_jobs\":" << (preview_gate ? preview_gate->jobs.load() : 0)
               << ",\"open_listing_gate_used\":" << (open_gate ? "true" : "false")
               << ",\"open_listing_gate_forced_release\":"
               << (open_gate && open_gate->forced ? "true" : "false")
               << ",\"open_listing_gate_jobs\":" << (open_gate ? open_gate->jobs.load() : 0) << "}\n";
        for (const auto &r : results)
            if (!r.error.empty())
                std::cerr << r.action << ": " << r.error << '\n';
        std::cout << (failed ? "FAIL " : "PASS ") << script.filename().string() << " " << position << "/"
                  << steps.size() << " steps\n";
        return failed ? 1 : 0;
    }
};
} // namespace gvn
int wmain(int argc, wchar_t **argv) {
    if (argc != 4)
        return 2;
    gvn_test::ClipboardLock clipboard_lock;
    if (!clipboard_lock)
        return 3;
    auto station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
    if (!station || !gvn_test::noninteractive(station) || !SetProcessWindowStation(station))
        return 3;
    auto desktop = CreateDesktopW(L"GVN-Autopilot", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    if (!desktop || !SetThreadDesktop(desktop))
        return 4;
    if (FAILED(OleInitialize(nullptr)))
        return 5;
    if (!gvn::initialize_controls())
        return 6;
    int result = 1;
    try {
        gvn::Autopilot pilot(std::filesystem::absolute(argv[2]), std::filesystem::absolute(argv[3]));
        result = pilot.run(argv[1]);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
    OleUninitialize();
    return result;
}
