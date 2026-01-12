#include "MagnificationCapture.h"
#include "../core/Logger.h"
#include <wincodec.h> // For WIC pixel format GUIDs

namespace blurwindow {

// Global or static map to retrieve instance from HWND if GWLP_USERDATA doesn't work on Mag window
// But usually GWLP_USERDATA works on any window we created or own.
// Magnifier window is created by us with "Magnifier" class.

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

    // Create a hidden host window (off-screen or just not visible?)
    // Magnification requires the window to be "visible" to system?
    // We'll place it off-screen.
    m_hostHwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"BlurMagnificationHost",
        L"MagnificationHost",
        WS_POPUP, // Visible but off-screen
        -32000, -32000, 10, 10,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr
    );

    if (!m_hostHwnd) return false;
    
    // It must be visible for its children to be visible/active
    ShowWindow(m_hostHwnd, SW_SHOWNA);
    
    // Make it "visible" to the system by setting opacity to 255 (opaque)
    // Even if it's off-screen, layered windows need this to be composited/active.
    SetLayeredWindowAttributes(m_hostHwnd, 0, 255, LWA_ALPHA);
    
    // Exclude host from capture too
    SetWindowDisplayAffinity(m_hostHwnd, WDA_EXCLUDEFROMCAPTURE);

    return true;
}

bool MagnificationCapture::CreateMagnifierControl() {
    // Create the magnifier control as a child of the host
    // The size will be updated in CaptureFrame
    // WC_MAGNIFIER is usually "Magnifier" but on some systems it might be wide.
    // However, since we are using explicit Wide API, we should check if WC_MAGNIFIERW is available or cast.
    // Standard Magnification API defines WC_MAGNIFIER as L"Magnifier" or "Magnifier". 
    // Safest is to use L"Magnifier" directly corresponding to the wide API.
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

    // Store 'this' pointer in the Magnifier window for the callback
    SetWindowLongPtr(m_magHwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    return true;
}

bool MagnificationCapture::InitOnRenderThread() {
    if (m_magInitialized) return true;

    if (!MagInitialize()) {
        LOG_ERROR("MagInitialize failed.");
        return false;
    }

    if (!CreateHostWindow()) {
        LOG_ERROR("Failed to create host window for Magnification.");
        return false;
    }

    if (!CreateMagnifierControl()) {
        LOG_ERROR("Failed to create magnifier control.");
        return false;
    }
    
    m_magInitialized = true;
    m_filterDirty = true; // Force filter application
    
    LOG_INFO("MagnificationCapture initialized on Render Thread.");
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
    // Get instance
    auto* self = reinterpret_cast<MagnificationCapture*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!self) return TRUE;

    // Lock and copy data
    // srcheader.width/height should match what we requested
    // srcdata points to the bits
    
    // Assume 32bpp (4 bytes per pixel)
    size_t size = srcheader.width * srcheader.height * 4;
    
    std::lock_guard<std::mutex> lock(self->m_bufferMutex);
    
    if (self->m_pixelBuffer.size() != size) {
        self->m_pixelBuffer.resize(size);
    }
    
    memcpy(self->m_pixelBuffer.data(), srcdata, size);
    self->m_bufferWidth = srcheader.width;
    self->m_bufferHeight = srcheader.height;
    self->m_hasNewFrame = true;

    return TRUE; // Return TRUE to continue
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
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // Assuming source is BGRA compatible
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
            // LOG_INFO("Updated exclusion filter list.");
        } else {
            LOG_ERROR("MagSetWindowFilterList failed in CaptureFrame.");
        }
        m_filterDirty = false;
    }

    if (!m_magHwnd || !m_device) return false;

    int width = region.right - region.left;
    int height = region.bottom - region.top;
    if (width <= 0 || height <= 0) return false;

    // 1. Resize Host and Magnifier windows
    // Resize Host first to ensure it contains the child (if clipping applies)
    SetWindowPos(m_hostHwnd, nullptr, 0, 0, width, height, 
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

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
    // Update: InvalidateRect + UpdateWindow usually works
    InvalidateRect(m_magHwnd, nullptr, TRUE);
    // UpdateWindow(m_magHwnd); 
    // Or pump messages if we are on the same thread that created the window
    
    // Since we created the window on THIS thread (in Initialize), we must pump messages
    MSG msg;
    // Process all pending messages to ensure WM_PAINT is handled by the Mag control
    // We use PeekMessage to clear the queue, but we might need to wait?
    // MagSetImageScalingCallback is called during WM_PAINT processing by the Mag control.
    // So pumping messages is essential.
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
                m_hasNewFrame = false; // Reset flag? Or keep the last frame?
                // Probably reset to ensure we don't return stale data if next update fails?
                // But returning stale data is better than nothing.
                // Keeping it is fine.
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
    return std::make_unique<MagnificationCapture>();
}

} // namespace blurwindow
