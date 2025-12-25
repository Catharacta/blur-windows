#include "IBlurEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include <vector>
#include <cmath>

namespace blurwindow {

// Embedded Gaussian blur horizontal shader
static const char* g_GaussianBlurH = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer BlurParams : register(b0) {
    float2 texelSize;
    float sigma;
    int radius;
};

float GaussianWeight(float x, float s) {
    return exp(-0.5f * (x * x) / (s * s));
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = float4(0, 0, 0, 0);
    float weightSum = 0;
    
    for (int i = -radius; i <= radius; i++) {
        float weight = GaussianWeight(float(i), sigma);
        float2 offset = float2(float(i) * texelSize.x, 0);
        color += inputTexture.Sample(linearSampler, texcoord + offset) * weight;
        weightSum += weight;
    }
    
    return color / weightSum;
}
)";

// Embedded Gaussian blur vertical shader
static const char* g_GaussianBlurV = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer BlurParams : register(b0) {
    float2 texelSize;
    float sigma;
    int radius;
};

float GaussianWeight(float x, float s) {
    return exp(-0.5f * (x * x) / (s * s));
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = float4(0, 0, 0, 0);
    float weightSum = 0;
    
    for (int i = -radius; i <= radius; i++) {
        float weight = GaussianWeight(float(i), sigma);
        float2 offset = float2(0, float(i) * texelSize.y);
        color += inputTexture.Sample(linearSampler, texcoord + offset) * weight;
        weightSum += weight;
    }
    
    return color / weightSum;
}
)";

/// Separable Gaussian blur effect (2-pass)
class GaussianBlur : public IBlurEffect {
public:
    GaussianBlur() = default;
    ~GaussianBlur() override = default;

    const char* GetName() const override {
        return "Gaussian";
    }

    bool Initialize(ID3D11Device* device) override {
        m_device = device;
        
        // Compile embedded shaders
        if (!ShaderLoader::CompilePixelShader(
            device, g_GaussianBlurH, strlen(g_GaussianBlurH),
            "main", m_horizontalPS.GetAddressOf()
        )) {
            OutputDebugStringA("Failed to compile GaussianBlurH shader\n");
            return false;
        }

        if (!ShaderLoader::CompilePixelShader(
            device, g_GaussianBlurV, strlen(g_GaussianBlurV),
            "main", m_verticalPS.GetAddressOf()
        )) {
            OutputDebugStringA("Failed to compile GaussianBlurV shader\n");
            return false;
        }
        
        // Initialize fullscreen renderer
        if (!m_fullscreenRenderer.Initialize(device)) {
            return false;
        }

        // Create constant buffer for blur parameters
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(BlurParams);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
        if (FAILED(hr)) return false;

        // Create sampler state
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

        hr = m_device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf());
        if (FAILED(hr)) return false;

        m_initialized = true;
        return true;
    }

    bool Apply(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* input,
        ID3D11RenderTargetView* output,
        uint32_t width,
        uint32_t height
    ) override {
        if (!m_initialized || !m_horizontalPS || !m_verticalPS) {
            return false;
        }

        // Ensure intermediate texture exists
        EnsureIntermediateTexture(width, height);

        // Update constant buffer
        UpdateConstantBuffer(context, width, height);

        // Set viewport
        m_fullscreenRenderer.SetViewport(context, width, height);

        // Pass 1: Horizontal blur (input -> intermediate)
        context->PSSetShader(m_horizontalPS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &input);
        context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, m_intermediateRTV.GetAddressOf(), nullptr);
        
        m_fullscreenRenderer.DrawFullscreen(context);

        // Unbind intermediate as SRV before using as input
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->PSSetShaderResources(0, 1, &nullSRV);

        // Pass 2: Vertical blur (intermediate -> output)
        context->PSSetShader(m_verticalPS.Get(), nullptr, 0);
        ID3D11ShaderResourceView* intermediateSRV = m_intermediateSRV.Get();
        context->PSSetShaderResources(0, 1, &intermediateSRV);
        context->OMSetRenderTargets(1, &output, nullptr);
        
        m_fullscreenRenderer.DrawFullscreen(context);

        // Cleanup
        context->PSSetShaderResources(0, 1, &nullSRV);
        ID3D11RenderTargetView* nullRTV = nullptr;
        context->OMSetRenderTargets(1, &nullRTV, nullptr);

        return true;
    }

    bool SetParameters(const char* json) override {
        // TODO: Parse JSON
        return true;
    }

    std::string GetParameters() const override {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), 
            "{\"sigma\": %.2f, \"radius\": %d}",
            m_sigma, m_radius);
        return buffer;
    }

    void SetSigma(float sigma) {
        m_sigma = std::max(0.1f, std::min(sigma, 50.0f));
        // Update radius based on sigma (3-sigma rule)
        m_radius = static_cast<int>(std::ceil(m_sigma * 3.0f));
        m_radius = std::min(m_radius, 32);  // Max 32 samples per side
    }

    float GetSigma() const { return m_sigma; }
    int GetRadius() const { return m_radius; }

private:
    // Constant buffer layout (must match shader)
    struct BlurParams {
        float texelSize[2];  // 8 bytes
        float sigma;         // 4 bytes
        int radius;          // 4 bytes
    };  // Total: 16 bytes (aligned)

    void UpdateConstantBuffer(ID3D11DeviceContext* context, uint32_t width, uint32_t height) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            BlurParams* params = static_cast<BlurParams*>(mapped.pData);
            params->texelSize[0] = 1.0f / static_cast<float>(width);
            params->texelSize[1] = 1.0f / static_cast<float>(height);
            params->sigma = m_sigma;
            params->radius = m_radius;
            context->Unmap(m_constantBuffer.Get(), 0);
        }
    }

    void EnsureIntermediateTexture(uint32_t width, uint32_t height) {
        if (m_intermediateWidth == width && m_intermediateHeight == height) {
            return;
        }

        m_intermediateTexture.Reset();
        m_intermediateSRV.Reset();
        m_intermediateRTV.Reset();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // Match capture format
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        m_device->CreateTexture2D(&desc, nullptr, m_intermediateTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_intermediateTexture.Get(), nullptr, m_intermediateSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_intermediateTexture.Get(), nullptr, m_intermediateRTV.GetAddressOf());

        m_intermediateWidth = width;
        m_intermediateHeight = height;
    }

    ID3D11Device* m_device = nullptr;
    bool m_initialized = false;
    
    FullscreenRenderer m_fullscreenRenderer;
    
    ComPtr<ID3D11PixelShader> m_horizontalPS;
    ComPtr<ID3D11PixelShader> m_verticalPS;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;

    // Intermediate texture for 2-pass blur
    ComPtr<ID3D11Texture2D> m_intermediateTexture;
    ComPtr<ID3D11ShaderResourceView> m_intermediateSRV;
    ComPtr<ID3D11RenderTargetView> m_intermediateRTV;
    uint32_t m_intermediateWidth = 0;
    uint32_t m_intermediateHeight = 0;

    // Blur parameters
    float m_sigma = 5.0f;
    int m_radius = 15;
};

} // namespace blurwindow
