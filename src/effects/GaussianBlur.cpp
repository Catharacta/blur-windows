#include "IBlurEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>

namespace blurwindow {

// Embedded noise shader (applied BEFORE blur)
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
    return frac(sin(dot(st.xy, float2(12.9898, 78.233))) * 43758.5453123);
}

// Simple Perlin-like 2D noise
float dotGridGradient(int2 i, float2 f) {
    float2 rand = float2(random(float2(i)), random(float2(i) + float2(1.0, 1.0)));
    float2 gradient = sin(rand * 6.2831853 + time);
    float2 d = f - float2(i);
    return dot(d, gradient);
}

float perlinNoise(float2 uv) {
    float2 i = floor(uv);
    float2 f = frac(uv);
    float2 u = f * f * (3.0 - 2.0 * f);

    return lerp(lerp(dotGridGradient(i + float2(0, 0), f), 
                     dotGridGradient(i + float2(1, 0), f), u.x),
                lerp(dotGridGradient(i + float2(0, 1), f), 
                     dotGridGradient(i + float2(1, 1), f), u.x), u.y);
}

// Simplex noise inspired implementation
float simplexNoise(float2 uv) {
    float2 i = floor(uv + (uv.x + uv.y) * 0.366025);
    float2 f0 = uv - (i - (i.x + i.y) * 0.211324);
    float2 i1 = (f0.x > f0.y) ? float2(1.0, 0.0) : float2(0.0, 1.0);
    float2 f1 = f0 - i1 + 0.211324;
    float2 f2 = f0 - 0.57735;
    float3 p = max(0.5 - float3(dot(f0, f0), dot(f1, f1), dot(f2, f2)), 0.0);
    float3 n = p * p * p * p * float3(random(i), random(i + i1), random(i + 1.0));
    return dot(n, float3(1.0, 1.0, 1.0)) * 40.0;
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = inputTexture.Sample(linearSampler, texcoord);
    if (noiseIntensity <= 0) return color;

    float n = 0;
    float2 uv = texcoord * noiseScale;

    if (noiseType == 0) { // White
        n = random(uv + time) - 0.5f;
    } else if (noiseType == 1) { // Sinusoid (Exaggerated)
        float s1 = sin(uv.x * 2.5 + time) * sin(uv.y * 1.8 + time * 0.7);
        float s2 = sin(uv.x * 0.5 - time * 0.3) * sin(uv.y * 0.4 + time * 0.2);
        n = (s1 * 0.7 + s2 * 0.3) * 2.0;
    } else if (noiseType == 2) { // Grid (Exaggerated)
        float2 grid = frac(uv * 0.05);
        float line = step(0.96, grid.x) + step(0.96, grid.y);
        n = (line > 0.5) ? 1.5 : -0.3;
    } else if (noiseType == 3) { // Perlin (High Contrast)
        n = perlinNoise(uv * 0.3) * 2.5;
    } else if (noiseType == 4) { // Simplex (High Contrast)
        n = simplexNoise(uv * 0.15) * 3.5;
    }

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

// Embedded composite shader (with alpha fix)
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
    float4 result = lerp(original, blurred, strength);
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
        
        if (!ShaderLoader::CompilePixelShader(
            device, g_NoisePS, strlen(g_NoisePS),
            "main", m_noisePS.GetAddressOf()
        )) {
            OutputDebugStringA("Failed to compile Noise shader\n");
            return false;
        }

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

        // Create constant buffer for noise parameters
        D3D11_BUFFER_DESC noiseCbDesc = {};
        noiseCbDesc.ByteWidth = sizeof(NoiseParams);
        noiseCbDesc.Usage = D3D11_USAGE_DYNAMIC;
        noiseCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        noiseCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        HRESULT hr = m_device->CreateBuffer(&noiseCbDesc, nullptr, m_noiseConstantBuffer.GetAddressOf());
        if (FAILED(hr)) return false;

        // Create constant buffer for blur parameters
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(BlurParams);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        hr = m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
        if (FAILED(hr)) return false;
        
        // Create constant buffer for composite shader
        D3D11_BUFFER_DESC compCbDesc = {};
        compCbDesc.ByteWidth = sizeof(CompositeParams);
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
        if (!m_initialized || !m_noisePS || !m_horizontalPS || !m_verticalPS || !m_compositePS) {
            return false;
        }

        // Ensure textures exist
        EnsureIntermediateTexture(width, height);
        EnsureBlurredTexture(width, height);
        EnsureOriginalTexture(width, height);
        EnsureNoisedTexture(width, height);
        
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
        m_noiseType = std::clamp(type, 0, 4);
    }

    void Update(float deltaTime) override {
        m_currentTime += deltaTime * m_noiseSpeed;
        // Keep time in reasonable range to avoid precision issues
        if (m_currentTime > 10000.0f) {
            m_currentTime = fmod(m_currentTime, 10000.0f);
        }
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
    };

    void UpdateCompositeConstantBuffer(ID3D11DeviceContext* context) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(context->Map(m_compositeConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            CompositeParams* params = static_cast<CompositeParams*>(mapped.pData);
            params->strength = m_strength;
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
            params->radius = (float)static_cast<int>(std::ceil(sigma * 3.0f));
            if (params->radius > 32.0f) params->radius = 32.0f;
            
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
    

    void EnsureNoisedTexture(uint32_t width, uint32_t height) {
        if (m_noisedWidth == width && m_noisedHeight == height) return;
        m_noisedTexture.Reset();
        m_noisedSRV.Reset();
        m_noisedRTV.Reset();
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        m_device->CreateTexture2D(&desc, nullptr, m_noisedTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_noisedTexture.Get(), nullptr, m_noisedSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_noisedTexture.Get(), nullptr, m_noisedRTV.GetAddressOf());
        m_noisedWidth = width;
        m_noisedHeight = height;
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

    // Noised texture
    ComPtr<ID3D11Texture2D> m_noisedTexture;
    ComPtr<ID3D11ShaderResourceView> m_noisedSRV;
    ComPtr<ID3D11RenderTargetView> m_noisedRTV;
    uint32_t m_noisedWidth = 0;
    uint32_t m_noisedHeight = 0;

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
