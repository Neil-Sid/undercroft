// Sound effects, creature voices and ambience beds, built from the layers in synth.h.
#include "synth.h"

namespace synth {
namespace {

// Builds a sound by laying layers at offsets: Mix(len).add(at, layer, gain)...
struct Mix {
    Buf b;
    explicit Mix(float len) : b(make(len)) {}
    Mix& add(float at, const Buf& x, float gain) {
        put(b, at, x, gain);
        return *this;
    }
    operator Buf() { return std::move(b); }
};

// ---------- small shared gestures ----------

// A rodent or bat squeak: a sine darting up and back, with a breathy edge and a little roughness.
Buf squeak(float f, float len, float bend, float rough, uint32_t seed) {
    Noise n(seed);
    Osc o, buzz;
    Svf air;
    return render(len, [&](float t) {
        float u = t / len, grain = 1 - rough * (0.5f + 0.5f * std::sin(TAU * buzz.tick(70 + 30 * u)));
        return (o.sine(f * (1 + bend * std::sin(PI * u)) * (1 - 0.2f * u)) * grain + air.bp(n.next(), f, 3) * 0.25f) * arc(u, 0.12f);
    });
}

// A ghostly voice: detuned sines bending from one pitch to another over a breath.
Buf ghost(float len, float from, float to, float tremolo, uint32_t seed) {
    Noise n(seed);
    Osc o[3];
    Svf air;
    return render(len, [&](float t) {
        float u = t / len, f = from * std::pow(to / from, u) * (1 + 0.02f * std::sin(TAU * 5.5f * t));
        float s = o[0].sine(f) + 0.6f * o[1].sine(f * 1.007f) + 0.35f * o[2].sine(f * 2.01f);
        float trem = 1 - tremolo * (0.5f + 0.5f * std::sin(TAU * 9 * t));
        return (s + air.bp(n.next(), f * 2, 2) * 0.6f) * trem * arc(u, 0.25f);
    });
}

// Wings: one downbeat of leather or stone pushing air.
Buf wingbeat(float lo, float len, uint32_t seed) {
    return whoosh(len, 0.6f, seed, [=](float u) { return lo + lo * 1.5f * arc(u, 0.4f); }, [](float u) { return arc(u, 0.3f); });
}

// A hiss: air forced through teeth or mandibles.
Buf hiss(float len, float f, uint32_t seed) {
    return whoosh(len, 1.2f, seed, [=](float u) { return f * (1 + 0.15f * u); }, [](float u) { return arc(u, 0.35f); });
}

Buf chainPing(float f) { return modal(f, 0.12f, {{1, 1, 40}, {1.62f, 0.5f, 50}, {2.71f, 0.3f, 60}}); }
Buf coin(float f) { return modal(f, 0.3f, {{1, 1, 22}, {1.49f, 0.6f, 30}, {2.17f, 0.4f, 40}}); }
Buf bubbles(float len, int count, float lo, float hi, uint32_t seed) {
    return scatter(len, count, lo, hi, 1.0f, seed, [](float f) { return bubble(f, 0.06f + 30 / f); });
}

// ---------- creatures ----------
// Notice: it spots you. Attack: its own wind-up or cast (armed ones also get the app's weapon
// whoosh). Impact: your blow landing on its body. Hurt: its cry. Die. Step: moving in the dark.

Buf rat(Cue c, uint32_t v) {
    float k = 1 + 0.07f * float(v);
    switch (c) {
    case Cue::Notice: return Mix(0.4f).add(0, squeak(3600 * k, 0.06f, 0.25f, 0.2f, v), 1).add(0.09f, squeak(4100 * k, 0.05f, 0.2f, 0.2f, v + 1), 0.8f).add(0.21f, squeak(3300 * k, 0.08f, 0.3f, 0.3f, v + 2), 0.9f);
    case Cue::Attack: return Mix(0.3f).add(0, hiss(0.14f, 5200, 10 + v), 0.5f).add(0.12f, bone(3000 * k, 0.03f), 0.8f).add(0.145f, bone(2600 * k, 0.03f), 0.6f);
    case Cue::Impact: return Mix(0.25f).add(0, thud(130 * k, 0.12f, 20 + v), 0.8f).add(0, crunch(0.08f, 1100, 21 + v), 0.6f);
    case Cue::Hurt: return squeak(3900 * k, 0.28f, 0.35f, 0.65f, 30 + v);
    case Cue::Die: return Mix(0.8f).add(0, squeak(4200, 0.55f, -0.45f, 0.5f, 40), 1).add(0.5f, thud(150, 0.1f, 41), 0.5f).add(0.62f, squeak(2600, 0.06f, 0.1f, 0.4f, 42), 0.25f);
    default: return Mix(0.25f).add(0, chitter(0.18f, 45, 4200, 6, 0.5f, 50 + v), 0.8f).add(0.02f, thud(220, 0.05f, 51 + v), 0.3f);
    }
}

Buf bat(Cue c, uint32_t v) {
    float k = 1 + 0.06f * float(v);
    switch (c) {
    case Cue::Notice: return chitter(0.35f, 28 * k, 6400, 8, 0.25f, 100 + v);
    case Cue::Attack: return Mix(0.35f).add(0, fm(2900 * k, 0.28f, 1.47f, 2.5f, 5, 3), 0.7f).add(0, squeak(3100 * k, 0.25f, 0.2f, 0.8f, 110 + v), 0.5f).add(0.02f, wingbeat(300, 0.1f, 111 + v), 0.6f);
    case Cue::Impact: return Mix(0.15f).add(0, whoosh(0.06f, 0.7f, 120 + v, [](float) { return 1200.0f; }, [](float u) { return ad(u, 0.05f, 6); }), 0.8f).add(0, thud(240, 0.06f, 121 + v), 0.5f);
    case Cue::Hurt: return squeak(3400 * k, 0.22f, 0.4f, 0.75f, 130 + v);
    case Cue::Die:
        return Mix(0.95f).add(0, squeak(3300, 0.5f, -0.5f, 0.6f, 140), 1).add(0.15f, wingbeat(260, 0.1f, 141), 0.5f).add(0.33f, wingbeat(230, 0.12f, 142), 0.4f)
            .add(0.58f, wingbeat(200, 0.14f, 143), 0.3f).add(0.78f, thud(190, 0.08f, 144), 0.45f);
    default: return Mix(0.3f).add(0, wingbeat(320 * k, 0.09f, 150 + v), 0.8f).add(0.12f, wingbeat(300 * k, 0.09f, 151 + v), 0.7f);
    }
}

Buf skeleton(Cue c, uint32_t v) {
    auto knock = [](float f) { return bone(f); };
    switch (c) {
    case Cue::Notice: return Mix(0.5f).add(0, chitter(0.4f, 24, 2300, 9, 0.15f, 200 + v), 0.9f).add(0.05f, creak(0.3f, 60, 120, {900, 1700}, 201 + v), 0.25f);
    case Cue::Attack: return Mix(0.4f).add(0, hiss(0.25f, 3600, 210 + v), 0.4f).add(0, creak(0.25f, 70, 160, {1100, 2100}, 211 + v), 0.3f).add(0.05f, scatter(0.2f, 6, 1700, 2600, 1, 212 + v, knock), 0.6f);
    case Cue::Impact:
        return Mix(0.3f).add(0, bone(2400 + 200 * float(v), 0.09f), 1).add(0, crackle(0.12f, 900, 3000, 2, 0.05f, 220 + v), 0.6f).add(0, thud(180, 0.07f, 221 + v), 0.5f)
            .add(0.008f, bone(1500, 0.1f), 0.5f);
    case Cue::Hurt: return scatter(0.3f, 10, 1600, 2800, 1, 230 + v, knock);
    case Cue::Die: {
        Mix m(1.6f);
        m.add(0, scatter(0.9f, 40, 1200, 2800, 2.2f, 240, knock), 1).add(0.18f, thud(110, 0.2f, 241), 0.5f);
        const float bounce[4] = {0.3f, 0.55f, 0.7f, 0.79f};  // the skull, bouncing
        for (int i = 0; i < 4; i++) m.add(bounce[i], bone(880, 0.15f), 0.7f - 0.15f * float(i));
        return m.add(0.2f, grit(1.0f, 2800, 0.7f, 0.01f, 0.1f, 242), 0.12f);
    }
    default: return Mix(0.2f).add(0, bone(1700 + 150 * float(v), 0.05f), 0.6f).add(0.05f, bone(2100, 0.04f), 0.4f).add(0.01f, grit(0.12f, 1800, 0.8f, 0.02f, 0.3f, 250 + v), 0.3f);
    }
}

Buf ooze(Cue c, uint32_t v) {
    switch (c) {
    case Cue::Notice: return Mix(0.7f).add(0, bubbles(0.5f, 8, 140, 420, 300 + v), 0.8f).add(0.05f, call(0.5f, 70, 60, OO, UH, 0.8f, 0.9f, 0.7f, 0.3f, 301 + v), 0.5f);
    case Cue::Attack: return Mix(0.4f).add(0, squelch(0.25f, 2500, 300, 310 + v), 1).add(0, thud(90, 0.15f, 311 + v), 0.6f);
    case Cue::Impact: return Mix(0.3f).add(0, squelch(0.2f, 1800, 250, 320 + v), 1).add(0.05f, bubble(300, 0.08f), 0.4f);
    case Cue::Hurt: return Mix(0.5f).add(0, bubbles(0.35f, 12, 200, 600, 330 + v), 0.8f).add(0, squelch(0.15f, 900, 300, 331 + v), 0.5f);
    case Cue::Die: return Mix(1.6f).add(0, squelch(1.2f, 900, 120, 340), 1).add(0.1f, bubbles(1.0f, 16, 120, 360, 341), 0.6f).add(1.1f, squelch(0.4f, 1500, 200, 342), 0.5f);
    default: return Mix(0.35f).add(0, squelch(0.3f, 700, 300, 350 + v), 0.6f).add(0.1f, bubble(260, 0.07f), 0.25f);
    }
}

Buf ghoul(Cue c, uint32_t v) {
    float k = 1 + 0.05f * float(v);
    switch (c) {
    case Cue::Notice: return call(0.95f, 115 * k, 92 * k, OO, AH, 1.05f, 0.55f, 0.45f, 0.35f, 400 + v);
    case Cue::Attack:
        return Mix(0.45f).add(0, call(0.38f, 170 * k, 140 * k, AH, EH, 1.0f, 0.95f, 0.6f, 0.15f, 410 + v), 1)
            .add(0.18f, whoosh(0.12f, 1.4f, 411 + v, [](float u) { return 1800 + 1500 * u; }, [](float u) { return arc(u, 0.3f); }), 0.4f);
    case Cue::Impact: return Mix(0.3f).add(0, crunch(0.12f, 650, 420 + v), 1).add(0, thud(85, 0.15f, 421 + v), 0.6f).add(0.01f, squelch(0.1f, 1600, 500, 422 + v), 0.3f);
    case Cue::Hurt: return call(0.42f, 150 * k, 105 * k, EH, UH, 1.0f, 0.6f, 0.9f, 0.12f, 430 + v);
    case Cue::Die: return Mix(1.4f).add(0, call(1.2f, 125, 55, AH, OO, 1.05f, 0.95f, 0.6f, 0.15f, 440), 1).add(0.45f, bubbles(0.5f, 6, 150, 300, 441), 0.25f).add(1.05f, thud(60, 0.3f, 442), 0.7f);
    default: return Mix(0.4f).add(0, grit(0.28f, 600, 0.8f, 0.02f, 0.6f, 450 + v), 0.5f).add(0.2f, thud(95, 0.1f, 451 + v), 0.5f);
    }
}

Buf spider(Cue c, uint32_t v) {
    auto tick = [](float f) { return bone(f, 0.02f); };
    switch (c) {
    case Cue::Notice: return Mix(0.5f).add(0, chitter(0.4f, 70, 3800, 7, 0.4f, 500 + v), 0.8f).add(0, hiss(0.45f, 5000, 501 + v), 0.4f);
    case Cue::Attack: return Mix(0.4f).add(0, hiss(0.3f, 4600, 510 + v), 0.6f).add(0.25f, bone(3400, 0.03f), 0.8f).add(0.29f, bone(3100, 0.03f), 0.7f);
    case Cue::Impact: return Mix(0.25f).add(0, crackle(0.1f, 1500, 2500, 1.5f, 0.05f, 520 + v), 0.9f).add(0, stone(1300 + 100 * float(v), 0.08f), 0.5f).add(0, crunch(0.08f, 1500, 521 + v), 0.5f);
    case Cue::Hurt: return chitter(0.3f, 110, 4800, 5, 0.6f, 530 + v);
    case Cue::Die: return Mix(1.3f).add(0, scatter(1.0f, 30, 2500, 4200, 1.8f, 540, tick), 0.8f).add(0, crunch(0.15f, 1200, 541), 0.6f).add(0.05f, hiss(0.5f, 4200, 542), 0.3f);
    default: return scatter(0.2f, 8, 2600, 3800, 1.0f, 550 + v, tick);
    }
}

Buf cultist(Cue c, uint32_t v) {
    switch (c) {
    case Cue::Notice: return whisper(0.9f, 4, 1.0f, 600 + v);
    case Cue::Attack:
        return Mix(0.9f).add(0, call(0.8f, 110, 110, AH, OH, 1, 0.15f, 0.25f, 0.3f, 610 + v), 0.8f).add(0, call(0.8f, 165, 165, AH, OH, 1, 0.15f, 0.25f, 0.3f, 611 + v), 0.5f)
            .add(0.1f, whisper(0.5f, 3, 1.0f, 612 + v), 0.3f);
    case Cue::Impact: return Mix(0.3f).add(0, thud(100, 0.12f, 620 + v), 0.8f).add(0, grit(0.12f, 2200, 0.8f, 0.05f, 0.2f, 621 + v), 0.4f).add(0, crunch(0.06f, 900, 622 + v), 0.4f);
    case Cue::Hurt: return call(0.35f, 230 - 15 * float(v), 185, AH, EH, 1, 0.25f, 0.3f, 0.15f, 630 + v);
    case Cue::Die:
        return Mix(1.2f).add(0, whoosh(0.22f, 1.5f, 640, [](float u) { return 900 + 900 * u; }, [](float u) { return arc(u, 0.7f); }), 0.4f)
            .add(0.22f, call(0.6f, 210, 130, AH, UH, 1, 0.3f, 0.35f, 0.12f, 641), 1).add(0.85f, thud(70, 0.3f, 642), 0.6f);
    default: return Mix(0.3f).add(0, thud(100, 0.1f, 650 + v), 0.6f).add(0, grit(0.15f, 1500, 0.8f, 0.03f, 0.3f, 651 + v), 0.3f);
    }
}

Buf wraith(Cue c, uint32_t v) {
    switch (c) {
    case Cue::Notice: return ghost(1.4f, 520 + 30 * float(v), 430, 0.2f, 700 + v);
    case Cue::Attack:
        return Mix(0.6f).add(0, whoosh(0.5f, 1.2f, 710 + v, [](float u) { return 600 * std::pow(5.0f, u); }, [](float u) { return arc(u, 0.7f); }), 0.6f)
            .add(0.15f, ghost(0.45f, 900, 640, 0.3f, 711 + v), 0.8f);
    case Cue::Impact: return Mix(0.5f).add(0, hiss(0.3f, 3000, 720 + v), 0.6f).add(0, chime(2700, 0.4f), 0.4f).add(0, chime(2735, 0.4f), 0.35f);
    case Cue::Hurt: return ghost(0.6f, 700 - 40 * float(v), 380, 0.6f, 730 + v);
    case Cue::Die: return Mix(2.6f).add(0, ghost(2.2f, 620, 140, 0.3f, 740), 1).add(0, whoosh(2.5f, 0.7f, 741, [](float u) { return 1400 - 1000 * u; }, [](float u) { return arc(u, 0.2f); }), 0.4f);
    default: return whoosh(0.45f, 0.7f, 750 + v, [](float u) { return 500 + 200 * u; }, [](float u) { return arc(u, 0.5f); });
    }
}

// The voice echoes inside the helm, and every blow rings off plate.
Buf knight(Cue c, uint32_t v) {
    auto jingle = [](float len, int count, uint32_t seed) { return scatter(len, count, 3000, 5200, 1, seed, chainPing); };
    switch (c) {
    case Cue::Notice: return Mix(1.0f).add(0, comb(call(0.9f, 82, 74, OH, UH, 1.1f, 0.6f, 0.3f, 0.3f, 800 + v), 3.5f, 0.6f), 1).add(0.1f, jingle(0.4f, 6, 801 + v), 0.2f);
    case Cue::Attack: return Mix(0.45f).add(0, jingle(0.3f, 9, 810 + v), 0.5f).add(0.05f, comb(call(0.3f, 100, 88, UH, AH, 1.1f, 0.7f, 0.35f, 0.2f, 811 + v), 3.5f, 0.6f), 0.8f);
    case Cue::Impact: return Mix(1.0f).add(0, plate(620 + 70 * float(v), 0.9f), 1).add(0, click(0.004f, 820 + v), 0.6f).add(0, thud(120, 0.1f, 821 + v), 0.5f);
    case Cue::Hurt: return Mix(0.8f).add(0, plate(480, 0.7f), 0.6f).add(0.02f, comb(call(0.45f, 95, 80, UH, OH, 1.1f, 0.6f, 0.3f, 0.15f, 830 + v), 3.5f, 0.6f), 0.9f);
    case Cue::Die: {
        Mix m(1.8f);
        m.add(0, comb(call(0.6f, 90, 60, OH, UH, 1.1f, 0.7f, 0.3f, 0.15f, 840), 3.5f, 0.6f), 0.7f).add(0.35f, thud(60, 0.4f, 841), 0.8f);
        m.add(0.35f, scatter(1.2f, 14, 350, 900, 1.6f, 842, [](float f) { return plate(f, 0.6f); }), 0.9f);
        return m.add(0.35f, jingle(0.8f, 16, 843), 0.4f);
    }
    default: return Mix(0.4f).add(0, thud(55, 0.2f, 850 + v), 0.9f).add(0.01f, jingle(0.15f, 5, 851 + v), 0.25f);
    }
}

Buf ogre(Cue c, uint32_t v) {
    switch (c) {
    case Cue::Notice: return call(1.1f, 78, 64, AH, OH, 1.6f, 0.85f, 0.35f, 0.3f, 900 + v);
    case Cue::Attack: return Mix(0.75f).add(0, call(0.6f, 95, 72, AH, AH, 1.55f, 1.0f, 0.45f, 0.2f, 910 + v), 1).add(0.4f, whoosh(0.3f, 0.8f, 911 + v, [](float u) { return 300 + 300 * u; }, [](float u) { return arc(u, 0.4f); }), 0.4f);
    case Cue::Impact: return Mix(0.4f).add(0, thud(55, 0.25f, 920 + v), 1).add(0, crunch(0.15f, 280, 921 + v), 0.8f).add(0.01f, squelch(0.12f, 900, 300, 922 + v), 0.3f);
    case Cue::Hurt: return call(0.5f, 105, 80, EH, AH, 1.55f, 0.7f, 0.4f, 0.15f, 930 + v);
    case Cue::Die:
        return Mix(2.4f).add(0, call(1.6f, 90, 40, AH, OO, 1.6f, 0.9f, 0.5f, 0.15f, 940), 1).add(1.35f, thud(35, 0.8f, 941), 1)
            .add(1.36f, scatter(0.6f, 12, 400, 1200, 1.5f, 942, [](float f) { return stone(f, 0.1f); }), 0.3f);
    default: return Mix(0.45f).add(0, thud(42, 0.35f, 950 + v), 1).add(0.01f, grit(0.2f, 400, 0.7f, 0.04f, 0.2f, 951 + v), 0.3f);
    }
}

Buf imp(Cue c, uint32_t v) {
    auto cackle = [](int bursts, uint32_t seed) {
        Mix m(0.15f * float(bursts) + 0.2f);
        for (int i = 0; i < bursts; i++) m.add(0.11f * float(i), call(0.09f, 330 + 25 * float(i), 300, EH, AH, 0.65f, 0.3f, 0.3f, 0.2f, seed + uint32_t(i)), 1 - 0.1f * float(i));
        return Buf(m);
    };
    switch (c) {
    case Cue::Notice: return cackle(5, 1000 + v * 10);
    case Cue::Attack:
        return Mix(0.6f).add(0, whoosh(0.4f, 1, 1010 + v, [](float u) { return 400 + 2200 * u; }, [](float u) { return arc(u, 0.6f); }), 0.7f)
            .add(0.05f, crackle(0.45f, 200, 2600, 1, 0.3f, 1011 + v), 0.4f).add(0, cackle(2, 1020 + v * 10), 0.5f);
    case Cue::Impact: return Mix(0.3f).add(0, hiss(0.2f, 4500, 1030 + v), 0.6f).add(0, crackle(0.2f, 1200, 3000, 1, 0.05f, 1031 + v), 0.6f).add(0, thud(160, 0.06f, 1032 + v), 0.4f);
    case Cue::Hurt: return call(0.25f, 620 + 40 * float(v), 420, EE, EH, 0.65f, 0.3f, 0.2f, 0.1f, 1040 + v);
    case Cue::Die:
        return Mix(1.2f).add(0, call(0.4f, 700, 300, EE, AH, 0.65f, 0.4f, 0.3f, 0.1f, 1050), 1).add(0.35f, bubble(500, 0.08f), 0.6f)
            .add(0.38f, whoosh(0.8f, 1.5f, 1051, [](float u) { return 5000 - 2000 * u; }, [](float u) { return ad(u, 0.02f, 4); }), 0.4f);
    default: return Mix(0.2f).add(0, thud(160, 0.06f, 1060 + v), 0.5f).add(0, crackle(0.15f, 300, 3000, 1, 0.3f, 1061 + v), 0.25f);
    }
}

Buf gargoyle(Cue c, uint32_t v) {
    auto chip = [](float f) { return stone(f); };
    switch (c) {
    case Cue::Notice: return Mix(1.1f).add(0, grind(0.9f, 12, 1100 + v), 1).add(0.1f, scatter(0.8f, 8, 1400, 2400, 1, 1101 + v, chip), 0.3f);
    case Cue::Attack: return Mix(0.6f).add(0, wingbeat(160, 0.18f, 1110 + v), 0.9f).add(0.2f, wingbeat(150, 0.18f, 1111 + v), 0.8f).add(0.05f, grind(0.3f, 18, 1112 + v), 0.4f);
    case Cue::Impact: return Mix(0.3f).add(0, stone(1500 + 150 * float(v), 0.12f), 1).add(0, grit(0.15f, 3000, 0.8f, 0.05f, 0.05f, 1120 + v), 0.4f).add(0, thud(140, 0.08f, 1121 + v), 0.5f);
    case Cue::Hurt: return Mix(0.4f).add(0, crackle(0.15f, 2000, 2200, 1.2f, 0.05f, 1130 + v), 0.8f).add(0, stone(900, 0.15f), 0.6f).add(0.05f, grind(0.25f, 20, 1131 + v), 0.4f);
    case Cue::Die:
        return Mix(1.8f).add(0, scatter(1.2f, 30, 500, 1800, 1.8f, 1140, chip), 1).add(0.1f, thud(45, 0.6f, 1141), 0.8f).add(0.1f, grit(1.5f, 2400, 0.7f, 0.02f, 0.1f, 1142), 0.15f);
    default: return Mix(0.35f).add(0, thud(60, 0.25f, 1150 + v), 0.9f).add(0, stone(700, 0.1f), 0.3f);
    }
}

Buf mimic(Cue c, uint32_t v) {
    auto coins = [](float len, int count, uint32_t seed) { return scatter(len, count, 2600, 4000, 1.3f, seed, coin); };
    auto teeth = [](uint32_t seed) { return scatter(0.05f, 5, 2500, 3200, 1, seed, [](float f) { return bone(f, 0.03f); }); };
    switch (c) {
    case Cue::Notice:
        return Mix(0.8f).add(0, creak(0.6f, 60, 160, {380, 900, 1800}, 1200 + v), 0.7f).add(0.35f, squelch(0.25f, 1200, 400, 1201 + v), 0.4f)
            .add(0.3f, call(0.4f, 75, 70, OH, UH, 1.3f, 0.9f, 0.5f, 0.2f, 1202 + v), 0.5f);
    case Cue::Attack: return Mix(0.4f).add(0.12f, wood(260, 0.25f), 1).add(0.12f, teeth(1210 + v), 0.7f).add(0, call(0.25f, 85, 75, AH, UH, 1.3f, 0.9f, 0.5f, 0.2f, 1211 + v), 0.5f);
    case Cue::Impact: return Mix(0.4f).add(0, wood(300 + 30 * float(v), 0.3f), 1).add(0.01f, coins(0.2f, 4, 1220 + v), 0.25f);
    case Cue::Hurt: return Mix(0.5f).add(0, creak(0.3f, 90, 200, {420, 1000}, 1230 + v), 0.6f).add(0.05f, call(0.35f, 80, 65, UH, OH, 1.3f, 0.9f, 0.5f, 0.2f, 1231 + v), 0.7f);
    case Cue::Die:
        return Mix(1.6f).add(0, crackle(0.3f, 1500, 1800, 1, 0.05f, 1240), 0.8f).add(0.05f, scatter(0.6f, 10, 250, 600, 1.5f, 1241, [](float f) { return wood(f, 0.2f); }), 0.8f)
            .add(0.2f, coins(1.0f, 22, 1242), 0.6f);
    default: return Mix(0.35f).add(0, wood(220, 0.2f), 0.8f).add(0, thud(110, 0.1f, 1250 + v), 0.5f).add(0.02f, coins(0.15f, 3, 1251 + v), 0.2f);
    }
}

// The Lich laughs in a cathedral of its own making.
Buf lich(Cue c, uint32_t v) {
    switch (c) {
    case Cue::Notice: {
        Mix m(1.6f);
        const float pitch[3] = {100, 94, 86};
        for (int i = 0; i < 3; i++) m.add(0.26f * float(i), comb(call(0.22f, pitch[i], pitch[i] * 0.95f, AH, AH, 1.2f, 0.5f, 0.35f, 0.2f, 1300 + v * 3 + uint32_t(i)), 6, 0.4f), 1);
        return m.add(0, choir(hz(38), 1.5f, OO, 0.4f, 0.8f, 1303 + v), 0.3f);
    }
    case Cue::Attack: return Mix(1.0f).add(0, choir(hz(50), 0.9f, AH, 0.3f, 0.4f, 1310 + v), 0.6f).add(0, choir(hz(51), 0.9f, AH, 0.3f, 0.4f, 1311 + v), 0.5f).add(0.1f, whisper(0.6f, 3, 1.1f, 1312 + v), 0.4f);
    case Cue::Impact: return Mix(0.6f).add(0, bone(2100, 0.1f), 0.9f).add(0, plate(1250, 0.5f), 0.3f).add(0, hiss(0.4f, 3200, 1320 + v), 0.4f);
    case Cue::Hurt: return comb(call(0.6f, 130, 95, AH, UH, 1.2f, 0.95f, 0.4f, 0.15f, 1330 + v), 4, 0.5f);
    case Cue::Die:
        return Mix(3.6f).add(0, call(2.4f, 140, 45, AH, OO, 1.2f, 0.9f, 0.6f, 0.15f, 1340), 1).add(0.2f, choir(hz(45), 2.8f, OO, 0.8f, 1.5f, 1341), 0.4f)
            .add(0.5f, toll(73.4f, 3.0f), 0.5f).add(1.0f, scatter(1.2f, 25, 1200, 2600, 2.0f, 1342, [](float f) { return bone(f); }), 0.5f);
    default: return Mix(0.4f).add(0, whoosh(0.35f, 0.7f, 1350 + v, [](float u) { return 450 + 250 * u; }, [](float u) { return arc(u, 0.5f); }), 0.6f).add(0.15f, bone(1900, 0.05f), 0.3f);
    }
}

// ---------- player and world ----------

Buf footstep(uint32_t v) {
    int theme = int(v) / STEP_VARIANTS;
    uint32_t k = v % STEP_VARIANTS;
    Noise r(v * 31 + 7);
    Mix m(0.4f);
    if (theme == 0) {  // crypt flagstones: a hard heel that rings faintly in the stone
        m.add(0, thud(r.range(70, 85), 0.12f, v), 0.8f).add(0.002f, stone(r.range(1600, 2200), 0.06f), 0.3f).add(0.005f, grit(0.07f, 2600, 1, 0.03f, 0.2f, v + 1), 0.25f);
        m.add(r.range(0.05f, 0.07f), thud(105, 0.07f, v + 2), 0.35f);
    } else if (theme == 1) {  // catacomb earth, with the odd bone shard underfoot
        m.add(0, thud(r.range(60, 72), 0.16f, v), 0.9f).add(0.01f, grit(0.18f, r.range(600, 900), 0.7f, 0.05f, 0.25f, v + 3), 0.5f);
        if (k % 2 == 0) m.add(0.03f, scatter(0.06f, 3, 1800, 2600, 1, v + 4, [](float f) { return bone(f, 0.04f); }), 0.25f);
    } else {  // ember ash that crunches, and now and then an iron grate
        m.add(0, thud(r.range(65, 80), 0.12f, v), 0.7f).add(0.004f, crackle(0.14f, 900, r.range(2200, 3200), 1.2f, 0.15f, v + 5), 0.4f).add(0.01f, grit(0.12f, 1400, 0.8f, 0.08f, 0.2f, v + 6), 0.3f);
        if (k == 3) m.add(0.004f, plate(r.range(380, 460), 0.4f), 0.25f);
    }
    return m;
}

Buf swing(uint32_t v) {
    float len = 0.25f + 0.03f * float(v);
    Buf b = make(len + 0.05f);
    put(b, 0, whoosh(len, 1.4f, 20 + v, [](float u) { return 380 + 2100 * arc(u, 0.45f); }, [](float u) { return arc(u, 0.5f); }), 0.8f);
    put(b, 0, whoosh(len, 0.7f, 30 + v, [](float u) { return 220 + 300 * arc(u); }, [](float u) { return arc(u, 0.45f); }), 0.45f);
    Buf blade = modal(2350 + 140 * float(v), len, {{1, 1, 3}, {1.47f, 0.7f, 4}, {2.23f, 0.5f, 5}});
    for (size_t i = 0; i < blade.size(); i++) blade[i] *= arc(float(i) / float(blade.size()), 0.55f);
    put(b, 0, blade, 0.15f);  // the edge sings faintly as it cuts the air
    return b;
}

// Your own grunts: short, breathy, never the same vowel twice in a row.
Buf playerHurt(uint32_t v) {
    const Formant vowels[3] = {UH, AH, EH};
    float f0 = 140 - 12 * float(v);
    return Mix(0.4f).add(0, thud(78, 0.15f, 100 + v), 0.6f).add(0.01f, call(0.28f, f0, f0 * 0.75f, vowels[v % 3], UH, 1.0f, 0.35f, 0.7f, 0.1f, 101 + v), 0.9f);
}

}  // namespace

Buf creature(int kind, Cue cue, uint32_t v) {
    switch (kind) {
    case 0: return rat(cue, v);
    case 1: return bat(cue, v);
    case 2: return skeleton(cue, v);
    case 3: return ooze(cue, v);
    case 4: return ghoul(cue, v);
    case 5: return spider(cue, v);
    case 6: return cultist(cue, v);
    case 7: return wraith(cue, v);
    case 8: return knight(cue, v);
    case 9: return ogre(cue, v);
    case 10: return imp(cue, v);
    case 11: return gargoyle(cue, v);
    case 12: return mimic(cue, v);
    default: return lich(cue, v);
    }
}

Spec creatureSpec(Cue cue) {
    switch (cue) {
    case Cue::Notice: return {0.55f, 0.45f, 2, 0.04f};
    case Cue::Attack: return {0.55f, 0.35f, 2, 0.04f};
    case Cue::Impact: return {0.75f, 0.3f, 3, 0.05f};
    case Cue::Hurt: return {0.6f, 0.4f, 2, 0.05f};
    case Cue::Die: return {0.65f, 0.5f, 1, 0.03f};
    default: return {0.3f, 0.25f, 3, 0.06f};
    }
}

Buf design(Sound s, uint32_t v) {
    switch (s) {
    case Sound::Step: return footstep(v);
    case Sound::Swing: return swing(v);
    case Sound::PlayerHurt: return playerHurt(v);
    // Generic creature sounds for anything that doesn't say which creature: a ghoul's.
    case Sound::Hit: return ghoul(Cue::Impact, v);
    case Sound::MonsterHurt: return ghoul(Cue::Hurt, v);
    case Sound::MonsterDie: return ghoul(Cue::Die, v);
    case Sound::Notice: return ghoul(Cue::Notice, v);
    case Sound::Growl: return ghoul(Cue::Attack, v);
    case Sound::Shuffle: return ghoul(Cue::Step, v);
    case Sound::Bump:
        return Mix(0.5f).add(0, thud(52, 0.3f, 1), 1.0f).add(0.004f, grit(0.12f, 900, 0.9f, 0.03f, 0.1f, 2), 0.35f).add(0.01f, stone(900, 0.15f), 0.2f)
            .add(0.05f, grit(0.45f, 3500, 0.7f, 0.004f, 0.05f, 3), 0.06f);  // dust trickles
    case Sound::Door:
        return Mix(1.45f).add(0, modal(1900, 0.15f, {{1, 1, 60}, {1.52f, 0.6f, 70}, {2.7f, 0.4f, 90}}), 0.25f)  // latch lifts
            .add(0.06f, creak(0.85f, 45, 140, {290, 690, 1450}, 110), 0.9f).add(0.92f, thud(62, 0.4f, 111), 0.85f)  // swings into the stop
            .add(0.92f, wood(180, 0.35f), 0.4f).add(0.93f, grit(0.3f, 1600, 0.8f, 0.02f, 0.1f, 112), 0.15f);
    case Sound::Pickup:  // into the pack: leather, a knock against what's already there, a buckle
        return Mix(0.45f).add(0, grit(0.22f, 2200, 0.6f, 0.03f, 0.3f, 123), 0.45f).add(0.08f, wood(520, 0.15f), 0.4f).add(0.12f, chainPing(3300), 0.2f)
            .add(0.06f, thud(140, 0.08f, 124), 0.3f);
    case Sound::Gold: {
        Mix m(0.7f);
        m.add(0, thud(110, 0.1f, 130), 0.35f);  // the pouch
        return m.add(0.01f, scatter(0.42f, 14, 2600, 4000, 1.4f, 131, coin), 0.8f);
    }
    case Sound::Drink: {
        Mix m(1.05f);
        m.add(0, bubble(520, 0.06f), 0.4f).add(0, click(0.003f, 140), 0.3f);  // the cork
        for (int i = 0; i < 3; i++) m.add(0.22f + float(i) * 0.24f, thud(90, 0.12f, 141 + uint32_t(i)), 0.5f).add(0.24f + float(i) * 0.24f, bubble(260 + 40 * float(i), 0.09f), 0.6f - 0.1f * float(i));
        return m;
    }
    case Sound::Scroll:  // dry parchment cracks open and the word is spoken under the breath
        return Mix(1.3f).add(0, crackle(0.45f, 600, 3800, 1.2f, 0.3f, 150), 0.5f).add(0.05f, grit(0.35f, 1600, 1.2f, 0.03f, 0.2f, 151), 0.25f)
            .add(0.35f, whisper(0.8f, 3, 0.95f, 152), 0.6f).add(0.4f, choir(hz(62), 0.8f, OO, 0.2f, 0.5f, 153), 0.2f);
    case Sound::Stairs: {
        Mix m(2.0f);
        for (int i = 0; i < 4; i++) m.add(float(i) * 0.3f, footstep(uint32_t(i)), 0.85f - float(i) * 0.12f);
        return m.add(0, whoosh(1.9f, 0.8f, 165, [](float u) { return 380 - 220 * u; }, [](float u) { return arc(u, 0.6f); }), 0.25f);
    }
    // Stingers quote the game's motif (see audio_music.cpp).
    case Sound::LevelUp:
        return Mix(2.6f).add(0, phrase(Harp, "d4:.25 f#4:.25 a4:.25 d5:.25 f#5:.25 a5:.25 d6:2", 120), 0.7f).add(0.75f, phrase(MusicBox, "a5:.5 d6:2", 120), 0.5f)
            .add(0, phrase(Strings, "d3+a3+f#4:4", 120), 0.25f);
    case Sound::Death:
        return Mix(6.4f).add(0, toll(110, 4.0f), 0.8f).add(0.2f, phrase(Organ, "d4:1.5 c#4:.5 d4:4", 50), 0.6f).add(0.2f, phrase(Organ, "d3+f3+a3:6", 50), 0.35f)
            .add(2.4f, toll(110, 4.0f), 0.55f).add(0.4f, phrase(Choir, "f3:2 e3:2 d3:4", 50), 0.3f);
    case Sound::Victory:
        return Mix(5.0f).add(0, phrase(Brass, "a3:.5 d4:.5 f#4:.5 a4:2.5 g4:.5 f#4:.5 e4:.5 d4:3", 96), 0.6f).add(0, phrase(Timpani, "d2:.5 d2:.5 a1:.5 d2:2.5 .:1.5 a1:.5 d2:3", 96), 0.6f)
            .add(2.5f, phrase(Choir, "d3+f#3+a3:5", 96), 0.35f).add(2.5f, phrase(Bell, "d4:4", 96), 0.4f);
    default: break;
    }
    switch (s) {
    case Sound::Bolt:
        return Mix(0.8f).add(0, whoosh(0.6f, 1.2f, 200, [](float u) { return 600 + 1200 * arc(u, 0.3f); }, [](float u) { return arc(u, 0.2f); }), 0.6f)
            .add(0.02f, crackle(0.5f, 160, 3200, 1.0f, 0.2f, 201), 0.3f).add(0, thud(72, 0.15f, 202), 0.3f).add(0, fm(330, 0.6f, 1.41f, 3, 4, 2), 0.3f);
    case Sound::MenuMove: return Mix(0.1f).add(0, wood(1400, 0.06f), 0.8f);
    case Sound::MenuSelect: return Mix(0.9f).add(0, wood(700, 0.1f), 0.5f).add(0, phrase(Harp, "d4:.15 a4:.15 d5:1", 120), 0.6f);
    case Sound::Trap: {
        Osc a, c;
        return Mix(1.0f).add(0, modal(3300, 0.08f, {{1, 1, 120}, {1.6f, 0.6f, 140}}), 0.35f)  // trigger clicks
            .add(0.01f, render(0.7f, [&](float t) {  // the spring lets go
                float f = 240 * (1 + 0.3f * std::exp(-t * 7) * std::sin(TAU * 16 * t));
                return (a.sine(f) + 0.3f * c.sine(f * 2.7f)) * ad(t, 0.002f, 5);
            }), 0.5f)
            .add(0.06f, plate(520, 0.7f), 0.55f).add(0.06f, thud(90, 0.15f, 230), 0.4f).add(0.06f, click(0.004f, 231), 0.3f);
    }
    case Sound::Chest:
        return Mix(1.15f).add(0, modal(3100, 0.12f, {{1, 1, 80}, {1.38f, 0.7f, 90}, {2.05f, 0.5f, 110}}), 0.35f)  // latch flicks up
            .add(0.05f, modal(2650, 0.12f, {{1, 1, 90}, {1.45f, 0.6f, 100}}), 0.25f).add(0.05f, thud(150, 0.06f, 240), 0.25f)
            .add(0.16f, creak(0.6f, 90, 240, {520, 1180, 2300}, 241), 0.5f).add(0.78f, thud(95, 0.25f, 242), 0.7f).add(0.78f, wood(240, 0.3f), 0.35f);  // lid falls back
    case Sound::Fountain: {
        Mix m(1.5f);
        m.add(0, whoosh(0.5f, 0.7f, 250, [](float u) { return 2600 - 1200 * u; }, [](float u) { return arc(u, 0.05f); }), 0.6f);
        m.add(0, scatter(0.35f, 40, 600, 2200, 1.5f, 251, [](float f) { return bubble(f, 0.05f); }), 0.5f);
        m.add(0.4f, scatter(0.95f, 14, 900, 1900, 1, 252, [](float f) { return bubble(f, 0.04f); }), 0.2f);
        return m.add(0.75f, thud(90, 0.12f, 253), 0.35f).add(0.77f, bubble(280, 0.09f), 0.4f);  // a gulp
    }
    case Sound::Zap: {
        Osc sweep, buzz;
        Noise r(260);
        Svf bp;
        float f = 120;
        return Mix(0.9f).add(0, render(0.15f, [&](float t) { return sweep.sine(400 + 2800 * std::exp(-t * 25)) * ad(t, 0.002f, 20); }), 0.4f)
            .add(0.02f, render(0.65f, [&](float t) {  // arcing current: a saw that jumps pitch
                if (r.uniform() < 40.0f / SR) f = r.range(70, 220);
                return (bp.bp(buzz.saw(f), 1500, 0.7f) + buzz.saw(f) * 0.3f) * arc(t / 0.65f, 0.1f) * (0.6f + 0.4f * r.next());
            }), 0.55f)
            .add(0, crackle(0.75f, 400, 3500, 0.8f, 0.1f, 261), 0.5f);
    }
    case Sound::Summon:  // the floor groans and a choir rises out of it
        return Mix(1.9f).add(0, whoosh(1.15f, 2.5f, 270, [](float u) { return 200 * std::pow(14.0f, u); }, [](float u) { return u * u * std::min(1.0f, (1 - u) * 20); }), 0.4f)
            .add(0, choir(hz(43), 1.2f, OO, 0.9f, 0.1f, 271), 0.4f).add(0, choir(hz(44), 1.2f, OO, 0.9f, 0.1f, 272), 0.35f).add(0.1f, grind(1.0f, 9, 273), 0.3f)
            .add(1.12f, thud(42, 0.7f, 274), 1.0f);
    case Sound::Secret:
        return Mix(2.4f).add(0, grind(1.5f, 14, 280), 0.75f).add(0, creak(1.5f, 10, 26, {130, 340, 760}, 281), 0.35f).add(1.48f, thud(46, 0.6f, 282), 1.0f)
            .add(1.48f, scatter(0.6f, 10, 1200, 2400, 1.5f, 283, [](float f) { return stone(f, 0.08f); }), 0.25f);  // pebbles
    case Sound::Fire: {
        Noise n(290);
        Svf lp;
        return Mix(1.8f).add(0, thud(45, 0.6f, 291), 0.7f).add(0, render(1.6f, [&](float t) { return lp.lp(n.next(), 250 + 3200 * ad(t, 0.04f, 3), 0.9f) * ad(t, 0.035f, 2.2f); }), 1.0f)
            .add(0.05f, crackle(1.6f, 120, 2200, 1.0f, 0.1f, 292), 0.35f);
    }
    case Sound::Fall:
        return Mix(2.0f).add(0, whoosh(1.02f, 1.1f, 300, [](float u) { return 250 * std::pow(6.0f, u); },
                                       [](float u) { return std::pow(u, 1.5f) * std::min(1.0f, (1 - u) * 30) * (0.8f + 0.2f * std::sin(TAU * 11 * u)); }), 0.75f)
            .add(1.0f, thud(44, 0.55f, 301), 1.0f).add(1.0f, crunch(0.15f, 700, 302), 0.45f).add(1.05f, playerHurt(1), 0.5f);
    case Sound::Teleport: {
        Osc o[6];
        const float ratio[6] = {1, 1.5f, 2.01f, 2.99f, 4.02f, 5.03f};
        return Mix(1.3f).add(0, render(1.0f, [&](float t) {  // a cluster of partials gliding upward
                                 float s = 0;
                                 for (int i = 0; i < 6; i++) s += o[i].sine(180 * ratio[i] * std::pow(4.0f, t)) / (1 + float(i) * 0.4f);
                                 return s * arc(t, 0.6f);
                             }), 0.5f)
            .add(0, whoosh(1.0f, 2.0f, 310, [](float u) { return 400 * std::pow(8.0f, u); }, [](float u) { return arc(u, 0.7f); }), 0.35f).add(0.95f, thud(220, 0.12f, 311), 0.35f);
    }
    case Sound::TrapFound:  // a mechanism clicks, and a low plucked minor second
        return Mix(1.0f).add(0, modal(2400, 0.06f, {{1, 1, 140}, {1.7f, 0.5f, 160}}), 0.3f).add(0.09f, modal(2150, 0.06f, {{1, 1, 140}, {1.7f, 0.5f, 160}}), 0.25f)
            .add(0.12f, phrase(Bass, "d3+eb3:2", 120), 0.6f);
    case Sound::Equip:
        return Mix(0.7f).add(0, grit(0.25f, 2200, 0.7f, 0.04f, 0.3f, 320), 0.25f).add(0.03f, plate(430, 0.5f), 0.45f).add(0.03f, click(0.004f, 321), 0.3f)
            .add(0.03f, thud(120, 0.1f, 322), 0.3f).add(0.17f, scatter(0.2f, 4, 3200, 4600, 1, 323, chainPing), 0.3f);
    case Sound::Alarm: {
        Mix m(1.6f);
        for (int i = 0; i < 7; i++) {  // a handbell swung hard, the clapper striking each side in turn
            float side = i % 2 ? 1.03f : 1.0f;
            m.add(float(i) * 0.15f, modal(1150 * side, 0.6f, {{1, 1, 7}, {1.59f, 0.8f, 9}, {2.14f, 0.6f, 11}, {2.83f, 0.5f, 14}, {3.6f, 0.3f, 18}}), 1.0f - 0.08f * float(i));
            m.add(float(i) * 0.15f, click(0.003f, 340 + uint32_t(i)), 0.3f);
        }
        return m;
    }
    default: break;
    }
    return make(0.1f);
}

Spec specOf(Sound s) {
    switch (s) {
    case Sound::Step: return {0.5f, 0.22f, 3 * STEP_VARIANTS, 0.03f};
    case Sound::Bump: return {0.6f, 0.25f, 1, 0.04f};
    case Sound::Swing: return {0.45f, 0.3f, 2, 0.03f};
    case Sound::Hit: return {0.8f, 0.3f, 3, 0.04f};
    case Sound::MonsterHurt: return {0.55f, 0.4f, 2, 0.04f};
    case Sound::MonsterDie: return {0.6f, 0.45f, 1, 0.03f};
    case Sound::PlayerHurt: return {0.65f, 0.3f, 3, 0.04f};
    case Sound::Door: return {0.55f, 0.45f};
    case Sound::Pickup: return {0.4f, 0.25f, 1, 0.03f};
    case Sound::Gold: return {0.35f, 0.3f, 1, 0.04f};
    case Sound::Drink: return {0.45f, 0.25f};
    case Sound::Scroll: return {0.45f, 0.5f};
    case Sound::Stairs: return {0.5f, 0.5f};
    case Sound::LevelUp: return {0.45f, 0.55f};
    case Sound::Death: return {0.7f, 0.6f};
    case Sound::Notice: return {0.55f, 0.5f, 1, 0.03f};
    case Sound::Bolt: return {0.5f, 0.45f, 1, 0.03f};
    case Sound::Victory: return {0.6f, 0.55f};
    case Sound::MenuMove: return {0.25f, 0.1f};
    case Sound::MenuSelect: return {0.35f, 0.3f};
    case Sound::Trap: return {0.6f, 0.4f};
    case Sound::Chest: return {0.55f, 0.4f};
    case Sound::Fountain: return {0.5f, 0.45f};
    case Sound::Zap: return {0.5f, 0.4f};
    case Sound::Summon: return {0.6f, 0.6f};
    case Sound::Secret: return {0.65f, 0.5f};
    case Sound::Fire: return {0.75f, 0.45f};
    case Sound::Fall: return {0.75f, 0.4f};
    case Sound::Teleport: return {0.5f, 0.55f};
    case Sound::TrapFound: return {0.45f, 0.45f};
    case Sound::Growl: return {0.55f, 0.4f, 2, 0.04f};
    case Sound::Equip: return {0.45f, 0.3f};
    case Sound::Alarm: return {0.55f, 0.5f};
    case Sound::Shuffle: return {0.3f, 0.3f, 3, 0.05f};
    case Sound::Count: break;
    }
    return {0.5f, 0.3f};
}

// ---------- ambience beds: no wind down here, only the earth, water and fire ----------

Ambience ambience(int theme) {
    Ambience a;
    Buf& b = a.texture;
    if (theme == 0) {  // crypt: the still weight of stone, a low drone, drips in far corners
        Noise n(400), r(401);
        Svf air;
        put(b, 0, bed(AMB_LEN, [&](float) { return air.lp(n.next(), 160); }), 0.035f);
        float root = loopHz(55, AMB_LEN), fifth = loopHz(82.4f, AMB_LEN), beat = loopHz(110.25f, AMB_LEN);
        Osc o[3];
        for (size_t i = 0; i < b.size(); i++) {
            float p = float(i) / float(b.size());
            b[i] += (o[0].sine(root) + 0.5f * o[1].sine(fifth) * (0.6f + 0.4f * std::sin(TAU * 3 * p)) + 0.25f * o[2].sine(beat)) * 0.014f;
        }
        for (float at = r.range(0.5f, 2); at < EVENTS_LEN; at += r.range(1.8f, 4.2f)) {
            Buf d = bubble(r.range(1300, 2400), r.range(0.04f, 0.06f));
            float g = r.range(0.12f, 0.28f);
            for (int echo = 0; echo < 3; echo++) put(a.events, at + 0.11f * float(echo), d, g * std::pow(0.38f, float(echo)), true);
        }
    } else if (theme == 1) {  // catacombs: deep earth, distant bones settling, grit trickling
        Noise n(410), r(411);
        Svf earth;
        put(b, 0, bed(AMB_LEN, [&](float) { return earth.lp(n.next(), 70, 0.8f); }), 0.04f);
        Osc o[2];
        float root = loopHz(49, AMB_LEN), fifth = loopHz(73.5f, AMB_LEN);
        for (float& x : b) x += (o[0].sine(root) + 0.4f * o[1].sine(fifth)) * 0.01f;
        for (float at = r.range(1, 3); at < EVENTS_LEN; at += r.range(3.5f, 6.5f)) {
            int clicks = 4 + int(r.uniform() * 6);
            float spread = r.range(0.25f, 0.5f), g = r.range(0.08f, 0.16f);
            for (int i = 0; i < clicks; i++) {
                Buf knock = bone(r.range(900, 2300), 0.08f);
                float when = at + spread * std::pow(r.uniform(), 1.4f);
                put(a.events, when, knock, g * r.range(0.4f, 1), true);
                put(a.events, when + 0.17f, knock, g * 0.3f, true);
            }
        }
        for (int i = 0; i < 5; i++) put(a.events, r.range(0, EVENTS_LEN), grit(0.6f, 3500, 0.7f, 0.01f, 0.1f, 412 + uint32_t(i)), 0.03f, true);
    } else {  // ember halls: the furnace below, steam venting, embers crackling, molten blorps
        Noise n(420), r(421);
        Svf rumble, roar;
        OnePole steam;
        put(b, 0, bed(AMB_LEN, [&](float t) { return rumble.lp(n.next(), 55, 0.9f) * (0.7f + 0.3f * std::sin(TAU * 3 * t / AMB_LEN)); }), 0.07f);
        put(b, 0, bed(AMB_LEN, [&](float t) { return roar.lp(n.next(), 320, 1.2f) * (0.6f + 0.4f * std::sin(TAU * 2 * t / AMB_LEN + 2)); }), 0.03f);
        put(b, 0, bed(AMB_LEN, [&](float t) {
                float vent = std::pow(0.5f + 0.5f * std::sin(TAU * 3 * t / AMB_LEN + 0.5f), 6.0f);
                return steam.hp(n.next(), corner(3000)) * (0.2f + 2 * vent);
            }), 0.012f);
        for (int burst = 0; burst < 26; burst++) {
            float at = r.range(0, EVENTS_LEN), len = r.range(0.4f, 1.6f);
            put(a.events, at, crackle(len, r.range(20, 60), r.range(1800, 3600), 0.8f, 0.3f, 422 + uint32_t(burst)), r.range(0.15f, 0.3f), true);
        }
        for (int i = 0; i < 7; i++) put(a.events, r.range(0, EVENTS_LEN), bubble(r.range(80, 150), 0.25f), 0.08f, true);
        Osc o[2];
        float root = loopHz(41.2f, AMB_LEN), fifth = loopHz(61.7f, AMB_LEN);
        for (float& x : b) x += (o[0].sine(root) + 0.5f * o[1].sine(fifth)) * 0.015f;
    }
    return a;
}

}  // namespace synth
