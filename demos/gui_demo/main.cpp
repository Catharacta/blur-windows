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
#define ID_SLIDER_STRENGTH 1031
#define ID_BTN_COLOR    1032
#define ID_SLIDER_NOISE_INT   1033
#define ID_SLIDER_NOISE_SCALE 1034
#define ID_SLIDER_NOISE_SPEED 1035
#define ID_RADIO_NOISE_WHITE    1036
#define ID_RADIO_NOISE_SIN      1037
#define ID_RADIO_NOISE_GRID     1038
#define ID_RADIO_NOISE_PERLIN   1039
#define ID_RADIO_NOISE_SIMPLEX  1040
#define ID_COMBO_EFFECT 1041
#define ID_RADIO_NOISE_VORONOI  1042

HWND g_hComboEffect = NULL;
 
#define WM_APP_LOG      (WM_APP + 1)

// Helper to log message to the GUI
void AppendLog(const std::wstring& msg) {
    if (g_hLogEdit && IsWindow(g_hLogEdit)) {
        std::wstring* pMsg = new std::wstring(msg);
        if (!PostMessage(GetParent(g_hLogEdit), WM_APP_LOG, 0, (LPARAM)pMsg)) {
            delete pMsg;
        }
    }
}

void UpdateStatus(const std::wstring& status) {
    if (g_hStatusText) {
        SetWindowText(g_hStatusText, status.c_str());
    }
}

// Log callback for the library
void OnLibraryLog(const char* message) {
    // Convert to wstring and append
    int len = MultiByteToWideChar(CP_UTF8, 0, message, -1, NULL, 0);
    if (len > 0) {
        std::vector<wchar_t> buf(len);
        MultiByteToWideChar(CP_UTF8, 0, message, -1, buf.data(), len);
        // Prefix with [LIB] to distinguish
        std::wstring msg = L"[LIB] ";
        msg += buf.data();
        // The library already adds \n, but AppendLog adds \r\n
        // Remove trailing \n if present
        if (!msg.empty() && msg.back() == L'\n') msg.pop_back();
        AppendLog(msg);
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
        CreateWindow(L"STATIC", L"Effect:", WS_VISIBLE | WS_CHILD, x, y, 60, 20, hwnd, NULL, NULL, NULL);
        g_hComboEffect = CreateWindow(WC_COMBOBOX, L"",
            WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            x + 70, y, 150, 200, hwnd, (HMENU)ID_COMBO_EFFECT, NULL, NULL);
        SendMessage(g_hComboEffect, CB_ADDSTRING, 0, (LPARAM)L"Gaussian");
        SendMessage(g_hComboEffect, CB_ADDSTRING, 0, (LPARAM)L"Kawase");
        SendMessage(g_hComboEffect, CB_ADDSTRING, 0, (LPARAM)L"Box");
        SendMessage(g_hComboEffect, CB_ADDSTRING, 0, (LPARAM)L"Radial");
        SendMessage(g_hComboEffect, CB_SETCURSEL, 0, 0);

        y += 40;
        CreateWindow(L"STATIC", L"Strength (0-100):", WS_VISIBLE | WS_CHILD, x, y, 120, 20, hwnd, NULL, NULL, NULL);
        HWND hSlider = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS | TBS_HORZ, 
            x + 130, y, 300, 30, hwnd, (HMENU)ID_SLIDER_STRENGTH, NULL, NULL);
        SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessage(hSlider, TBM_SETPOS, TRUE, 100);

        y += 40;
        CreateWindow(L"STATIC", L"Noise Intensity:", WS_VISIBLE | WS_CHILD, x, y, 120, 20, hwnd, NULL, NULL, NULL);
        HWND hNoiseInt = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS | TBS_HORZ, 
            x + 130, y, 300, 30, hwnd, (HMENU)ID_SLIDER_NOISE_INT, NULL, NULL);
        SendMessage(hNoiseInt, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessage(hNoiseInt, TBM_SETPOS, TRUE, 0);

        y += 40;
        CreateWindow(L"STATIC", L"Noise Scale:", WS_VISIBLE | WS_CHILD, x, y, 120, 20, hwnd, NULL, NULL, NULL);
        HWND hNoiseScale = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS | TBS_HORZ, 
            x + 130, y, 300, 30, hwnd, (HMENU)ID_SLIDER_NOISE_SCALE, NULL, NULL);
        SendMessage(hNoiseScale, TBM_SETRANGE, TRUE, MAKELONG(1, 1000));
        SendMessage(hNoiseScale, TBM_SETPOS, TRUE, 100);

        y += 40;
        CreateWindow(L"STATIC", L"Noise Speed:", WS_VISIBLE | WS_CHILD, x, y, 120, 20, hwnd, NULL, NULL, NULL);
        HWND hNoiseSpeed = CreateWindow(TRACKBAR_CLASS, L"", WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS | TBS_HORZ, 
            x + 130, y, 300, 30, hwnd, (HMENU)ID_SLIDER_NOISE_SPEED, NULL, NULL);
        SendMessage(hNoiseSpeed, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessage(hNoiseSpeed, TBM_SETPOS, TRUE, 10);

        y += 40;
        CreateWindow(L"STATIC", L"Noise Type:", WS_VISIBLE | WS_CHILD, x, y, 100, 20, hwnd, NULL, NULL, NULL);
        y += 25;
        CreateWindow(L"BUTTON", L"White", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP, x, y, 70, 20, hwnd, (HMENU)ID_RADIO_NOISE_WHITE, NULL, NULL);
        CreateWindow(L"BUTTON", L"Sin", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, x + 80, y, 60, 20, hwnd, (HMENU)ID_RADIO_NOISE_SIN, NULL, NULL);
        CreateWindow(L"BUTTON", L"Grid", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, x + 150, y, 60, 20, hwnd, (HMENU)ID_RADIO_NOISE_GRID, NULL, NULL);
        CreateWindow(L"BUTTON", L"Perlin", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, x + 240, y, 70, 20, hwnd, (HMENU)ID_RADIO_NOISE_PERLIN, NULL, NULL);
        CreateWindow(L"BUTTON", L"Simplex", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, x + 310, y, 70, 20, hwnd, (HMENU)ID_RADIO_NOISE_SIMPLEX, NULL, NULL);
        CreateWindow(L"BUTTON", L"Voronoi", WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, x + 380, y, 70, 20, hwnd, (HMENU)ID_RADIO_NOISE_VORONOI, NULL, NULL);
        CheckRadioButton(hwnd, ID_RADIO_NOISE_WHITE, ID_RADIO_NOISE_VORONOI, ID_RADIO_NOISE_WHITE);

        y += 30;
        CreateWindow(L"STATIC", L"Status:", WS_VISIBLE | WS_CHILD, x, y, 60, 20, hwnd, NULL, NULL, NULL);
        g_hStatusText = CreateWindow(L"STATIC", L"Ready", WS_VISIBLE | WS_CHILD, x + 70, y, 300, 20, hwnd, NULL, NULL, NULL);

        y += 25;
        CreateWindow(L"STATIC", L"Logs:", WS_VISIBLE | WS_CHILD, x, y, 100, 20, hwnd, NULL, NULL, NULL);
        y += 25;
        g_hLogEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", 
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 
            x, y, 560, 150, hwnd, NULL, NULL, NULL);

        // Initialize library with callback
        BlurSystemOptions opts;
        opts.enableLogging = true;
        opts.logCallback = OnLibraryLog;
        if (!BlurSystem::Instance().Initialize(opts)) {
            AppendLog(L"Error: Failed to initialize BlurSystem");
        } else {
            AppendLog(L"BlurSystem initialized.");
        }
        return 0;
    }
    case WM_APP_LOG: {
        std::unique_ptr<std::wstring> pMsg((std::wstring*)lParam);
        if (g_hLogEdit && pMsg) {
            int len = GetWindowTextLength(g_hLogEdit);
            SendMessage(g_hLogEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
            SendMessage(g_hLogEdit, EM_REPLACESEL, 0, (LPARAM)(*pMsg + L"\r\n").c_str());
            SendMessage(g_hLogEdit, EM_SCROLLCARET, 0, 0);
        }
        return 0;
    }
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        switch (wmId) {
        case ID_BTN_START:
            if (!g_blurWindow) {
                WindowOptions winOpts;
                winOpts.bounds = { 620, 100, 1120, 500 };
                winOpts.topMost = true;
                g_blurWindow = BlurSystem::Instance().CreateBlurWindow(NULL, winOpts);
                if (g_blurWindow) {
                    AppendLog(L"BlurWindow created. Starting graphics...");
                    g_blurWindow->Start();
                    
                    if (g_blurWindow->IsInitialized()) {
                        ShowWindow(g_blurWindow->GetHWND(), SW_SHOW);
                        AppendLog(L"BlurWindow started (Gaussian effect).");
                        UpdateStatus(L"Running");
                    } else {
                        AppendLog(L"Error: Graphics initialization failed in Start().");
                        AppendLog(L"See [LIB] logs for details.");
                        UpdateStatus(L"Init Failed");
                    }
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
        case ID_RADIO_NOISE_WHITE:
        case ID_RADIO_NOISE_SIN:
        case ID_RADIO_NOISE_PERLIN:
        case ID_RADIO_NOISE_SIMPLEX:
        case ID_RADIO_NOISE_VORONOI:
            if (g_blurWindow) {
                int type = wmId - ID_RADIO_NOISE_WHITE;
                g_blurWindow->SetNoiseType(type);
                AppendLog(L"Noise type changed.");
            }
            break;
        }
        // Handle ComboBox selection change
        if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == ID_COMBO_EFFECT) {
            if (g_blurWindow) {
                int sel = (int)SendMessage(g_hComboEffect, CB_GETCURSEL, 0, 0);
                const char* configs[] = {
                    "{\"pipeline\": [{\"type\": \"gaussian\"}]}",
                    "{\"pipeline\": [{\"type\": \"kawase\"}]}",
                    "{\"pipeline\": [{\"type\": \"box\"}]}",
                    "{\"pipeline\": [{\"type\": \"radial\"}]}"
                };
                const wchar_t* names[] = { L"Gaussian", L"Kawase", L"Box", L"Radial" };
                if (sel >= 0 && sel < 4) {
                    if (g_blurWindow->SetEffectPipeline(configs[sel])) {
                        AppendLog(std::wstring(L"Effect changed to ") + names[sel]);
                    } else {
                        AppendLog(std::wstring(L"Error: Failed to set ") + names[sel] + L" effect");
                    }
                }
            }
        }
        return 0;
    }
    case WM_HSCROLL: {
        if (g_blurWindow && (HWND)lParam == GetDlgItem(hwnd, ID_SLIDER_STRENGTH)) {
            int pos = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
            g_blurWindow->SetBlurStrength(pos / 100.0f);
            wchar_t buf[64];
            swprintf_s(buf, L"Strength: %d%% (0=transparent, 100=full blur)", pos);
            AppendLog(buf);
        } else if (g_blurWindow && (HWND)lParam == GetDlgItem(hwnd, ID_SLIDER_NOISE_INT)) {
            int pos = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
            g_blurWindow->SetNoiseIntensity(pos / 100.0f);
        } else if (g_blurWindow && (HWND)lParam == GetDlgItem(hwnd, ID_SLIDER_NOISE_SCALE)) {
            int pos = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
            g_blurWindow->SetNoiseScale((float)pos);
        } else if (g_blurWindow && (HWND)lParam == GetDlgItem(hwnd, ID_SLIDER_NOISE_SPEED)) {
            int pos = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
            g_blurWindow->SetNoiseSpeed(pos / 10.0f);
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
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 680,
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
