#include "IBlurEffect.h"
#include <algorithm>

namespace blurwindow {

/// Simple box blur effect (fast, low quality)
class BoxBlur : public IBlurEffect {
public:
    BoxBlur() = default;
    ~BoxBlur() override = default;

    const char* GetName() const override {
        return "Box";
    }

    bool Initialize(ID3D11Device* device) override {
        m_device = device;

        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(BoxParams);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
        if (FAILED(hr)) return false;

        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

        hr = m_device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf());
        return SUCCEEDED(hr);
    }

    bool Apply(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* input,
        ID3D11RenderTargetView* output,
        uint32_t width,
        uint32_t height
    ) override {
        if (!m_boxPS) {
            return true;
        }

        UpdateConstantBuffer(context, width, height);

        context->PSSetShader(m_boxPS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &input);
        context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, &output, nullptr);

        context->Draw(3, 0);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->PSSetShaderResources(0, 1, &nullSRV);

        return true;
    }

    bool SetParameters(const char* json) override {
        return true;
    }

    std::string GetParameters() const override {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "{\"radius\": %d}", m_radius);
        return buffer;
    }

    void SetRadius(int radius) {
        m_radius = (std::max)(1, (std::min)(radius, 16));
    }

private:
    struct BoxParams {
        float texelSize[2];
        int radius;
        float padding;
    };

    void UpdateConstantBuffer(ID3D11DeviceContext* context, uint32_t width, uint32_t height) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            BoxParams* params = static_cast<BoxParams*>(mapped.pData);
            params->texelSize[0] = 1.0f / width;
            params->texelSize[1] = 1.0f / height;
            params->radius = m_radius;
            context->Unmap(m_constantBuffer.Get(), 0);
        }
    }

    ID3D11Device* m_device = nullptr;
    ComPtr<ID3D11PixelShader> m_boxPS;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;

    int m_radius = 3;
};

} // namespace blurwindow
