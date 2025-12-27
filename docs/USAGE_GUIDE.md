# Usage Guide (導入ガイド)

`blurwindow` ライブラリを自身のプロジェクトに導入するためのガイドです。

## 必要なファイル一覧

リリースパッケージ（ZIP）には以下のファイルが含まれています。

| ファイル / フォルダ | 役割 | 開発時に必要 | 実行時に必要 |
| :--- | :--- | :---: | :---: |
| `bin/blurwindow.dll` | ライブラリ本体（プログラムの実体） | - | **必須** |
| `lib/blurwindow.lib` | インポートライブラリ（リンク用） | **必須** (C++/Rust) | - |
| `include/blurwindow/*.h` | 公開ヘッダーファイル（API定義） | **必須** | - |

---

## 開発環境ごとの設定方法

### 1. C++ (Visual Studio / MSBuild)

1. **インクルードパスの設定**:
   - プロジェクトのプロパティ > C/C++ > 全般 > 追加のインクルードディレクトリ に、`include` フォルダのパスを追加します。
2. **ライブラリパスの設定**:
   - プロジェクトのプロパティ > リンカー > 全般 > 追加のライブラリディレクトリ に、`lib` フォルダのパスを追加します。
3. **ライブラリの指定**:
   - プロジェクトのプロパティ > リンカー > 入力 > 追加の依存ファイル に `blurwindow.lib` を追加します（またはコード内で `#pragma comment(lib, "blurwindow.lib")` を記述）。
4. **実行準備**:
   - ビルドされた `.exe` と同じフォルダに `blurwindow.dll` をコピーします。

### 2. Rust / Tauri

1. **ファイルの配置**:
   - プロジェクトルートに `libs` フォルダを作成し、そこに `blurwindow.lib` と `blurwindow.dll` をコピーします。
2. **build.rs の作成/更新**:
   ```rust
   fn main() {
       println!("cargo:rustc-link-search=native=libs");
       println!("cargo:rustc-link-lib=dylib=blurwindow");
   }
   ```
3. **API 定義**:
   - `include/blurwindow/c_api.h` を参考に、`extern "C"` ブロックで関数を宣言します。
4. **実行準備 (Tauri)**:
   - `tauri.conf.json` の `bundle > resources` に `libs/blurwindow.dll` を追加し、開発時は `src-tauri` 直下またはバイナリと同じ場所に DLL を配置します。

---

## トラブルシューティング

### `DLL が見つかりません` というエラーが出る
- 実行ファイル (`.exe`) と同じディレクトリに `blurwindow.dll` が存在するか確認してください。
- 64bit (x64) 向けにビルドされているか確認してください（本ライブラリは x64 専用です）。

### シンボル未解決 (LNK2019) エラーが出る
- `blurwindow.lib` が正しくリンクされているか、ライブラリパスが通っているか確認してください。
- C++ から C API を使用する場合は、`extern "C"` で囲われているか確認してください（`c_api.h` は自動で対応しています）。
