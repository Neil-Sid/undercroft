#include "content.h"
#include <cstddef>

namespace {

// name, hp, acc, def, dmg, speed, xp, depths, frequency, flags
const MonsterDef MONSTERS[] = {
    {"giant rat", 6, 0, 0, 1, 3, 100, 2, 1, 3, 10, MF_COWARD},
    {"cave bat", 5, 1, 2, 1, 2, 160, 3, 1, 4, 7, MF_ERRATIC},
    {"skeleton", 14, 3, 2, 2, 5, 100, 6, 2, 6, 9, MF_UNDEAD},
    {"grave ooze", 18, 1, 0, 2, 4, 60, 5, 2, 5, 5, MF_POISON},
    {"ghoul", 20, 4, 2, 3, 7, 100, 9, 3, 7, 8, MF_UNDEAD},
    {"tomb spider", 16, 5, 3, 3, 6, 130, 10, 4, 8, 7, MF_POISON},
    {"cultist", 16, 5, 1, 3, 7, 100, 12, 4, 9, 6, MF_RANGED},
    {"wraith", 26, 7, 4, 4, 9, 110, 16, 5, 10, 5, MF_UNDEAD | MF_DRAIN_LIGHT},
    {"hollow knight", 42, 8, 8, 6, 12, 80, 24, 6, 10, 5, MF_UNDEAD},
    {"barrow ogre", 55, 7, 3, 8, 16, 90, 30, 7, 10, 4, MF_REGEN},
    {"fire imp", 12, 8, 4, 4, 7, 140, 14, 7, 10, 5, MF_RANGED | MF_COWARD},
    {"gargoyle", 34, 8, 9, 5, 10, 100, 20, 5, 10, 3, MF_DORMANT},
    {"mimic", 24, 6, 4, 4, 9, 100, 18, 3, 10, 2, MF_DORMANT},
    {"the Lich", 160, 12, 7, 10, 18, 100, 0, 10, 10, 0, MF_BOSS | MF_RANGED | MF_UNDEAD | MF_SUMMONER},
};

// name, class, dmg, acc, def, minDepth, frequency, blurb
const ItemDef ITEMS[] = {
    {"dagger", ItemClass::Weapon, 2, 4, 2, 0, 1, 4, "A short blade. Quick to find a gap in armour."},
    {"short sword", ItemClass::Weapon, 3, 6, 1, 0, 1, 6, "Plain, reliable steel."},
    {"mace", ItemClass::Weapon, 4, 7, 0, 0, 2, 5, "A flanged iron head on an oak haft."},
    {"war axe", ItemClass::Weapon, 4, 9, -1, 0, 3, 4, "Heavy and hungry."},
    {"spear", ItemClass::Weapon, 3, 8, 1, 0, 3, 4, "Long ash shaft, leaf-shaped head."},
    {"greatsword", ItemClass::Weapon, 6, 12, -2, 0, 5, 3, "Two hands of steel. Slow to recover."},
    {"leather armour", ItemClass::Armor, 0, 0, 0, 2, 1, 5, "Boiled hide, stiff and cracked."},
    {"chain mail", ItemClass::Armor, 0, 0, 0, 4, 3, 4, "Riveted rings over a padded coat."},
    {"plate armour", ItemClass::Armor, 0, 0, 0, 6, 6, 2, "Dented steel plates from a fallen knight."},
    {"potion of healing", ItemClass::Potion, 0, 0, 0, 0, 1, 6, "Closes wounds and purges poison."},
    {"potion of strength", ItemClass::Potion, 0, 0, 0, 0, 2, 4, "Permanently hardens your blows."},
    {"potion of sight", ItemClass::Potion, 0, 0, 0, 0, 1, 5, "Shows you the shape of this floor and its traps."},
    {"potion of poison", ItemClass::Potion, 0, 0, 0, 0, 1, 5, "Bitter. Burns on the way down."},
    {"potion of haste", ItemClass::Potion, 0, 0, 0, 0, 2, 4, "For a while, the world moves at half speed."},
    {"potion of darkness", ItemClass::Potion, 0, 0, 0, 0, 2, 4, "Smothers light. Your torch shrinks."},
    {"scroll of teleportation", ItemClass::Scroll, 0, 0, 0, 0, 1, 6, "Folds space and drops you elsewhere on this floor."},
    {"scroll of mapping", ItemClass::Scroll, 0, 0, 0, 0, 1, 5, "Inks the floor's plan into your memory."},
    {"scroll of enchant weapon", ItemClass::Scroll, 0, 0, 0, 0, 1, 5, "Your weapon strikes truer and harder."},
    {"scroll of enchant armour", ItemClass::Scroll, 0, 0, 0, 0, 1, 5, "Your armour turns more blows."},
    {"scroll of identify", ItemClass::Scroll, 0, 0, 0, 0, 1, 7, "Reveals the nature of one unknown thing you carry."},
    {"scroll of fire", ItemClass::Scroll, 0, 0, 0, 0, 3, 4, "Flame bursts out around you, sparing only the reader."},
    {"scroll of fear", ItemClass::Scroll, 0, 0, 0, 0, 2, 4, "Every creature that sees you turns and runs."},
    {"scroll of summoning", ItemClass::Scroll, 0, 0, 0, 0, 2, 3, "Calls things from the dark. They are not friendly."},
    {"wand of lightning", ItemClass::Wand, 0, 0, 0, 0, 2, 3, "A bolt that strikes everything in a line ahead."},
    {"wand of slowness", ItemClass::Wand, 0, 0, 0, 0, 2, 3, "The first creature ahead moves at half speed."},
    {"wand of blinking", ItemClass::Wand, 0, 0, 0, 0, 3, 2, "Carries you several paces forward in an eyeblink."},
    {"ring of regeneration", ItemClass::Ring, 0, 0, 0, 0, 3, 2, "Wounds close as you walk."},
    {"ring of protection", ItemClass::Ring, 0, 0, 0, 0, 2, 2, "An unseen shell turns aside blows."},
    {"ring of light", ItemClass::Ring, 0, 0, 0, 0, 1, 2, "Your torch burns brighter and slower."},
    {"flask of oil", ItemClass::Oil, 0, 0, 0, 0, 1, 9, "Lamp oil. Keeps your torch burning."},
    {"gold", ItemClass::Gold, 0, 0, 0, 0, 1, 0, "Old coins, green with age."},
};

static_assert(sizeof MONSTERS / sizeof *MONSTERS == size_t(MonsterKind::Count));
static_assert(sizeof ITEMS / sizeof *ITEMS == size_t(ItemKind::Count));

}  // namespace

const MonsterDef& monsterDef(MonsterKind k) { return MONSTERS[int(k)]; }
const ItemDef& itemDef(ItemKind k) { return ITEMS[int(k)]; }
