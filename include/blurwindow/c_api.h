#pragma once

#include <cstdint>

/**
 * @file c_api.h
 * @brief C ABI wrapper for Rust/FFI interoperability.
 * 
 * Provides a stable C interface for the BlurWindow library, allowing it to be used
 * from managed languages (C#, Python) or other native languages (Rust, Go).
 */

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque handle to the global blur system.
typedef void* BlurSystemHandle;

/// Opaque handle to a specific blur window instance.
typedef void* BlurWindowHandle;

/// Quality preset levels for the blur renderer.
typedef enum {
    BLUR_PRESET_HIGH = 0,         ///< High quality, multi-pass rendering.
    BLUR_PRESET_BALANCED = 1,     ///< Balanced quality and performance.
    BLUR_PRESET_PERFORMANCE = 2,  ///< Optimized for performance.
    BLUR_PRESET_MINIMAL = 3       ///< Minimum overhead.
} BlurQualityPreset;

/// Error codes returned by API functions.
typedef enum {
    BLUR_OK = 0,                         ///< Success.
    BLUR_ERROR_NOT_INITIALIZED = -1,     ///< System not initialized.
    BLUR_ERROR_INVALID_HANDLE = -2,      ///< Handle is NULL or invalid.
    BLUR_ERROR_INVALID_PARAMETER = -3,   ///< One or more parameters are invalid.
    BLUR_ERROR_D3D11_FAILED = -4,        ///< Direct3D 11 operation failed.
    BLUR_ERROR_CAPTURE_FAILED = -5,      ///< Desktop capture failed.
    BLUR_ERROR_UNKNOWN = -99             ///< An unexpected error occurred.
} BlurErrorCode;

/// Rect structure for window bounds.
typedef struct {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} BlurRect;

/// Global system configuration options.
typedef struct {
    int32_t enableLogging;               ///< 0 = false, 1 = true.
    const char* logPath;                 ///< Path to log file (NULL for console).
    BlurQualityPreset defaultPreset;     ///< Default quality level.
} BlurSystemOptionsC;

/// Per-window creation options.
typedef struct {
    void* owner;                         ///< Parent HWND (can be NULL).
    BlurRect bounds;                     ///< Initial window position and size.
    int32_t topMost;                     ///< 1 to stay on top of other windows.
    int32_t clickThrough;                ///< 1 to allow mouse clicks to pass through.
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

/**
 * @brief Initialize the global blur system.
 * @param opts Configuration options (NULL for defaults).
 * @return Handle to the system, or NULL on failure.
 */
BLURWINDOW_API BlurSystemHandle blur_init(const BlurSystemOptionsC* opts);

/**
 * @brief Shutdown the blur system and release resources.
 * @param sys System handle returned by blur_init.
 */
BLURWINDOW_API void blur_shutdown(BlurSystemHandle sys);

/**
 * @brief Create a new blur window.
 * @param sys System handle.
 * @param owner Parent window handle (HWND).
 * @param opts Window creation options.
 * @return Handle to the window, or NULL on failure.
 */
BLURWINDOW_API BlurWindowHandle blur_create_window(BlurSystemHandle sys, void* owner, const BlurWindowOptionsC* opts);

/**
 * @brief Destroy a blur window.
 * @param window Window handle.
 */
BLURWINDOW_API void blur_destroy_window(BlurWindowHandle window);

/**
 * @brief Start rendering the blur effect.
 * @param window Window handle.
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_start(BlurWindowHandle window);

/**
 * @brief Stop rendering the blur effect.
 * @param window Window handle.
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_stop(BlurWindowHandle window);

/**
 * @brief Set the quality preset for a specific window.
 * @param window Window handle.
 * @param preset New quality level.
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_set_preset(BlurWindowHandle window, BlurQualityPreset preset);

/**
 * @brief Set the effect pipeline configuration using a JSON string.
 * @param window Window handle.
 * @param json_config JSON configuration.
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_set_pipeline(BlurWindowHandle window, const char* json_config);

/**
 * @brief Update the window bounds.
 * @param window Window handle.
 * @param bounds New rectangle (left, top, right, bottom).
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_set_bounds(BlurWindowHandle window, const BlurRect* bounds);

// --- Effect Management ---

/**
 * @brief Set the active effect type.
 * @param window Window handle.
 * @param type 0: Gaussian, 1: Box, 2: Kawase, 3: Radial.
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_set_effect_type(BlurWindowHandle window, int32_t type);

/**
 * @brief Set the overall blend strength.
 * @param window Window handle.
 * @param strength 0.0 (transparent) to 1.0 (full blur).
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_set_strength(BlurWindowHandle window, float strength);

/**
 * @brief Set the primary parameter for the active effect.
 * @param window Window handle.
 * @param param Effect specific (Sigma for Gaussian, Radius for Box, etc).
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_set_blur_param(BlurWindowHandle window, float param);

/**
 * @brief Set the tint color.
 * @param r Red (0-1).
 * @param g Green (0-1).
 * @param b Blue (0-1).
 * @param a Alpha (0-1).
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_set_tint_color(BlurWindowHandle window, float r, float g, float b, float a);

// --- Noise Control ---

/**
 * @brief Set the intensity of the noise overlay.
 * @param window Window handle.
 * @param intensity 0.0 to 1.0.
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_set_noise_intensity(BlurWindowHandle window, float intensity);

/**
 * @brief Set the spatial scale of the noise pattern.
 * @param window Window handle.
 * @param scale 1.0 to 1000.0.
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_set_noise_scale(BlurWindowHandle window, float scale);

/**
 * @brief Set the animation speed of the noise.
 * @param window Window handle.
 * @param speed Speed factor (0 for static).
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_set_noise_speed(BlurWindowHandle window, float speed);

/**
 * @brief Set the noise pattern type.
 * @param window Window handle.
 * @param type 0: White, 1: Sin, 2: Grid, 3: Perlin, 4: Simplex, 5: Voronoi.
 * @return BLUR_OK on success.
 */
BLURWINDOW_API BlurErrorCode blur_set_noise_type(BlurWindowHandle window, int32_t type);

// --- Utility ---

/**
 * @brief Get current FPS for the window.
 * @return Current frames per second.
 */
BLURWINDOW_API float blur_get_fps(BlurWindowHandle window);

/**
 * @brief Get the last error string.
 * @return Static error string pointer.
 */
BLURWINDOW_API const char* blur_get_last_error(void);

/**
 * @brief Enable or disable detailed logging.
 * @param sys System handle.
 * @param enable 1 to enable, 0 to disable.
 * @param path Optional file path (NULL for console).
 */
BLURWINDOW_API void blur_enable_logging(BlurSystemHandle sys, int32_t enable, const char* path);

#ifdef __cplusplus
}
#endif
