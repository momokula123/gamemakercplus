#include "gui.h"

#pragma comment(lib, "gdiplus.lib")

namespace {

bool isHovered(GUIState& state, int x, int y, int w, int h) {
    Rect r = { x, y, w, h };
    return r.contains(state.mouseX, state.mouseY);
}

Gdiplus::Font* getDefaultFont() {
    static Gdiplus::Font* font = nullptr;
    if (!font) {
        font = new Gdiplus::Font(L"Microsoft YaHei UI", 12.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    }
    return font;
}

Gdiplus::Font* getSmallFont() {
    static Gdiplus::Font* font = nullptr;
    if (!font) {
        font = new Gdiplus::Font(L"Microsoft YaHei UI", 10.0f, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    }
    return font;
}

void drawRoundedRect(Gdiplus::Graphics* g, int x, int y, int w, int h, int radius, Gdiplus::Color fill, Gdiplus::Color border) {
    Gdiplus::SolidBrush brush(fill);
    Gdiplus::Pen pen(border, 1.0f);

    Gdiplus::GraphicsPath path;
    int d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();

    g->FillPath(&brush, &path);
    g->DrawPath(&pen, &path);
}

void drawText(Gdiplus::Graphics* g, int x, int y, const wchar_t* text, Gdiplus::Color color, Gdiplus::Font* font = nullptr) {
    if (!font) font = getDefaultFont();
    Gdiplus::SolidBrush brush(color);
    Gdiplus::PointF pt(static_cast<Gdiplus::REAL>(x), static_cast<Gdiplus::REAL>(y));
    g->DrawString(text, -1, font, pt, (const Gdiplus::Brush*)&brush);
}

Gdiplus::SizeF measureText(Gdiplus::Graphics* g, const wchar_t* text, Gdiplus::Font* font = nullptr) {
    if (!font) font = getDefaultFont();
    Gdiplus::RectF rect;
    g->MeasureString(text, -1, font, Gdiplus::PointF(0, 0), &rect);
    return Gdiplus::SizeF(rect.Width, rect.Height);
}

} // anonymous namespace

namespace GUI {

void beginFrame(GUIState& state, int mx, int my, bool leftDown, bool rightDown) {
    state.setMouse(mx, my, leftDown, rightDown);
}

bool button(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w, int h,
            const wchar_t* text, bool active) {
    bool hovered = isHovered(state, x, y, w, h);
    bool clicked = hovered && state.mouseClicked;

    drawButton(g, state, x, y, w, h, text, hovered, active);

    return clicked;
}

bool slider(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w,
            float* value, float min, float max) {
    bool changed = false;
    bool hovered = isHovered(state, x, y, w, 20);
    int knobSize = 14;
    int trackH = 4;
    int trackY = y + 8;

    Gdiplus::SolidBrush trackBrush(Theme::sliderTrack.toGdiplus());
    g->FillRectangle(&trackBrush, static_cast<int>(x), static_cast<int>(trackY),
                     static_cast<int>(w), static_cast<int>(trackH));

    float t = 0.0f;
    if (max > min) {
        t = (*value - min) / (max - min);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    int fillW = static_cast<int>(t * w);
    Gdiplus::SolidBrush fillBrush(Theme::sliderFill.toGdiplus());
    g->FillRectangle(&fillBrush, static_cast<int>(x), static_cast<int>(trackY),
                     static_cast<int>(fillW), static_cast<int>(trackH));

    int knobX = x + fillW - knobSize / 2;
    int knobY = trackY - (knobSize - trackH) / 2;

    Gdiplus::Color knobColor = hovered || state.mouseDown ? Theme::accent.toGdiplus() : Theme::text.toGdiplus();
    Gdiplus::SolidBrush knobBrush(knobColor);
    g->FillEllipse(&knobBrush, static_cast<int>(knobX), static_cast<int>(knobY),
                   static_cast<int>(knobSize), static_cast<int>(knobSize));

    if (hovered && state.mouseDown) {
        float newT = static_cast<float>(state.mouseX - x) / static_cast<float>(w);
        if (newT < 0.0f) newT = 0.0f;
        if (newT > 1.0f) newT = 1.0f;
        float newVal = min + newT * (max - min);
        if (newVal != *value) {
            *value = newVal;
            changed = true;
        }
    }

    return changed;
}

void panel(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w, int h,
           const wchar_t* title) {
    drawPanelBackground(g, x, y, w, h);

    Gdiplus::Pen borderPen(Theme::border.toGdiplus(), 1.0f);
    g->DrawRectangle(&borderPen, static_cast<int>(x), static_cast<int>(y),
                     static_cast<int>(w), static_cast<int>(h));

    drawText(g, x + 10, y + 8, title, Theme::accent.toGdiplus());

    Gdiplus::Pen linePen(Theme::border.toGdiplus(), 1.0f);
    g->DrawLine(&linePen, static_cast<int>(x + 8), static_cast<int>(y + 32),
                static_cast<int>(x + w - 8), static_cast<int>(y + 32));
}

void label(Gdiplus::Graphics* g, int x, int y, const wchar_t* text) {
    drawText(g, x, y, text, Theme::text.toGdiplus());
}

void textInput(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w,
               char* buf, int bufSize, bool& active) {
    bool hovered = isHovered(state, x, y, w, 28);
    if (hovered && state.mouseClicked) {
        active = true;
    } else if (!hovered && state.mouseClicked) {
        active = false;
    }

    drawTextInput(g, state, x, y, w, buf, active);
}

void tooltip(Gdiplus::Graphics* g, GUIState& state, int x, int y, const wchar_t* text) {
    bool hovered = isHovered(state, x, y, 32, 32);
    if (hovered) {
        drawTooltip(g, state, state.mouseX + 12, state.mouseY + 12, text);
    }
}

void drawPanelBackground(Gdiplus::Graphics* g, int x, int y, int w, int h) {
    Gdiplus::SolidBrush brush(Theme::panelBg.toGdiplus());
    g->FillRectangle(&brush, static_cast<int>(x), static_cast<int>(y),
                     static_cast<int>(w), static_cast<int>(h));
}

void drawButton(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w, int h,
                const wchar_t* text, bool hover, bool active) {
    Gdiplus::Color bgColor;
    if (active) {
        bgColor = Theme::accent.toGdiplus();
    } else if (hover) {
        bgColor = Theme::hover.toGdiplus();
    } else {
        bgColor = Theme::button.toGdiplus();
    }

    drawRoundedRect(g, x, y, w, h, 4, bgColor, Theme::border.toGdiplus());

    Gdiplus::SizeF textSize = measureText(g, text);
    int tx = x + (w - static_cast<int>(textSize.Width)) / 2;
    int ty = y + (h - static_cast<int>(textSize.Height)) / 2;
    drawText(g, tx, ty, text, active ? Gdiplus::Color(255, 255, 255) : Theme::text.toGdiplus());
}

void drawSlider(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w,
                float value, float min, float max) {
    int knobSize = 14;
    int trackH = 4;
    int trackY = y + 8;

    Gdiplus::SolidBrush trackBrush(Theme::sliderTrack.toGdiplus());
    g->FillRectangle(&trackBrush, static_cast<int>(x), static_cast<int>(trackY),
                     static_cast<int>(w), static_cast<int>(trackH));

    float t = 0.0f;
    if (max > min) {
        t = (value - min) / (max - min);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    int fillW = static_cast<int>(t * w);
    Gdiplus::SolidBrush fillBrush(Theme::sliderFill.toGdiplus());
    g->FillRectangle(&fillBrush, static_cast<int>(x), static_cast<int>(trackY),
                     static_cast<int>(fillW), static_cast<int>(trackH));

    int knobX = x + fillW - knobSize / 2;
    int knobY = trackY - (knobSize - trackH) / 2;
    bool hovered = isHovered(state, x, y, w, 20);
    Gdiplus::Color knobColor = hovered || state.mouseDown ? Theme::accent.toGdiplus() : Theme::text.toGdiplus();
    Gdiplus::SolidBrush knobBrush(knobColor);
    g->FillEllipse(&knobBrush, static_cast<int>(knobX), static_cast<int>(knobY),
                   static_cast<int>(knobSize), static_cast<int>(knobSize));
}

void drawTextInput(Gdiplus::Graphics* g, GUIState& state, int x, int y, int w,
                   const char* buf, bool focused) {
    int h = 28;
    Gdiplus::Color bgColor = focused ? Theme::accent.toGdiplus() : Theme::inputBg.toGdiplus();
    drawRoundedRect(g, x, y, w, h, 3, bgColor, Theme::border.toGdiplus());

    wchar_t wbuf[256];
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, 256);

    Gdiplus::SolidBrush textBrush(Theme::text.toGdiplus());
    Gdiplus::RectF layoutRect(static_cast<Gdiplus::REAL>(x + 6), static_cast<Gdiplus::REAL>(y + 4),
                              static_cast<Gdiplus::REAL>(w - 12), static_cast<Gdiplus::REAL>(h - 8));
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g->DrawString(wbuf, -1, getDefaultFont(), layoutRect, &fmt, (const Gdiplus::Brush*)&textBrush);

    if (focused) {
        Gdiplus::SizeF textSize = measureText(g, wbuf);
        static ULONGLONG lastBlink = 0;
        ULONGLONG now = GetTickCount64();
        if ((now / 500) % 2 == 0) {
            Gdiplus::Pen cursorPen(Theme::text.toGdiplus(), 1.0f);
            int cx = x + 6 + static_cast<int>(textSize.Width) + 1;
            g->DrawLine(&cursorPen, static_cast<int>(cx), static_cast<int>(y + 5),
                        static_cast<int>(cx), static_cast<int>(y + h - 5));
        }
    }
}

void drawTooltip(Gdiplus::Graphics* g, GUIState& state, int x, int y, const wchar_t* text) {
    Gdiplus::SizeF sz = measureText(g, text, getSmallFont());
    int tw = static_cast<int>(sz.Width) + 12;
    int th = static_cast<int>(sz.Height) + 8;

    drawRoundedRect(g, x, y, tw, th, 4, Theme::tooltipBg.toGdiplus(), Theme::border.toGdiplus());
    drawText(g, x + 6, y + 4, text, Theme::text.toGdiplus(), getSmallFont());
}

void drawStatus(Gdiplus::Graphics* g, GUIState& state, int screenWidth) {
    int barH = 24;
    int y = GetSystemMetrics(SM_CYSCREEN) - barH;

    Gdiplus::SolidBrush bgBrush(Theme::panel.toGdiplus());
    g->FillRectangle(&bgBrush, 0, y, screenWidth, barH);

    wchar_t wstatus[256];
    MultiByteToWideChar(CP_UTF8, 0, state.statusText, -1, wstatus, 256);
    drawText(g, 10, y + 3, wstatus, Theme::dimText.toGdiplus(), getSmallFont());

    wchar_t coord[64];
    wsprintfW(coord, L"鼠标: (%d, %d)", state.mouseX, state.mouseY);
    Gdiplus::SizeF coordSz = measureText(g, coord, getSmallFont());
    drawText(g, screenWidth - static_cast<int>(coordSz.Width) - 10, y + 3, coord,
             Theme::dimText.toGdiplus(), getSmallFont());
}

} // namespace GUI
