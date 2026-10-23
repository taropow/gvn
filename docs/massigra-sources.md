# MassiGra 0.45 公式ヘルプ対応表

指定された[基本的な使い方](http://www.massigra.net/massigrahelp/currentver/basic.htm)から、同じ`currentver`配下にあるヘルプページへのリンクをたどった。2026年10月18日の取得では、HTMLページ91件、取得失敗0件を確認した。うちダイアログとその詳細ページは51件、一般設定の詳細ページは11件である。

仕様本文は公式説明を用途別に要約し、全メニュー一覧は項目名と階層を整理した。FAQは関係する機能へ統合し、更新履歴は0.45の仕様や初期状態を読むために必要な変更を抽出した。過去の修正履歴全文は再録していない。

アプリ本体は実行していない。ヘルプ未記載の初期値、選択肢、画素演算の詳細は推測で補わず、[実機確認項目](massigra-spec.md#25-実機で確認する必要がある細部)に分けた。

## 公式ページと整理先

| No | 公式ページ | 整理先 | 主な内容 |
| ---: | --- | --- | --- |
| 1 | [ソフト紹介 / 動作環境](http://www.massigra.net/massigrahelp/currentver/about.htm) | [全体仕様](massigra-spec.md) | 製品概要と標準対応 |
| 2 | [基本的な使い方](http://www.massigra.net/massigrahelp/currentver/basic.htm) | [全体仕様](massigra-spec.md) | 画面と基本操作 |
| 3 | [コマンドラインによる起動オプション](http://www.massigra.net/massigrahelp/currentver/cmndline.htm) | [全体仕様](massigra-spec.md) | 起動オプション |
| 4 | [カスタマイズで割り当てられる機能](http://www.massigra.net/massigrahelp/currentver/custom.htm) | [設定](massigra-settings.md) | メニュー外の割り当て機能 |
| 5 | [関連付け (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_assoc.htm) | [全体仕様](massigra-spec.md) | 関連付けとアンインストール |
| 6 | [自動スライド (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_autonext.htm) | [全体仕様](massigra-spec.md) | 自動スライド |
| 7 | [ガウスぼかし (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_blur_gau.htm) | [画像加工](massigra-editing.md) | シャープとぼかし |
| 8 | [明るさ調整 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_bright.htm) | [画像加工](massigra-editing.md) | 明るさ |
| 9 | [減色/増色 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_coldepth.htm) | [画像加工](massigra-editing.md) | 減色と増色 |
| 10 | [CPマスク (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_cpmask.htm) | [画像加工](massigra-editing.md) | CPマスク |
| 11 | [ファイルを移動 / コピー (基本) (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_cpymov.htm) | [全体仕様](massigra-spec.md) | ファイルの振り分け |
| 12 | [ファイルを移動 / コピー (10 個 登録) (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_cpymovex.htm) | [全体仕様](massigra-spec.md) | ファイルの振り分け |
| 13 | [画像の情報 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_imginfo.htm) | [全体仕様](massigra-spec.md) | 画像情報 |
| 14 | [画像サイズ (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_imgsize.htm) | [画像加工](massigra-editing.md) | 画像サイズ |
| 15 | [ジャンプ (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_jump.htm) | [全体仕様](massigra-spec.md) | 移動と終端 |
| 16 | [キー設定 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_keycstm.htm) | [設定](massigra-settings.md) | キー設定と割り当て |
| 17 | [キー割り当て (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_keyset.htm) | [設定](massigra-settings.md) | キー設定と割り当て |
| 18 | [レベル調整 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_level.htm) | [画像加工](massigra-editing.md) | レベル |
| 19 | [マウス設定 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_mous.htm) | [設定](massigra-settings.md) | マウス設定 |
| 20 | [カスタマイズ (「マウス設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_mous_cs.htm) | [設定](massigra-settings.md) | マウス設定 |
| 21 | [オプション (「マウス設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_mous_op.htm) | [設定](massigra-settings.md) | マウス設定 |
| 22 | [新規作成 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_newimg.htm) | [画像加工](massigra-editing.md) | 新規画像 |
| 23 | [次へ / 前へ (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_nextprev.htm) | [全体仕様](massigra-spec.md) | 移動と終端 |
| 24 | [ファイルを開く (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_open.htm) | [全体仕様](massigra-spec.md) | 画像を開く |
| 25 | [他のアプリの設定 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_otherapp.htm) | [設定](massigra-settings.md) | 外部アプリ設定 |
| 26 | [パレット編集 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_palette.htm) | [画像加工](massigra-editing.md) | パレット |
| 27 | [プラグイン設定 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_plugset.htm) | [設定](massigra-settings.md) | プラグインの設定と互換性 |
| 28 | [印刷 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_print.htm) | [全体仕様](massigra-spec.md) | 印刷 |
| 29 | [設定 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop.htm) | [設定](massigra-settings.md) | 一般設定11ページ |
| 30 | [バー (「設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop_bar.htm) | [設定](massigra-settings.md) | バー |
| 31 | [背景 (「設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop_bg.htm) | [設定](massigra-settings.md) | 背景 |
| 32 | [ファイル読み込み (「設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop_fload.htm) | [設定](massigra-settings.md) | ファイル読み込み |
| 33 | [ファイル操作 (「設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop_fope.htm) | [設定](massigra-settings.md) | ファイル操作 |
| 34 | [全画面表示 (「設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop_full.htm) | [設定](massigra-settings.md) | 全画面表示 |
| 35 | [全般 (「設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop_gene.htm) | [設定](massigra-settings.md) | 全般 |
| 36 | [マスク (「設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop_mask.htm) | [設定](massigra-settings.md) | マスク設定 |
| 37 | [移動 (「設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop_move.htm) | [設定](massigra-settings.md) | 移動 |
| 38 | [表示 (「設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop_view.htm) | [設定](massigra-settings.md) | 表示 |
| 39 | [ウィンドウ (「設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop_wnd.htm) | [設定](massigra-settings.md) | ウィンドウ |
| 40 | [ズーム (「設定」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_prop_zoom.htm) | [設定](massigra-settings.md) | ズーム |
| 41 | [虹色マスク (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_rainbow.htm) | [画像加工](massigra-editing.md) | 虹色マスク |
| 42 | [ファイル名を変更 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_rename.htm) | [全体仕様](massigra-spec.md) | ファイルの振り分け |
| 43 | [拡大縮小 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_resize.htm) | [画像加工](massigra-editing.md) | 拡大縮小とアルゴリズム |
| 44 | [回転 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_rotate.htm) | [画像加工](massigra-editing.md) | 画像加工としての回転 |
| 45 | [上書き保存 / 名前を付けて保存 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_save.htm) | [全体仕様](massigra-spec.md) | 保存と形式別設定 |
| 46 | [BMP 保存設定 (「保存」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_save_bmp.htm) | [全体仕様](massigra-spec.md) | 保存と形式別設定 |
| 47 | [GIF 保存設定 (「保存」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_save_gif.htm) | [全体仕様](massigra-spec.md) | 保存と形式別設定 |
| 48 | [JPEG 保存設定 (「保存」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_save_jpeg.htm) | [全体仕様](massigra-spec.md) | 保存と形式別設定 |
| 49 | [PNG 保存設定 (「保存」ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_save_png.htm) | [全体仕様](massigra-spec.md) | 保存と形式別設定 |
| 50 | [選択範囲 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_selection.htm) | [画像加工](massigra-editing.md) | 選択範囲 |
| 51 | [選択範囲の単位 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_selstep.htm) | [画像加工](massigra-editing.md) | 選択範囲の単位 |
| 52 | [色合い調整 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_tune_hsl.htm) | [画像加工](massigra-editing.md) | 色合い |
| 53 | [アンシャープマスク (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_unshmask.htm) | [画像加工](massigra-editing.md) | シャープとぼかし |
| 54 | [バージョン情報 (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_version.htm) | [全体仕様](massigra-spec.md) | 設定保存先とインストール |
| 55 | [ズーム (ダイアログ)](http://www.massigra.net/massigrahelp/currentver/d_zoom.htm) | [全体仕様](massigra-spec.md) | 表示倍率 |
| 56 | [ダイアログ](http://www.massigra.net/massigrahelp/currentver/dialog.htm) | [対応表](massigra-sources.md) | ダイアログ全体への入口 |
| 57 | [画像加工の基本](http://www.massigra.net/massigrahelp/currentver/edit.htm) | [画像加工](massigra-editing.md) | 加工の基本と選択とUndo |
| 58 | [よくある質問](http://www.massigra.net/massigrahelp/currentver/faq.htm) | [全体仕様](massigra-spec.md) | FAQの機能制約と例外を各節へ統合 |
| 59 | [画像ビューワ「MassiGra」公式ヘルプ](http://www.massigra.net/massigrahelp/currentver/help.htm) | [対応表](massigra-sources.md) | ヘルプ全体への入口 |
| 60 | [更新履歴](http://www.massigra.net/massigrahelp/currentver/history.htm) | [全体仕様](massigra-spec.md) | 0.45に至る主要変更を整理 |
| 61 | [インストール / アンインストール](http://www.massigra.net/massigrahelp/currentver/install.htm) | [全体仕様](massigra-spec.md) | 設定保存先とインストール |
| 62 | [キーボード操作](http://www.massigra.net/massigrahelp/currentver/keyboard.htm) | [全体仕様](massigra-spec.md) | キーボード操作 |
| 63 | [使用条件 / 配布条件](http://www.massigra.net/massigrahelp/currentver/license.htm) | [全体仕様](massigra-spec.md) | 公式の使用条件と配布条件 |
| 64 | [ファイル名をクリップボードへ (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_e_clip.htm) | [全体仕様](massigra-spec.md) | 名前とパスのコピー |
| 65 | [選択範囲の単位 (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_e_selstep.htm) | [画像加工](massigra-editing.md) | 選択範囲の単位 |
| 66 | [編集 (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_edit.htm) | [画像加工](massigra-editing.md) | 加工の基本と選択とUndo |
| 67 | [他のアプリで開く (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_f_app.htm) | [設定](massigra-settings.md) | 外部アプリ設定 |
| 68 | [最近開いたフォルダ (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_f_mrud.htm) | [全体仕様](massigra-spec.md) | 最近開いたフォルダ |
| 69 | [もう１つ起動 (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_f_newwnd.htm) | [全体仕様](massigra-spec.md) | 複数起動 |
| 70 | [ファイル (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_file.htm) | [全体仕様](massigra-spec.md) | 開くと保存とファイル整理 |
| 71 | [マスク処理 (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_i_mask.htm) | [画像加工](massigra-editing.md) | マスク |
| 72 | [回転/反転 (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_i_rotate.htm) | [画像加工](massigra-editing.md) | 画像加工としての回転 |
| 73 | [シャープ/ぼかし (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_i_shbl.htm) | [画像加工](massigra-editing.md) | シャープとぼかし |
| 74 | [色の入れ替え (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_i_swapcol.htm) | [画像加工](massigra-editing.md) | RGB交換 |
| 75 | [画像 (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_image.htm) | [画像加工](massigra-editing.md) | 画像加工と色形式の変化 |
| 76 | [ファイルの並び順 (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_l_sort.htm) | [全体仕様](massigra-spec.md) | ファイルの並び順 |
| 77 | [リスト (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_list.htm) | [全体仕様](massigra-spec.md) | 移動と終端 |
| 78 | [壁紙にする (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_o_wall.htm) | [全体仕様](massigra-spec.md) | 壁紙 |
| 79 | [その他 (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_option.htm) | [全体仕様](massigra-spec.md) | その他メニューの設定とOS連携 |
| 80 | [漫画オプション (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_v_cmcopt.htm) | [全体仕様](massigra-spec.md) | 漫画モード |
| 81 | [表示オプション (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_v_disp.htm) | [全体仕様](massigra-spec.md) | 読み込み時の表示オプション |
| 82 | [フィットオプション (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_v_fit.htm) | [全体仕様](massigra-spec.md) | フィット |
| 83 | [回転オプション (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_v_rotate.htm) | [全体仕様](massigra-spec.md) | 表示用の回転 |
| 84 | [ウィンドウ (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_v_wnd.htm) | [全体仕様](massigra-spec.md) | 画面の構成とバー |
| 85 | [表示 (メニュー)](http://www.massigra.net/massigrahelp/currentver/m_view.htm) | [全体仕様](massigra-spec.md) | ズームと全画面と漫画 |
| 86 | [全てのメニューリスト](http://www.massigra.net/massigrahelp/currentver/menulist.htm) | [全メニュー](massigra-menu.md) | 全メニュー階層 |
| 87 | [仕様 / 制限](http://www.massigra.net/massigrahelp/currentver/specifi.htm) | [全体仕様](massigra-spec.md) | 制限と書庫内仕様 |
| 88 | [Susie Plug-in 対応状況](http://www.massigra.net/massigrahelp/currentver/spiinfo.htm) | [設定](massigra-settings.md) | プラグインの設定と互換性 |
| 89 | [サポート / 連絡先](http://www.massigra.net/massigrahelp/currentver/support.htm) | [全体仕様](massigra-spec.md) | サポート方針 |
| 90 | [謝辞 / 参考資料](http://www.massigra.net/massigrahelp/currentver/thanks.htm) | [全体仕様](massigra-spec.md) | 利用技術と参考ソフト |
| 91 | [今後の予定](http://www.massigra.net/massigrahelp/currentver/todo.htm) | [全体仕様](massigra-spec.md) | 搭載済みと予定の区別 |

## 取得資料の記録

[取得記録JSON](massigra-source-manifest.json)には、各ページのURL、タイトル、取得日時、HTMLのSHA-256、整理先を記録した。原HTMLと抽出テキストは作業用キャッシュの`.cache/massigra-research`に保存している。本文中の出典リンクは公式ページへ移動する。

取得対象は公式ヘルプのHTMLページであり、広告、外部サイト、プラグインの配布物、MassiGra本体は対象に含めていない。ナビゲーションや広告を本文と混ぜずに抽出した。
