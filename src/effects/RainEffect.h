#pragma once

#include "IBlurEffect.h"
#include "../core/FullscreenRenderer.h"
#include <vector>
#include <random>

namespace blurwindow {

/// Rain effect - procedural GPU-based simulation (Heartfelt algorithm)
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
    void SetOpacity(float opacity) override { m_opacity = opacity; }
    void Update(float deltaTime) override;
    bool SetParameters(const char* json) override;
    std::string GetParameters() const override;

    // Rain-specific settings
    void SetRainIntensity(float intensity) { m_rainIntensity = intensity; }
    void SetDropSpeed(float speed) { m_dropSpeed = speed; }
    // Legacy setters kept for API compatibility, mapped to new shader parameters
    void SetRefractionStrength(float strength) { m_normalStrength = strength; } 
    void SetTrailLength(float length) { /* No-op in procedural version or mapped if applicable */ }
    void SetDropSizeRange(float minSize, float maxSize) { /* No-op in procedural version */ }
    
    // New parameters
    void SetZoom(float zoom) { m_zoom = zoom; }
    void SetBrightness(float brightness) { m_brightness = brightness; }

private:
    // GPU resources
    ComPtr<ID3D11PixelShader> m_rainPS;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;

    ID3D11Device* m_device = nullptr;

    // Fullscreen renderer
    FullscreenRenderer m_fullscreenRenderer;

    // Parameters
    float m_strength = 1.0f;
    float m_opacity = 1.0f;
    float m_tintColor[4] = { 0, 0, 0, 0 };
    
    // Shader parameters
    float m_time = 0.0f;
    float m_rainIntensity = 0.5f;      
    float m_dropSpeed = 1.0f;          
    float m_normalStrength = 2.0f;     
    float m_zoom = 1.0f;
    float m_brightness = 1.0f;
    
    // Legacy mapping or unused
    float m_noiseIntensity = 0.0f;
    float m_noiseScale = 100.0f;
    float m_noiseSpeed = 1.0f;
    int m_noiseType = 0;
};

} // namespace blurwindow
