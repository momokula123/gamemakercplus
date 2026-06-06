#pragma once
#include <string>
#include <vector>
#include <windows.h>

struct LayerInfo {
    std::wstring name;
    bool visible = true;
    float opacity = 1.0f;
    bool locked = false;
};

class D2DRenderer;

class LayerManager {
public:
    static const int NUM_LAYERS = 7;
    static const int LAYER_TERRAIN_VIS    = 0;
    static const int LAYER_DRAW           = 1;  // imported images above terrain
    static const int LAYER_TERRAIN_COL    = 2;
    static const int LAYER_CHARACTER_VIS  = 3;
    static const int LAYER_FOREGROUND     = 4;
    static const int LAYER_FG_DRAW        = 5;  // imported images above foreground
    static const int LAYER_CHARACTER_COL  = 6;

    LayerInfo layers[NUM_LAYERS];
    int activeLayer = 0;
    int renderOrder[NUM_LAYERS];

    LayerManager();

    void getLayerColors(int idx, float& r, float& g, float& b) const;
};

class LayerPanel {
public:
    static const int ENTRY_H = 28;
    static const int EYE_W = 26;
    static const int DRAG_THRESHOLD = 5;

    LayerPanel();

    int getHeight(int count) const;
    void render(D2DRenderer* r, LayerManager* lm, int px, int py, int pw);

    bool onMouseDown(int mx, int my, int px, int py, int pw, LayerManager* lm);
    bool onMouseMove(int mx, int my, int px, int py, int pw, LayerManager* lm);
    bool onMouseUp(int mx, int my, int px, int py, int pw, LayerManager* lm);

private:
    int dragFromIdx;
    int dragToIdx;
    bool dragging;
    bool mouseDownOnEntry;
    int downX, downY;
    int downEntryIdx;

    int hitTest(int mx, int my, int px, int py, int pw, int count);
};
