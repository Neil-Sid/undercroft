#include "models.h"
#include <algorithm>
#include <cmath>

namespace {

Model models[int(ModelId::Count)];
constexpr uint32_t WHITE = 0xFFFFFFFF;
constexpr uint32_t BLACK = hex(0x070505);

float hashf(int a, int b) {
    uint32_t h = uint32_t(a) * 374761393u + uint32_t(b) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
}

// Rotation taking +Y onto `d`.
Mat4 alignY(Vec3 d) {
    Vec3 y = normalize(d), h = std::fabs(y.z) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
    Vec3 x = normalize(cross(y, h)), z = cross(x, y);
    Mat4 m;
    m.at(0, 0) = x.x, m.at(1, 0) = x.y, m.at(2, 0) = x.z;
    m.at(0, 1) = y.x, m.at(1, 1) = y.y, m.at(2, 1) = y.z;
    m.at(0, 2) = z.x, m.at(1, 2) = z.y, m.at(2, 2) = z.z;
    return m;
}

// The front surface of an ellipsoid at (x, y), for setting eyes and mouths onto a ball.
float frontZ(Vec3 c, Vec3 r, float x, float y) {
    float dx = (x - c.x) / r.x, dy = (y - c.y) / r.y;
    return c.z + r.z * std::sqrt(std::max(0.0f, 1 - dx * dx - dy * dy));
}

struct Builder {
    std::vector<Vertex> v;
    Mat4 xf;

    void vert(Vec3 p, Vec3 n, float u, float w, Tex t, uint32_t col) {
        Vertex out;
        out.pos = xf.point(p);
        out.normal = normalize(xf.point(n) - xf.point({0, 0, 0}));
        out.u = u, out.v = w, out.tile = float(int(t));
        out.color = col;
        v.push_back(out);
    }

    void tri(Vec3 a, Vec3 b, Vec3 c, Tex t, uint32_t col = WHITE, Vec3 n = {}) {
        if (length(n) == 0) n = normalize(cross(c - a, b - a));
        vert(a, n, 0, 0, t, col), vert(b, n, 1, 0, t, col), vert(c, n, 0.5f, 1, t, col);
    }

    // a top-left, b top-right, c bottom-right, d bottom-left, seen from the front.
    void face(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Tex t, uint32_t col, float uw, float vh, Vec3 n = {}) {
        if (length(n) == 0) n = normalize(cross(d - a, b - a));
        vert(a, n, 0, 0, t, col), vert(b, n, uw, 0, t, col), vert(c, n, uw, vh, t, col);
        vert(a, n, 0, 0, t, col), vert(c, n, uw, vh, t, col), vert(d, n, 0, vh, t, col);
    }

    // `scale` is metres per texture repeat; `vScale` overrides it vertically.
    void box(Vec3 lo, Vec3 hi, Tex t, uint32_t col = WHITE, float scale = 0.5f, float vScale = 0) {
        float su = 1 / scale, sv = 1 / (vScale > 0 ? vScale : scale);
        float dx = (hi.x - lo.x) * su, dy = (hi.y - lo.y) * sv, dz = (hi.z - lo.z) * su, dzv = (hi.z - lo.z) * sv;
        face({lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z}, t, col, dx, dy);
        face({hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z}, {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, t, col, dx, dy);
        face({hi.x, hi.y, hi.z}, {hi.x, hi.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, t, col, dz, dy);
        face({lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z}, {lo.x, lo.y, hi.z}, {lo.x, lo.y, lo.z}, t, col, dz, dy);
        face({lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}, t, col, dx, dzv);
        face({lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}, t, col, dx, dzv);
    }

    // A tapered n-sided column standing on `base`; six or more sides are smooth shaded.
    void prism(Vec3 base, float r0, float r1, float h, int sides, Tex t, uint32_t col = WHITE, float scale = 0.5f, bool caps = true) {
        float around = 2 * PI * std::max(r0, r1) / scale, slope = (r0 - r1) / std::max(h, 0.001f);
        auto rim = [&](float a, float r, float y) { return base + Vec3{std::cos(a) * r, y, std::sin(a) * r}; };
        auto normal = [&](float a) { return normalize(Vec3{std::cos(a), slope, std::sin(a)}); };
        for (int i = 0; i < sides; i++) {
            float a0 = 2 * PI * i / sides + PI / sides, a1 = a0 + 2 * PI / sides;
            Vec3 n0 = normal(sides < 6 ? (a0 + a1) / 2 : a0), n1 = normal(sides < 6 ? (a0 + a1) / 2 : a1);
            float u0 = around * i / sides, u1 = around * (i + 1) / sides, vh = h / scale;
            Vec3 b0 = rim(a0, r0, 0), b1 = rim(a1, r0, 0), t0 = rim(a0, r1, h), t1 = rim(a1, r1, h);
            vert(t1, n1, u1, 0, t, col), vert(t0, n0, u0, 0, t, col), vert(b0, n0, u0, vh, t, col);
            vert(t1, n1, u1, 0, t, col), vert(b0, n0, u0, vh, t, col), vert(b1, n1, u1, vh, t, col);
            if (!caps) continue;
            auto uv = [&](float a, float r) { return Vec2{0.5f + std::cos(a) * r / scale, 0.5f + std::sin(a) * r / scale}; };
            Vec2 c0 = uv(a0, r1), c1 = uv(a1, r1);
            vert(base + Vec3{0, h, 0}, {0, 1, 0}, 0.5f, 0.5f, t, col), vert(t1, {0, 1, 0}, c1.x, c1.y, t, col), vert(t0, {0, 1, 0}, c0.x, c0.y, t, col);
            c0 = uv(a0, r0), c1 = uv(a1, r0);
            vert(base, {0, -1, 0}, 0.5f, 0.5f, t, col), vert(b0, {0, -1, 0}, c0.x, c0.y, t, col), vert(b1, {0, -1, 0}, c1.x, c1.y, t, col);
        }
    }

    // A tapered column from a to b.
    void limb(Vec3 a, Vec3 b, float r0, float r1, int sides, Tex t, uint32_t col = WHITE, float scale = 0.5f, bool caps = true) {
        Mat4 saved = xf;
        xf = saved * translate(a) * alignY(b - a);
        prism({0, 0, 0}, r0, r1, length(b - a), sides, t, col, scale, caps);
        xf = saved;
    }

    // A low-poly ellipsoid; `bumps` jitters the radius for lumpy, organic shapes.
    void ball(Vec3 c, Vec3 r, int sides, int rings, Tex t, uint32_t col = WHITE, float scale = 0.5f, float bumps = 0) {
        auto dir = [&](int i, int j) {
            float a = 2 * PI * float(i) / float(sides), e = PI * float(j) / float(rings) - PI / 2;
            float k = (j == 0 || j == rings) ? 1 : 1 + bumps * (hashf(i % sides, j) - 0.5f);
            return Vec3{std::cos(e) * std::cos(a) * k, std::sin(e) * k, std::cos(e) * std::sin(a) * k};
        };
        float su = 2 * PI * std::max(r.x, r.z) / scale / float(sides), sv = PI * r.y / scale / float(rings);
        for (int j = 0; j < rings; j++)
            for (int i = 0; i < sides; i++) {
                Vec3 q[4] = {dir(i, j + 1), dir(i + 1, j + 1), dir(i + 1, j), dir(i, j)};
                float us[4] = {i * su, (i + 1) * su, (i + 1) * su, i * su};
                float vs[4] = {(rings - j - 1) * sv, (rings - j - 1) * sv, (rings - j) * sv, (rings - j) * sv};
                for (int o : {0, 1, 2, 0, 2, 3}) vert(c + q[o] * r, normalize(Vec3{q[o].x / r.x, q[o].y / r.y, q[o].z / r.z}), us[o], vs[o], t, col);
            }
    }

    // A flat outline (x, y, half-thickness at each point) faceted up to a ridge at its centre:
    // axe heads, ears, flanges. Thin points take the edge colour, so cutting edges catch the light.
    void plate(std::initializer_list<Vec3> outline, Tex t, uint32_t col = WHITE, uint32_t edge = WHITE, float scale = 0.3f) {
        std::vector<Vec3> p(outline);
        Vec3 c{};
        float apex = 0;
        for (Vec3 q : p) c += Vec3{q.x, q.y, 0}, apex = std::max(apex, q.z);
        c = c * (1.0f / float(p.size()));
        auto colour = [&](Vec3 q) { return q.z < apex * 0.35f ? edge : col; };
        for (size_t i = 0; i < p.size(); i++) {
            Vec3 a = p[i], b = p[(i + 1) % p.size()];
            for (float s : {1.0f, -1.0f}) {
                Vec3 A{a.x, a.y, a.z * s}, B{b.x, b.y, b.z * s}, C{c.x, c.y, apex * s};
                Vec3 n = normalize(cross(A - C, B - C));
                if (n.z * s < 0) n = -n;
                vert(C, n, C.x / scale, C.y / scale, t, col), vert(A, n, A.x / scale, A.y / scale, t, colour(a)), vert(B, n, B.x / scale, B.y / scale, t, colour(b));
            }
            if (a.z > 0 || b.z > 0) {
                Vec3 n = normalize(Vec3{b.y - a.y, a.x - b.x, 0});
                face({a.x, a.y, a.z}, {b.x, b.y, b.z}, {b.x, b.y, -b.z}, {a.x, a.y, -a.z}, t, col, 0.1f, 0.1f, n);
            }
        }
    }

    // A blade along +Y: diamond section, a ridge down the middle and bright bevelled edges,
    // `w0` wide at the hilt and `w1` where it starts tapering to the point.
    void blade(float y0, float len, float w0, float w1, float thick, float tip, Tex t, uint32_t col, uint32_t edge) {
        float y1 = y0 + len - tip;
        Vec3 T{0, y0 + len, 0};
        for (float s : {1.0f, -1.0f})
            for (float side : {-1.0f, 1.0f}) {
                Vec3 E0{side * w0, y0, 0}, E1{side * w1, y1, 0}, M0{0, y0, thick * s}, M1{0, y1, thick * 0.75f * s};
                Vec3 n = normalize(Vec3{side * thick, 0, s * w1});
                float ve0 = y0 * 3, ve1 = y1 * 3;
                vert(E0, n, 0, ve0, t, edge), vert(E1, n, 0, ve1, t, edge), vert(M1, n, 0.3f, ve1, t, col);
                vert(E0, n, 0, ve0, t, edge), vert(M1, n, 0.3f, ve1, t, col), vert(M0, n, 0.3f, ve0, t, col);
                vert(E1, n, 0, ve1, t, edge), vert(T, n, 0.15f, T.y * 3, t, edge), vert(M1, n, 0.3f, ve1, t, col);
            }
    }

    // A ring in the XY plane (axis Z), like a finger ring or a hoop.
    void torus(Vec3 c, float R, float r, int segs, int tube, Tex t, uint32_t col = WHITE) {
        auto at = [&](int i, int j, Vec3& n) {
            float a = 2 * PI * float(i) / float(segs), b = 2 * PI * float(j) / float(tube);
            Vec3 out{std::cos(a), std::sin(a), 0};
            n = out * std::cos(b) + Vec3{0, 0, std::sin(b)};
            return c + out * R + n * r;
        };
        for (int i = 0; i < segs; i++)
            for (int j = 0; j < tube; j++) {
                Vec3 n[4], p[4] = {at(i, j, n[0]), at(i + 1, j, n[1]), at(i + 1, j + 1, n[2]), at(i, j + 1, n[3])};
                for (int o : {0, 1, 2, 0, 2, 3}) vert(p[o], n[o], float(i) / 2, float(j) / 2, t, col);
            }
    }

    void clear() { v.clear(), xf = Mat4{}; }
};

Role armRole(int s) { return s < 0 ? Role::ArmL : Role::ArmR; }
Role foreRole(int s) { return s < 0 ? Role::ForeL : Role::ForeR; }
Role legRole(int s) { return s < 0 ? Role::LegL : Role::LegR; }
Role shinRole(int s) { return s < 0 ? Role::ShinL : Role::ShinR; }
Role wingRole(int s) { return s < 0 ? Role::WingL : Role::WingR; }

// Collects geometry into parts of one model.
struct Rigger {
    Renderer& r;
    Model m;
    Builder b;

    Rigger(Renderer& renderer, Rig rig, float height) : r(renderer) { m.rig = rig, m.height = height; }

    int part(int parent, Vec3 pivot, Role role, float phase = 0) {
        Part& p = m.parts.emplace_back();
        p.mesh = r.addMesh(b.v), p.parent = parent, p.pivot = pivot, p.role = role, p.phase = phase;
        b.clear();
        return int(m.parts.size()) - 1;
    }
};

Model single(Renderer& r, Builder& b, float height) {
    Rigger g(r, Rig::Static, height);
    g.b.v = std::move(b.v);
    b.clear();
    g.part(-1, {0, 0, 0}, Role::Root);
    return g.m;
}

// Glowing eyes centred on `c`, `spread` either side.
void eyes(Builder& b, Vec3 c, float spread, float size, Tex t = Tex::Eye) {
    for (float s : {-1.0f, 1.0f}) b.box(c + Vec3{s * spread - size, -size * 0.75f, -0.006f}, c + Vec3{s * spread + size, size * 0.75f, 0.006f}, t);
}

// A row of `n` pointed teeth between x0 and x1, hanging from y by `len` (negative points up).
void teeth(Builder& b, float x0, float x1, float y, float z, int n, float len, uint32_t col = hex(0xE8E0C8)) {
    for (int i = 0; i < n; i++) {
        float x = mix(x0, x1, (i + 0.5f) / float(n)), w = std::fabs(x1 - x0) / float(n) * 0.45f;
        b.limb({x, y, z}, {x, y - len, z}, w, 0, 4, Tex::Bone, col);
    }
}

// Hooked claws reaching down from a palm and curling forward.
void claws(Builder& b, Vec3 palm, float len, int n, float spread, uint32_t col) {
    for (int i = 0; i < n; i++) {
        float x = (i - (n - 1) * 0.5f) * spread;
        Vec3 root = palm + Vec3{x, 0, 0.015f}, mid = palm + Vec3{x * 1.3f, -len * 0.6f, 0.05f}, tip = palm + Vec3{x * 1.5f, -len, 0.1f};
        b.limb(root, mid, 0.014f, 0.01f, 4, Tex::Bone, col);
        b.limb(mid, tip, 0.01f, 0.0f, 4, Tex::Bone, col);
    }
}

// The open front of a hood (rim at `c`): a face lost in shadow, with glowing eyes.
void hoodOpening(Builder& b, Vec3 c, float rx, float ry, Tex t, uint32_t col, Tex eyeTex) {
    Mat4 saved = b.xf;
    b.xf = saved * translate(c + Vec3{0, 0, -0.06f}) * rotateX(PI / 2) * scale({1, 1, ry / rx});
    b.prism({0, 0, 0}, rx * 0.9f, rx, 0.07f, 8, t, col, 0.4f, false);
    b.xf = saved;
    b.ball(c + Vec3{0, 0, -0.06f}, {rx * 0.85f, ry * 0.85f, 0.04f}, 8, 4, Tex::White, BLACK);
    eyes(b, c + Vec3{0, ry * 0.12f, -0.014f}, rx * 0.38f, rx * 0.18f, eyeTex);
}

// A skull centred on its cranium, facing +Z; pass Tex::White as `eyeTex` for empty sockets.
// The jaw is separate (skullJaw) so it can open; its hinge sits at (0, -0.07, 0) * s.
void skull(Builder& b, Vec3 c, float s, uint32_t col, Tex eyeTex) {
    b.ball(c + Vec3{0, 0.02f, -0.01f} * s, Vec3{0.095f, 0.1f, 0.11f} * s, 8, 5, Tex::Bone, col);
    b.box(c + Vec3{-0.07f, -0.075f, 0.0f} * s, c + Vec3{0.07f, -0.01f, 0.1f} * s, Tex::Bone, col);
    for (float x : {-1.0f, 1.0f}) {
        b.box(c + Vec3{x * 0.037f - 0.026f, -0.03f, 0.08f} * s, c + Vec3{x * 0.037f + 0.026f, 0.018f, 0.107f} * s, Tex::White, BLACK);
        if (eyeTex != Tex::White) b.box(c + Vec3{x * 0.037f - 0.011f, -0.014f, 0.1075f} * s, c + Vec3{x * 0.037f + 0.011f, 0.002f, 0.111f} * s, eyeTex);
    }
    b.box(c + Vec3{-0.012f, -0.062f, 0.098f} * s, c + Vec3{0.012f, -0.036f, 0.106f} * s, Tex::White, BLACK);
    for (int i = 0; i < 5; i++) {
        float x = (i - 2) * 0.018f;
        b.box(c + Vec3{x - 0.007f, -0.092f, 0.075f} * s, c + Vec3{x + 0.007f, -0.072f, 0.1f} * s, Tex::Bone, hex(0xF4ECD8));
    }
}

void skullJaw(Builder& b, float s, uint32_t col) {
    b.box(Vec3{-0.066f, -0.05f, -0.005f} * s, Vec3{0.066f, -0.022f, 0.095f} * s, Tex::Bone, col);
    for (float x : {-1.0f, 1.0f}) b.box(Vec3{x * 0.066f - 0.01f, -0.05f, -0.01f} * s, Vec3{x * 0.066f + 0.01f, 0.0f, 0.02f} * s, Tex::Bone, col);
    for (int i = 0; i < 5; i++) {
        float x = (i - 2) * 0.018f;
        b.box(Vec3{x - 0.007f, -0.024f, 0.07f} * s, Vec3{x + 0.007f, -0.006f, 0.093f} * s, Tex::Bone, hex(0xF4ECD8));
    }
}

// Hand-held items: grip at the origin, business end along +Y. Composes with the caller's b.xf.
void weapon(Builder& b, ModelId w, bool rusty = false) {
    Tex metal = rusty ? Tex::Rust : Tex::Steel;
    uint32_t blade = rusty ? hex(0xD0C4B4) : hex(0xA8B0BC), edge = rusty ? hex(0xF4ECE0) : WHITE, haft = hex(0x9A7A58);
    Mat4 base = b.xf;
    switch (w) {
    case ModelId::Dagger:
        b.ball({0, -0.115f, 0}, {0.022f, 0.022f, 0.022f}, 6, 3, Tex::Gold);
        b.limb({0, -0.1f, 0}, {0, 0, 0}, 0.017f, 0.019f, 6, Tex::Leather, WHITE, 0.2f);
        b.box({-0.06f, 0, -0.016f}, {0.06f, 0.022f, 0.016f}, Tex::Gold, WHITE, 0.2f);
        b.blade(0.022f, 0.27f, 0.036f, 0.028f, 0.01f, 0.09f, metal, blade, edge);
        break;
    case ModelId::Sword:
        b.ball({0, -0.155f, 0}, {0.03f, 0.03f, 0.03f}, 6, 3, metal, hex(0xA0A4AC));
        b.limb({0, -0.135f, 0}, {0, 0, 0}, 0.019f, 0.021f, 6, Tex::Leather, WHITE, 0.2f);
        b.box({-0.12f, 0, -0.02f}, {0.12f, 0.03f, 0.02f}, metal, hex(0xA0A4AC), 0.2f);
        for (float s : {-1.0f, 1.0f}) b.ball({s * 0.125f, 0.015f, 0}, {0.022f, 0.022f, 0.022f}, 6, 3, metal, hex(0xA0A4AC));
        b.blade(0.03f, 0.6f, 0.036f, 0.03f, 0.011f, 0.12f, metal, blade, edge);
        break;
    case ModelId::Greatsword:
        b.ball({0, -0.31f, 0}, {0.04f, 0.035f, 0.04f}, 6, 3, metal, hex(0x9098A4));
        b.limb({0, -0.29f, 0}, {0, 0, 0}, 0.023f, 0.025f, 6, Tex::Leather, WHITE, 0.2f);
        b.box({-0.2f, 0, -0.03f}, {0.2f, 0.045f, 0.03f}, metal, hex(0x9098A4), 0.2f);
        for (float s : {-1.0f, 1.0f}) b.limb({s * 0.19f, 0.02f, 0}, {s * 0.25f, 0.09f, 0}, 0.024f, 0.01f, 4, metal, hex(0x9098A4));
        b.box({-0.035f, 0.045f, -0.013f}, {0.035f, 0.15f, 0.013f}, metal, blade, 0.2f);
        b.blade(0.15f, 0.95f, 0.055f, 0.045f, 0.016f, 0.16f, metal, blade, edge);
        break;
    case ModelId::Mace:
        b.limb({0, -0.12f, 0}, {0, 0.44f, 0}, 0.02f, 0.022f, 6, Tex::Wood, haft, 0.3f);
        b.limb({0, -0.12f, 0}, {0, 0.06f, 0}, 0.025f, 0.025f, 6, Tex::Leather, WHITE, 0.2f);
        b.ball({0, -0.13f, 0}, {0.03f, 0.025f, 0.03f}, 6, 3, Tex::Iron);
        b.ball({0, 0.48f, 0}, {0.06f, 0.075f, 0.06f}, 8, 4, Tex::Iron, hex(0xB0B4BC));
        for (int i = 0; i < 6; i++) {
            b.xf = base * rotateY(float(i) * PI / 3);
            b.plate({{0.03f, 0.4f, 0.012f}, {0.11f, 0.44f, 0.008f}, {0.115f, 0.53f, 0.008f}, {0.03f, 0.57f, 0.012f}}, metal, blade, edge, 0.2f);
        }
        b.xf = base;
        b.limb({0, 0.55f, 0}, {0, 0.64f, 0}, 0.022f, 0.0f, 4, metal, blade);
        break;
    case ModelId::Axe:
        b.limb({0, -0.12f, 0}, {0, 0.64f, 0}, 0.021f, 0.023f, 6, Tex::Wood, haft, 0.3f);
        b.limb({0, -0.12f, 0}, {0, 0.05f, 0}, 0.025f, 0.025f, 6, Tex::Leather, WHITE, 0.2f);
        b.box({-0.032f, 0.44f, -0.03f}, {0.032f, 0.58f, 0.03f}, Tex::Iron, hex(0x9098A0), 0.2f);
        b.plate({{0.03f, 0.56f, 0.022f}, {0.09f, 0.55f, 0.016f}, {0.2f, 0.67f, 0.0f}, {0.24f, 0.52f, 0.0f}, {0.19f, 0.35f, 0.0f}, {0.09f, 0.45f, 0.016f}, {0.03f, 0.46f, 0.022f}},
                metal, blade, edge, 0.2f);
        b.limb({-0.03f, 0.51f, 0}, {-0.12f, 0.49f, 0}, 0.022f, 0.0f, 4, metal, blade);
        break;
    case ModelId::Spear:
        b.limb({0, -0.6f, 0}, {0, 0.84f, 0}, 0.019f, 0.017f, 6, Tex::Wood, haft, 0.4f);
        b.limb({0, -0.6f, 0}, {0, -0.68f, 0}, 0.02f, 0.006f, 4, Tex::Iron);
        b.limb({0, 0.78f, 0}, {0, 0.92f, 0}, 0.026f, 0.018f, 6, Tex::Iron, hex(0x9098A0), 0.2f);
        b.prism({0, 0.72f, 0}, 0.024f, 0.024f, 0.05f, 6, Tex::Cloth, hex(0xA02820), 0.2f);
        b.blade(0.92f, 0.3f, 0.018f, 0.05f, 0.013f, 0.17f, metal, blade, edge);
        break;
    case ModelId::Torch:
        b.limb({0, -0.16f, 0}, {0, 0.3f, 0}, 0.022f, 0.028f, 6, Tex::Wood, WHITE, 0.3f);
        b.prism({0, 0.25f, 0}, 0.034f, 0.034f, 0.04f, 6, Tex::Iron, hex(0x908880), 0.2f, false);
        b.prism({0, 0.29f, 0}, 0.038f, 0.048f, 0.1f, 7, Tex::Cloth, hex(0x6A5040), 0.2f);
        b.ball({0, 0.39f, 0}, {0.048f, 0.03f, 0.048f}, 7, 3, Tex::Coals);
        break;
    default: break;
    }
    b.xf = base;
}

// The ogre's club: a knotted log studded with nails.
void club(Builder& b) {
    b.limb({0, -0.12f, 0}, {0, 0.5f, 0}, 0.04f, 0.06f, 7, Tex::Wood, hex(0xA08060), 0.3f);
    b.limb({0, 0.45f, 0}, {0, 1.05f, 0}, 0.075f, 0.14f, 8, Tex::Wood, hex(0xA08060), 0.4f);
    b.prism({0, 0.46f, 0}, 0.08f, 0.085f, 0.06f, 8, Tex::Iron, hex(0x8A8078), 0.3f, false);
    for (int i = 0; i < 9; i++) {
        float a = float(i) * 2.4f, y = 0.65f + 0.13f * float(i % 3), r = 0.09f + 0.045f * float(i % 3);
        Vec3 out{std::cos(a), 0, std::sin(a)};
        b.limb(out * r + Vec3{0, y, 0}, out * (r + 0.09f) + Vec3{0, y + 0.02f, 0}, 0.016f, 0.0f, 4, Tex::Iron, hex(0xC0C0C8));
    }
}

// Chest body (0.9 x 0.45 x 0.6) and its rounded lid, hinged along the back top edge.
void chestBox(Builder& b) {
    const uint32_t band = hex(0x8A8078);
    b.box({-0.45f, 0, -0.3f}, {0.45f, 0.45f, 0.3f}, Tex::Wood, hex(0xD0B090), 0.6f);
    for (float x : {-0.3f, 0.3f}) b.box({x - 0.035f, -0.005f, -0.31f}, {x + 0.035f, 0.455f, 0.31f}, Tex::Iron, band, 0.3f);
    for (float x : {-1.0f, 1.0f})
        for (float z : {-1.0f, 1.0f}) b.box({x * 0.46f - 0.05f, 0, z * 0.31f - 0.05f}, {x * 0.46f + 0.05f, 0.1f, z * 0.31f + 0.05f}, Tex::Iron, band, 0.3f);
    b.box({-0.07f, 0.26f, 0.29f}, {0.07f, 0.42f, 0.325f}, Tex::Iron, hex(0xE8C070), 0.2f);
    b.box({-0.012f, 0.3f, 0.325f}, {0.012f, 0.36f, 0.33f}, Tex::White, BLACK);
}

void chestLid(Builder& b) {
    const int steps = 6;
    auto arc = [](int i) { float a = PI * float(i) / steps; return Vec3{0, std::sin(a) * 0.17f, 0.3f - std::cos(a) * 0.3f}; };
    for (int i = 0; i < steps; i++) {
        Vec3 p0 = arc(i), p1 = arc(i + 1);
        Vec3 n = normalize(Vec3{0, p0.y + p1.y, (p0.z + p1.z) / 2 - 0.3f});
        b.face({-0.45f, p1.y, p1.z}, {0.45f, p1.y, p1.z}, {0.45f, p0.y, p0.z}, {-0.45f, p0.y, p0.z}, Tex::Wood, hex(0xD0B090), 1.5f, 0.2f, n);
        for (float x : {-0.3f, 0.3f}) {
            Vec3 o0 = p0 + (p0 - Vec3{0, 0, 0.3f}) * 0.04f, o1 = p1 + (p1 - Vec3{0, 0, 0.3f}) * 0.04f;
            b.face({x - 0.035f, o1.y, o1.z}, {x + 0.035f, o1.y, o1.z}, {x + 0.035f, o0.y, o0.z}, {x - 0.035f, o0.y, o0.z}, Tex::Iron, hex(0x8A8078), 0.2f, 0.2f, n);
        }
        for (float x : {-0.45f, 0.45f}) b.tri({x, 0, 0.3f}, {x, p0.y, p0.z}, {x, p1.y, p1.z}, Tex::Wood, hex(0xB09070), {x, 0, 0});
    }
    b.face({-0.45f, 0, 0}, {0.45f, 0, 0}, {0.45f, 0, 0.6f}, {-0.45f, 0, 0.6f}, Tex::Wood, hex(0x705038), 1.5f, 1, {0, -1, 0});
    b.box({-0.05f, -0.07f, 0.585f}, {0.05f, 0.05f, 0.62f}, Tex::Iron, hex(0xE8C070), 0.2f);
}

Model rat(Renderer& r) {
    Rigger g(r, Rig::Rat, 0.7f);
    g.m.size = 1.3f;
    Builder& b = g.b;
    const uint32_t pink = hex(0xD89088), tooth = hex(0xF0D080), muzzle = hex(0xC8B8A8);
    b.ball({0, 0.02f, -0.06f}, {0.2f, 0.17f, 0.3f}, 8, 6, Tex::Fur);
    b.ball({0, 0.05f, -0.2f}, {0.22f, 0.19f, 0.2f}, 8, 5, Tex::Fur);
    b.ball({0, 0, 0.14f}, {0.15f, 0.14f, 0.16f}, 8, 5, Tex::Fur);
    int body = g.part(-1, {0, 0.3f, 0}, Role::Root);

    b.ball({0, 0, 0.08f}, {0.11f, 0.1f, 0.13f}, 8, 5, Tex::Fur);
    b.limb({0, -0.01f, 0.14f}, {0, -0.035f, 0.32f}, 0.075f, 0.03f, 6, Tex::Fur, muzzle);
    b.ball({0, -0.035f, 0.335f}, {0.028f, 0.024f, 0.022f}, 6, 3, Tex::Flesh, pink);
    for (float s : {-1.0f, 1.0f}) b.box({s * 0.012f - 0.01f, -0.085f, 0.295f}, {s * 0.012f + 0.01f, -0.04f, 0.315f}, Tex::Bone, tooth);
    eyes(b, {0, 0.035f, frontZ({0, 0, 0.08f}, {0.11f, 0.1f, 0.13f}, 0.06f, 0.035f) + 0.004f}, 0.058f, 0.014f);
    for (float s : {-1.0f, 1.0f}) {
        b.xf = translate({s * 0.075f, 0.075f, 0.05f}) * rotateY(s * 0.5f) * rotateZ(-s * 0.35f);
        b.plate({{-0.045f, 0, 0.01f}, {0.045f, 0, 0.01f}, {0.055f, 0.05f, 0.01f}, {0.025f, 0.09f, 0.01f}, {-0.025f, 0.09f, 0.01f}, {-0.055f, 0.05f, 0.01f}}, Tex::Flesh, pink, pink, 0.2f);
        b.xf = {};
    }
    int head = g.part(body, {0, 0.05f, 0.26f}, Role::Head);

    b.limb({0, 0, 0}, {0, -0.005f, 0.165f}, 0.045f, 0.022f, 5, Tex::Fur, muzzle);
    b.box({-0.016f, 0, 0.14f}, {0.016f, 0.035f, 0.158f}, Tex::Bone, tooth);
    g.part(head, {0, -0.065f, 0.14f}, Role::Jaw);

    int prev = body;
    Vec3 at{0, 0.03f, -0.4f};
    for (int i = 0; i < 5; i++) {
        float r0 = 0.032f - float(i) * 0.005f;
        b.limb({0, 0, 0}, {0, 0, -0.17f}, r0, r0 - 0.005f, 4, Tex::Flesh, hex(0xC89890));
        prev = g.part(prev, at, Role::Tail, float(i) * 0.7f);
        at = {0, 0, -0.17f};
    }
    for (int i = 0; i < 4; i++) {
        float s = i % 2 ? 1.0f : -1.0f;
        bool front = i < 2;
        if (front) {
            b.limb({0, 0, 0}, {0, -0.21f, 0.03f}, 0.045f, 0.028f, 5, Tex::Fur);
            b.box({-0.03f, -0.25f, -0.01f}, {0.03f, -0.21f, 0.07f}, Tex::Flesh, pink);
        } else {
            b.ball({0, -0.02f, 0}, {0.07f, 0.1f, 0.11f}, 6, 4, Tex::Fur);
            b.limb({0, -0.04f, 0.02f}, {0, -0.24f, -0.04f}, 0.05f, 0.028f, 5, Tex::Fur);
            b.box({-0.03f, -0.27f, -0.08f}, {0.03f, -0.24f, 0.08f}, Tex::Flesh, pink);
        }
        g.part(body, {s * (front ? 0.11f : 0.15f), front ? -0.06f : -0.04f, front ? 0.16f : -0.22f}, Role::Leg, (i == 0 || i == 3) ? 0 : PI);
    }
    return g.m;
}

Model bat(Renderer& r) {
    Rigger g(r, Rig::Bat, 1.6f);
    Builder& b = g.b;
    const uint32_t fur = hex(0x8C7870), bone = hex(0x5A4440), skin = hex(0xC09090);
    b.ball({0, 0, 0}, {0.09f, 0.13f, 0.08f}, 8, 5, Tex::Fur, fur);
    b.ball({0, 0.16f, 0.03f}, {0.075f, 0.07f, 0.075f}, 8, 4, Tex::Fur, fur);
    b.limb({0, 0.15f, 0.08f}, {0, 0.135f, 0.14f}, 0.035f, 0.016f, 5, Tex::Flesh, skin);
    for (float s : {-1.0f, 1.0f}) {
        b.plate({{s * 0.075f, 0.18f, 0.008f}, {s * 0.02f, 0.215f, 0.008f}, {s * 0.11f, 0.35f, 0.0f}}, Tex::Membrane, skin, skin);
        b.limb({s * 0.016f, 0.12f, 0.122f}, {s * 0.014f, 0.08f, 0.124f}, 0.009f, 0.0f, 4, Tex::Bone, WHITE);
        b.limb({s * 0.04f, -0.11f, -0.01f}, {s * 0.045f, -0.21f, 0.0f}, 0.015f, 0.008f, 4, Tex::Bone, bone);
    }
    eyes(b, {0, 0.18f, frontZ({0, 0.16f, 0.03f}, {0.075f, 0.07f, 0.075f}, 0.035f, 0.18f) + 0.004f}, 0.035f, 0.017f);
    int body = g.part(-1, {0, 1.35f, 0}, Role::Root);

    for (float s : {-1.0f, 1.0f}) {
        Vec3 wrist{s * 0.32f, 0.07f, -0.02f}, trail{s * 0.28f, -0.14f, -0.03f};
        b.limb({0, 0.02f, 0}, wrist, 0.02f, 0.013f, 4, Tex::Bone, bone);
        b.tri({0, 0.03f, 0}, wrist, trail, Tex::Membrane, WHITE, {0, 0, 1});
        b.tri({0, 0.03f, 0}, trail, {0, -0.15f, 0}, Tex::Membrane, WHITE, {0, 0, 1});
        int arm = g.part(body, {s * 0.07f, 0.04f, 0}, wingRole(s));
        Vec3 f[3] = {{s * 0.4f, 0.1f, -0.01f}, {s * 0.36f, -0.14f, -0.02f}, {s * 0.13f, -0.27f, -0.03f}};
        for (Vec3 tip : f) b.limb({0, 0, 0}, tip, 0.012f, 0.005f, 4, Tex::Bone, bone);
        Vec3 m0 = (f[0] + f[1]) * 0.4f, m1 = (f[1] + f[2]) * 0.4f;
        Vec3 fan[6] = {f[0], m0, f[1], m1, f[2], trail - wrist};
        for (int i = 0; i < 5; i++) b.tri({0, 0, 0}, fan[i], fan[i + 1], Tex::Membrane, WHITE, {0, 0, 1});
        g.part(arm, wrist, s < 0 ? Role::WingTipL : Role::WingTipR);
    }
    return g.m;
}

Model skeleton(Renderer& r) {
    Rigger g(r, Rig::Humanoid, 1.85f);
    g.m.crumble = true;
    Builder& b = g.b;
    const uint32_t bone = hex(0xE0D8C8);
    b.ball({0, 0, 0}, {0.15f, 0.075f, 0.085f}, 8, 4, Tex::Bone, bone);
    int hips = g.part(-1, {0, 0.98f, 0}, Role::Root);

    b.limb({0, 0, -0.05f}, {0, 0.58f, -0.03f}, 0.028f, 0.022f, 4, Tex::Bone, bone);
    for (int i = 0; i < 5; i++) {
        float y = 0.25f + float(i) * 0.065f, rx = 0.115f + 0.04f * std::sin((float(i) + 0.5f) / 5 * PI);
        b.xf = translate({0, y, 0}) * scale({1, 1, 0.72f});
        b.prism({0, 0, 0}, rx, rx * 0.96f, 0.028f, 10, Tex::Bone, bone, 0.5f, false);
    }
    b.xf = {};
    b.box({-0.016f, 0.27f, 0.08f}, {0.016f, 0.52f, 0.1f}, Tex::Bone, bone);
    b.limb({-0.22f, 0.55f, 0}, {0.22f, 0.55f, 0}, 0.022f, 0.022f, 4, Tex::Bone, bone);
    int torso = g.part(hips, {0, 0.05f, 0}, Role::Torso);

    b.limb({0, 0, -0.01f}, {0, 0.08f, 0}, 0.022f, 0.02f, 4, Tex::Bone, bone);
    skull(b, {0, 0.17f, 0.01f}, 1, bone, Tex::Eye);
    int head = g.part(torso, {0, 0.58f, 0}, Role::Head);
    skullJaw(b, 1, bone);
    g.part(head, {0, 0.1f, 0.01f}, Role::Jaw);

    for (int s : {-1, 1}) {
        b.limb({0, 0, 0}, {0, -0.3f, 0}, 0.03f, 0.022f, 5, Tex::Bone, bone);
        b.ball({0, -0.3f, 0}, {0.034f, 0.034f, 0.034f}, 5, 3, Tex::Bone, bone);
        int arm = g.part(torso, {s * 0.22f, 0.54f, 0}, armRole(s));
        for (float x : {-0.012f, 0.012f}) b.limb({x, 0, 0}, {x, -0.26f, 0.01f}, 0.015f, 0.013f, 4, Tex::Bone, bone);
        b.box({-0.035f, -0.32f, -0.015f}, {0.035f, -0.26f, 0.025f}, Tex::Bone, bone);
        for (int f = 0; f < 3; f++) b.limb({(f - 1) * 0.022f, -0.32f, 0.01f}, {(f - 1) * 0.024f, -0.37f, 0.035f}, 0.008f, 0.005f, 4, Tex::Bone, bone);
        if (s > 0) {
            b.xf = translate({0, -0.3f, 0.01f}) * rotateZ(-0.55f) * rotateX(0.55f);
            weapon(b, ModelId::Sword, true);
        } else {
            b.xf = translate({-0.07f, -0.16f, 0.03f}) * alignY({-0.55f, 0.1f, 0.83f});
            b.prism({0, -0.015f, 0}, 0.21f, 0.21f, 0.03f, 10, Tex::Wood, hex(0xA06048), 0.5f);
            b.prism({0, -0.02f, 0}, 0.22f, 0.22f, 0.045f, 10, Tex::Rust, WHITE, 0.4f, false);
            b.prism({0, 0.015f, 0}, 0.06f, 0.03f, 0.04f, 6, Tex::Rust, WHITE, 0.2f);
        }
        b.xf = {};
        g.part(arm, {0, -0.3f, 0}, foreRole(s));
    }
    for (int s : {-1, 1}) {
        b.limb({0, 0, 0}, {0, -0.46f, 0}, 0.032f, 0.024f, 5, Tex::Bone, bone);
        b.ball({0, -0.46f, 0}, {0.04f, 0.04f, 0.04f}, 5, 3, Tex::Bone, bone);
        int thigh = g.part(hips, {s * 0.09f, -0.02f, 0}, legRole(s));
        b.limb({0, 0, 0}, {0, -0.43f, 0}, 0.026f, 0.02f, 5, Tex::Bone, bone);
        b.box({-0.04f, -0.48f, -0.03f}, {0.04f, -0.43f, 0.1f}, Tex::Bone, bone);
        g.part(thigh, {0, -0.46f, 0}, shinRole(s));
    }
    return g.m;
}

Model ooze(Renderer& r) {
    Rigger g(r, Rig::Slime, 0.8f);
    g.m.blend = Blend::Alpha, g.m.alpha = 0.7f;
    Builder& b = g.b;
    const uint32_t bone = hex(0xC8C8A0);
    skull(b, {0, 0, 0}, 1.15f, bone, Tex::Eye);
    b.xf = translate(Vec3{0, -0.07f, 0} * 1.15f) * rotateX(0.35f);
    skullJaw(b, 1.15f, bone);
    b.xf = {};
    b.limb({-0.24f, -0.14f, -0.08f}, {0.2f, -0.04f, -0.16f}, 0.028f, 0.022f, 5, Tex::Bone, bone);
    for (float x : {-0.25f, 0.21f}) b.ball({x, x < 0 ? -0.14f : -0.04f, x < 0 ? -0.08f : -0.16f}, {0.04f, 0.04f, 0.04f}, 5, 3, Tex::Bone, bone);
    int head = g.part(-1, {0, 0.34f, 0.04f}, Role::Head);
    g.m.parts[head].opaque = true;
    b.ball({0, 0.36f, 0}, {0.52f, 0.4f, 0.5f}, 10, 6, Tex::Slime, WHITE, 0.6f, 0.22f);
    b.prism({0, 0, 0}, 0.62f, 0.5f, 0.07f, 10, Tex::Slime, WHITE, 0.6f);
    for (int i = 0; i < 5; i++) {
        float a = float(i) * 1.3f;
        b.ball({std::cos(a) * 0.56f, 0.04f, std::sin(a) * 0.56f}, {0.1f, 0.07f, 0.1f}, 6, 3, Tex::Slime);
    }
    g.part(-1, {0, 0, 0}, Role::Root);
    return g.m;
}

Model ghoul(Renderer& r) {
    Rigger g(r, Rig::Humanoid, 1.6f);
    g.m.strike = Strike::Claw, g.m.hunch = 0.75f, g.m.crouch = 0.5f, g.m.legs = 0.86f;
    Builder& b = g.b;
    const uint32_t claw = hex(0xC8B890), rag = hex(0x6A5A48);
    b.ball({0, 0, 0}, {0.14f, 0.09f, 0.1f}, 8, 4, Tex::Ghoul);
    b.xf = translate({0, 0, 0.09f}) * rotateX(-0.1f);
    b.plate({{-0.12f, 0.02f, 0.01f}, {0.12f, 0.02f, 0.01f}, {0.09f, -0.24f, 0}, {0.01f, -0.3f, 0}, {-0.08f, -0.22f, 0}}, Tex::Cloth, rag, rag, 0.4f);
    b.xf = {};
    int hips = g.part(-1, {0, 0.92f, 0}, Role::Root);

    b.xf = scale({1, 1, 0.75f});
    b.prism({0, 0, 0}, 0.12f, 0.19f, 0.46f, 8, Tex::Ghoul);
    b.xf = {};
    b.ball({0, 0.42f, -0.05f}, {0.21f, 0.12f, 0.13f}, 8, 4, Tex::Ghoul);
    for (int i = 0; i < 5; i++) b.box({-0.02f, 0.08f + float(i) * 0.07f, -0.165f}, {0.02f, 0.11f + float(i) * 0.07f, -0.12f}, Tex::Ghoul);
    for (int i = 0; i < 3; i++) {
        float y = 0.2f + float(i) * 0.07f, w = 0.12f - float(i) * 0.005f;
        b.box({-w, y, 0.09f}, {w, y + 0.02f, 0.135f}, Tex::Ghoul, hex(0xD8E0D0));
    }
    int torso = g.part(hips, {0, 0.06f, 0}, Role::Torso);

    Vec3 c{0, 0.16f, 0.03f}, cr{0.1f, 0.12f, 0.11f};
    b.limb({0, -0.04f, -0.03f}, {0, 0.1f, 0.02f}, 0.05f, 0.045f, 6, Tex::Ghoul);
    b.ball(c, cr, 8, 5, Tex::Ghoul);
    b.box({-0.07f, 0.075f, 0.05f}, {0.07f, 0.13f, 0.15f}, Tex::Ghoul);
    b.box({-0.088f, 0.17f, 0.08f}, {0.088f, 0.2f, 0.14f}, Tex::Ghoul);
    for (float s : {-1.0f, 1.0f}) {
        b.box({s * 0.044f - 0.026f, 0.13f, 0.1f}, {s * 0.044f + 0.026f, 0.168f, frontZ(c, cr, 0.04f, 0.15f) + 0.006f}, Tex::White, BLACK);
        b.plate({{s * 0.09f, 0.14f, 0.008f}, {s * 0.09f, 0.2f, 0.008f}, {s * 0.2f, 0.24f, 0}}, Tex::Ghoul);
    }
    eyes(b, {0, 0.15f, frontZ(c, cr, 0.044f, 0.15f) + 0.008f}, 0.044f, 0.013f);
    teeth(b, -0.06f, 0.06f, 0.08f, 0.14f, 6, 0.035f);
    int head = g.part(torso, {0, 0.43f, 0.1f}, Role::Head);
    b.box({-0.066f, -0.05f, 0}, {0.066f, -0.006f, 0.11f}, Tex::Ghoul);
    teeth(b, -0.055f, 0.055f, -0.006f, 0.1f, 5, -0.03f);
    g.part(head, {0, 0.08f, 0.04f}, Role::Jaw);

    for (int s : {-1, 1}) {
        b.ball({0, 0, 0}, {0.07f, 0.06f, 0.07f}, 6, 3, Tex::Ghoul);
        b.limb({0, 0, 0}, {0, -0.36f, 0}, 0.045f, 0.035f, 6, Tex::Ghoul);
        b.ball({0, -0.36f, 0}, {0.042f, 0.042f, 0.042f}, 5, 3, Tex::Ghoul);
        int arm = g.part(torso, {s * 0.21f, 0.42f, -0.02f}, armRole(s));
        b.limb({0, 0, 0}, {0, -0.33f, 0}, 0.036f, 0.028f, 6, Tex::Ghoul);
        b.box({-0.035f, -0.4f, -0.03f}, {0.035f, -0.32f, 0.035f}, Tex::Ghoul);
        claws(b, {0, -0.39f, 0}, 0.19f, 4, 0.02f, claw);
        g.part(arm, {0, -0.36f, 0}, foreRole(s));
    }
    for (int s : {-1, 1}) {
        b.limb({0, 0, 0}, {0, -0.43f, 0}, 0.06f, 0.04f, 6, Tex::Ghoul);
        b.ball({0, -0.43f, 0}, {0.045f, 0.045f, 0.045f}, 5, 3, Tex::Ghoul);
        int thigh = g.part(hips, {s * 0.1f, -0.03f, 0}, legRole(s));
        b.limb({0, 0, 0}, {0, -0.41f, 0}, 0.04f, 0.03f, 6, Tex::Ghoul);
        b.box({-0.045f, -0.46f, -0.04f}, {0.045f, -0.41f, 0.1f}, Tex::Ghoul);
        for (int i = 0; i < 3; i++) b.limb({(i - 1) * 0.025f, -0.44f, 0.09f}, {(i - 1) * 0.03f, -0.46f, 0.15f}, 0.01f, 0, 4, Tex::Bone, claw);
        g.part(thigh, {0, -0.43f, 0}, shinRole(s));
    }
    return g.m;
}

Model spider(Renderer& r) {
    Rigger g(r, Rig::Spider, 0.95f);
    g.m.size = 1.1f;
    Builder& b = g.b;
    const uint32_t fang = hex(0xD8D0B8), mark = hex(0xC02818), band = hex(0xA89880);
    Vec3 c{0, 0, 0.1f}, cr{0.2f, 0.13f, 0.22f};
    b.ball(c, cr, 8, 5, Tex::Chitin);
    eyes(b, {0, 0.05f, frontZ(c, cr, 0.05f, 0.05f) + 0.004f}, 0.05f, 0.028f);
    eyes(b, {0, 0.095f, frontZ(c, cr, 0.03f, 0.095f) + 0.004f}, 0.03f, 0.016f);
    eyes(b, {0, 0.03f, frontZ(c, cr, 0.1f, 0.03f) + 0.004f}, 0.1f, 0.016f);
    for (float s : {-1.0f, 1.0f}) b.limb({s * 0.1f, -0.03f, 0.28f}, {s * 0.13f, -0.1f, 0.42f}, 0.025f, 0.015f, 5, Tex::Chitin);
    int body = g.part(-1, {0, 0.42f, 0}, Role::Root);

    b.ball({0, 0.1f, -0.3f}, {0.3f, 0.26f, 0.36f}, 10, 6, Tex::Chitin, hex(0xC8B8D0));
    b.ball({0, 0.35f, -0.2f}, {0.06f, 0.02f, 0.07f}, 6, 3, Tex::Flesh, mark);
    b.ball({0, 0.33f, -0.38f}, {0.08f, 0.025f, 0.09f}, 6, 3, Tex::Flesh, mark);
    for (int i = 0; i < 10; i++) {
        float a = float(i) * 2.3f, e = 0.3f + 0.25f * float(i % 3);
        Vec3 d{std::cos(a) * std::cos(e), std::sin(e), std::sin(a) * std::cos(e) * 1.2f};
        Vec3 at = Vec3{0, 0.1f, -0.3f} + d * Vec3{0.3f, 0.26f, 0.36f};
        b.limb(at, at + d * 0.07f, 0.008f, 0, 3, Tex::Chitin);
    }
    g.part(body, {0, 0.04f, -0.08f}, Role::Tail);

    for (float s : {-1.0f, 1.0f}) {
        b.limb({s * 0.045f, 0, 0}, {s * 0.04f, -0.08f, 0.05f}, 0.04f, 0.03f, 5, Tex::Chitin);
        b.limb({s * 0.04f, -0.08f, 0.05f}, {s * 0.02f, -0.18f, 0.02f}, 0.022f, 0, 4, Tex::Bone, fang);
    }
    g.part(body, {0, -0.01f, 0.29f}, Role::Jaw);

    const float spread[4] = {0.95f, 0.3f, -0.25f, -0.85f}, zs[4] = {0.22f, 0.13f, 0.04f, -0.05f};
    for (int i = 0; i < 8; i++) {
        int s = i < 4 ? -1 : 1, k = i % 4;
        float phase = float((k + (s > 0)) % 2) * PI;
        b.limb({0, 0, 0}, {0.32f, 0.3f, 0}, 0.038f, 0.03f, 5, Tex::Chitin);
        b.ball({0.32f, 0.3f, 0}, {0.045f, 0.045f, 0.045f}, 5, 3, Tex::Bone, band);
        int femur = g.part(body, {s * 0.15f, 0, zs[k]}, Role::Leg, phase);
        g.m.parts[femur].yaw = s > 0 ? -spread[k] : PI + spread[k];
        b.limb({0, 0, 0}, {0.27f, -0.48f, 0}, 0.032f, 0.022f, 5, Tex::Chitin);
        b.limb({0.07f, -0.124f, 0}, {0.1f, -0.178f, 0}, 0.036f, 0.034f, 5, Tex::Bone, band);
        b.ball({0.27f, -0.48f, 0}, {0.026f, 0.026f, 0.026f}, 5, 3, Tex::Bone, band);
        b.limb({0.27f, -0.48f, 0}, {0.36f, -0.72f, 0}, 0.022f, 0.006f, 4, Tex::Chitin);
        g.part(femur, {0.32f, 0.3f, 0}, Role::LegTip, phase);
    }
    return g.m;
}

Model cultist(Renderer& r) {
    Rigger g(r, Rig::Humanoid, 1.85f);
    g.m.strike = Strike::Cast;
    Builder& b = g.b;
    const uint32_t robe = hex(0xA82C24), dark = hex(0x2A1414), skin = hex(0xC0A898), rope = hex(0xC0A070);
    b.prism({0, -0.93f, 0}, 0.36f, 0.2f, 0.95f, 10, Tex::Robe, robe, 0.6f, false);
    b.prism({0, -0.03f, 0}, 0.212f, 0.212f, 0.05f, 10, Tex::Cloth, rope, 0.3f, false);
    b.limb({0.1f, -0.03f, 0.18f}, {0.14f, -0.4f, 0.24f}, 0.015f, 0.012f, 4, Tex::Cloth, rope);
    for (float s : {-1.0f, 1.0f}) b.box({s * 0.09f - 0.045f, -0.95f, 0.18f}, {s * 0.09f + 0.045f, -0.89f, 0.33f}, Tex::Leather, hex(0x605048));
    int hips = g.part(-1, {0, 0.95f, 0}, Role::Root);

    b.xf = scale({1, 1, 0.78f});
    b.prism({0, 0, 0}, 0.2f, 0.23f, 0.5f, 10, Tex::Robe, robe, 0.6f);
    b.xf = {};
    b.ball({0, 0.47f, 0}, {0.26f, 0.09f, 0.17f}, 8, 4, Tex::Robe, robe);
    for (float s : {-1.0f, 1.0f}) b.box({s * 0.075f - 0.03f, 0, 0.165f}, {s * 0.075f + 0.03f, 0.48f, 0.19f}, Tex::Cloth, dark);
    b.ball({0, 0.32f, 0.2f}, {0.035f, 0.045f, 0.02f}, 6, 3, Tex::Gold);
    int torso = g.part(hips, {0, 0.05f, 0}, Role::Torso);

    b.ball({0, 0.14f, -0.05f}, {0.14f, 0.16f, 0.15f}, 8, 5, Tex::Robe, robe);
    b.limb({0, 0.24f, -0.1f}, {0, 0.36f, -0.22f}, 0.07f, 0.0f, 6, Tex::Robe, robe);
    hoodOpening(b, {0, 0.12f, 0.13f}, 0.095f, 0.115f, Tex::Robe, robe, Tex::Eye);
    g.part(torso, {0, 0.52f, 0.02f}, Role::Head);

    for (int s : {-1, 1}) {
        b.limb({0, 0.02f, 0}, {0, -0.3f, 0}, 0.075f, 0.08f, 6, Tex::Robe, robe);
        int arm = g.part(torso, {s * 0.26f, 0.45f, 0}, armRole(s));
        b.prism({0, -0.3f, 0}, 0.12f, 0.08f, 0.31f, 8, Tex::Robe, robe, 0.6f, false);
        b.ball({0, -0.33f, 0.01f}, {0.04f, 0.055f, 0.045f}, 6, 3, Tex::Flesh, skin);
        if (s > 0) {
            b.xf = translate({0, -0.33f, 0.01f});
            b.limb({0, -0.8f, 0}, {0, 0.95f, 0}, 0.022f, 0.02f, 5, Tex::Wood, hex(0x7A5A40), 0.4f);
            for (float x : {-1.0f, 1.0f}) b.limb({0, 0.92f, 0}, {x * 0.075f, 1.1f, 0}, 0.018f, 0.006f, 4, Tex::Iron, hex(0x9A9088));
            b.ball({0, 1.03f, 0}, {0.05f, 0.06f, 0.05f}, 6, 4, Tex::Orb);
            b.xf = {};
        }
        g.part(arm, {0, -0.3f, 0}, foreRole(s));
    }
    return g.m;
}

Model wraith(Renderer& r) {
    Rigger g(r, Rig::Wraith, 1.9f);
    g.m.blend = Blend::Alpha, g.m.alpha = 0.75f, g.m.hover = 0.15f;
    Builder& b = g.b;
    const uint32_t shroud = hex(0xB8C0E0), dark = hex(0x6A7498), bone = hex(0xD8D8E8);
    b.xf = scale({1, 1, 0.8f});
    b.prism({0, 0, 0}, 0.2f, 0.25f, 0.48f, 8, Tex::Robe, shroud, 0.6f);
    b.prism({0, -0.4f, 0}, 0.3f, 0.2f, 0.4f, 8, Tex::Robe, shroud, 0.6f, false);
    b.xf = {};
    b.ball({0, 0.46f, 0}, {0.28f, 0.09f, 0.17f}, 8, 4, Tex::Robe, shroud);
    int root = g.part(-1, {0, 0.95f, 0}, Role::Root);
    for (int i = 0; i < 9; i++) {
        float a = 2 * PI * float(i) / 9, len = 0.42f + 0.28f * hashf(i, 7), w = 0.11f;
        b.plate({{-w, 0, 0.005f}, {w, 0, 0.005f}, {w * 0.7f, -len * 0.7f, 0}, {w * 0.2f, -len, 0}, {-w * 0.3f, -len * 0.8f, 0}, {-w * 0.8f, -len * 0.55f, 0}},
                Tex::Robe, shroud, dark, 0.6f);
        int p = g.part(root, {std::sin(a) * 0.27f, -0.38f, std::cos(a) * 0.22f}, Role::Cloth, float(i) * 1.7f);
        g.m.parts[p].yaw = a;
    }

    b.ball({0, 0.13f, -0.06f}, {0.15f, 0.17f, 0.15f}, 8, 5, Tex::Robe, dark);
    b.limb({0, 0.22f, -0.12f}, {0, 0.3f, -0.27f}, 0.07f, 0.0f, 6, Tex::Robe, dark);
    hoodOpening(b, {0, 0.11f, 0.12f}, 0.1f, 0.12f, Tex::Robe, dark, Tex::Orb);
    g.part(root, {0, 0.5f, 0.03f}, Role::Head);

    for (int s : {-1, 1}) {
        b.limb({0, 0.02f, 0}, {0, -0.34f, 0}, 0.07f, 0.085f, 6, Tex::Robe, shroud);
        int arm = g.part(root, {s * 0.27f, 0.42f, 0}, armRole(s));
        b.prism({0, -0.24f, 0}, 0.11f, 0.085f, 0.25f, 8, Tex::Robe, shroud, 0.6f, false);
        b.box({-0.035f, -0.3f, -0.02f}, {0.035f, -0.24f, 0.02f}, Tex::Bone, bone);
        for (int f = 0; f < 4; f++) {
            float x = (float(f) - 1.5f) * 0.022f;
            b.limb({x, -0.29f, 0.01f}, {x * 1.6f, -0.53f, 0.09f}, 0.011f, 0.003f, 4, Tex::Bone, bone);
        }
        g.part(arm, {0, -0.34f, 0}, foreRole(s));
    }
    return g.m;
}

Model knight(Renderer& r) {
    Rigger g(r, Rig::Humanoid, 2.05f);
    g.m.strike = Strike::Smash, g.m.crumble = true, g.m.legs = 0.95f;
    Builder& b = g.b;
    const uint32_t steel = hex(0x9AA0AC), dark = hex(0x6A707C), plume = hex(0x9A2018);
    b.prism({0, -0.33f, 0}, 0.27f, 0.21f, 0.35f, 10, Tex::Chain, hex(0xA8ACB4), 0.4f, false);
    b.prism({0, -0.04f, 0}, 0.215f, 0.215f, 0.07f, 10, Tex::Leather, WHITE, 0.3f, false);
    b.ball({0, -0.005f, 0.215f}, {0.04f, 0.035f, 0.015f}, 6, 3, Tex::Gold);
    for (float s : {-1.0f, 1.0f}) {
        b.xf = translate({s * 0.11f, -0.05f, 0.2f}) * rotateX(-0.12f);
        b.plate({{-0.1f, 0, 0.012f}, {0.1f, 0, 0.012f}, {0.09f, -0.24f, 0.008f}, {-0.09f, -0.24f, 0.008f}}, Tex::Steel, dark, steel);
    }
    b.xf = {};
    int hips = g.part(-1, {0, 1.0f, 0}, Role::Root);

    b.xf = scale({1, 1, 0.72f});
    b.prism({0, 0, 0}, 0.2f, 0.28f, 0.48f, 8, Tex::Steel, steel, 0.5f);
    b.xf = {};
    b.limb({0, 0.04f, 0.135f}, {0, 0.44f, 0.185f}, 0.02f, 0.02f, 4, Tex::Steel, hex(0xD8DCE4));
    b.prism({0, 0.47f, 0}, 0.15f, 0.13f, 0.08f, 8, Tex::Steel, dark);
    int torso = g.part(hips, {0, 0.06f, 0}, Role::Torso);

    b.prism({0, 0, 0}, 0.135f, 0.125f, 0.3f, 8, Tex::Steel, steel);
    b.ball({0, 0.3f, 0}, {0.125f, 0.06f, 0.125f}, 8, 3, Tex::Steel, steel);
    b.box({-0.105f, 0.165f, 0.09f}, {0.105f, 0.2f, 0.135f}, Tex::White, BLACK);
    eyes(b, {0, 0.182f, 0.137f}, 0.045f, 0.017f, Tex::Orb);
    for (int i = 0; i < 6; i++) {
        float x = (float(i) - 2.5f) * 0.026f;
        b.box({x - 0.006f, 0.07f, 0.112f}, {x + 0.006f, 0.12f, 0.128f}, Tex::White, BLACK);
    }
    b.box({-0.018f, 0.28f, -0.13f}, {0.018f, 0.43f, 0.09f}, Tex::Cloth, plume);
    b.xf = translate({0, 0.36f, -0.13f}) * rotateX(-0.3f);
    b.plate({{-0.02f, 0.02f, 0.015f}, {0.02f, 0.02f, 0.015f}, {0.015f, -0.4f, 0}, {-0.015f, -0.38f, 0}}, Tex::Cloth, plume, plume);
    b.xf = {};
    g.part(torso, {0, 0.53f, 0}, Role::Head);

    for (int s : {-1, 1}) {
        b.ball({s * 0.03f, 0, 0}, {0.17f, 0.13f, 0.17f}, 8, 4, Tex::Steel, steel);
        b.prism({s * 0.03f, -0.13f, 0}, 0.17f, 0.165f, 0.06f, 8, Tex::Steel, dark, 0.5f, false);
        b.limb({0, 0, 0}, {0, -0.32f, 0}, 0.07f, 0.065f, 6, Tex::Chain, WHITE, 0.4f);
        b.ball({0, -0.32f, 0}, {0.075f, 0.075f, 0.075f}, 6, 3, Tex::Steel, steel);
        int arm = g.part(torso, {s * 0.32f, 0.43f, 0}, armRole(s));
        b.prism({0, -0.3f, 0}, 0.07f, 0.075f, 0.3f, 6, Tex::Steel, steel);
        b.prism({0, -0.3f, 0}, 0.085f, 0.075f, 0.05f, 6, Tex::Steel, dark, 0.5f, false);
        b.box({-0.055f, -0.42f, -0.05f}, {0.055f, -0.3f, 0.06f}, Tex::Steel, dark);
        if (s > 0) {
            b.xf = translate({0, -0.37f, 0.01f}) * rotateZ(-0.4f) * rotateX(0.85f);
            weapon(b, ModelId::Greatsword);
            b.xf = {};
        }
        g.part(arm, {0, -0.32f, 0}, foreRole(s));
    }
    for (int s : {-1, 1}) {
        b.limb({0, 0, 0}, {0, -0.46f, 0}, 0.085f, 0.07f, 6, Tex::Steel, steel);
        b.ball({0, -0.46f, 0.01f}, {0.08f, 0.08f, 0.085f}, 6, 3, Tex::Steel, dark);
        int thigh = g.part(hips, {s * 0.11f, -0.03f, 0}, legRole(s));
        b.prism({0, -0.42f, 0}, 0.06f, 0.075f, 0.42f, 6, Tex::Steel, steel);
        b.box({-0.065f, -0.5f, -0.06f}, {0.065f, -0.42f, 0.12f}, Tex::Steel, dark);
        b.limb({0, -0.465f, 0.12f}, {0, -0.49f, 0.19f}, 0.04f, 0.0f, 4, Tex::Steel, dark);
        g.part(thigh, {0, -0.46f, 0}, shinRole(s));
    }
    return g.m;
}

Model ogre(Renderer& r) {
    Rigger g(r, Rig::Humanoid, 2.3f);
    g.m.strike = Strike::Smash, g.m.hunch = 0.18f, g.m.legs = 0.9f;
    Builder& b = g.b;
    const uint32_t tusk = hex(0xF0E4C8), pelt = hex(0xB09080);
    b.ball({0, 0.04f, 0.02f}, {0.36f, 0.24f, 0.3f}, 10, 5, Tex::Hide);
    b.xf = scale({1, 1, 0.84f});
    b.prism({0, -0.05f, 0}, 0.375f, 0.375f, 0.1f, 10, Tex::Leather, WHITE, 0.4f, false);
    b.xf = {};
    b.ball({0, 0, 0.315f}, {0.06f, 0.06f, 0.03f}, 6, 3, Tex::Bone);
    b.xf = translate({0, -0.05f, 0.28f}) * rotateX(0.08f);
    b.plate({{-0.17f, 0, 0.012f}, {0.17f, 0, 0.012f}, {0.14f, -0.36f, 0}, {0.02f, -0.42f, 0}, {-0.15f, -0.34f, 0}}, Tex::Fur, pelt, pelt, 0.4f);
    b.xf = {};
    int hips = g.part(-1, {0, 0.98f, 0}, Role::Root);

    b.ball({0, 0.22f, 0.08f}, {0.42f, 0.36f, 0.38f}, 10, 6, Tex::Hide);
    b.ball({0, 0.6f, -0.02f}, {0.5f, 0.3f, 0.32f}, 10, 5, Tex::Hide);
    b.ball({0, 0.76f, -0.1f}, {0.38f, 0.17f, 0.25f}, 8, 4, Tex::Hide);
    b.ball({-0.22f, 0.8f, -0.04f}, {0.28f, 0.12f, 0.28f}, 8, 4, Tex::Fur, pelt, 0.5f, 0.3f);
    int torso = g.part(hips, {0, 0.08f, 0}, Role::Torso);

    Vec3 c{0, 0.13f, 0.03f}, cr{0.16f, 0.16f, 0.16f};
    b.ball(c, cr, 8, 5, Tex::Hide);
    b.box({-0.15f, 0.16f, 0.1f}, {0.15f, 0.21f, 0.19f}, Tex::Hide);
    b.ball({0, 0.1f, 0.19f}, {0.05f, 0.045f, 0.04f}, 6, 3, Tex::Hide);
    eyes(b, {0, 0.145f, frontZ(c, cr, 0.07f, 0.145f) + 0.006f}, 0.07f, 0.018f);
    for (float s : {-1.0f, 1.0f}) b.plate({{s * 0.15f, 0.1f, 0.01f}, {s * 0.15f, 0.18f, 0.01f}, {s * 0.24f, 0.16f, 0}}, Tex::Hide);
    int head = g.part(torso, {0, 0.84f, 0.16f}, Role::Head);
    b.box({-0.14f, -0.08f, 0}, {0.14f, 0, 0.21f}, Tex::Hide);
    for (float s : {-1.0f, 1.0f}) b.limb({s * 0.1f, -0.02f, 0.18f}, {s * 0.13f, 0.13f, 0.23f}, 0.03f, 0.0f, 5, Tex::Bone, tusk);
    teeth(b, -0.06f, 0.06f, 0, 0.19f, 4, -0.025f, tusk);
    g.part(head, {0, 0.05f, 0.02f}, Role::Jaw);

    for (int s : {-1, 1}) {
        b.ball({0, -0.04f, 0}, {0.17f, 0.16f, 0.17f}, 8, 4, Tex::Hide);
        b.limb({0, 0, 0}, {0, -0.44f, 0}, 0.14f, 0.11f, 8, Tex::Hide);
        int arm = g.part(torso, {s * 0.52f, 0.66f, 0}, armRole(s));
        b.limb({0, 0, 0}, {0, -0.4f, 0}, 0.115f, 0.125f, 8, Tex::Hide);
        b.prism({0, -0.34f, 0}, 0.13f, 0.13f, 0.1f, 8, Tex::Leather, WHITE, 0.3f, false);
        b.ball({0, -0.48f, 0.01f}, {0.12f, 0.11f, 0.12f}, 8, 4, Tex::Hide);
        if (s > 0) {
            b.xf = translate({0, -0.48f, 0.01f}) * rotateX(PI / 2 - 0.5f);
            club(b);
            b.xf = {};
        }
        g.part(arm, {0, -0.44f, 0}, foreRole(s));
    }
    for (int s : {-1, 1}) {
        b.limb({0, 0, 0}, {0, -0.44f, 0}, 0.16f, 0.12f, 8, Tex::Hide);
        int thigh = g.part(hips, {s * 0.2f, -0.06f, 0}, legRole(s));
        b.limb({0, 0, 0}, {0, -0.38f, 0}, 0.12f, 0.1f, 8, Tex::Hide);
        b.box({-0.1f, -0.48f, -0.08f}, {0.1f, -0.38f, 0.17f}, Tex::Hide);
        g.part(thigh, {0, -0.44f, 0}, shinRole(s));
    }
    return g.m;
}

Model imp(Renderer& r) {
    Rigger g(r, Rig::Humanoid, 1.1f);
    g.m.strike = Strike::Cast, g.m.hover = 0.3f, g.m.legs = 0.38f;
    Builder& b = g.b;
    const uint32_t horn = hex(0x4A3020), wing = hex(0xC05038);
    b.ball({0, 0, 0}, {0.1f, 0.07f, 0.08f}, 6, 4, Tex::ImpSkin);
    int hips = g.part(-1, {0, 0.42f, 0}, Role::Root);
    b.ball({0, 0.15f, 0}, {0.12f, 0.17f, 0.1f}, 8, 5, Tex::ImpSkin);
    int torso = g.part(hips, {0, 0.03f, 0}, Role::Torso);

    Vec3 c{0, 0.11f, 0.02f}, cr{0.12f, 0.11f, 0.11f};
    b.ball(c, cr, 8, 5, Tex::ImpSkin);
    for (float s : {-1.0f, 1.0f}) {
        b.limb({s * 0.06f, 0.19f, 0}, {s * 0.12f, 0.29f, -0.04f}, 0.032f, 0.02f, 5, Tex::Bone, horn);
        b.limb({s * 0.12f, 0.29f, -0.04f}, {s * 0.09f, 0.38f, -0.12f}, 0.02f, 0.0f, 5, Tex::Bone, horn);
        b.plate({{s * 0.1f, 0.09f, 0.01f}, {s * 0.1f, 0.15f, 0.01f}, {s * 0.23f, 0.18f, 0}}, Tex::ImpSkin);
    }
    eyes(b, {0, 0.135f, frontZ(c, cr, 0.045f, 0.135f) + 0.006f}, 0.045f, 0.026f);
    float mouthZ = frontZ(c, cr, 0, 0.06f);
    b.box({-0.06f, 0.05f, mouthZ - 0.02f}, {0.06f, 0.075f, mouthZ + 0.006f}, Tex::White, BLACK);
    teeth(b, -0.055f, 0.055f, 0.075f, mouthZ, 6, 0.016f);
    g.part(torso, {0, 0.32f, 0.02f}, Role::Head);

    for (int s : {-1, 1}) {
        Vec3 wrist{s * 0.3f, 0.2f, -0.06f}, tips[3] = {{s * 0.38f, -0.02f, -0.08f}, {s * 0.22f, -0.1f, -0.05f}, {s * 0.05f, -0.08f, 0}};
        b.limb({0, 0, 0}, wrist, 0.018f, 0.012f, 4, Tex::Bone, horn);
        b.limb(wrist, {s * 0.38f, 0.28f, -0.08f}, 0.012f, 0, 4, Tex::Bone, horn);
        b.tri({0, 0, 0}, wrist, tips[0], Tex::Membrane, wing, {0, 0, 1});
        b.tri({0, 0, 0}, tips[0], tips[1], Tex::Membrane, wing, {0, 0, 1});
        b.tri({0, 0, 0}, tips[1], tips[2], Tex::Membrane, wing, {0, 0, 1});
        g.part(torso, {s * 0.05f, 0.24f, -0.08f}, wingRole(s));
    }
    for (int s : {-1, 1}) {
        b.limb({0, 0, 0}, {0, -0.16f, 0}, 0.032f, 0.026f, 5, Tex::ImpSkin);
        int arm = g.part(torso, {s * 0.13f, 0.26f, 0}, armRole(s));
        b.limb({0, 0, 0}, {0, -0.15f, 0}, 0.026f, 0.022f, 5, Tex::ImpSkin);
        claws(b, {0, -0.16f, 0}, 0.06f, 3, 0.014f, horn);
        if (s > 0) b.ball({0, -0.22f, 0.04f}, {0.06f, 0.06f, 0.06f}, 6, 4, Tex::Eye);
        g.part(arm, {0, -0.16f, 0}, foreRole(s));
    }
    for (int s : {-1, 1}) {
        b.limb({0, 0, 0}, {0, -0.18f, 0.05f}, 0.04f, 0.03f, 5, Tex::ImpSkin);
        int thigh = g.part(hips, {s * 0.06f, -0.02f, 0}, legRole(s));
        b.limb({0, 0, 0}, {0, -0.17f, -0.05f}, 0.03f, 0.02f, 5, Tex::ImpSkin);
        b.box({-0.025f, -0.21f, -0.08f}, {0.025f, -0.17f, 0.02f}, Tex::Bone, horn);
        g.part(thigh, {0, -0.18f, 0.05f}, shinRole(s));
    }
    int prev = hips;
    Vec3 at{0, -0.02f, -0.07f};
    for (int i = 0; i < 4; i++) {
        float r0 = 0.024f - float(i) * 0.004f;
        b.limb({0, 0, 0}, {0, -0.05f, -0.12f}, r0, r0 - 0.004f, 4, Tex::ImpSkin);
        if (i == 3) {
            b.xf = translate({0, -0.05f, -0.12f}) * rotateX(-0.4f);
            b.plate({{0, 0.0f, 0.01f}, {-0.05f, -0.05f, 0}, {0, -0.12f, 0}, {0.05f, -0.05f, 0}}, Tex::ImpSkin);
            b.xf = {};
        }
        prev = g.part(prev, at, Role::Tail, float(i) * 0.8f);
        at = {0, -0.05f, -0.12f};
    }
    return g.m;
}

Model gargoyle(Renderer& r) {
    Rigger g(r, Rig::Humanoid, 1.7f);
    g.m.strike = Strike::Claw, g.m.hunch = 0.45f, g.m.crouch = 1, g.m.lively = 0, g.m.crumble = true, g.m.legs = 0.84f;
    Builder& b = g.b;
    const uint32_t stone = hex(0xA8AEA4), dark = hex(0x70766E), talon = hex(0x606660);
    b.ball({0, 0, 0}, {0.17f, 0.12f, 0.13f}, 8, 4, Tex::Stone, stone);
    int hips = g.part(-1, {0, 0.95f, 0}, Role::Root);

    b.ball({0, 0.3f, 0.02f}, {0.25f, 0.28f, 0.2f}, 8, 5, Tex::Stone, stone, 0.5f, 0.1f);
    b.ball({0, 0.44f, -0.06f}, {0.3f, 0.12f, 0.17f}, 8, 4, Tex::Stone, stone);
    int torso = g.part(hips, {0, 0.06f, 0}, Role::Torso);

    Vec3 c{0, 0.1f, 0.04f}, cr{0.13f, 0.12f, 0.14f};
    b.ball(c, cr, 8, 5, Tex::Stone, stone);
    b.limb({0, 0.06f, 0.12f}, {0, 0.03f, 0.27f}, 0.075f, 0.035f, 6, Tex::Stone, stone);
    b.box({-0.11f, 0.13f, 0.1f}, {0.11f, 0.17f, 0.18f}, Tex::Stone, dark);
    for (float s : {-1.0f, 1.0f}) {
        b.limb({s * 0.08f, 0.17f, 0}, {s * 0.2f, 0.26f, -0.06f}, 0.045f, 0.032f, 5, Tex::Stone, dark);
        b.limb({s * 0.2f, 0.26f, -0.06f}, {s * 0.27f, 0.21f, -0.2f}, 0.032f, 0, 5, Tex::Stone, dark);
        b.plate({{s * 0.12f, 0.08f, 0.012f}, {s * 0.12f, 0.15f, 0.012f}, {s * 0.24f, 0.2f, 0}}, Tex::Stone, stone);
        b.box({s * 0.045f - 0.025f, 0.1f, frontZ(c, cr, 0.045f, 0.11f) - 0.02f}, {s * 0.045f + 0.025f, 0.125f, frontZ(c, cr, 0.045f, 0.11f) + 0.004f}, Tex::White, BLACK);
    }
    eyes(b, {0, 0.112f, frontZ(c, cr, 0.045f, 0.11f) + 0.006f}, 0.045f, 0.009f);
    teeth(b, -0.05f, 0.05f, 0.03f, 0.25f, 4, 0.04f, hex(0xC8CCC0));
    int head = g.part(torso, {0, 0.55f, 0.08f}, Role::Head);
    b.box({-0.06f, -0.04f, 0}, {0.06f, 0, 0.2f}, Tex::Stone, stone);
    teeth(b, -0.045f, 0.045f, 0, 0.18f, 3, -0.035f, hex(0xC8CCC0));
    g.part(head, {0, 0.03f, 0.06f}, Role::Jaw);

    for (int s : {-1, 1}) {
        Vec3 elbow{s * 0.42f, 0.38f, -0.04f}, f[3] = {{s * 0.8f, 0.18f, -0.06f}, {s * 0.68f, -0.2f, -0.06f}, {s * 0.38f, -0.38f, -0.05f}};
        b.limb({0, 0, 0}, elbow, 0.05f, 0.035f, 5, Tex::Stone, stone);
        for (Vec3 tip : f) b.limb(elbow, tip, 0.03f, 0.008f, 4, Tex::Stone, stone);
        b.tri({0, 0, 0}, elbow, f[2], Tex::Stone, dark, {0, 0, 1});
        b.tri(elbow, f[0], f[1], Tex::Stone, dark, {0, 0, 1});
        b.tri(elbow, f[1], f[2], Tex::Stone, dark, {0, 0, 1});
        b.tri({0, 0, 0}, f[2], {s * 0.08f, -0.3f, -0.03f}, Tex::Stone, dark, {0, 0, 1});
        g.part(torso, {s * 0.12f, 0.42f, -0.16f}, wingRole(s));
    }
    for (int s : {-1, 1}) {
        b.ball({0, -0.02f, 0}, {0.1f, 0.09f, 0.1f}, 6, 3, Tex::Stone, stone);
        b.limb({0, 0, 0}, {0, -0.32f, 0}, 0.08f, 0.07f, 6, Tex::Stone, stone);
        int arm = g.part(torso, {s * 0.27f, 0.45f, 0}, armRole(s));
        b.limb({0, 0, 0}, {0, -0.3f, 0}, 0.07f, 0.06f, 6, Tex::Stone, stone);
        b.box({-0.055f, -0.38f, -0.04f}, {0.055f, -0.29f, 0.05f}, Tex::Stone, stone);
        claws(b, {0, -0.37f, 0}, 0.13f, 3, 0.03f, talon);
        g.part(arm, {0, -0.32f, 0}, foreRole(s));
    }
    for (int s : {-1, 1}) {
        b.limb({0, 0, 0}, {0, -0.42f, 0}, 0.1f, 0.08f, 6, Tex::Stone, stone);
        int thigh = g.part(hips, {s * 0.13f, -0.03f, 0}, legRole(s));
        b.limb({0, 0, 0}, {0, -0.38f, 0}, 0.07f, 0.06f, 6, Tex::Stone, stone);
        b.box({-0.07f, -0.44f, -0.06f}, {0.07f, -0.37f, 0.1f}, Tex::Stone, stone);
        for (int i = 0; i < 3; i++) b.limb({(i - 1) * 0.04f, -0.42f, 0.09f}, {(i - 1) * 0.05f, -0.44f, 0.17f}, 0.016f, 0, 4, Tex::Stone, talon);
        g.part(thigh, {0, -0.42f, 0}, shinRole(s));
    }
    return g.m;
}

Model mimic(Renderer& r) {
    Rigger g(r, Rig::Mimic, 0.8f);
    g.m.lively = 0;
    Builder& b = g.b;
    const uint32_t tooth = hex(0xE8E0C8), flesh = hex(0x806068);
    chestBox(b);
    b.box({-0.42f, 0.3f, -0.27f}, {0.42f, 0.445f, 0.27f}, Tex::Maw);
    teeth(b, -0.4f, 0.4f, 0.44f, 0.255f, 8, -0.09f, tooth);
    int box = g.part(-1, {0, 0.17f, 0}, Role::Root);

    chestLid(b);
    b.box({-0.43f, -0.015f, 0.02f}, {0.43f, 0.0f, 0.58f}, Tex::Maw);
    teeth(b, -0.45f, 0.35f, 0, 0.555f, 8, 0.09f, tooth);
    eyes(b, {0, -0.03f, 0.5f}, 0.16f, 0.03f);
    g.part(box, {0, 0.45f, -0.3f}, Role::Head);

    b.xf = scale({1, 0.35f, 1});
    b.limb({0, 0, 0}, {0, 0, 0.45f}, 0.09f, 0.05f, 6, Tex::Maw);
    b.xf = {};
    g.part(box, {0, 0.34f, -0.25f}, Role::Tongue);
    for (int i = 0; i < 4; i++) {
        float x = i % 2 ? 0.33f : -0.33f, z = i < 2 ? 0.2f : -0.2f;
        b.limb({0, 0, 0}, {x * 0.15f, -0.18f, z * 0.15f}, 0.055f, 0.04f, 5, Tex::Flesh, flesh);
        for (int t = 0; t < 3; t++) b.limb({x * 0.15f + (t - 1) * 0.02f, -0.17f, z * 0.15f}, {x * 0.15f + (t - 1) * 0.03f, -0.19f, z * 0.15f + 0.07f}, 0.01f, 0, 4, Tex::Bone, tooth);
        g.part(box, {x, 0.03f, z}, Role::Leg, (i == 0 || i == 3) ? 0 : PI);
    }
    return g.m;
}

Model lich(Renderer& r) {
    Rigger g(r, Rig::Humanoid, 2.5f);
    g.m.strike = Strike::Cast, g.m.hover = 0.15f, g.m.crumble = true, g.m.size = 1.08f;
    Builder& b = g.b;
    const uint32_t robe = hex(0x6050A0), dark = hex(0x2A2040), bone = hex(0xD8D0C0);
    b.prism({0, -1.05f, 0}, 0.44f, 0.24f, 1.1f, 10, Tex::Robe, robe, 0.6f, false);
    b.prism({0, -1.05f, 0}, 0.445f, 0.425f, 0.08f, 10, Tex::Gold, WHITE, 0.3f, false);
    b.xf = translate({0, 0, 0.22f}) * rotateX(-0.18f);
    b.plate({{-0.07f, 0.02f, 0.012f}, {0.07f, 0.02f, 0.012f}, {0.13f, -1.06f, 0.012f}, {-0.13f, -1.06f, 0.012f}}, Tex::Robe, dark, dark, 0.6f);
    b.xf = {};
    int hips = g.part(-1, {0, 1.1f, 0}, Role::Root);

    b.xf = scale({1, 1, 0.8f});
    b.prism({0, 0, 0}, 0.24f, 0.3f, 0.55f, 10, Tex::Robe, robe, 0.6f);
    b.xf = {};
    b.ball({0, 0.52f, 0}, {0.33f, 0.1f, 0.2f}, 8, 4, Tex::Robe, dark);
    for (float a : {-1.0f, 0.0f, 1.0f}) {
        b.xf = translate({std::sin(a) * 0.14f, 0.55f, -std::cos(a) * 0.1f - 0.04f}) * rotateY(a) * rotateX(-0.35f);
        b.plate({{-0.14f, 0, 0.012f}, {0.14f, 0, 0.012f}, {0.2f, 0.36f, 0.008f}, {0, 0.42f, 0.008f}, {-0.2f, 0.36f, 0.008f}}, Tex::Robe, dark, hex(0xC8A040), 0.6f);
    }
    b.xf = {};
    b.ball({0, 0.4f, 0.24f}, {0.045f, 0.055f, 0.03f}, 6, 3, Tex::Orb);
    int torso = g.part(hips, {0, 0.05f, 0}, Role::Torso);

    b.limb({0, 0, -0.01f}, {0, 0.08f, 0}, 0.025f, 0.022f, 4, Tex::Bone, bone);
    skull(b, {0, 0.18f, 0.01f}, 1.15f, bone, Tex::Orb);
    b.prism({0, 0.27f, 0}, 0.115f, 0.12f, 0.05f, 10, Tex::Gold, WHITE, 0.3f, false);
    for (int i = 0; i < 7; i++) {
        float a = PI / 2 + (float(i) - 3) * 0.42f;
        Vec3 out{std::cos(a), 0, std::sin(a)};
        b.limb(out * 0.115f + Vec3{0, 0.3f, 0}, out * 0.13f + Vec3{0, 0.42f - 0.025f * std::fabs(float(i) - 3), 0}, 0.022f, 0, 4, Tex::Gold);
    }
    b.box({-0.02f, 0.28f, 0.115f}, {0.02f, 0.31f, 0.13f}, Tex::Orb);
    int head = g.part(torso, {0, 0.6f, 0.02f}, Role::Head);
    skullJaw(b, 1.15f, bone);
    g.part(head, {0, 0.18f - 0.08f, 0.01f}, Role::Jaw);

    for (int s : {-1, 1}) {
        b.limb({0, 0.02f, 0}, {0, -0.32f, 0}, 0.08f, 0.09f, 6, Tex::Robe, robe);
        int arm = g.part(torso, {s * 0.3f, 0.48f, 0}, armRole(s));
        b.prism({0, -0.3f, 0}, 0.14f, 0.09f, 0.31f, 8, Tex::Robe, robe, 0.6f, false);
        b.prism({0, -0.3f, 0}, 0.145f, 0.14f, 0.04f, 8, Tex::Gold, WHITE, 0.3f, false);
        b.box({-0.035f, -0.38f, -0.02f}, {0.035f, -0.31f, 0.025f}, Tex::Bone, bone);
        for (int f = 0; f < 4; f++) {
            float x = (float(f) - 1.5f) * 0.022f;
            b.limb({x, -0.37f, 0.01f}, {x * 1.4f, -0.49f, 0.06f}, 0.01f, 0.003f, 4, Tex::Bone, bone);
        }
        if (s > 0) {
            b.xf = translate({0, -0.35f, 0.01f});
            b.limb({0, -0.95f, 0}, {0, 1.0f, 0}, 0.026f, 0.022f, 6, Tex::Wood, hex(0x5A4A40), 0.4f);
            for (float x : {-1.0f, 1.0f}) {
                b.limb({0, 0.95f, 0}, {x * 0.1f, 1.1f, 0}, 0.02f, 0.016f, 4, Tex::Gold);
                b.limb({x * 0.1f, 1.1f, 0}, {x * 0.05f, 1.26f, 0}, 0.016f, 0, 4, Tex::Gold);
            }
            b.ball({0, 1.12f, 0}, {0.075f, 0.08f, 0.075f}, 8, 5, Tex::Orb);
            b.xf = {};
        }
        g.part(arm, {0, -0.32f, 0}, foreRole(s));
    }
    return g.m;
}

void armour(Builder& b, ModelId a) {
    switch (a) {
    case ModelId::LeatherArmor:
        b.xf = scale({1, 1, 0.68f});
        b.prism({0, 0, 0}, 0.17f, 0.21f, 0.4f, 8, Tex::Leather, hex(0xC09070), 0.4f);
        b.prism({0, 0.06f, 0}, 0.178f, 0.18f, 0.05f, 8, Tex::Leather, hex(0x705040), 0.3f, false);
        b.xf = {};
        for (float s : {-1.0f, 1.0f}) b.ball({s * 0.2f, 0.4f, 0}, {0.09f, 0.05f, 0.1f}, 6, 3, Tex::Leather, hex(0xA07858));
        b.box({-0.025f, 0.06f, 0.115f}, {0.025f, 0.11f, 0.13f}, Tex::Gold);
        b.prism({0, 0.4f, 0}, 0.1f, 0.09f, 0.04f, 8, Tex::Leather, hex(0x705040), 0.3f, false);
        break;
    case ModelId::ChainMail:
        b.xf = scale({1, 1, 0.7f});
        b.prism({0, -0.04f, 0}, 0.21f, 0.21f, 0.46f, 8, Tex::Chain, hex(0xC8CCD4), 0.25f);
        b.prism({0, 0.08f, 0}, 0.216f, 0.216f, 0.045f, 8, Tex::Leather, WHITE, 0.3f, false);
        b.xf = {};
        for (float s : {-1.0f, 1.0f}) b.limb({s * 0.18f, 0.38f, 0}, {s * 0.33f, 0.2f, 0}, 0.075f, 0.065f, 6, Tex::Chain, hex(0xC8CCD4), 0.25f);
        b.prism({0, 0.42f, 0}, 0.13f, 0.11f, 0.06f, 8, Tex::Chain, hex(0xC8CCD4), 0.25f, false);
        b.box({-0.025f, 0.08f, 0.145f}, {0.025f, 0.125f, 0.16f}, Tex::Gold);
        break;
    default:
        b.xf = scale({1, 1, 0.72f});
        b.prism({0, 0.08f, 0}, 0.19f, 0.24f, 0.36f, 8, Tex::Steel, hex(0xD8DCE4), 0.4f);
        for (int i = 0; i < 3; i++) b.prism({0, 0.08f - float(i) * 0.05f, 0}, 0.2f + float(i) * 0.012f, 0.19f + float(i) * 0.012f, 0.05f, 8, Tex::Steel, hex(0xA8ACB8), 0.4f, false);
        b.xf = {};
        b.limb({0, 0.1f, 0.155f}, {0, 0.42f, 0.18f}, 0.018f, 0.018f, 4, Tex::Steel, WHITE);
        for (float s : {-1.0f, 1.0f}) {
            b.ball({s * 0.24f, 0.4f, 0}, {0.12f, 0.08f, 0.12f}, 8, 3, Tex::Steel, hex(0xC0C4CC));
            b.prism({s * 0.25f, 0.33f, 0}, 0.115f, 0.11f, 0.04f, 8, Tex::Gold, WHITE, 0.3f, false);
        }
        b.prism({0, 0.43f, 0}, 0.12f, 0.1f, 0.05f, 8, Tex::Gold, WHITE, 0.3f, false);
        break;
    }
    b.xf = {};
}

// The first-person gauntlet: leather fingers wrapped round a grip on the Y axis, steel plates
// over the knuckles, palm facing -X, knuckles forward (-Z), thumb over the top, forearm to +Z.
void glove(Builder& b) {
    const uint32_t leather = hex(0xC89A70), plate = hex(0xD0D4DC), sleeve = hex(0x8A3A30);
    b.ball({0.03f, 0, 0.02f}, {0.024f, 0.052f, 0.056f}, 7, 4, Tex::Leather, leather, 0.2f);
    b.ball({-0.012f, -0.004f, 0.045f}, {0.03f, 0.046f, 0.032f}, 7, 4, Tex::Leather, leather, 0.2f);
    b.ball({0.045f, 0, 0.012f}, {0.012f, 0.054f, 0.046f}, 7, 3, Tex::Steel, plate, 0.2f);
    for (int i = 0; i < 4; i++) {
        float y = 0.034f - float(i) * 0.0225f, r = i == 3 ? 0.0095f : 0.011f;
        Vec3 k{0.035f, y, -0.03f}, p1{-0.008f, y, -0.047f}, p2{-0.04f, y, -0.018f}, tip{-0.028f, y, 0.014f};
        b.limb(k, p1, r * 1.25f, r * 1.15f, 6, Tex::Steel, plate, 0.2f);
        b.ball(p1, {r * 1.1f, r, r * 1.1f}, 6, 3, Tex::Leather, leather, 0.2f);
        b.limb(p1, p2, r, r * 0.95f, 6, Tex::Leather, leather, 0.2f);
        b.ball(p2, {r, r, r}, 6, 3, Tex::Leather, leather, 0.2f);
        b.limb(p2, tip, r * 0.95f, r * 0.85f, 6, Tex::Leather, leather, 0.2f);
    }
    Vec3 t0{0.0f, 0.036f, 0.05f}, t1{-0.036f, 0.052f, 0.012f}, t2{-0.026f, 0.05f, -0.03f};
    b.limb(t0, t1, 0.017f, 0.015f, 6, Tex::Leather, leather, 0.2f);
    b.ball(t1, {0.015f, 0.015f, 0.015f}, 6, 3, Tex::Leather, leather, 0.2f);
    b.limb(t1, t2, 0.015f, 0.012f, 6, Tex::Leather, leather, 0.2f);
    Vec3 wrist{0.01f, -0.004f, 0.07f}, arm = normalize(Vec3{0, -0.35f, 1});
    b.limb(wrist, wrist + arm * 0.13f, 0.046f, 0.066f, 8, Tex::Leather, leather, 0.2f);
    b.limb(wrist + arm * 0.11f, wrist + arm * 0.145f, 0.07f, 0.07f, 8, Tex::Steel, plate, 0.2f);
    b.limb(wrist + arm * 0.14f, wrist + arm * 0.5f, 0.062f, 0.074f, 8, Tex::Cloth, sleeve, 0.3f);
}

void buildAll(Renderer& r) {
    Builder b;
    auto set = [&](ModelId id, Model m) { models[int(id)] = std::move(m); };

    set(ModelId::Rat, rat(r));
    set(ModelId::Bat, bat(r));
    set(ModelId::Skeleton, skeleton(r));
    set(ModelId::Slime, ooze(r));
    set(ModelId::Ghoul, ghoul(r));
    set(ModelId::Spider, spider(r));
    set(ModelId::Cultist, cultist(r));
    set(ModelId::Wraith, wraith(r));
    set(ModelId::Knight, knight(r));
    set(ModelId::Ogre, ogre(r));
    set(ModelId::Imp, imp(r));
    set(ModelId::Gargoyle, gargoyle(r));
    set(ModelId::Mimic, mimic(r));
    set(ModelId::Lich, lich(r));

    for (ModelId w : {ModelId::Dagger, ModelId::Sword, ModelId::Mace, ModelId::Axe, ModelId::Spear, ModelId::Greatsword, ModelId::Torch}) {
        weapon(b, w);
        set(w, single(r, b, 0.8f));
    }
    for (ModelId a : {ModelId::LeatherArmor, ModelId::ChainMail, ModelId::PlateArmor}) {
        armour(b, a);
        set(a, single(r, b, 0.5f));
    }

    {
        Rigger g(r, Rig::Static, 0.3f);
        g.b.ball({0, 0.09f, 0}, {0.085f, 0.09f, 0.085f}, 8, 5, Tex::Glass, WHITE, 0.3f);
        g.b.limb({0, 0.16f, 0}, {0, 0.25f, 0}, 0.032f, 0.026f, 6, Tex::Glass, WHITE, 0.3f);
        g.b.prism({0, 0.245f, 0}, 0.037f, 0.035f, 0.018f, 6, Tex::Glass, WHITE, 0.3f);
        g.part(-1, {0, 0, 0}, Role::Root);
        g.b.prism({0, 0.255f, 0}, 0.024f, 0.029f, 0.045f, 6, Tex::Wood, WHITE, 0.2f);
        g.b.limb({-0.03f, 0.25f, 0}, {0.03f, 0.25f, 0}, 0.034f, 0.034f, 6, Tex::Cloth, hex(0xC0A880), 0.2f, false);
        int cork = g.part(-1, {0, 0, 0}, Role::Fixed);
        g.m.parts[cork].fixedColour = true;
        set(ModelId::Potion, g.m);
    }

    b.limb({-0.15f, 0.1f, 0}, {0.15f, 0.1f, 0}, 0.045f, 0.045f, 8, Tex::Parchment, WHITE, 0.3f);
    for (float s : {-1.0f, 1.0f}) b.ball({s * 0.165f, 0.1f, 0}, {0.022f, 0.03f, 0.03f}, 6, 3, Tex::Wood);
    b.limb({-0.015f, 0.1f, 0}, {0.015f, 0.1f, 0}, 0.049f, 0.049f, 8, Tex::Cloth, hex(0xC03028), 0.2f);
    b.ball({0, 0.1f, 0.05f}, {0.026f, 0.026f, 0.012f}, 6, 3, Tex::Cloth, hex(0x901810));
    b.xf = translate({0, 0, 0.042f});
    b.plate({{-0.13f, 0.1f, 0.003f}, {0.13f, 0.1f, 0.003f}, {0.13f, -0.05f, 0.003f}, {-0.13f, -0.05f, 0.003f}}, Tex::Parchment, WHITE, WHITE, 0.26f);
    b.xf = {};
    set(ModelId::Scroll, single(r, b, 0.2f));

    b.limb({0, 0, 0}, {0, 0.36f, 0}, 0.017f, 0.012f, 6, Tex::Wood, hex(0x6A4A3A), 0.3f);
    b.limb({0, 0, 0}, {0, 0.1f, 0}, 0.02f, 0.02f, 6, Tex::Leather, WHITE, 0.2f);
    for (int i = 0; i < 3; i++) b.prism({0, 0.12f + float(i) * 0.08f, 0}, 0.019f, 0.017f, 0.012f, 6, Tex::Gold, WHITE, 0.2f);
    b.prism({0, 0.34f, 0}, 0.014f, 0.02f, 0.02f, 6, Tex::Gold);
    b.prism({0, 0.36f, 0}, 0.0f, 0.035f, 0.035f, 4, Tex::Orb);
    b.prism({0, 0.395f, 0}, 0.035f, 0.0f, 0.05f, 4, Tex::Orb);
    set(ModelId::Wand, single(r, b, 0.45f));

    b.torus({0, 0.1f, 0}, 0.075f, 0.017f, 12, 5, Tex::Gold);
    b.box({-0.03f, 0.165f, -0.025f}, {0.03f, 0.185f, 0.025f}, Tex::Gold);
    b.prism({0, 0.16f, 0}, 0.0f, 0.03f, 0.025f, 4, Tex::Orb);
    b.prism({0, 0.185f, 0}, 0.03f, 0.0f, 0.035f, 4, Tex::Orb);
    set(ModelId::Ring, single(r, b, 0.22f));

    b.ball({0, 0.1f, 0}, {0.1f, 0.1f, 0.1f}, 8, 5, Tex::Clay, WHITE, 0.3f);
    b.limb({0, 0.18f, 0}, {0, 0.25f, 0}, 0.036f, 0.03f, 6, Tex::Clay, WHITE, 0.3f);
    b.prism({0, 0.2f, 0}, 0.04f, 0.04f, 0.02f, 6, Tex::Cloth, hex(0xC0A070), 0.2f, false);
    b.limb({0, 0.24f, 0}, {0.02f, 0.31f, 0.01f}, 0.03f, 0.022f, 5, Tex::Cloth, hex(0xA09080), 0.2f);
    b.xf = translate({0.02f, 0.3f, 0.01f}) * rotateZ(-0.6f);
    b.plate({{-0.02f, 0, 0.004f}, {0.03f, 0, 0.004f}, {0.05f, -0.08f, 0}, {0.0f, -0.06f, 0}}, Tex::Cloth, hex(0xA09080), hex(0xA09080), 0.2f);
    b.xf = {};
    set(ModelId::Oil, single(r, b, 0.32f));

    b.prism({0, 0, 0}, 0.17f, 0.05f, 0.08f, 10, Tex::Gold, WHITE, 0.25f);
    for (int i = 0; i < 12; i++) {
        float a = float(i) * 2.4f, d = 0.08f + 0.1f * hashf(i, 1);
        b.xf = translate({std::cos(a) * d, 0.02f + 0.045f * hashf(i, 2), std::sin(a) * d}) * rotateX(0.5f * (hashf(i, 3) - 0.5f)) * rotateZ(0.6f * (hashf(i, 4) - 0.5f));
        b.prism({0, 0, 0}, 0.035f, 0.035f, 0.01f, 8, Tex::Gold, WHITE, 0.15f);
    }
    b.xf = {};
    for (int i = 0; i < 4; i++) b.prism({0.13f, float(i) * 0.012f, -0.08f}, 0.034f, 0.034f, 0.011f, 8, Tex::Gold, WHITE, 0.15f);
    set(ModelId::Gold, single(r, b, 0.12f));

    for (int i = 0; i < 3; i++) {
        float a = float(i) * 2 * PI / 3;
        Vec3 out{std::cos(a), 0, std::sin(a)};
        b.limb(out * 0.14f + Vec3{0, 0.74f, 0}, out * 0.22f + Vec3{0, 0.36f, 0}, 0.025f, 0.022f, 5, Tex::Iron, hex(0x8A7A70));
        b.limb(out * 0.22f + Vec3{0, 0.36f, 0}, out * 0.32f, 0.022f, 0.026f, 5, Tex::Iron, hex(0x8A7A70));
        b.ball(out * 0.33f + Vec3{0, 0.015f, 0}, {0.04f, 0.022f, 0.04f}, 5, 2, Tex::Iron);
    }
    b.prism({0, 0.35f, 0}, 0.215f, 0.215f, 0.025f, 8, Tex::Iron, hex(0x8A7A70), 0.4f, false);
    b.prism({0, 0.72f, 0}, 0.16f, 0.34f, 0.18f, 10, Tex::Iron, hex(0x8A7A70), 0.4f);
    b.prism({0, 0.89f, 0}, 0.355f, 0.355f, 0.035f, 10, Tex::Iron, hex(0x6A5A50), 0.4f, false);
    b.ball({0, 0.885f, 0}, {0.31f, 0.075f, 0.31f}, 10, 3, Tex::Coals, WHITE, 0.4f, 0.4f);
    set(ModelId::Brazier, single(r, b, 1.0f));

    b.box({-0.42f, 0, -0.42f}, {0.42f, 0.14f, 0.42f}, Tex::Stone, WHITE, 0.8f);
    b.box({-0.36f, 0.14f, -0.36f}, {0.36f, 0.26f, 0.36f}, Tex::Stone, hex(0xE0E0E0), 0.8f);
    b.prism({0, 0.26f, 0}, 0.3f, 0.27f, 0.07f, 12, Tex::Stone, WHITE, 0.8f, false);
    b.prism({0, 0.33f, 0}, 0.26f, 0.235f, 1.95f, 12, Tex::Stone, WHITE, 0.8f, false);
    b.prism({0, 2.22f, 0}, 0.25f, 0.27f, 0.06f, 12, Tex::Stone, WHITE, 0.8f, false);
    b.prism({0, 2.28f, 0}, 0.26f, 0.36f, 0.14f, 12, Tex::Stone, hex(0xE0E0E0), 0.8f, false);
    b.box({-0.4f, 2.42f, -0.4f}, {0.4f, 2.6f, 0.4f}, Tex::Stone, WHITE, 0.8f);
    set(ModelId::Pillar, single(r, b, 2.6f));

    const uint32_t old = hex(0xC8BCA0);
    b.xf = rotateY(0.4f) * translate({0.05f, 0.095f, 0.02f}) * rotateZ(0.25f) * rotateX(-0.15f);
    skull(b, {0, 0, 0}, 1, old, Tex::White);
    b.xf = translate({-0.2f, 0.02f, 0.18f}) * rotateY(1.1f) * rotateX(0.5f);
    skullJaw(b, 1, old);
    for (int i = 0; i < 5; i++) {
        float a = float(i) * 1.7f + 0.3f, d = 0.18f + 0.12f * float(i % 2), len = 0.18f + 0.06f * float(i % 3);
        Vec3 c{std::cos(a) * d, 0.022f, std::sin(a) * d}, dir{std::cos(a * 2.1f), 0.02f, std::sin(a * 2.1f)};
        b.xf = {};
        b.limb(c - dir * len, c + dir * len, 0.02f, 0.018f, 5, Tex::Bone, old);
        b.ball(c - dir * len, {0.03f, 0.025f, 0.03f}, 5, 2, Tex::Bone, old);
        b.ball(c + dir * len, {0.03f, 0.025f, 0.03f}, 5, 2, Tex::Bone, old);
    }
    for (int i = 0; i < 3; i++) {
        b.xf = translate({-0.15f + float(i) * 0.06f, 0.02f, -0.2f}) * rotateZ(PI / 2) * scale({1, 1, 0.8f});
        b.prism({0, -0.03f, 0}, 0.12f, 0.12f, 0.025f, 8, Tex::Bone, old, 0.5f, false);
    }
    b.xf = {};
    set(ModelId::Bones, single(r, b, 0.25f));

    {
        Rigger g(r, Rig::Static, 0.8f);
        chestBox(g.b);
        int box = g.part(-1, {0, 0, 0}, Role::Root);
        chestLid(g.b);
        g.part(box, {0, 0.45f, -0.3f}, Role::Head);
        set(ModelId::Chest, g.m);
    }

    for (ModelId id : {ModelId::Fountain, ModelId::FountainDry}) {
        bool wet = id == ModelId::Fountain;
        b.prism({0, 0, 0}, 0.8f, 0.76f, 0.42f, 8, Tex::Stone, WHITE, 0.8f, false);
        b.prism({0, 0.42f, 0}, 0.8f, 0.8f, 0.06f, 8, Tex::Stone, hex(0xE8E8E8), 0.8f, false);
        b.prism({0, 0.1f, 0}, 0.66f, 0.66f, 0.38f, 8, Tex::Stone, hex(0xA0A0A0), 0.8f, false);
        for (int i = 0; i < 8; i++) {
            float a0 = 2 * PI * float(i) / 8 + PI / 8, a1 = a0 + 2 * PI / 8;
            auto at = [](float a, float rr) { return Vec3{std::cos(a) * rr, 0.48f, std::sin(a) * rr}; };
            b.face(at(a0, 0.66f), at(a1, 0.66f), at(a1, 0.8f), at(a0, 0.8f), Tex::Stone, hex(0xF0F0F0), 0.3f, 0.1f, {0, 1, 0});
        }
        b.prism({0, 0.09f, 0}, 0.67f, 0.67f, 0.02f, 8, Tex::Stone, hex(0x707070), 0.8f);
        b.prism({0, 0.1f, 0}, 0.15f, 0.11f, 0.78f, 8, Tex::Stone, WHITE, 0.6f, false);
        b.prism({0, 0.86f, 0}, 0.1f, 0.33f, 0.13f, 8, Tex::Stone, WHITE, 0.6f);
        b.ball({0, 1.0f, 0}, {0.07f, 0.09f, 0.07f}, 6, 4, Tex::Stone);
        if (wet) {
            b.prism({0, 0.33f, 0}, 0.665f, 0.665f, 0.02f, 8, Tex::Water, WHITE, 1.3f);
            b.prism({0, 0.97f, 0}, 0.3f, 0.3f, 0.015f, 8, Tex::Water, WHITE, 0.6f);
        } else {
            b.ball({0.3f, 0.1f, 0.2f}, {0.2f, 0.03f, 0.15f}, 6, 2, Tex::Stone, hex(0x5A6048));
        }
        set(id, single(r, b, 1.2f));
    }

    {
        Rigger g(r, Rig::Static, 2.6f);
        g.b.box({-1.0f, 0, -0.2f}, {-0.72f, 2.6f, 0.2f}, Tex::Stone, WHITE, 0.8f);
        g.b.box({0.72f, 0, -0.2f}, {1.0f, 2.6f, 0.2f}, Tex::Stone, WHITE, 0.8f);
        g.b.box({-0.72f, 2.15f, -0.2f}, {0.72f, 2.6f, 0.2f}, Tex::Stone, hex(0xE0E0E0), 0.8f);
        g.b.box({-0.12f, 2.1f, -0.23f}, {0.12f, 2.5f, 0.23f}, Tex::Stone, WHITE, 0.6f);
        for (float s : {-1.0f, 1.0f}) g.b.box({s * 0.86f - 0.17f, 0, -0.23f}, {s * 0.86f + 0.17f, 0.2f, 0.23f}, Tex::Stone, hex(0xC8C8C8), 0.6f);
        g.part(-1, {0, 0, 0}, Role::Fixed);
        g.b.box({0, 0, -0.06f}, {1.44f, 2.15f, 0.06f}, Tex::Door, WHITE, 1.44f, 2.15f);
        g.part(0, {-0.72f, 0, 0}, Role::Fixed);
        set(ModelId::Door, g.m);
    }

    b.box({-0.55f, 0, -0.55f}, {0.55f, 0.025f, 0.55f}, Tex::Iron, hex(0x7A746C), 1.1f);
    b.box({-0.45f, 0.025f, -0.45f}, {0.45f, 0.045f, 0.45f}, Tex::Stone, hex(0x9A948C), 0.9f);
    for (int i = 0; i < 16; i++) {
        float x = -0.33f + float(i % 4) * 0.22f, z = -0.33f + float(i / 4) * 0.22f;
        b.box({x - 0.035f, 0.044f, z - 0.035f}, {x + 0.035f, 0.047f, z + 0.035f}, Tex::White, BLACK);
        if ((i * 7) % 3 == 0) b.prism({x, 0.04f, z}, 0.025f, 0, 0.12f + 0.04f * float(i % 2), 4, Tex::Steel, hex(0xC0C0C0));
    }
    set(ModelId::Trap, single(r, b, 0.2f));

    glove(b);
    set(ModelId::Hand, single(r, b, 0.1f));
}

// Values every rig's pose is built from.
struct Motion {
    float k = 0;       // progress through a one-shot animation, 0..1
    float idle = 0;    // slow breathing sway, -1..1
    float stride = 0;  // walk cycle, -1..1
    float hurt = 0;    // 0 -> 1 -> 0 over the flinch
    float die = 0;     // 0 -> 1
    float windup = 0;  // attack: rises to 1 at 45%, back to 0 by the strike at 65%
    float lunge = 0;   // attack: 0 until 45%, 1 at the strike, back to 0 by the end
};

Motion motion(Anim anim, float t, float phase, float lively) {
    Motion m;
    float dur = anim == Anim::Attack ? ATTACK_TIME : anim == Anim::Hurt ? HURT_TIME : anim == Anim::Die ? DIE_TIME : 0;
    m.k = dur > 0 ? clamp01(t / dur) : 0;
    m.idle = std::sin(t * 2.2f + phase) * lively;
    m.stride = anim == Anim::Walk ? std::sin(t / WALK_TIME * 2 * PI + phase) : 0;
    m.hurt = anim == Anim::Hurt ? std::sin(m.k * PI) : 0;
    m.die = anim == Anim::Die ? smooth(m.k) : 0;
    if (anim == Anim::Attack) {
        m.windup = m.k < 0.45f ? smooth(m.k / 0.45f) : m.k < 0.65f ? 1 - smooth((m.k - 0.45f) / 0.2f) : 0;
        m.lunge = m.k < 0.45f ? 0 : m.k < 0.65f ? smooth((m.k - 0.45f) / 0.2f) : 1 - smooth((m.k - 0.65f) / 0.35f);
    }
    return m;
}

float sideOf(Role r) {
    return r == Role::ArmL || r == Role::ForeL || r == Role::LegL || r == Role::ShinL || r == Role::WingL || r == Role::WingTipL ? -1.0f : 1.0f;
}

Mat4 humanoidPose(const Model& m, const Part& p, Anim anim, float t) {
    Motion a = motion(anim, t, 0, m.lively);
    float side = sideOf(p.role), fall = m.crumble ? 0 : a.die;
    bool hovering = m.hover > 0;
    float crouch = m.crouch * (anim == Anim::Walk ? 0.6f : 1.0f) * (1 - a.die);
    switch (p.role) {
    case Role::Root: {
        float lift = hovering ? m.hover * (1 - a.die) + 0.06f * std::sin(t * 2.4f) * (1 - a.die) : std::fabs(a.stride) * 0.04f;
        float drop = m.legs * 0.5f * (2 - std::cos(crouch) - std::cos(crouch * 0.9f)) + (m.strike == Strike::Smash ? a.lunge * 0.12f : 0);
        Vec3 move{0, lift - drop - fall * p.pivot.y * 0.8f, a.lunge * 0.22f - a.hurt * 0.1f};
        return translate(move) * rotateX(-fall * 1.45f) * rotateZ(0.05f * a.stride);
    }
    case Role::Torso: {
        float lean = m.hunch + 0.03f * a.idle - 0.3f * a.hurt + 0.25f * fall, twist = 0.1f * a.stride;
        switch (m.strike) {
        case Strike::Slash: lean += 0.3f * a.lunge - 0.08f * a.windup, twist += -0.35f * a.windup + 0.3f * a.lunge; break;
        case Strike::Claw: lean += 0.35f * a.lunge + 0.1f * a.windup, twist += 0.25f * (a.windup - a.lunge); break;
        case Strike::Cast: lean += -0.25f * a.windup + 0.3f * a.lunge; break;
        case Strike::Smash: lean += -0.3f * a.windup + 0.55f * a.lunge, twist += -0.2f * a.windup; break;
        }
        return rotateX(lean) * rotateY(twist);
    }
    case Role::Head: {
        float look = 0.25f * std::sin(t * 0.6f + 1) * m.lively * (anim == Anim::Idle);
        return rotateX(-m.hunch * 0.85f - 0.35f * a.hurt + 0.04f * a.idle + 0.45f * fall - 0.25f * a.windup * (m.strike == Strike::Cast)) * rotateY(look);
    }
    case Role::Jaw: return rotateX(0.04f + 0.55f * a.windup + 0.15f * a.lunge + 0.35f * a.hurt + 0.5f * a.die + 0.08f * std::max(0.0f, std::sin(t * 9)) * m.lively);
    case Role::ArmL:
    case Role::ArmR: {
        bool lead = p.role == Role::ArmR;
        float x = (lead ? 0.5f : -0.5f) * a.stride + 0.05f * a.idle + 0.35f * a.hurt - 0.5f * fall;
        float z = side * (0.12f + 0.06f * a.idle + 0.45f * a.hurt + 0.9f * fall);
        switch (m.strike) {
        case Strike::Slash:
            if (lead) x += -2.6f * a.windup - 0.4f * a.lunge, z += side * 0.3f * a.windup;
            else x += -0.6f * a.lunge - 0.3f * a.windup;
            break;
        case Strike::Claw: {
            Motion c = lead ? a : motion(anim, t - 0.07f, 0, 1);
            x += -0.35f - 2.2f * c.windup - 1.0f * c.lunge;
            z += side * 0.55f * c.windup;
            break;
        }
        case Strike::Cast: x += -2.7f * a.windup - 1.5f * a.lunge, z += side * 0.35f * a.windup; break;
        case Strike::Smash: x += -2.9f * a.windup - 0.9f * a.lunge, z += -side * 0.15f * a.windup; break;
        }
        if (hovering) x += -0.2f;
        return rotateX(x) * rotateZ(z);
    }
    case Role::ForeL:
    case Role::ForeR: {
        bool lead = p.role == Role::ForeR;
        float x = -0.3f - 0.4f * a.hurt + 0.3f * fall;
        if (m.strike == Strike::Slash && lead) x += -0.9f * a.windup + 0.25f * a.lunge;
        if (m.strike == Strike::Claw) x += -0.5f * a.windup + 0.3f * a.lunge;
        if (m.strike == Strike::Smash) x += -0.6f * a.windup + 0.25f * a.lunge;
        return rotateX(x);
    }
    case Role::LegL:
    case Role::LegR: {
        float s = p.role == Role::LegL ? 1.0f : -1.0f;
        if (hovering) return rotateX(-0.3f + 0.12f * std::sin(t * 2 + s) - 0.4f * a.die) * rotateZ(side * 0.08f);
        return rotateZ(side * (0.03f + 0.3f * crouch)) * rotateX(s * 0.6f * a.stride - crouch - 0.4f * fall - 0.3f * a.lunge * (s > 0));
    }
    case Role::ShinL:
    case Role::ShinR: {
        float s = p.role == Role::ShinL ? 1.0f : -1.0f;
        if (hovering) return rotateX(0.5f + 0.12f * std::sin(t * 2 + s + 1));
        return rotateZ(-sideOf(p.role) * 0.3f * crouch) * rotateX(std::max(0.0f, s * a.stride) * 0.9f + crouch * 1.9f + 0.5f * fall);
    }
    case Role::WingL:
    case Role::WingR: {
        float spread = std::max(a.windup + a.lunge, anim == Anim::Walk ? 0.5f : 0.0f);
        float fold = hovering ? 0.35f : 1.05f * (1 - 0.8f * spread);
        float flap = hovering ? 0.55f * std::sin(t * 14) : 0.25f + 0.35f * (a.windup + a.lunge) + 0.3f * std::fabs(a.stride);
        return rotateY(side * fold) * rotateZ(side * (flap - 0.4f * fall));
    }
    case Role::Tail: return rotateY(0.35f * std::sin(t * 3 + p.phase)) * rotateX(0.15f * std::sin(t * 2 + p.phase));
    default: return Mat4{};
    }
}

Mat4 pose(const Model& m, const Part& p, Anim anim, float t) {
    Motion a = motion(anim, t, p.phase, m.lively);
    float side = sideOf(p.role);
    switch (m.rig) {
    case Rig::Humanoid: return humanoidPose(m, p, anim, t);
    case Rig::Rat:
        switch (p.role) {
        case Role::Root: return translate({0, std::fabs(a.stride) * 0.04f - a.die * 0.12f + 0.05f * a.windup, a.lunge * 0.35f - a.hurt * 0.12f}) * rotateZ(a.die * 1.7f) * rotateX(-0.25f * a.windup + 0.15f * a.lunge);
        case Role::Head: {
            float sniff = anim == Anim::Idle ? 0.12f * std::sin(t * 9) : 0;
            return rotateX(-sniff - 0.35f * a.windup + 0.25f * a.lunge - 0.4f * a.hurt) * rotateY(anim == Anim::Idle ? 0.3f * std::sin(t * 1.3f) : 0);
        }
        case Role::Jaw: return rotateX(0.05f + 0.75f * a.windup + 0.2f * a.hurt + 0.4f * a.die);
        case Role::Tail: return rotateY(0.35f * std::sin(t * 4 + p.phase) + 0.25f * a.stride) * rotateX((p.phase == 0 ? -0.55f : 0.14f) + 0.05f * std::sin(t * 3 + p.phase));
        case Role::Leg: return rotateX(0.9f * a.stride + a.die * (0.9f + 0.25f * std::sin(t * 30 + p.phase)));
        default: return Mat4{};
        }
    case Rig::Bat: {
        float alive = 1 - a.die, flap = std::sin(t * 15) * alive;
        switch (p.role) {
        case Role::Root:
            return translate({0, (0.08f * std::sin(t * 3) + 0.15f * a.windup - 0.3f * a.lunge) * alive - a.die * 1.15f, a.lunge * 0.75f - a.hurt * 0.15f - 0.2f * a.windup}) *
                   rotateZ(0.15f * std::sin(t * 2.3f) * alive) * rotateX(-a.hurt * 0.6f + 0.5f * a.lunge - 0.3f * a.windup + a.die * 1.4f);
        case Role::WingL:
        case Role::WingR: return rotateY(side * a.die * 1.2f) * rotateZ(side * (flap * 0.85f + 0.1f + 0.5f * a.windup - a.die * 0.9f));
        case Role::WingTipL:
        case Role::WingTipR: return rotateZ(side * (std::sin(t * 15 - 1.0f) * 0.6f * alive - a.die * 0.8f));
        default: return Mat4{};
        }
    }
    case Rig::Slime: {
        float squash = 1 + 0.07f * std::sin(t * 3) + 0.3f * a.lunge - 0.12f * a.windup + std::fabs(a.stride) * 0.2f - 0.25f * a.hurt;
        float sy = squash * (1 - 0.85f * a.die), sxz = (1 + 0.6f * a.die) / std::sqrt(squash);
        if (p.role == Role::Head)
            return translate({0, std::max(0.34f * (sy - 1), -0.24f) + 0.03f * std::sin(t * 1.7f), a.lunge * 0.45f + 0.03f * std::sin(t * 1.1f)}) *
                   rotateY(0.5f * std::sin(t * 0.5f)) * rotateZ(0.2f * std::sin(t * 0.8f) + 0.5f * a.die);
        return translate({0, 0, a.lunge * 0.45f - a.hurt * 0.1f}) * scale({sxz, sy, sxz * (1 + 0.25f * a.lunge)});
    }
    case Rig::Spider: {
        float walkLift = anim == Anim::Walk ? 0.35f * std::max(0.0f, std::cos(t / WALK_TIME * 2 * PI + p.phase)) : 0;
        switch (p.role) {
        case Role::Root:
            return translate({0, std::fabs(a.stride) * 0.03f + 0.01f * a.idle - a.die * 0.28f + 0.1f * a.windup, a.lunge * 0.4f - a.hurt * 0.12f}) *
                   rotateX(-0.45f * a.windup + 0.15f * a.lunge - 0.35f * a.hurt) * rotateZ(0.3f * a.die);
        case Role::Tail: return rotateX(0.04f * std::sin(t * 2) + 0.2f * a.windup - 0.1f * a.die) * rotateY(0.06f * a.stride);
        case Role::Jaw: return rotateX(-0.5f * a.windup + 0.6f * a.lunge + 0.08f * std::sin(t * 6));
        case Role::Leg: {
            bool front = p.pivot.z > 0.18f;
            float lift = walkLift + (front ? 0.9f * a.windup : 0) - 0.45f * a.die + 0.15f * a.hurt;
            return rotateY(p.yaw + 0.3f * a.stride + 0.04f * a.idle) * rotateZ(lift);
        }
        case Role::LegTip: return rotateZ(-0.5f * walkLift - 2.1f * a.die);
        default: return Mat4{};
        }
    }
    case Rig::Wraith: {
        float fade = a.die;
        switch (p.role) {
        case Role::Root:
            return translate({0, m.hover + 0.08f * std::sin(t * 1.7f) + 0.4f * fade, a.lunge * 0.45f - a.hurt * 0.15f}) * rotateX(-a.hurt * 0.3f + a.lunge * 0.25f) *
                   scale({1 + 0.3f * fade, 1 + 0.5f * fade, 1 + 0.3f * fade});
        case Role::Head: return rotateX(-a.hurt * 0.4f - 0.2f * a.windup) * rotateZ(0.1f * std::sin(t * 1.1f));
        case Role::ArmL:
        case Role::ArmR: {
            Motion c = p.role == Role::ArmR ? a : motion(anim, t - 0.07f, 0, 1);
            return rotateX(-0.55f + 0.1f * a.idle - 2.1f * c.windup - 1.0f * c.lunge) * rotateZ(side * (0.25f + 0.1f * a.idle + 0.4f * c.windup));
        }
        case Role::ForeL:
        case Role::ForeR: return rotateX(-0.35f - 0.4f * a.windup + 0.1f * std::sin(t * 1.9f + side));
        case Role::Cloth: return rotateY(p.yaw) * rotateX(-0.1f - 0.12f * std::sin(t * 2.3f + p.phase) + 0.5f * a.lunge * std::cos(p.yaw) - 0.4f * fade);
        default: return Mat4{};
        }
    }
    case Rig::Mimic: {
        float rise = anim == Anim::Idle ? 1 - smooth(t / 0.25f) : smooth(t / 0.12f);
        float open = 1.15f * a.windup + 0.35f * a.lunge + 0.5f * a.hurt + 1.5f * a.die + 0.25f * std::fabs(a.stride);
        switch (p.role) {
        case Role::Root:
            return translate({0, -0.17f * (1 - rise) + 0.05f * std::fabs(a.stride) + 0.06f * a.windup, a.lunge * 0.35f - a.hurt * 0.08f}) *
                   rotateZ(0.12f * a.hurt * std::sin(t * 40) + 0.3f * a.die) * rotateX(-0.15f * a.windup + 0.1f * a.lunge);
        case Role::Head: return rotateX(-open - 0.03f * std::max(0.0f, std::sin(t * 0.9f)));
        case Role::Tongue: {
            float out = std::min(1.0f, open);
            return translate({0, 0.06f * out, 0.42f * out}) * rotateX(out * (0.45f + 0.2f * std::sin(t * 9)));
        }
        case Role::Leg: return rotateX(0.9f * a.stride) * rotateZ((p.pivot.x > 0 ? 1.0f : -1.0f) * 0.8f * a.die);
        default: return Mat4{};
        }
    }
    case Rig::Static: return Mat4{};
    }
    return Mat4{};
}

// Drops every part to the floor with a tumble, as if the joints let go.
void crumble(const Mat4& world, Mat4* xf, size_t n, float t) {
    float k = clamp01(t / (DIE_TIME * 0.75f));
    k *= k;
    for (size_t i = 0; i < n; i++) {
        int id = int(i);
        float h = xf[i].m[13] - world.m[13];
        Vec3 scatter{(hashf(id, 1) - 0.5f) * 0.7f * k, -(h - 0.08f) * k, (hashf(id, 2) - 0.5f) * 0.7f * k};
        xf[i] = translate(scatter) * xf[i] * rotateX((hashf(id, 3) - 0.5f) * 2.8f * k) * rotateZ((hashf(id, 4) - 0.5f) * 2.4f * k);
    }
}

}  // namespace

void buildModels(Renderer& r) { buildAll(r); }

const Model& model(ModelId id) { return models[int(id)]; }

ModelId monsterModel(MonsterKind k) { return ModelId(int(k)); }

ModelId itemModel(ItemKind k) {
    switch (itemClass(k)) {
    case ItemClass::Weapon:
    case ItemClass::Armor: return ModelId(int(ModelId::Dagger) + int(k));
    case ItemClass::Potion: return ModelId::Potion;
    case ItemClass::Scroll: return ModelId::Scroll;
    case ItemClass::Wand: return ModelId::Wand;
    case ItemClass::Ring: return ModelId::Ring;
    case ItemClass::Oil: return ModelId::Oil;
    case ItemClass::Gold: return ModelId::Gold;
    }
    return ModelId::Gold;
}

void drawModel(Renderer& r, ModelId id, const Mat4& world, Anim anim, float t, Vec4 tint) {
    const Model& m = model(id);
    constexpr size_t MAX_PARTS = 48;
    Mat4 xf[MAX_PARTS];
    size_t n = std::min(m.parts.size(), MAX_PARTS);
    Mat4 root = m.size == 1 ? world : world * scale({m.size, m.size, m.size});
    for (size_t i = 0; i < n; i++) {
        const Part& p = m.parts[i];
        xf[i] = (p.parent < 0 ? root : xf[p.parent]) * translate(p.pivot) * pose(m, p, anim, t);
    }
    if (m.crumble && anim == Anim::Die) crumble(world, xf, n, t);
    float fade = m.rig == Rig::Wraith && anim == Anim::Die ? 1 - smooth(t / DIE_TIME) : 1;
    // Solid parts first, so translucent ones blend over them.
    for (int pass = 0; pass < 2; pass++)
        for (size_t i = 0; i < n; i++) {
            const Part& p = m.parts[i];
            Blend blend = p.opaque ? Blend::Opaque : m.blend;
            if ((blend == Blend::Opaque) != (pass == 0)) continue;
            Vec4 c = p.fixedColour ? Vec4{1, 1, 1, tint.w} : tint;
            if (blend != Blend::Opaque) c.w *= m.alpha * fade;
            r.draw(p.mesh, xf[i], c, blend);
        }
}
