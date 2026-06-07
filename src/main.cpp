#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <cstdio>
#include <d2d1.h>

#include "editor/editor.h"
#include "renderer/d2d_renderer.h"

static const wchar_t* WINDOW_CLASS   = L"GameMakerCPlusWnd";
static const wchar_t* WINDOW_TITLE   = L"GameMaker C++ - Warcraft风格 瓦片地图编辑器";
static const int      CLIENT_W       = 1280;
static const int      CLIENT_H       = 800;
static const int      TARGET_FPS     = 60;

static HWND          g_hWnd     = nullptr;
static HINSTANCE     g_hInst    = nullptr;
static Editor*       g_pEditor  = nullptr;
static D2DRenderer*  g_pD2D     = nullptr;

static ULONG_PTR g_gdiplusToken = 0;

// Performance timer
static LARGE_INTEGER g_qpcFreq   = {};
static LARGE_INTEGER g_lastTick  = {};
static double        g_accumSec  = 0.0;

static void Timer_Init() {
    QueryPerformanceFrequency(&g_qpcFreq);
    QueryPerformanceCounter(&g_lastTick);
}

static double Timer_DeltaSeconds() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double dt = static_cast<double>(now.QuadPart - g_lastTick.QuadPart)
              / static_cast<double>(g_qpcFreq.QuadPart);
    g_lastTick = now;
    return dt;
}

// Window procedure
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg,
                                WPARAM wParam, LPARAM lParam) {
    Editor* p = nullptr;
    if (msg != WM_CREATE)
        p = reinterpret_cast<Editor*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        p = static_cast<Editor*>(cs->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
        if (p) p->init(hWnd);
        return 0;
    }

    case WM_SIZE: {
        if (p) {
            p->windowW = LOWORD(lParam);
            p->windowH = HIWORD(lParam);
        }
        if (g_pD2D) {
            g_pD2D->resize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    }

    case WM_PAINT: {
        // Validate the window - actual rendering happens in the main loop
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_DISPLAYCHANGE: {
        if (g_pD2D) {
            g_pD2D->resize(p ? p->windowW : CLIENT_W,
                           p ? p->windowH : CLIENT_H);
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_KEYDOWN:
    case WM_KEYUP: {
        if (p) p->handleInput(msg, wParam, lParam);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static BOOL RegisterWindowClass(HINSTANCE hInst) {
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground  = nullptr;
    wc.lpszClassName  = WINDOW_CLASS;
    return RegisterClassExW(&wc);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nShow) {
    g_hInst = hInstance;

    // Initialize COM (needed for WIC)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Initialize GDI+ (still needed for image loading from DB)
    Gdiplus::GdiplusStartupInput gdipInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdipInput, nullptr);

    if (!RegisterWindowClass(hInstance)) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        CoUninitialize();
        return 1;
    }

    RECT rc = { 0, 0, CLIENT_W, CLIENT_H };
    DWORD dwStyle   = WS_OVERLAPPEDWINDOW;
    DWORD dwExStyle = 0;
    AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);

    g_pEditor = new Editor();

    g_hWnd = CreateWindowExW(
        dwExStyle, WINDOW_CLASS, WINDOW_TITLE, dwStyle,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, g_pEditor);

    if (!g_hWnd) {
        delete g_pEditor;
        g_pEditor = nullptr;
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        CoUninitialize();
        return 1;
    }

    // Initialize Direct2D renderer
    g_pD2D = new D2DRenderer();
    if (!g_pD2D->init(g_hWnd)) {
        printf("Failed to init D2D, falling back to GDI+\n");
        delete g_pD2D;
        g_pD2D = nullptr;
    }

    // Pass D2D renderer to editor
    if (g_pEditor && g_pD2D) {
        g_pEditor->setD2DRenderer(g_pD2D);
    }

    ShowWindow(g_hWnd, nShow);
    UpdateWindow(g_hWnd);

    // Main loop (60fps target)
    Timer_Init();
    const double targetDt = 1.0 / static_cast<double>(TARGET_FPS);

    MSG msg = {};
    bool running = true;

    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running) break;

        double elapsed = Timer_DeltaSeconds();
        g_accumSec += elapsed;

        if (g_accumSec >= targetDt) {
            g_accumSec -= targetDt;
            if (g_accumSec > targetDt) g_accumSec = 0.0;

            // Render with Direct2D
            if (g_pD2D && g_pD2D->beginDraw()) {
                g_pD2D->clear(0.078f, 0.078f, 0.094f);

                if (g_pEditor)
                    g_pEditor->renderD2D(g_pD2D);

                g_pD2D->endDraw();
            } else {
                // Fallback: GDI+ render
                HDC hdc = GetDC(g_hWnd);
                if (hdc && g_pEditor) {
                    RECT wrc;
                    GetClientRect(g_hWnd, &wrc);
                    // Minimal GDI+ fallback
                    Gdiplus::Graphics g(hdc);
                    g_pEditor->render(&g);
                }
                if (hdc) ReleaseDC(g_hWnd, hdc);
            }
        } else {
            Sleep(1);
        }
    }

    delete g_pD2D;
    g_pD2D = nullptr;

    delete g_pEditor;
    g_pEditor = nullptr;

    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    CoUninitialize();

    return static_cast<int>(msg.wParam);
}
