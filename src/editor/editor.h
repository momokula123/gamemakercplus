#pragma once

#include <windows.h>
#include <propkeydef.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>

#include "tilemap.h"
#include "layers.h"
#include "asset_database.h"

class PollinationsAPI;
class BgRemoveAPI;
class ScreenshotAPI;
class MapSerializer;
class D2DRenderer;
class LayerPanel;

enum Tool {
    BRUSH = 0,
    ERASER,
    SELECT,
    PAN,
    FILL
};

struct GUIState {
    bool showGrid;
    bool showObjects;
    bool showUnits;
    bool panelHovered;
    bool menuHovered;
    int  hoveredTile;
    int  statusMessage;
    char statusText[256];
    float menuAnimProgress;
    float panelAnimProgress;
};

struct UndoSnapshot {
    std::vector<std::vector<uint8_t>> terrain;
    std::vector<std::vector<uint8_t>> objects;
    std::vector<std::vector<uint8_t>> units;
};

class Editor {
public:
    TileMap* map;
    LayerManager* layers;
    LayerPanel* layerPanel;
    AssetDatabase* assetDb;
    std::wstring exeDir;
    std::wstring assetsDir;
    D2DRenderer* d2d;
    PollinationsAPI* pollinations;
    BgRemoveAPI* bgremove;
    ScreenshotAPI* screenshot;
    MapSerializer* serializer;

    GUIState gui;
    Camera camera;
    Tool currentTool;

    int windowW;
    int windowH;

    int selectedTileId;
    int brushSize;
    int tileScrollOffset = 0;
    int selectedCharacterIdx = -1;

    char currentFilePath[MAX_PATH] = {};

    HWND hwnd = nullptr;
    bool isDragging = false;
    bool isPanning = false;
    bool isPainting = false;
    int paintAnchorX = 0; // grid anchor for source-image tiles
    int paintAnchorY = 0;
    // Permanent anchor per tileId: once a tile is first used, its anchor is fixed forever
    std::unordered_map<int, std::pair<int,int>> tileAnchors;
    bool isDraggingSlider = false;
    int lastMouseX = 0;
    int lastMouseY = 0;
    int hoverTileX = -1;
    int hoverTileY = -1;

    static const int PANEL_WIDTH  = 0;
    static const int TOP_BAR_H    = 0;
    static const int STATUS_BAR_H = 140;

    // Menu bar layout
    static const int MENU_BTN_X = 8;
    static const int MENU_BTN_Y = 0;
    static const int MENU_BTN_W = 68;
    static const int MENU_BTN_H = 28;
    static const int MENU_BTN_GAP = 4;
    static const int MENU_TOOL_GAP = 30;

    // Tool palette layout
    static const int TILE_BTN_X = 0;
    static const int TILE_BTN_W = PANEL_WIDTH - 16;
    static const int TILE_BTN_H = 36;
    static const int TILE_BTN_GAP = 2;
    static const int TILE_START_Y = TOP_BAR_H + 46;

    // Tool icon buttons (horizontal row)
    static const int TOOL_ICON_SIZE = 26;
    static const int TOOL_ICON_GAP = 4;

    // Layer buttons (below tool icons)
    static const int LAYER_BTN_Y = TOP_BAR_H + 2;  // just below tool icons
    static const int LAYER_BTN_H = 18;

    // Brush slider layout
    static const int SLIDER_X = 12;
    static const int SLIDER_W = PANEL_WIDTH - 24;
    static const int SLIDER_H = 8;

    Editor();
    ~Editor();

    void init(HWND hwnd);
    void setD2DRenderer(D2DRenderer* renderer);
    void render(Gdiplus::Graphics* g);
    void renderD2D(D2DRenderer* d2d);
    void handleInput(UINT msg, WPARAM wParam, LPARAM lParam);

    void newMap(int w, int h);
    void saveMap(const char* path);
    void loadMap(const char* path);

    void generateTile(const char* prompt);
    void removeBackground();
    void captureScreenshot();
    void initTileTextures();
    void runGame();

    void pushUndo();
    void undo();
    void redo();

    int getCanvasWidth() const;
    int getCanvasHeight() const;
    int tileCount() const;

private:
    std::vector<UndoSnapshot> undoStack;
    std::vector<UndoSnapshot> redoStack;
    static const int MAX_UNDO = 50;

    void onMouseDown(int x, int y, bool left);
    void onMouseUp(int x, int y);
    void onMouseMove(int x, int y);
    void onMouseWheel(int delta, int x, int y);
    void onKeyDown(WPARAM key);
    void onKeyUp(WPARAM key);

    void handleBrush(int worldX, int worldY);
    void handleEraser(int worldX, int worldY);
    void handleFill(int worldX, int worldY);

    void renderMenuBar(Gdiplus::Graphics* g);
    void renderToolPalette(Gdiplus::Graphics* g);
    void renderStatusBar(Gdiplus::Graphics* g);
    void renderCanvas(Gdiplus::Graphics* g);
    void renderGrid(Gdiplus::Graphics* g);
    void renderMinimap(Gdiplus::Graphics* g);
    void renderTooltip(Gdiplus::Graphics* g);

    void drawButton(Gdiplus::Graphics* g, int x, int y, int w, int h,
                    const wchar_t* label, bool hovered, bool selected = false);

    // D2D rendering methods
    void renderCanvasD2D(D2DRenderer* r);
    void renderMenuBarD2D(D2DRenderer* r);
    void renderToolPaletteD2D(D2DRenderer* r);
    void renderStatusBarD2D(D2DRenderer* r);
    void renderMinimapD2D(D2DRenderer* r);
    void renderTooltipD2D(D2DRenderer* r);

    Gdiplus::Bitmap* offscreenBuffer;
    Gdiplus::Bitmap* tileAtlas;
    Gdiplus::Font* fontSmall;
    Gdiplus::Font* fontMedium;
    Gdiplus::Font* fontLarge;
    Gdiplus::SolidBrush* brushBg;
    Gdiplus::SolidBrush* brushFg;
    Gdiplus::SolidBrush* brushAccent;
    Gdiplus::SolidBrush* brushPanel;
    Gdiplus::SolidBrush* brushMenu;
    Gdiplus::SolidBrush* brushStatus;
    Gdiplus::Pen* penGrid;
    Gdiplus::Pen* penSelection;
    Gdiplus::Pen* penBorder;

    void createResources();
    void destroyResources();
};
