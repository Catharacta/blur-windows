#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>
#include "blurwindow/blurwindow.h"
#include "blurwindow/blur_window.h"

#pragma comment(lib, "comctl32.lib")

using namespace blurwindow;

// Global variables
std::unique_ptr<BlurWindow> g_blurWindow;
HWND g_hLogEdit = NULL;
HWND g_hStatusText = NULL;

// Constants for controls
#define ID_BTN_START    1001
#define ID_BTN_STOP     1002
#define ID_BTN_GAUSSIAN 1011
#define ID_BTN_KAWASE   1012
#define ID_BTN_BOX      1013
#define ID_BTN_HIGH     1021
#define ID_BTN_BALANCED 1022
#define ID_BTN_PERF     1023
#define ID_BTN_MINIMAL  1024

// Helper to log message to the GUI
void AppendLog(const std::wstring& msg) {
    if (g_hLogEdit) {
        int len = GetWindowTextLength(g_hLogEdit);
        SendMessage(g_hLogEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessage(g_hLogEdit, EM_REPLACESEL, 0, (LPARAM)(msg + L"\r\n").c_str());
    }
}

void UpdateStatus(const std::wstring& status) {
    if (g_hStatusText) {
        SetWindowText(g_hStatusText, status.c_str());
    }
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // Create controls
        int x = 10, y = 10;
        CreateWindow(L"STATIC", L"Controls:", WS_VISIBLE | WS_CHILD, x, y, 100, 20, hwnd, NULL, NULL, NULL);
        y += 30;
        CreateWindow(L"BUTTON", L"Start", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x, y, 80, 30, hwnd, (HMENU)ID_BTN_START, NULL, NULL);
        CreateWindow(L"BUTTON", L"Stop", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 90, y, 80, 30, hwnd, (HMENU)ID_BTN_STOP, NULL, NULL);
        
        y += 40;
        CreateWindow(L"STATIC", L"Effects:", WS_VISIBLE | WS_CHILD, x, y, 100, 20, hwnd, NULL, NULL, NULL);
        y += 30;
        CreateWindow(L"BUTTON", L"Gaussian", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x, y, 80, 30, hwnd, (HMENU)ID_BTN_GAUSSIAN, NULL, NULL);
        CreateWindow(L"BUTTON", L"Kawase", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 90, y, 80, 30, hwnd, (HMENU)ID_BTN_KAWASE, NULL, NULL);
        CreateWindow(L"BUTTON", L"Box", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 180, y, 80, 30, hwnd, (HMENU)ID_BTN_BOX, NULL, NULL);

        y += 40;
        CreateWindow(L"STATIC", L"Quality:", WS_VISIBLE | WS_CHILD, x, y, 100, 20, hwnd, NULL, NULL, NULL);
        y += 30;
        CreateWindow(L"BUTTON", L"High", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x, y, 100, 30, hwnd, (HMENU)ID_BTN_HIGH, NULL, NULL);
        CreateWindow(L"BUTTON", L"Balanced", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 110, y, 100, 30, hwnd, (HMENU)ID_BTN_BALANCED, NULL, NULL);
        CreateWindow(L"BUTTON", L"Performance", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 220, y, 100, 30, hwnd, (HMENU)ID_BTN_PERF, NULL, NULL);
        CreateWindow(L"BUTTON", L"Minimal", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 330, y, 100, 30, hwnd, (HMENU)ID_BTN_MINIMAL, NULL, NULL);

        y += 50;
        CreateWindow(L"STATIC", L"Status:", WS_VISIBLE | WS_CHILD, x, y, 100, 20, hwnd, NULL, NULL, NULL);
        g_hStatusText = CreateWindow(L"STATIC", L"Ready", WS_VISIBLE | WS_CHILD, x + 60, y, 300, 20, hwnd, NULL, NULL, NULL);

        y += 30;
        CreateWindow(L"STATIC", L"Logs:", WS_VISIBLE | WS_CHILD, x, y, 100, 20, hwnd, NULL, NULL, NULL);
        y += 25;
        g_hLogEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", 
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 
            x, y, 560, 250, hwnd, NULL, NULL, NULL);

        // Initialize library
        BlurSystemOptions opts;
        opts.enableLogging = true;
        if (!BlurSystem::Instance().Initialize(opts)) {
            AppendLog(L"Error: Failed to initialize BlurSystem");
        } else {
            AppendLog(L"BlurSystem initialized.");
        }
        return 0;
    }
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case ID_BTN_START:
            if (!g_blurWindow) {
                WindowOptions winOpts;
                winOpts.bounds = { 100, 100, 600, 500 };
                winOpts.topMost = true;
                g_blurWindow = BlurSystem::Instance().CreateBlurWindow(NULL, winOpts);
                if (g_blurWindow) {
                    ShowWindow(g_blurWindow->GetHWND(), SW_SHOW);
                    g_blurWindow->Start();
                    AppendLog(L"BlurWindow started.");
                    UpdateStatus(L"Running");
                } else {
                    AppendLog(L"Error: Failed to create BlurWindow.");
                }
            }
            break;
        case ID_BTN_STOP:
            if (g_blurWindow) {
                g_blurWindow->Stop();
                g_blurWindow.reset();
                AppendLog(L"BlurWindow stopped.");
                UpdateStatus(L"Stopped");
            }
            break;
        case ID_BTN_GAUSSIAN:
            if (g_blurWindow) {
                g_blurWindow->SetEffectPipeline("{\"pipeline\": [{\"type\": \"gaussian\"}]}");
                AppendLog(L"Effect: Gaussian");
            }
            break;
        case ID_BTN_KAWASE:
            if (g_blurWindow) {
                g_blurWindow->SetEffectPipeline("{\"pipeline\": [{\"type\": \"kawase\"}]}");
                AppendLog(L"Effect: Kawase");
            }
            break;
        case ID_BTN_BOX:
            if (g_blurWindow) {
                g_blurWindow->SetEffectPipeline("{\"pipeline\": [{\"type\": \"box\"}]}");
                AppendLog(L"Effect: Box");
            }
            break;
        case ID_BTN_HIGH:
            if (g_blurWindow) {
                g_blurWindow->SetPreset(QualityPreset::High);
                AppendLog(L"Preset: High");
            }
            break;
        case ID_BTN_BALANCED:
            if (g_blurWindow) {
                g_blurWindow->SetPreset(QualityPreset::Balanced);
                AppendLog(L"Preset: Balanced");
            }
            break;
        case ID_BTN_PERF:
            if (g_blurWindow) {
                g_blurWindow->SetPreset(QualityPreset::Performance);
                AppendLog(L"Preset: Performance");
            }
            break;
        case ID_BTN_MINIMAL:
            if (g_blurWindow) {
                g_blurWindow->SetPreset(QualityPreset::Minimal);
                AppendLog(L"Preset: Minimal");
            }
            break;
        }
        return 0;
    }
    case WM_TIMER: {
        if (g_blurWindow) {
            wchar_t buf[64];
            swprintf_s(buf, L"Running - FPS: %.1f", g_blurWindow->GetCurrentFPS());
            UpdateStatus(buf);
        }
        return 0;
    }
    case WM_DESTROY:
        if (g_blurWindow) g_blurWindow.reset();
        BlurSystem::Instance().Shutdown();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // Register the window class
    const wchar_t CLASS_NAME[] = L"BlurWindowGUIDemo";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    // Create the window
    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"BlurWindow Library GUI Demo",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 500,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) return 0;

    ShowWindow(hwnd, nCmdShow);
    SetTimer(hwnd, 1, 500, NULL); // Timer for FPS update

    // Run the message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
