#include "d2d_renderer.h"
#include <cstdio>
#include <cstdint>
#include <cmath>

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) { if(p) { (p)->Release(); (p)=nullptr; } }
#endif

D2DRenderer::D2DRenderer()
    : pD2DFactory(nullptr)
    , pRT(nullptr)
    , pDWriteFactory(nullptr)
    , pWICFactory(nullptr)
    , pTempBrush(nullptr)
    , width(0)
    , height(0) {
}

D2DRenderer::~D2DRenderer() {
    shutdown();
}

bool D2DRenderer::init(HWND hwnd) {
    HRESULT hr;

    // Create D2D factory
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pD2DFactory);
    if (FAILED(hr)) {
        printf("D2D: Failed to create factory\n");
        return false;
    }

    // Create DirectWrite factory
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(&pDWriteFactory));
    if (FAILED(hr)) {
        printf("D2D: Failed to create DirectWrite factory\n");
        return false;
    }

    // Create WIC factory
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                          CLSCTX_INPROC_SERVER,
                          __uuidof(IWICImagingFactory),
                          reinterpret_cast<void**>(&pWICFactory));
    if (FAILED(hr)) {
        printf("D2D: Failed to create WIC factory\n");
        return false;
    }

    // Get client rect
    RECT rc;
    GetClientRect(hwnd, &rc);
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;

    // Create HWND render target
    D2D1_SIZE_U size = D2D1::SizeU(width, height);
    hr = pD2DFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_HARDWARE,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
        D2D1::HwndRenderTargetProperties(hwnd, size,
            D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &pRT);

    if (FAILED(hr)) {
        // Fallback to software rendering
        hr = pD2DFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            D2D1::HwndRenderTargetProperties(hwnd, size,
                D2D1_PRESENT_OPTIONS_IMMEDIATELY),
            &pRT);

        if (FAILED(hr)) {
            printf("D2D: Failed to create render target (hw+sw)\n");
            return false;
        }
        printf("D2D: Using software fallback\n");
    }

    // Create temp brush for drawing
    hr = pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pTempBrush);
    if (FAILED(hr)) {
        printf("D2D: Failed to create brush\n");
        return false;
    }

    // Set text antialiasing
    pRT->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    printf("D2D: Initialized successfully (GPU accelerated)\n");
    return true;
}

void D2DRenderer::shutdown() {
    SAFE_RELEASE(pTempBrush);
    SAFE_RELEASE(pRT);
    SAFE_RELEASE(pDWriteFactory);
    SAFE_RELEASE(pWICFactory);
    SAFE_RELEASE(pD2DFactory);
}

void D2DRenderer::resize(int w, int h) {
    if (pRT) {
        D2D1_SIZE_U size = D2D1::SizeU(w, h);
        pRT->Resize(size);
    }
    width = w;
    height = h;
}

bool D2DRenderer::beginDraw() {
    if (!pRT) return false;
    pRT->BeginDraw();
    return true;
}

void D2DRenderer::endDraw() {
    if (!pRT) return;
    HRESULT hr = pRT->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        // Need to recreate resources
        SAFE_RELEASE(pTempBrush);
        SAFE_RELEASE(pRT);
        // Caller should re-init
    }
}

void D2DRenderer::clear(float r, float g, float b, float a) {
    if (!pRT) return;
    pRT->Clear(D2D1::ColorF(r, g, b, a));
}

bool D2DRenderer::ensureBrush(float r, float g, float b, float a) {
    if (!pTempBrush) return false;
    pTempBrush->SetColor(D2D1::ColorF(r, g, b, a));
    return true;
}

void D2DRenderer::fillRect(float x, float y, float w, float h,
                            float r, float g, float b, float a) {
    if (!pRT || !ensureBrush(r, g, b, a)) return;
    D2D1_RECT_F rect = D2D1::RectF(x, y, x + w, y + h);
    pRT->FillRectangle(&rect, pTempBrush);
}

void D2DRenderer::drawRect(float x, float y, float w, float h,
                            float r, float g, float b, float a, float strokeWidth) {
    if (!pRT || !ensureBrush(r, g, b, a)) return;
    D2D1_RECT_F rect = D2D1::RectF(x, y, x + w, y + h);
    pRT->DrawRectangle(&rect, pTempBrush, strokeWidth);
}

void D2DRenderer::drawLine(float x1, float y1, float x2, float y2,
                            float r, float g, float b, float a, float width) {
    if (!pRT || !ensureBrush(r, g, b, a)) return;
    D2D1_POINT_2F p0 = D2D1::Point2F(x1, y1);
    D2D1_POINT_2F p1 = D2D1::Point2F(x2, y2);
    pRT->DrawLine(p0, p1, pTempBrush, width);
}

void D2DRenderer::fillEllipse(float cx, float cy, float rx, float ry,
                               float r, float g, float b, float a) {
    if (!pRT || !ensureBrush(r, g, b, a)) return;
    D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry);
    pRT->FillEllipse(&ellipse, pTempBrush);
}

void D2DRenderer::drawEllipse(float cx, float cy, float rx, float ry,
                               float r, float g, float b, float a, float strokeWidth) {
    if (!pRT || !ensureBrush(r, g, b, a)) return;
    D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry);
    pRT->DrawEllipse(&ellipse, pTempBrush, strokeWidth);
}

void D2DRenderer::drawText(const wchar_t* text, float x, float y, float w, float h,
                            float r, float g, float b, float a,
                            float fontSize, bool bold,
                            DWRITE_TEXT_ALIGNMENT hAlign,
                            DWRITE_PARAGRAPH_ALIGNMENT vAlign) {
    if (!pRT || !pDWriteFactory || !ensureBrush(r, g, b, a)) return;

    IDWriteTextFormat* pFormat = nullptr;
    HRESULT hr = pDWriteFactory->CreateTextFormat(
        L"Microsoft YaHei", nullptr,
        bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"zh-CN", &pFormat);

    if (FAILED(hr)) {
        // Fallback
        hr = pDWriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr,
            bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize, L"", &pFormat);
    }

    if (FAILED(hr)) return;

    pFormat->SetTextAlignment(hAlign);
    pFormat->SetParagraphAlignment(vAlign);

    D2D1_RECT_F layoutRect = D2D1::RectF(x, y, x + w, y + h);
    pRT->DrawText(text, (UINT32)wcslen(text), pFormat, layoutRect, pTempBrush);

    SAFE_RELEASE(pFormat);
}

ID2D1Bitmap* D2DRenderer::createBitmapFromPngData(const uint8_t* data, int size) {
    if (!pRT || !pWICFactory) return nullptr;

    // Create WIC stream from memory
    IWICStream* pStream = nullptr;
    HRESULT hr = pWICFactory->CreateStream(&pStream);
    if (FAILED(hr)) return nullptr;

    // Need to copy data to a global memory block
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) { pStream->Release(); return nullptr; }
    void* pMem = GlobalLock(hMem);
    memcpy(pMem, data, size);
    GlobalUnlock(hMem);

    hr = pStream->InitializeFromMemory(reinterpret_cast<BYTE*>(hMem), size);
    // Note: WIC stream doesn't take ownership of hMem, but we need it alive during decoding

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pWICFactory->CreateDecoderFromStream(pStream, nullptr,
        WICDecodeMetadataCacheOnLoad, &pDecoder);

    if (FAILED(hr)) {
        pStream->Release();
        GlobalFree(hMem);
        return nullptr;
    }

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) {
        pDecoder->Release();
        pStream->Release();
        GlobalFree(hMem);
        return nullptr;
    }

    // Convert to 32bpp BGRA
    IWICFormatConverter* pConverter = nullptr;
    hr = pWICFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr)) {
        pFrame->Release();
        pDecoder->Release();
        pStream->Release();
        GlobalFree(hMem);
        return nullptr;
    }

    hr = pConverter->Initialize(pFrame,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);

    if (FAILED(hr)) {
        pConverter->Release();
        pFrame->Release();
        pDecoder->Release();
        pStream->Release();
        GlobalFree(hMem);
        return nullptr;
    }

    ID2D1Bitmap* pBitmap = nullptr;
    hr = pRT->CreateBitmapFromWicBitmap(pConverter, nullptr, &pBitmap);

    pConverter->Release();
    pFrame->Release();
    pDecoder->Release();
    pStream->Release();
    GlobalFree(hMem);

    if (FAILED(hr)) return nullptr;
    return pBitmap;
}

ID2D1Bitmap* D2DRenderer::createBitmapFromGdiplusBitmap(Gdiplus::Bitmap* bmp) {
    if (!pRT || !bmp) return nullptr;

    int w = bmp->GetWidth();
    int h = bmp->GetHeight();

    // Lock bitmap bits
    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData bmpData;
    if (bmp->LockBits(&rect, Gdiplus::ImageLockModeRead,
                      PixelFormat32bppARGB, &bmpData) != Gdiplus::Ok)
        return nullptr;

    // GDI+ PixelFormat32bppARGB is already premultiplied alpha.
    // D2D also expects premultiplied alpha. Pass directly.

    // Create D2D bitmap
    D2D1_SIZE_U size = D2D1::SizeU(w, h);
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ID2D1Bitmap* pBitmap = nullptr;
    HRESULT hr = pRT->CreateBitmap(size, bmpData.Scan0, bmpData.Stride, &props, &pBitmap);

    bmp->UnlockBits(&bmpData);

    if (FAILED(hr)) return nullptr;
    return pBitmap;
}

void D2DRenderer::drawBitmap(ID2D1Bitmap* bmp, float x, float y, float w, float h, float opacity) {
    if (!pRT || !bmp) return;
    D2D1_RECT_F dest = D2D1::RectF(x, y, x + w, y + h);
    pRT->DrawBitmap(bmp, dest, opacity);
}

void D2DRenderer::drawBitmapSubRect(ID2D1Bitmap* bmp,
                                     float dx, float dy, float dw, float dh,
                                     float sx, float sy, float sw, float sh,
                                     float opacity) {
    if (!pRT || !bmp) return;
    D2D1_RECT_F dest = D2D1::RectF(dx, dy, dx + dw, dy + dh);
    D2D1_RECT_F src = D2D1::RectF(sx, sy, sx + sw, sy + sh);
    pRT->DrawBitmap(bmp, dest, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
}

void D2DRenderer::pushClip(float x, float y, float w, float h) {
    if (!pRT) return;
    D2D1_RECT_F rect = D2D1::RectF(x, y, x + w, y + h);
    pRT->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
}

void D2DRenderer::popClip() {
    if (!pRT) return;
    pRT->PopAxisAlignedClip();
}

void D2DRenderer::setTransform(float scaleX, float scaleY, float transX, float transY) {
    if (!pRT) return;
    D2D1_MATRIX_3X2_F mat = D2D1::Matrix3x2F::Scale(scaleX, scaleY) *
                             D2D1::Matrix3x2F::Translation(transX, transY);
    pRT->SetTransform(mat);
}

void D2DRenderer::resetTransform() {
    if (!pRT) return;
    pRT->SetTransform(D2D1::IdentityMatrix());
}
