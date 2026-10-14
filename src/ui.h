#pragma once
// Everything drawn in the 640x480 overlay: HUD, pack, automap and the full-screen cards.
#include "game.h"
#include "render.h"
#include <string>
#include <vector>

// Text that floats up from a point in the world (damage numbers); the app projects it to UI pixels.
struct FloatText {
    float x = 0, y = 0;
    std::string text;
    uint32_t color = 0xFFFFFFFF;  // packed R,G,B,A as from hex()
    float age = 0;                // seconds since it appeared
};

constexpr float FLOAT_TEXT_LIFE = 1.1f;  // the HUD stops drawing a FloatText after this many seconds

struct HudState {
    float time = 0;
    float hurt = 0;        // 1 right after taking damage, fading to 0
    int hurtFrom = -1;     // 0 ahead, 1 right, 2 behind, 3 left; -1 none
    float levelUp = 0;     // seconds left on the level-up banner
    bool resting = false;
    std::vector<FloatText> floats;
};

namespace ui {
void hud(Renderer& r, const Game& g, const HudState& h);
void inventory(Renderer& r, const Game& g, int selected);
void automap(Renderer& r, const Game& g, float time);
void title(Renderer& r, float time, int menu = -1);  // menu >= 0 draws the menu with that row selected
void death(Renderer& r, const Game& g, float t);
void victory(Renderer& r, const Game& g, float t);
void help(Renderer& r, bool inRun = true);  // inRun lists Q to abandon the run
void credits(Renderer& r, float time);

// Pack slots in the order the inventory lists them (grouped by class). Row i shows PACK_LETTERS[i],
// which skips the keys the pack screen already uses (E use, I close, S/W move, X drop).
std::vector<int> packOrder(const Game& g);
constexpr char PACK_LETTERS[] = "abcdfghjklmnopqrtuv";
static_assert(sizeof PACK_LETTERS - 1 >= PACK_SIZE);
constexpr int TITLE_MENU_ITEMS = 4;  // Descend, How to play, Credits, Leave
}
