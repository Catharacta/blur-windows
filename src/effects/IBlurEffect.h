#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace blurwindow {

using Microsoft::WRL::ComPtr;

/// Abstract interface for blur effects
class IBlurEffect {
public:
    virtual ~IBlurEffect() = default;

    /// Get effect name
    virtual const char* GetName() const = 0;

    /// Initialize the effect
    /// @param device D3D11 device
    /// @return true on success
    virtual bool Initialize(ID3D11Device* device) = 0;

    /// Apply the effect
    /// @param context D3D11 device context
    /// @param input Input texture SRV
    /// @param output Output render target
    /// @param width Output width
    /// @param height Output height
    /// @return true on success
    virtual bool Apply(
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* input,
        ID3D11RenderTargetView* output,
        uint32_t width,
        uint32_t height
    ) = 0;

    /// Set effect parameters from JSON
    /// @param json JSON parameter string
    /// @return true on success
    virtual bool SetParameters(const char* json) = 0;

    /// Get current parameters as JSON
    virtual std::string GetParameters() const = 0;
};

} // namespace blurwindow
