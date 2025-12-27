#pragma once

#include <cstdint>

/// @file c_api.h
/// @brief C ABI wrapper for Rust/FFI interoperability

#ifdef __cplusplus
extern "C" {
#endif

// Handle types
typedef void* BlurSystemHandle;
typedef void* BlurWindowHandle;

// Quality preset enum (C compatible)
typedef enum {
    BLUR_PRESET_HIGH = 0,
    BLUR_PRESET_BALANCED = 1,
    BLUR_PRESET_PERFORMANCE = 2,
    BLUR_PRESET_MINIMAL = 3
} BlurQualityPreset;

// Error codes
typedef enum {
    BLUR_OK = 0,
    BLUR_ERROR_NOT_INITIALIZED = -1,
    BLUR_ERROR_INVALID_HANDLE = -2,
    BLUR_ERROR_INVALID_PARAMETER = -3,
    BLUR_ERROR_D3D11_FAILED = -4,
    BLUR_ERROR_CAPTURE_FAILED = -5,
    BLUR_ERROR_UNKNOWN = -99
} BlurErrorCode;

// Rect structure (C compatible)
typedef struct {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} BlurRect;

// System options (C compatible)
typedef struct {
    int32_t enableLogging;     // 0 = false, 1 = true
    const char* logPath;       // NULL for console
    BlurQualityPreset defaultPreset;
} BlurSystemOptionsC;

// Window options (C compatible)
typedef struct {
    void* owner;               // HWND
    BlurRect bounds;
    int32_t topMost;           // 0 = false, 1 = true
    int32_t clickThrough;      // 0 = false, 1 = true
} BlurWindowOptionsC;

#ifndef BLURWINDOW_API
#ifdef BLURWINDOW_DLL
    #ifdef BLURWINDOW_EXPORTS
        #define BLURWINDOW_API __declspec(dllexport)
    #else
        #define BLURWINDOW_API __declspec(dllimport)
    #endif
#else
    #define BLURWINDOW_API
#endif
#endif

/// Initialize the blur system
/// @param opts System options (can be NULL for defaults)
/// @return System handle, or NULL on failure
BLURWINDOW_API BlurSystemHandle blur_init(const BlurSystemOptionsC* opts);

/// Shutdown the blur system
/// @param sys System handle
BLURWINDOW_API void blur_shutdown(BlurSystemHandle sys);

/// Create a blur window
/// @param sys System handle
/// @param owner Owner window handle (HWND)
/// @param opts Window options
/// @return Window handle, or NULL on failure
BLURWINDOW_API BlurWindowHandle blur_create_window(BlurSystemHandle sys, void* owner, const BlurWindowOptionsC* opts);

/// Destroy a blur window
/// @param window Window handle
BLURWINDOW_API void blur_destroy_window(BlurWindowHandle window);

/// Start blur effect
/// @param window Window handle
/// @return Error code
BLURWINDOW_API BlurErrorCode blur_start(BlurWindowHandle window);

/// Stop blur effect
/// @param window Window handle
/// @return Error code
BLURWINDOW_API BlurErrorCode blur_stop(BlurWindowHandle window);

/// Set quality preset
/// @param window Window handle
/// @param preset Quality preset
/// @return Error code
BLURWINDOW_API BlurErrorCode blur_set_preset(BlurWindowHandle window, BlurQualityPreset preset);

/// Set effect pipeline from JSON
/// @param window Window handle
/// @param json_config JSON configuration string
/// @return Error code
BLURWINDOW_API BlurErrorCode blur_set_pipeline(BlurWindowHandle window, const char* json_config);

/// Set window bounds
/// @param window Window handle
/// @param bounds New bounds
BLURWINDOW_API BlurErrorCode blur_set_bounds(BlurWindowHandle window, const BlurRect* bounds);

// --- Effect Management ---

/// Set the active effect type (0: Gaussian, 1: Box, 2: Kawase, 3: Radial)
BLURWINDOW_API BlurErrorCode blur_set_effect_type(BlurWindowHandle window, int32_t type);

/// Set overall effect strength (0.0 to 1.0)
/// Determines the blend amount between original and blurred image.
BLURWINDOW_API BlurErrorCode blur_set_strength(BlurWindowHandle window, float strength);

/// Set blur-specific parameter (Sigma for Gaussian, Radius for Box, Iterations for Kawase)
BLURWINDOW_API BlurErrorCode blur_set_blur_param(BlurWindowHandle window, float param);

/// Set tint color (RGBA, each 0.0 to 1.0)
/// This replaces the previous tint color.
BLURWINDOW_API BlurErrorCode blur_set_tint_color(BlurWindowHandle window, float r, float g, float b, float a);

// --- Noise Control ---

/// Set noise intensity (0.0 to 1.0)
BLURWINDOW_API BlurErrorCode blur_set_noise_intensity(BlurWindowHandle window, float intensity);

/// Set noise scale (1.0 to 1000.0)
BLURWINDOW_API BlurErrorCode blur_set_noise_scale(BlurWindowHandle window, float scale);

/// Set noise animation speed
BLURWINDOW_API BlurErrorCode blur_set_noise_speed(BlurWindowHandle window, float speed);

/// Set noise type (0: White, 1: Sinusoid, 2: Grid, 3: Perlin, 4: Simplex, 5: Voronoi)
BLURWINDOW_API BlurErrorCode blur_set_noise_type(BlurWindowHandle window, int32_t type);

// --- Utility ---

/// Get current FPS
/// @param window Window handle
/// @return Current FPS, or -1 on error
BLURWINDOW_API float blur_get_fps(BlurWindowHandle window);

/// Get last error message
/// @return Error message string (static, do not free)
BLURWINDOW_API const char* blur_get_last_error(void);

/// Enable/disable logging
/// @param sys System handle
/// @param enable 0 = disable, 1 = enable
/// @param path Log file path (NULL for console)
BLURWINDOW_API void blur_enable_logging(BlurSystemHandle sys, int32_t enable, const char* path);

#ifdef __cplusplus
}
#endif
