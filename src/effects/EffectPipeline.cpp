#include "IBlurEffect.h"
#include <vector>
#include <memory>
#include <string>

namespace blurwindow {

/// Effect pipeline - chains multiple effects together
class EffectPipeline {
public:
    EffectPipeline() = default;
    ~EffectPipeline() = default;

    bool Initialize(ID3D11Device* device) {
        m_device = device;
        
        // Initialize all effects
        for (auto& effect : m_effects) {
            if (!effect->Initialize(device)) {
                return false;
            }
        }
        
        return true;
    }

    void AddEffect(std::unique_ptr<IBlurEffect> effect) {
        if (m_device) {
            effect->Initialize(m_device);
        }
        m_effects.push_back(std::move(effect));
    }

    void Clear() {
        m_effects.clear();
    }

    bool Execute(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* input,
        ID3D11RenderTargetView* output,
        uint32_t width,
        uint32_t height
    ) {
        if (m_effects.empty()) {
            // No effects, just copy input to output
            // TODO: Implement passthrough
            return true;
        }

        // Single effect optimization
        if (m_effects.size() == 1) {
            return m_effects[0]->Apply(context, input, output, width, height);
        }

        // Multiple effects - need intermediate buffers
        EnsureIntermediateBuffers(width, height);

        ID3D11ShaderResourceView* currentInput = input;
        ID3D11RenderTargetView* currentOutput = nullptr;

        for (size_t i = 0; i < m_effects.size(); i++) {
            bool isLast = (i == m_effects.size() - 1);
            
            if (isLast) {
                currentOutput = output;
            } else {
                currentOutput = m_intermediateRTVs[i % 2].Get();
            }

            if (!m_effects[i]->Apply(context, currentInput, currentOutput, width, height)) {
                return false;
            }

            if (!isLast) {
                currentInput = m_intermediateSRVs[i % 2].Get();
            }
        }

        return true;
    }

    std::string ToJson() const {
        // TODO: Serialize pipeline to JSON
        std::string json = "{\n  \"effects\": [\n";
        
        for (size_t i = 0; i < m_effects.size(); i++) {
            json += "    {\"name\": \"" + std::string(m_effects[i]->GetName()) + "\", ";
            json += "\"params\": " + m_effects[i]->GetParameters() + "}";
            if (i < m_effects.size() - 1) json += ",";
            json += "\n";
        }
        
        json += "  ]\n}";
        return json;
    }

    static std::unique_ptr<EffectPipeline> FromJson(const std::string& json, ID3D11Device* device) {
        auto pipeline = std::make_unique<EffectPipeline>();
        pipeline->m_device = device;
        
        // TODO: Parse JSON and create effects
        // For now, return empty pipeline
        
        return pipeline;
    }

private:
    void EnsureIntermediateBuffers(uint32_t width, uint32_t height) {
        if (m_bufferWidth == width && m_bufferHeight == height) {
            return;
        }

        m_intermediateTextures[0].Reset();
        m_intermediateTextures[1].Reset();
        m_intermediateSRVs[0].Reset();
        m_intermediateSRVs[1].Reset();
        m_intermediateRTVs[0].Reset();
        m_intermediateRTVs[1].Reset();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        for (int i = 0; i < 2; i++) {
            m_device->CreateTexture2D(&desc, nullptr, m_intermediateTextures[i].GetAddressOf());
            m_device->CreateShaderResourceView(m_intermediateTextures[i].Get(), nullptr, m_intermediateSRVs[i].GetAddressOf());
            m_device->CreateRenderTargetView(m_intermediateTextures[i].Get(), nullptr, m_intermediateRTVs[i].GetAddressOf());
        }

        m_bufferWidth = width;
        m_bufferHeight = height;
    }

    ID3D11Device* m_device = nullptr;
    std::vector<std::unique_ptr<IBlurEffect>> m_effects;

    // Ping-pong buffers for multi-effect chains
    ComPtr<ID3D11Texture2D> m_intermediateTextures[2];
    ComPtr<ID3D11ShaderResourceView> m_intermediateSRVs[2];
    ComPtr<ID3D11RenderTargetView> m_intermediateRTVs[2];
    uint32_t m_bufferWidth = 0;
    uint32_t m_bufferHeight = 0;
};

} // namespace blurwindow
