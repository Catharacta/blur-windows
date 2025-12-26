#include "IBlurEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>

namespace blurwindow {

// Embedded Gaussian blur horizontal shader
static const char* g_GaussianBlurH = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer BlurParams : register(b0) {
    float2 texelSize;
    float sigma;
    float radius;
    float strength;
    float3 padding;
    float4 tintColor;
};

float GaussianWeight(float x, float s) {
    return exp(-0.5f * (x * x) / (s * s));
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = float4(0, 0, 0, 0);
    float weightSum = 0;
    
    // Always use full radius for blur calculation
    for (int i = -int(radius); i <= int(radius); i++) {
        float weight = GaussianWeight(float(i), sigma);
        float2 offset = float2(float(i) * texelSize.x, 0);
        color += inputTexture.Sample(linearSampler, texcoord + offset) * weight;
        weightSum += weight;
    }
    
    float4 blurred = color / weightSum;
    return blurred;
}
)";

// Embedded Gaussian blur vertical shader (pure blur, no composition)
static const char* g_GaussianBlurV = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer BlurParams : register(b0) {
    float2 texelSize;
    float sigma;
    float radius;
    float strength;
    float3 padding;
    float4 tintColor;
};

float GaussianWeight(float x, float s) {
    return exp(-0.5f * (x * x) / (s * s));
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = float4(0, 0, 0, 0);
    float weightSum = 0;
    
    for (int i = -int(radius); i <= int(radius); i++) {
        float weight = GaussianWeight(float(i), sigma);
        float2 offset = float2(0, float(i) * texelSize.y);
        color += inputTexture.Sample(linearSampler, texcoord + offset) * weight;
        weightSum += weight;
    }
    
    return color / weightSum;
}
)";

// Embedded composite shader (blends original with blurred based on strength)
static const char* g_CompositePS = R"(
Texture2D originalTexture : register(t0);
Texture2D blurredTexture : register(t1);
SamplerState linearSampler : register(s0);

cbuffer CompositeParams : register(b0) {
    float strength;
    float3 padding;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 original = originalTexture.Sample(linearSampler, texcoord);
    float4 blurred = blurredTexture.Sample(linearSampler, texcoord);
    
    // strength=0 -> original (no blur), strength=1 -> full blur
    return lerp(original, blurred, strength);
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
        
        // Compile composite shader (for strength blending)
        if (!ShaderLoader::CompilePixelShader(
            device, g_CompositePS, strlen(g_CompositePS),
            "main", m_compositePS.GetAddressOf()
        )) {
            OutputDebugStringA("Failed to compile Composite shader\n");
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
        
        // Create constant buffer for composite shader (strength only)
        D3D11_BUFFER_DESC compCbDesc = {};
        compCbDesc.ByteWidth = 16;  // float strength + float3 padding = 16 bytes
        compCbDesc.Usage = D3D11_USAGE_DYNAMIC;
        compCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        compCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        
        hr = m_device->CreateBuffer(&compCbDesc, nullptr, m_compositeConstantBuffer.GetAddressOf());
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
        if (!m_initialized || !m_horizontalPS || !m_verticalPS || !m_compositePS) {
            return false;
        }

        // Ensure textures exist
        EnsureIntermediateTexture(width, height);
        EnsureBlurredTexture(width, height);
        EnsureOriginalTexture(width, height);
        
        // CRITICAL: Copy input to original texture FIRST before any blur processing
        // This preserves the original image before the GPU pipeline modifies it
        CopyInputToOriginal(context, input);

        // Update blur constant buffer
        UpdateConstantBuffer(context, width, height);

        // Set viewport
        m_fullscreenRenderer.SetViewport(context, width, height);

        // ===== Pass 1: Horizontal blur (input -> intermediate) =====
        context->PSSetShader(m_horizontalPS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &input);
        context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, m_intermediateRTV.GetAddressOf(), nullptr);
        
        m_fullscreenRenderer.DrawFullscreen(context);

        // Unbind resources
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->PSSetShaderResources(0, 1, &nullSRV);

        // ===== Pass 2: Vertical blur (intermediate -> blurred) =====
        context->PSSetShader(m_verticalPS.Get(), nullptr, 0);
        ID3D11ShaderResourceView* intermediateSRV = m_intermediateSRV.Get();
        context->PSSetShaderResources(0, 1, &intermediateSRV);
        context->OMSetRenderTargets(1, m_blurredRTV.GetAddressOf(), nullptr);
        
        m_fullscreenRenderer.DrawFullscreen(context);

        // Unbind resources
        context->PSSetShaderResources(0, 1, &nullSRV);
        ID3D11RenderTargetView* nullRTV = nullptr;
        context->OMSetRenderTargets(1, &nullRTV, nullptr);

        // ===== Pass 3: Composite (original + blurred -> output) =====
        // Update composite constant buffer with strength
        UpdateCompositeConstantBuffer(context);
        
        context->PSSetShader(m_compositePS.Get(), nullptr, 0);
        // t0 = COPIED original, t1 = blurred
        ID3D11ShaderResourceView* originalSRV = m_originalSRV.Get();
        context->PSSetShaderResources(0, 1, &originalSRV);
        ID3D11ShaderResourceView* blurredSRV = m_blurredSRV.Get();
        context->PSSetShaderResources(1, 1, &blurredSRV);
        context->PSSetConstantBuffers(0, 1, m_compositeConstantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, &output, nullptr);
        
        m_fullscreenRenderer.DrawFullscreen(context);

        // Cleanup
        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        context->PSSetShaderResources(0, 2, nullSRVs);
        context->OMSetRenderTargets(1, &nullRTV, nullptr);

        return true;
    }

    void SetStrength(float strength) override {
        m_strength = std::clamp(strength, 0.0f, 1.0f);
    }

    void SetColor(float r, float g, float b, float a) override {
        m_tintColor[0] = r;
        m_tintColor[1] = g;
        m_tintColor[2] = b;
        m_tintColor[3] = a;
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
        m_sigma = (std::max)(0.1f, (std::min)(sigma, 50.0f));
        // Update radius based on sigma (3-sigma rule)
        m_radius = static_cast<int>(std::ceil(m_sigma * 3.0f));
        m_radius = (std::min)(m_radius, 32);  // Max 32 samples per side
    }

    float GetSigma() const { return m_sigma; }
    int GetRadius() const { return m_radius; }

private:
    // Constant buffer layout (must match shader)
    struct BlurParams {
        float texelSize[2];
        float sigma;
        float radius;
        float strength;
        float padding[3];
        float tintColor[4];
    };  // Total: 48 bytes (aligned to 16)

    void UpdateConstantBuffer(ID3D11DeviceContext* context, uint32_t width, uint32_t height) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            BlurParams* params = static_cast<BlurParams*>(mapped.pData);
            params->texelSize[0] = 1.0f / static_cast<float>(width);
            params->texelSize[1] = 1.0f / static_cast<float>(height);
            params->sigma = m_sigma;
            params->radius = m_radius;
            params->strength = m_strength;
            params->tintColor[0] = m_tintColor[0];
            params->tintColor[1] = m_tintColor[1];
            params->tintColor[2] = m_tintColor[2];
            params->tintColor[3] = m_tintColor[3];
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
    
    void EnsureBlurredTexture(uint32_t width, uint32_t height) {
        if (m_blurredWidth == width && m_blurredHeight == height) {
            return;
        }

        m_blurredTexture.Reset();
        m_blurredSRV.Reset();
        m_blurredRTV.Reset();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        m_device->CreateTexture2D(&desc, nullptr, m_blurredTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_blurredTexture.Get(), nullptr, m_blurredSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_blurredTexture.Get(), nullptr, m_blurredRTV.GetAddressOf());

        m_blurredWidth = width;
        m_blurredHeight = height;
    }
    
    void UpdateCompositeConstantBuffer(ID3D11DeviceContext* context) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(m_compositeConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            // CompositeParams: float strength + float3 padding
            float* params = static_cast<float*>(mapped.pData);
            params[0] = m_strength;
            params[1] = 0.0f;  // padding
            params[2] = 0.0f;
            params[3] = 0.0f;
            context->Unmap(m_compositeConstantBuffer.Get(), 0);
        }
    }

    ID3D11Device* m_device = nullptr;
    bool m_initialized = false;
    
    FullscreenRenderer m_fullscreenRenderer;
    
    ComPtr<ID3D11PixelShader> m_horizontalPS;
    ComPtr<ID3D11PixelShader> m_verticalPS;
    ComPtr<ID3D11PixelShader> m_compositePS;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11Buffer> m_compositeConstantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;

    // Intermediate texture for horizontal blur
    ComPtr<ID3D11Texture2D> m_intermediateTexture;
    ComPtr<ID3D11ShaderResourceView> m_intermediateSRV;
    ComPtr<ID3D11RenderTargetView> m_intermediateRTV;
    uint32_t m_intermediateWidth = 0;
    uint32_t m_intermediateHeight = 0;
    
    // Blurred texture (full blur result for composite)
    ComPtr<ID3D11Texture2D> m_blurredTexture;
    ComPtr<ID3D11ShaderResourceView> m_blurredSRV;
    ComPtr<ID3D11RenderTargetView> m_blurredRTV;
    uint32_t m_blurredWidth = 0;
    uint32_t m_blurredHeight = 0;
    
    // Original texture copy (preserved before blur processing)
    ComPtr<ID3D11Texture2D> m_originalTexture;
    ComPtr<ID3D11ShaderResourceView> m_originalSRV;
    uint32_t m_originalWidth = 0;
    uint32_t m_originalHeight = 0;
    
    void EnsureOriginalTexture(uint32_t width, uint32_t height) {
        if (m_originalWidth == width && m_originalHeight == height) {
            return;
        }

        m_originalTexture.Reset();
        m_originalSRV.Reset();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        m_device->CreateTexture2D(&desc, nullptr, m_originalTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_originalTexture.Get(), nullptr, m_originalSRV.GetAddressOf());

        m_originalWidth = width;
        m_originalHeight = height;
    }
    
    void CopyInputToOriginal(ID3D11DeviceContext* context, ID3D11ShaderResourceView* input) {
        ComPtr<ID3D11Resource> inputResource;
        input->GetResource(inputResource.GetAddressOf());
        context->CopyResource(m_originalTexture.Get(), inputResource.Get());
    }

    // Blur parameters
    float m_sigma = 5.0f;
    int m_radius = 15;
    float m_strength = 1.0f;
    float m_tintColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

// Factory function
std::unique_ptr<IBlurEffect> CreateGaussianBlur() {
    return std::make_unique<GaussianBlur>();
}

} // namespace blurwindow
