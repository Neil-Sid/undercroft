#include "game.h"
#include <algorithm>
#include <cctype>
#include <utility>

namespace {

constexpr uint32_t BAD = 0xE05040, GOOD = 0x9FD08A, GOLD = 0xF0C060, LOOT = 0xA8C8F0, DIM = 0x8A8070;

const char* POTION_COLOURS[] = {"murky", "crimson", "amber", "violet", "milky", "ochre", "teal", "black"};
const char* SYLLABLES[] = {"xan", "vor", "eth", "mul", "kra", "zo", "uth", "bel", "nar", "qua", "ith", "dros"};
const char* WAND_WOODS[] = {"oak", "bone", "iron", "glass", "ebony", "yew"};
const char* RING_STONES[] = {"jade", "opal", "garnet", "onyx", "amber", "moonstone"};
const char* TRAP_NAMES[] = {"", "spike trap", "dart trap", "alarm plate", "pit", "teleport rune"};

std::string the(MonsterKind k) {
    const char* name = monsterDef(k).name;
    return name[0] == 't' && name[1] == 'h' && name[2] == 'e' ? name : std::string("the ") + name;
}

std::string capitalise(std::string s) {
    if (!s.empty()) s[0] = char(std::toupper(s[0]));
    return s;
}

std::string article(const std::string& noun) {
    bool vowel = std::string("aeiou").find(noun[0]) != std::string::npos;
    return (vowel ? "an " : "a ") + noun;
}

bool dormant(const Monster& m) { return !m.awake && (monsterDef(m.kind).flags & MF_DORMANT); }

Pos scaledDir(int d, int k) { return {DIRS[d].x * k, DIRS[d].y * k}; }

Decor* decorAt(Level& level, Pos p) {
    for (Decor& d : level.decor)
        if (d.pos == p) return &d;
    return nullptr;
}

}  // namespace

void Game::start(uint32_t s, int depth) {
    seed = s;
    rng = Rng(s);
    player = Player{};
    log.clear();
    events.clear();
    outcome = Outcome::Playing;
    deathCause.clear();
    turn = 0;
    std::fill(std::begin(known), std::end(known), false);
    for (int k = 0; k < int(ItemKind::Count); k++) {
        ItemClass c = itemClass(ItemKind(k));
        known[k] = c != ItemClass::Potion && c != ItemClass::Scroll && c != ItemClass::Wand && c != ItemClass::Ring;
    }

    std::vector<int> colours{0, 1, 2, 3, 4, 5, 6, 7}, woods{0, 1, 2, 3, 4, 5}, stones{0, 1, 2, 3, 4, 5};
    rng.shuffle(colours);
    rng.shuffle(woods);
    rng.shuffle(stones);
    for (int i = 0; i < POTION_COUNT; i++) appearance[POTION_FIRST + i] = colours[size_t(i)];
    for (int i = 0; i < SCROLL_COUNT; i++) {
        auto title = [&](int v) { return v % 1728; };  // the name uses three of 12 syllables
        do appearance[SCROLL_FIRST + i] = int(rng.next() & 0xFFFF);
        while (std::any_of(appearance + SCROLL_FIRST, appearance + SCROLL_FIRST + i, [&](int o) { return title(o) == title(appearance[SCROLL_FIRST + i]); }));
    }
    for (int i = 0; i < WAND_COUNT; i++) appearance[WAND_FIRST + i] = woods[size_t(i)];
    for (int i = 0; i < RING_COUNT; i++) appearance[RING_FIRST + i] = stones[size_t(i)];

    player.pack.push_back({ItemKind::PotionHealing});
    player.pack.push_back({ItemKind::OilFlask});
    enterLevel(depth);
    say("You descend into the Undercroft. Somewhere below, the Lich waits.", GOLD);
}

void Game::enterLevel(int depth) {
    level = generateLevel(depth, seed);
    player.pos = level.start;
    // Face down the longest clear view.
    int bestView = -1;
    for (int d = 0; d < 4; d++) {
        int view = 0;
        while (view < 12 && !level.opaque(player.pos + scaledDir(d, view + 1))) view++;
        if (view > bestView) bestView = view, player.facing = d;
    }
    wanderTimer = rng.range(120, 200);
    wanderers = 0;
    refreshView();
}

std::vector<Event> Game::takeEvents() { return std::exchange(events, {}); }

void Game::say(const std::string& text, uint32_t color) {
    log.push_back({capitalise(text), color, turn});
    if (log.size() > 200) log.erase(log.begin());
}

bool Game::identified(ItemKind k) const { return known[int(k)]; }

std::string Game::itemName(const Item& item) const {
    const ItemDef& d = itemDef(item.kind);
    switch (d.cls) {
    case ItemClass::Gold: return std::to_string(item.count) + " gold";
    case ItemClass::Weapon:
    case ItemClass::Armor: return item.enchant ? "+" + std::to_string(item.enchant) + " " + d.name : d.name;
    case ItemClass::Potion:
        return known[int(item.kind)] ? d.name : std::string(POTION_COLOURS[appearance[int(item.kind)]]) + " potion";
    case ItemClass::Scroll: {
        if (known[int(item.kind)]) return d.name;
        int a = appearance[int(item.kind)];
        return std::string("scroll titled '") + SYLLABLES[a % 12] + SYLLABLES[(a / 12) % 12] + " " + SYLLABLES[(a / 144) % 12] + "'";
    }
    case ItemClass::Wand: {
        std::string name = known[int(item.kind)] ? d.name : std::string(WAND_WOODS[appearance[int(item.kind)]]) + " wand";
        return known[int(item.kind)] ? name + " (" + std::to_string(item.charges) + ")" : name;
    }
    case ItemClass::Ring:
        if (!known[int(item.kind)]) return std::string(RING_STONES[appearance[int(item.kind)]]) + " ring";
        return item.enchant ? "+" + std::to_string(item.enchant) + " " + d.name : d.name;
    default: return d.name;
    }
}

std::string Game::itemInfo(const Item& item) const {
    const ItemDef& d = itemDef(item.kind);
    if (d.cls == ItemClass::Weapon)
        return "Damage " + std::to_string(d.damageMin + item.enchant) + "-" + std::to_string(d.damageMax + item.enchant) + ", accuracy " +
               (d.accuracy + item.enchant >= 0 ? "+" : "") + std::to_string(d.accuracy + item.enchant) + ". " + d.blurb;
    if (d.cls == ItemClass::Armor) return "Defense " + std::to_string(d.defense + item.enchant) + ". " + d.blurb;
    if (!known[int(item.kind)]) return "You don't know what this does. Try it and see.";
    return d.blurb;
}

int Game::attackAccuracy() const {
    int dark = player.torch > 0 ? 0 : 4;  // swinging at shapes in the dark
    return player.level + itemDef(player.weapon.kind).accuracy + player.weapon.enchant - dark;
}
int Game::damageMin() const { return itemDef(player.weapon.kind).damageMin + player.weapon.enchant + player.strength; }
int Game::damageMax() const { return itemDef(player.weapon.kind).damageMax + player.weapon.enchant + player.strength; }
int Game::defense() const {
    int ring = player.hasRing && player.ring.kind == ItemKind::RingProtection ? 2 + player.ring.enchant : 0;
    return (player.hasArmor ? itemDef(player.armor.kind).defense + player.armor.enchant : 0) + player.level / 3 + ring;
}
int Game::xpForNextLevel() const { return 10 * player.level * (player.level + 1); }
int Game::torchRadius() const {
    int ring = player.hasRing && player.ring.kind == ItemKind::RingLight ? 1 + player.ring.enchant : 0;
    return player.torch > 0 ? 3 + ring + (player.torch * 3 + player.torchMax - 1) / player.torchMax : 1 + ring;
}

int Game::score() const {
    return player.gold + 100 * level.depth + 10 * player.kills + 50 * player.level + (outcome == Outcome::Won ? 5000 : 0);
}

bool Game::act(Action a) {
    if (outcome != Outcome::Playing) return false;
    int facing = player.facing;
    switch (a) {
    case Action::TurnLeft:
    case Action::TurnRight:
        player.facing = (facing + (a == Action::TurnLeft ? 3 : 1)) % 4;
        emit({EventType::PlayerTurn, -1, player.pos, player.pos, player.facing});
        return false;
    case Action::Attack:
        playerAttack(player.pos + DIRS[facing]);
        endTurn();
        return true;
    case Action::Wait:
        search(50, 35);
        endTurn();
        return true;
    case Action::Descend:
        if (!onStairs()) {
            say("There are no stairs here.");
            return false;
        }
        turn++;
        enterLevel(level.depth + 1);
        emit({EventType::Descend, -1, player.pos, player.pos, level.depth});
        say(level.depth == FINAL_DEPTH ? "The air is cold and still. This is the Lich's floor." : "You descend to depth " + std::to_string(level.depth) + ".",
            level.depth == FINAL_DEPTH ? BAD : GOLD);
        return true;
    default: break;
    }

    int dir = a == Action::Forward ? facing : a == Action::Back ? (facing + 2) % 4 : a == Action::StrafeLeft ? (facing + 3) % 4 : (facing + 1) % 4;
    Pos to = player.pos + DIRS[dir];
    Monster* m = level.monsterAt(to);
    Decor* decor = decorAt(level, to);
    if (m && dormant(*m)) {
        if (a != Action::Forward) {
            emit({EventType::PlayerBlocked, -1, player.pos, to});
            return false;
        }
        reveal(*m);  // you reach for the chest, or brush the statue
        endTurn();
        return true;
    }
    if (m) {
        if (a != Action::Forward) {
            say(capitalise(the(m->kind)) + " is in the way.");
            return false;
        }
        playerAttack(to);
        endTurn();
        return true;
    }
    if (decor && a == Action::Forward && !decor->used && (decor->kind == DecorKind::Chest || decor->kind == DecorKind::Fountain)) {
        if (decor->kind == DecorKind::Chest) openChest(*decor);
        else drinkFountain(*decor);
        endTurn();
        return true;
    }
    if (level.tile(to) == Tile::Door && !level.at(to).doorOpen) {
        level.at(to).doorOpen = true;
        emit({EventType::DoorOpen, -1, player.pos, to});
        endTurn();
        return true;
    }
    if (!level.walkable(to)) {
        emit({EventType::PlayerBlocked, -1, player.pos, to});
        if (decor && decor->used && a == Action::Forward) say(decor->kind == DecorKind::Chest ? "The chest is empty." : "The basin is dry.");
        return false;
    }
    emit({EventType::PlayerStep, -1, player.pos, to});
    player.pos = to;
    pickUp();
    if (level.at(to).trap != TrapKind::None) springTrap(to);
    else search(8, 4);
    endTurn();
    return true;
}

// ---- combat ----

void Game::playerAttack(Pos target) {
    Monster* m = level.monsterAt(target);
    if (!m) {
        emit({EventType::PlayerAttack, -1, player.pos, target, 0});
        return;
    }
    noise(target, 6);
    const MonsterDef& d = monsterDef(m->kind);
    int chance = std::clamp(70 + 5 * (attackAccuracy() - d.defense), 10, 95);
    if (!rng.percent(chance)) {
        emit({EventType::PlayerAttack, m->id, player.pos, target, 0});
        if (dormant(*m)) reveal(*m);
        m->awake = true;
        say("You miss " + the(m->kind) + ".");
        return;
    }
    int damage = rng.range(damageMin(), damageMax());
    bool crit = rng.percent(player.weapon.kind == ItemKind::Dagger ? 15 : 5);
    if (crit) damage *= 2;
    emit({EventType::PlayerAttack, m->id, player.pos, target, damage, crit});
    hurtMonster(*m, damage);
    if (m->hp > 0) say(crit ? "You land a crushing blow on " + the(m->kind) + "!" : "You hit " + the(m->kind) + ".");
}

void Game::hurtMonster(Monster& m, int damage) {
    if (dormant(m)) reveal(m);
    m.awake = true;
    m.hp -= damage;
    emit({EventType::MonsterHurt, m.id, m.pos, m.pos, damage});
    if (m.hp <= 0) killMonster(m);
}

void Game::killMonster(Monster& m) {
    const MonsterDef& d = monsterDef(m.kind);
    m.hp = 0;
    emit({EventType::MonsterDied, m.id, m.pos, m.pos});
    say((d.flags & MF_UNDEAD ? "You destroy " : "You kill ") + the(m.kind) + ".", GOOD);
    player.kills++;
    player.xp += d.xp;
    while (player.xp >= xpForNextLevel()) {
        player.level++;
        int gain = 4 + player.level / 3;
        player.maxHp += gain;
        player.hp += gain;
        emit({EventType::PlayerLevelUp, -1, player.pos, player.pos, player.level});
        say("You reach level " + std::to_string(player.level) + ".", GOLD);
    }
    if (d.flags & MF_BOSS) {
        outcome = Outcome::Won;
        emit({EventType::Victory, -1, {}, {}});
        say("The Lich unravels into ash. The Undercroft is still at last.", GOLD);
    }
}

void Game::hurtPlayer(int damage, const std::string& cause, int attackerId) {
    player.hp -= damage;
    emit({EventType::PlayerHurt, attackerId, player.pos, player.pos, damage});
    if (player.hp <= 0 && outcome == Outcome::Playing) {
        player.hp = 0;
        outcome = Outcome::Dead;
        deathCause = cause + " on depth " + std::to_string(level.depth);
        emit({EventType::PlayerDied, -1, {}, {}});
        say("You die...", BAD);
    }
}

void Game::heal(int amount) {
    player.hp = std::min(player.maxHp, player.hp + amount);
    player.poison = 0;
    emit({EventType::PlayerHeal, -1, player.pos, player.pos, amount});
}

// ---- the dungeon's furniture ----

void Game::pickUp() {
    for (auto it = level.items.begin(); it != level.items.end();) {
        if (!(it->pos == player.pos)) {
            ++it;
            continue;
        }
        if (it->item.kind == ItemKind::Gold) {
            player.gold += it->item.count;
            say("You pick up " + std::to_string(it->item.count) + " gold.", GOLD);
        } else if (int(player.pack.size()) < PACK_SIZE) {
            player.pack.push_back(it->item);
            say("You pick up " + article(itemName(it->item)) + ".", LOOT);
        } else {
            say("Your pack is full.");
            ++it;
            continue;
        }
        emit({EventType::Pickup, -1, player.pos, player.pos, int(it->item.kind)});
        it = level.items.erase(it);
    }
}

void Game::openChest(Decor& chest) {
    chest.used = true;
    emit({EventType::ChestOpened, -1, player.pos, chest.pos});
    say("You heave the lid open.");
    for (int i = rng.percent(35) ? 2 : 1; i > 0; i--) spawnItem(randomItem(level.depth + 1, rng), player.pos);
    if (rng.percent(60)) spawnItem({ItemKind::Gold, rng.range(8, 20) * level.depth}, player.pos);
    pickUp();
}

void Game::drinkFountain(Decor& fountain) {
    fountain.used = true;
    emit({EventType::FountainUsed, -1, player.pos, fountain.pos});
    int roll = rng.range(1, 100);
    if (roll <= 35) {
        heal(std::max(10, player.maxHp / 2));
        say("The water is cold and clean. Your wounds close.", GOOD);
    } else if (roll <= 55) {
        revealMap();
        say("In the still water you see the whole floor laid out.", GOOD);
    } else if (roll <= 70) {
        player.haste += 15;
        say("The water fizzes on your tongue. The world slows around you.", GOOD);
    } else if (roll <= 85) {
        player.poison += 6;
        say("The water is foul. You retch.", BAD);
    } else {
        say("Something climbs out of the basin!", BAD);
        summonNear(player.pos, rng.range(1, 2));
    }
    say("The fountain runs dry.", DIM);
}

void Game::springTrap(Pos at) {
    Cell& c = level.at(at);
    c.trapFound = true;
    emit({EventType::TrapTriggered, -1, at, at, int(c.trap)});
    switch (c.trap) {
    case TrapKind::Spikes:
        say("Spikes punch up through the floor!", BAD);
        hurtPlayer(rng.range(2, 4) + level.depth, "Impaled by a spike trap", -1);
        break;
    case TrapKind::Dart:
        say("A dart flicks out of the wall. You feel sick.", BAD);
        player.poison += 3 + level.depth / 2;
        hurtPlayer(rng.range(1, 3), "Struck by a poisoned dart", -1);
        break;
    case TrapKind::Alarm:
        say("A bell clangs somewhere in the dark!", BAD);
        for (Monster& m : level.monsters)
            if (!m.awake && !(monsterDef(m.kind).flags & MF_DORMANT)) notice(m);
        break;
    case TrapKind::Pit: fall(); break;
    case TrapKind::Teleport:
        say("Runes flare under your feet!", BAD);
        teleportPlayer();
        break;
    default: break;
    }
}

void Game::fall() {
    say("The floor gives way beneath you!", BAD);
    enterLevel(level.depth + 1);
    emit({EventType::Fall, -1, player.pos, player.pos, level.depth});
    say("You crash down onto depth " + std::to_string(level.depth) + ".", BAD);
    if (level.depth == FINAL_DEPTH) say("The air is cold and still. This is the Lich's floor.", BAD);
    hurtPlayer(rng.range(3, 6), "Fell into a pit", -1);
}

// Look around the cells next to you for traps and hidden doors.
void Game::search(int trapChance, int secretChance) {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            Pos p = player.pos + Pos{dx, dy};
            if (!level.inside(p)) continue;
            Cell& c = level.at(p);
            if (c.trap != TrapKind::None && !c.trapFound && rng.percent(trapChance)) {
                c.trapFound = true;
                emit({EventType::TrapFound, -1, player.pos, p});
                say("You spot " + article(TRAP_NAMES[int(c.trap)]) + ".", LOOT);
            }
            if (c.tile == Tile::SecretDoor && rng.percent(secretChance)) {
                c.tile = Tile::Door;
                c.doorOpen = false;
                emit({EventType::SecretFound, -1, player.pos, p});
                say("You find a secret door!", GOLD);
            }
        }
}

// A random empty floor cell whose walking distance from `from` lies in the given range.
Pos Game::farCell(Pos from, int minDistance, int maxDistance, bool unseen) {
    std::vector<int> dist = distanceMap(level, from);
    std::vector<Pos> spots;
    for (int y = 0; y < level.height; y++)
        for (int x = 0; x < level.width; x++) {
            Pos p{x, y};
            int d = dist[size_t(y) * level.width + x];
            if (d < minDistance || d > maxDistance || level.tile(p) != Tile::Floor || level.at(p).trap != TrapKind::None) continue;
            if (level.monsterAt(p) || level.decorAt(p) || p == player.pos || (unseen && level.at(p).visible)) continue;
            spots.push_back(p);
        }
    return spots.empty() ? Pos{-1, -1} : spots[size_t(rng.range(0, int(spots.size()) - 1))];
}

void Game::teleportPlayer() {
    Pos to = farCell(player.pos, 10, 999, false);
    if (to.x < 0) return;
    emit({EventType::Teleport, -1, player.pos, to});
    player.pos = to;
    say("Space folds, and you are elsewhere.");
    pickUp();
}

// Every so often something new arrives from the dark, already hunting.
void Game::wander() {
    if (--wanderTimer > 0) return;
    wanderTimer = rng.range(120, 180);
    if (wanderers >= 3 + level.depth / 3) return;
    Pos p = farCell(player.pos, 12, 999, true);
    if (p.x < 0) return;
    spawnMonster(randomMonster(level.depth, rng), p, true);
    wanderers++;
    say("Somewhere in the dark, something is moving.", DIM);
}

void Game::endTurn() {
    turn++;
    std::erase_if(level.monsters, [](const Monster& m) { return m.hp <= 0; });

    bool lightRing = player.hasRing && player.ring.kind == ItemKind::RingLight;
    if (player.torch > 0 && !(lightRing && turn % 2)) {
        player.torch--;
        if (player.torch == 150) say("Your torch is guttering.", BAD);
        if (player.torch == 0) say("Your torch goes out. The dark presses in.", BAD);
    }
    if (player.poison > 0) {
        player.poison--;
        hurtPlayer(1, "Succumbed to poison", -1);
    } else if (player.hp < player.maxHp) {
        bool regenRing = player.hasRing && player.ring.kind == ItemKind::RingRegeneration;
        if (turn % (regenRing ? 3 : std::max(5, 20 - player.level)) == 0) player.hp++;
    }
    if (player.haste > 0 && --player.haste == 0) say("The world speeds up again.");
    wander();

    playerDistance = distanceMap(level, player.pos);
    // Index, don't hold references: a monster's turn may summon more monsters.
    // Monsters raised during this loop are appended past `acting` and wait for the next turn.
    for (size_t i = 0, acting = level.monsters.size(); i < acting && outcome == Outcome::Playing; i++) {
        int speed = monsterDef(level.monsters[i].kind).speed;
        if (level.monsters[i].slowed > 0) level.monsters[i].slowed--, speed /= 2;
        if (player.haste > 0) speed /= 2;
        level.monsters[i].energy += speed;
        while (level.monsters[i].energy >= 100 && level.monsters[i].hp > 0 && outcome == Outcome::Playing) {
            level.monsters[i].energy -= 100;
            monsterTurn(level.monsters[i]);
        }
    }
    std::erase_if(level.monsters, [](const Monster& m) { return m.hp <= 0; });
    refreshView();
}

// ---- monsters ----

bool Game::tryMoveMonster(Monster& m, Pos to) {
    if (!level.walkable(to) || level.monsterAt(to) || to == player.pos) return false;
    m.facing = dirTowards(m.pos, to);
    if (level.tile(to) == Tile::Door && !level.at(to).doorOpen) {
        level.at(to).doorOpen = true;
        emit({EventType::DoorOpen, m.id, m.pos, to});
        return true;
    }
    emit({EventType::MonsterStep, m.id, m.pos, to});
    m.pos = to;
    return true;
}

void Game::notice(Monster& m) {
    m.awake = true;
    emit({EventType::MonsterNotice, m.id, m.pos, player.pos});
    if (monsterDef(m.kind).flags & MF_BOSS) say("The Lich turns its hollow gaze on you.", BAD);
    else if (level.at(m.pos).visible) say(capitalise(the(m.kind)) + " notices you.");
}

void Game::reveal(Monster& m) {
    m.awake = true;
    emit({EventType::MonsterAppear, m.id, m.pos, m.pos});
    say(m.kind == MonsterKind::Mimic ? "The chest grows teeth!" : "The statue grinds into motion!", BAD);
}

// Fighting is loud: sleepers within earshot wake up and come looking.
void Game::noise(Pos at, int radius) {
    for (Monster& m : level.monsters)
        if (m.hp > 0 && !m.awake && !(monsterDef(m.kind).flags & MF_DORMANT) && manhattan(m.pos, at) <= radius) notice(m);
}

void Game::meleeAttack(Monster& m) {
    const MonsterDef& d = monsterDef(m.kind);
    m.facing = dirTowards(m.pos, player.pos);
    noise(m.pos, 6);
    int chance = std::clamp(70 + 5 * (d.accuracy - defense()), 10, 95);
    if (!rng.percent(chance)) {
        emit({EventType::MonsterAttack, m.id, m.pos, player.pos, 0});
        say(capitalise(the(m.kind)) + " misses you.");
        return;
    }
    int damage = std::max(1, rng.range(d.damageMin, d.damageMax) - defense() / 3);
    emit({EventType::MonsterAttack, m.id, m.pos, player.pos, damage});
    say(capitalise(the(m.kind)) + " hits you.", BAD);
    if ((d.flags & MF_POISON) && rng.percent(35)) {
        player.poison += 4;
        say("You are poisoned.", BAD);
    }
    if (d.flags & MF_DRAIN_LIGHT) player.torch = std::max(0, player.torch - 40);
    hurtPlayer(damage, "Slain by " + std::string(d.name), m.id);
}

void Game::castBolt(Monster& m) {
    const MonsterDef& d = monsterDef(m.kind);
    m.facing = dirTowards(m.pos, player.pos);
    bool hit = rng.percent(std::clamp(60 + 5 * (d.accuracy - defense()), 10, 90));
    int damage = hit ? std::max(1, rng.range(d.damageMin, d.damageMax) - defense() / 4) : 0;
    emit({EventType::MonsterBolt, m.id, m.pos, player.pos, damage});
    if (!hit) {
        say(capitalise(the(m.kind)) + "'s bolt crackles past you.");
        return;
    }
    say(capitalise(the(m.kind)) + "'s bolt sears you.", BAD);
    hurtPlayer(damage, "Burned by " + std::string(d.name), m.id);
}

bool Game::stepAway(Monster& m) {
    auto distAt = [&](Pos p) { return level.inside(p) ? playerDistance[size_t(p.y) * level.width + p.x] : -1; };
    int here = distAt(m.pos), best = -1, bestDist = here;
    for (int i = 0; i < 4; i++) {
        Pos to = m.pos + DIRS[(turn + i) % 4];
        if (distAt(to) > bestDist && level.walkable(to) && !level.monsterAt(to)) best = (turn + i) % 4, bestDist = distAt(to);
    }
    return best >= 0 && tryMoveMonster(m, m.pos + DIRS[best]);
}

void Game::approach(Monster& m) {
    auto distAt = [&](Pos p) { return level.inside(p) ? playerDistance[size_t(p.y) * level.width + p.x] : -1; };
    int here = distAt(m.pos);
    int start = rng.range(0, 3);
    for (int i = 0; i < 4; i++) {
        Pos to = m.pos + DIRS[(start + i) % 4];
        int d2 = distAt(to);
        if (d2 >= 0 && (here < 0 || d2 < here) && tryMoveMonster(m, to)) return;
    }
}

void Game::lichBlink(Monster& m) {
    m.blinked = true;
    Pos to = farCell(player.pos, 6, 14, false);
    if (to.x < 0) return;
    emit({EventType::MonsterAppear, m.id, m.pos, to});
    m.pos = to;
    m.facing = dirTowards(to, player.pos);
    say("The Lich folds into shadow and reappears across the hall!", BAD);
    summonNear(to, 2);
}

void Game::monsterTurn(Monster& m) {
    const MonsterDef& d = monsterDef(m.kind);
    if ((d.flags & MF_REGEN) && m.hp < d.hp && turn % 2 == 0) m.hp++;

    int dist = manhattan(m.pos, player.pos);
    bool sees = level.at(m.pos).visible && dist <= 9;
    if (dormant(m)) {
        if (m.kind == MonsterKind::Gargoyle && dist == 1) reveal(m);
        return;
    }
    if (!m.awake) {
        if (sees && rng.percent(dist <= 3 ? 60 : 30)) notice(m);
        return;
    }
    if ((d.flags & MF_BOSS) && !m.blinked && m.hp * 2 < d.hp) return lichBlink(m);

    if (m.fleeing < 0) m.fleeing++;
    if ((d.flags & MF_COWARD) && m.hp * 10 < d.hp * 3 && m.fleeing == 0) m.fleeing = 8;
    if (m.fleeing > 0) {
        if (--m.fleeing == 0) m.fleeing = -15;  // catches its breath before it will run again
        if (stepAway(m) || dist != 1) return;
    }

    if ((d.flags & MF_SUMMONER) && sees && rng.percent(15)) {
        int minions = 0;
        for (const Monster& o : level.monsters) minions += o.hp > 0 && manhattan(o.pos, m.pos) <= 6;
        if (minions <= 4) {
            m.facing = dirTowards(m.pos, player.pos);
            emit({EventType::MonsterAttack, m.id, m.pos, m.pos, 0});
            say(capitalise(the(m.kind)) + " raises the dead!", BAD);
            summonNear(m.pos, rng.range(2, 3));
            return;
        }
    }

    // Casters keep a few paces away and fire from there.
    bool ranged = d.flags & MF_RANGED, clear = sees && lineOfSight(level, m.pos, player.pos);
    if (ranged && dist == 1 && rng.percent(d.flags & MF_BOSS ? 35 : 60) && stepAway(m)) return;
    if (dist == 1) return meleeAttack(m);
    if (ranged && clear && dist <= 5 && rng.percent(55)) return castBolt(m);
    if (ranged && clear && dist <= 4) return;

    if ((d.flags & MF_ERRATIC) && rng.percent(50)) {
        tryMoveMonster(m, m.pos + DIRS[rng.range(0, 3)]);
        return;
    }
    approach(m);
}

// ---- items ----

bool Game::use(int slot) {
    if (outcome != Outcome::Playing || slot < 0 || slot >= int(player.pack.size())) return false;
    Item item = player.pack[size_t(slot)];
    const ItemDef& d = itemDef(item.kind);
    auto consume = [&] { player.pack.erase(player.pack.begin() + slot); };

    switch (d.cls) {
    case ItemClass::Weapon:
        player.pack[size_t(slot)] = player.weapon;
        player.weapon = item;
        say("You ready the " + itemName(item) + ".");
        break;
    case ItemClass::Armor:
        if (player.hasArmor) player.pack[size_t(slot)] = player.armor;
        else consume();
        player.armor = item;
        player.hasArmor = true;
        say("You strap on the " + itemName(item) + ".");
        break;
    case ItemClass::Potion:
        consume();
        quaffPotion(item.kind);
        break;
    case ItemClass::Scroll:
        consume();
        readScroll(item.kind);
        break;
    case ItemClass::Wand:
        zapWand(player.pack[size_t(slot)]);
        break;
    case ItemClass::Ring:
        if (player.hasRing) player.pack[size_t(slot)] = player.ring;
        else consume();
        player.ring = item;
        player.hasRing = true;
        say("You slip on the " + itemName(item) + ".");
        identify(item.kind);
        break;
    case ItemClass::Oil:
        consume();
        player.torch = std::min(player.torchMax, player.torch + 500);
        say("You soak the torch in oil. It flares up.", GOOD);
        break;
    case ItemClass::Gold: return false;
    }
    emit({EventType::UseItem, -1, player.pos, player.pos, int(item.kind)});
    endTurn();
    return true;
}

void Game::quaffPotion(ItemKind k) {
    identify(k);
    switch (k) {
    case ItemKind::PotionHealing:
        heal(std::max(15, player.maxHp / 2));
        say("Warmth floods through you.", GOOD);
        break;
    case ItemKind::PotionStrength:
        player.strength++;
        say("Your arms thrum with new strength.", GOOD);
        break;
    case ItemKind::PotionSight:
        revealMap();
        for (int y = 0; y < level.height; y++)
            for (int x = 0; x < level.width; x++)
                if (Cell& c = level.at({x, y}); c.trap != TrapKind::None && !c.trapFound) {
                    c.trapFound = true;
                    emit({EventType::TrapFound, -1, player.pos, {x, y}});
                }
        say("The shape of the floor blooms in your mind, traps and all.", GOOD);
        break;
    case ItemKind::PotionHaste:
        player.haste += 20;
        say("Everything around you slows to a crawl.", GOOD);
        break;
    case ItemKind::PotionDarkness:
        player.torch = std::max(0, player.torch - 300);
        say("Darkness pours out of the bottle. Your torch shrinks.", BAD);
        break;
    default:
        player.poison += 8;
        say("It burns. You feel very sick.", BAD);
        break;
    }
}

void Game::readScroll(ItemKind k) {
    identify(k);
    switch (k) {
    case ItemKind::ScrollTeleport: teleportPlayer(); break;
    case ItemKind::ScrollMapping: {
        revealMap();
        int secrets = 0;
        for (int y = 0; y < level.height; y++)
            for (int x = 0; x < level.width; x++)
                if (level.tile({x, y}) == Tile::SecretDoor) {
                    level.at({x, y}).tile = Tile::Door;
                    emit({EventType::SecretFound, -1, player.pos, {x, y}});
                    secrets++;
                }
        say(secrets ? "A map unfurls in your memory, hidden doors and all." : "A map unfurls in your memory.", GOOD);
        break;
    }
    case ItemKind::ScrollEnchantWeapon:
        player.weapon.enchant++;
        say("Your " + std::string(itemDef(player.weapon.kind).name) + " glows a moment.", GOOD);
        break;
    case ItemKind::ScrollEnchantArmor:
        if (player.hasArmor) {
            player.armor.enchant++;
            say("Your " + std::string(itemDef(player.armor.kind).name) + " shimmers and hardens.", GOOD);
        } else {
            say("Your skin prickles, then nothing.");
        }
        break;
    case ItemKind::ScrollIdentify: {
        for (const Item& it : player.pack)
            if (!known[int(it.kind)]) {
                identify(it.kind);
                return;
            }
        say("You already know everything you carry.");
        break;
    }
    case ItemKind::ScrollFire: {
        emit({EventType::Explosion, -1, player.pos, player.pos, 2});
        say("Fire roars out from the page!", GOLD);
        for (size_t i = 0; i < level.monsters.size(); i++) {
            Monster& m = level.monsters[i];
            if (m.hp > 0 && manhattan(m.pos, player.pos) <= 2 && lineOfSight(level, player.pos, m.pos)) hurtMonster(m, rng.range(8, 16));
        }
        break;
    }
    case ItemKind::ScrollFear: {
        int scared = 0;
        for (Monster& m : level.monsters)
            if (m.hp > 0 && level.at(m.pos).visible && !(monsterDef(m.kind).flags & MF_BOSS) && !dormant(m)) m.fleeing = 15, m.awake = true, scared++;
        say(scared ? "A wail goes up. Your foes turn and flee." : "You hear distant screaming.", GOOD);
        break;
    }
    default:
        say("The dark answers the call.", BAD);
        summonNear(player.pos, rng.range(2, 3));
        break;
    }
}

void Game::summonNear(Pos centre, int count) {
    for (int i = 0; i < count; i++)
        for (int tries = 0; tries < 20; tries++) {
            Pos p = centre + Pos{rng.range(-2, 2), rng.range(-2, 2)};
            if (!level.walkable(p) || level.monsterAt(p) || p == player.pos) continue;
            MonsterKind kind = centre == player.pos ? randomMonster(level.depth, rng) : MonsterKind::Skeleton;
            spawnMonster(kind, p, true);
            emit({EventType::MonsterAppear, level.monsters.back().id, centre, p});
            break;
        }
}

void Game::zapWand(Item& wand) {
    if (wand.charges <= 0) {
        say("The wand sputters. It is empty.");
        return;
    }
    wand.charges--;
    identify(wand.kind);
    Pos dir = DIRS[player.facing], p = player.pos;
    if (wand.kind == ItemKind::WandBlink) {
        Pos to = player.pos;
        for (int i = 0; i < 4 && level.walkable(to + dir) && !level.monsterAt(to + dir) && !level.opaque(to + dir); i++) to = to + dir;
        emit({EventType::WandZap, -1, player.pos, to, int(wand.kind)});
        emit({EventType::Teleport, -1, player.pos, to});
        player.pos = to;
        pickUp();
        say("You blink forward.");
        return;
    }
    Pos end = p;
    std::vector<int> struck;
    for (int i = 0; i < 8; i++) {
        Pos next = end + dir;
        if (level.opaque(next) || !level.walkable(next)) break;
        end = next;
        if (Monster* m = level.monsterAt(end)) {
            struck.push_back(m->id);
            if (wand.kind == ItemKind::WandSlow) break;
        }
    }
    emit({EventType::WandZap, -1, player.pos, end, int(wand.kind)});
    if (wand.kind == ItemKind::WandLightning) say("Lightning cracks down the passage!", GOLD);
    for (int id : struck)
        for (size_t i = 0; i < level.monsters.size(); i++) {
            Monster& m = level.monsters[i];
            if (m.id != id || m.hp <= 0) continue;
            if (wand.kind == ItemKind::WandSlow) {
                if (dormant(m)) reveal(m);
                m.awake = true;
                m.slowed = 25;
                say(capitalise(the(m.kind)) + " slows to a crawl.", GOOD);
            } else {
                hurtMonster(m, rng.range(7, 15));
            }
        }
}

bool Game::drop(int slot) {
    if (outcome != Outcome::Playing || slot < 0 || slot >= int(player.pack.size())) return false;
    Item item = player.pack[size_t(slot)];
    player.pack.erase(player.pack.begin() + slot);
    level.items.push_back({nextId++, item, player.pos});
    say("You drop the " + itemName(item) + ".");
    endTurn();
    return true;
}

void Game::identify(ItemKind k) {
    if (known[int(k)]) return;
    std::string before = itemName({k});
    known[int(k)] = true;
    say("The " + before + " was " + article(itemDef(k).name) + ".", LOOT);
}

void Game::refreshView() { computeFov(level, player.pos, std::clamp(torchRadius() + 3, 3, 9)); }

void Game::revealMap() {
    for (int y = 0; y < level.height; y++)
        for (int x = 0; x < level.width; x++) {
            if (level.tile({x, y}) == Tile::Wall) continue;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (level.inside({x + dx, y + dy})) level.at({x + dx, y + dy}).seen = true;
        }
}

void Game::spawnMonster(MonsterKind kind, Pos p, bool awake) {
    Monster m;
    m.id = nextId++;
    m.kind = kind;
    m.pos = p;
    m.hp = monsterDef(kind).hp;
    m.awake = awake;
    m.facing = dirTowards(p, player.pos);
    level.monsters.push_back(m);
}

void Game::spawnItem(Item item, Pos p) { level.items.push_back({nextId++, item, p}); }
