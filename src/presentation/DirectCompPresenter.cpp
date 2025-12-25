#include "IPresenter.h"
#include <dcomp.h>
#include <dxgi1_2.h>

#pragma comment(lib, "dcomp.lib")

namespace blurwindow {

/// DirectComposition-based presenter for low-latency display
class DirectCompPresenter : public IPresenter {
public:
    DirectCompPresenter() = default;
    ~DirectCompPresenter() override { Shutdown(); }

    bool Initialize(HWND hwnd, ID3D11Device* device) override {
        m_hwnd = hwnd;
        m_device = device;

        // Get DXGI device
        ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr = m_device->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()));
        if (FAILED(hr)) return false;

        // Create DirectComposition device
        hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(m_dcompDevice.GetAddressOf()));
        if (FAILED(hr)) return false;

        // Create target for the window
        hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, m_dcompTarget.GetAddressOf());
        if (FAILED(hr)) return false;

        // Create visual
        hr = m_dcompDevice->CreateVisual(m_visual.GetAddressOf());
        if (FAILED(hr)) return false;

        // Set visual as root
        hr = m_dcompTarget->SetRoot(m_visual.Get());
        if (FAILED(hr)) return false;

        // Create swap chain
        if (!CreateSwapChain()) return false;

        // Commit
        hr = m_dcompDevice->Commit();
        return SUCCEEDED(hr);
    }

    bool Present(ID3D11Texture2D* texture) override {
        if (!m_swapChain || !texture) return false;

        // Get back buffer
        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
        if (FAILED(hr)) return false;

        // Copy texture to back buffer
        ComPtr<ID3D11DeviceContext> context;
        m_device->GetImmediateContext(context.GetAddressOf());
        context->CopyResource(backBuffer.Get(), texture);

        // Present
        DXGI_PRESENT_PARAMETERS params = {};
        hr = m_swapChain->Present1(1, 0, &params);  // VSync on
        
        return SUCCEEDED(hr);
    }

    bool Resize(uint32_t width, uint32_t height) override {
        if (width == 0 || height == 0) return false;
        if (width == m_width && height == m_height) return true;

        m_width = width;
        m_height = height;

        // Resize swap chain
        HRESULT hr = m_swapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        if (FAILED(hr)) return false;

        m_dcompDevice->Commit();
        return true;
    }

    void Shutdown() override {
        m_visual.Reset();
        m_dcompTarget.Reset();
        m_dcompDevice.Reset();
        m_swapChain.Reset();
        m_device = nullptr;
        m_hwnd = nullptr;
    }

private:
    bool CreateSwapChain() {
        RECT rect;
        GetClientRect(m_hwnd, &rect);
        m_width = rect.right - rect.left;
        m_height = rect.bottom - rect.top;

        if (m_width == 0) m_width = 1;
        if (m_height == 0) m_height = 1;

        // Get DXGI factory
        ComPtr<IDXGIDevice> dxgiDevice;
        m_device->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()));

        ComPtr<IDXGIAdapter> adapter;
        dxgiDevice->GetAdapter(adapter.GetAddressOf());

        ComPtr<IDXGIFactory2> factory;
        adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));

        // Create swap chain
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = m_width;
        desc.Height = m_height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

        HRESULT hr = factory->CreateSwapChainForComposition(
            m_device, &desc, nullptr, m_swapChain.GetAddressOf()
        );
        if (FAILED(hr)) return false;

        // Set swap chain on visual
        hr = m_visual->SetContent(m_swapChain.Get());
        return SUCCEEDED(hr);
    }

    HWND m_hwnd = nullptr;
    ID3D11Device* m_device = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    ComPtr<IDCompositionDevice> m_dcompDevice;
    ComPtr<IDCompositionTarget> m_dcompTarget;
    ComPtr<IDCompositionVisual> m_visual;
    ComPtr<IDXGISwapChain1> m_swapChain;
};

} // namespace blurwindow
