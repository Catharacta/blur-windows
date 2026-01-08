#include "WGCCapture.h"
#include "../core/Logger.h"

#include <winrt/base.h>
#include <inspectable.h>
#include <dxgi.h>
#include <shellscalingapi.h>
#include <mutex>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "shcore.lib")

namespace blurwindow {

// WinRT apartment initialization (singleton pattern)
static std::once_flag s_winrtInitFlag;
static void EnsureWinRTInitialized() {
    std::call_once(s_winrtInitFlag, []() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    });
}

// Helper to convert ID3D11Device to WinRT IDirect3DDevice
extern "C" {
    HRESULT __stdcall CreateDirect3D11DeviceFromDXGIDevice(::IDXGIDevice* dxgiDevice, ::IInspectable** graphicsDevice);
}

WGCCapture::WGCCapture() {
    LOG_INFO("WGCCapture constructor called");
    // Initialize WinRT (only once per process)
    try {
        EnsureWinRTInitialized();
        LOG_INFO("WGCCapture WinRT initialized");
    } catch (...) {
        LOG_ERROR("WGCCapture WinRT initialization failed");
    }
}

WGCCapture::~WGCCapture() {
    LOG_INFO("WGCCapture destructor called");
    Shutdown();
}

bool WGCCapture::IsAvailable() {
    // Windows Graphics Capture is available on Windows 10 1803 (build 17134) and later
    try {
        bool supported = winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
        // LOG_INFO("WGCCapture::IsAvailable: %d", supported);
        return supported;
    } catch (...) {
        LOG_ERROR("WGCCapture::IsAvailable exception");
        return false;
    }
}

winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice WGCCapture::CreateDirect3DDevice() {
    // Get DXGI device from D3D11 device
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (FAILED(hr)) {
        LOG_ERROR("WGCCapture: Failed to get IDXGIDevice (0x%08X)", hr);
        return nullptr;
    }

    // Create WinRT Direct3D device
    winrt::com_ptr<::IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put());
    if (FAILED(hr)) {
        LOG_ERROR("WGCCapture: Failed to create Direct3D11 device (0x%08X)", hr);
        return nullptr;
    }

    return inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

bool WGCCapture::Initialize(ID3D11Device* device) {
    if (!device) return false;
    
    LOG_INFO("WGCCapture: Initializing Windows Graphics Capture...");

    if (!IsAvailable()) {
        LOG_ERROR("WGCCapture: Windows Graphics Capture is not available on this system");
        return false;
    }

    m_device = device;
    m_device->GetImmediateContext(m_context.GetAddressOf());

    // Create WinRT Direct3D device
    try {
        m_winrtDevice = CreateDirect3DDevice();
        if (!m_winrtDevice) {
            LOG_ERROR("WGCCapture: Failed to create WinRT Direct3D device");
            return false;
        }
    } catch (winrt::hresult_error const& ex) {
        LOG_ERROR("WGCCapture: WinRT error creating device: 0x%08X", ex.code().value);
        return false;
    }

    // Enumerate all monitors
    EnumerateMonitors();

    if (m_monitors.empty()) {
        LOG_ERROR("WGCCapture: No monitors found");
        return false;
    }

    LOG_INFO("WGCCapture: Found %zu monitors", m_monitors.size());

    // Initialize capture for primary monitor
    if (!InitializeCaptureForMonitor(0)) {
        LOG_ERROR("WGCCapture: Failed to initialize capture for primary monitor");
        return false;
    }

    m_initialized = true;
    LOG_INFO("WGCCapture: Initialization complete");
    return true;
}

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC /*hdcMonitor*/, LPRECT /*lprcMonitor*/, LPARAM dwData) {
    auto* monitors = reinterpret_cast<std::vector<WGCMonitorInfo>*>(dwData);
    
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMonitor, &mi)) {
        WGCMonitorInfo info;
        info.hMonitor = hMonitor;
        info.bounds = mi.rcMonitor;
        info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        
        // Get DPI
        UINT dpiX, dpiY;
        if (SUCCEEDED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
            info.dpi = dpiX;
        } else {
            info.dpi = 96;
        }
        
        monitors->push_back(info);
        
        LOG_INFO("WGCCapture Monitor[%zu]: bounds=(%d,%d,%d,%d), dpi=%u, primary=%d",
            monitors->size() - 1,
            info.bounds.left, info.bounds.top, info.bounds.right, info.bounds.bottom,
            info.dpi, info.isPrimary ? 1 : 0);
    }
    
    return TRUE;
}

void WGCCapture::EnumerateMonitors() {
    m_monitors.clear();
    LOG_INFO("WGCCapture: Enumerating monitors...");
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&m_monitors));
    LOG_INFO("WGCCapture: Found %zu monitors", m_monitors.size());
}

int WGCCapture::FindMonitorForRegion(const RECT& region) const {
    LONG centerX = (region.left + region.right) / 2;
    LONG centerY = (region.top + region.bottom) / 2;

    for (size_t i = 0; i < m_monitors.size(); i++) {
        const RECT& bounds = m_monitors[i].bounds;
        if (centerX >= bounds.left && centerX < bounds.right &&
            centerY >= bounds.top && centerY < bounds.bottom) {
            return static_cast<int>(i);
        }
    }

    return 0; // Default to primary
}

bool WGCCapture::InitializeCaptureForMonitor(int monitorIndex) {
    if (monitorIndex < 0 || monitorIndex >= static_cast<int>(m_monitors.size())) {
        LOG_ERROR("WGCCapture: Invalid monitor index %d", monitorIndex);
        return false;
    }

    LOG_DEBUG("WGCCapture: Initializing capture for monitor %d", monitorIndex);

    // Stop existing session
    if (m_session) {
        m_session.Close();
        m_session = nullptr;
    }
    if (m_framePool) {
        m_framePool.FrameArrived(m_frameArrivedToken);
        m_framePool.Close();
        m_framePool = nullptr;
    }
    m_captureItem = nullptr;
    
    // Reset latest frame to avoid showing stall frame from previous monitor
    m_latestFrame.store(nullptr);
    m_latestFrameHolder.Reset();

    try {
        // Get GraphicsCaptureItem from HMONITOR using interop
        auto interopFactory = winrt::get_activation_factory<
            winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
            IGraphicsCaptureItemInterop>();

        HMONITOR hMonitor = m_monitors[monitorIndex].hMonitor;
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
        
        HRESULT hr = interopFactory->CreateForMonitor(
            hMonitor,
            winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
            winrt::put_abi(item));
        
        if (FAILED(hr)) {
            LOG_ERROR("WGCCapture: Failed to create GraphicsCaptureItem for monitor %d (0x%08X)", monitorIndex, hr);
            return false;
        }

        m_captureItem = item;
        auto size = m_captureItem.Size();
        m_frameWidth = size.Width;
        m_frameHeight = size.Height;

        LOG_INFO("WGCCapture: Created capture item for monitor %d (%dx%d)", monitorIndex, m_frameWidth, m_frameHeight);

        // Create frame pool (2 frames for better buffering)
        m_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            m_winrtDevice,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,  // Number of frames (increased from 1 for better latency)
            size);

        // Subscribe to frame arrived event
        m_frameArrivedToken = m_framePool.FrameArrived({ this, &WGCCapture::OnFrameArrived });

        // Create and start capture session
        m_session = m_framePool.CreateCaptureSession(m_captureItem);
        m_session.IsBorderRequired(false);  // Hide yellow border (Windows 11+)
        m_session.IsCursorCaptureEnabled(true);
        m_session.StartCapture();

        m_currentMonitorIndex = monitorIndex;
        LOG_INFO("WGCCapture: Capture session started for monitor %d", monitorIndex);
        return true;

    } catch (winrt::hresult_error const& ex) {
        LOG_ERROR("WGCCapture: WinRT error initializing capture: 0x%08X", ex.code().value);
        return false;
    }
}

void WGCCapture::OnFrameArrived(
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
    winrt::Windows::Foundation::IInspectable const& /*args*/) {
    
    try {
        auto frame = sender.TryGetNextFrame();
        if (!frame) return;

        // Log first frame arrival for this session
        // static int frameCount = 0;
        // if (frameCount++ % 60 == 0) {
        //    LOG_DEBUG("WGCCapture: Frame arrived"); 
        // }

        auto surface = frame.Surface();
        // ... (rest of the function)
        
        // Get D3D11 texture from WinRT surface
        auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;
        HRESULT hr = access->GetInterface(IID_PPV_ARGS(&texture));
        
        if (SUCCEEDED(hr) && texture) {
            // Copy to our texture for thread-safe access
            D3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);
            
            if (!m_latestFrameHolder || 
                desc.Width != static_cast<UINT>(m_frameWidth) || 
                desc.Height != static_cast<UINT>(m_frameHeight)) {
                
                D3D11_TEXTURE2D_DESC copyDesc = desc;
                copyDesc.Usage = D3D11_USAGE_DEFAULT;
                copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                copyDesc.MiscFlags = 0;
                
                hr = m_device->CreateTexture2D(&copyDesc, nullptr, m_latestFrameHolder.ReleaseAndGetAddressOf());
                if (FAILED(hr)) return;
            }
            
            m_context->CopyResource(m_latestFrameHolder.Get(), texture.Get());
            m_latestFrame.store(m_latestFrameHolder.Get());
        }
        
        frame.Close();
    } catch (...) {
        // Ignore frame errors
    }
}

bool WGCCapture::CaptureFrame(const RECT& region, ID3D11Texture2D** outTexture) {
    if (!m_initialized || !outTexture) return false;

    // Skip monitor switching if target is locked
    if (!m_targetLocked) {
        int targetMonitor = FindMonitorForRegion(region);
        if (targetMonitor != m_currentMonitorIndex) {
            LOG_DEBUG("WGCCapture: Switching from monitor %d to %d", m_currentMonitorIndex, targetMonitor);
            if (!InitializeCaptureForMonitor(targetMonitor)) {
                targetMonitor = m_currentMonitorIndex;
            }
        }
    }

    // Get latest frame
    ID3D11Texture2D* latestFrame = m_latestFrame.load();
    if (!latestFrame) {
        // No frame yet, wait a bit for first frame
        Sleep(16);
        latestFrame = m_latestFrame.load();
        if (!latestFrame) return false;
    }

    // Calculate region relative to monitor
    const RECT& monBounds = m_monitors[m_currentMonitorIndex].bounds;
    int relLeft = region.left - monBounds.left;
    int relTop = region.top - monBounds.top;
    int relRight = region.right - monBounds.left;
    int relBottom = region.bottom - monBounds.top;

    // Clamp to frame bounds
    relLeft = (std::max)(0, relLeft);
    relTop = (std::max)(0, relTop);
    relRight = (std::min)(m_frameWidth, relRight);
    relBottom = (std::min)(m_frameHeight, relBottom);

    int regionWidth = relRight - relLeft;
    int regionHeight = relBottom - relTop;

    if (regionWidth <= 0 || regionHeight <= 0) {
        // Region is outside current monitor
        if (m_cachedTexture) {
            *outTexture = m_cachedTexture.Get();
            return true;
        }
        return false;
    }

    // Create or resize cached texture
    D3D11_TEXTURE2D_DESC existingDesc = {};
    if (m_cachedTexture) {
        m_cachedTexture->GetDesc(&existingDesc);
    }
    
    bool needsRecreate = !m_cachedTexture || 
        existingDesc.Width != static_cast<UINT>(regionWidth) || 
        existingDesc.Height != static_cast<UINT>(regionHeight);
    
    if (needsRecreate) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = regionWidth;
        desc.Height = regionHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, m_cachedTexture.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            LOG_ERROR("WGCCapture: Failed to create cached texture (0x%08X)", hr);
            return false;
        }
    }

    // Copy region
    D3D11_BOX srcBox = {};
    srcBox.left = relLeft;
    srcBox.top = relTop;
    srcBox.right = relRight;
    srcBox.bottom = relBottom;
    srcBox.front = 0;
    srcBox.back = 1;

    m_context->CopySubresourceRegion(m_cachedTexture.Get(), 0, 0, 0, 0, latestFrame, 0, &srcBox);

    *outTexture = m_cachedTexture.Get();
    return true;
}

void WGCCapture::SetSelfWindow(HWND hwnd) {
    m_selfHwnd = hwnd;
}

void WGCCapture::SetTargetBounds(const RECT& bounds) {
    if (m_targetLocked) {
        LOG_DEBUG("WGCCapture::SetTargetBounds: Target already locked to monitor %d", m_currentMonitorIndex);
        return;
    }
    
    // Find monitor for the given bounds
    int monitorIndex = FindMonitorForRegion(bounds);
    
    if (monitorIndex >= 0) {
        if (monitorIndex != m_currentMonitorIndex) {
            if (InitializeCaptureForMonitor(monitorIndex)) {
                m_targetLocked = true;
                LOG_INFO("WGCCapture::SetTargetBounds: Locked to monitor %d, bounds=(%d,%d,%d,%d)",
                    monitorIndex, bounds.left, bounds.top, bounds.right, bounds.bottom);
            } else {
                // Keep current monitor but still lock
                m_targetLocked = true;
                LOG_WARN("WGCCapture::SetTargetBounds: Failed to switch to monitor %d, locked to current monitor %d",
                    monitorIndex, m_currentMonitorIndex);
            }
        } else {
            // Already on correct monitor, just lock
            m_targetLocked = true;
            LOG_INFO("WGCCapture::SetTargetBounds: Already on monitor %d, locked", monitorIndex);
        }
    }
}

bool WGCCapture::IsTargetLocked() const {
    return m_targetLocked;
}

void WGCCapture::Shutdown() {
    LOG_INFO("WGCCapture: Shutting down...");

    if (m_session) {
        try {
            m_session.Close();
        } catch (...) {}
        m_session = nullptr;
    }

    if (m_framePool) {
        try {
            m_framePool.FrameArrived(m_frameArrivedToken);
            m_framePool.Close();
        } catch (...) {}
        m_framePool = nullptr;
    }

    m_captureItem = nullptr;
    m_winrtDevice = nullptr;
    m_latestFrameHolder.Reset();
    m_cachedTexture.Reset();
    m_context.Reset();
    m_monitors.clear();
    m_device = nullptr;
    m_initialized = false;
    m_currentMonitorIndex = -1;
}

// Factory function
std::unique_ptr<ICaptureSubsystem> CreateWGCCapture() {
    return std::make_unique<WGCCapture>();
}

// Check if WGC is available (exported for SubsystemFactory)
bool IsWGCAvailable() {
    LOG_INFO("IsWGCAvailable called");
    try {
        bool result = WGCCapture::IsAvailable();
        LOG_INFO("IsWGCAvailable result: %d", result);
        return result;
    } catch (...) {
        LOG_ERROR("IsWGCAvailable crashed");
        return false;
    }
}

} // namespace blurwindow
