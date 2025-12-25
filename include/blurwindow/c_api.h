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

/// Initialize the blur system
/// @param opts System options (can be NULL for defaults)
/// @return System handle, or NULL on failure
BlurSystemHandle blur_init(const BlurSystemOptionsC* opts);

/// Shutdown the blur system
/// @param sys System handle
void blur_shutdown(BlurSystemHandle sys);

/// Create a blur window
/// @param sys System handle
/// @param owner Owner window handle (HWND)
/// @param opts Window options
/// @return Window handle, or NULL on failure
BlurWindowHandle blur_create_window(BlurSystemHandle sys, void* owner, const BlurWindowOptionsC* opts);

/// Destroy a blur window
/// @param window Window handle
void blur_destroy_window(BlurWindowHandle window);

/// Start blur effect
/// @param window Window handle
/// @return Error code
BlurErrorCode blur_start(BlurWindowHandle window);

/// Stop blur effect
/// @param window Window handle
/// @return Error code
BlurErrorCode blur_stop(BlurWindowHandle window);

/// Set quality preset
/// @param window Window handle
/// @param preset Quality preset
/// @return Error code
BlurErrorCode blur_set_preset(BlurWindowHandle window, BlurQualityPreset preset);

/// Set effect pipeline from JSON
/// @param window Window handle
/// @param json_config JSON configuration string
/// @return Error code
BlurErrorCode blur_set_pipeline(BlurWindowHandle window, const char* json_config);

/// Set window bounds
/// @param window Window handle
/// @param bounds New bounds
/// @return Error code
BlurErrorCode blur_set_bounds(BlurWindowHandle window, const BlurRect* bounds);

/// Get current FPS
/// @param window Window handle
/// @return Current FPS, or -1 on error
float blur_get_fps(BlurWindowHandle window);

/// Get last error message
/// @return Error message string (static, do not free)
const char* blur_get_last_error(void);

/// Enable/disable logging
/// @param sys System handle
/// @param enable 0 = disable, 1 = enable
/// @param path Log file path (NULL for console)
void blur_enable_logging(BlurSystemHandle sys, int32_t enable, const char* path);

#ifdef __cplusplus
}
#endif
