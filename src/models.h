#pragma once
// Low-poly models built from code, rigged like PS1 characters: rigid segments on a
// joint hierarchy, animated procedurally (no skinning).
#include "content.h"
#include "render.h"
#include "textures.h"
#include <vector>

// Monsters come first in MonsterKind order, then weapons and armour in ItemKind order.
// Chest: part 0 is the box, part 1 the lid hinged along its back edge (opens with a
// negative X rotation). Door: part 0 the frame, part 1 the leaf hinged on its left edge.
// Hand: a gloved right fist closed around a grip that runs along its Y axis through the
// origin; the forearm leaves towards +Z (and a little -Y). Mirror X for a left hand.
// FountainDry is the fountain once drunk from.
enum class ModelId : uint8_t {
    Rat, Bat, Skeleton, Slime, Ghoul, Spider, Cultist, Wraith, Knight, Ogre, Imp, Gargoyle, Mimic, Lich,
    Dagger, Sword, Mace, Axe, Spear, Greatsword, LeatherArmor, ChainMail, PlateArmor,
    Potion, Scroll, Wand, Ring, Oil, Gold,
    Brazier, Pillar, Bones, Chest, Fountain, Door, Torch, Hand, Trap,
    FountainDry,
    Count
};

enum class Anim : uint8_t { Idle, Walk, Attack, Hurt, Die };
constexpr float ATTACK_TIME = 0.45f, HURT_TIME = 0.3f, DIE_TIME = 0.9f, WALK_TIME = 0.22f;

enum class Rig : uint8_t { Static, Humanoid, Rat, Bat, Slime, Spider, Wraith, Mimic };

enum class Role : uint8_t {
    Root, Torso, Head, ArmL, ForeL, ArmR, ForeR, LegL, ShinL, LegR, ShinR, WingL, WingR, Tail, Leg, Fixed,
    Jaw, WingTipL, WingTipR, LegTip, Cloth, Tongue
};

// How a humanoid attacks.
enum class Strike : uint8_t { Slash, Claw, Cast, Smash };

struct Part {
    Mesh mesh;
    int parent = -1;
    Vec3 pivot;  // joint position in the parent's space (model space for the root)
    Role role = Role::Fixed;
    float phase = 0;           // gait or sway offset
    float yaw = 0;             // rest heading of splayed limbs (spider legs, tatters)
    bool opaque = false;       // stays solid inside a translucent model (the ooze's skull)
    bool fixedColour = false;  // ignores the tint's colour (a potion's cork)
};

struct Model {
    Rig rig = Rig::Static;
    std::vector<Part> parts;
    float height = 1;      // top of the model as drawn (size included), for effects
    Blend blend = Blend::Opaque;
    float alpha = 1;
    Strike strike = Strike::Slash;
    float hunch = 0;       // humanoid forward lean at rest, radians
    float crouch = 0;      // humanoid knee bend at rest, 0..1
    float legs = 0.9f;     // humanoid thigh plus shin length, to keep crouched feet on the floor
    float hover = 0;       // floats this far above its modelled height, bobbing
    float lively = 1;      // idle motion; 0 for statues and lurking mimics
    bool crumble = false;  // dies by falling apart into its pieces
    float size = 1;        // uniform scale applied when drawn
};

void buildModels(Renderer& r);
const Model& model(ModelId id);
ModelId monsterModel(MonsterKind k);
ModelId itemModel(ItemKind k);

// Draws `id` at `world` playing `anim`, `t` seconds in. Idle and Walk loop.
void drawModel(Renderer& r, ModelId id, const Mat4& world, Anim anim, float t, Vec4 tint = {1, 1, 1, 1});
