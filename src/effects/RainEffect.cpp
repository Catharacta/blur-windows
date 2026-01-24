#include "RainEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/Logger.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace blurwindow {

// Embedded Rain.hlsl
static const char* g_RainPS = R"(
Texture2D InputTexture : register(t0);
SamplerState LinearSampler : register(s0);

cbuffer Parameters : register(b0) {
    float Time;
    float Intensity;
    float Speed;
    float Brightness;
    float NormalStrength;
    float Zoom;
    float2 Resolution;    // Screen resolution
    float2 TexResolution; // Texture resolution
    int PostProcessing;
    int Lightning;      
    float2 Padding;     
};

#define S(a, b, t) smoothstep(a, b, t)

// --- Random Functions ---
float3 N13(float p) {
    float3 p3 = frac(float3(p, p, p) * float3(0.1031, 0.11369, 0.13787));
    p3 += dot(p3, p3.yzx + 19.19);
    return frac(float3((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y, (p3.y + p3.z) * p3.x));
}

float4 N14(float t) {
    return frac(sin(t * float4(123., 1024., 1456., 264.)) * float4(6547., 345., 8799., 1564.));
}

float N(float t) {
    return frac(sin(t * 12345.564) * 7658.76);
}

float Saw(float b, float t) {
    return S(0., b, t) * S(1., b, t);
}

// --- Drop Simulation ---
float2 DropLayer2(float2 uv, float t) {
    float2 UV = uv;

    uv.y += t * 0.75;
    float2 a = float2(6., 1.);
    float2 grid = a * 2.;
    float2 id = floor(uv * grid);

    float colShift = N(id.x);
    uv.y += colShift;

    id = floor(uv * grid);
    float3 n = N13(id.x * 35.2 + id.y * 2376.1);
    float2 st = frac(uv * grid) - float2(.5, 0);

    float x = n.x - .5;

    float y = UV.y * 20.;
    float wiggle = sin(y + sin(y));
    x += wiggle * (.5 - abs(x)) * (n.z - .5);
    x *= .7;
    float ti = frac(t + n.z);
    y = (Saw(.85, ti) - .5) * .9 + .5;
    float2 p = float2(x, y);

    float d = length((st - p) * a.yx);

    float mainDrop = S(.4, .0, d);

    float r = sqrt(S(1., y, st.y));
    float cd = abs(st.x - x);
    float trail = S(.23 * r, .15 * r * r, cd);
    float trailFront = S(-.02, .02, st.y - y);
    trail *= trailFront * r * r;

    y = UV.y;
    float trail2 = S(.2 * r, .0, cd);
    float droplets = max(0., (sin(y * (1. - y) * 120.) - st.y)) * trail2 * trailFront * n.z;
    y = frac(y * 10.) + (st.y - .5);
    float dd = length(st - float2(x, y));
    droplets = S(.3, 0., dd);
    float m = mainDrop + droplets * r * trailFront;

    // x: drop amount, y: distance mask
    return float2(m, m);
}

float StaticDrops(float2 uv, float t) {
    uv *= 40.;

    float2 id = floor(uv);
    uv = frac(uv) - .5;
    float3 n = N13(id.x * 107.45 + id.y * 3543.654);
    float2 p = (n.xy - .5) * .7;
    float d = length(uv - p);

    float fade = Saw(.025, frac(t + n.z));
    float c = S(.3, 0., d) * frac(n.z * 10.) * fade;
    return c;
}

float2 GetDrops(float2 uv, float t, float l0, float l1, float l2) {
    float s = StaticDrops(uv, t) * l0;
    float2 m1 = DropLayer2(uv, t) * l1;
    float2 m2 = DropLayer2(uv * 1.85, t) * l2;

    float c = s + m1.x + m2.x;
    c = S(.3, 1., c);

    return float2(c, max(m1.x * l0, m2.x * l1)); 
}

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    float2 uv = input.Tex;
    
    // UV for simulation (aspect corrected)
    float aspect = Resolution.x / Resolution.y;
    float2 st = uv * float2(aspect, 1.0);
    
    // Time & Zoom
    float T = Time + (sin(Time * sin(Time * sin(Time) * 0.5)) * 0.5);
    float t = T * .2 * Speed;
    
    // Zoom
    float finalZoom = Zoom > 0.0 ? Zoom : 1.0;
    st *= finalZoom; 
    
    float rainAmount = Intensity;

    float staticDrops = S(-.5, 1., rainAmount) * 2.;
    float layer1 = S(.25, .75, rainAmount);
    float layer2 = S(.0, .5, rainAmount);

    float2 c = GetDrops(st, t, staticDrops, layer1, layer2);

    // Calculate Normals (Expensive mode for quality)
    // ddx/ddy often produces blocky artifacts for smooth procedural noise, so manual sampling is preferred for high quality
    float2 e = float2(.001, 0.) * NormalStrength; 
    float cx = GetDrops(st + e, t, staticDrops, layer1, layer2).x;
    float cy = GetDrops(st + e.yx, t, staticDrops, layer1, layer2).x;
    float2 n = float2(cx - c.x, cy - c.x);

    // Sample background with offset
    // Note: Assuming InputTexture is ALREADY blurred by previous passes if desired.
    float4 col = InputTexture.Sample(LinearSampler, uv + n);

    // Post processing (e.g. slight color shift or lightning)
    if (PostProcessing) {
        col.rgb *= lerp(float3(1.,1.,1.), float3(0.8, 0.9, 1.3), Intensity * 0.5);
    }
    
    // Lightning
    if (Lightning) {
        float timeVal = (T + 3.) * .5;
        float lightning = sin(timeVal * sin(timeVal * 10.));
        lightning *= pow(max(0., sin(timeVal + sin(timeVal))), 10.);
        col.rgb *= 1. + lightning * S(0., 10., T) * lerp(1., .1, 0.);
    }

    col.rgb *= Brightness;
    
    return col;
}
)";

struct RainParams {
    float Time;
    float Intensity;
    float Speed;
    float Brightness;
    float NormalStrength;
    float Zoom;
    float ResolutionX;
    float ResolutionY;
    float TexResolutionX;
    float TexResolutionY;
    int PostProcessing;
    int Lightning;
    float Padding[2];
};

bool RainEffect::Initialize(ID3D11Device* device) {
    m_device = device;
    
    // Compile embedded shader
    if (!ShaderLoader::CompilePixelShader(device, g_RainPS, strlen(g_RainPS), "main", m_rainPS.GetAddressOf())) {
        LOG_ERROR("RainEffect: Failed to compile shader");
        return false;
    }
    
    // Create sampler
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_MIRROR; // Mirror for better edge handling
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_MIRROR;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    
    HRESULT hr = device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // Create constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(RainParams); // 64 bytes
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // Initialize fullscreen renderer
    if (!m_fullscreenRenderer.Initialize(device)) {
        return false;
    }
    
    LOG_INFO("RainEffect::Initialize - Success (GPU-based)");
    return true;
}

bool RainEffect::Apply(
    ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* input,
    ID3D11RenderTargetView* output,
    uint32_t width,
    uint32_t height
) {
    if (!m_rainPS || !context || !input || !output) return false;
    
    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
        RainParams* params = static_cast<RainParams*>(mappedResource.pData);
        params->Time = m_time;
        params->Intensity = m_rainIntensity;
        params->Speed = m_dropSpeed;
        params->Brightness = m_brightness;
        params->NormalStrength = m_normalStrength;
        params->Zoom = m_zoom;
        params->ResolutionX = static_cast<float>(width);
        params->ResolutionY = static_cast<float>(height);
        // Assuming texture resolution matches window size used for render
        params->TexResolutionX = static_cast<float>(width);
        params->TexResolutionY = static_cast<float>(height);
        params->PostProcessing = true; // Hardcoded on for now, or add parameter
        params->Lightning = false;     // Hardcoded off
        // Zero padding
        params->Padding[0] = 0.0f;
        params->Padding[1] = 0.0f;
        
        context->Unmap(m_constantBuffer.Get(), 0);
    }
    
    // Set render target
    context->OMSetRenderTargets(1, &output, nullptr);
    
    // Set viewport
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);
    
    // Set resources
    context->PSSetShader(m_rainPS.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, &input);
    context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    
    // Draw
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
    // Increase time
    m_time += deltaTime;
}

bool RainEffect::SetParameters(const char* json) {
    if (!json) return false;
    
    // Simple parsing for new parameters
    // Support legacy "intensity" and new keys
    float fVal;
    if (sscanf_s(json, "{\"intensity\": %f}", &fVal) == 1) {
        m_rainIntensity = fVal;
        return true;
    }
    // TODO: Use a real JSON parser if more complex interactions needed
    return false;
}

std::string RainEffect::GetParameters() const {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
        R"({"intensity": %.2f, "speed": %.2f, "zoom": %.2f})",
        m_rainIntensity, m_dropSpeed, m_zoom);
    return std::string(buffer);
}

// Factory function
std::unique_ptr<IBlurEffect> CreateRainEffect() {
    return std::make_unique<RainEffect>();
}

} // namespace blurwindow
