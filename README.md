# GVN Image Viewer

Windows 10/11 x64用の軽量な画像ビューアです。C++20・Win32・Direct2Dで実装しています。

## 使い始める

配布ZIP `GVN-0.77.0.zip` をフォルダーごと展開し、`GVN.exe`を起動してください。インストーラー、Python、開発ツール、追加プラグインは不要です。

画像をウィンドウへドロップするか、Ctrl+Oで開けます。操作説明はF1またはツールバーの「？」で開きます。[操作説明を読む](docs/gvn-manual.md)。

配布物は実行ファイル・操作説明・アイコン・チェックサム・第三者ライセンスだけです。ソースコード、開発資料、テストデータ、検証画像、個人設定は配布ZIPに含めません。ZIP全体を展開して使用してください。

## 主な機能

0.77.0では高画質表示の非同期化を拡大し、古い読み込みの途中取消と大画像コピーの非同期処理を追加しました。[操作応答の改善](docs/gvn-interactive-hotpaths.md)。

- PNG・JPEG・BMP・TIFF・ICO・PSDの表示、GIF・APNGのアニメーション再生、ZIP内画像の閲覧。
- ホイールでズーム、画像送り、手のひら移動、全画面、スライドショー。
- Alphaを保持するコピー・貼り付け、画像ファイルとしてのドラッグ出力。
- 選択範囲、移動・自由変形、トリミング、ペン、文字入れ、色調整、Undo/Redo。
- ライト／ダークテーマ、キー・マウス割り当て、メニューのカスタマイズ。

PSDは保存済みの合成画像を表示します。対応する色モードなどの制限は[操作説明](docs/gvn-manual.md)を参照してください。MassiGraを参考に開発していますが、完全互換ではありません。

設定は`%LOCALAPPDATA%\GVN\settings.ini`に保存します。利用者ごとの設定を配布物へ同梱しません。

## 開発・ビルド

Visual Studio Build Tools 2022の「C++によるデスクトップ開発」、CMake、Pythonを使用します。依存ライブラリのソースは`third_party`へ固定リビジョンで同梱しています。

```powershell
powershell -NoProfile -File tools/build.ps1
powershell -NoProfile -File tools/package.ps1 -BuildDirectory ./build -SkipDailyUpdate
```

配布ZIPは`dist/GVN-<バージョン>.zip`に生成します。`-SkipDailyUpdate`を省略すると開発環境の普段使い用フォルダーも更新します。ビルド・検証用のソースや資料はGitHubリポジトリ側に保持し、利用者向けのZIPから除外します。

- [配布物の構成と検証](docs/gvn-distribution.md)
- [変更履歴・以前の開発記録](docs/gvn-release-history.md)
- [機能対応表](docs/gvn-feature-matrix.md)・[作業リスト](docs/gvn-worklist.md)
- [第三者ライブラリの使用版](third_party/manifest.json)
