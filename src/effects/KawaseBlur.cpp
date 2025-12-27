#include "IBlurEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include <algorithm>
#include <memory>

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
        result.rgb = lerp(result.rgb, tintColor.rgb, tintColor.a);
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
    return frac(sin(dot(st.xy, float2(12.9898, 78.233))) * 43758.5453123);
}

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
    if (noiseType == 0) {
        n = random(uv + time) - 0.5f;
    } else if (noiseType == 1) {
        float s1 = sin(uv.x * 2.5 + time) * sin(uv.y * 1.8 + time * 0.7);
        float s2 = sin(uv.x * 0.5 - time * 0.3) * sin(uv.y * 0.4 + time * 0.2);
        n = (s1 * 0.7 + s2 * 0.3) * 2.0;
    } else if (noiseType == 2) {
        float2 grid = frac(uv * 0.05);
        float line = step(0.96, grid.x) + step(0.96, grid.y);
        n = (line > 0.5) ? 1.5 : -0.3;
    } else if (noiseType == 3) {
        n = perlinNoise(uv * 0.3) * 2.5;
    } else if (noiseType == 4) {
        n = simplexNoise(uv * 0.15) * 3.5;
    }
    color.rgb += n * noiseIntensity;
    return color;
}
)";

/// Kawase blur effect (fast iterative blur)
class KawaseBlur : public IBlurEffect {
public:
    KawaseBlur() = default;
    ~KawaseBlur() override = default;

    const char* GetName() const override {
        return "Kawase";
    }

    bool Initialize(ID3D11Device* device) override {
        m_device = device;
        m_device->GetImmediateContext(m_context.GetAddressOf());

        // Compile shaders
        if (!ShaderLoader::CompilePixelShader(
            device, g_KawaseBlurPS, strlen(g_KawaseBlurPS),
            "main", m_kawasePS.GetAddressOf()
        ) || !ShaderLoader::CompilePixelShader(
            device, g_NoisePS, strlen(g_NoisePS),
            "main", m_noisePS.GetAddressOf()
        )) {
            OutputDebugStringA("Failed to compile shaders in KawaseBlur\n");
            return false;
        }

        // Initialize fullscreen renderer
        m_fullscreenRenderer = std::make_unique<FullscreenRenderer>();
        if (!m_fullscreenRenderer->Initialize(device)) {
            return false;
        }

        // Create constant buffer
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(KawaseParams);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
        if (FAILED(hr)) return false;

        cbDesc.ByteWidth = sizeof(NoiseParams);
        hr = m_device->CreateBuffer(&cbDesc, nullptr, m_noiseConstantBuffer.GetAddressOf());
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
        if (!m_kawasePS) {
            return false;
        }

        EnsureBuffers(width, height);

        ID3D11ShaderResourceView* currentInput = input;
        ID3D11RenderTargetView* currentOutput = nullptr;
        m_fullscreenRenderer->SetViewport(context, width, height);

        // ===== Phase 1: Background Blur (Iterations 0 to N-1) =====
        int backgroundIterations = (m_iterations > 1) ? (int)m_iterations - 1 : 1;
        for (int i = 0; i < backgroundIterations; i++) {
            currentOutput = m_pingPongRTVs[i % 2].Get();
            float iterationOffset = m_offset + static_cast<float>(i);
            UpdateConstantBuffer(context, width, height, iterationOffset, 0.0f);

            context->PSSetShader(m_kawasePS.Get(), nullptr, 0);
            context->PSSetShaderResources(0, 1, &currentInput);
            context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
            context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
            context->OMSetRenderTargets(1, &currentOutput, nullptr);
            m_fullscreenRenderer->DrawFullscreen(context);

            ID3D11RenderTargetView* nullRTV = nullptr;
            ID3D11ShaderResourceView* nullSRV = nullptr;
            context->OMSetRenderTargets(1, &nullRTV, nullptr);
            context->PSSetShaderResources(0, 1, &nullSRV);
            currentInput = m_pingPongSRVs[i % 2].Get();
        }

        // ===== Phase 2: Add Noise =====
        UpdateNoiseConstantBuffer(context);
        ID3D11RenderTargetView* noiseOutput = m_noisedRTV.Get();
        context->OMSetRenderTargets(1, &noiseOutput, nullptr);
        context->PSSetShader(m_noisePS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &currentInput);
        context->PSSetConstantBuffers(0, 1, m_noiseConstantBuffer.GetAddressOf());
        m_fullscreenRenderer->DrawFullscreen(context);
         
        ID3D11RenderTargetView* nullRTV = nullptr;
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->OMSetRenderTargets(1, &nullRTV, nullptr);
        context->PSSetShaderResources(0, 1, &nullSRV);
        currentInput = m_noisedSRV.Get();

        // ===== Phase 3: Final Softening & Composite =====
        UpdateConstantBuffer(context, width, height, m_offset, 1.0f);
        context->OMSetRenderTargets(1, &output, nullptr);
        context->PSSetShader(m_kawasePS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &currentInput);
        context->PSSetShaderResources(1, 1, &input); // Original to t1
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        m_fullscreenRenderer->DrawFullscreen(context);

        // Cleanup
        ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
        context->PSSetShaderResources(0, 2, nullSRVs);

        return true;
    }

    bool SetParameters(const char* json) override {
        // TODO: Parse JSON
        return true;
    }

    std::string GetParameters() const override {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), 
            "{\"iterations\": %d, \"offset\": %.2f}",
            m_iterations, m_offset);
        return buffer;
    }

    void SetIterations(int iterations) {
        m_iterations = (std::max)(1, (std::min)(iterations, 8));
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
        if (m_currentTime > 10000.0f) {
            m_currentTime = fmod(m_currentTime, 10000.0f);
        }
    }

private:
    struct KawaseParams {
        float texelSize[2];
        float offset;
        float isFinalPass;
        float strength;
        float tintColor[4];
    };

    struct NoiseParams {
        float noiseIntensity;
        float noiseScale;
        float time;
        int noiseType;
    };

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

    void UpdateConstantBuffer(ID3D11DeviceContext* context, uint32_t width, uint32_t height, float offset, float isFinalPass) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            KawaseParams* params = static_cast<KawaseParams*>(mapped.pData);
            params->texelSize[0] = 1.0f / width;
            params->texelSize[1] = 1.0f / height;
            params->offset = offset;
            params->isFinalPass = isFinalPass;
            params->strength = m_strength;
            params->tintColor[0] = m_tintColor[0];
            params->tintColor[1] = m_tintColor[1];
            params->tintColor[2] = m_tintColor[2];
            params->tintColor[3] = m_tintColor[3];
            context->Unmap(m_constantBuffer.Get(), 0);
        }
    }

    void EnsureBuffers(uint32_t width, uint32_t height) {
        if (m_bufferWidth == width && m_bufferHeight == height) return;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        for (int i = 0; i < 2; i++) {
            m_pingPongTextures[i].Reset();
            m_pingPongSRVs[i].Reset();
            m_pingPongRTVs[i].Reset();

            m_device->CreateTexture2D(&desc, nullptr, m_pingPongTextures[i].GetAddressOf());
            m_device->CreateShaderResourceView(m_pingPongTextures[i].Get(), nullptr, m_pingPongSRVs[i].GetAddressOf());
            m_device->CreateRenderTargetView(m_pingPongTextures[i].Get(), nullptr, m_pingPongRTVs[i].GetAddressOf());
        }

        m_noisedTexture.Reset();
        m_noisedSRV.Reset();
        m_noisedRTV.Reset();
        m_device->CreateTexture2D(&desc, nullptr, m_noisedTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_noisedTexture.Get(), nullptr, m_noisedSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_noisedTexture.Get(), nullptr, m_noisedRTV.GetAddressOf());

        m_bufferWidth = width;
        m_bufferHeight = height;
    }

    ID3D11Device* m_device = nullptr;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11PixelShader> m_kawasePS;
    ComPtr<ID3D11PixelShader> m_noisePS;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11Buffer> m_noiseConstantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;

    std::unique_ptr<FullscreenRenderer> m_fullscreenRenderer;

    ComPtr<ID3D11Texture2D> m_pingPongTextures[2];
    ComPtr<ID3D11ShaderResourceView> m_pingPongSRVs[2];
    ComPtr<ID3D11RenderTargetView> m_pingPongRTVs[2];

    ComPtr<ID3D11Texture2D> m_noisedTexture;
    ComPtr<ID3D11ShaderResourceView> m_noisedSRV;
    ComPtr<ID3D11RenderTargetView> m_noisedRTV;
    uint32_t m_bufferWidth = 0;
    uint32_t m_bufferHeight = 0;

    float m_iterations = 4;
    float m_offset = 1.0f;
    float m_strength = 1.0f;
    float m_tintColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Noise parameters (placeholder for now in Kawase)
    float m_noiseIntensity = 0.0f;
    float m_noiseScale = 100.0f;
    float m_noiseSpeed = 1.0f;
    float m_currentTime = 0.0f;
    int m_noiseType = 0;
};

// Factory function
std::unique_ptr<IBlurEffect> CreateKawaseBlur() {
    return std::make_unique<KawaseBlur>();
}

} // namespace blurwindow
