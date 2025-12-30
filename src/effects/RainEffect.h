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
    void SetDropSizeRange(float minSize, float maxSize);

private:
    // Raindrop data structure
    struct Raindrop {
        float x, y;           // Position (0-1 normalized)
        float radius;         // Radius in pixels
        float velocity;       // Fall velocity
        float seed;           // Random seed for variation
        bool isFalling;       // Currently falling/trailing
        float trailY;         // Trail end Y position
        float lifetime;       // Time since creation
    };

    // Simulation
    void UpdateDrops(float deltaTime);
    void SpawnNewDrops(uint32_t width, uint32_t height);
    void MergeDrops();
    void RenderDropTexture(ID3D11DeviceContext* context, uint32_t width, uint32_t height);
    bool CreateDropTexture(uint32_t width, uint32_t height);

    // GPU resources
    ComPtr<ID3D11PixelShader> m_refractionPS;
    ComPtr<ID3D11PixelShader> m_raindropPS;
    ComPtr<ID3D11PixelShader> m_compositorPS;
    ComPtr<ID3D11Texture2D> m_dropTexture;
    ComPtr<ID3D11ShaderResourceView> m_dropSRV;
    ComPtr<ID3D11RenderTargetView> m_dropRTV;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11Buffer> m_dropParamsBuffer;
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
    std::vector<Raindrop> m_staticDrops;   // Small static drops

    // Parameters
    float m_strength = 1.0f;
    float m_tintColor[4] = { 0, 0, 0, 0 };
    float m_noiseIntensity = 0.0f;
    float m_noiseScale = 100.0f;
    float m_noiseSpeed = 1.0f;
    int m_noiseType = 0;
    float m_time = 0.0f;

    // Rain-specific parameters
    float m_rainIntensity = 0.5f;      // 0-1: density of drops
    float m_dropSpeed = 1.0f;          // Fall speed multiplier
    float m_refractionStrength = 0.5f; // Refraction intensity
    float m_trailLength = 0.3f;        // Trail length (0-1)
    float m_minDropSize = 5.0f;        // Minimum drop radius (pixels)
    float m_maxDropSize = 20.0f;       // Maximum drop radius (pixels)

    // Simulation state
    float m_spawnTimer = 0.0f;
    std::mt19937 m_rng;
    uint32_t m_lastWidth = 0;
    uint32_t m_lastHeight = 0;
};

} // namespace blurwindow
