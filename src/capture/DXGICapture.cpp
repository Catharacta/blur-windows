#include "ICaptureSubsystem.h"
#include "../core/Logger.h"
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <shellscalingapi.h>

#pragma comment(lib, "shcore.lib")

using Microsoft::WRL::ComPtr;

namespace blurwindow {

/// Monitor information for multi-monitor support
struct MonitorInfo {
    ComPtr<IDXGIOutput> output;
    ComPtr<IDXGIAdapter> adapter;  // Adapter this monitor belongs to
    int adapterIndex;              // Index of the adapter
    RECT bounds;                   // Physical coordinates
    UINT dpi;                      // DPI scale
    HMONITOR hMonitor;             // Monitor handle
    bool isPrimary;
};

/// Adapter information with its D3D device
struct AdapterInfo {
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    int index;
};

class DXGICapture : public ICaptureSubsystem {
public:
    DXGICapture() = default;
    ~DXGICapture() override { Shutdown(); }

    bool Initialize(ID3D11Device* device) override {
        if (!device) return false;
        
        m_primaryDevice = device;
        m_primaryDevice->GetImmediateContext(m_primaryContext.GetAddressOf());

        LOG_INFO("Initializing DXGI capture (multi-adapter mode)...");

        // Get DXGI Factory to enumerate all adapters
        ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr = m_primaryDevice->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()));
        if (FAILED(hr)) {
            LOG_ERROR("Failed to query IDXGIDevice from D3D11 device (0x%08X).", hr);
            return false;
        }

        // Get adapter of primary device
        ComPtr<IDXGIAdapter> primaryAdapter;
        hr = dxgiDevice->GetAdapter(primaryAdapter.GetAddressOf());
        if (FAILED(hr)) {
            LOG_ERROR("Failed to get adapter from DXGI device (0x%08X).", hr);
            return false;
        }

        // Get factory from adapter
        hr = primaryAdapter->GetParent(IID_PPV_ARGS(m_factory.GetAddressOf()));
        if (FAILED(hr)) {
            LOG_ERROR("Failed to get DXGI factory (0x%08X).", hr);
            return false;
        }

        // Enumerate all adapters and their monitors
        EnumerateAllAdaptersAndMonitors(primaryAdapter.Get());

        // Initialize duplication for primary monitor first
        if (!m_monitors.empty()) {
            LOG_INFO("Found %zu monitors across all adapters. Initializing duplication...", m_monitors.size());
            return InitializeDuplicationForMonitor(0);
        }

        LOG_ERROR("No monitors found to capture.");
        return false;
    }

    bool CaptureFrame(const RECT& region, ID3D11Texture2D** outTexture) override {
        if (!m_initialized || !outTexture) return false;

        // Skip monitor switching if target is locked
        if (!m_targetLocked) {
            int monitorIndex = FindMonitorForRegion(region);
            if (monitorIndex != m_currentMonitorIndex && monitorIndex >= 0) {
                // Switch to the new monitor
                if (!InitializeDuplicationForMonitor(monitorIndex)) {
                    // Fallback to current monitor
                    monitorIndex = m_currentMonitorIndex;
                }
            }
        }

        // Release previous frame if any
        if (m_frameAcquired) {
            m_duplication->ReleaseFrame();
            m_frameAcquired = false;
        }

        // Acquire next frame (non-blocking)
        ComPtr<IDXGIResource> desktopResource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        
        HRESULT hr = m_duplication->AcquireNextFrame(
            0,  // Strictly non-blocking to prevent any UI hang
            &frameInfo,
            desktopResource.GetAddressOf()
        );

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // No new frame, use cached if available
            if (m_cachedTexture) {
                *outTexture = m_cachedTexture.Get();
                return true;
            }
            // First frame timeout is common if nothing moves
            return false;
        }

        if (hr == DXGI_ERROR_ACCESS_LOST) {
            LOG_WARN("DXGI Desktop Duplication access lost. Reinitializing...");
            m_initialized = false;
            InitializeDuplicationForMonitor(m_currentMonitorIndex);
            return false;
        }

        if (FAILED(hr)) {
            LOG_ERROR("AcquireNextFrame failed (0x%08X).", hr);
            return false;
        }

        m_frameAcquired = true;

        // Get texture from resource
        ComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource.As(&desktopTexture);
        if (FAILED(hr)) return false;

        // Convert region from logical to physical coordinates (DPI-aware)
        RECT physicalRegion = ConvertToPhysicalCoordinates(region, m_currentMonitorIndex);

        // Calculate region dimensions
        int regionWidth = physicalRegion.right - physicalRegion.left;
        int regionHeight = physicalRegion.bottom - physicalRegion.top;

        if (regionWidth <= 0 || regionHeight <= 0) {
            return false;
        }

        // Create or recreate output texture if needed
        // Note: We create texture on the primary device for compatibility with BlurWindow
        if (!m_cachedTexture || NeedsResize(regionWidth, regionHeight)) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = regionWidth;
            desc.Height = regionHeight;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

            hr = m_primaryDevice->CreateTexture2D(&desc, nullptr, m_cachedTexture.ReleaseAndGetAddressOf());
            if (FAILED(hr)) return false;

            m_cachedWidth = regionWidth;
            m_cachedHeight = regionHeight;
        }

        // Calculate source box relative to current monitor
        D3D11_BOX srcBox = {};
        const MonitorInfo& monInfo = m_monitors[m_currentMonitorIndex];
        
        // Convert physical screen coordinates to monitor-relative coordinates
        LONG relLeft = physicalRegion.left - monInfo.bounds.left;
        LONG relTop = physicalRegion.top - monInfo.bounds.top;
        LONG relRight = physicalRegion.right - monInfo.bounds.left;
        LONG relBottom = physicalRegion.bottom - monInfo.bounds.top;
        
        // Check if region is completely outside the current monitor
        if (relRight <= 0 || relBottom <= 0 || 
            relLeft >= (LONG)m_outputWidth || relTop >= (LONG)m_outputHeight) {
            LOG_WARN("CaptureFrame: Region (%d,%d,%d,%d) is outside current monitor bounds (%d,%d,%d,%d)",
                physicalRegion.left, physicalRegion.top, physicalRegion.right, physicalRegion.bottom,
                monInfo.bounds.left, monInfo.bounds.top, monInfo.bounds.right, monInfo.bounds.bottom);
            // Return cached texture if available (show last good frame)
            if (m_cachedTexture) {
                *outTexture = m_cachedTexture.Get();
                return true;
            }
            return false;
        }
        
        // Clamp to valid range (all values are now guaranteed non-negative for in-bounds region)
        srcBox.left = (UINT)(std::max)(0L, relLeft);
        srcBox.top = (UINT)(std::max)(0L, relTop);
        srcBox.right = (UINT)(std::min)((LONG)m_outputWidth, relRight);
        srcBox.bottom = (UINT)(std::min)((LONG)m_outputHeight, relBottom);
        srcBox.front = 0;
        srcBox.back = 1;

        // Copy region from desktop texture
        // Use primary context since we only capture from primary adapter
        m_primaryContext->CopySubresourceRegion(
            m_cachedTexture.Get(), 0,
            0, 0, 0,
            desktopTexture.Get(), 0,
            &srcBox
        );

        *outTexture = m_cachedTexture.Get();
        return true;
    }

    void Shutdown() override {
        if (m_frameAcquired && m_duplication) {
            m_duplication->ReleaseFrame();
            m_frameAcquired = false;
        }
        
        m_cachedTexture.Reset();
        m_duplication.Reset();
        m_primaryContext.Reset();
        m_factory.Reset();
        m_adapters.clear();
        m_monitors.clear();
        m_primaryDevice = nullptr;
        m_currentDevice = nullptr;
        m_initialized = false;
    }

    void SetSelfWindow(HWND hwnd) override {
        m_selfHwnd = hwnd;
    }

    void SetTargetMonitor(HMONITOR monitor) override {
        if (m_targetLocked) {
            LOG_DEBUG("DXGICapture::SetTargetMonitor: Target already locked to monitor %d", m_currentMonitorIndex);
            return;
        }

        // Find monitor with matching handle
        int monitorIndex = -1;
        for (size_t i = 0; i < m_monitors.size(); i++) {
            if (m_monitors[i].hMonitor == monitor) {
                monitorIndex = static_cast<int>(i);
                break;
            }
        }
        
        if (monitorIndex >= 0) {
            if (monitorIndex != m_currentMonitorIndex) {
                if (InitializeDuplicationForMonitor(monitorIndex)) {
                    m_targetLocked = true;
                    LOG_INFO("DXGICapture::SetTargetMonitor: Locked to monitor %p (index %d)", monitor, monitorIndex);
                } else {
                    LOG_WARN("DXGICapture::SetTargetMonitor: Failed to switch to monitor %p, locked to current %d", 
                             monitor, m_currentMonitorIndex);
                    // Force lock anyway to prevent thrashing
                    m_targetLocked = true;
                }
            } else {
                m_targetLocked = true;
                LOG_INFO("DXGICapture::SetTargetMonitor: Already on target monitor %p, locked", monitor);
            }
        } else {
            LOG_WARN("DXGICapture::SetTargetMonitor: Monitor %p not found in enumeration", monitor);
        }
    }

    bool IsTargetLocked() const override {
        return m_targetLocked;
    }

private:
    void EnumerateAllAdaptersAndMonitors(IDXGIAdapter* primaryAdapter) {
        m_monitors.clear();
        m_adapters.clear();
        
        LOG_INFO("EnumerateAllAdaptersAndMonitors: Starting multi-adapter enumeration...");
        
        // Enumerate all adapters
        ComPtr<IDXGIAdapter> adapter;
        for (UINT adapterIdx = 0; m_factory->EnumAdapters(adapterIdx, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; adapterIdx++) {
            DXGI_ADAPTER_DESC adapterDesc;
            adapter->GetDesc(&adapterDesc);
            
            // Convert adapter description to narrow string for logging
            char adapterName[128];
            WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, adapterName, sizeof(adapterName), nullptr, nullptr);
            LOG_INFO("Adapter[%u]: %s", adapterIdx, adapterName);
            
            // Check if this is the primary adapter (same as the one used by the main D3D device)
            bool isPrimaryAdapter = (adapter.Get() == primaryAdapter);
            
            // Create D3D device for this adapter (needed for Desktop Duplication)
            AdapterInfo adapterInfo;
            adapterInfo.adapter = adapter;
            adapterInfo.index = adapterIdx;
            
            if (isPrimaryAdapter) {
                // Reuse the primary device
                adapterInfo.device = m_primaryDevice;
                adapterInfo.context = m_primaryContext;
                m_primaryAdapterIndex = adapterIdx;
                LOG_INFO("  -> Primary adapter (index %u), reusing existing D3D device", adapterIdx);
            } else {
                // Create new D3D device for this adapter
                D3D_FEATURE_LEVEL featureLevel;
                UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
                #ifdef _DEBUG
                flags |= D3D11_CREATE_DEVICE_DEBUG;
                #endif
                
                HRESULT hr = D3D11CreateDevice(
                    adapter.Get(),
                    D3D_DRIVER_TYPE_UNKNOWN,  // Must use UNKNOWN when specifying adapter
                    nullptr,
                    flags,
                    nullptr, 0,
                    D3D11_SDK_VERSION,
                    adapterInfo.device.GetAddressOf(),
                    &featureLevel,
                    adapterInfo.context.GetAddressOf()
                );
                
                if (FAILED(hr)) {
                    LOG_WARN("  -> Failed to create D3D device for adapter %u (0x%08X), skipping", adapterIdx, hr);
                    continue;
                }
                LOG_INFO("  -> Created D3D device for secondary adapter");
            }
            
            m_adapters.push_back(adapterInfo);
            
            // Enumerate outputs (monitors) for this adapter
            ComPtr<IDXGIOutput> output;
            for (UINT outputIdx = 0; adapter->EnumOutputs(outputIdx, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; outputIdx++) {
                DXGI_OUTPUT_DESC desc;
                output->GetDesc(&desc);
                
                MonitorInfo info;
                info.output = output;
                info.adapter = adapter;
                info.adapterIndex = adapterIdx;
                info.bounds = desc.DesktopCoordinates;
                info.hMonitor = desc.Monitor;
                info.isPrimary = (adapterIdx == 0 && outputIdx == 0);
                
                // Get DPI for this monitor
                UINT dpiX, dpiY;
                if (SUCCEEDED(GetDpiForMonitor(desc.Monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
                    info.dpi = dpiX;
                } else {
                    info.dpi = 96;  // Default DPI
                }
                
                size_t monitorGlobalIndex = m_monitors.size();
                LOG_INFO("Monitor[%zu]: adapter=%u, bounds=(%d,%d,%d,%d), dpi=%u, primary=%d",
                    monitorGlobalIndex, adapterIdx,
                    info.bounds.left, info.bounds.top, 
                    info.bounds.right, info.bounds.bottom,
                    info.dpi, info.isPrimary ? 1 : 0);
                
                m_monitors.push_back(std::move(info));
            }
        }
        
        LOG_INFO("EnumerateAllAdaptersAndMonitors: Found %zu monitors across %zu adapters.",
            m_monitors.size(), m_adapters.size());
    }

    int FindMonitorForRegion(const RECT& region) const {
        // Find the monitor that contains the center of the region
        LONG centerX = (region.left + region.right) / 2;
        LONG centerY = (region.top + region.bottom) / 2;
        
        for (size_t i = 0; i < m_monitors.size(); i++) {
            const RECT& bounds = m_monitors[i].bounds;
            if (centerX >= bounds.left && centerX < bounds.right &&
                centerY >= bounds.top && centerY < bounds.bottom) {
                LOG_DEBUG("FindMonitorForRegion: region=(%d,%d,%d,%d) center=(%d,%d) -> monitor %d",
                    region.left, region.top, region.right, region.bottom,
                    centerX, centerY, static_cast<int>(i));
                return static_cast<int>(i);
            }
        }
        
        // Default to primary monitor
        LOG_WARN("FindMonitorForRegion: region=(%d,%d,%d,%d) center=(%d,%d) not found, defaulting to monitor 0",
            region.left, region.top, region.right, region.bottom, centerX, centerY);
        return 0;
    }

    bool InitializeDuplicationForMonitor(int monitorIndex) {
        LOG_DEBUG("InitializeDuplicationForMonitor: Attempting to initialize for monitor %d", monitorIndex);
        
        if (monitorIndex < 0 || monitorIndex >= static_cast<int>(m_monitors.size())) {
            LOG_ERROR("InitializeDuplicationForMonitor: Invalid monitor index %d (total monitors: %zu)",
                monitorIndex, m_monitors.size());
            return false;
        }

        // Find the adapter info for this monitor BEFORE releasing existing duplication
        const MonitorInfo& monInfo = m_monitors[monitorIndex];
        
        // Check if this monitor is on the primary adapter
        // Cross-adapter texture copy is not supported, so we can only capture from primary adapter
        if (monInfo.adapterIndex != m_primaryAdapterIndex) {
            LOG_WARN("InitializeDuplicationForMonitor: Monitor %d is on adapter %d, but primary adapter is %d. "
                "Cross-adapter capture not supported. Bounds: (%d,%d,%d,%d). Keeping current monitor.",
                monitorIndex, monInfo.adapterIndex, m_primaryAdapterIndex,
                monInfo.bounds.left, monInfo.bounds.top, monInfo.bounds.right, monInfo.bounds.bottom);
            // Do NOT reset existing duplication - keep current monitor working
            return false;
        }

        // Release existing duplication (only after we know we can switch)
        if (m_frameAcquired && m_duplication) {
            m_duplication->ReleaseFrame();
            m_frameAcquired = false;
        }
        m_duplication.Reset();
        
        ID3D11Device* deviceForMonitor = m_primaryDevice;

        // Get Output1 for duplication
        ComPtr<IDXGIOutput1> output1;
        HRESULT hr = monInfo.output.As(&output1);
        if (FAILED(hr)) {
            LOG_ERROR("InitializeDuplicationForMonitor: Failed to get IDXGIOutput1 for monitor %d (0x%08X)",
                monitorIndex, hr);
            return false;
        }

        // Create desktop duplication using the correct device for this adapter
        hr = output1->DuplicateOutput(deviceForMonitor, m_duplication.GetAddressOf());
        if (FAILED(hr)) {
            LOG_ERROR("InitializeDuplicationForMonitor: DuplicateOutput FAILED for monitor %d (adapter %d) (0x%08X)",
                monitorIndex, monInfo.adapterIndex, hr);
            // Common error codes:
            // E_ACCESSDENIED (0x80070005): Another app has exclusive access
            // DXGI_ERROR_NOT_CURRENTLY_AVAILABLE (0x887A0022): Desktop duplication not available
            // DXGI_ERROR_UNSUPPORTED (0x887A0004): Unsupported operation
            return false;
        }

        // Get output description for coordinate mapping
        DXGI_OUTDUPL_DESC duplDesc;
        m_duplication->GetDesc(&duplDesc);
        m_outputWidth = duplDesc.ModeDesc.Width;
        m_outputHeight = duplDesc.ModeDesc.Height;

        m_currentMonitorIndex = monitorIndex;
        m_currentDevice = deviceForMonitor;
        m_initialized = true;
        
        const RECT& bounds = monInfo.bounds;
        LOG_INFO("InitializeDuplicationForMonitor: SUCCESS monitor %d (adapter %d), output %ux%u, bounds=(%d,%d,%d,%d)",
            monitorIndex, monInfo.adapterIndex, m_outputWidth, m_outputHeight,
            bounds.left, bounds.top, bounds.right, bounds.bottom);
        return true;
    }

    RECT ConvertToPhysicalCoordinates(const RECT& logicalRect, int monitorIndex) const {
        if (monitorIndex < 0 || monitorIndex >= static_cast<int>(m_monitors.size())) {
            return logicalRect;
        }

        // For now, assume coordinates are already in physical units
        // Windows 10+ with DPI awareness typically reports physical coordinates
        // TODO: Add per-monitor DPI scaling if needed
        return logicalRect;
    }

    bool NeedsResize(int width, int height) const {
        return m_cachedWidth != width || m_cachedHeight != height;
    }

    // Primary device (passed from BlurWindow)
    ID3D11Device* m_primaryDevice = nullptr;
    ComPtr<ID3D11DeviceContext> m_primaryContext;
    
    // Current device used for duplication (may differ per monitor)
    ID3D11Device* m_currentDevice = nullptr;
    
    // Factory for enumerating all adapters
    ComPtr<IDXGIFactory> m_factory;
    
    // All adapters with their D3D devices
    std::vector<AdapterInfo> m_adapters;
    
    ComPtr<IDXGIOutputDuplication> m_duplication;
    ComPtr<ID3D11Texture2D> m_cachedTexture;

    std::vector<MonitorInfo> m_monitors;
    int m_currentMonitorIndex = 0;
    int m_primaryAdapterIndex = 0;  // Index of the primary adapter

    bool m_initialized = false;
    bool m_frameAcquired = false;
    bool m_targetLocked = false;  // Target monitor lock flag
    UINT m_outputWidth = 0;
    UINT m_outputHeight = 0;
    int m_cachedWidth = 0;
    int m_cachedHeight = 0;

    HWND m_selfHwnd = nullptr;
};

// Factory function
std::unique_ptr<ICaptureSubsystem> CreateDXGICapture() {
    return std::make_unique<DXGICapture>();
}

} // namespace blurwindow
