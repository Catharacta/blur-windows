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
    // Sample 4 corners at offset distance
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);
    
    float2 halfTexel = texelSize * 0.5f;
    float2 dUV = texelSize * offset;
    
    // Sample pattern: 4 corners
    color += inputTexture.Sample(linearSampler, texcoord + float2(-dUV.x + halfTexel.x, -dUV.y + halfTexel.y));
    color += inputTexture.Sample(linearSampler, texcoord + float2( dUV.x + halfTexel.x, -dUV.y + halfTexel.y));
    color += inputTexture.Sample(linearSampler, texcoord + float2(-dUV.x + halfTexel.x,  dUV.y + halfTexel.y));
    color += inputTexture.Sample(linearSampler, texcoord + float2( dUV.x + halfTexel.x,  dUV.y + halfTexel.y));
    
    float4 blurred = color * 0.25f;
    
    if (isFinalPass > 0.5f) {
        // Get original pixel from t1 (the actual captured image)
        float4 original = originalTexture.Sample(linearSampler, texcoord);
        
        // Apply strength as blend factor between original and blurred
        float4 result = lerp(original, blurred, strength);
        
        // Apply tint using lerp (not additive to prevent white accumulation)
        result.rgb = lerp(result.rgb, tintColor.rgb, tintColor.a);
        
        return result;
    }
    
    return blurred;
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

        // Compile embedded pixel shader
        if (!ShaderLoader::CompilePixelShader(
            device, g_KawaseBlurPS, strlen(g_KawaseBlurPS),
            "main", m_kawasePS.GetAddressOf()
        )) {
            OutputDebugStringA("Failed to compile Kawase blur shader\n");
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

        // Set viewport and common state
        m_fullscreenRenderer->SetViewport(context, width, height);

        for (int i = 0; i < m_iterations; i++) {
            bool isLast = (i == m_iterations - 1);
            
            if (isLast) {
                currentOutput = output;
            } else {
                currentOutput = m_pingPongRTVs[i % 2].Get();
            }

            // Update offset for this iteration (strength is applied in shader, not here)
            float iterationOffset = m_offset + static_cast<float>(i);
            UpdateConstantBuffer(context, width, height, iterationOffset, isLast ? 1.0f : 0.0f);

            // Set shader state
            context->PSSetShader(m_kawasePS.Get(), nullptr, 0);
            context->PSSetShaderResources(0, 1, &currentInput);
            // Bind original image to t1 for final pass strength blending
            if (isLast) {
                context->PSSetShaderResources(1, 1, &input);
            }
            context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
            context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
            context->OMSetRenderTargets(1, &currentOutput, nullptr);

            // Draw fullscreen
            m_fullscreenRenderer->DrawFullscreen(context);

            // Unbind current render target before using it as input
            if (!isLast) {
                ID3D11RenderTargetView* nullRTV = nullptr;
                context->OMSetRenderTargets(1, &nullRTV, nullptr);
                currentInput = m_pingPongSRVs[i % 2].Get();
            }
        }

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

private:
    struct KawaseParams {
        float texelSize[2];
        float offset;
        float isFinalPass;
        float strength;
        float padding[3];
        float tintColor[4];
    };

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

        m_bufferWidth = width;
        m_bufferHeight = height;
    }

    ID3D11Device* m_device = nullptr;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11PixelShader> m_kawasePS;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;

    std::unique_ptr<FullscreenRenderer> m_fullscreenRenderer;

    ComPtr<ID3D11Texture2D> m_pingPongTextures[2];
    ComPtr<ID3D11ShaderResourceView> m_pingPongSRVs[2];
    ComPtr<ID3D11RenderTargetView> m_pingPongRTVs[2];
    uint32_t m_bufferWidth = 0;
    uint32_t m_bufferHeight = 0;

    int m_iterations = 4;
    float m_offset = 1.0f;
    float m_strength = 1.0f;
    float m_tintColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

// Factory function
std::unique_ptr<IBlurEffect> CreateKawaseBlur() {
    return std::make_unique<KawaseBlur>();
}

} // namespace blurwindow
