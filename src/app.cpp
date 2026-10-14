#include "app.h"
#include "keys.h"
#include "scene.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

constexpr float STEP_TIME = 0.2f, TURN_TIME = 0.16f, BUMP_TIME = 0.18f, BOLT_TIME = 0.3f, DART_TIME = 0.14f;
constexpr float DYING_TIME = 1.0f, CARD_DELAY = 2.2f, SPRING_TIME = 1.2f;
const uint32_t POTION_RGB[8] = {0x8A8A4A, 0xD02838, 0xF0B030, 0x9050D0, 0xE8E8DA, 0xC88838, 0x38B0B0, 0x404050};
const Vec3 UP{0, 1, 0};

Vec4 colourVec(uint32_t rgb) { return {float((rgb >> 16) & 255) / 255, float((rgb >> 8) & 255) / 255, float(rgb & 255) / 255, 1}; }
Vec3 lightOf(uint32_t c) { return Vec3{float(c & 255), float((c >> 8) & 255), float((c >> 16) & 255)} * (1.4f / 255); }
uint32_t withAlpha(uint32_t c, float a) { return (c & 0x00FFFFFF) | uint32_t(clamp01(a) * 255) << 24; }

float hash(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return float(s >> 8) / 16777216.0f;
}

Vertex vertex(Vec3 p, float u, float v, Tex t, uint32_t color) {
    Vertex x;
    x.pos = p, x.u = u, x.v = v, x.tile = float(int(t)), x.color = color;
    return x;
}

// a top-left, b top-right, c bottom-right, d bottom-left. vRow >= 0 samples one texture row.
void quad(std::vector<Vertex>& out, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Tex t, uint32_t color, float vRow = -1) {
    float v0 = vRow < 0 ? 0 : vRow, v1 = vRow < 0 ? 0.999f : vRow;
    Vertex A = vertex(a, 0, v0, t, color), B = vertex(b, 0.999f, v0, t, color), C = vertex(c, 0.999f, v1, t, color), D = vertex(d, 0, v1, t, color);
    out.insert(out.end(), {A, B, C, A, C, D});
}

// A camera-facing quad standing on `base`.
void billboard(std::vector<Vertex>& out, Vec3 base, Vec3 right, Vec3 up, float w, float h, Tex t, uint32_t color) {
    Vec3 half = right * (w / 2), top = up * h;
    quad(out, base - half + top, base + half + top, base + half, base - half, t, color);
}

// A camera-facing strip through `pts`. Soft textures are sampled along their middle row,
// so the strip fades at its edges but stays even along its length.
void ribbon(std::vector<Vertex>& out, const std::vector<Vec3>& pts, Vec3 eye, float width, Tex t, uint32_t color) {
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        Vec3 a = pts[i], b = pts[i + 1];
        Vec3 side = normalize(cross(b - a, eye - (a + b) * 0.5f)) * (width / 2);
        quad(out, a - side, a + side, b + side, b - side, t, color, t == Tex::White ? -1 : 0.5f);
    }
}

void floorQuad(std::vector<Vertex>& out, Vec3 c, float size, Tex t, uint32_t color) {
    float h = size / 2;
    quad(out, c + Vec3{-h, 0, -h}, c + Vec3{h, 0, -h}, c + Vec3{h, 0, h}, c + Vec3{-h, 0, h}, t, color);
}

Mat4 basis(Vec3 x, Vec3 y, Vec3 z, Vec3 at) {
    Mat4 m;
    m.at(0, 0) = x.x, m.at(1, 0) = x.y, m.at(2, 0) = x.z;
    m.at(0, 1) = y.x, m.at(1, 1) = y.y, m.at(2, 1) = y.z;
    m.at(0, 2) = z.x, m.at(1, 2) = z.y, m.at(2, 2) = z.z;
    m.at(0, 3) = at.x, m.at(1, 3) = at.y, m.at(2, 3) = at.z;
    return m;
}

// Draws a static model with part `hinged` turned by `hinge` (door leaves, chest lids).
void drawHinged(Renderer& r, ModelId id, const Mat4& world, int hinged, const Mat4& hinge) {
    const Model& m = model(id);
    Mat4 xf[32];
    for (size_t i = 0; i < m.parts.size() && i < 32; i++) {
        const Part& p = m.parts[i];
        xf[i] = (p.parent < 0 ? world : xf[p.parent]) * translate(p.pivot) * (int(i) == hinged ? hinge : Mat4{});
        r.draw(p.mesh, xf[i], {1, 1, 1, m.alpha}, m.blend);
    }
}

// Chests (and mimics pretending to be chests) turn their backs to the nearest wall.
float wallBackedYaw(const Level& L, Pos p) {
    for (int d = 0; d < 4; d++)
        if (solid(L.tile(p + DIRS[d]))) return yawOf((d + 2) % 4);
    return yawOf((p.x * 7 + p.y * 3) % 4);
}

float animCycle(Anim a) { return a == Anim::Attack ? ATTACK_TIME + 0.4f : a == Anim::Hurt ? HURT_TIME + 0.3f : a == Anim::Die ? DIE_TIME + 0.8f : WALK_TIME * 8; }
float springPop(float t) { return t < 0.06f ? t / 0.06f : t < 0.5f ? 1 : clamp01(1 - (t - 0.5f) / 0.5f); }
float lidCurve(float t) { return t >= 1 ? 1 : smooth(t) + std::sin(t * PI) * 0.15f; }

static_assert(int(MonsterKind::Count) == CREATURE_KINDS);

// Monsters that swing a weapon, so their attack also gets a blade whoosh.
bool armed(MonsterKind k) { return k == MonsterKind::Skeleton || k == MonsterKind::Knight || k == MonsterKind::Ogre; }

// What flies off a monster when it is hit.
Burst goreFor(MonsterKind k) {
    switch (k) {
    case MonsterKind::Slime: return {hex(0x70D040), Tex::White, 0.08f, 1.6f, 2.2f, 9, 1.2f};
    case MonsterKind::Wraith: return {hex(0x7080C0), Tex::Glow, 0.3f, 0.8f, 0.6f, -1.5f, 0.8f, 0, true};
    case MonsterKind::Knight: return {hex(0xFFD080), Tex::Glow, 0.07f, 3.0f, 2.0f, 9, 0.4f, 0, true, true};
    case MonsterKind::Gargoyle: return {hex(0x9A968C), Tex::Stone, 0.09f, 2.0f, 2.5f, 10, 1.4f, 0, false, true};
    case MonsterKind::Mimic: return {hex(0xA07040), Tex::Wood, 0.08f, 2.0f, 2.5f, 10, 1.4f, 0, false, true};
    case MonsterKind::Ghoul: return {hex(0x6A2016), Tex::White, 0.08f, 1.8f, 2.4f, 9, 1.8f};
    case MonsterKind::Skeleton:
    case MonsterKind::Lich: return {hex(0xE0D8C0), Tex::Bone, 0.07f, 2.0f, 2.6f, 10, 1.4f, 0, false, true};
    default: return {hex(0xB01414), Tex::White, 0.08f, 1.8f, 2.4f, 9, 1.8f};
    }
}

// A soft puff of dust, lit as if by the torch.
const Burst DUST{hex(0x40382E), Tex::Glow, 0.5f, 1.2f, 0.5f, -0.2f, 1.1f, 0.8f, true};

uint32_t boltColour(MonsterKind k) { return k == MonsterKind::Imp ? hex(0xFF9040) : k == MonsterKind::Lich ? hex(0x90FFB0) : hex(0xE070FF); }

// Per-theme torch flicker and fog: the crypt is still and clear, the ember deeps restless and hazy.
struct ThemeFeel {
    float flicker, rate, fogStart, fogScale;
};
const ThemeFeel FEEL[] = {{0.05f, 1.0f, 2.0f, 1.2f}, {0.07f, 1.15f, 1.6f, 1.05f}, {0.11f, 1.5f, 1.0f, 0.95f}};

float flickerAt(const ThemeFeel& f, float t) {
    t *= f.rate;
    return 1 + f.flicker * (0.5f * std::sin(t * 11.3f) + 0.3f * std::sin(t * 23.7f + 1.3f) + 0.2f * std::sin(t * 41.1f + 2.1f));
}

// The first-person weapon: grip position in camera space (x right, y up, -z ahead) and
// rotation (x pitch, negative tips the blade forward; y yaw; z lean towards the centre).
struct Pose {
    Vec3 off, rot;
};
Pose mix(const Pose& a, const Pose& b, float t) { return {mix(a.off, b.off, t), mix(a.rot, b.rot, t)}; }

// rest -> wind-up -> strike -> rest. `twist` turns the blade about its own axis so it shows
// an edge to the camera instead of a flat slab.
struct Swing {
    float time, windEnd, strikeEnd, twist;
    Pose rest, wind, strike;
    float impact() const { return time * strikeEnd; }
};

const Swing STAB{0.24f, 0.3f, 0.5f, 0.25f,
                 {{0.19f, -0.21f, -0.58f}, {-0.4f, 0, 0.35f}},
                 {{0.24f, -0.2f, -0.5f}, {-1.0f, 0, 0.15f}},
                 {{0.04f, -0.14f, -0.95f}, {-1.5f, 0.05f, 0}}};
const Swing SLASH{0.34f, 0.28f, 0.55f, 0.9f,
                  {{0.2f, -0.23f, -0.63f}, {-0.4f, 0, 0.35f}},
                  {{0.34f, -0.08f, -0.58f}, {0.1f, 0, -0.6f}},
                  {{-0.1f, -0.34f, -0.72f}, {-1.6f, 0.7f, 0.7f}}};
const Swing CHOP{0.42f, 0.4f, 0.6f, 0.6f,
                 {{0.2f, -0.24f, -0.62f}, {-0.35f, 0, 0.3f}},
                 {{0.26f, -0.02f, -0.6f}, {0.5f, 0, 0.1f}},
                 {{0.04f, -0.38f, -0.8f}, {-1.9f, 0, 0.05f}}};
const Swing THRUST{0.32f, 0.3f, 0.5f, 0,
                   {{0.2f, -0.23f, -0.55f}, {-1.2f, 0, 0.12f}},
                   {{0.23f, -0.24f, -0.4f}, {-1.25f, 0, 0.15f}},
                   {{0.08f, -0.18f, -1.1f}, {-1.5f, 0, 0.02f}}};
const Swing ARC{0.56f, 0.35f, 0.65f, 0.9f,
                {{0.22f, -0.25f, -0.66f}, {-0.3f, 0, 0.4f}},
                {{0.38f, -0.12f, -0.55f}, {-1.2f, -1.1f, 0.3f}},
                {{-0.35f, -0.28f, -0.66f}, {-1.3f, 1.2f, 0.3f}}};

const Swing& swingFor(ItemKind k) {
    switch (k) {
    case ItemKind::Dagger: return STAB;
    case ItemKind::Mace:
    case ItemKind::Axe: return CHOP;
    case ItemKind::Spear: return THRUST;
    case ItemKind::Greatsword: return ARC;
    default: return SLASH;
    }
}

Pose swingPose(const Swing& s, float t) {
    float k = t / s.time;
    if (k >= 1) return s.rest;
    if (k < s.windEnd) return mix(s.rest, s.wind, smooth(k / s.windEnd));
    if (k < s.strikeEnd) {
        float u = (k - s.windEnd) / (s.strikeEnd - s.windEnd);
        return mix(s.wind, s.strike, u * u);
    }
    return mix(s.strike, s.rest, smooth((k - s.strikeEnd) / (1 - s.strikeEnd)));
}

bool isMoveKey(int vk) { return std::string("WASDQE").find(char(vk)) != std::string::npos || (vk >= VK_LEFT && vk <= VK_DOWN); }
bool isTurn(Action a) { return a == Action::TurnLeft || a == Action::TurnRight; }

bool actionFor(int vk, Action& a) {
    switch (vk) {
    case 'W':
    case VK_UP: a = Action::Forward; return true;
    case 'S':
    case VK_DOWN: a = Action::Back; return true;
    case 'A': a = Action::StrafeLeft; return true;
    case 'D': a = Action::StrafeRight; return true;
    case 'Q':
    case VK_LEFT: a = Action::TurnLeft; return true;
    case 'E':
    case VK_RIGHT: a = Action::TurnRight; return true;
    case VK_SPACE:
    case 'F': a = Action::Attack; return true;
    case 'Z':
    case VK_OEM_PERIOD: a = Action::Wait; return true;
    case VK_RETURN: a = Action::Descend; return true;
    default: return false;
    }
}

}  // namespace

App::App(Renderer& renderer, uint32_t s) : r(renderer), seed(s) { startRun(seed); }

void App::startRun(uint32_t s, int depth) {
    seed = s;
    game.start(s, depth);
    hud = {};
    resting = false;
    dyingT = victoryT = -1;
    hitStop = 0;
    pending.clear();
    queued.clear();
    resetView();
}

void App::resetView() {
    monsters.clear();
    doors.clear();
    lids.clear();
    springs.clear();
    bolts.clear();
    particles.clear();
    beams.clear();
    flares.clear();
    floaters.clear();
    snapCamera();
    addViews();
}

void App::snapCamera() {
    camFrom = camTo = cellCenter(game.player.pos);
    camT = turnT = 1;
    bumpT = 10;
    yawFrom = yawTo = yawOf(game.player.facing);
    lastEye = eyePos(), lastYaw = yawTo;
    sway = {};
    meshDirty = true;
}

float App::random() { return hash(fxSeed); }

App::MonsterView* App::view(int id) {
    for (MonsterView& v : monsters)
        if (v.id == id) return &v;
    return nullptr;
}

void App::addViews(const std::vector<Event>& batch) {
    for (const Monster& m : game.level.monsters) {
        if (m.hp <= 0 || view(m.id)) continue;
        MonsterView v;
        v.id = m.id;
        v.kind = m.kind;
        v.from = v.to = cellCenter(m.pos);
        v.dormant = !m.awake && (monsterDef(m.kind).flags & MF_DORMANT);
        v.yaw = v.targetYaw = v.dormant ? wallBackedYaw(game.level, m.pos) : yawOf(m.facing);
        v.animT = float(m.id % 7) * 0.37f;
        auto summons = [&](const Event& e) { return e.type == EventType::MonsterAppear && e.monster == m.id; };
        if (std::any_of(batch.begin(), batch.end(), summons) || std::any_of(pending.begin(), pending.end(), [&](const Pending& p) { return summons(p.event); }))
            v.rise = -1e9f;  // hidden until its summon plays
        monsters.push_back(v);
    }
}

// Drops finished corpses and views of monsters that vanished, and snaps views onto
// monsters that moved without an event.
void App::pruneViews() {
    auto alive = [&](int id) -> const Monster* {
        for (const Monster& m : game.level.monsters)
            if (m.id == id && m.hp > 0) return &m;
        return nullptr;
    };
    for (MonsterView& v : monsters) {
        const Monster* m = v.dead ? nullptr : alive(v.id);
        if (!m) continue;
        Vec3 at = cellCenter(m->pos);
        if (length(at - v.to) > 0.01f) v.from = v.to = at, v.moveT = 1;
        v.dormant = !m->awake && (monsterDef(m->kind).flags & MF_DORMANT);
    }
    std::erase_if(monsters, [&](const MonsterView& v) { return v.dead ? v.deadT > DIE_TIME + 1.5f : !alive(v.id); });
}

Vec3 App::monsterPos(const MonsterView& v) const { return mix(v.from, v.to, smooth(v.moveT)) + v.knock; }
float App::monsterTop(const MonsterView& v) const { return model(monsterModel(v.kind)).height; }

Vec3 App::camNow() const { return mix(camFrom, camTo, smooth(camT)); }

Vec3 App::forward() const {
    if (screen == Screen::Title) return yawVector(time * 0.15f);
    return yawVector(yawFrom + angleDelta(yawFrom, yawTo) * smooth(turnT));
}

Vec3 App::eyePos() const {
    Vec3 f = forward();
    if (screen == Screen::Title) return cellCenter(game.level.start) - f * 0.6f + Vec3{0, EYE_H, 0};
    Vec3 e = camNow() - f * 0.45f + Vec3{0, EYE_H, 0};
    if (camT < 1) e.y += std::sin(camT * PI) * 0.05f;
    if (bumpT < BUMP_TIME) e += bumpDir * (std::sin(bumpT / BUMP_TIME * PI) * 0.14f);
    e.y -= stairDip * 0.28f;
    if (landT < 0.6f) e.y -= (1 - smooth(landT / 0.6f)) * 0.7f;
    return e;
}

// The eye plus everything that tilts it: shake, the dip over stairs and the fall on death.
App::Camera App::camera() const {
    Vec3 e = eyePos(), f = forward(), right = normalize(cross(f, UP));
    float pitch = -stairDip * 0.5f, roll = 0;
    if (dyingT >= 0 && screen != Screen::Title) {
        float k = clamp01(dyingT / DYING_TIME);
        float fall = k * k;
        float bounce = dyingT > DYING_TIME ? std::sin(clamp01((dyingT - DYING_TIME) / 0.25f) * PI) * 0.05f : 0;
        e.y += -fall * (EYE_H - 0.42f) + bounce;
        e += right * (smooth(k) * 0.25f);
        roll = smooth(k) * 1.25f;
        pitch += std::sin(k * PI) * -0.35f + smooth(k) * 0.12f;  // slump forward, then a last look up at the killer
    }
    if (shake > 0) {
        e += right * (std::sin(time * 71) * shake * 0.1f) + UP * (std::sin(time * 53) * shake * 0.07f);
        roll += std::sin(time * 37) * shake * 0.04f;
    }
    f = normalize(f * std::cos(pitch) + UP * std::sin(pitch));
    Vec3 up = cross(right, f);
    return {e, f, up * std::cos(roll) + right * std::sin(roll)};
}

bool App::busy() const {
    return camT < 0.8f || turnT < 0.75f || bumpT < BUMP_TIME * 0.7f || swingT < swingFor(game.player.weapon.kind).time * 0.6f || !pending.empty();
}

// Positional sound. A sound already played this frame is skipped, so a fireball hitting
// five monsters doesn't stack five screams into one.
void App::playAt(Sound sound, Vec3 where, float gain, float pitch) {
    uint64_t bit = uint64_t(1) << (int(sound) & 63);
    if (soundsThisFrame & bit) return;
    soundsThisFrame |= bit;
    Vec3 eye = eyePos(), d = where - eye;
    float dist = length(d);
    Vec3 right = cross(forward(), UP);
    float pan = dist > 0.01f ? dot(d * (1 / dist), right) * 0.8f : 0;
    audio::play(sound, gain * std::clamp(1 - dist / 26, 0.12f, 1.0f), pan, pitch);
}

void App::creatureAt(MonsterKind kind, Cue cue, Vec3 where, float gain) {
    Vec3 d = where - eyePos();
    float dist = length(d), pan = dist > 0.01f ? dot(d * (1 / dist), cross(forward(), UP)) * 0.8f : 0;
    audio::playCreature(int(kind), cue, gain * std::clamp(1 - dist / 26, 0.12f, 1.0f), pan);
}

void App::burst(Vec3 at, int count, const Burst& b, Vec3 drift) {
    for (int i = 0; i < count; i++) {
        float a = random() * 2 * PI, s = b.speed * (0.3f + 0.7f * random());
        Particle p;
        p.pos = at;
        p.vel = Vec3{std::cos(a) * s, b.lift * (0.4f + 0.8f * random()), std::sin(a) * s} + drift;
        p.life = p.maxLife = b.life * (0.7f + 0.6f * random());
        p.size = b.size * (0.7f + 0.6f * random());
        p.gravity = b.gravity, p.grow = b.grow, p.color = b.color, p.tex = b.tex, p.glow = b.glow, p.bounce = b.bounce;
        particles.push_back(p);
    }
    if (particles.size() > 800) particles.erase(particles.begin(), particles.end() - 800);
}

// Blood, bone chips, sparks or ichor, sprayed away from the player.
void App::gore(const MonsterView& v, int count) {
    Vec3 at = monsterPos(v), away = at - cellCenter(game.player.pos);
    away.y = 0;
    burst(at + Vec3{0, monsterTop(v) * 0.55f, 0}, count, goreFor(v.kind), normalize(away) * 1.2f);
}

// Numbers that land on top of fresh ones climb above them instead of overlapping.
void App::addFloat(Vec3 at, std::string text, uint32_t color) {
    for (const Floater& f : floaters)
        if (f.age < 0.5f && std::abs(f.pos.x - at.x) < 0.6f && std::abs(f.pos.z - at.z) < 0.6f) at.y = std::max(at.y, f.pos.y + 0.22f);
    floaters.push_back({at, std::move(text), color, 0});
}

void App::act(Action a) {
    if (game.outcome != Outcome::Playing) return;
    game.act(a);
    handleEvents();
}

// Runs buffered keypresses in order once the move in flight is far enough along.
void App::runQueue() {
    while (!queued.empty() && game.outcome == Outcome::Playing) {
        Action a = queued.front();
        if (!pending.empty() || (!isTurn(a) && busy())) return;
        queued.erase(queued.begin());
        act(a);
    }
}

// Events normally play at once. After a melee swing, everything that follows waits until
// the blade lands, so hits, deaths and the monsters' replies happen on impact.
// A trap stepped on springs as the step lands rather than as it starts.
void App::handleEvents() {
    std::vector<Event> batch = game.takeEvents();
    addViews(batch);
    float delay = pending.empty() ? 0 : pending.back().delay;
    bool stepped = false;
    for (const Event& e : batch) {
        if (e.type == EventType::PlayerAttack && delay <= 0) {
            startSwing();
            delay = swingFor(game.player.weapon.kind).impact();
        }
        if (e.type == EventType::TrapTriggered && stepped) delay = std::max(delay, STEP_TIME * 0.7f);
        stepped = stepped || e.type == EventType::PlayerStep;
        if (delay > 0) pending.push_back({delay, e});
        else present(e);
    }
}

void App::startSwing() {
    const Swing& s = swingFor(game.player.weapon.kind);
    swingT = 0;
    audio::play(Sound::Swing, 0.7f, 0.2f, std::clamp(0.34f / s.time, 0.75f, 1.3f) * (0.92f + 0.16f * float(game.turn % 5) / 5));
}

void App::present(const Event& e) {
    MonsterView* v = e.monster >= 0 ? view(e.monster) : nullptr;
    Vec3 at = cellCenter(e.to);
    Camera cam = camera();
    switch (e.type) {
    case EventType::PlayerStep:
        camFrom = camNow(), camTo = at, camT = 0;
        audio::play(Sound::Step, 0.7f, 0, 0.85f + 0.3f * float((game.turn * 7) % 10) / 10);
        break;
    case EventType::PlayerTurn:
        yawFrom = yawFrom + angleDelta(yawFrom, yawTo) * smooth(turnT), yawTo = yawOf(e.amount), turnT = 0;
        break;
    case EventType::PlayerBlocked:
        bumpDir = normalize(cellCenter(e.to) - cellCenter(e.from)), bumpT = 0;
        audio::play(Sound::Bump, 0.8f);
        break;
    case EventType::PlayerAttack:
        meleeTarget = e.amount > 0 ? e.monster : -1;
        meleeCrit = e.crit;
        if (!v) break;
        if (e.amount > 0) {
            creatureAt(v->kind, Cue::Impact, at, 1.0f);
        } else {
            addFloat(monsterPos(*v) + Vec3{0, monsterTop(*v) * 0.8f, 0}, "miss", hex(0xB8B0A0));
            v->knock = normalize(cross(cam.forward, UP)) * (game.turn % 2 ? 0.18f : -0.18f);
        }
        break;
    case EventType::PlayerHurt: {
        float frac = float(e.amount) / float(std::max(1, game.player.maxHp));
        bool slight = e.monster < 0 && e.amount <= 2;
        hud.hurt = 1;
        shake = std::max(shake, slight ? 0.08f : 0.15f + frac * 1.6f);
        flash = slight ? Vec4{0.25f, 0.5f, 0.1f, 0.2f} : Vec4{0.6f, 0.0f, 0.0f, std::min(0.6f, 0.2f + frac * 1.5f)};
        flinchT = 0;
        hud.hurtFrom = -1;
        for (const Monster& m : game.level.monsters)
            if (m.id == e.monster) hud.hurtFrom = (dirTowards(game.player.pos, m.pos) - game.player.facing + 4) % 4;
        audio::play(Sound::PlayerHurt, slight ? 0.5f : 0.9f);
        break;
    }
    case EventType::PlayerHeal:
        flash = {0.3f, 0.8f, 0.4f, 0.25f};
        burst(cam.eye + cam.forward * 0.8f - UP * 0.6f, 14, {hex(0x80FF90), Tex::Glow, 0.1f, 0.6f, 1.2f, -0.8f, 0.9f, 0, true});
        break;
    case EventType::PlayerLevelUp:
        hud.levelUp = 2.5f;
        burst(cam.eye + cam.forward * 0.9f - UP * 0.7f, 24, {hex(0xFFD060), Tex::Glow, 0.1f, 1.0f, 1.6f, -0.6f, 1.2f, 0, true});
        audio::play(Sound::LevelUp, 0.8f);
        break;
    case EventType::PlayerDied:
        dyingT = 0;
        resting = false;
        queued.clear();
        audio::play(Sound::Death, 1.0f);
        break;
    case EventType::MonsterNotice:
        if (v) creatureAt(v->kind, Cue::Notice, cellCenter(e.from), 0.8f);
        if (v) addFloat(monsterPos(*v) + Vec3{0, monsterTop(*v) + 0.1f, 0}, "!", hex(0xF0A030));
        break;
    case EventType::MonsterStep:
        if (!v) break;
        v->from = mix(v->from, v->to, smooth(v->moveT)), v->to = at, v->moveT = 0;
        v->targetYaw = std::atan2(at.x - v->from.x, at.z - v->from.z);
        if (v->anim != Anim::Attack || v->animT > ATTACK_TIME) v->anim = Anim::Walk, v->animT = 0;
        if (manhattan(e.to, game.player.pos) <= 10) creatureAt(v->kind, Cue::Step, at, 0.35f);
        break;
    case EventType::MonsterAttack:
    case EventType::MonsterBolt: {
        if (!v) break;
        Vec3 me = cellCenter(game.player.pos);
        v->anim = Anim::Attack, v->animT = 0;
        v->targetYaw = std::atan2(me.x - v->to.x, me.z - v->to.z);
        if (e.type == EventType::MonsterBolt) {
            Vec3 target = cam.eye + cam.forward * 0.6f - cam.up * 0.2f;
            if (e.amount == 0) target += normalize(cross(cam.forward, UP)) * 0.9f;
            bolts.push_back({v->to + Vec3{0, 1.4f, 0}, target, -0.15f, BOLT_TIME, boltColour(v->kind)});
            playAt(Sound::Bolt, v->to, 0.8f);
            creatureAt(v->kind, Cue::Attack, v->to, 0.7f);
        } else {
            creatureAt(v->kind, Cue::Attack, v->to, 0.7f);
            if (armed(v->kind)) playAt(Sound::Swing, v->to, 0.5f, 0.75f);
        }
        break;
    }
    case EventType::MonsterHurt: {
        if (!v) break;
        bool crit = meleeCrit && meleeTarget == e.monster;
        Vec3 away = monsterPos(*v) - cellCenter(game.player.pos);
        away.y = 0;
        v->flash = 1;
        v->knock = normalize(away) * (crit ? 0.4f : 0.25f);
        if (!v->dead) v->anim = Anim::Hurt, v->animT = 0;
        gore(*v, crit ? 18 : 10);
        addFloat(monsterPos(*v) + Vec3{(random() - 0.5f) * 0.4f, monsterTop(*v) * 0.85f, 0}, std::to_string(e.amount) + (crit ? "!" : ""),
                 crit ? hex(0xFFD040) : hex(0xF0E8D8));
        if (crit) shake = std::max(shake, 0.2f);
        if (meleeTarget == e.monster) meleeTarget = -1;
        creatureAt(v->kind, Cue::Hurt, v->to, 0.8f);
        break;
    }
    case EventType::MonsterDied:
        if (!v) break;
        v->dead = true, v->deadT = 0, v->anim = Anim::Die, v->animT = 0, v->dormant = false;
        gore(*v, 24);
        hitStop = 0.09f;
        creatureAt(v->kind, Cue::Die, v->to, 0.95f);
        break;
    case EventType::DoorOpen:
        doors.push_back({cellIndex(e.to), 0.0f});
        playAt(Sound::Door, at, 0.8f);
        break;
    case EventType::Pickup: audio::play(e.amount == int(ItemKind::Gold) ? Sound::Gold : Sound::Pickup, 0.7f); break;
    case EventType::UseItem: {
        ItemClass c = itemClass(ItemKind(e.amount));
        if (c == ItemClass::Oil) torchBoost = 1;
        Sound s = c == ItemClass::Potion ? Sound::Drink : c == ItemClass::Scroll ? Sound::Scroll : c == ItemClass::Oil ? Sound::Fire : Sound::Equip;
        if (c != ItemClass::Wand) audio::play(s, c == ItemClass::Oil ? 0.5f : 0.8f);
        break;
    }
    case EventType::Teleport:
        if (blinking) {
            camFrom = camNow(), camTo = at, camT = 0;
        } else {
            camFrom = camTo = at, camT = 1;
            flash = {0.8f, 0.7f, 1.0f, 0.8f};
            audio::play(Sound::Teleport, 0.9f);
        }
        blinking = false;
        break;
    case EventType::Descend:
        resetView();
        brightness = 0;
        audio::play(Sound::Stairs, 0.9f);
        break;
    case EventType::Victory:
        victoryT = 0;
        audio::play(Sound::Victory, 1.0f);
        break;
    case EventType::TrapTriggered: presentTrap(e, v); break;
    case EventType::TrapFound:
        if (manhattan(e.to, game.player.pos) > 8) break;  // a scroll of mapping reveals them all at once
        burst(at + Vec3{0, 0.05f, 0}, 1, {hex(0xFFF0C0), Tex::Glow, 1.0f, 0, 0, 0, 0.9f, -0.9f, true});
        burst(at + Vec3{0, 0.1f, 0}, 10, {hex(0xFFE0A0), Tex::Glow, 0.08f, 0.6f, 1.2f, -0.4f, 1.0f, 0, true});
        playAt(Sound::TrapFound, at, 0.9f);
        break;
    case EventType::SecretFound:
        meshDirty = true;
        if (manhattan(e.to, game.player.pos) > 8) break;
        burst(at + Vec3{0, 2.3f, 0}, 30, {hex(0x8A8278), Tex::White, 0.09f, 0.7f, 0, 2.0f, 1.6f, 0.6f});
        burst(at + Vec3{0, 0.2f, 0}, 14, DUST);
        shake = std::max(shake, 0.25f);
        playAt(Sound::Secret, at, 1.0f);
        break;
    case EventType::ChestOpened:
        lids.push_back({cellIndex(e.to), 0.0f});
        burst(at + Vec3{0, 0.5f, 0}, 18, {hex(0xFFD060), Tex::Glow, 0.09f, 0.5f, 1.8f, -0.3f, 1.1f, 0, true});
        flares.push_back({at + Vec3{0, 0.8f, 0}, {1.4f, 1.1f, 0.4f}, 4, 0, 0.8f});
        playAt(Sound::Chest, at, 0.9f);
        break;
    case EventType::FountainUsed:
        burst(at + Vec3{0, 0.6f, 0}, 40, {hex(0x90C0FF), Tex::White, 0.08f, 1.5f, 3.2f, 9, 0.8f});
        burst(at + Vec3{0, 0.6f, 0}, 12, {hex(0x80B0FF), Tex::Glow, 0.25f, 0.8f, 2.0f, 6, 0.6f, 0, true});
        flares.push_back({at + Vec3{0, 0.9f, 0}, {0.4f, 0.7f, 1.4f}, 5, 0, 0.8f});
        playAt(Sound::Fountain, at, 0.9f);
        break;
    case EventType::WandZap: presentZap(e); break;
    case EventType::MonsterAppear:
        if (!v) break;
        if (e.from == e.to && (monsterDef(v->kind).flags & MF_DORMANT)) {
            Vec3 me = cellCenter(game.player.pos);
            v->dormant = false, v->anim = Anim::Attack, v->animT = 0, v->flash = 0.6f;
            v->targetYaw = std::atan2(me.x - v->to.x, me.z - v->to.z);
            burst(at + Vec3{0, 0.5f, 0}, 16, DUST);
            shake = std::max(shake, 0.2f);
            creatureAt(v->kind, Cue::Notice, at, 1.0f);
        } else {
            v->from = v->to = at, v->moveT = 1;  // summoned here, or the Lich blinking away
            v->rise = 0;
            burst(at + Vec3{0, 0.2f, 0}, 18, {hex(0xA060FF), Tex::Glow, 0.18f, 0.9f, 1.4f, -0.5f, 0.9f, 0.5f, true});
            flares.push_back({at + Vec3{0, 1.0f, 0}, {1.0f, 0.4f, 1.5f}, 5, 0, 0.7f});
            playAt(Sound::Summon, at, 0.8f);
        }
        break;
    case EventType::Explosion: {
        float radius = (float(e.amount) + 0.5f) * CELL;
        Vec3 c = at + Vec3{0, 0.9f, 0};
        burst(c, 36, {hex(0xFFB060), Tex::Flame, 0.4f, radius * 2.0f, 1.2f, -1.0f, 0.55f, 0.8f, true});
        burst(c, 20, {hex(0xFF6020), Tex::Glow, 0.7f, radius * 1.2f, 0.8f, -0.5f, 0.7f, 0.6f, true});
        flares.push_back({c, {2.4f, 1.2f, 0.4f}, radius * 2.2f, 0, 0.8f});
        flash = {1.0f, 0.55f, 0.15f, 0.4f};
        shake = std::max(shake, 0.55f);
        playAt(Sound::Fire, c, 1.0f);
        break;
    }
    case EventType::Fall:
        resetView();
        brightness = 0;
        landT = 0;
        shake = 0.5f;
        audio::play(Sound::Fall, 1.0f);
        break;
    default: break;
    }
}

void App::presentTrap(const Event& e, MonsterView* v) {
    Vec3 c = cellCenter(e.to);
    Camera cam = camera();
    Vec3 victim = v ? monsterPos(*v) + Vec3{0, monsterTop(*v) * 0.5f, 0} : cam.eye + cam.forward * 0.5f - UP * 0.35f;
    TrapKind kind = TrapKind(e.amount);
    if (kind == TrapKind::Alarm) playAt(Sound::Alarm, c, 1.0f);
    else playAt(Sound::Trap, c, 0.9f, kind == TrapKind::Dart ? 1.4f : 1.0f);
    switch (kind) {
    case TrapKind::Spikes:
        springs.push_back({cellIndex(e.to), 0.0f});
        if (!v) shake = std::max(shake, 0.4f);
        break;
    case TrapKind::Dart: {
        int wall = 0;
        while (wall < 3 && !solid(game.level.tile(e.to + DIRS[wall]))) wall++;
        bolts.push_back({c + dirVector(wall) * (CELL * 0.5f) + Vec3{0, 1.0f, 0}, victim, 0, DART_TIME, 0, true});
        break;
    }
    case TrapKind::Alarm:
        flash = {1.0f, 0.7f, 0.2f, 0.3f};
        flares.push_back({c + Vec3{0, 1.2f, 0}, {1.6f, 0.9f, 0.2f}, 8, 0, 1.4f});
        break;
    case TrapKind::Pit:
        burst(c + Vec3{0, 0.1f, 0}, 16, DUST);
        if (v) v->dead = true, v->deadT = DIE_TIME, v->sink = 5, v->anim = Anim::Hurt, v->animT = 0;
        break;
    case TrapKind::Teleport:
        burst(c + Vec3{0, 0.2f, 0}, 20, {hex(0xB070FF), Tex::Glow, 0.14f, 0.7f, 2.0f, -0.5f, 0.9f, 0, true});
        flares.push_back({c + Vec3{0, 1.0f, 0}, {1.1f, 0.5f, 1.6f}, 6, 0, 0.8f});
        break;
    default: break;
    }
}

void App::presentZap(const Event& e) {
    ItemKind kind = ItemKind(e.amount);
    Camera cam = camera();
    Vec3 right = normalize(cross(cam.forward, cam.up));
    Vec3 start = cam.eye + cam.forward * 0.9f + right * 0.15f - cam.up * 0.25f;
    Vec3 end = cellCenter(e.to) + Vec3{0, 1.1f, 0};
    if (e.to == e.from) end = start + cam.forward * 1.2f;
    else if (kind != ItemKind::WandBlink && !game.level.monsterAt(e.to)) end += normalize(cellCenter(e.to) - cellCenter(e.from)) * (CELL * 0.48f);

    Vec3 light = kind == ItemKind::WandSlow ? Vec3{1.2f, 0.4f, 1.6f} : kind == ItemKind::WandBlink ? Vec3{1.4f, 1.4f, 1.6f} : Vec3{0.9f, 1.0f, 1.7f};
    beams.push_back({start, end, 0, kind == ItemKind::WandSlow ? 0.55f : 0.35f, kind});
    flares.push_back({mix(start, end, 0.5f), light, 8, 0, 0.35f});
    flares.push_back({end, light, 5, 0, 0.45f});
    burst(end, 12, {kind == ItemKind::WandSlow ? hex(0xC080FF) : hex(0xD0E0FF), Tex::Glow, 0.14f, 2.5f, 1.5f, 3, 0.4f, 0, true});
    if (kind == ItemKind::WandBlink) flash = {0.9f, 0.95f, 1.0f, 0.55f}, blinking = true;
    if (kind == ItemKind::WandLightning) flash = {0.7f, 0.8f, 1.0f, 0.3f}, shake = std::max(shake, 0.15f);
    playAt(Sound::Zap, end, 0.9f, kind == ItemKind::WandSlow ? 0.7f : kind == ItemKind::WandBlink ? 1.3f : 1.0f);
}

void App::key(int vk) {
    switch (screen) {
    case Screen::Title: titleKey(vk); return;
    case Screen::Inventory: inventoryKey(vk); return;
    case Screen::Map:
        if (vk == 'M' || vk == VK_ESCAPE || vk == VK_TAB) screen = Screen::Play;
        return;
    case Screen::Help:
        if (vk == VK_ESCAPE || vk == 'H' || vk == VK_F1 || vk == VK_RETURN) screen = helpReturn;
        else if (vk == 'Q' && helpReturn == Screen::Play) screen = Screen::Title, startRun(seed * 22695477u + 1);
        return;
    case Screen::Credits:
        if (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_SPACE) screen = Screen::Title;
        return;
    case Screen::Dead:
    case Screen::Won:
        if ((vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_SPACE) && screenT > 2) screen = Screen::Title, startRun(seed * 22695477u + 1);
        return;
    case Screen::Play: break;
    }

    resting = false;
    if (game.outcome != Outcome::Playing) return;
    if (isMoveKey(vk) && std::find(held.begin(), held.end(), vk) == held.end()) held.push_back(vk), holdT = 0;
    switch (vk) {
    case 'I':
    case VK_TAB: {
        std::vector<int> order = ui::packOrder(game);
        invSelected = order.empty() ? 0 : order[0];
        screen = Screen::Inventory, queued.clear();
        return;
    }
    case 'M': screen = Screen::Map, queued.clear(); return;
    case 'H':
    case VK_F1:
    case VK_ESCAPE: helpReturn = Screen::Play, screen = Screen::Help, queued.clear(); return;
    case 'R': resting = true, restTurns = 0, queued.clear(); return;
    default: break;
    }

    Action a;
    if (!actionFor(vk, a)) return;
    if (queued.size() >= 2) queued.pop_back();
    queued.push_back(a);
    runQueue();
}

void App::keyUp(int vk) { std::erase(held, vk); }

void App::titleKey(int vk) {
    int n = ui::TITLE_MENU_ITEMS;
    if (vk == 'W' || vk == VK_UP) titleMenu = (titleMenu + n - 1) % n, audio::play(Sound::MenuMove, 0.6f);
    else if (vk == 'S' || vk == VK_DOWN) titleMenu = (titleMenu + 1) % n, audio::play(Sound::MenuMove, 0.6f);
    else if (vk == 'H' || vk == VK_F1) helpReturn = Screen::Title, screen = Screen::Help;
    else if (vk == VK_ESCAPE) quit = true;
    else if (vk == VK_RETURN || vk == VK_SPACE) {
        audio::play(Sound::MenuSelect, 0.7f);
        if (titleMenu == 0) startRun(seed), screen = Screen::Play;
        else if (titleMenu == 1) helpReturn = Screen::Title, screen = Screen::Help;
        else if (titleMenu == 2) screen = Screen::Credits;
        else quit = true;
    }
}

void App::showScreen(Screen s) {
    screen = s;
    screenT = 5;
    if (s == Screen::Dead) dyingT = CARD_DELAY;
    if (s == Screen::Inventory) {
        std::vector<int> order = ui::packOrder(game);
        invSelected = order.empty() ? 0 : order[0];
    }
}

// The pack lists slots grouped by class (ui::packOrder); `invSelected` is a slot, rows move through that order.
void App::inventoryKey(int vk) {
    std::vector<int> order = ui::packOrder(game);
    int n = int(order.size());
    int row = int(std::find(order.begin(), order.end(), invSelected) - order.begin());
    switch (vk) {
    case 'W':
    case VK_UP: row = (row + n - 1) % std::max(1, n), audio::play(Sound::MenuMove, 0.6f); break;
    case 'S':
    case VK_DOWN: row = (row + 1) % std::max(1, n), audio::play(Sound::MenuMove, 0.6f); break;
    case VK_RETURN:
    case 'E':
        if (game.use(invSelected)) {
            handleEvents();
            screen = Screen::Play;
            return;
        }
        break;
    case 'X':
        if (game.drop(invSelected)) handleEvents();
        break;
    case 'I':
    case VK_TAB:
    case VK_ESCAPE: screen = Screen::Play; return;
    default:
        for (int i = 0; i < n; i++)
            if (vk == std::toupper(ui::PACK_LETTERS[i])) row = i;
        break;
    }
    order = ui::packOrder(game);
    row = std::clamp(row, 0, std::max(0, int(order.size()) - 1));
    invSelected = order.empty() ? 0 : order[size_t(row)];
}

void App::update(float dt) {
    time += dt;
    screenT += dt;
    soundsThisFrame = 0;
    bool over = screen == Screen::Dead || screen == Screen::Won;
    audio::setAmbience(screen == Screen::Title || screen == Screen::Credits ? 4 : over ? -1 : game.level.depth == FINAL_DEPTH ? 3 : int(game.level.theme));
    float world = hitStop > 0 ? dt * 0.1f : dt;  // a kill freezes the world for a beat
    hitStop = std::max(0.0f, hitStop - dt);
    hud.time = time;
    hud.hurt = std::max(0.0f, hud.hurt - dt * 1.5f);
    hud.levelUp = std::max(0.0f, hud.levelUp - dt);
    hud.resting = resting;
    holdT += dt;
    swingT += world;
    updateCamera(dt);
    updateMonsters(world);
    updateEffects(world);
    updatePending(dt);
    addViews();
    if (pending.empty()) pruneViews();

    bool inRun = screen == Screen::Play || screen == Screen::Inventory || screen == Screen::Map || (screen == Screen::Help && helpReturn == Screen::Play);
    if (game.outcome == Outcome::Dead && dyingT < 0 && pending.empty()) dyingT = 0;
    if (game.outcome == Outcome::Won && victoryT < 0 && pending.empty()) victoryT = 0;
    if (inRun && dyingT > CARD_DELAY) screen = Screen::Dead, screenT = 0;
    if (inRun && victoryT > 2.5f) screen = Screen::Won, screenT = 0;

    if ((screen == Screen::Play || screen == Screen::Inventory) && scriptPos < debug.keys.size()) {
        scriptT += dt;
        if (scriptT > 0.3f && !busy()) {
            scriptT = 0;
            char c = debug.keys[scriptPos++];
            int vk = c == 'f' ? VK_SPACE : c == '>' ? VK_RETURN : c == '.' ? 'Z' : c == '<' ? VK_ESCAPE : int(std::toupper(c));
            key(vk);
            keyUp(vk);
        }
    }
    if (screen != Screen::Play) {
        queued.clear();
        return;
    }

    if (resting && !busy()) {
        bool threatened = std::any_of(game.level.monsters.begin(), game.level.monsters.end(), [&](const Monster& m) {
            bool disguised = !m.awake && (monsterDef(m.kind).flags & MF_DORMANT);  // resting must not give a mimic away
            return game.level.at(m.pos).visible && !disguised;
        });
        if (threatened || game.player.hp >= game.player.maxHp || restTurns >= 300 || game.outcome != Outcome::Playing) {
            resting = false;
        } else {
            int hp = game.player.hp;
            for (int i = 0; i < 4 && resting; i++, restTurns++) {
                game.act(Action::Wait);
                handleEvents();
                if (game.player.hp < hp) resting = false;
            }
        }
    }

    runQueue();
    Action a;
    if (!held.empty() && holdT > 0.2f && queued.empty() && !busy() && game.outcome == Outcome::Playing && actionFor(held.back(), a)) {
        size_t logged = game.log.size();
        act(a);
        if (game.log.size() > logged && !busy()) held.pop_back();  // refused ("in the way"): don't repeat the message every frame
    }
}

void App::updateCamera(float dt) {
    camT = std::min(1.0f, camT + dt / STEP_TIME);
    turnT = std::min(1.0f, turnT + dt / TURN_TIME);
    bumpT += dt, landT += dt, flinchT += dt;
    shake = std::max(0.0f, shake - dt * 1.6f);
    brightness = std::min(1.0f, brightness + dt * 1.2f);
    flash.w = std::max(0.0f, flash.w - dt * 2);
    torchBoost = std::max(0.0f, torchBoost - dt * 0.4f);
    if (victoryT >= 0) victoryT += dt;
    if (dyingT >= 0) {
        bool falling = dyingT < DYING_TIME;
        dyingT += dt;
        if (falling && dyingT >= DYING_TIME) shake = 0.35f, audio::play(Sound::Bump, 0.9f, 0, 0.6f);
    }
    stairDip += ((game.onStairs() && camT >= 1 ? 1.0f : 0.0f) - stairDip) * std::min(1.0f, dt * 6);

    // The hands lag behind the camera, drifting against turns and steps.
    Vec3 eye = eyePos(), f = forward(), right = normalize(cross(f, UP));
    float yaw = std::atan2(f.x, f.z), inv = 1 / std::max(dt, 0.001f);
    Vec3 vel = (eye - lastEye) * inv;
    float yawVel = angleDelta(lastYaw, yaw) * inv;
    lastEye = eye, lastYaw = yaw;
    Vec3 target{std::clamp(yawVel * 0.005f - dot(vel, right) * 0.004f, -0.07f, 0.07f), 0, std::clamp(dot(vel, f) * 0.004f, -0.05f, 0.05f)};
    sway += (target - sway) * std::min(1.0f, dt * 10);
}

void App::updateMonsters(float dt) {
    for (MonsterView& v : monsters) {
        v.moveT = std::min(1.0f, v.moveT + dt / STEP_TIME);
        if (!v.dormant) v.yaw += angleDelta(v.yaw, v.targetYaw) * std::min(1.0f, dt * 12);
        v.animT += dt;
        v.flash = std::max(0.0f, v.flash - dt * 4);
        v.knock = v.knock * std::max(0.0f, 1 - dt * 7);
        v.rise = std::min(1.0f, v.rise + dt / 0.7f);
        if (v.dead) v.deadT += dt;
        bool oneShotDone = (v.anim == Anim::Attack && v.animT > ATTACK_TIME) || (v.anim == Anim::Hurt && v.animT > HURT_TIME) ||
                           (v.anim == Anim::Walk && v.moveT >= 1);
        if (!v.dead && oneShotDone) v.anim = Anim::Idle, v.animT = 0;
    }
}

void App::updateEffects(float dt) {
    for (auto& d : doors) d.second = std::min(1.0f, d.second + dt * 2.2f);
    for (auto& l : lids) l.second = std::min(1.0f, l.second + dt * 2.2f);
    for (auto& s : springs) s.second += dt;
    std::erase_if(springs, [](const auto& s) { return s.second > SPRING_TIME; });

    for (Bolt& b : bolts) {
        b.t += dt / b.time;
        if (b.t >= 1 && !b.dart) burst(b.to, 8, {b.color, Tex::Glow, 0.08f, 1.5f, 1.0f, 2, 0.3f, 0, true});
    }
    std::erase_if(bolts, [](const Bolt& b) { return b.t >= 1; });

    for (Particle& p : particles) {
        p.vel.y -= p.gravity * dt;
        p.pos += p.vel * dt;
        p.life -= dt;
        if (p.pos.y < 0.02f && p.gravity > 0) {
            p.pos.y = 0.02f;
            p.vel = p.bounce && p.vel.y < -1.2f ? Vec3{p.vel.x * 0.5f, -p.vel.y * 0.35f, p.vel.z * 0.5f} : Vec3{};
        }
    }
    std::erase_if(particles, [](const Particle& p) { return p.life <= 0; });
    for (Beam& b : beams) b.age += dt;
    std::erase_if(beams, [](const Beam& b) { return b.age >= b.life; });
    for (Flare& f : flares) f.age += dt;
    std::erase_if(flares, [](const Flare& f) { return f.age >= f.life; });
    for (Floater& f : floaters) f.age += dt;
    std::erase_if(floaters, [](const Floater& f) { return f.age >= FLOAT_TEXT_LIFE; });

    // Fountains that still hold water glitter now and then.
    for (const Decor& d : game.level.decor)
        if (d.kind == DecorKind::Fountain && !d.used && manhattan(d.pos, game.player.pos) < 10 && random() < dt * 4) {
            float a = random() * 2 * PI;
            burst(cellCenter(d.pos) + Vec3{std::cos(a) * 0.4f, 0.5f, std::sin(a) * 0.4f}, 1, {hex(0xA0D0FF), Tex::Glow, 0.07f, 0.05f, 0.3f, 0, 0.8f, 0, true});
        }
}

void App::updatePending(float dt) {
    for (Pending& p : pending) p.delay -= dt;
    while (!pending.empty() && pending.front().delay <= 0) {
        Event e = pending.front().event;
        pending.erase(pending.begin());
        addViews();
        present(e);
    }
}

void App::render() {
    if (debug.viewer >= 0) {
        drawViewer();
        return;
    }
    drawWorld();
    switch (screen) {
    case Screen::Title: ui::title(r, time, titleMenu); break;
    case Screen::Play: ui::hud(r, game, hud); break;
    case Screen::Inventory:
        ui::hud(r, game, hud);
        ui::inventory(r, game, invSelected);
        break;
    case Screen::Map: ui::automap(r, game, time); break;
    case Screen::Help: ui::help(r, helpReturn == Screen::Play); break;
    case Screen::Credits: ui::credits(r, time); break;
    case Screen::Dead: ui::death(r, game, screenT); break;
    case Screen::Won: ui::victory(r, game, screenT); break;
    }
}

void App::drawWorld() {
    const Level& L = game.level;
    const ThemeLook& look = themeLook(L.theme);
    const ThemeFeel& feel = FEEL[std::min(int(L.theme), 2)];
    Camera c = camera();
    Vec3 right = normalize(cross(c.forward, c.up));
    float flicker = flickerAt(feel, time);
    int radius = screen == Screen::Title ? 5 : game.torchRadius();
    bool lit = game.player.torch > 0 || screen == Screen::Title;
    if (meshDirty) r.setLevel(buildLevelMesh(L)), meshDirty = false;

    SceneParams p;
    p.view = lookAt(c.eye, c.eye + c.forward, c.up);
    p.proj = perspective(65 * PI / 180, 4.0f / 3, 0.08f, 80);
    p.eye = c.eye;
    p.fogColor = look.fog;
    p.fogStart = lit ? feel.fogStart : 1;
    p.fogEnd = 6 + float(radius) * CELL * feel.fogScale;
    p.ambient = look.ambient;
    p.lights = levelLights(L, time);
    if (lit)
        p.lights.push_back({c.eye - right * 0.35f + c.forward * 0.3f, (float(radius) * CELL + 1.5f) * flicker * (1 + torchBoost * 0.4f),
                            look.torch * flicker * (1 + torchBoost * 0.3f)});
    else
        p.lights.push_back({c.eye + c.forward * 0.3f, 3.5f, Vec3{0.25f, 0.25f, 0.35f}});
    for (const Decor& d : L.decor)
        if (d.kind == DecorKind::Fountain && !d.used && manhattan(d.pos, game.player.pos) < 14)
            p.lights.push_back({cellCenter(d.pos) + Vec3{0, 0.9f, 0}, 4.5f, Vec3{0.2f, 0.42f, 0.8f} * (0.9f + 0.1f * std::sin(time * 2.3f))});
    for (const Bolt& b : bolts)
        if (b.t > 0 && !b.dart) p.lights.push_back({mix(b.from, b.to, b.t), 6, lightOf(b.color)});
    for (const Flare& f : flares) {
        float k = 1 - f.age / f.life;
        p.lights.push_back({f.pos, f.radius, f.color * (k * k)});
    }
    r.beginScene(p);
    r.drawLevel();

    std::vector<Vertex> glow, matte;
    drawProps(c, glow, matte);
    drawMonsters(c.eye);
    drawEffects(c, glow, matte);
    r.drawVerts(matte, Mat4{}, {1, 1, 1, 1}, Blend::Alpha, false);
    r.drawVerts(glow, Mat4{}, {1, 1, 1, 1}, Blend::Add, true);
    projectFloats(p.proj * p.view);

    if (screen != Screen::Title) {
        r.beginViewmodel(perspective(55 * PI / 180, 4.0f / 3, 0.02f, 10));
        drawViewmodel(c);
    }
    r.endScene(brightness, flash);
}

void App::drawProps(const Camera& c, std::vector<Vertex>& glow, std::vector<Vertex>& matte) {
    const Level& L = game.level;
    Pos me = game.player.pos;
    Vec3 right = normalize(cross(c.forward, UP)), ahead = cross(UP, right);  // world billboards stay upright when the camera rolls
    auto inRange = [&](Pos q, int cells) { return std::abs(q.x - me.x) <= cells && std::abs(q.y - me.y) <= cells; };

    // Doors and traps, from the cells around the player.
    for (int y = std::max(0, me.y - 14); y <= std::min(L.height - 1, me.y + 14); y++)
        for (int x = std::max(0, me.x - 14); x <= std::min(L.width - 1, me.x + 14); x++) {
            Pos q{x, y};
            const Cell& cell = L.at(q);
            Vec3 cc = cellCenter(q);
            if (cell.tile == Tile::Door) {
                float open = 0;
                if (cell.doorOpen) {
                    open = 1;
                    for (auto& d : doors)
                        if (d.first == cellIndex(q)) open = smooth(d.second);
                }
                bool spansX = solid(L.tile({x - 1, y})) && solid(L.tile({x + 1, y}));
                drawHinged(r, ModelId::Door, translate(cc) * rotateY(spansX ? 0 : PI / 2), 1, rotateY(-open * 1.7f));
            }
            float fired = -1;
            for (auto& s : springs)
                if (s.first == cellIndex(q)) fired = s.second;
            if (cell.trap == TrapKind::None || (!cell.trapFound && fired < 0)) continue;
            if (cell.trap == TrapKind::Pit) {
                floorQuad(matte, cc + Vec3{0, 0.015f, 0}, 1.5f, Tex::White, hex(0x040303));
            } else if (cell.trap == TrapKind::Teleport) {
                floorQuad(glow, cc + Vec3{0, 0.02f, 0}, 2.2f, Tex::Glow, withAlpha(hex(0xA050FF), 0.75f + 0.25f * std::sin(time * 3)));
            } else {
                float pop = cell.trap == TrapKind::Spikes && fired >= 0 ? springPop(fired) : 0;
                Vec4 tint = cell.trap == TrapKind::Alarm ? Vec4{1.3f, 1.05f, 0.55f, 1} : cell.trap == TrapKind::Dart ? Vec4{0.75f, 0.75f, 0.8f, 1} : Vec4{1, 1, 1, 1};
                drawModel(r, ModelId::Trap, translate(cc) * scale({1, 1 + pop * 3.0f, 1}), Anim::Idle, 0, tint);
            }
        }

    for (const Decor& d : L.decor) {
        if (!inRange(d.pos, 14)) continue;
        Vec3 cc = cellCenter(d.pos);
        uint32_t h = uint32_t(d.pos.x * 73 + d.pos.y * 151);
        switch (d.kind) {
        case DecorKind::Brazier: {
            drawModel(r, ModelId::Brazier, translate(cc), Anim::Idle, 0);
            float s = 0.9f + 0.15f * std::sin(time * 11 + float(h));
            billboard(glow, cc + Vec3{0, 0.84f, 0}, right, UP, 0.55f * s, 0.75f * s, Tex::Flame, 0xFFFFFFFF);
            billboard(glow, cc + Vec3{0, 0.84f, 0}, ahead, UP, 0.45f * s, 0.6f * s, Tex::Flame, hex(0xFFC080));
            break;
        }
        case DecorKind::Pillar: drawModel(r, ModelId::Pillar, translate(cc), Anim::Idle, 0); break;
        case DecorKind::Bones:
            drawModel(r, ModelId::Bones, translate(cc + Vec3{float(h % 5) * 0.1f - 0.2f, 0, float(h % 3) * 0.15f - 0.15f}) * rotateY(float(h % 628) / 100), Anim::Idle, 0);
            break;
        case DecorKind::Chest: {
            float open = d.used ? 1 : 0;
            for (auto& l : lids)
                if (l.first == cellIndex(d.pos)) open = lidCurve(l.second);
            drawHinged(r, ModelId::Chest, translate(cc) * rotateY(wallBackedYaw(L, d.pos)), 1, rotateX(-open * 1.9f));
            break;
        }
        case DecorKind::Fountain: drawModel(r, d.used ? ModelId::FountainDry : ModelId::Fountain, translate(cc), Anim::Idle, 0); break;
        default: break;
        }
    }

    for (const FloorItem& it : L.items) {
        if (!inRange(it.pos, 12)) continue;
        Vec3 cc = cellCenter(it.pos) + Vec3{0, 0.25f + 0.05f * std::sin(time * 2 + float(it.id)), 0};
        Vec4 tint{1, 1, 1, 1};
        if (itemClass(it.item.kind) == ItemClass::Potion) tint = colourVec(POTION_RGB[game.potionColour(it.item.kind) % 8]);
        drawModel(r, itemModel(it.item.kind), translate(cc) * rotateY(time * 1.2f + float(it.id)), Anim::Idle, 0, tint);
    }
}

void App::drawMonsters(Vec3 eye) {
    for (const MonsterView& v : monsters) {
        Vec3 pos = monsterPos(v);
        if (v.rise < 0 || std::abs(pos.x - eye.x) > 30 || std::abs(pos.z - eye.z) > 30) continue;
        if (v.dormant) {
            if (v.kind == MonsterKind::Mimic) drawHinged(r, ModelId::Chest, translate(pos) * rotateY(v.yaw), -1, Mat4{});
            else drawModel(r, monsterModel(v.kind), translate(pos) * rotateY(v.yaw), Anim::Idle, 0);
            continue;
        }
        Anim anim = v.anim;
        float t = v.animT;
        if (debug.forceAnimOn && !v.dead) anim = debug.forceAnim, t = std::fmod(time, animCycle(anim));
        if (v.dead && v.deadT > DIE_TIME) pos.y -= (v.deadT - DIE_TIME) * v.sink;
        if (v.rise < 1) pos.y -= (1 - smooth(v.rise)) * monsterTop(v);
        Vec4 tint{1 + v.flash * 2.5f, 1 + v.flash * 0.6f, 1 + v.flash * 0.6f, 1};
        drawModel(r, monsterModel(v.kind), translate(pos) * rotateY(v.yaw), anim, t, tint);
    }
}

void App::drawEffects(const Camera& c, std::vector<Vertex>& glow, std::vector<Vertex>& matte) {
    Vec3 right = normalize(cross(c.forward, UP)), up = UP;

    for (const Bolt& b : bolts) {
        if (b.t <= 0) continue;
        Vec3 head = mix(b.from, b.to, b.t), dir = normalize(b.to - b.from);
        if (b.dart) {
            ribbon(matte, {head - dir * 0.3f, head}, c.eye, 0.035f, Tex::White, hex(0x3A3028));
            continue;
        }
        for (int i = 6; i >= 1; i--) {
            float t = b.t - float(i) * 0.035f, s = 0.42f * (1 - float(i) * 0.11f);
            if (t > 0) billboard(glow, mix(b.from, b.to, t) - up * (s / 2), right, up, s, s, Tex::Glow, withAlpha(b.color, 0.9f - float(i) * 0.13f));
        }
        billboard(glow, head - up * 0.25f, right, up, 0.5f, 0.5f, Tex::Glow, b.color);
        billboard(glow, head - up * 0.1f, right, up, 0.2f, 0.2f, Tex::Glow, 0xFFFFFFFF);
    }

    for (size_t i = 0; i < beams.size(); i++) {
        const Beam& b = beams[i];
        float fade = 1 - b.age / b.life;
        Vec3 d = b.to - b.from;
        float len = length(d);
        Vec3 side = normalize(cross(d, UP)), lift = normalize(cross(side, d));
        int n = std::max(3, int(len / 0.35f));
        uint32_t s = uint32_t(b.age * 30) * 2654435761u + uint32_t(i) * 40503u + 7;  // re-jag 30 times a second
        bool bolt = b.kind == ItemKind::WandLightning, slow = b.kind == ItemKind::WandSlow, blink = b.kind == ItemKind::WandBlink;
        std::vector<Vec3> pts;
        for (int k = 0; k <= n; k++) {
            float f = float(k) / float(n), taper = std::sin(f * PI);
            Vec3 p = mix(b.from, b.to, f);
            if (bolt) {
                float sx = hash(s) - 0.5f, sy = hash(s) - 0.5f;
                p += (side * sx * 0.8f + lift * sy * 0.6f) * taper;
            } else if (slow) {
                float a = f * len * 4 - b.age * 30;
                p += (side * std::cos(a) + lift * std::sin(a)) * (0.14f * taper);
            }
            pts.push_back(p);
        }
        uint32_t core = slow ? hex(0xE0A0FF) : hex(0xF0F4FF), halo = slow ? hex(0x9030E0) : blink ? hex(0xC0C8FF) : hex(0x5070FF);
        ribbon(glow, pts, c.eye, blink ? 0.14f : 0.07f, Tex::White, withAlpha(core, fade));
        ribbon(glow, pts, c.eye, blink ? 0.5f : 0.3f, Tex::Glow, withAlpha(halo, fade * 0.8f));
        // Lightning forks: short crooked spurs off random kinks.
        for (int k = 1; bolt && k < n; k += 2) {
            if (hash(s) < 0.5f) continue;
            Vec3 a = pts[size_t(k)], dir = normalize(side * (hash(s) - 0.5f) + lift * (hash(s) - 0.5f) + normalize(d) * 0.3f);
            Vec3 m = a + dir * 0.35f + lift * ((hash(s) - 0.5f) * 0.2f);
            ribbon(glow, {a, m, m + dir * 0.3f + side * ((hash(s) - 0.5f) * 0.2f)}, c.eye, 0.04f, Tex::White, withAlpha(core, fade * 0.8f));
        }
    }

    for (const Particle& p : particles) {
        float k = 1 - p.life / p.maxLife, s = p.size * std::max(0.05f, 1 + p.grow * k);
        float a = p.glow ? std::min(1.0f, p.life / p.maxLife * 1.5f) : std::min(1.0f, p.life / 0.3f);
        billboard(p.glow ? glow : matte, p.pos - up * (s / 2), right, up, s, s, p.tex, withAlpha(p.color, a));
    }
}

void App::projectFloats(const Mat4& viewProj) {
    hud.floats.clear();
    for (const Floater& f : floaters) {
        Vec4 c = viewProj * Vec4{f.pos.x, f.pos.y, f.pos.z, 1};
        if (c.w < 0.2f) continue;
        hud.floats.push_back({(c.x / c.w * 0.5f + 0.5f) * UI_W, (0.5f - c.y / c.w * 0.5f) * UI_H, f.text, f.color, f.age});
    }
}

void App::drawViewmodel(const Camera& c) {
    Mat4 cam = cameraToWorld(c.eye, c.forward, c.up);
    float step = camT < 1 ? std::sin(camT * PI) : 0;
    float flinch = flinchT < 0.3f ? std::sin(flinchT / 0.3f * PI) : 0;
    float drop = dyingT >= 0 ? smooth(dyingT / 0.6f) * 1.4f : 0;
    Vec3 idle{std::sin(time * 1.3f) * 0.006f, std::sin(time * 1.7f) * 0.008f, 0};
    Vec3 shared = sway + Vec3{0, -flinch * 0.06f - drop, flinch * 0.05f};

    const Swing& s = swingFor(game.player.weapon.kind);
    Pose pose = swingPose(s, swingT);
    Mat4 weapon = translate(pose.off + idle + shared - Vec3{0, step * 0.035f, 0}) * rotateY(pose.rot.y) * rotateZ(pose.rot.z) * rotateX(pose.rot.x) * rotateY(s.twist);
    drawModel(r, itemModel(game.player.weapon.kind), cam * weapon, Anim::Idle, 0);
    drawArm(cam, weapon, {0.4f, -0.75f, 0.1f}, false);

    // The torch bobs against the weapon and its flame trails the sway.
    Vec3 torchOff = Vec3{-0.24f, -0.25f, -0.7f} - idle + shared * 0.8f + Vec3{0, step * 0.02f, 0};
    Mat4 torch = translate(torchOff) * rotateZ(-0.3f - sway.x * 2) * rotateX(-0.45f + sway.z * 2);
    drawModel(r, ModelId::Torch, cam * torch, Anim::Idle, 0);
    drawArm(cam, torch, {-0.4f, -0.75f, 0.1f}, true);
    if (game.player.torch > 0) {
        Vec3 tip = (cam * torch).point({0, 0.42f, 0}), right = normalize(cross(c.forward, c.up));
        Vec3 lean = normalize(c.up + right * (sway.x * 6));
        float size = (0.6f + 0.4f * float(game.torchRadius()) / 6) * (1 + torchBoost * 0.5f);
        float s1 = size * (0.9f + 0.12f * std::sin(time * 17) + 0.06f * std::sin(time * 29));
        std::vector<Vertex> flame;
        billboard(flame, tip - lean * 0.02f, right, lean, 0.13f * s1, 0.22f * s1, Tex::Flame, 0xFFFFFFFF);
        r.drawVerts(flame, Mat4{}, {1, 1, 1, 1}, Blend::Add, true);
    }
}

// The fist closes on the handle below the guard: its Y runs exactly along the grip, and the
// forearm (its +Z) leaves at a right angle, turned as far toward the shoulder as that allows,
// so the knuckles face outward with the thumb on top. The model is a right hand; mirroring X
// makes the left.
void App::drawArm(const Mat4& cam, const Mat4& grip, Vec3 shoulder, bool left) {
    Vec3 at = grip.point({0, -0.06f, 0}), y = normalize(grip.point({0, 1, 0}) - grip.point({0, 0, 0}));
    Vec3 back = shoulder - at, z = back - y * dot(back, y);
    z = length(z) < 0.01f ? Vec3{0, 0, 1} : normalize(z);
    Vec3 x = cross(y, z);
    drawModel(r, ModelId::Hand, cam * basis(left ? x * -1 : x, y, z, at), Anim::Idle, 0);
}

// Harness art review: one model turning slowly on a dark stage.
void App::drawViewer() {
    ModelId id = ModelId(debug.viewer);
    const Model& m = model(id);
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi = lo * -1;
    std::vector<Mat4> xf(m.parts.size());
    for (size_t i = 0; i < m.parts.size(); i++) {
        const Part& part = m.parts[i];
        xf[i] = (part.parent < 0 ? Mat4{} : xf[size_t(part.parent)]) * translate(part.pivot);
        r.bounds(part.mesh, xf[i], lo, hi);
    }
    if (lo.x > hi.x) lo = {}, hi = {1, 1, 1};
    Vec3 centre = (lo + hi) * 0.5f;
    float size = std::max(0.2f, length(hi - lo)), dist = size * 1.2f + 0.2f;
    Vec3 eye = centre + Vec3{0, size * 0.4f, dist};
    SceneParams p;
    p.view = lookAt(eye, centre, UP);
    p.proj = perspective(50 * PI / 180, 4.0f / 3, 0.02f, 50);
    p.eye = eye;
    p.fogColor = {0.03f, 0.03f, 0.04f};
    p.fogStart = 30, p.fogEnd = 50;
    p.ambient = {0.55f, 0.52f, 0.5f};
    p.lights = {{centre + Vec3{0.8f, 0.9f, 1.0f} * (size + 0.5f), size * 4 + 2, {1.5f, 1.35f, 1.15f}},
                {centre + Vec3{-1.2f, 0.1f, 0.3f} * (size + 0.5f), size * 3 + 1.5f, {0.35f, 0.42f, 0.65f}}};
    r.beginScene(p);
    std::vector<Vertex> stage;
    floorQuad(stage, {0, std::min(0.0f, lo.y) - 0.002f, 0}, size * 1.2f, Tex::Stone, hex(0x2A2A2A));
    r.drawVerts(stage, Mat4{});
    Anim anim = debug.forceAnimOn ? debug.forceAnim : Anim::Idle;
    drawModel(r, id, rotateY(debug.viewerYaw + time * 0.6f), anim, debug.forceAnimOn ? std::fmod(time, animCycle(anim)) : time);
    r.endScene(1, {});
    r.text(Font::Small, 12, 10, debug.viewerName, hex(0xD8CCB0));
}
