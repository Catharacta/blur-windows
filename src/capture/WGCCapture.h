#pragma once

#include "ICaptureSubsystem.h"

// Windows Graphics Capture API (WinRT) headers
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

// Interop headers
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <memory>
#include <vector>
#include <atomic>

namespace blurwindow {

/// Monitor information for WGC capture
struct WGCMonitorInfo {
    HMONITOR hMonitor;
    RECT bounds;
    UINT dpi;
    bool isPrimary;
};

/// Windows Graphics Capture based capture subsystem
/// Supports cross-GPU capture (unlike Desktop Duplication)
class WGCCapture : public ICaptureSubsystem {
public:
    WGCCapture();
    ~WGCCapture() override;

    /// Check if Windows Graphics Capture is available on this system
    static bool IsAvailable();

    /// Initialize the WGC capture subsystem
    bool Initialize(ID3D11Device* device) override;

    /// Capture a frame from the specified region
    bool CaptureFrame(const RECT& region, ID3D11Texture2D** outTexture) override;

    /// Release capture resources
    void Shutdown() override;

    /// Set self window handle for self-capture avoidance
    void SetSelfWindow(HWND hwnd) override;

    /// Set target monitor and lock to it
    void SetTargetMonitor(HMONITOR monitor) override;

    /// Check if target monitor is locked
    bool IsTargetLocked() const override;

private:
    void EnumerateMonitors();
    int FindMonitorForRegion(const RECT& region) const;
    bool InitializeCaptureForMonitor(int monitorIndex);
    
    // Direct3D interop helpers
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice CreateDirect3DDevice();
    
    // Frame callback handler
    void OnFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const& args);

    // D3D11 resources
    ID3D11Device* m_device = nullptr;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Texture2D> m_cachedTexture;
    
    // WinRT Direct3D device
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{ nullptr };
    
    // Capture session components
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_captureItem{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
    
    // Frame pool event token
    winrt::event_token m_frameArrivedToken;
    
    // Monitor list
    std::vector<WGCMonitorInfo> m_monitors;
    int m_currentMonitorIndex = -1;
    
    // Latest captured frame (thread-safe access)
    std::atomic<ID3D11Texture2D*> m_latestFrame{ nullptr };
    ComPtr<ID3D11Texture2D> m_latestFrameHolder;
    
    // State
    bool m_initialized = false;
    bool m_targetLocked = false;  // Target monitor lock flag
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    HWND m_selfHwnd = nullptr;
};

/// Factory function
std::unique_ptr<ICaptureSubsystem> CreateWGCCapture();

} // namespace blurwindow
