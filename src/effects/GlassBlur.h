#pragma once

#include "IBlurEffect.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

namespace blurwindow {

/// Glass blur effect (Rain effect's blur without raindrops)
/// Simple 5-tap cross pattern blur for performance
class GlassBlur : public IBlurEffect {
public:
    GlassBlur() = default;
    ~GlassBlur() override = default;

    const char* GetName() const override { return "Glass"; }
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
    void Update(float deltaTime) override { m_time += deltaTime; }
    bool SetParameters(const char* json) override;
    std::string GetParameters() const override;

private:
    void UpdateConstantBuffer(ID3D11DeviceContext* context);

    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_glassPS;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;
    
    float m_strength = 1.0f;
    float m_blurRadius = 5.0f;  // Controlled by blur_param
    float m_tintColor[4] = { 0, 0, 0, 0 };
    float m_noiseIntensity = 0.0f;
    float m_noiseScale = 100.0f;
    float m_noiseSpeed = 1.0f;
    int m_noiseType = 0;
    float m_time = 0.0f;
    bool m_dirty = true;
};

// Factory function
std::unique_ptr<IBlurEffect> CreateGlassBlur();

} // namespace blurwindow
