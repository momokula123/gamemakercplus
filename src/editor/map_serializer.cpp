#include "map_serializer.h"
#include <algorithm>
#include "tilemap.h"
#include <cstring>

static const uint32_t FORMAT_VERSION = 1;
static const char MAGIC[4] = { 'G', 'M', 'A', 'P' };

MapSerializer::MapSerializer() {}

void MapSerializer::writeUint32(FILE* f, uint32_t val) {
    fwrite(&val, sizeof(uint32_t), 1, f);
}

uint32_t MapSerializer::readUint32(FILE* f) {
    uint32_t val = 0;
    fread(&val, sizeof(uint32_t), 1, f);
    return val;
}

void MapSerializer::writeString(FILE* f, const std::wstring& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    writeUint32(f, len);
    if (len > 0) {
        fwrite(str.data(), sizeof(wchar_t), len, f);
    }
}

std::wstring MapSerializer::readString(FILE* f) {
    uint32_t len = readUint32(f);
    if (len == 0) return L"";
    std::wstring str(len, L'\0');
    fread(str.data(), sizeof(wchar_t), len, f);
    return str;
}

bool MapSerializer::saveMap(const char* path, TileMap* map) {
    if (!map) return false;

    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    if (!f) return false;

    fwrite(MAGIC, 1, 4, f);

    uint32_t version = FORMAT_VERSION;
    fwrite(&version, sizeof(uint32_t), 1, f);

    uint32_t w = (uint32_t)map->width;
    uint32_t h = (uint32_t)map->height;
    fwrite(&w, sizeof(uint32_t), 1, f);
    fwrite(&h, sizeof(uint32_t), 1, f);

    uint32_t ts = (uint32_t)TileMap::TILE_SIZE;
    fwrite(&ts, sizeof(uint32_t), 1, f);

    uint32_t numLayers = TileMap::NUM_LAYERS;
    fwrite(&numLayers, sizeof(uint32_t), 1, f);

    for (uint32_t layer = 0; layer < numLayers; layer++) {
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint8_t id = map->layers[layer][y][x];
                fwrite(&id, sizeof(uint8_t), 1, f);
            }
        }
    }

    fclose(f);
    return true;
}

bool MapSerializer::loadMap(const char* path, TileMap* map) {
    if (!map) return false;

    FILE* f = nullptr;
    fopen_s(&f, path, "rb");
    if (!f) return false;

    char magic[4] = {};
    fread(magic, 1, 4, f);
    if (memcmp(magic, MAGIC, 4) != 0) {
        fclose(f);
        return false;
    }

    uint32_t version = 0;
    fread(&version, sizeof(uint32_t), 1, f);
    if (version > FORMAT_VERSION) {
        fclose(f);
        return false;
    }

    uint32_t w = 0, h = 0;
    fread(&w, sizeof(uint32_t), 1, f);
    fread(&h, sizeof(uint32_t), 1, f);

    uint32_t ts = 0;
    fread(&ts, sizeof(uint32_t), 1, f);

    uint32_t numLayers = 0;
    fread(&numLayers, sizeof(uint32_t), 1, f);

    map->width = (int)w;
    map->height = (int)h;

    for (uint32_t i = 0; i < TileMap::NUM_LAYERS; i++) {
        map->layers[i].resize(h);
        for (uint32_t y = 0; y < h; y++)
            map->layers[i][y].resize(w, 0);
    }

    uint32_t layersToRead = std::min(numLayers, (uint32_t)TileMap::NUM_LAYERS);
    for (uint32_t layer = 0; layer < layersToRead; layer++) {
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                uint8_t id = 0;
                fread(&id, sizeof(uint8_t), 1, f);
                map->layers[layer][y][x] = id;
            }
        }
    }

    fclose(f);
    return true;
}

