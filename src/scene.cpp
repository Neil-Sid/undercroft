#include "scene.h"
#include <algorithm>
#include <cmath>

namespace {

const ThemeLook LOOKS[] = {
    {Tex::CryptWall, Tex::CryptFloor, Tex::CryptCeiling, {0.010f, 0.013f, 0.024f}, {0.06f, 0.07f, 0.105f}, {1.25f, 0.93f, 0.66f}},
    {Tex::CatacombWall, Tex::CatacombFloor, Tex::CatacombCeiling, {0.017f, 0.014f, 0.008f}, {0.075f, 0.068f, 0.045f}, {1.24f, 0.84f, 0.48f}},
    {Tex::EmberWall, Tex::EmberFloor, Tex::EmberCeiling, {0.036f, 0.009f, 0.004f}, {0.13f, 0.048f, 0.032f}, {1.32f, 0.8f, 0.48f}},
};

// Surface variants: 16 slots per theme, so the rarer looks take fewer slots.
const Tex WALLS[3][16] = {
    {Tex::CryptWall, Tex::CryptWall, Tex::CryptWall, Tex::CryptWall, Tex::CryptWall, Tex::CryptWall, Tex::CryptWall, Tex::CryptWall,
     Tex::CryptWallCracked, Tex::CryptWallCracked, Tex::CryptWallCracked, Tex::CryptWallMoss, Tex::CryptWallMoss, Tex::CryptWallNiche,
     Tex::CryptWallBanner, Tex::CryptWallSconce},
    {Tex::CatacombWall, Tex::CatacombWall, Tex::CatacombWall, Tex::CatacombWall, Tex::CatacombWall, Tex::CatacombWall, Tex::CatacombWall,
     Tex::CatacombWallRough, Tex::CatacombWallRough, Tex::CatacombWallRough, Tex::CatacombWallRough, Tex::CatacombWallNiche,
     Tex::CatacombWallNiche, Tex::CatacombWallNiche, Tex::CatacombWallBones, Tex::CatacombWallBones},
    {Tex::EmberWall, Tex::EmberWall, Tex::EmberWall, Tex::EmberWall, Tex::EmberWall, Tex::EmberWall, Tex::EmberWall, Tex::EmberWall,
     Tex::EmberWall, Tex::EmberWallVein, Tex::EmberWallVein, Tex::EmberWallChains, Tex::EmberWallChains, Tex::EmberWallChains,
     Tex::EmberWallGrate, Tex::EmberWallGrate},
};
const Tex FLOORS[3][16] = {
    {Tex::CryptFloor, Tex::CryptFloor, Tex::CryptFloor, Tex::CryptFloor, Tex::CryptFloor, Tex::CryptFloor, Tex::CryptFloor, Tex::CryptFloor,
     Tex::CryptFloor, Tex::CryptFloor, Tex::CryptFloorCracked, Tex::CryptFloorCracked, Tex::CryptFloorCracked, Tex::CryptFloorStain,
     Tex::CryptFloorStain, Tex::CryptFloorGrate},
    {Tex::CatacombFloor, Tex::CatacombFloor, Tex::CatacombFloor, Tex::CatacombFloor, Tex::CatacombFloor, Tex::CatacombFloor,
     Tex::CatacombFloor, Tex::CatacombFloor, Tex::CatacombFloor, Tex::CatacombFloorBones, Tex::CatacombFloorBones, Tex::CatacombFloorBones,
     Tex::CatacombFloorPuddle, Tex::CatacombFloorPuddle, Tex::CatacombFloorSlab, Tex::CatacombFloorSlab},
    {Tex::EmberFloor, Tex::EmberFloor, Tex::EmberFloor, Tex::EmberFloor, Tex::EmberFloor, Tex::EmberFloor, Tex::EmberFloor, Tex::EmberFloor,
     Tex::EmberFloor, Tex::EmberFloor, Tex::EmberFloorCracked, Tex::EmberFloorCracked, Tex::EmberFloorAsh, Tex::EmberFloorAsh,
     Tex::EmberFloorAsh, Tex::EmberFloorGrate},
};

// How each theme builds its ceilings: vault rise, rib material, cornice along the wall tops,
// and timber frames (instead of stone ribs) across corridors.
struct Style {
    float rise;
    Tex rib;
    uint32_t ribTint;
    bool cornice, timber;
};
const Style STYLES[] = {
    {0.55f, Tex::Rib, 0xFFFFFFFF, true, false},
    {0.32f, Tex::Beam, 0xFFFFFFFF, false, true},
    {0.45f, Tex::Rib, hex(0x8A6058), true, false},
};

// Stairwell proportions: steps run down the cell and on under its neighbour.
constexpr int STEPS = 12;
constexpr float RUN = CELL / 6, RISE = 0.27f;

uint32_t mixBits(uint32_t a) {
    a ^= a >> 16, a *= 0x7feb352du, a ^= a >> 15, a *= 0x846ca68bu, a ^= a >> 16;
    return a;
}

// Per-cell tint so repeated textures don't look stamped: brightness and a little warmth.
uint32_t tintFor(const Cell& c, float shade) {
    float v = (0.82f + 0.18f * float(c.variant & 15) / 15.0f) * shade;
    float warm = float((c.variant >> 4) & 3) * 0.02f * shade;
    auto b = [](float f) { return int(std::fmin(1.0f, std::fmax(0.0f, f)) * 255); };
    return rgba(b(v + warm), b(v), b(v - warm), 255);
}

uint32_t mulColour(uint32_t a, uint32_t b) {
    uint32_t out = 0;
    for (int i = 0; i < 32; i += 8) out |= (((a >> i) & 255) * ((b >> i) & 255) / 255) << i;
    return out;
}

const Cell& cellOr(const Level& L, Pos p, const Cell& fallback) { return L.inside(p) ? L.at(p) : fallback; }

// The texture of the wall face that the rock cell `rock` shows towards direction `d`'s opposite.
Tex wallTex(const Level& L, Pos rock, int d, const Cell& fallback) {
    uint32_t h = mixBits(uint32_t(cellOr(L, rock, fallback).variant) * 4u + uint32_t(d) + uint32_t(rock.x) * 131u + uint32_t(rock.y) * 7919u);
    return WALLS[int(L.theme)][h & 15];
}

Tex floorTex(const Level& L, Pos p) {
    uint32_t h = mixBits(uint32_t(L.at(p).variant) * 13u + uint32_t(p.x) * 977u + uint32_t(p.y) * 61u);
    return FLOORS[int(L.theme)][h & 15];
}

// A stairwell descends straight on from the way the player most likely arrives (the shortest
// route from the level start), so the steps are seen falling away into the dark.
int stairsDir(const Level& L, Pos p) {
    std::vector<int> dist = distanceMap(L, L.start);
    int best = (L.at(p).variant >> 2) & 3, bestDist = 1 << 30;
    for (int d = 0; d < 4; d++) {
        Pos from = p - DIRS[d];
        int k = L.inside(from) ? dist[size_t(from.y) * L.width + from.x] : -1;
        if (k >= 0 && k < bestDist) best = d, bestDist = k;
    }
    return best;
}

enum class Vault : uint8_t { Dome, AlongX, AlongZ, Flat };

// Ceiling height above WALL_H, as a fraction of the rise, at (u, w) across a cell.
float vaultCurve(Vault v, float u, float w) {
    switch (v) {
    case Vault::Dome: return std::sin(PI * u) * std::sin(PI * w);
    case Vault::AlongX: return std::sin(PI * w);
    case Vault::AlongZ: return std::sin(PI * u);
    case Vault::Flat: return 0;
    }
    return 0;
}

struct LevelMesh {
    const Level& L;
    const ThemeLook& look;
    const Style& style;
    std::vector<Vault> vaults;
    std::vector<Vertex> out;

    LevelMesh(const Level& level) : L(level), look(themeLook(level.theme)), style(STYLES[int(level.theme)]) {
        vaults.assign(size_t(L.width) * L.height, Vault::Flat);
        for (int y = 0; y < L.height; y++)
            for (int x = 0; x < L.width; x++) vaults[size_t(y) * L.width + x] = classify({x, y});
    }

    bool open(Pos p) const { return !solid(L.tile(p)); }

    // Corridors get barrel vaults along their length, rooms a dome per cell; doorways and
    // pillars sit under flat ceilings so nothing shows above them.
    Vault classify(Pos p) const {
        Tile t = L.tile(p);
        if (solid(t)) return Vault::Flat;
        const Decor* d = L.decorAt(p);
        if (t == Tile::Door || (d && d->kind == DecorKind::Pillar)) return Vault::Flat;
        bool ew = !open(p + DIRS[1]) && !open(p + DIRS[3]), ns = !open(p + DIRS[0]) && !open(p + DIRS[2]);
        if (ew && !ns) return Vault::AlongZ;
        if (ns && !ew) return Vault::AlongX;
        return Vault::Dome;
    }

    Vault vaultAt(Pos p) const { return L.inside(p) ? vaults[size_t(p.y) * L.width + p.x] : Vault::Flat; }

    float ceilingAt(Pos p, float u, float w) const { return WALL_H + style.rise * vaultCurve(vaultAt(p), u, w); }

    float ceilingAt(Vec3 q) const {
        Pos p{int(std::floor(q.x / CELL)), int(std::floor(q.z / CELL))};
        return ceilingAt(p, q.x / CELL - float(p.x), q.z / CELL - float(p.y));
    }

    bool rock(Vec3 q) const {
        Pos p{int(std::floor(q.x / CELL)), int(std::floor(q.z / CELL))};
        if (!open(p)) return true;
        return (q.y < 0 && L.tile(p) != Tile::StairsDown) || q.y > ceilingAt(q);
    }

    // Ambient occlusion: how much of the space around `p`, on the side `n` faces, is rock.
    float occlusion(Vec3 p, Vec3 n) const {
        static const Vec3 DIRS3[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0.58f, 0.58f, 0.58f}, {-0.58f, 0.58f, 0.58f},
                                     {0.58f, -0.58f, 0.58f}, {-0.58f, -0.58f, 0.58f}, {0.58f, 0.58f, -0.58f}, {-0.58f, 0.58f, -0.58f},
                                     {0.58f, -0.58f, -0.58f}, {-0.58f, -0.58f, -0.58f}};
        float hit = 0, total = 0;
        for (Vec3 d : DIRS3) {
            float facing = dot(d, n);
            if (facing < -0.05f) continue;
            for (float reach : {0.3f, 0.75f}) {
                float w = (0.4f + facing) * (reach < 0.5f ? 1.0f : 0.6f);
                total += w;
                if (rock(p + n * 0.04f + d * reach)) hit += w;
            }
        }
        return total > 0 ? hit / total : 0;
    }

    uint32_t shade(const Cell& c, Vec3 p, Vec3 n, float k = 1) const { return tintFor(c, k * (1 - 0.75f * occlusion(p, n))); }

    void vertex(Vec3 p, Vec3 n, float u, float v, Tex t, uint32_t col) {
        Vertex x;
        x.pos = p, x.normal = n, x.u = u, x.v = v, x.tile = float(int(t)), x.color = col;
        out.push_back(x);
    }

    // A grid-subdivided quad: affine texturing needs the extra vertices, and they carry the baked light.
    template <class Colour>
    void quad(Vec3 origin, Vec3 du, Vec3 dv, Vec3 n, Vec2 uv0, Vec2 uvSpan, Tex t, int divU, int divV, Colour colour) {
        Vertex grid[9][9];
        for (int b = 0; b <= divV; b++)
            for (int a = 0; a <= divU; a++) {
                float fu = float(a) / float(divU), fv = float(b) / float(divV);
                Vertex& x = grid[b][a];
                x.pos = origin + du * fu + dv * fv, x.normal = n, x.u = uv0.x + fu * uvSpan.x, x.v = uv0.y + fv * uvSpan.y, x.tile = float(int(t));
                x.color = colour(x.pos);
            }
        for (int j = 0; j < divV; j++)
            for (int i = 0; i < divU; i++) out.insert(out.end(), {grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j][i], grid[j + 1][i + 1], grid[j + 1][i]});
    }

    void floor(Pos p, const Cell& c) {
        Vec3 o{float(p.x) * CELL, 0, float(p.y) * CELL};
        quad(o, {CELL, 0, 0}, {0, 0, CELL}, {0, 1, 0}, {0, 0}, {1, 1}, floorTex(L, p), 4, 4, [&](Vec3 q) { return shade(c, q, {0, 1, 0}); });
    }

    // The vault over a cell as a height field, normals from its slope.
    void ceiling(Pos p, const Cell& c) {
        const int div = 4;
        auto at = [&](int i, int j) {
            float u = float(i) / div, w = float(j) / div;
            return Vec3{(float(p.x) + u) * CELL, ceilingAt(p, u, w), (float(p.y) + w) * CELL};
        };
        auto normal = [&](int i, int j) {
            Vec3 dx = at(std::min(i + 1, div), j) - at(std::max(i - 1, 0), j), dz = at(i, std::min(j + 1, div)) - at(i, std::max(j - 1, 0));
            return normalize(cross(dx, dz));
        };
        for (int j = 0; j < div; j++)
            for (int i = 0; i < div; i++) {
                int idx[4][2] = {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}};
                Vertex v[4];
                for (int k = 0; k < 4; k++) {
                    Vec3 q = at(idx[k][0], idx[k][1]), n = normal(idx[k][0], idx[k][1]);
                    if (n.y > 0) n = -n;
                    v[k].pos = q, v[k].normal = n, v[k].u = float(idx[k][0]) / div, v[k].v = float(idx[k][1]) / div;
                    v[k].tile = float(int(look.ceiling)), v[k].color = shade(c, q, n, 0.9f);
                }
                out.insert(out.end(), {v[0], v[2], v[1], v[0], v[3], v[2]});
            }
    }

    // Walls face into this cell wherever the neighbour is rock; the texture spans the full face once.
    void walls(Pos p, const Cell& c) {
        Vec3 o{float(p.x) * CELL + CELL / 2, 0, float(p.y) * CELL + CELL / 2};
        for (int d = 0; d < 4; d++) {
            if (open(p + DIRS[d])) continue;
            const Cell& wc = cellOr(L, p + DIRS[d], c);
            Vec3 n = -dirVector(d), right{n.z, 0, -n.x};
            Vec3 topLeft = o + dirVector(d) * (CELL / 2) - right * (CELL / 2) + Vec3{0, WALL_H, 0};
            quad(topLeft, right * CELL, {0, -WALL_H, 0}, n, {0, 0}, {1, 1}, wallTex(L, p + DIRS[d], d, c), 4, 4,
                 [&](Vec3 q) { return shade(wc, q, n, 0.88f + 0.12f * q.y / WALL_H); });
            if (style.cornice) {
                Vec3 base = topLeft - Vec3{0, 0.14f, 0};
                quad(base + n * 0.07f, right * CELL, {0, -0.1f, 0}, n, {0, 0}, {2, 0.2f}, style.rib, 2, 1, [&](Vec3) { return mulColour(tintFor(wc, 0.9f), style.ribTint); });
                quad(base + n * 0.07f + Vec3{0, -0.1f, 0}, right * CELL, n * -0.07f, {0, -1, 0}, {0, 0}, {2, 0.1f}, style.rib, 2, 1,
                     [&](Vec3) { return mulColour(tintFor(wc, 0.55f), style.ribTint); });
            }
        }
    }

    // A band hanging under the ceiling along a cell edge, following the vault: ribs and beams.
    void rib(Vec3 a, Vec3 b, Pos owner, float width, float depth, Tex t, uint32_t col) {
        Vec3 along = b - a, across = normalize(cross(along, {0, 1, 0})) * (width / 2);
        const int div = 6;
        for (int i = 0; i < div; i++) {
            Vec3 p0 = a + along * (float(i) / div), p1 = a + along * (float(i + 1) / div);
            p0.y = ceilingAt(p0 + across * 0.01f) + 0.002f, p1.y = ceilingAt(p1 + across * 0.01f) + 0.002f;
            Vec3 d0 = p0 - Vec3{0, depth, 0}, d1 = p1 - Vec3{0, depth, 0};
            const Cell& c = L.at(owner);
            uint32_t lit = mulColour(tintFor(c, 0.85f), col), dim = mulColour(tintFor(c, 0.6f), col);
            float u0 = float(i) / div * 2, u1 = float(i + 1) / div * 2;
            quad(d0 - across, d1 - d0, across * 2, {0, -1, 0}, {u0, 0}, {u1 - u0, 0.25f}, t, 1, 1, [&](Vec3) { return lit; });
            for (float s : {-1.0f, 1.0f}) {
                Vec3 side = across * s;
                quad(p0 + side, p1 - p0, d0 - p0, normalize(side), {u0, 0}, {u1 - u0, 0.2f}, t, 1, 1, [&](Vec3) { return dim; });
            }
        }
    }

    // Where neighbouring vaults meet at different heights, fill the gap with a lunette.
    void edges(Pos p, const Cell& c) {
        for (int d : {1, 2}) {
            Pos q = p + DIRS[d];
            if (!open(q)) continue;
            Vec3 a = d == 1 ? Vec3{float(q.x) * CELL, 0, float(p.y) * CELL} : Vec3{float(p.x) * CELL, 0, float(q.y) * CELL};
            Vec3 along = d == 1 ? Vec3{0, 0, CELL} : Vec3{CELL, 0, 0};
            const int div = 6;
            auto heights = [&](int i) {
                float s = float(i) / div;
                float hp = d == 1 ? ceilingAt(p, 1, s) : ceilingAt(p, s, 1), hq = d == 1 ? ceilingAt(q, 0, s) : ceilingAt(q, s, 0);
                return Vec2{hp, hq};
            };
            bool mismatch = false;
            for (int i = 0; i <= div; i++) mismatch |= std::fabs(heights(i).x - heights(i).y) > 0.01f;
            if (mismatch) {
                for (int i = 0; i < div; i++) {
                    Vec2 h0 = heights(i), h1 = heights(i + 1);
                    Vec3 e0 = a + along * (float(i) / div), e1 = a + along * (float(i + 1) / div);
                    Vec3 n = dirVector(d) * ((h0.x + h1.x) > (h0.y + h1.y) ? 1.0f : -1.0f);
                    Vec3 t0 = e0 + Vec3{0, std::max(h0.x, h0.y), 0}, t1 = e1 + Vec3{0, std::max(h1.x, h1.y), 0};
                    Vec3 b0 = e0 + Vec3{0, std::min(h0.x, h0.y), 0}, b1 = e1 + Vec3{0, std::min(h1.x, h1.y), 0};
                    uint32_t col = tintFor(c, 0.7f);
                    vertex(t0, n, float(i) / div, 0, look.ceiling, col), vertex(t1, n, float(i + 1) / div, 0, look.ceiling, col),
                        vertex(b1, n, float(i + 1) / div, 0.2f, look.ceiling, col);
                    vertex(t0, n, float(i) / div, 0, look.ceiling, col), vertex(b1, n, float(i + 1) / div, 0.2f, look.ceiling, col),
                        vertex(b0, n, float(i) / div, 0.2f, look.ceiling, col);
                }
                continue;
            }
            Vault vp = vaultAt(p);
            bool corridor = vp == Vault::AlongX || vp == Vault::AlongZ;
            if (style.timber) {
                if (corridor && (p.x + p.y) % 2 == 0) timberFrame(a, a + along, p);
            } else {
                rib(a, a + along, p, 0.18f, 0.11f, style.rib, style.ribTint);
            }
        }
    }

    // A square timber from lo to hi, `r` either side of its axis.
    void timber(Vec3 lo, Vec3 hi, float r, uint32_t col) {
        Vec3 ax = hi - lo, a = normalize(cross(ax, std::fabs(ax.y) > 0.9f * length(ax) ? Vec3{1, 0, 0} : Vec3{0, 1, 0})) * r, b = normalize(cross(ax, a)) * r;
        Vec3 sides[4] = {a, b, -a, -b};
        for (int i = 0; i < 4; i++) {
            Vec3 s = sides[i], t = sides[(i + 1) % 4];
            quad(lo + s - t, ax, t * 2, normalize(s), {0, 0}, {length(ax), 0.15f}, Tex::Beam, 1, 1, [&](Vec3) { return col; });
        }
    }

    // Catacomb shoring: two posts against the walls and a beam across the passage.
    void timberFrame(Vec3 a, Vec3 b, Pos owner) {
        Vec3 along = normalize(b - a), l = a + along * 0.1f, r = b - along * 0.1f, top{0, WALL_H - 0.12f, 0};
        uint32_t col = tintFor(L.at(owner), 0.8f);
        timber(l, l + Vec3{0, WALL_H, 0}, 0.09f, col);
        timber(r, r + Vec3{0, WALL_H, 0}, 0.09f, col);
        timber(l + top, r + top, 0.1f, col);
    }

    // A stairwell: steps fall away through the cell and on under the next one into darkness.
    void stairs(Pos p, const Cell& c) {
        int d = stairsDir(L, p);
        Vec3 fwd = dirVector(d), side{-fwd.z, 0, fwd.x};
        Vec3 origin = cellCenter(p) - fwd * (CELL / 2) - side * (CELL / 2);
        auto at = [&](float u, float w, float y) { return origin + side * u + fwd * w + Vec3{0, y, 0}; };
        auto roof = [](float w) { return std::min(-0.2f, -RISE * w / RUN + 1.7f); };
        auto dark = [&](Vec3 q) { float k = std::clamp(1 + q.y / 4.2f, 0.03f, 1.0f); return tintFor(c, std::pow(k, 1.4f) * 1.15f); };
        Tex wall = look.ceiling;
        for (int i = 0; i < STEPS; i++) {
            float w0 = float(i) * RUN, w1 = w0 + RUN, top = -float(i + 1) * RISE, above = -float(i) * RISE;
            bool tunnel = i >= STEPS / 2;
            quad(at(0, w0, top), side * CELL, fwd * RUN, {0, 1, 0}, {0, 0.3f}, {2, -0.3f}, Tex::Rib, 2, 1, dark);
            quad(at(0, w0, above), side * CELL, {0, top - above, 0}, fwd, {0, 0}, {1, RISE / CELL}, wall, 2, 1, dark);
            float t0 = tunnel ? roof(w0) : 0, t1 = tunnel ? roof(w1) : 0;
            for (float s : {0.0f, 1.0f}) {
                Vec3 n = side * (s == 0 ? 1.0f : -1.0f);
                Vec3 a = at(s * CELL, w0, t0), b = at(s * CELL, w1, t1), bb = at(s * CELL, w1, top), ab = at(s * CELL, w0, top);
                float u0 = w0 / CELL, u1 = w1 / CELL;
                vertex(a, n, u0, -t0 / WALL_H, wall, dark(a)), vertex(b, n, u1, -t1 / WALL_H, wall, dark(b)), vertex(bb, n, u1, -top / WALL_H, wall, dark(bb));
                vertex(a, n, u0, -t0 / WALL_H, wall, dark(a)), vertex(bb, n, u1, -top / WALL_H, wall, dark(bb)), vertex(ab, n, u0, -top / WALL_H, wall, dark(ab));
            }
            if (tunnel) quad(at(0, w0, roof(w0)), side * CELL, at(0, w1, roof(w1)) - at(0, w0, roof(w0)), {0, -1, 0}, {0, 0}, {1, RUN / CELL}, look.ceiling, 2, 1, dark);
        }
        float end = float(STEPS) * RUN;
        quad(at(0, CELL, 0), side * CELL, {0, roof(CELL), 0}, -fwd, {0, 0}, {1, 0.1f}, wall, 2, 1, dark);
        quad(at(0, end, roof(end)), side * CELL, {0, -float(STEPS) * RISE - roof(end), 0}, -fwd, {0, 0}, {1, 1}, wall, 2, 2, dark);
        // A low kerb round the three open edges of the well.
        uint32_t kerb = tintFor(c, 0.9f), kerbSide = tintFor(c, 0.6f);
        const float k = 0.14f, h = 0.12f;
        auto kerbRun = [&](Vec3 a, Vec3 b, Vec3 inward) {
            quad(a + Vec3{0, h, 0}, b - a, inward * k, {0, 1, 0}, {0, 0}, {2, 0.1f}, style.rib, 2, 1, [&](Vec3) { return kerb; });
            quad(a + inward * k + Vec3{0, h, 0}, b - a, {0, -h, 0}, inward, {0, 0}, {2, 0.1f}, style.rib, 2, 1, [&](Vec3) { return kerbSide; });
            quad(a + Vec3{0, h, 0}, b - a, {0, -h, 0}, -inward, {0, 0}, {2, 0.1f}, style.rib, 2, 1, [&](Vec3) { return kerbSide; });
        };
        kerbRun(at(0, 0, 0), at(CELL, 0, 0), fwd);
        kerbRun(at(0, CELL, 0), at(0, 0, 0), side);
        kerbRun(at(CELL, 0, 0), at(CELL, CELL, 0), -side);
        // Newel posts at the corners mark the well from across the room.
        for (float u : {0.12f, CELL - 0.12f})
            for (float w : {0.12f, CELL - 0.12f}) post(at(u, w, 0), 0.11f, 0.8f, c);
    }

    // A squat square post with a pyramid cap.
    void post(Vec3 base, float r, float h, const Cell& c) {
        uint32_t lit = mulColour(tintFor(c, 0.95f), style.ribTint), dim = mulColour(tintFor(c, 0.65f), style.ribTint);
        Vec3 sides[4] = {{r, 0, 0}, {0, 0, r}, {-r, 0, 0}, {0, 0, -r}};
        for (int i = 0; i < 4; i++) {
            Vec3 s = sides[i], t = sides[(i + 1) % 4], apex = base + Vec3{0, h + r * 1.6f, 0};
            quad(base + s - t + Vec3{0, h, 0}, t * 2, {0, -h, 0}, normalize(s), {0, 0}, {0.3f, h}, style.rib, 1, 2, [&](Vec3 q) { return q.y > base.y + h * 0.5f ? lit : dim; });
            Vec3 a = base + s - t + Vec3{0, h, 0}, b = base + s + t + Vec3{0, h, 0}, n = normalize(s + Vec3{0, r, 0});
            vertex(a, n, 0, 0.2f, style.rib, lit), vertex(b, n, 0.3f, 0.2f, style.rib, lit), vertex(apex, n, 0.15f, 0, style.rib, lit);
        }
    }

    void build() {
        out.reserve(size_t(L.width) * L.height * 200);
        for (int y = 0; y < L.height; y++)
            for (int x = 0; x < L.width; x++) {
                Pos p{x, y};
                if (!open(p)) continue;
                const Cell& c = L.at(p);
                if (L.tile(p) == Tile::StairsDown) stairs(p, c);
                else floor(p, c);
                ceiling(p, c);
                walls(p, c);
                edges(p, c);
            }
    }
};

}  // namespace

const ThemeLook& themeLook(Theme t) { return LOOKS[int(t)]; }

std::vector<Vertex> buildLevelMesh(const Level& level) {
    LevelMesh m(level);
    m.build();
    return std::move(m.out);
}

std::vector<PointLight> levelLights(const Level& L, float time) {
    std::vector<PointLight> lights;
    for (const Decor& d : L.decor) {
        if (d.kind != DecorKind::Brazier) continue;
        float flicker = 0.9f + 0.1f * std::sin(time * 9 + float(d.pos.x) * 3.1f) * std::sin(time * 5.3f + float(d.pos.y));
        lights.push_back({cellCenter(d.pos) + Vec3{0, 1.3f, 0}, 9.0f, Vec3{1.35f, 0.62f, 0.25f} * flicker});
    }
    // Sconces, candle niches and lava behind grates light the walls they hang on.
    for (int y = 0; y < L.height; y++)
        for (int x = 0; x < L.width; x++) {
            Pos p{x, y};
            if (solid(L.tile(p))) continue;
            for (int d = 0; d < 4; d++) {
                if (!solid(L.tile(p + DIRS[d]))) continue;
                Vec3 face = cellCenter(p) + dirVector(d) * (CELL / 2 - 0.4f);
                float flicker = 0.85f + 0.15f * std::sin(time * 7 + float(x * 5 + y * 3 + d));
                switch (wallTex(L, p + DIRS[d], d, L.at(p))) {
                case Tex::CryptWallSconce: lights.push_back({face + Vec3{0, 2.0f, 0}, 5.5f, Vec3{1.1f, 0.68f, 0.32f} * flicker}); break;
                case Tex::CryptWallNiche: lights.push_back({face + Vec3{0, 1.1f, 0}, 2.8f, Vec3{0.8f, 0.5f, 0.25f} * flicker}); break;
                case Tex::EmberWallGrate: lights.push_back({face + Vec3{0, 1.2f, 0}, 4.5f, Vec3{1.2f, 0.42f, 0.12f} * flicker}); break;
                case Tex::EmberWallVein: lights.push_back({face + Vec3{0, 1.3f, 0}, 3.2f, Vec3{0.9f, 0.32f, 0.08f} * flicker}); break;
                default: break;
                }
            }
            if (L.tile(p) == Tile::Floor && floorTex(L, p) == Tex::EmberFloorGrate)
                lights.push_back({cellCenter(p) + Vec3{0, 0.4f, 0}, 3.8f, Vec3{1.1f, 0.4f, 0.1f} * (0.85f + 0.15f * std::sin(time * 4 + float(x)))});
        }
    return lights;
}
