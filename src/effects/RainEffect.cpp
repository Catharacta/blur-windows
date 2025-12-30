#include "RainEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace blurwindow {

// Passthrough pixel shader (Phase 1: basic passthrough)
static const char* g_PassthroughPS = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    return inputTexture.Sample(linearSampler, texcoord);
}
)";

bool RainEffect::Initialize(ID3D11Device* device) {
    m_device = device;
    
    // Initialize random number generator
    std::random_device rd;
    m_rng = std::mt19937(rd());
    
    // Compile passthrough shader (Phase 1)
    if (!ShaderLoader::CompilePixelShader(device, g_PassthroughPS, strlen(g_PassthroughPS), "main", m_refractionPS.GetAddressOf())) {
        return false;
    }
    
    // Create sampler state
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    
    HRESULT hr = device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    
    // Initialize fullscreen renderer
    if (!m_fullscreenRenderer.Initialize(device)) {
        return false;
    }
    
    return true;
}

bool RainEffect::Apply(
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* input,
    ID3D11RenderTargetView* output,
    uint32_t width,
    uint32_t height
) {
    if (!m_refractionPS || !context || !input || !output) {
        return false;
    }
    
    // Phase 1: Simple passthrough rendering
    // Future phases will add raindrop simulation and refraction
    
    // Set render target
    context->OMSetRenderTargets(1, &output, nullptr);
    
    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);
    
    // Set shader resources
    context->PSSetShaderResources(0, 1, &input);
    context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    context->PSSetShader(m_refractionPS.Get(), nullptr, 0);
    
    // Draw fullscreen quad
    m_fullscreenRenderer.DrawFullscreen(context);
    
    // Cleanup
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->PSSetShaderResources(0, 1, &nullSRV);
    
    return true;
}

void RainEffect::SetColor(float r, float g, float b, float a) {
    m_tintColor[0] = r;
    m_tintColor[1] = g;
    m_tintColor[2] = b;
    m_tintColor[3] = a;
}

void RainEffect::Update(float deltaTime) {
    m_time += deltaTime * m_noiseSpeed;
    
    // Update raindrop simulation
    UpdateDrops(deltaTime);
}

void RainEffect::UpdateDrops(float deltaTime) {
    // Phase 2: Implement raindrop movement and lifecycle
    // For now, this is a placeholder
    
    // Update spawn timer
    m_spawnTimer += deltaTime;
}

void RainEffect::SpawnNewDrops(uint32_t width, uint32_t height) {
    // Phase 2: Spawn new raindrops based on intensity
    // Placeholder for future implementation
}

void RainEffect::MergeDrops() {
    // Phase 2: Merge overlapping drops (gooey effect)
    // Placeholder for future implementation
}

void RainEffect::RenderDropTexture(ID3D11DeviceContext* context, uint32_t width, uint32_t height) {
    // Phase 2: Render drops to texture for refraction
    // Placeholder for future implementation
}

bool RainEffect::CreateDropTexture(uint32_t width, uint32_t height) {
    // Phase 2: Create drop texture for refraction mapping
    // Placeholder for future implementation
    return true;
}

void RainEffect::SetDropSizeRange(float minSize, float maxSize) {
    m_minDropSize = minSize;
    m_maxDropSize = maxSize;
}

bool RainEffect::SetParameters(const char* json) {
    if (!json) return false;
    
    // Parse simple JSON parameters
    float intensity = 0.0f;
    if (std::sscanf(json, "{\"intensity\": %f}", &intensity) == 1) {
        m_rainIntensity = intensity;
        return true;
    }
    
    return false;
}

std::string RainEffect::GetParameters() const {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
        R"({"intensity": %.2f, "dropSpeed": %.2f, "refraction": %.2f})",
        m_rainIntensity, m_dropSpeed, m_refractionStrength);
    return std::string(buffer);
}

// Factory function for SubsystemFactory
std::unique_ptr<IBlurEffect> CreateRainEffect() {
    return std::make_unique<RainEffect>();
}

} // namespace blurwindow
