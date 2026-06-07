#include "tile_generator.h"
#include <propkeydef.h>
#include <gdiplus.h>
#include <cstdlib>
#include <cstdio>

using namespace Gdiplus;

// ---------------------------------------------------------------------------
// Hash / noise helpers
// ---------------------------------------------------------------------------

float TileGenerator::hash(float x, float y) {
    // Simple hash-based pseudo-random for deterministic per-pixel variation
    unsigned int h = (unsigned int)(x * 374761393 + y * 668265263);
    h = (h ^ (h >> 13)) * 1274126177;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFF) / 65535.0f;
}

float TileGenerator::noise(float x, float y) {
    // Value noise with smooth interpolation
    int ix = (int)floorf(x);
    int iy = (int)floorf(y);
    float fx = x - (float)ix;
    float fy = y - (float)iy;
    // Smoothstep
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);

    float v00 = hash((float)ix, (float)iy);
    float v10 = hash((float)(ix + 1), (float)iy);
    float v01 = hash((float)ix, (float)(iy + 1));
    float v11 = hash((float)(ix + 1), (float)(iy + 1));

    float a = v00 + sx * (v10 - v00);
    float b = v01 + sx * (v11 - v01);
    return a + sy * (b - a);
}

// ---------------------------------------------------------------------------
// Save helper
// ---------------------------------------------------------------------------

void TileGenerator::saveBmp(Bitmap* bmp, const char* dir, const char* name) {
    CLSID pngClsid;
    UINT num = 0, sz = 0;
    GetImageEncodersSize(&num, &sz);
    if (sz == 0) return;
    ImageCodecInfo* info = (ImageCodecInfo*)malloc(sz);
    if (!info) return;
    GetImageEncoders(num, sz, info);
    bool found = false;
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) {
            pngClsid = info[i].Clsid;
            found = true;
            break;
        }
    }
    free(info);
    if (!found) return;
    char path[512];
    snprintf(path, sizeof(path), "%s\\%s.png", dir, name);
    int len = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t* wpath = new wchar_t[len];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, len);
    bmp->Save(wpath, &pngClsid, NULL);
    delete[] wpath;
}

// ---------------------------------------------------------------------------
// Helper: clamp a color component
// ---------------------------------------------------------------------------
static inline BYTE clmp(int v) { return (BYTE)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// ---------------------------------------------------------------------------
// Per-pixel base fill with noise variation
// ---------------------------------------------------------------------------
static void fillBaseNoise(Graphics* g, int S, BYTE r, BYTE gr, BYTE b, int variance, float seed) {
    BitmapData bd;
    Rect rect(0, 0, S, S);
    Bitmap* tmpBmp = new Bitmap(S, S, PixelFormat32bppARGB);
    tmpBmp->LockBits(&rect, ImageLockModeWrite, PixelFormat32bppARGB, &bd);
    BYTE* row = (BYTE*)bd.Scan0;
    for (int y = 0; y < S; y++) {
        BYTE* p = row;
        for (int x = 0; x < S; x++) {
            float n = TileGenerator::noise((float)x * 0.15f + seed, (float)y * 0.15f + seed * 1.7f);
            int v = (int)((n - 0.5f) * variance);
            p[0] = clmp(b + v);
            p[1] = clmp(gr + v);
            p[2] = clmp(r + v);
            p[3] = 255;
            p += 4;
        }
        row += bd.Stride;
    }
    tmpBmp->UnlockBits(&bd);
    g->DrawImage(tmpBmp, 0, 0, S, S);
    delete tmpBmp;
}

// ---------------------------------------------------------------------------
// Edge shading for depth (darken edges)
// ---------------------------------------------------------------------------
static void addEdgeShading(Graphics* g, int S, BYTE darken = 40) {
    // Top edge
    for (int i = 0; i < 4; i++) {
        int a = darken - i * (darken / 4);
        SolidBrush br(Color((BYTE)a, 0, 0, 0));
        g->FillRectangle(&br, 0, i, S, 1);
    }
    // Left edge
    for (int i = 0; i < 4; i++) {
        int a = darken - i * (darken / 4);
        SolidBrush br(Color((BYTE)a, 0, 0, 0));
        g->FillRectangle(&br, i, 0, 1, S);
    }
    // Bottom edge (lighter, highlight)
    for (int i = 0; i < 3; i++) {
        int a = darken / 2 - i * (darken / 6);
        SolidBrush br(Color((BYTE)a, 255, 255, 255));
        g->FillRectangle(&br, 0, S - 1 - i, S, 1);
    }
    // Right edge
    for (int i = 0; i < 3; i++) {
        int a = darken / 2 - i * (darken / 6);
        SolidBrush br(Color((BYTE)a, 255, 255, 255));
        g->FillRectangle(&br, S - 1 - i, 0, 1, S);
    }
}

// ---------------------------------------------------------------------------
// Generate individual tile types
// ---------------------------------------------------------------------------

static Bitmap* genGrass(int S) {
    Bitmap* bmp = new Bitmap(S, S, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Rich green base with noise
    fillBaseNoise(&g, S, 55, 145, 45, 30, 1.0f);

    // Darker grass blade patches (layer 1)
    srand(100);
    for (int i = 0; i < 30; i++) {
        int bx = rand() % S;
        int by = rand() % S;
        int len = 3 + rand() % 5;
        BYTE shade = (BYTE)(30 + rand() % 40);
        Pen pen(Color(180, shade, (BYTE)(shade + 80), 20), 1.0f);
        g.DrawLine(&pen, (REAL)bx, (REAL)by, (REAL)(bx + rand() % 3 - 1), (REAL)(by - len));
    }

    // Lighter grass highlight patches
    srand(101);
    for (int i = 0; i < 20; i++) {
        int bx = rand() % S;
        int by = rand() % S;
        int len = 2 + rand() % 4;
        Pen pen(Color(120, 80, 180, 40), 1.0f);
        g.DrawLine(&pen, (REAL)bx, (REAL)by, (REAL)(bx + rand() % 2), (REAL)(by - len));
    }

    // Small flower dots (red, yellow, white)
    srand(102);
    for (int i = 0; i < 8; i++) {
        int fx = 4 + rand() % (S - 8);
        int fy = 4 + rand() % (S - 8);
        int r = 1 + rand() % 2;
        int colorType = rand() % 3;
        Color fc;
        if (colorType == 0) fc = Color(220, 230, 60, 80);    // red
        else if (colorType == 1) fc = Color(230, 240, 220, 50); // yellow
        else fc = Color(200, 255, 255, 240);                   // white
        SolidBrush fb(fc);
        g.FillEllipse(&fb, (REAL)(fx - r), (REAL)(fy - r), (REAL)(r * 2), (REAL)(r * 2));
    }

    // Subtle shadow patches
    srand(103);
    for (int i = 0; i < 6; i++) {
        int sx = rand() % S;
        int sy = rand() % S;
        int sw = 4 + rand() % 8;
        int sh = 3 + rand() % 5;
        SolidBrush sb(Color(40, 20, 50, 15));
        g.FillEllipse(&sb, (REAL)sx, (REAL)sy, (REAL)sw, (REAL)sh);
    }

    addEdgeShading(&g, S, 30);
    return bmp;
}

static Bitmap* genSand(int S) {
    Bitmap* bmp = new Bitmap(S, S, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Warm tan base with noise
    fillBaseNoise(&g, S, 215, 195, 140, 25, 5.0f);

    // Grain dots (fine texture)
    srand(200);
    for (int i = 0; i < 60; i++) {
        int gx = rand() % S;
        int gy = rand() % S;
        int shade = 170 + rand() % 40;
        SolidBrush gb(Color(200, (BYTE)shade, (BYTE)(shade - 15), (BYTE)(shade - 50)));
        g.FillEllipse(&gb, (REAL)gx, (REAL)gy, 1.0f, 1.0f);
    }

    // Ripple lines (wind patterns)
    srand(201);
    for (int i = 0; i < 5; i++) {
        int ry = 8 + rand() % (S - 16);
        int rx = rand() % 10;
        int rlen = 20 + rand() % 30;
        Pen rp(Color(100, 200, 185, 145), 1.0f);
        g.DrawLine(&rp, (REAL)rx, (REAL)ry, (REAL)(rx + rlen), (REAL)(ry + rand() % 3 - 1));
    }

    // Scattered pebbles
    srand(202);
    for (int i = 0; i < 6; i++) {
        int px = 4 + rand() % (S - 8);
        int py = 4 + rand() % (S - 8);
        int pr = 1 + rand() % 2;
        SolidBrush pb(Color(220, 170, 150, 120));
        g.FillEllipse(&pb, (REAL)(px - pr), (REAL)(py - pr), (REAL)(pr * 2), (REAL)(pr * 2));
        // Highlight on pebble
        SolidBrush ph(Color(120, 210, 195, 165));
        g.FillEllipse(&ph, (REAL)(px - pr + 1), (REAL)(py - pr), 1.0f, 1.0f);
    }

    // Darker sand patches
    srand(203);
    for (int i = 0; i < 4; i++) {
        int sx = rand() % S;
        int sy = rand() % S;
        int sw = 5 + rand() % 10;
        int sh = 3 + rand() % 6;
        SolidBrush sb(Color(60, 185, 165, 115));
        g.FillEllipse(&sb, (REAL)sx, (REAL)sy, (REAL)sw, (REAL)sh);
    }

    addEdgeShading(&g, S, 25);
    return bmp;
}

static Bitmap* genWater(int S) {
    Bitmap* bmp = new Bitmap(S, S, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Deep blue base with noise
    fillBaseNoise(&g, S, 35, 80, 185, 30, 10.0f);

    // Wave streaks (lighter blue horizontal curves)
    srand(300);
    for (int i = 0; i < 8; i++) {
        int wy = 4 + rand() % (S - 8);
        int wx = rand() % 8;
        int wlen = 15 + rand() % 35;
        BYTE shade = (BYTE)(130 + rand() % 60);
        Pen wp(Color(160, shade, (BYTE)(shade + 30), 240), 1.5f);
        // Draw slightly wavy line
        int segs = wlen / 6;
        REAL prevX = (REAL)wx, prevY = (REAL)wy;
        for (int s = 0; s < segs; s++) {
            REAL nx = prevX + 6;
            REAL ny = prevY + (rand() % 3 - 1);
            g.DrawLine(&wp, prevX, prevY, nx, ny);
            prevX = nx; prevY = ny;
        }
    }

    // Foam dots (white/light blue)
    srand(301);
    for (int i = 0; i < 12; i++) {
        int fx = rand() % S;
        int fy = rand() % S;
        int fr = 1 + rand() % 2;
        SolidBrush fb(Color(180, 220, 235, 255));
        g.FillEllipse(&fb, (REAL)(fx - fr), (REAL)(fy - fr), (REAL)(fr * 2), (REAL)(fr * 2));
    }

    // Shimmer highlights (bright spots)
    srand(302);
    for (int i = 0; i < 5; i++) {
        int hx = 6 + rand() % (S - 12);
        int hy = 6 + rand() % (S - 12);
        SolidBrush hb(Color(100, 180, 210, 255));
        g.FillEllipse(&hb, (REAL)hx, (REAL)hy, 2.0f, 1.0f);
    }

    // Depth variation (darker patches)
    srand(303);
    for (int i = 0; i < 4; i++) {
        int dx = rand() % S;
        int dy = rand() % S;
        int dw = 6 + rand() % 12;
        int dh = 4 + rand() % 8;
        SolidBrush db(Color(50, 20, 50, 140));
        g.FillEllipse(&db, (REAL)dx, (REAL)dy, (REAL)dw, (REAL)dh);
    }

    addEdgeShading(&g, S, 35);
    return bmp;
}

static Bitmap* genStone(int S) {
    Bitmap* bmp = new Bitmap(S, S, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Gray base with noise
    fillBaseNoise(&g, S, 115, 110, 105, 25, 20.0f);

    // Stone block pattern (subtle grid lines)
    Pen gridPen(Color(60, 80, 75, 70), 1.0f);
    // Horizontal cracks
    srand(400);
    for (int i = 0; i < 3; i++) {
        int ly = 10 + rand() % (S - 20);
        int lx = rand() % 8;
        int lLen = 20 + rand() % 35;
        g.DrawLine(&gridPen, (REAL)lx, (REAL)ly, (REAL)(lx + lLen), (REAL)(ly + rand() % 3 - 1));
    }
    // Vertical cracks
    for (int i = 0; i < 3; i++) {
        int lx = 10 + rand() % (S - 20);
        int ly = rand() % 8;
        int lLen = 15 + rand() % 30;
        g.DrawLine(&gridPen, (REAL)lx, (REAL)ly, (REAL)(lx + rand() % 3 - 1), (REAL)(ly + lLen));
    }

    // Angular highlights
    srand(401);
    for (int i = 0; i < 5; i++) {
        int hx = 4 + rand() % (S - 8);
        int hy = 4 + rand() % (S - 8);
        int hs = 2 + rand() % 4;
        SolidBrush hb(Color(80, 160, 155, 145));
        g.FillRectangle(&hb, (REAL)hx, (REAL)hy, (REAL)hs, (REAL)(hs - 1));
    }

    // Moss patches (green spots)
    srand(402);
    for (int i = 0; i < 4; i++) {
        int mx = 6 + rand() % (S - 12);
        int my = 6 + rand() % (S - 12);
        int mr = 2 + rand() % 4;
        SolidBrush mb(Color(140, 60, 100, 40));
        g.FillEllipse(&mb, (REAL)(mx - mr), (REAL)(my - mr), (REAL)(mr * 2), (REAL)(mr * 2));
    }

    // Dark shadow spots
    srand(403);
    for (int i = 0; i < 6; i++) {
        int sx = rand() % S;
        int sy = rand() % S;
        int sr = 1 + rand() % 3;
        SolidBrush sb(Color(70, 50, 45, 40));
        g.FillEllipse(&sb, (REAL)(sx - sr), (REAL)(sy - sr), (REAL)(sr * 2), (REAL)(sr * 2));
    }

    addEdgeShading(&g, S, 40);
    return bmp;
}

static Bitmap* genDirt(int S) {
    Bitmap* bmp = new Bitmap(S, S, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Brown base with noise
    fillBaseNoise(&g, S, 150, 105, 70, 25, 30.0f);

    // Root lines (thin dark lines)
    srand(500);
    for (int i = 0; i < 6; i++) {
        int rx = rand() % S;
        int ry = rand() % S;
        int rLen = 5 + rand() % 15;
        float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
        Pen rp(Color(160, 80, 55, 35), 1.0f);
        g.DrawLine(&rp, (REAL)rx, (REAL)ry,
                   (REAL)(rx + cosf(angle) * rLen), (REAL)(ry + sinf(angle) * rLen));
    }

    // Pebble dots
    srand(501);
    for (int i = 0; i < 15; i++) {
        int px = rand() % S;
        int py = rand() % S;
        int pr = 1 + rand() % 2;
        BYTE shade = (BYTE)(110 + rand() % 40);
        SolidBrush pb(Color(220, shade, (BYTE)(shade - 15), (BYTE)(shade - 30)));
        g.FillEllipse(&pb, (REAL)(px - pr), (REAL)(py - pr), (REAL)(pr * 2), (REAL)(pr * 2));
    }

    // Moisture spots (darker patches)
    srand(502);
    for (int i = 0; i < 5; i++) {
        int mx = rand() % S;
        int my = rand() % S;
        int mw = 4 + rand() % 10;
        int mh = 3 + rand() % 7;
        SolidBrush mb(Color(60, 90, 60, 40));
        g.FillEllipse(&mb, (REAL)mx, (REAL)my, (REAL)mw, (REAL)mh);
    }

    // Lighter dirt highlights
    srand(503);
    for (int i = 0; i < 8; i++) {
        int hx = rand() % S;
        int hy = rand() % S;
        SolidBrush hb(Color(80, 175, 145, 100));
        g.FillEllipse(&hb, (REAL)hx, (REAL)hy, 2.0f, 2.0f);
    }

    addEdgeShading(&g, S, 30);
    return bmp;
}

static Bitmap* genLava(int S) {
    Bitmap* bmp = new Bitmap(S, S, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Dark red base with noise
    fillBaseNoise(&g, S, 75, 18, 8, 20, 40.0f);

    // Bright orange/yellow cracks (main feature)
    srand(600);
    for (int i = 0; i < 10; i++) {
        int cx = rand() % S;
        int cy = rand() % S;
        int cLen = 4 + rand() % 12;
        float angle = (float)(rand() % 360) * 3.14159f / 180.0f;
        int endX = (int)(cx + cosf(angle) * cLen);
        int endY = (int)(cy + sinf(angle) * cLen);
        // Glow (wider, dimmer)
        Pen glowPen(Color(100, 255, 140, 0), 3.0f);
        g.DrawLine(&glowPen, (REAL)cx, (REAL)cy, (REAL)endX, (REAL)endY);
        // Core (thin, bright)
        Pen corePen(Color(240, 255, 220, 50), 1.5f);
        g.DrawLine(&corePen, (REAL)cx, (REAL)cy, (REAL)endX, (REAL)endY);
    }

    // Glow spots (bright orange/yellow circles)
    srand(601);
    for (int i = 0; i < 6; i++) {
        int gx = 4 + rand() % (S - 8);
        int gy = 4 + rand() % (S - 8);
        int gr = 2 + rand() % 4;
        // Outer glow
        SolidBrush go(Color(80, 255, 120, 0));
        g.FillEllipse(&go, (REAL)(gx - gr - 1), (REAL)(gy - gr - 1),
                       (REAL)((gr + 1) * 2), (REAL)((gr + 1) * 2));
        // Inner bright
        SolidBrush gi(Color(200, 255, 230, 80));
        g.FillEllipse(&gi, (REAL)(gx - gr / 2), (REAL)(gy - gr / 2),
                       (REAL)(gr), (REAL)(gr));
    }

    // Heat distortion (subtle lighter streaks)
    srand(602);
    for (int i = 0; i < 5; i++) {
        int hx = rand() % S;
        int hy = rand() % S;
        int hLen = 3 + rand() % 8;
        Pen hp(Color(50, 200, 60, 20), 1.0f);
        g.DrawLine(&hp, (REAL)hx, (REAL)hy, (REAL)(hx + rand() % 4 - 2), (REAL)(hy - hLen));
    }

    // Dark cooled patches
    srand(603);
    for (int i = 0; i < 4; i++) {
        int dx = rand() % S;
        int dy = rand() % S;
        int dw = 4 + rand() % 8;
        int dh = 3 + rand() % 6;
        SolidBrush db(Color(120, 40, 10, 5));
        g.FillEllipse(&db, (REAL)dx, (REAL)dy, (REAL)dw, (REAL)dh);
    }

    addEdgeShading(&g, S, 50);
    return bmp;
}

static Bitmap* genSnow(int S) {
    Bitmap* bmp = new Bitmap(S, S, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // White-blue base with noise
    fillBaseNoise(&g, S, 235, 240, 250, 15, 50.0f);

    // Sparkle dots (bright white)
    srand(700);
    for (int i = 0; i < 15; i++) {
        int sx = rand() % S;
        int sy = rand() % S;
        int sr = 1;
        SolidBrush sb(Color(255, 255, 255, 255));
        g.FillEllipse(&sb, (REAL)(sx - sr), (REAL)(sy - sr), (REAL)(sr * 2), (REAL)(sr * 2));
    }

    // Drift patterns (subtle curved lighter streaks)
    srand(701);
    for (int i = 0; i < 6; i++) {
        int dy = 6 + rand() % (S - 12);
        int dx = rand() % 8;
        int dLen = 15 + rand() % 30;
        Pen dp(Color(80, 245, 248, 255), 1.0f);
        g.DrawLine(&dp, (REAL)dx, (REAL)dy, (REAL)(dx + dLen), (REAL)(dy + rand() % 3 - 1));
    }

    // Ice patches (slightly blue, translucent)
    srand(702);
    for (int i = 0; i < 4; i++) {
        int ix = 4 + rand() % (S - 8);
        int iy = 4 + rand() % (S - 8);
        int iw = 4 + rand() % 8;
        int ih = 3 + rand() % 6;
        SolidBrush ib(Color(100, 200, 220, 255));
        g.FillEllipse(&ib, (REAL)ix, (REAL)iy, (REAL)iw, (REAL)ih);
        // Highlight
        SolidBrush ih2(Color(60, 230, 240, 255));
        g.FillEllipse(&ih2, (REAL)(ix + 1), (REAL)(iy + 1), (REAL)(iw / 2), (REAL)(ih / 2));
    }

    // Shadow patches (very subtle blue-gray)
    srand(703);
    for (int i = 0; i < 4; i++) {
        int sx = rand() % S;
        int sy = rand() % S;
        int sw = 5 + rand() % 10;
        int sh = 3 + rand() % 6;
        SolidBrush sb(Color(40, 200, 210, 225));
        g.FillEllipse(&sb, (REAL)sx, (REAL)sy, (REAL)sw, (REAL)sh);
    }

    addEdgeShading(&g, S, 20);
    return bmp;
}

static Bitmap* genTree(int S) {
    Bitmap* bmp = new Bitmap(S, S, PixelFormat32bppARGB);
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Dark green base with noise
    fillBaseNoise(&g, S, 28, 55, 22, 20, 60.0f);

    // Tree trunk (brown circle at center-bottom area)
    {
        int trunkX = S / 2;
        int trunkY = S * 3 / 4;
        int trunkR = 4;
        SolidBrush trunkBr(Color(255, 100, 65, 30));
        g.FillEllipse(&trunkBr, (REAL)(trunkX - trunkR), (REAL)(trunkY - trunkR),
                       (REAL)(trunkR * 2), (REAL)(trunkR * 2));
        // Trunk highlight
        SolidBrush trunkHi(Color(180, 140, 90, 50));
        g.FillEllipse(&trunkHi, (REAL)(trunkX - trunkR + 1), (REAL)(trunkY - trunkR + 1),
                       (REAL)(trunkR), (REAL)(trunkR));
    }

    // Leaf clusters (multiple overlapping circles)
    srand(800);
    {
        // Main canopy
        int cx = S / 2;
        int cy = S / 3;
        // Large dark green cluster
        for (int i = 0; i < 5; i++) {
            int ox = cx - 8 + rand() % 16;
            int oy = cy - 6 + rand() % 12;
            int r = 6 + rand() % 6;
            SolidBrush lb(Color(200, 35, 95 + rand() % 30, 25 + rand() % 15));
            g.FillEllipse(&lb, (REAL)(ox - r), (REAL)(oy - r), (REAL)(r * 2), (REAL)(r * 2));
        }
        // Lighter leaf highlights
        for (int i = 0; i < 4; i++) {
            int ox = cx - 6 + rand() % 12;
            int oy = cy - 4 + rand() % 8;
            int r = 4 + rand() % 4;
            SolidBrush lb(Color(160, 55, 130 + rand() % 30, 40 + rand() % 20));
            g.FillEllipse(&lb, (REAL)(ox - r), (REAL)(oy - r), (REAL)(r * 2), (REAL)(r * 2));
        }
        // Bright leaf spots
        for (int i = 0; i < 3; i++) {
            int ox = cx - 4 + rand() % 8;
            int oy = cy - 3 + rand() % 6;
            int r = 2 + rand() % 3;
            SolidBrush lb(Color(120, 80, 160, 55));
            g.FillEllipse(&lb, (REAL)(ox - r), (REAL)(oy - r), (REAL)(r * 2), (REAL)(r * 2));
        }
    }

    // Shadow under tree
    {
        SolidBrush shadowBr(Color(60, 10, 25, 8));
        g.FillEllipse(&shadowBr, (REAL)(S / 2 - 12), (REAL)(S * 3 / 4 + 2), 24.0f, 8.0f);
    }

    // Small grass tufts around base
    srand(801);
    for (int i = 0; i < 6; i++) {
        int gx = rand() % S;
        int gy = S - 4 - rand() % 8;
        Pen gp(Color(160, 40, 110, 25), 1.0f);
        g.DrawLine(&gp, (REAL)gx, (REAL)gy, (REAL)(gx + rand() % 2 - 1), (REAL)(gy - 3));
    }

    addEdgeShading(&g, S, 35);
    return bmp;
}

// ---------------------------------------------------------------------------
// Public: generateTile
// ---------------------------------------------------------------------------

Bitmap* TileGenerator::generateTile(int index) {
    const int S = TILE_SIZE;
    switch (index) {
    case 0: return genGrass(S);
    case 1: return genSand(S);
    case 2: return genWater(S);
    case 3: return genStone(S);
    case 4: return genDirt(S);
    case 5: return genLava(S);
    case 6: return genSnow(S);
    case 7: return genTree(S);
    default: {
        // Fallback: gray tile
        Bitmap* bmp = new Bitmap(S, S, PixelFormat32bppARGB);
        Graphics g(bmp);
        SolidBrush br(Color(255, 128, 128, 128));
        g.FillRectangle(&br, 0, 0, S, S);
        return bmp;
    }
    }
}

// ---------------------------------------------------------------------------
// Public: generateAll
// ---------------------------------------------------------------------------

void TileGenerator::generateAll(const char* outputDir) {
    CreateDirectoryA(outputDir, NULL);
    const char* names[] = {"grass","sand","water","stone","dirt","lava","snow","tree"};

    for (int idx = 0; idx < 8; idx++) {
        Bitmap* bmp = generateTile(idx);
        if (bmp) {
            saveBmp(bmp, outputDir, names[idx]);
            delete bmp;
        }
    }
}
