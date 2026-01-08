# C API Reference

This is the C-compatible API reference provided by `blurwindow.dll`.
It is designed to be used from other languages such as Rust, C#, and Python via FFI (Foreign Function Interface).

## Type Definitions

### Handles
- `BlurSystemHandle`: System handle managing the library's lifecycle. (`void*`)
- `BlurWindowHandle`: Window handle managing individual blur windows. (`void*`)

### Enumerations

#### `BlurQualityPreset`
Quality presets.
| Value | Name | Description |
|---|---|---|
| 0 | `BLUR_PRESET_HIGH` | High quality, multi-pass rendering |
| 1 | `BLUR_PRESET_BALANCED` | Balanced quality and performance |
| 2 | `BLUR_PRESET_PERFORMANCE` | Performance-oriented |
| 3 | `BLUR_PRESET_MINIMAL` | Minimum overhead |

#### `BlurErrorCode`
Return values for API functions.
| Value | Name | Description |
|---|---|---|
| 0 | `BLUR_OK` | Success |
| -1 | `BLUR_ERROR_NOT_INITIALIZED` | System not initialized |
| -2 | `BLUR_ERROR_INVALID_HANDLE` | Invalid handle |
| -3 | `BLUR_ERROR_INVALID_PARAMETER` | Invalid parameter |
| -4 | `BLUR_ERROR_D3D11_FAILED` | Direct3D 11 operation failed |
| -5 | `BLUR_ERROR_CAPTURE_FAILED` | Desktop capture failed |
| -99 | `BLUR_ERROR_UNKNOWN` | Unknown error |

#### `BlurCaptureMethod`
Capture method selection.
| Value | Name | Description |
|---|---|---|
| 0 | `BLUR_CAPTURE_AUTO` | Auto-select (WGC preferred, fallback to DXGI) |
| 1 | `BLUR_CAPTURE_DXGI` | Desktop Duplication API (Windows 8+) |
| 2 | `BLUR_CAPTURE_WGC` | Windows Graphics Capture (Windows 10 1803+, cross-GPU capable) |

### Structures
#### `BlurRect`
Specifies the window bounds.
- `int32_t left`, `top`, `right`, `bottom`

#### `BlurSystemOptionsC`
Options for system initialization.
| Field | Type | Description |
|---|---|---|
| `enableLogging` | `int32_t` | Enable logging output (0: disabled, 1: enabled) |
| `logPath` | `const char*` | Path to log file (NULL for console output) |
| `defaultPreset` | `BlurQualityPreset` | Default quality preset |
| `captureMethod` | `BlurCaptureMethod` | Default capture method |

#### `BlurWindowOptionsC`
Options for window creation.
| Field | Type | Description |
|---|---|---|
| `owner` | `void*` | Parent window HWND (NULL for standalone window) |
| `bounds` | `BlurRect` | Initial position and size |
| `topMost` | `int32_t` | Always on top (0: disabled, 1: enabled) |
| `clickThrough` | `int32_t` | Pass clicks through to background (0: disabled, 1: enabled) |
| `captureMethod` | `BlurCaptureMethod` | Capture method preference (0=Auto) |

---

## Core Functions

### `blur_init`
```c
BLURWINDOW_API BlurSystemHandle blur_init(const BlurSystemOptionsC* opts);
```
Initializes the library. Must be called once before using other functions.

### `blur_shutdown`
```c
BLURWINDOW_API void blur_shutdown(BlurSystemHandle sys);
```
Shuts down the library and releases resources.

### `blur_create_window`
```c
BLURWINDOW_API BlurWindowHandle blur_create_window(BlurSystemHandle sys, void* owner, const BlurWindowOptionsC* opts);
```
Creates a new blur window.

### `blur_destroy_window`
```c
BLURWINDOW_API void blur_destroy_window(BlurWindowHandle window);
```
Destroys a blur window.

---

## Control Functions

### `blur_start` / `blur_stop`
```c
BLURWINDOW_API BlurErrorCode blur_start(BlurWindowHandle window);
BLURWINDOW_API BlurErrorCode blur_stop(BlurWindowHandle window);
```
Starts/stops rendering the blur effect.

> [!IMPORTANT]
> When `blur_start` is called, the target monitor is automatically determined based on the window's `bounds`, and a capture session dedicated to that monitor is locked.
> This ensures that even when multiple blur windows exist simultaneously, there is no flickering or image mixing caused by capture session conflicts.
> 
> The target monitor is determined from the center coordinates of the `bounds`. Even if the window position changes afterward, the captured monitor remains the same.

### `blur_set_effect_type`
```c
BLURWINDOW_API BlurErrorCode blur_set_effect_type(BlurWindowHandle window, int32_t type);
```
Sets the blur effect type.
- `0`: Gaussian
- `1`: Kawase
- `2`: Box
- `3`: Radial
- `4`: Rain (raindrop effect)
- `5`: Glass (simple frosted glass)
- `6`: FrostedGlass (Voronoi distortion)

### `blur_set_strength`
```c
BLURWINDOW_API BlurErrorCode blur_set_strength(BlurWindowHandle window, float strength);
```
Sets the final blend strength of the blur (0.0 to 1.0).

### `blur_set_blur_param`
```c
BLURWINDOW_API BlurErrorCode blur_set_blur_param(BlurWindowHandle window, float param);
```
Sets effect-specific parameters.
- **Gaussian**: Sigma value
- **Box**: Radius
- **Kawase**: Iterations
- **Radial**: Blur amount

### `blur_set_tint_color`
```c
BLURWINDOW_API BlurErrorCode blur_set_tint_color(BlurWindowHandle window, float r, float g, float b, float a);
```
Sets the tint color overlay (RGBA, each 0.0 to 1.0).

### `blur_set_opacity`
```c
BLURWINDOW_API BlurErrorCode blur_set_opacity(BlurWindowHandle window, float opacity);
```
Sets the overall opacity of the blur effect (0.0 = transparent, 1.0 = opaque).

---

## Capture Method Control

### `blur_set_capture_method`
```c
BLURWINDOW_API BlurErrorCode blur_set_capture_method(BlurWindowHandle window, BlurCaptureMethod method);
```
Changes the capture method.

| Method | Description |
|---|---|
| `BLUR_CAPTURE_AUTO` | Uses WGC if available, otherwise falls back to DXGI |
| `BLUR_CAPTURE_DXGI` | Desktop Duplication API. Only supports monitors on the same GPU |
| `BLUR_CAPTURE_WGC` | Windows Graphics Capture. Supports cross-GPU capture |

> [!NOTE]
> Changing the capture method will restart the capture session.

---

## Noise Control

### `blur_set_noise_intensity`
```c
BLURWINDOW_API BlurErrorCode blur_set_noise_intensity(BlurWindowHandle window, float intensity);
```
Sets the noise intensity.

### `blur_set_noise_scale`
```c
BLURWINDOW_API BlurErrorCode blur_set_noise_scale(BlurWindowHandle window, float scale);
```
Sets the spatial scale of the noise pattern (1.0 to 1000.0).

### `blur_set_noise_speed`
```c
BLURWINDOW_API BlurErrorCode blur_set_noise_speed(BlurWindowHandle window, float speed);
```
Sets the noise animation speed (0 for static).

### `blur_set_noise_type`
```c
BLURWINDOW_API BlurErrorCode blur_set_noise_type(BlurWindowHandle window, int32_t type);
```
Sets the noise type.
- `0`: White Noise
- `1`: Sinusoid
- `2`: Grid
- `3`: Perlin
- `4`: Simplex
- `5`: Voronoi

---

## Rain Effect Control

> [!IMPORTANT]
> To use the Rain effect, call `blur_set_effect_type(window, 4)` after `blur_start`,
> or call any Rain API function (which automatically switches to Rain effect).

### `blur_set_rain_intensity`
```c
BLURWINDOW_API BlurErrorCode blur_set_rain_intensity(BlurWindowHandle window, float intensity);
```
Sets the rain density.
- `intensity`: 0.0 (no rain) to 1.0 (heavy rain)

### `blur_set_rain_drop_speed`
```c
BLURWINDOW_API BlurErrorCode blur_set_rain_drop_speed(BlurWindowHandle window, float speed);
```
Sets the raindrop fall speed.
- `speed`: 0.1 (slow) to 5.0 (fast)

### `blur_set_rain_refraction`
```c
BLURWINDOW_API BlurErrorCode blur_set_rain_refraction(BlurWindowHandle window, float strength);
```
Sets the raindrop refraction strength.
- `strength`: 0.0 (no refraction) to 1.0 (maximum refraction)

### `blur_set_rain_trail_length`
```c
BLURWINDOW_API BlurErrorCode blur_set_rain_trail_length(BlurWindowHandle window, float length);
```
Sets the trail length of falling raindrops.
- `length`: 0.0 (no trail) to 1.0 (long trail)

### `blur_set_rain_drop_size`
```c
BLURWINDOW_API BlurErrorCode blur_set_rain_drop_size(BlurWindowHandle window, float minSize, float maxSize);
```
Sets the raindrop size range.
- `minSize`: Minimum radius (pixels)
- `maxSize`: Maximum radius (pixels)

---

## Click Callback

A feature to invoke a callback function when the blur window is clicked.

> [!NOTE]
> Click callbacks only work when `clickThrough: 0`.
> When `clickThrough: 1`, clicks pass through to the window behind.

### `BlurClickCallback` (Type Definition)
```c
typedef void (*BlurClickCallback)(BlurWindowHandle window, int32_t x, int32_t y, void* userData);
```
- `window`: Handle of the clicked window
- `x`, `y`: Click position (screen coordinates)
- `userData`: User data passed to `blur_set_click_callback`

### `blur_set_click_callback`
```c
BLURWINDOW_API BlurErrorCode blur_set_click_callback(
    BlurWindowHandle window,
    BlurClickCallback callback,
    void* userData
);
```
Sets the callback for click events.

**Example (C)**:
```c
void on_click(BlurWindowHandle window, int32_t x, int32_t y, void* userData) {
    printf("Clicked at (%d, %d)\n", x, y);
    blur_stop(window);
    blur_destroy_window(window);
}

// Register callback
blur_set_click_callback(window, on_click, NULL);
```

---

## Utilities

### `blur_get_hwnd`
```c
BLURWINDOW_API void* blur_get_hwnd(BlurWindowHandle window);
```
Gets the native window handle (HWND) of the blur window.
Use this for Z-order control or direct Win32 API operations.

**Returns**: Native HWND, or NULL if invalid

### `blur_get_fps`
```c
BLURWINDOW_API float blur_get_fps(BlurWindowHandle window);
```
Gets the current rendering FPS.

### `blur_get_last_error`
```c
BLURWINDOW_API const char* blur_get_last_error(void);
```
Gets the last error message.

**Returns**: Error message string (static memory, no free required)

### `blur_enable_logging`
```c
BLURWINDOW_API void blur_enable_logging(BlurSystemHandle sys, int32_t enable, const char* path);
```
Dynamically enables/disables logging.
- `enable`: 1 to enable, 0 to disable
- `path`: Log file path (NULL for console output)
