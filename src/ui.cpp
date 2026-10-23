#include "gvn/ui.h"
#include "gvn/transfer.h"
#include "gvn/file_operations.h"
#include "gvn/resources.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <commdlg.h>
#include <array>
#include <cstring>
#include <functional>
#include <span>
#include <map>

namespace gvn {
bool initialize_controls() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS | ICC_TAB_CLASSES |
                                                        ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES};
    return InitCommonControlsEx(&controls) != FALSE;
}
constexpr UINT kPauseIcon = 65000;
const wchar_t *command_label(UINT id) {
    switch (id) {
    case MoreCommands:return L"その他の操作";
    case Open:
        return L"開く (Ctrl+O)";
    case Save:
        return L"保存 (Ctrl+S)";
    case FileDelete:
        return L"ごみ箱へ (Delete)";
    case Copy:
        return L"コピー・アルファ維持 (Ctrl+C)";
    case Paste:
        return L"貼り付け (Ctrl+V)";
    case PasteSelection:return L"選択範囲でペースト";
    case CommitSelection:return L"適用";
    case CancelSelection:return L"キャンセル";
    case TransformSelection:return L"位置・サイズ・角度";
    case Undo:
        return L"元に戻す (Ctrl+Z)";
    case Redo:
        return L"やり直し (Ctrl+Shift+Z)";
    case Info:
        return L"画像情報";
    case ShowPrompt:return L"プロンプト表示";
    case Previous:
        return L"前の画像 (←)";
    case Next:
        return L"次の画像 (→)";
    case ParentFolder:
        return L"親フォルダ (Backspace)";
    case ChildFolder:
        return L"子フォルダ";
    case Slideshow:
        return L"スライドショー (F8)";
    case ZoomIn:
        return L"拡大 (+)";
    case ZoomOut:
        return L"縮小 (-)";
    case FitImage:
        return L"画面に収める (0)";
    case Actual:
        return L"原寸 (1)";
    case Fullscreen:
        return L"全画面 (F11)";
    case Help:
        return L"操作説明 (F1)";
    case Preferences:
        return L"設定";
    case SaveAs:
        return L"名前を付けて保存";
    case Cut:
        return L"切り取り";
    case SelectAll:
        return L"すべて選択";
    case ArmDrag:
        return L"ファイルとしてドラッグ (F6)";
    case SkipBack:
        return L"10枚戻る";
    case SkipNext:
        return L"10枚進む";
    case First:
        return L"最初の画像";
    case Last:
        return L"最後の画像";
    case PreviousFolder:
        return L"前のフォルダ";
    case NextFolder:
        return L"次のフォルダ";
    case Reload:
        return L"再読み込み";
    case Play:
        return L"再生・一時停止";
    case FramePrevious:
        return L"前のフレーム";
    case FrameNext:
        return L"次のフレーム";
    case FirstFrame:
        return L"先頭のフレーム";
    case LastFrame:
        return L"末尾のフレーム";
    case StopAnimation:
        return L"停止して先頭へ";
    case Loop:
        return L"アニメーションのループ";
    case RenameFile:
        return L"名前変更";
    case Jump:
        return L"番号へジャンプ";
    case SelectMode:
        return L"矩形選択／閲覧モードを切り替え (S)";
    case ToggleEditor:return L"画像編集モード";
    case Crop:
        return L"切り出し";
    case RotateRight:
        return L"右90度回転";
    case RotateLeft:
        return L"左90度回転";
    case ToggleMenu:
        return L"メニューバー";
    default:
        return command_name(id);
    }
}
static HBITMAP icon_bitmap(UINT id, int size) {
    // Draw original vector shapes at 4x resolution, then downsample into premultiplied BGRA.
    const int n = size * 4;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), n, -n, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void *memory{};
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP source = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &memory, nullptr, 0);
    auto old = SelectObject(dc, source);
    auto *pixels = static_cast<uint32_t *>(memory);
    std::fill_n(pixels, static_cast<size_t>(n) * n, 0x00ff00ff);
    SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowExtEx(dc, 20, 20, nullptr);
    SetViewportExtEx(dc, n, n, nullptr);
    SetBkMode(dc, TRANSPARENT);
    auto line = [&](int x, int y, int xx, int yy, COLORREF color = RGB(44, 83, 133), int width = 2) {
        HPEN pen = CreatePen(PS_SOLID, width, color);
        auto previous = SelectObject(dc, pen);
        MoveToEx(dc, x, y, nullptr);
        LineTo(dc, xx, yy);
        SelectObject(dc, previous);
        DeleteObject(pen);
    };
    auto shape = [&](std::initializer_list<POINT> points, COLORREF fill, COLORREF stroke) {
        HPEN pen = CreatePen(PS_SOLID, 1, stroke);
        HBRUSH brush = CreateSolidBrush(fill);
        auto p = SelectObject(dc, pen), b = SelectObject(dc, brush);
        Polygon(dc, points.begin(), static_cast<int>(points.size()));
        SelectObject(dc, p);
        SelectObject(dc, b);
        DeleteObject(pen);
        DeleteObject(brush);
    };
    auto rectangle = [&](int x, int y, int xx, int yy, COLORREF fill, COLORREF stroke = RGB(65, 88, 122)) {
        shape({{x, y}, {xx, y}, {xx, yy}, {x, yy}}, fill, stroke);
    };
    auto circle = [&](int x, int y, int xx, int yy, COLORREF fill, COLORREF stroke) {
        HPEN pen = CreatePen(PS_SOLID, 1, stroke);
        HBRUSH brush = CreateSolidBrush(fill);
        auto p = SelectObject(dc, pen), b = SelectObject(dc, brush);
        Ellipse(dc, x, y, xx, yy);
        SelectObject(dc, p);
        SelectObject(dc, b);
        DeleteObject(pen);
        DeleteObject(brush);
    };
    constexpr COLORREF blue = RGB(32, 123, 206), green = RGB(47, 151, 61), red = RGB(219, 63, 61),
                       paper = RGB(249, 252, 255);
    switch (id) {
    case ShowPrompt:
        rectangle(3,2,16,18,paper);line(6,6,13,6,blue,1);line(6,9,13,9,blue,1);line(6,12,11,12,blue,1);line(12,16,18,11,green,2);break;
    case ToolPen:
        line(4,16,15,4,red,4);circle(3,14,7,18,red,red);break;
    case ToolText:
        line(3,4,17,4,blue,2);line(10,4,10,17,blue,2);line(6,17,14,17,blue,2);break;
    case ToolCrop:
        line(5,2,5,15,blue,2);line(5,15,18,15,blue,2);line(2,5,15,5,blue,2);line(15,5,15,18,blue,2);break;
    case ToggleEditor:
        rectangle(2,3,16,17,paper);line(4,15,15,4,blue,3);shape({{3,17},{4,12},{8,16}},RGB(245,181,63),RGB(122,87,25));break;
    case SelectRectangle:case SelectAll:case ClearSelection:case InvertSelection:
        for(int tick=3;tick<17;tick+=4){line(tick,3,tick+2,3,blue,1);line(tick,17,tick+2,17,blue,1);line(3,tick,3,tick+2,blue,1);line(17,tick,17,tick+2,blue,1);}
        if(id==SelectAll){line(7,10,9,13,green,2);line(9,13,14,7,green,2);}
        if(id==ClearSelection){line(7,7,13,13,red,2);line(13,7,7,13,red,2);}
        if(id==InvertSelection){shape({{4,4},{16,4},{4,16}},blue,blue);}
        break;
    case SelectFreehand:
        line(4,6,8,3,blue,1);line(8,3,15,4,blue,1);line(15,4,17,11,blue,1);line(17,11,12,16,blue,1);line(12,16,4,14,blue,1);line(4,14,2,9,blue,1);line(2,9,4,6,blue,1);circle(2,13,6,17,paper,blue);break;
    case SelectPolygon:
        line(3,15,6,3,blue,1);line(6,3,17,8,blue,1);line(17,8,3,15,blue,1);for(auto p:{POINT{3,15},POINT{6,3},POINT{17,8}})rectangle(p.x-1,p.y-1,p.x+1,p.y+1,paper);break;
    case SelectMove:
        line(10,2,10,18,blue,1);line(2,10,18,10,blue,1);
        shape({{10,1},{7,5},{13,5}},blue,blue);shape({{10,19},{7,15},{13,15}},blue,blue);shape({{1,10},{5,7},{5,13}},blue,blue);shape({{19,10},{15,7},{15,13}},blue,blue);break;
    case ToolPan:
        shape({{5,17},{2,10},{3,8},{7,11},{7,3},{9,2},{10,8},{11,2},{13,3},{13,8},{15,4},{17,5},{17,13},{14,18}},RGB(255,224,172),RGB(145,108,63));break;
    case DeleteSelection:
        rectangle(3,3,17,17,paper);line(6,6,14,14,red,2);line(14,6,6,14,red,2);break;
    case PasteSelection:
        rectangle(3,3,13,17,paper);rectangle(9,8,18,17,RGB(127,215,229));line(11,12,16,12,blue,1);line(14,9,14,15,blue,1);break;
    case Resize:case Canvas:
        rectangle(3,4,15,16,paper);line(6,14,17,3,blue,2);line(12,3,17,3,blue,2);line(17,3,17,8,blue,2);if(id==Canvas){line(2,2,2,18,green,1);line(2,18,18,18,green,1);}break;
    case Crop:
        line(5,1,5,15,RGB(54,76,92),2);line(5,15,19,15,RGB(54,76,92),2);
        line(1,5,15,5,RGB(54,76,92),2);line(15,5,15,19,RGB(54,76,92),2);line(7,13,13,7,green,1);break;
    case RotateRight:
        rectangle(3,10,11,18,paper);line(3,8,4,5,blue,2);line(4,5,8,2,blue,2);line(8,2,13,3,blue,2);line(13,3,16,7,blue,2);
        shape({{18,4},{17,11},{11,8}},green,green);break;
    case FlipH:case FlipV:
        shape({{3,3},{8,10},{3,17}},blue,blue);shape({{17,3},{12,10},{17,17}},RGB(136,210,243),blue);line(10,2,10,18,green,1);if(id==FlipV)line(2,10,18,10,green,1);break;
    case Brightness:
        circle(5,5,15,15,RGB(255,217,65),RGB(205,148,13));for(auto p:{POINT{10,1},POINT{10,19},POINT{1,10},POINT{19,10}})line(10+(p.x-10)*2/3,10+(p.y-10)*2/3,p.x,p.y,RGB(205,148,13),1);break;
    case ColorAdjust:
        circle(2,2,12,12,RGB(244,91,83),red);circle(8,2,18,12,RGB(103,206,94),green);circle(5,8,15,18,RGB(67,170,235),blue);break;
    case Levels:
        line(2,18,18,18,blue,1);for(int bar=3;bar<18;bar+=3)rectangle(bar,16-(bar*7)%14,bar+2,17,RGB(83,167,208));break;
    case CommitSelection:
        line(3,10,8,15,green,3);line(8,15,18,4,green,3);break;
    case CancelSelection:
        line(4,4,16,16,red,3);line(16,4,4,16,red,3);break;
    case TransformSelection:
        rectangle(4,4,16,16,paper);rectangle(2,2,6,6,blue);rectangle(14,14,18,18,blue);break;
    case Open:
        shape({{2, 6}, {2, 4}, {8, 4}, {10, 6}, {17, 6}, {17, 16}, {2, 16}}, RGB(255, 205, 80),
              RGB(164, 123, 30));
        shape({{2, 9}, {5, 7}, {19, 7}, {16, 16}, {2, 16}}, RGB(255, 225, 119), RGB(164, 123, 30));
        line(10, 3, 10, 11, green);
        line(7, 6, 10, 3, green);
        line(10, 3, 13, 6, green);
        break;
    case Save:
        rectangle(3, 2, 17, 18, RGB(105, 128, 202));
        rectangle(6, 3, 14, 8, paper);
        rectangle(6, 12, 14, 18, paper);
        line(12, 3, 12, 7);
        break;
    case FileDelete:
        line(4, 4, 16, 16, red, 3);
        line(16, 4, 4, 16, red, 3);
        break;
    case Copy:
        rectangle(2, 2, 12, 14, paper);
        rectangle(7, 6, 17, 18, paper);
        line(9, 9, 15, 9, blue, 1);
        line(9, 12, 15, 12, blue, 1);
        break;
    case Paste:
        rectangle(4, 4, 16, 18, RGB(247, 196, 85));
        rectangle(7, 2, 13, 6, paper);
        rectangle(7, 8, 14, 16, paper);
        break;
    case ArmDrag:
        rectangle(3, 3, 12, 17, paper);
        line(9, 11, 17, 3, green, 2);
        line(12, 3, 17, 3, green, 2);
        line(17, 3, 17, 8, green, 2);
        break;
    case Undo:
        line(16, 14, 16, 8, RGB(75, 75, 75));
        line(16, 8, 6, 8, RGB(75, 75, 75));
        shape({{3, 8}, {9, 3}, {9, 13}}, RGB(75, 75, 75), RGB(75, 75, 75));
        break;
    case Redo:
        line(4,14,4,8,RGB(75,75,75));line(4,8,14,8,RGB(75,75,75));
        shape({{17,8},{11,3},{11,13}},RGB(75,75,75),RGB(75,75,75));break;
    case Previous:
    case Next:
    case ParentFolder:
    case ChildFolder: {
        const bool horizontal = id == Previous || id == Next;
        circle(2, 2, 18, 18, horizontal ? blue : green, horizontal ? blue : green);
        if (horizontal) {
            const int direction = id == Previous ? -1 : 1;
            shape({{10 + direction * 4, 10}, {10 - direction * 2, 6}, {10 - direction * 2, 14}}, paper,
                  paper);
            line(10 - direction * 4, 6, 10 - direction * 4, 14, paper, 1);
        } else {
            const int direction = id == ParentFolder ? -1 : 1;
            shape({{10, 10 + direction * 4}, {6, 10 - direction * 2}, {14, 10 - direction * 2}}, paper,
                  paper);
        }
        break;
    }
    case Slideshow:
        rectangle(2, 3, 18, 14, paper);
        line(10, 14, 10, 18);
        line(6, 18, 14, 18);
        shape({{8, 6}, {13, 9}, {8, 12}}, green, green);
        break;
    case Play:
    case kPauseIcon:
    case StopAnimation:
    case FramePrevious:
    case FrameNext:
    case FirstFrame:
    case LastFrame:
        circle(1, 1, 19, 19, blue, RGB(25, 95, 165));
        if (id == Play)
            shape({{8, 5}, {15, 10}, {8, 15}}, paper, paper);
        else if (id == kPauseIcon) {
            rectangle(6, 5, 8, 15, paper, paper);
            rectangle(12, 5, 14, 15, paper, paper);
        } else if (id == StopAnimation)
            rectangle(6, 6, 14, 14, paper, paper);
        else {
            const bool previous = id == FramePrevious || id == FirstFrame;
            const bool endpoint = id == FirstFrame || id == LastFrame;
            auto px = [&](int x) { return previous ? 20 - x : x; };
            shape({{px(5), 5}, {px(11), 10}, {px(5), 15}}, paper, paper);
            line(px(14), 5, px(14), 15, paper, 1);
            if (endpoint)
                shape({{px(9), 5}, {px(14), 10}, {px(9), 15}}, paper, paper);
        }
        break;
    case Loop:
        line(4, 6, 15, 6, green, 2);
        line(15, 6, 15, 11, green, 2);
        shape({{12, 9}, {18, 9}, {15, 13}}, green, green);
        line(16, 14, 5, 14, green, 2);
        line(5, 14, 5, 9, green, 2);
        shape({{2, 11}, {8, 11}, {5, 7}}, green, green);
        break;
    case ZoomIn:
    case ZoomOut:
        circle(2, 2, 13, 13, RGB(225, 247, 255), blue);
        line(12, 12, 18, 18, blue, 3);
        line(4, 7, 11, 7, red, 1);
        if (id == ZoomIn)
            line(7, 4, 7, 11, red, 1);
        break;
    case FitImage:
    case Actual:
    case Fullscreen:
        rectangle(3, 4, 17, 16, paper);
        shape({{5, 14}, {9, 8}, {12, 11}, {14, 7}, {16, 14}}, RGB(94, 176, 103), green);
        if (id == Actual) {
            line(10, 6, 10, 14, blue, 2);
        } else {
            line(1, 6, 1, 2, blue, 1);
            line(1, 2, 5, 2, blue, 1);
            line(15, 2, 19, 2, blue, 1);
            line(19, 2, 19, 6, blue, 1);
            line(1, 14, 1, 18, blue, 1);
            line(1, 18, 5, 18, blue, 1);
            line(15, 18, 19, 18, blue, 1);
            line(19, 14, 19, 18, blue, 1);
        }
        break;
    case MoreCommands:for(int x:{4,10,16})circle(x-1,9,x+1,11,RGB(50,55,63),RGB(50,55,63));break;
    case Preferences:
        circle(3, 3, 17, 17, RGB(162, 174, 190), RGB(72, 86, 107));
        circle(7, 7, 13, 13, paper, RGB(72, 86, 107));
        for (int j = 0; j < 4; ++j) {
            const int x = j % 2 ? 16 : 2, y = j / 2 ? 16 : 2;
            rectangle(x - 1, y - 1, x + 2, y + 2, RGB(162, 174, 190));
        }
        break;
    default:
        circle(2, 2, 18, 18, blue, RGB(25, 95, 165));
        if (id == Info) {
            line(10, 9, 10, 15, paper);
            circle(9, 5, 11, 7, paper, paper);
        } else {
            line(7, 6, 12, 6, paper);
            line(12, 6, 13, 8, paper);
            line(13, 8, 10, 11, paper);
            line(10, 11, 10, 13, paper);
            line(10, 15, 10, 17, paper);
        }
        break;
    }
    GdiFlush();
    BITMAPINFO out_info{};
    out_info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void *output{};
    HBITMAP result = CreateDIBSection(dc, &out_info, DIB_RGB_COLORS, &output, nullptr, 0);
    auto *target = static_cast<uint32_t *>(output);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            uint32_t a{}, r{}, g{}, b{};
            for (int yy = 0; yy < 4; ++yy)
                for (int xx = 0; xx < 4; ++xx) {
                    const auto p = pixels[static_cast<size_t>(y * 4 + yy) * n + x * 4 + xx] & 0xffffff;
                    if (p == 0xff00ff)
                        continue;
                    a += 255;
                    r += (p >> 16) & 255;
                    g += (p >> 8) & 255;
                    b += p & 255;
                }
            target[y * size + x] = ((a / 16) << 24) | ((r / 16) << 16) | ((g / 16) << 8) | (b / 16);
        }
    const auto ink=ui_palette().text;
    for(int pixel_index=0;pixel_index<size*size;++pixel_index){const auto p=target[pixel_index],coverage=p>>24;const auto brightness=(((p>>16)&255)*30+((p>>8)&255)*59+(p&255)*11)/100;
        // Keep the original glyph's white cut-outs and its antialiased coverage.
        const auto a=coverage>brightness?std::min(coverage,2*(coverage-brightness)):0;target[pixel_index]=(a<<24)|((GetRValue(ink)*a/255)<<16)|((GetGValue(ink)*a/255)<<8)|(GetBValue(ink)*a/255);}
    SelectObject(dc, old);
    DeleteObject(source);
    DeleteDC(dc);
    return result;
}
static constexpr UINT buttons[]{
    Open, Save,         FileDelete,  0, Copy,      Paste, ArmDrag, Undo,    0,        Info, ShowPrompt,  Previous,
    Next, ParentFolder, ChildFolder, 0, Slideshow, 0,     ZoomIn,  ZoomOut, FitImage, Actual, Fullscreen,
    0,    ToggleEditor, Preferences,  Help,MoreCommands};
static constexpr UINT tool_buttons[]{ToolPan,ToolPen,ToolText,SelectMove,SelectRectangle,SelectFreehand,SelectPolygon,0,
    PasteSelection,ToolCrop,Resize,Canvas,0,RotateRight,FlipH,FlipV,0,Brightness,ColorAdjust,Levels,0,
    SelectAll,InvertSelection,ClearSelection,DeleteSelection};
static constexpr UINT player_buttons[]{FirstFrame, FramePrevious, Play, StopAnimation, FrameNext, LastFrame,
                                       0,          Loop,          0,    Copy,          ArmDrag};
HICON window_icon(int size) {
    const auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_GVN_APP),
                                                   IMAGE_ICON, size, size, 0));
    if (icon)
        return icon; // Owned handle, paired with the existing DestroyIcon calls.
    // UI test hosts do not embed the application's resources.
    return command_icon(FitImage, size);
}
HICON command_icon(UINT command, int size, bool disabled) {
    const auto bitmap = icon_bitmap(command, size);
    if (disabled) {
        DIBSECTION section{};
        if (GetObjectW(bitmap, sizeof(section), &section) && section.dsBm.bmBits) {
            auto pixels = static_cast<uint32_t *>(section.dsBm.bmBits);
            for (int i = 0; i < size * size; ++i) {
                const auto p = pixels[i];
                const auto gray = (((p >> 16) & 255) * 30 + ((p >> 8) & 255) * 59 + (p & 255) * 11) / 100;
                pixels[i] = (p & 0xff000000) | (gray << 16) | (gray << 8) | gray;
            }
        }
    }
    std::vector<uint8_t> bits(static_cast<size_t>((size + 15) / 16) * 2 * size);
    auto mask = CreateBitmap(size, size, 1, 1, bits.data());
    ICONINFO info{TRUE, 0, 0, mask, bitmap};
    const auto icon = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(bitmap);
    return icon;
}
IconToolbar::~IconToolbar() {
    if (images)
        ImageList_Destroy(images);
}
static void draw_toolbar_host(HWND hwnd, HDC dc) {
    RECT area{};
    GetClientRect(hwnd, &area);
    FillRect(dc, &area, ui_brush(ui_palette().surface));
    if (GetWindowLongPtrW(hwnd, GWLP_USERDATA)) {
        area.bottom = 2;
        DrawEdge(dc, &area, EDGE_ETCHED, BF_TOP);
    }
}
static LRESULT CALLBACK toolbar_host_proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COMMAND || message == WM_NOTIFY || message == WM_CONTEXTMENU)
        return SendMessageW(GetParent(hwnd), message, w, l);
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(hwnd, &paint);
        draw_toolbar_host(hwnd, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT) {
        draw_toolbar_host(hwnd, reinterpret_cast<HDC>(w));
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
static LRESULT CALLBACK tools_host_proc(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    auto tools=reinterpret_cast<IconToolbar *>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE){tools=static_cast<IconToolbar *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(tools));}
    if(tools&&message==WM_VSCROLL){tools->scroll_tools(LOWORD(w));return 0;}
    if(tools&&message==WM_MOUSEWHEEL){tools->scroll_tools(100,GET_WHEEL_DELTA_WPARAM(w));return 0;}
    return toolbar_host_proc(hwnd,message,w,l);
}
void IconToolbar::create(HWND parent, bool playback,bool editing_tools) {
    player = playback;
    tools=editing_tools;
    if (!player) {
        WNDCLASSW cls{};
        cls.lpfnWndProc = tools?tools_host_proc:toolbar_host_proc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.lpszClassName = tools?L"GVN.ToolsHost":L"GVN.ToolbarHost";
        RegisterClassW(&cls);
        host = CreateWindowExW(0, cls.lpszClassName, L"", WS_CHILD | (tools?0:WS_VISIBLE) | WS_CLIPCHILDREN, 0, 0, 1, 1,
                               parent, nullptr, cls.hInstance, tools?this:nullptr);
        if(tools){tools_scroll=CreateWindowExW(0,L"SCROLLBAR",L"",WS_CHILD|WS_TABSTOP|SBS_VERT,0,0,1,1,host,nullptr,cls.hInstance,nullptr);if(!tools_scroll)throw std::runtime_error("Cannot create tools scrollbar");}
        parent = host;
    }
    hwnd = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                           WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_TOOLTIPS |
                               (player ? 0 : TBSTYLE_WRAPABLE) | CCS_NODIVIDER | CCS_NORESIZE |
                               CCS_NOPARENTALIGN,
                           0, 0, 0, 0, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(hwnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(hwnd, TB_SETUNICODEFORMAT, TRUE, 0);apply_ui_theme(bar_window());
}
void IconToolbar::rebuild() {
    measured_width=-1;tool_content=0;texts.clear();palette_generation=ui_theme_generation();
    struct Saved {
        BYTE state{};
        std::wstring text;
    };
    std::map<UINT, Saved> saved;
    for (int i = 0; i < SendMessageW(hwnd, TB_BUTTONCOUNT, 0, 0); ++i) {
        TBBUTTON button{};
        SendMessageW(hwnd, TB_GETBUTTON, i, reinterpret_cast<LPARAM>(&button));
        if (!button.idCommand)
            continue;
        const auto length = SendMessageW(hwnd, TB_GETBUTTONTEXTW, button.idCommand, 0);
        std::wstring label(static_cast<size_t>(std::max<LRESULT>(0, length)) + 1, L'\0');
        if (length >= 0)
            SendMessageW(hwnd, TB_GETBUTTONTEXTW, button.idCommand, reinterpret_cast<LPARAM>(label.data()));
        saved[button.idCommand] = {button.fsState, label.c_str()};
    }
    while (SendMessageW(hwnd, TB_BUTTONCOUNT, 0, 0) > 0)
        SendMessageW(hwnd, TB_DELETEBUTTON, 0, 0);
    built_large = large;built_floating=floating_mode;
    const int icon = MulDiv(ui_icon_size(large), dpi, 96), button = MulDiv(ui_button_size(large), dpi, 96);
    HIMAGELIST next = ImageList_Create(icon, icon, ILC_COLOR32, 24, 0);
    SendMessageW(hwnd, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(next));
    if (images)
        ImageList_Destroy(images);
    images = next;
    SendMessageW(hwnd, TB_SETBITMAPSIZE, 0, MAKELPARAM(icon, icon));
    SendMessageW(hwnd, TB_SETBUTTONSIZE, 0, MAKELPARAM(button, button));
    SendMessageW(hwnd, TB_SETMAXTEXTROWS, floating_mode&&!tools?1:0, 0);
    auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);SetWindowLongPtrW(hwnd,GWL_STYLE,style|TBSTYLE_LIST);
    SendMessageW(hwnd,TB_SETEXTENDEDSTYLE,0,TBSTYLE_EX_MIXEDBUTTONS);
    std::vector<UINT> items;
    if(tools)items.assign(std::begin(tool_buttons),std::end(tool_buttons));
    else if(player)items.assign(std::begin(player_buttons),std::end(player_buttons));
    else {items.assign(std::begin(buttons),std::end(buttons));if(floating_mode)items.insert(items.end(),{0,TransformSelection});}
    for (UINT id : items) {
        TBBUTTON item{};
        if (!id) {
            item.fsStyle = BTNS_SEP;
            item.iBitmap = MulDiv(7, dpi, 96);
        } else {
            const auto bitmap = icon_bitmap(id, icon);
            item.iBitmap = ImageList_Add(images, bitmap, nullptr);
            if (id == Play)
                play_bitmap = item.iBitmap;
            DeleteObject(bitmap);
            item.idCommand = id;
            item.fsState = saved.contains(id) ? saved[id].state : TBSTATE_ENABLED;
            item.fsStyle = (id == Loop || id == Slideshow || id==ToggleEditor || (tools&&(id==ToolPan||id==ToolPen||id==ToolText||id==ToolCrop||(id>=SelectRectangle&&id<=SelectMove)))) ? BTNS_CHECK : BTNS_BUTTON;
            if(id==CommitSelection||id==CancelSelection||id==TransformSelection)item.fsStyle|=BTNS_AUTOSIZE|BTNS_SHOWTEXT;
            item.iString =
                reinterpret_cast<INT_PTR>(saved.contains(id) ? saved[id].text.c_str() : command_label(id));
            texts[id]=reinterpret_cast<const wchar_t *>(item.iString);
        }
        SendMessageW(hwnd, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&item));
    }
    if (player) {
        const auto pause = icon_bitmap(kPauseIcon, icon);
        pause_bitmap = ImageList_Add(images, pause, nullptr);
        DeleteObject(pause);
        playing_bitmap_current=false;playing(is_playing);
    }
}
namespace {
void show_control(HWND window,bool visible){
    if(bool(GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)!=visible)
        ShowWindow(window,visible?SW_SHOWNA:SW_HIDE);
}
void place_control(HWND window,int x,int y,int width,int height){
    RECT previous{};GetWindowRect(window,&previous);
    MapWindowPoints(nullptr,GetParent(window),reinterpret_cast<POINT *>(&previous),2);
    const RECT next{x,y,x+width,y+height};
    if(!EqualRect(&previous,&next))SetWindowPos(window,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
}
}
int IconToolbar::layout_tools(UINT new_dpi,int top,int height,bool visible){
    if(!hwnd)return 0;
    if(!visible){show_control(host,false);return 0;}
    if(dpi!=int(new_dpi)||large!=built_large||palette_generation!=ui_theme_generation()){dpi=int(new_dpi);rebuild();}
    if(!tool_content){
        RECT bounds{};const int count=int(SendMessageW(hwnd,TB_BUTTONCOUNT,0,0));
        SendMessageW(hwnd,TB_SETROWS,MAKEWPARAM(count,TRUE),reinterpret_cast<LPARAM>(&bounds));
        for(int n=0;n<count;++n){RECT item{};if(SendMessageW(hwnd,TB_GETITEMRECT,n,reinterpret_cast<LPARAM>(&item)))bounds.bottom=std::max(bounds.bottom,item.bottom);}
        tool_content=std::max(1L,bounds.bottom);
    }
    tool_height=std::max(1,height);
    const int button=MulDiv(ui_button_size(large),dpi,96);
    tool_width=button+4+(tool_content>tool_height?GetSystemMetricsForDpi(SM_CXVSCROLL,new_dpi):0);
    tool_scroll=std::clamp(tool_scroll,0,std::max(0,tool_content-tool_height));
    SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,tool_content-1,UINT(tool_height),tool_scroll};ui_set_scroll_info(tools_scroll,info);
    place_control(host,0,top,tool_width,tool_height);place_control(hwnd,1,-tool_scroll,button+2,tool_content);
    if(tool_content>tool_height)place_control(tools_scroll,button+4,0,tool_width-button-4,tool_height);
    show_control(tools_scroll,tool_content>tool_height);show_control(host,true);show_control(hwnd,true);return tool_width;
}
void IconToolbar::scroll_tools(UINT action,int delta){
    const int step=MulDiv(ui_button_size(large),dpi,96);int next=tool_scroll;
    switch(action){case SB_TOP:next=0;break;case SB_BOTTOM:next=tool_content;break;case SB_LINEUP:next-=step;break;case SB_LINEDOWN:next+=step;break;case SB_PAGEUP:next-=tool_height;break;case SB_PAGEDOWN:next+=tool_height;break;
    case SB_THUMBTRACK:case SB_THUMBPOSITION:{SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(tools_scroll,SB_CTL,&info);next=info.nTrackPos;break;}
    case 100:next-=delta/WHEEL_DELTA*step*3;break;default:return;}
    tool_scroll=std::clamp(next,0,std::max(0,tool_content-tool_height));SCROLLINFO info{sizeof(info),SIF_POS};info.nPos=tool_scroll;ui_set_scroll_info(tools_scroll,info);SetWindowPos(hwnd,nullptr,1,-tool_scroll,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
}
int IconToolbar::measure_height(int width, UINT new_dpi) {
    if (!hwnd)
        return 0;
    if (dpi != static_cast<int>(new_dpi) || large != built_large || floating_mode!=built_floating || palette_generation!=ui_theme_generation()) {
        dpi = new_dpi;
        rebuild();
    }
    const int divider = !player && line ? std::max(2, MulDiv(2, dpi, 96)) : 0;
    int height = MulDiv(ui_button_size(large)+2, dpi, 96);
    if (!player) {
        if(measured_width==width)return measured_height+divider;
        arrange_overflow(width);
        const int count = static_cast<int>(SendMessageW(hwnd, TB_BUTTONCOUNT, 0, 0));
        for (int rows = 1; rows <= count; ++rows) {
            RECT rc{};
            SendMessageW(hwnd, TB_SETROWS, MAKEWPARAM(rows, TRUE), reinterpret_cast<LPARAM>(&rc));
            LONG right{},bottom{};
            for(int n=0;n<count;++n){TBBUTTON item{};RECT bounds{};SendMessageW(hwnd,TB_GETBUTTON,n,reinterpret_cast<LPARAM>(&item));
                if(!(item.fsStyle&BTNS_SEP)&&!(item.fsState&TBSTATE_HIDDEN)&&SendMessageW(hwnd,TB_GETITEMRECT,n,reinterpret_cast<LPARAM>(&bounds))){right=std::max(right,bounds.right);bottom=std::max(bottom,bounds.bottom);}}
            measured_extent=int(right);measured_height=std::max(height,int(bottom)+MulDiv(2,dpi,96));
            if (right <= width || rows == count)
                break;
        }
        measured_width=width;height=measured_height;
    }
    return height + divider;
}
int IconToolbar::layout(int width, UINT new_dpi, bool visible, int x, int y) {
    if (!hwnd)
        return 0;
    const int measured = measure_height(width, new_dpi);
    const int divider = !player && line ? std::max(2, MulDiv(2, dpi, 96)) : 0;
    const int height = measured - divider;
    show_control(bar_window(),visible);
    show_control(hwnd,visible);
    int extent = player?0:measured_extent;
    for (int i = 0; player&&i < SendMessageW(hwnd, TB_BUTTONCOUNT, 0, 0); ++i) {
        TBBUTTON item{};
        RECT bounds{};
        SendMessageW(hwnd, TB_GETBUTTON, i, reinterpret_cast<LPARAM>(&item));
        SendMessageW(hwnd, TB_GETITEMRECT, i, reinterpret_cast<LPARAM>(&bounds));
        if (!(item.fsStyle & BTNS_SEP))
            extent = std::max(extent, static_cast<int>(bounds.right));
    }
    const int used_width = !player && centered ? std::min(width, extent + MulDiv(2, dpi, 96)) : width;
    if (host) {
        place_control(host,x,y,width,height+divider);
        if(GetWindowLongPtrW(host,GWLP_USERDATA)!=LONG_PTR(line)){SetWindowLongPtrW(host,GWLP_USERDATA,line);InvalidateRect(host,nullptr,TRUE);}
    }
    place_control(hwnd,host?(width-used_width)/2:x,host?divider:y,std::max(1,used_width),height);
    bar_height = height + divider;
    return visible ? bar_height : 0;
}
void IconToolbar::arrange_overflow(int width){
    if(player||tools)return;overflow_commands.clear();
    struct Item {TBBUTTON button{};int width{};bool hidden{};};std::vector<Item> items;
    const int count=int(SendMessageW(hwnd,TB_BUTTONCOUNT,0,0)),normal=LOWORD(SendMessageW(hwnd,TB_GETBUTTONSIZE,0,0));
    for(int i=0;i<count;++i){Item item;SendMessageW(hwnd,TB_GETBUTTON,i,reinterpret_cast<LPARAM>(&item.button));TBBUTTONINFOW info{sizeof(info)};info.dwMask=TBIF_SIZE|TBIF_BYINDEX;SendMessageW(hwnd,TB_GETBUTTONINFOW,i,reinterpret_cast<LPARAM>(&info));item.width=(item.button.fsStyle&BTNS_SEP)?MulDiv(7,dpi,96):std::max(normal,int(info.cx));item.hidden=item.button.idCommand==MoreCommands;items.push_back(item);}
    const auto extent=[&]{bool previous{};int separator=-1,total{};
        for(int i=0;i<count;++i){auto &item=items[i];if(item.button.fsStyle&BTNS_SEP){item.hidden=true;if(separator<0)separator=i;continue;}if(item.hidden)continue;
            if(previous&&separator>=0){items[separator].hidden=false;total+=items[separator].width;}separator=-1;previous=true;total+=item.width;}
        return total;};
    if(overflow&&extent()>width){for(auto &item:items)if(item.button.idCommand==MoreCommands)item.hidden=false;
        for(UINT id:{Help,ChildFolder,ParentFolder,Slideshow,Info,ShowPrompt,ArmDrag,FileDelete,Save,Fullscreen,ZoomOut,ZoomIn,Undo,Actual,Preferences,FitImage,Paste,Copy,Previous,Next,ToggleEditor}){
            if(extent()<=width)break;for(auto &item:items)if(item.button.idCommand==id){item.hidden=true;overflow_commands.push_back(id);break;}}
    }
    extent();
    // A resize changes only the buttons crossing the overflow threshold. No
    // repeated native row layout is needed while choosing which commands fit.
    for(int i=0;i<count;++i){auto &item=items[i];if(bool(item.button.fsState&TBSTATE_HIDDEN)==item.hidden)continue;
        TBBUTTONINFOW info{sizeof(info)};info.dwMask=TBIF_STATE|TBIF_BYINDEX;info.fsState=BYTE(item.hidden?item.button.fsState|TBSTATE_HIDDEN:item.button.fsState&~TBSTATE_HIDDEN);SendMessageW(hwnd,TB_SETBUTTONINFOW,i,reinterpret_cast<LPARAM>(&info));}
}
void IconToolbar::show_more(HWND owner) const{
    if(overflow_commands.empty())return;auto menu=CreatePopupMenu();
    for(auto id:overflow_commands){const auto state=SendMessageW(hwnd,TB_GETSTATE,id,0);const auto p=texts.find(id);const auto text=p==texts.end()?command_name(id):p->second.c_str();AppendMenuW(menu,MF_STRING|((state&TBSTATE_ENABLED)?0:MF_GRAYED)|((state&TBSTATE_CHECKED)?MF_CHECKED:0),id,text);}
    RECT button{};const auto index=SendMessageW(hwnd,TB_COMMANDTOINDEX,MoreCommands,0);SendMessageW(hwnd,TB_GETITEMRECT,index,reinterpret_cast<LPARAM>(&button));MapWindowPoints(hwnd,nullptr,reinterpret_cast<POINT *>(&button),2);
    ui_theme_menu(menu);const auto id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,button.left,button.bottom,0,owner,nullptr);ui_destroy_menu(menu);if(id)SendMessageW(owner,WM_COMMAND,id,0);
}
void IconToolbar::enable(UINT command, bool enabled) {
    const auto state=SendMessageW(hwnd,TB_GETSTATE,command,0);if(state<0||bool(state&TBSTATE_ENABLED)==enabled)return;
    SendMessageW(hwnd, TB_ENABLEBUTTON, command, MAKELPARAM(enabled, 0));
}
void IconToolbar::check(UINT command, bool checked) {
    const auto state=SendMessageW(hwnd,TB_GETSTATE,command,0);if(state<0||bool(state&TBSTATE_CHECKED)==checked)return;
    SendMessageW(hwnd, TB_CHECKBUTTON, command, MAKELPARAM(checked, 0));
}
void IconToolbar::text(UINT command, const wchar_t *value) {
    if(const auto found=texts.find(command);found!=texts.end()&&found->second==value)return;
    texts[command]=value;measured_width=-1;
    TBBUTTONINFOW info{sizeof(info)};
    info.dwMask = TBIF_TEXT;
    info.pszText = const_cast<wchar_t *>(value);
    SendMessageW(hwnd, TB_SETBUTTONINFOW, command, reinterpret_cast<LPARAM>(&info));
}
void IconToolbar::playing(bool value) {
    if(playing_bitmap_current&&is_playing==value)return;
    is_playing = value;
    if (play_bitmap >= 0) {
        playing_bitmap_current=true;
        SendMessageW(hwnd, TB_CHANGEBITMAP, Play, value ? pause_bitmap : play_bitmap);
        text(Play, value ? L"一時停止" : L"再生");
    }
}
int IconToolbar::content_width() const {
    SIZE size{};
    SendMessageW(hwnd, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&size));
    return size.cx;
}
int IconToolbar::ideal_width() const {
    const int button = LOWORD(SendMessageW(hwnd, TB_GETBUTTONSIZE, 0, 0));
    const auto items = player ? std::span<const UINT>(player_buttons) : std::span<const UINT>(buttons);
    int width = 0;
    for (UINT item : items)
        if(item!=MoreCommands)width += item ? button : MulDiv(7, dpi, 96);
    return width + MulDiv(4, dpi, 96);
}
static LRESULT CALLBACK player_bar_proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COMMAND || message == WM_HSCROLL)
        return SendMessageW(GetParent(hwnd), message, w, l);
    if (message == WM_NOTIFY) {
        const auto header = reinterpret_cast<NMHDR *>(l);
        if (header->code == NM_CUSTOMDRAW) {
            auto draw = reinterpret_cast<NMTBCUSTOMDRAW *>(l);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
                FillRect(draw->nmcd.hdc, &draw->nmcd.rc, ui_brush(ui_palette().surface));
                return CDRF_DODEFAULT;
            }
        }
    }
    if (message == WM_CTLCOLORSTATIC) {
        SetBkColor(reinterpret_cast<HDC>(w), GetSysColor(COLOR_BTNFACE));
        return reinterpret_cast<LRESULT>(ui_brush(ui_palette().surface));
    }
    return DefWindowProcW(hwnd, message, w, l);
}
void AnimationBar::create(HWND parent) {
    WNDCLASSW cls{};
    cls.lpfnWndProc = player_bar_proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hbrBackground = ui_brush(ui_palette().surface);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.lpszClassName = L"GVN.AnimationBar";
    RegisterClassW(&cls);
    hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"アニメーションプレイヤー",
                           WS_CHILD | WS_CLIPCHILDREN, 0, 0, 1, 1, parent, nullptr, cls.hInstance, nullptr);
    toolbar.create(hwnd, true);
    slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"再生位置",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 0, 0, 1, 1, hwnd,
                             reinterpret_cast<HMENU>(500), cls.hInstance, nullptr);
    counter = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 0, 0, 1,
                              1, hwnd, nullptr, cls.hInstance, nullptr);
    SendMessageW(counter, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), FALSE);
}
int AnimationBar::measure_height(int width, UINT dpi) {
    toolbar.measure_height(width, dpi);
    const int icons = toolbar.ideal_width(), info = MulDiv(80, dpi, 96);
    const bool narrow = width < icons + info + MulDiv(120, dpi, 96);
    const bool third_row = narrow && width < icons + info + MulDiv(4, dpi, 96);
    return MulDiv(ui_button_size(toolbar.large_buttons())+(narrow?34:8),dpi,96) +
           (third_row ? MulDiv(28, dpi, 96) : 0);
}
int AnimationBar::layout(int width, int client_height, int status_height, UINT dpi, bool visible) {
    auto px = [&](int v) { return MulDiv(v, dpi, 96); };
    show_control(hwnd,visible);
    if(!visible)return 0;
    toolbar.measure_height(width,dpi);
    const int icons = toolbar.ideal_width(), info = px(80);
    const bool narrow = width < icons + info + px(120);
    const bool third_row = narrow && width < icons + info + px(4);
    const int height = measure_height(width, dpi);
    place_control(hwnd,0,std::max(0,client_height-status_height-height),width,height);
    toolbar.layout(icons, dpi, true, px(2), px(narrow ? 29 : 3));
    const int seek_x = narrow ? px(4) : icons + px(6);
    const int controls_height=toolbar.height();
    place_control(slider, seek_x, narrow?px(3):px(3)+(controls_height-px(26))/2, std::max(px(20), width - seek_x - (narrow ? px(4) : info + px(4))),
               px(26));
    place_control(counter, third_row ? std::max(0, width - info) : std::max(icons, width - info),
               third_row ? height - px(28) : px(narrow ? 29 : 3), info,
               third_row?px(26):controls_height);
    return height;
}
void AnimationBar::update(size_t frame, size_t count, bool playing, bool loop, bool honor_file_loops,
                          uint32_t file_loops) {
    const bool count_changed=displayed_count!=count,frame_changed=displayed_frame!=frame;
    if(count_changed){
        SendMessageW(slider,TBM_SETRANGEMIN,FALSE,0);
        SendMessageW(slider,TBM_SETRANGEMAX,FALSE,count?count-1:0);
    }
    if(count_changed||frame_changed)SendMessageW(slider,TBM_SETPOS,TRUE,frame);
    toolbar.playing(playing);
    toolbar.check(Loop, loop);
    const auto mode = !loop ? std::wstring(L"ループ: オフ")
                      : honor_file_loops && file_loops
                          ? L"ループ: オン（ファイル指定: " + std::to_wstring(file_loops) + L"周）"
                          : std::wstring(L"ループ: オン（無限）");
    toolbar.text(Loop, mode.c_str());
    if(count_changed||frame_changed){
        const auto text=std::to_wstring(count?frame+1:0)+L" / "+std::to_wstring(count);
        SetWindowTextW(counter,text.c_str());
        displayed_frame=frame;displayed_count=count;
    }
}
BoundaryChoice choose_boundary(HWND owner, bool forward, const std::filesystem::path &folder) {
    const TASKDIALOG_BUTTON choices[]{{101, forward ? L"フォルダの先頭へ(&F)" : L"フォルダの末尾へ(&F)"},
                                      {102, forward ? L"次のフォルダへ(&D)" : L"前のフォルダへ(&D)"},
                                      {IDCANCEL, L"キャンセル(&C)"}};
    const auto path = folder.wstring();
    TASKDIALOGCONFIG config{sizeof(config)};
    config.hwndParent = owner;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT;
    config.pszWindowTitle = forward ? L"GVN - 次へ" : L"GVN - 前へ";
    config.pszMainInstruction = L"これ以上表示する画像がありません。";
    config.pszContent = path.c_str();
    config.cButtons = static_cast<UINT>(std::size(choices));
    config.pButtons = choices;
    config.nDefaultButton = IDCANCEL;
    int selected = IDCANCEL;
    if (FAILED(TaskDialogIndirect(&config, &selected, nullptr, nullptr)))
        throw std::runtime_error("Cannot show end-of-folder dialog");
    return selected == 101   ? BoundaryChoice::FolderEnd
           : selected == 102 ? BoundaryChoice::Folder
                             : BoundaryChoice::Cancel;
}
struct PromptState {
    const wchar_t *title;
    const wchar_t *description;
    std::wstring value;
    bool accepted{};
};
static INT_PTR CALLBACK prompt_proc(HWND dialog, UINT msg, WPARAM w, LPARAM l) {
    auto state = reinterpret_cast<PromptState *>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        state = reinterpret_cast<PromptState *>(l);
        SetWindowLongPtrW(dialog, DWLP_USER, l);
        SetWindowTextW(dialog, state->title);
        auto control = [&](const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int width,
                           int height, int id) {
            const auto c =
                CreateWindowExW(wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0, cls, text,
                                WS_CHILD | WS_VISIBLE | style, x, y, width, height, dialog,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            return c;
        };
        control(L"STATIC", state->description, 0, 14, 12, 420, 48, 0);
        const auto edit =
            control(L"EDIT", state->value.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 14, 66, 420, 26, 101);
        SendMessageW(edit, EM_SETLIMITTEXT, 32760, 0);
        control(L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 264, 108, 80, 28, IDOK);
        control(L"BUTTON", L"キャンセル", WS_TABSTOP, 354, 108, 80, 28, IDCANCEL);
        apply_ui_theme(dialog);SetFocus(edit);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        return FALSE;
    }
    if (msg == WM_COMMAND) {
        if (LOWORD(w) == IDOK) {
            const auto edit = GetDlgItem(dialog, 101);
            std::wstring text(static_cast<size_t>(GetWindowTextLengthW(edit)) + 1, L'\0');
            GetWindowTextW(edit, text.data(), static_cast<int>(text.size()));
            text.pop_back();
            state->value = std::move(text);
            state->accepted = true;
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}
std::optional<std::wstring> prompt(HWND owner, const wchar_t *title, const wchar_t *description,
                                   const std::wstring &initial) {
    struct {
        DLGTEMPLATE dialog;
        WORD menu, cls, title;
    } data{};
    data.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    data.dialog.cx = 300;
    data.dialog.cy = 100;
    PromptState state{title, description, initial};
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &data.dialog, owner, prompt_proc,
                            reinterpret_cast<LPARAM>(&state));
    if (state.accepted)
        return state.value;
    return {};
}
std::filesystem::path choose_folder(HWND owner, const wchar_t *title) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        throw std::runtime_error("Cannot open folder picker");
    DWORD options{};
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(title);
    if (dialog->Show(owner) == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    Microsoft::WRL::ComPtr<IShellItem> result;
    if (FAILED(dialog->GetResult(&result)))
        return {};
    PWSTR path{};
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)))
        return {};
    std::filesystem::path value(path);
    CoTaskMemFree(path);
    return value;
}
std::filesystem::path choose_executable(HWND owner) {
    wchar_t file[32768]{};
    OPENFILENAMEW d{sizeof(d)};
    d.hwndOwner = owner;
    d.lpstrFile = file;
    d.nMaxFile = 32768;
    d.lpstrFilter = L"アプリケーション\0*.exe\0\0";
    d.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&d))
        return file;
    return {};
}
std::filesystem::path choose_image_file(HWND owner, bool save, const std::filesystem::path &folder,
                                        const std::wstring &suggested) {
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    HRESULT result = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                                      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result))
        throw std::runtime_error("Cannot create file dialog");
    DWORD options{};
    dialog->GetOptions(&options);
    // The application confirms the final target once, including Save As and supplied dialog paths.
    options &= ~FOS_OVERWRITEPROMPT;
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR |
                       (save ? 0 : FOS_FILEMUSTEXIST));
    const COMDLG_FILTERSPEC save_types[]{{L"PNG（透明度維持）", L"*.png"},
                                         {L"JPEG", L"*.jpg;*.jpeg"},
                                         {L"BMP", L"*.bmp"},
                                         {L"GIF（静止画）", L"*.gif"}};
    const COMDLG_FILTERSPEC open_types[]{
        {L"画像・ZIP", L"*.png;*.apng;*.gif;*.jpg;*.jpeg;*.jpe;*.bmp;*.psd;*.tif;*.tiff;*.ico;*.zip"},
        {L"すべて", L"*.*"}};
    dialog->SetFileTypes(save ? 4 : 2, save ? save_types : open_types);
    dialog->SetTitle(save ? L"画像を保存" : L"画像・ZIPを開く");
    if (save) {
        dialog->SetDefaultExtension(L"png");
        dialog->SetFileName(suggested.c_str());
    }
    if (!folder.empty() && std::filesystem::is_directory(folder)) {
        Microsoft::WRL::ComPtr<IShellItem> item;
        if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item))))
            dialog->SetFolder(item.Get());
    }
    result = dialog->Show(owner);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    if (FAILED(result))
        throw std::runtime_error("File dialog failed");
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item)))
        throw std::runtime_error("Cannot read selected file");
    PWSTR file{};
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &file)))
        throw std::runtime_error("Selected file has no filesystem path");
    std::filesystem::path path(file);
    CoTaskMemFree(file);
    return path;
}
void recycle_file(HWND owner, const std::filesystem::path &path) {
    delete_file_checked(owner, path, true);
}

} // namespace gvn
