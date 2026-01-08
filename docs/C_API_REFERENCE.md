# C API Reference

`blurwindow.dll` が提供する C 言語互換の API リファレンスです。
Rust, C#, Python などの他言語から FFI (Foreign Function Interface) を通じて利用することを想定しています。

## 型定義

### ハンドル
- `BlurSystemHandle`: ライブラリ全体のライフサイクルを管理するシステムハンドル。 (`void*`)
- `BlurWindowHandle`: 個別のブラーウィンドウを管理するウィンドウハンドル。 (`void*`)

### 列挙型

#### `BlurQualityPreset`
品質プリセット。
| 値 | 名前 | 説明 |
|---|---|---|
| 0 | `BLUR_PRESET_HIGH` | 高品質、マルチパス描画 |
| 1 | `BLUR_PRESET_BALANCED` | バランス重視 |
| 2 | `BLUR_PRESET_PERFORMANCE` | パフォーマンス重視 |
| 3 | `BLUR_PRESET_MINIMAL` | 最小負荷 |

#### `BlurErrorCode`
API関数の戻り値。
| 値 | 名前 | 説明 |
|---|---|---|
| 0 | `BLUR_OK` | 成功 |
| -1 | `BLUR_ERROR_NOT_INITIALIZED` | システム未初期化 |
| -2 | `BLUR_ERROR_INVALID_HANDLE` | ハンドルが無効 |
| -3 | `BLUR_ERROR_INVALID_PARAMETER` | パラメータが無効 |
| -4 | `BLUR_ERROR_D3D11_FAILED` | Direct3D 11 操作失敗 |
| -5 | `BLUR_ERROR_CAPTURE_FAILED` | デスクトップキャプチャ失敗 |
| -99 | `BLUR_ERROR_UNKNOWN` | 不明なエラー |

#### `BlurCaptureMethod`
キャプチャ方式の選択。
| 値 | 名前 | 説明 |
|---|---|---|
| 0 | `BLUR_CAPTURE_AUTO` | 自動選択 (WGC優先、非対応環境ではDXGI) |
| 1 | `BLUR_CAPTURE_DXGI` | Desktop Duplication API (Windows 8+) |
| 2 | `BLUR_CAPTURE_WGC` | Windows Graphics Capture (Windows 10 1803+、クロスGPU対応) |

### 構造体
#### `BlurRect`
ウィンドウの範囲を指定します。
- `int32_t left`, `top`, `right`, `bottom`

#### `BlurSystemOptionsC`
システム初期化時のオプション。
| フィールド | 型 | 説明 |
|---|---|---|
| `enableLogging` | `int32_t` | ログ出力の有効化 (0: 無効, 1: 有効) |
| `logPath` | `const char*` | ログファイルのパス (NULL でコンソール出力) |
| `defaultPreset` | `BlurQualityPreset` | デフォルトの品質プリセット |
| `captureMethod` | `BlurCaptureMethod` | デフォルトのキャプチャ方式 |

#### `BlurWindowOptionsC`
ウィンドウ作成時のオプション。
| フィールド | 型 | 説明 |
|---|---|---|
| `owner` | `void*` | 親ウィンドウの HWND (NULL で独立ウィンドウ) |
| `bounds` | `BlurRect` | 初期位置とサイズ |
| `topMost` | `int32_t` | 常に最前面に表示 (0: 無効, 1: 有効) |
| `clickThrough` | `int32_t` | クリックを背面に透過 (0: 無効, 1: 有効) |
| `captureMethod` | `BlurCaptureMethod` | キャプチャ方式 (0=Auto) |

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

> [!IMPORTANT]
> `blur_start` を呼び出すと、ウィンドウの `bounds` に基づいてターゲットモニターが自動的に決定され、そのモニター専用のキャプチャセッションが固定されます。
> これにより、複数のブラーウィンドウが同時に存在しても、キャプチャセッションの競合による点滅や映像混入が発生しません。
> 
> ターゲットモニターは `bounds` の中心座標から決定されます。ウィンドウの位置を変更しても、キャプチャ対象のモニターは変更されません。

### `blur_set_effect_type`
```c
BLURWINDOW_API BlurErrorCode blur_set_effect_type(BlurWindowHandle window, int32_t type);
```
実行するブラーの種類を設定します。
- `0`: Gaussian
- `1`: Kawase
- `2`: Box
- `3`: Radial
- `4`: Rain (雨粒エフェクト)
- `5`: Glass (シンプルなすりガラス)
- `6`: FrostedGlass (Voronoi歪み)

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

### `blur_set_opacity`
```c
BLURWINDOW_API BlurErrorCode blur_set_opacity(BlurWindowHandle window, float opacity);
```
ブラーエフェクト全体の透明度 (0.0 = 完全透明, 1.0 = 完全不透明) を設定します。

---

## キャプチャ方式制御

### `blur_set_capture_method`
```c
BLURWINDOW_API BlurErrorCode blur_set_capture_method(BlurWindowHandle window, BlurCaptureMethod method);
```
キャプチャ方式を変更します。

| 方式 | 説明 |
|---|---|
| `BLUR_CAPTURE_AUTO` | WGCが利用可能な場合はWGC、そうでなければDXGIを使用 |
| `BLUR_CAPTURE_DXGI` | Desktop Duplication API。同一GPU上のモニターのみ対応 |
| `BLUR_CAPTURE_WGC` | Windows Graphics Capture。クロスGPUキャプチャに対応 |

> [!NOTE]
> キャプチャ方式を変更すると、キャプチャセッションが再起動されます。

---

## ノイズ制御

### `blur_set_noise_intensity`
```c
BLURWINDOW_API BlurErrorCode blur_set_noise_intensity(BlurWindowHandle window, float intensity);
```
ノイズの強度を設定します。

### `blur_set_noise_scale`
```c
BLURWINDOW_API BlurErrorCode blur_set_noise_scale(BlurWindowHandle window, float scale);
```
ノイズの空間スケールを設定します (1.0 ～ 1000.0)。

### `blur_set_noise_speed`
```c
BLURWINDOW_API BlurErrorCode blur_set_noise_speed(BlurWindowHandle window, float speed);
```
ノイズのアニメーション速度を設定します (0 で静璐)。

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

## Rain Effect Control

> [!IMPORTANT]
> Rainエフェクトを使用する場合、`blur_start`後に`blur_set_effect_type(window, 4)`を呼び出すか、
> `blur_set_rain_intensity`などのRain API関数を呼び出してください（自動的にRainエフェクトに切り替わります）。

### `blur_set_rain_intensity`
```c
BLURWINDOW_API BlurErrorCode blur_set_rain_intensity(BlurWindowHandle window, float intensity);
```
雨の密度を設定します。
- `intensity`: 0.0 (雨なし) ～ 1.0 (大雨)

### `blur_set_rain_drop_speed`
```c
BLURWINDOW_API BlurErrorCode blur_set_rain_drop_speed(BlurWindowHandle window, float speed);
```
雨粒の落下速度を設定します。
- `speed`: 0.1 (遅い) ～ 5.0 (速い)

### `blur_set_rain_refraction`
```c
BLURWINDOW_API BlurErrorCode blur_set_rain_refraction(BlurWindowHandle window, float strength);
```
雨粒の屈折強度を設定します。
- `strength`: 0.0 (屈折なし) ～ 1.0 (最大屈折)

### `blur_set_rain_trail_length`
```c
BLURWINDOW_API BlurErrorCode blur_set_rain_trail_length(BlurWindowHandle window, float length);
```
落下中の雨粒の軌跡の長さを設定します。
- `length`: 0.0 (軌跡なし) ～ 1.0 (長い軌跡)

### `blur_set_rain_drop_size`
```c
BLURWINDOW_API BlurErrorCode blur_set_rain_drop_size(BlurWindowHandle window, float minSize, float maxSize);
```
雨粒のサイズ範囲を設定します。
- `minSize`: 最小半径 (ピクセル)
- `maxSize`: 最大半径 (ピクセル)

---

## Click Callback

ブラーウィンドウがクリックされたときにコールバック関数を呼び出す機能です。

> [!NOTE]
> クリックコールバックは `clickThrough: 0` の場合のみ動作します。
> `clickThrough: 1` の場合、クリックは背面のウィンドウに透過されます。

### `BlurClickCallback` (型定義)
```c
typedef void (*BlurClickCallback)(BlurWindowHandle window, int32_t x, int32_t y, void* userData);
```
- `window`: クリックされたウィンドウのハンドル
- `x`, `y`: クリック位置（スクリーン座標）
- `userData`: `blur_set_click_callback` で渡したユーザーデータ

### `blur_set_click_callback`
```c
BLURWINDOW_API BlurErrorCode blur_set_click_callback(
    BlurWindowHandle window,
    BlurClickCallback callback,
    void* userData
);
```
クリックイベントのコールバックを設定します。

**使用例 (C)**:
```c
void on_click(BlurWindowHandle window, int32_t x, int32_t y, void* userData) {
    printf("Clicked at (%d, %d)\n", x, y);
    blur_stop(window);
    blur_destroy_window(window);
}

// 登録
blur_set_click_callback(window, on_click, NULL);
```

---

## ユーティリティ

### `blur_get_hwnd`
```c
BLURWINDOW_API void* blur_get_hwnd(BlurWindowHandle window);
```
ブラーウィンドウのネイティブウィンドウハンドル (HWND) を取得します。
Zオーダー制御など、Win32 API を直接操作する場合に使用します。

**戻り値**: ネイティブ HWND、無効な場合は NULL

### `blur_get_fps`
```c
BLURWINDOW_API float blur_get_fps(BlurWindowHandle window);
```
現在のレンダリング FPS を取得します。

### `blur_get_last_error`
```c
BLURWINDOW_API const char* blur_get_last_error(void);
```
最後に発生したエラーのメッセージを取得します。

**戻り値**: エラーメッセージ文字列（静的メモリ、解放不要）

### `blur_enable_logging`
```c
BLURWINDOW_API void blur_enable_logging(BlurSystemHandle sys, int32_t enable, const char* path);
```
ログ出力を動的に有効/無効にします。
- `enable`: 1 で有効、0 で無効
- `path`: ログファイルのパス (NULL でコンソール出力)

