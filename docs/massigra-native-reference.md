# MassiGra 0.45の設定リソース照合

公式ヘルプ本文で省略されている選択肢を、[公式ページ](http://www.massigra.net/)が案内する配布ZIP内のWindowsダイアログリソースから照合した。このリソース抽出では調査用本体を起動しない。0.9.0では、別の専用プローブで隔離した子プロセスの補間出力も取得する。いずれも参照本体はキャッシュにだけ置き、GVNへ配布しない。

- ZIP: `MassiGra045.zip`、1,300,913 bytes。
- SHA-256: `76f28cf65e900778ae0f67d41ea324d35d0ec162624dcf50ada3e44ce831ca25`。
- 公式の予備配布リンク: [MassiGra045.zip](https://www.dropbox.com/s/vhd2xni16qfcder/MassiGra045.zip)。
- 抽出方法: Win32の`LoadLibraryExW`をDATAFILE/IMAGE_RESOURCE指定で使い、DLGTEMPLATE/DLGTEMPLATEEXを読み取る。本体の起動処理やDllMainを呼ばない。
- 再現ツール: `tools/inspect_massigra_reference.py ZIP 出力フォルダ`。
- 本体ハッシュ・コントロールID・選択肢: [抽出記録](massigra-native-reference.json)。

| リソース | コントロール | 確認した選択肢 |
| --- | --- | --- |
| DLG_PROP_04 | 2083 | 上 - 左、上 - 中、上 - 右、中 - 左、中 - 中、中 - 右、下 - 左、下 - 中、下 - 右 |
| DLG_PROP_04 | 2084 | 透明、塗り潰す、影を表示 |
| DLG_PROP_10 | 2081 | フルパス、フォルダ名以降、ファイル名 |
| DLG_PROP_04 | 2081 | Simple、Linear、Integra、Hermite、Bell、B-Spline、Mitchell、Cubic-0.5、Lan2+0.25、Lan2+0.50、Lanczos3 |

補間方式は11選択肢と確認できた。過去のGVN文書にあった「元の10方式」は、この照合に従って訂正する。0.8.0までのGVNの5方式を、この11方式と同一には扱わない。0.9.0の追加実装と参照出力の比較は[表示用補間の契約](gvn-display-filters.md)を参照。

[公式変更履歴](http://www.massigra.net/massigrahelp/currentver/history.htm)には、重ねるファイル名が収まらない場合は末尾側を表示する変更が記されている。GVNもこの条件を実装・検証する。リソースから確認できるのは選択肢と画面構成であり、既定値や各処理の画素完全一致を証明するものではない。
