// Headless balance check: a scripted bot plays many runs and reports how deep it gets.
//   sim.exe [--runs 200] [--seed 1] [--verbose]
//   sim.exe --check 300          validate the generator over 300 seeds x 10 floors
//   sim.exe --dump 7 5           print the plan of seed 7, depth 5
//   sim.exe --replay 7           play seed 7 twice and confirm the runs are identical
#include "game.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>

namespace {

float weaponScore(const Item& w) {
    const ItemDef& d = itemDef(w.kind);
    return (d.damageMin + d.damageMax) * 0.5f + w.enchant * 1.5f + d.accuracy * 0.6f;
}

int armorScore(const Item& a) { return itemDef(a.kind).defense + a.enchant; }

Action faceOrWalk(const Game& g, int dir) {
    if (dir == g.player.facing) return Action::Forward;
    return (dir - g.player.facing + 4) % 4 == 3 ? Action::TurnLeft : Action::TurnRight;
}

// Plays like a careful human: reads the map it has seen, not the level's secrets.
struct Bot {
    Game& g;
    int depth = 0, onLevel = 0, restTurns = 0, searchLeft = 0, waited = 0;
    std::vector<uint8_t> searched = {};
    std::vector<Pos> dropped = {};

    const Level& L() const { return g.level; }
    size_t idx(Pos p) const { return size_t(p.y) * L().width + p.x; }

    int slotOf(ItemKind k) const {
        for (size_t i = 0; i < g.player.pack.size(); i++)
            if (g.player.pack[i].kind == k && g.identified(k)) return int(i);
        return -1;
    }
    bool use(ItemKind k) {
        int s = slotOf(k);
        return s >= 0 && g.use(s);
    }
    bool useUnknown(ItemClass c) {
        for (size_t i = 0; i < g.player.pack.size(); i++)
            if (itemClass(g.player.pack[i].kind) == c && !g.identified(g.player.pack[i].kind)) return g.use(int(i));
        return false;
    }

    // What the player can see: awake monsters, and dormant ones only as the furniture they pretend to be.
    std::vector<const Monster*> threats() const {
        std::vector<const Monster*> list;
        for (const Monster& m : L().monsters)
            if (m.hp > 0 && m.awake && L().at(m.pos).visible) list.push_back(&m);
        return list;
    }
    bool looksLikeChest(Pos p) const {
        const Decor* d = L().decorAt(p);
        const Monster* m = L().monsterAt(p);
        return (d && d->kind == DecorKind::Chest && !d->used) || (m && !m->awake && m->kind == MonsterKind::Mimic);
    }

    // First step of a shortest path to a cell satisfying `goal`; the goal itself may be solid (a chest).
    // Careful paths go around known traps and monsters.
    template <class Goal>
    int firstStep(Goal goal, bool careful) const {
        std::vector<int> firstDir(L().cells.size(), -2);
        std::deque<Pos> open{g.player.pos};
        firstDir[idx(g.player.pos)] = -1;
        while (!open.empty()) {
            Pos p = open.front();
            open.pop_front();
            int fd = firstDir[idx(p)];
            for (int d = 0; d < 4; d++) {
                Pos n = p + DIRS[d];
                if (!L().inside(n) || firstDir[idx(n)] != -2 || !L().at(n).seen) continue;
                int dir = fd < 0 ? d : fd;
                if (goal(n)) return dir;
                if (!L().walkable(n) || (careful && L().monsterAt(n))) continue;
                if (careful && L().at(n).trapFound) continue;
                firstDir[idx(n)] = dir;
                open.push_back(n);
            }
        }
        return -1;
    }
    template <class Goal>
    int pathTo(Goal goal) const {
        int d = firstStep(goal, true);
        return d >= 0 ? d : firstStep(goal, false);
    }
    bool walk(int dir) {
        if (dir < 0) return false;
        g.act(faceOrWalk(g, dir));
        return true;
    }

    // Worth walking over to: gold, anything unknown, and gear better than what we wear.
    bool wanted(const FloorItem& f) const {
        const Player& p = g.player;
        if (std::find(dropped.begin(), dropped.end(), f.pos) != dropped.end()) return false;
        if (f.item.kind == ItemKind::Gold) return true;
        if (p.pack.size() >= PACK_SIZE) return false;
        ItemClass c = itemClass(f.item.kind);
        if (c == ItemClass::Weapon) return weaponScore(f.item) > weaponScore(p.weapon) + 0.1f;
        if (c == ItemClass::Armor) return !p.hasArmor || armorScore(f.item) > armorScore(p.armor);
        return !isJunk(f.item);
    }
    bool isJunk(const Item& it) const {
        const Player& p = g.player;
        ItemClass c = itemClass(it.kind);
        bool known = g.identified(it.kind);
        return (c == ItemClass::Weapon && weaponScore(it) <= weaponScore(p.weapon)) || (c == ItemClass::Armor && p.hasArmor && armorScore(it) <= armorScore(p.armor)) ||
               (c == ItemClass::Wand && known && it.charges == 0) || (c == ItemClass::Ring && known && p.hasRing) ||
               (known && (it.kind == ItemKind::PotionPoison || it.kind == ItemKind::PotionDarkness || it.kind == ItemKind::ScrollSummoning));
    }

    void newLevel() {
        dropped.clear();
        depth = L().depth;
        onLevel = restTurns = searchLeft = 0;
        searched.assign(L().cells.size(), 0);
    }

    bool emergency(bool danger) {
        Player& p = g.player;
        bool low = p.hp < p.maxHp * 2 / 5 || p.poison >= p.hp;
        if (!low) return false;
        if (use(ItemKind::PotionHealing)) return true;
        if (danger && p.hp < p.maxHp / 4 && use(ItemKind::ScrollTeleport)) return true;
        if (useUnknown(ItemClass::Potion)) return true;
        return danger && p.hp < p.maxHp / 4 && useUnknown(ItemClass::Scroll);
    }

    bool manageGear() {
        Player& p = g.player;
        if (p.torch < 150 && use(ItemKind::OilFlask)) return true;
        if (onLevel < 3 && (use(ItemKind::ScrollMapping) || use(ItemKind::PotionSight))) return true;
        for (size_t i = 0; i < p.pack.size(); i++) {
            const Item& it = p.pack[i];
            ItemClass c = itemClass(it.kind);
            if (c == ItemClass::Weapon && weaponScore(it) > weaponScore(p.weapon) + 0.1f) return g.use(int(i));
            if (c == ItemClass::Armor && (!p.hasArmor || armorScore(it) > armorScore(p.armor))) return g.use(int(i));
            if (c == ItemClass::Ring && (!p.hasRing || (g.identified(it.kind) && it.kind != ItemKind::RingLight && p.ring.kind == ItemKind::RingLight)))
                return g.use(int(i));
            bool boost = it.kind == ItemKind::PotionStrength || it.kind == ItemKind::ScrollEnchantWeapon ||
                         (it.kind == ItemKind::ScrollEnchantArmor && p.hasArmor) || (it.kind == ItemKind::ScrollIdentify && hasUnknown());
            if (boost && g.identified(it.kind)) return g.use(int(i));
        }
        for (size_t i = 0; i < p.pack.size(); i++)
            if (isJunk(p.pack[i])) return dropped.push_back(p.pos), g.drop(int(i));
        return false;
    }

    bool hasUnknown() const {
        return std::any_of(g.player.pack.begin(), g.player.pack.end(), [&](const Item& it) { return !g.identified(it.kind); });
    }

    // Try unknown things while it's quiet and healthy.
    bool experiment() {
        Player& p = g.player;
        if (p.hp < p.maxHp * 3 / 5) return false;
        if (useUnknown(ItemClass::Scroll)) return true;
        return p.hp > p.maxHp * 3 / 4 && useUnknown(ItemClass::Potion);
    }

    // Monster in a straight, clear line from the player, or -1.
    int lineTo(const Monster& m) const {
        Pos d = m.pos - g.player.pos;
        if (d.x != 0 && d.y != 0) return -1;
        int dir = dirTowards(g.player.pos, m.pos), steps = std::abs(d.x) + std::abs(d.y);
        if (steps > 8) return -1;
        for (Pos p = g.player.pos + DIRS[dir]; !(p == m.pos); p = p + DIRS[dir])
            if (!L().walkable(p) || L().opaque(p)) return -1;
        return dir;
    }

    bool zap(const std::vector<const Monster*>& seen) {
        for (size_t i = 0; i < g.player.pack.size(); i++) {
            const Item& w = g.player.pack[i];
            if (itemClass(w.kind) != ItemClass::Wand || w.kind == ItemKind::WandBlink || (g.identified(w.kind) && w.charges == 0)) continue;
            for (const Monster* m : seen) {
                const MonsterDef& d = monsterDef(m->kind);
                bool worth = m->hp >= 14 || (d.flags & MF_RANGED);
                if (w.kind == ItemKind::WandSlow && g.identified(w.kind)) worth = m->hp >= 30 && m->slowed == 0;
                int dir = lineTo(*m);
                if (!worth || dir < 0) continue;
                if (dir != g.player.facing) return g.act(faceOrWalk(g, dir)), true;
                return g.use(int(i));
            }
        }
        return false;
    }

    bool fight(const std::vector<const Monster*>& seen) {
        Player& p = g.player;
        int adjacent = 0;
        bool boss = false;
        for (const Monster* m : seen) adjacent += manhattan(m->pos, p.pos) == 1, boss |= m->kind == MonsterKind::Lich;
        if (boss && p.haste == 0 && use(ItemKind::PotionHaste)) return true;
        if (adjacent >= 2 && use(ItemKind::ScrollFire)) return true;
        if (adjacent >= 3 && use(ItemKind::ScrollFear)) return true;
        if (zap(seen)) return true;
        for (int d = 0; d < 4; d++)
            if (const Monster* m = L().monsterAt(p.pos + DIRS[d]); m && m->awake) {
                waited = 0;
                Action a = faceOrWalk(g, d);
                g.act(a == Action::Forward ? Action::Attack : a);
                return true;
            }
        // Close in on whatever is shooting at us; let melee monsters come to us.
        const Monster* target = nullptr;
        for (const Monster* m : seen)
            if (!target || manhattan(m->pos, p.pos) < manhattan(target->pos, p.pos)) target = m;
        if (!target) return false;
        if (monsterDef(target->kind).flags & MF_RANGED) return walk(pathTo([&](Pos q) { return q == target->pos; }));
        if (++waited > 6) return false;  // it isn't coming; carry on
        g.act(Action::Wait);
        return true;
    }

    bool deadEnd(Pos q) const {
        if (L().tile(q) != Tile::Floor || searched[idx(q)]) return false;
        int exits = 0;
        for (Pos d : DIRS) exits += L().walkable(q + d);
        return exits == 1;
    }

    bool explore() {
        Player& p = g.player;
        auto hasItem = [&](Pos q) {
            if (L().at(q).trapFound) return false;
            for (const FloorItem& f : L().items)
                if (f.pos == q && wanted(f)) return true;
            return false;
        };
        auto frontier = [&](Pos q) {
            if (!L().walkable(q)) return false;
            for (Pos d : DIRS)
                if (L().inside(q + d) && !L().at(q + d).seen) return true;
            return false;
        };
        auto fountain = [&](Pos q) {
            const Decor* d = L().decorAt(q);
            return d && d->kind == DecorKind::Fountain && !d->used && p.hp < p.maxHp * 7 / 10;
        };
        if (onLevel < 2500) {
            if (walk(pathTo(hasItem))) return true;
            if (walk(pathTo([&](Pos q) { return looksLikeChest(q) || fountain(q); }))) return true;
            if (walk(pathTo(frontier))) return true;
            if (searchLeft > 0) return searchLeft--, g.act(Action::Wait), true;
            if (deadEnd(p.pos)) return searched[idx(p.pos)] = 1, searchLeft = 6, true;
            if (walk(pathTo([&](Pos q) { return deadEnd(q); }))) return true;
        }
        if (g.onStairs()) return g.act(Action::Descend), true;
        if (walk(pathTo([&](Pos q) { return L().tile(q) == Tile::StairsDown; }))) return true;
        if (!L().at(L().stairs).seen) return walk(firstStep(frontier, false));
        // The Lich's floor has no stairs: go find it.
        return walk(pathTo([&](Pos q) { return q == L().stairs; }));
    }

    void step() {
        if (L().depth != depth) newLevel();
        onLevel++;
        Player& p = g.player;
        std::vector<const Monster*> seen = threats();
        bool danger = !seen.empty();
        if (emergency(danger) || manageGear()) return;
        if (!danger) waited = 0;
        if (danger) {
            restTurns = 0;
            if (fight(seen)) return;
        }
        if (!danger && experiment()) return;
        bool hurt = p.hp < p.maxHp * 3 / 5 || (L().depth == FINAL_DEPTH && p.hp < p.maxHp * 9 / 10);
        if (!danger && hurt && restTurns < 250) {
            restTurns++;
            g.act(Action::Wait);
            return;
        }
        if (p.hp >= p.maxHp) restTurns = 0;
        if (!explore()) g.act(Action::Wait);
    }
};

struct RunResult {
    Outcome outcome;
    int depth, level, turn, score;
    std::string cause;
};

std::map<std::string, long> tally;  // things that happened, summed over all runs

void count(const Event& e) {
    switch (e.type) {
    case EventType::Pickup:
        if (e.amount == int(ItemKind::PotionHealing)) tally["healing potions found"]++;
        if (e.amount == int(ItemKind::OilFlask)) tally["oil flasks found"]++;
        break;
    case EventType::UseItem:
        if (e.amount == int(ItemKind::PotionHealing)) tally["healing potions drunk"]++;
        if (e.amount == int(ItemKind::OilFlask)) tally["oil flasks burned"]++;
        break;
    case EventType::ChestOpened: tally["chests opened"]++; break;
    case EventType::FountainUsed: tally["fountains drunk"]++; break;
    case EventType::TrapTriggered: tally["traps sprung"]++; break;
    case EventType::TrapFound: tally["traps found"]++; break;
    case EventType::SecretFound: tally["secret doors found"]++; break;
    case EventType::Fall: tally["falls"]++; break;
    case EventType::MonsterAppear: tally["monsters appeared"]++; break;
    default: break;
    }
}

RunResult play(uint32_t seed) {
    Game g;
    g.start(seed);
    Bot bot{g};
    int dark = 0;
    for (int steps = 0; g.outcome == Outcome::Playing && steps < 80000; steps++) {
        int turn = g.turn;
        bot.step();
        for (const Event& e : g.takeEvents()) count(e);
        dark += g.turn > turn && g.player.torch == 0;
    }
    tally["turns in the dark"] += dark;
    std::string cause = g.outcome == Outcome::Won ? "victory" : g.outcome == Outcome::Dead ? g.deathCause.substr(0, g.deathCause.find(" on depth")) : "timed out";
    return {g.outcome, g.level.depth, g.player.level, g.turn, g.score(), cause};
}

// ASCII plan of one floor, for eyeballing the generator.
void dump(const Level& L) {
    for (int y = 0; y < L.height; y++) {
        std::string row;
        for (int x = 0; x < L.width; x++) {
            Pos p{x, y};
            const Cell& c = L.at(p);
            char ch = " .+>S"[int(c.tile)];
            if (c.tile == Tile::Wall) {
                bool edge = false;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) edge |= L.inside({x + dx, y + dy}) && !solid(L.tile({x + dx, y + dy}));
                ch = edge ? '#' : ' ';
            }
            if (c.trap != TrapKind::None) ch = '^';
            if (const Decor* d = L.decorAt(p)) ch = "*O,CF"[int(d->kind)];
            for (const FloorItem& f : L.items)
                if (f.pos == p) ch = f.item.kind == ItemKind::Gold ? '$' : '!';
            if (const Monster* m = L.monsterAt(p)) ch = m->kind == MonsterKind::Mimic ? 'm' : m->kind == MonsterKind::Gargoyle ? 'g' : m->kind == MonsterKind::Lich ? 'L' : 'M';
            if (p == L.start) ch = '@';
            row += ch;
        }
        std::printf("%s\n", row.c_str());
    }
}

// Generates many floors and checks the generator's promises; prints totals.
int check(int seeds) {
    int bad = 0, floors = 0;
    std::map<std::string, long> count;
    for (uint32_t s = 1; s <= uint32_t(seeds); s++)
        for (int depth = 1; depth <= FINAL_DEPTH; depth++) {
            Level L = generateLevel(depth, s);
            floors++;
            auto fail = [&](const char* what, Pos p) {
                if (bad++ < 12) std::printf("seed %u depth %d: %s at %d,%d\n", s, depth, what, p.x, p.y);
            };
            std::vector<int> dist = distanceMap(L, L.start);
            Level open = L;  // secret doors found
            for (Cell& c : open.cells)
                if (c.tile == Tile::SecretDoor) c.tile = Tile::Door;
            std::vector<int> all = distanceMap(open, L.start);
            auto at = [&](const std::vector<int>& d, Pos p) { return d[size_t(p.y) * L.width + p.x]; };
            if (at(dist, L.stairs) < 0) fail("stairs unreachable", L.stairs);
            count["stair distance"] += at(dist, L.stairs);
            for (int y = 0; y < L.height; y++)
                for (int x = 0; x < L.width; x++) {
                    Pos p{x, y};
                    Tile t = L.tile(p);
                    if (t == Tile::Door || t == Tile::SecretDoor) {
                        bool ew = solid(L.tile(p + DIRS[1])) && solid(L.tile(p + DIRS[3])) && !solid(L.tile(p + DIRS[0])) && !solid(L.tile(p + DIRS[2]));
                        bool ns = solid(L.tile(p + DIRS[0])) && solid(L.tile(p + DIRS[2])) && !solid(L.tile(p + DIRS[1])) && !solid(L.tile(p + DIRS[3]));
                        if (!ew && !ns) fail("door not in a doorway", p);
                        count[t == Tile::Door ? "doors" : "secret doors"]++;
                    }
                    if (L.walkable(p) && at(all, p) < 0) fail("floor cut off", p);
                    if (L.walkable(p) && at(dist, p) < 0) count["hidden cells"]++;
                    if (L.at(p).trap != TrapKind::None) count["traps"]++;
                    int exits = 0;
                    for (Pos d : DIRS) exits += L.walkable(p + d);
                    if (t == Tile::Floor && exits == 1 && !L.decorAt(p)) count["dead ends"]++;
                }
            for (const Decor& d : L.decor) {
                if (d.kind == DecorKind::Chest) count["chests"]++;
                if (d.kind == DecorKind::Fountain) count["fountains"]++;
            }
            for (const Monster& m : L.monsters) {
                count["monsters"]++;
                if (m.kind == MonsterKind::Mimic) count["mimics"]++;
                if (m.kind == MonsterKind::Gargoyle) count["gargoyles"]++;
                if (at(all, m.pos) < 0) fail("monster walled in", m.pos);
            }
            for (const FloorItem& f : L.items) {
                count["items"]++;
                if (f.item.kind == ItemKind::PotionHealing) count["healing potions"]++;
                if (f.item.kind == ItemKind::OilFlask) count["oil flasks"]++;
                if (at(all, f.pos) < 0) fail("item walled in", f.pos);
            }
            if (depth == FINAL_DEPTH && !(L.monsterAt(L.stairs) && L.monsterAt(L.stairs)->kind == MonsterKind::Lich)) fail("no Lich", L.stairs);
        }
    std::printf("%d floors, %d problems\n", floors, bad);
    for (auto& [k, v] : count) std::printf("  %-16s %.2f per floor\n", k.c_str(), double(v) / floors);
    return bad ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    int runs = 200;
    uint32_t seed = 1;
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        bool more = i + 1 < argc;
        if (!std::strcmp(argv[i], "--check") && more) return check(std::atoi(argv[i + 1]));
        if (!std::strcmp(argv[i], "--dump") && i + 2 < argc) return dump(generateLevel(std::atoi(argv[i + 2]), uint32_t(std::atoi(argv[i + 1])))), 0;
        if (!std::strcmp(argv[i], "--replay") && more) {
            RunResult a = play(uint32_t(std::atoi(argv[i + 1]))), b = play(uint32_t(std::atoi(argv[i + 1])));
            bool same = a.turn == b.turn && a.score == b.score && a.cause == b.cause;
            std::printf("%s: depth %d, turn %d, score %d, %s\n", same ? "identical" : "DIFFERENT", a.depth, a.turn, a.score, a.cause.c_str());
            return same ? 0 : 1;
        }
        if (!std::strcmp(argv[i], "--runs") && more) runs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--seed") && more) seed = uint32_t(std::strtoul(argv[++i], nullptr, 10));
        else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
    }

    int wins = 0;
    long totalDepth = 0, totalTurns = 0, totalLevel = 0, totalScore = 0;
    std::map<int, int> depths;
    std::map<std::string, int> causes;
    for (int r = 0; r < runs; r++) {
        RunResult res = play(seed + uint32_t(r));
        wins += res.outcome == Outcome::Won;
        totalDepth += res.depth;
        totalTurns += res.turn;
        totalLevel += res.level;
        totalScore += res.score;
        depths[res.depth]++;
        causes[res.cause]++;
        if (verbose)
            std::printf("run %3d seed %u: depth %d, level %d, turn %d, score %d, %s\n", r, seed + r, res.depth, res.level, res.turn, res.score, res.cause.c_str());
    }

    std::printf("%d runs: %d wins (%.1f%%), mean depth %.2f, mean level %.1f, mean turns %.0f, mean score %.0f\n", runs, wins, 100.0 * wins / runs,
                double(totalDepth) / runs, double(totalLevel) / runs, double(totalTurns) / runs, double(totalScore) / runs);
    std::printf("final depth:");
    for (auto& [d, n] : depths) std::printf("  d%d %d", d, n);
    std::printf("\noutcomes:\n");
    std::vector<std::pair<int, std::string>> sorted;
    for (auto& [c, n] : causes) sorted.push_back({n, c});
    std::sort(sorted.rbegin(), sorted.rend());
    for (auto& [n, c] : sorted) std::printf("  %4d  %4.1f%%  %s\n", n, 100.0 * n / runs, c.c_str());
    std::printf("per floor reached:\n");
    for (auto& [k, v] : tally) std::printf("  %-22s %.2f\n", k.c_str(), double(v) / double(totalDepth));
}
