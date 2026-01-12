#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <windows.h>

namespace blurwindow {

using Microsoft::WRL::ComPtr;

/// Abstract interface for capture subsystems
class ICaptureSubsystem {
public:
    virtual ~ICaptureSubsystem() = default;

    /// Initialize the capture subsystem
    /// @param device D3D11 device
    /// @return true on success
    virtual bool Initialize(ID3D11Device* device) = 0;

    /// Capture a frame from the specified region
    /// @param region Screen region to capture
    /// @param outTexture Output texture (caller does NOT own)
    /// @return true on success
    virtual bool CaptureFrame(const RECT& region, ID3D11Texture2D** outTexture) = 0;

    /// Release capture resources
    virtual void Shutdown() = 0;

    /// Set self window handle for self-capture avoidance
    /// @param hwnd Self window handle
    virtual void SetSelfWindow(HWND hwnd) = 0;

    /// Set target monitor and lock initialization to it
    /// @param monitor Target monitor handle (HMONITOR)
    virtual void SetTargetMonitor(HMONITOR monitor) = 0;

    /// Check if target monitor is locked
    /// @return true if target is locked
    virtual bool IsTargetLocked() const = 0;
};

} // namespace blurwindow
