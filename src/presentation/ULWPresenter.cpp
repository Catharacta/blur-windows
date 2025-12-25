#include "IPresenter.h"

namespace blurwindow {

/// UpdateLayeredWindow-based presenter (fallback/compatibility)
class ULWPresenter : public IPresenter {
public:
    ULWPresenter() = default;
    ~ULWPresenter() override { Shutdown(); }

    bool Initialize(HWND hwnd, ID3D11Device* device) override {
        m_hwnd = hwnd;
        m_device = device;
        m_device->GetImmediateContext(m_context.GetAddressOf());

        RECT rect;
        GetClientRect(m_hwnd, &rect);
        m_width = rect.right - rect.left;
        m_height = rect.bottom - rect.top;

        // Create staging texture for GPU->CPU copy
        return CreateStagingTexture();
    }

    bool Present(ID3D11Texture2D* texture) override {
        if (!texture || !m_stagingTexture) return false;

        // Copy GPU texture to staging
        m_context->CopyResource(m_stagingTexture.Get(), texture);

        // Map staging texture
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = m_context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return false;

        // Create DC and bitmap
        HDC screenDC = GetDC(nullptr);
        HDC memDC = CreateCompatibleDC(screenDC);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = m_width;
        bmi.bmiHeader.biHeight = -(LONG)m_height; // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);

        // Copy pixel data
        uint8_t* src = static_cast<uint8_t*>(mapped.pData);
        uint8_t* dst = static_cast<uint8_t*>(bits);
        for (uint32_t y = 0; y < m_height; y++) {
            memcpy(dst + y * m_width * 4, src + y * mapped.RowPitch, m_width * 4);
        }

        m_context->Unmap(m_stagingTexture.Get(), 0);

        // Update layered window
        POINT ptSrc = {0, 0};
        SIZE size = {(LONG)m_width, (LONG)m_height};
        BLENDFUNCTION blend = {};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;

        POINT ptDst;
        RECT windowRect;
        GetWindowRect(m_hwnd, &windowRect);
        ptDst.x = windowRect.left;
        ptDst.y = windowRect.top;

        UpdateLayeredWindow(m_hwnd, screenDC, &ptDst, &size, memDC, &ptSrc, 0, &blend, ULW_ALPHA);

        // Cleanup
        SelectObject(memDC, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);

        return true;
    }

    bool Resize(uint32_t width, uint32_t height) override {
        if (width == m_width && height == m_height) return true;
        
        m_width = width;
        m_height = height;
        return CreateStagingTexture();
    }

    void Shutdown() override {
        m_stagingTexture.Reset();
        m_context.Reset();
        m_device = nullptr;
        m_hwnd = nullptr;
    }

private:
    bool CreateStagingTexture() {
        m_stagingTexture.Reset();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = m_width;
        desc.Height = m_height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        return SUCCEEDED(m_device->CreateTexture2D(&desc, nullptr, m_stagingTexture.GetAddressOf()));
    }

    HWND m_hwnd = nullptr;
    ID3D11Device* m_device = nullptr;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Texture2D> m_stagingTexture;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace blurwindow
