#include "ICaptureSubsystem.h"
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

using Microsoft::WRL::ComPtr;

namespace blurwindow {

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
        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
        if (FAILED(hr)) return false;

        // Enumerate outputs and find the one containing our region
        ComPtr<IDXGIOutput> output;
        for (UINT i = 0; adapter->EnumOutputs(i, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);
            
            // For now, use the first output (primary monitor)
            // TODO: Select based on capture region
            if (i == 0) {
                break;
            }
        }

        if (!output) return false;

        // Get Output1 for duplication
        ComPtr<IDXGIOutput1> output1;
        hr = output.As(&output1);
        if (FAILED(hr)) return false;

        // Create desktop duplication
        hr = output1->DuplicateOutput(m_device, m_duplication.GetAddressOf());
        if (FAILED(hr)) {
            // Desktop duplication may fail if:
            // - Running in a restricted session
            // - Another app has exclusive access
            // - GPU driver doesn't support it
            return false;
        }

        // Get output description for coordinate mapping
        DXGI_OUTDUPL_DESC duplDesc;
        m_duplication->GetDesc(&duplDesc);
        m_outputWidth = duplDesc.ModeDesc.Width;
        m_outputHeight = duplDesc.ModeDesc.Height;

        m_initialized = true;
        return true;
    }

    bool CaptureFrame(const RECT& region, ID3D11Texture2D** outTexture) override {
        if (!m_initialized || !outTexture) return false;

        // Release previous frame if any
        if (m_frameAcquired) {
            m_duplication->ReleaseFrame();
            m_frameAcquired = false;
        }

        // Acquire next frame (non-blocking)
        ComPtr<IDXGIResource> desktopResource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        
        HRESULT hr = m_duplication->AcquireNextFrame(
            0,  // 0ms timeout - non-blocking, use cache if no new frame
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

        if (FAILED(hr)) {
            // Check if we need to reinitialize
            if (hr == DXGI_ERROR_ACCESS_LOST) {
                m_initialized = false;
                // TODO: Reinitialize
            }
            return false;
        }

        m_frameAcquired = true;

        // Get texture from resource
        ComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource.As(&desktopTexture);
        if (FAILED(hr)) return false;

        // Calculate region dimensions
        int regionWidth = region.right - region.left;
        int regionHeight = region.bottom - region.top;

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

        // Copy region from desktop texture
        D3D11_BOX srcBox = {};
        srcBox.left = max(0, min(region.left, (LONG)m_outputWidth));
        srcBox.top = max(0, min(region.top, (LONG)m_outputHeight));
        srcBox.right = max(0, min(region.right, (LONG)m_outputWidth));
        srcBox.bottom = max(0, min(region.bottom, (LONG)m_outputHeight));
        srcBox.front = 0;
        srcBox.back = 1;

        // Apply self-capture mask if needed
        if (m_selfHwnd && IsWindowVisible(m_selfHwnd)) {
            // TODO: Implement mask + cache for self-capture avoidance
        }

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
        m_device = nullptr;
        m_initialized = false;
    }

    void SetSelfWindow(HWND hwnd) override {
        m_selfHwnd = hwnd;
    }

private:
    bool NeedsResize(int width, int height) const {
        return m_cachedWidth != width || m_cachedHeight != height;
    }

    ID3D11Device* m_device = nullptr;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<ID3D11Texture2D> m_cachedTexture;

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
