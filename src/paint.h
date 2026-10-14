#pragma once
// GDI/GDI+ helpers: pixel fonts rasterised at startup, and PNG export for screenshots.
#include <cstdint>
#include <vector>

struct Pixels {
    int w = 0, h = 0;
    std::vector<uint32_t> rgba;  // top row first, bytes R,G,B,A
};

struct Glyph {
    int x = 0, y = 0, w = 0, h = 0;
    int offset = 0, advance = 0;  // pen offset to the glyph box, and pen advance
};

struct FontAtlas {
    Pixels image;  // white, coverage in alpha (hard-edged, no antialiasing)
    Glyph glyphs[96];
    int lineHeight = 0;
};

namespace paint {
void startup();
void shutdown();
FontAtlas font(const wchar_t* face, int px, bool bold);
bool savePng(const Pixels& img, const char* path);
}
