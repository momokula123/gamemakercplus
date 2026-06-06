#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cstdint>
#include <sqlite3.h>

// Tile record in database
struct TileRecord {
    int id;
    std::string name;
    std::string category;
    int width;
    int height;
    std::vector<uint8_t> pngData;
    bool animated;
    int frameCount;
    int frameWidth;
    int frameHeight;
    int fps;
    std::string filePath;
    int64_t fileMtime;
    std::string createdAt;
};

// Sprite / animation frame set
struct SpriteRecord {
    int id;
    std::string name;
    std::string category;   // "character", "effect", "item", etc.
    int sheetWidth;
    int sheetHeight;
    std::vector<uint8_t> pngData;
    int frameCount;
    int frameWidth;
    int frameHeight;
    int fps;
    bool loop;
    std::string createdAt;
};

// Single animation frame reference (for tile animations)
struct AnimFrame {
    int frameIndex;
    int tileId;         // reference to tile record
    int durationMs;
};

class AssetDatabase {
public:
    AssetDatabase();
    ~AssetDatabase();

    // Open / close database
    bool open(const char* path);
    void close();
    bool isOpen() const;

    // Initialize schema (creates tables if not exist)
    bool initSchema();

    // ---- Tile CRUD ----
    int  addTile(const char* name, const char* category,
                 int w, int h, const uint8_t* pngData, int pngSize,
                 bool animated = false, int frameCount = 1,
                 int frameW = 0, int frameH = 0, int fps = 0);
    bool updateTile(int id, const char* name, const char* category,
                    int w, int h, const uint8_t* pngData, int pngSize);
    bool deleteTile(int id);
    bool getTile(int id, TileRecord& out);
    std::vector<TileRecord> getAllTiles();
    std::vector<TileRecord> getTilesByCategory(const char* category);
    int  getTileCount();

    // Import tile from PNG file
    int  importTileFromFile(const char* filePath, const char* name,
                            const char* category);
    // Update existing tile from file (by path match)
    bool updateTileFromFile(const char* filePath, const char* name,
                            const char* category);
    // Get tile by file path
    bool getTileByPath(const char* filePath, TileRecord& out);
    // Export tile to PNG file
    bool exportTileToFile(int id, const char* filePath);

    // ---- Sprite CRUD ----
    int  addSprite(const char* name, const char* category,
                   int sheetW, int sheetH, const uint8_t* pngData, int pngSize,
                   int frameCount, int frameW, int frameH, int fps, bool loop);
    bool deleteSprite(int id);
    bool getSprite(int id, SpriteRecord& out);
    std::vector<SpriteRecord> getAllSprites();
    std::vector<SpriteRecord> getSpritesByCategory(const char* category);

    // Import sprite from PNG file
    int  importSpriteFromFile(const char* filePath, const char* name,
                              const char* category,
                              int frameCount, int frameW, int frameH,
                              int fps, bool loop);

    // ---- Tile Animation ----
    bool setTileAnimation(int tileId, const std::vector<AnimFrame>& frames);
    std::vector<AnimFrame> getTileAnimation(int tileId);

    // ---- Settings ----
    bool setSetting(const char* key, const char* value);
    std::string getSetting(const char* key, const char* defaultVal = "");

    // ---- Utility ----
    // Load tile PNG as GDI+ Bitmap
    Gdiplus::Bitmap* loadTileBitmap(int id);
    // Load sprite PNG as GDI+ Bitmap
    Gdiplus::Bitmap* loadSpriteBitmap(int id);

    // Get last error message
    const char* getLastError() const;

private:
    sqlite3* db;
    char lastError[512];

    bool execSql(const char* sql);
};
