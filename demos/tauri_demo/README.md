# Tauri Demo - blur-windows

blur-windowsライブラリをTauriアプリケーションから使用するデモです。

## 前提条件

- [Node.js](https://nodejs.org/) (v18以上)
- [Rust](https://www.rust-lang.org/tools/install)
- [Tauri CLI](https://tauri.app/v1/guides/getting-started/prerequisites)

## セットアップ

1. **DLLの配置**:
   ```bash
   # プロジェクトルートから
   cp build/bin/Release/blurwindow.dll demos/tauri_demo/src-tauri/
   ```

2. **依存関係のインストール**:
   ```bash
   cd demos/tauri_demo
   npm install
   ```

3. **開発サーバーの起動**:
   ```bash
   npm run tauri dev
   ```

## 利用可能なAPIコマンド

| コマンド | 説明 |
|----------|------|
| `start_blur` | ブラーウィンドウを開始 |
| `stop_blur` | ブラーウィンドウを停止 |
| `update_blur_parameters` | エフェクト種類・強度・色を変更 |
| `update_noise_parameters` | ノイズ設定を変更 |
| `update_rain_parameters` | 雨エフェクト設定を変更 |
| `get_blur_fps` | 現在のFPSを取得 |

## エフェクトタイプ

| 値 | エフェクト |
|----|------------|
| 0 | Gaussian Blur |
| 1 | Box Blur |
| 2 | Kawase Blur |
| 3 | Radial Blur |
| 4 | Glass Blur |
| 5 | Frosted Glass |
| 6 | Rain Effect |

## FFI宣言 (lib.rs)

`lib.rs`で宣言されている主なC API関数:

```rust
extern "C" {
    fn blur_init(opts: *const BlurSystemOptionsC) -> *mut c_void;
    fn blur_create_window(...) -> *mut c_void;
    fn blur_start(window: *mut c_void) -> i32;
    fn blur_stop(window: *mut c_void) -> i32;
    fn blur_set_effect_type(window: *mut c_void, effect_type: i32) -> i32;
    fn blur_set_strength(window: *mut c_void, strength: f32) -> i32;
    fn blur_set_blur_param(window: *mut c_void, param: f32) -> i32;
    fn blur_set_tint_color(window: *mut c_void, r: f32, g: f32, b: f32, a: f32) -> i32;
    fn blur_set_opacity(window: *mut c_void, opacity: f32) -> i32;
    // ... 詳細は lib.rs を参照
}
```

## 参考

- [C API リファレンス (日本語)](../../docs/C_API_REFERENCE.md)
- [C API Reference (English)](../../docs/C_API_REFERENCE_EN.md)
- [導入ガイド](../../docs/USAGE_GUIDE.md)
