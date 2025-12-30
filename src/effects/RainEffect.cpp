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

// Raindrop rendering pixel shader
// Renders drops to a texture where R=Y refraction, G=X refraction, A=mask
static const char* g_RaindropPS = R"(
cbuffer DropParams : register(b0) {
    float2 dropCenter;   // Normalized position (0-1)
    float dropRadius;    // Radius in normalized screen space
    float dropSeed;      // Random variation
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    // Calculate distance from drop center
    float2 diff = texcoord - dropCenter;
    float dist = length(diff);
    
    // Outside drop radius - fully transparent
    if (dist > dropRadius) {
        return float4(0.5, 0.5, 0.0, 0.0);
    }
    
    // Normalized distance within drop (0 at center, 1 at edge)
    float normDist = dist / dropRadius;
    
    // Sphere-like height profile (hemisphere)
    float height = sqrt(1.0 - normDist * normDist);
    
    // Calculate refraction offset based on sphere normal
    // Normal of hemisphere: (diff.x/r, diff.y/r, height) normalized
    float3 normal = normalize(float3(diff / dropRadius, height));
    
    // Refraction direction (light bending through water drop)
    // R channel = Y offset, G channel = X offset (following normal mapping convention)
    float refractionX = normal.x * 0.5 + 0.5;  // Map -1..1 to 0..1
    float refractionY = normal.y * 0.5 + 0.5;
    
    // Alpha is drop visibility (stronger at center, fades at edge)
    float alpha = height * (1.0 - normDist * normDist * 0.5);
    
    return float4(refractionY, refractionX, 0.0, alpha);
}
)";

// Refraction/composite pixel shader - Codrops compatible
// Applies the drop texture to create the final refraction effect
static const char* g_RefractionPS = R"(
Texture2D backgroundBlurred : register(t0);  // Background (will be blurred in shader)
Texture2D backgroundFocus : register(t1);    // Sharp/focused background for drops
Texture2D dropTexture : register(t2);         // Drop data (R=Y offset, G=X offset, B=depth, A=mask)
SamplerState linearSampler : register(s0);

cbuffer RefractionParams : register(b0) {
    float refractionStrength;  // 0-1, scaled to pixel range
    float blurAmount;          // Background blur amount
    float2 resolution;         // Screen resolution for pixel calculations
};

// Simple 5-tap blur for background (simulates looking through glass)
float4 blurBackground(float2 uv) {
    float2 pixelSize = 1.0 / resolution;
    float blurRadius = blurAmount * 8.0 + 2.0; // 2-10 pixel blur
    
    float4 color = float4(0, 0, 0, 0);
    float total = 0.0;
    
    // Sample in a cross pattern for performance
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            float2 offset = float2(i, j) * pixelSize * blurRadius * 0.5;
            float weight = 1.0 / (1.0 + abs(i) + abs(j));
            color += backgroundBlurred.Sample(linearSampler, uv + offset) * weight;
            total += weight;
        }
    }
    return color / total;
}

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    // Sample drop texture
    float4 drop = dropTexture.Sample(linearSampler, texcoord);
    
    // Get blurred background (looking through frosted glass effect)
    float4 blurred = blurBackground(texcoord);
    
    // If no drop at this pixel, return blurred background
    if (drop.a < 0.01) {
        return blurred;
    }
    
    // Codrops-style refraction calculation
    float2 refraction = (drop.rg - 0.5) * 2.0;  // Normalized refraction direction
    float depth = drop.b;                        // Drop depth/thickness
    
    // Calculate pixel size for proper scaling
    float2 pixelSize = 1.0 / resolution;
    
    // Codrops uses minRefraction=256, maxRefraction=512
    // We scale this by refractionStrength (default ~0.5)
    float minRefraction = 128.0 * refractionStrength;
    float refractionDelta = 256.0 * refractionStrength;
    float refractionAmount = minRefraction + depth * refractionDelta;
    
    // Apply refraction
    float2 refractedUV = texcoord + pixelSize * refraction * refractionAmount;
    
    // Clamp to valid UV range
    refractedUV = clamp(refractedUV, float2(0.001, 0.001), float2(0.999, 0.999));
    
    // Sample blurred background through refraction
    // Water drops show refracted view of the already-blurred background
    float4 refracted = blurBackground(refractedUV);
    
    // Codrops-style alpha processing: alphaMultiply=20, alphaSubtract=5
    float alpha = saturate(drop.a * 20.0 - 5.0);
    
    // Add subtle highlight at top of drop
    float highlight = saturate(drop.r - 0.5) * 2.0 * alpha * 0.15;
    refracted.rgb += float3(highlight, highlight, highlight);
    
    // Blend refracted view with blurred background
    return lerp(blurred, refracted, alpha);
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
    
    // Raindrop rendering shader
    if (!ShaderLoader::CompilePixelShader(device, g_RaindropPS, strlen(g_RaindropPS), "main", m_raindropPS.GetAddressOf())) {
        return false;
    }
    
    // Refraction/composite shader
    if (!ShaderLoader::CompilePixelShader(device, g_RefractionPS, strlen(g_RefractionPS), "main", m_refractionPS.GetAddressOf())) {
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
    
    // Create constant buffer for refraction parameters
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = 16; // 4 floats: refractionStrength, blurAmount, padding[2]
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    
    // Create drop params constant buffer
    D3D11_BUFFER_DESC dropCbDesc = {};
    dropCbDesc.ByteWidth = 16; // dropCenter(2) + dropRadius(1) + dropSeed(1)
    dropCbDesc.Usage = D3D11_USAGE_DYNAMIC;
    dropCbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    dropCbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = device->CreateBuffer(&dropCbDesc, nullptr, m_dropParamsBuffer.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    
    // Initialize fullscreen renderer
    if (!m_fullscreenRenderer.Initialize(device)) {
        LOG_ERROR("RainEffect::Initialize - FullscreenRenderer init failed");
        return false;
    }
    
    LOG_INFO("RainEffect::Initialize - Success (refractionPS=%p, raindropPS=%p)",
        m_refractionPS.Get(), m_raindropPS.Get());
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
    
    // Create/update drop texture if needed (before setting m_lastWidth/Height)
    if (!CreateDropTexture(width, height)) {
        LOG_ERROR("RainEffect::Apply - CreateDropTexture failed");
        return false;
    }
    
    // Store dimensions for simulation (after texture creation)
    m_lastWidth = width;
    m_lastHeight = height;
    
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
    
    // ===== Pass 1: Render raindrops to drop texture =====
    RenderDropTexture(context, width, height);
    
    // ===== Pass 2: Apply refraction and composite =====
    // Set output as render target
    context->OMSetRenderTargets(1, &output, nullptr);
    
    // Update refraction constant buffer
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    if (SUCCEEDED(context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
        struct RefractionParams {
            float refractionStrength;
            float blurAmount;
            float resolutionX;
            float resolutionY;
        } params;
        params.refractionStrength = m_refractionStrength;
        params.blurAmount = m_strength;
        params.resolutionX = static_cast<float>(width);
        params.resolutionY = static_cast<float>(height);
        memcpy(mappedResource.pData, &params, sizeof(params));
        context->Unmap(m_constantBuffer.Get(), 0);
    }
    
    // Set shader resources
    // t0 = blurred background (using input as both blurred and focus for now)
    // t1 = focused background
    // t2 = drop texture
    ID3D11ShaderResourceView* srvs[3] = { input, input, m_dropSRV.Get() };
    context->PSSetShaderResources(0, 3, srvs);
    context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    context->PSSetShader(m_refractionPS.Get(), nullptr, 0);
    
    // Draw fullscreen quad
    m_fullscreenRenderer.DrawFullscreen(context);
    
    // Cleanup
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
    
    // Update raindrop simulation
    UpdateDrops(deltaTime);
}

void RainEffect::UpdateDrops(float deltaTime) {
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
    
    // Spawn background droplets
    m_dropletsCounter += m_dropletsRate * timeScale * m_rainIntensity;
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
    
    // Spawn rain drops (chance-based like Codrops)
    std::uniform_real_distribution<float> chance01(0.0f, 1.0f);
    float rainChance = 0.3f * m_rainIntensity;
    int rainLimit = 3;
    int rainCount = 0;
    while (chance01(m_rng) < rainChance * timeScale && rainCount < rainLimit && m_drops.size() < 900) {
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
            trail.y = drop.y - drop.radius * 0.001f;
            trail.radius = drop.radius * trailR(m_rng);
            trail.momentum = 0.0f;
            trail.momentumX = 0.0f;
            trail.spreadX = 0.0f;
            trail.spreadY = drop.momentum * 0.1f;
            trail.seed = chance01(m_rng);
            trail.shrink = 0.0f;
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
        
        // Collision detection
        if ((moved || drop.isNew) && !drop.killed) {
            for (auto& drop2 : m_drops) {
                if (&drop == &drop2 || drop2.killed) continue;
                if (drop.radius <= drop2.radius) continue;
                if (drop.parent == &drop2 || drop2.parent == &drop) continue;
                
                float dx = drop2.x - drop.x;
                float dy = drop2.y - drop.y;
                float d = std::sqrt(dx*dx + dy*dy);
                // Convert radius to normalized coordinates for comparison
                float normRadius1 = drop.radius / static_cast<float>(m_lastWidth);
                float normRadius2 = drop2.radius / static_cast<float>(m_lastWidth);
                float threshold = (normRadius1 + normRadius2) * m_collisionRadius;
                
                if (d < threshold) {
                    // Merge drops
                    float a1 = 3.14159f * drop.radius * drop.radius;
                    float a2 = 3.14159f * drop2.radius * drop2.radius;
                    float targetR = std::sqrt((a1 + a2 * 0.8f) / 3.14159f);
                    if (targetR > m_maxDropSize) targetR = m_maxDropSize;
                    
                    drop.radius = targetR;
                    drop.momentumX += dx * 0.1f;
                    drop.spreadX = 0;
                    drop.spreadY = 0;
                    drop2.killed = true;
                    drop.momentum = (std::max)(drop2.momentum, (std::min)(40.0f, drop.momentum + targetR * 0.05f + 1.0f));
                    
                    // Clear droplets in path
                    if (m_dropletsWidth > 0) {
                        int cx = static_cast<int>(drop.x * m_dropletsWidth);
                        int cy = static_cast<int>(drop.y * m_dropletsHeight);
                        int clearR = static_cast<int>(drop.radius * 0.43f);
                        for (int cdy = -clearR; cdy <= clearR; ++cdy) {
                            for (int cdx = -clearR; cdx <= clearR; ++cdx) {
                                int px = cx + cdx;
                                int py = cy + cdy;
                                if (px >= 0 && px < (int)m_dropletsWidth && py >= 0 && py < (int)m_dropletsHeight) {
                                    size_t idx = (py * m_dropletsWidth + px) * 4;
                                    m_dropletsData[idx + 3] = 0; // Clear alpha
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Slow down momentum
        drop.momentum -= (std::max)(1.0f, m_minDropSize * 0.5f - drop.momentum) * 0.1f * timeScale;
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
    // Render drops to texture for refraction (Codrops compatible)
    // R = Y offset for refraction
    // G = X offset for refraction  
    // B = depth/thickness
    // A = drop mask
    
    if (!m_dropRTV || !context || !m_raindropPS) return;
    
    // Clear drop texture to neutral (0.5, 0.5, 0, 0) - no refraction, no mask
    float clearColor[4] = { 0.5f, 0.5f, 0.0f, 0.0f };
    context->ClearRenderTargetView(m_dropRTV.Get(), clearColor);
    
    // Set drop texture as render target
    context->OMSetRenderTargets(1, m_dropRTV.GetAddressOf(), nullptr);
    
    // Generate drop texture on CPU
    std::vector<uint8_t> dropData(width * height * 4);
    
    // First, copy droplets background
    if (!m_dropletsData.empty() && m_dropletsWidth == width && m_dropletsHeight == height) {
        for (size_t i = 0; i < dropData.size(); i += 4) {
            dropData[i + 0] = 128;  // R = 0.5
            dropData[i + 1] = 128;  // G = 0.5
            dropData[i + 2] = 0;
            dropData[i + 3] = m_dropletsData[i + 3];  // Copy alpha from droplets
        }
    } else {
        for (size_t i = 0; i < dropData.size(); i += 4) {
            dropData[i + 0] = 128;
            dropData[i + 1] = 128;
            dropData[i + 2] = 0;
            dropData[i + 3] = 0;
        }
    }
    
    // Render drops with teardrop shape (scaleX=1, scaleY=1.5, spread)
    for (const auto& drop : m_drops) {
        if (drop.killed) continue;
        
        int centerX = static_cast<int>(drop.x * width);
        int centerY = static_cast<int>(drop.y * height);
        float radius = drop.radius;
        
        // Codrops uses scaleX=1, scaleY=1.5 for teardrop shape
        float scaleX = 1.0f * (drop.spreadX + 1.0f);
        float scaleY = 1.5f * (drop.spreadY + 1.0f);
        
        int extentX = static_cast<int>(radius * scaleX);
        int extentY = static_cast<int>(radius * scaleY);
        
        for (int dy = -extentY; dy <= extentY; ++dy) {
            for (int dx = -extentX; dx <= extentX; ++dx) {
                int px = centerX + dx;
                int py = centerY + dy;
                
                if (px < 0 || px >= static_cast<int>(width) || 
                    py < 0 || py >= static_cast<int>(height)) continue;
                
                // Normalize to ellipse coordinates
                float ex = dx / (radius * scaleX);
                float ey = dy / (radius * scaleY);
                float dist = std::sqrt(ex * ex + ey * ey);
                
                if (dist > 1.0f) continue;
                
                // Smoother height profile for more natural look
                float t = 1.0f - dist;
                float height_val = t * t * (3.0f - 2.0f * t); // Smoothstep
                
                // Add slight variation for organic look
                float variation = 1.0f + std::sin(ex * 3.14159f * 2.0f) * 0.05f;
                height_val *= variation;
                
                // Calculate normal for refraction
                float nx = ex * (1.0f - height_val * 0.5f);
                float ny = ey * (1.0f - height_val * 0.5f);
                float len = std::sqrt(nx * nx + ny * ny + height_val * height_val);
                if (len > 0) {
                    nx /= len;
                    ny /= len;
                }
                
                // Map to 0-255 range with softer edges
                float edgeFade = (1.0f - dist) * (1.0f - dist);
                uint8_t r = static_cast<uint8_t>((ny * 0.5f + 0.5f) * 255);
                uint8_t g = static_cast<uint8_t>((nx * 0.5f + 0.5f) * 255);
                uint8_t b = static_cast<uint8_t>(height_val * 255);
                uint8_t a = static_cast<uint8_t>(height_val * edgeFade * 255);
                
                size_t idx = (py * width + px) * 4;
                if (a > dropData[idx + 3]) {
                    dropData[idx + 0] = r;
                    dropData[idx + 1] = g;
                    dropData[idx + 2] = b;
                    dropData[idx + 3] = a;
                }
            }
        }
    }
    
    // Upload to GPU texture
    context->UpdateSubresource(m_dropTexture.Get(), 0, nullptr, 
        dropData.data(), width * 4, 0);
}

bool RainEffect::CreateDropTexture(uint32_t width, uint32_t height) {
    // Phase 2: Create drop texture for refraction mapping
    
    if (!m_device) return false;
    
    // Only recreate if size changed
    if (m_lastWidth == width && m_lastHeight == height && m_dropTexture) {
        return true;
    }
    
    m_lastWidth = width;
    m_lastHeight = height;
    
    // Release old resources
    m_dropTexture.Reset();
    m_dropSRV.Reset();
    m_dropRTV.Reset();
    
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
