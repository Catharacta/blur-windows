#include "RainEffect.h"
#include "../core/ShaderLoader.h"
#include "../core/FullscreenRenderer.h"
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

// Refraction/composite pixel shader
// Applies the drop texture to create the final refraction effect
static const char* g_RefractionPS = R"(
Texture2D backgroundBlurred : register(t0);  // Blurred background
Texture2D backgroundFocus : register(t1);    // Sharp/focused background for drops
Texture2D dropTexture : register(t2);         // Drop normals/mask (RG=offset, A=mask)
SamplerState linearSampler : register(s0);

cbuffer RefractionParams : register(b0) {
    float refractionStrength;
    float blurAmount;
    float2 padding;
};

float4 main(float4 position : SV_Position, float2 texcoord : TEXCOORD0) : SV_Target {
    // Sample drop texture
    float4 drop = dropTexture.Sample(linearSampler, texcoord);
    
    // Get blurred background
    float4 blurred = backgroundBlurred.Sample(linearSampler, texcoord);
    
    // If no drop at this pixel, return blurred background
    if (drop.a < 0.01) {
        return blurred;
    }
    
    // Calculate refraction offset from drop texture
    // RG channels contain normal-mapped refraction direction (0.5 = no offset)
    float2 offset = (drop.rg - 0.5) * 2.0 * refractionStrength;
    
    // Apply refraction to sample from focused background
    // The drop acts like a lens, showing inverted/refracted view of background
    float2 refractedUV = texcoord + offset;
    refractedUV.y = 1.0 - refractedUV.y; // Invert Y (lens effect)
    
    // Clamp to valid UV range
    refractedUV = clamp(refractedUV, float2(0.001, 0.001), float2(0.999, 0.999));
    
    // Sample focused background through refraction
    float4 refracted = backgroundFocus.Sample(linearSampler, refractedUV);
    
    // Add slight darkening at edges for depth
    float edgeDarkening = 1.0 - drop.a * 0.2;
    refracted.rgb *= edgeDarkening;
    
    // Blend refracted view with blurred background based on drop alpha
    return lerp(blurred, refracted, drop.a);
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
    
    // Store dimensions for simulation
    m_lastWidth = width;
    m_lastHeight = height;
    
    // Create/update drop texture if needed
    if (!CreateDropTexture(width, height)) {
        return false;
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
            float padding[2];
        } params;
        params.refractionStrength = m_refractionStrength;
        params.blurAmount = m_strength;
        params.padding[0] = 0.0f;
        params.padding[1] = 0.0f;
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
    // Phase 2: Implement raindrop movement and lifecycle
    
    // Update spawn timer and spawn new drops
    m_spawnTimer += deltaTime;
    float spawnInterval = 0.1f / (m_rainIntensity + 0.01f); // More rain = more frequent spawns
    
    if (m_spawnTimer >= spawnInterval && m_lastWidth > 0 && m_lastHeight > 0) {
        SpawnNewDrops(m_lastWidth, m_lastHeight);
        m_spawnTimer = 0.0f;
    }
    
    // Update existing drops
    for (auto& drop : m_drops) {
        if (drop.isFalling) {
            // Apply gravity and update position
            drop.velocity += deltaTime * m_dropSpeed * 0.5f;
            drop.y += drop.velocity * deltaTime;
            
            // Update trail
            drop.trailY = drop.y - m_trailLength * drop.velocity * 0.1f;
            
            // Lifetime for removal
            drop.lifetime += deltaTime;
            
            // Random chance to stop falling and become static
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            if (dist(m_rng) < 0.01f * deltaTime || drop.y > 1.2f) {
                drop.isFalling = false;
            }
        } else {
            // Static drops slowly fade/shrink
            drop.lifetime += deltaTime;
            drop.radius *= (1.0f - 0.1f * deltaTime);
        }
    }
    
    // Remove drops that are too small or off-screen
    m_drops.erase(
        std::remove_if(m_drops.begin(), m_drops.end(), [](const Raindrop& d) {
            return d.radius < 1.0f || d.y > 1.5f || d.lifetime > 10.0f;
        }),
        m_drops.end()
    );
    
    // Merge nearby drops
    MergeDrops();
    
    // Update static drops (small background drops)
    for (auto& drop : m_staticDrops) {
        drop.lifetime += deltaTime;
        drop.radius *= (1.0f - 0.02f * deltaTime);
    }
    
    // Remove faded static drops
    m_staticDrops.erase(
        std::remove_if(m_staticDrops.begin(), m_staticDrops.end(), [](const Raindrop& d) {
            return d.radius < 0.5f;
        }),
        m_staticDrops.end()
    );
}

void RainEffect::SpawnNewDrops(uint32_t width, uint32_t height) {
    // Phase 2: Spawn new raindrops based on intensity
    
    std::uniform_real_distribution<float> posX(0.0f, 1.0f);
    std::uniform_real_distribution<float> posY(-0.1f, 0.1f); // Start just above screen
    std::uniform_real_distribution<float> sizeDist(m_minDropSize, m_maxDropSize);
    std::uniform_real_distribution<float> velDist(0.5f, 2.0f);
    std::uniform_real_distribution<float> seedDist(0.0f, 1.0f);
    
    // Spawn 1-3 large drops
    int dropCount = static_cast<int>(1 + m_rainIntensity * 2);
    for (int i = 0; i < dropCount && m_drops.size() < 100; ++i) {
        Raindrop drop;
        drop.x = posX(m_rng);
        drop.y = posY(m_rng);
        drop.radius = sizeDist(m_rng);
        drop.velocity = velDist(m_rng);
        drop.seed = seedDist(m_rng);
        drop.isFalling = true;
        drop.trailY = drop.y;
        drop.lifetime = 0.0f;
        m_drops.push_back(drop);
    }
    
    // Spawn small static drops (background texture)
    std::uniform_real_distribution<float> smallSize(1.0f, 3.0f);
    int staticCount = static_cast<int>(5 * m_rainIntensity);
    for (int i = 0; i < staticCount && m_staticDrops.size() < 500; ++i) {
        Raindrop drop;
        drop.x = posX(m_rng);
        drop.y = posX(m_rng); // Anywhere on screen
        drop.radius = smallSize(m_rng);
        drop.velocity = 0.0f;
        drop.seed = seedDist(m_rng);
        drop.isFalling = false;
        drop.trailY = drop.y;
        drop.lifetime = 0.0f;
        m_staticDrops.push_back(drop);
    }
}

void RainEffect::MergeDrops() {
    // Phase 2: Merge overlapping drops (gooey effect)
    
    for (size_t i = 0; i < m_drops.size(); ++i) {
        for (size_t j = i + 1; j < m_drops.size(); ++j) {
            Raindrop& a = m_drops[i];
            Raindrop& b = m_drops[j];
            
            // Calculate distance (normalized coordinates)
            float dx = a.x - b.x;
            float dy = a.y - b.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            
            // Check if drops overlap (normalize radius to screen space)
            float radiusSum = (a.radius + b.radius) / static_cast<float>(m_lastWidth) * 2.0f;
            
            if (dist < radiusSum) {
                // Merge: larger drop absorbs smaller
                if (a.radius > b.radius) {
                    // Grow radius based on absorbed volume
                    float volumeSum = a.radius * a.radius + b.radius * b.radius;
                    a.radius = std::sqrt(volumeSum);
                    a.velocity = (a.velocity * a.radius + b.velocity * b.radius) / (a.radius + b.radius);
                    b.radius = 0; // Mark for removal
                } else {
                    float volumeSum = a.radius * a.radius + b.radius * b.radius;
                    b.radius = std::sqrt(volumeSum);
                    b.velocity = (a.velocity * a.radius + b.velocity * b.radius) / (a.radius + b.radius);
                    a.radius = 0; // Mark for removal
                }
            }
        }
    }
    
    // Remove merged drops
    m_drops.erase(
        std::remove_if(m_drops.begin(), m_drops.end(), [](const Raindrop& d) {
            return d.radius <= 0;
        }),
        m_drops.end()
    );
}

void RainEffect::RenderDropTexture(ID3D11DeviceContext* context, uint32_t width, uint32_t height) {
    // Phase 3: Render drops to texture for refraction
    // This function generates a texture where:
    // R = Y offset for refraction
    // G = X offset for refraction  
    // A = drop mask
    
    if (!m_dropRTV || !context || !m_raindropPS) return;
    
    // Clear drop texture to neutral (0.5, 0.5, 0, 0) - no refraction, no mask
    float clearColor[4] = { 0.5f, 0.5f, 0.0f, 0.0f };
    context->ClearRenderTargetView(m_dropRTV.Get(), clearColor);
    
    // Set drop texture as render target
    context->OMSetRenderTargets(1, m_dropRTV.GetAddressOf(), nullptr);
    
    // Set up additive blending for overlapping drops
    // For now, we use CPU-based drop texture generation
    // (Full GPU instancing would be more efficient but complex)
    
    // Generate drop texture on CPU and upload
    // This is a simplified approach - ideal would be GPU instanced rendering
    std::vector<uint8_t> dropData(width * height * 4, 0);
    
    // Render large drops
    for (const auto& drop : m_drops) {
        int centerX = static_cast<int>(drop.x * width);
        int centerY = static_cast<int>(drop.y * height);
        int radius = static_cast<int>(drop.radius);
        
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                int px = centerX + dx;
                int py = centerY + dy;
                
                if (px < 0 || px >= static_cast<int>(width) || 
                    py < 0 || py >= static_cast<int>(height)) continue;
                
                float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (dist > radius) continue;
                
                float normDist = dist / radius;
                float height_val = std::sqrt(1.0f - normDist * normDist);
                
                // Calculate normal
                float nx = (radius > 0) ? dx / static_cast<float>(radius) : 0.0f;
                float ny = (radius > 0) ? dy / static_cast<float>(radius) : 0.0f;
                float len = std::sqrt(nx * nx + ny * ny + height_val * height_val);
                if (len > 0) {
                    nx /= len;
                    ny /= len;
                }
                
                // Map to 0-255 range
                uint8_t r = static_cast<uint8_t>((ny * 0.5f + 0.5f) * 255);
                uint8_t g = static_cast<uint8_t>((nx * 0.5f + 0.5f) * 255);
                uint8_t a = static_cast<uint8_t>(height_val * (1.0f - normDist * normDist * 0.5f) * 255);
                
                size_t idx = (py * width + px) * 4;
                // Blend with existing (max blend for alpha)
                dropData[idx + 0] = (std::max)(dropData[idx + 0], r);
                dropData[idx + 1] = (std::max)(dropData[idx + 1], g);
                dropData[idx + 2] = 0;
                dropData[idx + 3] = (std::max)(dropData[idx + 3], a);
            }
        }
    }
    
    // Render static drops (smaller, simpler)
    for (const auto& drop : m_staticDrops) {
        int centerX = static_cast<int>(drop.x * width);
        int centerY = static_cast<int>(drop.y * height);
        int radius = static_cast<int>(drop.radius);
        
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                int px = centerX + dx;
                int py = centerY + dy;
                
                if (px < 0 || px >= static_cast<int>(width) || 
                    py < 0 || py >= static_cast<int>(height)) continue;
                
                float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (dist > radius) continue;
                
                float normDist = dist / radius;
                float alpha = (1.0f - normDist) * 0.3f; // Subtle static drops
                
                size_t idx = (py * width + px) * 4;
                uint8_t a = static_cast<uint8_t>(alpha * 255);
                dropData[idx + 3] = (std::max)(dropData[idx + 3], a);
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
