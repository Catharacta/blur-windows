# CustomBlurWindowLibrary (BlurWindow)

Windows 11向け高機能カスタムブラーウィンドウC++ライブラリ。
OS標準の Acrylic や Mica よりも自由度の高いブラー、ティント、ノイズ合成を提供します。

## 主な特徴

- **多彩なブラーエフェクト**: Gaussian, Box, Kawase, Radial Blur をサポート。
- **高速な処理**: Box Blur の分離型パス最適化などにより、低負荷で高品質なブラーを実現。
- **ノイズオーバーレイ**: 6種類のノイズ（White, Sin, Grid, Perlin, Simplex, Voronoi）を合成可能。
- **柔軟なティント**: 強度調整可能なカラーオーバーレイ。
- **マルチプラットフォーム連携**: 純粋な C API を通じて、C++ だけでなく Rust (Tauri), C#, Python 等からも利用可能。

## 技術スタック

- **言語**: C++17
- **グラフィックスAPI**: Direct3D 11 / HLSL
- **キャプチャ**: DXGI Desktop Duplication API
- **表示**: DirectComposition / Layered Window (UpdateLayeredWindow)

## クイックスタート (C++)

```cpp
#include "blurwindow/blurwindow.h"

// 1. システムの初期化
BlurSystemOptions opts;
opts.enableLogging = true;
BlurSystem::Instance().Initialize(opts);

// 2. ウィンドウの作成
WindowOptions winOpts;
winOpts.bounds = { 100, 100, 600, 500 };
auto window = BlurSystem::Instance().CreateBlurWindow(nullptr, winOpts);

// 3. エフェクトの設定と開始
window->SetEffectType(0); // Gaussian
window->SetBlurStrength(0.8f);
window->Start();
```

## デモアプリケーション

本リポジトリには2つのデモが含まれています：
1. **Win32 GUI Demo (`demos/gui_demo`)**: C++ 標準 Win32 API を使用した詳細な機能デモ。
2. **Tauri v2 Demo (`demos/tauri_demo`)**: Rust/ウェブ技術から C API を利用したモダンなデモ。

### Tauri デモの実行方法
```powershell
cd demos/tauri_demo
npm install
npm run tauri dev
```

## ビルド要件

- Windows 11
- Visual Studio 2022 (MSVC)
- Windows SDK 10.0.19041.0+
- CMake 3.20+

## ライセンス

MIT License
