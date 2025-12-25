#include "blurwindow/blurwindow.h"
#include "blurwindow/blur_window.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <conio.h>

using namespace blurwindow;

// Simple console-based demo
int main() {
    std::cout << "=== BlurWindow Library Demo ===" << std::endl;
    std::cout << "Version: " << BLURWINDOW_VERSION_MAJOR << "."
              << BLURWINDOW_VERSION_MINOR << "."
              << BLURWINDOW_VERSION_PATCH << std::endl;
    std::cout << std::endl;

    // Initialize blur system
    BlurSystemOptions opts = {};
    opts.enableLogging = true;
    opts.defaultPreset = QualityPreset::Balanced;

    std::cout << "[1] Initializing BlurSystem..." << std::endl;
    if (!BlurSystem::Instance().Initialize(opts)) {
        std::cerr << "Failed to initialize BlurSystem!" << std::endl;
        return 1;
    }
    std::cout << "    BlurSystem initialized successfully." << std::endl;

    // Create a demo window (owner = desktop)
    WindowOptions winOpts = {};
    winOpts.owner = nullptr;  // No owner
    winOpts.bounds = {100, 100, 500, 400};  // 400x300 window
    winOpts.topMost = true;
    winOpts.clickThrough = true;

    std::cout << "[2] Creating BlurWindow..." << std::endl;
    auto window = BlurSystem::Instance().CreateBlurWindow(nullptr, winOpts);
    if (!window) {
        std::cerr << "Failed to create BlurWindow!" << std::endl;
        BlurSystem::Instance().Shutdown();
        return 1;
    }
    std::cout << "    BlurWindow created. HWND: " << window->GetHWND() << std::endl;

    // Show the window
    ShowWindow(window->GetHWND(), SW_SHOW);

    // Start blur effect
    std::cout << "[3] Starting blur effect..." << std::endl;
    window->Start();

    // Demo loop
    std::cout << std::endl;
    std::cout << "Demo running. Press Enter to cycle through presets, 'q' to quit." << std::endl;
    std::cout << std::endl;

    QualityPreset presets[] = {
        QualityPreset::High,
        QualityPreset::Balanced,
        QualityPreset::Performance,
        QualityPreset::Minimal
    };
    const char* presetNames[] = {"High", "Balanced", "Performance", "Minimal"};
    int currentPreset = 1;  // Start with Balanced

    bool running = true;
    while (running) {
        // Print current status
        std::cout << "\r[" << presetNames[currentPreset] << "] FPS: " 
                  << window->GetCurrentFPS() << "    " << std::flush;

        // Check for input (non-blocking would be better, but simple for demo)
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q') {
                running = false;
            } else if (ch == '\r' || ch == ' ') {
                // Cycle preset
                currentPreset = (currentPreset + 1) % 4;
                window->SetPreset(presets[currentPreset]);
                std::cout << std::endl << "Switched to: " << presetNames[currentPreset] << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Cleanup
    std::cout << std::endl << "[4] Stopping blur effect..." << std::endl;
    window->Stop();

    std::cout << "[5] Destroying window..." << std::endl;
    window.reset();

    std::cout << "[6] Shutting down BlurSystem..." << std::endl;
    BlurSystem::Instance().Shutdown();

    std::cout << "Demo complete." << std::endl;
    return 0;
}
