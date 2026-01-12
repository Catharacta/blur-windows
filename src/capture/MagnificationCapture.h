#pragma once
#include "ICaptureSubsystem.h"
#include <magnification.h>
#include <mutex>
#include <vector>

namespace blurwindow {

class MagnificationCapture : public ICaptureSubsystem {
public:
    MagnificationCapture();
    ~MagnificationCapture() override;

    bool Initialize(ID3D11Device* device) override;
    bool CaptureFrame(const RECT& region, ID3D11Texture2D** outTexture) override;
    void Shutdown() override;
    void SetSelfWindow(HWND hwnd) override;
    void SetTargetMonitor(HMONITOR monitor) override;
    bool IsTargetLocked() const override;

private:
    // Window procedure for the host window
    static LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Magnification callback
    static BOOL CALLBACK MagImageScalingCallback(
        HWND hwnd,
        void* srcdata,
        MAGIMAGEHEADER srcheader,
        void* destdata,
        MAGIMAGEHEADER destheader,
        RECT unclipped,
        RECT clipped,
        HRGN dirty
    );

    bool CreateHostWindow();
    bool CreateMagnifierControl();
    bool EnsureTexture(int width, int height);
    bool InitOnRenderThread(); // New: Initialize on the render thread

    ID3D11Device* m_device = nullptr;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Texture2D> m_capturedTexture;
    
    bool m_magInitialized = false; // Flag for deferred init
    bool m_filterDirty = false; // Flag to update exclusion list on render thread
    
    HWND m_hostHwnd = nullptr;
    HWND m_magHwnd = nullptr;
    HWND m_selfHwnd = nullptr; // Window to exclude

    // Captured data buffer
    std::vector<uint8_t> m_pixelBuffer;
    int m_bufferWidth = 0;
    int m_bufferHeight = 0;
    std::mutex m_bufferMutex;
    bool m_hasNewFrame = false;

    // Monitor targeting
    HMONITOR m_targetMonitor = nullptr;
    bool m_monitorLocked = false;
};

} // namespace blurwindow
