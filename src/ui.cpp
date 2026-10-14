#include "ui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <numeric>
#include <string>

namespace {

constexpr uint32_t INK = hex(0xD8CCB0), DIM = hex(0x8A8070), FAINT = hex(0x4A4236), LABEL = hex(0xA08850), GOLDEN = hex(0xE0B050),
                   BLOOD = hex(0xC02828), AMBER = hex(0xE09030), VENOM = hex(0x86C04A), QUICK = hex(0xE8D878), MYSTERY = hex(0xB49CD0),
                   STEEL = hex(0xB8C0C8), LEATHER = hex(0x8A5A30), BETTER = hex(0x9FD08A), WORSE = hex(0xE07050);

const uint32_t POTION_RGB[8] = {0x8A8A4A, 0xD02838, 0xF0B030, 0x9050D0, 0xE8E8DA, 0xC88838, 0x38B0B0, 0x404050};
const uint32_t WAND_RGB[6] = {0x8A6A40, 0xD8D0B8, 0x8890A0, 0xA0D8E0, 0x3A3030, 0xA06A3A};
const uint32_t STONE_RGB[6] = {0x50B070, 0xD8E0F0, 0xB02838, 0x404048, 0xE09030, 0xA8B8E0};
const char* GROUPS[] = {"WEAPONS", "ARMOUR", "POTIONS", "SCROLLS", "WANDS", "RINGS", "OIL", "GOLD"};  // ItemClass order

uint32_t fade(uint32_t c, float a) { return (c & 0x00FFFFFF) | uint32_t(std::clamp(a, 0.0f, 1.0f) * float(c >> 24)) << 24; }

uint32_t mix(uint32_t a, uint32_t b, float t) {
    uint32_t out = a & 0xFF000000;
    for (int s = 0; s < 24; s += 8) {
        float x = float(a >> s & 255), y = float(b >> s & 255);
        out |= uint32_t(std::clamp(x + (y - x) * t, 0.0f, 255.0f)) << s;
    }
    return out;
}

float hash(int i, int k) {
    float v = std::sin(float(i) * 12.9898f + float(k) * 78.233f) * 43758.5453f;
    return v - std::floor(v);
}

std::string signedNum(int v) { return (v >= 0 ? "+" : "") + std::to_string(v); }

std::string grouped(int v) {
    std::string s = std::to_string(v);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return s;
}

std::string capitalised(std::string s) {
    if (!s.empty()) s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

// ---- Drawing primitives ----

// 8x8 pixel icons. m main, l light, d dark, a accent, w highlight.
enum class Icon : uint8_t { Sword, Armor, Potion, Scroll, Wand, Ring, Oil, Coin, Heart, Flame, Drop, Bolt, Stairs, Chest, Fountain, Arrow, Foe, Fist };

const char* const ICONS[][8] = {
    {"......lw", ".....lmd", "....lmd.", "...lmd..", ".a.lmd..", "..aad...", ".da.a...", "dd......"},
    {".mm..mm.", "mlmmmmdm", "mlmmmmdm", ".lmmmmd.", ".lmaamd.", ".lmmmmd.", ".lmmmmd.", "..dddd.."},
    {"...aa...", "...ll...", "...ll...", "..dmmd..", ".dwmmmd.", ".dlmmmd.", ".dmmmmd.", "..dddd.."},
    {"........", "dlllllld", ".mmmmmm.", ".maaaam.", ".mmmmmm.", ".maaamm.", "dlllllld", "........"},
    {"......w.", ".....waw", "......w.", "....l...", "...lm...", "..lm....", ".lm.....", "dm......"},
    {"...aa...", "..awaa..", "..dmmd..", ".m....m.", "l......d", "l......d", ".m....m.", "..mmmm.."},
    {"..dd....", "..mm.dd.", ".lmmmd.d", "lmmmmmdd", "lmaaamd.", "lmmmmmd.", ".lmmmd..", "..ddd..."},
    {"..lmmd..", ".lwmmmd.", ".lmmmmd.", "..dddd..", ".lmmmmd.", "..dddd..", ".lmmmmd.", "..dddd.."},
    {".mm.mm..", "mwlmmmd.", "mlmmmmd.", "mmmmmmd.", ".mmmmd..", "..mmd...", "...d....", "........"},
    {"...m....", "...mm...", "..mmm.m.", "..mlmmm.", ".mmllmm.", ".mlwwlm.", ".mlwwlm.", "..mmmm.."},
    {"...m....", "...m....", "..mmm...", "..lmm...", ".lmmmd..", ".wmmmd..", ".mmmmd..", "..ddd..."},
    {"....ll..", "...lm...", "..lm....", ".lmmmm..", "...mm...", "..mm....", ".mm.....", ".m......"},
    {"......mm", "......dd", "....mmmm", "....dddd", "..mmmmmm", "..dddddd", "mmmmmmmm", "dddddddd"},
    {"........", ".dmmmmd.", "dmllllmd", "dddaaddd", "dmmawmmd", "dmmmmmmd", "dmmmmmmd", "dddddddd"},
    {"...w....", "..lml...", "...m....", ".ddmdd..", "dlllllld", ".dmmmmd.", "..dmmd..", ".dddddd."},
    {"...mm...", "...mm...", "..mmmm..", "..mmmm..", ".mmmmmm.", ".mmmmmm.", "mmmmmmmm", "mm....mm"},
    {"........", "...dd...", "..dmmd..", ".dmlmmd.", ".dmmmmd.", "..dmmd..", "...dd...", "........"},
    {".lml....", ".lmlmlm.", ".lmmmmmd", "lmmmmmmd", "lmmmmmmd", ".mmmmmd.", "..mmmd..", "..dddd.."},
};

// rot turns the icon clockwise in quarter turns (the map arrow).
void icon(Renderer& r, Icon ic, float x, float y, int scale, uint32_t main, uint32_t accent = GOLDEN, float alpha = 1, int rot = 0) {
    const char* const* rows = ICONS[int(ic)];
    float s = float(scale);
    for (int pass = 0; pass < 2; pass++)
        for (int py = 0; py < 8; py++)
            for (int px = 0; px < 8; px++) {
                char ch = rows[py][px];
                if (ch == '.') continue;
                int qx = px, qy = py;
                for (int i = 0; i < rot; i++) std::swap(qx, qy), qx = 7 - qx;
                uint32_t c = ch == 'l' ? mix(main, 0xFFFFFFFF, 0.35f) : ch == 'd' ? mix(main, 0xFF000000, 0.45f) : ch == 'a' ? accent : ch == 'w' ? hex(0xFFF8E8) : main;
                if (pass == 0) c = rgba(0, 0, 0, 170);
                float off = pass == 0 ? std::max(1.0f, s / 2) : 0;
                r.rect(x + float(qx) * s + off, y + float(qy) * s + off, s, s, fade(c, alpha));
            }
}

// A dark window with clipped corners, like a PS1 menu.
void panel(Renderer& r, float x, float y, float w, float h, float alpha = 0.9f) {
    uint32_t bg = rgba(10, 8, 11, int(alpha * 255)), edge = hex(0x5A5040, 230), low = hex(0x2A241C, 230);
    r.rect(x + 2, y + 1, w - 4, h - 2, bg);
    r.rect(x + 1, y + 2, 1, h - 4, bg);
    r.rect(x + w - 2, y + 2, 1, h - 4, bg);
    r.rect(x + 2, y, w - 4, 1, edge);
    r.rect(x, y + 2, 1, h - 4, edge);
    r.rect(x + 2, y + h - 1, w - 4, 1, low);
    r.rect(x + w - 1, y + 2, 1, h - 4, low);
    r.rect(x + 1, y + 1, 1, 1, edge), r.rect(x + w - 2, y + 1, 1, 1, edge);
    r.rect(x + 1, y + h - 2, 1, 1, low), r.rect(x + w - 2, y + h - 2, 1, 1, low);
}

void bar(Renderer& r, float x, float y, float w, float h, float frac, uint32_t fill, uint32_t back) {
    float f = std::floor(w * std::clamp(frac, 0.0f, 1.0f));
    r.rect(x - 1, y - 1, w + 2, h + 2, hex(0x000000, 190));
    r.rect(x, y, w, h, back);
    r.rect(x, y, f, h, fill);
    if (h > 3) r.rect(x, y, f, 1, mix(fill, 0xFFFFFFFF, 0.3f));
    if (f > 0 && f < w) r.rect(x + f, y, 1, h, mix(fill, 0xFFFFFFFF, 0.5f));
}

void diamond(Renderer& r, float cx, float cy, int radius, uint32_t c) {
    for (int i = -radius; i <= radius; i++) {
        float half = float(radius - std::abs(i));
        r.rect(cx - half, cy + float(i), half * 2 + 1, 1, c);
    }
}

void ornament(Renderer& r, float cx, float y, float half, uint32_t c) {
    r.rect(cx - half, y, half - 9, 1, c);
    r.rect(cx + 10, y, half - 9, 1, c);
    diamond(r, cx, y, 3, c);
}

void trapMark(Renderer& r, float x, float y) {
    for (int i = 0; i < 6; i++) r.rect(x + float(i), y + float(i), 1, 1, BLOOD), r.rect(x + 5 - float(i), y + float(i), 1, 1, BLOOD);
}

// Darkened screen edges, in soft steps.
void vignette(Renderer& r, uint32_t c, float alpha) {
    const int steps = 8;
    for (int i = 0; i < steps; i++) {
        float t = float(i) * 5, a = alpha * (1 - float(i) / steps);
        uint32_t k = fade(c, a);
        r.rect(t, t, UI_W - 2 * t, 5, k);
        r.rect(t, UI_H - t - 5, UI_W - 2 * t, 5, k);
        r.rect(t, t + 5, 5, UI_H - 2 * t - 10, k);
        r.rect(UI_W - t - 5, t + 5, 5, UI_H - 2 * t - 10, k);
    }
}

// Embers drifting up (dir -1) or ash sinking (dir 1) across the whole layer.
void motes(Renderer& r, float time, int count, uint32_t hot, uint32_t cool, float alpha, float dir = -1) {
    for (int i = 0; i < count; i++) {
        float phase = time * (0.04f + 0.07f * hash(i, 1)) + hash(i, 2);
        phase -= std::floor(phase);
        float travel = phase * (UI_H * (0.5f + 0.5f * hash(i, 3)));
        float y = dir < 0 ? UI_H + 6 - travel : -6 + travel;
        float x = hash(i, 4) * UI_W + std::sin(time * (0.7f + hash(i, 5)) + float(i)) * 18 * phase;
        float flicker = 0.55f + 0.45f * std::sin(time * (6 + hash(i, 6) * 9) + float(i) * 3);
        float s = hash(i, 7) > 0.7f ? 3.0f : 2.0f;
        r.rect(std::floor(x), std::floor(y), s, s, fade(mix(hot, cool, phase), (1 - phase) * flicker * alpha));
    }
}

// ---- Text ----

// Letter-spaced text for headings and labels.
float tracked(Renderer& r, Font f, float x, float y, const std::string& s, uint32_t c, float gap, Align a = Align::Left) {
    float w = r.textWidth(f, s) + gap * float(std::max<size_t>(1, s.size()) - 1);
    if (a == Align::Center) x -= std::floor(w / 2);
    if (a == Align::Right) x -= w;
    for (char ch : s) x += r.text(f, x, y, std::string_view(&ch, 1), c) + gap;
    return w;
}

// A small-caps label with a hairline running on to `right`.
void sectionLabel(Renderer& r, float x, float y, float right, const std::string& s, uint32_t c = LABEL) {
    float w = tracked(r, Font::Small, x, y, s, c, 2);
    r.rect(x + w + 8, y + 8, right - x - w - 8, 1, FAINT);
}

std::vector<std::string> wrap(const Renderer& r, Font f, const std::string& text, float width) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush = [&] {
        if (line.empty()) line = word;
        else if (r.textWidth(f, line + " " + word) <= width) line += " " + word;
        else lines.push_back(line), line = word;
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flush();
        else word += c;
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// ---- Items ----

Icon iconFor(ItemClass c) {
    const Icon icons[] = {Icon::Sword, Icon::Armor, Icon::Potion, Icon::Scroll, Icon::Wand, Icon::Ring, Icon::Oil, Icon::Coin};
    return icons[int(c)];
}

// Main and accent colour of an item's icon. Unknown items keep their disguise: potion colour, wand wood, ring stone.
std::pair<uint32_t, uint32_t> itemColours(const Game& g, ItemKind k) {
    switch (itemClass(k)) {
    case ItemClass::Weapon: return {STEEL, LEATHER};
    case ItemClass::Armor: return {k == ItemKind::LeatherArmor ? hex(0x9A6A3A) : k == ItemKind::ChainMail ? hex(0x9098A0) : hex(0xC8D0D8), GOLDEN};
    case ItemClass::Potion: return {hex(POTION_RGB[g.potionColour(k) % 8]), LEATHER};
    case ItemClass::Scroll: return {hex(0xD8C8A0), hex(0x7A5A3A)};
    case ItemClass::Wand: return {hex(WAND_RGB[g.potionColour(k) % 6]), hex(0xC8B8FF)};
    case ItemClass::Ring: return {GOLDEN, hex(STONE_RGB[g.potionColour(k) % 6])};
    case ItemClass::Oil: return {hex(0x9A6A3A), AMBER};
    case ItemClass::Gold: break;
    }
    return {GOLDEN, GOLDEN};
}

void itemIcon(Renderer& r, const Game& g, ItemKind k, float x, float y, int scale, float alpha = 1) {
    auto [main, accent] = itemColours(g, k);
    icon(r, iconFor(itemClass(k)), x, y, scale, main, accent, alpha);
}

std::string damageRange(const Item& it) {
    const ItemDef& d = itemDef(it.kind);
    return std::to_string(d.damageMin + it.enchant) + "-" + std::to_string(d.damageMax + it.enchant);
}

int armourDefense(const Item& it) { return itemDef(it.kind).defense + it.enchant; }

// "ring of regeneration" -> "Regeneration"; an unknown ring keeps its stone name.
std::string shortRing(const Game& g) {
    std::string n = g.itemName(g.player.ring);
    size_t of = n.find(" of ");
    return capitalised(of == std::string::npos ? n : n.substr(of + 4));
}

const char* useVerb(ItemClass c) {
    const char* verbs[] = {"wield", "wear", "drink", "read", "zap", "put on", "refuel torch", "use"};
    return verbs[int(c)];
}

uint32_t diffColour(int d) { return d > 0 ? BETTER : d < 0 ? WORSE : DIM; }

void chargePips(Renderer& r, float x, float y, int charges, bool alignRight) {
    int n = std::max(charges, 1);
    if (alignRight) x -= float(n) * 5;
    for (int i = 0; i < n; i++) r.rect(x + float(i) * 5, y, 3, 8, i < charges ? hex(0xC8B8FF) : FAINT);
}

// ---- HUD ----

void messageLog(Renderer& r, const Game& g) {
    struct Line {
        const Message* m;
        int count;
    };
    std::vector<Line> lines;
    for (int i = int(g.log.size()) - 1; i >= 0 && lines.size() < 5; i--) {
        const Message& m = g.log[size_t(i)];
        if (g.turn - m.turn > 28) break;
        if (!lines.empty() && lines.back().m->text == m.text) lines.back().count++;
        else lines.push_back({&m, 1});
    }
    for (size_t i = 0; i < lines.size(); i++) {
        const Message& m = *lines[i].m;
        float age = float(g.turn - m.turn);
        float a = std::clamp(1.2f - age / 24.0f, 0.0f, 1.0f) * (1.0f - float(i) * 0.14f);
        float y = 12 + float(lines.size() - 1 - i) * 16;
        uint32_t c = fade(hex(m.color), a);
        float w = r.text(Font::Small, 14, y, m.text, c);
        if (lines[i].count > 1) r.text(Font::Small, 20 + w, y, "x" + std::to_string(lines[i].count), fade(DIM, a));
        if (age == 0) r.rect(6, y + 6, 3, 3, c);
    }
}

void compass(Renderer& r, const Game& g) {
    const char* names[4] = {"N", "E", "S", "W"};
    int f = g.player.facing;
    float cx = UI_W - 70, y = 12;
    r.rect(cx - 54, y + 22, 108, 1, fade(DIM, 0.55f));
    for (int i = -4; i <= 4; i++) {
        bool cardinal = i % 4 == 0;
        r.rect(cx + float(i) * 13, y + (cardinal ? 18 : 20), 1, cardinal ? 4 : 2, fade(DIM, cardinal ? 0.8f : 0.5f));
    }
    r.text(Font::Small, cx - 52, y + 3, names[(f + 3) % 4], fade(DIM, 0.9f), Align::Center);
    r.text(Font::Small, cx + 52, y + 3, names[(f + 1) % 4], fade(DIM, 0.9f), Align::Center);
    r.text(Font::Body, cx, y - 1, names[f], GOLDEN, Align::Center);
    diamond(r, cx, y + 27, 2, GOLDEN);
    tracked(r, Font::Small, UI_W - 16, y + 36, "DEPTH " + std::to_string(g.level.depth), LABEL, 2, Align::Right);
}

void statusChip(Renderer& r, float& x, float y, Icon ic, std::pair<uint32_t, uint32_t> colours, const std::string& label, uint32_t c) {
    icon(r, ic, x, y - 1, 2, colours.first, colours.second);
    x += 20;
    x += r.text(Font::Small, x, y, label, c) + 12;
}

void vitals(Renderer& r, const Game& g, const HudState& h) {
    const Player& p = g.player;
    float x = 16, y = UI_H - 66;

    bool flash = h.hurt > 0.5f && std::fmod(h.time * 14, 1.0f) < 0.5f;
    uint32_t hpCol = flash ? hex(0xFF8070) : p.poison > 0 ? hex(0x6E9A3A) : BLOOD;
    icon(r, Icon::Heart, x, y - 3, 2, p.poison > 0 ? VENOM : BLOOD);
    bar(r, x + 22, y + 1, 150, 8, float(p.hp) / float(p.maxHp), hpCol, hex(0x2A0E0E, 220));
    float w = r.text(Font::Small, x + 180, y - 2, std::to_string(p.hp), INK);
    r.text(Font::Small, x + 180 + w, y - 2, " / " + std::to_string(p.maxHp), DIM);

    bool low = p.torch < 150;
    float gutter = low && p.torch > 0 ? 0.6f + 0.4f * std::sin(h.time * 19) * std::sin(h.time * 7) : 1;
    icon(r, Icon::Flame, x, y + 17, 2, p.torch > 0 ? AMBER : FAINT, GOLDEN, gutter);
    bar(r, x + 22, y + 22, 150, 5, float(p.torch) / float(p.torchMax), fade(low ? hex(0xB04020) : AMBER, gutter), hex(0x24160A, 220));
    r.text(Font::Small, x + 180, y + 17, p.torch > 0 ? std::to_string(p.torch) : "out", p.torch <= 0 ? BLOOD : low ? WORSE : INK);

    float cx = x, cy = y + 40;
    if (p.poison > 0) statusChip(r, cx, cy, Icon::Drop, {VENOM, VENOM}, "Poison " + std::to_string(p.poison), VENOM);
    if (p.haste > 0) statusChip(r, cx, cy, Icon::Bolt, {QUICK, QUICK}, "Haste " + std::to_string(p.haste), QUICK);
    if (p.strength > 0) statusChip(r, cx, cy, Icon::Fist, {hex(0xC89070), GOLDEN}, "Str " + signedNum(p.strength), INK);
    if (p.hasArmor) statusChip(r, cx, cy, Icon::Armor, itemColours(g, p.armor.kind), "Def " + std::to_string(g.defense()), INK);
    if (p.hasRing) statusChip(r, cx, cy, Icon::Ring, itemColours(g, p.ring.kind), shortRing(g), INK);
}

void progress(Renderer& r, const Game& g) {
    const Player& p = g.player;
    float right = UI_W - 16, y = UI_H - 66;
    float w = r.text(Font::Body, right, y - 6, std::to_string(p.level), INK, Align::Right);
    tracked(r, Font::Small, right - w - 6, y - 2, "LEVEL", DIM, 2, Align::Right);
    float need = float(g.xpForNextLevel());
    bar(r, right - 110, y + 19, 110, 3, need > 0 ? float(p.xp) / need : 0, hex(0xC8A050), hex(0x3A3224, 230));
    float gw = r.text(Font::Small, right, y + 34, grouped(p.gold), GOLDEN, Align::Right);
    icon(r, Icon::Coin, right - gw - 20, y + 32, 2, GOLDEN);
}

// A chevron pointing at whoever struck last, plus a strip along that edge.
void hurtIndicator(Renderer& r, const HudState& h) {
    if (h.hurt <= 0) return;
    if (h.hurtFrom < 0) {
        vignette(r, hex(0xA01818), h.hurt * 0.35f);
        return;
    }
    uint32_t c = fade(hex(0xD02020), h.hurt), strip = fade(hex(0xB01818), h.hurt * 0.5f);
    auto chevron = [&](float cx, float cy, int dir) {
        for (int i = 0; i < 7; i++) {
            float a = float(i) * 2, b = float(i) * 3;
            if (dir == 0) r.rect(cx - b - 4, cy + a, 4, 2, c), r.rect(cx + b, cy + a, 4, 2, c);
            if (dir == 2) r.rect(cx - b - 4, cy - a, 4, 2, c), r.rect(cx + b, cy - a, 4, 2, c);
            if (dir == 3) r.rect(cx + a, cy - b - 4, 2, 4, c), r.rect(cx + a, cy + b, 2, 4, c);
            if (dir == 1) r.rect(cx - a, cy - b - 4, 2, 4, c), r.rect(cx - a, cy + b, 2, 4, c);
        }
    };
    switch (h.hurtFrom) {
    case 0: r.rect(UI_W / 2 - 110, 0, 220, 4, strip), chevron(UI_W / 2, 64, 0); break;
    case 1: r.rect(UI_W - 4, UI_H / 2 - 90, 4, 180, strip), chevron(UI_W - 20, UI_H / 2, 1); break;
    case 2: r.rect(UI_W / 2 - 110, UI_H - 4, 220, 4, strip), chevron(UI_W / 2, UI_H - 96, 2); break;
    case 3: r.rect(0, UI_H / 2 - 90, 4, 180, strip), chevron(20, UI_H / 2, 3); break;
    }
}

void floatingText(Renderer& r, const HudState& h) {
    for (const FloatText& f : h.floats) {
        if (f.age >= FLOAT_TEXT_LIFE) continue;
        float t = f.age / FLOAT_TEXT_LIFE;
        float rise = 30 * (1 - (1 - t) * (1 - t));
        float a = t < 0.55f ? 1 : 1 - (t - 0.55f) / 0.45f;
        uint32_t c = f.age < 0.07f ? mix(f.color, 0xFFFFFFFF, 0.7f) : f.color;
        bool number = !f.text.empty() && (std::isdigit(static_cast<unsigned char>(f.text[0])) || f.text[0] == '-' || f.text[0] == '+');
        Font font = number ? Font::Body : Font::Small;
        float y = f.y - rise;
        for (Pos d : DIRS) r.text(font, f.x + float(d.x), y + float(d.y), f.text, rgba(0, 0, 0, int(200 * a)), Align::Center, false);
        r.text(font, f.x, y, f.text, fade(c, a), Align::Center);
    }
}

void levelBanner(Renderer& r, const Game& g, const HudState& h) {
    if (h.levelUp <= 0) return;
    float in = std::clamp((2.5f - h.levelUp) / 0.3f, 0.0f, 1.0f), out = std::clamp(h.levelUp / 0.8f, 0.0f, 1.0f);
    float a = std::min(in, out), y = 104 + (1 - in) * 10;
    tracked(r, Font::Small, UI_W / 2, y, "YOU GROW STRONGER", fade(LABEL, a), 3, Align::Center);
    tracked(r, Font::Title, UI_W / 2, y + 14, "LEVEL " + std::to_string(g.player.level), fade(GOLDEN, a), 6, Align::Center);
    ornament(r, UI_W / 2, y + 74, 120, fade(GOLDEN, a * 0.8f));
    std::string sub = "Max HP " + std::to_string(g.player.maxHp) + "   ·   Accuracy " + signedNum(g.attackAccuracy());
    r.text(Font::Small, UI_W / 2, y + 84, sub, fade(INK, a), Align::Center);
}

void prompts(Renderer& r, const Game& g, const HudState& h) {
    if (g.onStairs()) {
        std::string s = "Stairs lead down to depth " + std::to_string(g.level.depth + 1);
        float w = r.textWidth(Font::Body, s) + 22;
        float x = std::floor(UI_W / 2 - w / 2), y = 112;
        icon(r, Icon::Stairs, x, y + 2, 2, GOLDEN);
        r.text(Font::Body, x + 22, y, s, INK);
        r.text(Font::Small, UI_W / 2, y + 24, "[Enter] descend", fade(GOLDEN, 0.65f + 0.35f * std::sin(h.time * 4)), Align::Center);
    }
    if (h.resting) {
        uint32_t c = fade(INK, 0.7f + 0.3f * std::sin(h.time * 3));
        float w = r.textWidth(Font::Small, "RESTING") + 18, x = std::floor(UI_W / 2 - w / 2);
        tracked(r, Font::Small, x, UI_H / 2 + 56, "RESTING", c, 3);
        r.text(Font::Small, x + w + 2, UI_H / 2 + 56, std::string(size_t(int(h.time * 3) % 4), '.'), c);
        r.text(Font::Small, UI_W / 2, UI_H / 2 + 74, "any key to stop", fade(DIM, 0.8f), Align::Center);
    }
}

// ---- Pack ----

std::string itemTag(const Game& g, const Item& it) {
    switch (itemClass(it.kind)) {
    case ItemClass::Weapon: return damageRange(it);
    case ItemClass::Armor: return "def " + std::to_string(armourDefense(it));
    default: return g.identified(it.kind) ? "" : "?";
    }
}

void packList(Renderer& r, const Game& g, int selected, float x, float y, float right, float bottom) {
    const std::vector<Item>& pack = g.player.pack;
    if (pack.empty()) {
        r.text(Font::Small, x, y, "Your pack is empty.", DIM);
        return;
    }
    std::vector<int> order = ui::packOrder(g);
    int groups = 1;
    for (size_t i = 1; i < order.size(); i++) groups += itemClass(pack[size_t(order[i])].kind) != itemClass(pack[size_t(order[i - 1])].kind);
    bool roomy = float(order.size()) * 17 + float(groups) * 16 + float(groups - 1) * 4 <= bottom - y;
    float rowH = roomy ? 17 : 16, headH = roomy ? 16 : 14, gap = roomy ? 4 : 1;

    int lastClass = -1;
    for (size_t row = 0; row < order.size(); row++) {
        const Item& it = pack[size_t(order[row])];
        int cls = int(itemClass(it.kind));
        if (cls != lastClass) {
            if (lastClass >= 0) y += gap;
            sectionLabel(r, x, y, right, GROUPS[cls]);
            y += headH, lastClass = cls;
        }
        bool sel = order[row] == selected, mystery = !g.identified(it.kind);
        if (sel) r.rect(x - 8, y - 1, right - x + 12, rowH, hex(0x5A4630, 150)), r.rect(x - 8, y - 1, 2, rowH, GOLDEN);
        r.text(Font::Small, x, y, std::string(1, ui::PACK_LETTERS[row]), sel ? GOLDEN : DIM);
        itemIcon(r, g, it.kind, x + 14, y, 2, sel ? 1.0f : 0.85f);
        uint32_t name = mystery ? MYSTERY : INK;
        r.text(Font::Small, x + 36, y, g.itemName(it), sel ? mix(name, 0xFFFFFFFF, 0.25f) : mix(name, 0xFF000000, 0.12f));
        if (itemClass(it.kind) == ItemClass::Wand && !mystery) chargePips(r, right, y + 3, it.charges, true);
        else r.text(Font::Small, right, y, itemTag(g, it), mystery ? MYSTERY : DIM, Align::Right);
        y += rowH;
    }
}

void equipment(Renderer& r, const Game& g, float x, float y, float right) {
    const Player& p = g.player;
    sectionLabel(r, x, y, right, "EQUIPPED");
    auto worn = [&](float yy, Icon ic, bool has, const Item& it, const char* none, const std::string& tag) {
        auto [main, accent] = itemColours(g, it.kind);
        icon(r, ic, x, yy - 1, 2, has ? main : FAINT, has ? accent : FAINT, has ? 1.0f : 0.6f);
        r.text(Font::Small, x + 22, yy, has ? g.itemName(it) : none, has ? INK : DIM);
        if (has) r.text(Font::Small, right, yy, tag, DIM, Align::Right);
    };
    worn(y + 18, Icon::Sword, true, p.weapon, "", damageRange(p.weapon));
    worn(y + 36, Icon::Armor, p.hasArmor, p.armor, "no armour", "def " + std::to_string(armourDefense(p.armor)));
    worn(y + 54, Icon::Ring, p.hasRing, p.ring, "no ring", "");

    sectionLabel(r, x, y + 80, right, "STATS");
    float half = (right - x) / 2 + 8;
    auto stat = [&](int col, int row, const char* label, const std::string& value) {
        float sx = x + float(col) * half, sy = y + 98 + float(row) * 18;
        r.text(Font::Small, sx, sy, label, DIM);
        r.text(Font::Small, sx + 112, sy, value, INK, Align::Right);
    };
    stat(0, 0, "Attack", std::to_string(g.damageMin()) + "-" + std::to_string(g.damageMax()));
    stat(1, 0, "Accuracy", signedNum(g.attackAccuracy()));
    stat(0, 1, "Defense", std::to_string(g.defense()));
    stat(1, 1, "Strength", signedNum(p.strength));
    stat(0, 2, "Level", std::to_string(p.level));
    stat(1, 2, "Ring", p.hasRing ? shortRing(g) : "none");
    float need = float(g.xpForNextLevel());
    bar(r, x, y + 154, right - x, 2, need > 0 ? float(p.xp) / need : 0, hex(0xC8A050), hex(0x2A2418));
    r.text(Font::Small, right, y + 158, std::to_string(p.xp) + " / " + std::to_string(g.xpForNextLevel()) + " xp", DIM, Align::Right);
}

// How a weapon or armour in the pack compares with what is equipped.
void comparison(Renderer& r, const Game& g, const Item& it, float x, float y) {
    const Player& p = g.player;
    if (itemClass(it.kind) == ItemClass::Weapon) {
        const ItemDef &now = itemDef(p.weapon.kind), &d = itemDef(it.kind);
        int dmg = ((d.damageMin + d.damageMax) - (now.damageMin + now.damageMax)) / 2 + it.enchant - p.weapon.enchant;
        int acc = (d.accuracy + it.enchant) - (now.accuracy + p.weapon.enchant);
        r.text(Font::Small, x, y, "vs wielded", DIM);
        float cx = x + 76 + r.text(Font::Small, x + 76, y, signedNum(dmg) + " avg dmg", diffColour(dmg)) + 12;
        r.text(Font::Small, cx, y, signedNum(acc) + " acc", diffColour(acc));
    } else if (itemClass(it.kind) == ItemClass::Armor) {
        int diff = armourDefense(it) - (p.hasArmor ? armourDefense(p.armor) : 0);
        r.text(Font::Small, x, y, p.hasArmor ? "vs worn" : "vs nothing", DIM);
        r.text(Font::Small, x + 76, y, signedNum(diff) + " def", diffColour(diff));
    }
}

void itemDetails(Renderer& r, const Game& g, const Item& it, float x, float y, float right, float bottom) {
    ItemClass c = itemClass(it.kind);
    bool mystery = !g.identified(it.kind);
    sectionLabel(r, x, y, right, mystery ? "UNIDENTIFIED" : GROUPS[int(c)], mystery ? MYSTERY : LABEL);
    itemIcon(r, g, it.kind, x, y + 22, 3);
    float ty = y + 22;
    for (const std::string& line : wrap(r, Font::Body, capitalised(g.itemName(it)), right - x - 34))
        r.text(Font::Body, x + 34, ty, line, mystery ? MYSTERY : GOLDEN), ty += 19;
    ty = std::max(ty, y + 50) + 6;
    for (const std::string& line : wrap(r, Font::Small, g.itemInfo(it), right - x)) r.text(Font::Small, x, ty, line, INK), ty += 16;
    ty += 6;
    if (c == ItemClass::Wand && !mystery) {
        r.text(Font::Small, x, ty, "Charges", DIM);
        chargePips(r, x + 76, ty + 3, it.charges, false);
    }
    comparison(r, g, it, x, ty);

    float ax = x + r.text(Font::Small, x, bottom, "[Enter]", GOLDEN) + 6;
    ax += r.text(Font::Small, ax, bottom, useVerb(c), INK) + 18;
    ax += r.text(Font::Small, ax, bottom, "[X]", GOLDEN) + 6;
    r.text(Font::Small, ax, bottom, "drop", INK);
}

// ---- Automap ----

constexpr float MAP_CELL = 10;
constexpr uint32_t MAP_FLOOR = hex(0x1C1915), MAP_WALL = hex(0x4E463A), MAP_DOOR = hex(0xA87040), MAP_ITEM = hex(0x90B8F0), MAP_FOE = hex(0xE03030),
                   MAP_YOU = hex(0xF8E0A0), MAP_WATER = hex(0x5090E0);

void mapCell(Renderer& r, const Level& L, Pos p, float x, float y) {
    const Cell& cell = L.at(p);
    switch (cell.tile) {
    case Tile::Wall:
    case Tile::SecretDoor: r.rect(x, y, MAP_CELL, MAP_CELL, MAP_WALL), r.rect(x, y, MAP_CELL, 1, hex(0x5E5446)); break;
    case Tile::Floor: r.rect(x, y, MAP_CELL, MAP_CELL, cell.visible ? hex(0x302A22) : MAP_FLOOR); break;
    case Tile::Door: {
        r.rect(x, y, MAP_CELL, MAP_CELL, MAP_FLOOR);
        bool across = solid(L.tile(p + Pos{1, 0})) && solid(L.tile(p + Pos{-1, 0}));
        uint32_t c = cell.doorOpen ? hex(0x6A4828) : MAP_DOOR;
        if (across) r.rect(x, y + 3, MAP_CELL, 4, c);
        else r.rect(x + 3, y, 4, MAP_CELL, c);
        break;
    }
    case Tile::StairsDown: r.rect(x, y, MAP_CELL, MAP_CELL, MAP_FLOOR), icon(r, Icon::Stairs, x + 1, y + 1, 1, GOLDEN); break;
    }
    if (cell.trap != TrapKind::None && cell.trapFound) trapMark(r, x + 2, y + 2);
}

void mapDecor(Renderer& r, const Decor& d, float x, float y) {
    switch (d.kind) {
    case DecorKind::Chest: icon(r, Icon::Chest, x + 1, y + 1, 1, d.used ? hex(0x5A4430) : MAP_DOOR, d.used ? FAINT : GOLDEN); break;
    case DecorKind::Fountain: icon(r, Icon::Fountain, x + 1, y + 1, 1, d.used ? hex(0x606060) : MAP_WATER, INK); break;
    case DecorKind::Brazier: r.rect(x + 3, y + 3, 4, 4, AMBER), r.rect(x + 4, y + 2, 2, 1, hex(0xF0D060)); break;
    case DecorKind::Pillar: r.rect(x + 2, y + 2, 6, 6, MAP_WALL); break;
    default: break;
    }
}

void mapLegend(Renderer& r, float x, float y, float right) {
    sectionLabel(r, x, y, right, "LEGEND");
    y += 22;
    auto row = [&](const char* label, auto draw) {
        draw(x, y + 2);
        r.text(Font::Small, x + 18, y, label, INK);
        y += 19;
    };
    row("You", [&](float lx, float ly) { icon(r, Icon::Arrow, lx + 1, ly, 1, MAP_YOU, GOLDEN, 1, 1); });
    row("Stairs down", [&](float lx, float ly) { icon(r, Icon::Stairs, lx + 1, ly, 1, GOLDEN); });
    row("Door", [&](float lx, float ly) { r.rect(lx + 1, ly + 3, 9, 4, MAP_DOOR); });
    row("Item", [&](float lx, float ly) { diamond(r, lx + 5, ly + 4, 2, MAP_ITEM); });
    row("Gold", [&](float lx, float ly) { r.rect(lx + 3, ly + 2, 4, 4, GOLDEN); });
    row("Chest", [&](float lx, float ly) { icon(r, Icon::Chest, lx + 1, ly, 1, MAP_DOOR); });
    row("Fountain", [&](float lx, float ly) { icon(r, Icon::Fountain, lx + 1, ly, 1, MAP_WATER, INK); });
    row("Trap", [&](float lx, float ly) { trapMark(r, lx + 2, ly + 1); });
    row("Foe in sight", [&](float lx, float ly) { icon(r, Icon::Foe, lx + 1, ly, 1, MAP_FOE); });
    row("Brazier", [&](float lx, float ly) { r.rect(lx + 3, ly + 2, 4, 4, AMBER); });
    r.text(Font::Small, x, y + 2, "Dim chests are open,", DIM);
    r.text(Font::Small, x, y + 17, "grey fountains dry.", DIM);
}

// A north-up window on the automap, centred on the player, under the compass.
void minimap(Renderer& r, const Game& g, float time) {
    const Level& L = g.level;
    const int reach = 6;
    const float cell = 8, size = cell * (reach * 2 + 1), x0 = UI_W - 16 - size, y0 = 70;
    Pos you = g.player.pos;
    auto at = [&](Pos p) { return std::pair{x0 + float(p.x - you.x + reach) * cell, y0 + float(p.y - you.y + reach) * cell}; };
    auto shown = [&](Pos p) { return std::abs(p.x - you.x) <= reach && std::abs(p.y - you.y) <= reach && L.inside(p) && L.at(p).seen; };

    panel(r, x0 - 5, y0 - 5, size + 10, size + 10, 0.55f);
    for (int dy = -reach; dy <= reach; dy++)
        for (int dx = -reach; dx <= reach; dx++) {
            Pos p = you + Pos{dx, dy};
            if (!shown(p)) continue;
            auto [px, py] = at(p);
            const Cell& c = L.at(p);
            switch (c.tile) {
            case Tile::Wall:
            case Tile::SecretDoor: r.rect(px, py, cell, cell, MAP_WALL); break;
            case Tile::Floor: r.rect(px, py, cell, cell, c.visible ? hex(0x302A22) : MAP_FLOOR); break;
            case Tile::Door: r.rect(px, py, cell, cell, MAP_FLOOR), r.rect(px + 2, py + 2, cell - 4, cell - 4, c.doorOpen ? hex(0x6A4828) : MAP_DOOR); break;
            case Tile::StairsDown: r.rect(px, py, cell, cell, MAP_FLOOR), r.rect(px + 1, py + 1, cell - 2, cell - 2, GOLDEN); break;
            }
            if (c.trap != TrapKind::None && c.trapFound) r.rect(px + 2, py + 2, 4, 4, BLOOD);
        }
    for (const Decor& d : L.decor) {
        if (!shown(d.pos)) continue;
        auto [px, py] = at(d.pos);
        if (d.kind == DecorKind::Chest) r.rect(px + 1, py + 2, 6, 4, d.used ? hex(0x5A4430) : MAP_DOOR);
        if (d.kind == DecorKind::Fountain) r.rect(px + 2, py + 2, 4, 4, d.used ? hex(0x606060) : MAP_WATER);
        if (d.kind == DecorKind::Brazier) r.rect(px + 2, py + 2, 4, 4, AMBER);
        if (d.kind == DecorKind::Pillar) r.rect(px + 2, py + 2, 4, 4, MAP_WALL);
    }
    for (const FloorItem& it : L.items)
        if (shown(it.pos)) {
            auto [px, py] = at(it.pos);
            r.rect(px + 3, py + 3, 2, 2, it.item.kind == ItemKind::Gold ? GOLDEN : MAP_ITEM);
        }
    for (const Monster& m : L.monsters) {
        bool disguised = (monsterDef(m.kind).flags & MF_DORMANT) && !m.awake;
        if (m.hp <= 0 || disguised || !shown(m.pos) || !L.at(m.pos).visible) continue;
        auto [px, py] = at(m.pos);
        r.rect(px + 2, py + 2, 4, 4, MAP_FOE);
    }
    auto [px, py] = at(you);
    icon(r, Icon::Arrow, px, py, 1, fade(MAP_YOU, 0.8f + 0.2f * std::sin(time * 6)), GOLDEN, 1, g.player.facing);
    r.text(Font::Small, x0 + size / 2, y0 + size + 4, "M  map", fade(DIM, 0.7f), Align::Center);
}

// ---- Cards ----

const char* const EPITAPHS[] = {
    "The dark keeps what the torch gives up.",
    "Brave enough to descend. Not enough to return.",
    "Another name for the walls to forget.",
    "The Undercroft is patient. It always is.",
    "Rest now. The rats will keep the watch.",
    "Here the light went out, and no one came looking.",
};

const char* themeName(Theme t) { return t == Theme::Crypt ? "The Crypt" : t == Theme::Catacomb ? "The Catacombs" : "The Ember Halls"; }

void statStrip(Renderer& r, float y, const std::vector<std::pair<std::string, const char*>>& stats, float a) {
    const float cell = 92;
    float x0 = UI_W / 2 - cell * float(stats.size()) / 2;
    for (size_t i = 0; i < stats.size(); i++) {
        float cx = x0 + cell * (float(i) + 0.5f);
        r.text(Font::Body, cx, y, stats[i].first, fade(INK, a), Align::Center);
        tracked(r, Font::Small, cx, y + 24, stats[i].second, fade(DIM, a), 2, Align::Center);
        if (i) r.rect(x0 + cell * float(i), y + 4, 1, 34, fade(FAINT, a));
    }
}

void runSummary(Renderer& r, const Game& g, float y, float a, uint32_t accent) {
    const Player& p = g.player;
    statStrip(r, y,
              {{std::to_string(g.level.depth), "DEPTH"},
               {std::to_string(p.level), "LEVEL"},
               {std::to_string(p.kills), "KILLS"},
               {grouped(p.gold), "GOLD"},
               {grouped(g.turn), "TURNS"}},
              a);
    tracked(r, Font::Small, UI_W / 2, y + 66, "SCORE", fade(DIM, a), 4, Align::Center);
    tracked(r, Font::Title, UI_W / 2, y + 80, grouped(g.score()), fade(accent, a), 4, Align::Center);
}

// A heavy title with a carved drop shadow.
void cardTitle(Renderer& r, float y, const std::string& s, uint32_t c, uint32_t shadow, float a) {
    tracked(r, Font::Title, UI_W / 2, y + 3, s, fade(shadow, a), 6, Align::Center);
    tracked(r, Font::Title, UI_W / 2, y, s, fade(c, a), 6, Align::Center);
}

void pressEnter(Renderer& r, float t, float after) {
    if (t < after) return;
    float a = std::min(1.0f, (t - after) * 2) * (0.6f + 0.4f * std::sin(t * 3.5f));
    tracked(r, Font::Small, UI_W / 2, UI_H - 44, "PRESS ENTER", fade(INK, a), 4, Align::Center);
}

// The title wordmark: each carved letter catches the torchlight on its own beat.
void wordmark(Renderer& r, float y, const std::string& s, float gap, float time) {
    float x = std::floor(UI_W / 2 - (r.textWidth(Font::Title, s) + gap * float(s.size() - 1)) / 2);
    for (size_t i = 0; i < s.size(); i++) {
        std::string_view ch(&s[i], 1);
        float lit = 0.78f + 0.15f * std::sin(time * (1.3f + hash(int(i), 8) * 2.5f) + float(i) * 1.7f) + 0.07f * std::sin(time * 11.3f + float(i) * 2.3f);
        r.text(Font::Title, x - 2, y, ch, fade(hex(0xC04010), 0.16f * lit), Align::Left, false);
        r.text(Font::Title, x + 2, y, ch, fade(hex(0xC04010), 0.16f * lit), Align::Left, false);
        r.text(Font::Title, x, y + 3, ch, hex(0x200A04, 230), Align::Left, false);
        r.text(Font::Title, x, y, ch, mix(hex(0x6A3A14), hex(0xF8D070), lit));
        x += r.textWidth(Font::Title, ch) + gap;
    }
}

float helpRow(Renderer& r, float x, float y, float keyW, float width, const char* key, const char* what) {
    r.text(Font::Small, x, y, key, GOLDEN);
    float ly = y;
    for (const std::string& line : wrap(r, Font::Small, what, width - keyW)) r.text(Font::Small, x + keyW, ly, line, INK), ly += 15;
    return ly - y + 5;
}

}  // namespace

namespace ui {

std::vector<int> packOrder(const Game& g) {
    const std::vector<Item>& pack = g.player.pack;
    std::vector<int> order(pack.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return itemClass(pack[size_t(a)].kind) < itemClass(pack[size_t(b)].kind); });
    return order;
}

void hud(Renderer& r, const Game& g, const HudState& h) {
    const Player& p = g.player;
    if (p.hp > 0 && p.hp * 4 <= p.maxHp) vignette(r, hex(0x901010), 0.22f + 0.12f * std::sin(h.time * 5));
    for (int i = 0; i < 3; i++) r.rect(0, UI_H - 84 + float(i) * 18, UI_W, 84 - float(i) * 18, rgba(0, 0, 0, 38));

    floatingText(r, h);
    hurtIndicator(r, h);
    messageLog(r, g);
    compass(r, g);
    minimap(r, g, h.time);
    vitals(r, g, h);
    progress(r, g);
    prompts(r, g, h);
    levelBanner(r, g, h);
}

void inventory(Renderer& r, const Game& g, int selected) {
    const Player& p = g.player;
    float x = 24, y = 12, w = UI_W - 48, h = UI_H - 24;
    r.rect(0, 0, UI_W, UI_H, rgba(0, 0, 0, 140));
    panel(r, x, y, w, h, 0.95f);
    tracked(r, Font::Body, x + 20, y + 12, "PACK", GOLDEN, 4);
    r.text(Font::Small, x + 284, y + 16, std::to_string(p.pack.size()) + " / " + std::to_string(PACK_SIZE), DIM, Align::Right);
    r.rect(x + 300, y + 14, 1, h - 52, FAINT);

    float footer = y + h - 32;
    packList(r, g, selected, x + 20, y + 44, x + 284, footer - 6);
    equipment(r, g, x + 320, y + 16, x + w - 20);
    if (selected >= 0 && selected < int(p.pack.size())) itemDetails(r, g, p.pack[size_t(selected)], x + 320, y + 206, x + w - 20, footer - 26);

    r.rect(x + 16, footer, w - 32, 1, FAINT);
    r.text(Font::Small, x + w / 2, footer + 7, "W/S or a letter to choose   ·   Esc close", DIM, Align::Center);
}

void automap(Renderer& r, const Game& g, float time) {
    const Level& L = g.level;
    r.rect(0, 0, UI_W, UI_H, rgba(6, 5, 8, 236));
    float ox = 24, oy = std::floor((UI_H - L.height * MAP_CELL) / 2) + 14, mw = L.width * MAP_CELL, mh = L.height * MAP_CELL;
    tracked(r, Font::Body, ox, 12, "DEPTH " + std::to_string(L.depth), GOLDEN, 3);
    r.text(Font::Small, ox + mw, 17, themeName(L.theme), DIM, Align::Right);
    r.rect(ox - 4, oy - 4, mw + 8, 1, FAINT);
    r.rect(ox - 4, oy + mh + 3, mw + 8, 1, FAINT);
    auto at = [&](Pos p) { return std::pair{ox + float(p.x) * MAP_CELL, oy + float(p.y) * MAP_CELL}; };

    int seen = 0, open = 0;
    for (int y = 0; y < L.height; y++)
        for (int x = 0; x < L.width; x++) {
            Pos p{x, y};
            bool floor = !solid(L.tile(p));
            open += floor;
            if (!L.at(p).seen) continue;
            seen += floor;
            auto [px, py] = at(p);
            mapCell(r, L, p, px, py);
        }
    for (const Decor& d : L.decor)
        if (L.at(d.pos).seen) mapDecor(r, d, at(d.pos).first, at(d.pos).second);
    for (const FloorItem& it : L.items) {
        if (!L.at(it.pos).seen) continue;
        auto [px, py] = at(it.pos);
        if (it.item.kind == ItemKind::Gold) r.rect(px + 3, py + 3, 4, 4, GOLDEN);
        else diamond(r, px + 4, py + 4, 2, MAP_ITEM);
    }
    int foes = 0;
    for (const Monster& m : L.monsters) {
        bool disguised = (monsterDef(m.kind).flags & MF_DORMANT) && !m.awake;
        if (m.hp <= 0 || !L.at(m.pos).visible || disguised) continue;
        foes++;
        icon(r, Icon::Foe, at(m.pos).first + 1, at(m.pos).second + 1, 1, MAP_FOE);
    }
    auto [px, py] = at(g.player.pos);
    icon(r, Icon::Arrow, px + 1, py + 1, 1, fade(MAP_YOU, 0.75f + 0.25f * std::sin(time * 6)), GOLDEN, 1, g.player.facing);

    float lx = ox + mw + 28, right = UI_W - 20;
    mapLegend(r, lx, oy + 4, right);
    float sy = oy + mh - 52;
    sectionLabel(r, lx, sy, right, "THIS FLOOR");
    r.text(Font::Small, lx, sy + 18, "Explored", DIM);
    r.text(Font::Small, right, sy + 18, std::to_string(open ? seen * 100 / open : 0) + "%", INK, Align::Right);
    r.text(Font::Small, lx, sy + 34, "Foes in sight", DIM);
    r.text(Font::Small, right, sy + 34, std::to_string(foes), foes ? WORSE : INK, Align::Right);
    r.text(Font::Small, ox + mw, UI_H - 22, "M  close", DIM, Align::Right);
}

void title(Renderer& r, float time, int menu) {
    r.rect(0, 0, UI_W, UI_H, rgba(0, 0, 0, 120));
    for (int i = 0; i < 4; i++) r.rect(0, UI_H - 140 + float(i) * 35, UI_W, 140 - float(i) * 35, rgba(0, 0, 0, 34));
    motes(r, time, 64, hex(0xFFD070), hex(0xC03818), 1);

    float ty = 116;
    tracked(r, Font::Small, UI_W / 2, ty - 28, "A DESCENT IN TEN FLOORS", hex(0x7A6A50), 6, Align::Center);
    wordmark(r, ty, "UNDERCROFT", 9, time);
    ornament(r, UI_W / 2, ty + 66, 150, fade(GOLDEN, 0.7f));
    tracked(r, Font::Small, UI_W / 2, ty + 80, "THE LICH WAITS AT THE BOTTOM", DIM, 4, Align::Center);

    if (menu >= 0) {
        const char* items[TITLE_MENU_ITEMS] = {"Descend", "How to play", "Credits", "Leave"};
        for (int i = 0; i < TITLE_MENU_ITEMS; i++) {
            float y = 284 + float(i) * 28;
            float w = r.text(Font::Body, UI_W / 2, y, items[i], i == menu ? INK : DIM, Align::Center);
            if (i != menu) continue;
            uint32_t c = fade(GOLDEN, 0.7f + 0.3f * std::sin(time * 4));
            diamond(r, UI_W / 2 - w / 2 - 16, y + 10, 3, c);
            diamond(r, UI_W / 2 + w / 2 + 15, y + 10, 3, c);
        }
    } else {
        r.text(Font::Body, UI_W / 2, 296, "Press Enter to descend", fade(INK, 0.55f + 0.45f * std::sin(time * 2.6f)), Align::Center);
    }
    r.text(Font::Small, UI_W / 2, UI_H - 34, "WASD move  ·  Q/E turn  ·  Space attack  ·  I pack  ·  M map  ·  H help", hex(0x7A7062), Align::Center);
}

void death(Renderer& r, const Game& g, float t) {
    float a = std::clamp(t / 1.5f, 0.0f, 1.0f);
    r.rect(0, 0, UI_W, UI_H, rgba(14, 0, 0, int(a * 175)));
    vignette(r, hex(0x300000), a * 0.6f);
    motes(r, t, 30, hex(0x8A8078), hex(0x4A4440), a * 0.6f, 1);

    cardTitle(r, 84 - (1 - a) * 12, "YOU HAVE DIED", BLOOD, hex(0x200000), a);
    ornament(r, UI_W / 2, 150, 140, fade(hex(0x802020), a));
    std::string cause = g.deathCause.empty() ? "Lost in the dark" : capitalised(g.deathCause);
    r.text(Font::Body, UI_W / 2, 166, cause + ".", fade(INK, a), Align::Center);
    const char* epitaph = EPITAPHS[(g.seed + uint32_t(g.turn)) % std::size(EPITAPHS)];
    r.text(Font::Small, UI_W / 2, 194, std::string("\"") + epitaph + "\"", fade(DIM, a), Align::Center);

    runSummary(r, g, 246, std::clamp(t - 0.8f, 0.0f, 1.0f), hex(0xD04838));
    pressEnter(r, t, 2);
}

void victory(Renderer& r, const Game& g, float t) {
    float a = std::clamp(t / 2.0f, 0.0f, 1.0f);
    r.rect(0, 0, UI_W, UI_H, rgba(0, 0, 0, int(a * 150)));
    motes(r, t, 60, hex(0xFFE090), hex(0xC07020), a);

    cardTitle(r, 84, "THE LICH IS DUST", GOLDEN, hex(0x2A1004), a);
    ornament(r, UI_W / 2, 150, 140, fade(GOLDEN, a * 0.7f));
    r.text(Font::Body, UI_W / 2, 166, "You climb out of the Undercroft with " + grouped(g.player.gold) + " gold.", fade(INK, a), Align::Center);
    r.text(Font::Small, UI_W / 2, 194, "\"Below, at last, nothing stirs.\"", fade(DIM, a), Align::Center);

    runSummary(r, g, 246, std::clamp(t - 1.0f, 0.0f, 1.0f), GOLDEN);
    pressEnter(r, t, 2.5f);
}

void help(Renderer& r, bool inRun) {
    float x = 24, y = 18, w = UI_W - 48, h = UI_H - 36;
    r.rect(0, 0, UI_W, UI_H, rgba(0, 0, 0, 120));
    panel(r, x, y, w, h, 0.95f);
    tracked(r, Font::Body, x + w / 2, y + 12, "HOW TO SURVIVE", GOLDEN, 4, Align::Center);
    ornament(r, x + w / 2, y + 40, 120, fade(GOLDEN, 0.6f));

    float colW = (w - 84) / 2, keyW = 86;
    float lx = x + 30, rx = x + 54 + colW, ly = y + 56, ry = y + 56;
    auto section = [&](float cx, float& cy, const char* name) {
        sectionLabel(r, cx, cy, cx + colW, name);
        cy += 18;
    };
    auto row = [&](float cx, float& cy, const char* key, const char* what) { cy += helpRow(r, cx, cy, keyW, colW, key, what); };

    section(lx, ly, "MOVING");
    row(lx, ly, "W / S", "step forward, step back");
    row(lx, ly, "A / D", "sidestep");
    row(lx, ly, "Q / E", "turn; the arrow keys work too");
    row(lx, ly, "Hold", "keep walking");
    ly += 14;
    section(lx, ly, "FIGHTING");
    row(lx, ly, "Space / F", "strike the square ahead");
    row(lx, ly, "Walk into", "a foe to attack it");
    ly += 14;
    section(lx, ly, "WAITING");
    row(lx, ly, "Z / .", "wait a turn. Waiting also searches for hidden traps and secret doors.");
    row(lx, ly, "R", "rest until healed. A foe coming into view stops it.");

    section(rx, ry, "THE DUNGEON");
    row(rx, ry, "Enter", "take the stairs down");
    row(rx, ry, "Chest", "bump it to open it");
    row(rx, ry, "Fountain", "bump it to drink. Each works once, for good or ill.");
    row(rx, ry, "Traps", "stay hidden until found or sprung");
    ry += 14;
    section(rx, ry, "YOUR THINGS");
    row(rx, ry, "I / Tab", "the pack: drink, read, zap, wield, wear, drop");
    row(rx, ry, "M", "map of this floor");
    ry += 14;
    section(rx, ry, "THIS CARD");
    row(rx, ry, "H / Esc", "close");
    if (inRun) row(rx, ry, "Q", "abandon the run");
    row(rx, ry, "F11", "fullscreen");

    float fy = y + h - 50;
    r.rect(x + 20, fy - 10, w - 40, 1, FAINT);
    r.text(Font::Small, x + w / 2, fy, "Your torch burns down every turn. Flasks of oil feed it.", DIM, Align::Center);
    r.text(Font::Small, x + w / 2, fy + 16, "Potions, scrolls, wands and rings reveal what they are once used.", DIM, Align::Center);
}

// The sound credits (also in sounds/CREDITS.md and on the web page).
void credits(Renderer& r, float time) {
    float x = 24, y = 18, w = UI_W - 48, h = UI_H - 36;
    r.rect(0, 0, UI_W, UI_H, rgba(0, 0, 0, 150));
    panel(r, x, y, w, h, 0.95f);
    tracked(r, Font::Body, x + w / 2, y + 12, "CREDITS", GOLDEN, 4, Align::Center);
    ornament(r, x + w / 2, y + 40, 120, fade(GOLDEN, 0.6f));

    float lx = x + 40, right = x + w - 40, ly = y + 62;
    auto entry = [&](const char* who, const char* what, const char* where) {
        r.text(Font::Body, lx, ly, who, INK);
        r.text(Font::Small, right, ly + 3, where, DIM, Align::Right);
        r.text(Font::Small, lx, ly + 20, what, hex(0xB0A488));
        ly += 46;
    };
    sectionLabel(r, lx, ly, right, "SOUND EFFECTS");
    ly += 22;
    entry("Kenney", "RPG Audio, Impact Sounds: footsteps, doors, coins, impacts", "kenney.nl");
    entry("artisticdude", "RPG Sound Pack: creature voices, swings, armour, slime", "opengameart.org");
    entry("rubberduck", "80 CC0 creature SFX: roars, squeaks, screeches, hurts", "opengameart.org");
    sectionLabel(r, lx, ly, right, "MUSIC");
    ly += 22;
    entry("yd", "Dungeon Ambience: the music while exploring", "opengameart.org");

    float fy = y + h - 74;
    r.rect(x + 20, fy - 10, w - 40, 1, FAINT);
    r.text(Font::Small, x + w / 2, fy, "All recordings are public domain (CC0 1.0). Thank you to their authors.", INK, Align::Center);
    r.text(Font::Small, x + w / 2, fy + 16, "Every other sound, the title and Lich themes, the art and the code are made by the game itself.", DIM, Align::Center);
    r.text(Font::Small, x + w / 2, fy + 40, "Esc  back", fade(DIM, 0.7f + 0.3f * std::sin(time * 3)), Align::Center);
}

}  // namespace ui
