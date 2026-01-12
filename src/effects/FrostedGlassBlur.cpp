#include "FrostedGlassBlur.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include "../core/Logger.h"
#include <cstdio>
#include <cstring>

namespace blurwindow {

// Frosted glass pixel shader with Voronoi distortion
static const char* g_FrostedGlassPS = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer FrostedParams : register(b0) {
    float strength;
    float blurRadius;
    float distortionStrength;
    float cellScale;
    float2 resolution;
    float time;
    float opacity;
    float4 tintColor;
};

// Simple hash function for Voronoi
float2 hash2(float2 p) {
    p = float2(dot(p, float2(127.1, 311.7)),
               dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

// Voronoi noise - returns distance to nearest cell and offset vector
float2 voronoi(float2 uv) {
    float2 n = floor(uv);
    float2 f = frac(uv);
    
    float2 minOffset = float2(0, 0);
    float minDist = 8.0;
    
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            float2 g = float2(i, j);
            float2 o = hash2(n + g);
            // Animate the cell centers slightly
            o = 0.5 + 0.4 * sin(time * 0.5 + 6.2831 * o);
            
            float2 r = g + o - f;
            float d = dot(r, r);
            
            if (d < minDist) {
                minDist = d;
                minOffset = r;
            }
        }
    }
    
    return minOffset;
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float2 pixelSize = 1.0 / resolution;
    
    // Get Voronoi-based distortion
    float2 cellUV = texcoord * cellScale;
    float2 voronoiOffset = voronoi(cellUV);
    
    // Apply distortion to UV coordinates
    float2 distortedUV = texcoord + voronoiOffset * distortionStrength;
    distortedUV = clamp(distortedUV, 0.0, 1.0);
    
    // Multi-tap blur at the distorted position
    float4 color = float4(0, 0, 0, 0);
    float total = 0.0;
    float radius = blurRadius;
    
    // 5x5 Gaussian-weighted blur
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            float2 offset = float2(i, j) * pixelSize * radius * 0.4;
            float weight = exp(-0.5 * (i*i + j*j) / 2.0);
            color += inputTexture.Sample(linearSampler, distortedUV + offset) * weight;
            total += weight;
        }
    }
    
    float4 blurred = color / total;
    
    // Apply tint color
    float3 tinted = lerp(blurred.rgb, tintColor.rgb, tintColor.a);
    
    // Apply strength (blend with original)
    float4 original = inputTexture.Sample(linearSampler, texcoord);
    float3 result = lerp(original.rgb, tinted, strength);
    
    return float4(result, opacity);
}
)";

bool FrostedGlassBlur::Initialize(ID3D11Device* device) {
    // Compile pixel shader
    ID3D11PixelShader* ps = nullptr;
    if (!ShaderLoader::CompilePixelShader(device, g_FrostedGlassPS, strlen(g_FrostedGlassPS), "main", &ps)) {
        OutputDebugStringA("FrostedGlassBlur: Failed to compile pixel shader\n");
        return false;
    }
    m_frostedPS.Attach(ps);
    
    // Create sampler state
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf()))) {
        return false;
    }
    
    // Create constant buffer (48 bytes aligned to 16)
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = 48;
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&bufferDesc, nullptr, m_constantBuffer.GetAddressOf()))) {
        return false;
    }
    
    OutputDebugStringA("FrostedGlassBlur: Initialized successfully\n");
    return true;
}

bool FrostedGlassBlur::Apply(ID3D11DeviceContext* context,
                              ID3D11ShaderResourceView* input,
                              ID3D11RenderTargetView* output,
                              uint32_t width, uint32_t height) {
    if (!m_frostedPS || !input || !output) return false;
    
    // Diagnostic log for effect parameters (throttled)
    static int frameCount = 0;
    if (frameCount++ % 300 == 0) {
        LOG_INFO("FrostedGlassBlur::Apply: strength=%.2f, distortion=%.3f, cellScale=%.1f",
                 m_strength, m_distortionStrength, m_cellScale);
    }
    
    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        struct Params {
            float strength;
            float blurRadius;
            float distortionStrength;
            float cellScale;
            float resolution[2];
            float time;
            float opacity;
            float tintColor[4];
        };
        Params* params = static_cast<Params*>(mapped.pData);
        params->strength = m_strength;
        params->blurRadius = m_blurRadius;
        params->distortionStrength = m_distortionStrength;
        params->cellScale = m_cellScale;
        params->resolution[0] = static_cast<float>(width);
        params->resolution[1] = static_cast<float>(height);
        params->time = m_time;
        params->opacity = m_opacity;
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
    context->PSSetShader(m_frostedPS.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, &input);
    context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    
    // Draw fullscreen quad (using per-instance renderer to avoid cross-device issues)
    if (!m_rendererInitialized) {
        ID3D11Device* dev = nullptr;
        context->GetDevice(&dev);
        m_fullscreenRenderer.Initialize(dev);
        dev->Release();
        m_rendererInitialized = true;
    }
    m_fullscreenRenderer.DrawFullscreen(context);
    
    // Cleanup
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->PSSetShaderResources(0, 1, &nullSRV);
    
    m_dirty = false;
    return true;
}

void FrostedGlassBlur::SetColor(float r, float g, float b, float a) {
    m_tintColor[0] = r;
    m_tintColor[1] = g;
    m_tintColor[2] = b;
    m_tintColor[3] = a;
    m_dirty = true;
}

bool FrostedGlassBlur::SetParameters(const char* json) {
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

std::string FrostedGlassBlur::GetParameters() const {
    char buffer[128];
    snprintf(buffer, sizeof(buffer), 
             "{\"blurRadius\": %.2f, \"distortion\": %.3f, \"cellScale\": %.1f}",
             m_blurRadius, m_distortionStrength, m_cellScale);
    return buffer;
}

// Factory function
std::unique_ptr<IBlurEffect> CreateFrostedGlassBlur() {
    return std::make_unique<FrostedGlassBlur>();
}

} // namespace blurwindow
