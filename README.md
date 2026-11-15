# tpad_utf8

Windows 向け軽量テキストエディタ **TeraPad**（32bit ANSI版）に対し、実行ファイル（`TeraPad.exe`）本体を改変することなく、絵文字・サロゲートペア・JIS第3/第4水準漢字・多言語文字を含む **UTF-8 の入出力および描画** を行えるようにするプロキシ DLL パッチ（`version.dll`）です。

> [!NOTE]
> 本リポジトリには `TeraPad.exe` 本体は含まれません。TeraPad 本体は公式サイト等から別途入手してください。

## 特徴と仕組み

TeraPad は内部処理が ANSI（Shift_JIS）ベースであるため、通常は UTF-8 ファイルを開く際に Shift_JIS へ変換され、表現できない文字が `?` や `$[xxxx]` に置換されます。

本パッチは `TeraPad.exe` と同じフォルダに `version.dll` を配置することで起動時に自動ロードされ、IAT（Import Address Table）フックによって以下を行います。

1. **文字変換フック（`MultiByteToWideChar` / `WideCharToMultiByte`）**
   UTF-8 ファイルの読み込みを検知すると、内部の Shift_JIS 変換をバイパスして生の UTF-8 バイト列を保持し、保存時も UTF-8 として往復させます。
2. **描画フック（`ExtTextOutA` / `GetTextExtentPoint32A` / `DrawTextA`）**
   有効な UTF-8 文字列を検知すると UTF-16 へ変換し、`ExtTextOutW` 等の Unicode 版 GDI API で描画します（UI メニュー等の Shift_JIS 文字列は元の A 版にフォールバックします）。
3. **入力・クリップボードフック（`GetClipboardData` / `SetClipboardData` / `ImmGetCompositionStringA`）**
   `CF_UNICODETEXT` および `ImmGetCompositionStringW` を介して UTF-8 へ変換することで、絵文字や環境依存文字のコピー＆貼り付け・IME 入力に対応します。

## 使い方

1. 本プロジェクトをビルドして生成された `dist\version.dll` を、`TeraPad.exe` と同じフォルダにコピーします。
2. 通常通り `TeraPad.exe` を起動します。

アンインストールしたい場合は、`version.dll` を削除するだけです。

## ビルド方法

Windows 上で 32bit (i386) コードを生成できる MinGW-w64 (`gcc -m32`, `ld`, `dlltool`) がインストールされている環境で実行してください。

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

ビルドが成功すると `dist\version.dll` が生成されます。

## 既知の制限事項

* **カーソル・文字幅の計算:** TeraPad 本体の `TEditor` 内部では Shift_JIS（1〜2バイト）前提でカラム計算が行われているため、3〜4 バイト文字（日本語・絵文字など）が混在すると文字単位の横幅（`dx`）が広めになる場合があります。BackSpace / Delete / 左右矢印キーによる移動・削除はフック側で UTF-8 文字境界へ自動追従します。
* **対象バージョン:** TeraPad Ver.1.2.9（32bit）で動作確認を行っています。

## ライセンス

MIT License（[LICENSE](LICENSE) を参照）
