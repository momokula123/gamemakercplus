#pragma once

#include <windows.h>
#include <propkeydef.h>
#include <gdiplus.h>
#include <queue>
#include <vector>
#include <cstdint>

struct Rect {
    int x, y, w, h;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct Color {
    uint8_t r, g, b, a;
    constexpr Color() : r(0), g(0), b(0), a(255) {}
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}
    Gdiplus::Color toGdiplus() const {
        return Gdiplus::Color(a, r, g, b);
    }
};

namespace Theme {
    static constexpr Color bg       = Color(0x2D, 0x2D, 0x30);
    static constexpr Color panel    = Color(0x1E, 0x1E, 0x1E);
    static constexpr Color button   = Color(0x3E, 0x3E, 0x42);
    static constexpr Color text     = Color(0xFF, 0xFF, 0xFF);
    static constexpr Color accent   = Color(0x00, 0x78, 0xD4);
    static constexpr Color hover    = Color(0x50, 0x50, 0x54);
    static constexpr Color border   = Color(0x55, 0x55, 0x55);
    static constexpr Color dimText  = Color(0xAA, 0xAA, 0xAA);
    static constexpr Color panelBg  = Color(0x25, 0x25, 0x26);
    static constexpr Color sliderTrack = Color(0x3E, 0x3E, 0x42);
    static constexpr Color sliderFill  = Color(0x00, 0x78, 0xD4);
    static constexpr Color inputBg     = Color(0x33, 0x33, 0x37);
    static constexpr Color tooltipBg   = Color(0x2D, 0x2D, 0x30);
}

struct GUIState {
    bool panelOpen_left = true;
    bool panelOpen_right = true;
    int activeTool = 0;
    float scrollY = 0.0f;
    int selectedTileId = 0;
    char promptBuffer[256] = {0};
    char statusText[256] = {0};

    int mouseX = 0;
    int mouseY = 0;
    bool mouseDown = false;
    bool mouseClicked = false;
    bool rightMouseDown = false;
    bool rightMouseClicked = false;

    std::queue<int> clickQueue;

    void resetFrame() {
        mouseClicked = false;
        rightMouseClicked = false;
    }

    void setMouse(int x, int y, bool left, bool right) {
        mouseX = x;
        mouseY = y;
        if (left && !mouseDown) {
            mouseClicked = true;
        }
        if (right && !rightMouseDown) {
            rightMouseClicked = true;
        }
        mouseDown = left;
        rightMouseDown = right;
    }
};

namespace GUI {

void beginFrame(GUIState& state, int mx, int my, bool leftDown, bool rightDown);

bool button(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w, int h,
            const wchar_t* text, bool active = false);

bool slider(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w,
            float* value, float min, float max);

void panel(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w, int h,
           const wchar_t* title);

void label(Gdiplus::Graphics* g, int x, int y, const wchar_t* text);

void textInput(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w,
               char* buf, int bufSize, bool& active);

void tooltip(Gdiplus::Graphics* g, GUIState& state, int x, int y, const wchar_t* text);

void drawPanelBackground(Gdiplus::Graphics* g, int x, int y, int w, int h);

void drawButton(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w, int h,
                const wchar_t* text, bool hover, bool active);

void drawSlider(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w,
                float value, float min, float max);

void drawTextInput(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w,
                   const char* buf, bool focused);

void drawTooltip(Gdiplus::Graphics* g, GUIState& state, int x, int y, const wchar_t* text);

void drawStatus(Gdiplus::Graphics* g, GUIState& state, int screenWidth);

}
