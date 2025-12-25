#include "FullscreenRenderer.h"
#include "ShaderLoader.h"

namespace blurwindow {

// Embedded fullscreen vertex shader source
static const char* g_FullscreenVS = R"(
struct VSOutput {
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID) {
    VSOutput output;
    output.texcoord = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.texcoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}
)";

bool FullscreenRenderer::Initialize(ID3D11Device* device) {
    // Compile embedded vertex shader
    if (!ShaderLoader::CompileVertexShader(
        device, g_FullscreenVS, strlen(g_FullscreenVS),
        "main", m_vertexShader.GetAddressOf(), nullptr
    )) {
        return false;
    }

    // Create rasterizer state (no culling for fullscreen)
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.DepthClipEnable = FALSE;

    HRESULT hr = device->CreateRasterizerState(&rastDesc, m_rasterizerState.GetAddressOf());
    if (FAILED(hr)) return false;

    // Create blend state (alpha blending)
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = device->CreateBlendState(&blendDesc, m_blendState.GetAddressOf());
    return SUCCEEDED(hr);
}

void FullscreenRenderer::SetViewport(ID3D11DeviceContext* context, uint32_t width, uint32_t height) {
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;

    context->RSSetViewports(1, &viewport);
    context->RSSetState(m_rasterizerState.Get());
    
    float blendFactor[4] = {0, 0, 0, 0};
    context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
}

void FullscreenRenderer::DrawFullscreen(ID3D11DeviceContext* context) {
    // Set vertex shader
    context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    
    // Set input layout to null (no vertex buffer needed)
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    
    // Draw 3 vertices (fullscreen triangle)
    context->Draw(3, 0);
}

} // namespace blurwindow
