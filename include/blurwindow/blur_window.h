#pragma once

#include "blurwindow.h"
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

    /// Set quality preset
    /// @param preset Quality preset
    void SetPreset(QualityPreset preset);

    /// Get current quality preset
    QualityPreset GetPreset() const;

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
