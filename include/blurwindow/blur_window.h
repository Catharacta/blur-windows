#pragma once

#include "blurwindow.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4251)
#endif
#include <string>
#include <functional>

namespace blurwindow {

/// Blur window class - manages a single blur effect window
class BLURWINDOW_API BlurWindow {
public:
    /// Destructor
    ~BlurWindow();

    /// Start the blur effect rendering loop
    void Start();

    /// Stop the blur effect rendering loop
    void Stop();

    /// Check if the blur window is running
    bool IsRunning() const;

    /// Set the effect pipeline from JSON configuration
    /// @param jsonConfig JSON configuration string
    /// @return true on success
    bool SetEffectPipeline(const std::string& jsonConfig);

    /// Check if graphics subsystems are initialized
    bool IsInitialized() const;

    /// Set quality preset
    /// @param preset Quality preset
    void SetPreset(QualityPreset preset);

    /// Get current quality preset
    QualityPreset GetPreset() const;

    /// Set blur strength
    /// @param strength Blur strength (0.0 to 1.0)
    void SetBlurStrength(float strength);

    /// Set blur tint color
    /// @param r Red component (0.0 to 1.0)
    /// @param g Green component (0.0 to 1.0)
    /// @param b Blue component (0.0 to 1.0)
    /// @param a Alpha component (0.0 to 1.0)
    void SetBlurColor(float r, float g, float b, float a);

    /// Set noise intensity (0.0 to 1.0)
    void SetNoiseIntensity(float intensity);

    /// Set noise scale (1.0 to 100.0)
    void SetNoiseScale(float scale);

    /// Set noise animation speed
    void SetNoiseSpeed(float speed);

    /// Set noise type (0: White, 1: Sinusoid, 2: Grid, 3: Perlin, 4: Simplex, 5: Voronoi)
    void SetNoiseType(int type);

    /// Set the active effect type (0: Gaussian, 1: Box, 2: Kawase, 3: Radial)
    void SetEffectType(int type);

    /// Set blur-specific parameter (Sigma for Gaussian, Radius for Box, Iterations for Kawase)
    void SetBlurParam(float param);

    // --- Rain Effect Control ---

    /// Set rain effect intensity (0.0 to 1.0)
    void SetRainIntensity(float intensity);

    /// Set raindrop fall speed (0.1 to 5.0)
    void SetRainDropSpeed(float speed);

    /// Set refraction strength (0.0 to 1.0)
    void SetRainRefraction(float strength);

    /// Set trail length (0.0 to 1.0)
    void SetRainTrailLength(float length);

    /// Set drop size range (in pixels)
    void SetRainDropSize(float minSize, float maxSize);

    /// Enable/disable click-through
    /// @param enable true to enable click-through
    void SetClickThrough(bool enable);

    /// Enable/disable always-on-top
    /// @param enable true to enable always-on-top
    void SetTopMost(bool enable);

    /// Update window bounds
    /// @param bounds New bounds (screen coordinates)
    void SetBounds(const RECT& bounds);

    /// Get current bounds
    RECT GetBounds() const;

    /// Get the window handle
    HWND GetHWND() const;

    /// Get current FPS
    float GetCurrentFPS() const;

    // Disable copy
    BlurWindow(const BlurWindow&) = delete;
    BlurWindow& operator=(const BlurWindow&) = delete;

private:
    friend class BlurSystem;
    BlurWindow(HWND owner, const WindowOptions& opts);

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace blurwindow

#ifdef _MSC_VER
#pragma warning(pop)
#endif
