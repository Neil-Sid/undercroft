// Web build of paint.h: the GDI fonts come pre-baked (tools/bake_fonts.cpp), and there is
// no screenshot export.
#include "paint.h"
#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace {

struct BakedFont {
    int px;
    bool bold;
    int lineHeight, width, rows;
    Glyph glyphs[96];
    const uint32_t* bits;  // 1 bit per pixel, row-major, `width` pixels per row
};

#include "fonts.inc"

}  // namespace

namespace paint {

void startup() {}
void shutdown() {}

FontAtlas font(const wchar_t*, int px, bool bold) {
    const BakedFont* best = BAKED_FONTS[0];
    for (const BakedFont* f : BAKED_FONTS)
        if (f->bold == bold && std::abs(f->px - px) < std::abs(best->px - px)) best = f;
    FontAtlas atlas;
    atlas.lineHeight = best->lineHeight;
    std::copy(std::begin(best->glyphs), std::end(best->glyphs), atlas.glyphs);
    atlas.image.w = atlas.image.h = best->width;
    atlas.image.rgba.assign(size_t(best->width) * best->width, 0x00FFFFFF);
    int words = best->width / 32;
    for (int y = 0; y < best->rows; y++)
        for (int x = 0; x < best->width; x++)
            if (best->bits[y * words + x / 32] >> (x % 32) & 1) atlas.image.rgba[size_t(y) * best->width + x] = 0xFFFFFFFF;
    return atlas;
}

bool savePng(const Pixels&, const char*) { return false; }

}  // namespace paint
