# tpad_utf8 (WIP)

Windows 向けテキストエディタ **TeraPad**（32bit ANSI版）に対し、実行ファイル（`TeraPad.exe`）本体を改変せずに、絵文字・サロゲートペア・JIS第3/第4水準漢字・多言語文字を含む **UTF-8 の表示および入出力** を試みる実験的なプロキシ DLL パッチ（`version.dll`）です。

> [!WARNING]
> **現在開発中（実験段階）です。** 大切なファイルの編集には必ずバックアップを取った上でご使用ください。
> 本リポジトリには `TeraPad.exe` 本体は含まれません。TeraPad 本体は公式サイト等から別途入手してください。

## 仕組み

TeraPad は内部処理が ANSI（Shift_JIS）ベースであるため、通常は UTF-8 ファイルを開く際に Shift_JIS へ変換され、表現できない文字が `?` や `$[xxxx]` に置換されます。

本パッチは `TeraPad.exe` と同じフォルダに `version.dll` を配置することで起動時に自動ロードされ、IAT フックによって以下の処理を横取りしています。

1. **文字変換フック（`MultiByteToWideChar` / `WideCharToMultiByte`）**
   UTF-8 ファイルの読み込み時に内部の Shift_JIS 変換をバイパスして生の UTF-8 バイト列を保持し、保存時も UTF-8 として変換します。
2. **描画フック（`ExtTextOutA` / `GetTextExtentPoint32A` / `DrawTextA`）**
   有効な UTF-8 文字列を UTF-16 へ変換し、`ExtTextOutW` 等の Unicode 版 GDI API で描画します。
3. **入力・クリップボードフック（`GetClipboardData` / `SetClipboardData` / `ImmGetCompositionStringA` / `GetWindowTextA` / `SetWindowTextA`）**
   `CF_UNICODETEXT` や `ImmGetCompositionStringW` を介して UTF-8 との橋渡しを試みます。

## 使い方

1. ビルドして生成された `dist\version.dll`（または Releases の `version.dll`）を、`TeraPad.exe` と同じフォルダにコピーします。
2. 通常通り `TeraPad.exe` を起動します。

アンインストールしたい場合は、`version.dll` を削除してください。

## ビルド方法

Windows 上で 32bit (i386) コードを生成できる MinGW-w64 (`gcc -m32`, `ld`, `dlltool`) がインストールされている環境で実行してください。

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

ビルドが成功すると `dist\version.dll` が生成されます。

## 課題・既知の制限事項

* **日本語・絵文字の文字間隔が広く表示される**
  TeraPad 本体の `TEditor` 内部では「1バイト ＝ 半角1文字幅（例：8px）」でカーソル位置や横幅配列を計算しているため、UTF-8 の 3バイト文字（日本語など）は 24px、4バイト文字（絵文字など）は 32px 幅として描画されます。これを通常の全角幅（16px）に詰めるとカーソル位置がズレてしまうため、現状はこの仕様としています。
* **BackSpace / Delete / 左右矢印キーの追従はヒューリスティック**
  `TEditor` 本体のバイナリを書き換える代わりに、描画結果やキャレット位置を監視して追加のキーメッセージを送ることで 1文字単位の削除・移動を行っています。状況によっては 1フレームだけ置換文字（``）がちらついたり、特殊な操作（矩形選択や折り返し境界など）で意図通り動かない可能性があります。
* **エンコード状態の切り替え・新規作成時の挙動**
  UTF-8 ファイルを開いた後に Shift_JIS 等の別ファイルを開き直した場合や、新規作成から編集を始めた場合のエンコード状態の切り替わりについては検証が不十分です。
* **未検証の機能が多い**
  検索・置換、印刷、外部ツール連携、各種強調表示（パーサー）など、すべての機能での動作検証はまだ行えていません。
* **対象バージョン**
  TeraPad Ver.1.2.9（32bit）でのみ確認しています。
* **セキュリティソフトの誤検知**
  未署名のプロキシ DLL（`version.dll`）という性質上、環境によってはウイルス対策ソフト等に検知される場合があります。気になる場合はソースコードを確認の上、ご自身でビルドしてください。

## ライセンス

MIT License（[LICENSE](LICENSE) を参照）
