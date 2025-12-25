#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

using Microsoft::WRL::ComPtr;

namespace blurwindow {

/// Manages fullscreen rendering pipeline
class FullscreenRenderer {
public:
    FullscreenRenderer() = default;
    ~FullscreenRenderer() = default;

    bool Initialize(ID3D11Device* device);
    
    /// Set viewport for rendering
    void SetViewport(ID3D11DeviceContext* context, uint32_t width, uint32_t height);
    
    /// Bind the fullscreen vertex shader and draw
    void DrawFullscreen(ID3D11DeviceContext* context);

    /// Get the vertex shader for external use
    ID3D11VertexShader* GetVertexShader() const { return m_vertexShader.Get(); }

private:
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;
    ComPtr<ID3D11BlendState> m_blendState;
};

} // namespace blurwindow
