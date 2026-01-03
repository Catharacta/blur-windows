# C API Reference

This is the C-compatible API reference provided by `blurwindow.dll`.
It is designed to be used from other languages such as Rust, C#, and Python via FFI (Foreign Function Interface).

## Type Definitions

### Handles
- `BlurSystemHandle`: System handle managing the library's lifecycle. (`void*`)
- `BlurWindowHandle`: Window handle managing individual blur windows. (`void*`)

### Structures
#### `BlurRect`
Specifies the window bounds.
- `int32_t left`, `top`, `right`, `bottom`

#### `BlurSystemOptionsC`
Options for system initialization.
- `int32_t enableLogging`: Enable logging output (0: disabled, 1: enabled)
- `const char* logPath`: Path to log file (NULL for console output)
- `int32_t defaultPreset`: Default quality preset (0: High, 1: Balanced, 2: Performance, 3: Minimal)

#### `BlurWindowOptionsC`
Options for window creation.
- `void* owner`: Parent window HWND (NULL for standalone window)
- `BlurRect bounds`: Initial position and size
- `int32_t topMost`: Always on top (0: disabled, 1: enabled)
- `int32_t clickThrough`: Pass clicks through to background (0: disabled, 1: enabled)

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

---

## Noise Control

### `blur_set_noise_intensity`
```c
BLURWINDOW_API BlurErrorCode blur_set_noise_intensity(BlurWindowHandle window, float intensity);
```
Sets the noise intensity.

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
