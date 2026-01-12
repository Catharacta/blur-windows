#include "blurwindow/blur_window.h"
#include "Logger.h"
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

        m_options = opts;

        // D3D11 Device creation moved to BlurWindow to ensure thread safety (one device per window)
        // See BlurWindow::InitializeGraphicsBasics

        m_initialized = true;

        m_initialized = true;
        
        // Sync options with Logger
        Logger::Instance().Enable(m_options.enableLogging);
        if (m_options.logPath) Logger::Instance().SetOutputPath(m_options.logPath);
        if (m_options.logCallback) Logger::Instance().SetCallback(m_options.logCallback);

        return true;
    }

    void SetOptions(const BlurSystemOptions& opts) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_options = opts;
        
        Logger::Instance().Enable(m_options.enableLogging);
        if (m_options.logPath) Logger::Instance().SetOutputPath(m_options.logPath);
        if (m_options.logCallback) Logger::Instance().SetCallback(m_options.logCallback);
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_windows.clear();
        m_windows.clear();
        // Devices are now owned by individual windows
        m_initialized = false;
    }

    bool IsInitialized() const {
        return m_initialized;
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

void BlurSystem::SetOptions(const BlurSystemOptions& opts) {
    m_impl->SetOptions(opts);
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
