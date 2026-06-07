#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <string>
#include <unordered_map>
#include <cstdint>

// Cached brush / pen resources
struct D2DBrush {
    ID2D1SolidColorBrush* solid = nullptr;
};

class D2DRenderer {
public:
    D2DRenderer();
    ~D2DRenderer();

    // Initialize Direct2D, DirectWrite, WIC
    bool init(HWND hwnd);
    void shutdown();

    // Resize render target
    void resize(int w, int h);

    // Begin / end frame
    bool beginDraw();
    void endDraw();

    // Clear
    void clear(float r, float g, float b, float a = 1.0f);

    // ---- Drawing primitives ----
    void fillRect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f);
    void drawRect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f, float strokeWidth = 1.0f);
    void drawLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a = 1.0f, float width = 1.0f);
    void fillEllipse(float cx, float cy, float rx, float ry, float r, float g, float b, float a = 1.0f);
    void drawEllipse(float cx, float cy, float rx, float ry, float r, float g, float b, float a = 1.0f, float strokeWidth = 1.0f);

    // Text
    void drawText(const wchar_t* text, float x, float y, float w, float h,
                  float r, float g, float b, float a = 1.0f,
                  float fontSize = 12.0f, bool bold = false,
                  DWRITE_TEXT_ALIGNMENT hAlign = DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT vAlign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // Bitmap
    ID2D1Bitmap* createBitmapFromPngData(const uint8_t* data, int size);
    ID2D1Bitmap* createBitmapFromGdiplusBitmap(Gdiplus::Bitmap* bmp);
    void drawBitmap(ID2D1Bitmap* bmp, float x, float y, float w, float h, float opacity = 1.0f);
    void drawBitmapSubRect(ID2D1Bitmap* bmp, float dx, float dy, float dw, float dh,
                           float sx, float sy, float sw, float sh, float opacity = 1.0f);

    // Clip
    void pushClip(float x, float y, float w, float h);
    void popClip();

    // Transform
    void setTransform(float scaleX, float scaleY, float transX, float transY);
    void resetTransform();

    // Getters
    ID2D1HwndRenderTarget* getRenderTarget() { return pRT; }
    IDWriteFactory* getDWriteFactory() { return pDWriteFactory; }
    IWICImagingFactory* getWICFactory() { return pWICFactory; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }

private:
    ID2D1Factory*          pD2DFactory;
    ID2D1HwndRenderTarget* pRT;
    IDWriteFactory*        pDWriteFactory;
    IWICImagingFactory*    pWICFactory;

    // Cached brushes
    ID2D1SolidColorBrush*  pTempBrush;

    int width;
    int height;

    bool ensureBrush(float r, float g, float b, float a);
};
