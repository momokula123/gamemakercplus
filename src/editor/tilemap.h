#pragma once
#include <windows.h>
#include <propkeydef.h>
#include <gdiplus.h>
#include <d2d1.h>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <string>

class D2DRenderer;

struct Camera {
    float x = 0, y = 0;
    float zoom = 1.0f;
    float minZoom = 0.1f, maxZoom = 8.0f;

    void screenToWorld(float sx, float sy, float& wx, float& wy) {
        wx = sx / zoom + x;
        wy = sy / zoom + y;
    }
    void worldToScreen(float wx, float wy, float& sx, float& sy) {
        sx = (wx - x) * zoom;
        sy = (wy - y) * zoom;
    }
};

struct TileDef {
    std::wstring name;
    DWORD color;
    int category; // 0=terrain, 1=object, 2=character
    bool solid = false;
    int customW = 0; // 0 = use TILE_SIZE
    int customH = 0; // 0 = use TILE_SIZE
    // Source image grid: when a multi-cell image is imported,
    // the full image is stored here and gridCols/gridRows record the layout.
    // The tile's customTexture is just cell (0,0) for palette display.
    Gdiplus::Bitmap* sourceImage = nullptr;
    int gridCols = 0;
    int gridRows = 0;
};

// Per-tile terrain collision flag (layer 1)
struct TerrainCollision {
    bool solid = false;
};

// Collision capsule for characters - cross shape: horizontal ellipse + vertical ellipse
struct CollisionCapsule {
    float radiusX = 32.0f;      // horizontal ellipse half-width (touches left/right cell edges)
    float radiusY = 10.0f;      // horizontal ellipse half-height
    float radiusXv = 10.0f;     // vertical ellipse half-width
    float radiusYv = 32.0f;     // vertical ellipse half-height (touches top/bottom cell edges)
    float offsetY = 0.0f;       // offset from character center
};

// Character entity with visual + collision
struct Character {
    int id;
    std::wstring name;
    std::wstring type;        // "player", "npc", "enemy", "animal", "building", "decoration"
    float worldX, worldY;     // center position in world pixels
    uint8_t tileId;           // visual tile id for rendering
    CollisionCapsule capsule; // collision capsule (bound to center)
    int hp = 100;
    int attack = 10;
    int defense = 5;
    float speed = 1.0f;
    std::wstring script;      // script/event name

    // Sprite rendering
    int spriteId = -1;              // reference to AssetDatabase sprite record (-1 = none)
    Gdiplus::Bitmap* spriteSheet = nullptr;  // loaded sprite sheet bitmap
    int frameCount = 1;             // total number of animation frames (for backward compat)
    int frameWidth = 64;            // width of each frame
    int frameHeight = 64;           // height of each frame
    int animFps = 8;                // animation playback speed
    bool animLoop = true;           // loop animation
    int currentFrame = 0;           // current animation frame index (within current direction)
    float animTimer = 0.0f;         // animation timer accumulator
    float spriteScale = 1.0f;       // render scale
    int spriteCols = 1;             // columns in sprite sheet (frames per direction)
    int spriteRows = 1;             // rows in sprite sheet (number of directions)
    int facingDir = 0;              // current facing direction row (0=S, 1=W, 2=E, 3=N)
};

class TileMap {
public:
    static const int DEFAULT_SIZE = 64;
    static const int TILE_SIZE = 64;
    static const int NUM_LAYERS = 7;

    int width, height;

    // Layer 0: terrain visual (tile ids)
    // Layer 1: terrain collision (solid flags per tile)
    // Layer 2: character/decoration/building visual (tile ids + character references)
    // Layer 3: character collision (capsule rendering)
    std::vector<std::vector<uint8_t>> layers[NUM_LAYERS];
    std::vector<std::vector<TerrainCollision>> terrainCollision; // layer 1 data

    // Characters list
    std::vector<Character> characters;
    int nextCharacterId = 1;

    std::unordered_map<uint8_t, Gdiplus::Bitmap*> customTextures;
    std::unordered_map<uint8_t, ID2D1Bitmap*> d2dTextures;
    struct CellCacheKey {
        void* renderer;
        uint64_t cellKey;
        bool operator==(const CellCacheKey& o) const { return renderer == o.renderer && cellKey == o.cellKey; }
    };
    struct CellCacheKeyHash {
        size_t operator()(const CellCacheKey& k) const {
            return std::hash<void*>()(k.renderer) ^ (std::hash<uint64_t>()(k.cellKey) << 1);
        }
    };
    std::unordered_map<CellCacheKey, ID2D1Bitmap*, CellCacheKeyHash> cellD2DCache;
    std::unordered_map<int, ID2D1Bitmap*> characterD2dSprites; // charId -> D2D sprite sheet
    // For grid tiles: records the grid offset (gridX, gridY) for each cell
    // that was painted with a source-image tile.
    // Key = y * width + x, Value = gridX | (gridY << 16)
    std::unordered_map<int, int> gridOffsets;

    std::vector<TileDef> tileDefs;

    TileMap();
    ~TileMap();

    void init();
    void render(Gdiplus::Graphics* g, Camera& cam, int activeLayer, int winW, int winH);
    void setTile(int layer, int x, int y, uint8_t id);
    uint8_t getTile(int layer, int x, int y);
    void floodFill(int layer, int startX, int startY, uint8_t newId);
    bool inBounds(int x, int y);
    void clear();

    // Terrain collision (layer 1)
    void setTerrainCollision(int x, int y, bool solid);
    bool isTerrainSolid(int x, int y);
    void toggleTerrainCollision(int x, int y);

    // Character management (layer 2 + 3)
    int  createCharacter(const std::wstring& name, const std::wstring& type,
                         float worldX, float worldY, uint8_t tileId,
                         float capsuleRX = 16.0f, float capsuleRY = 24.0f,
                         int hp = 100, int atk = 10, int def = 5, float spd = 1.0f);
    void deleteCharacter(int id);
    Character* getCharacter(int id);
    Character* getCharacterAt(float worldX, float worldY, float tolerance = 32.0f);
    std::vector<Character*> getAllCharacters();
    void moveCharacter(int id, float newX, float newY);
    void resizeCapsule(int id, float rx, float ry);

    // Check if character collides with terrain
    bool checkCharacterTerrainCollision(int charId);

    void setCustomTexture(uint8_t id, Gdiplus::Bitmap* bmp);
    void convertTexturesToD2D(D2DRenderer* d2d,
                              std::unordered_map<uint8_t, ID2D1Bitmap*>* outTextures = nullptr);

    // Character sprite management
    void setCharacterSprite(int charId, Gdiplus::Bitmap* spriteSheet,
                            int frameCount, int frameW, int frameH, int fps, bool loop,
                            int cols = 0, int rows = 0);
    void updateCharacterAnimation(float dt);
    void convertCharacterSpritesToD2D(D2DRenderer* d2d,
                                       std::unordered_map<int, ID2D1Bitmap*>* outMap = nullptr);

    void renderD2D(D2DRenderer* d2d, Camera& cam, int activeLayer,
                   int winW, int winH,
                   const bool* layerVisible, const float* layerOpacity,
                   const int* renderOrder = nullptr,
                   std::unordered_map<uint8_t, ID2D1Bitmap*>* textures = nullptr,
                   std::unordered_map<int, ID2D1Bitmap*>* charSprites = nullptr);

    // Dynamic tile management
    int addTileDef(const std::wstring& name, DWORD color, int category = 0, bool solid = false, int customW = 0, int customH = 0);

    // Get the appropriate cell texture for a tile at grid position (gridX, gridY).
    // If the tile has a sourceImage, returns the corresponding cell slice.
    // Otherwise returns the tile's normal custom texture (or nullptr).
    Gdiplus::Bitmap* getCellTexture(uint8_t tileId, int gridX, int gridY);

    // Set/get grid offset for a cell (used by grid tiles)
    void setGridOffset(int x, int y, int gridX, int gridY);
    bool getGridOffset(int x, int y, int& gridX, int& gridY);
    bool removeTileDef(int tileId);
};
