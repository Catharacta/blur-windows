#include "blurwindow/c_api.h"
#include "blurwindow/blurwindow.h"
#include "blurwindow/blur_window.h"
#include <string>

using namespace blurwindow;

static std::string g_lastError;

extern "C" {

BlurSystemHandle blur_init(const BlurSystemOptionsC* opts) {
    BlurSystemOptions options = {};
    
    if (opts) {
        options.enableLogging = (opts->enableLogging != 0);
        options.logPath = opts->logPath;
        options.defaultPreset = static_cast<QualityPreset>(opts->defaultPreset);
    }

    if (!BlurSystem::Instance().Initialize(options)) {
        g_lastError = "Failed to initialize blur system";
        return nullptr;
    }

    return reinterpret_cast<BlurSystemHandle>(&BlurSystem::Instance());
}

void blur_shutdown(BlurSystemHandle sys) {
    if (sys) {
        BlurSystem::Instance().Shutdown();
    }
}

BlurWindowHandle blur_create_window(BlurSystemHandle sys, void* owner, const BlurWindowOptionsC* opts) {
    if (!sys || !opts) {
        g_lastError = "Invalid parameters";
        return nullptr;
    }

    WindowOptions options = {};
    options.owner = static_cast<HWND>(owner);
    options.bounds.left = opts->bounds.left;
    options.bounds.top = opts->bounds.top;
    options.bounds.right = opts->bounds.right;
    options.bounds.bottom = opts->bounds.bottom;
    options.topMost = (opts->topMost != 0);
    options.clickThrough = (opts->clickThrough != 0);

    auto window = BlurSystem::Instance().CreateBlurWindow(options.owner, options);
    if (!window) {
        g_lastError = "Failed to create blur window";
        return nullptr;
    }

    return reinterpret_cast<BlurWindowHandle>(window.release());
}

void blur_destroy_window(BlurWindowHandle window) {
    if (window) {
        delete reinterpret_cast<BlurWindow*>(window);
    }
}

BlurErrorCode blur_start(BlurWindowHandle window) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    
    auto* w = reinterpret_cast<BlurWindow*>(window);
    w->Start();
    return BLUR_OK;
}

BlurErrorCode blur_stop(BlurWindowHandle window) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    
    auto* w = reinterpret_cast<BlurWindow*>(window);
    w->Stop();
    return BLUR_OK;
}

BlurErrorCode blur_set_preset(BlurWindowHandle window, BlurQualityPreset preset) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    
    auto* w = reinterpret_cast<BlurWindow*>(window);
    w->SetPreset(static_cast<QualityPreset>(preset));
    return BLUR_OK;
}

BlurErrorCode blur_set_pipeline(BlurWindowHandle window, const char* json_config) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    if (!json_config) return BLUR_ERROR_INVALID_PARAMETER;
    
    auto* w = reinterpret_cast<BlurWindow*>(window);
    if (!w->SetEffectPipeline(json_config)) {
        g_lastError = "Failed to set effect pipeline";
        return BLUR_ERROR_INVALID_PARAMETER;
    }
    return BLUR_OK;
}

BlurErrorCode blur_set_bounds(BlurWindowHandle window, const BlurRect* bounds) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    if (!bounds) return BLUR_ERROR_INVALID_PARAMETER;
    
    // Validate bounds
    if (bounds->right <= bounds->left || bounds->bottom <= bounds->top) {
        g_lastError = "Invalid bounds: width and height must be positive";
        return BLUR_ERROR_INVALID_PARAMETER;
    }
    
    auto* w = reinterpret_cast<BlurWindow*>(window);
    RECT r = {bounds->left, bounds->top, bounds->right, bounds->bottom};
    w->SetBounds(r);
    return BLUR_OK;
}

BlurErrorCode blur_set_noise_intensity(BlurWindowHandle window, float intensity) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    auto* w = reinterpret_cast<BlurWindow*>(window);
    w->SetNoiseIntensity(intensity);
    return BLUR_OK;
}

BlurErrorCode blur_set_effect_type(BlurWindowHandle window, int32_t type) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    auto* w = reinterpret_cast<BlurWindow*>(window);
    w->SetEffectType(type);
    return BLUR_OK;
}

BlurErrorCode blur_set_strength(BlurWindowHandle window, float strength) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    auto* w = reinterpret_cast<BlurWindow*>(window);
    w->SetBlurStrength(strength);
    return BLUR_OK;
}

BlurErrorCode blur_set_blur_param(BlurWindowHandle window, float param) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    auto* w = reinterpret_cast<BlurWindow*>(window);
    w->SetBlurParam(param);
    return BLUR_OK;
}

BlurErrorCode blur_set_tint_color(BlurWindowHandle window, float r, float g, float b, float a) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    auto* w = reinterpret_cast<BlurWindow*>(window);
    w->SetBlurColor(r, g, b, a);
    return BLUR_OK;
}

BlurErrorCode blur_set_noise_scale(BlurWindowHandle window, float scale) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    auto* w = reinterpret_cast<BlurWindow*>(window);
    w->SetNoiseScale(scale);
    return BLUR_OK;
}

BlurErrorCode blur_set_noise_speed(BlurWindowHandle window, float speed) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    auto* w = reinterpret_cast<BlurWindow*>(window);
    w->SetNoiseSpeed(speed);
    return BLUR_OK;
}

BlurErrorCode blur_set_noise_type(BlurWindowHandle window, int32_t type) {
    if (!window) return BLUR_ERROR_INVALID_HANDLE;
    auto* w = reinterpret_cast<BlurWindow*>(window);
    w->SetNoiseType(type);
    return BLUR_OK;
}

float blur_get_fps(BlurWindowHandle window) {
    if (!window) return -1.0f;
    
    auto* w = reinterpret_cast<BlurWindow*>(window);
    return w->GetCurrentFPS();
}

const char* blur_get_last_error(void) {
    return g_lastError.c_str();
}

void blur_enable_logging(BlurSystemHandle sys, int32_t enable, const char* path) {
    // TODO: Implement logging control
    (void)sys;
    (void)enable;
    (void)path;
}

} // extern "C"
