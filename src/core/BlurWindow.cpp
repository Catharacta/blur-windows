#include "blurwindow/blur_window.h"
#include "blurwindow/blurwindow.h"
#include "../capture/ICaptureSubsystem.h"
#include "../effects/IBlurEffect.h"
#include "../presentation/IPresenter.h"
#include "FullscreenRenderer.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace blurwindow {

// Forward declarations for implementations
class DXGICapture;
class GaussianBlur;
class ULWPresenter;

// Factory functions (defined in respective .cpp files)
std::unique_ptr<ICaptureSubsystem> CreateDXGICapture();
std::unique_ptr<IBlurEffect> CreateGaussianBlur();
std::unique_ptr<IPresenter> CreateULWPresenter();

class BlurWindow::Impl {
public:
    Impl(HWND owner, const WindowOptions& opts)
        : m_owner(owner)
        , m_options(opts)
        , m_preset(QualityPreset::Balanced)
        , m_running(false)
        , m_currentFPS(0.0f)
    {
        CreateBlurWindow();
        InitializeGraphics();
    }

    ~Impl() {
        Stop();
        ShutdownGraphics();
        DestroyBlurWindow();
    }

    void Start() {
        if (m_running.exchange(true)) {
            return; // Already running
        }
        
        m_renderThread = std::thread([this]() {
            RenderLoop();
        });
    }

    void Stop() {
        m_running = false;
        if (m_renderThread.joinable()) {
            m_renderThread.join();
        }
    }

    bool IsRunning() const {
        return m_running;
    }

    void SetPreset(QualityPreset preset) {
        m_preset = preset;
        UpdatePresetSettings();
    }

    QualityPreset GetPreset() const {
        return m_preset;
    }

    void SetClickThrough(bool enable) {
        if (!m_hwnd) return;
        
        LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
        if (enable) {
            exStyle |= WS_EX_TRANSPARENT;
        } else {
            exStyle &= ~WS_EX_TRANSPARENT;
        }
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle);
    }

    void SetTopMost(bool enable) {
        if (!m_hwnd) return;
        
        SetWindowPos(m_hwnd,
            enable ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        );
    }

    void SetBounds(const RECT& bounds) {
        m_options.bounds = bounds;
        m_width = bounds.right - bounds.left;
        m_height = bounds.bottom - bounds.top;
        
        if (m_hwnd) {
            SetWindowPos(m_hwnd, nullptr,
                bounds.left, bounds.top,
                m_width, m_height,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
        }
        
        // Recreate output texture
        if (m_device) {
            CreateOutputTexture();
        }
    }

    RECT GetBounds() const {
        return m_options.bounds;
    }

    HWND GetHWND() const {
        return m_hwnd;
    }

    float GetCurrentFPS() const {
        return m_currentFPS;
    }

    bool SetEffectPipeline(const std::string& jsonConfig) {
        // TODO: Parse JSON and configure pipeline
        return true;
    }

private:
    bool InitializeGraphics() {
        // Get device from BlurSystem
        m_device = BlurSystem::Instance().GetDevice();
        if (!m_device) return false;
        
        m_device->GetImmediateContext(m_context.GetAddressOf());
        
        // Calculate dimensions
        m_width = m_options.bounds.right - m_options.bounds.left;
        m_height = m_options.bounds.bottom - m_options.bounds.top;
        if (m_width == 0) m_width = 400;
        if (m_height == 0) m_height = 300;
        
        // Create output texture
        if (!CreateOutputTexture()) return false;
        
        // Initialize capture
        m_capture = CreateDXGICapture();
        if (m_capture && !m_capture->Initialize(m_device)) {
            OutputDebugStringA("Failed to initialize DXGI capture\n");
            m_capture.reset();
        }
        
        // Set self window for capture avoidance
        if (m_capture && m_hwnd) {
            m_capture->SetSelfWindow(m_hwnd);
        }
        
        // Initialize effect
        m_effect = CreateGaussianBlur();
        if (m_effect && !m_effect->Initialize(m_device)) {
            OutputDebugStringA("Failed to initialize Gaussian blur\n");
            m_effect.reset();
        }
        
        // Initialize presenter
        m_presenter = CreateULWPresenter();
        if (m_presenter && !m_presenter->Initialize(m_hwnd, m_device)) {
            OutputDebugStringA("Failed to initialize presenter\n");
            m_presenter.reset();
        }
        
        m_graphicsInitialized = (m_capture && m_effect && m_presenter);
        return m_graphicsInitialized;
    }

    void ShutdownGraphics() {
        if (m_presenter) m_presenter->Shutdown();
        if (m_capture) m_capture->Shutdown();
        
        m_outputRTV.Reset();
        m_outputSRV.Reset();
        m_outputTexture.Reset();
        m_context.Reset();
        m_device = nullptr;
        
        m_capture.reset();
        m_effect.reset();
        m_presenter.reset();
    }

    bool CreateOutputTexture() {
        m_outputTexture.Reset();
        m_outputSRV.Reset();
        m_outputRTV.Reset();

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = m_width;
        desc.Height = m_height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, m_outputTexture.GetAddressOf());
        if (FAILED(hr)) return false;

        hr = m_device->CreateShaderResourceView(m_outputTexture.Get(), nullptr, m_outputSRV.GetAddressOf());
        if (FAILED(hr)) return false;

        hr = m_device->CreateRenderTargetView(m_outputTexture.Get(), nullptr, m_outputRTV.GetAddressOf());
        return SUCCEEDED(hr);
    }

    void CreateBlurWindow() {
        // Register window class
        static bool classRegistered = false;
        static const wchar_t* CLASS_NAME = L"BlurWindowClass";
        
        if (!classRegistered) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = CLASS_NAME;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            RegisterClassExW(&wc);
            classRegistered = true;
        }

        // Create layered window
        DWORD exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        if (m_options.topMost) {
            exStyle |= WS_EX_TOPMOST;
        }
        if (m_options.clickThrough) {
            exStyle |= WS_EX_TRANSPARENT;
        }

        m_hwnd = CreateWindowExW(
            exStyle,
            CLASS_NAME,
            L"BlurWindow",
            WS_POPUP,
            m_options.bounds.left,
            m_options.bounds.top,
            m_options.bounds.right - m_options.bounds.left,
            m_options.bounds.bottom - m_options.bounds.top,
            m_owner,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr
        );
    }

    void DestroyBlurWindow() {
        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    void RenderLoop() {
        using clock = std::chrono::high_resolution_clock;
        
        auto lastFrame = clock::now();
        int frameCount = 0;
        auto lastFPSUpdate = clock::now();

        while (m_running) {
            auto now = clock::now();
            
            // Calculate target frame time based on preset
            int targetFPS = GetTargetFPS();
            auto targetFrameTime = std::chrono::microseconds(1000000 / targetFPS);
            
            // Render frame
            if (m_graphicsInitialized) {
                RenderFrame();
            }

            frameCount++;
            
            // Update FPS every second
            auto fpsDelta = std::chrono::duration_cast<std::chrono::seconds>(now - lastFPSUpdate);
            if (fpsDelta.count() >= 1) {
                m_currentFPS = static_cast<float>(frameCount) / fpsDelta.count();
                frameCount = 0;
                lastFPSUpdate = now;
            }

            // Frame timing
            auto frameTime = clock::now() - now;
            if (frameTime < targetFrameTime) {
                std::this_thread::sleep_for(targetFrameTime - frameTime);
            }
            
            lastFrame = now;
        }
    }

    void RenderFrame() {
        // 1. Capture desktop
        ID3D11Texture2D* capturedTexture = nullptr;
        if (!m_capture->CaptureFrame(m_options.bounds, &capturedTexture)) {
            return;  // No new frame
        }

        // 2. Create SRV for captured texture
        ComPtr<ID3D11ShaderResourceView> inputSRV;
        HRESULT hr = m_device->CreateShaderResourceView(capturedTexture, nullptr, inputSRV.GetAddressOf());
        if (FAILED(hr)) return;

        // 3. Apply blur effect
        if (!m_effect->Apply(m_context.Get(), inputSRV.Get(), m_outputRTV.Get(), m_width, m_height)) {
            return;
        }

        // 4. Present to window
        m_presenter->Present(m_outputTexture.Get());
    }

    int GetTargetFPS() const {
        switch (m_preset) {
            case QualityPreset::High:       return 60;
            case QualityPreset::Balanced:   return 60;
            case QualityPreset::Performance: return 30;
            case QualityPreset::Minimal:    return 15;
            default:                        return 60;
        }
    }

    void UpdatePresetSettings() {
        // Update blur sigma based on preset
        float sigma = 5.0f;
        switch (m_preset) {
            case QualityPreset::High:       sigma = 8.0f; break;
            case QualityPreset::Balanced:   sigma = 5.0f; break;
            case QualityPreset::Performance: sigma = 3.0f; break;
            case QualityPreset::Minimal:    sigma = 2.0f; break;
        }
        
        // TODO: Pass sigma to effect
    }

    HWND m_owner = nullptr;
    HWND m_hwnd = nullptr;
    WindowOptions m_options;
    QualityPreset m_preset;

    std::thread m_renderThread;
    std::atomic<bool> m_running;
    std::atomic<float> m_currentFPS;

    // Graphics resources
    ID3D11Device* m_device = nullptr;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Texture2D> m_outputTexture;
    ComPtr<ID3D11ShaderResourceView> m_outputSRV;
    ComPtr<ID3D11RenderTargetView> m_outputRTV;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_graphicsInitialized = false;

    // Subsystems
    std::unique_ptr<ICaptureSubsystem> m_capture;
    std::unique_ptr<IBlurEffect> m_effect;
    std::unique_ptr<IPresenter> m_presenter;
};

BlurWindow::BlurWindow(HWND owner, const WindowOptions& opts)
    : m_impl(std::make_unique<Impl>(owner, opts))
{
}

BlurWindow::~BlurWindow() = default;

void BlurWindow::Start() {
    m_impl->Start();
}

void BlurWindow::Stop() {
    m_impl->Stop();
}

bool BlurWindow::IsRunning() const {
    return m_impl->IsRunning();
}

bool BlurWindow::SetEffectPipeline(const std::string& jsonConfig) {
    return m_impl->SetEffectPipeline(jsonConfig);
}

void BlurWindow::SetPreset(QualityPreset preset) {
    m_impl->SetPreset(preset);
}

QualityPreset BlurWindow::GetPreset() const {
    return m_impl->GetPreset();
}

void BlurWindow::SetClickThrough(bool enable) {
    m_impl->SetClickThrough(enable);
}

void BlurWindow::SetTopMost(bool enable) {
    m_impl->SetTopMost(enable);
}

void BlurWindow::SetBounds(const RECT& bounds) {
    m_impl->SetBounds(bounds);
}

RECT BlurWindow::GetBounds() const {
    return m_impl->GetBounds();
}

HWND BlurWindow::GetHWND() const {
    return m_impl->GetHWND();
}

float BlurWindow::GetCurrentFPS() const {
    return m_impl->GetCurrentFPS();
}

// Factory function implementations
std::unique_ptr<ICaptureSubsystem> CreateDXGICapture() {
    // Will be properly linked from DXGICapture.cpp
    return nullptr;  // TODO: Implement factory
}

std::unique_ptr<IBlurEffect> CreateGaussianBlur() {
    // Will be properly linked from GaussianBlur.cpp
    return nullptr;  // TODO: Implement factory
}

std::unique_ptr<IPresenter> CreateULWPresenter() {
    // Will be properly linked from ULWPresenter.cpp
    return nullptr;  // TODO: Implement factory
}

} // namespace blurwindow

