# GameMakerC++

A lightweight 2D tile-based game editor and runtime built with C++, Direct2D, and GDI+.

## Features

- **Tile Map Editor** with 7-layer system (terrain, draw, collision, character, foreground, fg_draw, character collision)
- **Direct2D hardware-accelerated rendering** with software fallback
- **Brush tool** with adjustable brush size (1-10) and green brush preview
- **Grid tile import** - import large images that auto-slice into tile grids
- **Character system** with sprite animation support
- **Map serialization** (save/load .gmc format)
- **Game runtime mode** (WASD movement, ESC to exit)
- **Dark theme UI** with bottom-bar layout (Unreal Engine style)

## Building

Requires MinGW + CMake on Windows.

`ash
cd build
cmake .. -G "MinGW Makefiles"
mingw32-make -j8
`

## Dependencies

- Direct2D / DirectWrite (Windows SDK)
- GDI+ (Windows)
- SQLite3 (bundled)
- stb_image (bundled)

## License

MIT
