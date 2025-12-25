#include "IBlurEffect.h"
#include <vector>
#include <cmath>

namespace blurwindow {

/// Separable Gaussian blur effect (2-pass)
class GaussianBlur : public IBlurEffect {
public:
    GaussianBlur() = default;
    ~GaussianBlur() override = default;

    const char* GetName() const override {
        return "Gaussian";
    }

    bool Initialize(ID3D11Device* device) override {
        m_device = device;
        
        // TODO: Load compiled shaders
        // For now, we'll create shaders at runtime or load from embedded resources
        
        // Create constant buffer for blur parameters
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(BlurParams);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = m_device->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
        if (FAILED(hr)) return false;

        // Create sampler state
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

        hr = m_device->CreateSamplerState(&samplerDesc, m_sampler.GetAddressOf());
        if (FAILED(hr)) return false;

        UpdateKernel();
        return true;
    }

    bool Apply(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* input,
        ID3D11RenderTargetView* output,
        uint32_t width,
        uint32_t height
    ) override {
        if (!m_horizontalPS || !m_verticalPS) {
            // Shaders not loaded, fallback to passthrough
            return true;
        }

        // Ensure intermediate texture exists
        EnsureIntermediateTexture(width, height);

        // Update constant buffer
        UpdateConstantBuffer(context, width, height);

        // Pass 1: Horizontal blur
        context->PSSetShader(m_horizontalPS.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &input);
        context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
        context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
        context->OMSetRenderTargets(1, m_intermediateRTV.GetAddressOf(), nullptr);
        
        // Draw fullscreen quad
        DrawFullscreenQuad(context);

        // Pass 2: Vertical blur
        context->PSSetShader(m_verticalPS.Get(), nullptr, 0);
        ID3D11ShaderResourceView* intermediateSRV = m_intermediateSRV.Get();
        context->PSSetShaderResources(0, 1, &intermediateSRV);
        context->OMSetRenderTargets(1, &output, nullptr);
        
        DrawFullscreenQuad(context);

        // Cleanup
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context->PSSetShaderResources(0, 1, &nullSRV);

        return true;
    }

    bool SetParameters(const char* json) override {
        // TODO: Parse JSON
        // Example: {"sigma": 5.0, "downscale": 0.5}
        return true;
    }

    std::string GetParameters() const override {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), 
            "{\"sigma\": %.2f, \"downscale\": %.2f}",
            m_sigma, m_downscale);
        return buffer;
    }

    void SetSigma(float sigma) {
        m_sigma = std::max(0.1f, sigma);
        UpdateKernel();
    }

    float GetSigma() const { return m_sigma; }

    void SetDownscale(float factor) {
        m_downscale = std::clamp(factor, 0.1f, 1.0f);
    }

    float GetDownscale() const { return m_downscale; }

private:
    struct BlurParams {
        float sigma;
        float texelSize[2];
        int sampleCount;
        float weights[64];  // Max kernel size
    };

    void UpdateKernel() {
        // Calculate kernel size based on sigma (3-sigma rule)
        m_sampleCount = static_cast<int>(std::ceil(m_sigma * 3.0f));
        m_sampleCount = std::min(m_sampleCount, 32);  // Max 32 samples per side

        // Calculate Gaussian weights
        m_weights.resize(m_sampleCount * 2 + 1);
        float sum = 0.0f;
        
        for (int i = -m_sampleCount; i <= m_sampleCount; i++) {
            float weight = std::exp(-0.5f * (i * i) / (m_sigma * m_sigma));
            m_weights[i + m_sampleCount] = weight;
            sum += weight;
        }

        // Normalize
        for (auto& w : m_weights) {
            w /= sum;
        }
    }

    void UpdateConstantBuffer(ID3D11DeviceContext* context, uint32_t width, uint32_t height) {
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            BlurParams* params = static_cast<BlurParams*>(mapped.pData);
            params->sigma = m_sigma;
            params->texelSize[0] = 1.0f / width;
            params->texelSize[1] = 1.0f / height;
            params->sampleCount = m_sampleCount;
            
            size_t copyCount = std::min(m_weights.size(), (size_t)64);
            memcpy(params->weights, m_weights.data(), copyCount * sizeof(float));
            
            context->Unmap(m_constantBuffer.Get(), 0);
        }
    }

    void EnsureIntermediateTexture(uint32_t width, uint32_t height) {
        if (m_intermediateWidth == width && m_intermediateHeight == height) {
            return;
        }

        m_intermediateTexture.Reset();
        m_intermediateSRV.Reset();
        m_intermediateRTV.Reset();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        m_device->CreateTexture2D(&desc, nullptr, m_intermediateTexture.GetAddressOf());
        m_device->CreateShaderResourceView(m_intermediateTexture.Get(), nullptr, m_intermediateSRV.GetAddressOf());
        m_device->CreateRenderTargetView(m_intermediateTexture.Get(), nullptr, m_intermediateRTV.GetAddressOf());

        m_intermediateWidth = width;
        m_intermediateHeight = height;
    }

    void DrawFullscreenQuad(ID3D11DeviceContext* context) {
        // TODO: Use a proper fullscreen quad setup
        // For now, assume vertex shader handles fullscreen triangle
        context->Draw(3, 0);
    }

    ID3D11Device* m_device = nullptr;
    
    ComPtr<ID3D11PixelShader> m_horizontalPS;
    ComPtr<ID3D11PixelShader> m_verticalPS;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_sampler;

    // Intermediate texture for 2-pass blur
    ComPtr<ID3D11Texture2D> m_intermediateTexture;
    ComPtr<ID3D11ShaderResourceView> m_intermediateSRV;
    ComPtr<ID3D11RenderTargetView> m_intermediateRTV;
    uint32_t m_intermediateWidth = 0;
    uint32_t m_intermediateHeight = 0;

    // Blur parameters
    float m_sigma = 5.0f;
    float m_downscale = 1.0f;
    int m_sampleCount = 15;
    std::vector<float> m_weights;
};

} // namespace blurwindow
