// Renders every sound, ambience and music bed to WAV in out/audio/ and prints level stats.
//   g++ -std=c++20 -O2 -o out/audio/audio_test.exe src/audio_test.cpp src/audio.cpp src/audio_sfx.cpp src/audio_music.cpp -static -lwinmm
#include "audio.h"
#include <algorithm>
#include <chrono>
#include <complex>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

const char* NAMES[] = {"step", "bump", "swing", "hit", "monster_hurt", "monster_die", "player_hurt", "door", "pickup", "gold", "drink",
                       "scroll", "stairs", "level_up", "death", "notice", "bolt", "victory", "menu_move", "menu_select", "trap", "chest",
                       "fountain", "zap", "summon", "secret", "fire", "fall", "teleport", "trap_found", "growl", "equip", "alarm", "shuffle"};
static_assert(sizeof NAMES / sizeof *NAMES == size_t(Sound::Count));

void writeWav(const std::string& path, const std::vector<float>& pcm, int rate, int channels) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    uint32_t data = uint32_t(pcm.size() * 2), riff = 36 + data, fmtLen = 16, byteRate = uint32_t(rate * channels * 2);
    uint16_t pcmTag = 1, ch = uint16_t(channels), align = uint16_t(channels * 2), bits = 16;
    std::fwrite("RIFF", 1, 4, f), std::fwrite(&riff, 4, 1, f), std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmtLen, 4, 1, f), std::fwrite(&pcmTag, 2, 1, f), std::fwrite(&ch, 2, 1, f);
    std::fwrite(&rate, 4, 1, f), std::fwrite(&byteRate, 4, 1, f), std::fwrite(&align, 2, 1, f), std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f), std::fwrite(&data, 4, 1, f);
    for (float x : pcm) {
        int16_t s = int16_t(std::lround(std::fmax(-1.0f, std::fmin(1.0f, x)) * 32767));
        std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
}

// ---------- PNG spectrograms (uncompressed deflate, no libraries) ----------

uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c;
}

void writePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + y * w * 3, rgb.begin() + (y + 1) * w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t x : raw) a = (a + x) % 65521, b = (b + a) % 65521;
    for (size_t at = 0; at < raw.size(); at += 65535) {
        uint16_t len = uint16_t(std::min<size_t>(65535, raw.size() - at));
        z.push_back(at + len >= raw.size());
        z.insert(z.end(), {uint8_t(len), uint8_t(len >> 8), uint8_t(~len), uint8_t(~len >> 8)});
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + len);
    }
    uint32_t adler = b << 16 | a;
    z.insert(z.end(), {uint8_t(adler >> 24), uint8_t(adler >> 16), uint8_t(adler >> 8), uint8_t(adler)});
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    auto be = [](uint32_t v) { return std::vector<uint8_t>{uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        std::fwrite(be(uint32_t(data.size())).data(), 1, 4, f), std::fwrite(body.data(), 1, body.size(), f);
        std::fwrite(be(~crc32(body.data(), body.size())).data(), 1, 4, f);
    };
    std::fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    std::vector<uint8_t> ihdr = be(uint32_t(w));
    for (uint8_t x : be(uint32_t(h))) ihdr.push_back(x);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk("IHDR", ihdr), chunk("IDAT", z), chunk("IEND", {});
    std::fclose(f);
}

void fft(std::vector<std::complex<float>>& x) {
    size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        std::complex<float> w = std::polar(1.0f, -6.2831853f / float(len));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> v = 1;
            for (size_t k = 0; k < len / 2; k++, v *= w) {
                auto a = x[i + k], b = x[i + k + len / 2] * v;
                x[i + k] = a + b, x[i + k + len / 2] = a - b;
            }
        }
    }
}

// Waveform on top (absolute scale, gridlines at 0.25), log-frequency spectrogram below (30 Hz - 11 kHz, 0 to -80 dBFS).
void spectrogram(const std::string& path, const std::vector<float>& pcm, int rate, float seconds = 0) {
    const int N = 1024, H = 240, WAVE = 90;
    int hop = std::max(32, int(pcm.size() / 900)), w = int(pcm.size()) / hop;
    if (seconds > 0) w = std::min(w, int(seconds * float(rate)) / hop);
    std::vector<uint8_t> img(size_t(w) * (H + WAVE) * 3, 0);
    auto px = [&](int x, int y, float r, float g, float b) {
        uint8_t* p = &img[(size_t(y) * w + x) * 3];
        p[0] = uint8_t(std::fmin(255.0f, r * 255)), p[1] = uint8_t(std::fmin(255.0f, g * 255)), p[2] = uint8_t(std::fmin(255.0f, b * 255));
    };
    std::vector<std::complex<float>> buf(N);
    for (int x = 0; x < w; x++) {
        float lo = 0, hi = 0;
        for (int i = x * hop; i < (x + 1) * hop && i < int(pcm.size()); i++) lo = std::fmin(lo, pcm[size_t(i)]), hi = std::fmax(hi, pcm[size_t(i)]);
        for (int y = 0; y < WAVE; y++) {
            float v = 1 - 2 * float(y) / (WAVE - 1);
            bool on = v <= hi && v >= lo, grid = std::fabs(std::fmod(std::fabs(v) + 0.0001f, 0.25f)) < 0.012f;
            px(x, y, on ? 0.9f : grid ? 0.15f : 0.05f, on ? 0.85f : grid ? 0.15f : 0.05f, on ? 0.6f : grid ? 0.18f : 0.07f);
        }
        for (int i = 0; i < N; i++) {
            int j = x * hop + hop / 2 - N / 2 + i;
            float win = 0.5f - 0.5f * std::cos(6.2831853f * float(i) / N);
            buf[size_t(i)] = j >= 0 && j < int(pcm.size()) ? pcm[size_t(j)] * win : 0;
        }
        fft(buf);
        for (int y = 0; y < H; y++) {
            float f = 30 * std::pow(11000.0f / 30, 1 - float(y) / (H - 1)), bin = f * N / float(rate);
            int b0 = int(bin);
            float mag = std::abs(buf[size_t(std::min(b0, N / 2))]) / (N / 4);
            float level = std::fmax(0.0f, std::fmin(1.0f, (20 * std::log10(mag + 1e-9f) + 80) / 80));
            px(x, WAVE + y, level * 1.6f, level * level * 1.2f, 0.25f * level + 0.6f * level * level * level);
        }
    }
    writePng(path, w, H + WAVE, img);
}

struct Stats {
    float peak = 0, rms = 0, edge = 0;
};

// edge: the largest |sample| at either end, which would click if the voice started or stopped there.
Stats stats(const std::vector<float>& x) {
    Stats s;
    double sum = 0;
    for (float v : x) s.peak = std::fmax(s.peak, std::fabs(v)), sum += double(v) * v;
    s.rms = x.empty() ? 0 : float(std::sqrt(sum / double(x.size())));
    if (!x.empty()) s.edge = std::fmax(std::fabs(x.front()), std::fabs(x.back()));
    return s;
}

float db(float x) { return 20 * std::log10(std::fmax(x, 1e-9f)); }

void report(const char* label, const std::vector<float>& x, int rate, int channels) {
    Stats s = stats(x);
    std::printf("%-22s %6.2fs  peak %5.3f (%6.1f dB)  rms %5.3f (%6.1f dB)  edge %.4f\n", label, double(x.size()) / channels / rate, s.peak,
                db(s.peak), s.rms, db(s.rms), s.edge);
}

// Pulls the live mix for `seconds`, calling `cue(t)` each 10 ms block so it can trigger sounds.
template <class F>
std::vector<float> mix(float seconds, F cue) {
    std::vector<float> out;
    std::vector<float> block(441 * 2);
    for (int i = 0; i < int(seconds * 100); i++) {
        cue(float(i) / 100);
        audio::renderMix(block.data(), 441);
        out.insert(out.end(), block.begin(), block.end());
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--device") {  // the real waveOut path: open, mix, close
        auto t0 = std::chrono::steady_clock::now();
        bool ok = audio::start();
        std::printf("device start: %s in %.0f ms\n", ok ? "ok" : "FAILED",
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        audio::setAmbience(0);
        audio::play(Sound::MenuMove, 0.05f);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        audio::setAmbience(-1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        audio::stop();
        std::printf("device stop: ok\n");
        return ok ? 0 : 1;
    }
    auto t0 = std::chrono::steady_clock::now();
    audio::startOffline();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("build: %.0f ms\n\n", ms);

    int rate = audio::sourceRate();
    for (int s = 0; s < int(Sound::Count); s++)
        for (int v = 0; v < audio::variants(Sound(s)); v++) {
            std::string name = std::string(NAMES[s]) + (audio::variants(Sound(s)) > 1 ? "_" + std::to_string(v) : "");
            std::vector<float> pcm = audio::renderSound(Sound(s), v);
            writeWav("out/audio/sfx_" + name + ".wav", pcm, rate, 1);
            spectrogram("out/audio/png/sfx_" + name + ".png", pcm, rate);
            report(name.c_str(), pcm, rate, 1);
        }

    std::printf("\n");
    const char* kinds[] = {"rat", "bat", "skeleton", "ooze", "ghoul", "spider", "cultist", "wraith", "knight", "ogre", "imp", "gargoyle", "mimic", "lich"};
    const char* cues[] = {"notice", "attack", "impact", "hurt", "die", "step"};
    static_assert(sizeof kinds / sizeof *kinds == CREATURE_KINDS && sizeof cues / sizeof *cues == size_t(Cue::Count));
    for (int k = 0; k < CREATURE_KINDS; k++)
        for (int c = 0; c < int(Cue::Count); c++) {
            std::string name = std::string(kinds[k]) + "_" + cues[c];
            std::vector<float> pcm = audio::renderCreature(k, Cue(c));
            writeWav("out/audio/creature_" + name + ".wav", pcm, rate, 1);
            spectrogram("out/audio/png/creature_" + name + ".png", pcm, rate);
            report(name.c_str(), pcm, rate, 1);
        }

    std::printf("\nsongs through the mixer (first 60 s)\n");
    const char* moods[] = {"crypt", "catacomb", "ember", "lair", "title"};
    for (int m = 0; m < 5; m++) {
        audio::setAmbience(m);
        std::vector<float> out = mix(60, [](float) {});
        std::string name = std::string("song_") + moods[m];
        writeWav("out/audio/" + name + ".wav", out, 44100, 2);
        report(name.c_str(), out, 44100, 2);
        std::vector<float> mono(out.size() / 2);
        for (size_t i = 0; i < mono.size(); i++) mono[i] = 0.5f * (out[i * 2] + out[i * 2 + 1]);
        spectrogram("out/audio/png/" + name + ".png", mono, 44100);
    }
    audio::setAmbience(-1);
    mix(4, [](float) {});

    std::printf("\n");
    const char* themes[] = {"crypt", "catacomb", "ember"};
    for (int t = 0; t < 3; t++)
        for (bool music : {false, true}) {
            std::vector<float> pcm = audio::renderLoop(t, music);
            std::string name = std::string(music ? "music_" : "amb_") + themes[t];
            writeWav("out/audio/" + name + ".wav", pcm, rate, 1);
            spectrogram("out/audio/png/" + name + ".png", pcm, rate);
            report(name.c_str(), pcm, rate, 1);
            // Music loops end to start; the ambience preview has its 24 s texture loop wrapping at 24 s.
            size_t seam = music ? pcm.size() : size_t(24 * rate);
            float jump = std::fabs(pcm[seam % pcm.size()] - pcm[seam - 1]);
            std::vector<float> deltas;
            for (size_t i = 1; i < pcm.size(); i++) deltas.push_back(std::fabs(pcm[i] - pcm[i - 1]));
            std::sort(deltas.begin(), deltas.end());
            std::printf("%-22s seam jump %.4f vs typical step: median %.4f, 99.9%% %.4f\n", "", double(jump), double(deltas[deltas.size() / 2]),
                        double(deltas[deltas.size() * 999 / 1000]));
        }

    // Full chain: each theme's beds with a burst of combat over them, then a pile-up stress test.
    std::printf("\nmaster mix (stereo 44.1 kHz, after reverb and limiter)\n");
    for (int t = 0; t < 3; t++) {
        audio::setAmbience(t);
        std::vector<float> out = mix(14, [](float time) {
            int tick = int(std::lround(time * 100));
            if (tick % 45 == 0 && tick < 300) audio::play(Sound::Step, 0.7f, 0, 0.9f + 0.05f * float(tick % 4));
            if (tick == 320) audio::play(Sound::Notice, 0.7f, -0.4f, 0.8f);
            if (tick == 380) audio::play(Sound::Swing, 0.7f, 0.2f), audio::play(Sound::Hit, 1.0f, 0.1f);
            if (tick == 420) audio::play(Sound::Growl, 0.7f, -0.2f, 1.0f), audio::play(Sound::Swing, 0.6f, -0.2f, 0.75f);
            if (tick == 440) audio::play(Sound::PlayerHurt, 0.9f);
            if (tick == 500) audio::play(Sound::Swing, 0.7f, 0.2f), audio::play(Sound::Hit, 1.0f), audio::play(Sound::MonsterHurt, 0.8f, 0, 1.25f);
            if (tick == 560) audio::play(Sound::MonsterDie, 0.9f, 0.1f, 1.25f);
            if (tick == 700) audio::play(Sound::Door, 0.8f, 0.3f);
            if (tick == 850) audio::play(Sound::Chest, 0.8f);
            if (tick == 1000) audio::play(Sound::Gold, 0.7f);
        });
        std::string name = std::string("mix_") + themes[t];
        writeWav("out/audio/" + name + ".wav", out, 44100, 2);
        report(name.c_str(), out, 44100, 2);
        std::vector<float> mono(out.size() / 2);
        for (size_t i = 0; i < mono.size(); i++) mono[i] = 0.5f * (out[i * 2] + out[i * 2 + 1]);
        spectrogram("out/audio/png/" + name + ".png", mono, 44100);
    }

    std::vector<float> idle = mix(12, [](float) {});
    report("mix_idle_ember", idle, 44100, 2);

    std::vector<float> pileup = mix(4, [](float time) {
        if (time == 0.5f) {
            audio::play(Sound::Fire, 1.0f);
            for (int i = 0; i < 8; i++) audio::play(Sound::MonsterHurt, 0.9f, float(i % 3 - 1) * 0.5f, 0.7f + 0.15f * float(i));
            for (int i = 0; i < 30; i++) audio::play(Sound::Hit, 1.0f);
        }
    });
    writeWav("out/audio/mix_pileup.wav", pileup, 44100, 2);
    report("mix_pileup", pileup, 44100, 2);

    auto busy = std::chrono::steady_clock::now();
    mix(10, [](float time) {  // the voice cap kept full, plus all three loops
        if (int(time * 100) % 10 == 0)
            for (int i = 0; i < 6; i++) audio::play(Sound(i * 5 % int(Sound::Count)), 0.3f, 0, 0.6f);
    });
    double cost = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - busy).count() / 10;
    std::printf("mixer cost at the voice cap: %.1f ms per second of audio (%.2f%% of one core)\n", cost, cost / 10);

    audio::setAmbience(-1);
    std::vector<float> fadeOut = mix(4, [](float) {});
    std::vector<float> hitTail = mix(3, [](float time) {
        if (time == 0.0f) audio::play(Sound::Hit, 1.0f);
    });
    std::printf("hit through the hall, energy per 100 ms (dB):");
    for (size_t at = 0; at + 8820 <= hitTail.size(); at += 8820) {
        double e = 0;
        for (size_t i = at; i < at + 8820; i++) e += double(hitTail[i]) * hitTail[i];
        std::printf(" %.0f", 10 * std::log10(e / 8820 + 1e-12));
    }
    std::printf("\n");
    writeWav("out/audio/mix_hit_tail.wav", hitTail, 44100, 2);
    report("mix_fade_to_silence", fadeOut, 44100, 2);
    std::vector<float> tail(fadeOut.end() - 4410 * 2, fadeOut.end());
    report("  last 0.1 s", tail, 44100, 2);
    return 0;
}
