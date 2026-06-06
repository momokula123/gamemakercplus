#include "layers.h"
#include "../renderer/d2d_renderer.h"
#include <algorithm>
#include <cmath>

LayerManager::LayerManager()
    : activeLayer(0)
{
    layers[0].name = L"地形";
    layers[0].visible = true;
    layers[0].opacity = 1.0f;

    layers[1].name = L"绘制";
    layers[1].visible = true;
    layers[1].opacity = 1.0f;

    layers[2].name = L"地形碰撞";
    layers[2].visible = true;
    layers[2].opacity = 0.6f;

    layers[3].name = L"人物";
    layers[3].visible = true;
    layers[3].opacity = 1.0f;

    layers[4].name = L"前景";
    layers[4].visible = true;
    layers[4].opacity = 1.0f;

    layers[5].name = L"前景绘制";
    layers[5].visible = true;
    layers[5].opacity = 1.0f;

    layers[6].name = L"人物碰撞";
    layers[6].visible = true;
    layers[6].opacity = 0.5f;

    for (int i = 0; i < NUM_LAYERS; i++)
        renderOrder[i] = i;
}

void LayerManager::getLayerColors(int idx, float& r, float& g, float& b) const {
    switch (idx) {
    case 0: r = 0.30f; g = 0.69f; b = 0.31f; break;
    case 1: r = 0.20f; g = 0.71f; b = 1.00f; break;
    case 2: r = 0.80f; g = 0.24f; b = 0.24f; break;
    case 3: r = 0.85f; g = 0.55f; b = 0.10f; break;
    case 4: r = 1.00f; g = 0.75f; b = 0.20f; break;
    case 5: r = 0.60f; g = 0.30f; b = 0.90f; break;
    case 6: r = 0.60f; g = 0.20f; b = 0.60f; break;
    default: r = 0.5f; g = 0.5f; b = 0.5f; break;
    }
}

LayerPanel::LayerPanel()
    : dragFromIdx(-1)
    , dragToIdx(-1)
    , dragging(false)
    , mouseDownOnEntry(false)
    , downX(0), downY(0)
    , downEntryIdx(-1)
{
}

int LayerPanel::getHeight(int count) const {
    return count * ENTRY_H + 4;
}

int LayerPanel::hitTest(int mx, int my, int px, int py, int pw, int count) {
    for (int i = 0; i < count; i++) {
        int ey = py + i * ENTRY_H;
        if (mx >= px && mx < px + pw && my >= ey && my < ey + ENTRY_H)
            return i;
    }
    return -1;
}

void LayerPanel::render(D2DRenderer* r, LayerManager* lm, int px, int py, int pw) {
    if (!r || !lm) return;

    int count = LayerManager::NUM_LAYERS;
    int totalH = getHeight(count);

    r->fillRect((float)px, (float)py, (float)pw, (float)totalH,
                0.16f, 0.16f, 0.17f);

    for (int i = 0; i < count; i++) {
        int layerIdx = lm->renderOrder[count - 1 - i];
        int ey = py + i * ENTRY_H;
        bool isActive = (layerIdx == lm->activeLayer);
        bool vis = lm->layers[layerIdx].visible;

        float bgR, bgG, bgB;
        if (isActive) {
            bgR = 0.20f; bgG = 0.40f; bgB = 0.65f;
        } else {
            bgR = 0.22f; bgG = 0.22f; bgB = 0.23f;
        }
        r->fillRect((float)px, (float)ey, (float)pw, (float)ENTRY_H,
                    bgR, bgG, bgB);

        r->drawRect((float)px, (float)ey, (float)pw, (float)ENTRY_H,
                    0.30f, 0.30f, 0.31f, 1.0f, 1.0f);

        float cR, cG, cB;
        lm->getLayerColors(layerIdx, cR, cG, cB);
        r->fillRect((float)px + 2, (float)ey + 2, 4.0f, (float)(ENTRY_H - 4),
                    cR, cG, cB, 0.8f);

        float eyeX = (float)(px + 8);
        float eyeY = (float)(ey + ENTRY_H / 2);
        if (vis) {
            r->fillEllipse(eyeX + 6, eyeY, 6.0f, 4.0f,
                           1.0f, 1.0f, 1.0f, 0.9f);
            r->fillEllipse(eyeX + 6, eyeY, 2.5f, 2.5f,
                           0.2f, 0.2f, 0.2f, 1.0f);
        } else {
            r->drawRect(eyeX, eyeY - 4, 12.0f, 8.0f,
                        0.5f, 0.5f, 0.5f, 0.5f, 1.0f);
        }

        const wchar_t* name = lm->layers[layerIdx].name.c_str();
        r->drawText(name, (float)(px + 28), (float)ey,
                    (float)(pw - 32), (float)ENTRY_H,
                    1.0f, 1.0f, 1.0f, vis ? 1.0f : 0.4f, 10.0f, false,
                    DWRITE_TEXT_ALIGNMENT_LEADING,
                    DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        if (dragging && i == dragToIdx) {
            r->fillRect((float)px, (float)ey - 1, (float)pw, 2.0f,
                        0.30f, 0.60f, 1.0f, 1.0f);
        }
    }
}

bool LayerPanel::onMouseDown(int mx, int my, int px, int py, int pw, LayerManager* lm) {
    if (!lm) return false;
    int count = LayerManager::NUM_LAYERS;
    int idx = hitTest(mx, my, px, py, pw, count);
    if (idx < 0) return false;

    int layerIdx = lm->renderOrder[count - 1 - idx];

    int eyeX = px + 8;
    int eyeY = py + idx * ENTRY_H;
    if (mx >= eyeX && mx < eyeX + 14 &&
        my >= eyeY && my < eyeY + ENTRY_H) {
        lm->layers[layerIdx].visible = !lm->layers[layerIdx].visible;
        return true;
    }

    lm->activeLayer = layerIdx;

    mouseDownOnEntry = true;
    downX = mx;
    downY = my;
    downEntryIdx = idx;
    dragFromIdx = idx;
    dragToIdx = idx;
    dragging = false;

    return true;
}

bool LayerPanel::onMouseMove(int mx, int my, int px, int py, int pw, LayerManager* lm) {
    if (!lm) return false;

    if (mouseDownOnEntry && !dragging) {
        int dx = mx - downX;
        int dy = my - downY;
        if (abs(dx) > DRAG_THRESHOLD || abs(dy) > DRAG_THRESHOLD) {
            dragging = true;
        }
    }

    if (dragging) {
        int count = LayerManager::NUM_LAYERS;
        int newIdx = hitTest(mx, my, px, py, pw, count);
        if (newIdx < 0) {
            if (my < py) newIdx = 0;
            else if (my > py + count * ENTRY_H) newIdx = count - 1;
        }
        if (newIdx >= 0 && newIdx < count) {
            dragToIdx = newIdx;
        }
        return true;
    }

    return false;
}

bool LayerPanel::onMouseUp(int mx, int my, int px, int py, int pw, LayerManager* lm) {
    if (!lm) return false;

    if (dragging && dragFromIdx != dragToIdx) {
        int count = LayerManager::NUM_LAYERS;
        int fromReal = count - 1 - dragFromIdx;
        int toReal = count - 1 - dragToIdx;

        int temp = lm->renderOrder[fromReal];
        if (fromReal < toReal) {
            for (int i = fromReal; i < toReal; i++)
                lm->renderOrder[i] = lm->renderOrder[i + 1];
        } else {
            for (int i = fromReal; i > toReal; i--)
                lm->renderOrder[i] = lm->renderOrder[i - 1];
        }
        lm->renderOrder[toReal] = temp;
    }

    bool consumed = dragging;
    mouseDownOnEntry = false;
    dragging = false;
    dragFromIdx = -1;
    dragToIdx = -1;
    return consumed;
}