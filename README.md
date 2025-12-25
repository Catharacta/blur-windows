# CustomBlurWindowLibrary

Windows 11向けカスタムブラーウィンドウC++ライブラリ

## 概要

OS提供のAcrylic/Micaより細かく制御できる「疑似透過（背景キャプチャ＋合成）」を行うカスタムブラーウィンドウ機能を提供するC++ライブラリ。

## 技術スタック

- **言語**: C++17/20
- **グラフィックスAPI**: Direct3D 11
- **キャプチャ**: DXGI Desktop Duplication API
- **表示**: DirectComposition / UpdateLayeredWindow
- **ビルド**: CMake

## ビルド要件

- Windows 11
- Visual Studio 2022
- Windows SDK 10.0.19041.0+
- CMake 3.20+

## ライセンス

MIT License
