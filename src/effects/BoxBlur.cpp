#include "IBlurEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include <algorithm>
#include <memory>

namespace blurwindow {

// Box blur shader (HLSL embedded)
static const char* g_BoxBlurPS = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer BoxParams : register(b0) {
    float2 texelSize;
    int radius;
    float padding;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float count = 0.0f;
    
    // Simple box kernel sampling
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            float2 offset = float2(float(x), float(y)) * texelSize;
            color += inputTexture.Sample(linearSampler, texcoord + offset);
            count += 1.0f;
        }
    }
    
    return color / count;
}
)";

/// Simple box blur effect (lightweight)
class BoxBlur : public IBlurEffect {
public:
    BoxBlur() = default;
    ~BoxBlur() override = default;

    const char* GetName() const override {
        return "Box";
    }

    bool Initialize(ID3D11Device* device) override {
        m_device = device;
        m_device->GetImmediateContext(m_context.GetAddressOf());

        // Compile embedded pixel shader
        if (!ShaderLoader::CompilePixelShader(
            device, g_BoxBlurPS, strlen(g_BoxBlurPS),
            "main", m_boxPS.GetAddressOf()
        )) {
            OutputDebugStringA("Failed to compile Box blur shader\n");
            return false;
        }

        // Initialize fullscreen renderer
        m_fullscreenRenderer = std::make_unique<FullscreenRenderer>();
        if (!m_fullscreenRenderer->Initialize(device)) {
            return false;
        }

        // Create constant buffer (16-byte aligned)
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = 16;  // float2 + int + float padding
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
        if (FAILED(hr)) return false;

        // Create sampler
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
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
            return false;
        }

        // Update constant buffer
        UpdateConstantBuffer(context, width, height);

        // Set viewport and common state
        m_fullscreenRenderer->SetViewport(context, width, height);

        // Set shader state
        context->PSSetShader(m_boxPS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &input);
        context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, &output, nullptr);

        // Draw fullscreen
        m_fullscreenRenderer->DrawFullscreen(context);

        // Cleanup
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->PSSetShaderResources(0, 1, &nullSRV);

        return true;
    }

    bool SetParameters(const char* json) override {
        // TODO: Parse JSON
        return true;
    }

    std::string GetParameters() const override {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "{\"radius\": %d}", m_radius);
        return buffer;
    }

    void SetRadius(int radius) {
        m_radius = (std::max)(1, (std::min)(radius, 10));
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
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11PixelShader> m_boxPS;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;

    std::unique_ptr<FullscreenRenderer> m_fullscreenRenderer;

    int m_radius = 3;
};

// Factory function
std::unique_ptr<IBlurEffect> CreateBoxBlur() {
    return std::make_unique<BoxBlur>();
}

} // namespace blurwindow
