// Floor generation: shaped rooms joined by winding corridors through proper doorways,
// extra loops (some behind secret doors), dead ends and hidden vaults; then stairs,
// decor, traps, monsters and loot.
#include "rng.h"
#include "world.h"
#include <algorithm>
#include <climits>
#include <queue>

bool Level::opaque(Pos p) const {
    Tile t = tile(p);
    return t == Tile::Wall || t == Tile::SecretDoor || (t == Tile::Door && !at(p).doorOpen);
}

bool Level::walkable(Pos p) const {
    if (tile(p) == Tile::Wall || tile(p) == Tile::SecretDoor) return false;
    const Decor* d = decorAt(p);
    return !d || !decorBlocks(d->kind);
}

Monster* Level::monsterAt(Pos p) {
    for (Monster& m : monsters)
        if (m.pos == p && m.hp > 0) return &m;
    return nullptr;
}

const Monster* Level::monsterAt(Pos p) const { return const_cast<Level*>(this)->monsterAt(p); }

const Decor* Level::decorAt(Pos p) const {
    for (const Decor& d : decor)
        if (d.pos == p) return &d;
    return nullptr;
}

Theme themeForDepth(int depth) { return depth <= 3 ? Theme::Crypt : depth <= 6 ? Theme::Catacomb : Theme::Ember; }

namespace {

template <class Kind, class Weight>
Kind weightedPick(Weight weight, Rng& rng) {
    int total = 0;
    for (int i = 0; i < int(Kind::Count); i++) total += weight(Kind(i));
    int roll = rng.range(1, std::max(1, total));
    for (int i = 0; i < int(Kind::Count); i++)
        if ((roll -= weight(Kind(i))) <= 0) return Kind(i);
    return Kind(0);
}

Pos scaled(Pos p, int k) { return {p.x * k, p.y * k}; }

enum class Shape : uint8_t { Rect, Cross, Hall, Round, Cave, Vault };

struct Room {
    Shape shape = Shape::Rect;
    int x = 0, y = 0, w = 0, h = 0;
    std::vector<Pos> cells;
    Pos center;
};

struct Opening {
    Pos door, out;  // the doorway in the wall ring, and the corridor cell just outside it
    int dir;
};

// Interior cells of the largest blob left by a few passes of cellular automaton smoothing.
std::vector<Pos> caveCells(int x0, int y0, int w, int h, Rng& rng) {
    std::vector<uint8_t> open(size_t(w * h));
    for (auto& o : open) o = rng.percent(58);
    auto at = [&](int i, int j) { return i >= 0 && j >= 0 && i < w && j < h && open[size_t(j * w + i)]; };
    for (int pass = 0; pass < 4; pass++) {
        std::vector<uint8_t> next(open.size());
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int n = 0;
                for (int dj = -1; dj <= 1; dj++)
                    for (int di = -1; di <= 1; di++) n += (di || dj) && at(i + di, j + dj);
                next[size_t(j * w + i)] = n >= 5 || (at(i, j) && n >= 4);
            }
        open = next;
    }
    std::vector<int> blob(open.size(), -1);
    std::vector<Pos> best;
    for (int start = 0; start < w * h; start++) {
        if (!open[size_t(start)] || blob[size_t(start)] >= 0) continue;
        std::vector<Pos> cells{{start % w, start / w}};
        blob[size_t(start)] = start;
        for (size_t k = 0; k < cells.size(); k++)
            for (Pos d : DIRS) {
                Pos n = cells[k] + d;
                if (at(n.x, n.y) && blob[size_t(n.y * w + n.x)] < 0) blob[size_t(n.y * w + n.x)] = start, cells.push_back(n);
            }
        if (cells.size() > best.size()) best = cells;
    }
    for (Pos& p : best) p = p + Pos{x0, y0};
    return best;
}

std::vector<Pos> shapeCells(Shape s, int x0, int y0, int w, int h, Rng& rng) {
    if (s == Shape::Cave) return caveCells(x0, y0, w, h, rng);
    std::vector<Pos> cells;
    float cx = w / 2.0f, cy = h / 2.0f;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            bool in = true;
            if (s == Shape::Cross) in = (j >= h / 3 && j < h - h / 3) || (i >= w / 3 && i < w - w / 3);
            if (s == Shape::Round) {
                float dx = (i + 0.5f - cx) / cx, dy = (j + 0.5f - cy) / cy;
                in = dx * dx + dy * dy <= 1.15f;
            }
            if (in) cells.push_back({x0 + i, y0 + j});
        }
    return cells;
}

struct Gen {
    Level& L;
    Rng& rng;
    std::vector<Room> rooms, vaults;
    std::vector<int> roomOf;      // interior cell -> room index
    std::vector<uint8_t> ring;    // walls hugging a room: corridors cross them only at doorways
    std::vector<uint8_t> noise;   // per-cell corridor cost jitter, so corridors wander a little
    std::vector<Pos> doorways;
    std::vector<std::pair<int, int>> links;
    int nextId = 1;

    Gen(Level& level, Rng& r) : L(level), rng(r) {
        roomOf.assign(L.cells.size(), -1);
        ring.assign(L.cells.size(), 0);
        noise.resize(L.cells.size());
        for (auto& n : noise) n = uint8_t(rng.range(0, 6));
    }

    size_t idx(Pos p) const { return size_t(p.y) * L.width + p.x; }
    bool inBounds(Pos p) const { return p.x >= 1 && p.y >= 1 && p.x < L.width - 1 && p.y < L.height - 1; }
    bool interior(Pos p) const { return L.inside(p) && roomOf[idx(p)] >= 0; }
    bool rock(Pos p) const { return inBounds(p) && L.at(p).tile == Tile::Wall && !interior(p); }
    bool isDoorway(Pos p) const { return std::find(doorways.begin(), doorways.end(), p) != doorways.end(); }
    bool nearDoorway(Pos p) const {
        return std::any_of(doorways.begin(), doorways.end(), [&](Pos d) { return std::abs(d.x - p.x) <= 1 && std::abs(d.y - p.y) <= 1; });
    }

    // ---- rooms ----

    bool fits(const std::vector<Pos>& cells) const {
        for (Pos p : cells) {
            if (p.x < 2 || p.y < 2 || p.x > L.width - 3 || p.y > L.height - 3) return false;
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++)
                    if (interior(p + Pos{dx, dy})) return false;
        }
        return true;
    }

    void markRing(const std::vector<Pos>& cells) {
        for (Pos p : cells)
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (L.inside(p + Pos{dx, dy}) && !interior(p + Pos{dx, dy})) ring[idx(p + Pos{dx, dy})] = 1;
    }

    void carveRoom(Room r) {
        for (Pos p : r.cells) L.at(p).tile = Tile::Floor, roomOf[idx(p)] = int(rooms.size());
        markRing(r.cells);
        Pos mid{r.x + r.w / 2, r.y + r.h / 2};
        r.center = *std::min_element(r.cells.begin(), r.cells.end(), [&](Pos a, Pos b) { return manhattan(a, mid) < manhattan(b, mid); });
        rooms.push_back(std::move(r));
    }

    bool tryRoom(Shape s, int w, int h) {
        int x = rng.range(2, L.width - w - 2), y = rng.range(2, L.height - h - 2);
        std::vector<Pos> cells = shapeCells(s, x, y, w, h, rng);
        if (cells.size() < (s == Shape::Cave ? 20u : 6u) || !fits(cells)) return false;
        carveRoom({s, x, y, w, h, std::move(cells), {}});
        return true;
    }

    Shape pickShape() {
        // Rect, Cross, Hall, Round, Cave
        static const int WEIGHTS[3][5] = {{50, 14, 14, 22, 0}, {22, 8, 10, 15, 45}, {40, 20, 20, 20, 0}};
        const int* w = WEIGHTS[int(L.theme)];
        int roll = rng.range(1, w[0] + w[1] + w[2] + w[3] + w[4]);
        for (int i = 0; i < 5; i++)
            if ((roll -= w[i]) <= 0) return Shape(i);
        return Shape::Rect;
    }

    bool placeShaped(Shape s) {
        switch (s) {
        case Shape::Cross: {
            int n = rng.range(3, 4) * 2 + 1;
            return tryRoom(s, n, n);
        }
        case Shape::Hall: {
            int a = rng.range(4, 5) * 2 + 1, b = rng.range(2, 3) * 2 + 1;
            return rng.percent(50) ? tryRoom(s, a, b) : tryRoom(s, b, a);
        }
        case Shape::Round: return tryRoom(s, rng.range(5, 9), rng.range(5, 9));
        case Shape::Cave: return tryRoom(s, rng.range(8, 11), rng.range(8, 11));
        default: return tryRoom(s, rng.range(3, 7), rng.range(3, 7));
        }
    }

    void placeRooms() {
        for (int tries = 0; L.depth == FINAL_DEPTH && rooms.empty() && tries < 100; tries++) tryRoom(Shape::Hall, 11, 9);
        int target = rng.range(9, 12);
        for (int tries = 0; tries < 700 && int(rooms.size()) < target; tries++) placeShaped(pickShape());
    }

    // ---- corridors ----

    std::vector<Opening> openings(const Room& r) const {
        std::vector<Opening> list;
        for (Pos c : r.cells)
            for (int d = 0; d < 4; d++) {
                Pos door = c + DIRS[d], out = door + DIRS[d], side = DIRS[(d + 1) % 4];
                if (interior(door) || !inBounds(out) || interior(out) || ring[idx(out)]) continue;
                if (interior(door + side) || interior(door - side) || nearDoorway(door)) continue;
                list.push_back({door, out, d});
            }
        return list;
    }

    Opening pickOpening(std::vector<Opening> list, Pos target) {
        std::stable_sort(list.begin(), list.end(), [&](const Opening& a, const Opening& b) { return manhattan(a.out, target) < manhattan(b.out, target); });
        return list[size_t(rng.range(0, std::min(3, int(list.size())) - 1))];
    }

    int stepCost(Pos q) const {
        if (L.at(q).tile == Tile::Floor) return 5;  // happily reuse an existing corridor
        int cost = 10 + noise[idx(q)];
        for (Pos d : DIRS)
            if (L.tile(q + d) == Tile::Floor) cost += 6;  // but don't run alongside one
        return cost;
    }

    // Cheapest corridor from `from` (leaving in `dir`) to `to`, avoiding rooms and their walls.
    std::vector<Pos> route(Pos from, int dir, Pos to) const {
        std::vector<int> cost(L.cells.size() * 4, INT_MAX), prev(L.cells.size() * 4, -1);
        using Node = std::pair<int, int>;
        std::priority_queue<Node, std::vector<Node>, std::greater<>> open;
        int first = int(idx(from)) * 4 + dir;
        cost[size_t(first)] = 0;
        open.push({0, first});
        while (!open.empty()) {
            auto [c, s] = open.top();
            open.pop();
            if (c > cost[size_t(s)]) continue;
            Pos p{(s / 4) % L.width, (s / 4) / L.width};
            if (p == to) {
                std::vector<Pos> path;
                for (int k = s; k >= 0; k = prev[size_t(k)]) path.push_back({(k / 4) % L.width, (k / 4) / L.width});
                return path;
            }
            for (int d = 0; d < 4; d++) {
                Pos q = p + DIRS[d];
                if (d == (s % 4 + 2) % 4 || !inBounds(q) || interior(q) || ring[idx(q)]) continue;
                int next = int(idx(q)) * 4 + d, nc = c + stepCost(q) + (d != s % 4 ? 14 : 0);
                if (nc < cost[size_t(next)]) cost[size_t(next)] = nc, prev[size_t(next)] = s, open.push({nc, next});
            }
        }
        return {};
    }

    bool linked(int a, int b) const {
        return std::find(links.begin(), links.end(), std::pair{a, b}) != links.end() || std::find(links.begin(), links.end(), std::pair{b, a}) != links.end();
    }

    bool connect(int a, int b, bool secret) {
        std::vector<Opening> from = openings(rooms[size_t(a)]), to = openings(rooms[size_t(b)]);
        if (from.empty() || to.empty()) return false;
        for (int attempt = 0; attempt < 3; attempt++) {
            Opening oa = pickOpening(from, rooms[size_t(b)].center), ob = pickOpening(to, oa.out);
            std::vector<Pos> path = route(oa.out, oa.dir, ob.out);
            if (path.empty()) continue;
            for (Pos p : path) L.at(p).tile = Tile::Floor;
            L.at(oa.door).tile = secret ? Tile::SecretDoor : Tile::Floor;
            L.at(ob.door).tile = Tile::Floor;
            doorways.push_back(oa.door);
            doorways.push_back(ob.door);
            links.push_back({a, b});
            return true;
        }
        return false;
    }

    // Prim's tree over room centres; a room that can't be reached is filled back in.
    void connectRooms() {
        std::vector<bool> joined(rooms.size());
        joined[0] = true;
        for (;;) {
            std::vector<std::tuple<int, int, int>> pairs;
            for (size_t i = 0; i < rooms.size(); i++)
                for (size_t j = 0; j < rooms.size(); j++)
                    if (joined[i] && !joined[j]) pairs.push_back({manhattan(rooms[i].center, rooms[j].center), int(i), int(j)});
            if (pairs.empty()) break;
            std::stable_sort(pairs.begin(), pairs.end());
            auto ok = std::find_if(pairs.begin(), pairs.end(), [&](auto& t) { return connect(std::get<1>(t), std::get<2>(t), false); });
            if (ok == pairs.end()) break;
            joined[size_t(std::get<2>(*ok))] = true;
        }
        for (size_t i = 0; i < rooms.size(); i++)
            if (!joined[i]) {
                for (Pos p : rooms[i].cells) L.at(p).tile = Tile::Wall, roomOf[idx(p)] = -1;
                rooms[i].cells.clear();
            }
    }

    std::vector<int> liveRooms() const {
        std::vector<int> live;
        for (size_t i = 0; i < rooms.size(); i++)
            if (!rooms[i].cells.empty()) live.push_back(int(i));
        return live;
    }

    void addLoops() {
        std::vector<int> live = liveRooms();
        for (int n = rng.range(2, 4); n > 0 && live.size() > 3; n--) {
            int a = live[size_t(rng.range(0, int(live.size()) - 1))];
            std::vector<int> near;
            for (int b : live)
                if (b != a && !linked(a, b)) near.push_back(b);
            std::stable_sort(near.begin(), near.end(), [&](int p, int q) {
                return manhattan(rooms[size_t(p)].center, rooms[size_t(a)].center) < manhattan(rooms[size_t(q)].center, rooms[size_t(a)].center);
            });
            if (!near.empty()) connect(a, near[size_t(rng.range(0, std::min(2, int(near.size()) - 1)))], rng.percent(40));
        }
    }

    // ---- dead ends and vaults ----

    bool corridor(Pos p) const { return L.tile(p) == Tile::Floor && !interior(p) && !ring[idx(p)]; }

    // A short blind passage, leaving from a room wall or a straight stretch of corridor.
    void addDeadEnd() {
        Pos tip;
        int dir;
        std::vector<Pos> path;
        if (rng.percent(50)) {
            std::vector<int> live = liveRooms();
            std::vector<Opening> list = openings(rooms[size_t(live[size_t(rng.range(0, int(live.size()) - 1))])]);
            if (list.empty()) return;
            Opening o = list[size_t(rng.range(0, int(list.size()) - 1))];
            if (!rock(o.out)) return;
            for (Pos d : DIRS)
                if (o.out + d != o.door && L.tile(o.out + d) != Tile::Wall) return;
            path = {o.door, o.out};
            tip = o.out, dir = o.dir;
        } else {
            Pos c{rng.range(1, L.width - 2), rng.range(1, L.height - 2)};
            dir = rng.range(0, 3);
            Pos side = DIRS[(dir + 1) % 4];
            if (!corridor(c) || !corridor(c + side) || !corridor(c - side) || !rock(c + DIRS[dir])) return;
            tip = c;
        }
        for (int len = rng.range(3, 7); len > 0; len--) {
            if (rng.percent(25)) dir = (dir + (rng.percent(50) ? 1 : 3)) % 4;
            Pos next = tip + DIRS[dir];
            if (!rock(next) || ring[idx(next)]) break;
            bool touches = false;
            for (Pos d : DIRS) touches |= next + d != tip && L.tile(next + d) != Tile::Wall;
            if (touches) break;
            path.push_back(next);
            tip = next;
        }
        if (path.size() < 4) return;
        for (Pos p : path) L.at(p).tile = Tile::Floor;
        if (L.at(path[0]).tile == Tile::Floor && ring[idx(path[0])]) doorways.push_back(path[0]);
        if (rng.percent(55)) tryVault(tip, dir);
    }

    // A small sealed room behind a secret door at tip + dir.
    bool tryVault(Pos tip, int dir) {
        Pos door = tip + DIRS[dir], fwd = DIRS[dir], side = DIRS[(dir + 1) % 4];
        int deep = rng.range(2, 3), wide = rng.range(2, 3), offset = rng.range(0, wide - 1);
        if (L.tile(door) != Tile::Wall || interior(door) || nearDoorway(door)) return false;
        if (L.tile(door + side) != Tile::Wall || L.tile(door - side) != Tile::Wall) return false;
        std::vector<Pos> cells;
        for (int f = 1; f <= deep; f++)
            for (int s = 0; s < wide; s++) cells.push_back(door + scaled(fwd, f) + scaled(side, s - offset));
        for (Pos p : cells) {
            if (!inBounds(p) || ring[idx(p)]) return false;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (p + Pos{dx, dy} != door && !rock(p + Pos{dx, dy})) return false;
        }
        for (Pos p : cells) L.at(p).tile = Tile::Floor;
        markRing(cells);
        L.at(door).tile = Tile::SecretDoor;
        doorways.push_back(door);
        vaults.push_back({Shape::Vault, 0, 0, 0, 0, cells, cells.back()});
        return true;
    }

    void addVaultsOffRooms(int count) {
        for (int tries = 0; tries < 200 && count > 0; tries++) {
            Pos tip{rng.range(1, L.width - 2), rng.range(1, L.height - 2)};
            int dir = rng.range(0, 3);
            if (L.at(tip).tile != Tile::Floor || isDoorway(tip)) continue;
            if (tryVault(tip, dir)) count--;
        }
    }

    // Doors go only where a doorway has wall on both sides and open floor through it.
    void hangDoors() {
        for (Pos p : doorways) {
            Cell& c = L.at(p);
            bool spansX = solid(L.tile(p + DIRS[1])) && solid(L.tile(p + DIRS[3])) && !solid(L.tile(p + DIRS[0])) && !solid(L.tile(p + DIRS[2]));
            bool spansY = solid(L.tile(p + DIRS[0])) && solid(L.tile(p + DIRS[2])) && !solid(L.tile(p + DIRS[1])) && !solid(L.tile(p + DIRS[3]));
            bool cave = false;
            for (Pos d : DIRS)
                if (interior(p + d)) cave |= rooms[size_t(roomOf[idx(p + d)])].shape == Shape::Cave;
            if (c.tile == Tile::SecretDoor) {
                if (!spansX && !spansY) c.tile = Tile::Wall;
            } else if ((spansX || spansY) && rng.percent(cave ? 15 : 60)) {
                c.tile = Tile::Door;
            }
        }
    }

    // ---- placement ----

    bool blocked(Pos p) const {
        const Monster* m = L.monsterAt(p);
        if (L.tile(p) == Tile::SecretDoor) return false;
        return !L.walkable(p) || (m && (monsterDef(m->kind).flags & MF_DORMANT));
    }

    // True if something solid can stand on p without cutting any path: the open cells
    // around p must stay joined to each other by stepping around its eight neighbours.
    bool canBlock(Pos p) const {
        if (L.tile(p) != Tile::Floor || p == L.start || p == L.stairs || L.decorAt(p) || L.monsterAt(p) || isDoorway(p)) return false;
        static const Pos AROUND[8] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
        bool open[8];
        for (int i = 0; i < 8; i++) open[i] = !blocked(p + AROUND[i]);
        int runs = 0;
        for (int i = 0; i < 8; i++) {
            if (!open[i] || open[(i + 7) % 8]) continue;
            bool orthogonal = false;
            for (int j = i; open[j % 8] && j < i + 8; j++) orthogonal |= j % 2 == 0;
            runs += orthogonal;
        }
        return runs <= 1;
    }

    int solidSides(Pos p) const {
        int n = 0;
        for (Pos d : DIRS) n += solid(L.tile(p + d));
        return n;
    }

    Pos randomCell(const std::vector<Pos>& cells) { return cells[size_t(rng.range(0, int(cells.size()) - 1))]; }
    const Room& randomRoom() {
        std::vector<int> live = liveRooms();
        return rooms[size_t(live[size_t(rng.range(0, int(live.size()) - 1))])];
    }

    // A cell in some room where a blocking thing may stand, preferring spots against a wall.
    Pos roomSpot(bool againstWall, const std::vector<int>& dist, int minDistance) {
        for (int tries = 0; tries < 300; tries++) {
            Pos p = randomCell(randomRoom().cells);
            if (dist[idx(p)] < minDistance || !canBlock(p) || L.monsterAt(p)) continue;
            if (againstWall && solidSides(p) == 0 && tries < 200) continue;
            return p;
        }
        return {-1, -1};
    }

    Pos freeCell(const std::vector<Pos>& cells, const std::vector<int>& dist, int minDistance) {
        for (int tries = 0; tries < 300; tries++) {
            Pos p = randomCell(cells);
            if (L.at(p).tile != Tile::Floor || p == L.start || p == L.stairs || dist[idx(p)] < minDistance) continue;
            if (L.monsterAt(p) || L.decorAt(p) || L.at(p).trap != TrapKind::None) continue;
            return p;
        }
        return {-1, -1};
    }

    void addDecor(DecorKind kind, Pos p) {
        if (p.x >= 0) L.decor.push_back({kind, p});
    }

    void addMonster(MonsterKind kind, Pos p, bool awake) {
        if (p.x < 0) return;
        Monster m;
        m.id = nextId++;
        m.kind = kind;
        m.pos = p;
        m.hp = monsterDef(kind).hp;
        m.facing = rng.range(0, 3);
        m.awake = awake;
        L.monsters.push_back(m);
    }

    void addItem(Item item, Pos p) {
        if (p.x >= 0) L.items.push_back({nextId++, item, p});
    }

    void placeStairs() {
        std::vector<int> live = liveRooms();
        const Room* start = &rooms[size_t(live[size_t(rng.range(0, int(live.size()) - 1))])];
        if (L.depth == FINAL_DEPTH) {
            std::vector<int> fromHall = distanceMap(L, rooms[0].center);
            for (int i : live)
                if (fromHall[idx(rooms[size_t(i)].center)] > fromHall[idx(start->center)]) start = &rooms[size_t(i)];
        }
        L.start = start->center;
        std::vector<int> dist = distanceMap(L, L.start);
        const Room* far = start;
        for (int i : live)
            if (dist[idx(rooms[size_t(i)].center)] > dist[idx(far->center)]) far = &rooms[size_t(i)];
        L.stairs = L.depth == FINAL_DEPTH ? rooms[0].center : far->center;
        if (L.depth < FINAL_DEPTH) L.at(L.stairs).tile = Tile::StairsDown;
    }

    void placeArchitecture() {
        for (const Room& r : rooms) {
            if (r.cells.empty()) continue;
            if (r.shape == Shape::Hall) {
                bool wide = r.w > r.h;
                for (int k = 1; k < (wide ? r.w : r.h) - 1; k += 2)
                    for (int side : {1, (wide ? r.h : r.w) - 2}) {
                        Pos p = wide ? Pos{r.x + k, r.y + side} : Pos{r.x + side, r.y + k};
                        if (canBlock(p)) addDecor(DecorKind::Pillar, p);
                    }
            } else if (r.shape == Shape::Rect && r.w >= 5 && r.h >= 5 && rng.percent(45)) {
                for (Pos p : {Pos{r.x + 1, r.y + 1}, Pos{r.x + r.w - 2, r.y + 1}, Pos{r.x + 1, r.y + r.h - 2}, Pos{r.x + r.w - 2, r.y + r.h - 2}})
                    if (canBlock(p)) addDecor(DecorKind::Pillar, p);
            }
            int braziers = r.shape == Shape::Hall || L.depth == FINAL_DEPTH ? 2 : rng.percent(r.shape == Shape::Cave ? 30 : 55);
            for (int i = 0; i < braziers; i++)
                for (int tries = 0; tries < 30; tries++) {
                    Pos p = randomCell(r.cells);
                    if (solidSides(p) >= 2 && canBlock(p)) {
                        addDecor(DecorKind::Brazier, p);
                        break;
                    }
                }
        }
    }

    void placeFeatures(const std::vector<int>& dist) {
        std::vector<Pos> floor;
        for (int y = 1; y < L.height - 1; y++)
            for (int x = 1; x < L.width - 1; x++)
                if (dist[idx({x, y})] >= 0) floor.push_back({x, y});

        for (int i = rng.range(3, 7); i > 0; i--) addDecor(DecorKind::Bones, freeCell(floor, dist, 1));
        if (L.depth < FINAL_DEPTH && rng.percent(45)) addDecor(DecorKind::Fountain, roomSpot(false, dist, 3));

        for (int i = rng.range(1, 2) + (L.depth >= 4 && rng.percent(50)); i > 0; i--) {
            Pos p = roomSpot(true, dist, 4);
            if (L.depth >= 3 && rng.percent(25)) addMonster(MonsterKind::Mimic, p, false);
            else addDecor(DecorKind::Chest, p);
        }
        if (L.depth >= monsterDef(MonsterKind::Gargoyle).minDepth)
            for (int i = rng.range(0, 2); i > 0; i--) addMonster(MonsterKind::Gargoyle, roomSpot(true, dist, 6), false);

        for (const Room& v : vaults) {
            Pos chest = v.center;
            for (int tries = 0; !canBlock(chest) && tries < 20; tries++) chest = randomCell(v.cells);
            if (!canBlock(chest)) continue;
            if (L.depth >= 3 && rng.percent(10)) addMonster(MonsterKind::Mimic, chest, false);
            else addDecor(DecorKind::Chest, chest);
            std::vector<int> none(L.cells.size(), 99);
            addItem(randomItem(L.depth + 1, rng), freeCell(v.cells, none, 0));
            if (rng.percent(60)) addItem({ItemKind::Gold, rng.range(10, 20) * L.depth}, freeCell(v.cells, none, 0));
        }

        for (int i = 1 + L.depth / 3 + rng.range(0, 2); i > 0; i--) {
            Pos p = freeCell(floor, dist, 6);
            if (p.x < 0 || isDoorway(p) || manhattan(p, L.stairs) <= 1) continue;
            TrapKind kind = weightedPick<TrapKind>([&](TrapKind k) { return trapWeight(k); }, rng);
            if (kind == TrapKind::Teleport && !canBlock(p)) kind = TrapKind::Spikes;
            L.at(p).trap = kind;
        }
    }

    int trapWeight(TrapKind k) const {
        int d = L.depth;
        switch (k) {
        case TrapKind::Spikes: return 30;
        case TrapKind::Dart: return d >= 2 ? 25 : 0;
        case TrapKind::Alarm: return d >= 2 ? 15 : 0;
        case TrapKind::Pit: return d >= 2 && d < FINAL_DEPTH ? 14 : 0;
        case TrapKind::Teleport: return 14;
        default: return 0;
        }
    }

    void placeMonsters(const std::vector<int>& dist) {
        std::vector<Pos> roomCells;
        for (int i : liveRooms())
            if (!(L.depth == FINAL_DEPTH && i == 0)) roomCells.insert(roomCells.end(), rooms[size_t(i)].cells.begin(), rooms[size_t(i)].cells.end());
        for (int i = 3 + L.depth + rng.range(0, 2); i > 0; i--) {
            Pos p = freeCell(roomCells, dist, 7);
            if (p.x >= 0 && L.walkable(p)) addMonster(randomMonster(L.depth, rng), p, rng.percent(20));
        }
        if (L.depth == FINAL_DEPTH) addMonster(MonsterKind::Lich, L.stairs, false);
    }

    void placeLoot(const std::vector<int>& dist) {
        std::vector<Pos> roomCells;
        for (int i : liveRooms()) roomCells.insert(roomCells.end(), rooms[size_t(i)].cells.begin(), rooms[size_t(i)].cells.end());
        auto spot = [&] {
            Pos p = freeCell(roomCells, dist, 0);
            return p.x >= 0 && L.walkable(p) ? p : Pos{-1, -1};
        };
        for (int i = 3 + rng.range(0, 3); i > 0; i--) addItem(randomItem(L.depth, rng), spot());
        if (rng.percent(40)) addItem({ItemKind::OilFlask}, spot());
        if (rng.percent(10)) addItem({ItemKind::PotionHealing}, spot());
        for (int i = rng.range(2, 4); i > 0; i--) addItem({ItemKind::Gold, rng.range(5, 12) * L.depth}, spot());
    }
};

int itemWeight(ItemKind k, int depth) {
    const ItemDef& d = itemDef(k);
    if (depth < d.minDepth || d.frequency == 0) return 0;
    bool gear = d.cls == ItemClass::Weapon || d.cls == ItemClass::Armor;
    return gear && depth > d.minDepth + 4 ? (d.frequency + 1) / 2 : d.frequency;
}

}  // namespace

Item randomItem(int depth, Rng& rng) {
    // Pick the class first so each class keeps its share as deeper kinds unlock.
    static const struct { ItemClass cls; int weight, minDepth; } CLASSES[] = {
        {ItemClass::Potion, 30, 1}, {ItemClass::Scroll, 26, 1}, {ItemClass::Weapon, 10, 1}, {ItemClass::Armor, 8, 1},
        {ItemClass::Wand, 7, 2}, {ItemClass::Ring, 4, 2}, {ItemClass::Oil, 5, 1},
    };
    int total = 0;
    for (auto& c : CLASSES) total += depth >= c.minDepth ? c.weight : 0;
    int roll = rng.range(1, total);
    ItemClass cls = ItemClass::Potion;
    for (auto& c : CLASSES)
        if (depth >= c.minDepth && (roll -= c.weight) <= 0) {
            cls = c.cls;
            break;
        }
    Item item{weightedPick<ItemKind>([&](ItemKind k) { return itemClass(k) == cls ? itemWeight(k, depth) : 0; }, rng)};
    if ((cls == ItemClass::Weapon || cls == ItemClass::Armor) && rng.percent(20 + depth * 4)) item.enchant = rng.range(1, 1 + depth / 4);
    if (cls == ItemClass::Ring) item.enchant = rng.range(0, depth / 4);
    if (cls == ItemClass::Wand) item.charges = item.kind == ItemKind::WandSlow ? rng.range(4, 6) : rng.range(3, 5);
    return item;
}

MonsterKind randomMonster(int depth, Rng& rng) {
    return weightedPick<MonsterKind>(
        [&](MonsterKind k) {
            const MonsterDef& d = monsterDef(k);
            bool placed = d.flags & (MF_DORMANT | MF_BOSS);
            return !placed && depth >= d.minDepth && depth <= d.maxDepth ? d.frequency : 0;
        },
        rng);
}

Level generateLevel(int depth, uint32_t seed) {
    Rng rng(uint64_t(seed) * 7919u + uint64_t(depth) * 104729u);
    Level L;
    L.depth = depth;
    L.theme = themeForDepth(depth);
    L.width = 40;
    L.height = 40;
    L.cells.assign(size_t(L.width) * L.height, Cell{});
    for (Cell& c : L.cells) c.variant = uint8_t(rng.next());

    Gen g(L, rng);
    g.placeRooms();
    g.connectRooms();
    g.addLoops();
    for (int i = rng.range(1, 3); i > 0; i--) g.addDeadEnd();
    if (depth >= 2) g.addVaultsOffRooms(rng.range(0, 1));
    g.hangDoors();
    g.placeStairs();

    std::vector<int> dist = distanceMap(L, L.start);
    g.placeArchitecture();
    g.placeFeatures(dist);
    g.placeMonsters(dist);
    g.placeLoot(dist);
    return L;
}
