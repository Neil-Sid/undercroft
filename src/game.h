#pragma once
// The rules of the game. Turn-based and deterministic for a given seed; the
// presentation layer reads `level`/`player` and replays `Event`s as animation.
#include "rng.h"
#include "world.h"
#include <string>
#include <vector>

enum class Action : uint8_t { Forward, Back, StrafeLeft, StrafeRight, TurnLeft, TurnRight, Attack, Wait, Descend };

enum class EventType : uint8_t {
    PlayerStep,     // from -> to
    PlayerTurn,     // amount = new facing
    PlayerBlocked,  // walked into a wall
    PlayerAttack,   // to = target cell, monster = target id or -1, amount = damage (0 = miss)
    PlayerHurt,     // monster = attacker id (-1 = poison etc), amount = damage
    PlayerHeal,     // amount
    PlayerLevelUp,  // amount = new level
    PlayerDied,
    MonsterNotice,  // monster woke up and saw you
    MonsterStep,    // monster, from -> to
    MonsterAttack,  // monster, to = target cell, amount = damage (0 = miss)
    MonsterBolt,    // monster, from -> to, amount = damage (0 = miss)
    MonsterHurt,    // monster, amount
    MonsterDied,    // monster, from = where it fell
    DoorOpen,       // to = door cell
    Pickup,         // amount = ItemKind
    UseItem,        // amount = ItemKind
    Teleport,       // from -> to
    Descend,        // amount = new depth
    Victory,
    TrapTriggered,  // to = trap cell, amount = TrapKind, monster = victim id or -1 for the player
    TrapFound,      // to = trap cell
    SecretFound,    // to = cell that is now a Door: the level mesh must be rebuilt
    ChestOpened,    // to = chest cell
    FountainUsed,   // to = fountain cell
    WandZap,        // from -> to (where the beam stops), amount = ItemKind
    MonsterAppear,  // monster appeared, revealed itself or blinked: snap its view from `from` to `to` (summoned, dormant mimic/gargoyle waking, the Lich escaping)
    Explosion,      // to = centre cell, amount = radius in cells (scroll of fire)
    Fall,           // dropped through a pit; amount = new depth (present like Descend)
};

struct Event {
    EventType type;
    int monster = -1;
    Pos from, to;
    int amount = 0;
    bool crit = false;  // PlayerAttack: the blow was a critical hit
};

struct Message {
    std::string text;
    uint32_t color;  // 0xRRGGBB
    int turn;
};

struct Player {
    Pos pos;
    int facing = 0;
    int hp = 30, maxHp = 30;
    int level = 1, xp = 0;
    int strength = 0;
    int torch = 1000, torchMax = 1000;
    Item weapon{ItemKind::Dagger};
    bool hasArmor = false;
    Item armor{ItemKind::LeatherArmor};
    bool hasRing = false;
    Item ring{ItemKind::RingLight};
    std::vector<Item> pack;
    int gold = 0, kills = 0;
    int poison = 0;  // turns of poison left
    int haste = 0;   // turns of haste left: monsters get half as many actions
};

enum class Outcome : uint8_t { Playing, Dead, Won };

constexpr int PACK_SIZE = 16;

class Game {
public:
    void start(uint32_t seed, int depth = 1);
    bool act(Action a);    // true if the action took a turn
    bool use(int slot);    // quaff, read, equip or refuel from the pack
    bool drop(int slot);
    std::vector<Event> takeEvents();

    std::string itemName(const Item& item) const;  // respects identification
    std::string itemInfo(const Item& item) const;
    bool identified(ItemKind k) const;
    int potionColour(ItemKind k) const { return appearance[int(k)]; }  // index into the shuffled colour names
    int attackAccuracy() const;
    int damageMin() const;
    int damageMax() const;
    int defense() const;
    int xpForNextLevel() const;
    int torchRadius() const;  // cells lit by the torch
    bool onStairs() const { return level.tile(player.pos) == Tile::StairsDown; }
    int score() const;  // gold, depth, kills and level, plus a big bonus for victory

    // Test harness hooks.
    void spawnMonster(MonsterKind kind, Pos p, bool awake = false);
    void spawnItem(Item item, Pos p);
    void revealMap();

    Level level;
    Player player;
    std::vector<Message> log;
    Outcome outcome = Outcome::Playing;
    std::string deathCause;
    int turn = 0;
    uint32_t seed = 0;

private:
    void enterLevel(int depth);
    void say(const std::string& text, uint32_t color = 0xC8C0B0);
    void emit(Event e) { events.push_back(e); }
    void endTurn();
    void monsterTurn(Monster& m);
    void playerAttack(Pos target);
    void hurtPlayer(int damage, const std::string& cause, int attackerId);
    void killMonster(Monster& m);
    void pickUp();
    void refreshView();
    void identify(ItemKind k);
    bool tryMoveMonster(Monster& m, Pos to);
    void zapWand(Item& wand);
    void readScroll(ItemKind k);
    void quaffPotion(ItemKind k);
    void summonNear(Pos centre, int count);
    void hurtMonster(Monster& m, int damage);
    void notice(Monster& m);
    void reveal(Monster& m);
    void noise(Pos at, int radius);
    void meleeAttack(Monster& m);
    void castBolt(Monster& m);
    bool stepAway(Monster& m);
    void approach(Monster& m);
    void lichBlink(Monster& m);
    void heal(int amount);
    void openChest(Decor& chest);
    void drinkFountain(Decor& fountain);
    void springTrap(Pos at);
    void search(int trapChance, int secretChance);
    void fall();
    void teleportPlayer();
    void wander();
    Pos farCell(Pos from, int minDistance, int maxDistance, bool unseen);

    Rng rng{1};
    std::vector<Event> events;
    std::vector<int> playerDistance;
    bool known[int(ItemKind::Count)] = {};
    int appearance[int(ItemKind::Count)] = {};
    int nextId = 10000;
    int wanderTimer = 0, wanderers = 0;
};
