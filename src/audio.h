#pragma once
// Synthesised sound through a small stereo waveOut mixer with an SPU-style reverb.
// Everything is rendered once at startup as 22 kHz "samples"; the mixer only resamples and sums.
#include <cstdint>
#include <vector>

enum class Sound : uint8_t {
    Step, Bump, Swing, Hit, MonsterHurt, MonsterDie, PlayerHurt, Door, Pickup, Gold, Drink, Scroll,
    Stairs, LevelUp, Death, Notice, Bolt, Victory, MenuMove, MenuSelect,
    Trap, Chest, Fountain, Zap, Summon, Secret, Fire, Fall,
    Teleport,   // shimmering swell and pop
    TrapFound,  // mechanism click and an ominous low pluck
    Growl,      // short snarl as a monster attacks; pass the monster's pitch
    Equip,      // weapon or armour buckled on
    Alarm,      // a clanging handbell: the alarm trap
    Shuffle,    // a monster's footstep; pass the monster's pitch (rats skitter, ogres thump)
    Count
};

// What a creature is doing. Every monster kind has its own voice for each.
enum class Cue : uint8_t { Notice, Attack, Impact, Hurt, Die, Step, Count };
constexpr int CREATURE_KINDS = 14;  // MonsterKind order: rat, bat, skeleton, ooze, ghoul, spider, cultist,
                                    // wraith, knight, ogre, imp, gargoyle, mimic, Lich

namespace audio {
bool start();
void stop();
// pan -1 left .. 1 right; pitch 1 = as synthesised.
void play(Sound s, float gain = 1, float pan = 0, float pitch = 1);
void playCreature(int kind, Cue cue, float gain = 1, float pan = 0);
// The ambience bed and music: 0 crypt, 1 catacomb, 2 ember, 3 the Lich's floor, 4 title, -1 silence.
void setAmbience(int mood);

// Offline use (tests, tools): build everything without opening a device, then pull the master mix.
bool startOffline();
void renderMix(float* stereo, int frames);  // interleaved L/R at 44.1 kHz, after reverb and limiter
int sourceRate();
int variants(Sound s);
std::vector<float> renderSound(Sound s, int variant = 0);
std::vector<float> renderCreature(int kind, Cue cue, int variant = 0);
// The music loop, or for ambience its 41 s events loop with the 24 s texture loop under it.
std::vector<float> renderLoop(int theme, bool music);
}
