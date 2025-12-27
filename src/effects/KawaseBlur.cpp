#include "IBlurEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include <algorithm>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace blurwindow {

// Kawase blur shader (HLSL embedded)
static const char* g_KawaseBlurPS = R"(
Texture2D inputTexture : register(t0);
Texture2D originalTexture : register(t1);
SamplerState linearSampler : register(s0);

cbuffer KawaseParams : register(b0) {
    float2 texelSize;
    float offset;
    float isFinalPass;
    float strength;
    float3 padding;
    float4 tintColor;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float2 halfTexel = texelSize * 0.5f;
    float2 dUV = texelSize * offset;
    
    color += inputTexture.Sample(linearSampler, texcoord + float2(-dUV.x + halfTexel.x, -dUV.y + halfTexel.y));
    color += inputTexture.Sample(linearSampler, texcoord + float2( dUV.x + halfTexel.x, -dUV.y + halfTexel.y));
    color += inputTexture.Sample(linearSampler, texcoord + float2(-dUV.x + halfTexel.x,  dUV.y + halfTexel.y));
    color += inputTexture.Sample(linearSampler, texcoord + float2( dUV.x + halfTexel.x,  dUV.y + halfTexel.y));
    
    float4 blurred = color * 0.25f;
    
    if (isFinalPass > 0.5f) {
        float4 original = originalTexture.Sample(linearSampler, texcoord);
        float4 result = lerp(original, blurred, strength);
        result.rgb = lerp(result.rgb, tintColor.rgb, tintColor.a * tintColor.a);
        result.a = 1.0f; // Force opaque
        return result;
    }
    return blurred;
}
)";

// Dedicated Noise Pixel Shader
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
    else if (noiseType == 1) {
        float s1 = sin(uv.x * 2.5f + time) * sin(uv.y * 1.8f + time * 0.7f);
        float s2 = sin(uv.x * 0.5f - time * 0.3f) * sin(uv.y * 0.4f + time * 0.2f);
        n = (s1 * 0.7f + s2 * 0.3f) * 2.0f;
    } else if (noiseType == 2) {
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

/// Kawase blur effect (fast iterative blur)
class KawaseBlur : public IBlurEffect {
public:
    KawaseBlur() = default;
    ~KawaseBlur() override = default;

    const char* GetName() const override { return "Kawase"; }

    bool Initialize(ID3D11Device* device) override {
        m_device = device;
        if (!ShaderLoader::CompilePixelShader(device, g_KawaseBlurPS, strlen(g_KawaseBlurPS), "main", m_kawasePS.GetAddressOf())) {
            LOG_ERROR("Failed to compile KawaseBlur shader");
            return false;
        }
        if (!ShaderLoader::CompilePixelShader(device, g_NoisePS, strlen(g_NoisePS), "main", m_noisePS.GetAddressOf())) {
            LOG_ERROR("Failed to compile Kawase Noise shader");
            return false;
        }
        
        if (!m_fullscreenRenderer.Initialize(device)) {
            return false;
        }

        D3D11_BUFFER_DESC cbDesc = { 0 };
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        cbDesc.ByteWidth = sizeof(KawaseParams);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf()))) return false;
        cbDesc.ByteWidth = sizeof(NoiseParams);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_noiseConstantBuffer.GetAddressOf()))) return false;

        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(m_device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf()))) return false;

        m_initialized = true;
        return true;
    }

    bool Apply(ID3D11DeviceContext* context, ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output, uint32_t width, uint32_t height) override {
        if (!m_initialized) return false;
        EnsureBuffers(width, height);
        m_fullscreenRenderer.SetViewport(context, width, height);

        ID3D11ShaderResourceView* currentInput = input;
        ID3D11RenderTargetView* currentOutput = nullptr;

        // Pass 1: Iterative Blur
        int blurIterations = (int)m_iterations;
        for (int i = 0; i < blurIterations; i++) {
            currentOutput = m_pingPongRTVs[i % 2].Get();
            float iterationOffset = m_offset + static_cast<float>(i);
            UpdateConstantBuffer(context, width, height, iterationOffset, 0.0f);

            context->PSSetShader(m_kawasePS.Get(), nullptr, 0);
            context->PSSetShaderResources(0, 1, &currentInput);
            context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
            context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
            context->OMSetRenderTargets(1, &currentOutput, nullptr);
            m_fullscreenRenderer.DrawFullscreen(context);

            ID3D11RenderTargetView* nullRTV = nullptr;
            ID3D11ShaderResourceView* nullSRV = nullptr;
            context->OMSetRenderTargets(1, &nullRTV, nullptr);
            context->PSSetShaderResources(0, 1, &nullSRV);
            currentInput = m_pingPongSRVs[i % 2].Get();
        }

        // Pass 2: Noise
        UpdateNoiseConstantBuffer(context);
        ID3D11RenderTargetView* noiseOutput = m_noisedRTV.Get();
        context->OMSetRenderTargets(1, &noiseOutput, nullptr);
        context->PSSetShader(m_noisePS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &currentInput);
        context->PSSetConstantBuffers(0, 1, m_noiseConstantBuffer.GetAddressOf());
        m_fullscreenRenderer.DrawFullscreen(context);
         
        ID3D11RenderTargetView* nullRTV = nullptr;
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->OMSetRenderTargets(1, &nullRTV, nullptr);
        context->PSSetShaderResources(0, 1, &nullSRV);
        currentInput = m_noisedSRV.Get();

        // Pass 3: Final Composite
        UpdateConstantBuffer(context, width, height, m_offset, 1.0f);
        context->OMSetRenderTargets(1, &output, nullptr);
        context->PSSetShader(m_kawasePS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &currentInput);
        context->PSSetShaderResources(1, 1, &input); // Original to t1
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        m_fullscreenRenderer.DrawFullscreen(context);

        // Cleanup
        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        context->PSSetShaderResources(0, 2, nullSRVs);
        return true;
    }

    void SetStrength(float strength) override { m_strength = std::clamp(strength, 0.0f, 1.0f); }
    void SetColor(float r, float g, float b, float a) override { m_tintColor[0] = r; m_tintColor[1] = g; m_tintColor[2] = b; m_tintColor[3] = a; }
    void SetNoiseIntensity(float intensity) override { m_noiseIntensity = std::clamp(intensity, 0.0f, 1.0f); }
    void SetNoiseScale(float scale) override { m_noiseScale = std::clamp(scale, 1.0f, 1000.0f); }
    void SetNoiseSpeed(float speed) override { m_noiseSpeed = std::clamp(speed, 0.0f, 100.0f); }
    void SetNoiseType(int type) override { m_noiseType = std::clamp(type, 0, 5); }
    void Update(float deltaTime) override { 
        m_currentTime += deltaTime * m_noiseSpeed;
        if (m_currentTime > 10000.0f) m_currentTime = fmod(m_currentTime, 10000.0f);
    }
    bool SetParameters(const char* json) override { 
        if (json) {
            float val = 0;
            if (strstr(json, "\"iterations\"") && sscanf_s(json, "{\"iterations\": %f}", &val) == 1) {
                m_iterations = std::clamp(val, 1.0f, 10.0f);
                return true;
            }
            if (strstr(json, "\"param\"") && sscanf_s(json, "{\"param\": %f}", &val) == 1) {
                m_iterations = std::clamp(val, 1.0f, 10.0f);
                return true;
            }
        }
        return true; 
    }
    std::string GetParameters() const override { 
        char buffer[64]; snprintf(buffer, sizeof(buffer), "{\"iterations\": %d}", (int)m_iterations); return buffer; 
    }

private:
    struct KawaseParams { float texelSize[2]; float offset; float isFinalPass; float strength; float padding[3]; float tintColor[4]; };
    struct NoiseParams { float noiseIntensity; float noiseScale; float time; int noiseType; };

    void UpdateConstantBuffer(ID3D11DeviceContext* ctx, uint32_t w, uint32_t h, float offset, float isFinal) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            KawaseParams* p = (KawaseParams*)m.pData;
            p->texelSize[0] = 1.0f / w; p->texelSize[1] = 1.0f / h;
            p->offset = offset; p->isFinalPass = isFinal; p->strength = m_strength;
            memcpy(p->tintColor, m_tintColor, sizeof(m_tintColor));
            ctx->Unmap(m_constantBuffer.Get(), 0);
        }
    }
    void UpdateNoiseConstantBuffer(ID3D11DeviceContext* ctx) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(m_noiseConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            NoiseParams* p = (NoiseParams*)m.pData;
            p->noiseIntensity = m_noiseIntensity; p->noiseScale = m_noiseScale; p->time = m_currentTime; p->noiseType = m_noiseType;
            ctx->Unmap(m_noiseConstantBuffer.Get(), 0);
        }
    }

    void EnsureBuffers(uint32_t w, uint32_t h) {
        if (m_w == w && m_h == h) return;
        D3D11_TEXTURE2D_DESC d = {}; d.Width = w; d.Height = h; d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        for (int i = 0; i < 2; i++) {
            m_pingPongTextures[i].Reset();
            m_device->CreateTexture2D(&d, nullptr, m_pingPongTextures[i].GetAddressOf());
            m_device->CreateShaderResourceView(m_pingPongTextures[i].Get(), nullptr, m_pingPongSRVs[i].GetAddressOf());
            m_device->CreateRenderTargetView(m_pingPongTextures[i].Get(), nullptr, m_pingPongRTVs[i].GetAddressOf());
        }
        m_noisedTexture.Reset();
        m_device->CreateTexture2D(&d, nullptr, m_noisedTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_noisedTexture.Get(), nullptr, m_noisedSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_noisedTexture.Get(), nullptr, m_noisedRTV.GetAddressOf());
        m_w = w; m_h = h;
    }

    ID3D11Device* m_device = nullptr;
    bool m_initialized = false;
    FullscreenRenderer m_fullscreenRenderer;
    ComPtr<ID3D11PixelShader> m_kawasePS, m_noisePS;
    ComPtr<ID3D11Buffer> m_constantBuffer, m_noiseConstantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11Texture2D> m_pingPongTextures[2], m_noisedTexture;
    ComPtr<ID3D11ShaderResourceView> m_pingPongSRVs[2], m_noisedSRV;
    ComPtr<ID3D11RenderTargetView> m_pingPongRTVs[2], m_noisedRTV;
    uint32_t m_w = 0, m_h = 0;
    float m_iterations = 4, m_offset = 1.0f, m_strength = 1.0f, m_tintColor[4] = {0}, m_noiseIntensity = 0, m_noiseScale = 100, m_noiseSpeed = 1, m_currentTime = 0;
    int m_noiseType = 0;
};

// Factory function
std::unique_ptr<IBlurEffect> CreateKawaseBlur() { return std::make_unique<KawaseBlur>(); }

} // namespace blurwindow
