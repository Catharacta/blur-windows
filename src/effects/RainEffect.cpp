#include "RainEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/Logger.h"
#include <cstring>
#include <cstdlib>
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
    float Strength;
    float Opacity;
    float TrailLength;  // 0.25..1 : how far the water trail extends above a drop
    float DropSize;     // 1.0 = default drop size
    float BackgroundBlur; // mip LOD used for fogged glass (0 = sharp)
    float3 PadBlur;
    float4 TintColor;
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

    float mainDrop = S(.4 * DropSize, .0, d);

    // Trail extends above the drop (y-up space) up to TrailLength of the remaining cell height
    float trailTop = y + (1. - y) * TrailLength;
    float r = sqrt(S(trailTop, y, st.y));
    float cd = abs(st.x - x);
    float trail = S(.23 * r, .15 * r * r, cd);
    float trailFront = S(-.02, .02, st.y - y);
    trail *= trailFront * r * r;

    y = UV.y;
    float trail2 = S(.2 * r, .0, cd);
    float droplets = max(0., (sin(y * (1. - y) * 120.) - st.y)) * trail2 * trailFront * n.z;
    y = frac(y * 10.) + (st.y - .5);
    float dd = length(st - float2(x, y));
    // Small beads left behind along the trail
    float beads = S(.3 * DropSize, 0., dd);
    droplets = max(droplets * .5, beads);
    float m = mainDrop + droplets * r * trailFront;

    // x: drop amount, y: trail (wiped path) mask
    return float2(m, trail);
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

    return float2(c, max(m1.y * l0, m2.y * l1)); 
}

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    float2 uv = input.Tex;
    
    // The drop algorithm (Heartfelt) assumes Y points UP. D3D texture space has Y pointing DOWN,
    // so flip it; otherwise drops climb upward and trails appear below them.
    float2 uvUp = float2(uv.x, 1.0 - uv.y);
    
    // UV for simulation (aspect corrected)
    float aspect = Resolution.x / Resolution.y;
    float2 st = uvUp * float2(aspect, 1.0);
    
    // Time & Zoom
    // Time must be monotonic. (The previous sin() time-warp made the clock run backwards
    // at times, which made drops jitter up and down instead of sliding.)
    float T = Time;
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
    // Back to texture space (Y down) for the refraction offset
    float2 nUV = float2(n.x, -n.y);

    // Limit refraction offset so tiny, steep drop edges cannot fling samples far away
    float nLen = length(nUV);
    if (nLen > .05) nUV *= .05 / nLen;

    // Sample the mip chain: fogged glass outside drops, light blur inside drops,
    // and wiped trails are clearer. Averaging via LOD removes the speckle that
    // fine text produced when a sharp background was refracted by tiny drops.
    float lodIn = min(1.0, BackgroundBlur);
    float lodOut = max(BackgroundBlur - c.y * 1.5, lodIn);
    float focus = lerp(lodOut, lodIn, S(.1, .2, c.x));
    float4 col = InputTexture.SampleLevel(LinearSampler, uv + nUV, focus);


    // Small specular highlight on the upper-left rim of each drop (lens-like look)
    float spec = saturate(dot(nUV, float2(-.4, -.9)) * 30. / max(NormalStrength, .1));
    col.rgb += spec * .15 * S(.05, .3, c.x);


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
    
    // Tinting (applied with alpha weighting)
    col.rgb = lerp(col.rgb, TintColor.rgb, TintColor.a * TintColor.a);

    // Blend with un-distorted original image based on Strength
    float4 original = InputTexture.SampleLevel(LinearSampler, uv, 0);
    col.rgb = lerp(original.rgb, col.rgb, Strength);

    // Apply Opacity
    col.a = Opacity;
    
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
    float Strength;
    float Opacity;
    float TrailLength;
    float DropSize;
    float BackgroundBlur;
    float PadBlur[3];
    float TintColor[4];
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
    cbDesc.ByteWidth = sizeof(RainParams); // 96 bytes
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // Initialize fullscreen renderer
    if (!m_fullscreenRenderer.Initialize(device)) {
        return false;
    }
    
    if (getenv("BLUR_RAIN_NOMIP")) m_useMips = false;
    if (getenv("BLUR_RAIN_PROFILE")) m_profile = true;

    LOG_INFO("RainEffect::Initialize - Success (GPU-based)");
    return true;
}

// Copy the captured frame into an internal texture with a full mip chain so the shader
// can sample pre-averaged (blurred) versions of the background.
bool RainEffect::PrepareMips(ID3D11DeviceContext* context, ID3D11ShaderResourceView* input) {
    if (!m_useMips || m_mipFailed) return false;

    ComPtr<ID3D11Resource> res;
    input->GetResource(res.GetAddressOf());
    ComPtr<ID3D11Texture2D> tex;
    if (!res || FAILED(res.As(&tex))) return false;

    D3D11_TEXTURE2D_DESC d = {};
    tex->GetDesc(&d);
    if (d.SampleDesc.Count != 1 || d.ArraySize != 1) return false;

    if (!m_mipTex || m_mipW != d.Width || m_mipH != d.Height || m_mipFormat != d.Format) {
        m_mipTex.Reset();
        m_mipSRV.Reset();

        D3D11_TEXTURE2D_DESC md = d;
        md.MipLevels = 0; // full chain
        md.Usage = D3D11_USAGE_DEFAULT;
        md.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        md.CPUAccessFlags = 0;
        md.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        HRESULT hr = m_device->CreateTexture2D(&md, nullptr, m_mipTex.GetAddressOf());
        if (SUCCEEDED(hr)) hr = m_device->CreateShaderResourceView(m_mipTex.Get(), nullptr, m_mipSRV.GetAddressOf());
        if (FAILED(hr)) {
            LOG_WARN("RainEffect: mip texture creation failed (0x%08X), falling back to sharp background", hr);
            m_mipTex.Reset();
            m_mipSRV.Reset();
            m_mipFailed = true;
            return false;
        }
        m_mipW = d.Width;
        m_mipH = d.Height;
        m_mipFormat = d.Format;
    }

    context->CopySubresourceRegion(m_mipTex.Get(), 0, 0, 0, 0, tex.Get(), 0, nullptr);
    context->GenerateMips(m_mipSRV.Get());
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

    if (m_profile) {
        if (!m_qDisjoint[0]) {
            D3D11_QUERY_DESC qd = {};
            qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
            for (int i = 0; i < 4; ++i) m_device->CreateQuery(&qd, m_qDisjoint[i].GetAddressOf());
            qd.Query = D3D11_QUERY_TIMESTAMP;
            for (int i = 0; i < 4; ++i) {
                m_device->CreateQuery(&qd, m_qStart[i].GetAddressOf());
                m_device->CreateQuery(&qd, m_qEnd[i].GetAddressOf());
            }
        }
        context->Begin(m_qDisjoint[m_qIndex].Get());
        context->End(m_qStart[m_qIndex].Get());
    }

    ID3D11ShaderResourceView* srv = input;
    if (PrepareMips(context, input)) srv = m_mipSRV.Get();
    
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
        params->Strength = m_strength;
        params->Opacity = m_opacity;
        params->TrailLength = std::clamp(0.25f + m_trailLength * 2.5f, 0.25f, 1.0f);
        params->DropSize = std::clamp(0.5f * (m_dropSizeMin + m_dropSizeMax) / 12.5f, 0.3f, 3.0f);
        // Without a mip chain there is nothing to blur with, so the LOD has no effect
        params->BackgroundBlur = (srv == m_mipSRV.Get()) ? m_backgroundBlur : 0.0f;
        params->PadBlur[0] = params->PadBlur[1] = params->PadBlur[2] = 0.0f;
        memcpy(params->TintColor, m_tintColor, sizeof(m_tintColor));
        
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
    context->PSSetShaderResources(0, 1, &srv);
    context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    
    // Draw
    m_fullscreenRenderer.DrawFullscreen(context);
    
    // Cleanup
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->PSSetShaderResources(0, 1, &nullSRV);

    if (m_profile) {
        context->End(m_qEnd[m_qIndex].Get());
        context->End(m_qDisjoint[m_qIndex].Get());
        m_qIndex = (m_qIndex + 1) % 4;
        // Read the oldest query (3 frames old) without stalling
        int ri = m_qIndex;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
        UINT64 t0 = 0, t1 = 0;
        if (context->GetData(m_qDisjoint[ri].Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(m_qStart[ri].Get(), &t0, sizeof(t0), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            context->GetData(m_qEnd[ri].Get(), &t1, sizeof(t1), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            !dj.Disjoint && dj.Frequency) {
            m_gpuMsSum += double(t1 - t0) * 1000.0 / double(dj.Frequency);
            if (++m_gpuMsCount == 120) {
                LOG_INFO("[RainProfile] mips=%d avg GPU time per Apply: %.3f ms (%ux%u)",
                    m_mipTex ? 1 : 0, m_gpuMsSum / m_gpuMsCount, width, height);
                m_gpuMsSum = 0; m_gpuMsCount = 0;
            }
        }
    }
    
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

static bool ParseFloatKey(const char* json, const char* key, float& out) {
    char pattern[64];
    std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == '\t') ++p;
    char* end = nullptr;
    float v = strtof(p, &end);
    if (end == p) return false;
    out = v;
    return true;
}

bool RainEffect::SetParameters(const char* json) {
    if (!json) return false;
    
    bool any = false;
    float fVal;
    if (ParseFloatKey(json, "intensity", fVal)) {
        m_rainIntensity = fVal;
        any = true;
    }
    if (ParseFloatKey(json, "background_blur", fVal)) {
        m_backgroundBlur = std::clamp(fVal, 0.0f, 5.0f);
        any = true;
    }
    return any;
}

std::string RainEffect::GetParameters() const {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
        R"({"intensity": %.2f, "speed": %.2f, "zoom": %.2f, "background_blur": %.2f})",
        m_rainIntensity, m_dropSpeed, m_zoom, m_backgroundBlur);
    return std::string(buffer);
}

// Factory function
std::unique_ptr<IBlurEffect> CreateRainEffect() {
    return std::make_unique<RainEffect>();
}

} // namespace blurwindow
