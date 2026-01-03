#include "blurwindow/blur_window.h"
#include "blurwindow/blurwindow.h"
#include "Logger.h"
#include "SubsystemFactory.h"
#include "FullscreenRenderer.h"
#include "../effects/RainEffect.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <d3d11.h>
#include <wrl/client.h>
#include <windowsx.h>  // for GET_X_LPARAM, GET_Y_LPARAM

using Microsoft::WRL::ComPtr;

namespace blurwindow {

class BlurWindow::Impl {
public:
    Impl(HWND owner, const WindowOptions& opts)
        : m_owner(owner)
        , m_options(opts)
        , m_preset(QualityPreset::Balanced)
        , m_running(false)
        , m_currentFPS(0.0f)
        , m_currentStrength(1.0f)
        , m_useDirectComp(false)
    {
        // Don't initialize graphics in constructor
        // Only determine rendering mode
        m_useDirectComp = ShouldUseDirectComposition();
        LOG_INFO("BlurWindow created (DirectComp: %d)", m_useDirectComp);
    }

    ~Impl() {
        Stop();
        ShutdownGraphics();
        DestroyBlurWindow();
    }

    void Start() {
        if (m_running) return;

        if (!m_hwnd) {
            CreateBlurWindow();
        }

        if (!m_graphicsInitialized) {
            LOG_INFO("Initializing graphics subsystems in Start()...");
            if (!InitializeGraphicsBasics() || !InitializeSubsystems()) {
                LOG_ERROR("Initialization failed. Cannot start render thread.");
                return;
            }
        }

        if (m_running.exchange(true)) {
            return;
        }
        
        m_renderThread = std::thread([this]() {
            RenderLoop();
        });
        LOG_INFO("BlurWindow render thread started.");
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
        // Window操作は即時実行（UIスレッドから呼ばれることが期待される）
        int newWidth = bounds.right - bounds.left;
        int newHeight = bounds.bottom - bounds.top;
        
        if (m_hwnd) {
            SetWindowPos(m_hwnd, nullptr,
                bounds.left, bounds.top,
                newWidth, newHeight,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
        }
        
        // D3Dリソースの再作成はRenderLoop内で安全に処理
        {
            std::lock_guard<std::mutex> lock(m_graphicsMutex);
            m_pendingBounds = bounds;
            m_resizeRequested = true;
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
        // Simple dispatcher based on JSON type field for now
        EffectType type = EffectType::Gaussian;
        if (jsonConfig.find("\"kawase\"") != std::string::npos) type = EffectType::Kawase;
        else if (jsonConfig.find("\"box\"") != std::string::npos) type = EffectType::Box;
        else if (jsonConfig.find("\"radial\"") != std::string::npos) type = EffectType::Radial;
        else if (jsonConfig.find("\"rain\"") != std::string::npos) type = EffectType::Rain;

        LOG_INFO("SetEffectPipeline: detected type=%d from config", static_cast<int>(type));

        auto newEffect = SubsystemFactory::CreateEffect(type);
        if (newEffect && newEffect->Initialize(m_device)) {
            std::lock_guard<std::mutex> lock(m_graphicsMutex);
            // Preserve current strength and apply to new effect
            newEffect->SetStrength(m_currentStrength);
            newEffect->SetNoiseIntensity(m_noiseIntensity);
            newEffect->SetNoiseScale(m_noiseScale);
            newEffect->SetNoiseSpeed(m_noiseSpeed);
            newEffect->SetNoiseType(m_noiseType);
            newEffect->SetColor(m_tintColor[0], m_tintColor[1], m_tintColor[2], m_tintColor[3]);
            m_effect = std::move(newEffect);
            m_graphicsInitialized = (m_capture && m_effect && m_presenter);
            LOG_INFO("SetEffectPipeline: new effect initialized successfully");
            return true;
        }
        LOG_ERROR("SetEffectPipeline: failed to create or initialize effect type=%d", static_cast<int>(type));
        return false;
    }

    void SetBlurStrength(float strength) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        m_currentStrength = strength;
        LOG_INFO("SetBlurStrength: %.2f", strength);
        if (m_effect) {
            m_effect->SetStrength(strength);
        }
    }

    void SetBlurColor(float r, float g, float b, float a) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        m_tintColor[0] = r; m_tintColor[1] = g; m_tintColor[2] = b; m_tintColor[3] = a;
        if (m_effect) {
            m_effect->SetColor(r, g, b, a);
        }
    }

    void SetEffectType(int type) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        SetEffectTypeInternal(type);
    }
    
    // Internal version without lock (must be called with m_graphicsMutex held)
    void SetEffectTypeInternal(int type) {
        EffectType effectType = static_cast<EffectType>(type);
        LOG_DEBUG("SetEffectTypeInternal: switching to type {}", type);
        
        auto newEffect = SubsystemFactory::CreateEffect(effectType);
        if (!newEffect) {
            LOG_ERROR("SetEffectTypeInternal: CreateEffect failed for type {}", type);
            return;
        }
        
        if (!newEffect->Initialize(m_device)) {
            LOG_ERROR("SetEffectTypeInternal: Initialize failed, m_device={}", (void*)m_device);
            return;
        }
        
        newEffect->SetStrength(m_currentStrength);
        newEffect->SetNoiseIntensity(m_noiseIntensity);
        newEffect->SetNoiseScale(m_noiseScale);
        newEffect->SetNoiseSpeed(m_noiseSpeed);
        newEffect->SetNoiseType(m_noiseType);
        newEffect->SetColor(m_tintColor[0], m_tintColor[1], m_tintColor[2], m_tintColor[3]);
        m_effect = std::move(newEffect);
        LOG_INFO("SetEffectTypeInternal: Successfully switched to type {}", type);
    }
    
    // Helper: Ensure RainEffect is active (must be called with m_graphicsMutex held)
    RainEffect* EnsureRainEffect() {
        auto* rain = dynamic_cast<RainEffect*>(m_effect.get());
        if (!rain) {
            LOG_INFO("EnsureRainEffect: Current effect is not RainEffect, switching...");
            SetEffectTypeInternal(static_cast<int>(EffectType::Rain));
            rain = dynamic_cast<RainEffect*>(m_effect.get());
            if (!rain) {
                LOG_ERROR("EnsureRainEffect: Failed to switch to RainEffect");
            } else {
                LOG_INFO("EnsureRainEffect: Successfully switched to RainEffect");
            }
        }
        return rain;
    }

    void SetBlurParam(float param) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        if (m_effect) {
            // This is a bit of a hack since IBlurEffect doesn't have a generic SetParam
            // We'll need to check the effect type or add SetParam to IBlurEffect
            // For now, let's assume SetParameters can handle a simple float-check
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "{\"param\": %.2f}", param);
            m_effect->SetParameters(buffer);
        }
    }

    void SetNoiseIntensity(float intensity) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        m_noiseIntensity = intensity;
        if (m_effect) m_effect->SetNoiseIntensity(intensity);
    }

    void SetNoiseScale(float scale) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        m_noiseScale = scale;
        if (m_effect) m_effect->SetNoiseScale(scale);
    }

    void SetNoiseSpeed(float speed) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        m_noiseSpeed = speed;
        if (m_effect) m_effect->SetNoiseSpeed(speed);
    }

    void SetNoiseType(int type) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        m_noiseType = type;
        if (m_effect) m_effect->SetNoiseType(type);
    }

    // --- Rain Effect Control ---

    void SetRainIntensity(float intensity) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        // Auto-enable RainEffect if intensity > 0
        RainEffect* rain = (intensity > 0) ? EnsureRainEffect() : dynamic_cast<RainEffect*>(m_effect.get());
        if (rain) rain->SetRainIntensity(intensity);
    }

    void SetRainDropSpeed(float speed) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        RainEffect* rain = EnsureRainEffect();
        if (rain) rain->SetDropSpeed(speed);
    }

    void SetRainRefraction(float strength) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        RainEffect* rain = EnsureRainEffect();
        if (rain) rain->SetRefractionStrength(strength);
    }

    void SetRainTrailLength(float length) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        RainEffect* rain = EnsureRainEffect();
        if (rain) rain->SetTrailLength(length);
    }

    void SetRainDropSize(float minSize, float maxSize) {
        std::lock_guard<std::mutex> lock(m_graphicsMutex);
        RainEffect* rain = EnsureRainEffect();
        if (rain) rain->SetDropSizeRange(minSize, maxSize);
    }

    // --- Click Callback ---
    void SetClickCallback(BlurWindow::ClickCallback callback, void* userData) {
        m_clickCallback = callback;
        m_clickUserData = userData;
        LOG_INFO("SetClickCallback: callback=%p, userData=%p", (void*)callback, userData);
    }

    bool IsInitialized() const {
        return m_graphicsInitialized;
    }

    bool Initialize() {
        if (m_graphicsInitialized) return true;
        
        LOG_INFO("Manual initialization requested.");
        if (!m_hwnd) CreateBlurWindow();
        bool success = InitializeGraphicsBasics() && InitializeSubsystems();
        if (success && m_capture && m_hwnd) {
            m_capture->SetSelfWindow(m_hwnd);
        }
        return success;
    }

private:
    bool InitializeGraphicsBasics() {
        m_device = BlurSystem::Instance().GetDevice();
        if (!m_device) {
            LOG_ERROR("D3D11 Device not available from BlurSystem.");
            return false;
        }
        m_context.Reset();
        m_device->GetImmediateContext(m_context.GetAddressOf());
        
        m_width = m_options.bounds.right - m_options.bounds.left;
        m_height = m_options.bounds.bottom - m_options.bounds.top;
        if (m_width <= 0) m_width = 400;
        if (m_height <= 0) m_height = 300;
        
        LOG_INFO("Graphics basics: %dx%d texture.", m_width, m_height);
        return CreateOutputTexture();
    }

    bool InitializeSubsystems() {
        if (!m_device || !m_hwnd) return false;

        LOG_INFO("Initializing subsystems...");

        // 1. Initialize capture
        m_capture = SubsystemFactory::CreateCapture(CaptureType::DXGI);
        if (m_capture) {
            if (!m_capture->Initialize(m_device)) {
                LOG_ERROR("Failed to initialize DXGI capture.");
                m_capture.reset();
            } else {
                m_capture->SetSelfWindow(m_hwnd);
                LOG_INFO("Capture initialized.");
            }
        }
        // 2. Initialize effect
        m_effect = SubsystemFactory::CreateEffect(EffectType::Gaussian);
        if (m_effect) {
            if (!m_effect->Initialize(m_device)) {
                LOG_ERROR("Failed to initialize Gaussian effect.");
                m_effect.reset();
            } else {
                LOG_INFO("Effect initialized.");
            }
        }
        
        // 3. Initialize presenter
        PresenterType presenterType = m_useDirectComp ? PresenterType::DirectComp : PresenterType::ULW;
        auto presenter = SubsystemFactory::CreatePresenter(presenterType, m_hwnd, m_device);
        
        if (!presenter && m_useDirectComp) {
            LOG_WARN("DirectComp presenter failed. Falling back to ULW...");
            m_useDirectComp = false;
            
            // Switch window style to layered for ULW
            if (m_hwnd) {
                LONG_PTR exStyle = GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);
                exStyle &= ~WS_EX_NOREDIRECTIONBITMAP;
                exStyle |= WS_EX_LAYERED;
                SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, exStyle);
                // Force style to take effect
                SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                LOG_INFO("Switched window style to WS_EX_LAYERED for fallback.");
            }

            presenter = SubsystemFactory::CreatePresenter(PresenterType::ULW, m_hwnd, m_device);
        }
        
        if (presenter) {
            m_presenter = std::move(presenter);
            LOG_INFO("Presenter initialized (%s).", m_useDirectComp ? "DirectComp" : "ULW");
        } else {
            LOG_ERROR("Failed to initialize any presenter.");
        }

        m_graphicsInitialized = (m_capture && m_effect && m_presenter);
        if (!m_graphicsInitialized) {
            LOG_ERROR("Initialization partial failure: Cap:%d, Eff:%d, Pres:%d", 
                m_capture != nullptr, m_effect != nullptr, m_presenter != nullptr);
        } else {
            LOG_INFO("All subsystems initialized successfully.");
        }

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

    // Static window procedure for handling click events
    static LRESULT CALLBACK BlurWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        
        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        
        if (self && msg == WM_LBUTTONDOWN) {
            if (self->m_clickCallback) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                // Convert to screen coordinates
                POINT pt = { x, y };
                ClientToScreen(hwnd, &pt);
                // Call callback with screen coordinates
                self->m_clickCallback(self, pt.x, pt.y, self->m_clickUserData);
            }
            return 0;
        }
        
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void CreateBlurWindow() {
        // Register window class
        static bool classRegistered = false;
        static const wchar_t* CLASS_NAME = L"BlurWindowClass";
        
        if (!classRegistered) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.lpfnWndProc = BlurWindowProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = CLASS_NAME;
            wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
            RegisterClassExW(&wc);
            classRegistered = true;
        }

        // Choose window style based on presenter type
        DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        
        if (m_useDirectComp) {
            // DirectComposition: use WS_EX_NOREDIRECTIONBITMAP for direct composition
            exStyle |= WS_EX_NOREDIRECTIONBITMAP;
            OutputDebugStringA("Creating window for DirectComposition\n");
        } else {
            // UpdateLayeredWindow: use WS_EX_LAYERED for alpha blending
            exStyle |= WS_EX_LAYERED;
            OutputDebugStringA("Creating window for UpdateLayeredWindow\n");
        }
        
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
            WS_POPUP | WS_VISIBLE,
            m_options.bounds.left,
            m_options.bounds.top,
            m_options.bounds.right - m_options.bounds.left,
            m_options.bounds.bottom - m_options.bounds.top,
            m_owner,
            nullptr,
            GetModuleHandleW(nullptr),
            this  // Pass Impl pointer for WndProc
        );
        
        // Exclude blur window from screen capture (Windows 10 2004+)
        // This prevents infinite recursion where the blur window captures itself
        if (m_hwnd) {
            SetWindowDisplayAffinity(m_hwnd, WDA_EXCLUDEFROMCAPTURE);
        }
    }

    void DestroyBlurWindow() {
        if (m_hwnd) {
            DestroyWindow(m_hwnd);
            m_hwnd = nullptr;
        }
    }

    void RenderLoop() {
        using clock = std::chrono::high_resolution_clock;
        
        // Enable high-resolution timer (1ms precision)
        timeBeginPeriod(1);
        
        int frameCount = 0;
        auto lastFPSUpdate = clock::now();

        // Wait a bit for UI thread to finish ShowWindow/etc
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        while (m_running) {
            auto frameStart = clock::now();
            
            static bool firstFrameLogged = false;
            
            // リサイズ要求の処理（RenderLoop内で安全にD3Dリソースを再作成）
            if (m_resizeRequested.exchange(false)) {
                m_options.bounds = m_pendingBounds;
                m_width = m_pendingBounds.right - m_pendingBounds.left;
                m_height = m_pendingBounds.bottom - m_pendingBounds.top;
                if (m_width > 0 && m_height > 0 && m_device) {
                    CreateOutputTexture();
                    LOG_INFO("Output texture resized to %dx%d in RenderLoop.", m_width, m_height);
                }
            }
            
            { // Strict lock around all D3D11 context usage
                std::lock_guard<std::mutex> lock(m_graphicsMutex);
                if (m_graphicsInitialized && m_capture && m_effect && m_presenter) {
                    ID3D11Texture2D* capturedTexture = nullptr;
                    // Inside lock, we rely on the 16ms/0ms timeout in DXGICapture to not block UI too long
                    if (m_capture->CaptureFrame(m_options.bounds, &capturedTexture)) {
                        RenderFrame(capturedTexture);
                        if (!firstFrameLogged) {
                            LOG_INFO("First frame rendered and presented successfully.");
                            firstFrameLogged = true;
                        }
                    }
                }
            }

            frameCount++;
            
            // Update FPS every second
            auto now = clock::now();
            auto fpsDelta = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFPSUpdate);
            if (fpsDelta.count() >= 1000) {
                m_currentFPS = static_cast<float>(frameCount) * 1000.0f / fpsDelta.count();
                frameCount = 0;
                lastFPSUpdate = now;
            }

            // Frame timing - sleep to achieve target FPS
            int targetFPS = GetTargetFPS();
            auto targetFrameTime = std::chrono::microseconds(1000000 / targetFPS);
            auto frameTime = clock::now() - frameStart;
            
            if (frameTime < targetFrameTime) {
                auto sleepTime = targetFrameTime - frameTime;
                // Use precise sleep (shorter intervals)
                auto sleepMs = std::chrono::duration_cast<std::chrono::milliseconds>(sleepTime);
                if (sleepMs.count() > 0) {
                    std::this_thread::sleep_for(sleepMs);
                }
            }
        }
        
        timeEndPeriod(1);
    }

    void RenderFrame(ID3D11Texture2D* capturedTexture) {
        using clock = std::chrono::high_resolution_clock;
        
        auto t0 = clock::now();
        
        auto t1 = clock::now();
        
        // 1. Update effect animation
        if (m_effect) {
            static auto lastUpdate = clock::now();
            auto now = clock::now();
            float deltaTime = std::chrono::duration<float>(now - lastUpdate).count();
            lastUpdate = now;
            m_effect->Update(deltaTime);
        }

        // 2. Manage SRV for captured texture
        if (capturedTexture != m_lastCapturedTexture) {
            m_capturedSRV.Reset();
            HRESULT hr = m_device->CreateShaderResourceView(capturedTexture, nullptr, m_capturedSRV.GetAddressOf());
            if (FAILED(hr)) return;
            m_lastCapturedTexture = capturedTexture;
        }
        
        auto t2 = clock::now();
 
        // 3. Apply blur effect
        if (!m_effect->Apply(m_context.Get(), m_capturedSRV.Get(), m_outputRTV.Get(), m_width, m_height)) {
            return;
        }
        
        auto t3 = clock::now();

        // 4. Present to window
        m_presenter->Present(m_outputTexture.Get());
        
        auto t4 = clock::now();
        
        // Log timings periodically (only to debug output now)
        static int frameCounter = 0;
        if (++frameCounter % 120 == 0) {
            auto captureMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
            auto blurMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
            auto presentMs = std::chrono::duration<double, std::milli>(t4 - t3).count();
            auto totalMs = std::chrono::duration<double, std::milli>(t4 - t0).count();
            
            char buf[256];
            snprintf(buf, sizeof(buf), "[Perf] Cap:%.1fms Blur:%.1fms Pres:%.1fms Total:%.1fms",
                captureMs, blurMs, presentMs, totalMs);
            OutputDebugStringA(buf);
            OutputDebugStringA("\n");
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
    float m_currentStrength = 1.0f;
    float m_noiseIntensity = 0.0f;
    float m_noiseScale = 100.0f;
    float m_noiseSpeed = 1.0f;
    int m_noiseType = 0;
    float m_tintColor[4] = { 0, 0, 0, 0 };

    // Resize request handling (deferred to RenderLoop for thread safety)
    std::atomic<bool> m_resizeRequested{false};
    RECT m_pendingBounds = {};

    // Graphics resources
    ID3D11Device* m_device = nullptr;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Texture2D> m_outputTexture;
    ComPtr<ID3D11ShaderResourceView> m_outputSRV;
    ComPtr<ID3D11RenderTargetView> m_outputRTV;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    std::atomic<bool> m_graphicsInitialized = false;

    // SRV cache for captured texture
    ComPtr<ID3D11ShaderResourceView> m_capturedSRV;
    ID3D11Texture2D* m_lastCapturedTexture = nullptr;

    // Subsystems
    std::unique_ptr<ICaptureSubsystem> m_capture;
    std::unique_ptr<IBlurEffect> m_effect;
    std::unique_ptr<IPresenter> m_presenter;
    bool m_useDirectComp = false;

    mutable std::mutex m_graphicsMutex;

    // Click callback
    BlurWindow::ClickCallback m_clickCallback = nullptr;
    void* m_clickUserData = nullptr;

    // Helper to check if DirectComposition should be used
    static bool ShouldUseDirectComposition() {
        // Try to load dcomp.dll
        HMODULE dcompDll = LoadLibraryW(L"dcomp.dll");
        if (!dcompDll) {
            return false;
        }
        FreeLibrary(dcompDll);
        
        // We could do more checks here, but the real test is in InitializeSubsystems
        return true;
    }
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

bool BlurWindow::IsInitialized() const {
    return m_impl->IsInitialized();
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

void BlurWindow::SetBlurStrength(float strength) {
    m_impl->SetBlurStrength(strength);
}

void BlurWindow::SetBlurColor(float r, float g, float b, float a) {
    m_impl->SetBlurColor(r, g, b, a);
}

void BlurWindow::SetNoiseIntensity(float intensity) {
    m_impl->SetNoiseIntensity(intensity);
}

void BlurWindow::SetNoiseScale(float scale) {
    m_impl->SetNoiseScale(scale);
}

void BlurWindow::SetNoiseSpeed(float speed) {
    m_impl->SetNoiseSpeed(speed);
}

void BlurWindow::SetNoiseType(int type) {
    m_impl->SetNoiseType(type);
}

void BlurWindow::SetEffectType(int type) {
    m_impl->SetEffectType(type);
}

void BlurWindow::SetBlurParam(float param) {
    m_impl->SetBlurParam(param);
}

void BlurWindow::SetRainIntensity(float intensity) {
    m_impl->SetRainIntensity(intensity);
}

void BlurWindow::SetRainDropSpeed(float speed) {
    m_impl->SetRainDropSpeed(speed);
}

void BlurWindow::SetRainRefraction(float strength) {
    m_impl->SetRainRefraction(strength);
}

void BlurWindow::SetRainTrailLength(float length) {
    m_impl->SetRainTrailLength(length);
}

void BlurWindow::SetRainDropSize(float minSize, float maxSize) {
    m_impl->SetRainDropSize(minSize, maxSize);
}

void BlurWindow::SetClickCallback(ClickCallback callback, void* userData) {
    m_impl->SetClickCallback(callback, userData);
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
