#pragma once

#include "IBlurEffect.h"
#include "../core/FullscreenRenderer.h"
#include <vector>
#include <random>

namespace blurwindow {

/// Rain effect - simulates raindrops on a glass surface with refraction
class RainEffect : public IBlurEffect {
public:
    RainEffect() = default;
    ~RainEffect() override = default;

    // IBlurEffect interface
    const char* GetName() const override { return "Rain"; }
    bool Initialize(ID3D11Device* device) override;
    bool Apply(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* input,
        ID3D11RenderTargetView* output,
        uint32_t width,
        uint32_t height
    ) override;

    void SetStrength(float strength) override { m_strength = strength; }
    void SetColor(float r, float g, float b, float a) override;
    void SetNoiseIntensity(float intensity) override { m_noiseIntensity = intensity; }
    void SetNoiseScale(float scale) override { m_noiseScale = scale; }
    void SetNoiseSpeed(float speed) override { m_noiseSpeed = speed; }
    void SetNoiseType(int type) override { m_noiseType = type; }
    void Update(float deltaTime) override;
    bool SetParameters(const char* json) override;
    std::string GetParameters() const override;

    // Rain-specific settings
    void SetRainIntensity(float intensity) { m_rainIntensity = intensity; }
    void SetDropSpeed(float speed) { m_dropSpeed = speed; }
    void SetRefractionStrength(float strength) { m_refractionStrength = strength; }
    void SetTrailLength(float length) { m_trailLength = length; }
    void SetDimensions(uint32_t width, uint32_t height) {
        m_lastWidth = width;
        m_lastHeight = height;
    }
    void SetDropSizeRange(float minSize, float maxSize);

private:
    // Raindrop data structure (Codrops compatible)
    struct Raindrop {
        float x, y;           // Position (0-1 normalized)
        float radius;         // Radius in pixels
        float momentum;       // Fall velocity (momentum)
        float momentumX;      // Horizontal momentum (for collision)
        float spreadX;        // X spread for teardrop shape
        float spreadY;        // Y spread for teardrop shape
        float seed;           // Random seed for variation
        float shrink;         // Shrink rate
        float lastSpawn;      // Time since last trail spawn
        float nextSpawn;      // Time until next trail spawn
        bool killed;          // Marked for removal
        bool isNew;           // Just created (for collision check)
        Raindrop* parent;     // Parent drop (for trails)
    };

    struct DropInstance {
        float x, y;
        float radius;
        float seed;
    };

    // Simulation
    void UpdateDrops(float deltaTime);
    void SpawnNewDrops(uint32_t width, uint32_t height);
    void MergeDrops();
    void RenderDropTexture(ID3D11DeviceContext* context, uint32_t width, uint32_t height);
    bool CreateDropTexture(uint32_t width, uint32_t height);

    // GPU resources
    ComPtr<ID3D11VertexShader> m_raindropVS;
    ComPtr<ID3D11PixelShader> m_refractionPS;
    ComPtr<ID3D11PixelShader> m_raindropPS;
    ComPtr<ID3D11PixelShader> m_boxBlurPS;
    ComPtr<ID3D11Texture2D> m_dropTexture;
    ComPtr<ID3D11ShaderResourceView> m_dropSRV;
    ComPtr<ID3D11RenderTargetView> m_dropRTV;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11Buffer> m_blurParamsBuffer;
    ComPtr<ID3D11Buffer> m_instanceBuffer;
    ComPtr<ID3D11ShaderResourceView> m_instanceSRV;
    ComPtr<ID3D11SamplerState> m_sampler;

    // Intermediate textures
    ComPtr<ID3D11Texture2D> m_blurredTexture;
    ComPtr<ID3D11ShaderResourceView> m_blurredSRV;
    ComPtr<ID3D11RenderTargetView> m_blurredRTV;

    ID3D11Device* m_device = nullptr;

    // Fullscreen renderer
    FullscreenRenderer m_fullscreenRenderer;

    // Raindrop collections
    std::vector<Raindrop> m_drops;         // Large moving drops

    // Droplets texture (background small drops)
    std::vector<uint8_t> m_dropletsData;   // CPU-side droplets texture
    uint32_t m_dropletsWidth = 0;
    uint32_t m_dropletsHeight = 0;

    // Parameters
    float m_strength = 1.0f;
    float m_tintColor[4] = { 0, 0, 0, 0 };
    float m_noiseIntensity = 0.0f;
    float m_noiseScale = 100.0f;
    float m_noiseSpeed = 1.0f;
    int m_noiseType = 0;
    float m_time = 0.0f;

    // Rain-specific parameters (Codrops compatible)
    float m_rainIntensity = 0.5f;      // 0-1: density of drops
    float m_dropSpeed = 1.0f;          // Fall speed multiplier
    float m_refractionStrength = 0.5f; // Refraction intensity
    float m_shininess = 32.0f;         // Specular shininess
    float m_trailLength = 0.3f;        // Trail length (0-1)
    float m_minDropSize = 0.02f;       // Minimum drop radius (normalized)
    float m_maxDropSize = 0.08f;       // Maximum drop radius (normalized)
    float m_dropletsRate = 50.0f;      // Background droplets spawn rate
    float m_collisionRadius = 0.65f;   // Collision detection radius

    // Simulation state
    float m_spawnTimer = 0.0f;
    float m_dropletsCounter = 0.0f;
    std::mt19937 m_rng;
    uint32_t m_lastWidth = 0;
    uint32_t m_lastHeight = 0;
};

} // namespace blurwindow
