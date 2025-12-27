#include "IBlurEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>
#include <cstdio>
#include <cstring>

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

// Radial Blur Shader
static const char* g_RadialBlurPS = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer RadialBlurParams : register(b0) {
    float2 center;
    float blurAmount;
    float radius;
    float samples;
    float3 padding;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = float4(0, 0, 0, 0);
    float2 dir = texcoord - center;
    float dist = length(dir);
    float amount = blurAmount * saturate(dist / radius);
    int numSamples = (int)samples;
    for (int i = 0; i < numSamples; i++) {
        float scale = 1.0f - amount * (float(i) / float(numSamples - 1));
        color += inputTexture.Sample(linearSampler, center + dir * scale);
    }
    return color / float(numSamples);
}
)";

// Final Composite Shader
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
    result.rgb = lerp(result.rgb, tintColor.rgb, tintColor.a * tintColor.a);
    result.a = 1.0f;
    return result;
}
)";

class RadialBlur : public IBlurEffect {
public:
    RadialBlur() = default;
    ~RadialBlur() override = default;

    const char* GetName() const override { return "Radial"; }

    bool Initialize(ID3D11Device* device) override {
        m_device = device;
        if (!ShaderLoader::CompilePixelShader(device, g_NoisePS, strlen(g_NoisePS), "main", m_noisePS.GetAddressOf())) return false;
        if (!ShaderLoader::CompilePixelShader(device, g_RadialBlurPS, strlen(g_RadialBlurPS), "main", m_radialPS.GetAddressOf())) return false;
        if (!ShaderLoader::CompilePixelShader(device, g_CompositePS, strlen(g_CompositePS), "main", m_compositePS.GetAddressOf())) return false;

        if (!m_fullscreenRenderer.Initialize(device)) return false;

        D3D11_BUFFER_DESC cbDesc = { 0 };
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        cbDesc.ByteWidth = sizeof(NoiseParams);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_noiseConstantBuffer.GetAddressOf()))) return false;
        cbDesc.ByteWidth = sizeof(RadialParams);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf()))) return false;
        cbDesc.ByteWidth = sizeof(CompositeParams);
        if (FAILED(m_device->CreateBuffer(&cbDesc, nullptr, m_compositeConstantBuffer.GetAddressOf()))) return false;

        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        if (FAILED(m_device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf()))) return false;

        m_initialized = true;
        return true;
    }

    bool Apply(ID3D11DeviceContext* context, ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output, uint32_t width, uint32_t height) override {
        if (!m_initialized) return false;
        EnsureTextures(width, height);
        CopyInputToOriginal(context, input);
        m_fullscreenRenderer.SetViewport(context, width, height);

        // Pass 1: Radial Blur
        UpdateConstantBuffer(context, m_blurAmount);
        context->PSSetShader(m_radialPS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &input);
        context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, m_intermediateRTV.GetAddressOf(), nullptr);
        m_fullscreenRenderer.DrawFullscreen(context);

        // Pass 2: Noise
        UpdateNoiseConstantBuffer(context);
        context->PSSetShader(m_noisePS.Get(), nullptr, 0);
        ID3D11ShaderResourceView* intermediateSRV = m_intermediateSRV.Get();
        context->PSSetShaderResources(0, 1, &intermediateSRV);
        context->PSSetConstantBuffers(0, 1, m_noiseConstantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, m_noisedRTV.GetAddressOf(), nullptr);
        m_fullscreenRenderer.DrawFullscreen(context);

        // Pass 3: Soften
        UpdateConstantBuffer(context, m_blurAmount * 0.3f);
        context->PSSetShader(m_radialPS.Get(), nullptr, 0);
        ID3D11ShaderResourceView* noisedSRV = m_noisedSRV.Get();
        context->PSSetShaderResources(0, 1, &noisedSRV);
        context->OMSetRenderTargets(1, m_blurredRTV.GetAddressOf(), nullptr);
        m_fullscreenRenderer.DrawFullscreen(context);

        // Pass 4: Composite
        UpdateCompositeConstantBuffer(context);
        context->PSSetShader(m_compositePS.Get(), nullptr, 0);
        ID3D11ShaderResourceView* originalSRV = m_originalSRV.Get();
        ID3D11ShaderResourceView* finalBlurredSRV = m_blurredSRV.Get();
        context->PSSetShaderResources(0, 1, &originalSRV);
        context->PSSetShaderResources(1, 1, &finalBlurredSRV);
        context->PSSetConstantBuffers(0, 1, m_compositeConstantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, &output, nullptr);
        m_fullscreenRenderer.DrawFullscreen(context);

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
        if (json && strstr(json, "\"param\"")) {
            float val = 0;
            if (sscanf_s(json, "{\"param\": %f}", &val) == 1) {
                m_blurAmount = std::clamp(val, 0.01f, 0.5f);
                return true;
            }
        }
        return true;
    }
    std::string GetParameters() const override { char buffer[64]; snprintf(buffer, sizeof(buffer), "{\"amount\": %.4f}", m_blurAmount); return buffer; }

private:
    struct NoiseParams { float noiseIntensity, noiseScale, time; int noiseType; };
    struct RadialParams { float center[2]; float blurAmount, radius, samples; float padding[3]; };
    struct CompositeParams { float strength; float padding[3]; float tintColor[4]; };

    void UpdateNoiseConstantBuffer(ID3D11DeviceContext* ctx) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(m_noiseConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            NoiseParams* p = (NoiseParams*)m.pData;
            p->noiseIntensity = m_noiseIntensity; p->noiseScale = m_noiseScale; p->time = m_currentTime; p->noiseType = m_noiseType;
            ctx->Unmap(m_noiseConstantBuffer.Get(), 0);
        }
    }
    void UpdateConstantBuffer(ID3D11DeviceContext* ctx, float amount) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            RadialParams* p = (RadialParams*)m.pData;
            p->center[0] = 0.5f; p->center[1] = 0.5f; p->blurAmount = amount; p->radius = 1.0f; p->samples = 16.0f;
            ctx->Unmap(m_constantBuffer.Get(), 0);
        }
    }
    void UpdateCompositeConstantBuffer(ID3D11DeviceContext* ctx) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx->Map(m_compositeConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            CompositeParams* p = (CompositeParams*)m.pData;
            p->strength = m_strength; memcpy(p->tintColor, m_tintColor, sizeof(m_tintColor));
            ctx->Unmap(m_compositeConstantBuffer.Get(), 0);
        }
    }

    void EnsureTextures(uint32_t w, uint32_t h) {
        if (m_w == w && m_h == h) return;
        D3D11_TEXTURE2D_DESC d = {}; d.Width = w; d.Height = h; d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        m_intermediateTexture.Reset(); m_noisedTexture.Reset(); m_blurredTexture.Reset(); m_originalTexture.Reset();
        m_device->CreateTexture2D(&d, nullptr, m_intermediateTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_intermediateTexture.Get(), nullptr, m_intermediateSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_intermediateTexture.Get(), nullptr, m_intermediateRTV.GetAddressOf());
        m_device->CreateTexture2D(&d, nullptr, m_noisedTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_noisedTexture.Get(), nullptr, m_noisedSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_noisedTexture.Get(), nullptr, m_noisedRTV.GetAddressOf());
        m_device->CreateTexture2D(&d, nullptr, m_blurredTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_blurredTexture.Get(), nullptr, m_blurredSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_blurredTexture.Get(), nullptr, m_blurredRTV.GetAddressOf());
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        m_device->CreateTexture2D(&d, nullptr, m_originalTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_originalTexture.Get(), nullptr, m_originalSRV.GetAddressOf());
        m_w = w; m_h = h;
    }

    void CopyInputToOriginal(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* input) {
        ComPtr<ID3D11Resource> res; input->GetResource(res.GetAddressOf());
        ctx->CopyResource(m_originalTexture.Get(), res.Get());
    }

    ID3D11Device* m_device = nullptr;
    bool m_initialized = false;
    FullscreenRenderer m_fullscreenRenderer;
    ComPtr<ID3D11PixelShader> m_noisePS, m_radialPS, m_compositePS;
    ComPtr<ID3D11Buffer> m_noiseConstantBuffer, m_constantBuffer, m_compositeConstantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11Texture2D> m_intermediateTexture, m_noisedTexture, m_blurredTexture, m_originalTexture;
    ComPtr<ID3D11ShaderResourceView> m_intermediateSRV, m_noisedSRV, m_blurredSRV, m_originalSRV;
    ComPtr<ID3D11RenderTargetView> m_intermediateRTV, m_noisedRTV, m_blurredRTV;
    uint32_t m_w = 0, m_h = 0;
    float m_strength = 1.0f, m_blurAmount = 0.15f, m_tintColor[4] = {0}, m_noiseIntensity = 0, m_noiseScale = 100, m_noiseSpeed = 1, m_currentTime = 0;
    int m_noiseType = 0;
};

std::unique_ptr<IBlurEffect> CreateRadialBlur() { return std::make_unique<RadialBlur>(); }

} // namespace blurwindow
