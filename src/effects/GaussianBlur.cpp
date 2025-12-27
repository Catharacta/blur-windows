#include "IBlurEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>
#include <cstdio> // For snprintf, sscanf
#include <cstring> // For strstr

namespace blurwindow {

// Embedded noise shader
static const char* g_NoisePS = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer NoiseParams : register(b0) {
    float noiseIntensity;
    float noiseScale;
    float time;
    int noiseType;
};

float random(float2 st) {
    return frac(sin(dot(st.xy, float2(12.9898f, 78.233f))) * 43758.5453123f);
}

// Simple Perlin-like 2D noise
float dotGridGradient(int2 i, float2 f) {
    float2 rand = float2(random(float2(i)), random(float2(i) + float2(1.0f, 1.0f)));
    float2 gradient = sin(rand * 6.2831853f + time);
    float2 d = f - float2(i);
    return dot(d, gradient);
}

float perlinNoise(float2 uv) {
    float2 i = floor(uv);
    float2 f = frac(uv);
    float2 u = f * f * (3.0f - 2.0f * f);

    return lerp(lerp(dotGridGradient(i + float2(0, 0), f), 
                     dotGridGradient(i + float2(1, 0), f), u.x),
                lerp(dotGridGradient(i + float2(0, 1), f), 
                     dotGridGradient(i + float2(1, 1), f), u.x), u.y);
}

// Simplex noise inspired implementation
float simplexNoise(float2 uv) {
    float2 i = floor(uv + (uv.x + uv.y) * 0.366025f);
    float2 f0 = uv - (i - (i.x + i.y) * 0.211324f);
    float2 i1 = (f0.x > f0.y) ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f);
    float2 f1 = f0 - i1 + 0.211324f;
    float2 f2 = f0 - 0.57735f;
    float3 p = max(0.5f - float3(dot(f0, f0), dot(f1, f1), dot(f2, f2)), 0.0f);
    float3 n = p * p * p * p * float3(random(i), random(i + i1), random(i + 1.0f));
    return dot(n, float3(1.0f, 1.0f, 1.0f)) * 40.0f;
}

// Voronoi noise for frosted glass effect
float voronoi(float2 uv) {
    float2 n = floor(uv);
    float2 f = frac(uv);
    float m = 8.0f;
    [unroll]
    for (int j = -1; j <= 1; j++) {
        [unroll]
        for (int i = -1; i <= 1; i++) {
            float2 g = float2((float)i, (float)j);
            float2 o = float2(random(n + g), random(n + g + float2(123.4f, 567.8f)));
            o = 0.5f + 0.5f * sin(time + 6.2831f * o);
            float d = distance(g + o, f);
            m = min(m, d);
        }
    }
    return m;
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = inputTexture.Sample(linearSampler, texcoord);
    if (noiseIntensity <= 0) return color;

    float n = 0;
    float2 uv = texcoord * noiseScale;

    if (noiseType == 0) n = random(uv + time) - 0.5f;
    else if (noiseType == 1) { // Sinusoid (Exaggerated)
        float s1 = sin(uv.x * 2.5f + time) * sin(uv.y * 1.8f + time * 0.7f);
        float s2 = sin(uv.x * 0.5f - time * 0.3f) * sin(uv.y * 0.4f + time * 0.2f);
        n = (s1 * 0.7f + s2 * 0.3f) * 2.0f;
    } else if (noiseType == 2) { // Grid (Exaggerated)
        float2 grid = frac(uv * 0.05f);
        float gridLine = step(0.96f, grid.x) + step(0.96f, grid.y);
        n = (gridLine > 0.5f) ? 1.5f : -0.3f;
    } else if (noiseType == 3) n = perlinNoise(uv * 0.3f) * 2.5f;
    else if (noiseType == 4) n = simplexNoise(uv * 0.15f) * 3.5f;
    else if (noiseType == 5) n = (1.0f - voronoi(uv * 0.2f)) * 2.0f - 0.5f;

    color.rgb += n * noiseIntensity;
    return color;
}
)";

// Embedded Gaussian blur horizontal shader
static const char* g_GaussianBlurH = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer BlurParams : register(b0) {
    float2 texelSize;
    float sigma;
    float radius;
};

float GaussianWeight(float x, float s) {
    return exp(-0.5f * (x * x) / (s * s));
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = float4(0, 0, 0, 0);
    float weightSum = 0;
    for (int i = -int(radius); i <= int(radius); i++) {
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
    float radius;
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

// Embedded composite shader (with alpha fix and tinting)
static const char* g_CompositePS = R"(
Texture2D originalTexture : register(t0);
Texture2D blurredTexture : register(t1);
SamplerState linearSampler : register(s0);

cbuffer CompositeParams : register(b0) {
    float strength;
    float3 padding;
    float4 tintColor;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 original = originalTexture.Sample(linearSampler, texcoord);
    float4 blurred = blurredTexture.Sample(linearSampler, texcoord);
    float4 result = lerp(original, blurred, strength);
    result.rgb = lerp(result.rgb, tintColor.rgb, tintColor.a * tintColor.a); // Apply tinting, tintColor.a acts as strength
    result.a = 1.0f; // Force opaque to prevent transparency issues
    return result;
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
        
        if (!ShaderLoader::CompilePixelShader(device, g_NoisePS, strlen(g_NoisePS), "main", m_noisePS.GetAddressOf())) {
            LOG_ERROR("Failed to compile Gaussian Noise shader");
            return false;
        }
        if (!ShaderLoader::CompilePixelShader(device, g_GaussianBlurH, strlen(g_GaussianBlurH), "main", m_horizontalPS.GetAddressOf())) {
            LOG_ERROR("Failed to compile GaussianBlurH shader");
            return false;
        }
        if (!ShaderLoader::CompilePixelShader(device, g_GaussianBlurV, strlen(g_GaussianBlurV), "main", m_verticalPS.GetAddressOf())) {
            LOG_ERROR("Failed to compile GaussianBlurV shader");
            return false;
        }
        if (!ShaderLoader::CompilePixelShader(device, g_CompositePS, strlen(g_CompositePS), "main", m_compositePS.GetAddressOf())) {
            LOG_ERROR("Failed to compile Gaussian Composite shader");
            return false;
        }
        
        if (!m_fullscreenRenderer.Initialize(device)) {
            return false;
        }

        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        cbDesc.ByteWidth = sizeof(NoiseParams);
        HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, m_noiseConstantBuffer.GetAddressOf());
        if (FAILED(hr)) return false;

        cbDesc.ByteWidth = sizeof(BlurParams);
        hr = m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
        if (FAILED(hr)) return false;
        
        cbDesc.ByteWidth = sizeof(CompositeParams);
        hr = m_device->CreateBuffer(&cbDesc, nullptr, m_compositeConstantBuffer.GetAddressOf());
        if (FAILED(hr)) return false;

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
        if (!m_initialized) {
            return false;
        }

        EnsureTextures(width, height);
        
        // 1. Preserve original image for final composition
        CopyInputToOriginal(context, input);

        // Set viewport
        m_fullscreenRenderer.SetViewport(context, width, height);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        ID3D11RenderTargetView* nullRTV = nullptr;

        // ===== Pass 1: Initial Horizontal Blur (Input -> Intermediate) =====
        UpdateConstantBuffer(context, width, height);
        context->PSSetShader(m_horizontalPS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &input);
        context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, m_intermediateRTV.GetAddressOf(), nullptr);
        m_fullscreenRenderer.DrawFullscreen(context);
        context->OMSetRenderTargets(1, &nullRTV, nullptr);
        context->PSSetShaderResources(0, 1, &nullSRV);

        // ===== Pass 2: Initial Vertical Blur (Intermediate -> Blurred) =====
        context->PSSetShader(m_verticalPS.Get(), nullptr, 0);
        ID3D11ShaderResourceView* intermediateSRV = m_intermediateSRV.Get();
        context->PSSetShaderResources(0, 1, &intermediateSRV);
        context->OMSetRenderTargets(1, m_blurredRTV.GetAddressOf(), nullptr);
        m_fullscreenRenderer.DrawFullscreen(context);
        context->OMSetRenderTargets(1, &nullRTV, nullptr);
        context->PSSetShaderResources(0, 1, &nullSRV);

        // ===== Pass 3: Noise Application (Blurred -> Noised) =====
        UpdateNoiseConstantBuffer(context);
        context->PSSetShader(m_noisePS.Get(), nullptr, 0);
        ID3D11ShaderResourceView* blurredSRV = m_blurredSRV.Get();
        context->PSSetShaderResources(0, 1, &blurredSRV);
        context->PSSetConstantBuffers(0, 1, m_noiseConstantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, m_noisedRTV.GetAddressOf(), nullptr);
        m_fullscreenRenderer.DrawFullscreen(context);
        context->OMSetRenderTargets(1, &nullRTV, nullptr);
        context->PSSetShaderResources(0, 1, &nullSRV);

        // ===== Pass 4 & 5: Second Blur (Noised -> Blurred/Final) =====
        // Use a smaller sigma for noise softening (30% of background sigma)
        float noiseSigma = m_sigma * 0.3f;
        UpdateConstantBuffer(context, width, height, noiseSigma);

        context->PSSetShader(m_horizontalPS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, m_noisedSRV.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, m_intermediateRTV.GetAddressOf(), nullptr);
        m_fullscreenRenderer.DrawFullscreen(context);
        context->OMSetRenderTargets(1, &nullRTV, nullptr);
        context->PSSetShaderResources(0, 1, &nullSRV);

        context->PSSetShader(m_verticalPS.Get(), nullptr, 0);
        ID3D11ShaderResourceView* intSRV_final = m_intermediateSRV.Get();
        context->PSSetShaderResources(0, 1, &intSRV_final);
        context->OMSetRenderTargets(1, m_blurredRTV.GetAddressOf(), nullptr);
        m_fullscreenRenderer.DrawFullscreen(context);
        context->OMSetRenderTargets(1, &nullRTV, nullptr);
        context->PSSetShaderResources(0, 1, &nullSRV);

        // ===== Pass 6: Final Composite (Original + Final Blurred -> Output) =====
        UpdateCompositeConstantBuffer(context);
        context->PSSetShader(m_compositePS.Get(), nullptr, 0);
        ID3D11ShaderResourceView* originalSRV = m_originalSRV.Get();
        ID3D11ShaderResourceView* finalBlurredSRV = m_blurredSRV.Get();
        context->PSSetShaderResources(0, 1, &originalSRV);
        context->PSSetShaderResources(1, 1, &finalBlurredSRV);
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

    void SetNoiseIntensity(float intensity) override {
        m_noiseIntensity = std::clamp(intensity, 0.0f, 1.0f);
    }

    void SetNoiseScale(float scale) override {
        m_noiseScale = std::clamp(scale, 1.0f, 1000.0f);
    }

    void SetNoiseSpeed(float speed) override {
        m_noiseSpeed = std::clamp(speed, 0.0f, 100.0f);
    }

    void SetNoiseType(int type) override {
        m_noiseType = std::clamp(type, 0, 5);
    }

    void Update(float deltaTime) override {
        m_currentTime += deltaTime * m_noiseSpeed;
        // Keep time in reasonable range to avoid precision issues
        if (m_currentTime > 10000.0f) {
            m_currentTime = fmod(m_currentTime, 10000.0f);
        }
    }

    bool SetParameters(const char* json) override {
        if (json && strstr(json, "\"param\"")) {
            float val = 0;
            if (sscanf_s(json, "{\"param\": %f}", &val) == 1) {
                m_sigma = std::clamp(val, 0.1f, 50.0f);
                return true;
            }
        }
        return true;
    }

    std::string GetParameters() const override {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "{\"sigma\": %.2f}", m_sigma);
        return buffer;
    }

private:
    // Constant buffer layout (must match shader)
    struct BlurParams {
        float texelSize[2];
        float sigma;
        float radius;
    };

    struct NoiseParams {
        float noiseIntensity;
        float noiseScale;
        float time;
        int noiseType;
    };

    struct CompositeParams {
        float strength;
        float padding[3];
        float tintColor[4];
    };

    void UpdateCompositeConstantBuffer(ID3D11DeviceContext* context) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(context->Map(m_compositeConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            CompositeParams* params = static_cast<CompositeParams*>(mapped.pData);
            params->strength = m_strength;
            params->tintColor[0] = m_tintColor[0];
            params->tintColor[1] = m_tintColor[1];
            params->tintColor[2] = m_tintColor[2];
            params->tintColor[3] = m_tintColor[3];
            context->Unmap(m_compositeConstantBuffer.Get(), 0);
        }
    }

    void UpdateNoiseConstantBuffer(ID3D11DeviceContext* context) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(context->Map(m_noiseConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            NoiseParams* params = static_cast<NoiseParams*>(mapped.pData);
            params->noiseIntensity = m_noiseIntensity;
            params->noiseScale = m_noiseScale;
            params->time = m_currentTime;
            params->noiseType = m_noiseType;
            context->Unmap(m_noiseConstantBuffer.Get(), 0);
        }
    }

    void UpdateConstantBuffer(ID3D11DeviceContext* context, uint32_t width, uint32_t height, float sigmaOverride = -1.0f) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            BlurParams* params = static_cast<BlurParams*>(mapped.pData);
            params->texelSize[0] = 1.0f / static_cast<float>(width);
            params->texelSize[1] = 1.0f / static_cast<float>(height);
            
            float sigma = (sigmaOverride > 0.0f) ? sigmaOverride : m_sigma;
            params->sigma = sigma;
            params->radius = (std::min)(std::ceil(sigma * 3.0f), 32.0f);
            
            context->Unmap(m_constantBuffer.Get(), 0);
        }
    }

    void EnsureTextures(uint32_t w, uint32_t h) {
        if (m_w == w && m_h == h) return;

        m_intermediateTexture.Reset();
        m_intermediateSRV.Reset();
        m_intermediateRTV.Reset();
        m_noisedTexture.Reset();
        m_noisedSRV.Reset();
        m_noisedRTV.Reset();
        m_blurredTexture.Reset();
        m_blurredSRV.Reset();
        m_blurredRTV.Reset();
        m_originalTexture.Reset();
        m_originalSRV.Reset();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w;
        desc.Height = h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // Match capture format
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        m_device->CreateTexture2D(&desc, nullptr, m_intermediateTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_intermediateTexture.Get(), nullptr, m_intermediateSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_intermediateTexture.Get(), nullptr, m_intermediateRTV.GetAddressOf());
        
        m_device->CreateTexture2D(&desc, nullptr, m_noisedTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_noisedTexture.Get(), nullptr, m_noisedSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_noisedTexture.Get(), nullptr, m_noisedRTV.GetAddressOf());

        m_device->CreateTexture2D(&desc, nullptr, m_blurredTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_blurredTexture.Get(), nullptr, m_blurredSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_blurredTexture.Get(), nullptr, m_blurredRTV.GetAddressOf());

        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; // Original only needs SRV
        m_device->CreateTexture2D(&desc, nullptr, m_originalTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_originalTexture.Get(), nullptr, m_originalSRV.GetAddressOf());

        m_w = w;
        m_h = h;
    }
    
    void CopyInputToOriginal(ID3D11DeviceContext* context, ID3D11ShaderResourceView* input) {
        ComPtr<ID3D11Resource> inputResource;
        input->GetResource(inputResource.GetAddressOf());
        context->CopyResource(m_originalTexture.Get(), inputResource.Get());
    }

    ID3D11Device* m_device = nullptr;
    bool m_initialized = false;
    FullscreenRenderer m_fullscreenRenderer;
    
    ComPtr<ID3D11PixelShader> m_noisePS;
    ComPtr<ID3D11PixelShader> m_horizontalPS;
    ComPtr<ID3D11PixelShader> m_verticalPS;
    ComPtr<ID3D11PixelShader> m_compositePS;
    ComPtr<ID3D11Buffer> m_noiseConstantBuffer;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11Buffer> m_compositeConstantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;

    // Textures and their views
    ComPtr<ID3D11Texture2D> m_noisedTexture;
    ComPtr<ID3D11ShaderResourceView> m_noisedSRV;
    ComPtr<ID3D11RenderTargetView> m_noisedRTV;
    
    ComPtr<ID3D11Texture2D> m_intermediateTexture;
    ComPtr<ID3D11ShaderResourceView> m_intermediateSRV;
    ComPtr<ID3D11RenderTargetView> m_intermediateRTV;
    
    ComPtr<ID3D11Texture2D> m_blurredTexture;
    ComPtr<ID3D11ShaderResourceView> m_blurredSRV;
    ComPtr<ID3D11RenderTargetView> m_blurredRTV;
    
    ComPtr<ID3D11Texture2D> m_originalTexture;
    ComPtr<ID3D11ShaderResourceView> m_originalSRV;

    uint32_t m_w = 0;
    uint32_t m_h = 0;
    
    // Blur parameters
    float m_sigma = 5.0f;
    float m_strength = 1.0f;
    float m_tintColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Noise parameters
    float m_noiseIntensity = 0.0f;
    float m_noiseScale = 100.0f;
    float m_noiseSpeed = 1.0f;
    float m_currentTime = 0.0f;
    int m_noiseType = 0;
};

// Factory function
std::unique_ptr<IBlurEffect> CreateGaussianBlur() {
    return std::make_unique<GaussianBlur>();
}

} // namespace blurwindow
