#include "asset_database.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

AssetDatabase::AssetDatabase()
    : db(nullptr) {
    memset(lastError, 0, sizeof(lastError));
}

AssetDatabase::~AssetDatabase() {
    close();
}

bool AssetDatabase::open(const char* path) {
    if (db) close();
    int rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK) {
        snprintf(lastError, sizeof(lastError), "Failed to open database: %s",
                 sqlite3_errmsg(db));
        sqlite3_close(db);
        db = nullptr;
        return false;
    }
    // Enable WAL mode for better concurrent read performance
    execSql("PRAGMA journal_mode=WAL;");
    execSql("PRAGMA foreign_keys=ON;");
    return true;
}

void AssetDatabase::close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool AssetDatabase::isOpen() const {
    return db != nullptr;
}

bool AssetDatabase::execSql(const char* sql) {
    if (!db) return false;
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        snprintf(lastError, sizeof(lastError), "SQL error: %s", errMsg);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool AssetDatabase::initSchema() {
    const char* sqlTiles =
        "CREATE TABLE IF NOT EXISTS tiles ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  category TEXT NOT NULL DEFAULT 'terrain',"
        "  width INTEGER NOT NULL DEFAULT 64,"
        "  height INTEGER NOT NULL DEFAULT 64,"
        "  png_data BLOB NOT NULL,"
        "  animated INTEGER NOT NULL DEFAULT 0,"
        "  frame_count INTEGER NOT NULL DEFAULT 1,"
        "  frame_width INTEGER NOT NULL DEFAULT 0,"
        "  frame_height INTEGER NOT NULL DEFAULT 0,"
        "  fps INTEGER NOT NULL DEFAULT 0,"
        "  file_path TEXT DEFAULT '',"
        "  file_mtime INTEGER DEFAULT 0,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    const char* sqlSprites =
        "CREATE TABLE IF NOT EXISTS sprites ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  category TEXT NOT NULL DEFAULT 'character',"
        "  sheet_width INTEGER NOT NULL,"
        "  sheet_height INTEGER NOT NULL,"
        "  png_data BLOB NOT NULL,"
        "  frame_count INTEGER NOT NULL DEFAULT 1,"
        "  frame_width INTEGER NOT NULL,"
        "  frame_height INTEGER NOT NULL,"
        "  fps INTEGER NOT NULL DEFAULT 8,"
        "  loop INTEGER NOT NULL DEFAULT 1,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    const char* sqlTileAnim =
        "CREATE TABLE IF NOT EXISTS tile_animations ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  tile_id INTEGER NOT NULL,"
        "  frame_index INTEGER NOT NULL,"
        "  frame_tile_id INTEGER NOT NULL,"
        "  duration_ms INTEGER NOT NULL DEFAULT 100,"
        "  FOREIGN KEY (tile_id) REFERENCES tiles(id) ON DELETE CASCADE,"
        "  FOREIGN KEY (frame_tile_id) REFERENCES tiles(id) ON DELETE CASCADE"
        ");";

    const char* sqlIdx1 =
        "CREATE INDEX IF NOT EXISTS idx_tiles_category ON tiles(category);";
    const char* sqlIdx2 =
        "CREATE INDEX IF NOT EXISTS idx_sprites_category ON sprites(category);";
    const char* sqlIdx3 =
        "CREATE INDEX IF NOT EXISTS idx_tile_anim_tile ON tile_animations(tile_id);";

    const char* sqlSettings =
        "CREATE TABLE IF NOT EXISTS settings ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL"
        ");";

    const char* sqlMaps =
        "CREATE TABLE IF NOT EXISTS maps ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  width INTEGER NOT NULL DEFAULT 128,"
        "  height INTEGER NOT NULL DEFAULT 128,"
        "  layer_data BLOB NOT NULL,"
        "  collision_data BLOB,"
        "  characters_data BLOB,"
        "  camera_x REAL DEFAULT 0,"
        "  camera_y REAL DEFAULT 0,"
        "  camera_zoom REAL DEFAULT 1.0,"
        "  render_order TEXT,"
        "  layer_vis TEXT,"
        "  layer_opa TEXT,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    if (!execSql(sqlTiles)) return false;
    if (!execSql(sqlSprites)) return false;
    if (!execSql(sqlTileAnim)) return false;
    if (!execSql(sqlSettings)) return false;
    if (!execSql(sqlMaps)) return false;
    if (!execSql(sqlIdx1)) return false;
    if (!execSql(sqlIdx2)) return false;
    if (!execSql(sqlIdx3)) return false;

    execSql("ALTER TABLE tiles ADD COLUMN file_path TEXT DEFAULT '';");
    execSql("ALTER TABLE tiles ADD COLUMN file_mtime INTEGER DEFAULT 0;");

    return true;
}

// ---- Tile CRUD ----

int AssetDatabase::addTile(const char* name, const char* category,
                            int w, int h, const uint8_t* pngData, int pngSize,
                            bool animated, int frameCount,
                            int frameW, int frameH, int fps) {
    if (!db) return -1;

    const char* sql =
        "INSERT INTO tiles (name, category, width, height, png_data, "
        "animated, frame_count, frame_width, frame_height, fps) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        snprintf(lastError, sizeof(lastError), "Prepare failed: %s",
                 sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, category, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, w);
    sqlite3_bind_int(stmt, 4, h);
    sqlite3_bind_blob(stmt, 5, pngData, pngSize, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, animated ? 1 : 0);
    sqlite3_bind_int(stmt, 7, frameCount);
    sqlite3_bind_int(stmt, 8, frameW);
    sqlite3_bind_int(stmt, 9, frameH);
    sqlite3_bind_int(stmt, 10, fps);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        snprintf(lastError, sizeof(lastError), "Insert failed: %s",
                 sqlite3_errmsg(db));
        return -1;
    }

    return (int)sqlite3_last_insert_rowid(db);
}

bool AssetDatabase::updateTile(int id, const char* name, const char* category,
                                int w, int h, const uint8_t* pngData, int pngSize) {
    if (!db) return false;

    const char* sql =
        "UPDATE tiles SET name=?, category=?, width=?, height=?, png_data=? "
        "WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, category, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, w);
    sqlite3_bind_int(stmt, 4, h);
    sqlite3_bind_blob(stmt, 5, pngData, pngSize, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, id);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool AssetDatabase::deleteTile(int id) {
    if (!db) return false;
    const char* sql = "DELETE FROM tiles WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool AssetDatabase::getTile(int id, TileRecord& out) {
    if (!db) return false;

    const char* sql =
        "SELECT id, name, category, width, height, png_data, "
        "animated, frame_count, frame_width, frame_height, fps, "
        "file_path, file_mtime, created_at "
        "FROM tiles WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    out.id = sqlite3_column_int(stmt, 0);
    out.name = (const char*)sqlite3_column_text(stmt, 1);
    out.category = (const char*)sqlite3_column_text(stmt, 2);
    out.width = sqlite3_column_int(stmt, 3);
    out.height = sqlite3_column_int(stmt, 4);

    int blobSize = sqlite3_column_bytes(stmt, 5);
    const uint8_t* blobData = (const uint8_t*)sqlite3_column_blob(stmt, 5);
    out.pngData.assign(blobData, blobData + blobSize);

    out.animated = sqlite3_column_int(stmt, 6) != 0;
    out.frameCount = sqlite3_column_int(stmt, 7);
    out.frameWidth = sqlite3_column_int(stmt, 8);
    out.frameHeight = sqlite3_column_int(stmt, 9);
    out.fps = sqlite3_column_int(stmt, 10);

    const char* fp = (const char*)sqlite3_column_text(stmt, 11);
    out.filePath = fp ? fp : "";
    out.fileMtime = sqlite3_column_int64(stmt, 12);

    const char* ca = (const char*)sqlite3_column_text(stmt, 13);
    out.createdAt = ca ? ca : "";

    sqlite3_finalize(stmt);
    return true;
}

std::vector<TileRecord> AssetDatabase::getAllTiles() {
    std::vector<TileRecord> result;
    if (!db) return result;

    const char* sql =
        "SELECT id, name, category, width, height, png_data, "
        "animated, frame_count, frame_width, frame_height, fps, created_at "
        "FROM tiles ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TileRecord rec;
        rec.id = sqlite3_column_int(stmt, 0);
        rec.name = (const char*)sqlite3_column_text(stmt, 1);
        rec.category = (const char*)sqlite3_column_text(stmt, 2);
        rec.width = sqlite3_column_int(stmt, 3);
        rec.height = sqlite3_column_int(stmt, 4);

        int blobSize = sqlite3_column_bytes(stmt, 5);
        const uint8_t* blobData = (const uint8_t*)sqlite3_column_blob(stmt, 5);
        rec.pngData.assign(blobData, blobData + blobSize);

        rec.animated = sqlite3_column_int(stmt, 6) != 0;
        rec.frameCount = sqlite3_column_int(stmt, 7);
        rec.frameWidth = sqlite3_column_int(stmt, 8);
        rec.frameHeight = sqlite3_column_int(stmt, 9);
        rec.fps = sqlite3_column_int(stmt, 10);
        rec.createdAt = (const char*)sqlite3_column_text(stmt, 11);

        result.push_back(std::move(rec));
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<TileRecord> AssetDatabase::getTilesByCategory(const char* category) {
    std::vector<TileRecord> result;
    if (!db) return result;

    const char* sql =
        "SELECT id, name, category, width, height, png_data, "
        "animated, frame_count, frame_width, frame_height, fps, created_at "
        "FROM tiles WHERE category=? ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, category, -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TileRecord rec;
        rec.id = sqlite3_column_int(stmt, 0);
        rec.name = (const char*)sqlite3_column_text(stmt, 1);
        rec.category = (const char*)sqlite3_column_text(stmt, 2);
        rec.width = sqlite3_column_int(stmt, 3);
        rec.height = sqlite3_column_int(stmt, 4);

        int blobSize = sqlite3_column_bytes(stmt, 5);
        const uint8_t* blobData = (const uint8_t*)sqlite3_column_blob(stmt, 5);
        rec.pngData.assign(blobData, blobData + blobSize);

        rec.animated = sqlite3_column_int(stmt, 6) != 0;
        rec.frameCount = sqlite3_column_int(stmt, 7);
        rec.frameWidth = sqlite3_column_int(stmt, 8);
        rec.frameHeight = sqlite3_column_int(stmt, 9);
        rec.fps = sqlite3_column_int(stmt, 10);
        rec.createdAt = (const char*)sqlite3_column_text(stmt, 11);

        result.push_back(std::move(rec));
    }

    sqlite3_finalize(stmt);
    return result;
}

int AssetDatabase::getTileCount() {
    if (!db) return 0;
    const char* sql = "SELECT COUNT(*) FROM tiles;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

int AssetDatabase::importTileFromFile(const char* filePath, const char* name,
                                       const char* category) {
    FILE* f = nullptr;
    fopen_s(&f, filePath, "rb");
    if (!f) {
        snprintf(lastError, sizeof(lastError), "Cannot open file: %s", filePath);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0 || fileSize > 16 * 1024 * 1024) {
        fclose(f);
        snprintf(lastError, sizeof(lastError), "Invalid file size: %ld", fileSize);
        return -1;
    }

    std::vector<uint8_t> data(fileSize);
    fread(data.data(), 1, fileSize, f);
    fclose(f);

    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, filePath, -1, wpath, 512);
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(wpath);
    int w = 64, h = 64;
    if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
        w = bmp->GetWidth();
        h = bmp->GetHeight();
        delete bmp;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad;
    int64_t mtime = 0;
    if (GetFileAttributesExW(wpath, GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER li;
        li.HighPart = fad.ftLastWriteTime.dwHighDateTime;
        li.LowPart = fad.ftLastWriteTime.dwLowDateTime;
        mtime = li.QuadPart;
    }

    if (!db) return -1;

    const char* sql =
        "INSERT INTO tiles (name, category, width, height, png_data, "
        "animated, frame_count, frame_width, frame_height, fps, file_path, file_mtime) "
        "VALUES (?, ?, ?, ?, ?, 0, 1, 0, 0, 0, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, category, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, w);
    sqlite3_bind_int(stmt, 4, h);
    sqlite3_bind_blob(stmt, 5, data.data(), (int)data.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, filePath, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 7, mtime);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return -1;
    return (int)sqlite3_last_insert_rowid(db);
}

bool AssetDatabase::getTileByPath(const char* filePath, TileRecord& out) {
    if (!db) return false;

    const char* sql =
        "SELECT id, name, category, width, height, png_data, "
        "animated, frame_count, frame_width, frame_height, fps, "
        "file_path, file_mtime, created_at "
        "FROM tiles WHERE file_path=?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, filePath, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    out.id = sqlite3_column_int(stmt, 0);
    out.name = (const char*)sqlite3_column_text(stmt, 1);
    out.category = (const char*)sqlite3_column_text(stmt, 2);
    out.width = sqlite3_column_int(stmt, 3);
    out.height = sqlite3_column_int(stmt, 4);

    int blobSize = sqlite3_column_bytes(stmt, 5);
    const uint8_t* blobData = (const uint8_t*)sqlite3_column_blob(stmt, 5);
    out.pngData.assign(blobData, blobData + blobSize);

    out.animated = sqlite3_column_int(stmt, 6) != 0;
    out.frameCount = sqlite3_column_int(stmt, 7);
    out.frameWidth = sqlite3_column_int(stmt, 8);
    out.frameHeight = sqlite3_column_int(stmt, 9);
    out.fps = sqlite3_column_int(stmt, 10);

    const char* fp = (const char*)sqlite3_column_text(stmt, 11);
    out.filePath = fp ? fp : "";
    out.fileMtime = sqlite3_column_int64(stmt, 12);

    const char* ca = (const char*)sqlite3_column_text(stmt, 13);
    out.createdAt = ca ? ca : "";

    sqlite3_finalize(stmt);
    return true;
}

bool AssetDatabase::updateTileFromFile(const char* filePath, const char* name,
                                        const char* category) {
    FILE* f = nullptr;
    fopen_s(&f, filePath, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0 || fileSize > 16 * 1024 * 1024) {
        fclose(f);
        return false;
    }

    std::vector<uint8_t> data(fileSize);
    fread(data.data(), 1, fileSize, f);
    fclose(f);

    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, filePath, -1, wpath, 512);
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(wpath);
    int w = 64, h = 64;
    if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
        w = bmp->GetWidth();
        h = bmp->GetHeight();
        delete bmp;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad;
    int64_t mtime = 0;
    if (GetFileAttributesExW(wpath, GetFileExInfoStandard, &fad)) {
        LARGE_INTEGER li;
        li.HighPart = fad.ftLastWriteTime.dwHighDateTime;
        li.LowPart = fad.ftLastWriteTime.dwLowDateTime;
        mtime = li.QuadPart;
    }

    if (!db) return false;

    const char* sql =
        "UPDATE tiles SET name=?, category=?, width=?, height=?, png_data=?, "
        "file_mtime=? WHERE file_path=?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, category, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, w);
    sqlite3_bind_int(stmt, 4, h);
    sqlite3_bind_blob(stmt, 5, data.data(), (int)data.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, mtime);
    sqlite3_bind_text(stmt, 7, filePath, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool AssetDatabase::exportTileToFile(int id, const char* filePath) {
    TileRecord rec;
    if (!getTile(id, rec)) return false;

    FILE* f = nullptr;
    fopen_s(&f, filePath, "wb");
    if (!f) return false;

    fwrite(rec.pngData.data(), 1, rec.pngData.size(), f);
    fclose(f);
    return true;
}

// ---- Sprite CRUD ----

int AssetDatabase::addSprite(const char* name, const char* category,
                              int sheetW, int sheetH,
                              const uint8_t* pngData, int pngSize,
                              int frameCount, int frameW, int frameH,
                              int fps, bool loop) {
    if (!db) return -1;

    const char* sql =
        "INSERT INTO sprites (name, category, sheet_width, sheet_height, "
        "png_data, frame_count, frame_width, frame_height, fps, loop) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, category, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, sheetW);
    sqlite3_bind_int(stmt, 4, sheetH);
    sqlite3_bind_blob(stmt, 5, pngData, pngSize, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, frameCount);
    sqlite3_bind_int(stmt, 7, frameW);
    sqlite3_bind_int(stmt, 8, frameH);
    sqlite3_bind_int(stmt, 9, fps);
    sqlite3_bind_int(stmt, 10, loop ? 1 : 0);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return -1;
    return (int)sqlite3_last_insert_rowid(db);
}

bool AssetDatabase::deleteSprite(int id) {
    if (!db) return false;
    const char* sql = "DELETE FROM sprites WHERE id=?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool AssetDatabase::getSprite(int id, SpriteRecord& out) {
    if (!db) return false;

    const char* sql =
        "SELECT id, name, category, sheet_width, sheet_height, png_data, "
        "frame_count, frame_width, frame_height, fps, loop, created_at "
        "FROM sprites WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, id);
    rc = sqlite3_step(stmt);

    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    out.id = sqlite3_column_int(stmt, 0);
    out.name = (const char*)sqlite3_column_text(stmt, 1);
    out.category = (const char*)sqlite3_column_text(stmt, 2);
    out.sheetWidth = sqlite3_column_int(stmt, 3);
    out.sheetHeight = sqlite3_column_int(stmt, 4);

    int blobSize = sqlite3_column_bytes(stmt, 5);
    const uint8_t* blobData = (const uint8_t*)sqlite3_column_blob(stmt, 5);
    out.pngData.assign(blobData, blobData + blobSize);

    out.frameCount = sqlite3_column_int(stmt, 6);
    out.frameWidth = sqlite3_column_int(stmt, 7);
    out.frameHeight = sqlite3_column_int(stmt, 8);
    out.fps = sqlite3_column_int(stmt, 9);
    out.loop = sqlite3_column_int(stmt, 10) != 0;
    out.createdAt = (const char*)sqlite3_column_text(stmt, 11);

    sqlite3_finalize(stmt);
    return true;
}

std::vector<SpriteRecord> AssetDatabase::getAllSprites() {
    std::vector<SpriteRecord> result;
    if (!db) return result;

    const char* sql =
        "SELECT id, name, category, sheet_width, sheet_height, png_data, "
        "frame_count, frame_width, frame_height, fps, loop, created_at "
        "FROM sprites ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SpriteRecord rec;
        rec.id = sqlite3_column_int(stmt, 0);
        rec.name = (const char*)sqlite3_column_text(stmt, 1);
        rec.category = (const char*)sqlite3_column_text(stmt, 2);
        rec.sheetWidth = sqlite3_column_int(stmt, 3);
        rec.sheetHeight = sqlite3_column_int(stmt, 4);

        int blobSize = sqlite3_column_bytes(stmt, 5);
        const uint8_t* blobData = (const uint8_t*)sqlite3_column_blob(stmt, 5);
        rec.pngData.assign(blobData, blobData + blobSize);

        rec.frameCount = sqlite3_column_int(stmt, 6);
        rec.frameWidth = sqlite3_column_int(stmt, 7);
        rec.frameHeight = sqlite3_column_int(stmt, 8);
        rec.fps = sqlite3_column_int(stmt, 9);
        rec.loop = sqlite3_column_int(stmt, 10) != 0;
        rec.createdAt = (const char*)sqlite3_column_text(stmt, 11);

        result.push_back(std::move(rec));
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<SpriteRecord> AssetDatabase::getSpritesByCategory(const char* category) {
    std::vector<SpriteRecord> result;
    if (!db) return result;

    const char* sql =
        "SELECT id, name, category, sheet_width, sheet_height, png_data, "
        "frame_count, frame_width, frame_height, fps, loop, created_at "
        "FROM sprites WHERE category=? ORDER BY id;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_text(stmt, 1, category, -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SpriteRecord rec;
        rec.id = sqlite3_column_int(stmt, 0);
        rec.name = (const char*)sqlite3_column_text(stmt, 1);
        rec.category = (const char*)sqlite3_column_text(stmt, 2);
        rec.sheetWidth = sqlite3_column_int(stmt, 3);
        rec.sheetHeight = sqlite3_column_int(stmt, 4);

        int blobSize = sqlite3_column_bytes(stmt, 5);
        const uint8_t* blobData = (const uint8_t*)sqlite3_column_blob(stmt, 5);
        rec.pngData.assign(blobData, blobData + blobSize);

        rec.frameCount = sqlite3_column_int(stmt, 6);
        rec.frameWidth = sqlite3_column_int(stmt, 7);
        rec.frameHeight = sqlite3_column_int(stmt, 8);
        rec.fps = sqlite3_column_int(stmt, 9);
        rec.loop = sqlite3_column_int(stmt, 10) != 0;
        rec.createdAt = (const char*)sqlite3_column_text(stmt, 11);

        result.push_back(std::move(rec));
    }

    sqlite3_finalize(stmt);
    return result;
}

int AssetDatabase::importSpriteFromFile(const char* filePath, const char* name,
                                         const char* category,
                                         int frameCount, int frameW, int frameH,
                                         int fps, bool loop) {
    FILE* f = nullptr;
    fopen_s(&f, filePath, "rb");
    if (!f) {
        snprintf(lastError, sizeof(lastError), "Cannot open file: %s", filePath);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0 || fileSize > 32 * 1024 * 1024) {
        fclose(f);
        return -1;
    }

    std::vector<uint8_t> data(fileSize);
    fread(data.data(), 1, fileSize, f);
    fclose(f);

    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, filePath, -1, wpath, 512);
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(wpath);
    int w = 64, h = 64;
    if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
        w = bmp->GetWidth();
        h = bmp->GetHeight();
        delete bmp;
    }

    return addSprite(name, category, w, h, data.data(), (int)data.size(),
                     frameCount, frameW, frameH, fps, loop);
}

// ---- Tile Animation ----

bool AssetDatabase::setTileAnimation(int tileId, const std::vector<AnimFrame>& frames) {
    if (!db) return false;

    // Delete existing animation frames
    const char* delSql = "DELETE FROM tile_animations WHERE tile_id=?;";
    sqlite3_stmt* delStmt = nullptr;
    int rc = sqlite3_prepare_v2(db, delSql, -1, &delStmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(delStmt, 1, tileId);
    sqlite3_step(delStmt);
    sqlite3_finalize(delStmt);

    // Insert new frames
    const char* insSql =
        "INSERT INTO tile_animations (tile_id, frame_index, frame_tile_id, duration_ms) "
        "VALUES (?, ?, ?, ?);";

    for (const auto& frame : frames) {
        sqlite3_stmt* insStmt = nullptr;
        rc = sqlite3_prepare_v2(db, insSql, -1, &insStmt, nullptr);
        if (rc != SQLITE_OK) return false;

        sqlite3_bind_int(insStmt, 1, tileId);
        sqlite3_bind_int(insStmt, 2, frame.frameIndex);
        sqlite3_bind_int(insStmt, 3, frame.tileId);
        sqlite3_bind_int(insStmt, 4, frame.durationMs);

        rc = sqlite3_step(insStmt);
        sqlite3_finalize(insStmt);
        if (rc != SQLITE_DONE) return false;
    }

    return true;
}

std::vector<AnimFrame> AssetDatabase::getTileAnimation(int tileId) {
    std::vector<AnimFrame> result;
    if (!db) return result;

    const char* sql =
        "SELECT frame_index, frame_tile_id, duration_ms "
        "FROM tile_animations WHERE tile_id=? ORDER BY frame_index;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, tileId);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AnimFrame frame;
        frame.frameIndex = sqlite3_column_int(stmt, 0);
        frame.tileId = sqlite3_column_int(stmt, 1);
        frame.durationMs = sqlite3_column_int(stmt, 2);
        result.push_back(frame);
    }

    sqlite3_finalize(stmt);
    return result;
}

// ---- Settings ----

bool AssetDatabase::setSetting(const char* key, const char* value) {
    if (!db) return false;
    const char* sql = "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::string AssetDatabase::getSetting(const char* key, const char* defaultVal) {
    if (!db) return defaultVal;
    const char* sql = "SELECT value FROM settings WHERE key=?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return defaultVal;
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    std::string result = defaultVal;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* val = (const char*)sqlite3_column_text(stmt, 0);
        if (val) result = val;
    }
    sqlite3_finalize(stmt);
    return result;
}

// ---- Utility ----

Gdiplus::Bitmap* AssetDatabase::loadTileBitmap(int id) {
    TileRecord rec;
    if (!getTile(id, rec)) return nullptr;
    if (rec.pngData.empty()) return nullptr;

    // Create bitmap from PNG blob via IStream
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, rec.pngData.size());
    if (!hMem) return nullptr;

    void* pMem = GlobalLock(hMem);
    memcpy(pMem, rec.pngData.data(), rec.pngData.size());
    GlobalUnlock(hMem);

    IStream* pStream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(hMem, TRUE, &pStream);
    if (FAILED(hr)) {
        GlobalFree(hMem);
        return nullptr;
    }

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();

    if (bmp && bmp->GetLastStatus() != Gdiplus::Ok) {
        delete bmp;
        return nullptr;
    }

    return bmp;
}

Gdiplus::Bitmap* AssetDatabase::loadSpriteBitmap(int id) {
    SpriteRecord rec;
    if (!getSprite(id, rec)) return nullptr;
    if (rec.pngData.empty()) return nullptr;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, rec.pngData.size());
    if (!hMem) return nullptr;

    void* pMem = GlobalLock(hMem);
    memcpy(pMem, rec.pngData.data(), rec.pngData.size());
    GlobalUnlock(hMem);

    IStream* pStream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(hMem, TRUE, &pStream);
    if (FAILED(hr)) {
        GlobalFree(hMem);
        return nullptr;
    }

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(pStream);
    pStream->Release();

    if (bmp && bmp->GetLastStatus() != Gdiplus::Ok) {
        delete bmp;
        return nullptr;
    }

    return bmp;
}

const char* AssetDatabase::getLastError() const {
    return lastError;
}
