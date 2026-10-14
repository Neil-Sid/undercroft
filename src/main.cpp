// UNDERCROFT: Win32 window, OpenGL context and the main loop.
//   undercroft.exe                      play
//   undercroft.exe --shot out.png [--at 1.5] [--seed 7] [--depth 3] [--keys wwqf>] [--spawn skeleton,chest]
//                  [--dist 1] [--anim attack] [--screen inventory] [--scale 2] [--reveal] [--torch 300]
//                  [--yaw 1] [--wield axe] [--give "wand of lightning"]
//       renders offscreen at a fixed 60 fps without showing a window, and saves one frame
//       (640x480 times --scale). Keys: wasdqe move/turn, f attack, . wait, > descend/use, i pack,
//       m map, < escape; in the pack a letter picks a row.
//       --spawn also takes chest, fountain, trap (spikes) and trap-dart/-alarm/-pit/-teleport[-hidden].
//   undercroft.exe --viewer skeleton [--anim walk] [--yaw 1] [--shot out.png]
//       one model turning slowly on a dark stage, for art review.
#define NOMINMAX
#include "gl.h"
#include "app.h"
#include "audio.h"
#include "models.h"
#include "paint.h"
#include "textures.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

App* app;
bool fullscreen;
WINDOWPLACEMENT windowed;

struct Options {
    const char* shot = nullptr;
    float at = 1.5f;
    uint32_t seed = 0;
    int depth = 1, scale = 2, torch = -1, dist = 2, yaw = 0;
    std::string keys, spawn, anim, screen, viewer, wield, give;
    bool reveal = false;
};

Options parse(int argc, char** argv) {
    Options o;
    o.seed = GetTickCount();
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        bool more = i + 1 < argc;
        if (a == "--shot" && more) o.shot = argv[++i];
        else if (a == "--at" && more) o.at = float(std::atof(argv[++i]));
        else if (a == "--seed" && more) o.seed = uint32_t(std::strtoul(argv[++i], nullptr, 10));
        else if (a == "--depth" && more) o.depth = std::atoi(argv[++i]);
        else if (a == "--scale" && more) o.scale = std::max(1, std::atoi(argv[++i]));
        else if (a == "--torch" && more) o.torch = std::atoi(argv[++i]);
        else if (a == "--dist" && more) o.dist = std::max(1, std::atoi(argv[++i]));
        else if (a == "--keys" && more) o.keys = argv[++i];
        else if (a == "--spawn" && more) o.spawn = argv[++i];
        else if (a == "--anim" && more) o.anim = argv[++i];
        else if (a == "--screen" && more) o.screen = argv[++i];
        else if (a == "--yaw" && more) o.yaw = std::atoi(argv[++i]);
        else if (a == "--viewer" && more) o.viewer = argv[++i];
        else if (a == "--wield" && more) o.wield = argv[++i];
        else if (a == "--give" && more) o.give = argv[++i];
        else if (a == "--reveal") o.reveal = true;
    }
    return o;
}

int findItem(const std::string& name) {
    for (int k = 0; k < int(ItemKind::Count); k++)
        if (std::string(itemDef(ItemKind(k)).name).find(name) != std::string::npos) return k;
    return -1;
}

std::string modelName(int id) {
    const char* props[] = {"potion", "scroll", "wand", "ring", "oil", "gold", "brazier", "pillar", "bones", "chest", "fountain", "door", "torch", "hand", "trap", "dry fountain"};
    if (id < int(MonsterKind::Count)) return monsterDef(MonsterKind(id)).name;
    if (id < int(ModelId::Potion)) return itemDef(ItemKind(id - int(ModelId::Dagger))).name;
    size_t prop = size_t(id - int(ModelId::Potion));
    return prop < std::size(props) ? props[prop] : "model " + std::to_string(id);
}

// A ModelId by number, exact name, or the first name containing `name`.
int findModel(const std::string& name) {
    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) return std::clamp(std::atoi(name.c_str()), 0, int(ModelId::Count) - 1);
    for (int id = 0; id < int(ModelId::Count); id++)
        if (modelName(id) == name) return id;
    for (int id = 0; id < int(ModelId::Count); id++)
        if (modelName(id).find(name) != std::string::npos) return id;
    return 0;
}

// Stairs, chests, fountains and traps for --spawn; "secret" hides a door in the wall beyond `p`.
// Traps are found unless the name says "hidden".
bool spawnFixture(Game& g, const std::string& name, Pos p) {
    if (name == "stairs") {
        g.level.at(p).tile = Tile::StairsDown;
        return true;
    }
    if (name == "secret") {
        Pos wall = p + DIRS[g.player.facing];
        if (g.level.tile(wall) == Tile::Wall) g.level.at(wall).tile = Tile::SecretDoor;
        return true;
    }
    if (name == "chest" || name == "fountain") {
        g.level.decor.push_back({name == "chest" ? DecorKind::Chest : DecorKind::Fountain, p});
        return true;
    }
    if (name.rfind("trap", 0) != 0) return false;
    const char* kinds[] = {"", "spikes", "dart", "alarm", "pit", "teleport"};
    Cell& c = g.level.at(p);
    c.trap = TrapKind::Spikes;
    for (int k = 1; k < int(std::size(kinds)); k++)
        if (name.find(kinds[k]) != std::string::npos) c.trap = TrapKind(k);
    c.trapFound = name.find("hidden") == std::string::npos;
    return true;
}

// Puts a named monster, item or fixture (comma-separated list) on the first free cells ahead of the player.
void spawnAhead(Game& g, const std::string& list, int distance) {
    size_t start = 0;
    while (start < list.size()) {
        size_t end = list.find(',', start);
        std::string name = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? list.size() : end + 1;
        Pos p = g.player.pos;
        for (int d = 1; d <= distance; d++)
            if (g.level.walkable(g.player.pos + Pos{DIRS[g.player.facing].x * d, DIRS[g.player.facing].y * d}))
                p = g.player.pos + Pos{DIRS[g.player.facing].x * d, DIRS[g.player.facing].y * d};
            else
                break;
        if (spawnFixture(g, name, p)) name.clear();
        for (int k = 0; k < int(MonsterKind::Count) && !name.empty(); k++)
            if (std::string(monsterDef(MonsterKind(k)).name).find(name) != std::string::npos) {
                g.spawnMonster(MonsterKind(k), p);
                name.clear();
                break;
            }
        for (int k = 0; k < int(ItemKind::Count) && !name.empty(); k++)
            if (std::string(itemDef(ItemKind(k)).name).find(name) != std::string::npos) {
                g.spawnItem({ItemKind(k), k == int(ItemKind::Gold) ? 50 : 1}, p);
                break;
            }
        distance++;
    }
}

void toggleFullscreen(HWND hwnd) {
    LONG style = GetWindowLongA(hwnd, GWL_STYLE);
    if (!fullscreen) {
        MONITORINFO mi{};
        mi.cbSize = sizeof mi;
        windowed.length = sizeof windowed;
        GetWindowPlacement(hwnd, &windowed);
        GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongA(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        RECT m = mi.rcMonitor;
        SetWindowPos(hwnd, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongA(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &windowed);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    fullscreen = !fullscreen;
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wp == VK_F11) toggleFullscreen(hwnd);
        else if (wp == VK_F4 && (lp & (1 << 29))) PostMessageA(hwnd, WM_CLOSE, 0, 0);
        else if (app && !(lp & (1 << 30))) app->key(int(wp));
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (app) app->keyUp(int(wp));
        return 0;
    case WM_KILLFOCUS:
        if (app) app->focusLost();
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

Pixels upscale(const Pixels& src, int k) {
    Pixels out{src.w * k, src.h * k, std::vector<uint32_t>(size_t(src.w * k) * src.h * k)};
    for (int y = 0; y < out.h; y++)
        for (int x = 0; x < out.w; x++) out.rgba[size_t(y) * out.w + x] = src.rgba[size_t(y / k) * src.w + x / k];
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt = parse(argc, argv);
    SetProcessDPIAware();

    HINSTANCE inst = GetModuleHandleA(nullptr);
    WNDCLASSA wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = "Undercroft";
    RegisterClassA(&wc);

    RECT rc{0, 0, 1280, 960};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowA("Undercroft", "Undercroft", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                              nullptr, nullptr, inst, nullptr);

    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof pfd;
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC context = wglCreateContext(dc);
    wglMakeCurrent(dc, context);
    if (!loadGL()) {
        MessageBoxA(hwnd, "Undercroft needs OpenGL 3.3 or newer.", "Undercroft", MB_ICONERROR);
        return 1;
    }
    wglSwapIntervalEXT(opt.shot ? 0 : 1);

    paint::startup();
    Renderer renderer;
    renderer.init(buildAtlas());
    buildModels(renderer);
    if (!opt.shot) audio::start();

    App a(renderer, opt.seed);
    app = &a;
    const char* anims[] = {"idle", "walk", "attack", "hurt", "die"};
    for (int i = 1; i < int(std::size(anims)); i++)
        if (opt.anim == anims[i]) a.debug.forceAnim = Anim(i), a.debug.forceAnimOn = true;
    if (!opt.viewer.empty()) {
        a.debug.viewer = findModel(opt.viewer);
        a.debug.viewerName = modelName(a.debug.viewer);
        a.debug.viewerYaw = float(opt.yaw) * PI / 2;
    } else if (opt.shot) {
        a.debug.keys = opt.keys;
        if (opt.screen != "title") {
            a.startRun(opt.seed, std::max(1, opt.depth));
            a.screen = Screen::Play;
            if (opt.yaw) a.game.player.facing = ((a.game.player.facing + opt.yaw) % 4 + 4) % 4, a.snapCamera();
            if (int k = findItem(opt.wield); !opt.wield.empty() && k >= 0) a.game.player.weapon = {ItemKind(k)};
            if (int k = findItem(opt.give); !opt.give.empty() && k >= 0) a.game.player.pack.insert(a.game.player.pack.begin(), {ItemKind(k), 1, 0, 5});
            if (!opt.spawn.empty()) spawnAhead(a.game, opt.spawn, opt.dist), a.snapCamera();
            if (opt.reveal) a.game.revealMap();
            if (opt.torch >= 0) a.game.player.torch = opt.torch;
            if (opt.screen == "inventory") a.showScreen(Screen::Inventory);
            if (opt.screen == "map") a.showScreen(Screen::Map);
            if (opt.screen == "help") a.showScreen(Screen::Help);
            if (opt.screen == "credits") a.showScreen(Screen::Credits);
            if (opt.screen == "dead") a.game.outcome = Outcome::Dead, a.game.deathCause = "Slain by ghoul on depth 4", a.showScreen(Screen::Dead);
            if (opt.screen == "won") a.game.outcome = Outcome::Won, a.showScreen(Screen::Won);
        }
    }
    if (!opt.shot) ShowWindow(hwnd, SW_SHOW);

    LARGE_INTEGER freq, last, current;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    float clock = 0;
    int exitCode = 0;
    for (bool running = true; running;) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running) break;
        if (a.quit) {
            DestroyWindow(hwnd);
            continue;
        }

        QueryPerformanceCounter(&current);
        float dt = opt.shot ? 1 / 60.0f : std::min(0.1f, float(current.QuadPart - last.QuadPart) / float(freq.QuadPart));
        last = current;
        clock += dt;
        a.update(dt);

        RECT client;
        GetClientRect(hwnd, &client);
        if (!opt.shot && (client.right == 0 || client.bottom == 0)) {
            Sleep(16);
            continue;
        }
        a.render();
        if (opt.shot && clock >= opt.at) {
            exitCode = paint::savePng(upscale(renderer.readFrame(), opt.scale), opt.shot) ? 0 : 1;
            break;
        }
        renderer.present(client.right, client.bottom);
        SwapBuffers(dc);
    }

    app = nullptr;
    audio::stop();
    paint::shutdown();
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(context);
    return exitCode;
}
