#pragma once

#include <windows.h>
#include <propkeydef.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

Gdiplus::Bitmap* loadBmpFromFile(const wchar_t* path);
bool saveBmpToFile(Gdiplus::Bitmap* bmp, const wchar_t* path);
Gdiplus::Bitmap* resizeBitmap(Gdiplus::Bitmap* src, int newW, int newH);
Gdiplus::Bitmap* createTileFromColor(int color, int size = 32);
void stbToGdiplus(const unsigned char* data, int w, int h, int channels, Gdiplus::Bitmap** out);
