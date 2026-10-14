#include "textures.h"
#include "render.h"
#include <algorithm>
#include <cmath>

namespace {

constexpr int N = ATLAS_CELL_PX;
constexpr float SOLID = 1.0f, GLOWING = 0.75f, HOLE = 0.0f;

struct Texel {
    float r = 0, g = 0, b = 0, a = SOLID;
};

struct RGB {
    float r, g, b;
    RGB operator*(float s) const { return {r * s, g * s, b * s}; }
    RGB operator*(RGB o) const { return {r * o.r, g * o.g, b * o.b}; }
    RGB operator+(RGB o) const { return {r + o.r, g + o.g, b + o.b}; }
};

struct Img {
    Texel px[N * N];
    Texel& at(int x, int y) { return px[((y % N + N) % N) * N + ((x % N + N) % N)]; }
};

RGB mixc(RGB a, RGB b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
float luma(RGB c) { return c.r * 0.3f + c.g * 0.59f + c.b * 0.11f; }

float hash(int x, int y, int seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF);
}

// Tileable value noise; `period` lattice cells span the 64-pixel texture.
float noise(float x, float y, int period, int seed) {
    float fx = x / N * period, fy = y / N * period;
    int ix = int(std::floor(fx)), iy = int(std::floor(fy));
    float tx = fx - ix, ty = fy - iy;
    tx = tx * tx * (3 - 2 * tx), ty = ty * ty * (3 - 2 * ty);
    auto h = [&](int a, int b) { return hash(((a % period) + period) % period, ((b % period) + period) % period, seed); };
    float top = h(ix, iy) + (h(ix + 1, iy) - h(ix, iy)) * tx;
    float bottom = h(ix, iy + 1) + (h(ix + 1, iy + 1) - h(ix, iy + 1)) * tx;
    return top + (bottom - top) * ty;
}

float fbm(float x, float y, int period, int seed, int octaves = 4) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int o = 0; o < octaves; o++, period *= 2, amp *= 0.5f) sum += noise(x, y, period, seed + o) * amp, norm += amp;
    return sum / norm;
}

// Tileable cellular noise: distances to the nearest and second-nearest feature point.
struct Cells {
    float d1 = 9, d2 = 9;
    int id = 0;
};

Cells cellular(float x, float y, int period, int seed) {
    float fx = x / N * period, fy = y / N * period;
    int ix = int(std::floor(fx)), iy = int(std::floor(fy));
    Cells c;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int cx = ix + dx, cy = iy + dy, wx = (cx % period + period) % period, wy = (cy % period + period) % period;
            float px = cx + 0.15f + 0.7f * hash(wx, wy, seed), py = cy + 0.15f + 0.7f * hash(wx, wy, seed + 1);
            float d = std::hypot(fx - px, fy - py);
            if (d < c.d1) c.d2 = c.d1, c.d1 = d, c.id = wy * period + wx;
            else if (d < c.d2) c.d2 = d;
        }
    return c;
}

void set(Img& img, int x, int y, RGB c, float a = SOLID) { img.at(x, y) = {c.r, c.g, c.b, a}; }

RGB get(Img& img, int x, int y) {
    const Texel& t = img.at(x, y);
    return {t.r, t.g, t.b};
}

// Paints every texel from its position: f(x, y, alpha&) returns the colour.
template <class F>
void fill(Img& img, F f) {
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            Texel& t = img.at(x, y);
            RGB c = f(x, y, t.a);
            t.r = c.r, t.g = c.g, t.b = c.b;
        }
}

// Repaints every texel from its current colour: f(x, y, colour, alpha&) returns the new one.
template <class F>
void tweak(Img& img, F f) {
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            Texel& t = img.at(x, y);
            RGB c = f(x, y, RGB{t.r, t.g, t.b}, t.a);
            t.r = c.r, t.g = c.g, t.b = c.b;
        }
}

void rect(Img& img, int x0, int y0, int x1, int y1, RGB c, float a = SOLID) {
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) set(img, x, y, c, a);
}

void line(Img& img, float x0, float y0, float x1, float y1, RGB c, float a = SOLID) {
    int steps = int(std::max(std::fabs(x1 - x0), std::fabs(y1 - y0))) + 1;
    for (int i = 0; i <= steps; i++) {
        float t = float(i) / float(steps);
        set(img, int(std::floor(mix(x0, x1, t))), int(std::floor(mix(y0, y1, t))), c, a);
    }
}

void disc(Img& img, float cx, float cy, float r, RGB colour, float alpha = SOLID) {
    for (int y = int(cy - r - 1); y <= int(cy + r + 1); y++)
        for (int x = int(cx - r - 1); x <= int(cx + r + 1); x++)
            if ((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy) <= r * r) set(img, x, y, colour, alpha);
}

// A disc lit from the top left, with a dark rim: reads as a rounded lump.
void lump(Img& img, float cx, float cy, float rx, float ry, RGB colour) {
    for (int y = int(cy - ry - 1); y <= int(cy + ry + 1); y++)
        for (int x = int(cx - rx - 1); x <= int(cx + rx + 1); x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, d = dx * dx + dy * dy;
            if (d > 1) continue;
            float k = 1.1f - 0.35f * (dx + dy) - (d > 0.7f ? 0.25f : 0);
            set(img, x, y, colour * k);
        }
}

// Brightens (k > 0) or darkens (k < 0) around a point with a smooth falloff, optionally tinted.
void glowAround(Img& img, float cx, float cy, float r, float k, RGB tint = {1, 1, 1}) {
    tweak(img, [&](int x, int y, RGB c, float&) {
        float dx = x - cx, dy = y - cy;
        dx -= std::round(dx / N) * N, dy -= std::round(dy / N) * N;
        float f = std::max(0.0f, 1 - std::hypot(dx, dy) / r);
        return k >= 0 ? mixc(c, c * tint * (1 + k), f * f) : c * (1 + k * f * f);
    });
}

void crack(Img& img, int seed, RGB colour, float alpha, int count, int length = 40) {
    for (int c = 0; c < count; c++) {
        float x = hash(c, 1, seed) * N, y = hash(c, 2, seed) * N, dir = hash(c, 3, seed) * 6.283f;
        for (int s = 0; s < length; s++) {
            set(img, int(x), int(y), colour, alpha);
            dir += (hash(c, s + 10, seed) - 0.5f) * 1.2f;
            x += std::cos(dir), y += std::sin(dir);
        }
    }
}

// Vertical grime running down from the top of a wall.
void streaks(Img& img, int seed, float strength) {
    tweak(img, [&](int x, int y, RGB c, float&) {
        float s = noise(x * 4.0f, y * 0.125f, 8, seed);
        return s > 0.55f ? c * (1 - strength * (s - 0.55f) * 2.2f * (1 - y / 90.0f)) : c;
    });
}

// Ashlar courses: per-block tint, chipped corners, a lit top edge and a shadowed lower edge.
void bricks(Img& img, int rows, int perRow, RGB base, RGB mortar, int seed, float roughness) {
    int bh = N / rows, bw = N / perRow;
    fill(img, [&](int x, int y, float&) {
        int row = y / bh, shift = (row % 2) * bw / 2;
        int bx = ((x + shift) % N) / bw, lx = (x + shift) % bw, ly = y % bh;
        bool corner = (lx < 4 || lx > bw - 3) && (ly < 4 || ly > bh - 3);
        if (lx < 2 || ly < 2 || (corner && hash(bx * 7 + lx / 2, row * 5 + ly / 2, seed + 3) > 0.55f))
            return mortar * (0.7f + 0.5f * fbm(x, y, 8, seed + 9));
        float tint = 0.8f + 0.28f * hash(bx, row, seed);
        float n = fbm(x, y, 8, seed) * roughness + (1 - roughness);
        float bevel = ly == 2 ? 1.25f : lx == 2 ? 1.1f : ly == bh - 1 ? 0.68f : lx == bw - 1 ? 0.8f : 1.0f;
        float speck = hash(x, y, seed + 5) > 0.93f ? 0.82f : 1.0f;
        return base * (tint * n * bevel * speck);
    });
}

// Large square slabs with bevelled edges and dark seams.
void flagstones(Img& img, RGB base, RGB seam, int seed) {
    fill(img, [&](int x, int y, float&) {
        int row = y / 32, shift = int(hash(row, 0, seed) * 12) + (row % 2) * 16;
        int lx = (x + shift) % 32, ly = y % 32, sx = ((x + shift) % N) / 32;
        if (lx < 2 || ly < 2) return seam * (0.7f + 0.6f * fbm(x, y, 16, seed + 7));
        float tint = 0.78f + 0.3f * hash(sx, row, seed);
        float n = 0.6f + 0.55f * fbm(x, y, 8, seed + 1);
        float bevel = ly == 2 ? 1.2f : lx == 2 ? 1.1f : ly == 31 ? 0.72f : lx == 31 ? 0.8f : 1.0f;
        float grit = hash(x, y, seed + 2) > 0.9f ? 0.82f : 1.0f;
        return base * (tint * n * bevel * grit);
    });
}

void skull(Img& img, float cx, float cy, float r, RGB bone) {
    lump(img, cx, cy + r * 0.55f, r * 0.62f, r * 0.55f, bone * 0.85f);
    lump(img, cx, cy, r, r * 0.9f, bone);
    RGB hole{0.04f, 0.03f, 0.03f};
    disc(img, cx - r * 0.42f, cy + r * 0.15f, r * 0.3f, hole);
    disc(img, cx + r * 0.42f, cy + r * 0.15f, r * 0.3f, hole);
    set(img, int(cx), int(cy + r * 0.55f), hole);
    for (int x = int(cx - r * 0.45f); x <= int(cx + r * 0.45f); x++) set(img, x, int(cy + r * 0.95f), x % 2 ? bone * 0.9f : hole);
}

// A short bone lying along a line, with knobbed ends.
void bone(Img& img, float x0, float y0, float x1, float y1, RGB c) {
    line(img, x0, y0, x1, y1, c);
    line(img, x0, y0 + 1, x1, y1 + 1, c * 0.75f);
    disc(img, x0, y0 + 0.5f, 1.6f, c * 1.05f);
    disc(img, x1, y1 + 0.5f, 1.6f, c * 0.95f);
}

const RGB CRYPT_STONE{0.50f, 0.50f, 0.54f}, CRYPT_MORTAR{0.13f, 0.12f, 0.13f};
const RGB TUFF{0.30f, 0.23f, 0.16f}, BONE{0.80f, 0.74f, 0.60f}, DARK{0.03f, 0.025f, 0.02f};
const RGB BASALT{0.20f, 0.16f, 0.16f}, LAVA{1.0f, 0.42f, 0.10f}, LAVA_HOT{1.0f, 0.78f, 0.35f}, IRON{0.30f, 0.30f, 0.33f};

void cryptWall(Img& img) {
    bricks(img, 4, 2, CRYPT_STONE, CRYPT_MORTAR, 11, 0.45f);
    streaks(img, 12, 0.35f);
    crack(img, 3, {0.16f, 0.16f, 0.18f}, SOLID, 1, 30);
}

void cryptFloor(Img& img) {
    flagstones(img, {0.44f, 0.41f, 0.37f}, {0.11f, 0.10f, 0.09f}, 21);
    crack(img, 8, {0.15f, 0.14f, 0.12f}, SOLID, 1, 25);
}

void catacombWallBase(Img& img, int seed) {
    fill(img, [&](int x, int y, float&) {
        float n = fbm(x, y, 8, seed), pick = noise(x * 3.0f + y * 2.0f, y * 1.0f, 8, seed + 1);
        return TUFF * ((0.5f + 0.6f * n) * (pick > 0.7f ? 0.8f : 1.0f));
    });
}

void emberWall(Img& img) {
    bricks(img, 4, 2, BASALT, {0.04f, 0.03f, 0.03f}, 71, 0.6f);
    streaks(img, 73, 0.5f);
    tweak(img, [](int, int y, RGB c, float&) { return y > 52 ? mixc(c, c * RGB{1.5f, 0.8f, 0.6f}, (y - 52) / 12.0f) : c; });
}

// Basalt flagstones; only a few short stretches of seam glow, one texel wide.
void emberFloor(Img& img, float glowShare) {
    fill(img, [&](int x, int y, float& a) {
        Cells c = cellular(x, y, 4, 81);
        float edge = c.d2 - c.d1;
        float n = fbm(x, y, 8, 82);
        if (edge < 0.05f && noise(x, y, 4, 83) > 1 - glowShare) {
            a = GLOWING;
            return mixc(LAVA, LAVA_HOT, hash(x, y, 84) * 0.5f);
        }
        if (edge < 0.09f) return RGB{0.05f, 0.035f, 0.03f};
        return BASALT * ((0.75f + 0.3f * hash(c.id, 0, 85)) * (0.55f + 0.6f * n) * (edge < 0.14f ? 0.8f : 1.0f));
    });
}

void grate(Img& img, int x0, int y0, int x1, int y1, RGB below, float belowAlpha) {
    rect(img, x0 - 2, y0 - 2, x1 + 2, y1 + 2, IRON * 0.7f);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            bool bar = (x - x0) % 5 < 2 || (y - y0) % 9 < 2;
            float depth = 0.6f + 0.4f * hash(x, y, 9);
            if (bar) set(img, x, y, IRON * (((x - x0) % 5 == 0 || (y - y0) % 9 == 0) ? 1.5f : 1.0f));
            else set(img, x, y, below * depth, belowAlpha);
        }
}

void paintTexture(Tex t, Img& img) {
    switch (t) {
    case Tex::White: fill(img, [](int, int, float&) { return RGB{1, 1, 1}; }); break;

    case Tex::CryptWall: cryptWall(img); break;
    case Tex::CryptWallCracked:
        cryptWall(img);
        for (int y = 30; y < 46; y++)
            for (int x = 34; x < 52; x++)
                if (std::hypot((x - 43) * 1.0f, (y - 38) * 1.3f) + fbm(x, y, 8, 5) * 8 < 10) set(img, x, y, get(img, x, y) * (y < 33 ? 0.3f : 0.55f));
        crack(img, 31, {0.10f, 0.10f, 0.11f}, SOLID, 3, 34);
        break;
    case Tex::CryptWallMoss:
        cryptWall(img);
        tweak(img, [](int x, int y, RGB c, float&) {
            float m = fbm(x, y, 4, 401) * 0.8f + y / 64.0f * 0.45f;
            RGB moss = RGB{0.20f, 0.30f, 0.11f} * (0.6f + 0.7f * fbm(x, y, 16, 402));
            if (m > 0.74f || (luma(c) < 0.15f && m > 0.58f)) return moss;
            return m > 0.68f ? mixc(c, moss, 0.5f) : c;
        });
        break;
    case Tex::CryptWallNiche: {
        cryptWall(img);
        auto inArch = [](float x, float y, float r) { return (y >= 26 && std::fabs(x - 32) <= r) || std::hypot(x - 32, y - 26) <= r; };
        for (int y = 8; y < 54; y++)
            for (int x = 14; x < 50; x++) {
                float fx = x + 0.5f, fy = y + 0.5f;
                if (y < 50 && inArch(fx, fy, 12)) {
                    float depth = 0.05f + 0.07f * (fy - 14) / 36;
                    set(img, x, y, RGB{1.0f, 0.95f, 1.0f} * depth);
                } else if (y < 50 && inArch(fx, fy, 15)) {
                    float a = std::atan2(fy - 26, fx - 32);
                    bool joint = fy < 26 && std::fmod(a + 7, 0.5f) < 0.09f;
                    set(img, x, y, CRYPT_STONE * (joint ? 0.4f : 1.1f + 0.2f * fbm(x, y, 8, 7) - (fx > 32 ? 0.15f : 0)));
                }
            }
        rect(img, 16, 50, 48, 53, CRYPT_STONE * 1.25f);
        rect(img, 16, 53, 48, 54, CRYPT_STONE * 0.45f);
        glowAround(img, 40, 40, 11, 2.5f, {1.0f, 0.75f, 0.45f});
        skull(img, 28, 42, 5.5f, BONE * 0.9f);
        rect(img, 39, 41, 42, 50, RGB{0.85f, 0.8f, 0.65f});
        set(img, 41, 44, RGB{0.7f, 0.65f, 0.5f});
        disc(img, 40.5f, 38.5f, 1.8f, RGB{1.0f, 0.65f, 0.2f}, GLOWING);
        set(img, 40, 36, RGB{1.0f, 0.9f, 0.5f}, GLOWING);
        set(img, 40, 38, LAVA_HOT, GLOWING);
        break;
    }
    case Tex::CryptWallBanner: {
        cryptWall(img);
        RGB red{0.46f, 0.07f, 0.06f}, gold{0.85f, 0.62f, 0.22f};
        auto cloth = [](int x, int y) { return x >= 19 && x < 45 && y >= 8 && y <= 46 + std::fabs(x - 31.5f) * 0.7f && !(y > 40 && fbm(x, y, 16, 411) > 0.7f); };
        for (int y = 63; y >= 0; y--)
            for (int x = 63; x >= 0; x--)
                if (!cloth(x, y) && cloth(x - 2, y - 2)) set(img, x, y, get(img, x, y) * 0.5f);
        for (int y = 8; y < 56; y++)
            for (int x = 19; x < 45; x++) {
                if (!cloth(x, y)) continue;
                float fold = 0.75f + 0.25f * std::sin(x * 0.9f);
                bool trim = x < 21 || x > 42 || y < 11;
                set(img, x, y, (trim ? gold : red) * (fold * (0.8f + 0.3f * fbm(x, y, 8, 412))));
            }
        lump(img, 32, 25, 6, 6, gold);
        disc(img, 29.5f, 25, 1.5f, red * 0.5f);
        disc(img, 34.5f, 25, 1.5f, red * 0.5f);
        rect(img, 30, 30, 35, 32, gold * 0.8f);
        rect(img, 15, 6, 50, 8, IRON * 1.4f);
        rect(img, 15, 8, 50, 9, IRON * 0.6f);
        disc(img, 15, 7, 1.6f, IRON * 1.6f);
        disc(img, 49, 7, 1.6f, IRON * 1.6f);
        break;
    }
    case Tex::CryptWallSconce:
        cryptWall(img);
        glowAround(img, 32, 16, 24, 1.6f, {1.0f, 0.78f, 0.5f});
        glowAround(img, 32, 4, 7, -0.6f);
        rect(img, 29, 30, 36, 42, IRON);
        rect(img, 29, 30, 36, 31, IRON * 1.6f);
        rect(img, 31, 25, 34, 31, IRON * 0.8f);
        rect(img, 27, 22, 38, 26, IRON * 1.2f);
        rect(img, 27, 25, 38, 26, IRON * 0.6f);
        rect(img, 30, 15, 35, 22, RGB{0.82f, 0.78f, 0.62f});
        rect(img, 33, 15, 35, 22, RGB{0.62f, 0.58f, 0.45f});
        disc(img, 32.5f, 11.5f, 3.2f, RGB{1.0f, 0.55f, 0.15f}, GLOWING);
        disc(img, 32.5f, 12, 2, LAVA_HOT, GLOWING);
        line(img, 32.5f, 4, 32.5f, 9, RGB{1.0f, 0.6f, 0.2f}, GLOWING);
        break;

    case Tex::CryptFloor: cryptFloor(img); break;
    case Tex::CryptFloorCracked:
        cryptFloor(img);
        crack(img, 44, {0.08f, 0.07f, 0.06f}, SOLID, 4, 46);
        for (int i = 0; i < 14; i++) lump(img, 20 + hash(i, 0, 45) * 24, 20 + hash(i, 1, 45) * 24, 1.5f, 1.2f, RGB{0.44f, 0.41f, 0.37f} * 0.9f);
        break;
    case Tex::CryptFloorGrate:
        cryptFloor(img);
        glowAround(img, 32, 32, 22, -0.5f);
        grate(img, 20, 20, 44, 44, {0.03f, 0.03f, 0.035f}, SOLID);
        crack(img, 46, RGB{0.36f, 0.20f, 0.10f}, SOLID, 2, 10);
        break;
    case Tex::CryptFloorStain:
        cryptFloor(img);
        tweak(img, [](int x, int y, RGB c, float&) {
            float m = fbm(x, y, 8, 421) - std::hypot(x - 34.0f, y - 30.0f) / 48;
            return m > 0.18f ? mixc(c, RGB{0.17f, 0.05f, 0.04f}, std::min(0.85f, (m - 0.18f) * 6)) : c;
        });
        for (int i = 0; i < 9; i++) disc(img, 10 + hash(i, 0, 422) * 44, 8 + hash(i, 1, 422) * 48, 0.8f + hash(i, 2, 422), {0.2f, 0.06f, 0.05f});
        break;
    case Tex::CryptCeiling:
        bricks(img, 4, 4, {0.30f, 0.29f, 0.31f}, {0.08f, 0.08f, 0.09f}, 31, 0.5f);
        glowAround(img, 20, 40, 18, -0.5f);
        break;

    case Tex::CatacombWall:
        catacombWallBase(img, 41);
        for (int band = 0; band < 2; band++) {
            int top = band * 32;
            rect(img, 0, top + 1, N, top + 17, DARK);
            for (int i = 0; i < 4; i++) skull(img, i * 16 + 8 + band * 8, top + 8.5f, 6.5f, BONE * (0.75f + 0.25f * hash(i, band, 3)));
            for (int row = 0; row < 3; row++) {
                int y = top + 19 + row * 4;
                rect(img, 0, y - 1, N, y + 3, DARK);
                for (int i = 0; i < 3; i++) {
                    float x0 = i * 22 + hash(row, i, 43) * 6 + band * 5, len = 15 + hash(i, row, 44) * 5;
                    bone(img, x0, float(y), x0 + len, float(y), BONE * (0.65f + 0.3f * hash(i + row, band, 45)));
                }
            }
        }
        break;
    case Tex::CatacombWallNiche:
        catacombWallBase(img, 431);
        for (int k = 0; k < 2; k++) {
            int y0 = 6 + k * 30;
            rect(img, 5, y0 - 1, 59, y0, TUFF * 1.3f);
            rect(img, 6, y0, 58, y0 + 20, DARK);
            for (int y = y0; y < y0 + 20; y++)
                for (int x = 6; x < 58; x++) set(img, x, y, DARK * (1 + 2.5f * float(y - y0) / 20));
            rect(img, 5, y0 + 20, 59, y0 + 22, TUFF * 1.25f);
            if (k == 0) {
                skull(img, 13, y0 + 12, 5, BONE * 0.85f);
                for (int i = 0; i < 4; i++) bone(img, 21 + i * 2.0f, y0 + 14.0f + i, 50 - i * 3.0f, y0 + 15.0f + i * 0.5f, BONE * (0.6f + 0.1f * i));
            } else {
                RGB old = BONE * 0.75f;
                skull(img, 50, y0 + 12, 4.5f, old);
                line(img, 18, y0 + 13, 45, y0 + 13, old * 0.8f);
                for (int i = 0; i < 6; i++) line(img, 31 + i * 2.0f, y0 + 9.0f + std::fabs(i - 2.5f) * 0.6f, 31 + i * 2.0f, y0 + 17.0f - std::fabs(i - 2.5f) * 0.6f, old);
                lump(img, 26, y0 + 14, 3, 2.5f, old * 0.9f);
                bone(img, 9, y0 + 12.0f, 23, y0 + 13.0f, old * 0.85f);
                bone(img, 9, y0 + 16.0f, 23, y0 + 15.0f, old * 0.75f);
                bone(img, 36, y0 + 17.0f, 46, y0 + 18.0f, old * 0.7f);
            }
        }
        break;
    case Tex::CatacombWallBones:
        catacombWallBase(img, 441);
        rect(img, 0, 0, N, N, DARK);
        for (int row = 0; row < 12; row++) {
            if (row == 5) continue;
            for (int i = 0; i < 8; i++) {
                float cx = i * 8 + 4 + (row % 2) * 4, cy = row * 5.4f + 3;
                RGB c = BONE * (0.6f + 0.35f * hash(i, row, 442));
                lump(img, cx - 1.6f, cy, 2.0f, 2.2f, c);
                lump(img, cx + 1.6f, cy, 2.0f, 2.2f, c * 0.92f);
            }
        }
        for (int i = 0; i < 5; i++) skull(img, i * 13 + 6.5f, 30, 5.5f, BONE * (0.8f + 0.2f * hash(i, 0, 443)));
        break;
    case Tex::CatacombWallRough:
        catacombWallBase(img, 451);
        tweak(img, [](int x, int y, RGB c, float&) { return c * (0.7f + 0.6f * fbm(x, y, 4, 452)); });
        crack(img, 453, DARK * 3, SOLID, 3, 30);
        for (int k = 0; k < 4; k++) {
            float x = 8 + k * 15 + hash(k, 0, 454) * 6, dir = 1.57f;
            for (int s = 0; s < 22 + k * 5; s++) {
                set(img, int(x), s, RGB{0.18f, 0.12f, 0.07f});
                set(img, int(x) + 1, s, RGB{0.10f, 0.07f, 0.04f});
                dir += (hash(k, s, 455) - 0.5f) * 0.9f;
                x += std::cos(dir) * 0.8f;
            }
        }
        break;

    case Tex::CatacombFloor:
        fill(img, [](int x, int y, float&) {
            float n = fbm(x, y, 8, 51), damp = fbm(x, y, 4, 53);
            float pebble = hash(x / 3, y / 3, 52) > 0.93f ? 1.45f : 1.0f;
            return TUFF * ((0.55f + 0.6f * n) * pebble * (damp > 0.6f ? 0.75f : 1.0f));
        });
        break;
    case Tex::CatacombFloorBones:
        paintTexture(Tex::CatacombFloor, img);
        for (int i = 0; i < 9; i++) {
            float x = 6 + hash(i, 0, 461) * 50, y = 6 + hash(i, 1, 461) * 50, a = hash(i, 2, 461) * 6.28f, len = 5 + hash(i, 3, 461) * 9;
            bone(img, x, y, x + std::cos(a) * len, y + std::sin(a) * len, BONE * (0.6f + 0.3f * hash(i, 4, 461)));
        }
        skull(img, 40, 22, 5, BONE * 0.8f);
        break;
    case Tex::CatacombFloorPuddle:
        paintTexture(Tex::CatacombFloor, img);
        tweak(img, [](int x, int y, RGB c, float& a) {
            float m = fbm(x, y, 8, 471) - std::hypot(x - 30.0f, (y - 34.0f) * 1.3f) / 40;
            if (m > 0.22f) {
                if (m < 0.25f) return RGB{0.10f, 0.09f, 0.08f};
                bool glint = hash(x, y, 472) > 0.985f;
                if (glint) a = GLOWING;
                return glint ? RGB{0.35f, 0.38f, 0.42f} : RGB{0.05f, 0.06f, 0.07f} * (1 + fbm(x, y, 16, 473));
            }
            return m > 0.15f ? c * 0.7f : c;
        });
        break;
    case Tex::CatacombFloorSlab:
        paintTexture(Tex::CatacombFloor, img);
        rect(img, 13, 3, 53, 63, DARK * 2);
        for (int y = 4; y < 62; y++)
            for (int x = 14; x < 52; x++) {
                bool rim = x < 16 || x > 49 || y < 6 || y > 59;
                float n = 0.75f + 0.35f * fbm(x, y, 8, 481);
                set(img, x, y, RGB{0.46f, 0.43f, 0.38f} * (n * (rim ? (x < 16 || y < 6 ? 1.15f : 0.75f) : 1.0f)));
            }
        rect(img, 31, 12, 35, 44, RGB{0.2f, 0.18f, 0.16f});
        rect(img, 24, 20, 42, 24, RGB{0.2f, 0.18f, 0.16f});
        for (int k = 0; k < 3; k++)
            for (int x = 20; x < 46; x++)
                if (hash(x / 2, k, 482) > 0.35f) set(img, x, 49 + k * 3, RGB{0.22f, 0.2f, 0.17f});
        crack(img, 483, RGB{0.15f, 0.13f, 0.11f}, SOLID, 1, 16);
        break;
    case Tex::CatacombCeiling:
        fill(img, [](int x, int y, float&) { return RGB{0.20f, 0.16f, 0.13f} * (0.45f + 0.7f * fbm(x, y, 4, 61)); });
        crack(img, 62, RGB{0.08f, 0.06f, 0.05f}, SOLID, 4, 30);
        break;

    case Tex::EmberWall:
        emberWall(img);
        crack(img, 72, LAVA, GLOWING, 1, 14);
        break;
    case Tex::EmberWallVein: {
        emberWall(img);
        float x = 30, dir = 1.57f;
        for (int y = 0; y < N; y++) {
            set(img, int(x) - 1, y, LAVA * 0.8f, GLOWING);
            set(img, int(x), y, LAVA_HOT, GLOWING);
            dir += (hash(y, 0, 491) - 0.5f) * 0.8f;
            x += std::cos(dir) * 1.2f;
            if (y % 17 == 9) crack(img, 492 + y, LAVA, GLOWING, 1, 9);
        }
        glowAround(img, 30, 32, 16, 0.8f, {1.4f, 0.7f, 0.4f});
        break;
    }
    case Tex::EmberWallGrate:
        emberWall(img);
        glowAround(img, 32, 34, 22, -0.6f);
        for (int y = 24; y < 46; y++)
            for (int x = 20; x < 44; x++) {
                float heat = 1 - std::hypot(x - 32.0f, y - 40.0f) / 20;
                set(img, x, y, mixc(RGB{0.6f, 0.12f, 0.03f}, LAVA_HOT, std::clamp(heat + hash(x, y, 501) * 0.3f, 0.0f, 1.0f)), GLOWING);
            }
        for (int x = 20; x < 44; x++)
            for (int y = 24; y < 46; y++)
                if ((x - 20) % 6 < 2 || y < 26 || y > 43) set(img, x, y, IRON * ((x - 20) % 6 == 0 ? 1.4f : 0.9f));
        rect(img, 18, 22, 46, 24, IRON * 1.3f);
        break;
    case Tex::EmberWallChains:
        emberWall(img);
        crack(img, 72, LAVA, GLOWING, 1, 10);
        for (int k = 0; k < 2; k++) {
            int cx = 20 + k * 22, len = 40 + k * 10;
            for (int y = 2; y < len; y++) {
                bool linkSide = (y / 4) % 2 == 0;
                RGB c = IRON * (y % 4 == 0 ? 1.7f : 1.2f);
                if (linkSide) set(img, cx - 1, y, c), set(img, cx + 1, y, c * 0.7f), set(img, cx + 3, y, get(img, cx + 3, y) * 0.5f);
                else set(img, cx, y, c);
            }
            rect(img, cx - 3, len, cx + 4, len + 2, IRON * 1.4f);
            rect(img, cx - 3, len + 2, cx - 1, len + 7, IRON * 1.1f);
            rect(img, cx + 2, len + 2, cx + 4, len + 7, IRON * 0.8f);
        }
        break;

    case Tex::EmberFloor: emberFloor(img, 0.26f); break;
    case Tex::EmberFloorCracked: {
        emberFloor(img, 0.08f);
        float x = 4, y = 20, dir = 0.3f;
        for (int s = 0; s < 70; s++) {
            set(img, int(x), int(y), LAVA_HOT, GLOWING);
            set(img, int(x), int(y) + 1, RGB{0.08f, 0.04f, 0.03f});
            dir += (hash(s, 0, 511) - 0.5f) * 0.9f;
            x += std::cos(dir), y += std::sin(dir) * 0.8f;
        }
        break;
    }
    case Tex::EmberFloorGrate:
        emberFloor(img, 0.05f);
        glowAround(img, 32, 32, 26, 0.6f, {1.5f, 0.6f, 0.3f});
        grate(img, 20, 20, 44, 44, LAVA, GLOWING);
        tweak(img, [](int x, int y, RGB c, float& a) {
            if (a != GLOWING || x < 20 || x >= 44 || y < 20 || y >= 44) return c;
            return mixc(RGB{0.5f, 0.08f, 0.02f}, LAVA_HOT, std::clamp(1 - std::hypot(x - 32.0f, y - 32.0f) / 16 + hash(x, y, 521) * 0.4f, 0.0f, 1.0f));
        });
        break;
    case Tex::EmberFloorAsh:
        emberFloor(img, 0.1f);
        tweak(img, [](int x, int y, RGB c, float& a) {
            float m = fbm(x, y, 8, 531) - std::hypot(x - 30.0f, y - 34.0f) / 44;
            if (m < 0.2f) return c;
            a = SOLID;
            RGB ash = RGB{0.42f, 0.40f, 0.38f} * (0.6f + 0.6f * fbm(x, y, 16, 532));
            return hash(x, y, 533) > 0.97f ? RGB{0.9f, 0.4f, 0.1f} : mixc(c, ash, std::min(1.0f, (m - 0.2f) * 8));
        });
        bone(img, 22, 30, 34, 36, BONE * 0.45f);
        break;
    case Tex::EmberCeiling:
        fill(img, [](int x, int y, float&) { return RGB{0.17f, 0.11f, 0.10f} * (0.45f + 0.65f * fbm(x, y, 4, 91)); });
        crack(img, 92, RGB{0.05f, 0.03f, 0.03f}, SOLID, 3, 30);
        break;

    case Tex::Door:
        fill(img, [](int x, int y, float&) {
            int plank = x / 13;
            float grain = noise(x * 4.0f, y * 0.25f, 8, 101 + plank);
            RGB wood = RGB{0.42f, 0.27f, 0.15f} * ((0.7f + 0.3f * hash(plank, 0, 102)) * (0.65f + 0.45f * grain));
            if (x % 13 == 0) wood = wood * 0.35f;
            if (x % 13 == 1) wood = wood * 1.2f;
            if ((y >= 10 && y < 15) || (y >= 49 && y < 54)) {
                bool rivet = (x % 13 == 6) && (y == 12 || y == 51);
                if (rivet) return RGB{0.70f, 0.66f, 0.60f};
                return RGB{0.20f, 0.20f, 0.22f} * ((y == 10 || y == 49 ? 1.6f : y == 14 || y == 53 ? 0.6f : 1.0f) * (0.8f + 0.4f * noise(x, y, 16, 103)));
            }
            return wood;
        });
        disc(img, 50, 33, 4.5f, {0.30f, 0.29f, 0.28f});
        disc(img, 50, 33, 2.8f, RGB{0.42f, 0.27f, 0.15f} * 0.3f);
        rect(img, 47, 26, 54, 29, IRON * 1.3f);
        break;
    case Tex::Wood:
        fill(img, [](int x, int y, float&) {
            float grain = noise(x * 0.3f, y * 5.0f, 8, 111), knot = std::hypot(x - 40.0f, (y - 20.0f) * 0.5f);
            return RGB{0.45f, 0.30f, 0.17f} * ((0.6f + 0.5f * grain) * (knot < 3 ? 0.55f : 1.0f));
        });
        break;
    case Tex::Iron:
        fill(img, [](int x, int y, float&) {
            float dent = hash(x / 4, y / 4, 122) > 0.85f ? 0.85f : 1.0f;
            return RGB{0.38f, 0.39f, 0.42f} * ((0.65f + 0.45f * fbm(x, y, 16, 121)) * dent);
        });
        break;
    case Tex::Bone:
        fill(img, [](int x, int y, float&) {
            float pore = hash(x, y, 133) > 0.94f ? 0.8f : 1.0f;
            return RGB{0.86f, 0.80f, 0.66f} * ((0.72f + 0.33f * fbm(x, y, 8, 131)) * pore);
        });
        crack(img, 132, {0.45f, 0.40f, 0.30f}, SOLID, 2);
        break;
    case Tex::Fur:
        fill(img, [](int x, int y, float&) {
            float strand = noise(x * 4.0f, y * 0.5f + noise(x, y, 4, 142) * 6, 8, 141);
            return RGB{0.48f, 0.40f, 0.33f} * (0.45f + 0.75f * strand);
        });
        break;
    case Tex::Flesh:
        fill(img, [](int x, int y, float&) {
            float n = fbm(x, y, 8, 151), blotch = fbm(x, y, 4, 152);
            return mixc({0.70f, 0.55f, 0.48f}, {0.48f, 0.32f, 0.32f}, blotch) * (0.6f + 0.55f * n);
        });
        break;
    case Tex::Cloth:
        fill(img, [](int x, int y, float&) {
            float weave = ((x + y) % 4 < 2) ? 1.0f : 0.85f;
            return RGB{0.62f, 0.58f, 0.50f} * (weave * (0.6f + 0.5f * fbm(x, y, 8, 161)));
        });
        break;
    case Tex::Robe:
        fill(img, [](int x, int y, float&) {
            float fold = 0.72f + 0.28f * std::sin(x * PI / 8 + noise(x, y, 4, 171) * 2.5f);
            return RGB{0.50f, 0.47f, 0.50f} * (fold * (0.7f + 0.3f * fbm(x, y, 8, 172)));
        });
        break;
    case Tex::Slime:
        fill(img, [](int x, int y, float& a) {
            float n = fbm(x, y, 4, 181);
            Cells c = cellular(x, y, 6, 182);
            bool bubble = c.d1 < 0.16f && hash(c.id, 0, 183) > 0.6f;
            a = bubble ? GLOWING : SOLID;
            return bubble ? RGB{0.55f, 0.95f, 0.40f} * (1.1f - c.d1 * 2) : RGB{0.30f, 0.62f, 0.22f} * (0.55f + 0.6f * n);
        });
        break;
    case Tex::Chitin:
        fill(img, [](int x, int y, float&) {
            int ly = y % 16;
            float plate = ly < 2 ? 0.35f : ly < 4 ? 1.7f : 1.0f - (ly - 4) * 0.03f;
            return RGB{0.24f, 0.17f, 0.26f} * (plate * (0.7f + 0.6f * fbm(x, y, 8, 191)));
        });
        break;
    case Tex::Rust:
        fill(img, [](int x, int y, float&) {
            float n = fbm(x, y, 8, 201), drip = noise(x * 4.0f, y * 0.25f, 8, 202);
            float rust = std::clamp((n - 0.42f) * 2.5f + (drip > 0.7f ? 0.4f : 0), 0.0f, 1.0f);
            return mixc({0.42f, 0.42f, 0.45f}, {0.42f, 0.22f, 0.11f}, rust) * (0.75f + 0.35f * n);
        });
        break;
    case Tex::Flame:
        fill(img, [](int x, int y, float& a) {
            float fx = (x - 31.5f) / 32, fy = y / 64.0f;
            float width = 0.15f + 0.85f * std::sin(std::min(1.0f, fy * 1.1f) * 3.1416f * 0.5f) * (1.0f - fy * 0.25f);
            float n = noise(x, y, 8, 211);
            float edge = std::fabs(fx) / std::max(0.05f, width) + (n - 0.5f) * 0.6f;
            a = edge < 1.0f ? GLOWING : HOLE;
            float heat = std::clamp(1.0f - edge + fy * 0.6f, 0.0f, 1.0f);
            return mixc({0.85f, 0.22f, 0.04f}, {1.0f, 0.92f, 0.55f}, heat);
        });
        break;
    case Tex::Glow:
        fill(img, [](int x, int y, float& a) {
            float d = std::hypot(x - 31.5f, y - 31.5f) / 32;
            a = d < 1 ? GLOWING : HOLE;
            float k = std::clamp(1 - d, 0.0f, 1.0f);
            return RGB{1, 1, 1} * (k * k);
        });
        break;
    case Tex::Gold:
        fill(img, [](int x, int y, float&) {
            float n = fbm(x, y, 8, 221), band = 0.75f + 0.35f * std::sin((x + y) * 0.35f + n * 3);
            float glint = hash(x / 2, y / 2, 222) > 0.95f ? 1.45f : 1.0f;
            return RGB{0.90f, 0.66f, 0.24f} * ((0.6f + 0.45f * n) * band * glint);
        });
        break;
    case Tex::Glass:
        fill(img, [](int x, int y, float& a) {
            bool hi = x > 10 && x < 15;
            a = hi ? GLOWING : SOLID;
            return hi ? RGB{0.95f, 0.95f, 0.92f} : RGB{0.80f, 0.80f, 0.82f} * (0.75f + 0.25f * noise(x, y, 4, 231));
        });
        break;
    case Tex::Parchment:
        fill(img, [](int x, int y, float&) {
            RGB c = RGB{0.82f, 0.74f, 0.55f} * (0.75f + 0.3f * fbm(x, y, 8, 241));
            if (y % 8 == 4 && x > 8 && x < 56 && hash(x / 5, y, 242) > 0.3f) c = c * 0.45f;
            return c;
        });
        break;
    case Tex::Stone:
        fill(img, [](int x, int y, float&) {
            float speck = hash(x, y, 252);
            return RGB{0.48f, 0.47f, 0.46f} * ((0.62f + 0.48f * fbm(x, y, 8, 251)) * (speck > 0.92f ? 0.75f : speck < 0.05f ? 1.2f : 1.0f));
        });
        crack(img, 253, {0.2f, 0.2f, 0.2f}, SOLID, 2, 24);
        break;
    case Tex::Eye:
        fill(img, [](int x, int y, float& a) {
            float d = std::hypot(x - 31.5f, y - 31.5f) / 32;
            a = GLOWING;
            return mixc({1.0f, 0.85f, 0.4f}, {0.9f, 0.15f, 0.05f}, std::clamp(d * 1.5f, 0.0f, 1.0f));
        });
        break;

    case Tex::Steel:
        fill(img, [](int x, int y, float&) {
            float brush = noise(x * 4.0f, y * 0.25f, 8, 541), n = fbm(x, y, 8, 542);
            return RGB{0.76f, 0.78f, 0.84f} * ((0.82f + 0.22f * brush) * (0.85f + 0.2f * n));
        });
        for (int i = 0; i < 6; i++) {
            float x = hash(i, 0, 543) * N, y = hash(i, 1, 543) * N;
            line(img, x, y, x + 5, y + 2, RGB{0.85f, 0.87f, 0.92f});
        }
        break;
    case Tex::Leather:
        fill(img, [](int x, int y, float&) {
            float n = fbm(x, y, 8, 551), crease = noise(x * 2.0f, y * 0.5f, 8, 552);
            RGB c = RGB{0.44f, 0.28f, 0.16f} * ((0.7f + 0.4f * n) * (crease > 0.8f ? 0.78f : 1.0f));
            if ((x == 3 || x == 60) && y % 4 < 2) c = RGB{0.62f, 0.50f, 0.34f};
            return c;
        });
        break;
    case Tex::Chain:
        fill(img, [](int x, int y, float&) {
            int row = y / 6;
            float lx = float((x + (row % 2) * 4) % 8) + 0.5f - 4, ly = float(y % 6) + 0.5f - 3;
            float d = std::hypot(lx / 3.6f, ly / 2.7f);
            float k = d < 0.5f ? 0.15f : d < 1.0f ? (ly < 0 ? 1.35f : 0.85f) : 0.3f;
            return RGB{0.55f, 0.57f, 0.62f} * (k * (0.75f + 0.4f * fbm(x, y, 8, 561)));
        });
        break;
    case Tex::Membrane:
        fill(img, [](int x, int y, float&) { return RGB{0.36f, 0.22f, 0.21f} * (0.6f + 0.6f * fbm(x, y, 4, 571)); });
        crack(img, 572, {0.14f, 0.07f, 0.07f}, SOLID, 6, 34);
        break;
    case Tex::Hide:
        fill(img, [](int x, int y, float&) {
            float n = fbm(x, y, 8, 581), blotch = fbm(x, y, 4, 582);
            return RGB{0.50f, 0.46f, 0.31f} * ((0.65f + 0.45f * n) * (blotch > 0.6f ? 0.75f : 1.0f));
        });
        for (int i = 0; i < 12; i++) lump(img, hash(i, 0, 583) * N, hash(i, 1, 583) * N, 1.6f, 1.6f, RGB{0.58f, 0.54f, 0.38f});
        break;
    case Tex::Ghoul:
        fill(img, [](int x, int y, float&) {
            float n = fbm(x, y, 8, 591), bruise = fbm(x, y, 4, 592);
            RGB c = mixc({0.58f, 0.62f, 0.53f}, {0.40f, 0.36f, 0.42f}, std::clamp((bruise - 0.55f) * 4, 0.0f, 1.0f));
            return c * (0.7f + 0.4f * n);
        });
        crack(img, 593, {0.26f, 0.30f, 0.34f}, SOLID, 5, 30);
        break;
    case Tex::ImpSkin:
        fill(img, [](int x, int y, float&) {
            Cells c = cellular(x, y, 10, 601);
            if (c.d2 - c.d1 < 0.1f) return RGB{0.22f, 0.04f, 0.03f};
            return RGB{0.62f, 0.14f, 0.08f} * ((0.7f + 0.3f * hash(c.id, 0, 602)) * (1.15f - c.d1 * 0.8f));
        });
        crack(img, 603, LAVA, GLOWING, 7, 14);
        crack(img, 604, LAVA_HOT, GLOWING, 3, 6);
        break;
    case Tex::Maw:
        fill(img, [](int x, int y, float&) {
            float ridge = 0.75f + 0.25f * std::sin(y * 1.1f), n = fbm(x, y, 8, 611);
            RGB c = RGB{0.42f, 0.07f, 0.08f} * (ridge * (0.6f + 0.6f * n));
            return hash(x, y, 612) > 0.992f ? RGB{0.85f, 0.55f, 0.55f} : c;
        });
        break;
    case Tex::Orb:
        fill(img, [](int x, int y, float& a) {
            a = GLOWING;
            float swirl = fbm(x + 8 * std::sin(y * 0.2f), y + 8 * std::cos(x * 0.2f), 4, 621);
            return mixc({0.12f, 0.55f, 0.45f}, {0.75f, 1.0f, 0.85f}, std::clamp((swirl - 0.35f) * 2.5f, 0.0f, 1.0f));
        });
        break;
    case Tex::Water:
        fill(img, [](int x, int y, float& a) {
            float ripple = std::sin(std::hypot(x - 32.0f, y - 32.0f) * 0.7f + fbm(x, y, 4, 631) * 5);
            bool glint = ripple > 0.93f && hash(x, y, 632) > 0.5f;
            a = glint ? GLOWING : SOLID;
            return glint ? RGB{0.45f, 0.62f, 0.68f} : RGB{0.10f, 0.22f, 0.30f} * (0.8f + 0.3f * ripple);
        });
        break;
    case Tex::Clay:
        fill(img, [](int x, int y, float&) {
            float ring = 0.85f + 0.15f * std::sin(y * 1.6f), n = fbm(x, y, 8, 641);
            return RGB{0.62f, 0.36f, 0.20f} * (ring * (0.7f + 0.4f * n));
        });
        break;
    case Tex::Coals:
        fill(img, [](int x, int y, float& a) {
            Cells c = cellular(x, y, 8, 651);
            float heat = hash(c.id, 0, 652);
            if (c.d2 - c.d1 < 0.1f || heat < 0.35f) return RGB{0.08f, 0.06f, 0.05f} * (1 + heat);
            a = GLOWING;
            return mixc({0.55f, 0.08f, 0.02f}, LAVA_HOT, std::clamp(heat - c.d1 * 0.8f, 0.0f, 1.0f));
        });
        break;
    case Tex::Rib:
        fill(img, [](int x, int y, float&) {
            int band = y % 16;
            float k = band < 2 ? 0.5f : band < 4 ? 1.3f : band > 13 ? 0.7f : 1.0f;
            float joint = (x % 32) < 1 ? 0.5f : 1.0f;
            return RGB{0.46f, 0.45f, 0.46f} * (k * joint * (0.7f + 0.4f * fbm(x, y, 8, 661)));
        });
        break;
    case Tex::Beam:
        fill(img, [](int x, int y, float&) {
            float grain = noise(x * 0.25f, y * 4.0f, 8, 671), split = noise(x * 0.5f, y * 2.0f, 8, 672) > 0.82f ? 0.5f : 1.0f;
            return RGB{0.34f, 0.22f, 0.13f} * ((0.55f + 0.5f * grain) * split);
        });
        break;
    case Tex::Count: break;
    }
}

// Reduce to a 16-colour palette like a 4-bit CLUT: farthest-point seeding keeps small,
// strongly coloured details (candle flames, eyes) from being averaged away, then k-means.
void quantise(Img& img) {
    auto dist = [](const Texel& t, RGB c) { return (t.r - c.r) * (t.r - c.r) + (t.g - c.g) * (t.g - c.g) + (t.b - c.b) * (t.b - c.b); };
    RGB centres[16];
    static float nearest[N * N];
    centres[0] = {img.px[0].r, img.px[0].g, img.px[0].b};
    for (int i = 0; i < N * N; i++) nearest[i] = dist(img.px[i], centres[0]);
    for (int c = 1; c < 16; c++) {
        int pick = int(std::max_element(nearest, nearest + N * N) - nearest);
        centres[c] = {img.px[pick].r, img.px[pick].g, img.px[pick].b};
        for (int i = 0; i < N * N; i++) nearest[i] = std::min(nearest[i], dist(img.px[i], centres[c]));
    }
    static int assign[N * N];
    for (int round = 0; round < 8; round++) {
        RGB sum[16] = {};
        int count[16] = {};
        for (int i = 0; i < N * N; i++) {
            float best = 1e9f;
            for (int c = 0; c < 16; c++)
                if (float d = dist(img.px[i], centres[c]); d < best) best = d, assign[i] = c;
            sum[assign[i]] = sum[assign[i]] + RGB{img.px[i].r, img.px[i].g, img.px[i].b};
            count[assign[i]]++;
        }
        for (int c = 0; c < 16; c++)
            if (count[c]) centres[c] = sum[c] * (1.0f / float(count[c]));
    }
    for (int i = 0; i < N * N; i++) img.px[i].r = centres[assign[i]].r, img.px[i].g = centres[assign[i]].g, img.px[i].b = centres[assign[i]].b;
}

}  // namespace

std::vector<uint32_t> buildAtlas() {
    static_assert(int(Tex::Count) <= ATLAS_CELLS * ATLAS_CELLS);
    std::vector<uint32_t> atlas(size_t(ATLAS_PX) * ATLAS_PX, 0xFFFF00FF);
    static Img img;
    for (int t = 0; t < int(Tex::Count); t++) {
        img = Img{};
        paintTexture(Tex(t), img);
        if (Tex(t) != Tex::White) quantise(img);
        int ox = (t % ATLAS_CELLS) * N, oy = (t / ATLAS_CELLS) * N;
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                const Texel& p = img.at(x, y);
                auto byte = [](float v) { return int(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f); };
                atlas[size_t(oy + y) * ATLAS_PX + ox + x] = rgba(byte(p.r), byte(p.g), byte(p.b), byte(p.a));
            }
    }
    return atlas;
}
