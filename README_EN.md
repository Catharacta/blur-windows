# CustomBlurWindowLibrary (BlurWindow)

A high-performance custom blur window C++ library for Windows 11.
Provides more flexible blur, tint, and noise compositing than the OS-standard Acrylic or Mica effects.

## Documentation

- [**Usage Guide (English)**](docs/USAGE_GUIDE_EN.md): Library setup, required files, and placement.
- [**C API Reference (English)**](docs/C_API_REFERENCE_EN.md): C API reference for FFI usage from Rust, C#, etc.
- [**導入ガイド (日本語)**](docs/USAGE_GUIDE.md): ライブラリのセットアップ、必要ファイル、配置場所について。
- [**C API リファレンス (日本語)**](docs/C_API_REFERENCE.md): 他言語から利用するための API 手引書。

## Key Features

- **Multiple Blur Effects**: Supports Gaussian, Box, Kawase, and Radial Blur.
- **Rain Effect**: Codrops-compatible raindrop effect with refraction simulation and gooey effect.
- **High Performance**: Low-overhead, high-quality blur through optimizations like separable Box Blur passes.
- **Noise Overlay**: Six types of noise compositing (White, Sin, Grid, Perlin, Simplex, Voronoi).
- **Flexible Tint**: Adjustable intensity color overlay.
- **Cross-Language Support**: Pure C API enables use from C++, Rust (Tauri), C#, Python, and more.

## Technology Stack

- **Language**: C++17
- **Graphics API**: Direct3D 11 / HLSL
- **Capture**: DXGI Desktop Duplication API
- **Presentation**: DirectComposition / Layered Window (UpdateLayeredWindow)

## Quick Start (C++)

```cpp
#include "blurwindow/blurwindow.h"

// 1. Initialize the system
BlurSystemOptions opts;
opts.enableLogging = true;
BlurSystem::Instance().Initialize(opts);

// 2. Create a window
WindowOptions winOpts;
winOpts.bounds = { 100, 100, 600, 500 };
auto window = BlurSystem::Instance().CreateBlurWindow(nullptr, winOpts);

// 3. Configure and start the effect
window->SetEffectType(0); // Gaussian
window->SetBlurStrength(0.8f);
window->Start();
```

## Demo Applications

This repository includes two demos:
1. **Win32 GUI Demo (`demos/gui_demo`)**: Detailed feature demo using standard C++ Win32 API.
2. **Tauri v2 Demo (`demos/tauri_demo`)**: Modern demo using Rust/web technologies via C API.

### Running the Tauri Demo
```powershell
cd demos/tauri_demo
npm install
npm run tauri dev
```

## Build Requirements

- Windows 11
- Visual Studio 2022 (MSVC)
- Windows SDK 10.0.19041.0+
- CMake 3.20+

## License

MIT License
