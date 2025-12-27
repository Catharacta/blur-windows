#include "IBlurEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include <algorithm>
#include <memory>

namespace blurwindow {

// Box blur shader (HLSL embedded)
static const char* g_BoxBlurPS = R"(
Texture2D inputTexture : register(t0);
Texture2D originalTexture : register(t1);
SamplerState linearSampler : register(s0);

cbuffer BoxParams : register(b0) {
    float2 texelSize;
    int radius;
    float strength;
    float noiseIntensity;
    float noiseScale;
    float time;
    float padding; // Alignment
    float4 tintColor;
};

float random(float2 st) {
    return frac(sin(dot(st.xy, float2(12.9898, 78.233))) * 43758.5453123);
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float count = 0.0f;
    
    // Always use full radius for blur calculation
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            float2 offset = float2(float(x), float(y)) * texelSize;
            color += inputTexture.Sample(linearSampler, texcoord + offset);
            count += 1.0f;
        }
    }
    
    float4 blurred = color / count;
    
    // Get original pixel from t1 (same as input for single-pass)
    float4 original = originalTexture.Sample(linearSampler, texcoord);
    
    // Apply strength as blend factor between original and blurred
    float4 result = lerp(original, blurred, strength);
    
    // Apply tint using lerp (not additive to prevent white accumulation)
    result.rgb = lerp(result.rgb, tintColor.rgb, tintColor.a);
    
    return result;
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
        cbDesc.ByteWidth = sizeof(BoxParams);
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
        // Bind input also to t1 for original texture reference
        context->PSSetShaderResources(1, 1, &input);
        context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, &output, nullptr);

        // Draw fullscreen
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
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "{\"radius\": %d}", m_radius);
        return buffer;
    }

    void SetRadius(int radius) {
        m_radius = (std::max)(1, (std::min)(radius, 10));
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
    void SetNoiseSpeed(float speed) override {}
    void SetNoiseType(int type) override {}
    void Update(float deltaTime) override {}

private:
    struct BoxParams {
        float texelSize[2];
        int radius;
        float strength;
        float noiseIntensity;
        float noiseScale;
        float time;
        float padding;
        float tintColor[4];
    };

    void UpdateConstantBuffer(ID3D11DeviceContext* context, uint32_t width, uint32_t height) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            BoxParams* params = static_cast<BoxParams*>(mapped.pData);
            params->texelSize[0] = 1.0f / (float)width;
            params->texelSize[1] = 1.0f / (float)height;
            params->radius = m_radius;
            params->strength = m_strength;
            params->tintColor[0] = m_tintColor[0];
            params->tintColor[1] = m_tintColor[1];
            params->tintColor[2] = m_tintColor[2];
            params->tintColor[3] = m_tintColor[3];
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
    float m_strength = 1.0f;
    float m_tintColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Noise parameters
    float m_noiseIntensity = 0.0f;
    float m_noiseScale = 100.0f;
    float m_noiseSpeed = 1.0f;
    float m_currentTime = 0.0f;
};

// Factory function
std::unique_ptr<IBlurEffect> CreateBoxBlur() {
    return std::make_unique<BoxBlur>();
}

} // namespace blurwindow
