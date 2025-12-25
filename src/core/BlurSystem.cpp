#include "blurwindow/blurwindow.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace blurwindow {

// Forward declarations
class BlurWindow;

class BlurSystem::Impl {
public:
    Impl() = default;
    ~Impl() { Shutdown(); }

    bool Initialize(const BlurSystemOptions& opts) {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_initialized) {
            return true;
        }

        m_options = opts;

        // Create D3D11 device
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        HRESULT hr = D3D11CreateDevice(
            nullptr,                    // Default adapter
            D3D_DRIVER_TYPE_HARDWARE,   // Hardware device
            nullptr,                    // No software rasterizer
            createFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            m_device.GetAddressOf(),
            &m_featureLevel,
            m_context.GetAddressOf()
        );

        if (FAILED(hr)) {
            // Try without debug layer
            createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                createFlags, featureLevels, ARRAYSIZE(featureLevels),
                D3D11_SDK_VERSION,
                m_device.GetAddressOf(), &m_featureLevel, m_context.GetAddressOf()
            );
        }

        if (FAILED(hr)) {
            return false;
        }

        m_initialized = true;
        return true;
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_windows.clear();
        m_context.Reset();
        m_device.Reset();
        m_initialized = false;
    }

    bool IsInitialized() const {
        return m_initialized;
    }

    ID3D11Device* GetDevice() const {
        return m_device.Get();
    }

    ID3D11DeviceContext* GetContext() const {
        return m_context.Get();
    }

    void RegisterWindow(BlurWindow* window) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_windows.push_back(window);
    }

    void UnregisterWindow(BlurWindow* window) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_windows.erase(
            std::remove(m_windows.begin(), m_windows.end(), window),
            m_windows.end()
        );
    }

private:
    std::mutex m_mutex;
    bool m_initialized = false;
    BlurSystemOptions m_options{};
    
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;
    
    std::vector<BlurWindow*> m_windows;
};

// Singleton instance
BlurSystem& BlurSystem::Instance() {
    static BlurSystem instance;
    return instance;
}

BlurSystem::BlurSystem() : m_impl(std::make_unique<Impl>()) {}

BlurSystem::~BlurSystem() = default;

bool BlurSystem::Initialize(const BlurSystemOptions& opts) {
    return m_impl->Initialize(opts);
}

void BlurSystem::Shutdown() {
    m_impl->Shutdown();
}

bool BlurSystem::IsInitialized() const {
    return m_impl->IsInitialized();
}

std::unique_ptr<BlurWindow> BlurSystem::CreateBlurWindow(HWND owner, const WindowOptions& opts) {
    if (!m_impl->IsInitialized()) {
        return nullptr;
    }
    
    auto window = std::unique_ptr<BlurWindow>(new BlurWindow(owner, opts));
    m_impl->RegisterWindow(window.get());
    return window;
}

} // namespace blurwindow
