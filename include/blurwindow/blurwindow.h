#pragma once

// Version information
#define BLURWINDOW_VERSION_MAJOR 0
#define BLURWINDOW_VERSION_MINOR 1
#define BLURWINDOW_VERSION_PATCH 0

// Export/Import macro
#ifdef BLURWINDOW_DLL
    #ifdef BLURWINDOW_EXPORTS
        #define BLURWINDOW_API __declspec(dllexport)
    #else
        #define BLURWINDOW_API __declspec(dllimport)
    #endif
#else
    #define BLURWINDOW_API
#endif

// Forward declarations
#include <windows.h>
#include <memory>

// Forward declaration for D3D11
struct ID3D11Device;

namespace blurwindow {

// Forward declarations
class BlurWindow;
class EffectPipeline;

/// Quality preset enumeration
enum class QualityPreset {
    High,        ///< High quality, higher GPU load
    Balanced,    ///< Balanced quality and performance (default)
    Performance, ///< Lower quality, prioritize performance
    Minimal      ///< Minimum quality, lowest GPU load
};

/// Window creation options
struct WindowOptions {
    HWND owner = nullptr;       ///< Owner window handle
    RECT bounds = {0, 0, 0, 0}; ///< Window bounds (screen coordinates)
    bool topMost = true;        ///< Always on top
    bool clickThrough = true;   ///< Click-through window
};

/// System initialization options
struct BlurSystemOptions {
    bool enableLogging = false;           ///< Enable logging
    const char* logPath = nullptr;        ///< Log file path (nullptr for console)
    void (*logCallback)(const char* message) = nullptr; ///< Optional log callback
    QualityPreset defaultPreset = QualityPreset::Balanced; ///< Default quality preset
};

/// Main blur system class (singleton)
class BLURWINDOW_API BlurSystem {
public:
    /// Get singleton instance
    static BlurSystem& Instance();

    /// Initialize the blur system
    /// @param opts System options
    /// @return true on success
    bool Initialize(const BlurSystemOptions& opts = {});

    /// Shutdown the blur system
    void Shutdown();

    /// Update system options
    void SetOptions(const BlurSystemOptions& opts);

    /// Check if system is initialized
    bool IsInitialized() const;

    /// Get the D3D11 device (internal use)
    /// @return D3D11 device pointer, nullptr if not initialized
    ID3D11Device* GetDevice() const;

    /// Create a blur window
    /// @param owner Owner window handle
    /// @param opts Window options
    /// @return Unique pointer to BlurWindow, nullptr on failure
    std::unique_ptr<BlurWindow> CreateBlurWindow(HWND owner, const WindowOptions& opts);

    // Disable copy/move
    BlurSystem(const BlurSystem&) = delete;
    BlurSystem& operator=(const BlurSystem&) = delete;

private:
    BlurSystem();
    ~BlurSystem();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace blurwindow
