# C API Reference

`blurwindow.dll` が提供する C 言語互換の API リファレンスです。
Rust, C#, Python などの他言語から FFI (Foreign Function Interface) を通じて利用することを想定しています。

## 型定義

### ハンドル
- `BlurSystemHandle`: ライブラリ全体のライフサイクルを管理するシステムハンドル。 (`void*`)
- `BlurWindowHandle`: 個別のブラーウィンドウを管理するウィンドウハンドル。 (`void*`)

### 構造体
#### `BlurRect`
ウィンドウの範囲を指定します。
- `int32_t left`, `top`, `right`, `bottom`

#### `BlurSystemOptionsC`
システム初期化時のオプション。
- `int32_t enableLogging`: ログ出力の有効化 (0: 無効, 1: 有効)
- `const char* logPath`: ログファイルのパス (NULL でコンソール出力)
- `int32_t defaultPreset`: デフォルトの品質プリセット (0: High, 1: Balanced, 2: Performance, 3: Minimal)

#### `BlurWindowOptionsC`
ウィンドウ作成時のオプション。
- `void* owner`: 親ウィンドウの HWND (NULL で独立ウィンドウ)
- `BlurRect bounds`: 初期位置とサイズ
- `int32_t topMost`: 常に最前面に表示 (0: 無効, 1: 有効)
- `int32_t clickThrough`: クリックを背面に透過 (0: 無効, 1: 有効)

---

## 基本機能

### `blur_init`
```c
BLURWINDOW_API BlurSystemHandle blur_init(const BlurSystemOptionsC* opts);
```
ライブラリを初期化します。最初に一度呼び出す必要があります。

### `blur_shutdown`
```c
BLURWINDOW_API void blur_shutdown(BlurSystemHandle sys);
```
ライブラリをシャットダウンし、リソースを解放します。

### `blur_create_window`
```c
BLURWINDOW_API BlurWindowHandle blur_create_window(BlurSystemHandle sys, void* owner, const BlurWindowOptionsC* opts);
```
新しいブラーウィンドウを作成します。

### `blur_destroy_window`
```c
BLURWINDOW_API void blur_destroy_window(BlurWindowHandle window);
```
ブラーウィンドウを破棄します。

---

## 制御機能

### `blur_start` / `blur_stop`
```c
BLURWINDOW_API BlurErrorCode blur_start(BlurWindowHandle window);
BLURWINDOW_API BlurErrorCode blur_stop(BlurWindowHandle window);
```
ブラーエフェクトの描画を開始/停止します。

### `blur_set_effect_type`
```c
BLURWINDOW_API BlurErrorCode blur_set_effect_type(BlurWindowHandle window, int32_t type);
```
実行するブラーの種類を設定します。
- `0`: Gaussian
- `1`: Kawase
- `2`: Box
- `3`: Radial

### `blur_set_strength`
```c
BLURWINDOW_API BlurErrorCode blur_set_strength(BlurWindowHandle window, float strength);
```
ブラーの最終的な合成強度 (0.0 ～ 1.0) を設定します。

### `blur_set_blur_param`
```c
BLURWINDOW_API BlurErrorCode blur_set_blur_param(BlurWindowHandle window, float param);
```
エフェクト固有のパラメータを設定します。
- **Gaussian**: Sigma 値
- **Box**: 半径 (Radius)
- **Kawase**: 反復回数 (Iterations)
- **Radial**: ブラー量 (Amount)

### `blur_set_tint_color`
```c
BLURWINDOW_API BlurErrorCode blur_set_tint_color(BlurWindowHandle window, float r, float g, float b, float a);
```
ブラーにかける色 (RGBA, 各 0.0 ～ 1.0) を設定します。

---

## ノイズ制御

### `blur_set_noise_intensity`
```c
BLURWINDOW_API BlurErrorCode blur_set_noise_intensity(BlurWindowHandle window, float intensity);
```
ノイズの強度を設定します。

### `blur_set_noise_type`
```c
BLURWINDOW_API BlurErrorCode blur_set_noise_type(BlurWindowHandle window, int32_t type);
```
ノイズの種類を設定します。
- `0`: White Noise
- `1`: Sinusoid
- `2`: Grid
- `3`: Perlin
- `4`: Simplex
- `5`: Voronoi

---

## ユーティリティ

### `blur_get_fps`
```c
BLURWINDOW_API float blur_get_fps(BlurWindowHandle window);
```
現在のレンダリング FPS を取得します。
