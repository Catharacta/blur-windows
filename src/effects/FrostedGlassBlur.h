#pragma once

#include "IBlurEffect.h"
#include "../core/FullscreenRenderer.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

namespace blurwindow {

/// Frosted glass blur effect with Voronoi distortion
/// Creates a realistic frosted glass appearance
class FrostedGlassBlur : public IBlurEffect {
public:
    FrostedGlassBlur() = default;
    ~FrostedGlassBlur() override = default;

    const char* GetName() const override { return "FrostedGlass"; }
    bool Initialize(ID3D11Device* device) override;
    bool Apply(ID3D11DeviceContext* context,
               ID3D11ShaderResourceView* input,
               ID3D11RenderTargetView* output,
               uint32_t width, uint32_t height) override;
    
    void SetStrength(float strength) override { m_strength = strength; m_dirty = true; }
    void SetColor(float r, float g, float b, float a) override;
    void SetNoiseIntensity(float intensity) override { m_noiseIntensity = intensity; }
    void SetNoiseScale(float scale) override { m_noiseScale = scale; }
    void SetNoiseSpeed(float speed) override { m_noiseSpeed = speed; }
    void SetNoiseType(int type) override { m_noiseType = type; }
    void SetOpacity(float opacity) override { m_opacity = opacity; m_dirty = true; }
    void Update(float deltaTime) override { m_time += deltaTime * m_noiseSpeed; }
    bool SetParameters(const char* json) override;
    std::string GetParameters() const override;

private:
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_frostedPS;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    
    // Per-instance FullscreenRenderer (fixes cross-device resource sharing)
    FullscreenRenderer m_fullscreenRenderer;
    bool m_rendererInitialized = false;
    
    float m_blurRadius = 10.0f;
    float m_strength = 1.0f;
    float m_distortionStrength = 0.02f; // Reverted to original default (was 5.0f)
    float m_cellScale = 50.0f;          // Reverted to original default (was 10.0f)
    float m_time = 0.0f;
    float m_opacity = 1.0f;
    float m_tintColor[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
    
    // Noise parameters
    float m_noiseIntensity = 0.0f;
    float m_noiseScale = 100.0f;
    float m_noiseSpeed = 1.0f;
    int m_noiseType = 0;
    
    bool m_dirty = true;
};

// Factory function
std::unique_ptr<IBlurEffect> CreateFrostedGlassBlur();

} // namespace blurwindow
