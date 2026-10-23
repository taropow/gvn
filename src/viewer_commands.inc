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
    bool menu_command_enabled(UINT id, const CommandContext &context) const {
        return common_command_enabled(id,context) &&
            !(id == ToggleOuterFrame && settings.caption) &&
            !(id >= WallpaperCenter && id <= WallpaperHeight && !image) &&
            !(id == PaletteEdit && (!image || image->frames.size()!=1 || !image->frames[0].indexed)) &&
            !(id == ConvertColorDepth && !image) && !(id == ColorAdjust && !image) &&
            !(id == WallpaperDelete && !has_wallpaper_files(config));
    }
    void add(HMENU menu, UINT id, const std::wstring &text) {
        auto label = text.substr(0, text.find(L'\t'));
        if (id == SkipBack || id == SkipNext)
            label = std::to_wstring(settings.skip_count) + (id == SkipBack ? L"枚戻る" : L"枚進む");
        label=command_caption(settings,id,label,true);
        AppendMenuW(menu, MF_STRING | (menu_command_enabled(id,command_context()) ? 0 : MF_GRAYED) |
                    (checked(id) ? MF_CHECKED : 0), id, label.c_str());
    }
    static void separator(HMENU menu) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    }
    static void submenu(HMENU root, HMENU child, const wchar_t *label) {
        AppendMenuW(root, MF_POPUP, reinterpret_cast<UINT_PTR>(child), label);
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
        ui_theme_menu(root,!popup);return root;
    }
    std::vector<std::wstring> menu_history;
    bool menu_floating{};
    void refresh_menu_state(HMENU target, const CommandContext &context) {
        const int count = GetMenuItemCount(target);
        for (int n = 0; n < count; ++n) {
            MENUITEMINFOW item{sizeof(item)};
            item.fMask = MIIM_ID | MIIM_SUBMENU | MIIM_STATE | MIIM_FTYPE;
            if (!GetMenuItemInfoW(target,n,TRUE,&item)) continue;
            if (item.hSubMenu) refresh_menu_state(item.hSubMenu,context);
            else if (!(item.fType & MFT_SEPARATOR)) {
                const UINT state = (item.fState & ~(MFS_DISABLED | MFS_CHECKED)) |
                    (menu_command_enabled(item.wID,context) ? MF_ENABLED : MF_GRAYED) |
                    (checked(item.wID) ? MFS_CHECKED : 0);
                const bool radio = item.wID == Grid || (item.wID >= GridFree && item.wID <= Grid32) ||
                    (item.wID >= ViewRotateNone && item.wID <= ViewRotateHalf);
                const UINT type = radio ? (item.fType & ~MFT_RADIOCHECK) |
                    (checked(item.wID) ? MFT_RADIOCHECK : 0) : item.fType;
                if (state != item.fState || type != item.fType) {
                    item.fMask = MIIM_STATE | MIIM_FTYPE; item.fState = state; item.fType = type;
                    SetMenuItemInfoW(target,n,TRUE,&item);
                }
            }
        }
    }
    // Settings changes request a full rebuild. Navigation and ordinary commands
    // reuse handles/owner-draw metadata unless history or floating items changed.
    void refresh_menu(bool rebuild = true) {
        rebuild = rebuild || !main_menu || menu_history != settings.history || menu_floating != floating.has_value();
        if (rebuild) {
            auto next = menu();
            const auto previous = main_menu;
            main_menu = next;
            menu_history = settings.history;
            menu_floating = floating.has_value();
            SetMenu(hwnd, (full ? settings.full_menu : settings.menu) ? next : nullptr);
            if (previous) ui_destroy_menu(previous);
        } else {
            refresh_menu_state(main_menu,command_context());
            const auto attached = (full ? settings.full_menu : settings.menu) ? main_menu : nullptr;
            if (GetMenu(hwnd) != attached) { SetMenu(hwnd,attached); DrawMenuBar(hwnd); }
        }
        if (rebuild) for (auto bar : {toolbar.hwnd, playback.toolbar.hwnd,editor_tools.hwnd})
            for (int i = 0; i < SendMessageW(bar, TB_BUTTONCOUNT, 0, 0); ++i) {
                TBBUTTON button{};
                SendMessageW(bar, TB_GETBUTTON, i, reinterpret_cast<LPARAM>(&button));
                if (button.idCommand && button.idCommand != Play && button.idCommand != Loop) {
                    const auto label=command_caption(settings,button.idCommand);
                    (bar == toolbar.hwnd ? toolbar : bar==editor_tools.hwnd?editor_tools:playback.toolbar).text(button.idCommand, label.c_str());
                }
            }
        if (rebuild && GetMenu(hwnd)) DrawMenuBar(hwnd);
    }
