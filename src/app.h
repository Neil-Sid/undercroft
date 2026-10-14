#pragma once
// Screens, input and presentation: turns game events into camera moves, monster
// animation, sound and effects.
#include "audio.h"
#include "game.h"
#include "models.h"
#include "render.h"
#include "ui.h"
#include <string>
#include <vector>

enum class Screen : uint8_t { Title, Play, Inventory, Map, Help, Dead, Won, Credits };

struct DebugOptions {
    std::string keys;        // scripted keypresses for the screenshot harness
    Anim forceAnim = Anim::Idle;
    bool forceAnimOn = false;
    int viewer = -1;          // a ModelId shown alone on a dark background, or -1
    std::string viewerName;
    float viewerYaw = 0;
};

// A spray of particles: velocity is `speed` outwards and `lift` upwards, both jittered.
struct Burst {
    uint32_t color = 0xFFFFFFFF;
    Tex tex = Tex::White;
    float size = 0.07f, speed = 1.5f, lift = 2, gravity = 9, life = 0.6f, grow = 0;
    bool glow = false, bounce = false;
};

class App {
public:
    App(Renderer& r, uint32_t seed);
    void startRun(uint32_t seed, int depth = 1);
    void key(int vk);
    void keyUp(int vk);
    void update(float dt);
    void render();
    void showScreen(Screen s);  // harness: jump straight to a screen
    void snapCamera();          // harness: after moving the player or editing the level directly
    void focusLost() { held.clear(); }

    Game game;
    Screen screen = Screen::Title;
    DebugOptions debug;
    bool quit = false;

private:
    struct MonsterView {
        int id = 0;
        MonsterKind kind{};
        Vec3 from, to;
        float moveT = 1;
        float yaw = 0, targetYaw = 0;
        Anim anim = Anim::Idle;
        float animT = 0;
        float flash = 0;
        bool dead = false;
        float deadT = 0;
        float sink = 0.6f;     // metres per second the corpse sinks once the death pose ends
        bool dormant = false;  // still disguised as a chest or a statue
        Vec3 knock;            // recoil from the last hit, easing back to zero
        float rise = 1;        // 0 when summoned, climbing out of the floor to 1; negative until the summon plays
    };
    struct Bolt {
        Vec3 from, to;
        float t = 0, time = 0.3f;
        uint32_t color = 0;
        bool dart = false;
    };
    struct Particle {
        Vec3 pos, vel;
        float life = 0, maxLife = 1, size = 0.07f, gravity = 9, grow = 0;
        uint32_t color = 0xFFFFFFFF;
        Tex tex = Tex::White;
        bool glow = false, bounce = false;
    };
    struct Beam {
        Vec3 from, to;
        float age = 0, life = 0.4f;
        ItemKind kind{};
    };
    struct Flare {  // a light that fades out
        Vec3 pos, color;
        float radius = 6, age = 0, life = 0.5f;
    };
    struct Floater {
        Vec3 pos;
        std::string text;
        uint32_t color = 0xFFFFFFFF;
        float age = 0;
    };
    struct Pending {
        float delay = 0;
        Event event;
    };
    struct Camera {
        Vec3 eye, forward, up;
    };

    void act(Action a);
    void runQueue();
    void handleEvents();
    void present(const Event& e);
    void presentTrap(const Event& e, MonsterView* v);
    void presentZap(const Event& e);
    void startSwing();
    void addViews(const std::vector<Event>& batch = {});
    void pruneViews();
    void resetView();
    void inventoryKey(int vk);
    void titleKey(int vk);
    void playAt(Sound sound, Vec3 where, float gain = 1, float pitch = 1);
    void creatureAt(MonsterKind kind, Cue cue, Vec3 where, float gain = 1);
    void burst(Vec3 at, int count, const Burst& b, Vec3 drift = {});
    void gore(const MonsterView& v, int count);
    void addFloat(Vec3 at, std::string text, uint32_t color);
    void updateCamera(float dt);
    void updateMonsters(float dt);
    void updateEffects(float dt);
    void updatePending(float dt);
    float random();
    int cellIndex(Pos p) const { return p.y * game.level.width + p.x; }
    Vec3 camNow() const;
    Vec3 eyePos() const;
    Vec3 forward() const;
    Camera camera() const;
    Vec3 monsterPos(const MonsterView& v) const;
    float monsterTop(const MonsterView& v) const;
    MonsterView* view(int id);
    bool busy() const;
    void drawWorld();
    void drawViewer();
    void drawProps(const Camera& c, std::vector<Vertex>& glow, std::vector<Vertex>& solid);
    void drawMonsters(Vec3 eye);
    void drawEffects(const Camera& c, std::vector<Vertex>& glow, std::vector<Vertex>& solid);
    void drawViewmodel(const Camera& c);
    void drawArm(const Mat4& cam, const Mat4& grip, Vec3 shoulder, bool left);
    void projectFloats(const Mat4& viewProj);

    Renderer& r;
    uint32_t seed;
    float time = 0;
    float screenT = 0;
    uint32_t fxSeed = 1;

    // Camera: animated between grid positions and facings.
    Vec3 camFrom, camTo;
    float camT = 1;
    float yawFrom = 0, yawTo = 0, turnT = 1;
    Vec3 bumpDir;
    float bumpT = 1;
    float shake = 0;
    float swingT = 10;  // seconds since the current swing began
    float brightness = 1;
    Vec4 flash;
    float stairDip = 0;
    float landT = 10;   // seconds since landing from a pit
    float flinchT = 10; // seconds since the player was hit
    float dyingT = -1;  // seconds since death, or -1
    float victoryT = -1;
    float hitStop = 0;  // seconds the world stays frozen after a kill
    float torchBoost = 0;
    bool blinking = false;
    bool meshDirty = false;          // a secret door opened: rebuild the level mesh before drawing
    uint64_t soundsThisFrame = 0;    // bit per Sound already played by playAt this frame
    Vec3 sway, lastEye;
    float lastYaw = 0;
    int meleeTarget = -1;
    bool meleeCrit = false;

    std::vector<MonsterView> monsters;
    std::vector<std::pair<int, float>> doors;    // cell index, open amount
    std::vector<std::pair<int, float>> lids;     // chest cell index, open amount
    std::vector<std::pair<int, float>> springs;  // spike trap cell index, seconds since it fired
    std::vector<Bolt> bolts;
    std::vector<Particle> particles;
    std::vector<Beam> beams;
    std::vector<Flare> flares;
    std::vector<Floater> floaters;
    std::vector<Pending> pending;  // events held back until the swing lands
    std::vector<Action> queued;    // keypresses waiting for the current move to finish
    std::vector<int> held;
    float holdT = 0;
    HudState hud;
    int invSelected = 0;
    int titleMenu = 0;
    Screen helpReturn = Screen::Play;
    bool resting = false;
    int restTurns = 0;
    float scriptT = 0;
    size_t scriptPos = 0;
};
