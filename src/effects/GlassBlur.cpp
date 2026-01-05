#include "GlassBlur.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include <cstdio>
#include <cstring>

namespace blurwindow {

// Glass blur pixel shader - 5-tap cross pattern blur (from RainEffect)
static const char* g_GlassPS = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer GlassParams : register(b0) {
    float strength;
    float blurRadius;
    float2 resolution;
    float4 tintColor;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float2 pixelSize = 1.0 / resolution;
    float radius = blurRadius;
    
    float4 color = float4(0, 0, 0, 0);
    float total = 0.0;
    
    // Sample in a cross pattern for performance (from RainEffect)
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            float2 offset = float2(i, j) * pixelSize * radius * 0.5;
            float weight = 1.0 / (1.0 + abs(i) + abs(j));
            color += inputTexture.Sample(linearSampler, texcoord + offset) * weight;
            total += weight;
        }
    }
    
    float4 blurred = color / total;
    
    // Apply tint color
    float3 tinted = lerp(blurred.rgb, tintColor.rgb, tintColor.a);
    
    // Apply strength (blend with original)
    float4 original = inputTexture.Sample(linearSampler, texcoord);
    float3 result = lerp(original.rgb, tinted, strength);
    
    return float4(result, 1.0);
}
)";

bool GlassBlur::Initialize(ID3D11Device* device) {
    // Compile pixel shader
    ID3D11PixelShader* ps = nullptr;
    if (!ShaderLoader::CompilePixelShader(device, g_GlassPS, strlen(g_GlassPS), "main", &ps)) {
        OutputDebugStringA("GlassBlur: Failed to compile pixel shader\n");
        return false;
    }
    m_glassPS.Attach(ps);
    
    // Create sampler state
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf()))) {
        return false;
    }
    
    // Create constant buffer
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = 32; // strength, blurRadius, resolution(2), tintColor(4)
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&bufferDesc, nullptr, m_constantBuffer.GetAddressOf()))) {
        return false;
    }
    
    OutputDebugStringA("GlassBlur: Initialized successfully\n");
    return true;
}

bool GlassBlur::Apply(ID3D11DeviceContext* context,
                      ID3D11ShaderResourceView* input,
                      ID3D11RenderTargetView* output,
                      uint32_t width, uint32_t height) {
    if (!m_glassPS || !input || !output) return false;
    
    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        struct Params {
            float strength;
            float blurRadius;
            float resolution[2];
            float tintColor[4];
        };
        Params* params = static_cast<Params*>(mapped.pData);
        params->strength = m_strength;
        params->blurRadius = m_blurRadius;
        params->resolution[0] = static_cast<float>(width);
        params->resolution[1] = static_cast<float>(height);
        params->tintColor[0] = m_tintColor[0];
        params->tintColor[1] = m_tintColor[1];
        params->tintColor[2] = m_tintColor[2];
        params->tintColor[3] = m_tintColor[3];
        context->Unmap(m_constantBuffer.Get(), 0);
    }
    
    // Set render target
    context->OMSetRenderTargets(1, &output, nullptr);
    
    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);
    
    // Set shader resources
    context->PSSetShader(m_glassPS.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, &input);
    context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    
    // Draw fullscreen quad
    static FullscreenRenderer renderer;
    static bool initialized = false;
    if (!initialized) {
        ID3D11Device* dev = nullptr;
        context->GetDevice(&dev);
        renderer.Initialize(dev);
        dev->Release();
        initialized = true;
    }
    renderer.DrawFullscreen(context);
    
    // Cleanup
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->PSSetShaderResources(0, 1, &nullSRV);
    
    m_dirty = false;
    return true;
}

void GlassBlur::SetColor(float r, float g, float b, float a) {
    m_tintColor[0] = r;
    m_tintColor[1] = g;
    m_tintColor[2] = b;
    m_tintColor[3] = a;
    m_dirty = true;
}

bool GlassBlur::SetParameters(const char* json) {
    if (!json) return false;
    
    // Parse blur_param for radius
    const char* paramStr = strstr(json, "\"param\"");
    if (paramStr) {
        float param = 0;
        if (sscanf(paramStr, "\"param\": %f", &param) == 1 ||
            sscanf(paramStr, "\"param\":%f", &param) == 1) {
            m_blurRadius = param;
            m_dirty = true;
        }
    }
    return true;
}

std::string GlassBlur::GetParameters() const {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "{\"blurRadius\": %.2f}", m_blurRadius);
    return buffer;
}

void GlassBlur::UpdateConstantBuffer(ID3D11DeviceContext* context) {
    // Already handled in Apply()
}

// Factory function
std::unique_ptr<IBlurEffect> CreateGlassBlur() {
    return std::make_unique<GlassBlur>();
}

} // namespace blurwindow
