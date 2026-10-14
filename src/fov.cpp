// Field of view (recursive shadowcasting), line of sight and BFS distance maps.
#include "world.h"
#include <deque>

namespace {

// One octant of Björn Bergström's recursive shadowcasting.
void castLight(Level& L, Pos o, int radius, int row, float start, float end, int xx, int xy, int yx, int yy) {
    if (start < end) return;
    float newStart = 0;
    for (int j = row; j <= radius; j++) {
        bool blocked = false;
        for (int dx = -j, dy = -j; dx <= 0; dx++) {
            float lSlope = (dx - 0.5f) / (dy + 0.5f), rSlope = (dx + 0.5f) / (dy - 0.5f);
            if (start < rSlope) continue;
            if (end > lSlope) break;
            Pos p{o.x + dx * xx + dy * xy, o.y + dx * yx + dy * yy};
            if (!L.inside(p)) continue;
            if (dx * dx + dy * dy <= radius * radius) {
                L.at(p).visible = true;
                L.at(p).seen = true;
            }
            if (blocked) {
                if (L.opaque(p)) {
                    newStart = rSlope;
                } else {
                    blocked = false;
                    start = newStart;
                }
            } else if (L.opaque(p) && j < radius) {
                blocked = true;
                castLight(L, o, radius, j + 1, start, lSlope, xx, xy, yx, yy);
                newStart = rSlope;
            }
        }
        if (blocked) break;
    }
}

}  // namespace

void computeFov(Level& L, Pos o, int radius) {
    for (Cell& c : L.cells) c.visible = false;
    L.at(o).visible = L.at(o).seen = true;
    static const int M[8][4] = {{1, 0, 0, 1}, {0, 1, 1, 0}, {0, -1, 1, 0}, {-1, 0, 0, 1},
                                {-1, 0, 0, -1}, {0, -1, -1, 0}, {0, 1, -1, 0}, {1, 0, 0, -1}};
    for (auto& m : M) castLight(L, o, radius, 1, 1.0f, 0.0f, m[0], m[1], m[2], m[3]);
}

// Bresenham walk; the end cells themselves may be opaque.
bool lineOfSight(const Level& L, Pos a, Pos b) {
    int dx = std::abs(b.x - a.x), dy = -std::abs(b.y - a.y);
    int sx = a.x < b.x ? 1 : -1, sy = a.y < b.y ? 1 : -1, err = dx + dy;
    Pos p = a;
    while (!(p == b)) {
        int e2 = 2 * err;
        if (e2 >= dy) err += dy, p.x += sx;
        if (e2 <= dx) err += dx, p.y += sy;
        if (!(p == b) && L.opaque(p)) return false;
    }
    return true;
}

std::vector<int> distanceMap(const Level& L, Pos from) {
    std::vector<int> dist(size_t(L.width) * L.height, -1);
    std::deque<Pos> open{from};
    dist[size_t(from.y) * L.width + from.x] = 0;
    while (!open.empty()) {
        Pos p = open.front();
        open.pop_front();
        int d = dist[size_t(p.y) * L.width + p.x];
        for (Pos dir : DIRS) {
            Pos n = p + dir;
            if (!L.walkable(n) || dist[size_t(n.y) * L.width + n.x] >= 0) continue;
            dist[size_t(n.y) * L.width + n.x] = d + 1;
            open.push_back(n);
        }
    }
    return dist;
}
