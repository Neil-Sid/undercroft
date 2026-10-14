#pragma once
// The synthesis toolkit behind the audio: DSP blocks, buffer helpers and sound layers, plus the
// declarations shared by audio.cpp (the mixer), audio_sfx.cpp (effects, creatures, ambience) and
// audio_music.cpp (instruments and the sequencer). Everything renders at 22 kHz mono like PS1 SPU
// samples; the mixer resamples to 44.1 kHz.
#include "audio.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <vector>

namespace synth {

constexpr int RATE = 44100, SR = 22050;
constexpr float PI = 3.14159265f, TAU = 2 * PI;
using Buf = std::vector<float>;

// ---------- DSP building blocks ----------

struct Noise {
    uint32_t s;
    explicit Noise(uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    float next() {
        s = s * 1664525u + 1013904223u;
        return float(s >> 8) / float(1 << 24) * 2 - 1;
    }
    float uniform() { return next() * 0.5f + 0.5f; }
    float range(float a, float b) { return a + (b - a) * uniform(); }
};

struct OnePole {
    float y = 0;
    float lp(float x, float k) { return y += (x - y) * k; }
    float hp(float x, float k) { return x - lp(x, k); }
};
inline float corner(float hz) { return 1 - std::exp(-TAU * hz / SR); }

// Zero-delay-feedback state-variable filter (Simper). Band output has unity gain at the centre.
struct Svf {
    float ic1 = 0, ic2 = 0, low = 0, band = 0;
    float freq = -1, q = -1, a1 = 0, a2 = 0, a3 = 0, k = 0;
    void run(float x, float f, float res) {
        if (f != freq || res != q) {
            freq = f, q = res, k = 1 / res;
            float g = std::tan(PI * std::clamp(f, 10.0f, SR * 0.45f) / SR);
            a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
        }
        float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1, ic2 = 2 * v2 - ic2;
        low = v2, band = v1 * k;
    }
    float bp(float x, float f, float res) { return run(x, f, res), band; }
    float lp(float x, float f, float res = 0.707f) { return run(x, f, res), low; }
};

struct Osc {
    float phase = 0;
    float tick(float hz) {
        phase += hz / SR;
        return phase -= std::floor(phase);
    }
    float sine(float hz) { return std::sin(TAU * tick(hz)); }
    float saw(float hz) {  // band-limited with polyBLEP
        float dt = std::min(hz / SR, 0.5f), p = tick(hz), s = 2 * p - 1;
        if (p < dt) p /= dt, s -= p + p - p * p - 1;
        else if (p > 1 - dt) p = (p - 1) / dt, s -= p * p + p + p + 1;
        return s;
    }
};

inline float ad(float t, float attack, float decay) { return t < 0 ? 0 : t < attack ? t / attack : std::exp(-(t - attack) * decay); }
inline float ramp(float t, float a, float b) { return std::clamp((t - a) / (b - a), 0.0f, 1.0f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
// 0 -> 1 -> 0 over u in [0, 1], peaking at `peak`.
inline float arc(float u, float peak = 0.5f) {
    if (u <= 0 || u >= 1) return 0;
    if (u < peak) return std::sin(PI / 2 * u / peak);
    float c = std::cos(PI / 2 * (u - peak) / (1 - peak));
    return c * c;
}
// Eases in over `attack` and out over the last `release` seconds of `len`.
inline float swell(float t, float len, float attack, float release) {
    float a = std::min(1.0f, t / attack), r = std::clamp((len - t) / release, 0.0f, 1.0f);
    return a * a * (3 - 2 * a) * r * r * (3 - 2 * r);
}
inline float hz(float midi) { return 440 * std::pow(2.0f, (midi - 69) / 12); }

// ---------- buffers ----------

inline Buf make(float seconds) { return Buf(size_t(seconds * SR)); }

inline float peakOf(const Buf& b) {
    float p = 0;
    for (float x : b) p = std::max(p, std::abs(x));
    return p;
}

inline float rmsOf(const Buf& b) {
    double sum = 0;
    for (float x : b) sum += double(x) * x;
    return b.empty() ? 0 : float(std::sqrt(sum / double(b.size())));
}

// How loud a sound feels: its loudest 50 ms, weighted toward the 2-5 kHz band the ear is most
// sensitive to and away from rumble it barely hears. Peaks alone make a squeak feel far louder
// than a thud with the same peak.
inline float loudness(const Buf& b) {
    OnePole rumble;
    struct {
        float ic1 = 0, ic2 = 0;
    } s;
    // A fixed band-pass around 3 kHz (Simper SVF, Q 0.6), added to the signal: about +6 dB there.
    float g = std::tan(PI * 3200 / SR), k = 1 / 0.6f, a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
    size_t win = std::min<size_t>(SR / 20, std::max<size_t>(1, b.size()));
    std::vector<float> w(b.size());
    double sum = 0, best = 0;
    for (size_t i = 0; i < b.size(); i++) {
        float x = rumble.hp(b[i], 1 - std::exp(-TAU * 100 / SR));
        float v3 = x - s.ic2, v1 = a1 * s.ic1 + a2 * v3, v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
        s.ic1 = 2 * v1 - s.ic1, s.ic2 = 2 * v2 - s.ic2;
        w[i] = x + v1 * k;
        sum += double(w[i]) * w[i];
        if (i >= win) sum -= double(w[i - win]) * w[i - win];
        best = std::max(best, sum);
    }
    return float(std::sqrt(best / double(win)));
}

inline Buf scaled(Buf b, float by) {
    for (float& x : b) x *= by;
    return b;
}
inline Buf normalized(Buf b, float peak = 1) {
    float p = peakOf(b);
    return p > 0 ? scaled(std::move(b), peak / p) : b;
}

// Mixes x into b at `at` seconds; `wrap` folds the overflow onto the start of a loop.
inline void put(Buf& b, float at, const Buf& x, float gain, bool wrap = false) {
    size_t start = size_t(std::max(0.0f, at) * SR);
    for (size_t i = 0; i < x.size(); i++) {
        size_t j = start + i;
        if (wrap) j %= b.size();
        else if (j >= b.size()) break;
        b[j] += x[i] * gain;
    }
}

template <class F>
Buf render(float seconds, F f) {
    Buf b = make(seconds);
    for (size_t i = 0; i < b.size(); i++) b[i] = f(float(i) / SR);
    size_t fade = std::min(b.size() / 4, size_t(SR * 0.03f));  // a layer cut while still ringing must not click
    for (size_t i = 0; i < fade; i++) b[b.size() - 1 - i] *= float(i) / float(fade);
    return normalized(std::move(b));
}

// A seamless loop of a noisy texture: the overrun is crossfaded onto the start. Unit RMS.
template <class F>
Buf bed(float seconds, F f) {
    Buf b = make(seconds + 1);
    for (size_t i = 0; i < b.size(); i++) b[i] = f(float(i) / SR);
    size_t n = size_t(seconds * SR), fade = b.size() - n;
    for (size_t i = 0; i < fade; i++) {
        float u = PI / 2 * float(i) / float(fade);
        b[i] = b[i] * std::sin(u) + b[n + i] * std::cos(u);
    }
    b.resize(n);
    float rms = std::max(rmsOf(b), 1e-6f);
    return scaled(std::move(b), 1 / rms);
}

// A frequency that fits a whole number of cycles in a loop, so tones join without a seam.
inline float loopHz(float f, float seconds) { return std::round(f * seconds) / seconds; }

// ---------- impact and texture layers (each normalised to peak 1) ----------

// A heavy low impact: a pitch-dropping sine body and a burst of muffled dirt.
inline Buf thud(float f, float len, uint32_t seed) {
    Noise n(seed);
    Osc o;
    Svf lp;
    return render(len, [&](float t) {
        float body = o.sine(f * (1 + std::exp(-t * 45))) * ad(t, 0.002f, 5 / len);
        return body + lp.lp(n.next(), f * 8, 0.7f) * ad(t, 0.0005f, 14 / len) * 4;
    });
}

// A sharp contact transient.
inline Buf click(float len, uint32_t seed) {
    Noise n(seed);
    OnePole hp;
    return render(len, [&](float t) { return hp.hp(n.next(), 0.3f) * std::exp(-t * 4 / len); });
}

// Band-passed noise salted with sparse pops: grit, gravel, parchment, leather.
inline Buf grit(float len, float f, float q, float pops, float peakAt, uint32_t seed) {
    Noise n(seed);
    Svf bp;
    return render(len, [&](float t) {
        float pop = n.uniform() < pops ? n.next() * 6 : 0;
        return bp.bp(n.next() + pop, f, q) * arc(t / len, peakAt);
    });
}

// Random crackle: sparse impulses of random size ringing a band-pass. Fire, sparks, splinters.
inline Buf crackle(float len, float perSecond, float f, float q, float peakAt, uint32_t seed) {
    Noise n(seed);
    Svf bp;
    return render(len, [&](float t) {
        float size = 0.25f + 0.75f * std::pow(n.uniform(), 2.0f);
        float pop = n.uniform() < perSecond / SR ? size * (n.next() < 0 ? -8 : 8) : 0;
        return bp.bp(pop, f, q) * arc(t / len, peakAt);
    });
}

// Struck metal, wood, stone or bell: decaying partials as ratios of `f`.
struct Mode {
    float ratio, amp, decay;
};
inline Buf modal(float f, float len, const std::vector<Mode>& m) {
    std::vector<Osc> o(m.size());
    return render(len, [&](float t) {
        float s = 0;
        for (size_t i = 0; i < m.size(); i++)
            if (f * m[i].ratio < SR * 0.45f) s += o[i].sine(f * m[i].ratio) * m[i].amp * std::exp(-t * m[i].decay);
        return s * std::min(1.0f, t * 2000);
    });
}

// Materials for modal(): the partial ratios that make bone, wood, stone and plate sound like themselves.
inline Buf bone(float f, float len = 0.07f) { return modal(f, len, {{1, 1, 80}, {2.31f, 0.55f, 110}, {3.87f, 0.3f, 150}}); }
inline Buf wood(float f, float len = 0.3f) { return modal(f, len, {{1, 1, 22}, {2.57f, 0.45f, 35}, {4.13f, 0.25f, 50}}); }
inline Buf stone(float f, float len = 0.12f) { return modal(f, len, {{1, 1, 45}, {2.21f, 0.6f, 60}, {3.62f, 0.35f, 80}, {5.1f, 0.2f, 100}}); }
inline Buf plate(float f, float len = 0.9f) {
    return modal(f, len, {{1, 1, 4.5f}, {1.47f, 0.8f, 5}, {2.09f, 0.6f, 6}, {2.56f, 0.5f, 7}, {3.31f, 0.35f, 9}, {4.11f, 0.25f, 11}, {5.37f, 0.15f, 14}});
}

// Air: noise through a band-pass that sweeps with the shape functions.
template <class F, class A>
Buf whoosh(float len, float q, uint32_t seed, F centre, A amp) {
    Noise n(seed);
    Svf bp;
    return render(len, [&](float t) { return bp.bp(n.next(), centre(t / len), q) * amp(t / len); });
}

// Stick-slip friction: a jittery pulse train ringing a few resonances. Hinges, lids, sliding stone.
inline Buf creak(float len, float slow, float fast, std::initializer_list<float> resonances, uint32_t seed) {
    Noise n(seed);
    OnePole wander;
    std::vector<float> res(resonances);
    std::vector<Svf> f(res.size());
    float phase = 0;
    return render(len, [&](float t) {
        float u = t / len, rate = (slow + (fast - slow) * arc(u, 0.6f)) * (1 + wander.lp(n.next(), 0.003f) * 12);
        phase += std::max(3.0f, rate) / SR;
        float pulse = 0;
        if (phase >= 1) phase -= 1, pulse = 1 + 0.5f * n.next();
        float s = 0;
        for (size_t i = 0; i < f.size(); i++) s += f[i].bp(pulse, res[i] * (1 + 0.03f * std::sin(TAU * 0.8f * t + float(i))), 14) / float(i + 1);
        return s * arc(u, 0.15f);
    });
}

// Stone dragging on stone, juddering as it goes.
inline Buf grind(float len, float judderHz, uint32_t seed) {
    Noise n(seed);
    Svf lo, hi;
    OnePole bite;
    float phase = 0;
    return render(len, [&](float t) {
        phase += (judderHz + judderHz * 0.4f * std::sin(TAU * 0.7f * t)) / SR;
        float judder = 0.5f + 0.5f * std::sin(TAU * phase), rough = std::abs(bite.lp(n.next(), 0.01f)) * 12;
        float x = lo.lp(n.next(), 260, 1.4f) * (0.4f + 0.6f * judder * judder) * (0.5f + rough) + hi.bp(n.next(), 1800, 1) * 0.15f * judder;
        return x * arc(t / len, 0.15f);
    });
}

// Wet, crunchy flesh: driven band-passed noise.
inline Buf crunch(float len, float f, uint32_t seed) {
    Noise n(seed);
    Svf bp;
    return render(len, [&](float t) { return std::tanh(bp.bp(n.next(), f, 0.9f) * 6) * ad(t, 0.001f, 5 / len); });
}

// Something wet: a resonant low-pass sweeping down through noise, with gulping pops.
inline Buf squelch(float len, float from, float to, uint32_t seed) {
    Noise n(seed);
    Svf lp;
    return render(len, [&](float t) {
        float u = t / len, pop = n.uniform() < 120.0f / SR ? n.next() * 5 : 0;
        return lp.lp(n.next() + pop, from * std::pow(to / from, u), 4) * arc(u, 0.08f);
    });
}

// A water drop or bubble: a sine gliding up as it dies.
inline Buf bubble(float f, float len) {
    Osc o;
    return render(len, [&](float t) { return o.sine(f * (1 + 2.5f * t / len)) * ad(t, 0.001f, 6 / len); });
}

// A train of tiny clicks ringing a resonance: mandibles, claws on stone, bat sonar.
inline Buf chitter(float len, float perSecond, float f, float q, float jitter, uint32_t seed) {
    Noise n(seed);
    Svf bp;
    float next = 0;
    return render(len, [&](float t) {
        float pulse = 0;
        if (t >= next) pulse = (n.next() < 0 ? -1 : 1) * (0.5f + 0.5f * n.uniform()), next = t + (1 + jitter * n.next()) / perSecond;
        return bp.bp(pulse * 6, f * (1 + 0.1f * n.next()), q) * arc(t / len, 0.2f);
    });
}

// Hollow knocks scattered in time: bones, pebbles, coins. `settle` > 1 bunches them at the start
// like a heap collapsing; each knock is one of `material`'s voices at a pitch between lo and hi.
template <class M>
Buf scatter(float len, int count, float lo, float hi, float settle, uint32_t seed, M material) {
    Noise r(seed);
    Buf b = make(len + 0.4f);
    for (int i = 0; i < count; i++) {
        float at = len * std::pow(r.uniform(), settle), late = at / len;
        put(b, at, material(r.range(lo, hi)), r.range(0.4f, 1.0f) * (1 - 0.6f * late));
    }
    return b;
}

// A short tuned comb: puts a voice inside a helm, a jar or a hollow chest.
inline Buf comb(Buf b, float delayMs, float feedback) {
    size_t d = std::max<size_t>(1, size_t(delayMs * SR / 1000));
    for (size_t i = d; i < b.size(); i++) b[i] += b[i - d] * feedback;
    return normalized(std::move(b));
}

// ---------- voices ----------

// A throat: a buzzing source with sub-harmonic roughness, shaped by two moving formants.
struct Vox {
    float f0, f1, f2, amp;
};
template <class F>
Buf throat(float len, float rough, float breath, uint32_t seed, F shape) {
    Noise n(seed);
    Osc o, sub;
    OnePole wobble, rasp;
    Svf a, b, c;
    return render(len, [&](float t) {
        Vox v = shape(t / len);
        float f0 = v.f0 * (1 + wobble.lp(n.next(), 0.01f) * 2 * rough);
        float grain = std::max(0.0f, 1 + rough * rasp.lp(n.next(), 0.03f) * 8);
        float src = o.saw(f0) * (1 - rough * 0.45f * (1 + std::sin(TAU * sub.tick(f0 * 0.5f)))) * grain + n.next() * breath;
        return (a.bp(src, v.f1, 5) + 0.7f * b.bp(src, v.f2, 7) + 0.2f * c.bp(src, 2600, 6)) * v.amp;
    });
}

// Vowel formants (an adult male throat; divide by `size` for bigger or smaller creatures).
struct Formant {
    float f1, f2;
};
constexpr Formant AH{730, 1090}, EH{530, 1840}, EE{270, 2290}, OH{570, 840}, OO{300, 870}, UH{640, 1190};

// A vocal gesture: the pitch glides from f0a to f0b and the vowel from va to vb.
inline Buf call(float len, float f0a, float f0b, Formant va, Formant vb, float size, float rough, float breath, float peak, uint32_t seed) {
    return throat(len, rough, breath, seed, [=](float u) {
        return Vox{lerp(f0a, f0b, u), lerp(va.f1, vb.f1, u) / size, lerp(va.f2, vb.f2, u) / size, arc(u, peak)};
    });
}

// Whispered syllables: noise through two formants that hop between vowels, gated into syllables.
inline Buf whisper(float len, int syllables, float size, uint32_t seed) {
    Noise n(seed), r(seed + 1);
    Svf a, b, s;
    const Formant vowels[] = {AH, EE, OO, EH, OH};
    std::vector<Formant> seq;
    for (int i = 0; i < syllables; i++) seq.push_back(vowels[int(r.uniform() * 4.99f)]);
    return render(len, [&](float t) {
        float u = t / len * float(syllables);
        int k = std::min(syllables - 1, int(u));
        float in = u - float(k), gate = arc(in, 0.3f);
        Formant f = seq[size_t(k)];
        float hiss = s.bp(n.next(), 5200, 2) * 0.35f * (in < 0.2f ? 1.0f : 0.2f);  // the consonant at the front
        return (a.bp(n.next(), f.f1 / size, 6) + 0.8f * b.bp(n.next(), f.f2 / size, 8) + hiss) * gate;
    });
}

// ---------- instruments (each one note, normalised) ----------

// Glassy bell for chimes.
inline Buf chime(float f, float len) { return modal(f, len, {{1, 1, 2.2f}, {2.0f, 0.2f, 3}, {2.76f, 0.3f, 4}, {5.4f, 0.1f, 7}}); }

// Church bell: hum, prime, minor-third tierce, quint and nominal, with a slow warble.
inline Buf toll(float f, float len) {
    return modal(f, len, {{0.5f, 0.7f, 0.6f}, {0.503f, 0.35f, 0.65f}, {1, 0.5f, 0.85f}, {1.004f, 0.3f, 0.85f}, {1.19f, 0.45f, 1.0f},
                          {1.5f, 0.25f, 1.4f}, {2.0f, 0.55f, 1.2f}, {2.52f, 0.2f, 2.2f}, {2.66f, 0.18f, 2.5f}, {3.0f, 0.15f, 3},
                          {4.17f, 0.12f, 4}, {5.43f, 0.08f, 5}});
}

// Karplus-Strong plucked string: a filtered noise burst circulating in a damped delay line.
// `bright` sets how hard it is plucked; `body` how long it rings.
inline Buf pluck(float f, float len, float bright, float body, uint32_t seed) {
    Noise n(seed);
    OnePole tone;
    size_t period = std::max<size_t>(2, size_t(std::lround(SR / f - 0.5f)));
    Buf line(period);
    for (float& x : line) x = tone.lp(n.next(), bright);
    float loss = std::pow(0.001f, 1.0f / (body * f));
    size_t i = 0;
    return render(len, [&](float) {
        float y = line[i];
        size_t j = (i + 1) % period;
        line[i] = 0.5f * (y + line[j]) * loss;
        i = j;
        return y;
    });
}

// Two-operator FM: music boxes, celestas and bells. The index decays faster than the note.
inline Buf fm(float f, float len, float ratio, float index, float decay, float indexDecay) {
    Osc c, m;
    return render(len, [&](float t) {
        float mod = std::sin(TAU * m.tick(f * ratio)) * index * std::exp(-t * indexDecay);
        return std::sin(TAU * c.tick(f) + mod) * std::exp(-t * decay) * std::min(1.0f, t * 3000);
    });
}

// A small pipe organ: drawbar sines with a breathy chiff as the pipe speaks.
inline Buf organ(float f, float len, uint32_t seed) {
    Noise n(seed);
    Osc o[5];
    Svf chiff;
    const float ratio[5] = {0.5f, 1, 2, 3, 4}, amp[5] = {0.45f, 1, 0.55f, 0.3f, 0.22f};
    return render(len, [&](float t) {
        float s = 0;
        for (int i = 0; i < 5; i++) s += o[i].sine(f * ratio[i] * (1 + 0.0015f * float(i))) * amp[i];
        float ch = chiff.bp(n.next(), f * 3, 2) * std::exp(-t * 25) * 0.7f;
        return s * std::min(1.0f, t / 0.05f) + ch;
    });
}

// Slow detuned-saw pad through a drifting low-pass, like a sampled string patch.
inline Buf pad(float f, float len, float bright, float attack, float release, uint32_t seed) {
    Noise n(seed);
    Osc o[3], sub;
    Svf lp;
    float detune[3] = {1, 1.0035f, 0.9967f};
    for (Osc& x : o) x.phase = n.uniform();
    return render(len, [&](float t) {
        float s = sub.sine(f * 0.5f) * 0.3f;
        for (int i = 0; i < 3; i++) s += o[i].saw(f * detune[i]) * 0.5f;
        return swell(t, len, attack, release) * lp.lp(s, f * bright * (1 + 0.2f * std::sin(TAU * 0.07f * t + float(seed))), 0.9f);
    });
}

// A wordless choir on one vowel: detuned saws through formants with a gentle vibrato.
inline Buf choir(float f, float len, Formant vowel, float attack, float release, uint32_t seed) {
    Noise n(seed);
    Osc o[3];
    Svf a, b, c;
    float detune[3] = {1, 1.006f, 0.994f};
    return render(len, [&](float t) {
        float v = 1 + 0.004f * std::sin(TAU * (4.6f + 0.3f * float(seed % 3)) * t), s = n.next() * 0.15f;
        for (int i = 0; i < 3; i++) s += o[i].saw(f * v * detune[i]);
        return (a.bp(s, vowel.f1, 6) + 0.6f * b.bp(s, vowel.f2, 8) + 0.15f * c.bp(s, 2400, 6)) * swell(t, len, attack, release);
    });
}

// Ocarina or bone flute: a near-sine with breath, the vibrato arriving late.
inline Buf flute(float f, float len, uint32_t seed) {
    Noise n(seed);
    Osc o, o2;
    Svf air;
    return render(len, [&](float t) {
        float vib = 1 + 0.007f * ramp(t, 0.3f, 0.9f) * std::sin(TAU * 4.8f * t);
        float s = o.sine(f * vib) + 0.1f * o2.sine(2 * f * vib) + air.bp(n.next(), f * 1.5f, 2) * (0.12f + 0.4f * std::exp(-t * 12));
        return s * std::min(1.0f, t / 0.07f);
    });
}

// A struck membrane (timpani, frame drum, taiko): drumhead modes that drop in pitch on the strike.
inline Buf drum(float f, float len, float slap, uint32_t seed) {
    Noise n(seed);
    Osc o[4];
    Svf skin;
    const float ratio[4] = {1, 1.59f, 2.14f, 2.65f}, amp[4] = {1, 0.5f, 0.3f, 0.18f}, dec[4] = {1, 1.7f, 2.4f, 3.2f};
    return render(len, [&](float t) {
        float bend = 1 + 0.22f * std::exp(-t * 28), s = 0;
        for (int i = 0; i < 4; i++) s += o[i].sine(f * ratio[i] * bend) * amp[i] * std::exp(-t * dec[i] * 4 / len);
        return s + skin.bp(n.next(), 700, 0.7f) * slap * std::exp(-t * 55);
    });
}

// A tam-tam: many inharmonic partials beating against each other, the highs blooming late.
inline Buf gong(float f, float len) {
    Buf body = modal(f, len, {{1, 1, 0.5f}, {1.006f, 0.6f, 0.5f}, {1.52f, 0.5f, 0.7f}, {2.08f, 0.45f, 0.8f}, {2.83f, 0.35f, 1.0f},
                              {3.47f, 0.3f, 1.2f}, {4.21f, 0.22f, 1.4f}, {5.13f, 0.16f, 1.7f}, {6.33f, 0.12f, 2.0f}});
    Buf shimmer = whoosh(len, 3, 7, [](float u) { return 2400 - 800 * u; }, [](float u) { return arc(u, 0.25f); });
    for (size_t i = 0; i < body.size(); i++) body[i] += shimmer[i] * 0.15f;
    return normalized(std::move(body));
}

// Two detuned saws through a low-pass that opens on the attack: a sampled brass section.
inline Buf brass(float f, float len) {
    Osc a, b, c;
    Svf lp;
    return render(len + 0.25f, [&](float t) {
        float v = 1 + 0.006f * ramp(t, 0.2f, 0.5f) * std::sin(TAU * 5.2f * t);
        float s = a.saw(f * v) + b.saw(f * v * 1.004f) + 0.4f * c.saw(f * v * 0.5f);
        float amp = std::min(1.0f, t / 0.03f) * (t < len ? 1 : std::exp(-(t - len) * 22));
        return lp.lp(s, f * (1.6f + 5 * ad(t, 0.04f, 4)), 1.1f) * amp;
    });
}

// ---------- shared between the audio files ----------

// Peak level, reverb send, number of variations and random detune for a sound.
struct Spec {
    float peak, send;
    int variants = 1;
    float jitter = 0;
};

// audio_sfx.cpp
constexpr int STEP_VARIANTS = 4;  // per floor theme: Sound::Step holds three sets
Buf design(Sound s, uint32_t variant);
Spec specOf(Sound s);
Buf creature(int kind, Cue cue, uint32_t variant);
Spec creatureSpec(Cue cue);
constexpr float AMB_LEN = 24, EVENTS_LEN = 41;
struct Ambience {
    Buf texture = make(AMB_LEN), events = make(EVENTS_LEN);
};
Ambience ambience(int theme);

// audio_music.cpp
enum Inst : int { Harp, MusicBox, Bell, Organ, Choir, Strings, Flute, Bass, Timpani, Taiko, Gong, Brass, INSTRUMENTS };

// One sampled note, pitched by resampling like an SPU voice. Sustained instruments loop the
// stretch between loopStart and loopEnd while the note is held, then fade over `release`.
struct Instrument {
    Buf sample;
    float baseMidi = 60, send = 0.5f, release = 0.4f;
    size_t loopStart = 0, loopEnd = 0;  // loopEnd 0: plays once
};
std::vector<std::function<void()>> instrumentJobs();  // fills the instrument table; run before anything plays it
const Instrument& instrument(int i);

struct Note {
    float beat, beats, midi;
};
struct Part {
    int instrument;
    float gain, pan;
    std::vector<std::vector<Note>> phrases;  // one is picked at random each time the song loops
};
struct Song {
    float bpm, beats;
    std::vector<Part> parts;
};
constexpr int SONGS = 5;  // crypt, catacomb, ember, the Lich's floor, title
const Song& song(int mood);

// Renders a melody on one instrument, for stingers. Notation as in audio_music.cpp.
Buf phrase(int inst, const char* notes, float bpm);
Buf bounce(const Song& s);  // one pass of a song, mono, for tests

// Walks a song in output frames and reports each note as it starts.
struct Sequencer {
    const Song* song = nullptr;
    double beat = 0;
    std::vector<int> chosen;
    std::vector<size_t> next;
    void start(const Song* s, Noise& rng);
    void advance(int frames, Noise& rng, const std::function<void(const Part&, const Note&, int offset, int frames)>& on);
};

}  // namespace synth
