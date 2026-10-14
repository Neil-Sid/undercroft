#pragma once
// Data tables for every monster and item. Pure data: no rendering, no Windows.
// The enums are shared contracts: presentation code indexes models, sounds and
// icons by them, so add entries only at the marked places and never reorder.
#include <cstdint>

enum class MonsterKind : uint8_t {
    Rat, Bat, Skeleton, Slime, Ghoul, Spider, Cultist, Wraith, Knight, Ogre, Imp, Gargoyle, Mimic,
    Lich,  // the boss stays last
    Count
};

enum MonsterFlag : uint32_t {
    MF_ERRATIC = 1 << 0,      // flutters about at random half the time
    MF_RANGED = 1 << 1,       // casts bolts from a distance
    MF_POISON = 1 << 2,       // bites poison
    MF_DRAIN_LIGHT = 1 << 3,  // hits burn torch fuel
    MF_REGEN = 1 << 4,        // heals each turn
    MF_BOSS = 1 << 5,
    MF_UNDEAD = 1 << 6,
    MF_DORMANT = 1 << 7,      // sits motionless (statue, chest) until the player is adjacent
    MF_SUMMONER = 1 << 8,     // raises skeletons
    MF_COWARD = 1 << 9,       // flees when badly hurt
};

struct MonsterDef {
    const char* name;
    int hp, accuracy, defense, damageMin, damageMax;
    int speed;  // 100 = one action per player turn
    int xp, minDepth, maxDepth, frequency;
    uint32_t flags;
};

enum class ItemClass : uint8_t { Weapon, Armor, Potion, Scroll, Wand, Ring, Oil, Gold };

enum class ItemKind : uint8_t {
    Dagger, Sword, Mace, Axe, Spear, Greatsword,
    LeatherArmor, ChainMail, PlateArmor,
    PotionHealing, PotionStrength, PotionSight, PotionPoison, PotionHaste, PotionDarkness,
    ScrollTeleport, ScrollMapping, ScrollEnchantWeapon, ScrollEnchantArmor, ScrollIdentify, ScrollFire, ScrollFear, ScrollSummoning,
    WandLightning, WandSlow, WandBlink,
    RingRegeneration, RingProtection, RingLight,
    OilFlask, Gold,
    Count
};

constexpr int POTION_FIRST = int(ItemKind::PotionHealing), POTION_COUNT = 6;
constexpr int SCROLL_FIRST = int(ItemKind::ScrollTeleport), SCROLL_COUNT = 8;
constexpr int WAND_FIRST = int(ItemKind::WandLightning), WAND_COUNT = 3;
constexpr int RING_FIRST = int(ItemKind::RingRegeneration), RING_COUNT = 3;

struct ItemDef {
    const char* name;
    ItemClass cls;
    int damageMin, damageMax, accuracy;  // weapons
    int defense;                         // armour
    int minDepth, frequency;             // spawn table; frequency 0 never spawns loose
    const char* blurb;
};

struct Item {
    ItemKind kind{};
    int count = 1;    // gold amount; 1 for everything else
    int enchant = 0;  // weapons, armour and rings
    int charges = 0;  // wands
};

const MonsterDef& monsterDef(MonsterKind k);
const ItemDef& itemDef(ItemKind k);
inline ItemClass itemClass(ItemKind k) { return itemDef(k).cls; }
