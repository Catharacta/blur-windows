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
    void SetNoiseIntensity(float intensity) override { m_distortionStrength = intensity * 0.1f; }
    void SetNoiseScale(float scale) override { m_cellScale = scale; }
    void SetNoiseSpeed(float speed) override { m_animSpeed = speed; }
    void SetNoiseType(int /*type*/) override { /* Not used */ }
    void SetOpacity(float opacity) override { m_opacity = opacity; m_dirty = true; }
    void Update(float deltaTime) override { m_time += deltaTime * m_animSpeed; }
    bool SetParameters(const char* json) override;
    std::string GetParameters() const override;

private:
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_frostedPS;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    
    // Per-instance FullscreenRenderer (fixes cross-device resource sharing)
    FullscreenRenderer m_fullscreenRenderer;
    bool m_rendererInitialized = false;
    
    float m_strength = 1.0f;
    float m_opacity = 1.0f;
    float m_blurRadius = 8.0f;
    float m_distortionStrength = 0.02f;  // Voronoi distortion amount
    float m_cellScale = 50.0f;           // Voronoi cell size
    float m_animSpeed = 0.5f;
    float m_tintColor[4] = { 0, 0, 0, 0 };
    float m_time = 0.0f;
    bool m_dirty = true;
};

// Factory function
std::unique_ptr<IBlurEffect> CreateFrostedGlassBlur();

} // namespace blurwindow
