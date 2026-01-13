#include "MagnificationCapture.h"
#include "../core/Logger.h"
#include <wincodec.h> // For WIC pixel format GUIDs
#include <memory>
#include <map> // Must be global

namespace blurwindow {

// Global map to retrieve instance from HWND
// This is necessary because GetWindowLongPtr/GetProp might fail on system-controlled Magnifier windows
static std::map<HWND, MagnificationCapture*> g_instanceMap;
static std::mutex g_mapMutex;

MagnificationCapture::MagnificationCapture() {
}

MagnificationCapture::~MagnificationCapture() {
    Shutdown();
}

bool MagnificationCapture::Initialize(ID3D11Device* device) {
    if (!device) return false;
    m_device = device;
    m_device->GetImmediateContext(m_context.ReleaseAndGetAddressOf());
    
    // Defer actual resource creation to InitOnRenderThread (called from CaptureFrame)
    // to ensure window creation and message pump run on the same thread.
    return true;
}

void MagnificationCapture::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(g_mapMutex);
        if (m_magHwnd) g_instanceMap.erase(m_magHwnd);
        if (m_hostHwnd) g_instanceMap.erase(m_hostHwnd);
    }

    if (m_magHwnd) {
        DestroyWindow(m_magHwnd);
        m_magHwnd = nullptr;
    }
    if (m_hostHwnd) {
        DestroyWindow(m_hostHwnd);
        m_hostHwnd = nullptr;
    }
    
    MagUninitialize();
    
    m_capturedTexture.Reset();
    m_pixelBuffer.clear();
    m_device = nullptr;
}

LRESULT CALLBACK MagnificationCapture::HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool MagnificationCapture::CreateHostWindow() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = HostWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"BlurMagnificationHost";
    RegisterClassExW(&wc);

    // Create a hidden host window (off-screen initially)
    // Magnification requires the window to be "visible" to system?
    // We'll place it off-screen initially.
    m_hostHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"BlurMagnificationHost",
        L"MagnificationHost",
        WS_POPUP, // Visible style
        -32000, -32000, 10, 10,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!m_hostHwnd) return false;
    
    // It must be visible for its children to be visible/active
    ShowWindow(m_hostHwnd, SW_SHOWNA);
    
    // Make it "visible" to the system but almost transparent
    // Alpha 1/255 is practically invisible but ensures the window is treated as "visible" by composition
    SetLayeredWindowAttributes(m_hostHwnd, 0, 1, LWA_ALPHA);
    
    // Exclude host from capture too
    SetWindowDisplayAffinity(m_hostHwnd, WDA_EXCLUDEFROMCAPTURE);

    return true;
}

bool MagnificationCapture::CreateMagnifierControl() {
    // Create the magnifier control as a child of the host
    m_magHwnd = CreateWindowW(L"Magnifier", L"MagnifierWindow",
        WS_CHILD | WS_VISIBLE, // Must be visible
        0, 0, 100, 100,
        m_hostHwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!m_magHwnd) {
        LOG_ERROR("CreateWindow(WC_MAGNIFIER) failed. Error: %d", GetLastError());
        return false;
    }

    // Set callback
    if (!MagSetImageScalingCallback(m_magHwnd, MagImageScalingCallback)) {
        LOG_ERROR("MagSetImageScalingCallback failed.");
        return false;
    }

    // Store 'this' pointer in the global map for known HWNDs
    {
        std::lock_guard<std::mutex> lock(g_mapMutex);
        g_instanceMap[m_magHwnd] = this;
        g_instanceMap[m_hostHwnd] = this; // Also register host
    }
    printf("[DLL] Stored 'this' %p for Mag=%p, Host=%p in Global Map\n", this, m_magHwnd, m_hostHwnd);

    return true;
}

bool MagnificationCapture::InitOnRenderThread() {
    if (m_magInitialized) return true;

    printf("[DLL] MagnificationCapture::InitOnRenderThread: ThreadID=%u\n", GetCurrentThreadId());

    if (!MagInitialize()) {
        LOG_ERROR("MagInitialize failed.");
        printf("[DLL] MagInitialize failed. Error: %u\n", GetLastError());
        return false;
    }

    if (!CreateHostWindow()) {
        LOG_ERROR("Failed to create host window for Magnification.");
        return false;
    }
    printf("[DLL] HostWindow Created: %p\n", m_hostHwnd);

    if (!CreateMagnifierControl()) {
        LOG_ERROR("Failed to create magnifier control.");
        return false;
    }
    printf("[DLL] MagnifierControl Created: %p\n", m_magHwnd);
    
    // Apply filter initially
    if (m_selfHwnd) {
         bool ret = MagSetWindowFilterList(m_magHwnd, MW_FILTERMODE_EXCLUDE, 1, &m_selfHwnd);
         printf("[DLL] MagSetWindowFilterList (Init): Hwnd=%p, Result=%d, Error=%u\n", m_selfHwnd, ret, GetLastError());
    }

    bool cbRet = MagSetImageScalingCallback(m_magHwnd, MagImageScalingCallback);
    printf("[DLL] MagSetImageScalingCallback: Result=%d, Error=%u\n", cbRet, GetLastError());

    m_magInitialized = true;
    m_filterDirty = true; // Force filter application
    
    LOG_INFO("MagnificationCapture initialized on Render Thread.");
    printf("[DLL] MagnificationCapture initialized successfully.\n");
    return true;
}

void MagnificationCapture::SetSelfWindow(HWND hwnd) {
    if (m_selfHwnd != hwnd) {
        m_selfHwnd = hwnd;
        m_filterDirty = true;
    }
}

void MagnificationCapture::SetTargetMonitor(HMONITOR monitor) {
    m_targetMonitor = monitor;
    m_monitorLocked = true;
}

bool MagnificationCapture::IsTargetLocked() const {
    return m_monitorLocked;
}

BOOL CALLBACK MagnificationCapture::MagImageScalingCallback(
    HWND hwnd,
    void* srcdata,
    MAGIMAGEHEADER srcheader,
    void* destdata,
    MAGIMAGEHEADER destheader,
    RECT unclipped,
    RECT clipped,
    HRGN dirty
) {
    // Debug log for first callback hits
    static int callbackCount = 0;
    bool debug = (callbackCount < 5);
    if (debug) {
        printf("[DLL] MagImageScalingCallback Hit! Count=%d, Size=%dx%d\n", callbackCount++, srcheader.width, srcheader.height);
    }
    
    // Silence warnings
    (void)srcdata; (void)destdata; (void)destheader; (void)unclipped; (void)clipped; (void)dirty;

    // Get instance from global map with parent chain lookup
    MagnificationCapture* self = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mapMutex);
        HWND current = hwnd;
        int depth = 0;
        while (current && depth < 10) { // Limit depth just in case
            auto it = g_instanceMap.find(current);
            if (it != g_instanceMap.end()) {
                self = it->second;
                break;
            }
            current = GetParent(current);
            depth++;
        }
        if (debug && !self) {
             printf("[DLL] Map lookup failed for HWND %p (Depth=%d)\n", hwnd, depth);
        }
    }

    if (!self) {
        // printf("[DLL] Callback: Self is NULL (Map lookup failed for HWND %p)\n", hwnd);
        return TRUE;
    }
    if (debug) printf("[DLL] Callback: Self=%p found for HWND %p\n", self, hwnd);

    if (!srcdata) {
         printf("[DLL] Callback: srcdata is NULL!\n");
         return TRUE;
    }
    
    size_t size = srcheader.width * srcheader.height * 4;
    
    if (debug) printf("[DLL] Callback: Locking mutex...\n");
    std::unique_lock<std::mutex> lock(self->m_bufferMutex, std::defer_lock);
    lock.lock(); // Try standard lock first
    
    if (debug) printf("[DLL] Callback: Resizing buffer to %llu...\n", size);
    if (self->m_pixelBuffer.size() != size) {
        self->m_pixelBuffer.resize(size);
    }
    
    if (debug) printf("[DLL] Callback: Memcpy...\n");
    memcpy(self->m_pixelBuffer.data(), srcdata, size);
    
    self->m_bufferWidth = srcheader.width;
    self->m_bufferHeight = srcheader.height;
    self->m_hasNewFrame = true;

    if (debug) printf("[DLL] Callback: Done.\n");
    return TRUE; 
}

bool MagnificationCapture::EnsureTexture(int width, int height) {
    if (m_capturedTexture) {
        D3D11_TEXTURE2D_DESC desc;
        m_capturedTexture->GetDesc(&desc);
        if (desc.Width == static_cast<UINT>(width) && desc.Height == static_cast<UINT>(height)) {
            return true;
        }
    }

    m_capturedTexture.Reset();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; 
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, m_capturedTexture.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create capture texture: 0x%08X", hr);
        return false;
    }
    return true;
}

bool MagnificationCapture::CaptureFrame(const RECT& region, ID3D11Texture2D** outTexture) {
    // Ensure initialized on this thread
    if (!m_magInitialized) {
        if (!InitOnRenderThread()) return false;
    }
    
    // Update filter if dirty (must be done on owning thread)
    if (m_filterDirty && m_magHwnd && m_selfHwnd) {
        if (MagSetWindowFilterList(m_magHwnd, MW_FILTERMODE_EXCLUDE, 1, &m_selfHwnd)) {
             // Success
        } else {
            LOG_ERROR("MagSetWindowFilterList failed in CaptureFrame.");
            printf("[DLL] MagSetWindowFilterList FAILED: Error=%u\n", GetLastError());
        }
        m_filterDirty = false;
    }

    if (!m_magHwnd || !m_device) {
        printf("[DLL] CaptureFrame: Pre-check failed. MagHwnd=%p, Device=%p\n", m_magHwnd, m_device);
        return false;
    }

    int width = region.right - region.left;
    int height = region.bottom - region.top;
    if (width <= 0 || height <= 0) return false;

    // 1. Resize Host and Magnifier windows
    // Move Host to cover the capture area to ensure it's "on screen" for painting
    SetWindowPos(m_hostHwnd, nullptr, region.left, region.top, width, height, 
        SWP_NOZORDER | SWP_NOACTIVATE); 

    // Resize Magnifier to match
    SetWindowPos(m_magHwnd, nullptr, 0, 0, width, height, 
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    // 2. Set Source (Desktop coordinates)
    RECT sourceRect = region;
    if (!MagSetWindowSource(m_magHwnd, sourceRect)) {
        LOG_ERROR("MagSetWindowSource failed.");
        return false;
    }

    // 3. Trigger update
    // Force a paint to trigger the callback
    InvalidateRect(m_magHwnd, nullptr, TRUE);
    
    // Pump messages
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 4. Check for new data
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        if (m_hasNewFrame && m_bufferWidth == width && m_bufferHeight == height) {
            if (EnsureTexture(width, height) && m_context) {
                // Upload data
                m_context->UpdateSubresource(
                    m_capturedTexture.Get(), 0, nullptr,
                    m_pixelBuffer.data(),
                    width * 4, // Pitch (4 bytes per pixel)
                    0
                );
                
                *outTexture = m_capturedTexture.Get();
                m_hasNewFrame = false; 
                return true;
            }
        }
    }
    
    // If we have an old texture, return it even if no new frame (better than flicker)
    if (m_capturedTexture) {
        *outTexture = m_capturedTexture.Get();
        return true;
    }

    return false;
}

// Factory function
std::unique_ptr<ICaptureSubsystem> CreateMagnificationCapture() {
    return std::unique_ptr<ICaptureSubsystem>(new MagnificationCapture());
}

} // namespace blurwindow
