#pragma once

#include <d3d11.h>
#include <wrl/client.h>

namespace blurwindow {

using Microsoft::WRL::ComPtr;

/// Abstract interface for presentation/display
class IPresenter {
public:
    virtual ~IPresenter() = default;

    /// Initialize the presenter
    /// @param hwnd Target window handle
    /// @param device D3D11 device
    /// @return true on success
    virtual bool Initialize(HWND hwnd, ID3D11Device* device) = 0;

    /// Present a texture to the window
    /// @param texture Texture to present
    /// @return true on success
    virtual bool Present(ID3D11Texture2D* texture) = 0;

    /// Resize the presentation surface
    /// @param width New width
    /// @param height New height
    /// @return true on success
    virtual bool Resize(uint32_t width, uint32_t height) = 0;

    /// Release resources
    virtual void Shutdown() = 0;
};

} // namespace blurwindow
