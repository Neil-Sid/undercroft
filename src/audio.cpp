// The mixer: voices resampled from the 22 kHz bank, an SPU-style hall reverb, a look-ahead
// limiter, and the music sequencer. waveOut on the desktop, Web Audio in the browser.
// Recorded sounds listed in sounds/sounds.txt replace their synthesised versions at startup.
#include "synth.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <windows.h>
#include <mmsystem.h>
#include <thread>
#endif
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

using namespace synth;

namespace {

constexpr int MAX_VOICES = 24, MAX_NOTES = 28;
[[maybe_unused]] constexpr int BLOCK = 512, BLOCKS = 6;  // waveOut buffering (desktop)
constexpr float MUSIC_GAIN = 0.42f;

struct Voice {
    const Buf* pcm;
    double pos = 0, step = 0.5;
    float left = 0, right = 0, send = 0;
    float fade = 1, fadeStep = 0;  // a ramp for fading in and out, and for stolen voices
    int delay = 0;                 // frames before it starts
    bool loop = false;             // ambience: the whole buffer loops
    bool music = false;
    double loopStart = 0, loopEnd = 0;  // a held note loops this stretch until stopAt
    uint64_t stopAt = UINT64_MAX;
    float release = 0.3f;
    int key = -1;  // which sound, for staggering identical starts
    uint64_t born = 0;
    bool done() const { return (fadeStep < 0 && fade <= 0) || (!loop && pos >= double(pcm->size())); }
};

// 4-point Hermite read: smooth like the SPU's gaussian interpolation, cheap enough per voice.
float tap(const Buf& b, double pos, bool loop) {
    ptrdiff_t n = ptrdiff_t(b.size()), i = ptrdiff_t(pos);
    float y[4];
    if (i >= 1 && i + 2 < n) {
        for (int k = 0; k < 4; k++) y[k] = b[size_t(i - 1 + k)];
    } else {
        for (int k = 0; k < 4; k++) {
            ptrdiff_t j = i - 1 + k;
            y[k] = loop ? b[size_t((j % n + n) % n)] : j >= 0 && j < n ? b[size_t(j)] : 0;
        }
    }
    float f = float(pos - double(i));
    float c1 = 0.5f * (y[2] - y[0]), c2 = y[0] - 2.5f * y[1] + 2 * y[2] - 0.5f * y[3], c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
    return ((c3 * f + c2) * f + c1) * f + y[1];
}

// Freeverb-style tank: damped combs in parallel into allpass diffusers.
struct Tank {
    struct Line {
        Buf buf;
        size_t i = 0;
        float store = 0;
    };
    Line combs[8], diffusers[4];
    explicit Tank(int spread) {
        const int comb[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}, ap[4] = {556, 441, 341, 225};
        for (int k = 0; k < 8; k++) combs[k].buf.assign(size_t(comb[k] + spread), 0);
        for (int k = 0; k < 4; k++) diffusers[k].buf.assign(size_t(ap[k] + spread), 0);
    }
    float run(float x, float feedback, float damp) {
        float out = 0;
        for (Line& c : combs) {
            float y = c.buf[c.i];
            c.store = y + (c.store - y) * damp;
            c.buf[c.i] = x + c.store * feedback;
            if (++c.i == c.buf.size()) c.i = 0;
            out += y;
        }
        for (Line& a : diffusers) {
            float y = a.buf[a.i];
            a.buf[a.i] = out + y * 0.5f;
            if (++a.i == a.buf.size()) a.i = 0;
            out = y - out;
        }
        return out;
    }
};

// The SPU-style hall: a pre-delay, a dark input filter and a stereo pair of tanks. Each area has its own room.
struct Reverb {
    struct Room {
        float feedback, damp;
    };
    static constexpr Room rooms[4] = {{0.9f, 0.3f}, {0.84f, 0.5f}, {0.87f, 0.6f}, {0.86f, 0.4f}};
    Tank left{0}, right{23};
    Buf pre = Buf(size_t(RATE * 0.025f));
    size_t pi = 0;
    OnePole tone;
    Room room = rooms[3];
    void operator()(float x, float& l, float& r) {
        float in = pre[pi];
        pre[pi] = tone.lp(x, 0.45f);
        if (++pi == pre.size()) pi = 0;
        in *= 0.02f;
        l = left.run(in, room.feedback, room.damp), r = right.run(in, room.feedback, room.damp);
    }
};

// Look-ahead peak limiter: the gain is the smoothed minimum of what the next LOOK samples need,
// so it is fully down before a peak leaves the delay line, then recovers slowly.
struct Limiter {
    static constexpr int LOOK = 48;
    static constexpr float CEILING = 0.89f;
    float wants[LOOK], mins[LOOK], delayL[LOOK - 1] = {}, delayR[LOOK - 1] = {}, gain = 1;
    int i = 0, d = 0;
    Limiter() { std::fill(wants, wants + LOOK, 1.0f), std::fill(mins, mins + LOOK, 1.0f); }
    void operator()(float& l, float& r) {
        float peak = std::max(std::abs(l), std::abs(r));
        wants[i] = peak > CEILING ? CEILING / peak : 1;
        mins[i] = *std::min_element(wants, wants + LOOK);
        i = (i + 1) % LOOK;
        float target = 0;
        for (float m : mins) target += m;
        target /= LOOK;
        gain = target < gain ? target : gain + (target - gain) * 0.0002f;
        std::swap(l, delayL[d]), std::swap(r, delayR[d]);
        d = (d + 1) % (LOOK - 1);
        l = soft(l * gain), r = soft(r * gain);
    }
    static float soft(float x) {
        float a = std::abs(x);
        return a < 0.9f ? x : std::copysign(0.9f + 0.08f * std::tanh((a - 0.9f) / 0.08f), x);
    }
};

std::vector<Buf> bank[int(Sound::Count)];
Spec specs[int(Sound::Count)];
std::vector<Buf> voicesOf[CREATURE_KINDS][int(Cue::Count)];
int lastVariant[int(Sound::Count) + CREATURE_KINDS * int(Cue::Count)];
Ambience beds[3];
Buf floorMusic;  // a recorded track for exploring; empty to use the composed songs
int stepFirst[3] = {0, STEP_VARIANTS, 2 * STEP_VARIANTS}, stepCount[3] = {STEP_VARIANTS, STEP_VARIANTS, STEP_VARIANTS};
std::vector<Voice> voices;
Reverb reverb;
Limiter limiter;
OnePole dcL, dcR;
Noise rng(99);
Sequencer sequencer;
int songWait = -1;  // frames until the next song starts, or -1
int songNext = -1;
uint64_t frame = 0;
std::mutex lock;
std::atomic<bool> ready{false}, running{false}, offline{false};
int mood = -1;

int bedOf(int m) { return m == 1 ? 1 : m == 2 || m == 3 ? 2 : 0; }
bool floorSong(int m) { return m >= 0 && m <= 2 && !floorMusic.empty(); }
int creatureKey(int kind, Cue cue) { return int(Sound::Count) + kind * int(Cue::Count) + int(cue); }

#ifndef __EMSCRIPTEN__
HWAVEOUT device;
HANDLE blockDone;
WAVEHDR headers[BLOCKS];
int16_t buffers[BLOCKS][BLOCK * 2];
std::thread pump;
#endif

// Synthesis jobs run on their own threads on the desktop; the web build has none to spare.
void runAll(std::vector<std::function<void()>>& jobs) {
#ifdef __EMSCRIPTEN__
    for (auto& j : jobs) j();
#else
    std::vector<std::thread> threads;
    for (auto& j : jobs) threads.emplace_back(j);
    for (std::thread& t : threads) t.join();
#endif
}

// Level a sample, trim its silent tail and fade both ends so voices never click. Anything that
// would feel louder than its peak suggests (squeals, shrieks, wails) is turned down to match.
Buf finish(Buf b, float peak) {
    b = normalized(std::move(b), peak);
    if (float feel = loudness(b), cap = peak * 0.4f; feel > cap) b = scaled(std::move(b), cap / feel);
    size_t end = b.size();
    while (end > 0 && std::abs(b[end - 1]) < 0.0005f) end--;
    b.resize(std::max<size_t>(end, 64));
    size_t in = SR / 1000, out = SR * 6 / 1000;
    for (size_t i = 0; i < in; i++) b[i] *= float(i) / float(in);
    for (size_t i = 0; i < out; i++) b[b.size() - 1 - i] *= float(i) / float(out);
    return b;
}

// ---------- recorded sounds ----------

const char* const SLOTS[int(Sound::Count)] = {"step", "bump", "swing", "hit", "monster_hurt", "monster_die", "player_hurt", "door", "pickup", "gold",
                                              "drink", "scroll", "stairs", "level_up", "death", "notice", "bolt", "victory", "menu_move", "menu_select",
                                              "trap", "chest", "fountain", "zap", "summon", "secret", "fire", "fall", "teleport", "trap_found",
                                              "growl", "equip", "alarm", "shuffle"};
const char* const KINDS[CREATURE_KINDS] = {"rat", "bat", "skeleton", "ooze", "ghoul", "spider", "cultist", "wraith", "knight", "ogre", "imp", "gargoyle", "mimic", "lich"};
const char* const CUES[int(Cue::Count)] = {"notice", "attack", "impact", "hurt", "die", "step"};
const char* const FLOORS[3] = {"crypt", "catacomb", "ember"};

std::string soundDir() {
#ifdef __EMSCRIPTEN__
    return "/sounds/";
#else
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string path = exe;
    return path.substr(0, path.find_last_of("\\/") + 1) + "sounds\\";
#endif
}

// Plays a sample faster (higher) or slower (lower) by resampling.
Buf pitched(const Buf& b, double pitch) {
    if (pitch == 1 || b.empty()) return b;
    Buf out(size_t(double(b.size()) / pitch));
    for (size_t i = 0; i < out.size(); i++) {
        double p = double(i) * pitch;
        size_t k = size_t(p);
        float f = float(p - double(k));
        out[i] = b[k] * (1 - f) + (k + 1 < b.size() ? b[k + 1] : 0) * f;
    }
    return out;
}

// A 16-bit PCM WAV as 22 kHz mono, or nothing if it can't be read.
Buf readWav(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    std::vector<uint8_t> d(file ? size_t(file.tellg()) : 0);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(d.data()), std::streamsize(d.size()));
    if (d.size() < 12 || std::memcmp(&d[0], "RIFF", 4) || std::memcmp(&d[8], "WAVE", 4)) return {};
    auto u16 = [&](size_t i) { return uint16_t(d[i] | d[i + 1] << 8); };
    auto u32 = [&](size_t i) { return uint32_t(u16(i)) | uint32_t(u16(i + 2)) << 16; };
    int channels = 0, rate = SR, bits = 0;
    Buf out;
    for (size_t i = 12; i + 8 <= d.size();) {
        size_t size = u32(i + 4), body = i + 8;
        if (!std::memcmp(&d[i], "fmt ", 4) && size >= 16) channels = u16(body + 2), rate = int(u32(body + 4)), bits = u16(body + 14);
        if (!std::memcmp(&d[i], "data", 4) && bits == 16 && channels > 0) {
            out.resize(std::min(size, d.size() - body) / (2 * size_t(channels)));
            for (size_t n = 0; n < out.size(); n++) {
                float sum = 0;
                for (int c = 0; c < channels; c++) sum += float(int16_t(u16(body + (n * size_t(channels) + size_t(c)) * 2))) / 32768;
                out[n] = sum / float(channels);
            }
        }
        i = body + size + (size & 1);
    }
    return pitched(out, double(rate) / SR);
}

// Drops the near-silence a recording starts with, keeping a few milliseconds before the onset.
Buf trimmed(Buf b) {
    size_t onset = 0;
    float floor = 0.01f * peakOf(b);
    while (onset < b.size() && std::abs(b[onset]) < floor) onset++;
    b.erase(b.begin(), b.begin() + std::ptrdiff_t(onset > SR / 200 ? onset - SR / 200 : 0));
    return b;
}

// A track made seamless: its last two seconds are crossfaded over its first.
Buf looped(Buf b) {
    size_t fade = std::min(b.size() / 4, size_t(2 * SR)), n = b.size() - fade;
    for (size_t i = 0; i < fade; i++) {
        float u = PI / 2 * float(i) / float(fade);
        b[i] = b[i] * std::sin(u) + b[n + i] * std::cos(u);
    }
    b.resize(n);
    return b;
}

// Reads sounds/sounds.txt and swaps each listed slot's synthesised sound for the recordings.
void loadRecordings() {
    std::string dir = soundDir();
    std::ifstream manifest(dir + "sounds.txt");
    std::vector<Buf> steps[3];
    for (int t = 0; t < 3; t++) steps[t].assign(bank[0].begin() + stepFirst[t], bank[0].begin() + stepFirst[t] + stepCount[t]);
    std::string line, slot, token;
    while (std::getline(manifest, line)) {
        std::istringstream words(line.substr(0, line.find('#')));
        if (!(words >> slot)) continue;
        std::vector<Buf> takes;
        while (words >> token) {
            size_t at = token.find('@');
            Buf b = readWav(dir + token.substr(0, at) + ".wav");
            if (!b.empty()) takes.push_back(pitched(trimmed(std::move(b)), at == std::string::npos ? 1 : std::stod(token.substr(at + 1))));
        }
        if (takes.empty()) continue;
        auto leveled = [&](float peak) {
            for (Buf& b : takes) b = finish(std::move(b), peak);
            return takes;
        };
        size_t dot = slot.find('.');
        std::string head = slot.substr(0, dot), tail = dot == std::string::npos ? "" : slot.substr(dot + 1);
        if (slot == "music.floors") floorMusic = scaled(looped(takes[0]), 0.11f / std::max(rmsOf(takes[0]), 1e-6f));
        for (int t = 0; t < 3; t++)
            if (head == "step" && tail == FLOORS[t]) steps[t] = leveled(specs[0].peak);
        for (int s = 1; s < int(Sound::Count); s++)
            if (slot == SLOTS[s]) bank[s] = leveled(specs[s].peak);
        for (int k = 0; k < CREATURE_KINDS; k++)
            for (int c = 0; c < int(Cue::Count); c++)
                if (head == KINDS[k] && tail == CUES[c]) voicesOf[k][c] = leveled(creatureSpec(Cue(c)).peak);
    }
    bank[0].clear();
    for (int t = 0; t < 3; t++) {
        stepFirst[t] = int(bank[0].size()), stepCount[t] = int(steps[t].size());
        bank[0].insert(bank[0].end(), steps[t].begin(), steps[t].end());
    }
}

void build() {
    if (ready) return;
    std::vector<std::function<void()>> jobs = instrumentJobs();  // the stingers play the instruments
    runAll(jobs);
    jobs.clear();
    for (int s = 0; s < int(Sound::Count); s++) {
        specs[s] = specOf(Sound(s));
        bank[s].resize(size_t(specs[s].variants));
        for (int v = 0; v < specs[s].variants; v++) jobs.emplace_back([s, v] { bank[s][size_t(v)] = finish(design(Sound(s), uint32_t(v)), specs[s].peak); });
    }
    for (int k = 0; k < CREATURE_KINDS; k++)
        jobs.emplace_back([k] {
            for (int c = 0; c < int(Cue::Count); c++) {
                Spec spec = creatureSpec(Cue(c));
                for (int v = 0; v < spec.variants; v++) voicesOf[k][c].push_back(finish(creature(k, Cue(c), uint32_t(v)), spec.peak));
            }
        });
    for (int t = 0; t < 3; t++) jobs.emplace_back([t] { beds[t] = ambience(t); });
    jobs.emplace_back([] { song(0); });  // compose the songs before the mixer needs them
    runAll(jobs);
    for (Ambience& a : beds) {  // texture to a fixed loudness; the events keep their level relative to it
        float k = 0.1f / rmsOf(a.texture);
        a.texture = scaled(std::move(a.texture), k), a.events = scaled(std::move(a.events), k);
    }
    loadRecordings();
    ready = true;
}

// Picks a variation from v[first, first + count), never the same one twice running.
const Buf& pickVariant(const std::vector<Buf>& v, int key, int first = 0, int count = -1) {
    int n = count < 0 ? int(v.size()) : count, k = n > 1 ? (lastVariant[key] + 1 + int(rng.uniform() * float(n - 1))) % n : 0;
    lastVariant[key] = k;
    return v[size_t(first + k)];
}

// Starts a one-shot voice. Call with the lock held.
void startVoice(const Buf& pcm, int key, float gain, float pan, float pitch, const Spec& spec) {
    Voice v{&pcm};
    v.key = key, v.send = spec.send;
    v.step = double(pitch * (1 + spec.jitter * rng.next())) * SR / RATE;
    // The same sound starting this instant (a blast hitting a crowd) is staggered and softened.
    for (const Voice& o : voices)
        if (!o.loop && !o.music && o.key == key && o.born + RATE / 25 > frame) {
            v.delay = int(rng.range(0.012f, 0.04f) * RATE), gain *= 0.7f;
            break;
        }
    v.born = frame + uint64_t(v.delay);
    pan = std::clamp(pan, -1.0f, 1.0f);
    v.left = gain * std::sqrt(0.5f * (1 - pan)), v.right = gain * std::sqrt(0.5f * (1 + pan));
    int live = 0;
    Voice* oldest = nullptr;
    for (Voice& o : voices)
        if (!o.loop && !o.music && o.fadeStep >= 0) {
            live++;
            if (!oldest || o.born < oldest->born) oldest = &o;
        }
    if (live >= MAX_VOICES && oldest) oldest->fadeStep = -1 / (0.008f * RATE);
    voices.push_back(v);
}

// A note from the sequencer, `offset` frames into this block, held for `length` frames.
void startNote(const Part& part, const Note& note, int offset, int length) {
    const Instrument& ins = instrument(part.instrument);
    Voice v{&ins.sample};
    v.music = true, v.send = ins.send, v.delay = offset, v.release = ins.release;
    v.step = std::pow(2.0, (double(note.midi) - ins.baseMidi) / 12) * SR / RATE;
    v.loopStart = double(ins.loopStart), v.loopEnd = double(ins.loopEnd);
    if (ins.loopEnd > 0) v.stopAt = frame + uint64_t(offset + length);
    float gain = part.gain * MUSIC_GAIN * (0.9f + 0.2f * rng.uniform()), pan = std::clamp(part.pan + 0.1f * rng.next(), -1.0f, 1.0f);
    v.left = gain * std::sqrt(0.5f * (1 - pan)), v.right = gain * std::sqrt(0.5f * (1 + pan));
    v.born = frame + uint64_t(offset);
    int live = 0;
    Voice* oldest = nullptr;
    for (Voice& o : voices)
        if (o.music && o.fadeStep >= 0) {
            live++;
            if (!oldest || o.born < oldest->born) oldest = &o;
        }
    if (live >= MAX_NOTES && oldest) oldest->fadeStep = -1 / (0.05f * RATE);
    voices.push_back(v);
}

void startMusicLoop();

void mixBlock(float* out, int frames) {
    std::lock_guard guard(lock);
    if (songWait >= 0 && (songWait -= frames) < 0 && songNext >= 0) {
        if (floorSong(songNext)) startMusicLoop();
        else sequencer.start(&song(songNext), rng);
    }
    sequencer.advance(frames, rng, startNote);
    for (int n = 0; n < frames; n++) {
        float l = 0, r = 0, wet = 0;
        uint64_t now = frame + uint64_t(n);
        for (Voice& v : voices) {
            if (v.delay > 0) {
                v.delay--;
                continue;
            }
            if (now >= v.stopAt && v.fadeStep >= 0) v.fadeStep = -1 / (v.release * RATE), v.loopEnd = 0;  // let go of the note
            if (v.loopEnd > 0 && v.pos >= v.loopEnd) v.pos -= v.loopEnd - v.loopStart;
            if (v.done()) continue;
            float s = tap(*v.pcm, v.pos, v.loop) * v.fade;
            v.fade = std::clamp(v.fade + v.fadeStep, 0.0f, 1.0f);
            v.pos += v.step;
            if (v.loop && v.pos >= double(v.pcm->size())) v.pos -= double(v.pcm->size());
            l += s * v.left, r += s * v.right, wet += s * (v.left + v.right) * v.send;
        }
        float wl, wr;
        reverb(wet, wl, wr);
        l += wl, r += wr;
        l = dcL.hp(l, 0.0015f), r = dcR.hp(r, 0.0015f);
        limiter(l, r);
        out[n * 2] = l, out[n * 2 + 1] = r;
    }
    frame += uint64_t(frames);
    std::erase_if(voices, [](const Voice& v) { return v.done(); });
}

#ifndef __EMSCRIPTEN__
void pumpLoop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    float block[BLOCK * 2];
    while (running) {
        WaitForSingleObject(blockDone, 50);
        for (WAVEHDR& h : headers)
            if (h.dwFlags & WHDR_DONE) {
                mixBlock(block, BLOCK);
                auto* out = reinterpret_cast<int16_t*>(h.lpData);
                for (int i = 0; i < BLOCK * 2; i++) out[i] = int16_t(std::lround(std::clamp(block[i], -1.0f, 1.0f) * 32767));
                waveOutWrite(device, &h, sizeof h);
            }
    }
}
#endif

void addLoop(const Buf& pcm, float gain, float send, bool music = false) {
    Voice v{&pcm};
    v.loop = true, v.music = music, v.left = v.right = gain, v.send = send;
    v.fade = 0, v.fadeStep = 1 / (2.5f * RATE);
    v.pos = music ? 0 : double(rng.uniform()) * double(pcm.size());
    voices.push_back(v);
}

void startMusicLoop() { addLoop(floorMusic, MUSIC_GAIN, 0.35f, true); }

}  // namespace

#ifdef __EMSCRIPTEN__
// Web Audio pulls blocks of the same master mix the desktop pump thread feeds to waveOut.
extern "C" EMSCRIPTEN_KEEPALIVE void audio_pull(float* stereo, int frames) {
    if (running) mixBlock(stereo, frames);
    else std::fill(stereo, stereo + frames * 2, 0.0f);
}

// Browsers keep the context suspended until a key or click; index.html resumes Module.audioContext.
EM_JS(void, openWebAudio, (int rate), {
    const ctx = new (window.AudioContext || window.webkitAudioContext)({sampleRate: rate});
    const frames = 2048, buf = _malloc(frames * 8);
    const node = ctx.createScriptProcessor(frames, 0, 2);
    node.onaudioprocess = (e) => {
        _audio_pull(buf, frames);
        const l = e.outputBuffer.getChannelData(0), r = e.outputBuffer.getChannelData(1), base = buf >> 2;
        for (let i = 0; i < frames; i++) {
            l[i] = HEAPF32[base + i * 2];
            r[i] = HEAPF32[base + i * 2 + 1];
        }
    };
    node.connect(ctx.destination);
    Module.audioContext = ctx;
});
#endif

namespace audio {

#ifdef __EMSCRIPTEN__
bool start() {
    build();
    running = true;
    openWebAudio(RATE);
    return true;
}

void stop() { running = false; }
#else
bool start() {
    build();
    WAVEFORMATEX fmt{WAVE_FORMAT_PCM, 2, RATE, RATE * 4, 4, 16, 0};
    blockDone = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (waveOutOpen(&device, WAVE_MAPPER, &fmt, DWORD_PTR(blockDone), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return false;
    running = true;
    for (int i = 0; i < BLOCKS; i++) {
        headers[i] = {};
        headers[i].lpData = reinterpret_cast<LPSTR>(buffers[i]);
        headers[i].dwBufferLength = sizeof buffers[i];
        waveOutPrepareHeader(device, &headers[i], sizeof headers[i]);
        waveOutWrite(device, &headers[i], sizeof headers[i]);
    }
    pump = std::thread(pumpLoop);
    return true;
}

void stop() {
    if (!running) return;
    running = false;
    pump.join();
    waveOutReset(device);
    for (WAVEHDR& h : headers) waveOutUnprepareHeader(device, &h, sizeof h);
    waveOutClose(device);
    CloseHandle(blockDone);
}
#endif

void play(Sound s, float gain, float pan, float pitch) {
    if (!ready || !(running || offline) || s >= Sound::Count) return;
    std::lock_guard guard(lock);
    bool step = s == Sound::Step;  // footsteps sound like the floor you're on
    const Buf& pcm = step ? pickVariant(bank[int(s)], int(s), stepFirst[bedOf(mood)], stepCount[bedOf(mood)]) : pickVariant(bank[int(s)], int(s));
    startVoice(pcm, int(s), gain, pan, pitch, specs[int(s)]);
}

void playCreature(int kind, Cue cue, float gain, float pan) {
    if (!ready || !(running || offline) || kind < 0 || kind >= CREATURE_KINDS || cue >= Cue::Count) return;
    std::lock_guard guard(lock);
    int key = creatureKey(kind, cue);
    startVoice(pickVariant(voicesOf[kind][int(cue)], key), key, gain, pan, 1, creatureSpec(cue));
}

void setAmbience(int m) {
    if (!ready || !(running || offline) || m == mood) return;
    std::lock_guard guard(lock);
    bool sameBed = m >= 0 && mood >= 0 && bedOf(m) == bedOf(mood), sameSong = floorSong(m) && floorSong(mood);
    for (Voice& v : voices)
        if ((v.loop && !v.music && !sameBed) || (v.music && !sameSong)) v.fadeStep = -1 / (1.5f * RATE);
    mood = m;
    reverb.room = Reverb::rooms[m >= 0 ? bedOf(m) : 3];
    if (!sameSong) {
        sequencer.start(nullptr, rng);
        songNext = m, songWait = m >= 0 ? RATE * 3 / 2 : -1;  // a breath of silence between pieces
    }
    if (m < 0 || sameBed) return;
    addLoop(beds[bedOf(m)].texture, 0.24f, 0.3f);
    addLoop(beds[bedOf(m)].events, 0.27f, 0.45f);
}

bool startOffline() {
    build();
    return offline = true;
}

void renderMix(float* stereo, int frames) { mixBlock(stereo, frames); }
int sourceRate() { return SR; }
int variants(Sound s) { return s < Sound::Count ? int(bank[int(s)].size()) : 0; }

std::vector<float> renderSound(Sound s, int variant) {
    if (s >= Sound::Count) return {};
    build();
    const auto& v = bank[int(s)];
    return v.empty() ? Buf{} : v[size_t(std::clamp(variant, 0, int(v.size()) - 1))];
}

std::vector<float> renderCreature(int kind, Cue cue, int variant) {
    build();
    const auto& v = voicesOf[std::clamp(kind, 0, CREATURE_KINDS - 1)][int(cue)];
    return v[size_t(std::clamp(variant, 0, int(v.size()) - 1))];
}

std::vector<float> renderLoop(int theme, bool music) {
    build();
    if (music) return bounce(song(theme));
    const Ambience& a = beds[std::clamp(theme, 0, 2)];
    Buf out = a.events;
    for (size_t i = 0; i < out.size(); i++) out[i] += a.texture[i % a.texture.size()];
    return out;
}

}  // namespace audio
