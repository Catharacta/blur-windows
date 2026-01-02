#include "RainEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
#include "../core/Logger.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace blurwindow {

// Passthrough pixel shader (fallback)
static const char* g_PassthroughPS = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    return inputTexture.Sample(linearSampler, texcoord);
}
)";

// Simple box blur for background
static const char* g_BoxBlurPS = R"(
Texture2D inputTexture : register(t0);
SamplerState linearSampler : register(s0);

cbuffer BlurParams : register(b0) {
    float2 texelSize;
    float blurRadius;
    float padding;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    float4 result = float4(0, 0, 0, 0);
    float samples = 0;
    int radius = int(blurRadius);
    
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            float2 offset = float2(x, y) * texelSize;
            result += inputTexture.Sample(linearSampler, texcoord + offset);
            samples++;
        }
    }
    
    return result / samples;
}
)";

// Droplets pixel shader - renders small background droplets
// Uses the same teardrop profile as main drops but smaller
static const char* g_DropletsPS = R"(
struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 dropData : TEXCOORD1;
};

cbuffer DropletsParams : register(b0) {
    float globalAlpha;
    float3 padding;
};

float4 main(VSOutput input) : SV_Target {
    float2 uv = input.texcoord * 2.0 - 1.0;
    
    // Simple circular droplet (smaller and simpler than main drops)
    float distSq = dot(uv, uv);
    if (distSq > 1.0) discard;
    
    float dist = sqrt(distSq);
    float height = sqrt(max(0.0, 1.0 - distSq)) * 0.5; // Lower profile
    
    float3 normal = normalize(float3(uv, height * 2.0));
    float alpha = smoothstep(1.0, 0.5, dist) * globalAlpha;
    
    return float4(normal.xy * 0.5 + 0.5, height, alpha * 0.6);
}
)";
static const char* g_RaindropVS = R"(
struct VSInput {
    uint vertexId : SV_VertexID;
    uint instanceId : SV_InstanceID;
};

struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 dropData : TEXCOORD1; // x, y, radius, seed
};

struct DropInstance {
    float2 pos;
    float radius;
    float seed;
};

cbuffer DropParams : register(b0) {
    float2 resolution;
    float2 padding;
};

StructuredBuffer<DropInstance> drops : register(t0);

VSOutput main(VSInput input) {
    VSOutput output;
    
    // Quad vertices [0,1]: (0,0), (1,0), (0,1), (1,1)
    float2 uv = float2(input.vertexId & 1, (input.vertexId >> 1) & 1);
    output.texcoord = uv;
    
    DropInstance drop = drops[input.instanceId];
    output.dropData = float4(drop.pos, drop.radius, drop.seed);
    
    // Convert UI (0..1, top-left) to NDC (-1..1, bottom-left)
    float2 ndcCenter = float2(drop.pos.x * 2.0 - 1.0, 1.0 - drop.pos.y * 2.0);
    
    // Codrops-style scaling: scaleX=1.0, scaleY=1.5 (raindrops are naturally taller)
    // Also correct for aspect ratio (NDC is square, screen is not)
    float aspectRatio = resolution.x / resolution.y;
    float2 scale = float2(1.0 / aspectRatio, 1.5);
    
    // Radius is in normalized [0,1] coords, convert to NDC by multiplying by 2
    float2 quadOffset = (uv * 2.0 - 1.0) * drop.radius * 2.0 * scale;
    
    output.position = float4(ndcCenter + quadOffset, 0.0, 1.0);
    return output;
}
)";

// Raindrop rendering pixel shader
// Produces NormalXY, Depth, Mask
// Implements teardrop shape profile for realistic water drops
static const char* g_RaindropPS = R"(
struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 dropData : TEXCOORD1;
};

float4 main(VSOutput input) : SV_Target {
    float2 uv = input.texcoord * 2.0 - 1.0;
    
    // Teardrop shape: wider at top, narrower at bottom
    // Apply y-dependent x scaling to create teardrop silhouette
    float yFactor = (uv.y + 1.0) * 0.5;  // 0 at top, 1 at bottom
    float xScale = lerp(1.0, 0.6, yFactor * yFactor);  // Narrower at bottom
    float2 scaledUV = float2(uv.x / xScale, uv.y);
    
    // SDF for the teardrop shape
    float distSq = dot(scaledUV, scaledUV);
    if (distSq > 1.0) discard;
    
    // Height profile: dome at top, tapers to point at bottom
    float dist = sqrt(distSq);
    float baseHeight = sqrt(max(0.0, 1.0 - distSq));
    // Modify height based on y position (lower = thinner)
    float heightMod = lerp(1.0, 0.3, yFactor * yFactor);
    float height = baseHeight * heightMod;
    
    // Normal calculation for teardrop surface
    // Account for the x-scaling in normal calculation
    float3 normal = normalize(float3(scaledUV.x * xScale, scaledUV.y * 0.8, height * 2.0));
    
    // Add slight randomization based on seed for variation
    float seed = input.dropData.w;
    normal.xy += (seed - 0.5) * 0.05;
    normal = normalize(normal);
    
    // Gooey effect: smooth edges using alpha threshold
    float alpha = smoothstep(0.95, 0.7, dist);
    
    return float4(normal.xy * 0.5 + 0.5, height, alpha);
}
)";

// Refraction/composite pixel shader - Enhanced Codrops Style
static const char* g_RefractionPS = R"(
Texture2D backgroundFocus : register(t0);
Texture2D dropTexture : register(t1);
Texture2D dropletsTexture : register(t2);
SamplerState linearSampler : register(s0);

cbuffer RefractionParams : register(b0) {
    float refractionStrength;
    float shininess;
    float2 resolution;
    float4 tintColor;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    // 1. Sample both drop textures and combine
    float4 dropData = dropTexture.Sample(linearSampler, texcoord);
    float4 dropletsData = dropletsTexture.Sample(linearSampler, texcoord);
    
    // Combine droplets and drops - droplets are background, drops are foreground
    float4 combinedData = dropData;
    if (dropletsData.a > 0.05 && dropData.a < 0.1) {
        // Use droplets where there are no main drops
        combinedData = dropletsData;
    } else if (dropletsData.a > 0.05 && dropData.a >= 0.1) {
        // Blend where both exist
        combinedData = lerp(dropletsData, dropData, dropData.a);
    }
    
    // Gooey threshold with smooth transition
    float dropAlpha = combinedData.a;
    if (dropAlpha < 0.05) {
        return backgroundFocus.Sample(linearSampler, texcoord);
    }
    
    // 2. High-fidelity Refraction with depth-based intensity
    float2 normal = combinedData.xy * 2.0 - 1.0;
    float depth = combinedData.z;
    
    // Refraction offset scales with depth (thicker = more refraction)
    float2 refractionOffset = normal * refractionStrength * 0.08 * depth;
    float4 refractedColor = backgroundFocus.Sample(linearSampler, texcoord + refractionOffset);
    
    // 3. Fresnel effect: stronger reflection at edges
    float normalMag = length(normal);
    float fresnel = pow(1.0 - depth, 2.0) * 0.5;
    
    // 4. Depth-based blue tint (distant drops appear bluer)
    float3 depthTint = lerp(float3(1.0, 1.0, 1.0), float3(0.85, 0.9, 1.0), depth * 0.4);
    
    // 5. Specular Highlight with dual light sources
    float3 lightDir1 = normalize(float3(0.8, 0.8, 1.2));
    float3 lightDir2 = normalize(float3(-0.5, 0.3, 1.0));
    float3 viewDir = float3(0, 0, 1);
    float3 dropNormal = normalize(float3(normal, max(0.1, depth)));
    
    float spec1 = pow(max(dot(reflect(-lightDir1, dropNormal), viewDir), 0.0), shininess);
    float spec2 = pow(max(dot(reflect(-lightDir2, dropNormal), viewDir), 0.0), shininess * 0.5) * 0.3;
    float3 specularColor = float3(1.0, 1.0, 1.0) * (spec1 + spec2);
    
    // 6. Edge highlight (rim lighting)
    float rim = smoothstep(0.5, 0.9, normalMag) * 0.15;
    
    // 7. Final blend with smooth alpha transition
    float3 baseColor = refractedColor.rgb * depthTint * (0.85 + depth * 0.15);
    float3 finalColor = baseColor + specularColor + rim;
    finalColor = lerp(finalColor, tintColor.rgb, tintColor.a * 0.5);
    
    // Smooth edge blending
    float finalAlpha = smoothstep(0.1, 0.4, dropAlpha);
    float4 background = backgroundFocus.Sample(linearSampler, texcoord);
    
    return float4(lerp(background.rgb, finalColor, finalAlpha), 1.0);
}
)";


bool RainEffect::Initialize(ID3D11Device* device) {
    m_device = device;
    
    // Initialize random number generator
    std::random_device rd;
    m_rng = std::mt19937(rd());
    
    // Compile shaders
    // Passthrough shader (fallback)
    ComPtr<ID3D11PixelShader> passthroughPS;
    if (!ShaderLoader::CompilePixelShader(device, g_PassthroughPS, strlen(g_PassthroughPS), "main", passthroughPS.GetAddressOf())) {
        return false;
    }
    
    // Raindrop rendering shaders (VS + PS)
    if (!ShaderLoader::CompileVertexShader(device, g_RaindropVS, strlen(g_RaindropVS), "main", m_raindropVS.GetAddressOf())) {
        return false;
    }
    if (!ShaderLoader::CompilePixelShader(device, g_RaindropPS, strlen(g_RaindropPS), "main", m_raindropPS.GetAddressOf())) {
        return false;
    }
    
    // Refraction/composite shader
    if (!ShaderLoader::CompilePixelShader(device, g_RefractionPS, strlen(g_RefractionPS), "main", m_refractionPS.GetAddressOf())) {
        return false;
    }
    
    // Box blur shader for background blur
    if (!ShaderLoader::CompilePixelShader(device, g_BoxBlurPS, strlen(g_BoxBlurPS), "main", m_boxBlurPS.GetAddressOf())) {
        LOG_ERROR("RainEffect::Initialize - BoxBlurPS compilation failed");
        return false;
    }
    
    // Droplets shader for background small drops
    if (!ShaderLoader::CompilePixelShader(device, g_DropletsPS, strlen(g_DropletsPS), "main", m_dropletsPS.GetAddressOf())) {
        LOG_ERROR("RainEffect::Initialize - DropletsPS compilation failed");
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
    if (FAILED(hr)) return false;
    
    // Create constant buffer for refraction parameters (32 bytes)
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = 32; // refractionStrength, shininess, resolution[2], tintColor[4]
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // Create constant buffer for blur params (16 bytes: texelSize[2], blurRadius, padding)
    D3D11_BUFFER_DESC blurCbDesc = {};
    blurCbDesc.ByteWidth = 16;
    blurCbDesc.Usage = D3D11_USAGE_DYNAMIC;
    blurCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    blurCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = device->CreateBuffer(&blurCbDesc, nullptr, m_blurParamsBuffer.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // Create StructuredBuffer for raindrop instances
    // We support up to 2000 drops simultaneously
    D3D11_BUFFER_DESC sbDesc = {};
    sbDesc.ByteWidth = sizeof(DropInstance) * 2000;
    sbDesc.Usage = D3D11_USAGE_DYNAMIC;
    sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    sbDesc.StructureByteStride = sizeof(DropInstance);
    
    hr = device->CreateBuffer(&sbDesc, nullptr, m_instanceBuffer.GetAddressOf());
    if (FAILED(hr)) return false;
    
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = 2000;
    
    hr = device->CreateShaderResourceView(m_instanceBuffer.Get(), &srvDesc, m_instanceSRV.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // Initialize fullscreen renderer
    if (!m_fullscreenRenderer.Initialize(device)) {
        LOG_ERROR("RainEffect::Initialize - FullscreenRenderer init failed");
        return false;
    }
    
    LOG_INFO("RainEffect initialized successfully (ConstantBuffer: 32 bytes, InstanceBuffer: %zu bytes)", sizeof(DropInstance) * 2000);
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
        LOG_ERROR("RainEffect::Apply - Invalid state: refractionPS=%p, context=%p, input=%p, output=%p",
            m_refractionPS.Get(), context, input, output);
        return false;
    }
    
    // Store dimensions for simulation FIRST (needed by Update which runs before Apply)
    m_lastWidth = width;
    m_lastHeight = height;
    
    // Create/update drop texture if needed
    if (!CreateDropTexture(width, height)) {
        LOG_ERROR("RainEffect::Apply - CreateDropTexture failed");
        return false;
    }
    
    // Spawn initial drops if none exist (first frame after initialization)
    if (m_drops.empty() && m_rainIntensity > 0.0f) {
        // Spawn several initial drops to have something visible immediately
        int initialDrops = static_cast<int>(20 * m_rainIntensity);
        for (int i = 0; i < initialDrops; ++i) {
            SpawnNewDrops(width, height);
        }
        LOG_INFO("RainEffect: Spawned %d initial drops (now have %zu drops, intensity=%.2f)",
            initialDrops, m_drops.size(), m_rainIntensity);
    }
    
    // Debug log (every ~60 frames to avoid spam)
    static int frameCount = 0;
    if (++frameCount % 60 == 0) {
        LOG_INFO("RainEffect: drops=%zu, dropletsSize=%zu, intensity=%.2f, dropTexture=%p",
            m_drops.size(), m_dropletsData.size(), m_rainIntensity, m_dropTexture.Get());
    }
    
    // Set viewport for all passes
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);
    
    // ===== Pass 0: Blur the background =====
    {
        ID3D11RenderTargetView* blurRTV = m_blurredRTV.Get();
        context->OMSetRenderTargets(1, &blurRTV, nullptr);
        
        // Update blur params
        D3D11_MAPPED_SUBRESOURCE blurMapped;
        if (SUCCEEDED(context->Map(m_blurParamsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &blurMapped))) {
            struct BlurParams {
                float texelSize[2];
                float blurRadius;
                float padding;
            };
            BlurParams* bp = static_cast<BlurParams*>(blurMapped.pData);
            bp->texelSize[0] = 1.0f / width;
            bp->texelSize[1] = 1.0f / height;
            bp->blurRadius = 4.0f;  // Fixed blur radius for rain effect
            bp->padding = 0.0f;
            context->Unmap(m_blurParamsBuffer.Get(), 0);
        }
        
        context->PSSetShader(m_boxBlurPS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &input);
        context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, m_blurParamsBuffer.GetAddressOf());
        
        m_fullscreenRenderer.DrawFullscreen(context);
        
        // Unbind
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->PSSetShaderResources(0, 1, &nullSRV);
    }
    
    // ===== Pass 1a: Render droplets (background small drops) =====
    RenderDropletsTexture(context, width, height);
    
    // ===== Pass 1b: Render raindrops to drop texture =====
    RenderDropTexture(context, width, height);
    
    // ===== Pass 2: Apply refraction and composite =====
    context->OMSetRenderTargets(1, &output, nullptr);
    
    // Update refraction constant buffer using Map (since it's DYNAMIC)
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        struct RefractionParams {
            float refractionStrength;
            float shininess;
            float resolution[2];
            float tintColor[4];
        };
        RefractionParams* p = static_cast<RefractionParams*>(mapped.pData);
        p->refractionStrength = m_refractionStrength;
        p->shininess = m_shininess;
        p->resolution[0] = static_cast<float>(width);
        p->resolution[1] = static_cast<float>(height);
        memcpy(p->tintColor, m_tintColor, sizeof(float) * 4);
        context->Unmap(m_constantBuffer.Get(), 0);
    }
    
    // Set shader resources: blurred background + drop texture + droplets texture
    ID3D11ShaderResourceView* srvs[3] = { m_blurredSRV.Get(), m_dropSRV.Get(), m_dropletsSRV.Get() };
    context->PSSetShaderResources(0, 3, srvs);
    context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    context->PSSetShader(m_refractionPS.Get(), nullptr, 0);
    
    // Draw fullscreen quad
    m_fullscreenRenderer.DrawFullscreen(context);
    
    // Cleanup: IMPORTANT to unbind SRVs to avoid binding loops in next frames
    ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
    context->PSSetShaderResources(0, 3, nullSRVs);
    
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
    UpdateDrops(deltaTime);
    
    // Debug log every ~60 frames
    static int updateLogCounter = 0;
    if (++updateLogCounter % 60 == 0) {
        LOG_INFO("RainEffect::Update - drops=%zu, intensity=%.2f, lastW=%u, lastH=%u",
            m_drops.size(), m_rainIntensity, m_lastWidth, m_lastHeight);
    }
}

void RainEffect::UpdateDrops(float deltaTime) {
    // Skip if dimensions not yet set (Apply hasn't been called yet)
    if (m_lastWidth == 0 || m_lastHeight == 0) {
        return;
    }
    
    // Codrops-compatible simulation
    float timeScale = deltaTime * 60.0f; // Normalize to 60fps
    if (timeScale > 1.1f) timeScale = 1.1f;
    timeScale *= m_dropSpeed;
    
    // Initialize droplets texture if needed
    if (m_lastWidth > 0 && m_lastHeight > 0) {
        if (m_dropletsData.empty() || m_dropletsWidth != m_lastWidth || m_dropletsHeight != m_lastHeight) {
            m_dropletsWidth = m_lastWidth;
            m_dropletsHeight = m_lastHeight;
            m_dropletsData.resize(m_dropletsWidth * m_dropletsHeight * 4, 0);
            // Initialize with neutral values (RG=128)
            for (size_t i = 0; i < m_dropletsData.size(); i += 4) {
                m_dropletsData[i + 0] = 128;
                m_dropletsData[i + 1] = 128;
                m_dropletsData[i + 2] = 0;
                m_dropletsData[i + 3] = 0;
            }
        }
    }
    
    // Screen area scaling: More drops for larger screens
    const float referenceArea = 1920.0f * 1080.0f;
    float currentArea = static_cast<float>(m_lastWidth) * static_cast<float>(m_lastHeight);
    float areaScale = (std::max)(1.0f, currentArea / referenceArea);

    // Spawn background droplets (scaled by screen area)
    m_dropletsCounter += m_dropletsRate * timeScale * m_rainIntensity * areaScale;
    std::uniform_real_distribution<float> pos01(0.0f, 1.0f);
    std::uniform_real_distribution<float> smallSize(2.0f, 4.0f);
    while (m_dropletsCounter >= 1.0f && m_dropletsWidth > 0) {
        m_dropletsCounter -= 1.0f;
        float x = pos01(m_rng);
        float y = pos01(m_rng);
        float r = smallSize(m_rng);
        // Draw small droplet to droplets texture
        int cx = static_cast<int>(x * m_dropletsWidth);
        int cy = static_cast<int>(y * m_dropletsHeight);
        int ir = static_cast<int>(r);
        for (int dy = -ir; dy <= ir; ++dy) {
            for (int dx = -ir; dx <= ir; ++dx) {
                int px = cx + dx;
                int py = cy + dy;
                if (px >= 0 && px < (int)m_dropletsWidth && py >= 0 && py < (int)m_dropletsHeight) {
                    float dist = std::sqrt((float)(dx*dx + dy*dy));
                    if (dist <= r) {
                        float alpha = (1.0f - dist/r) * 0.3f;
                        size_t idx = (py * m_dropletsWidth + px) * 4;
                        m_dropletsData[idx + 3] = (std::max)(m_dropletsData[idx + 3], (uint8_t)(alpha * 255));
                    }
                }
            }
        }
    }
    
    // Spawn rain drops (chance-based like Codrops, scaled by screen area)
    std::uniform_real_distribution<float> chance01(0.0f, 1.0f);
    float rainChance = 0.3f * m_rainIntensity * areaScale;  // スケール適用
    int rainLimit = static_cast<int>(3 * areaScale);        // 1フレームあたりの生成限度
    size_t maxDrops = static_cast<size_t>(900 * areaScale); // 最大雨滴数
    int rainCount = 0;
    while (chance01(m_rng) < rainChance * timeScale && rainCount < rainLimit && m_drops.size() < maxDrops) {
        rainCount++;
        SpawnNewDrops(m_lastWidth, m_lastHeight);
    }
    
    // Update existing drops
    std::vector<Raindrop> newDrops;
    
    for (auto& drop : m_drops) {
        if (drop.killed) continue;
        
        float deltaR = m_maxDropSize - m_minDropSize;
        
        // Gravity - chance of drops "creeping down"
        if (chance01(m_rng) < (drop.radius - m_minDropSize) * (0.1f / deltaR) * timeScale) {
            std::uniform_real_distribution<float> momDist(0.0f, (drop.radius / m_maxDropSize) * 4.0f);
            drop.momentum += momDist(m_rng);
        }
        
        // Shrink
        drop.radius -= drop.shrink * timeScale;
        if (drop.radius <= 0) {
            drop.killed = true;
            continue;
        }
        
        // Spawn trail drops
        drop.lastSpawn += drop.momentum * timeScale * m_trailLength;
        if (drop.lastSpawn > drop.nextSpawn && drop.momentum > 0.5f) {
            std::uniform_real_distribution<float> trailR(0.2f, 0.5f);
            Raindrop trail;
            trail.x = drop.x + (chance01(m_rng) - 0.5f) * drop.radius * 0.002f;
            trail.y = drop.y - drop.radius * 0.005f;  // Increased gap behind parent
            trail.radius = drop.radius * trailR(m_rng);
            trail.momentum = 0.0f;
            trail.momentumX = 0.0f;
            trail.spreadX = 0.0f;
            trail.spreadY = drop.momentum * 0.3f;  // More vertical stretch
            trail.seed = chance01(m_rng);
            trail.shrink = 0.02f;  // Trail fades over time
            trail.lastSpawn = 0.0f;
            trail.nextSpawn = 100.0f;
            trail.killed = false;
            trail.isNew = true;
            trail.parent = &drop;
            newDrops.push_back(trail);
            
            drop.radius *= std::pow(0.97f, timeScale);
            drop.lastSpawn = 0.0f;
            drop.nextSpawn = m_minDropSize + chance01(m_rng) * deltaR - drop.momentum * 2.0f;
        }
        
        // Normalize spread
        drop.spreadX *= std::pow(0.4f, timeScale);
        drop.spreadY *= std::pow(0.7f, timeScale);
        
        // Update position
        bool moved = drop.momentum > 0;
        if (moved) {
            drop.y += drop.momentum * m_dropSpeed * 0.01f;
            drop.x += drop.momentumX * m_dropSpeed * 0.01f;
            if (drop.y > 1.1f) {
                drop.killed = true;
                continue;
            }
        }
        
        // Collision detection and incorporation (Codrops Merge logic)
        if (!drop.killed) {
            for (auto& drop2 : m_drops) {
                if (&drop == &drop2 || drop2.killed) continue;
                
                float dx = drop2.x - drop.x;
                float dy = drop2.y - drop.y;
                float d = std::sqrt(dx*dx + dy*dy);
                
                // Convert radius to normalized coordinates
                float normR1 = drop.radius / static_cast<float>(m_lastWidth);
                float normR2 = drop2.radius / static_cast<float>(m_lastWidth);
                float threshold = (normR1 + normR2) * m_collisionRadius;
                
                if (d < threshold) {
                    bool drop1Moving = drop.momentum > 0.1f;
                    bool drop2Moving = drop2.momentum > 0.1f;
                    
                    if (drop1Moving && !drop2Moving) {
                        // Inherit mass and speed
                        float a1 = drop.radius * drop.radius;
                        float a2 = drop2.radius * drop2.radius;
                        drop.radius = std::sqrt(a1 + a2 * 0.8f);
                        if (drop.radius > m_maxDropSize) drop.radius = m_maxDropSize;
                        
                        drop.momentum += (drop2.radius / m_maxDropSize) * 2.0f; // Speed up
                        drop2.killed = true;
                        
                        // "Spread" effect upon impact
                        drop.spreadX += 0.5f;
                    } 
                    else if (!drop1Moving && !drop2Moving && drop.radius >= drop2.radius) {
                        // Static merge
                        float a1 = drop.radius * drop.radius;
                        float a2 = drop2.radius * drop2.radius;
                        drop.radius = std::sqrt(a1 + a2);
                        drop2.killed = true;
                    }
                }
            }
        }
        
        // Slow down momentum
        drop.momentum -= (std::max)(1.0f, m_minDropSize * 0.5f - drop.momentum) * 0.15f * timeScale;
        if (drop.momentum < 0) drop.momentum = 0;
        drop.momentumX *= std::pow(0.7f, timeScale);
        
        drop.isNew = false;
        
        if (!drop.killed) {
            newDrops.push_back(drop);
        }
    }
    
    m_drops = newDrops;
}

void RainEffect::SpawnNewDrops(uint32_t width, uint32_t height) {
    // Codrops-compatible spawning
    std::uniform_real_distribution<float> posX(0.0f, 1.0f);
    std::uniform_real_distribution<float> posY(-0.1f, 0.95f);
    std::uniform_real_distribution<float> chance01(0.0f, 1.0f);
    
    // Pow distribution for radius (more small drops)
    float t = chance01(m_rng);
    float r = m_minDropSize + std::pow(t, 3.0f) * (m_maxDropSize - m_minDropSize);
    
    Raindrop drop;
    drop.x = posX(m_rng);
    drop.y = posY(m_rng);
    drop.radius = r;
    drop.momentum = 1.0f + ((r - m_minDropSize) * 0.1f) + chance01(m_rng) * 2.0f;
    drop.momentumX = 0.0f;
    drop.spreadX = 1.5f;
    drop.spreadY = 1.5f;
    drop.seed = chance01(m_rng);
    drop.shrink = 0.0f;
    drop.lastSpawn = 0.0f;
    drop.nextSpawn = chance01(m_rng) * (m_maxDropSize - m_minDropSize);
    drop.killed = false;
    drop.isNew = true;
    drop.parent = nullptr;
    
    m_drops.push_back(drop);
}

void RainEffect::MergeDrops() {
    // Handled in UpdateDrops collision detection
}

void RainEffect::RenderDropTexture(ID3D11DeviceContext* context, uint32_t width, uint32_t height) {
    // Debug log at entry - always log first 10 frames, then every 60
    static int renderLogCounter = 0;
    bool shouldLog = (renderLogCounter < 10) || (renderLogCounter % 60 == 0);
    renderLogCounter++;
    
    if (shouldLog) {
        LOG_INFO("RenderDropTexture[%d] - context=%p, dropRTV=%p, VS=%p, PS=%p, drops=%zu",
            renderLogCounter, context, m_dropRTV.Get(), m_raindropVS.Get(), m_raindropPS.Get(), m_drops.size());
    }
    
    if (!m_dropRTV || !context || !m_raindropVS || !m_raindropPS || m_drops.empty()) {
        if (shouldLog) {
            LOG_WARN("RenderDropTexture[%d] - Early return: dropRTV=%p, VS=%p, PS=%p, drops=%zu",
                renderLogCounter, m_dropRTV.Get(), m_raindropVS.Get(), m_raindropPS.Get(), m_drops.size());
        }
        return;
    }

    // Clear drop texture to neutral (0.5, 0.5, 0, 0)
    float clearColor[4] = { 0.5f, 0.5f, 0.0f, 0.0f };
    context->ClearRenderTargetView(m_dropRTV.Get(), clearColor);
    context->OMSetRenderTargets(1, m_dropRTV.GetAddressOf(), nullptr);

    // Prepare instance data
    std::vector<DropInstance> instances;
    instances.reserve(m_drops.size());
    for (const auto& drop : m_drops) {
        if (drop.killed) continue;
        instances.push_back({ drop.x, drop.y, drop.radius, drop.seed });
    }

    if (shouldLog) {
        LOG_INFO("RenderDropTexture - instances=%zu (after filter)", instances.size());
    }

    if (instances.empty()) return;

    // Update instance buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context->Map(m_instanceBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, instances.data(), sizeof(DropInstance) * instances.size());
        context->Unmap(m_instanceBuffer.Get(), 0);
    }

    // Update vertex shader constant buffer with resolution (reuse m_blurParamsBuffer)
    D3D11_MAPPED_SUBRESOURCE cbMapped;
    if (SUCCEEDED(context->Map(m_blurParamsBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMapped))) {
        struct DropParams {
            float resolution[2];
            float padding[2];
        };
        DropParams* p = static_cast<DropParams*>(cbMapped.pData);
        p->resolution[0] = static_cast<float>(width);
        p->resolution[1] = static_cast<float>(height);
        p->padding[0] = 0.0f;
        p->padding[1] = 0.0f;
        context->Unmap(m_blurParamsBuffer.Get(), 0);
    }

    // Set shaders and resources
    context->VSSetShader(m_raindropVS.Get(), nullptr, 0);
    context->VSSetShaderResources(0, 1, m_instanceSRV.GetAddressOf());
    context->VSSetConstantBuffers(0, 1, m_blurParamsBuffer.GetAddressOf());
    context->PSSetShader(m_raindropPS.Get(), nullptr, 0);

    // Render drops using instancing
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->DrawInstanced(4, static_cast<UINT>(instances.size()), 0, 0);

    // Cleanup resources
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->VSSetShaderResources(0, 1, &nullSRV);
}

bool RainEffect::CreateDropTexture(uint32_t width, uint32_t height) {
    // Phase 2: Create drop texture for refraction mapping
    
    if (!m_device) return false;
    
    // Only recreate if size changed
    if (m_lastWidth == width && m_lastHeight == height && m_dropTexture && m_blurredTexture) {
        return true;
    }
    
    m_lastWidth = width;
    m_lastHeight = height;
    
    // Release old resources
    m_dropTexture.Reset();
    m_dropSRV.Reset();
    m_dropRTV.Reset();
    m_blurredTexture.Reset();
    m_blurredSRV.Reset();
    m_blurredRTV.Reset();
    
    // Create drop texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    
    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, m_dropTexture.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // Create SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    
    hr = m_device->CreateShaderResourceView(m_dropTexture.Get(), &srvDesc, m_dropSRV.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // Create RTV
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = texDesc.Format;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    
    hr = m_device->CreateRenderTargetView(m_dropTexture.Get(), &rtvDesc, m_dropRTV.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // Create blurred background texture (same format)
    hr = m_device->CreateTexture2D(&texDesc, nullptr, m_blurredTexture.GetAddressOf());
    if (FAILED(hr)) return false;
    
    hr = m_device->CreateShaderResourceView(m_blurredTexture.Get(), &srvDesc, m_blurredSRV.GetAddressOf());
    if (FAILED(hr)) return false;
    
    hr = m_device->CreateRenderTargetView(m_blurredTexture.Get(), &rtvDesc, m_blurredRTV.GetAddressOf());
    if (FAILED(hr)) return false;
    
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

// ========== Droplets Layer Functions ==========

bool RainEffect::CreateDropletsGPUTexture(uint32_t width, uint32_t height) {
    if (!m_device) return false;
    
    // Use 1/4 resolution for droplets (performance optimization)
    uint32_t dropletW = width / 4;
    uint32_t dropletH = height / 4;
    if (dropletW == 0) dropletW = 1;
    if (dropletH == 0) dropletH = 1;
    
    // Check if already created with correct size
    if (m_dropletsGPUTexture && m_dropletsWidth == dropletW && m_dropletsHeight == dropletH) {
        return true;
    }
    
    m_dropletsWidth = dropletW;
    m_dropletsHeight = dropletH;
    
    // Release old resources
    m_dropletsGPUTexture.Reset();
    m_dropletsSRV.Reset();
    m_dropletsRTV.Reset();
    
    // Create texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = dropletW;
    texDesc.Height = dropletH;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    
    HRESULT hr = m_device->CreateTexture2D(&texDesc, nullptr, m_dropletsGPUTexture.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("CreateDropletsGPUTexture: CreateTexture2D failed (0x%08X)", hr);
        return false;
    }
    
    hr = m_device->CreateShaderResourceView(m_dropletsGPUTexture.Get(), nullptr, m_dropletsSRV.GetAddressOf());
    if (FAILED(hr)) return false;
    
    hr = m_device->CreateRenderTargetView(m_dropletsGPUTexture.Get(), nullptr, m_dropletsRTV.GetAddressOf());
    if (FAILED(hr)) return false;
    
    // Initialize droplets data
    m_dropletsData.resize(dropletW * dropletH * 4, 0);
    
    LOG_INFO("CreateDropletsGPUTexture: created %ux%u droplets texture", dropletW, dropletH);
    return true;
}

void RainEffect::WipeDroplets(float x, float y, float radius) {
    // Clear droplets in the path of a large drop
    if (m_dropletsData.empty() || m_dropletsWidth == 0 || m_dropletsHeight == 0) return;
    
    int cx = static_cast<int>(x * m_dropletsWidth);
    int cy = static_cast<int>(y * m_dropletsHeight);
    int r = static_cast<int>(radius * m_dropletsWidth * 2.0f);
    
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int px = cx + dx;
            int py = cy + dy;
            
            if (px >= 0 && px < static_cast<int>(m_dropletsWidth) &&
                py >= 0 && py < static_cast<int>(m_dropletsHeight)) {
                if (dx * dx + dy * dy <= r * r) {
                    size_t idx = (py * m_dropletsWidth + px) * 4;
                    // Fade out the alpha channel
                    m_dropletsData[idx + 3] = static_cast<uint8_t>(m_dropletsData[idx + 3] * 0.2f);
                }
            }
        }
    }
}

void RainEffect::RenderDropletsTexture(ID3D11DeviceContext* context, uint32_t width, uint32_t height) {
    if (!m_dropletsRTV || !context || !m_dropletsPS) return;
    
    // Create texture if needed
    if (!CreateDropletsGPUTexture(width, height)) return;
    
    // For now, just clear to neutral - in future, render actual droplet instances
    float clearColor[4] = { 0.5f, 0.5f, 0.0f, 0.0f };
    context->ClearRenderTargetView(m_dropletsRTV.Get(), clearColor);
    
    // TODO: In a future iteration, render droplet instances similar to RenderDropTexture
    // For now, droplets are handled by the CPU-side m_dropletsData and merged into the main texture
}

// Factory function for SubsystemFactory
std::unique_ptr<IBlurEffect> CreateRainEffect() {
    return std::make_unique<RainEffect>();
}

} // namespace blurwindow
