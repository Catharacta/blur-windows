# Usage Guide

A guide for integrating the `blurwindow` library into your project.

## Required Files

The release package (ZIP) includes the following files:

| File / Folder | Purpose | Required for Development | Required at Runtime |
| :--- | :--- | :---: | :---: |
| `bin/blurwindow.dll` | Library binary (actual program) | - | **Required** |
| `lib/blurwindow.lib` | Import library (for linking) | **Required** (C++/Rust) | - |
| `include/blurwindow/*.h` | Public header files (API definitions) | **Required** | - |

---

## Setup Instructions by Development Environment

### 1. C++ (Visual Studio / MSBuild)

1. **Set Include Path**:
   - Project Properties > C/C++ > General > Additional Include Directories: Add the path to the `include` folder.
2. **Set Library Path**:
   - Project Properties > Linker > General > Additional Library Directories: Add the path to the `lib` folder.
3. **Specify Libraries**:
   - Project Properties > Linker > Input > Additional Dependencies: Add `blurwindow.lib` (or use `#pragma comment(lib, "blurwindow.lib")` in code).
4. **Runtime Setup**:
   - Copy `blurwindow.dll` to the same folder as your built `.exe`.

### 2. Rust / Tauri

1. **File Placement**:
   - Create a `libs` folder in your project root and copy `blurwindow.lib` and `blurwindow.dll` there.
2. **Create/Update build.rs**:
   ```rust
   fn main() {
       println!("cargo:rustc-link-search=native=libs");
       println!("cargo:rustc-link-lib=dylib=blurwindow");
   }
   ```
3. **API Definitions**:
   - Refer to `include/blurwindow/c_api.h` and declare functions in an `extern "C"` block.
4. **Runtime Setup (Tauri)**:
   - Add `libs/blurwindow.dll` to `bundle > resources` in `tauri.conf.json`. During development, place the DLL directly under `src-tauri` or in the same location as the binary.

---

## Troubleshooting

### "DLL not found" error
- Verify that `blurwindow.dll` exists in the same directory as your executable (`.exe`).
- Ensure the build targets 64-bit (x64). This library is x64 only.

### Unresolved symbol (LNK2019) error
- Verify that `blurwindow.lib` is correctly linked and the library path is set.
- When using the C API from C++, ensure it's wrapped with `extern "C"` (`c_api.h` handles this automatically).

---

## Multi-monitor Support and Capture Methods

Since version 0.1.0, BlurWindow supports two capture methods:

1.  **DXGI Desktop Duplication API**:
    *   Available on Windows 8 and later.
    *   **Limitation**: Capture between monitors connected to different GPUs is NOT supported (e.g., hybrid graphics setups with iGPU and dGPU). Attempting to blur a window on a different GPU will result in a black texture.

2.  **Windows Graphics Capture (WGC)**:
    *   Available on Windows 10 Version 1803 (April 2018 Update) and later.
    *   **Benefit**: Fully supports cross-GPU capture. Windows can be blurred correctly regardless of which monitor they are on.

### Auto Mode

By default (`Auto`), the library operates with the following logic:

*   **If WGC is available**: It prioritizes using WGC. This ensures blur works regardless of your GPU configuration.
*   **If WGC is unavailable**: It automatically falls back to DXGI for older Windows environments.

### Multiple Monitors / Multiple Windows Behavior

Each `BlurWindow` automatically determines its target monitor based on its `bounds` when `blur_start` is called, and locks its capture session to that specific monitor.

This ensures:
*   **No flickering**: Even when multiple blur windows exist simultaneously, capture sessions do not conflict.
*   **No image mixing**: Each window captures only the desktop of its own monitor.

```
BlurWindow for Monitor A → Fixed capture of Monitor A's desktop
BlurWindow for Monitor B → Fixed capture of Monitor B's desktop
BlurWindow for Monitor C → Fixed capture of Monitor C's desktop
```

> [!NOTE]
> The target monitor is determined from the center coordinates of the `bounds`. Even if the window position changes, the captured monitor remains the same.

### Control via C API

You can explicitly set the capture method using the `blur_set_capture_method` function.

```c
// 0=Auto, 1=DXGI, 2=WGC
blur_set_capture_method(window, 2); 
```
