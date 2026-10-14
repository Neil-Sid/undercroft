#pragma once
// The dungeon grid and what stands on it. Pure logic: no rendering, no Windows.
#include "content.h"
#include <cstdint>
#include <cstdlib>
#include <vector>

struct Pos {
    int x = 0, y = 0;
    bool operator==(const Pos&) const = default;
    Pos operator+(Pos o) const { return {x + o.x, y + o.y}; }
    Pos operator-(Pos o) const { return {x - o.x, y - o.y}; }
};

// Facing: 0 north (-y), 1 east (+x), 2 south (+y), 3 west (-x). Movement is 4-way only.
constexpr Pos DIRS[4] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
inline int manhattan(Pos a, Pos b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }
inline int dirTowards(Pos from, Pos to) {
    Pos d = to - from;
    if (std::abs(d.x) >= std::abs(d.y)) return d.x >= 0 ? 1 : 3;
    return d.y >= 0 ? 2 : 0;
}

// SecretDoor looks and blocks like Wall until found, then becomes Door.
enum class Tile : uint8_t { Wall, Floor, Door, StairsDown, SecretDoor };
inline bool solid(Tile t) { return t == Tile::Wall || t == Tile::SecretDoor; }
enum class Theme : uint8_t { Crypt, Catacomb, Ember };
enum class TrapKind : uint8_t { None, Spikes, Dart, Alarm, Pit, Teleport, Count };
// Chest: bump to open (spills loot). Fountain: bump to drink (random effect, then dry).
enum class DecorKind : uint8_t { Brazier, Pillar, Bones, Chest, Fountain, Count };

constexpr int FINAL_DEPTH = 10;

struct Cell {
    Tile tile = Tile::Wall;
    bool doorOpen = false;
    bool seen = false;     // ever seen: drawn on the automap
    bool visible = false;  // in view this turn
    uint8_t variant = 0;   // random per cell, for texture and tint variety
    TrapKind trap = TrapKind::None;
    bool trapFound = false;  // drawn once found or triggered
};

struct Monster {
    int id = 0;
    MonsterKind kind{};
    Pos pos;
    int facing = 2;
    int hp = 1;
    int energy = 0;
    bool awake = false;  // MF_DORMANT monsters stay disguised while asleep
    int fleeing = 0;     // turns left running away; negative while catching its breath
    int slowed = 0;      // turns left at half speed
    bool blinked = false;  // the Lich escapes once when badly hurt
};

struct FloorItem {
    int id = 0;
    Item item;
    Pos pos;
};

struct Decor {
    DecorKind kind{};
    Pos pos;
    bool used = false;  // chest opened, fountain dry
};

inline bool decorBlocks(DecorKind k) { return k != DecorKind::Bones; }

struct Level {
    int width = 0, height = 0, depth = 1;
    Theme theme = Theme::Crypt;
    std::vector<Cell> cells;
    Pos start, stairs;
    std::vector<Monster> monsters;
    std::vector<FloorItem> items;
    std::vector<Decor> decor;

    bool inside(Pos p) const { return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height; }
    Cell& at(Pos p) { return cells[size_t(p.y) * width + p.x]; }
    const Cell& at(Pos p) const { return cells[size_t(p.y) * width + p.x]; }
    Tile tile(Pos p) const { return inside(p) ? at(p).tile : Tile::Wall; }
    bool opaque(Pos p) const;    // walls, secret doors and closed doors block sight
    bool walkable(Pos p) const;  // terrain and decor only; closed doors count (walking in opens them)
    Monster* monsterAt(Pos p);
    const Monster* monsterAt(Pos p) const;
    const Decor* decorAt(Pos p) const;
};

Theme themeForDepth(int depth);
Level generateLevel(int depth, uint32_t seed);  // dungeon.cpp

struct Rng;
Item randomItem(int depth, Rng& rng);            // loot tables, deeper is better
MonsterKind randomMonster(int depth, Rng& rng);  // roaming kinds only: never mimics, gargoyles or the Lich

// fov.cpp
void computeFov(Level& level, Pos origin, int radius);  // sets Cell::visible and Cell::seen
bool lineOfSight(const Level& level, Pos a, Pos b);
std::vector<int> distanceMap(const Level& level, Pos from);  // 4-way BFS over walkable cells; -1 = unreachable
