#include "editor.h"
#include "renderer/d2d_renderer.h"
#include "tile_generator.h"
#include "api/api_pollinations.h"
#include "api/api_bgremove.h"
#include "api/api_screenshot.h"
#include "util/image_utils.h"
#include "map_serializer.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windowsx.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dwrite.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline float toF(int v) { return v / 255.0f; }

static const wchar_t* toolNames[] = {
    L"\u7b14\u5237",   // BRUSH
    L"\u6a61\u76ae",   // ERASER
    L"\u9009\u62e9",   // SELECT
    L"\u5e73\u79fb",   // PAN
    L"\u586b\u5145"    // FILL
};

static const int NUM_MENU_BUTTONS = 7;
static const wchar_t* menuLabels[] = {
    L"\u65b0\u5efa",   // New
    L"\u4fdd\u5b58",   // Save
    L"\u8bfb\u53d6",   // Load
    L"\u751f\u6210",   // Generate
    L"\u64a4\u9500",   // Undo
    L"\u91cd\u505a",   // Redo
    L"\u25b6 \u8fd0\u884c"   // ▶ Run
};

// ---------------------------------------------------------------------------
// New Map Dialog (Win32 custom dialog with presets + manual input)
// ---------------------------------------------------------------------------
struct NewMapDialog {
    struct Preset { const wchar_t* label; int w; int h; };
    static const int PRESET_COUNT = 5;
    static const int BTN_ID_BASE = 200;

    HWND hWidth = nullptr;
    HWND hHeight = nullptr;
    bool confirmed = false;
    int resultW = 64;
    int resultH = 64;
    HFONT hFont = nullptr;
    HBRUSH hbrBg = nullptr;
    HBRUSH hbrBtn = nullptr;
    HBRUSH hbrBtnHot = nullptr;
    HBRUSH hbrEdit = nullptr;
    HPEN hpenBorder = nullptr;

    static Preset presets[PRESET_COUNT];

    static bool show(HWND owner, int& outW, int& outH) {
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = DlgProc;
            wc.hInstance = (HINSTANCE)GetWindowLongPtr(owner, GWLP_HINSTANCE);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = L"NewMapDlgWnd";
            RegisterClassExW(&wc);
            registered = true;
        }

        NewMapDialog dlg;
        dlg.hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
        dlg.hbrBg       = CreateSolidBrush(RGB(0, 0, 0));
        dlg.hbrBtn      = CreateSolidBrush(RGB(255, 255, 255));
        dlg.hbrBtnHot   = CreateSolidBrush(RGB(180, 180, 180));
        dlg.hbrEdit     = CreateSolidBrush(RGB(30, 30, 30));
        dlg.hpenBorder  = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));

        RECT orc;
        GetWindowRect(owner, &orc);
        int cx = (orc.left + orc.right) / 2 - 170;
        int cy = (orc.top + orc.bottom) / 2 - 135;

        HWND hDlg = CreateWindowExW(
            0, L"NewMapDlgWnd", L"\u65b0\u5efa\u5730\u56fe",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            cx, cy, 340, 280,
            owner, nullptr,
            (HINSTANCE)GetWindowLongPtr(owner, GWLP_HINSTANCE),
            &dlg);

        if (!hDlg) {
            DeleteObject(dlg.hFont); DeleteObject(dlg.hbrBg);
            DeleteObject(dlg.hbrBtn); DeleteObject(dlg.hbrBtnHot);
            DeleteObject(dlg.hbrEdit); DeleteObject(dlg.hpenBorder);
            return false;
        }

        EnableWindow(owner, FALSE);
        ShowWindow(hDlg, SW_SHOW);
        UpdateWindow(hDlg);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && msg.hwnd == hDlg) {
                DestroyWindow(hDlg);
            }
            if (!IsWindow(hDlg)) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
        DeleteObject(dlg.hFont);
        DeleteObject(dlg.hbrBg);
        DeleteObject(dlg.hbrBtn);
        DeleteObject(dlg.hbrBtnHot);
        DeleteObject(dlg.hbrEdit);
        DeleteObject(dlg.hpenBorder);

        if (dlg.confirmed) {
            outW = dlg.resultW;
            outH = dlg.resultH;
            return true;
        }
        return false;
    }

    static void DrawDarkButton(HWND hWnd, HDC hdc, RECT* rc, bool hot, bool pressed) {
        HBRUSH br = hot ? CreateSolidBrush(RGB(180, 180, 180)) : CreateSolidBrush(RGB(255, 255, 255));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
        RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, 4, 4);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBr);
        DeleteObject(br);
        DeleteObject(pen);
    }

    static LRESULT CALLBACK DlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        NewMapDialog* pd = nullptr;
        if (msg == WM_NCCREATE) {
            CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
            pd = (NewMapDialog*)cs->lpCreateParams;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pd);
        } else {
            pd = (NewMapDialog*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        }

        switch (msg) {
        case WM_CREATE: {
            HFONT hf = pd->hFont;
            int y = 16;

            auto mkLabel = [&](int x, int yy, int w, const wchar_t* text) {
                HWND h = CreateWindowW(L"STATIC", text,
                    WS_CHILD | WS_VISIBLE, x, yy, w, 20, hWnd, nullptr, nullptr, nullptr);
                SendMessageW(h, WM_SETFONT, (WPARAM)hf, TRUE);
            };
            auto mkBtn = [&](int x, int yy, int w, int bh, const wchar_t* text, int id) {
                HWND hb = CreateWindowW(L"BUTTON", text,
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
                    x, yy, w, bh, hWnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
                SendMessageW(hb, WM_SETFONT, (WPARAM)hf, TRUE);
                return hb;
            };
            auto mkEdit = [&](int x, int yy, int w, const wchar_t* def, int id) {
                HWND he = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", def,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
                    x, yy, w, 24, hWnd, (HMENU)(INT_PTR)id, nullptr, nullptr);
                SendMessageW(he, WM_SETFONT, (WPARAM)hf, TRUE);
                return he;
            };

            mkLabel(16, y, 300, L"\u9009\u62e9\u9884\u8bbe\u5c3a\u5bf8\uff1a");
            y += 26;

            for (int i = 0; i < PRESET_COUNT; i++) {
                int col = i % 3;
                int row = i / 3;
                mkBtn(16 + col * 106, y + row * 36, 100, 30, presets[i].label, BTN_ID_BASE + i);
            }
            y += 80;

            mkLabel(16, y, 200, L"\u81ea\u5b9a\u4e49\u5c3a\u5bf8\uff1a");
            y += 24;
            mkLabel(16, y + 2, 26, L"\u5bbd:");
            pd->hWidth = mkEdit(44, y, 80, L"64", 1000);
            mkLabel(140, y + 2, 26, L"\u9ad8:");
            pd->hHeight = mkEdit(168, y, 80, L"64", 1001);
            y += 40;

            mkBtn(160, y, 80, 30, L"\u786e\u5b9a", IDOK);
            mkBtn(250, y, 80, 30, L"\u53d6\u6d88", IDCANCEL);
            return 0;
        }

        case WM_ERASEBKGND: {
            {
                HDC hdc = (HDC)wParam;
                RECT rc;
                GetClientRect(hWnd, &rc);
                HBRUSH br = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(hdc, &rc, br);
                DeleteObject(br);
            }
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            HBRUSH br = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hdc, &rc, br);
            DeleteObject(br);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_CTLCOLORDLG: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(0, 0, 0));
            return (LRESULT)pd->hbrBg;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(0, 0, 0));
            return (LRESULT)pd->hbrBg;
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(30, 30, 30));
            return (LRESULT)pd->hbrEdit;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlType == ODT_BUTTON) {
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                bool hot = (dis->itemState & ODS_SELECTED) != 0;
                bool focused = (dis->itemState & ODS_FOCUS) != 0;

                HBRUSH br;
                if (hot)
                    br = CreateSolidBrush(RGB(100, 100, 100));
                else
                    br = CreateSolidBrush(RGB(255, 255, 255));

                HPEN pen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
                HPEN oldPen = (HPEN)SelectObject(hdc, pen);
                HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
                RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBr);
                DeleteObject(br);
                DeleteObject(pen);

                // Text
                wchar_t text[64] = {};
                GetWindowTextW(dis->hwndItem, text, 64);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(0, 0, 0));
                SelectObject(hdc, pd->hFont);
                DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                if (focused) {
                    HPEN fp = CreatePen(PS_DOT, 1, RGB(100, 100, 100));
                    HPEN op = (HPEN)SelectObject(hdc, fp);
                    SelectObject(hdc, GetStockBrush(NULL_BRUSH));
                    InflateRect(&rc, -3, -3);
                    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 3, 3);
                    SelectObject(hdc, op);
                    DeleteObject(fp);
                }
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id >= BTN_ID_BASE && id < BTN_ID_BASE + PRESET_COUNT) {
                pd->resultW = presets[id - BTN_ID_BASE].w;
                pd->resultH = presets[id - BTN_ID_BASE].h;
                pd->confirmed = true;
                DestroyWindow(hWnd);
                return 0;
            }
            if (id == IDOK) {
                wchar_t wB[16] = {}, hB[16] = {};
                GetWindowTextW(pd->hWidth, wB, 16);
                GetWindowTextW(pd->hHeight, hB, 16);
                int w2 = _wtoi(wB);
                int h2 = _wtoi(hB);
                if (w2 >= 2 && w2 <= 1024 && h2 >= 2 && h2 <= 1024) {
                    pd->resultW = w2;
                    pd->resultH = h2;
                    pd->confirmed = true;
                    DestroyWindow(hWnd);
                } else {
                    MessageBoxW(hWnd, L"\u5c3a\u5bf8\u8303\u56f4\uff1a2 ~ 1024",
                                L"\u9519\u8bef", MB_ICONWARNING);
                }
                return 0;
            }
            if (id == IDCANCEL) {
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        }
        case WM_DESTROY:
            // WM_QUIT removed - modal loop handles exit via !IsWindow check
            return 0;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

NewMapDialog::Preset NewMapDialog::presets[] = {
    { L"32x32  \u5c0f",       32,   32  },
    { L"64x64  \u4e2d",       64,   64  },
    { L"128x128 \u5927",     128,  128  },
    { L"64x32  \u5bbd\u5c4f",  64,   32  },
    { L"128x64 \u8d85\u5bbd", 128,   64  },
};

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

Editor::Editor()
    : map(nullptr)
    , layers(nullptr)
    , layerPanel(nullptr)
    , assetDb(nullptr)
    , d2d(nullptr)
    , pollinations(nullptr)
    , bgremove(nullptr)
    , screenshot(nullptr)
    , serializer(nullptr)
    , currentTool(BRUSH)
    , windowW(1280)
    , windowH(800)
    , selectedTileId(1)
    , brushSize(1)
    , offscreenBuffer(nullptr)
    , tileAtlas(nullptr)
    , fontSmall(nullptr)
    , fontMedium(nullptr)
    , fontLarge(nullptr)
    , brushBg(nullptr)
    , brushFg(nullptr)
    , brushAccent(nullptr)
    , brushPanel(nullptr)
    , brushMenu(nullptr)
    , brushStatus(nullptr)
    , penGrid(nullptr)
    , penSelection(nullptr)
    , penBorder(nullptr)
{
    memset(&gui, 0, sizeof(gui));
    gui.showGrid = true;
    gui.showObjects = true;
    gui.showUnits = true;
    gui.hoveredTile = -1;
    gui.statusMessage = 0;
    memset(gui.statusText, 0, sizeof(gui.statusText));
    gui.menuAnimProgress = 1.0f;
    gui.panelAnimProgress = 1.0f;

    camera.x = 0.0f;
    camera.y = 0.0f;
    camera.zoom = 1.0f;

    currentFilePath[0] = '\0';
}

Editor::~Editor() {
    destroyResources();

    delete pollinations;  pollinations  = nullptr;
    delete bgremove;      bgremove      = nullptr;
    delete screenshot;    screenshot    = nullptr;
    delete serializer;    serializer    = nullptr;
    delete assetDb;       assetDb       = nullptr;
    delete layers;        layers        = nullptr;
    delete layerPanel;    layerPanel    = nullptr;
    delete map;           map           = nullptr;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void Editor::init(HWND hWnd) {
    hwnd = hWnd;

    map = new TileMap();
    map->init();

    // Center camera on player character
    float camCX = (float)(map->width * TileMap::TILE_SIZE) / 2.0f;
    float camCY = (float)(map->height * TileMap::TILE_SIZE) / 2.0f;
    for (auto& c : map->characters) {
        if (c.type == L"player") {
            camCX = c.worldX;
            camCY = c.worldY;
            break;
        }
    }
    camera.x = camCX - (float)windowW / 2.0f;
    camera.y = camCY - (float)(windowH - STATUS_BAR_H) / 2.0f;

    layers = new LayerManager();
    layerPanel = new LayerPanel();

    {
        wchar_t exePathBuf[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
        exeDir = exePathBuf;
        exeDir = exeDir.substr(0, exeDir.find_last_of(L'\\'));
    }

    assetDb = new AssetDatabase();
    {
        std::string dbPath(exeDir.begin(), exeDir.end());
        dbPath += "\\editor.db";
        assetDb->open(dbPath.c_str());
        assetDb->initSchema();
        assetDb->setSetting("project_root", std::string(exeDir.begin(), exeDir.end()).c_str());
    }

    pollinations = new PollinationsAPI();
    bgremove     = new BgRemoveAPI();
    screenshot   = new ScreenshotAPI();
    serializer   = nullptr;

    {
        const wchar_t* assetsSubdirs[] = {
            L"\\assets\\tiles\\",
            L"\\..\\assets\\tiles\\",
            L"\\..\\..\\assets\\tiles\\"
        };
        for (auto& sub : assetsSubdirs) {
            std::wstring testPath = exeDir + sub + L"grass.png";
            if (GetFileAttributesW(testPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                assetsDir = exeDir + sub;
                break;
            }
        }

        struct TileDef { const char* file; const char* name; const char* cat; };
        TileDef defs[] = {
            {"grass", "\u8349\u5730", "terrain"},
            {"sand",  "\u6c99\u5730", "terrain"},
            {"water", "\u6c34\u57df", "terrain"},
            {"stone", "\u5ca9\u77f3", "terrain"},
            {"dirt",  "\u6ce5\u571f", "terrain"},
            {"lava",  "\u5ca9\u6d46", "terrain"},
            {"snow",  "\u96ea\u5730", "terrain"},
            {"tree",  "\u6811\u6728", "object"}
        };

        for (int i = 0; i < 8; i++) {
            if (assetsDir.empty()) break;

            wchar_t pngPath[MAX_PATH] = {};
            wchar_t tileFileW[64] = {};
            MultiByteToWideChar(CP_UTF8, 0, defs[i].file, -1, tileFileW, 64);
            wcscpy_s(pngPath, assetsDir.c_str());
            wcscat_s(pngPath, tileFileW);
            wcscat_s(pngPath, L".png");

            if (GetFileAttributesW(pngPath) == INVALID_FILE_ATTRIBUTES) continue;

            char pathUtf8[MAX_PATH] = {};
            WideCharToMultiByte(CP_UTF8, 0, pngPath, -1, pathUtf8, MAX_PATH, nullptr, nullptr);

            WIN32_FILE_ATTRIBUTE_DATA fad;
            int64_t diskMtime = 0;
            if (GetFileAttributesExW(pngPath, GetFileExInfoStandard, &fad)) {
                LARGE_INTEGER li;
                li.HighPart = fad.ftLastWriteTime.dwHighDateTime;
                li.LowPart = fad.ftLastWriteTime.dwLowDateTime;
                diskMtime = li.QuadPart;
            }

            TileRecord rec;
            if (assetDb->getTileByPath(pathUtf8, rec)) {
                if (rec.fileMtime != diskMtime) {
                    assetDb->updateTileFromFile(pathUtf8, defs[i].name, defs[i].cat);
                }
            } else {
                assetDb->importTileFromFile(pathUtf8, defs[i].name, defs[i].cat);
            }
        }
    }

    initTileTextures();

    createResources();

    strncpy_s(gui.statusText, "\u5c31\u7eea", sizeof(gui.statusText));
}

void Editor::setD2DRenderer(D2DRenderer* renderer) {
    d2d = renderer;
    if (d2d && map) {
        map->convertTexturesToD2D(d2d);
        map->convertCharacterSpritesToD2D(d2d);
    }
}

// ---------------------------------------------------------------------------
// GDI+ Resource Management
// ---------------------------------------------------------------------------

void Editor::createResources() {
    fontSmall  = new Gdiplus::Font(L"Microsoft YaHei", 9.0f);
    fontMedium = new Gdiplus::Font(L"Microsoft YaHei", 11.0f);
    fontLarge  = new Gdiplus::Font(L"Microsoft YaHei", 14.0f, Gdiplus::FontStyleBold);

    brushBg     = new Gdiplus::SolidBrush(Gdiplus::Color(45, 45, 48));
    brushFg     = new Gdiplus::SolidBrush(Gdiplus::Color(255, 255, 255));
    brushAccent = new Gdiplus::SolidBrush(Gdiplus::Color(0, 120, 212));
    brushPanel  = new Gdiplus::SolidBrush(Gdiplus::Color(30, 30, 30));
    brushMenu   = new Gdiplus::SolidBrush(Gdiplus::Color(37, 37, 38));
    brushStatus = new Gdiplus::SolidBrush(Gdiplus::Color(0, 120, 212));

    penGrid      = new Gdiplus::Pen(Gdiplus::Color(80, 180, 180, 180), 1.0f);
    penSelection = new Gdiplus::Pen(Gdiplus::Color(0, 120, 212), 2.0f);
    penBorder    = new Gdiplus::Pen(Gdiplus::Color(85, 85, 85), 1.0f);
}

void Editor::destroyResources() {
    delete fontSmall;    fontSmall    = nullptr;
    delete fontMedium;   fontMedium   = nullptr;
    delete fontLarge;    fontLarge    = nullptr;
    delete brushBg;      brushBg      = nullptr;
    delete brushFg;      brushFg      = nullptr;
    delete brushAccent;  brushAccent  = nullptr;
    delete brushPanel;   brushPanel   = nullptr;
    delete brushMenu;    brushMenu    = nullptr;
    delete brushStatus;  brushStatus  = nullptr;
    delete penGrid;      penGrid      = nullptr;
    delete penSelection; penSelection = nullptr;
    delete penBorder;    penBorder    = nullptr;
    delete offscreenBuffer; offscreenBuffer = nullptr;
    delete tileAtlas;    tileAtlas    = nullptr;
}

// ---------------------------------------------------------------------------
// Canvas dimensions
// ---------------------------------------------------------------------------

int Editor::getCanvasWidth() const {
    return windowW;
}

int Editor::getCanvasHeight() const {
    return windowH - STATUS_BAR_H;
}

int Editor::tileCount() const {
    return map ? (int)map->tileDefs.size() : 0;
}

// ---------------------------------------------------------------------------
// GDI+ Rendering
// ---------------------------------------------------------------------------

void Editor::render(Gdiplus::Graphics* g) {
    if (!g) return;

    // Left panel background
    g->FillRectangle(brushPanel, 0, 0, PANEL_WIDTH, windowH);

    // Top bar background
    g->FillRectangle(brushMenu, PANEL_WIDTH, 0, getCanvasWidth(), TOP_BAR_H);

    // Status bar background
    g->FillRectangle(brushMenu, PANEL_WIDTH, windowH - STATUS_BAR_H,
                     getCanvasWidth(), STATUS_BAR_H);

    renderToolPalette(g);
    renderMenuBar(g);
    renderCanvas(g);
    renderStatusBar(g);
    renderMinimap(g);
    renderTooltip(g);
}

void Editor::renderMenuBar(Gdiplus::Graphics* g) {
    for (int i = 0; i < NUM_MENU_BUTTONS; i++) {
        int bx = MENU_BTN_X + i * (MENU_BTN_W + MENU_BTN_GAP);
        int by = MENU_BTN_Y;
        bool hovered = gui.menuHovered &&
                       (lastMouseX >= bx && lastMouseX < bx + MENU_BTN_W &&
                        lastMouseY >= by && lastMouseY < by + MENU_BTN_H);
        drawButton(g, bx, by, MENU_BTN_W, MENU_BTN_H,
                   menuLabels[i], hovered);
    }
}

void Editor::renderToolPalette(Gdiplus::Graphics* g) {
    // Section: Tools
    {
        int toolY = 8;
        for (int i = 0; i < 5; i++) {
            int tx = 8 + i * (TOOL_ICON_SIZE + TOOL_ICON_GAP);
            bool hovered = gui.panelHovered &&
                           (lastMouseX >= tx && lastMouseX < tx + TOOL_ICON_SIZE &&
                            lastMouseY >= toolY && lastMouseY < toolY + TOOL_ICON_SIZE);
            bool selected = (currentTool == i);

            Gdiplus::SolidBrush* bgBrush = selected ? brushAccent :
                (hovered ? new Gdiplus::SolidBrush(Gdiplus::Color(80, 80, 84)) :
                 new Gdiplus::SolidBrush(Gdiplus::Color(62, 62, 66)));
            g->FillRectangle(bgBrush, tx, toolY, TOOL_ICON_SIZE, TOOL_ICON_SIZE);
            g->DrawRectangle(penBorder, tx, toolY, TOOL_ICON_SIZE, TOOL_ICON_SIZE);

            // Draw tool label
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g->DrawString(toolNames[i], -1, fontSmall,
                          Gdiplus::RectF((float)tx, (float)toolY,
                                         (float)TOOL_ICON_SIZE, (float)TOOL_ICON_SIZE),
                          &sf, brushFg);

            if (!selected && !hovered) delete bgBrush;
            else if (!selected) delete bgBrush;
        }
    }

    // Section: Tiles
    {
        // Label
        g->DrawString(L"\u74e6\u7247", -1, fontMedium,
                      Gdiplus::PointF(8.0f, (float)(TOP_BAR_H + 44)), brushFg);

        int numTiles = tileCount();
        for (int i = 1; i < numTiles; i++) {
            int idx = i - 1 - tileScrollOffset;
            if (idx < 0) continue;
            int by = TILE_START_Y + idx * (TILE_BTN_H + TILE_BTN_GAP);
            if (by + TILE_BTN_H < TILE_START_Y || by > windowH) continue;
            bool hovered = gui.panelHovered &&
                           (lastMouseX >= TILE_BTN_X && lastMouseX < TILE_BTN_X + TILE_BTN_W &&
                            lastMouseY >= by && lastMouseY < by + TILE_BTN_H);
            bool selected = (selectedTileId == i);

            Gdiplus::SolidBrush* bgBrush = selected ? brushAccent :
                (hovered ? new Gdiplus::SolidBrush(Gdiplus::Color(80, 80, 84)) :
                 new Gdiplus::SolidBrush(Gdiplus::Color(62, 62, 66)));
            g->FillRectangle(bgBrush, TILE_BTN_X, by, TILE_BTN_W, TILE_BTN_H);
            g->DrawRectangle(penBorder, TILE_BTN_X, by, TILE_BTN_W, TILE_BTN_H);

            // Color swatch
            DWORD c = map->tileDefs[i].color;
            Gdiplus::SolidBrush swatch(Gdiplus::Color{c});
            g->FillRectangle(&swatch, TILE_BTN_X + 4, by + 4, 36, TILE_BTN_H - 8);

            // Name
            g->DrawString(map->tileDefs[i].name.c_str(), -1, fontSmall,
                          Gdiplus::PointF((float)(TILE_BTN_X + 44), (float)(by + 12)),
                          brushFg);

            if (!selected) delete bgBrush;
        }
    }

    // Add/Delete tile buttons
    {
        int numTiles = tileCount();
        int visibleTiles = numTiles - 1 - tileScrollOffset;
        int addBtnY = TILE_START_Y + visibleTiles * (TILE_BTN_H + TILE_BTN_GAP) + 4;

        // "+ 添加瓦片" button
        {
            bool hovered = gui.panelHovered &&
                           (lastMouseX >= TILE_BTN_X && lastMouseX < TILE_BTN_X + TILE_BTN_W &&
                            lastMouseY >= addBtnY && lastMouseY < addBtnY + 28);
            Gdiplus::SolidBrush* bgBrush = hovered ?
                new Gdiplus::SolidBrush(Gdiplus::Color(0, 100, 180)) :
                new Gdiplus::SolidBrush(Gdiplus::Color(0, 80, 150));
            g->FillRectangle(bgBrush, TILE_BTN_X, addBtnY, TILE_BTN_W, 28);
            g->DrawRectangle(penBorder, TILE_BTN_X, addBtnY, TILE_BTN_W, 28);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g->DrawString(L"+ \u6dfb\u52a0\u74e6\u7247", -1, fontSmall,
                          Gdiplus::RectF((float)TILE_BTN_X, (float)addBtnY,
                                         (float)TILE_BTN_W, 28.0f),
                          &sf, brushFg);
            delete bgBrush;
        }

        // "× 删除选中" button
        {
            int delBtnY = addBtnY + 32;
            bool hovered = gui.panelHovered &&
                           (lastMouseX >= TILE_BTN_X && lastMouseX < TILE_BTN_X + TILE_BTN_W &&
                            lastMouseY >= delBtnY && lastMouseY < delBtnY + 28);
            Gdiplus::SolidBrush* bgBrush = hovered ?
                new Gdiplus::SolidBrush(Gdiplus::Color(160, 40, 40)) :
                new Gdiplus::SolidBrush(Gdiplus::Color(120, 30, 30));
            g->FillRectangle(bgBrush, TILE_BTN_X, delBtnY, TILE_BTN_W, 28);
            g->DrawRectangle(penBorder, TILE_BTN_X, delBtnY, TILE_BTN_W, 28);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g->DrawString(L"\u00d7 \u5220\u9664\u9009\u4e2d", -1, fontSmall,
                          Gdiplus::RectF((float)TILE_BTN_X, (float)delBtnY,
                                         (float)TILE_BTN_W, 28.0f),
                          &sf, brushFg);
            delete bgBrush;
        }
    }

    // Brush size slider
    {
        int numTiles = tileCount();
        int visibleTiles = numTiles - 1 - tileScrollOffset;
        int sliderY = TILE_START_Y + visibleTiles * (TILE_BTN_H + TILE_BTN_GAP) + 68;

        wchar_t buf[64];
        swprintf_s(buf, L"\u7b14\u5237\u5927\u5c0f: %d", brushSize);
        g->DrawString(buf, -1, fontSmall,
                      Gdiplus::PointF((float)SLIDER_X, (float)sliderY), brushFg);

        sliderY += 20;

        // Track
        Gdiplus::SolidBrush trackBrush(Gdiplus::Color(62, 62, 66));
        g->FillRectangle(&trackBrush, SLIDER_X, sliderY, SLIDER_W, SLIDER_H);

        // Fill
        float fillRatio = (float)(brushSize - 1) / 9.0f;
        int fillW = (int)(SLIDER_W * fillRatio);
        Gdiplus::SolidBrush fillBrush(Gdiplus::Color(0, 120, 212));
        g->FillRectangle(&fillBrush, SLIDER_X, sliderY, fillW, SLIDER_H);

        // Knob
        int knobX = SLIDER_X + fillW;
        Gdiplus::SolidBrush knobBrush(Gdiplus::Color(255, 255, 255));
        g->FillEllipse(&knobBrush, knobX - 5, sliderY - 3, 10, SLIDER_H + 6);
    }
}

void Editor::renderCanvas(Gdiplus::Graphics* g) {
    int canvasW = getCanvasWidth();
    int canvasH = getCanvasHeight();

    // Clip to canvas area
    Gdiplus::Region oldClip;
    g->GetClip(&oldClip);
    g->SetClip(Gdiplus::Rect(PANEL_WIDTH, TOP_BAR_H, canvasW, canvasH));

    // Canvas background
    g->FillRectangle(brushBg, PANEL_WIDTH, TOP_BAR_H, canvasW, canvasH);

    if (map) {
        // Translate to canvas origin
        g->TranslateTransform((float)PANEL_WIDTH, (float)TOP_BAR_H);

        map->render(g, camera, layers ? layers->activeLayer : 0, canvasW, canvasH);

        // Hover highlight
        if (hoverTileX >= 0 && hoverTileY >= 0 && map->inBounds(hoverTileX, hoverTileY)) {
            float sx, sy;
            float tileSize = (float)TileMap::TILE_SIZE * camera.zoom;
            camera.worldToScreen(
                (float)(hoverTileX * TileMap::TILE_SIZE),
                (float)(hoverTileY * TileMap::TILE_SIZE),
                sx, sy);

            if (currentTool == BRUSH || currentTool == FILL) {
                Gdiplus::Pen hoverPen(Gdiplus::Color(200, 0, 255, 0), 2.0f);
                g->DrawRectangle(&hoverPen, sx, sy, tileSize, tileSize);
            } else if (currentTool == ERASER) {
                Gdiplus::Pen hoverPen(Gdiplus::Color(200, 255, 0, 0), 2.0f);
                g->DrawRectangle(&hoverPen, sx, sy, tileSize, tileSize);
            } else {
                Gdiplus::Pen hoverPen(Gdiplus::Color(200, 255, 255, 0), 2.0f);
                g->DrawRectangle(&hoverPen, sx, sy, tileSize, tileSize);
            }
        }

        g->ResetTransform();
    }

    g->SetClip(&oldClip);
}

void Editor::renderGrid(Gdiplus::Graphics* g) {
    // Grid is rendered by map->render() already
}

void Editor::renderMinimap(Gdiplus::Graphics* g) {
    if (!map) return;

    const int mmW = 150;
    const int mmH = 100;
    int canvasW = getCanvasWidth();
    int canvasH = getCanvasHeight();
    int mmX = canvasW - mmW - 10;
    int mmY = canvasH - mmH - 10;

    // Background
    Gdiplus::SolidBrush mmBg(Gdiplus::Color(20, 20, 20));
    g->FillRectangle(&mmBg, mmX, mmY, mmW, mmH);
    Gdiplus::Pen mmBorder(Gdiplus::Color(100, 100, 100));
    g->DrawRectangle(&mmBorder, mmX, mmY, mmW, mmH);

    float scaleX = (float)mmW / (map->width * TileMap::TILE_SIZE);
    float scaleY = (float)mmH / (map->height * TileMap::TILE_SIZE);
    float scale = std::min(scaleX, scaleY);

    int drawW = (int)(map->width * TileMap::TILE_SIZE * scale);
    int drawH = (int)(map->height * TileMap::TILE_SIZE * scale);
    int offX = mmX + (mmW - drawW) / 2;
    int offY = mmY + (mmH - drawH) / 2;

    // Draw tiles
    for (int ty = 0; ty < map->height; ty++) {
        for (int tx = 0; tx < map->width; tx++) {
            uint8_t id = map->layers[0][ty][tx];
            if (id == 0 || id >= map->tileDefs.size()) continue;
            DWORD c = map->tileDefs[id].color;
            Gdiplus::SolidBrush tb(Gdiplus::Color{c});
            int px = offX + (int)(tx * TileMap::TILE_SIZE * scale);
            int py = offY + (int)(ty * TileMap::TILE_SIZE * scale);
            int ps = std::max(1, (int)(TileMap::TILE_SIZE * scale));
            g->FillRectangle(&tb, px, py, ps, ps);
        }
    }

    // Viewport rectangle
    float vpLeft   = camera.x * scale;
    float vpTop    = camera.y * scale;
    float vpRight  = (camera.x + canvasW / camera.zoom) * scale;
    float vpBottom = (camera.y + canvasH / camera.zoom) * scale;

    Gdiplus::Pen vpPen(Gdiplus::Color(255, 255, 0), 1.0f);
    g->DrawRectangle(&vpPen,
                     offX + (int)vpLeft, offY + (int)vpTop,
                     (int)(vpRight - vpLeft), (int)(vpBottom - vpTop));
}

void Editor::renderStatusBar(Gdiplus::Graphics* g) {
    int sbY = windowH - STATUS_BAR_H;
    int canvasW = getCanvasWidth();

    wchar_t buf[512];
    swprintf_s(buf, L"%s | \u7b14\u5237: %d | \u7f29\u653e: %.0f%% | \u5750\u6807: (%d, %d) | %S",
               toolNames[currentTool], brushSize,
               camera.zoom * 100.0f,
               hoverTileX, hoverTileY,
               gui.statusText);

    g->DrawString(buf, -1, fontSmall,
                  Gdiplus::RectF((float)(PANEL_WIDTH + 8), (float)(sbY + 6),
                                 (float)(canvasW - 16), (float)(STATUS_BAR_H - 12)),
                  nullptr, brushFg);
}

void Editor::renderTooltip(Gdiplus::Graphics* g) {
    if (hoverTileX < 0 || hoverTileY < 0) return;
    if (!map || !map->inBounds(hoverTileX, hoverTileY)) return;

    // Only show tooltip when mouse is in canvas area
    // tooltip shows anywhere on canvas
    if (lastMouseY >= windowH - STATUS_BAR_H) return;

    uint8_t id = map->layers[0][hoverTileY][hoverTileX];
    if (id == 0 || id >= map->tileDefs.size()) return;

    const wchar_t* name = map->tileDefs[id].name.c_str();
    wchar_t buf[128];
    swprintf_s(buf, L"%s (%d, %d)", name, hoverTileX, hoverTileY);

    // Measure text
    Gdiplus::RectF layoutRect(0, 0, 300.0f, 30.0f);
    Gdiplus::RectF bounds;
    g->MeasureString(buf, -1, fontSmall, layoutRect, &bounds);

    int tx = lastMouseX + 16;
    int ty = lastMouseY - 24;
    int tw = (int)(bounds.Width + 12);
    int th = (int)(bounds.Height + 8);

    // Keep tooltip within window
    if (tx + tw > windowW) tx = lastMouseX - tw - 4;
    if (ty < 0) ty = lastMouseY + 16;

    Gdiplus::SolidBrush tipBg(Gdiplus::Color(45, 45, 48));
    g->FillRectangle(&tipBg, tx, ty, tw, th);
    Gdiplus::Pen tipBorder(Gdiplus::Color(85, 85, 85));
    g->DrawRectangle(&tipBorder, tx, ty, tw, th);
    g->DrawString(buf, -1, fontSmall,
                  Gdiplus::PointF((float)(tx + 6), (float)(ty + 4)), brushFg);
}

void Editor::drawButton(Gdiplus::Graphics* g, int x, int y, int w, int h,
                         const wchar_t* label, bool hovered, bool selected) {
    Gdiplus::SolidBrush* bg = selected ? brushAccent :
        (hovered ? new Gdiplus::SolidBrush(Gdiplus::Color(80, 80, 84)) :
         new Gdiplus::SolidBrush(Gdiplus::Color(62, 62, 66)));
    g->FillRectangle(bg, x, y, w, h);
    g->DrawRectangle(penBorder, x, y, w, h);

    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g->DrawString(label, -1, fontSmall,
                  Gdiplus::RectF((float)x, (float)y, (float)w, (float)h),
                  &sf, brushFg);

    if (!selected) delete bg;
}

// ---------------------------------------------------------------------------
// D2D Rendering
// ---------------------------------------------------------------------------

void Editor::renderD2D(D2DRenderer* r) {
    if (!r) return;

    // Update character animations
    if (map) {
        static LARGE_INTEGER lastAnimTime = {};
        static LARGE_INTEGER animFreq = {};
        static bool animInit = false;
        if (!animInit) {
            QueryPerformanceFrequency(&animFreq);
            QueryPerformanceCounter(&lastAnimTime);
            animInit = true;
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = (float)(now.QuadPart - lastAnimTime.QuadPart) / (float)animFreq.QuadPart;
        lastAnimTime = now;
        if (dt > 0.1f) dt = 0.1f;
        map->updateCharacterAnimation(dt);
    }

    renderToolPaletteD2D(r);
    renderMenuBarD2D(r);
    renderCanvasD2D(r);
    renderStatusBarD2D(r);
    renderMinimapD2D(r);
    renderTooltipD2D(r);
}

void Editor::renderCanvasD2D(D2DRenderer* r) {
    if (!r) return;

    int canvasW = getCanvasWidth();
    int canvasH = getCanvasHeight();

    // Canvas background
    r->fillRect((float)PANEL_WIDTH, (float)TOP_BAR_H,
                (float)canvasW, (float)canvasH,
                0.176f, 0.176f, 0.188f);

    if (!map) return;

    // Clip to canvas area
    r->pushClip((float)PANEL_WIDTH, (float)TOP_BAR_H,
                (float)canvasW, (float)canvasH);

    // Translate for canvas offset
    r->setTransform(1.0f, 1.0f, (float)PANEL_WIDTH, (float)TOP_BAR_H);

    // Prepare layer visibility/opacity arrays
    bool layerVis[7] = { true, true, true, true, true, true, true };
    float layerOpa[7] = { 1.0f, 1.0f, 0.6f, 1.0f, 1.0f, 1.0f, 0.5f };
    if (layers) {
        for (int i = 0; i < 7; i++) {
            layerVis[i] = layers->layers[i].visible;
            layerOpa[i] = layers->layers[i].opacity;
        }
    }

    map->renderD2D(r, camera, layers ? layers->activeLayer : 0,
                   canvasW, canvasH, layerVis, layerOpa,
                   layers ? layers->renderOrder : nullptr);

    // Hover highlight (brush size aware)
    if (hoverTileX >= 0 && hoverTileY >= 0 && map->inBounds(hoverTileX, hoverTileY)) {
        float tileSize = (float)TileMap::TILE_SIZE * camera.zoom;
        int half = brushSize / 2;

        if (currentTool == BRUSH || currentTool == ERASER) {
            // Show full brush area in green
            float bx, by;
            camera.worldToScreen(
                (float)((hoverTileX - half) * TileMap::TILE_SIZE),
                (float)((hoverTileY - half) * TileMap::TILE_SIZE),
                bx, by);
            float brushPx = tileSize * brushSize;
            r->drawRect(bx, by, brushPx, brushPx, 0.0f, 1.0f, 0.0f, 0.7f, 2.0f);
        } else if (currentTool == FILL) {
            float sx, sy;
            camera.worldToScreen(
                (float)(hoverTileX * TileMap::TILE_SIZE),
                (float)(hoverTileY * TileMap::TILE_SIZE),
                sx, sy);
            r->drawRect(sx, sy, tileSize, tileSize, 0.0f, 1.0f, 0.0f, 0.8f, 2.0f);
        } else {
            float sx, sy;
            camera.worldToScreen(
                (float)(hoverTileX * TileMap::TILE_SIZE),
                (float)(hoverTileY * TileMap::TILE_SIZE),
                sx, sy);
            r->drawRect(sx, sy, tileSize, tileSize, 1.0f, 1.0f, 0.0f, 0.8f, 2.0f);
        }
    }

    // Grid rendering
    if (gui.showGrid && camera.zoom >= 0.3f) {
        float ts = (float)TileMap::TILE_SIZE * camera.zoom;
        if (ts >= 4.0f) {
            float alpha = std::min(1.0f, (ts - 4.0f) / 16.0f) * 0.35f;
            int startTX = (int)(camera.x / TileMap::TILE_SIZE) - 1;
            int startTY = (int)(camera.y / TileMap::TILE_SIZE) - 1;
            int endTX = startTX + (int)(canvasW / ts) + 3;
            int endTY = startTY + (int)(canvasH / ts) + 3;
            startTX = std::max(0, startTX);
            startTY = std::max(0, startTY);
            endTX = std::min(map->width, endTX);
            endTY = std::min(map->height, endTY);

            for (int gx = startTX; gx <= endTX; gx++) {
                float wx = (float)(gx * TileMap::TILE_SIZE);
                float sx, sy0, sy1;
                camera.worldToScreen(wx, (float)(startTY * TileMap::TILE_SIZE), sx, sy0);
                camera.worldToScreen(wx, (float)(endTY * TileMap::TILE_SIZE), sx, sy1);
                r->drawLine(sx, sy0, sx, sy1, 0.5f, 0.5f, 0.5f, alpha);
            }
            for (int gy = startTY; gy <= endTY; gy++) {
                float wy = (float)(gy * TileMap::TILE_SIZE);
                float sy, sx0, sx1;
                camera.worldToScreen((float)(startTX * TileMap::TILE_SIZE), wy, sx0, sy);
                camera.worldToScreen((float)(endTX * TileMap::TILE_SIZE), wy, sx1, sy);
                r->drawLine(sx0, sy, sx1, sy, 0.5f, 0.5f, 0.5f, alpha);
            }
        }
    }

    // Character collision capsule visualization (layer 3)
    if (layers && layers->layers[LayerManager::LAYER_CHARACTER_COL].visible && map) {
        for (auto& ch : map->characters) {
            float cx, cy;
            camera.worldToScreen(ch.worldX, ch.worldY, cx, cy);
            float crx = ch.capsule.radiusX * camera.zoom;
            float cry = ch.capsule.radiusY * camera.zoom;
            float crxv = ch.capsule.radiusXv * camera.zoom;
            float cryv = ch.capsule.radiusYv * camera.zoom;
            float coffY = ch.capsule.offsetY * camera.zoom;

            r->drawEllipse(cx, cy + coffY, crx, cry, 0.9f, 0.2f, 0.2f, 0.6f, 1.5f);
            r->drawEllipse(cx, cy + coffY, crxv, cryv, 0.9f, 0.2f, 0.2f, 0.6f, 1.5f);
        }
    }

    r->resetTransform();
    r->popClip();
}

void Editor::renderMenuBarD2D(D2DRenderer* r) {
    if (!r) return;

    // Bottom bar background
    float barTop = (float)(windowH - STATUS_BAR_H);
    r->fillRect(0.0f, barTop, (float)windowW, (float)STATUS_BAR_H,
                0.118f, 0.118f, 0.118f);
    r->drawLine(0.0f, barTop, (float)windowW, barTop, 0.333f, 0.333f, 0.333f);

    // ---- Row 0: Tools (left) + Menu buttons (right) ----
    int row0Y = (int)barTop + 4;

    // Tool icons with symbols
    static const wchar_t* toolIcons[] = {
        L"\u270f",   // pencil
        L"\u2716",   // eraser
        L"\u25a1",   // select
        L"\u2194",   // pan
        L"\u2b1c"    // fill
    };
    for (int i = 0; i < 5; i++) {
        int tx = 8 + i * (TOOL_ICON_SIZE + TOOL_ICON_GAP);
        bool sel = (currentTool == i);
        bool hov = (lastMouseX >= tx && lastMouseX < tx + TOOL_ICON_SIZE &&
                    lastMouseY >= row0Y && lastMouseY < row0Y + TOOL_ICON_SIZE);
        r->fillRect((float)tx, (float)row0Y, (float)TOOL_ICON_SIZE, (float)TOOL_ICON_SIZE,
                    sel ? 0.0f : (hov ? 0.28f : 0.22f),
                    sel ? 0.471f : (hov ? 0.28f : 0.22f),
                    sel ? 0.831f : (hov ? 0.30f : 0.23f), 0.9f);
        r->drawRect((float)tx, (float)row0Y, (float)TOOL_ICON_SIZE, (float)TOOL_ICON_SIZE,
                    sel ? 0.2f : 0.4f, sel ? 0.6f : 0.4f, sel ? 1.0f : 0.4f);
        r->drawText(toolIcons[i], (float)tx, (float)row0Y,
                    (float)TOOL_ICON_SIZE, (float)TOOL_ICON_SIZE,
                    1.0f, 1.0f, 1.0f, sel ? 1.0f : 0.8f, 12.0f, false,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Tool name label next to icons
    int toolLabelX = 8 + 5 * (TOOL_ICON_SIZE + TOOL_ICON_GAP) + 8;
    r->drawText(toolNames[currentTool], (float)toolLabelX, (float)row0Y,
                80.0f, (float)TOOL_ICON_SIZE,
                0.7f, 0.7f, 0.7f, 0.9f, 10.0f, false,
                DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    // Menu buttons (right side of row 0)
    for (int i = 0; i < NUM_MENU_BUTTONS; i++) {
        int bx = windowW - (NUM_MENU_BUTTONS - i) * (MENU_BTN_W + MENU_BTN_GAP) - 8;
        bool hovered = (lastMouseX >= bx && lastMouseX < bx + MENU_BTN_W &&
                        lastMouseY >= row0Y && lastMouseY < row0Y + MENU_BTN_H);
        r->fillRect((float)bx, (float)row0Y, (float)MENU_BTN_W, (float)MENU_BTN_H,
                    hovered ? 0.314f : 0.243f, hovered ? 0.314f : 0.243f, hovered ? 0.329f : 0.259f);
        r->drawRect((float)bx, (float)row0Y, (float)MENU_BTN_W, (float)MENU_BTN_H,
                    0.333f, 0.333f, 0.333f);
        r->drawText(menuLabels[i], (float)bx, (float)row0Y,
                    (float)MENU_BTN_W, (float)MENU_BTN_H,
                    1.0f, 1.0f, 1.0f, 1.0f, 9.0f, false,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }


    // "+ NPC" button (next to menu buttons)
    {
        int npcBtnX = windowW - (NUM_MENU_BUTTONS + 1) * (MENU_BTN_W + MENU_BTN_GAP) - 8;
        bool hovered = (lastMouseX >= npcBtnX && lastMouseX < npcBtnX + MENU_BTN_W &&
                        lastMouseY >= row0Y && lastMouseY < row0Y + MENU_BTN_H);
        r->fillRect((float)npcBtnX, (float)row0Y, (float)MENU_BTN_W, (float)MENU_BTN_H,
                    hovered ? 0.6f : 0.5f, hovered ? 0.5f : 0.4f, 0.0f);
        r->drawRect((float)npcBtnX, (float)row0Y, (float)MENU_BTN_W, (float)MENU_BTN_H,
                    0.333f, 0.333f, 0.333f);
        r->drawText(L"+ NPC", (float)npcBtnX, (float)row0Y,
                    (float)MENU_BTN_W, (float)MENU_BTN_H,
                    1.0f, 1.0f, 0.6f, 1.0f, 9.0f, false,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    // ---- Row 1: Layer buttons (horizontal tabs, full width) ----
    int row1Y = row0Y + TOOL_ICON_SIZE + 6;
    if (layers) {
        int layerStartX = 8;
        int layerBtnW = 64;
        int layerBtnH = 20;
        for (int i = 0; i < LayerManager::NUM_LAYERS; i++) {
            int bx = layerStartX + i * (layerBtnW + 3);
            bool active = (layers->activeLayer == i);
            bool vis = layers->layers[i].visible;
            r->fillRect((float)bx, (float)row1Y, (float)layerBtnW, (float)layerBtnH,
                        active ? 0.20f : 0.18f, active ? 0.40f : 0.18f, active ? 0.65f : 0.19f);
            r->drawRect((float)bx, (float)row1Y, (float)layerBtnW, (float)layerBtnH,
                        0.333f, 0.333f, 0.333f);
            float cR, cG, cB;
            layers->getLayerColors(i, cR, cG, cB);
            r->fillRect((float)bx + 2, (float)row1Y + 3, 3.0f, (float)(layerBtnH - 6),
                        cR, cG, cB, vis ? 0.9f : 0.3f);
            r->drawText(layers->layers[i].name.c_str(), (float)(bx + 7), (float)row1Y,
                        (float)(layerBtnW - 9), (float)layerBtnH,
                        1.0f, 1.0f, 1.0f, vis ? 1.0f : 0.4f, 8.0f, false,
                        DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // ---- Row 2: Tile thumbnails (scrollable horizontal strip) + add/delete ----
    int tileRowY = row1Y + 24;
    int numTiles = tileCount();
    for (int i = 1; i < numTiles; i++) {
        int tx = (i - 1 - tileScrollOffset) * (TILE_BTN_H + TILE_BTN_GAP) + 4;
        if (tx + TILE_BTN_H < 0 || tx > windowW) continue;
        bool sel = (selectedTileId == i);
        r->fillRect((float)tx, (float)tileRowY, (float)TILE_BTN_H, (float)TILE_BTN_H,
                    sel ? 0.0f : 0.18f, sel ? 0.471f : 0.18f, sel ? 0.831f : 0.19f);
        r->drawRect((float)tx, (float)tileRowY, (float)TILE_BTN_H, (float)TILE_BTN_H,
                    sel ? 0.0f : 0.333f, sel ? 0.6f : 0.333f, sel ? 1.0f : 0.333f);
        if (i < (int)map->tileDefs.size()) {
            auto d2dIt = map->d2dTextures.find(i);
            if (d2dIt != map->d2dTextures.end() && d2dIt->second) {
                r->drawBitmap(d2dIt->second, (float)(tx + 2), (float)(tileRowY + 2),
                              (float)(TILE_BTN_H - 4), (float)(TILE_BTN_H - 4));
            } else {
                DWORD c = map->tileDefs[i].color;
                float cr = ((c >> 16) & 0xFF) / 255.0f;
                float cg = ((c >> 8) & 0xFF) / 255.0f;
                float cb = (c & 0xFF) / 255.0f;
                r->fillRect((float)(tx + 2), (float)(tileRowY + 2),
                            (float)(TILE_BTN_H - 4), (float)(TILE_BTN_H - 4), cr, cg, cb);
            }
        }
    }
    // "+" add tile button
    {
        int addBtnX = numTiles > 1 ?
            (numTiles - 1 - tileScrollOffset) * (TILE_BTN_H + TILE_BTN_GAP) + 4 + TILE_BTN_H + 8 : 4;
        r->fillRect((float)addBtnX, (float)tileRowY, 30.0f, (float)TILE_BTN_H,
                    0.0f, 0.392f, 0.706f, 0.8f);
        r->drawRect((float)addBtnX, (float)tileRowY, 30.0f, (float)TILE_BTN_H,
                    0.333f, 0.333f, 0.333f);
        r->drawText(L"+", (float)addBtnX, (float)tileRowY, 30.0f, (float)TILE_BTN_H,
                    1.0f, 1.0f, 1.0f, 1.0f, 14.0f, false,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    // "-" delete tile button
    {
        int addBtnX = numTiles > 1 ?
            (numTiles - 1 - tileScrollOffset) * (TILE_BTN_H + TILE_BTN_GAP) + 4 + TILE_BTN_H + 8 : 4;
        int delBtnX = addBtnX + 34;
        r->fillRect((float)delBtnX, (float)tileRowY, 30.0f, (float)TILE_BTN_H,
                    0.627f, 0.157f, 0.157f, 0.8f);
        r->drawRect((float)delBtnX, (float)tileRowY, 30.0f, (float)TILE_BTN_H,
                    0.333f, 0.333f, 0.333f);
        r->drawText(L"-", (float)delBtnX, (float)tileRowY, 30.0f, (float)TILE_BTN_H,
                    1.0f, 1.0f, 1.0f, 1.0f, 14.0f, false,
                    DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // ---- Row 2.5: Brush size control ----
    {
        int brushRowY = tileRowY + TILE_BTN_H + 4;
        // Label
        wchar_t bbuf[64];
        swprintf_s(bbuf, L"\u7b14\u5237: %d", brushSize);
        r->drawText(bbuf, 8.0f, (float)brushRowY, 70.0f, 18.0f,
            0.7f, 0.7f, 0.7f, 0.9f, 9.0f, false,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Slider track
        int sliderX = 80;
        int sliderW = windowW - 200;
        int sliderY2 = brushRowY + 4;
        int sliderH2 = 8;
        r->fillRect((float)sliderX, (float)sliderY2, (float)sliderW, (float)sliderH2,
            0.24f, 0.24f, 0.26f, 0.8f);
        // Slider fill
        float fillRatio = (float)(brushSize - 1) / 9.0f;
        int fillW = (int)(sliderW * fillRatio);
        r->fillRect((float)sliderX, (float)sliderY2, (float)fillW, (float)sliderH2,
            0.0f, 0.471f, 0.831f, 0.9f);
        // Slider knob
        float knobX = (float)(sliderX + fillW);
        r->fillEllipse(knobX, (float)(sliderY2 + sliderH2 / 2),
            5.0f, (float)(sliderH2 / 2 + 3),
            1.0f, 1.0f, 1.0f, 0.95f);

        // "-" button
        int minusX = windowW - 120;
        r->fillRect((float)minusX, (float)brushRowY, 24.0f, 18.0f,
            0.24f, 0.24f, 0.26f, 0.8f);
        r->drawRect((float)minusX, (float)brushRowY, 24.0f, 18.0f,
            0.4f, 0.4f, 0.4f);
        r->drawText(L"-", (float)minusX, (float)brushRowY, 24.0f, 18.0f,
            1.0f, 1.0f, 1.0f, 0.9f, 11.0f, false,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // "+" button
        int plusX = windowW - 90;
        r->fillRect((float)plusX, (float)brushRowY, 24.0f, 18.0f,
            0.24f, 0.24f, 0.26f, 0.8f);
        r->drawRect((float)plusX, (float)brushRowY, 24.0f, 18.0f,
            0.4f, 0.4f, 0.4f);
        r->drawText(L"+", (float)plusX, (float)brushRowY, 24.0f, 18.0f,
            1.0f, 1.0f, 1.0f, 0.9f, 11.0f, false,
            DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        // Size number
        wchar_t nbuf[8];
        swprintf_s(nbuf, L"%d", brushSize);
        r->drawText(nbuf, (float)(plusX + 28), (float)brushRowY, 40.0f, 18.0f,
            0.8f, 0.8f, 0.8f, 0.9f, 10.0f, false,
            DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // ---- Row 3: Status info (bottom of bar) ----
    int statusY = windowH - 20;
    wchar_t buf[256];
    swprintf_s(buf, L"%s | b:%d w:%d h:%d | zoom:%.0f%% | (%d,%d) | %S",
               toolNames[currentTool], windowH - STATUS_BAR_H, windowW, windowH, camera.zoom * 100.0f,
               hoverTileX, hoverTileY, gui.statusText);
    r->drawText(buf, 8.0f, (float)statusY, (float)(windowW - 16), 18.0f,
                0.6f, 0.6f, 0.6f, 0.9f, 9.0f);
}


void Editor::renderToolPaletteD2D(D2DRenderer* r) {
    if (!r) return;

    // All tools are now rendered in renderMenuBarD2D
    // This method is now empty (layout moved to bottom bar)
    return;

    // Legacy code below (unreachable)
    {
        int toolY = 8;
        for (int i = 0; i < 5; i++) {
            int tx = 8 + i * (TOOL_ICON_SIZE + TOOL_ICON_GAP);
            bool hovered = gui.panelHovered &&
                           (lastMouseX >= tx && lastMouseX < tx + TOOL_ICON_SIZE &&
                            lastMouseY >= toolY && lastMouseY < toolY + TOOL_ICON_SIZE);
            bool selected = (currentTool == i);

            if (selected) {
                r->fillRect((float)tx, (float)toolY,
                            (float)TOOL_ICON_SIZE, (float)TOOL_ICON_SIZE,
                            0.0f, 0.471f, 0.831f);
            } else if (hovered) {
                r->fillRect((float)tx, (float)toolY,
                            (float)TOOL_ICON_SIZE, (float)TOOL_ICON_SIZE,
                            0.314f, 0.314f, 0.329f);
            } else {
                r->fillRect((float)tx, (float)toolY,
                            (float)TOOL_ICON_SIZE, (float)TOOL_ICON_SIZE,
                            0.243f, 0.243f, 0.259f);
            }
            r->drawRect((float)tx, (float)toolY,
                        (float)TOOL_ICON_SIZE, (float)TOOL_ICON_SIZE,
                        0.333f, 0.333f, 0.333f);

            r->drawText(toolNames[i], (float)tx, (float)toolY,
                        (float)TOOL_ICON_SIZE, (float)TOOL_ICON_SIZE,
                        1.0f, 1.0f, 1.0f, 1.0f, 9.0f, false,
                        DWRITE_TEXT_ALIGNMENT_CENTER,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // Layer buttons (5 compact tabs below tool icons)
    {
        static const wchar_t* layerLabels[] = {
            L"\u5730\u5f62\u56fe",
            L"\u78b0\u649e\u5c42",
            L"\u89d2\u8272\u5c42",
            L"\u78b0\u649e\u6846",
            L"\u524d\u666f\u5c42"
        };
        int layerBtnW = (PANEL_WIDTH - 16 - 8) / 5;
        int layerBtnX = 8;
        int layerBtnY = 2;

        for (int i = 0; i < 5; i++) {
            int bx = layerBtnX + i * (layerBtnW + 2);
            bool active = (layers && layers->activeLayer == i);
            bool hovered = gui.panelHovered &&
                           (lastMouseX >= bx && lastMouseX < bx + layerBtnW &&
                            lastMouseY >= layerBtnY && lastMouseY < layerBtnY + LAYER_BTN_H);

            if (active) {
                r->fillRect((float)bx, (float)layerBtnY,
                            (float)layerBtnW, (float)LAYER_BTN_H,
                            0.0f, 0.471f, 0.831f);
            } else if (hovered) {
                r->fillRect((float)bx, (float)layerBtnY,
                            (float)layerBtnW, (float)LAYER_BTN_H,
                            0.314f, 0.314f, 0.329f);
            } else {
                r->fillRect((float)bx, (float)layerBtnY,
                            (float)layerBtnW, (float)LAYER_BTN_H,
                            0.243f, 0.243f, 0.259f);
            }
            r->drawRect((float)bx, (float)layerBtnY,
                        (float)layerBtnW, (float)LAYER_BTN_H,
                        0.333f, 0.333f, 0.333f);

            r->drawText(layerLabels[i], (float)bx, (float)layerBtnY,
                        (float)layerBtnW, (float)LAYER_BTN_H,
                        1.0f, 1.0f, 1.0f, 1.0f, 8.0f, false,
                        DWRITE_TEXT_ALIGNMENT_CENTER,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // Tile section label
    r->drawText(L"\u74e6\u7247", 8.0f, (float)(TOP_BAR_H + 4),
                (float)PANEL_WIDTH, 20.0f,
                1.0f, 1.0f, 1.0f, 1.0f, 11.0f);

    // Tile buttons
    int numTiles = tileCount();
    for (int i = 1; i < numTiles; i++) {
        int idx = i - 1 - tileScrollOffset;
        if (idx < 0) continue;
        int by = TILE_START_Y + idx * (TILE_BTN_H + TILE_BTN_GAP);
        if (by + TILE_BTN_H < TILE_START_Y || by > windowH) continue;
        bool hovered = gui.panelHovered &&
                       (lastMouseX >= TILE_BTN_X && lastMouseX < TILE_BTN_X + TILE_BTN_W &&
                        lastMouseY >= by && lastMouseY < by + TILE_BTN_H);
        bool selected = (selectedTileId == i);

        if (selected) {
            r->fillRect((float)TILE_BTN_X, (float)by,
                        (float)TILE_BTN_W, (float)TILE_BTN_H,
                        0.0f, 0.471f, 0.831f);
        } else if (hovered) {
            r->fillRect((float)TILE_BTN_X, (float)by,
                        (float)TILE_BTN_W, (float)TILE_BTN_H,
                        0.314f, 0.314f, 0.329f);
        } else {
            r->fillRect((float)TILE_BTN_X, (float)by,
                        (float)TILE_BTN_W, (float)TILE_BTN_H,
                        0.243f, 0.243f, 0.259f);
        }
        r->drawRect((float)TILE_BTN_X, (float)by,
                    (float)TILE_BTN_W, (float)TILE_BTN_H,
                    0.333f, 0.333f, 0.333f);

        // Draw tile preview
        auto d2dIt = map->d2dTextures.find(i);
        if (d2dIt != map->d2dTextures.end() && d2dIt->second) {
            r->drawBitmap(d2dIt->second, (float)(TILE_BTN_X + 4), (float)(by + 4), 36.0f, 36.0f);
        } else {
            DWORD c = map->tileDefs[i].color;
            float cr = ((c >> 16) & 0xFF) / 255.0f;
            float cg = ((c >> 8) & 0xFF) / 255.0f;
            float cb = (c & 0xFF) / 255.0f;
            r->fillRect((float)(TILE_BTN_X + 4), (float)(by + 4), 36.0f, 36.0f, cr, cg, cb);
        }

        // Tile name
        r->drawText(map->tileDefs[i].name.c_str(),
                    (float)(TILE_BTN_X + 44), (float)(by + 12),
                    (float)(TILE_BTN_W - 48), (float)(TILE_BTN_H - 16),
                    1.0f, 1.0f, 1.0f, 1.0f, 9.0f);
    }

    // Add/Delete tile buttons (D2D)
    {
        int visibleTiles = numTiles - 1 - tileScrollOffset;
        int addBtnY = TILE_START_Y + visibleTiles * (TILE_BTN_H + TILE_BTN_GAP) + 4;

        // "+ 添加瓦片" button
        {
            bool hovered = gui.panelHovered &&
                           (lastMouseX >= TILE_BTN_X && lastMouseX < TILE_BTN_X + TILE_BTN_W &&
                            lastMouseY >= addBtnY && lastMouseY < addBtnY + 28);
            if (hovered) {
                r->fillRect((float)TILE_BTN_X, (float)addBtnY,
                            (float)TILE_BTN_W, 28.0f,
                            0.0f, 0.392f, 0.706f);
            } else {
                r->fillRect((float)TILE_BTN_X, (float)addBtnY,
                            (float)TILE_BTN_W, 28.0f,
                            0.0f, 0.314f, 0.588f);
            }
            r->drawRect((float)TILE_BTN_X, (float)addBtnY,
                        (float)TILE_BTN_W, 28.0f,
                        0.333f, 0.333f, 0.333f);
            r->drawText(L"+ \u6dfb\u52a0\u74e6\u7247",
                        (float)TILE_BTN_X, (float)addBtnY,
                        (float)TILE_BTN_W, 28.0f,
                        1.0f, 1.0f, 1.0f, 1.0f, 9.0f, false,
                        DWRITE_TEXT_ALIGNMENT_CENTER,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        // "× 删除选中" button
        {
            int delBtnY = addBtnY + 32;
            bool hovered = gui.panelHovered &&
                           (lastMouseX >= TILE_BTN_X && lastMouseX < TILE_BTN_X + TILE_BTN_W &&
                            lastMouseY >= delBtnY && lastMouseY < delBtnY + 28);
            if (hovered) {
                r->fillRect((float)TILE_BTN_X, (float)delBtnY,
                            (float)TILE_BTN_W, 28.0f,
                            0.627f, 0.157f, 0.157f);
            } else {
                r->fillRect((float)TILE_BTN_X, (float)delBtnY,
                            (float)TILE_BTN_W, 28.0f,
                            0.471f, 0.118f, 0.118f);
            }
            r->drawRect((float)TILE_BTN_X, (float)delBtnY,
                        (float)TILE_BTN_W, 28.0f,
                        0.333f, 0.333f, 0.333f);
            r->drawText(L"\u00d7 \u5220\u9664\u9009\u4e2d",
                        (float)TILE_BTN_X, (float)delBtnY,
                        (float)TILE_BTN_W, 28.0f,
                        1.0f, 1.0f, 1.0f, 1.0f, 9.0f, false,
                        DWRITE_TEXT_ALIGNMENT_CENTER,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    // Brush size slider
    {
        int visibleTiles = numTiles - 1 - tileScrollOffset;
        int sliderLabelY = TILE_START_Y + visibleTiles * (TILE_BTN_H + TILE_BTN_GAP) + 72;

        wchar_t buf[64];
        swprintf_s(buf, L"\u7b14\u5237\u5927\u5c0f: %d", brushSize);
        r->drawText(buf, (float)SLIDER_X, (float)sliderLabelY,
                    (float)SLIDER_W, 18.0f,
                    1.0f, 1.0f, 1.0f, 1.0f, 9.0f);

        int sliderY = sliderLabelY + 20;

        // Track
        r->fillRect((float)SLIDER_X, (float)sliderY,
                    (float)SLIDER_W, (float)SLIDER_H,
                    0.243f, 0.243f, 0.259f);

        // Fill
        float fillRatio = (float)(brushSize - 1) / 9.0f;
        int fillW = (int)(SLIDER_W * fillRatio);
        r->fillRect((float)SLIDER_X, (float)sliderY,
                    (float)fillW, (float)SLIDER_H,
                    0.0f, 0.471f, 0.831f);

        // Knob
        float knobX = (float)(SLIDER_X + fillW);
        r->fillEllipse(knobX, (float)(sliderY + SLIDER_H / 2),
                       5.0f, (float)(SLIDER_H / 2 + 3),
                       1.0f, 1.0f, 1.0f);
    }

    // ---- Character Section ----
    {
        int numTiles = tileCount();
        int visibleTiles = numTiles - 1 - tileScrollOffset;
        int charSectionY = TILE_START_Y + visibleTiles * (TILE_BTN_H + TILE_BTN_GAP) + 100;

        // Section label
        r->drawText(L"\u89d2\u8272", 8.0f, (float)charSectionY,
                    (float)PANEL_WIDTH, 18.0f,
                    1.0f, 1.0f, 1.0f, 1.0f, 11.0f);
        charSectionY += 20;

        // Character list (compact)
        if (map) {
            int maxChars = std::min((int)map->characters.size(), 4);
            for (int i = 0; i < maxChars; i++) {
                int cy = charSectionY + i * 22;
                bool selected = (selectedCharacterIdx == i);
                bool hovered = gui.panelHovered &&
                               (lastMouseX >= TILE_BTN_X && lastMouseX < TILE_BTN_X + TILE_BTN_W &&
                                lastMouseY >= cy && lastMouseY < cy + 20);

                if (selected) {
                    r->fillRect((float)TILE_BTN_X, (float)cy,
                                (float)TILE_BTN_W, 20.0f,
                                0.0f, 0.471f, 0.831f);
                } else if (hovered) {
                    r->fillRect((float)TILE_BTN_X, (float)cy,
                                (float)TILE_BTN_W, 20.0f,
                                0.314f, 0.314f, 0.329f);
                } else {
                    r->fillRect((float)TILE_BTN_X, (float)cy,
                                (float)TILE_BTN_W, 20.0f,
                                0.243f, 0.243f, 0.259f);
                }
                r->drawRect((float)TILE_BTN_X, (float)cy,
                            (float)TILE_BTN_W, 20.0f,
                            0.333f, 0.333f, 0.333f);

                // Type color indicator
                auto& ch = map->characters[i];
                float typeR, typeG, typeB;
                if (ch.type == L"player") { typeR = 0.2f; typeG = 0.8f; typeB = 1.0f; }
                else if (ch.type == L"npc") { typeR = 0.2f; typeG = 1.0f; typeB = 0.4f; }
                else if (ch.type == L"enemy") { typeR = 1.0f; typeG = 0.3f; typeB = 0.2f; }
                else { typeR = 0.7f; typeG = 0.7f; typeB = 0.7f; }
                r->fillRect((float)(TILE_BTN_X + 3), (float)(cy + 3), 14.0f, 14.0f,
                            typeR, typeG, typeB);

                // Name
                r->drawText(ch.name.c_str(),
                            (float)(TILE_BTN_X + 20), (float)(cy + 2),
                            (float)(TILE_BTN_W - 24), 16.0f,
                            1.0f, 1.0f, 1.0f, 1.0f, 8.0f);
            }
            charSectionY += maxChars * 22;
        }

        // "Import Sprite" button
        {
            int btnY = charSectionY + 4;
            bool hovered = gui.panelHovered &&
                           (lastMouseX >= TILE_BTN_X && lastMouseX < TILE_BTN_X + TILE_BTN_W &&
                            lastMouseY >= btnY && lastMouseY < btnY + 22);
            if (hovered) {
                r->fillRect((float)TILE_BTN_X, (float)btnY,
                            (float)TILE_BTN_W, 22.0f,
                            0.0f, 0.392f, 0.706f);
            } else {
                r->fillRect((float)TILE_BTN_X, (float)btnY,
                            (float)TILE_BTN_W, 22.0f,
                            0.0f, 0.314f, 0.588f);
            }
            r->drawRect((float)TILE_BTN_X, (float)btnY,
                        (float)TILE_BTN_W, 22.0f,
                        0.333f, 0.333f, 0.333f);
            r->drawText(L"\u5bfc\u5165\u7cbe\u7075",
                        (float)TILE_BTN_X, (float)btnY,
                        (float)TILE_BTN_W, 22.0f,
                        1.0f, 1.0f, 1.0f, 1.0f, 9.0f, false,
                        DWRITE_TEXT_ALIGNMENT_CENTER,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        // "Import Animation" button
        {
            int btnY = charSectionY + 28;
            bool hovered = gui.panelHovered &&
                           (lastMouseX >= TILE_BTN_X && lastMouseX < TILE_BTN_X + TILE_BTN_W &&
                            lastMouseY >= btnY && lastMouseY < btnY + 22);
            if (hovered) {
                r->fillRect((float)TILE_BTN_X, (float)btnY,
                            (float)TILE_BTN_W, 22.0f,
                            0.0f, 0.392f, 0.706f);
            } else {
                r->fillRect((float)TILE_BTN_X, (float)btnY,
                            (float)TILE_BTN_W, 22.0f,
                            0.0f, 0.314f, 0.588f);
            }
            r->drawRect((float)TILE_BTN_X, (float)btnY,
                        (float)TILE_BTN_W, 22.0f,
                        0.333f, 0.333f, 0.333f);
            r->drawText(L"\u5bfc\u5165\u52a8\u753b",
                        (float)TILE_BTN_X, (float)btnY,
                        (float)TILE_BTN_W, 22.0f,
                        1.0f, 1.0f, 1.0f, 1.0f, 9.0f, false,
                        DWRITE_TEXT_ALIGNMENT_CENTER,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }


        // "Add NPC" button
        {
            int btnY = charSectionY + 52;
            bool hovered = gui.panelHovered &&
                           (lastMouseX >= TILE_BTN_X && lastMouseX < TILE_BTN_X + TILE_BTN_W &&
                            lastMouseY >= btnY && lastMouseY < btnY + 22);
            if (hovered) {
                r->fillRect((float)TILE_BTN_X, (float)btnY,
                            (float)TILE_BTN_W, 22.0f,
                            0.6f, 0.5f, 0.0f);
            } else {
                r->fillRect((float)TILE_BTN_X, (float)btnY,
                            (float)TILE_BTN_W, 22.0f,
                            0.5f, 0.4f, 0.0f);
            }
            r->drawRect((float)TILE_BTN_X, (float)btnY,
                        (float)TILE_BTN_W, 22.0f,
                        0.333f, 0.333f, 0.333f);
            r->drawText(L"+ \u6dfb\u52a0NPC",
                        (float)TILE_BTN_X, (float)btnY,
                        (float)TILE_BTN_W, 22.0f,
                        1.0f, 1.0f, 0.6f, 1.0f, 9.0f, false,
                        DWRITE_TEXT_ALIGNMENT_CENTER,
                        DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        // Sprite info for selected character
        if (map && selectedCharacterIdx >= 0 && selectedCharacterIdx < (int)map->characters.size()) {
            auto& ch = map->characters[selectedCharacterIdx];
            int infoY = charSectionY + 56;

            if (ch.spriteSheet) {
                wchar_t buf[128];
                swprintf_s(buf, L"%dx%d %d\u5e27 %dFPS %s",
                           ch.frameWidth, ch.frameHeight,
                           ch.frameCount, ch.animFps,
                           ch.animLoop ? L"\u5faa\u73af" : L"\u5355\u6b21");
                r->drawText(buf, 8.0f, (float)infoY,
                            (float)PANEL_WIDTH, 16.0f,
                            0.667f, 0.667f, 0.667f, 1.0f, 8.0f);
            } else {
                r->drawText(L"\u65e0\u7cbe\u7075", 8.0f, (float)infoY,
                            (float)PANEL_WIDTH, 16.0f,
                            0.5f, 0.5f, 0.5f, 1.0f, 8.0f);
            }
        }
    }

    // Character properties panel (shown when a character is selected)
    if (map && selectedCharacterIdx >= 0 && selectedCharacterIdx < (int)map->characters.size()) {
        auto& ch = map->characters[selectedCharacterIdx];
        int propY = 100;

        r->fillRect(0.0f, (float)propY, (float)PANEL_WIDTH, 2.0f, 0.333f, 0.333f, 0.333f);
        propY += 6;

        r->drawText(L"角色属性", 8.0f, (float)propY, (float)PANEL_WIDTH, 16.0f,
                    0.0f, 0.471f, 0.831f, 1.0f, 10.0f, true);
        propY += 18;

        // Type
        wchar_t typeBuf[64];
        swprintf_s(typeBuf, L"类型: %s", ch.type.c_str());
        r->drawText(typeBuf, 8.0f, (float)propY, (float)PANEL_WIDTH, 14.0f,
                    0.8f, 0.8f, 0.8f, 1.0f, 9.0f);
        propY += 16;

        // Name
        wchar_t nameBuf[64];
        swprintf_s(nameBuf, L"名称: %s", ch.name.c_str());
        r->drawText(nameBuf, 8.0f, (float)propY, (float)PANEL_WIDTH, 14.0f,
                    0.8f, 0.8f, 0.8f, 1.0f, 9.0f);
        propY += 16;

        // Position
        wchar_t posBuf[64];
        swprintf_s(posBuf, L"位置: (%.0f, %.0f)", ch.worldX, ch.worldY);
        r->drawText(posBuf, 8.0f, (float)propY, (float)PANEL_WIDTH, 14.0f,
                    0.8f, 0.8f, 0.8f, 1.0f, 9.0f);
        propY += 16;

        // Stats
        wchar_t hpBuf[64];
        swprintf_s(hpBuf, L"HP:%d  攻击:%d  防御:%d", ch.hp, ch.attack, ch.defense);
        r->drawText(hpBuf, 8.0f, (float)propY, (float)PANEL_WIDTH, 14.0f,
                    0.8f, 0.8f, 0.8f, 1.0f, 9.0f);
        propY += 16;

        wchar_t spdBuf[64];
        swprintf_s(spdBuf, L"速度: %.1f", ch.speed);
        r->drawText(spdBuf, 8.0f, (float)propY, (float)PANEL_WIDTH, 14.0f,
                    0.8f, 0.8f, 0.8f, 1.0f, 9.0f);
        propY += 16;

        // Collision capsule
        wchar_t capBuf[80];
        swprintf_s(capBuf, L"碰撞: RX%.0f RY%.0f", ch.capsule.radiusX, ch.capsule.radiusY);
        r->drawText(capBuf, 8.0f, (float)propY, (float)PANEL_WIDTH, 14.0f,
                    0.8f, 0.8f, 0.8f, 1.0f, 9.0f);
        propY += 16;

        // Sprite info
        if (ch.spriteSheet) {
            wchar_t sprBuf[80];
            swprintf_s(sprBuf, L"精灵: %dx%d %d帧",
                       ch.frameWidth, ch.frameHeight, ch.frameCount);
            r->drawText(sprBuf, 8.0f, (float)propY, (float)PANEL_WIDTH, 14.0f,
                        0.0f, 0.784f, 0.278f, 1.0f, 9.0f);
        }
    }

    if (layerPanel && layers) {
        int layerPanelY = windowH; // hidden, layers now horizontal in bottom bar
        layerPanel->render(r, layers, 8, layerPanelY, PANEL_WIDTH - 16);
    }
}

void Editor::renderStatusBarD2D(D2DRenderer* r) {

    // Status info is now part of renderMenuBarD2D

}


void Editor::renderMinimapD2D(D2DRenderer* r) {
    if (!r || !map) return;

    const int mmW = 150;
    const int mmH = 100;
    int canvasW = getCanvasWidth();
    int canvasH = getCanvasHeight();
    int mmX = canvasW - mmW - 10;
    int mmY = canvasH - mmH - 10;

    // Background
    r->fillRect((float)mmX, (float)mmY, (float)mmW, (float)mmH,
                0.078f, 0.078f, 0.078f, 0.85f);
    r->drawRect((float)mmX, (float)mmY, (float)mmW, (float)mmH,
                0.392f, 0.392f, 0.392f, 0.85f);

    float scaleX = (float)mmW / (map->width * TileMap::TILE_SIZE);
    float scaleY = (float)mmH / (map->height * TileMap::TILE_SIZE);
    float scale = std::min(scaleX, scaleY);

    int drawW = (int)(map->width * TileMap::TILE_SIZE * scale);
    int drawH = (int)(map->height * TileMap::TILE_SIZE * scale);
    int offX = mmX + (mmW - drawW) / 2;
    int offY = mmY + (mmH - drawH) / 2;

    // Draw tiles
    for (int ty = 0; ty < map->height; ty++) {
        for (int tx = 0; tx < map->width; tx++) {
            uint8_t id = map->layers[0][ty][tx];
            if (id == 0 || id >= map->tileDefs.size()) continue;
            DWORD c = map->tileDefs[id].color;
            int px = offX + (int)(tx * TileMap::TILE_SIZE * scale);
            int py = offY + (int)(ty * TileMap::TILE_SIZE * scale);
            int ps = std::max(1, (int)(TileMap::TILE_SIZE * scale));
            r->fillRect((float)px, (float)py, (float)ps, (float)ps,
                        toF((c >> 0) & 0xFF), toF((c >> 8) & 0xFF), toF((c >> 16) & 0xFF));
        }
    }

    // Viewport rectangle
    float vpLeft   = camera.x * scale;
    float vpTop    = camera.y * scale;
    float vpRight  = (camera.x + canvasW / camera.zoom) * scale;
    float vpBottom = (camera.y + canvasH / camera.zoom) * scale;

    r->drawRect((float)(offX + (int)vpLeft), (float)(offY + (int)vpTop),
                (float)(int)(vpRight - vpLeft), (float)(int)(vpBottom - vpTop),
                1.0f, 1.0f, 0.0f, 0.8f);
}

void Editor::renderTooltipD2D(D2DRenderer* r) {
    if (!r) return;
    if (hoverTileX < 0 || hoverTileY < 0) return;
    if (!map || !map->inBounds(hoverTileX, hoverTileY)) return;

    // Only show tooltip when mouse is in canvas area
    // tooltip shows anywhere on canvas
    if (lastMouseY >= windowH - STATUS_BAR_H) return;

    uint8_t id = map->layers[0][hoverTileY][hoverTileX];
    if (id == 0 || id >= map->tileDefs.size()) return;

    const wchar_t* name = map->tileDefs[id].name.c_str();
    wchar_t buf[128];
    swprintf_s(buf, L"%s (%d, %d)", name, hoverTileX, hoverTileY);

    int tx = lastMouseX + 16;
    int ty = lastMouseY - 24;
    int tw = 120;
    int th = 24;

    if (tx + tw > windowW) tx = lastMouseX - tw - 4;
    if (ty < 0) ty = lastMouseY + 16;

    r->fillRect((float)tx, (float)ty, (float)tw, (float)th,
                0.176f, 0.176f, 0.188f, 0.95f);
    r->drawRect((float)tx, (float)ty, (float)tw, (float)th,
                0.333f, 0.333f, 0.333f, 0.95f);
    r->drawText(buf, (float)(tx + 6), (float)(ty + 4),
                (float)(tw - 12), (float)(th - 8),
                1.0f, 1.0f, 1.0f, 1.0f, 9.0f);
}

// ---------------------------------------------------------------------------
// Input Handling
// ---------------------------------------------------------------------------

void Editor::handleInput(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN:
        onMouseDown(LOWORD(lParam), HIWORD(lParam), true);
        break;
    case WM_RBUTTONDOWN:
        onMouseDown(LOWORD(lParam), HIWORD(lParam), false);
        break;
    case WM_MBUTTONDOWN: {
        int mx = LOWORD(lParam);
        int my = HIWORD(lParam);
        isPanning = true;
        lastMouseX = mx;
        lastMouseY = my;
        break;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        onMouseUp(LOWORD(lParam), HIWORD(lParam));
        break;
    case WM_MOUSEMOVE:
        onMouseMove(LOWORD(lParam), HIWORD(lParam));
        break;
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        onMouseWheel(delta, pt.x, pt.y);
        break;
    }
    case WM_KEYDOWN:
        onKeyDown(wParam);
        break;
    case WM_KEYUP:
        onKeyUp(wParam);
        break;
    case WM_SIZE:
        windowW = LOWORD(lParam);
        windowH = HIWORD(lParam);
        if (offscreenBuffer) {
            delete offscreenBuffer;
            offscreenBuffer = new Gdiplus::Bitmap(windowW, windowH, PixelFormat32bppARGB);
        }
        break;
    }
}

void Editor::onMouseDown(int x, int y, bool left) {
    lastMouseX = x;
    lastMouseY = y;

    int barTop = windowH - STATUS_BAR_H;

    // --- Row 0: Tools (left) + Menu buttons (right) ---
    int row0Y = barTop + 4;
    if (y >= barTop && y < row0Y + TOOL_ICON_SIZE) {
        // Tool icons (left side)
        for (int i = 0; i < 5; i++) {
            int tx = 8 + i * (TOOL_ICON_SIZE + TOOL_ICON_GAP);
            if (x >= tx && x < tx + TOOL_ICON_SIZE &&
                y >= row0Y && y < row0Y + TOOL_ICON_SIZE) {
                currentTool = (Tool)i;
                return;
            }
        }
        // Menu buttons (right side)
        for (int i = 0; i < NUM_MENU_BUTTONS; i++) {
            int bx = windowW - (NUM_MENU_BUTTONS - i) * (MENU_BTN_W + MENU_BTN_GAP) - 8;
            if (x >= bx && x < bx + MENU_BTN_W &&
                y >= row0Y && y < row0Y + MENU_BTN_H) {
                switch (i) {
                case 0: {
                    int mw = 64, mh = 64;
                    if (NewMapDialog::show(hwnd, mw, mh)) {
                        newMap(mw, mh);
                    }
                    break;
                }
                case 1: {
                    if (currentFilePath[0]) {
                        saveMap(currentFilePath);
                    } else {
                        const wchar_t* filter = L"GMC Map (*.gmc)\0*.gmc\0All Files (*.*)\0*.*\0";
                        wchar_t path[MAX_PATH] = {};
                        OPENFILENAMEW ofn = {};
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFilter = filter;
                        ofn.lpstrFile = path;
                        ofn.nMaxFile = MAX_PATH;
                        ofn.lpstrDefExt = L"gmc";
                        ofn.Flags = OFN_OVERWRITEPROMPT;
                        if (GetSaveFileNameW(&ofn)) {
                            char mbPath[MAX_PATH] = {};
                            WideCharToMultiByte(CP_UTF8, 0, path, -1, mbPath, MAX_PATH, nullptr, nullptr);
                            strncpy_s(currentFilePath, mbPath, sizeof(currentFilePath));
                            saveMap(currentFilePath);
                        }
                    }
                    break;
                }
                case 2: {
                    const wchar_t* filter = L"GMC Map (*.gmc)\0*.gmc\0All Files (*.*)\0*.*\0";
                    wchar_t path[MAX_PATH] = {};
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = filter;
                    ofn.lpstrFile = path;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrDefExt = L"gmc";
                    ofn.Flags = OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        char mbPath[MAX_PATH] = {};
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, mbPath, MAX_PATH, nullptr, nullptr);
                        strncpy_s(currentFilePath, mbPath, sizeof(currentFilePath));
                        loadMap(currentFilePath);
                    }
                    break;
                }
                case 3: generateTile("fantasy terrain tile"); break;
                case 4: undo(); break;
                case 5: redo(); break;
                case 6: runGame(); break;
                }
                return;
            }
        }

        // "+ NPC" button click
        {
            int npcBtnX = windowW - (NUM_MENU_BUTTONS + 1) * (MENU_BTN_W + MENU_BTN_GAP) - 8;
            if (x >= npcBtnX && x < npcBtnX + MENU_BTN_W &&
                y >= row0Y && y < row0Y + MENU_BTN_H) {
                if (map) {
                    float spawnX = camera.x + (float)windowW / (2.0f * camera.zoom);
                    float spawnY = camera.y + (float)(windowH - STATUS_BAR_H) / (2.0f * camera.zoom);
                    wchar_t npcName[32];
                    _snwprintf(npcName, 32, L"NPC_%d", (int)map->characters.size());
                    int newId = map->createCharacter(npcName, L"npc", spawnX, spawnY, 0);
                    selectedCharacterIdx = (int)map->characters.size() - 1;
                    if (d2d) map->convertCharacterSpritesToD2D(d2d);
                    snprintf(gui.statusText, sizeof(gui.statusText), "NPC created: id=%d", newId);
                }
                return;
            }
        }
        // Clicked row 0 but hit nothing - consume
        return;
    }

    // --- Row 1: Layer buttons ---
    int row1Y = row0Y + TOOL_ICON_SIZE + 6;
    int layerBtnW = 64;
    int layerBtnH = 20;
    if (y >= row1Y && y < row1Y + layerBtnH) {
        if (layers) {
            for (int i = 0; i < LayerManager::NUM_LAYERS; i++) {
                int bx = 8 + i * (layerBtnW + 3);
                if (x >= bx && x < bx + layerBtnW &&
                    y >= row1Y && y < row1Y + layerBtnH) {
                    layers->activeLayer = i;
                    return;
                }
            }
        }
        return;
    }

    // --- Row 2: Tile thumbnails + add/delete buttons ---
    int tileRowY = row1Y + 24;
    if (y >= tileRowY && y < tileRowY + TILE_BTN_H + 4) {
        int numTiles = tileCount();
        // Check tile thumbnail clicks
        for (int i = 1; i < numTiles; i++) {
            int tx = (i - 1 - tileScrollOffset) * (TILE_BTN_H + TILE_BTN_GAP) + 4;
            if (x >= tx && x < tx + TILE_BTN_H && y >= tileRowY && y < tileRowY + TILE_BTN_H) {
                selectedTileId = i;
                return;
            }
        }
        // Check + add tile button
        {
            int addBtnX = numTiles > 1 ?
                (numTiles - 1 - tileScrollOffset) * (TILE_BTN_H + TILE_BTN_GAP) + 4 + TILE_BTN_H + 8 : 4;
            if (x >= addBtnX && x < addBtnX + 30 && y >= tileRowY && y < tileRowY + TILE_BTN_H) {
                const wchar_t* filter = L"Image Files (*.png;*.jpg;*.bmp)\0*.png;*.jpg;*.bmp\0All Files (*.*)\0*.*\0";
                wchar_t path[MAX_PATH] = {};
                OPENFILENAMEW ofn = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = filter;
                ofn.lpstrFile = path;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    Gdiplus::Bitmap* fullBmp = loadBmpFromFile(path);
                    if (fullBmp && map) {
                        int imgW = fullBmp->GetWidth();
                        int imgH = fullBmp->GetHeight();
                        if (imgW > TileMap::TILE_SIZE || imgH > TileMap::TILE_SIZE) {
                            int cols = (imgW + TileMap::TILE_SIZE - 1) / TileMap::TILE_SIZE;
                            int rows = (imgH + TileMap::TILE_SIZE - 1) / TileMap::TILE_SIZE;
                            // Store the full source image in a single tile definition.
                            // Cell (0,0) is used as the palette thumbnail.
                            // When painting, handleBrush auto-fills the rest of the grid.
                            Gdiplus::Bitmap cellBmp(TileMap::TILE_SIZE, TileMap::TILE_SIZE, PixelFormat32bppARGB);
                            {
                                Gdiplus::Graphics g2(&cellBmp);
                                int copyW = std::min(TileMap::TILE_SIZE, imgW);
                                int copyH = std::min(TileMap::TILE_SIZE, imgH);
                                g2.DrawImage(fullBmp, Gdiplus::Rect(0, 0, copyW, copyH), 0, 0, copyW, copyH, Gdiplus::UnitPixel);
                            }
                            wchar_t tileName[64];
                            _snwprintf(tileName, 64, L"tile_%d", map->tileDefs.size());
                            int newId = map->addTileDef(tileName, 0xFFAAAAAA, 1, false);
                            // Store cell (0,0) as the palette texture
                            Gdiplus::Bitmap* cellClone = cellBmp.Clone(0, 0, TileMap::TILE_SIZE, TileMap::TILE_SIZE, PixelFormat32bppARGB);
                            map->setCustomTexture((uint8_t)newId, cellClone);
                            // Store the full source image + grid info in TileDef
                            map->tileDefs[newId].sourceImage = fullBmp->Clone(0, 0, imgW, imgH, PixelFormat32bppARGB);
                            map->tileDefs[newId].gridCols = cols;
                            map->tileDefs[newId].gridRows = rows;
                            if (d2d) map->convertTexturesToD2D(d2d);
                            strncpy_s(gui.statusText, "Grid tile imported (auto-fill on paint)", sizeof(gui.statusText));
                            fullBmp = nullptr; // ownership transferred to TileDef, don't delete below
                        } else {
                            wchar_t tileName[64];
                            _snwprintf(tileName, 64, L"tile_%d", map->tileDefs.size());
                            int newId = map->addTileDef(tileName, 0xFFAAAAAA, 1, false);
                            Gdiplus::Bitmap* clone = fullBmp->Clone(0, 0, imgW, imgH, PixelFormat32bppARGB);
                            map->setCustomTexture((uint8_t)newId, clone);
                            if (d2d) map->convertTexturesToD2D(d2d);
                            selectedTileId = newId;
                        }
                        delete fullBmp;
                    }
                }
                return;
            }
        }
        // Check - delete tile button
        {
            int addBtnX = numTiles > 1 ?
                (numTiles - 1 - tileScrollOffset) * (TILE_BTN_H + TILE_BTN_GAP) + 4 + TILE_BTN_H + 8 : 4;
            int delBtnX = addBtnX + 34;
            if (x >= delBtnX && x < delBtnX + 30 && y >= tileRowY && y < tileRowY + TILE_BTN_H) {
                if (map && selectedTileId > 0 && selectedTileId < (int)map->tileDefs.size()) {
                    map->tileDefs.erase(map->tileDefs.begin() + selectedTileId);
                    if (d2d) map->convertTexturesToD2D(d2d);
                    selectedTileId = std::min(selectedTileId, (int)map->tileDefs.size() - 1);
                    if (selectedTileId < 1) selectedTileId = 1;
                }
                return;
            }
        }
        return;
    }

    // --- Row 2.5: Brush size controls ---
    {
        int brushRowY = tileRowY + TILE_BTN_H + 4;
        if (y >= brushRowY && y < brushRowY + 18) {
            // "-" button
            int minusX = windowW - 120;
            if (x >= minusX && x < minusX + 24) {
                if (brushSize > 1) brushSize--;
                return;
            }
            // "+" button
            int plusX = windowW - 90;
            if (x >= plusX && x < plusX + 24) {
                if (brushSize < 10) brushSize++;
                return;
            }
            // Slider track
            int sliderX2 = 80;
            int sliderW2 = windowW - 200;
            if (x >= sliderX2 && x < sliderX2 + sliderW2) {
                float ratio = (float)(x - sliderX2) / (float)sliderW2;
                brushSize = (int)(ratio * 9.0f + 0.5f) + 1;
                if (brushSize < 1) brushSize = 1;
                if (brushSize > 10) brushSize = 10;
                isDraggingSlider = true;
                return;
            }
            return;
        }
    }


    // --- Character section: list click + Add NPC button ---
    {
        int numTiles = tileCount();
        int visibleTiles = numTiles - 1 - tileScrollOffset;
        int charSectionY = TILE_START_Y + visibleTiles * (TILE_BTN_H + TILE_BTN_GAP) + 100;
        charSectionY += 20; // after label

        // Character list click
        if (map) {
            int maxChars = std::min((int)map->characters.size(), 4);
            for (int i = 0; i < maxChars; i++) {
                int cy = charSectionY + i * 22;
                if (x >= TILE_BTN_X && x < TILE_BTN_X + TILE_BTN_W && y >= cy && y < cy + 20) {
                    selectedCharacterIdx = i;
                    return;
                }
            }
            charSectionY += maxChars * 22;
        }

        // "Add NPC" button click (charSectionY + 52)
        {
            int btnY = charSectionY + 52;
            if (x >= TILE_BTN_X && x < TILE_BTN_X + TILE_BTN_W && y >= btnY && y < btnY + 22) {
                if (map) {
                    float spawnX = camera.x + (float)windowW / (2.0f * camera.zoom);
                    float spawnY = camera.y + (float)windowH / (2.0f * camera.zoom);
                    wchar_t npcName[32];
                    _snwprintf(npcName, 32, L"NPC_%d", (int)map->characters.size());
                    int newId = map->createCharacter(npcName, L"npc", spawnX, spawnY, 0);
                    selectedCharacterIdx = (int)map->characters.size() - 1;
                    if (d2d) map->convertCharacterSpritesToD2D(d2d);
                    snprintf(gui.statusText, sizeof(gui.statusText), "NPC created: id=%d", newId);
                }
                return;
            }
        }
    }

    // --- Canvas area (everything above the bottom bar) ---
    if (y < barTop) {
        if (left) {
            if (currentTool == PAN) {
                isPanning = true;
            } else if (currentTool == SELECT) {
                float wx, wy;
                camera.screenToWorld((float)x, (float)y, wx, wy);
                if (map) {
                    Character* hit = map->getCharacterAt(wx, wy, TileMap::TILE_SIZE);
                    if (hit) {
                        for (int i = 0; i < (int)map->characters.size(); i++) {
                            if (map->characters[i].id == hit->id) {
                                selectedCharacterIdx = i;
                                break;
                            }
                        }
                    }
                }
            } else {
                isPainting = true;
                pushUndo();
                float wx, wy;
                camera.screenToWorld((float)x, (float)y, wx, wy);
                int tileX = (int)(wx / TileMap::TILE_SIZE);
                int tileY = (int)(wy / TileMap::TILE_SIZE);
                // For grid tiles: use permanent anchor per tileId
                // (first click fixes the anchor, all subsequent strokes use it)
                if (map && selectedTileId > 0 && selectedTileId < (int)map->tileDefs.size() &&
                    map->tileDefs[selectedTileId].sourceImage &&
                    map->tileDefs[selectedTileId].gridCols > 0) {
                    auto it = tileAnchors.find(selectedTileId);
                    if (it == tileAnchors.end()) {
                        // First time using this tile — lock the anchor
                        tileAnchors[selectedTileId] = {tileX, tileY};
                    }
                    paintAnchorX = it != tileAnchors.end() ? it->second.first : tileX;
                    paintAnchorY = it != tileAnchors.end() ? it->second.second : tileY;
                } else {
                    paintAnchorX = tileX;
                    paintAnchorY = tileY;
                }
                if (currentTool == BRUSH) handleBrush(tileX, tileY);
                else if (currentTool == ERASER) handleEraser(tileX, tileY);
                else if (currentTool == FILL) handleFill(tileX, tileY);
            }
        } else {
            isPanning = true;
        }
        isDragging = true;
    }
}
void Editor::onMouseUp(int x, int y) {
    isPainting = false;
    isPanning = false;
    isDragging = false;
    isDraggingSlider = false;
}

void Editor::onMouseMove(int x, int y) {
    int barTop = windowH - STATUS_BAR_H;
    int dx = x - lastMouseX;
    int dy = y - lastMouseY;
    lastMouseX = x;
    lastMouseY = y;

    // Update hover flags
    gui.panelHovered = (y >= windowH - STATUS_BAR_H);
    gui.menuHovered  = (x < PANEL_WIDTH && y >= windowH - STATUS_BAR_H - 36);

    // Handle brush size slider drag
    if (isDraggingSlider) {
        int sliderX2 = 80;
        int sliderW2 = windowW - 200;
        float ratio = (float)(x - sliderX2) / (float)sliderW2;
        brushSize = (int)(ratio * 9.0f + 0.5f) + 1;
        if (brushSize < 1) brushSize = 1;
        if (brushSize > 10) brushSize = 10;
    }

    if (layerPanel && layers) {
        int layerPanelY = windowH; // hidden, layers now horizontal in bottom bar
        // Layer panel moved to bottom bar (horizontal)
    }

    // Panning
    if (isPanning) {
        camera.x -= dx / camera.zoom;
        camera.y -= dy / camera.zoom;
        return;
    }

    // Update hover tile
    if (y < windowH - STATUS_BAR_H) {
        int canvasX = x;
        int canvasY = y;
        float wx, wy;
        camera.screenToWorld((float)canvasX, (float)canvasY, wx, wy);
        hoverTileX = (int)(wx / TileMap::TILE_SIZE);
        hoverTileY = (int)(wy / TileMap::TILE_SIZE);

        // Painting while dragging
        if (isPainting && map) {
            int tileX = (int)(wx / TileMap::TILE_SIZE);
            int tileY = (int)(wy / TileMap::TILE_SIZE);
            if (currentTool == BRUSH) handleBrush(tileX, tileY);
            else if (currentTool == ERASER) handleEraser(tileX, tileY);
        }
    } else {
        hoverTileX = -1;
        hoverTileY = -1;
    }
}

void Editor::onMouseWheel(int delta, int x, int y) {
    // Scroll tile palette when in bottom bar area
    if (y >= windowH - STATUS_BAR_H) {
        int numTiles = tileCount();
        int maxScroll = std::max(0, numTiles - 8);
        if (delta > 0)
            tileScrollOffset = std::max(0, tileScrollOffset - 1);
        else
            tileScrollOffset = std::min(maxScroll, tileScrollOffset + 1);
        return;
    }

    // Only zoom when in canvas area
    if (y >= windowH - STATUS_BAR_H) return;

    float oldZoom = camera.zoom;
    float zoomFactor = (delta > 0) ? 1.15f : (1.0f / 1.15f);
    camera.zoom *= zoomFactor;
    camera.zoom = std::max(camera.minZoom, std::min(camera.maxZoom, camera.zoom));

    // Zoom towards mouse position
    int canvasX = x;
    int canvasY = y;

    float wx, wy;
    camera.screenToWorld((float)canvasX, (float)canvasY, wx, wy);

    // After zoom change, adjust camera so the world point under the mouse stays
    float newWX, newWY;
    camera.screenToWorld((float)canvasX, (float)canvasY, newWX, newWY);

    camera.x += (wx - newWX);
    camera.y += (wy - newWY);
}

void Editor::onKeyDown(WPARAM key) {
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    switch (key) {
    case 'B': currentTool = BRUSH; break;
    case 'E': currentTool = ERASER; break;
    case 'S':
        if (ctrl) {
            if (currentFilePath[0]) {
                saveMap(currentFilePath);
            }
        } else {
            currentTool = SELECT;
        }
        break;
    case 'P': currentTool = PAN; break;
    case 'F': currentTool = FILL; break;
    case 'G': gui.showGrid = !gui.showGrid; break;
    case 'Z':
        if (ctrl) undo();
        break;
    case 'Y':
        if (ctrl) redo();
        break;
    case VK_DELETE:
        if (selectedCharacterIdx >= 0 && map &&
            selectedCharacterIdx < (int)map->characters.size()) {
            pushUndo();
            int charId = map->characters[selectedCharacterIdx].id;
            map->deleteCharacter(charId);
            if (d2d) map->convertCharacterSpritesToD2D(d2d);
            selectedCharacterIdx = -1;
            strncpy_s(gui.statusText, "\u5df2\u5220\u9664\u89d2\u8272", sizeof(gui.statusText));
        }
        break;
    case VK_F5:
        runGame();
        break;
    }
}

void Editor::onKeyUp(WPARAM key) {
    // Nothing needed currently
}

// ---------------------------------------------------------------------------
// Tool Handlers
// ---------------------------------------------------------------------------

void Editor::handleBrush(int worldX, int worldY) {
    if (!map) return;

    int activeLayer = layers ? layers->activeLayer : 0;

    // For grid tiles (imported images), route to the appropriate draw layer:
    // terrain(0) -> draw(1), foreground(4) -> fg_draw(5), others stay on active
    bool hasGrid = (selectedTileId > 0 && selectedTileId < (int)map->tileDefs.size() &&
                    map->tileDefs[selectedTileId].sourceImage != nullptr &&
                    map->tileDefs[selectedTileId].gridCols > 0);
    int drawLayer = activeLayer;
    if (hasGrid) {
        if (activeLayer == LayerManager::LAYER_TERRAIN_VIS)
            drawLayer = LayerManager::LAYER_DRAW;
        else if (activeLayer == LayerManager::LAYER_FOREGROUND)
            drawLayer = LayerManager::LAYER_FG_DRAW;
    }

    // Terrain collision layer: toggle solid on/off
    if (activeLayer == LayerManager::LAYER_TERRAIN_COL) {
        int half = brushSize / 2;
        for (int dy = -half; dy <= half; dy++) {
            for (int dx = -half; dx <= half; dx++) {
                int tx = worldX + dx;
                int ty = worldY + dy;
                if (map->inBounds(tx, ty)) {
                    map->setTerrainCollision(tx, ty, true);
                }
            }
        }
        return;
    }

    int half = brushSize / 2;
    for (int dy = -half; dy <= half; dy++) {
        for (int dx = -half; dx <= half; dx++) {
            int tx = worldX + dx;
            int ty = worldY + dy;
            if (map->inBounds(tx, ty)) {
                map->setTile(hasGrid ? drawLayer : activeLayer, tx, ty, (uint8_t)selectedTileId);
                if (hasGrid) {
                    int gridX = tx - paintAnchorX;
                    int gridY = ty - paintAnchorY;
                    map->setGridOffset(tx, ty, gridX, gridY);
                }
            }
        }
    }
}

void Editor::handleEraser(int worldX, int worldY) {
    if (!map) return;

    int activeLayer = layers ? layers->activeLayer : 0;

    // Terrain collision layer: clear solid
    if (activeLayer == LayerManager::LAYER_TERRAIN_COL) {
        int half = brushSize / 2;
        for (int dy = -half; dy <= half; dy++) {
            for (int dx = -half; dx <= half; dx++) {
                int tx = worldX + dx;
                int ty = worldY + dy;
                if (map->inBounds(tx, ty)) {
                    map->setTerrainCollision(tx, ty, false);
                }
            }
        }
        return;
    }

    int half = brushSize / 2;
    for (int dy = -half; dy <= half; dy++) {
        for (int dx = -half; dx <= half; dx++) {
            int tx = worldX + dx;
            int ty = worldY + dy;
            if (map->inBounds(tx, ty)) {
                map->setTile(activeLayer, tx, ty, 0);
            }
        }
    }
}

void Editor::handleFill(int worldX, int worldY) {
    if (!map) return;
    map->floodFill(layers ? layers->activeLayer : 0,
                   worldX, worldY, (uint8_t)selectedTileId);
}

// ---------------------------------------------------------------------------
// Map Operations
// ---------------------------------------------------------------------------

void Editor::newMap(int w, int h) {
    if (map) {
        delete map;
        map = nullptr;
    }
    map = new TileMap();
    map->width = w;
    map->height = h;

    // Re-initialize layers
    for (int i = 0; i < TileMap::NUM_LAYERS; i++) {
        map->layers[i].resize(h);
        for (int y = 0; y < h; y++)
            map->layers[i][y].assign(w, 0);
    }
    map->terrainCollision.resize(h);
    for (int y = 0; y < h; y++)
        map->terrainCollision[y].assign(w, {});

    map->init();
    initTileTextures();

    if (d2d) map->convertTexturesToD2D(d2d);
    if (d2d) map->convertCharacterSpritesToD2D(d2d);

    // Center camera on player character
    float camCenterX = (float)(w * TileMap::TILE_SIZE) / 2.0f;
    float camCenterY = (float)(h * TileMap::TILE_SIZE) / 2.0f;
    for (auto& c : map->characters) {
        if (c.type == L"player") {
            camCenterX = c.worldX;
            camCenterY = c.worldY;
            break;
        }
    }
    camera.x = camCenterX - (float)windowW / 2.0f / camera.zoom;
    camera.y = camCenterY - (float)(windowH - STATUS_BAR_H) / 2.0f / camera.zoom;

    // Clear undo/redo
    undoStack.clear();
    redoStack.clear();

    currentFilePath[0] = '\0';

    strncpy_s(gui.statusText, "\u65b0\u5efa\u5730\u56fe", sizeof(gui.statusText));
}

void Editor::saveMap(const char* path) {
    if (!map) return;

    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    if (!f) {
        strncpy_s(gui.statusText, "\u4fdd\u5b58\u5931\u8d25", sizeof(gui.statusText));
        return;
    }

    // Magic
    fwrite("GMAP", 1, 4, f);

    // Version
    uint32_t version = 6;
    fwrite(&version, sizeof(uint32_t), 1, f);

    // Dimensions
    uint32_t w = (uint32_t)map->width;
    uint32_t h = (uint32_t)map->height;
    fwrite(&w, sizeof(uint32_t), 1, f);
    fwrite(&h, sizeof(uint32_t), 1, f);

    // Tile size
    uint32_t ts = (uint32_t)TileMap::TILE_SIZE;
    fwrite(&ts, sizeof(uint32_t), 1, f);

    // Number of layers
    uint32_t numLayers = TileMap::NUM_LAYERS;
    fwrite(&numLayers, sizeof(uint32_t), 1, f);

    // Layer data
    for (uint32_t layer = 0; layer < numLayers; layer++) {
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint8_t id = map->layers[layer][y][x];
                fwrite(&id, sizeof(uint8_t), 1, f);
            }
        }
    }

    // Terrain collision data
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t solid = map->terrainCollision[y][x].solid ? 1 : 0;
            fwrite(&solid, sizeof(uint8_t), 1, f);
        }
    }

    // Characters
    uint32_t numChars = (uint32_t)map->characters.size();
    fwrite(&numChars, sizeof(uint32_t), 1, f);

    for (const auto& c : map->characters) {
        fwrite(&c.id, sizeof(int), 1, f);

        uint32_t nameLen = (uint32_t)c.name.size();
        fwrite(&nameLen, sizeof(uint32_t), 1, f);
        if (nameLen > 0) fwrite(c.name.data(), sizeof(wchar_t), nameLen, f);

        uint32_t typeLen = (uint32_t)c.type.size();
        fwrite(&typeLen, sizeof(uint32_t), 1, f);
        if (typeLen > 0) fwrite(c.type.data(), sizeof(wchar_t), typeLen, f);

        fwrite(&c.worldX, sizeof(float), 1, f);
        fwrite(&c.worldY, sizeof(float), 1, f);
        fwrite(&c.tileId, sizeof(uint8_t), 1, f);
        fwrite(&c.capsule.radiusX, sizeof(float), 1, f);
        fwrite(&c.capsule.radiusY, sizeof(float), 1, f);
        fwrite(&c.capsule.radiusXv, sizeof(float), 1, f);
        fwrite(&c.capsule.radiusYv, sizeof(float), 1, f);
        fwrite(&c.capsule.offsetY, sizeof(float), 1, f);
        fwrite(&c.hp, sizeof(int), 1, f);
        fwrite(&c.defense, sizeof(int), 1, f);
        fwrite(&c.speed, sizeof(float), 1, f);

        uint32_t scriptLen = (uint32_t)c.script.size();
        fwrite(&scriptLen, sizeof(uint32_t), 1, f);
        if (scriptLen > 0) fwrite(c.script.data(), sizeof(wchar_t), scriptLen, f);

        // Sprite data
        fwrite(&c.spriteId, sizeof(int), 1, f);
        fwrite(&c.frameCount, sizeof(int), 1, f);
        fwrite(&c.frameWidth, sizeof(int), 1, f);
        fwrite(&c.frameHeight, sizeof(int), 1, f);
        fwrite(&c.animFps, sizeof(int), 1, f);
        fwrite(&c.animLoop, sizeof(bool), 1, f);
        fwrite(&c.spriteScale, sizeof(float), 1, f);
        fwrite(&c.spriteCols, sizeof(int), 1, f);
        fwrite(&c.spriteRows, sizeof(int), 1, f);
        fwrite(&c.facingDir, sizeof(int), 1, f);
    }

    // Editor camera
    fwrite(&camera.x, sizeof(float), 1, f);
    fwrite(&camera.y, sizeof(float), 1, f);
    fwrite(&camera.zoom, sizeof(float), 1, f);

    // Layer render order
    if (layers) {
        fwrite(layers->renderOrder, sizeof(int), LayerManager::NUM_LAYERS, f);
    }

    fclose(f);
    strncpy_s(gui.statusText, "\u4fdd\u5b58\u6210\u529f", sizeof(gui.statusText));
}

void Editor::loadMap(const char* path) {
    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) {
        strncpy_s(gui.statusText, "\u8bfb\u53d6\u5931\u8d25", sizeof(gui.statusText));
        return;
    }

    // Magic
    char magic[4] = {};
    fread(magic, 1, 4, f);
    if (memcmp(magic, "GMAP", 4) != 0) {
        fclose(f);
        strncpy_s(gui.statusText, "\u6587\u4ef6\u683c\u5f0f\u9519\u8bef", sizeof(gui.statusText));
        return;
    }

    // Version
    uint32_t version = 0;
    fread(&version, sizeof(uint32_t), 1, f);
    if (version > 6) {
        fclose(f);
        strncpy_s(gui.statusText, "\u4e0d\u652f\u6301\u7684\u7248\u672c", sizeof(gui.statusText));
        return;
    }

    // Dimensions
    uint32_t w = 0, h = 0;
    fread(&w, sizeof(uint32_t), 1, f);
    fread(&h, sizeof(uint32_t), 1, f);

    // Tile size (skip)
    uint32_t ts = 0;
    fread(&ts, sizeof(uint32_t), 1, f);

    // Number of layers
    uint32_t numLayers = 0;
    fread(&numLayers, sizeof(uint32_t), 1, f);

    // Create new map
    if (map) delete map;
    map = new TileMap();
    map->width = (int)w;
    map->height = (int)h;

    for (uint32_t i = 0; i < TileMap::NUM_LAYERS; i++) {
        map->layers[i].resize(h);
        for (uint32_t y = 0; y < h; y++)
            map->layers[i][y].resize(w, 0);
    }
    map->terrainCollision.resize(h);
    for (uint32_t y = 0; y < h; y++)
        map->terrainCollision[y].assign(w, {});

    // Read layer data
    uint32_t layersToRead = std::min(numLayers, (uint32_t)TileMap::NUM_LAYERS);
    for (uint32_t layer = 0; layer < layersToRead; layer++) {
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint8_t id = 0;
                fread(&id, sizeof(uint8_t), 1, f);
                map->layers[layer][y][x] = id;
            }
        }
    }
    // Skip extra layers if file has more
    for (uint32_t layer = layersToRead; layer < numLayers; layer++) {
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                uint8_t dummy;
                fread(&dummy, sizeof(uint8_t), 1, f);
            }
    }

    // Read terrain collision data
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint8_t solid = 0;
            fread(&solid, sizeof(uint8_t), 1, f);
            map->terrainCollision[y][x].solid = (solid != 0);
        }
    }

    // Read characters
    uint32_t numChars = 0;
    fread(&numChars, sizeof(uint32_t), 1, f);

    map->characters.clear();
    map->nextCharacterId = 1;
    for (uint32_t i = 0; i < numChars; i++) {
        Character c = {};

        fread(&c.id, sizeof(int), 1, f);

        uint32_t nameLen = 0;
        fread(&nameLen, sizeof(uint32_t), 1, f);
        if (nameLen > 0) {
            c.name.resize(nameLen);
            fread(c.name.data(), sizeof(wchar_t), nameLen, f);
        }

        uint32_t typeLen = 0;
        fread(&typeLen, sizeof(uint32_t), 1, f);
        if (typeLen > 0) {
            c.type.resize(typeLen);
            fread(c.type.data(), sizeof(wchar_t), typeLen, f);
        }

        fread(&c.worldX, sizeof(float), 1, f);
        fread(&c.worldY, sizeof(float), 1, f);
        fread(&c.tileId, sizeof(uint8_t), 1, f);
        fread(&c.capsule.radiusX, sizeof(float), 1, f);
        fread(&c.capsule.radiusY, sizeof(float), 1, f);
        fread(&c.capsule.radiusXv, sizeof(float), 1, f);
        fread(&c.capsule.radiusYv, sizeof(float), 1, f);
        fread(&c.capsule.offsetY, sizeof(float), 1, f);
        fread(&c.hp, sizeof(int), 1, f);
        fread(&c.attack, sizeof(int), 1, f);
        fread(&c.defense, sizeof(int), 1, f);
        fread(&c.speed, sizeof(float), 1, f);

        uint32_t scriptLen = 0;
        fread(&scriptLen, sizeof(uint32_t), 1, f);
        if (scriptLen > 0) {
            c.script.resize(scriptLen);
            fread(c.script.data(), sizeof(wchar_t), scriptLen, f);
        }

        // Sprite data (version 3+)
        if (version >= 3) {
            fread(&c.spriteId, sizeof(int), 1, f);
            fread(&c.frameCount, sizeof(int), 1, f);
            fread(&c.frameWidth, sizeof(int), 1, f);
            fread(&c.frameHeight, sizeof(int), 1, f);
            fread(&c.animFps, sizeof(int), 1, f);
            fread(&c.animLoop, sizeof(bool), 1, f);
            fread(&c.spriteScale, sizeof(float), 1, f);

            // Grid sprite data (version 4+)
            if (version >= 4) {
                fread(&c.spriteCols, sizeof(int), 1, f);
                fread(&c.spriteRows, sizeof(int), 1, f);
                fread(&c.facingDir, sizeof(int), 1, f);
            } else {
                // Auto-detect for old saves
                if (c.spriteSheet) {
                    int sheetW = (int)c.spriteSheet->GetWidth();
                    int sheetH = (int)c.spriteSheet->GetHeight();
                    c.spriteCols = (c.frameWidth > 0) ? sheetW / c.frameWidth : c.frameCount;
                    c.spriteRows = (c.frameHeight > 0) ? sheetH / c.frameHeight : 1;
                } else {
                    c.spriteCols = c.frameCount;
                    c.spriteRows = 1;
                }
            }

            // Load sprite sheet from AssetDatabase if available
            if (c.spriteId >= 0 && assetDb) {
                Gdiplus::Bitmap* spriteBmp = assetDb->loadSpriteBitmap(c.spriteId);
                if (spriteBmp) {
                    c.spriteSheet = spriteBmp;
                }
            }
        }

        if (c.id >= map->nextCharacterId)
            map->nextCharacterId = c.id + 1;

        map->characters.push_back(c);
    }

    // Editor camera (version 5+)
    if (version >= 5) {
        fread(&camera.x, sizeof(float), 1, f);
        fread(&camera.y, sizeof(float), 1, f);
        fread(&camera.zoom, sizeof(float), 1, f);

        if (layers && version >= 6) {
            fread(layers->renderOrder, sizeof(int), LayerManager::NUM_LAYERS, f);
        }
    } else {
        camera.x = (float)(map->width * TileMap::TILE_SIZE) / 2.0f - (float)(windowW - PANEL_WIDTH) / 2.0f;
        camera.y = (float)(map->height * TileMap::TILE_SIZE) / 2.0f - (float)(windowH - STATUS_BAR_H) / 2.0f;
        camera.zoom = 1.0f;
    }

    fclose(f);

    initTileTextures();

    if (d2d) map->convertTexturesToD2D(d2d);
    if (d2d) map->convertCharacterSpritesToD2D(d2d);

    // Reset camera
    camera.x = 0.0f;
    camera.y = 0.0f;
    camera.zoom = 1.0f;

    // Clear undo/redo
    undoStack.clear();
    redoStack.clear();

    strncpy_s(gui.statusText, "\u8bfb\u53d6\u6210\u529f", sizeof(gui.statusText));
}

// ---------------------------------------------------------------------------
// Run Game Window
// ---------------------------------------------------------------------------


// Game window state
struct GameWindowState {
    HWND hwnd = nullptr;
    D2DRenderer* d2d = nullptr;
    TileMap* map = nullptr;
    Camera camera;
    bool keys[256] = {};
    bool running = true;
    float playerX, playerY;
    float playerSpeed = 200.0f;
    LARGE_INTEGER lastTime;
    LARGE_INTEGER freq;
    std::unordered_map<uint8_t, ID2D1Bitmap*> gameTextures; // separate D2D textures for game window
    std::unordered_map<int, ID2D1Bitmap*> gameCharSprites;  // charId -> D2D sprite sheet for game window

    // Selected NPC for API control
    int selectedNpcId = -1;

    // NPC API direction flags
    volatile bool apiNpcUp = false;
    volatile bool apiNpcDown = false;
    volatile bool apiNpcLeft = false;
    volatile bool apiNpcRight = false;
};

static GameWindowState* g_gameWnd = nullptr;

static LRESULT CALLBACK GameWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GameWindowState* gs = g_gameWnd;
    if (!gs) return DefWindowProcW(hWnd, msg, wParam, lParam);

    switch (msg) {
    case WM_KEYDOWN:
        if (wParam < 256) gs->keys[wParam] = true;
        if (wParam == VK_ESCAPE) {
            gs->running = false;
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
        }
        return 0;
    case WM_KEYUP:
        if (wParam < 256) gs->keys[wParam] = false;
        return 0;
    case WM_CLOSE:
        gs->running = false;
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        gs->running = false;
        gs->hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void Editor::initTileTextures() {
    if (!map || !assetDb) return;

    struct TileDef { const char* file; };
    TileDef defs[] = {
        {"grass"}, {"sand"}, {"water"}, {"stone"},
        {"dirt"}, {"lava"}, {"snow"}, {"tree"}
    };

    for (int i = 1; i < (int)map->tileDefs.size() && i <= 8; i++) {
        Gdiplus::Bitmap* tileBmp = nullptr;

        if (!assetsDir.empty()) {
            wchar_t pngPath[MAX_PATH] = {};
            wchar_t tileFileW[64] = {};
            MultiByteToWideChar(CP_UTF8, 0, defs[i - 1].file, -1, tileFileW, 64);
            wcscpy_s(pngPath, assetsDir.c_str());
            wcscat_s(pngPath, tileFileW);
            wcscat_s(pngPath, L".png");

            tileBmp = new Gdiplus::Bitmap(pngPath);
            if (tileBmp->GetLastStatus() != Gdiplus::Ok) {
                delete tileBmp;
                tileBmp = nullptr;
            }
        }

        if (!tileBmp) {
            tileBmp = TileGenerator::generateTile(i - 1);
        }
        if (tileBmp) {
            map->setCustomTexture((uint8_t)i, tileBmp);
        }
    }
}

#include "http_api.h"

void Editor::runGame() {
    if (g_gameWnd && g_gameWnd->hwnd && IsWindow(g_gameWnd->hwnd)) {
        SetForegroundWindow(g_gameWnd->hwnd);
        return;
    }

    // Register game window class
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = GameWndProc;
        wc.hInstance = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = L"GameRunWnd";
        RegisterClassExW(&wc);
        registered = true;
    }

    // Create 720p window (1280x720 client area)
    RECT rc = { 0, 0, 1280, 720 };
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRectEx(&rc, style, FALSE, 0);

    g_gameWnd = new GameWindowState();
    g_gameWnd->map = map;

    // Find player character
    Character* player = nullptr;
    for (auto& c : map->characters) {
        if (c.type == L"player") { player = &c; break; }
    }

    // Save player position before game starts
    float savedWorldX = 0, savedWorldY = 0;
    float savedCamX = camera.x, savedCamY = camera.y, savedCamZoom = camera.zoom;
    if (player) {
        savedWorldX = player->worldX;
        savedWorldY = player->worldY;
    }

    auto findSafeSpawn = [&]() -> std::pair<float,float> {
        int cx = map->width / 2;
        int cy = map->height / 2;
        for (int r = 0; r < std::max(map->width, map->height); r++) {
            for (int dy = -r; dy <= r; dy++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (abs(dx) != r && abs(dy) != r) continue;
                    int tx = cx + dx, ty = cy + dy;
                    if (map->inBounds(tx, ty) && !map->isTerrainSolid(tx, ty)) {
                        return {(tx + 0.5f) * TileMap::TILE_SIZE, (ty + 0.5f) * TileMap::TILE_SIZE};
                    }
                }
            }
        }
        return {map->width * TileMap::TILE_SIZE * 0.5f, map->height * TileMap::TILE_SIZE * 0.5f};
    };

    if (player) {
        int tx = (int)(player->worldX / TileMap::TILE_SIZE);
        int ty = (int)(player->worldY / TileMap::TILE_SIZE);
        if (map->inBounds(tx, ty) && map->isTerrainSolid(tx, ty)) {
            auto [sx, sy] = findSafeSpawn();
            player->worldX = sx;
            player->worldY = sy;
            savedWorldX = sx;
            savedWorldY = sy;
        }
        g_gameWnd->playerX = player->worldX;
        g_gameWnd->playerY = player->worldY;
    } else {
        auto [sx, sy] = findSafeSpawn();
        g_gameWnd->playerX = sx;
        g_gameWnd->playerY = sy;
    }

    // Game camera: match editor's world-center and zoom
    g_gameWnd->camera.zoom = camera.zoom;
    float edCanvasW = (float)windowW;
    float edCanvasH = (float)(windowH - STATUS_BAR_H);
    g_gameWnd->camera.x = camera.x + (edCanvasW - 1280.0f) / (2.0f * camera.zoom);
    g_gameWnd->camera.y = camera.y + (edCanvasH - 720.0f) / (2.0f * camera.zoom);

    QueryPerformanceFrequency(&g_gameWnd->freq);
    QueryPerformanceCounter(&g_gameWnd->lastTime);

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    g_gameWnd->hwnd = CreateWindowExW(
        0, L"GameRunWnd", L"\u6e38\u620f\u8fd0\u884c - GameMaker C++",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr);

    if (!g_gameWnd->hwnd) {
        delete g_gameWnd;
        g_gameWnd = nullptr;
        return;
    }

    // Create D2D renderer for game window
    g_gameWnd->d2d = new D2DRenderer();
    if (!g_gameWnd->d2d->init(g_gameWnd->hwnd)) {
        delete g_gameWnd->d2d;
        delete g_gameWnd;
        g_gameWnd = nullptr;
        return;
    }

    // Convert textures for game window's own D2D renderer (separate texture map)
    map->convertTexturesToD2D(g_gameWnd->d2d, &g_gameWnd->gameTextures);
    // Convert character sprites for game window
    map->convertCharacterSpritesToD2D(g_gameWnd->d2d, &g_gameWnd->gameCharSprites);

    ShowWindow(g_gameWnd->hwnd, SW_SHOW);
    UpdateWindow(g_gameWnd->hwnd);

    HttpApi_Start();

    snprintf(gui.statusText, sizeof(gui.statusText), "\u6e38\u620f\u5df2\u542f\u52a8 (WASD\u79fb\u52a8, ESC\u9000\u51fa)");

    // Game loop
    bool vis[7] = { true, true, true, true, true, true, true }; // all 7 layers visible by default
    float opa[7] = { 1.0f, 1.0f, 0.6f, 1.0f, 1.0f, 1.0f, 0.5f };

    while (g_gameWnd->running) {
        // Process messages
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_gameWnd->running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_gameWnd->running) break;

        // Calculate delta time
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double dt = (double)(now.QuadPart - g_gameWnd->lastTime.QuadPart)
                   / (double)g_gameWnd->freq.QuadPart;
        g_gameWnd->lastTime = now;
        if (dt > 0.1) dt = 0.1;

        // Player movement (WASD + arrows)
        float dx = 0, dy = 0;
        if (g_gameWnd->keys['W'] || g_gameWnd->keys[VK_UP])    dy -= 1;
        if (g_gameWnd->keys['S'] || g_gameWnd->keys[VK_DOWN])  dy += 1;
        if (g_gameWnd->keys['A'] || g_gameWnd->keys[VK_LEFT])  dx -= 1;
        if (g_gameWnd->keys['D'] || g_gameWnd->keys[VK_RIGHT]) dx += 1;

        if (dx != 0 || dy != 0) {
            float len = sqrtf(dx * dx + dy * dy);
            dx /= len; dy /= len;
            float newX = g_gameWnd->playerX + dx * g_gameWnd->playerSpeed * (float)dt;
            float newY = g_gameWnd->playerY + dy * g_gameWnd->playerSpeed * (float)dt;

            // Update facing direction (0=S, 1=W, 2=E, 3=N) for grid sprite sheets
            if (player && player->spriteRows > 1) {
                if (fabsf(dy) >= fabsf(dx)) {
                    player->facingDir = (dy > 0) ? 0 : 3; // South or North
                } else {
                    player->facingDir = (dx < 0) ? 1 : 2; // West or East
                }
            }

            // Cross-ellipse collision: check tile-rectangle vs each ellipse
            float hx = player->capsule.radiusX;
            float hy = player->capsule.radiusY;
            float vx = player->capsule.radiusXv;
            float vy = player->capsule.radiusYv;
            float oy = player->capsule.offsetY;

            auto checkCross = [&](float cx, float cy) -> bool {
                float ecx = cx;
                float ecy = cy + oy;
                float maxR = std::max(std::max(hx, vx), std::max(hy, vy));
                int tx1 = (int)((ecx - maxR) / TileMap::TILE_SIZE);
                int ty1 = (int)((ecy - maxR) / TileMap::TILE_SIZE);
                int tx2 = (int)((ecx + maxR) / TileMap::TILE_SIZE);
                int ty2 = (int)((ecy + maxR) / TileMap::TILE_SIZE);

                for (int ty = ty1; ty <= ty2; ty++) {
                    for (int tx = tx1; tx <= tx2; tx++) {
                        if (!map->inBounds(tx, ty) || !map->isTerrainSolid(tx, ty))
                            continue;
                        float tileL = (float)(tx * TileMap::TILE_SIZE);
                        float tileT = (float)(ty * TileMap::TILE_SIZE);
                        float tileR = tileL + TileMap::TILE_SIZE;
                        float tileB = tileT + TileMap::TILE_SIZE;
                        float closestX = std::max(tileL, std::min(ecx, tileR));
                        float closestY = std::max(tileT, std::min(ecy, tileB));
                        float dx = closestX - ecx;
                        float dy = closestY - ecy;
                        if ((dx * dx) / (hx * hx) + (dy * dy) / (hy * hy) <= 1.0f)
                            return true;
                        if ((dx * dx) / (vx * vx) + (dy * dy) / (vy * vy) <= 1.0f)
                            return true;
                    }
                }
                return false;
            };

            if (!checkCross(newX, newY)) {
                g_gameWnd->playerX = newX;
                g_gameWnd->playerY = newY;
            } else {
                if (!checkCross(newX, g_gameWnd->playerY)) {
                    g_gameWnd->playerX = newX;
                }
                if (!checkCross(g_gameWnd->playerX, newY)) {
                    g_gameWnd->playerY = newY;
                }
            }

            if (player) {
                player->worldX = g_gameWnd->playerX;
                player->worldY = g_gameWnd->playerY;
            }
        }



        // NPC movement (API-driven: move selected Character directly)
        {
            Character* npc = nullptr;
            for (auto& c : map->characters) {
                if (c.id == g_gameWnd->selectedNpcId) { npc = &c; break; }
            }
            if (npc) {
                float ndx = 0, ndy = 0;
                if (g_gameWnd->apiNpcUp) ndy -= 1;
                if (g_gameWnd->apiNpcDown) ndy += 1;
                if (g_gameWnd->apiNpcLeft) ndx -= 1;
                if (g_gameWnd->apiNpcRight) ndx += 1;
                if (ndx != 0 || ndy != 0) {
                    float len = sqrtf(ndx*ndx + ndy*ndy); ndx /= len; ndy /= len;
                    float spd = g_gameWnd->playerSpeed;
                    float nx = npc->worldX + ndx * spd * (float)dt;
                    float ny = npc->worldY + ndy * spd * (float)dt;
                    if (fabsf(ndy) >= fabsf(ndx)) npc->facingDir = (ndy > 0) ? 0 : 3;
                    else npc->facingDir = (ndx < 0) ? 1 : 2;
                    float maxR = 24.0f;
                    if (nx > maxR && nx < map->width * TileMap::TILE_SIZE - maxR) npc->worldX = nx;
                    if (ny > maxR && ny < map->height * TileMap::TILE_SIZE - maxR) npc->worldY = ny;
                }
            }
        }

        // Camera follows player (centered on player position)
        {
            float gameW = 1280.0f;
            float gameH = 720.0f;
            float targetCamX = g_gameWnd->playerX - gameW / (2.0f * g_gameWnd->camera.zoom);
            float targetCamY = g_gameWnd->playerY - gameH / (2.0f * g_gameWnd->camera.zoom);
            // Smooth follow with lerp
            float lerpSpeed = 8.0f * (float)dt;
            if (lerpSpeed > 1.0f) lerpSpeed = 1.0f;
            g_gameWnd->camera.x += (targetCamX - g_gameWnd->camera.x) * lerpSpeed;
            g_gameWnd->camera.y += (targetCamY - g_gameWnd->camera.y) * lerpSpeed;
        }

        // Update character animations
        map->updateCharacterAnimation((float)dt);

        // Render with D2D (GPU accelerated)
        if (g_gameWnd->d2d && g_gameWnd->d2d->beginDraw()) {
            g_gameWnd->d2d->clear(0.04f, 0.04f, 0.06f);

            // Render map with game window's own textures and character sprites
            g_gameWnd->map->renderD2D(g_gameWnd->d2d, g_gameWnd->camera, 0,
                                       1280, 720, vis, opa, nullptr,
                                       &g_gameWnd->gameTextures,
                                       &g_gameWnd->gameCharSprites);

            // Render player character sprite or fallback circle
            if (player && player->spriteSheet) {
                // Render sprite for player
                auto spriteIt = g_gameWnd->gameCharSprites.find(player->id);
                if (spriteIt != g_gameWnd->gameCharSprites.end() && spriteIt->second) {
                    float drawW = player->frameWidth * player->spriteScale * g_gameWnd->camera.zoom;
                    float drawH = player->frameHeight * player->spriteScale * g_gameWnd->camera.zoom;
                    float px, py;
                    g_gameWnd->camera.worldToScreen(
                        g_gameWnd->playerX - drawW / 2,
                        g_gameWnd->playerY - drawH / 2,
                        px, py);
                    float srcX = (float)(player->currentFrame * player->frameWidth);
                    float srcY = (float)(player->facingDir * player->frameHeight);
                    g_gameWnd->d2d->drawBitmapSubRect(spriteIt->second,
                                                       px, py, drawW, drawH,
                                                       srcX, srcY,
                                                       (float)player->frameWidth,
                                                       (float)player->frameHeight,
                                                       1.0f);
                }
            } else {
                // Fallback: player circle
                float px, py;
                g_gameWnd->camera.worldToScreen(
                    g_gameWnd->playerX - TileMap::TILE_SIZE / 2,
                    g_gameWnd->playerY - TileMap::TILE_SIZE / 2,
                    px, py);
                float ts = TileMap::TILE_SIZE * g_gameWnd->camera.zoom;

                g_gameWnd->d2d->fillEllipse(px + ts / 2, py + ts / 2,
                                             ts / 2, ts / 2,
                                             0.235f, 0.706f, 1.0f, 0.85f);
                g_gameWnd->d2d->drawEllipse(px + ts / 2, py + ts / 2,
                                             ts / 2, ts / 2,
                                             1.0f, 1.0f, 1.0f, 0.9f, 2.0f);
            }

            // Direction arrow
            if (dx != 0 || dy != 0) {
                float ts = TileMap::TILE_SIZE * g_gameWnd->camera.zoom;
                float cx, cy;
                g_gameWnd->camera.worldToScreen(g_gameWnd->playerX, g_gameWnd->playerY, cx, cy);
                float arrowLen = ts * 0.4f;
                g_gameWnd->d2d->drawLine(cx, cy,
                                          cx + dx * arrowLen, cy + dy * arrowLen,
                                          1.0f, 1.0f, 0.8f, 0.9f, 2.0f);
            }


            // Render non-player characters (npc/enemy)
            for (auto& c : map->characters) {
                if (c.type == L"player") continue;
                float ts = TileMap::TILE_SIZE * g_gameWnd->camera.zoom;
                float px, py;
                g_gameWnd->camera.worldToScreen(c.worldX - TileMap::TILE_SIZE/2, c.worldY - TileMap::TILE_SIZE/2, px, py);
                float cr = (c.type == L"enemy") ? 1.0f : 0.2f;
                float cg = (c.type == L"enemy") ? 0.3f : 1.0f;
                float cb = (c.type == L"enemy") ? 0.3f : 0.4f;
                bool sel = (c.id == g_gameWnd->selectedNpcId);
                g_gameWnd->d2d->fillEllipse(px+ts/2, py+ts/2, ts/2, ts/2, cr, cg, cb, sel ? 1.0f : 0.7f);
                g_gameWnd->d2d->drawEllipse(px+ts/2, py+ts/2, ts/2, ts/2, sel?1.0f:0.6f, sel?1.0f:0.6f, sel?1.0f:0.6f, 0.9f, sel?3.0f:1.5f);
            }


            // HUD - bottom status bar (not overlapping game world)
            {
                float barH = 28.0f;
                float barY = 720.0f - barH;
                g_gameWnd->d2d->fillRect(0.0f, barY, 1280.0f, barH, 0.0f, 0.0f, 0.0f, 0.7f);
                g_gameWnd->d2d->drawLine(0.0f, barY, 1280.0f, barY, 0.3f, 0.3f, 0.3f);

                wchar_t hudLeft[128];
                _snwprintf(hudLeft, 128, L"  \u73a9\u5bb6\u4f4d\u7f6e: (%.0f, %.0f)",
                           g_gameWnd->playerX, g_gameWnd->playerY);
                g_gameWnd->d2d->drawText(hudLeft, 8.0f, barY + 5.0f, 260.0f, 20.0f,
                                          0.8f, 0.8f, 0.8f, 0.9f, 10.0f);

                g_gameWnd->d2d->drawText(L"WASD\u79fb\u52a8  \u5c0f\u5730\u56fe\u6a21\u5f0f",
                                          400.0f, barY + 5.0f, 200.0f, 20.0f,
                                          0.6f, 0.6f, 0.6f, 0.8f, 10.0f);

                g_gameWnd->d2d->drawText(L"ESC \u9000\u51fa\u6e38\u620f",
                                          1120.0f, barY + 5.0f, 150.0f, 20.0f,
                                          0.6f, 0.6f, 0.6f, 0.8f, 10.0f);
            }

            g_gameWnd->d2d->endDraw();
        }

        Sleep(1);
    }

    HttpApi_Stop();

    // Cleanup game textures
    for (auto& kv : g_gameWnd->gameTextures) {
        if (kv.second) kv.second->Release();
    }
    g_gameWnd->gameTextures.clear();
    for (auto& kv : g_gameWnd->gameCharSprites) {
        if (kv.second) kv.second->Release();
    }
    g_gameWnd->gameCharSprites.clear();

    if (g_gameWnd->d2d) {
        delete g_gameWnd->d2d;
        g_gameWnd->d2d = nullptr;
    }
    delete g_gameWnd;
    g_gameWnd = nullptr;

    // Restore player position and editor camera
    if (player) {
        player->worldX = savedWorldX;
        player->worldY = savedWorldY;
    }
    camera.x = savedCamX;
    camera.y = savedCamY;
    camera.zoom = savedCamZoom;

    snprintf(gui.statusText, sizeof(gui.statusText), "\u6e38\u620f\u5df2\u5173\u95ed");
}

// ---------------------------------------------------------------------------
// API Operations
// ---------------------------------------------------------------------------

void Editor::generateTile(const char* prompt) {
    if (!pollinations) return;

    strncpy_s(gui.statusText, "\u6b63\u5728\u751f\u6210AI\u8d34\u56fe...", sizeof(gui.statusText));

    Gdiplus::Bitmap* bmp = nullptr;
    if (pollinations->generateTileTexture(prompt, &bmp, TileMap::TILE_SIZE)) {
        // Add as new tile definition
        if (map) {
            int newId = map->addTileDef(L"AI\u8d34\u56fe", 0xFF888888, 1, false);
            if (newId >= 0) {
                map->setCustomTexture((uint8_t)newId, bmp);
                if (d2d) map->convertTexturesToD2D(d2d);
                selectedTileId = newId;
            } else {
                delete bmp;
            }
        }
        strncpy_s(gui.statusText, "AI\u8d34\u56fe\u751f\u6210\u5b8c\u6210", sizeof(gui.statusText));
    } else {
        delete bmp;
        strncpy_s(gui.statusText, "AI\u8d34\u56fe\u751f\u6210\u5931\u8d25", sizeof(gui.statusText));
    }
}

void Editor::removeBackground() {
    if (!bgremove) return;

    strncpy_s(gui.statusText, "\u6b63\u5728\u53bb\u9664\u80cc\u666f...", sizeof(gui.statusText));

    char outputPath[512] = {};
    if (bgremove->removeBackground("temp_tile.png", outputPath, sizeof(outputPath))) {
        strncpy_s(gui.statusText, "\u80cc\u666f\u53bb\u9664\u5b8c\u6210", sizeof(gui.statusText));
    } else {
        strncpy_s(gui.statusText, "\u80cc\u666f\u53bb\u9664\u5931\u8d25", sizeof(gui.statusText));
    }
}

void Editor::captureScreenshot() {
    if (!screenshot) return;

    Gdiplus::Bitmap* bmp = nullptr;
    if (screenshot->captureWindow("GameMaker C++", &bmp)) {
        delete bmp;
        strncpy_s(gui.statusText, "\u622a\u56fe\u6210\u529f", sizeof(gui.statusText));
    } else {
        strncpy_s(gui.statusText, "\u622a\u56fe\u5931\u8d25", sizeof(gui.statusText));
    }
}

// ---------------------------------------------------------------------------
// Undo / Redo
// ---------------------------------------------------------------------------

void Editor::pushUndo() {
    if (!map) return;

    UndoSnapshot snap;
    int w = map->width;
    int h = map->height;

    // Save layers[0] (terrain visual)
    snap.terrain.resize(h);
    for (int y = 0; y < h; y++) {
        snap.terrain[y].assign(map->layers[0][y].begin(), map->layers[0][y].end());
    }

    // Save layers[2] (character/decoration visual)
    snap.objects.resize(h);
    for (int y = 0; y < h; y++) {
        snap.objects[y].assign(map->layers[2][y].begin(), map->layers[2][y].end());
    }

    // Save terrain collision as uint8_t
    snap.units.resize(h);
    for (int y = 0; y < h; y++) {
        snap.units[y].resize(w);
        for (int x = 0; x < w; x++) {
            snap.units[y][x] = map->terrainCollision[y][x].solid ? 1 : 0;
        }
    }

    undoStack.push_back(std::move(snap));
    if ((int)undoStack.size() > MAX_UNDO)
        undoStack.erase(undoStack.begin());

    redoStack.clear();
}

void Editor::undo() {
    if (undoStack.empty() || !map) return;

    // Save current state to redo stack
    UndoSnapshot current;
    int w = map->width;
    int h = map->height;
    current.terrain.resize(h);
    current.objects.resize(h);
    current.units.resize(h);
    for (int y = 0; y < h; y++) {
        current.terrain[y].assign(map->layers[0][y].begin(), map->layers[0][y].end());
        current.objects[y].assign(map->layers[2][y].begin(), map->layers[2][y].end());
        current.units[y].resize(w);
        for (int x = 0; x < w; x++)
            current.units[y][x] = map->terrainCollision[y][x].solid ? 1 : 0;
    }
    redoStack.push_back(std::move(current));

    // Restore undo state
    UndoSnapshot& snap = undoStack.back();
    for (int y = 0; y < h && y < (int)snap.terrain.size(); y++) {
        for (int x = 0; x < w && x < (int)snap.terrain[y].size(); x++)
            map->layers[0][y][x] = snap.terrain[y][x];
    }
    for (int y = 0; y < h && y < (int)snap.objects.size(); y++) {
        for (int x = 0; x < w && x < (int)snap.objects[y].size(); x++)
            map->layers[2][y][x] = snap.objects[y][x];
    }
    for (int y = 0; y < h && y < (int)snap.units.size(); y++) {
        for (int x = 0; x < w && x < (int)snap.units[y].size(); x++)
            map->terrainCollision[y][x].solid = (snap.units[y][x] != 0);
    }

    undoStack.pop_back();
    strncpy_s(gui.statusText, "\u64a4\u9500", sizeof(gui.statusText));
}

void Editor::redo() {
    if (redoStack.empty() || !map) return;

    // Save current state to undo stack
    pushUndo();

    // Restore redo state
    UndoSnapshot& snap = redoStack.back();
    int w = map->width;
    int h = map->height;

    for (int y = 0; y < h && y < (int)snap.terrain.size(); y++) {
        for (int x = 0; x < w && x < (int)snap.terrain[y].size(); x++)
            map->layers[0][y][x] = snap.terrain[y][x];
    }
    for (int y = 0; y < h && y < (int)snap.objects.size(); y++) {
        for (int x = 0; x < w && x < (int)snap.objects[y].size(); x++)
            map->layers[2][y][x] = snap.objects[y][x];
    }
    for (int y = 0; y < h && y < (int)snap.units.size(); y++) {
        for (int x = 0; x < w && x < (int)snap.units[y].size(); x++)
            map->terrainCollision[y][x].solid = (snap.units[y][x] != 0);
    }

    redoStack.pop_back();
    strncpy_s(gui.statusText, "\u91cd\u505a", sizeof(gui.statusText));
}