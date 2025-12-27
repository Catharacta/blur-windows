#pragma once

#include "../capture/ICaptureSubsystem.h"
#include "../effects/IBlurEffect.h"
#include "../presentation/IPresenter.h"
#include <memory>

namespace blurwindow {

/// Capture subsystem types
enum class CaptureType {
    DXGI,           // DXGI Desktop Duplication (default)
    WGC             // Windows.Graphics.Capture (future)
};

/// Effect types
enum class EffectType {
    Gaussian,       // Separable Gaussian blur
    Kawase,         // Kawase/Dual blur
    Box,            // Simple box blur
    Radial          // Radial / Zoom blur
};

/// Presenter types
enum class PresenterType {
    Auto,           // Auto-select (DirectComp preferred)
    DirectComp,     // DirectComposition (low latency)
    ULW             // UpdateLayeredWindow (fallback)
};

// Forward declarations for factory functions (defined in respective .cpp files)
std::unique_ptr<ICaptureSubsystem> CreateDXGICapture();
std::unique_ptr<IBlurEffect> CreateGaussianBlur();
std::unique_ptr<IBlurEffect> CreateKawaseBlur();
std::unique_ptr<IBlurEffect> CreateBoxBlur();
std::unique_ptr<IBlurEffect> CreateRadialBlur();
std::unique_ptr<IPresenter> CreateDirectCompPresenter();
std::unique_ptr<IPresenter> CreateULWPresenter();

/// Unified factory for creating subsystem instances
class SubsystemFactory {
public:
    /// Create a capture subsystem
    static std::unique_ptr<ICaptureSubsystem> CreateCapture(CaptureType type = CaptureType::DXGI) {
        switch (type) {
            case CaptureType::DXGI:
                return CreateDXGICapture();
            default:
                return nullptr;
        }
    }

    /// Create a blur effect
    static std::unique_ptr<IBlurEffect> CreateEffect(EffectType type = EffectType::Gaussian) {
        switch (type) {
            case EffectType::Gaussian:
                return CreateGaussianBlur();
            case EffectType::Kawase:
                return CreateKawaseBlur();
            case EffectType::Box:
                return CreateBoxBlur();
            case EffectType::Radial:
                return CreateRadialBlur();
            default:
                return nullptr;
        }
    }

    /// Create a presenter with automatic fallback
    static std::unique_ptr<IPresenter> CreatePresenter(
        PresenterType type,
        HWND hwnd,
        ID3D11Device* device
    ) {
        std::unique_ptr<IPresenter> presenter;

        // Auto: Try DirectComp first, then fallback to ULW
        if (type == PresenterType::Auto || type == PresenterType::DirectComp) {
            presenter = CreateDirectCompPresenter();
            if (presenter && presenter->Initialize(hwnd, device)) {
                OutputDebugStringA("Using DirectComposition presenter\n");
                return presenter;
            }
            OutputDebugStringA("DirectComposition failed, trying ULW fallback\n");
        }

        // ULW fallback (or explicit ULW request)
        if (type == PresenterType::Auto || type == PresenterType::ULW) {
            presenter = CreateULWPresenter();
            if (presenter && presenter->Initialize(hwnd, device)) {
                OutputDebugStringA("Using UpdateLayeredWindow presenter\n");
                return presenter;
            }
        }

        return nullptr;
    }
};

} // namespace blurwindow
