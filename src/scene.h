#pragma once
// World-space layout of a level, and its static mesh.
#include "render.h"
#include "textures.h"
#include "world.h"

constexpr float CELL = 2.0f, WALL_H = 2.6f, EYE_H = 1.3f;

inline Vec3 cellCenter(Pos p) { return {p.x * CELL + CELL / 2, 0, p.y * CELL + CELL / 2}; }
inline Vec3 dirVector(int facing) { return {float(DIRS[facing].x), 0, float(DIRS[facing].y)}; }
inline float yawOf(int facing) { return std::atan2(float(DIRS[facing].x), float(DIRS[facing].y)); }  // model +Z onto facing
inline Vec3 yawVector(float yaw) { return {std::sin(yaw), 0, std::cos(yaw)}; }

struct ThemeLook {
    Tex wall, floor, ceiling;
    Vec3 fog, ambient, torch;
};
const ThemeLook& themeLook(Theme t);

std::vector<Vertex> buildLevelMesh(const Level& level);
std::vector<PointLight> levelLights(const Level& level, float time);
