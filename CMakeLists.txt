cmake_minimum_required(VERSION 3.15)
project(GameMakerCPlus LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# SQLite3 amalgamation
add_library(sqlite3 STATIC ${CMAKE_SOURCE_DIR}/src/libs/sqlite3/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_SOURCE_DIR}/src/libs/sqlite3)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_ENABLE_FTS5
)
target_compile_options(sqlite3 PRIVATE -O2)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(GameMakerCPlus WIN32 ${SOURCES})

target_include_directories(GameMakerCPlus PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/libs/stb
    ${CMAKE_SOURCE_DIR}/src/libs/sqlite3
)

target_compile_definitions(GameMakerCPlus PRIVATE
    UNICODE _UNICODE
    NOMINMAX
)

target_compile_options(GameMakerCPlus PRIVATE
    -static-libgcc
    -static-libstdc++
    -O2
    -Wall
    -Wno-unused-variable
    -Wno-sign-compare
)

target_link_options(GameMakerCPlus PRIVATE -mwindows)

target_link_libraries(GameMakerCPlus PRIVATE
    sqlite3
    d2d1 dwrite windowscodecs ole32 oleaut32
    gdi32 user32 kernel32 gdiplus
    winhttp shell32 comctl32 shlwapi
    ws2_32 crypt32
)
