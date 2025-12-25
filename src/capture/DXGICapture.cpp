#include "ICaptureSubsystem.h"
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <shellscalingapi.h>

#pragma comment(lib, "shcore.lib")

using Microsoft::WRL::ComPtr;

namespace blurwindow {

/// Monitor information for multi-monitor support
struct MonitorInfo {
    ComPtr<IDXGIOutput> output;
    RECT bounds;          // Physical coordinates
    UINT dpi;             // DPI scale
    bool isPrimary;
};

class DXGICapture : public ICaptureSubsystem {
public:
    DXGICapture() = default;
    ~DXGICapture() override { Shutdown(); }

    bool Initialize(ID3D11Device* device) override {
        if (!device) return false;
        
        m_device = device;
        m_device->GetImmediateContext(m_context.GetAddressOf());

        // Get DXGI device
        ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr = m_device->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()));
        if (FAILED(hr)) return false;

        // Get adapter
        hr = dxgiDevice->GetAdapter(m_adapter.GetAddressOf());
        if (FAILED(hr)) return false;

        // Enumerate all outputs (monitors)
        EnumerateMonitors();

        // Initialize duplication for primary monitor first
        if (!m_monitors.empty()) {
            return InitializeDuplicationForMonitor(0);
        }

        return false;
    }

    bool CaptureFrame(const RECT& region, ID3D11Texture2D** outTexture) override {
        if (!m_initialized || !outTexture) return false;

        // Check if region is on a different monitor
        int monitorIndex = FindMonitorForRegion(region);
        if (monitorIndex != m_currentMonitorIndex && monitorIndex >= 0) {
            // Switch to the new monitor
            if (!InitializeDuplicationForMonitor(monitorIndex)) {
                // Fallback to current monitor
                monitorIndex = m_currentMonitorIndex;
            }
        }

        // Release previous frame if any
        if (m_frameAcquired) {
            m_duplication->ReleaseFrame();
            m_frameAcquired = false;
        }

        // Acquire next frame (non-blocking)
        ComPtr<IDXGIResource> desktopResource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        
        HRESULT hr = m_duplication->AcquireNextFrame(
            0,  // 0ms timeout - non-blocking
            &frameInfo,
            desktopResource.GetAddressOf()
        );

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // No new frame, use cached if available
            if (m_cachedTexture) {
                *outTexture = m_cachedTexture.Get();
                return true;
            }
            return false;
        }

        if (hr == DXGI_ERROR_ACCESS_LOST) {
            // Need to reinitialize
            m_initialized = false;
            InitializeDuplicationForMonitor(m_currentMonitorIndex);
            return false;
        }

        if (FAILED(hr)) {
            return false;
        }

        m_frameAcquired = true;

        // Get texture from resource
        ComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource.As(&desktopTexture);
        if (FAILED(hr)) return false;

        // Convert region from logical to physical coordinates (DPI-aware)
        RECT physicalRegion = ConvertToPhysicalCoordinates(region, monitorIndex);

        // Calculate region dimensions
        int regionWidth = physicalRegion.right - physicalRegion.left;
        int regionHeight = physicalRegion.bottom - physicalRegion.top;

        if (regionWidth <= 0 || regionHeight <= 0) {
            return false;
        }

        // Create or recreate output texture if needed
        if (!m_cachedTexture || NeedsResize(regionWidth, regionHeight)) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = regionWidth;
            desc.Height = regionHeight;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

            hr = m_device->CreateTexture2D(&desc, nullptr, m_cachedTexture.ReleaseAndGetAddressOf());
            if (FAILED(hr)) return false;

            m_cachedWidth = regionWidth;
            m_cachedHeight = regionHeight;
        }

        // Calculate source box relative to current monitor
        D3D11_BOX srcBox = {};
        const MonitorInfo& monInfo = m_monitors[m_currentMonitorIndex];
        
        // Convert physical screen coordinates to monitor-relative coordinates
        srcBox.left = (std::max)(0L, physicalRegion.left - monInfo.bounds.left);
        srcBox.top = (std::max)(0L, physicalRegion.top - monInfo.bounds.top);
        srcBox.right = (std::min)((LONG)m_outputWidth, physicalRegion.right - monInfo.bounds.left);
        srcBox.bottom = (std::min)((LONG)m_outputHeight, physicalRegion.bottom - monInfo.bounds.top);
        srcBox.front = 0;
        srcBox.back = 1;

        // Clamp to valid range
        srcBox.left = (std::max)(0U, (UINT)srcBox.left);
        srcBox.top = (std::max)(0U, (UINT)srcBox.top);
        srcBox.right = (std::min)(m_outputWidth, (UINT)srcBox.right);
        srcBox.bottom = (std::min)(m_outputHeight, (UINT)srcBox.bottom);

        // Copy region from desktop texture
        m_context->CopySubresourceRegion(
            m_cachedTexture.Get(), 0,
            0, 0, 0,
            desktopTexture.Get(), 0,
            &srcBox
        );

        *outTexture = m_cachedTexture.Get();
        return true;
    }

    void Shutdown() override {
        if (m_frameAcquired && m_duplication) {
            m_duplication->ReleaseFrame();
            m_frameAcquired = false;
        }
        
        m_cachedTexture.Reset();
        m_duplication.Reset();
        m_context.Reset();
        m_adapter.Reset();
        m_monitors.clear();
        m_device = nullptr;
        m_initialized = false;
    }

    void SetSelfWindow(HWND hwnd) override {
        m_selfHwnd = hwnd;
    }

private:
    void EnumerateMonitors() {
        m_monitors.clear();
        
        ComPtr<IDXGIOutput> output;
        for (UINT i = 0; m_adapter->EnumOutputs(i, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);
            
            MonitorInfo info;
            info.output = output;
            info.bounds = desc.DesktopCoordinates;
            info.isPrimary = (i == 0);
            
            // Get DPI for this monitor
            UINT dpiX, dpiY;
            if (SUCCEEDED(GetDpiForMonitor(desc.Monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
                info.dpi = dpiX;
            } else {
                info.dpi = 96;  // Default DPI
            }
            
            m_monitors.push_back(std::move(info));
        }
    }

    int FindMonitorForRegion(const RECT& region) const {
        // Find the monitor that contains the center of the region
        LONG centerX = (region.left + region.right) / 2;
        LONG centerY = (region.top + region.bottom) / 2;
        
        for (size_t i = 0; i < m_monitors.size(); i++) {
            const RECT& bounds = m_monitors[i].bounds;
            if (centerX >= bounds.left && centerX < bounds.right &&
                centerY >= bounds.top && centerY < bounds.bottom) {
                return static_cast<int>(i);
            }
        }
        
        // Default to primary monitor
        return 0;
    }

    bool InitializeDuplicationForMonitor(int monitorIndex) {
        if (monitorIndex < 0 || monitorIndex >= static_cast<int>(m_monitors.size())) {
            return false;
        }

        // Release existing duplication
        if (m_frameAcquired && m_duplication) {
            m_duplication->ReleaseFrame();
            m_frameAcquired = false;
        }
        m_duplication.Reset();

        // Get Output1 for duplication
        ComPtr<IDXGIOutput1> output1;
        HRESULT hr = m_monitors[monitorIndex].output.As(&output1);
        if (FAILED(hr)) return false;

        // Create desktop duplication
        hr = output1->DuplicateOutput(m_device, m_duplication.GetAddressOf());
        if (FAILED(hr)) {
            return false;
        }

        // Get output description for coordinate mapping
        DXGI_OUTDUPL_DESC duplDesc;
        m_duplication->GetDesc(&duplDesc);
        m_outputWidth = duplDesc.ModeDesc.Width;
        m_outputHeight = duplDesc.ModeDesc.Height;

        m_currentMonitorIndex = monitorIndex;
        m_initialized = true;
        return true;
    }

    RECT ConvertToPhysicalCoordinates(const RECT& logicalRect, int monitorIndex) const {
        if (monitorIndex < 0 || monitorIndex >= static_cast<int>(m_monitors.size())) {
            return logicalRect;
        }

        // For now, assume coordinates are already in physical units
        // Windows 10+ with DPI awareness typically reports physical coordinates
        // TODO: Add per-monitor DPI scaling if needed
        return logicalRect;
    }

    bool NeedsResize(int width, int height) const {
        return m_cachedWidth != width || m_cachedHeight != height;
    }

    ID3D11Device* m_device = nullptr;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIAdapter> m_adapter;
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<ID3D11Texture2D> m_cachedTexture;

    std::vector<MonitorInfo> m_monitors;
    int m_currentMonitorIndex = 0;

    bool m_initialized = false;
    bool m_frameAcquired = false;
    UINT m_outputWidth = 0;
    UINT m_outputHeight = 0;
    int m_cachedWidth = 0;
    int m_cachedHeight = 0;

    HWND m_selfHwnd = nullptr;
};

// Factory function
std::unique_ptr<ICaptureSubsystem> CreateDXGICapture() {
    return std::make_unique<DXGICapture>();
}

} // namespace blurwindow
