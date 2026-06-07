#include "tilemap.h"
#include "renderer/d2d_renderer.h"
#include <algorithm>
#include <cmath>
#include <queue>

TileMap::TileMap()
    : width(DEFAULT_SIZE), height(DEFAULT_SIZE)
{
    for (int i = 0; i < NUM_LAYERS; i++) {
        layers[i].resize(height);
        for (int y = 0; y < height; y++)
            layers[i][y].assign(width, 0);
    }

    terrainCollision.resize(height);
    for (int y = 0; y < height; y++)
        terrainCollision[y].assign(width, {});

    tileDefs.push_back({ L"\u7a7a",     0xFF323232, 0, false });
    tileDefs.push_back({ L"\u8349\u5730",   0xFF3C8C3C, 0, false });
    tileDefs.push_back({ L"\u6c99\u5730",   0xFFD2BE78, 0, false });
    tileDefs.push_back({ L"\u6c34\u57df",   0xFF2864C8, 0, false });
    tileDefs.push_back({ L"\u5ca9\u77f3",   0xFF828282, 0, false });
    tileDefs.push_back({ L"\u6ce5\u571f",   0xFF8C5A32, 0, false });
    tileDefs.push_back({ L"\u5ca9\u6d46",   0xFFDC3C1E, 0, false });
    tileDefs.push_back({ L"\u96ea\u5730",   0xFFE6EFF0, 0, false });
    tileDefs.push_back({ L"\u6811\u6728",   0xFF1E6428, 1, false });
}

TileMap::~TileMap() {
    for (auto& kv : customTextures)
        delete kv.second;
    customTextures.clear();
    for (auto& kv : cellD2DCache)
        if (kv.second) kv.second->Release();
    cellD2DCache.clear();
}

void TileMap::init() {
    int cx = width / 2;
    int cy = height / 2;

    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            layers[0][y][x] = 1;
            terrainCollision[y][x] = {};
        }

    int charId = createCharacter(L"\u73a9\u5bb6", L"player",
                    cx * TILE_SIZE, cy * TILE_SIZE, 1,
                    16.0f, 24.0f, 100, 10, 5, 1.0f);

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of(L'\\'));

    const wchar_t* spriteFiles[] = {
        L"assets\\sprites\\guy_walk.png",
        L"..\\assets\\sprites\\guy_walk.png",
        L"..\\..\\assets\\sprites\\guy_walk.png",
    };

    for (auto& sp : spriteFiles) {
        std::wstring fullPath = exeDir + L"\\" + sp;
        Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(fullPath.c_str());
        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
            int imgW = (int)bmp->GetWidth();
            int imgH = (int)bmp->GetHeight();
            int cols = imgW / 16;
            int rows = imgH / 32;
            if (cols >= 1 && rows >= 1) {
                setCharacterSprite(charId, bmp, cols * rows, 16, 32, 8, true, cols, rows);
            } else {
                delete bmp;
            }
            break;
        }
        delete bmp;
    }
}

void TileMap::clear() {
    for (auto& kv : cellD2DCache)
        if (kv.second) kv.second->Release();
    cellD2DCache.clear();
    for (int i = 0; i < NUM_LAYERS; i++)
        for (int y = 0; y < height; y++)
            std::fill(layers[i][y].begin(), layers[i][y].end(), 0);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            terrainCollision[y][x] = {};
    characters.clear();
}

void TileMap::setTile(int layer, int x, int y, uint8_t id) {
    if (layer < 0 || layer >= NUM_LAYERS) return;
    if (!inBounds(x, y)) return;
    layers[layer][y][x] = id;

    // No auto-sync: collision is managed separately on layer 1
}

uint8_t TileMap::getTile(int layer, int x, int y) {
    if (layer < 0 || layer >= NUM_LAYERS) return 0;
    if (!inBounds(x, y)) return 0;
    return layers[layer][y][x];
}

bool TileMap::inBounds(int x, int y) {
    return x >= 0 && x < width && y >= 0 && y < height;
}

// ---- Terrain Collision (layer 1) ----
void TileMap::setTerrainCollision(int x, int y, bool solid) {
    if (!inBounds(x, y)) return;
    terrainCollision[y][x].solid = solid;
}

bool TileMap::isTerrainSolid(int x, int y) {
    if (!inBounds(x, y)) return true;
    return terrainCollision[y][x].solid;
}

void TileMap::toggleTerrainCollision(int x, int y) {
    if (!inBounds(x, y)) return;
    terrainCollision[y][x].solid = !terrainCollision[y][x].solid;
}

// ---- Character Management (layer 2 + 3) ----
int TileMap::createCharacter(const std::wstring& name, const std::wstring& type,
                              float worldX, float worldY, uint8_t tileId,
                              float capsuleRX, float capsuleRY,
                              int hp, int atk, int def, float spd) {
    Character c;
    c.id = nextCharacterId++;
    c.name = name;
    c.type = type;
    c.worldX = worldX;
    c.worldY = worldY;
    c.tileId = tileId;
    c.capsule.radiusX = TILE_SIZE * 0.5f * 0.8f;
    c.capsule.radiusY = TILE_SIZE * 0.15f * 0.8f;
    c.capsule.radiusXv = TILE_SIZE * 0.15f * 0.8f;
    c.capsule.radiusYv = TILE_SIZE * 0.5f * 0.8f;
    c.capsule.offsetY = 0.0f;
    c.hp = hp;
    c.attack = atk;
    c.defense = def;
    c.speed = spd;
    characters.push_back(c);

    // Mark tile in layer 2
    int tx = (int)(worldX / TILE_SIZE);
    int ty = (int)(worldY / TILE_SIZE);
    if (inBounds(tx, ty))
        layers[3][ty][tx] = tileId;

    return c.id;
}

void TileMap::deleteCharacter(int id) {
    for (auto it = characters.begin(); it != characters.end(); ++it) {
        if (it->id == id) {
            // Clear layer 2 tile
            int tx = (int)(it->worldX / TILE_SIZE);
            int ty = (int)(it->worldY / TILE_SIZE);
            if (inBounds(tx, ty))
                layers[3][ty][tx] = 0;
            characters.erase(it);
            return;
        }
    }
}

Character* TileMap::getCharacter(int id) {
    for (auto& c : characters)
        if (c.id == id) return &c;
    return nullptr;
}

Character* TileMap::getCharacterAt(float worldX, float worldY, float tolerance) {
    Character* best = nullptr;
    float bestDist = tolerance;
    for (auto& c : characters) {
        float dx = c.worldX - worldX;
        float dy = c.worldY - worldY;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < bestDist) {
            bestDist = dist;
            best = &c;
        }
    }
    return best;
}

std::vector<Character*> TileMap::getAllCharacters() {
    std::vector<Character*> result;
    for (auto& c : characters)
        result.push_back(&c);
    return result;
}

void TileMap::moveCharacter(int id, float newX, float newY) {
    Character* c = getCharacter(id);
    if (!c) return;

    // Clear old tile
    int otx = (int)(c->worldX / TILE_SIZE);
    int oty = (int)(c->worldY / TILE_SIZE);

    c->worldX = newX;
    c->worldY = newY;

    // Set new tile
    int ntx = (int)(newX / TILE_SIZE);
    int nty = (int)(newY / TILE_SIZE);
    if (inBounds(ntx, nty))
        layers[3][nty][ntx] = c->tileId;
    if (inBounds(otx, oty) && (otx != ntx || oty != nty))
        layers[3][oty][otx] = 0;
}

void TileMap::resizeCapsule(int id, float rx, float ry) {
    Character* c = getCharacter(id);
    if (!c) return;
    c->capsule.radiusX = rx;
    c->capsule.radiusY = ry * 0.3f;
    c->capsule.radiusXv = rx * 0.3f;
    c->capsule.radiusYv = ry;
}

bool TileMap::checkCharacterTerrainCollision(int charId) {
    Character* c = getCharacter(charId);
    if (!c) return false;

    float hx = c->capsule.radiusX;
    float hy = c->capsule.radiusY;
    float vx = c->capsule.radiusXv;
    float vy = c->capsule.radiusYv;
    float oy = c->capsule.offsetY;
    float cx = c->worldX;
    float cy = c->worldY + oy;

    float maxR = std::max(std::max(hx, vx), std::max(hy, vy));
    int tx1 = (int)((cx - maxR) / TILE_SIZE);
    int ty1 = (int)((cy - maxR) / TILE_SIZE);
    int tx2 = (int)((cx + maxR) / TILE_SIZE);
    int ty2 = (int)((cy + maxR) / TILE_SIZE);

    for (int ty = ty1; ty <= ty2; ty++) {
        for (int tx = tx1; tx <= tx2; tx++) {
            if (!isTerrainSolid(tx, ty)) continue;
            float tileL = (float)(tx * TILE_SIZE);
            float tileT = (float)(ty * TILE_SIZE);
            float tileR = tileL + TILE_SIZE;
            float tileB = tileT + TILE_SIZE;
            float closestX = std::max(tileL, std::min(cx, tileR));
            float closestY = std::max(tileT, std::min(cy, tileB));
            float dx = closestX - cx;
            float dy = closestY - cy;
            if ((dx * dx) / (hx * hx) + (dy * dy) / (hy * hy) <= 1.0f)
                return true;
            if ((dx * dx) / (vx * vx) + (dy * dy) / (vy * vy) <= 1.0f)
                return true;
        }
    }
    return false;
}

void TileMap::setCustomTexture(uint8_t id, Gdiplus::Bitmap* bmp) {
    auto it = customTextures.find(id);
    if (it != customTextures.end())
        delete it->second;
    customTextures[id] = bmp;
    // Invalidate cell cache for this tile ID (across all renderers)
    for (auto it2 = cellD2DCache.begin(); it2 != cellD2DCache.end(); ) {
        if ((it2->first.cellKey >> 32) == (uint64_t)id) {
            if (it2->second) it2->second->Release();
            it2 = cellD2DCache.erase(it2);
        } else {
            ++it2;
        }
    }
}

void TileMap::floodFill(int layer, int startX, int startY, uint8_t newId) {
    if (layer < 0 || layer >= NUM_LAYERS) return;
    if (!inBounds(startX, startY)) return;

    auto& grid = layers[layer];
    uint8_t oldId = grid[startY][startX];
    if (oldId == newId) return;

    std::queue<std::pair<int, int>> q;
    q.push({startX, startY});

    std::vector<std::vector<bool>> visited(height);
    for (int i = 0; i < height; i++)
        visited[i].assign(width, false);
    visited[startY][startX] = true;

    const int dx[] = { 0, 0, -1, 1 };
    const int dy[] = { -1, 1, 0, 0 };

    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();

        if (grid[cy][cx] != oldId) continue;
        grid[cy][cx] = newId;

        // No auto-sync: collision is managed separately on layer 1

        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d];
            int ny = cy + dy[d];
            if (inBounds(nx, ny) && !visited[ny][nx] && grid[ny][nx] == oldId) {
                visited[ny][nx] = true;
                q.push({nx, ny});
            }
        }
    }
}

static Gdiplus::Color tileIdToColor(uint8_t id, const std::vector<TileDef>& tileDefs) {
    if (id < tileDefs.size()) {
        return Gdiplus::Color(tileDefs[id].color);
    }
    return Gdiplus::Color(255, 50, 50, 50);
}

void TileMap::render(Gdiplus::Graphics* g, Camera& cam, int activeLayer, int winW, int winH) {
    float tileWorldSize = (float)TILE_SIZE;
    float tileScreenSize = tileWorldSize * cam.zoom;
    if (tileScreenSize < 1.0f) return;

    int startX = (int)std::floor(cam.x / tileWorldSize);
    int startY = (int)std::floor(cam.y / tileWorldSize);
    int endX = (int)std::ceil((cam.x + (float)winW / cam.zoom) / tileWorldSize);
    int endY = (int)std::ceil((cam.y + (float)winH / cam.zoom) / tileWorldSize);

    startX = std::max(0, startX);
    startY = std::max(0, startY);
    endX = std::min(width, endX);
    endY = std::min(height, endY);

    // Helper lambda: render a tile layer (0, 1, 4, 5)
    auto renderTileLayer = [&](int layerIdx) {
        for (int ty = startY; ty < endY; ty++) {
            for (int tx = startX; tx < endX; tx++) {
                uint8_t id = layers[layerIdx][ty][tx];
                if (id == 0) continue;
                float sx, sy;
                cam.worldToScreen((float)(tx * TILE_SIZE), (float)(ty * TILE_SIZE), sx, sy);
                float tileSW = (float)TILE_SIZE * cam.zoom;
                float tileSH = (float)TILE_SIZE * cam.zoom;
                int gx, gy;
                if (getGridOffset(tx, ty, gx, gy) && id > 0 && id < (int)tileDefs.size() &&
                    tileDefs[id].sourceImage) {
                    Gdiplus::Bitmap* cellTex = getCellTexture(id, gx, gy);
                    if (cellTex) {
                        g->DrawImage(cellTex, (INT)sx, (INT)sy, (INT)tileSW, (INT)tileSH);
                        delete cellTex;
                    } else {
                        auto texIt = customTextures.find(id);
                        if (texIt != customTextures.end() && texIt->second)
                            g->DrawImage(texIt->second, (INT)sx, (INT)sy, (INT)tileSW, (INT)tileSH);
                        else {
                            Gdiplus::SolidBrush baseBrush(tileIdToColor(id, tileDefs));
                            g->FillRectangle(&baseBrush, sx, sy, tileSW, tileSH);
                        }
                    }
                } else {
                    auto texIt = customTextures.find(id);
                    if (texIt != customTextures.end() && texIt->second) {
                        g->DrawImage(texIt->second, (INT)sx, (INT)sy, (INT)tileSW, (INT)tileSH);
                    } else {
                        Gdiplus::SolidBrush baseBrush(tileIdToColor(id, tileDefs));
                        g->FillRectangle(&baseBrush, sx, sy, tileSW, tileSH);
                    }
                }
            }
        }
    };

    // Layer 0: terrain
    renderTileLayer(0);
    // Layer 1: draw (imported images above terrain)
    renderTileLayer(1);

    // Terrain collision overlay
    for (int ty = startY; ty < endY; ty++) {
        for (int tx = startX; tx < endX; tx++) {
            if (terrainCollision[ty][tx].solid) {
                float sx, sy;
                cam.worldToScreen((float)(tx * TILE_SIZE), (float)(ty * TILE_SIZE), sx, sy);
                Gdiplus::SolidBrush colBrush(Gdiplus::Color(128, 153, 0, 0));
                g->FillRectangle(&colBrush, sx, sy, tileScreenSize, tileScreenSize);
                Gdiplus::Pen colPen(Gdiplus::Color(255, 204, 0, 0), 2.0f);
                g->DrawRectangle(&colPen, sx, sy, tileScreenSize, tileScreenSize);
            }
        }
    }

    // Layer 3: characters
    for (const auto& c : characters) {
        float sx, sy;
        cam.worldToScreen(c.worldX - TILE_SIZE / 2, c.worldY - TILE_SIZE / 2, sx, sy);
        auto texIt = customTextures.find(c.tileId);
        if (texIt != customTextures.end() && texIt->second) {
            g->DrawImage(texIt->second, (INT)sx, (INT)sy, (INT)tileScreenSize, (INT)tileScreenSize);
        }
    }

    // Layer 4: foreground
    renderTileLayer(4);
    // Layer 5: foreground draw (imported images above foreground)
    renderTileLayer(5);

    // Character collision (cross: horizontal ellipse + vertical ellipse)
    for (const auto& c : characters) {
        float cx_s, cy_s;
        cam.worldToScreen(c.worldX, c.worldY + c.capsule.offsetY, cx_s, cy_s);
        float rx_h = c.capsule.radiusX * cam.zoom;
        float ry_h = c.capsule.radiusY * cam.zoom;
        float rx_v = c.capsule.radiusXv * cam.zoom;
        float ry_v = c.capsule.radiusYv * cam.zoom;

        Gdiplus::SolidBrush capBrush(Gdiplus::Color(40, 0, 255, 128));
        Gdiplus::Pen capPen(Gdiplus::Color(200, 0, 255, 128), 2.0f);

        // Horizontal ellipse
        g->FillEllipse(&capBrush, cx_s - rx_h, cy_s - ry_h, rx_h * 2, ry_h * 2);
        g->DrawEllipse(&capPen, cx_s - rx_h, cy_s - ry_h, rx_h * 2, ry_h * 2);

        // Vertical ellipse
        g->FillEllipse(&capBrush, cx_s - rx_v, cy_s - ry_v, rx_v * 2, ry_v * 2);
        g->DrawEllipse(&capPen, cx_s - rx_v, cy_s - ry_v, rx_v * 2, ry_v * 2);

        // Center dot
        Gdiplus::SolidBrush dotBrush(Gdiplus::Color(255, 0, 255, 128));
        g->FillEllipse(&dotBrush, (Gdiplus::REAL)(cx_s - 3), (Gdiplus::REAL)(cy_s - 3),
                       (Gdiplus::REAL)6, (Gdiplus::REAL)6);
    }

    // Grid
    if (cam.zoom > 0.25f) {
        Gdiplus::Pen gridPen(Gdiplus::Color(40, 180, 180, 180), 1.0f);
        for (int ty = startY; ty <= endY; ty++) {
            float gx, gy;
            cam.worldToScreen(0.0f, (float)(ty * TILE_SIZE), gx, gy);
            g->DrawLine(&gridPen, 0.0f, gy, (float)winW, gy);
        }
        for (int tx = startX; tx <= endX; tx++) {
            float gx, gy;
            cam.worldToScreen((float)(tx * TILE_SIZE), 0.0f, gx, gy);
            g->DrawLine(&gridPen, gx, 0.0f, gx, (float)winH);
        }
    }
}

void TileMap::convertTexturesToD2D(D2DRenderer* d2d,
                                    std::unordered_map<uint8_t, ID2D1Bitmap*>* outTextures) {
    if (!d2d) return;
    auto& target = outTextures ? *outTextures : d2dTextures;
    for (auto& kv : customTextures) {
        if (kv.second && target.find(kv.first) == target.end()) {
            ID2D1Bitmap* bmp = d2d->createBitmapFromGdiplusBitmap(kv.second);
            if (bmp) target[kv.first] = bmp;
        }
    }
}

void TileMap::setCharacterSprite(int charId, Gdiplus::Bitmap* spriteSheet,
                                  int frameCount, int frameW, int frameH, int fps, bool loop,
                                  int cols, int rows) {
    Character* c = getCharacter(charId);
    if (!c) return;

    // Delete old sprite sheet if owned
    if (c->spriteSheet) {
        delete c->spriteSheet;
        c->spriteSheet = nullptr;
    }

    c->spriteSheet = spriteSheet;
    c->frameCount = frameCount;
    c->frameWidth = frameW;
    c->frameHeight = frameH;
    c->animFps = fps;
    c->animLoop = loop;
    c->currentFrame = 0;
    c->animTimer = 0.0f;

    // Grid layout: if cols/rows specified, use them; otherwise auto-detect
    if (cols > 0 && rows > 0) {
        c->spriteCols = cols;
        c->spriteRows = rows;
    } else if (frameW > 0 && frameH > 0 && spriteSheet) {
        // Auto-detect from sprite sheet dimensions
        int sheetW = (int)spriteSheet->GetWidth();
        int sheetH = (int)spriteSheet->GetHeight();
        c->spriteCols = sheetW / frameW;
        c->spriteRows = sheetH / frameH;
        if (c->spriteCols < 1) c->spriteCols = 1;
        if (c->spriteRows < 1) c->spriteRows = 1;
    } else {
        c->spriteCols = frameCount;
        c->spriteRows = 1;
    }
}

void TileMap::updateCharacterAnimation(float dt) {
    for (auto& c : characters) {
        if (!c.spriteSheet || c.frameCount <= 1) continue;

        c.animTimer += dt;
        float frameDuration = 1.0f / (float)c.animFps;

        // For grid sprite sheets, animate within current direction's frames
        int framesPerDir = (c.spriteRows > 1) ? c.spriteCols : c.frameCount;

        while (c.animTimer >= frameDuration) {
            c.animTimer -= frameDuration;
            c.currentFrame++;
            if (c.currentFrame >= framesPerDir) {
                c.currentFrame = c.animLoop ? 0 : framesPerDir - 1;
                if (!c.animLoop) c.animTimer = 0.0f;
            }
        }
    }
}

void TileMap::convertCharacterSpritesToD2D(D2DRenderer* d2d,
                                             std::unordered_map<int, ID2D1Bitmap*>* outMap) {
    if (!d2d) return;
    auto& target = outMap ? *outMap : characterD2dSprites;
    for (auto& c : characters) {
        if (c.spriteSheet && target.find(c.id) == target.end()) {
            ID2D1Bitmap* bmp = d2d->createBitmapFromGdiplusBitmap(c.spriteSheet);
            if (bmp) target[c.id] = bmp;
        }
    }
}

void TileMap::renderD2D(D2DRenderer* r, Camera& cam, int activeLayer,
                        int winW, int winH,
                        const bool* layerVisible, const float* layerOpacity,
                        const int* renderOrder,
                        std::unordered_map<uint8_t, ID2D1Bitmap*>* textures,
                        std::unordered_map<int, ID2D1Bitmap*>* charSprites) {
    float tileWorldSize = (float)TILE_SIZE;
    float tileScreenSize = tileWorldSize * cam.zoom;
    if (tileScreenSize < 1.0f) return;

    int startX = (int)std::floor(cam.x / tileWorldSize);
    int startY = (int)std::floor(cam.y / tileWorldSize);
    int endX = (int)std::ceil((cam.x + (float)winW / cam.zoom) / tileWorldSize);
    int endY = (int)std::ceil((cam.y + (float)winH / cam.zoom) / tileWorldSize);

    startX = std::max(0, startX);
    startY = std::max(0, startY);
    endX = std::min(width, endX);
    endY = std::min(height, endY);

    auto& tex = textures ? *textures : d2dTextures;

    float opa[7] = {
        layerOpacity ? layerOpacity[0] : 1.0f,  // 0: terrain
        layerOpacity ? layerOpacity[1] : 1.0f,  // 1: draw
        layerOpacity ? layerOpacity[2] : 0.6f,  // 2: terrain collision
        layerOpacity ? layerOpacity[3] : 1.0f,  // 3: characters
        layerOpacity ? layerOpacity[4] : 1.0f,  // 4: foreground
        layerOpacity ? layerOpacity[5] : 1.0f,  // 5: fg draw
        layerOpacity ? layerOpacity[6] : 0.5f   // 6: char collision
    };

    int defaultOrder[7] = { 0, 1, 2, 3, 4, 5, 6 };
    const int* order = renderOrder ? renderOrder : defaultOrder;

    for (int oi = 0; oi < 7; oi++) {
        int li = order[oi];

        if (layerVisible && !layerVisible[li]) continue;

        if (li == 0 || li == 1 || li == 4 || li == 5) {
            // Tile layers: terrain, draw, foreground, fg_draw
            for (int ty = startY; ty < endY; ty++) {
                for (int tx = startX; tx < endX; tx++) {
                    uint8_t id = layers[li][ty][tx];
                    if (id == 0) continue;
                    float sx, sy;
                    cam.worldToScreen((float)(tx * TILE_SIZE), (float)(ty * TILE_SIZE), sx, sy);
                    float tsw = (float)TILE_SIZE * cam.zoom;
                    float tsh = (float)TILE_SIZE * cam.zoom;
                    int gx, gy;
                    if (getGridOffset(tx, ty, gx, gy) && id > 0 && id < (int)tileDefs.size() &&
                        tileDefs[id].sourceImage) {
                        uint64_t cellKey = ((uint64_t)id << 32) | ((uint64_t)gy << 16) | (uint64_t)gx;
                        CellCacheKey cacheKey{r, cellKey};
                        auto cacheIt = cellD2DCache.find(cacheKey);
                        if (cacheIt != cellD2DCache.end()) {
                            r->drawBitmap(cacheIt->second, sx, sy, tsw, tsh, opa[li]);
                        } else {
                            Gdiplus::Bitmap* cellTex = getCellTexture(id, gx, gy);
                            if (cellTex) {
                                ID2D1Bitmap* d2dCell = r->createBitmapFromGdiplusBitmap(cellTex);
                                if (d2dCell) {
                                    r->drawBitmap(d2dCell, sx, sy, tsw, tsh, opa[li]);
                                    cellD2DCache[cacheKey] = d2dCell;
                                }
                                delete cellTex;
                            }
                        }
                    } else {
                        auto d2dIt = tex.find(id);
                        if (d2dIt != tex.end() && d2dIt->second) {
                            r->drawBitmap(d2dIt->second, sx, sy, tsw, tsh, opa[li]);
                        } else {
                            Gdiplus::Color c = tileIdToColor(id, tileDefs);
                            r->fillRect(sx, sy, tsw, tsh,
                                        c.GetR() / 255.0f, c.GetG() / 255.0f, c.GetB() / 255.0f, opa[li]);
                        }
                    }
                }
            }
        } else if (li == 2) {
            for (int ty = startY; ty < endY; ty++) {
                for (int tx = startX; tx < endX; tx++) {
                    if (terrainCollision[ty][tx].solid) {
                        float sx, sy;
                        cam.worldToScreen((float)(tx * TILE_SIZE), (float)(ty * TILE_SIZE), sx, sy);
                        r->fillRect(sx, sy, tileScreenSize, tileScreenSize,
                                    0.6f, 0.0f, 0.0f, 0.5f * opa[1]);
                        r->drawRect(sx, sy, tileScreenSize, tileScreenSize,
                                    0.8f, 0.0f, 0.0f, 1.0f * opa[1], 2.0f);
                    }
                }
            }
        } else if (li == 3) {
            auto& sprites = charSprites ? *charSprites : characterD2dSprites;
            for (const auto& c : characters) {
                auto spriteIt = sprites.find(c.id);
                if (spriteIt != sprites.end() && spriteIt->second && c.spriteSheet) {
                    float drawW = c.frameWidth * c.spriteScale;
                    float drawH = c.frameHeight * c.spriteScale;
                    float sx, sy;
                    cam.worldToScreen(c.worldX, c.worldY, sx, sy);
                    sx -= drawW * cam.zoom / 2;
                    sy -= drawH * cam.zoom / 2;
                    drawW *= cam.zoom;
                    drawH *= cam.zoom;
                    float srcX = (float)(c.currentFrame * c.frameWidth);
                    float srcY = (float)(c.facingDir * c.frameHeight);
                    r->drawBitmapSubRect(spriteIt->second,
                                         sx, sy, drawW, drawH,
                                         srcX, srcY, (float)c.frameWidth, (float)c.frameHeight,
                                         opa[3]);
                } else {
                    float sx, sy;
                    cam.worldToScreen(c.worldX - TILE_SIZE / 2, c.worldY - TILE_SIZE / 2, sx, sy);
                    auto d2dIt = tex.find(c.tileId);
                    if (d2dIt != tex.end() && d2dIt->second) {
                        r->drawBitmap(d2dIt->second, sx, sy, tileScreenSize, tileScreenSize, opa[3]);
                    } else {
                        float typeR, typeG, typeB;
                        if (c.type == L"player") { typeR = 0.2f; typeG = 0.8f; typeB = 1.0f; }
                        else if (c.type == L"npc") { typeR = 0.2f; typeG = 1.0f; typeB = 0.4f; }
                        else if (c.type == L"enemy") { typeR = 1.0f; typeG = 0.3f; typeB = 0.2f; }
                        else { typeR = 0.7f; typeG = 0.7f; typeB = 0.7f; }
                        r->fillRect(sx, sy, tileScreenSize, tileScreenSize, typeR, typeG, typeB, opa[3]);
                        r->drawRect(sx + 1, sy + 1, tileScreenSize - 2, tileScreenSize - 2,
                                    1.0f, 1.0f, 1.0f, opa[3], 1.0f);
                    }
                }
            }
        } else if (li == 6) {
            for (const auto& c : characters) {
                float cx_s, cy_s;
                cam.worldToScreen(c.worldX, c.worldY + c.capsule.offsetY, cx_s, cy_s);
                float rx_h = c.capsule.radiusX * cam.zoom;
                float ry_h = c.capsule.radiusY * cam.zoom;
                float rx_v = c.capsule.radiusXv * cam.zoom;
                float ry_v = c.capsule.radiusYv * cam.zoom;

                r->fillEllipse(cx_s, cy_s, rx_h, ry_h,
                               0.0f, 1.0f, 0.502f, 0.157f * opa[6]);
                r->drawEllipse(cx_s, cy_s, rx_h, ry_h,
                               0.0f, 1.0f, 0.502f, 0.502f * opa[6], 2.0f);

                r->fillEllipse(cx_s, cy_s, rx_v, ry_v,
                               0.0f, 1.0f, 0.502f, 0.157f * opa[6]);
                r->drawEllipse(cx_s, cy_s, rx_v, ry_v,
                               0.0f, 1.0f, 0.502f, 0.502f * opa[6], 2.0f);

                r->fillEllipse(cx_s, cy_s, 3.0f * cam.zoom, 3.0f * cam.zoom,
                               1.0f, 1.0f, 0.0f, 0.784f * opa[6]);

                if (cam.zoom > 0.5f) {
                    float nameX, nameY;
                    cam.worldToScreen(c.worldX, c.worldY - TILE_SIZE * 0.6f, nameX, nameY);
                    r->drawText(c.name.c_str(), nameX - 40.0f, nameY - 8.0f, 80.0f, 16.0f,
                                1.0f, 1.0f, 1.0f, 0.863f * opa[6], 8.0f, false,
                                DWRITE_TEXT_ALIGNMENT_CENTER);
                }
            }
        }
    }

    // Grid
    if (cam.zoom > 0.25f) {
        for (int ty = startY; ty <= endY; ty++) {
            float gx, gy;
            cam.worldToScreen(0.0f, (float)(ty * TILE_SIZE), gx, gy);
            r->drawLine(0.0f, gy, (float)winW, gy, 0.706f, 0.706f, 0.706f, 0.157f);
        }
        for (int tx = startX; tx <= endX; tx++) {
            float gx, gy;
            cam.worldToScreen((float)(tx * TILE_SIZE), 0.0f, gx, gy);
            r->drawLine(gx, 0.0f, gx, (float)winH, 0.706f, 0.706f, 0.706f, 0.157f);
        }
    }
}

// ---- Dynamic Tile Management ----

int TileMap::addTileDef(const std::wstring& name, DWORD color, int category, bool solid, int customW, int customH) {
    if (tileDefs.size() >= 256)
        return -1;
    TileDef td;
    td.name = name;
    td.color = color;
    td.category = category;
    td.solid = solid;
    td.customW = customW;
    td.customH = customH;
    tileDefs.push_back(td);
    return (int)(tileDefs.size() - 1);
}

bool TileMap::removeTileDef(int tileId) {
    if (tileId <= 0 || tileId >= (int)tileDefs.size())
        return false;

    // Mark as empty (can't erase from middle because indices are tile IDs)
    tileDefs[tileId] = { L"\u7a7a", 0xFF323232, 0, false };

    // Scan all layers and replace references to this tileId with 0
    for (int layer = 0; layer < NUM_LAYERS; layer++) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (layers[layer][y][x] == tileId)
                    layers[layer][y][x] = 0;
            }
        }
    }

    // Clean up custom textures
    auto gdiIt = customTextures.find((uint8_t)tileId);
    if (gdiIt != customTextures.end()) {
        delete gdiIt->second;
        customTextures.erase(gdiIt);
    }
    auto d2dIt = d2dTextures.find((uint8_t)tileId);
    if (d2dIt != d2dTextures.end()) {
        d2dIt->second->Release();
        d2dTextures.erase(d2dIt);
    }

    return true;
}

Gdiplus::Bitmap* TileMap::getCellTexture(uint8_t tileId, int gridX, int gridY) {
    if (tileId <= 0 || tileId >= (int)tileDefs.size())
        return nullptr;
    const TileDef& td = tileDefs[tileId];
    if (!td.sourceImage || td.gridCols <= 0 || td.gridRows <= 0)
        return nullptr;
    if (gridX < 0 || gridX >= td.gridCols || gridY < 0 || gridY >= td.gridRows)
        return nullptr;
    int cellPx = gridX * TILE_SIZE;
    int cellPy = gridY * TILE_SIZE;
    int copyW = std::min(TILE_SIZE, (int)(td.sourceImage->GetWidth() - cellPx));
    int copyH = std::min(TILE_SIZE, (int)(td.sourceImage->GetHeight() - cellPy));
    if (copyW <= 0 || copyH <= 0)
        return nullptr;
    Gdiplus::Bitmap* cell = new Gdiplus::Bitmap(TILE_SIZE, TILE_SIZE, PixelFormat32bppARGB);
    Gdiplus::Graphics g(cell);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    g.DrawImage(td.sourceImage, Gdiplus::Rect(0, 0, copyW, copyH), cellPx, cellPy, copyW, copyH, Gdiplus::UnitPixel);
    return cell;
}

void TileMap::setGridOffset(int x, int y, int gridX, int gridY) {
    if (inBounds(x, y))
        gridOffsets[y * width + x] = gridX | (gridY << 16);
}

bool TileMap::getGridOffset(int x, int y, int& gridX, int& gridY) {
    if (!inBounds(x, y)) return false;
    auto it = gridOffsets.find(y * width + x);
    if (it == gridOffsets.end()) return false;
    gridX = it->second & 0xFFFF;
    gridY = (it->second >> 16) & 0xFFFF;
    return true;
}

