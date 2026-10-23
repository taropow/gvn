#include "gvn/color_dialog.h"
#include "gvn/preferences.h"
#include "gvn/ui_theme.h"
#include "gvn/ui.h"
#include "gvn/display_quality.h"
#include <commctrl.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <cmath>
#include <fstream>
#include <chrono>
#include <climits>
#include <iomanip>
#include <map>
#include <sstream>
#include <type_traits>

namespace gvn {
namespace {
// Optional developer trace; no clock/file work is done unless explicitly enabled.
struct PreferencesTrace {
    std::ofstream stream;
    PreferencesTrace(){wchar_t path[32768]{};if(GetEnvironmentVariableW(L"GVN_PREFS_TIMING",path,32768))stream.open(std::filesystem::path(path));}
};
PreferencesTrace &preferences_trace(){static PreferencesTrace value;return value;}
using PrefTime=std::chrono::steady_clock::time_point;
PrefTime pref_now(){return preferences_trace().stream.is_open()?std::chrono::steady_clock::now():PrefTime{};}
void pref_elapsed(const char *phase,int id,PrefTime since){if(since!=PrefTime{})preferences_trace().stream<<phase<<','<<id<<','<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-since).count()<<'\n';}
using Page = PreferencePage;
namespace cid = preference_id;
enum class Kind { Check, Number, Combo, Color, Seconds, Font };
struct Field {
    Page page;
    int id;
    const wchar_t *label;
    Kind kind;
    bool Settings::*flag{};
    int Settings::*number{};
    uint32_t Settings::*color{};
    int low{}, high{};
    std::vector<const wchar_t *> options;
    bool quality{};
    bool MouseSettings::*mouse_flag{};
    int MouseSettings::*mouse_number{};
    bool &flag_value(Settings &s) const {
        return flag ? s.*flag : s.mouse.*mouse_flag;
    }
    bool flag_value(const Settings &s) const {
        return flag ? s.*flag : s.mouse.*mouse_flag;
    }
    int &number_value(Settings &s) const {
        return number ? s.*number : s.mouse.*mouse_number;
    }
    int number_value(const Settings &s) const {
        return number ? s.*number : s.mouse.*mouse_number;
    }
};
std::vector<Field> fields() {
    std::vector<Field> out;
    auto check = [&](Page p, int id, const wchar_t *label, auto member) {
        Field f{p, id, label, Kind::Check};
        if constexpr (std::is_same_v<decltype(member), bool Settings::*>)
            f.flag = member;
        else
            f.mouse_flag = member;
        out.push_back(f);
    };
    auto number = [&](Page p, int id, const wchar_t *label, auto member, int low, int high) {
        Field f{p, id, label, Kind::Number};
        if constexpr (std::is_same_v<decltype(member), int Settings::*>)
            f.number = member;
        else
            f.mouse_number = member;
        f.low = low;
        f.high = high;
        out.push_back(f);
    };
    auto combo = [&](Page p, int id, const wchar_t *label, auto member,
                     std::initializer_list<const wchar_t *> options) {
        Field f{p, id, label, Kind::Combo};
        if constexpr (std::is_same_v<decltype(member), int Settings::*>)
            f.number = member;
        else
            f.mouse_number = member;
        f.options = options;
        out.push_back(f);
    };
    auto color = [&](Page p, int id, const wchar_t *label, uint32_t Settings::*member) {
        Field f{p, id, label, Kind::Color};
        f.color = member;
        out.push_back(f);
    };
    check(Page::General, cid::AllowMultiple, L"多重起動を許可する", &Settings::allow_multiple);
    check(Page::General, cid::ActivateReused, L"多重起動しない場合にウィンドウをアクティブにする",
          &Settings::activate_reused);
    check(Page::General, cid::ActivateDropped, L"ファイルドロップされたらアクティブにする",
          &Settings::activate_dropped);
    check(Page::General, cid::AllowPhysicalOverflow, L"物理メモリを超えるサイズの画像を作成する",
          &Settings::allow_physical_overflow);
    check(Page::General, cid::Undo, L"元に戻すを利用する", &Settings::undo_enabled);
    check(Page::General, cid::CutTransparent, L"切り取り領域を透明にする", &Settings::cut_transparent);
    color(Page::General, cid::CutColor, L"切り取りの背景色", &Settings::cut_color);
    check(Page::General, cid::History, L"最近開いたフォルダを保存する", &Settings::remember_history);
    check(Page::General, cid::PasteNew, L"選択範囲がないときは新規画像に貼り付ける", &Settings::paste_new);
    check(Page::General, cid::ReopenLast, L"起動時に前回終了時のファイルを開く", &Settings::reopen_last);
    check(Page::General, cid::DisableIme, L"画像表示中はIMEを無効にする（文字入力画面を除く）",
          &Settings::disable_ime);
    number(Page::General, cid::Grid, L"矩形選択の共通単位（px）", &Settings::grid, 1, 256);
    check(Page::Background, cid::BackgroundTiled, L"背景をタイル状にする", &Settings::background_tiled);
    combo(Page::Background, cid::BackgroundPattern, L"背景タイルのサイズ", &Settings::background_pattern,
          {L"最小（8 px）", L"小（16 px）", L"中（32 px）", L"大（64 px）", L"最大（128 px）"});
    color(Page::Background, cid::BackgroundFirst, L"背景色 1", &Settings::background_first);
    color(Page::Background, cid::BackgroundSecond, L"背景色 2", &Settings::background_second);
    check(Page::Background, cid::Checker, L"透明部分を市松模様で表示する", &Settings::checker);
    color(Page::Background, cid::Background, L"市松を使わない透明部分の色", &Settings::background);
    color(Page::Background, cid::Light, L"透明部分の市松色 1", &Settings::checker_light);
    color(Page::Background, cid::Dark, L"透明部分の市松色 2", &Settings::checker_dark);
    number(Page::Background, cid::Cell, L"透明部分の市松サイズ（px）", &Settings::checker_size, 2, 64);
    check(Page::Window, cid::Topmost, L"常に手前に表示する", &Settings::topmost);
    check(Page::Window, cid::WindowCenter, L"ウィンドウを画面の中央に置く", &Settings::window_center);
    check(Page::Window, cid::WindowToolbarMin, L"自動サイズ調整でツールバーより小さくしない",
          &Settings::window_toolbar_min);
    combo(Page::Window, cid::StartupPosition, L"起動時の位置", &Settings::startup_position,
          {L"Windowsに任せる", L"前回終了時の位置とサイズ", L"ユーザー指定"});
    number(Page::Window, cid::WindowX, L"ユーザー指定: X位置（画面座標 / px）", &Settings::window_x, -INT_MAX, INT_MAX);
    number(Page::Window, cid::WindowY, L"ユーザー指定: Y位置（画面座標 / px）", &Settings::window_y, -INT_MAX, INT_MAX);
    number(Page::Window, cid::WindowWidth, L"ユーザー指定: 幅（px）", &Settings::window_width, 1, INT_MAX);
    number(Page::Window, cid::WindowHeight, L"ユーザー指定: 高さ（px）", &Settings::window_height, 1, INT_MAX);
    combo(Page::Window, cid::StartupState, L"起動時の表示状態", &Settings::startup_state,
          {L"通常", L"最小化", L"最大化", L"全画面"});
    check(Page::Window, cid::RestoreMaximized, L"通常起動時、前回が最大化なら最大化する",
          &Settings::restore_maximized);
    check(Page::Window, cid::RestoreFullscreen, L"通常起動時、前回が全画面なら全画面にする",
          &Settings::restore_fullscreen);
    check(Page::Window, cid::Caption, L"キャプション（タイトルバー）を表示する", &Settings::caption);
    check(Page::Window, cid::OuterFrame, L"キャプションがない時も外側の枠を表示する", &Settings::outer_frame);
    check(Page::Window, cid::InnerFrame, L"画像領域の内側の枠を表示する", &Settings::inner_frame);
    combo(Page::Display, cid::InitialFit, L"画像を開いたときの表示", &Settings::initial_fit,
          {L"ウィンドウ内に収める", L"幅に合わせる", L"高さに合わせる", L"画面を埋める", L"原寸"});
    check(Page::Display, cid::CenterImage, L"画像を中央に表示する（解除すると左上）",
          &Settings::center_image);
    combo(Page::Display, cid::GifSpeed, L"アニメGIFの速度制限", &Settings::gif_speed,
          {L"制限なし", L"IE風", L"Firefox風", L"Opera風", L"Chrome風"});
    number(Page::Display, cid::FitMax, L"フィット表示の最大倍率（%）", &Settings::fit_max, 1, 5000);
    const auto quality = [&](Page page, int id, const wchar_t *label, int Settings::*member) {
        combo(page,id,label,member,{display_quality_labels[0],display_quality_labels[1],display_quality_labels[2]});
        out.back().quality=true;
    };
    quality(Page::Display, cid::DisplayDown, L"縮小の画質（通常表示・手動ズーム）", &Settings::display_down);
    quality(Page::Display, cid::DisplayUp, L"拡大の画質（通常表示・手動ズーム）", &Settings::display_up);
    check(Page::Display, cid::ComicRTL, L"漫画モードは右から左に読む", &Settings::comic_rtl);
    check(Page::Display, cid::ComicRotateBefore, L"漫画: 表示用回転を適用してから見開きに合成する",
          &Settings::comic_rotate_before);
    combo(Page::Display, cid::ComicHeightMode, L"漫画: 左右の縦サイズが違う場合", &Settings::comic_mode,
          {L"指定色で塗り潰す", L"小さい方に合わせる", L"大きい方に合わせる",
           L"最初のページに合わせる（旧GVN）", L"高さを変えず透明余白（旧GVN）"});
    color(Page::Display, cid::ComicFill, L"漫画: 塗り潰しの色", &Settings::comic_fill);
    quality(Page::Display,cid::ComicDown,L"漫画: 縮小方法",&Settings::comic_down);
    quality(Page::Display,cid::ComicUp,L"漫画: 拡大方法",&Settings::comic_up);
    combo(Page::Display, cid::DisplayMode, L"読み込み時の表示オプション（通常）", &Settings::display_mode,
          {L"ウィンドウを画像に合わせる", L"画像をウィンドウに合わせる",
           L"大きい画像だけウィンドウに合わせる", L"画像をデスクトップに合わせる",
           L"大きい画像だけデスクトップに合わせる", L"そのままのサイズ"});
    check(Page::Zoom, cid::KeepZoom, L"画像切り替え時に倍率を維持する", &Settings::keep_zoom);
    number(Page::Zoom, cid::ZoomStep, L"ズームインの増加率（%）", &Settings::zoom_step, 1, 400);
    combo(Page::Zoom, cid::ZoomMode, L"倍率の段階", &Settings::zoom_mode,
          {L"現在の倍率に対する割合（旧GVN）", L"指定%ずつ（100%を基準にした目盛り）", L"段階的に（標準倍率）"});
    number(Page::Zoom, cid::ZoomBelow, L"100%未満の倍率差（ポイント）", &Settings::zoom_below, 1, 400);
    number(Page::Zoom, cid::ZoomAbove, L"100%以上の倍率差（ポイント）", &Settings::zoom_above, 1, 5000);
    check(Page::Zoom, cid::DragSimple, L"ドラッグズーム中は一時的に高速表示する", &Settings::drag_simple);
    check(Page::Fullscreen, cid::HideCursor, L"画像上のカーソルを隠す", &Settings::full_hide_cursor);
    check(Page::Fullscreen, cid::HideTaskbar, L"全画面表示でタスクバーを隠す", &Settings::full_hide_taskbar);
    check(Page::Fullscreen, cid::HideWait, L"待機中のカーソルも隠す", &Settings::full_hide_wait);
    quality(Page::Fullscreen, cid::FullDown, L"縮小の画質（全画面・手動ズーム）", &Settings::full_down);
    quality(Page::Fullscreen, cid::FullUp, L"拡大の画質（全画面・手動ズーム）", &Settings::full_up);
    check(Page::Fullscreen, cid::FullTopmost, L"全画面では常に手前に表示する", &Settings::full_topmost);
    combo(Page::Fullscreen, cid::FullDisplayMode, L"読み込み時の表示オプション（全画面）",
          &Settings::full_display_mode,
          {L"画像の原寸", L"画像を全画面に合わせる", L"大きい画像だけ全画面に合わせる",
           L"画像を全画面に合わせる", L"大きい画像だけ全画面に合わせる", L"そのままのサイズ"});
    combo(Page::Fullscreen, cid::FullFit, L"全画面のフィット方法", &Settings::full_initial_fit,
          {L"画面内に収める", L"幅に合わせる", L"高さに合わせる", L"画面を埋める", L"原寸"});
    check(Page::Fullscreen, cid::OverlayFull, L"全画面時にファイル名を表示する", &Settings::overlay_full);
    check(Page::Fullscreen, cid::OverlayWindow, L"通常表示でキャプション非表示ならファイル名を表示する",
          &Settings::overlay_window);
    check(Page::Fullscreen, cid::OverlayAnimation, L"アニメーション表示中もファイル名を表示する",
          &Settings::overlay_animation);
    combo(Page::Fullscreen, cid::OverlayPosition, L"ファイル名の表示位置", &Settings::overlay_position,
          {L"上 - 左", L"上 - 中", L"上 - 右", L"中 - 左", L"中 - 中", L"中 - 右", L"下 - 左", L"下 - 中",
           L"下 - 右"});
    combo(Page::Fullscreen, cid::OverlayBackground, L"ファイル名の背景", &Settings::overlay_background,
          {L"透明", L"塗り潰す", L"影を表示"});
    out.push_back({Page::Fullscreen, cid::OverlayFont, L"ファイル名のフォント", Kind::Font});
    color(Page::Fullscreen, cid::OverlayForeground, L"ファイル名の文字色", &Settings::overlay_foreground);
    color(Page::Fullscreen, cid::OverlayBackcolor, L"ファイル名の背景色", &Settings::overlay_backcolor);
    check(Page::Bars, cid::Toolbar, L"ツールバーを表示する", &Settings::toolbar);
    check(Page::Bars, cid::Status, L"ステータスバーを表示する", &Settings::status);
    check(Page::Bars, cid::Menu, L"メニューバーを表示する", &Settings::menu);
    combo(Page::Bars, cid::TitleMode, L"キャプションのファイル名表示", &Settings::title_mode,
          {L"ファイル名", L"フルパス", L"親フォルダ名とファイル名", L"拡張子を除いたファイル名",
           L"ファイル名を表示しない"});
    check(Page::Bars, cid::ToolbarCenter, L"ツールバーのボタンを中央に揃える", &Settings::toolbar_center);
    check(Page::Bars, cid::ToolbarLine, L"ツールバー上部に横線を表示する", &Settings::toolbar_line);
    check(Page::Bars, cid::ToolbarLarge, L"ツールバーのボタンを大きくする", &Settings::toolbar_large);
    check(Page::Bars, cid::ToolbarAuto, L"カーソル位置でツールバーを自動表示する", &Settings::toolbar_auto);
    check(Page::Bars, cid::StatusAuto, L"カーソル位置でステータスバーを自動表示する", &Settings::status_auto);
    check(Page::Bars, cid::FullToolbar, L"全画面のツールバーを有効にする（端で自動表示）",
          &Settings::full_toolbar);
    check(Page::Bars, cid::FullStatus, L"全画面のステータスバーを有効にする（端で自動表示）",
          &Settings::full_status);
    check(Page::Bars, cid::FullMenu, L"全画面のメニューバーを表示する", &Settings::full_menu);
    check(Page::Bars, cid::Scrollbars, L"通常表示の画像スクロールバーを有効にする", &Settings::scrollbars);
    check(Page::Bars, cid::FullScrollbars, L"全画面の画像スクロールバーを有効にする",
          &Settings::full_scrollbars);
    combo(Page::Move, cid::Sort, L"ファイルの並び順", &Settings::sort,
          {L"ファイル名", L"更新日時", L"サイズ", L"拡張子", L"ランダム"});
    check(Page::Move, cid::Natural, L"ファイル名の数字を自然順に並べる", &Settings::natural);
    check(Page::Move, cid::Reverse, L"逆順に並べる", &Settings::reverse);
    number(Page::Move, cid::SkipCount, L"スキップする画像の枚数", &Settings::skip_count, 1, 100000);
    check(Page::Move, cid::FolderHistory, L"フォルダ移動で開いたフォルダも履歴へ追加する",
          &Settings::folder_history);
    check(Page::Move, cid::PreviousFolderFirst, L"前のフォルダでは先頭の画像を開く",
          &Settings::previous_folder_first);
    check(Page::Move, cid::AdjacentPreviousFirst, L"前隣のフォルダでは先頭の画像を開く",
          &Settings::adjacent_previous_first);
    check(Page::Move, cid::ParentFirst, L"親フォルダでは先頭の画像を開く", &Settings::parent_first);
    combo(Page::Move, cid::ListLoop, L"最後・最初に到達した時", &Settings::end_action,
          {L"何もしない", L"ダイアログで確認", L"フォルダ内をループ", L"次・前のフォルダへ移動"});
    check(Page::Move, cid::BoundarySound, L"最後・最初に到達した時に音を鳴らす", &Settings::boundary_sound);
    check(Page::Move, cid::ComicToolbarReverse, L"右から左の漫画モードでツールバーの前・次を逆にする",
          &Settings::comic_toolbar_reverse);
    check(Page::Move, cid::ComicJumpReverse, L"右から左の漫画モードでジャンプのスクロールバーを逆にする",
          &Settings::comic_jump_reverse);
    check(Page::FileOperations, cid::SaveDialog, L"上書き保存でも保存ダイアログを開く",
          &Settings::save_dialog);
    check(Page::FileOperations, cid::ConfirmOverwrite, L"上書き保存する前に確認する",
          &Settings::confirm_overwrite);
    check(Page::FileOperations, cid::RememberSaveFolder, L"名前を付けて保存のパスを保持する",
          &Settings::remember_save_folder);
    check(Page::FileOperations, cid::SaveAsRetarget, L"静止画の別名保存後、保存先のファイルへ切り替える",
          &Settings::save_as_retarget);
    check(Page::FileOperations, cid::RenameResort, L"ファイル名変更後に表示順を整理する",
          &Settings::rename_resort);
    check(Page::FileOperations, cid::ConfirmDelete, L"ファイルを削除する前に確認する",
          &Settings::confirm_delete);
    check(Page::FileOperations, cid::RecycleDeleted, L"削除したファイルはごみ箱へ送る",
          &Settings::recycle_deleted);
    check(Page::FileOperations, cid::NextAfterFileop, L"削除や移動したら次のファイルを表示する",
          &Settings::next_after_fileop);
    check(Page::FileOperations, cid::ConfirmMove, L"移動先の同名ファイルを上書きする前に確認する",
          &Settings::confirm_move);
    check(Page::FileOperations, cid::ConfirmCopy, L"コピー先の同名ファイルを上書きする前に確認する",
          &Settings::confirm_copy);
    check(Page::FileOperations, cid::ConfirmUnsaved, L"保存していない画像の変更を確認する（既定OFF）",
          &Settings::confirm_unsaved);
    check(Page::Loading, cid::PreserveAlpha, L"アルファを保持する（OFFなら透過画像を背景色と合成）",
          &Settings::preserve_alpha);
    check(Page::Loading, cid::BmpTransparency, L"透過BMPを透過表示する", &Settings::bmp_transparency);
    check(Page::Loading, cid::GifTransparency, L"透過GIFを透過表示する（アニメ中は常に有効）",
          &Settings::gif_transparency);
    check(Page::Loading, cid::GifBackground, L"背景合成時にGIFの背景色を利用する", &Settings::gif_background);
    check(Page::Loading, cid::PngTransparency, L"透過PNG / APNGを透過表示する", &Settings::png_transparency);
    check(Page::Loading, cid::PngBackground, L"背景合成時にPNG / APNGの背景色を利用する",
          &Settings::png_background);
    color(Page::Loading, cid::TransparencyBackground, L"透過画像を合成する背景色",
          &Settings::transparency_background);
    check(Page::Loading, cid::GifAnimation, L"アニメGIFをアニメーション表示する（OFFなら先頭コマのみ）",
          &Settings::gif_animation);
    check(Page::Loading, cid::ExifOrientation, L"JPEGのExif情報に従い回転して表示する",
          &Settings::exif_orientation);
    check(Page::Loading, cid::BitfieldTo24, L"ビットフィールド画像を読み込み時に24 BPP化（Alphaは保持）",
          &Settings::bitfield_to24);
    check(Page::Loading, cid::Hidden, L"隠し属性の画像もリストに含める", &Settings::hidden);
    check(Page::Loading, cid::SystemFiles, L"システム属性の画像もリストに含める", &Settings::system_files);
    check(Page::Loading, cid::ProtectedFiles, L"隠し＋システム属性の画像もリストに含める",
          &Settings::protected_files);
    check(Page::Loading, cid::Autoplay, L"GIF / APNGを開いたら自動再生する", &Settings::animation_autoplay);
    check(Page::Loading, cid::AnimationLoop, L"アニメーションを繰り返し再生する", &Settings::animation_loop);
    check(Page::Loading, cid::AnimationFileLoops, L"GIF / APNGの指定回数に従う（解除すると無限）",
          &Settings::animation_file_loops);
    check(Page::Loading, cid::PrefetchEnabled, L"隣の画像を先読みする", &Settings::prefetch_enabled);
    number(Page::Loading, cid::CacheMiB, L"画像キャッシュ容量（MiB、0で無効）", &Settings::cache_mib, 0,
           4096);
    check(Page::Mask, cid::FLNegative, L"FL: 色を反転する", &Settings::fl_negative);
    number(Page::Mask, cid::GlassSize, L"ガラスのサイズ（px）", &Settings::glass_size, 1, 999);
    check(Page::Mask, cid::GlassRemainder, L"ガラスの余りも反転する", &Settings::glass_remainder);
    combo(Page::Mask, cid::MaskSwapChoice, L"RGB入れ替え（マスク）", &Settings::mask_swap,
          {L"R ↔ G", L"R ↔ B", L"G ↔ B", L"R → G → B → R", L"R ← G ← B ← R"});
    check(Page::Mask, cid::MaskNegativeRed, L"ネガポジ反転: Red", &Settings::mask_negative_red);
    check(Page::Mask, cid::MaskNegativeGreen, L"ネガポジ反転: Green", &Settings::mask_negative_green);
    check(Page::Mask, cid::MaskNegativeBlue, L"ネガポジ反転: Blue", &Settings::mask_negative_blue);
    check(Page::Mask, cid::MaskXorRed, L"XOR 0x80: Red", &Settings::mask_xor_red);
    check(Page::Mask, cid::MaskXorGreen, L"XOR 0x80: Green", &Settings::mask_xor_green);
    check(Page::Mask, cid::MaskXorBlue, L"XOR 0x80: Blue", &Settings::mask_xor_blue);
    check(Page::Mask, cid::Q0Vertical, L"Q0: 縦ガラス", &Settings::mask_q0_vertical);
    check(Page::Mask, cid::Q0Horizontal, L"Q0: 横ガラス", &Settings::mask_q0_horizontal);
    check(Page::Mask, cid::Q0Negative, L"Q0: ネガポジ反転", &Settings::mask_q0_negative);
    check(Page::Mask, cid::Q0Xor, L"Q0: XOR 0x80", &Settings::mask_q0_xor);
    check(Page::Mask, cid::Q0FlipVertical, L"Q0: 上下反転", &Settings::mask_q0_flip_vertical);
    check(Page::Mask, cid::Q0FlipHorizontal, L"Q0: 左右反転", &Settings::mask_q0_flip_horizontal);
    combo(Page::Mouse, cid::Wheel, L"ホイール", &Settings::wheel,
          {L"前後の画像に移動", L"マウス位置を中心にズーム", L"カスタム（マウス割当で編集）"});
    combo(Page::Mouse, cid::Middle, L"中ボタン", &Settings::middle,
          {L"何もしない", L"終了", L"全画面の切り替え", L"カスタム（マウス割当で編集）"});
    number(Page::Mouse, cid::WheelScroll, L"ホイールのスクロール量（px）", &MouseSettings::wheel_scroll, 1,
           2000);
    number(Page::Mouse, cid::DragScroll, L"ドラッグのスクロール量（%）", &MouseSettings::drag_scroll, 1,
           1000);
    number(Page::Mouse, cid::DragZoom, L"ドラッグのズーム速度（%）", &MouseSettings::drag_zoom, 1, 1000);
    combo(Page::Mouse, cid::DragAxis, L"ドラッグのズーム方向", &MouseSettings::drag_axis,
          {L"上へ動かすと拡大", L"下へ動かすと拡大", L"右へ動かすと拡大", L"左へ動かすと拡大"});
    number(Page::Mouse, cid::DragThreshold, L"ドラッグ開始までの余裕（px）", &MouseSettings::drag_threshold,
           0, 100);
    number(Page::Mouse, cid::HoverMargin, L"ホバースクロールの余白（%）", &MouseSettings::hover_margin, 0,
           50);
    number(Page::Mouse, cid::HoverMin, L"ホバーの最小移動量（px）", &MouseSettings::hover_min, 0, 100);
    check(Page::Mouse, cid::IgnoreActivation, L"アクティブ化時のクリック機能を無効にする",
          &MouseSettings::ignore_activation);
    check(Page::Mouse, cid::SlowWheel, L"ホイールの反応を鈍くする（2刻みで1操作）",
          &MouseSettings::slow_wheel);
    check(Page::Slideshow, cid::SlideFullscreen, L"スライド中は全画面表示にする", &Settings::slide_fullscreen);
    check(Page::Slideshow, cid::SlideLoop, L"ループする", &Settings::slide_loop);
    check(Page::Slideshow, cid::SlideNextFolder, L"次のフォルダも表示する", &Settings::slide_next_folder);
    check(Page::Slideshow, cid::SlideSubtree, L"開始フォルダと下位フォルダだけ表示する", &Settings::slide_subtree);
    Field interval{Page::Slideshow, cid::Interval, L"スライド間隔（秒）", Kind::Seconds};
    interval.number = &Settings::interval;
    interval.low = 250;
    interval.high = 3600000;
    out.push_back(interval);
    check(Page::Slideshow, cid::SlideRandom, L"スライド中はランダムな並び順にする", &Settings::slide_random);
    combo(Page::General,cid::UiTheme,L"外観のテーマ",&Settings::ui_theme,{L"OSに合わせる",L"ライト",L"ダーク"});
    combo(Page::General,cid::UiDensity,L"操作部の密度",&Settings::ui_density,{L"コンパクト",L"ゆったり"});
    check(Page::Bars,cid::ToolbarOverflow,L"狭いウィンドウでは操作を「…」にまとめる",&Settings::toolbar_overflow);
    check(Page::General,cid::ToastEnabled,L"操作のトースト通知を表示する",&Settings::toast_enabled);
    check(Page::General,cid::CommandSound,L"操作完了時に短い確認音を鳴らす",&Settings::command_sound);
    check(Page::General,cid::ToastViewCommands,L"最前面・見開き・ループなどの表示切り替えも通知する",&Settings::toast_view_commands);
    number(Page::General,cid::ToastDuration,L"トーストの表示時間（ms）",&Settings::toast_duration,500,10000);
    return out;
}
constexpr const wchar_t *titles[]{L"全般",       L"背景", L"ウィンドウ", L"表示",         L"ズーム",
                                  L"全画面表示", L"バー", L"移動",       L"ファイル操作", L"ファイル読み込み",
                                  L"マスク",     L"キー", L"マウス",     L"マウス割当",   L"スライド",
                                  L"外部アプリ"};
std::wstring get_text(HWND control) {
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    GetWindowTextW(control, text.data(), static_cast<int>(text.size()));
    text.pop_back();
    return text;
}
std::wstring hex(uint32_t value) {
    std::wostringstream s;
    s << std::uppercase << std::hex << std::setw(6) << std::setfill(L'0') << value;
    return s.str();
}
struct DialogTemplate {
    DLGTEMPLATE dialog{};
    WORD menu{}, cls{}, title{}, points{9};
    wchar_t face[13]{L"Yu Gothic UI"};
    DialogTemplate() {
        dialog.style =
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | DS_MODALFRAME | DS_CENTER | DS_SETFONT;
        dialog.cx = 450;
        dialog.cy = 325;
    }
};
} // namespace

struct PreferencesDialog::Impl {
    Settings draft;
#include "preferences_navigation.inc"
    Apply apply;
    Error error;
    HWND hwnd{}, tab{};
    std::array<HWND, static_cast<size_t>(Page::Count)> pages{};
    std::array<HWND, static_cast<size_t>(Page::Count)> page_scrollbars{};
    std::array<int, static_cast<size_t>(Page::Count)> scroll_y{};
    std::array<bool, static_cast<size_t>(Page::Count)> page_ready{};
    std::map<int, HWND> controls;
    std::vector<Field> schema = fields();
    HFONT font{};
    UINT dpi{96};
    bool modal{}, accepted{}, building{}, dirty{};
    int selected{}, key_index{}, destination_index{}, external_index{};
    int key_slot{};
    std::vector<UINT> key_commands;
    bool capturing_key{};
    UINT captured_key{};
    MouseGesture mouse_gesture{MouseGesture::LeftClick};
    unsigned mouse_held{}, mouse_modifiers{};
    std::wstring failure;
    std::function<std::filesystem::path(HWND)> executable_picker = choose_executable;
    std::vector<std::pair<const char *, ULONGLONG>> initialization_ms;
    struct LayoutStamp {int width{},height{},dpi{},scroll{},comic_mode{};bool valid{};};
    std::array<LayoutStamp,static_cast<size_t>(Page::Count)> page_layouts;
    int reset_label_external{-1};
    Impl(const Settings &settings, Apply commit, Error alert)
        : draft(settings), saved_mouse(settings.mouse), apply(std::move(commit)), error(std::move(alert)) {
        draft.set_external_applications(settings.external_applications());
    }
    int px(int v) const {
        return MulDiv(v, dpi, 96);
    }
    std::wstring font_label() const {
        std::wostringstream label;
        label << draft.overlay_family << L"  " << std::fixed << std::setprecision(1)
              << draft.overlay_points / 10.0 << L" pt";
        if (draft.overlay_weight >= 600)
            label << L" 太字";
        if (draft.overlay_italic)
            label << L" 斜体";
        if (draft.overlay_underline)
            label << L" 下線";
        if (draft.overlay_strikeout)
            label << L" 取消線";
        return label.str();
    }
    void choose_overlay_font() {
        LOGFONTW selected_font{};
        wcsncpy_s(selected_font.lfFaceName, draft.overlay_family.c_str(), _TRUNCATE);
        selected_font.lfHeight = -MulDiv(draft.overlay_points, dpi, 720);
        selected_font.lfWeight = draft.overlay_weight;
        selected_font.lfItalic = draft.overlay_italic;
        selected_font.lfUnderline = draft.overlay_underline;
        selected_font.lfStrikeOut = draft.overlay_strikeout;
        selected_font.lfCharSet = static_cast<BYTE>(draft.overlay_charset);
        auto color = draft.overlay_foreground;
        try {
            const auto value = get_text(find(cid::OverlayForeground));
            if (value.size() == 6)
                color = std::stoul(value, nullptr, 16) & 0xffffff;
        } catch (...) {
        }
        CHOOSEFONTW choose{sizeof(choose)};
        choose.hwndOwner = hwnd;
        choose.lpLogFont = &selected_font;
        choose.rgbColors = RGB((color >> 16) & 255, (color >> 8) & 255, color & 255);
        choose.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_EFFECTS | CF_FORCEFONTEXIST;
        if (!ChooseFontW(&choose))
            return;
        draft.overlay_family = selected_font.lfFaceName;
        draft.overlay_points = std::max(choose.iPointSize, 10);
        draft.overlay_weight = std::clamp(static_cast<int>(selected_font.lfWeight), 1, 999);
        draft.overlay_charset = selected_font.lfCharSet;
        draft.overlay_italic = selected_font.lfItalic != 0;
        draft.overlay_underline = selected_font.lfUnderline != 0;
        draft.overlay_strikeout = selected_font.lfStrikeOut != 0;
        SetWindowTextW(find(cid::OverlayForeground),
                       hex((GetRValue(choose.rgbColors) << 16) | (GetGValue(choose.rgbColors) << 8) |
                           GetBValue(choose.rgbColors))
                           .c_str());
        SetWindowTextW(find(cid::OverlayFont), font_label().c_str());
        changed();
    }
    HWND find(int id) const {
        const auto it = controls.find(id);
        return it == controls.end() ? nullptr : it->second;
    }
    void invalidate_preview() {
        if (const auto preview = find(cid::Preview))
            InvalidateRect(preview, nullptr, TRUE);
    }
    HWND make(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int id) {
        if (_wcsicmp(cls, WC_COMBOBOXW) == 0)
            style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
        auto measured=pref_now();
        HWND c = CreateWindowExW(0, cls, text,
                                 WS_CHILD | (wcscmp(cls,L"EDIT")==0?WS_BORDER:0) | (building&&parent!=hwnd?0:WS_VISIBLE) | style, 0, 0, px(320), px(_wcsicmp(cls, WC_COMBOBOXW)==0?220:26), parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
                                 nullptr);
        apply_ui_theme(c,false);pref_elapsed("create",id,measured);measured=pref_now();
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        pref_elapsed("font",id,measured);
        if (_wcsicmp(cls, WC_COMBOBOXW) == 0) {
            // Inserting options into a 1px visible combo repeatedly recomputes its native layout.
            // Populate once while redraw is suspended; the page is still hidden.
            if(building)SendMessageW(c,WM_SETREDRAW,FALSE,0);
            SetWindowSubclass(c, combo_print_proc, 1, 0);
        }
        if (id)
            controls[id] = c;
        return c;
    }
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
                draw.itemData = SendMessageW(h, CB_GETITEMDATA, item, 0);
                SendMessageW(GetParent(h), WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
            }
        }
        return result;
    }
    void dependencies() {
        const auto checked = [&](int id) { return SendMessageW(find(id), BM_GETCHECK, 0, 0) == BST_CHECKED; };
        const auto enable = [&](int id, bool value) {
            EnableWindow(find(id), value);
            if (find(id + 10000))
                EnableWindow(find(id + 10000), value);
        };
        const auto mode = SendMessageW(find(cid::ZoomMode), CB_GETCURSEL, 0, 0);
        enable(cid::ActivateReused, !checked(cid::AllowMultiple));
        enable(cid::CutColor, !checked(cid::CutTransparent));
        enable(cid::CutColor + 5000, !checked(cid::CutTransparent));
        for (const int id : {cid::BackgroundPattern, cid::BackgroundSecond})
            enable(id, checked(cid::BackgroundTiled));
        enable(cid::BackgroundSecond + 5000, checked(cid::BackgroundTiled));
        enable(cid::ZoomStep, mode == 0);
        enable(cid::ZoomBelow, mode == 1);
        enable(cid::ZoomAbove, mode == 1);
        for(int id:{cid::DisplayDown,cid::DisplayUp,cid::FullDown,cid::FullUp})enable(id,true);
        enable(cid::HideWait, checked(cid::HideCursor));
        const bool user_position = SendMessageW(find(cid::StartupPosition), CB_GETCURSEL, 0, 0) == 2;
        for (int id : {cid::WindowX, cid::WindowY, cid::WindowWidth, cid::WindowHeight})
            enable(id, user_position);
        enable(cid::WindowCurrent, user_position);
        enable(cid::SlideSubtree, checked(cid::SlideNextFolder));
        const bool normal_start = SendMessageW(find(cid::StartupState), CB_GETCURSEL, 0, 0) == 0;
        enable(cid::RestoreMaximized, normal_start);
        enable(cid::RestoreFullscreen, normal_start);
        enable(cid::OuterFrame, !checked(cid::Caption));
    }
    void changed() {
        if (!building) {
            if(!dirty)SetWindowTextW(hwnd,L"GVN の設定（変更あり）");dirty = true;
            EnableWindow(find(cid::ApplyButton), TRUE);
            dependencies();mark_fields();
        }
    }
    void scroll_page(HWND page, int code, int position = 0) {
        const int index = GetDlgCtrlID(page) - cid::PageBase;
        if (index < 0 || index >= static_cast<int>(pages.size()))
            return;
        SCROLLINFO info{sizeof(info), SIF_ALL};
        GetScrollInfo(page_scrollbars[index], SB_CTL, &info);
        int target = scroll_y[index];
        if (code == SB_LINEUP)
            target -= 34;
        else if (code == SB_LINEDOWN)
            target += 34;
        else if (code == SB_PAGEUP)
            target -= std::max(34, static_cast<int>(info.nPage) - 34);
        else if (code == SB_PAGEDOWN)
            target += std::max(34, static_cast<int>(info.nPage) - 34);
        else if (code == SB_TOP)
            target = 0;
        else if (code == SB_BOTTOM)
            target = info.nMax;
        else if (code == SB_THUMBPOSITION)
            target = position;
        else if (code == SB_THUMBTRACK)
            target = info.nTrackPos;
        target = std::clamp(target, 0, std::max(0, info.nMax - static_cast<int>(info.nPage) + 1));
        if (target != scroll_y[index]) {
            scroll_y[index] = target;
            layout();
            RedrawWindow(page, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
        }
    }
    void reveal_control(HWND control) {
        const auto page = GetParent(control);
        const int index = GetDlgCtrlID(page) - cid::PageBase;
        if (index < 0 || index >= static_cast<int>(pages.size()))
            return;
        RECT r{}, area{};
        GetWindowRect(control, &r);
        MapWindowPoints(nullptr, page, reinterpret_cast<POINT *>(&r), 2);
        GetClientRect(page, &area);
        if (r.top < 0)
            scroll_page(page, SB_THUMBPOSITION, scroll_y[index] + MulDiv(r.top - px(8), 96, dpi));
        else if (r.bottom > area.bottom)
            scroll_page(page, SB_THUMBPOSITION,
                        scroll_y[index] + MulDiv(r.bottom - area.bottom + px(8), 96, dpi));
    }
    static LRESULT CALLBACK field_focus_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        const auto self=reinterpret_cast<Impl *>(data);
        if(m==WM_SETFOCUS){self->reveal_control(h);self->focused_field=GetDlgCtrlID(h);if(self->focused_field>=5000)self->focused_field-=5000;const bool resettable=std::any_of(self->schema.begin(),self->schema.end(),[&](const auto &f){return f.id==self->focused_field;});EnableWindow(self->find(cid::ResetField),resettable);}
        if(m==WM_CONTEXTMENU){auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"この項目を標準に戻す");AppendMenuW(menu,MF_STRING,2,L"関連する設定…");RECT rect{};GetWindowRect(h,&rect);const int x=GET_X_LPARAM(l)==-1?rect.left:GET_X_LPARAM(l),y=GET_Y_LPARAM(l)==-1?rect.bottom:GET_Y_LPARAM(l);ui_theme_menu(menu);const auto id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,x,y,0,self->hwnd,nullptr);ui_destroy_menu(menu);if(id==1)self->reset_field(GetDlgCtrlID(h));if(id==2)self->related_settings(h);return 0;}
        return DefSubclassProc(h, m, w, l);
    }
    static LRESULT CALLBACK page_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Impl *>(data);
        if (m == WM_DROPFILES && GetDlgCtrlID(h) == cid::PageBase + static_cast<int>(Page::External)) {
            self->external_drop(reinterpret_cast<HDROP>(w));
            return 0;
        }
        if (m == WM_VSCROLL) {
            self->scroll_page(h, LOWORD(w), HIWORD(w));
            return 0;
        }
        if (m == WM_MOUSEWHEEL) {
            const auto code = GET_WHEEL_DELTA_WPARAM(w) > 0 ? SB_LINEUP : SB_LINEDOWN;
            for (int i = 0; i < 3; ++i)
                self->scroll_page(h, code);
            return 0;
        }
        if (m == WM_COMMAND || m == WM_NOTIFY || m == WM_DRAWITEM || m == WM_MEASUREITEM ||
            m == WM_CTLCOLORSTATIC) {
            const auto parent = GetParent(h);
            SetWindowLongPtrW(parent, DWLP_MSGRESULT, 0);
            const auto result = SendMessageW(parent, m, w, l);
            return m == WM_NOTIFY ? GetWindowLongPtrW(parent, DWLP_MSGRESULT) : result;
        }
        return DefSubclassProc(h, m, w, l);
    }
    void stop_key_capture() {
        capturing_key = false;
        SetWindowTextW(find(cid::KeyCapture), L"キーを押して入力");
    }
    static LRESULT CALLBACK key_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Impl *>(data);
        if (self->capturing_key && m == WM_GETDLGCODE)
            return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
        if (self->capturing_key && (m == WM_KEYDOWN || m == WM_SYSKEYDOWN || m == WM_IME_KEYDOWN)) {
            // The IME placeholder is followed by its concrete virtual-key notification.
            if (w == VK_PROCESSKEY)
                return 0;
            if (w == VK_CONTROL || w == VK_SHIFT || w == VK_MENU)
                return 0;
            const UINT mods = ((GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0) |
                              ((GetKeyState(VK_SHIFT) & 0x8000) ? 2 : 0) |
                              ((GetKeyState(VK_MENU) & 0x8000) ? 4 : 0);
            self->captured_key = static_cast<UINT>(w);
            self->stop_key_capture();
            if (w == VK_TAB && mods == 7) {
                SetFocus(self->find(cid::KeyCapture));
                return 0;
            }
            if (w == VK_ESCAPE && mods == 0)
                return 0;
            const auto text = format_key(static_cast<UINT>(w), mods);
            if (text.empty() || !assignable_key(text)) {
                self->failure = L"このキーは登録できません。別のキーを入力してください。";
                self->notify_error();
            } else
                SetWindowTextW(h, text.c_str());
            return 0;
        }
        if (self->captured_key && (m == WM_CHAR || m == WM_SYSCHAR || m == WM_IME_CHAR))
            return 0;
        if (self->captured_key == w && (m == WM_KEYUP || m == WM_SYSKEYUP || m == WM_IME_KEYUP)) {
            self->captured_key = 0;
            return 0;
        }
        if (m == WM_KILLFOCUS)
            self->stop_key_capture();
        return DefSubclassProc(h, m, w, l);
    }
    void place(HWND c, int x, int y, int w, int h) {
        if (!c || GetParent(c) != pages[selected])
            return;
        SetWindowPos(c,nullptr,px(x),px(y),px(w),px(h),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW|SWP_SHOWWINDOW);
    }
    void initialize() {
        auto last = GetTickCount64();
        const auto checkpoint = [&](const char *name) {
            const auto now = GetTickCount64();
            initialization_ms.emplace_back(name, now - last);
            last = now;
        };
        building = true;
        for (const auto &binding : default_bindings())
            if (std::find(key_commands.begin(), key_commands.end(), binding.command) == key_commands.end())
                key_commands.push_back(binding.command);
        for (const auto &entry : command_catalog())
            if (std::find(key_commands.begin(), key_commands.end(), entry.id) == key_commands.end())
                key_commands.push_back(entry.id);
        checkpoint("command_catalog");
        dpi = GetDpiForWindow(hwnd);
        font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0,
                           0, CLEARTYPE_QUALITY, 0, L"Yu Gothic UI");
        checkpoint("font");
        SetWindowTextW(hwnd, L"GVN の設定");
        groups=make(hwnd,WC_TABCONTROLW,L"設定カテゴリ",WS_TABSTOP|TCS_BUTTONS|TCS_FLATBUTTONS|TCS_FIXEDWIDTH,cid::Groups);MoveWindow(groups,px(12),px(48),px(736),px(32),FALSE);
        for(int i=0;i<7;++i){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t *>(group_titles[i]);TabCtrl_InsertItem(groups,i,&item);}
        TabCtrl_SetItemSize(groups,px(84),px(30));
        search=make(hwnd,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,cid::Search);SendMessageW(search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"設定を検索（例：ホイール、透明、起動位置）"));
        search_results=make(hwnd,L"LISTBOX",L"",WS_TABSTOP|WS_BORDER|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,cid::SearchResults);ShowWindow(search_results,SW_HIDE);
        SetWindowSubclass(search,search_proc,91,reinterpret_cast<DWORD_PTR>(this));SetWindowSubclass(search_results,search_proc,91,reinterpret_cast<DWORD_PTR>(this));
        make(hwnd,L"BUTTON",L"項目を標準に戻す",WS_TABSTOP,cid::ResetField);EnableWindow(find(cid::ResetField),FALSE);
        make(hwnd,L"BUTTON",L"関連設定…",WS_TABSTOP,cid::Related);
        tab = make(hwnd, WC_TABCONTROLW, L"", WS_TABSTOP, cid::Tabs);
        // Avoid repeatedly laying out a one-pixel-wide multiline tab while adding labels.
        MoveWindow(tab, px(12), px(12), px(736), px(450), FALSE);
        SendMessageW(tab, WM_SETREDRAW, FALSE, 0);
        rebuild_tabs(selected);
        SendMessageW(tab, WM_SETREDRAW, TRUE, 0);
        checkpoint("tab_labels");
        make(hwnd, L"BUTTON", L"このページを標準に戻す", WS_TABSTOP, cid::Reset);
        make(hwnd, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
        make(hwnd, L"BUTTON", L"キャンセル", WS_TABSTOP, IDCANCEL);
        make(hwnd, L"BUTTON", L"適用", WS_TABSTOP, cid::ApplyButton);
        checkpoint("dialog_buttons");
        EnableWindow(find(cid::ApplyButton), FALSE);
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int width = std::min(px(760), static_cast<int>(work.right - work.left)),
                  height = std::min(px(570), static_cast<int>(work.bottom - work.top));
        SetWindowPos(hwnd, nullptr, work.left + (work.right - work.left - width) / 2,
                     work.top + (work.bottom - work.top - height) / 2, width, height, SWP_NOZORDER);
        building = false;
        const int requested_destination = destination_index;
        select(selected);
        if (selected == static_cast<int>(Page::FileOperations) && destination_index >= 0) {
            const bool previous = building;
            building = true;
            ListView_SetItemState(find(cid::DestinationList), -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_SetItemState(find(cid::DestinationList), requested_destination,
                                  LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(find(cid::DestinationList), requested_destination, FALSE);
            load_row(cid::DestinationList, requested_destination);
            building = previous;
            reveal_control(find(cid::DestinationEdit));
            SetFocus(find(cid::DestinationEdit));
            // The modal dialog is activated after WM_INITDIALOG. Apply its requested
            // initial focus through the dialog manager once activation has completed.
            PostMessageW(hwnd, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(find(cid::DestinationEdit)), TRUE);
        }
        checkpoint("first_page_and_layout");
    }
    void ensure_page(Page requested) {
        const auto index = static_cast<size_t>(requested);
        if (page_ready[index])
            return;
        auto measured=pref_now();
        const bool previous = building;
        building = true;
        pages[index] = CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0,
                                       1, 1, hwnd, reinterpret_cast<HMENU>(cid::PageBase + index),
                                       GetModuleHandleW(nullptr), nullptr);
        SetWindowSubclass(pages[index], page_proc, 1, reinterpret_cast<DWORD_PTR>(this));apply_ui_theme(pages[index],false);
        controls[cid::PageBase + static_cast<int>(index)] = pages[index];
        page_scrollbars[index] = make(pages[index], L"SCROLLBAR", L"", SBS_VERT | WS_TABSTOP,
                                      cid::ScrollBase + static_cast<int>(index));
        for (const auto &f : schema) {
            if (f.page != requested)
                continue;
            auto page = pages[static_cast<size_t>(f.page)];
            if (f.kind == Kind::Check)
                make(page, L"BUTTON", f.label, BS_AUTOCHECKBOX | WS_TABSTOP, f.id);
            else {
                make(page, L"STATIC", f.label, SS_LEFT, f.id + 10000);
                make(page,
                     f.kind == Kind::Combo  ? WC_COMBOBOXW
                     : f.kind == Kind::Font ? L"BUTTON"
                                            : L"EDIT",
                     L"",
                     WS_TABSTOP | (f.kind == Kind::Combo  ? CBS_DROPDOWNLIST | WS_VSCROLL
                                   : f.kind == Kind::Font ? 0
                                                          : ES_AUTOHSCROLL | ((f.id == cid::WindowWidth || f.id == cid::WindowHeight) ? ES_NUMBER : 0)),
                     f.id);
                if (f.kind == Kind::Combo)
                    for (auto option : f.options)
                        SendMessageW(find(f.id), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option));
                else
                    SendMessageW(find(f.id), EM_SETLIMITTEXT,
                                 (f.id == cid::WindowX || f.id == cid::WindowY ||
                                  f.id == cid::WindowWidth || f.id == cid::WindowHeight) ? 30000 : 32, 0);
                if (f.kind == Kind::Color) {
                    make(page, L"BUTTON", L"色を選択…", WS_TABSTOP, f.id + 5000);
                    SetWindowSubclass(find(f.id + 5000), field_focus_proc, 2,
                                      reinterpret_cast<DWORD_PTR>(this));
                }
            }
            SetWindowSubclass(find(f.id), field_focus_proc, 2, reinterpret_cast<DWORD_PTR>(this));
        }
        pref_elapsed("page-schema",int(index),measured);measured=pref_now();
        if (requested == Page::Window) {
            make(pages[static_cast<int>(Page::Window)], L"BUTTON", L"現在の位置とサイズを取り込む",
                 WS_TABSTOP, cid::WindowCurrent);
            SetWindowSubclass(find(cid::WindowCurrent), field_focus_proc, 2,
                              reinterpret_cast<DWORD_PTR>(this));
        }
        if (requested == Page::Background) {
            make(pages[static_cast<int>(Page::Background)], L"STATIC", L"背景のプレビュー", SS_OWNERDRAW,
                 cid::Preview);
        }
        auto list = [&](Page page, int list_id, int edit_id, int button_id, const wchar_t *button_text) {
            auto parent = pages[static_cast<int>(page)];
            auto h = make(parent, WC_LISTVIEWW, L"",
                          WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, list_id);
            ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<wchar_t *>(list_id == cid::KeyList           ? L"機能"
                                                   : list_id == cid::DestinationList ? L"操作 / 番号"
                                                                                     : L"番号");
            column.cx = px(210);
            ListView_InsertColumn(h, 0, &column);
            column.pszText = const_cast<wchar_t *>(list_id == cid::KeyList ? L"ショートカット" : L"パス");
            column.cx = px(370);
            ListView_InsertColumn(h, 1, &column);
            make(parent, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, edit_id);
            SendMessageW(find(edit_id), EM_SETLIMITTEXT, list_id == cid::KeyList ? 80 : 32760, 0);
            make(parent, L"BUTTON", button_text, WS_TABSTOP, button_id);
        };
        if (requested == Page::Keys) {
            list(Page::Keys, cid::KeyList, cid::KeyEdit, cid::KeyAssign, L"置換");
            auto key_page = pages[static_cast<int>(Page::Keys)];
            make(key_page, L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, cid::KeySlots);
            make(key_page, L"BUTTON", L"追加", WS_TABSTOP, cid::KeyAdd);
            make(key_page, L"BUTTON", L"削除", WS_TABSTOP, cid::KeyRemove);
            make(key_page, L"BUTTON", L"先頭へ", WS_TABSTOP, cid::KeyFirst);
            make(key_page, L"BUTTON", L"キーを押して入力", WS_TABSTOP, cid::KeyCapture);
            SetWindowSubclass(find(cid::KeyEdit), key_proc, 1, reinterpret_cast<DWORD_PTR>(this));
            make(key_page, L"STATIC",
                 L"1機能に最大8キー。先頭のキーをメニューに表示します。\r\n入力例: Ctrl+O / Shift+R / "
                 L"F8。Esc単独・OS予約キーは登録できません。",
                 SS_LEFT, cid::KeyHelp);
        }
        if (requested == Page::FileOperations) {
            list(Page::FileOperations, cid::DestinationList, cid::DestinationEdit, cid::DestinationBrowse,
                 L"参照…");
        }
        if (requested == Page::External) {
            initialize_external();
        }
        if (requested == Page::MouseCustom)
            initialize_mouse();
        pref_elapsed("page-extra",int(index),measured);measured=pref_now();
        fill_fields(static_cast<int>(requested));
        pref_elapsed("page-values",int(index),measured);measured=pref_now();
        const int list_id = requested == Page::Keys             ? cid::KeyList
                            : requested == Page::FileOperations ? cid::DestinationList
                            : requested == Page::External       ? cid::ExternalList
                                                                : 0;
        if (list_id) {
            fill_list(list_id);
            if (list_id != cid::ExternalList)
                ListView_SetItemState(find(list_id), 0, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
            load_row(list_id, 0);
        }
        if (requested == Page::Mouse || requested == Page::MouseCustom)
            fill_mouse();
        for(const auto &[id,control]:controls)if(GetParent(control)==pages[index]) {
            wchar_t cls[32]{};GetClassNameW(control,cls,32);
            if(_wcsicmp(cls,WC_COMBOBOXW)==0)SendMessageW(control,WM_SETREDRAW,TRUE,0);
        }
        pref_elapsed("page-lists",int(index),measured);
        page_ready[index] = true;
        building = previous;remember_fields();
    }
    void initialize_external() {
        const auto page = pages[static_cast<size_t>(Page::External)];
        const auto labeled = [&](int id, const wchar_t *label, const wchar_t *cls, DWORD style) {
            make(page, L"STATIC", label, SS_LEFT, id + 10000);
            const auto control = make(page, cls, L"", WS_TABSTOP | style, id);
            SetWindowSubclass(control, field_focus_proc, 2, reinterpret_cast<DWORD_PTR>(this));
        };
        labeled(cid::ExternalList, L"登録する番号", WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_VSCROLL);
        labeled(cid::ExternalName, L"メニュー項目名", L"EDIT", ES_AUTOHSCROLL);
        labeled(cid::ExternalEdit, L"実行ファイル名", L"EDIT", ES_AUTOHSCROLL);
        labeled(cid::ExternalSend, L"送る", WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_VSCROLL);
        labeled(cid::ExternalShow, L"ウィンドウ", WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_VSCROLL);
        labeled(cid::ExternalVerb, L"アクション", L"EDIT", ES_AUTOHSCROLL);
        for (const auto *option : {L"送らない", L"現在のファイルのパスを送る", L"現在のフォルダのパスを送る",
                                  L"画像をクリップボードへコピー（Alpha保持）", L"表示中の画像を送る（従来のGVN）"})
            SendMessageW(find(cid::ExternalSend), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option));
        for (const auto *option : {L"通常", L"最小化", L"最大化"})
            SendMessageW(find(cid::ExternalShow), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option));
        SendMessageW(find(cid::ExternalName), EM_SETLIMITTEXT, 50, 0);
        for (int id : {cid::ExternalEdit, cid::ExternalVerb}) SendMessageW(find(id), EM_SETLIMITTEXT, 32760, 0);
        make(page, L"BUTTON", L"書庫内を閲覧しているとき", BS_GROUPBOX, cid::ExternalArchiveGroup);
        for (const auto &[id, label] : std::vector<std::pair<int, const wchar_t *>>{
                 {cid::ExternalQuit, L"起動後にGVNを終了"}, {cid::ExternalSeparator, L"メニュー項目の下に区切りを表示"},
                 {cid::ExternalArchiveMember, L"ファイルパスを送るとき、書庫内のパスを含める"},
                 {cid::ExternalArchiveFolder, L"フォルダパスを送るとき、書庫ファイルのパスを送る"}}) {
            const auto control = make(page, L"BUTTON", label, BS_AUTOCHECKBOX | WS_TABSTOP, id);
            SetWindowSubclass(control, field_focus_proc, 2, reinterpret_cast<DWORD_PTR>(this));
        }
        for (const auto &[id, label] : std::vector<std::pair<int, const wchar_t *>>{
                 {cid::ExternalUp, L"1つ上へ"}, {cid::ExternalDown, L"1つ下へ"}, {cid::ExternalBrowse, L"参照…"},
                 {cid::ExternalReset, L"この番号を初期状態に戻す"}}) {
            const auto control = make(page, L"BUTTON", label, WS_TABSTOP, id);
            SetWindowSubclass(control, field_focus_proc, 2, reinterpret_cast<DWORD_PTR>(this));
        }
        make(page, L"STATIC", L"実行ファイルをここへドロップして登録できます。\r\n引数の例: \"C:\\App\\editor.exe\" /open \"($f)\"　 ($d)はフォルダです。",
             SS_LEFT, cid::ExternalHelp);
        DragAcceptFiles(page, TRUE);
    }
    void load_external() {
        if (!find(cid::ExternalList)) return;
        const bool previous = building; building = true;
        const auto entry = draft.external_application(external_index);
        SetWindowTextW(find(cid::ExternalName), entry.name.c_str());
        SetWindowTextW(find(cid::ExternalEdit), entry.executable.c_str());
        SetWindowTextW(find(cid::ExternalVerb), entry.verb.c_str());
        SendMessageW(find(cid::ExternalList), CB_SETCURSEL, external_index, 0);
        SendMessageW(find(cid::ExternalSend), CB_SETCURSEL, static_cast<int>(entry.send), 0);
        SendMessageW(find(cid::ExternalShow), CB_SETCURSEL, static_cast<int>(entry.show), 0);
        for (const auto &[id, value] : std::vector<std::pair<int, bool>>{
                 {cid::ExternalQuit, entry.quit_after_launch}, {cid::ExternalSeparator, entry.separator},
                 {cid::ExternalArchiveMember, entry.include_archive_member}, {cid::ExternalArchiveFolder, entry.archive_file_for_folder}})
            SendMessageW(find(id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(find(cid::ExternalUp), external_index > 0);
        EnableWindow(find(cid::ExternalDown), external_index < 8);
        building = previous;
    }
    void store_external() {
        if (!find(cid::ExternalList)) return;
        const auto before = draft.external_application(external_index);
        auto entry = before;
        entry.name = external_application_name(get_text(find(cid::ExternalName)));
        entry.executable = get_text(find(cid::ExternalEdit)); entry.verb = get_text(find(cid::ExternalVerb));
        const auto send = SendMessageW(find(cid::ExternalSend), CB_GETCURSEL, 0, 0),
                   show = SendMessageW(find(cid::ExternalShow), CB_GETCURSEL, 0, 0);
        if (send >= 0 && send <= 4) entry.send = static_cast<ExternalSend>(send);
        if (show >= 0 && show <= 2) entry.show = static_cast<ExternalShow>(show);
        entry.quit_after_launch = SendMessageW(find(cid::ExternalQuit), BM_GETCHECK, 0, 0) == BST_CHECKED;
        entry.separator = SendMessageW(find(cid::ExternalSeparator), BM_GETCHECK, 0, 0) == BST_CHECKED;
        entry.include_archive_member = SendMessageW(find(cid::ExternalArchiveMember), BM_GETCHECK, 0, 0) == BST_CHECKED;
        entry.archive_file_for_folder = SendMessageW(find(cid::ExternalArchiveFolder), BM_GETCHECK, 0, 0) == BST_CHECKED;
        draft.set_external_application(external_index, entry);
        const bool previous = building; building = true;
        const auto selected_row = SendMessageW(find(cid::ExternalList), CB_GETCURSEL, 0, 0);
        const auto label = L"(" + std::to_wstring(external_index + 1) + L")  " + entry.name;
        SendMessageW(find(cid::ExternalList), CB_DELETESTRING, external_index, 0);
        SendMessageW(find(cid::ExternalList), CB_INSERTSTRING, external_index, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(find(cid::ExternalList), CB_SETCURSEL, selected_row >= 0 ? selected_row : external_index, 0);
        SetWindowTextW(find(cid::ExternalName), entry.name.c_str());
        building = previous;
        if (before != entry) changed();
    }
    void reset_external() {
        auto entries = draft.external_applications();
        reset_external_application(entries, external_index);
        draft.set_external_applications(entries);
        fill_list(cid::ExternalList); load_external();
    }
    void register_external_path(const std::filesystem::path &path) {
        store_external();
        auto entry = draft.external_application(external_index);
        const auto absolute = std::filesystem::absolute(path);
        entry.name = external_application_name(absolute.stem().wstring()); entry.executable = absolute.wstring();
        draft.set_external_application(external_index, std::move(entry));
        fill_list(cid::ExternalList); load_external(); changed();
    }
    void external_drop(HDROP drop) {
        struct Finish { HDROP value; ~Finish() { DragFinish(value); } } finish{drop};
        if (DragQueryFileW(drop, 0xffffffff, nullptr, 0) != 1) return;
        const auto length = DragQueryFileW(drop, 0, nullptr, 0);
        if (!length || length > 32760) return;
        std::wstring text(length + 1, L'\0');
        if (DragQueryFileW(drop, 0, text.data(), length + 1) != length) return;
        text.resize(length);
        const std::filesystem::path path(text);
        const auto extension = path.extension().wstring();
        std::error_code file_error;
        if ((_wcsicmp(extension.c_str(), L".exe") == 0 || _wcsicmp(extension.c_str(), L".com") == 0) &&
            std::filesystem::is_regular_file(path, file_error)) register_external_path(path);
    }
    void initialize_mouse() {
        const auto page = pages[static_cast<int>(Page::MouseCustom)];
        const auto list = make(page, WC_LISTVIEWW, L"",
                               WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, cid::MouseList);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        int column = 0;
        for (auto title : {L"マウス操作", L"押しているボタン", L"修飾キー", L"機能"}) {
            LVCOLUMNW c{};
            c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.cx = px(column == 3 ? 260 : column == 1 ? 150 : 120);
            c.pszText = const_cast<wchar_t *>(title);
            ListView_InsertColumn(list, column++, &c);
        }
        for (auto [id, label] : {std::pair{cid::MouseGestureChoice, L"操作"},
                                 {cid::MouseHeld, L"併用"},
                                 {cid::MouseModifiers, L"修飾"},
                                 {cid::MouseActionChoice, L"機能"}}) {
            make(page, L"STATIC", label, SS_LEFT, id + 10000);
            make(page, WC_COMBOBOXW, L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, id);
        }
        SendMessageW(find(cid::MouseActionChoice), CB_SETDROPPEDWIDTH, px(540), 0);
        for (unsigned i = 0; i < static_cast<unsigned>(MouseGesture::Count); ++i)
            SendMessageW(find(cid::MouseGestureChoice), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(mouse_gesture_name(static_cast<MouseGesture>(i))));
        for (unsigned i = 0; i < 8; ++i) {
            const auto label = mouse_modifiers_name(i);
            SendMessageW(find(cid::MouseModifiers), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        make(page, L"BUTTON", L"割り当て", WS_TABSTOP, cid::MouseAssign);
        make(page, L"BUTTON", L"何もしない", WS_TABSTOP, cid::MouseClear);
        make(page, L"BUTTON", L"標準操作を引き継ぐ", WS_TABSTOP, cid::MouseInherit);
        make(page, L"STATIC", L"", SS_LEFT, cid::MouseExplanation);
    }
    void load_mouse_context() {
        if (!find(cid::MouseActionChoice))
            return;
        const bool previous = building;
        building = true;
        SendMessageW(find(cid::MouseGestureChoice), CB_SETCURSEL, static_cast<unsigned>(mouse_gesture), 0);
        SendMessageW(find(cid::MouseModifiers), CB_SETCURSEL, mouse_modifiers, 0);
        mouse_held &= ~mouse_button(mouse_gesture);
        SendMessageW(find(cid::MouseHeld), CB_RESETCONTENT, 0, 0);
        for (unsigned held = 0; held < 32; ++held) {
            if (held & mouse_button(mouse_gesture))
                continue;
            const auto label = mouse_held_name(held);
            const auto i =
                SendMessageW(find(cid::MouseHeld), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(find(cid::MouseHeld), CB_SETITEMDATA, i, held);
            if (held == mouse_held)
                SendMessageW(find(cid::MouseHeld), CB_SETCURSEL, i, 0);
        }
        const auto exact = draft.mouse.exact(mouse_gesture, mouse_held, mouse_modifiers);
        SendMessageW(find(cid::MouseActionChoice), CB_RESETCONTENT, 0, 0);
        auto choices = mouse_actions(mouse_gesture);
        choices.insert(choices.begin(), {gvn::MouseInherit, L"標準操作を引き継ぐ"});
        for (const auto &entry : choices) {
            const auto i = SendMessageW(find(cid::MouseActionChoice), CB_ADDSTRING, 0,
                                        reinterpret_cast<LPARAM>(entry.name.c_str()));
            SendMessageW(find(cid::MouseActionChoice), CB_SETITEMDATA, i, entry.id);
            if (entry.id == exact)
                SendMessageW(find(cid::MouseActionChoice), CB_SETCURSEL, i, 0);
        }
        const bool disabled = draft.mouse.exact(MouseGesture::LeftDrag, 0, 0) == MouseWindow &&
                              (mouse_gesture == MouseGesture::LeftClick || (mouse_held & mouse_left));
        for (int id : {cid::MouseActionChoice, cid::MouseAssign, cid::MouseClear, cid::MouseInherit})
            EnableWindow(find(id), !disabled);
        std::wstring text =
            disabled ? L"左ドラッグがシステム標準のウィンドウ移動のため、この操作は利用できません。"
                     : L"未指定の条件は修飾なし・単独の操作を引き継ぎます。「何もしない」で無効にできます。\r"
                       L"\n同じ併用・修飾条件で、複数ボタンへ同じドラッグ機能は指定できません。";
        if (!disabled)
            text += L"\r\n現在の動作: " + std::wstring(mouse_action_name(draft.mouse.action(
                                              mouse_gesture, mouse_held, mouse_modifiers)));
        SetWindowTextW(find(cid::MouseExplanation), text.c_str());
        building = previous;
    }
    void fill_mouse() {
        const bool previous = building;
        building = true;
        draft.wheel = draft.mouse.wheel_preset();
        draft.middle = draft.mouse.middle_preset();
        SendMessageW(find(cid::Wheel), CB_SETCURSEL, draft.wheel, 0);
        SendMessageW(find(cid::Middle), CB_SETCURSEL, draft.middle, 0);
        ListView_DeleteAllItems(find(cid::MouseList));
        for (size_t i = 0; i < draft.mouse.bindings.size(); ++i) {
            const auto &b = draft.mouse.bindings[i];
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(i);
            item.pszText = const_cast<wchar_t *>(mouse_gesture_name(b.gesture));
            ListView_InsertItem(find(cid::MouseList), &item);
            auto held = mouse_held_name(b.held), mods = mouse_modifiers_name(b.modifiers);
            ListView_SetItemText(find(cid::MouseList), item.iItem, 1, held.data());
            ListView_SetItemText(find(cid::MouseList), item.iItem, 2, mods.data());
            ListView_SetItemText(find(cid::MouseList), item.iItem, 3,
                                 const_cast<wchar_t *>(mouse_action_name(b.action)));
        }
        load_mouse_context();
        building = previous;
    }
    bool assign_mouse(unsigned action) {
        try {
            draft.mouse.assign(mouse_gesture, mouse_held, mouse_modifiers, action);
        } catch (const std::invalid_argument &) {
            failure = L"このマウス操作へは割り当てられません。同じ条件のドラッグ機能が重複していないか確認し"
                      L"てください。";
            notify_error();
            load_mouse_context();
            return false;
        }
        fill_mouse();
        changed();
        return true;
    }
    void fill_fields(int only_page = -1) {
        const bool previous = building;
        building = true;
        for (const auto &f : schema) {
            if (only_page >= 0 && static_cast<int>(f.page) != only_page)
                continue;
            auto c = find(f.id);
            if (!c)
                continue;
            if (f.kind == Kind::Font)
                SetWindowTextW(c, font_label().c_str());
            else if (f.kind == Kind::Check)
                SendMessageW(c, BM_SETCHECK, f.flag_value(draft) ? BST_CHECKED : BST_UNCHECKED, 0);
            else if (f.kind == Kind::Combo)
                SendMessageW(c, CB_SETCURSEL, f.quality?display_quality_index(f.number_value(draft)):f.number_value(draft), 0);
            else {
                std::wstring value = f.kind == Kind::Color ? hex(draft.*f.color)
                                     : f.kind == Kind::Seconds
                                         ? std::to_wstring((f.number_value(draft)) / 1000.0)
                                         : std::to_wstring(f.number_value(draft));
                SetWindowTextW(c, value.c_str());
            }
        }
        dependencies();
        building = previous;
    }
    std::wstring &destination(int row) {
        return row < 10 ? draft.copy_destinations.at(row) : draft.move_destinations.at(row - 10);
    }
    void fill_list(int list_id) {
        auto h = find(list_id);
        if (!h)
            return;
        if (list_id == cid::ExternalList) {
            SendMessageW(h, CB_RESETCONTENT, 0, 0);
            for (size_t i = 0; i < 9; ++i) {
                const auto label = L"(" + std::to_wstring(i + 1) + L")  " + draft.external_application(i).name;
                SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            SendMessageW(h, CB_SETCURSEL, external_index, 0);
            return;
        }
        SendMessageW(h, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(h);
        const int count = list_id == cid::KeyList           ? static_cast<int>(key_commands.size())
                          : list_id == cid::DestinationList ? 20
                                                            : 9;
        for (int i = 0; i < count; ++i) {
            auto name = list_id == cid::KeyList ? std::wstring(command_name(key_commands[i]))
                        : list_id == cid::DestinationList
                            ? std::wstring(i < 10 ? L"コピー " : L"移動 ") + std::to_wstring(i % 10 + 1)
                            : std::to_wstring(i + 1);
            LVITEMW item{};
            item.mask = LVIF_TEXT | (list_id == cid::KeyList ? LVIF_PARAM : 0);
            item.iItem = i;
            item.pszText = name.data();
            item.lParam = list_id == cid::KeyList ? key_commands[i] : 0;
            ListView_InsertItem(h, &item);
            auto value = list_id == cid::KeyList           ? key_summary(key_commands[i])
                         : list_id == cid::DestinationList ? destination(i)
                                                           : draft.external_application(i).name;
            ListView_SetItemText(h, i, 1, value.data());
        }
        SendMessageW(h, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(h, nullptr, FALSE);
    }
    std::wstring key_summary(UINT command) const {
        std::wstring result;
        for (const auto &key : draft.keys_for(command)) {
            if (!result.empty())
                result += L" / ";
            result += key;
        }
        return result;
    }
    void load_key_slots(int selection) {
        const bool previous = building;
        building = true;
        const auto keys = draft.keys_for(key_commands[key_index]);
        SendMessageW(find(cid::KeySlots), LB_RESETCONTENT, 0, 0);
        for (const auto &key : keys)
            SendMessageW(find(cid::KeySlots), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(key.c_str()));
        key_slot = keys.empty() ? -1 : std::clamp(selection, 0, static_cast<int>(keys.size()) - 1);
        SendMessageW(find(cid::KeySlots), LB_SETCURSEL, key_slot, 0);
        SetWindowTextW(find(cid::KeyEdit), key_slot < 0 ? L"" : keys[key_slot].c_str());
        EnableWindow(find(cid::KeyRemove), key_slot >= 0);
        EnableWindow(find(cid::KeyFirst), key_slot > 0);
        auto summary = key_summary(key_commands[key_index]);
        ListView_SetItemText(find(cid::KeyList), key_index, 1, summary.data());
        building = previous;
    }
    void fill_lists() {
        const bool previous = building;
        building = true;
        for (int list_id : {cid::KeyList, cid::DestinationList, cid::ExternalList}) {
            if (!find(list_id)) continue;
            fill_list(list_id);
            int index = list_id == cid::KeyList           ? key_index
                        : list_id == cid::DestinationList ? destination_index
                                                          : external_index;
            if (list_id != cid::ExternalList)
                ListView_SetItemState(find(list_id), index, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
            load_row(list_id, index);
        }
        building = previous;
    }
    void load_row(int list_id, int index) {
        const bool previous = building;
        building = true;
        if (list_id == cid::KeyList) {
            key_index = index;
            load_key_slots(0);
        } else if (list_id == cid::DestinationList) {
            destination_index = index;
            SetWindowTextW(find(cid::DestinationEdit), destination(index).c_str());
        } else {
            external_index = index;
            load_external();
        }
        building = previous;
    }
    bool store_key() {
        if (!find(cid::KeyEdit))
            return true;
        auto value = get_text(find(cid::KeyEdit));
        auto keys = draft.keys_for(key_commands[key_index]);
        if (key_slot >= 0 && value == keys[key_slot])
            return true;
        if (key_slot >= 0) {
            if (value.empty())
                keys.erase(keys.begin() + key_slot);
            else
                keys[key_slot] = value;
        } else if (!value.empty())
            keys.push_back(value);
        try {
            draft.set_keys(key_commands[key_index], keys);
        } catch (const std::invalid_argument &) {
            failure = L"書式・予約キー・重複を確認してください。1機能に最大8キーです。";
            return false;
        }
        load_key_slots(key_slot);
        return true;
    }
    void key_action(int id) {
        auto keys = draft.keys_for(key_commands[key_index]);
        int selection = key_slot;
        if (id == cid::KeyAdd) {
            keys.push_back(get_text(find(cid::KeyEdit)));
            selection = static_cast<int>(keys.size()) - 1;
        } else if (key_slot >= 0 && id == cid::KeyRemove) {
            keys.erase(keys.begin() + key_slot);
        } else if (key_slot > 0 && id == cid::KeyFirst) {
            auto key = keys[key_slot];
            keys.erase(keys.begin() + key_slot);
            keys.insert(keys.begin(), key);
            selection = 0;
        } else
            return;
        try {
            draft.set_keys(key_commands[key_index], keys);
        } catch (const std::invalid_argument &) {
            failure = L"書式・予約キー・重複を確認してください。1機能に最大8キーです。";
            notify_error();
            return;
        }
        load_key_slots(selection);
        changed();
    }
    void store_path(int list_id) {
        if (!find(list_id))
            return;
        if (list_id == cid::ExternalList) { store_external(); return; }
        auto value =
            get_text(find(list_id == cid::DestinationList ? cid::DestinationEdit : cid::ExternalEdit));
        if (list_id == cid::DestinationList && value.size() >= 2 && value.front() == L'"' &&
            value.back() == L'"')
            value = value.substr(1, value.size() - 2);
        const int index = list_id == cid::DestinationList ? destination_index : external_index;
        if (list_id == cid::DestinationList)
            destination(index) = value;
        else
            draft.external[index] = value;
        ListView_SetItemText(find(list_id), index, 1, value.data());
    }
    void notify_error() {
        if (error)
            error(hwnd, failure);
        else
            MessageBoxW(hwnd, failure.c_str(), L"設定を確認してください", MB_OK | MB_ICONWARNING);
    }
    bool commit() {
        if (!store_key()) {
            select(static_cast<int>(Page::Keys));
            notify_error();
            return false;
        }
        store_path(cid::DestinationList);
        store_path(cid::ExternalList);
        Settings next = draft;
        for (const auto &f : schema)
            try {
                if (!find(f.id) || f.kind == Kind::Font)
                    continue;
                if (f.kind == Kind::Check)
                    f.flag_value(next) = SendMessageW(find(f.id), BM_GETCHECK, 0, 0) == BST_CHECKED;
                else if (f.kind == Kind::Combo) {
                    const auto index = SendMessageW(find(f.id), CB_GETCURSEL, 0, 0);
                    if (index < 0 || index >= static_cast<int>(f.options.size()))
                        throw std::runtime_error("choice");
                    f.number_value(next) = f.quality?display_quality_filter(static_cast<int>(index)):static_cast<int>(index);
                } else {
                    const auto value = get_text(find(f.id));
                    size_t used{};
                    if (f.kind == Kind::Color) {
                        if (value.size() != 6)
                            throw std::runtime_error("color");
                        auto color = std::stoul(value, &used, 16);
                        if (used != value.size() || color > 0xffffff)
                            throw std::runtime_error("color");
                        next.*f.color = static_cast<uint32_t>(color);
                    } else if (f.id == cid::WindowX || f.id == cid::WindowY ||
                               f.id == cid::WindowWidth || f.id == cid::WindowHeight) {
                        BOOL valid{};
                        const auto control = find(f.id);
                        const int number = static_cast<int>(GetDlgItemInt(GetParent(control), f.id, &valid, TRUE));
                        const bool dimension = f.id == cid::WindowWidth || f.id == cid::WindowHeight;
                        if (valid && (!dimension || number > 0))
                            f.number_value(next) = number;
                    } else {
                        const auto number = std::stod(value, &used) * (f.kind == Kind::Seconds ? 1000 : 1);
                        if (used != value.size() || !std::isfinite(number) || number < f.low ||
                            number > f.high || (f.kind != Kind::Seconds && std::floor(number) != number))
                            throw std::runtime_error("range");
                        f.number_value(next) = static_cast<int>(std::lround(number));
                    }
                }
            } catch (...) {
                // An inactive option must not prevent applying a different mode.
                // Keep its last valid value and normalize the control after apply.
                if (!IsWindowEnabled(find(f.id)))
                    continue;
                failure = std::wstring(f.label) + L" の値を確認してください。";
                select(static_cast<int>(f.page));
                SetFocus(find(f.id));
                notify_error();
                return false;
            }
        next.title_path = next.title_mode == 1;
        if(next.grid!=draft.grid)next.selection_custom=false;
        next.set_end_action(next.end_action);
        try {
            if (apply)
                apply(next);apply_ui_theme(hwnd);
        } catch (const std::exception &) {
            failure = L"設定を保存・適用できませんでした。保存先を確認してください。";
            notify_error();
            return false;
        }
        draft = std::move(next);saved_mouse=draft.mouse;
        fill_fields();
        initial_values.clear();remember_fields();mark_fields();
        dirty = false;SetWindowTextW(hwnd,L"GVN の設定");
        EnableWindow(find(cid::ApplyButton), FALSE);
        return true;
    }
    void reset_page() {
        Settings defaults;
        if(selected==static_cast<int>(Page::General)){draft.selection_custom=false;draft.custom_selection_units={};}
        for (const auto &f : schema)
            if (static_cast<int>(f.page) == selected) {
                if (f.kind == Kind::Font) {
                    draft.overlay_family = defaults.overlay_family;
                    draft.overlay_points = defaults.overlay_points;
                    draft.overlay_weight = defaults.overlay_weight;
                    draft.overlay_charset = defaults.overlay_charset;
                    draft.overlay_italic = defaults.overlay_italic;
                    draft.overlay_underline = defaults.overlay_underline;
                    draft.overlay_strikeout = defaults.overlay_strikeout;
                } else if (f.kind == Kind::Check)
                    f.flag_value(draft) = f.flag_value(defaults);
                else if (f.kind == Kind::Color)
                    draft.*f.color = defaults.*f.color;
                else
                    f.number_value(draft) = f.number_value(defaults);
            }
        if (selected == static_cast<int>(Page::Keys))
            draft.bindings = defaults.bindings;
        if (selected == static_cast<int>(Page::Move))
            draft.list_loop = defaults.list_loop;
        if (selected == static_cast<int>(Page::FileOperations)) {
            draft.copy_destinations = defaults.copy_destinations;
            draft.move_destinations = defaults.move_destinations;
            draft.copy_basic_path = defaults.copy_basic_path;
            draft.move_basic_path = defaults.move_basic_path;
            draft.copy_registered = defaults.copy_registered;
            draft.move_registered = defaults.move_registered;
        }
        if (selected == static_cast<int>(Page::External))
            reset_external();
        if (selected == static_cast<int>(Page::Mouse)) {
            draft.mouse.basic_wheel(defaults.mouse.wheel_preset() == 1);
            draft.mouse.basic_middle(1);
            fill_mouse();
        }
        if (selected == static_cast<int>(Page::MouseCustom)) {
            draft.mouse.bindings = defaults.mouse.bindings;
            draft.wheel = defaults.wheel;
            draft.middle = defaults.middle;
            const bool previous = building;
            building = true;
            SendMessageW(find(cid::Wheel), CB_SETCURSEL, draft.wheel, 0);
            SendMessageW(find(cid::Middle), CB_SETCURSEL, draft.middle, 0);
            fill_mouse();
            building = previous;
        }
        fill_fields(selected);
        const bool previous = building;
        building = true;
        const int list_id = selected == static_cast<int>(Page::Keys)             ? cid::KeyList
                            : selected == static_cast<int>(Page::FileOperations) ? cid::DestinationList
                            : selected == static_cast<int>(Page::External)       ? cid::ExternalList
                                                                                 : 0;
        if (list_id) {
            fill_list(list_id);
            const int row = list_id == cid::KeyList           ? key_index
                            : list_id == cid::DestinationList ? destination_index
                                                              : external_index;
            if (list_id != cid::ExternalList)
                ListView_SetItemState(find(list_id), row, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
            load_row(list_id, row);
        }
        building = previous;
        changed();
        invalidate_preview();
        layout();
    }
    void select(int page) {
        if(search_results)ShowWindow(search_results,SW_HIDE);
        if (page < 0 || page >= static_cast<int>(pages.size()))
            return;
        const auto previous = selected;
        ensure_page(static_cast<Page>(page));
        selected = page;
        const int external=page==static_cast<int>(Page::External);
        if(reset_label_external!=external){SetWindowTextW(find(cid::Reset),external?L"この番号を初期状態に戻す":L"このタブを初期状態に戻す");reset_label_external=external;}
        rebuild_tabs(page);const int index=page_index(page);if(TabCtrl_GetCurSel(tab)!=index)TabCtrl_SetCurSel(tab,index);
        if (previous != page)
            ShowWindow(pages[previous], SW_HIDE);
        layout();
        ShowWindow(pages[page], SW_SHOW);
    }
    bool field_shown(const Field &field) const {
        int mode = draft.comic_mode;
        if (const auto control = find(cid::ComicHeightMode)) {
            const auto selection = SendMessageW(control, CB_GETCURSEL, 0, 0);
            if (selection != CB_ERR)
                mode = static_cast<int>(selection);
        }
        return field.id == cid::ComicFill   ? mode == 0
               : field.id == cid::ComicDown ? mode == 1
               : field.id == cid::ComicUp   ? mode == 2
                                            : true;
    }
    void layout() {
        if (!tab || building)
            return;
        const auto measured=pref_now();
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int mode=selected==int(Page::Display)&&find(cid::ComicHeightMode)?int(SendMessageW(find(cid::ComicHeightMode),CB_GETCURSEL,0,0)):0;
        auto &stamp=page_layouts[selected];
        if(stamp.valid&&stamp.width==rc.right&&stamp.height==rc.bottom&&stamp.dpi==int(dpi)&&stamp.scroll==scroll_y[selected]&&stamp.comic_mode==mode)return;
        stamp={int(rc.right),int(rc.bottom),int(dpi),scroll_y[selected],mode,true};
        MoveWindow(search,px(12),px(12),std::max(px(200),int(rc.right)-px(250)),px(28),TRUE);
        MoveWindow(find(cid::ResetField),int(rc.right)-px(230),px(12),px(130),px(28),TRUE);MoveWindow(find(cid::Related),int(rc.right)-px(94),px(12),px(82),px(28),TRUE);
        MoveWindow(groups,px(12),px(48),std::max(px(100),int(rc.right)-px(24)),px(32),TRUE);
        MoveWindow(tab, px(12), px(88), std::max(px(100), static_cast<int>(rc.right) - px(24)),
                   std::max(px(100), static_cast<int>(rc.bottom) - px(144)), TRUE);
        RECT area{};
        GetClientRect(tab, &area);
        TabCtrl_AdjustRect(tab, FALSE, &area);
        for (auto page : {pages[selected]})
            MoveWindow(page, area.left + px(12), area.top + px(88), area.right - area.left,
                       area.bottom - area.top, TRUE);
        RECT page_rc{};
        GetClientRect(pages[selected], &page_rc);
        const int width = MulDiv(page_rc.right, 96, dpi);
        std::array<int, static_cast<size_t>(Page::Count)> content{}, y{};
        content.fill(32);
        for (const auto &f : schema)
            if (field_shown(f))
                content[static_cast<size_t>(f.page)] += f.page == Page::Mouse ? 29 : 34;
        content[static_cast<size_t>(Page::Window)] += 34;
        content[static_cast<size_t>(Page::Background)] += 116;
        content[static_cast<size_t>(Page::FileOperations)] += 260;
        content[static_cast<size_t>(Page::External)] = 470;
        content[static_cast<size_t>(Page::Mask)] = 354;
        const int page_height = MulDiv(area.bottom - area.top, 96, dpi);
        for (size_t i = 0; i < pages.size(); ++i) {
            if (static_cast<int>(i) != selected)
                continue;
            scroll_y[i] = std::clamp(scroll_y[i], 0, std::max(0, content[i] - page_height));
            SCROLLINFO info{sizeof(info),
                            SIF_RANGE | SIF_PAGE | SIF_POS,
                            0,
                            content[i] - 1,
                            static_cast<UINT>(std::max(1, page_height)),
                            scroll_y[i]};
            ui_set_scroll_info(page_scrollbars[i],info);
            ShowWindow(page_scrollbars[i], content[i] > page_height ? SW_SHOW : SW_HIDE);
            RECT client{};
            GetClientRect(pages[i], &client);
            const int bar_width = GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
            MoveWindow(page_scrollbars[i], client.right - bar_width, 0, bar_width, client.bottom, TRUE);
        }
        y.fill(16);
        for (const auto &f : schema) {
            const auto page = static_cast<size_t>(f.page);
            if (static_cast<int>(page) != selected)
                continue;
            const auto show = field_shown(f);
            for (const auto id : {f.id, f.id + 10000, f.id + 5000})
                if (const auto control = find(id))
                    ShowWindow(control, show ? SW_SHOW : SW_HIDE);
            if (!show)
                continue;
            const int row = y[page] - scroll_y[page];
            RECT field_area{};
            GetClientRect(pages[page], &field_area);
            const int field_width =
                MulDiv(field_area.right -
                           (content[page] > page_height ? GetSystemMetricsForDpi(SM_CXVSCROLL, dpi) : 0),
                       96, dpi);
            if (f.page == Page::Mask && f.kind == Kind::Check && f.id != cid::GlassRemainder && f.id != cid::FLNegative) {
                const int negative[]{cid::MaskNegativeRed, cid::MaskNegativeGreen, cid::MaskNegativeBlue};
                const int xor_ids[]{cid::MaskXorRed, cid::MaskXorGreen, cid::MaskXorBlue};
                const int q0[]{cid::Q0Vertical, cid::Q0Horizontal, cid::Q0Negative, cid::Q0Xor,
                               cid::Q0FlipVertical, cid::Q0FlipHorizontal};
                int column{}, position{};
                const auto locate = [&](auto &ids, int col) {
                    for (size_t n = 0; n < std::size(ids); ++n)
                        if (ids[n] == f.id) { column = col; position = static_cast<int>(n); }
                };
                locate(negative, 0); locate(xor_ids, 1); locate(q0, 2);
                const int cell = (field_width - 36) / 3;
                place(find(f.id), 18 + column * cell, 158 + position * 30 - scroll_y[page], cell - 4, 26);
                continue;
            }
            y[page] += f.page == Page::Mouse ? 29 : 34;
            if (f.kind == Kind::Check)
                place(find(f.id), 18, row, field_width - 36, 26);
            else {
                place(find(f.id + 10000), 18, row + 4, 290, 24);
                place(find(f.id), 312, row, f.kind == Kind::Color ? 112 : std::max(120, field_width - 336),
                      f.kind == Kind::Combo ? 220 : 26);
                if (f.kind == Kind::Color)
                    place(find(f.id + 5000), 436, row, 100, 26);
            }
            if (f.id == cid::WindowHeight) {
                place(find(cid::WindowCurrent), 312, row + 34, std::max(200, field_width - 336), 26);
                y[page] += 34;
            }
        }
        if (selected == static_cast<int>(Page::Background)) {
            const auto page = static_cast<size_t>(Page::Background);
            const int scrollbar = content[page] > page_height ?
                                      MulDiv(GetSystemMetricsForDpi(SM_CXVSCROLL, dpi), 96, dpi) : 0;
            place(find(cid::Preview), 18, y[page] + 8 - scroll_y[page],
                  std::max(150, width - 36 - scrollbar), 100);
        }
        auto list = [&](int list_id, int edit_id, int button_id, int start, int height) {
            place(find(list_id), 18, start, width - 36, height);
            place(find(edit_id), 18, start + height + 12, width - 150, 28);
            place(find(button_id), width - 122, start + height + 12, 104, 28);
            const int first_width = list_id == cid::KeyList           ? 210
                                    : list_id == cid::DestinationList ? 100
                                                                      : 50;
            ListView_SetColumnWidth(find(list_id), 0, px(first_width));
            ListView_SetColumnWidth(find(list_id), 1, px(std::max(100, width - first_width - 50)));
        };
        const int mouse_height = std::max(100, page_height - 220), mouse_y = mouse_height + 28;
        place(find(cid::MouseList), 18, 16, width - 36, mouse_height);
        for (int column = 0; column < 4; ++column)
            ListView_SetColumnWidth(find(cid::MouseList), column,
                                    px(column == 3   ? std::max(180, width - 440)
                                       : column == 1 ? 150
                                                     : 120));
        place(find(cid::MouseGestureChoice + 10000), 18, mouse_y + 4, 60, 24);
        place(find(cid::MouseGestureChoice), 86, mouse_y, width / 2 - 104, 250);
        place(find(cid::MouseHeld + 10000), width / 2, mouse_y + 4, 48, 24);
        place(find(cid::MouseHeld), width / 2 + 54, mouse_y, width / 2 - 72, 250);
        place(find(cid::MouseModifiers + 10000), 18, mouse_y + 36, 60, 24);
        place(find(cid::MouseModifiers), 86, mouse_y + 32, width / 2 - 104, 250);
        place(find(cid::MouseActionChoice + 10000), width / 2, mouse_y + 36, 48, 24);
        place(find(cid::MouseActionChoice), width / 2 + 54, mouse_y + 32, width / 2 - 72, 300);
        place(find(cid::MouseAssign), 18, mouse_y + 68, 104, 28);
        place(find(cid::MouseClear), 134, mouse_y + 68, 104, 28);
        place(find(cid::MouseInherit), 250, mouse_y + 68, 180, 28);
        place(find(cid::MouseExplanation), 18, mouse_y + 104, width - 36, 72);
        const int key_height = std::max(110, page_height - 226), key_y = 28 + key_height;
        place(find(cid::KeyList), 18, 16, width - 36, key_height);
        ListView_SetColumnWidth(find(cid::KeyList), 0, px(210));
        ListView_SetColumnWidth(find(cid::KeyList), 1, px(std::max(100, width - 270)));
        place(find(cid::KeySlots), 18, key_y, 235, 94);
        place(find(cid::KeyEdit), 267, key_y, width - 285, 28);
        int key_x = 267;
        for (int id : {cid::KeyAssign, cid::KeyAdd, cid::KeyRemove, cid::KeyFirst}) {
            place(find(id), key_x, key_y + 40, (width - 305) / 4, 28);
            key_x += (width - 285) / 4;
        }
        place(find(cid::KeyHelp), 18, key_y + 105, width - 36, 48);
        place(find(cid::KeyCapture), 267, key_y + 78, width - 285, 26);
        const int destination_start = y[static_cast<size_t>(Page::FileOperations)] + 10 -
                                      scroll_y[static_cast<size_t>(Page::FileOperations)];
        list(cid::DestinationList, cid::DestinationEdit, cid::DestinationBrowse, destination_start, 200);
        if (selected == static_cast<int>(Page::External)) {
            const auto index = static_cast<size_t>(Page::External);
            const int w = width - (content[index] > page_height ? MulDiv(GetSystemMetricsForDpi(SM_CXVSCROLL, dpi), 96, dpi) : 0);
            const int offset = scroll_y[index];
            const auto at = [&](int id, int x, int y, int width, int height) { place(find(id), x, y - offset, width, height); };
            at(cid::ExternalList + 10000, 18, 20, 116, 24);
            at(cid::ExternalList, 140, 16, w - 380, 260);
            at(cid::ExternalUp, w - 226, 16, 96, 28); at(cid::ExternalDown, w - 122, 16, 104, 28);
            at(cid::ExternalName + 10000, 18, 58, 116, 24); at(cid::ExternalName, 140, 54, w - 158, 26);
            at(cid::ExternalEdit + 10000, 18, 92, 116, 24); at(cid::ExternalEdit, 140, 88, w - 270, 26);
            at(cid::ExternalBrowse, w - 122, 88, 104, 28);
            at(cid::ExternalSend + 10000, 18, 126, 116, 24); at(cid::ExternalSend, 140, 122, w - 158, 250);
            at(cid::ExternalQuit, 18, 158, w - 36, 26);
            at(cid::ExternalShow + 10000, 18, 192, 116, 24); at(cid::ExternalShow, 140, 188, 180, 200);
            at(cid::ExternalVerb + 10000, 336, 192, 80, 24); at(cid::ExternalVerb, 420, 188, w - 438, 26);
            at(cid::ExternalSeparator, 18, 226, w - 36, 26);
            at(cid::ExternalArchiveGroup, 18, 260, w - 36, 94);
            at(cid::ExternalArchiveMember, 32, 282, w - 64, 26); at(cid::ExternalArchiveFolder, 32, 316, w - 64, 26);
            at(cid::ExternalReset, 18, 366, 250, 28); at(cid::ExternalHelp, 18, 406, w - 36, 56);
        }
        int bottom = static_cast<int>(rc.bottom) - px(43), right = static_cast<int>(rc.right) - px(12);
        MoveWindow(find(cid::Reset), px(12), bottom, px(205), px(29), TRUE);
        for (int id : {cid::ApplyButton, IDCANCEL, IDOK}) {
            right -= px(88);
            MoveWindow(find(id), right, bottom, px(80), px(29), TRUE);
        }
        RedrawWindow(pages[selected], nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        pref_elapsed("layout",selected,measured);
    }
    void finish(bool ok) {
        accepted = ok;
        if (modal)
            EndDialog(hwnd, ok ? IDOK : IDCANCEL);
        else
            DestroyWindow(hwnd);
    }
    INT_PTR message(UINT m, WPARAM w, LPARAM l) {
        if (m == WM_MEASUREITEM && reinterpret_cast<MEASUREITEMSTRUCT *>(l)->CtlType == ODT_COMBOBOX) {
            reinterpret_cast<MEASUREITEMSTRUCT *>(l)->itemHeight = px(20);
            return TRUE;
        }
        if (m == WM_DRAWITEM && reinterpret_cast<DRAWITEMSTRUCT *>(l)->CtlType == ODT_COMBOBOX) {
            const auto d = reinterpret_cast<DRAWITEMSTRUCT *>(l);
            const bool highlighted = (d->itemState & ODS_SELECTED) != 0;
            const auto saved = SaveDC(d->hDC);
            SelectObject(d->hDC, font);
            FillRect(d->hDC, &d->rcItem, ui_brush(highlighted?ui_palette().selected:ui_palette().input));
            if (d->itemID != static_cast<UINT>(-1)) {
                const auto length = SendMessageW(d->hwndItem, CB_GETLBTEXTLEN, d->itemID, 0);
                if (length >= 0) {
                    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
                    SendMessageW(d->hwndItem, CB_GETLBTEXT, d->itemID, reinterpret_cast<LPARAM>(text.data()));
                    SetTextColor(d->hDC, (d->itemState&ODS_DISABLED)?ui_palette().muted:ui_palette().text);
                    SetBkMode(d->hDC, TRANSPARENT);
                    auto rect = d->rcItem;
                    rect.left += px(3);
                    DrawTextW(d->hDC, text.data(), static_cast<int>(length), &rect,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }
            if (d->itemState & ODS_FOCUS)
                DrawFocusRect(d->hDC, &d->rcItem);
            RestoreDC(d->hDC, saved);
            return TRUE;
        }
        if (m == WM_INITDIALOG) {
            initialize();apply_ui_theme(hwnd);
            return selected == static_cast<int>(Page::FileOperations) ? FALSE : TRUE;
        }
        if (m == WM_SIZE) {
            layout();
            return TRUE;
        }
        if (m == WM_GETMINMAXINFO) {
            auto info = reinterpret_cast<MINMAXINFO *>(l);
            info->ptMinTrackSize = {px(660), px(510)};
            return TRUE;
        }
        if (m == WM_CLOSE) {
            finish(false);
            return TRUE;
        }
        if (m == WM_NOTIFY) {
            auto header = reinterpret_cast<NMHDR *>(l);
            if(header->idFrom==cid::Groups&&header->code==TCN_SELCHANGE){const int group=TabCtrl_GetCurSel(groups);for(int p=0;p<int(Page::Count);++p)if(group_for(p)==group){select(p);break;}return TRUE;}
            if (header->idFrom == cid::Tabs && header->code == TCN_SELCHANGE) {
                const int row=TabCtrl_GetCurSel(tab);if(row>=0&&size_t(row)<tab_pages.size())select(tab_pages[row]);
                return TRUE;
            }
            if (!building && header->code == LVN_ITEMCHANGING) {
                auto n = reinterpret_cast<NMLISTVIEW *>(l);
                if ((n->uOldState & LVIS_SELECTED) && !(n->uNewState & LVIS_SELECTED) &&
                    header->idFrom == cid::KeyList && !store_key()) {
                    SetWindowLongPtrW(hwnd, DWLP_MSGRESULT, TRUE);
                    notify_error();
                    return TRUE;
                }
            }
            if (!building && header->code == LVN_ITEMCHANGED) {
                auto n = reinterpret_cast<NMLISTVIEW *>(l);
                if ((n->uNewState & LVIS_SELECTED) && !(n->uOldState & LVIS_SELECTED) && n->iItem >= 0) {
                    const int id = static_cast<int>(header->idFrom);
                    if (id == cid::MouseList) {
                        if (static_cast<size_t>(n->iItem) < draft.mouse.bindings.size()) {
                            const auto &b = draft.mouse.bindings[n->iItem];
                            mouse_gesture = b.gesture;
                            mouse_held = b.held;
                            mouse_modifiers = b.modifiers;
                            load_mouse_context();
                        }
                        return TRUE;
                    }
                    if (id == cid::DestinationList || id == cid::ExternalList)
                        store_path(id);
                    load_row(id, n->iItem);
                }
            }
        }
        if (m == WM_COMMAND) {
            const int id = LOWORD(w), code = HIWORD(w);
            if(id==cid::Search&&code==EN_CHANGE){search_changed();return TRUE;}
            if(id==cid::SearchResults){if(code==LBN_DBLCLK)open_search_result();return TRUE;}
            if(id==cid::ResetField){reset_field(focused_field);return TRUE;}
            if(id==cid::Related){related_settings(find(cid::Related));return TRUE;}
            if(id==IDOK&&(GetFocus()==search||GetFocus()==search_results)&&!get_text(search).empty()){open_search_result();return TRUE;}
            if (!building && id == cid::ExternalList && code == CBN_SELCHANGE) {
                const int row = static_cast<int>(SendMessageW(find(id), CB_GETCURSEL, 0, 0));
                if (row >= 0 && row < 9) { store_external(); load_row(id, row); }
                return TRUE;
            }
            if (!building && (id == cid::ExternalUp || id == cid::ExternalDown)) {
                store_external();
                auto entries = draft.external_applications();
                external_index = static_cast<int>(move_external_application(entries, external_index, id == cid::ExternalUp ? -1 : 1));
                draft.set_external_applications(entries);
                fill_list(cid::ExternalList); load_external(); changed();
                return TRUE;
            }
            if (!building && id == cid::ExternalReset) {
                reset_external(); changed(); return TRUE;
            }
            if (id == cid::OverlayFont && code == BN_CLICKED) {
                choose_overlay_font();
                return TRUE;
            }
            if (id == cid::WindowCurrent && code == BN_CLICKED) {
                RECT current{};
                if (GetWindowRect(GetWindow(hwnd, GW_OWNER), &current)) {
                    SendMessageW(find(cid::StartupPosition), CB_SETCURSEL, 2, 0);
                    for (const auto &[field, value] :
                         std::vector<std::pair<int, int>>{{cid::WindowX, current.left},
                                                          {cid::WindowY, current.top},
                                                          {cid::WindowWidth, current.right - current.left},
                                                          {cid::WindowHeight, current.bottom - current.top}})
                        SetWindowTextW(find(field), std::to_wstring(value).c_str());
                    changed();
                }
                return TRUE;
            }
            if (!building && code == CBN_SELCHANGE && (id == cid::Wheel || id == cid::Middle)) {
                if (id == cid::Wheel) {
                    draft.wheel = static_cast<int>(SendMessageW(find(id), CB_GETCURSEL, 0, 0));
                    if (draft.wheel < 2)
                        draft.mouse.basic_wheel(draft.wheel != 0);
                    else {
                        mouse_gesture = MouseGesture::WheelUp;
                        mouse_held = mouse_modifiers = 0;
                        select(static_cast<int>(Page::MouseCustom));
                    }
                } else {
                    draft.middle = static_cast<int>(SendMessageW(find(id), CB_GETCURSEL, 0, 0));
                    if (draft.middle < 3)
                        draft.mouse.basic_middle(draft.middle);
                    else {
                        mouse_gesture = MouseGesture::MiddleClick;
                        mouse_held = mouse_modifiers = 0;
                        select(static_cast<int>(Page::MouseCustom));
                    }
                }
                fill_mouse();
                changed();
                return TRUE;
            }
            if (!building && code == CBN_SELCHANGE &&
                (id == cid::MouseGestureChoice || id == cid::MouseHeld || id == cid::MouseModifiers)) {
                if (id == cid::MouseGestureChoice)
                    mouse_gesture = static_cast<MouseGesture>(SendMessageW(find(id), CB_GETCURSEL, 0, 0));
                if (id == cid::MouseModifiers)
                    mouse_modifiers = static_cast<unsigned>(SendMessageW(find(id), CB_GETCURSEL, 0, 0));
                if (id == cid::MouseHeld)
                    mouse_held = static_cast<unsigned>(SendMessageW(
                        find(id), CB_GETITEMDATA, SendMessageW(find(id), CB_GETCURSEL, 0, 0), 0));
                load_mouse_context();
                return TRUE;
            }
            if (!building && (id == cid::MouseAssign || id == cid::MouseClear || id == cid::MouseInherit ||
                              (id == cid::MouseActionChoice && code == CBN_SELCHANGE))) {
                const auto action =
                    id == cid::MouseClear ? static_cast<unsigned>(MouseNone)
                    : id == cid::MouseInherit
                        ? gvn::MouseInherit
                        : static_cast<unsigned>(SendMessageW(
                              find(cid::MouseActionChoice), CB_GETITEMDATA,
                              SendMessageW(find(cid::MouseActionChoice), CB_GETCURSEL, 0, 0), 0));
                assign_mouse(action);
                return TRUE;
            }
            if (id == IDCANCEL) {
                finish(false);
                return TRUE;
            }
            if (id == IDOK || id == cid::ApplyButton) {
                if (commit() && id == IDOK)
                    finish(true);
                return TRUE;
            }
            if (id == cid::Reset) {
                reset_page();
                return TRUE;
            }
            if (id == cid::KeyAssign) {
                if (!store_key())
                    notify_error();
                else
                    changed();
                return TRUE;
            }
            if (id == cid::KeyCapture) {
                captured_key = 0;
                capturing_key = true;
                SetWindowTextW(find(cid::KeyCapture), L"入力待ち（Escで中止）");
                SetFocus(find(cid::KeyEdit));
                return TRUE;
            }
            if (id == cid::KeyAdd || id == cid::KeyRemove || id == cid::KeyFirst) {
                key_action(id);
                return TRUE;
            }
            if (!building && id == cid::KeySlots && code == LBN_SELCHANGE) {
                const int target = static_cast<int>(SendMessageW(find(cid::KeySlots), LB_GETCURSEL, 0, 0));
                if (store_key())
                    load_key_slots(target);
                else {
                    SendMessageW(find(cid::KeySlots), LB_SETCURSEL, key_slot, 0);
                    notify_error();
                }
                return TRUE;
            }
            if (id == cid::DestinationBrowse || id == cid::ExternalBrowse) {
                auto path = id == cid::DestinationBrowse ? choose_folder(hwnd, L"振り分け先フォルダ")
                                                         : executable_picker(hwnd);
                if (!path.empty()) {
                    if (id == cid::ExternalBrowse) { register_external_path(path); return TRUE; }
                    SetWindowTextW(
                        find(id == cid::DestinationBrowse ? cid::DestinationEdit : cid::ExternalEdit),
                        path.c_str());
                    store_path(id == cid::DestinationBrowse ? cid::DestinationList : cid::ExternalList);
                    changed();
                }
                return TRUE;
            }
            for (const auto &f : schema)
                if (f.kind == Kind::Color && id == f.id + 5000) {
                    auto initial = draft.*f.color;
                    try {
                        const auto text = get_text(find(f.id));
                        size_t used{};
                        const auto value = std::stoul(text, &used, 16);
                        if (text.size() == 6 && used == 6 && value <= 0xffffff)
                            initial = static_cast<uint32_t>(value);
                    } catch (...) {
                    }
                    if (const auto value=choose_color(hwnd,initial)) {
                        SetWindowTextW(find(f.id), hex(*value).c_str());
                        changed();
                        invalidate_preview();
                    }
                    return TRUE;
                }
            if (code == BN_CLICKED || code == CBN_SELCHANGE || code == EN_CHANGE) {
                changed();
                invalidate_preview();
                if (!building && id == cid::ComicHeightMode && code == CBN_SELCHANGE)
                    layout();
            }
        }
        if (m == WM_DRAWITEM && reinterpret_cast<DRAWITEMSTRUCT *>(l)->CtlID == cid::Preview) {
            auto d = reinterpret_cast<DRAWITEMSTRUCT *>(l);
            uint32_t colors[2]{draft.background_first, draft.background_second};
            const int ids[]{cid::BackgroundFirst, cid::BackgroundSecond};
            for (int i = 0; i < 2; ++i)
                try {
                    const auto text = get_text(find(ids[i]));
                    size_t used{};
                    const auto value = std::stoul(text, &used, 16);
                    if (text.size() == 6 && used == 6 && value <= 0xffffff)
                        colors[i] = static_cast<uint32_t>(value);
                } catch (...) {
                }
            const auto selected_pattern = SendMessageW(find(cid::BackgroundPattern), CB_GETCURSEL, 0, 0);
            const int size = 8 << std::clamp(static_cast<int>(selected_pattern), 0, 4);
            const bool tiled = SendMessageW(find(cid::BackgroundTiled), BM_GETCHECK, 0, 0) == BST_CHECKED;
            HBRUSH brushes[2]{};
            for (int i = 0; i < 2; ++i)
                brushes[i] = CreateSolidBrush(RGB(colors[i] >> 16 & 255, colors[i] >> 8 & 255, colors[i] & 255));
            for (int y = d->rcItem.top; y < d->rcItem.bottom; y += size)
                for (int x = d->rcItem.left; x < d->rcItem.right; x += size) {
                    const int index = tiled ? ((x - d->rcItem.left) / size + (y - d->rcItem.top) / size) % 2 : 0;
                    RECT tile{x, y, std::min(x + size, static_cast<int>(d->rcItem.right)),
                              std::min(y + size, static_cast<int>(d->rcItem.bottom))};
                    FillRect(d->hDC, &tile, brushes[index]);
                }
            for (auto brush : brushes)
                DeleteObject(brush);
            return TRUE;
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
            self->failure = L"設定画面の処理に失敗しました。";
            self->notify_error();
            return TRUE;
        }
    }
};
PreferencesDialog::PreferencesDialog(const Settings &s, Apply a, Error e, Page initial_page,
                                     int destination_row)
    : impl(std::make_unique<Impl>(s, std::move(a), std::move(e))) {
    impl->selected = static_cast<int>(initial_page);
    impl->destination_index = std::clamp(destination_row, 0, 19);
}
PreferencesDialog::~PreferencesDialog() {
    if (IsWindow(impl->hwnd))
        DestroyWindow(impl->hwnd);
    if (impl->font)
        DeleteObject(impl->font);
}
bool PreferencesDialog::show(HWND owner) {
    DialogTemplate t;
    impl->modal = true;
    if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &t.dialog, owner, Impl::procedure,
                                reinterpret_cast<LPARAM>(impl.get())) == -1)
        throw std::runtime_error("Cannot create preferences dialog");
    return impl->accepted;
}
HWND PreferencesDialog::create(HWND owner) {
    DialogTemplate t;
    return CreateDialogIndirectParamW(GetModuleHandleW(nullptr), &t.dialog, owner, Impl::procedure,
                                      reinterpret_cast<LPARAM>(impl.get()));
}
HWND PreferencesDialog::window() const {
    return impl->hwnd;
}
HWND PreferencesDialog::control(int id) const {
    return impl->find(id);
}
void PreferencesDialog::select_page(Page page) {
    impl->select(static_cast<int>(page));
}
void PreferencesDialog::set_executable_picker(std::function<std::filesystem::path(HWND)> picker) {
    impl->executable_picker = picker ? std::move(picker) : std::function<std::filesystem::path(HWND)>(choose_executable);
}
const std::vector<std::pair<const char *, ULONGLONG>> &PreferencesDialog::initialization_timings() const {
    return impl->initialization_ms;
}
bool show_preferences(HWND owner, const Settings &s, const PreferencesDialog::Apply &apply, Page initial_page,
                      int destination_row) {
    PreferencesDialog dialog(s, apply, {}, initial_page, destination_row);
    return dialog.show(owner);
}
} // namespace gvn

namespace gvn {int PreferencesDialog::selected_page() const{return impl->selected;}
int PreferencesDialog::page_tab_index(PreferencePage page) const{return impl->page_index(int(page));}}
