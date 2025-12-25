#include <d3d11.h>
#include <wrl/client.h>
#include <unordered_map>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace blurwindow {

/// Resource manager for sharing GPU resources
class ResourceManager {
public:
    ResourceManager() = default;
    ~ResourceManager() = default;

    bool Initialize(ID3D11Device* device) {
        m_device = device;
        return m_device != nullptr;
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_textures.clear();
        m_device = nullptr;
    }

    /// Get or create a texture with the specified dimensions
    ID3D11Texture2D* GetTexture(uint32_t width, uint32_t height, DXGI_FORMAT format) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        TextureKey key{width, height, format};
        
        auto it = m_textures.find(key);
        if (it != m_textures.end()) {
            return it->second.Get();
        }

        // Create new texture
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf());
        if (FAILED(hr)) {
            return nullptr;
        }

        auto result = texture.Get();
        m_textures[key] = std::move(texture);
        return result;
    }

private:
    struct TextureKey {
        uint32_t width;
        uint32_t height;
        DXGI_FORMAT format;

        bool operator==(const TextureKey& other) const {
            return width == other.width && height == other.height && format == other.format;
        }
    };

    struct TextureKeyHash {
        size_t operator()(const TextureKey& key) const {
            return std::hash<uint32_t>()(key.width) ^
                   (std::hash<uint32_t>()(key.height) << 1) ^
                   (std::hash<uint32_t>()(key.format) << 2);
        }
    };

    std::mutex m_mutex;
    ID3D11Device* m_device = nullptr;
    std::unordered_map<TextureKey, ComPtr<ID3D11Texture2D>, TextureKeyHash> m_textures;
};

} // namespace blurwindow
