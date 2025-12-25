#include "blurwindow/blurwindow.h"
#include "blurwindow/blur_window.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <conio.h>

using namespace blurwindow;

void PrintHelp() {
    std::cout << "\n=== Controls ===" << std::endl;
    std::cout << "  [1-3]  Switch effect: 1=Gaussian, 2=Kawase, 3=Box" << std::endl;
    std::cout << "  [Enter/Space] Cycle quality preset" << std::endl;
    std::cout << "  [Arrow Keys] Move window (Up/Down/Left/Right)" << std::endl;
    std::cout << "  [+/-]  Resize window" << std::endl;
    std::cout << "  [t]    Toggle topmost" << std::endl;
    std::cout << "  [c]    Toggle click-through" << std::endl;
    std::cout << "  [h]    Show this help" << std::endl;
    std::cout << "  [q]    Quit" << std::endl;
    std::cout << "================\n" << std::endl;
}

const char* GetPresetName(QualityPreset preset) {
    switch (preset) {
        case QualityPreset::High: return "High";
        case QualityPreset::Balanced: return "Balanced";
        case QualityPreset::Performance: return "Performance";
        case QualityPreset::Minimal: return "Minimal";
        default: return "Unknown";
    }
}

int main() {
    std::cout << "=== BlurWindow Library Interactive Demo ===" << std::endl;
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

    // Initial window position and size
    int winX = 100, winY = 100, winW = 400, winH = 300;

    // Create a demo window
    WindowOptions winOpts = {};
    winOpts.owner = nullptr;
    winOpts.bounds = {winX, winY, winX + winW, winY + winH};
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

    PrintHelp();

    // Current state
    QualityPreset presets[] = {
        QualityPreset::High,
        QualityPreset::Balanced,
        QualityPreset::Performance,
        QualityPreset::Minimal
    };
    int currentPresetIndex = 1;  // Start with Balanced
    const char* currentEffect = "Gaussian";
    bool topMost = true;
    bool clickThrough = true;

    bool running = true;
    while (running) {
        // Print current status
        std::cout << "\r[" << currentEffect << " | " 
                  << GetPresetName(presets[currentPresetIndex]) << "] FPS: " 
                  << window->GetCurrentFPS() 
                  << " | Pos: " << winX << "," << winY
                  << " | Size: " << winW << "x" << winH
                  << "        " << std::flush;

        // Check for input
        if (_kbhit()) {
            int ch = _getch();
            
            // Handle special keys (arrow keys)
            if (ch == 0 || ch == 224) {
                int ext = _getch();
                switch (ext) {
                    case 72: winY -= 20; break;  // Up
                    case 80: winY += 20; break;  // Down
                    case 75: winX -= 20; break;  // Left
                    case 77: winX += 20; break;  // Right
                }
                // Update window position
                SetWindowPos(window->GetHWND(), nullptr, winX, winY, 0, 0, 
                    SWP_NOSIZE | SWP_NOZORDER);
            } else {
                switch (ch) {
                    case 'q': case 'Q':
                        running = false;
                        break;
                    
                    case '\r': case ' ':
                        // Cycle preset
                        currentPresetIndex = (currentPresetIndex + 1) % 4;
                        window->SetPreset(presets[currentPresetIndex]);
                        std::cout << "\n>>> Switched to preset: " << GetPresetName(presets[currentPresetIndex]) << std::endl;
                        break;
                    
                    case '1':
                        currentEffect = "Gaussian";
                        std::cout << "\n>>> Switched to effect: Gaussian" << std::endl;
                        // TODO: window->SetEffect("gaussian");
                        break;
                    
                    case '2':
                        currentEffect = "Kawase";
                        std::cout << "\n>>> Switched to effect: Kawase" << std::endl;
                        // TODO: window->SetEffect("kawase");
                        break;
                    
                    case '3':
                        currentEffect = "Box";
                        std::cout << "\n>>> Switched to effect: Box" << std::endl;
                        // TODO: window->SetEffect("box");
                        break;
                    
                    case '+': case '=':
                        winW += 50; winH += 40;
                        SetWindowPos(window->GetHWND(), nullptr, 0, 0, winW, winH, 
                            SWP_NOMOVE | SWP_NOZORDER);
                        break;
                    
                    case '-': case '_':
                        winW = (winW > 150) ? winW - 50 : 150;
                        winH = (winH > 120) ? winH - 40 : 120;
                        SetWindowPos(window->GetHWND(), nullptr, 0, 0, winW, winH, 
                            SWP_NOMOVE | SWP_NOZORDER);
                        break;
                    
                    case 't': case 'T':
                        topMost = !topMost;
                        SetWindowPos(window->GetHWND(), 
                            topMost ? HWND_TOPMOST : HWND_NOTOPMOST,
                            0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                        std::cout << "\n>>> TopMost: " << (topMost ? "ON" : "OFF") << std::endl;
                        break;
                    
                    case 'c': case 'C':
                        clickThrough = !clickThrough;
                        {
                            LONG_PTR style = GetWindowLongPtr(window->GetHWND(), GWL_EXSTYLE);
                            if (clickThrough) {
                                style |= WS_EX_TRANSPARENT;
                            } else {
                                style &= ~WS_EX_TRANSPARENT;
                            }
                            SetWindowLongPtr(window->GetHWND(), GWL_EXSTYLE, style);
                        }
                        std::cout << "\n>>> Click-through: " << (clickThrough ? "ON" : "OFF") << std::endl;
                        break;
                    
                    case 'h': case 'H':
                        PrintHelp();
                        break;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
