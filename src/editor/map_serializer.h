#pragma once

#include <cstdint>
#include <vector>
#include <string>

class TileMap;

class MapSerializer {
public:
    MapSerializer();

    bool saveMap(const char* path, TileMap* map);
    bool loadMap(const char* path, TileMap* map);

private:
    void writeUint32(FILE* f, uint32_t val);
    uint32_t readUint32(FILE* f);
    void writeString(FILE* f, const std::wstring& str);
    std::wstring readString(FILE* f);
};
