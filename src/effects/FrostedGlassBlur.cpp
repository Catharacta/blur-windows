#include "FrostedGlassBlur.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include "../core/Logger.h"
#include <cstdio>
#include <cstring>

namespace blurwindow {

// Frosted glass pixel shader with Voronoi distortion and Grain Noise
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
    float noiseIntensity;
    float noiseScale;
    int noiseType;
    float padding; // Alignment
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
    float m = 8.0f;
    
    // For distortion (original logic)
    float2 minOffset = float2(0, 0);
    float minDist = 8.0;
    
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            float2 g = float2(i, j);
            float2 o = hash2(n + g);
            // Animate
            o = 0.5 + 0.4 * sin(time * 0.5 + 6.2831 * o);
            
            float2 r = g + o - f;
            float d = dot(r, r);
            
            if (d < minDist) {
                minDist = d;
                minOffset = r;
            }
            m = min(m, d);
        }
    }
    
    return minOffset; // Return offset for distortion
}

// Separate voronoi for noise pattern (returns distance)
float voronoiPattern(float2 uv) {
    float2 n = floor(uv);
    float2 f = frac(uv);
    float m = 8.0f;
    for (int j = -1; j <= 1; j++) {
        for (int i = -1; i <= 1; i++) {
            float2 g = float2(i, j);
            float2 o = hash2(n + g);
            o = 0.5 + 0.5 * sin(time + 6.2831 * o);
            float d = distance(g + o, f);
            m = min(m, d);
        }
    }
    return m;
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float2 pixelSize = 1.0 / resolution;
    
    // ======= 1. Distortion (Frosted Effect) =======
    // Scales adjusted by resolution to maintain consistent density across window sizes
    // Base resolution reference: 600px width (approx small window size)
    float2 adjustedCellScale = cellScale * (resolution / 600.0f);
    float2 cellUV = texcoord * adjustedCellScale;
    float2 voronoiOffset = voronoi(cellUV);
    
    // Apply distortion to UV coordinates
    float2 distortedUV = texcoord + voronoiOffset * distortionStrength;
    distortedUV = clamp(distortedUV, 0.0, 1.0);
    
    // ======= 2. Blur =======
    float4 color = float4(0, 0, 0, 0);
    float total = 0.0;
    float radius = blurRadius;
    
    // 5x5 Gaussian-weighted blur at distorted position
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            float2 offset = float2(i, j) * pixelSize * radius * 0.4;
            float weight = exp(-0.5 * (i*i + j*j) / 2.0);
            color += inputTexture.Sample(linearSampler, distortedUV + offset) * weight;
            total += weight;
        }
    }
    float4 blurred = color / total;
    
    // ======= 3. Noise Overlay =======
    float n_val = 0;
    if (noiseIntensity > 0) {
        // Adjust noise scale consistently with distortion
        float2 adjustedNoiseScale = noiseScale * (resolution / 600.0f);
        float2 uv = texcoord * adjustedNoiseScale;
        
        if (noiseType == 0) n_val = random(uv + time) - 0.5f;
        else if (noiseType == 1) { // Sinusoid
             float s1 = sin(uv.x * 2.5f + time) * sin(uv.y * 1.8f + time * 0.7f);
             float s2 = sin(uv.x * 0.5f - time * 0.3f) * sin(uv.y * 0.4f + time * 0.2f);
             n_val = (s1 * 0.7f + s2 * 0.3f) * 2.0f;
        } else if (noiseType == 2) { // Grid
             float2 grid = frac(uv * 0.05f);
             float gridLine = step(0.96f, grid.x) + step(0.96f, grid.y);
             n_val = (gridLine > 0.5f) ? 1.5f : -0.3f;
        } else if (noiseType == 3) n_val = perlinNoise(uv * 0.3f) * 2.5f;
        else if (noiseType == 4) n_val = simplexNoise(uv * 0.15f) * 3.5f;
        else if (noiseType == 5) n_val = (1.0f - voronoiPattern(uv * 0.2f)) * 2.0f - 0.5f;
    }
    
    // Apply noise to blurred color
    blurred.rgb += n_val * noiseIntensity;
    
    // ======= 4. Tint & Opacity =======
    float3 tinted = lerp(blurred.rgb, tintColor.rgb, tintColor.a);
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
    
    // Create constant buffer (80 bytes aligned to 16)
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = 80; // Expanded for noise params
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
            float noiseIntensity;
            float noiseScale;
            int noiseType;
            float padding;
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
        params->noiseIntensity = m_noiseIntensity;
        params->noiseScale = m_noiseScale;
        params->noiseType = m_noiseType;
        params->padding = 0.0f;
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
    
    // Parse JSON parameters using sscanf_s
    bool updated = false;
    
    // Blur Radius (Standard "param")
    const char* paramStr = strstr(json, "\"param\"");
    if (paramStr) {
        float param = 0;
        if (sscanf_s(paramStr, "\"param\": %f", &param) == 1 ||
            sscanf_s(paramStr, "\"param\":%f", &param) == 1) {
            m_blurRadius = param;
            updated = true;
        }
    }

    // Distortion Intensity
    const char* distStr = strstr(json, "\"distortion\"");
    if (distStr) {
        float dist = 0;
        if (sscanf_s(distStr, "\"distortion\": %f", &dist) == 1 ||
            sscanf_s(distStr, "\"distortion\":%f", &dist) == 1) {
            m_distortionStrength = dist;
            updated = true;
        }
    }
    
    // Cell Scale
    const char* scaleStr = strstr(json, "\"scale\"");
    if (scaleStr) {
        float scale = 0;
        if (sscanf_s(scaleStr, "\"scale\": %f", &scale) == 1 ||
            sscanf_s(scaleStr, "\"scale\":%f", &scale) == 1) {
            m_cellScale = scale;
            updated = true;
        }
    }

    if (updated) m_dirty = true;
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
