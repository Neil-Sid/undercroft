#pragma once
// Every texture is painted procedurally at startup into one 1024x1024 atlas of
// 64x64 cells, each reduced to a 16-colour palette like a 4-bit PS1 CLUT.
#include <cstdint>
#include <vector>

enum class Tex : uint8_t {
    White,  // must stay cell 0: the UI draws solid rectangles with it
    CryptWall, CryptFloor, CryptCeiling,
    CatacombWall, CatacombFloor, CatacombCeiling,
    EmberWall, EmberFloor, EmberCeiling,
    Door, Wood, Iron, Bone, Fur, Flesh, Cloth, Robe, Slime, Chitin, Rust,
    Flame, Glow, Gold, Glass, Parchment, Stone, Eye,
    // Model materials.
    Steel, Leather, Chain, Membrane, Hide, Ghoul, ImpSkin, Maw, Orb, Water, Clay, Coals, Rib, Beam,
    // Per-theme surface variants, picked per cell by the level mesh.
    CryptWallCracked, CryptWallMoss, CryptWallNiche, CryptWallBanner, CryptWallSconce,
    CryptFloorCracked, CryptFloorGrate, CryptFloorStain,
    CatacombWallNiche, CatacombWallBones, CatacombWallRough,
    CatacombFloorBones, CatacombFloorPuddle, CatacombFloorSlab,
    EmberWallVein, EmberWallGrate, EmberWallChains,
    EmberFloorCracked, EmberFloorGrate, EmberFloorAsh,
    Count
};

std::vector<uint32_t> buildAtlas();  // ATLAS_PX x ATLAS_PX, top row first, bytes R,G,B,A
