#include "blurwindow/blur_window.h"
#include "blurwindow/blurwindow.h"
#include "../capture/ICaptureSubsystem.h"
#include "../effects/IBlurEffect.h"
#include "../presentation/IPresenter.h"
#include <thread>
#include <atomic>
#include <chrono>

namespace blurwindow {

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
    }

    ~Impl() {
        Stop();
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
        if (m_hwnd) {
            SetWindowPos(m_hwnd, nullptr,
                bounds.left, bounds.top,
                bounds.right - bounds.left,
                bounds.bottom - bounds.top,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
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
            // TODO: Implement actual rendering
            // 1. Capture desktop
            // 2. Apply blur effects
            // 3. Present to window

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
        // TODO: Update blur parameters based on preset
    }

    HWND m_owner = nullptr;
    HWND m_hwnd = nullptr;
    WindowOptions m_options;
    QualityPreset m_preset;

    std::thread m_renderThread;
    std::atomic<bool> m_running;
    std::atomic<float> m_currentFPS;

    // TODO: Add capture, effects, presenter
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

} // namespace blurwindow
