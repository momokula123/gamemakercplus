#include "image_utils.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../libs/stb/stb_image.h"
#include "../libs/stb/stb_image_write.h"

#include <vector>

Gdiplus::Bitmap* loadBmpFromFile(const wchar_t* path) {
    int w, h, channels;
    char pathBuf[1024];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, pathBuf, sizeof(pathBuf), nullptr, nullptr);

    unsigned char* data = stbi_load(pathBuf, &w, &h, &channels, 4);
    if (!data) return nullptr;

    Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(w, h, PixelFormat32bppARGB);

    Gdiplus::BitmapData bmpData;
    Gdiplus::Rect rect(0, 0, w, h);
    bmp->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpData);

    unsigned char* dst = static_cast<unsigned char*>(bmpData.Scan0);
    int stride = bmpData.Stride;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int srcIdx = (y * w + x) * 4;
            int dstIdx = y * stride + x * 4;
            unsigned char a = data[srcIdx + 3];
            // Write as premultiplied alpha (GDI+ PixelFormat32bppARGB convention)
            dst[dstIdx + 0] = (unsigned char)((data[srcIdx + 2] * a + 127) / 255);
            dst[dstIdx + 1] = (unsigned char)((data[srcIdx + 1] * a + 127) / 255);
            dst[dstIdx + 2] = (unsigned char)((data[srcIdx + 0] * a + 127) / 255);
            dst[dstIdx + 3] = a;
        }
    }

    bmp->UnlockBits(&bmpData);
    stbi_image_free(data);

    return bmp;
}

bool saveBmpToFile(Gdiplus::Bitmap* bmp, const wchar_t* path) {
    if (!bmp) return false;

    CLSID pngClsid;
    BOOL found = FALSE;
    unsigned int num = 0;
    unsigned int size = 0;

    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return false;

    std::vector<Gdiplus::ImageCodecInfo> encoders(num);
    Gdiplus::GetImageEncoders(num, size, &encoders[0]);

    for (unsigned int i = 0; i < num; i++) {
        if (encoders[i].MimeType && wcscmp(encoders[i].MimeType, L"image/png") == 0) {
            pngClsid = encoders[i].Clsid;
            found = TRUE;
            break;
        }
    }

    if (!found) {
        for (unsigned int i = 0; i < num; i++) {
            if (encoders[i].MimeType && wcscmp(encoders[i].MimeType, L"image/bmp") == 0) {
                pngClsid = encoders[i].Clsid;
                found = TRUE;
                break;
            }
        }
    }

    if (!found) return false;

    return bmp->Save(path, &pngClsid, nullptr) == Gdiplus::Ok;
}

Gdiplus::Bitmap* resizeBitmap(Gdiplus::Bitmap* src, int newW, int newH) {
    if (!src) return nullptr;

    Gdiplus::Bitmap* dst = new Gdiplus::Bitmap(newW, newH, PixelFormat32bppARGB);
    Gdiplus::Graphics g(dst);

    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);

    g.DrawImage(src, 0, 0, newW, newH);

    return dst;
}

Gdiplus::Bitmap* createTileFromColor(int color, int size) {
    Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(size, size, PixelFormat32bppARGB);
    Gdiplus::Graphics g(bmp);

    BYTE r = (color >> 16) & 0xFF;
    BYTE gr = (color >> 8) & 0xFF;
    BYTE b = color & 0xFF;

    Gdiplus::SolidBrush fillBrush(Gdiplus::Color(255, r, gr, b));
    g.FillRectangle(&fillBrush, 0, 0, size, size);

    Gdiplus::Pen borderPen(Gdiplus::Color(128, 255, 255, 255), 1.0f);
    g.DrawRectangle(&borderPen, 0, 0, size - 1, size - 1);

    return bmp;
}

void stbToGdiplus(const unsigned char* data, int w, int h, int channels, Gdiplus::Bitmap** out) {
    if (!data || w <= 0 || h <= 0) {
        *out = nullptr;
        return;
    }

    *out = new Gdiplus::Bitmap(w, h, PixelFormat32bppARGB);

    Gdiplus::BitmapData bmpData;
    Gdiplus::Rect rect(0, 0, w, h);
    (*out)->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpData);

    unsigned char* dst = static_cast<unsigned char*>(bmpData.Scan0);
    int stride = bmpData.Stride;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int srcIdx = (y * w + x) * channels;
            int dstIdx = y * stride + x * 4;

            if (channels >= 3) {
                unsigned char a = (channels >= 4) ? data[srcIdx + 3] : 255;
                // Premultiply alpha for GDI+ PixelFormat32bppARGB
                dst[dstIdx + 0] = (unsigned char)((data[srcIdx + 2] * a + 127) / 255);
                dst[dstIdx + 1] = (unsigned char)((data[srcIdx + 1] * a + 127) / 255);
                dst[dstIdx + 2] = (unsigned char)((data[srcIdx + 0] * a + 127) / 255);
                dst[dstIdx + 3] = a;
            } else {
                dst[dstIdx + 0] = data[srcIdx];
                dst[dstIdx + 1] = data[srcIdx];
                dst[dstIdx + 2] = data[srcIdx];
                dst[dstIdx + 3] = 255;
            }
        }
    }

    (*out)->UnlockBits(&bmpData);
}
