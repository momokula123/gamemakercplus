#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <cstdio>

class TileGenerator {
public:
    static const int TILE_SIZE = 64;
    static void generateAll(const char* outputDir);
    // Generate a single tile bitmap by index (0=grass, 1=sand, 2=water, 3=stone, 4=dirt, 5=lava, 6=snow, 7=tree)
    static Gdiplus::Bitmap* generateTile(int index);
    static float noise(float x, float y);
    static float hash(float x, float y);
private:
    static void saveBmp(Gdiplus::Bitmap* bmp, const char* dir, const char* name);
};
