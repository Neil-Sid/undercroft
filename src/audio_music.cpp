// The music. Like a PS1 sound bank, each instrument is a single sampled note that the sequencer
// pitches by resampling (sustained ones loop while held), all bathed in the SPU-style hall.
//
// Notation: a note is a letter, an optional # or b and an octave ("d4", "c#3", "bb2"); notes
// joined with '+' sound together; ":n" gives a length in beats (default 1); "." is a rest;
// '|' bar lines are ignored.
//
// The pieces share one motif, D-A-F-E-D-C#-D, rising to A over B-flat: the title states it, the
// crypt answers it, and the stingers quote it.
#include "synth.h"
#include <cctype>
#include <string>

namespace synth {
namespace {

Instrument table[INSTRUMENTS];

// Lets a held note loop the stretch [a, b) seconds: its end is crossfaded with what precedes `a`,
// so jumping back is seamless.
void sustain(Instrument& ins, float a, float b) {
    size_t s = size_t(a * SR), e = size_t(b * SR), fade = size_t(0.25f * SR);
    for (size_t i = 0; i < fade; i++) {
        float k = float(i) / float(fade);
        ins.sample[e - fade + i] = ins.sample[e - fade + i] * (1 - k) + ins.sample[s - fade + i] * k;
    }
    ins.loopStart = s, ins.loopEnd = e;
}

Instrument sampled(Buf sample, float base, float send, float release = 0.4f) { return {normalized(std::move(sample), 0.8f), base, send, release}; }

int midiOf(const std::string& p) {
    static const int semitone[7] = {9, 11, 0, 2, 4, 5, 7};  // a b c d e f g
    int n = semitone[p[0] - 'a'];
    size_t k = 1;
    if (k < p.size() && p[k] == '#') n++, k++;
    else if (k + 1 < p.size() && p[k] == 'b') n--, k++;
    return (std::stoi(p.substr(k)) + 1) * 12 + n;
}

std::vector<Note> parse(const std::string& s) {
    std::vector<Note> out;
    float beat = 0;
    size_t i = 0;
    while (i < s.size()) {
        if (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == '|') {
            i++;
            continue;
        }
        size_t j = i;
        while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) j++;
        std::string token = s.substr(i, j - i);
        i = j;
        float len = 1;
        if (size_t colon = token.find(':'); colon != std::string::npos) len = std::stof(token.substr(colon + 1)), token.resize(colon);
        for (size_t start = 0; token != "." && start < token.size();) {
            size_t plus = token.find('+', start);
            out.push_back({beat, len, float(midiOf(token.substr(start, plus - start)))});
            start = plus == std::string::npos ? token.size() : plus + 1;
        }
        beat += len;
    }
    return out;
}

std::string rep(const std::string& s, int n) {
    std::string out;
    for (int i = 0; i < n; i++) out += s + " ";
    return out;
}

Part part(int inst, float gain, float pan, std::initializer_list<std::string> phrases) {
    Part p{inst, gain, pan, {}};
    for (const std::string& s : phrases) p.phrases.push_back(parse(s));
    return p;
}

// Plays one note of an instrument into `out` at `at` seconds, held for `seconds`.
void renderNote(Buf& out, float at, const Instrument& ins, float midi, float seconds, float gain) {
    double step = std::pow(2.0, (double(midi) - ins.baseMidi) / 12), pos = 0;
    size_t start = size_t(at * SR), held = size_t(seconds * SR), release = size_t(ins.release * SR);
    bool loops = ins.loopEnd > 0;
    for (size_t i = 0; start + i < out.size(); i++) {
        if (loops && i < held && pos >= double(ins.loopEnd)) pos -= double(ins.loopEnd - ins.loopStart);
        if (pos >= double(ins.sample.size() - 1)) break;
        float fade = loops && i >= held ? 1 - float(i - held) / float(release) : 1;
        if (fade <= 0) break;
        size_t k = size_t(pos);
        float f = float(pos - double(k)), x = ins.sample[k] * (1 - f) + ins.sample[k + 1] * f;
        out[start + i] += x * gain * fade;
        pos += step;
    }
}

// ---------- the pieces ----------

const std::string MOTIF = "d5 a4 f4 e4 | d4:1.5 c#4:.5 d4:2 | f4 g4 a4 bb4 | a4:3 .";  // over Dm Dm Bb A
const std::string ANSWER = "g4 d5 bb4 a4 | g4:1.5 f#4:.5 g4:2 | bb4 a4 g4 f4 | e4:3 .";  // over Gm Gm Bb A

const std::string DM = "d4:.5 a4:.5 d5:.5 f5:.5 a5:.5 f5:.5 d5:.5 a4:.5 ", BB = "bb3:.5 f4:.5 bb4:.5 d5:.5 f5:.5 d5:.5 bb4:.5 f4:.5 ",
                  AM = "a3:.5 e4:.5 a4:.5 c#5:.5 e5:.5 c#5:.5 a4:.5 e4:.5 ", GM = "g3:.5 d4:.5 g4:.5 bb4:.5 d5:.5 bb4:.5 g4:.5 d4:.5 ",
                  EB = "eb4:.5 bb4:.5 eb5:.5 g5:.5 bb5:.5 g5:.5 eb5:.5 bb4:.5 ";

// Crypt, depths 1-3: D minor, i-VI-V / iv-VI-V. Strings and plucked bass, a harp that joins
// halfway, the motif on a music box between long silences, a tubular bell marking the loop.
Song crypt() {
    std::string cycle = "d3+f3+a3:8 bb2+d3+f3:4 a2+c#3+e3:4 g2+bb2+d3:8 bb2+d3+f3:4 a2+c#3+e3:4 ";
    std::string bass = "d2:2 a1:2 d2:2 a1:2 bb1:2 f2:2 a1:2 e2:2 g1:2 d2:2 g1:2 d2:2 bb1:2 f2:2 a1:2 a1:2 ";
    std::string harp = DM + DM + BB + AM + GM + GM + BB + AM;
    return {66, 128,
            {part(Strings, 0.3f, 0, {rep(cycle, 4)}), part(Bass, 0.42f, -0.15f, {rep(bass, 4)}),
             part(Harp, 0.22f, 0.35f, {".:64 " + rep(harp, 2), rep(harp, 4)}),
             part(MusicBox, 0.36f, -0.3f, {".:32 " + MOTIF + " .:32 " + ANSWER + " .:32", ".:64 " + MOTIF + " .:48"}), part(Bell, 0.26f, 0.1f, {"d4:64 a3:64"})}};
}

// Catacombs, depths 4-6: E Phrygian. A low choir holds the drone and leans on the flat second;
// a heartbeat on a frame drum; a bone flute sings between the bars of silence.
Song catacomb() {
    std::string A = "e4:1.5 f4:.5 g4:2 f4 e4 .:2 b3:1.5 c4:.5 d4:2 c4 b3:3", B = "a4:2 g4 f4 e4:2 f4:2 e4:1.5 d4:.5 c4:2 b3:4",
                C = "f4:3 e4 d4:2 c4:2 b3:2 c4 b3 e3:4";
    std::string heart = rep("c3:.5 c3:3.5", 3) + " .:4 ";
    return {56, 96,
            {part(Choir, 0.3f, 0, {"e3+b3:16 e3+b3:16 f3+c4:8 e3+b3:8 e3+b3:16 d3+a3:8 c3+g3:8 f3+c4:8 e3+b3:8"}),
             part(Strings, 0.2f, 0, {"e2:32 f2:8 e2:8 e2:16 d2:8 c2:8 f2:8 e2:8"}), part(Taiko, 0.28f, -0.1f, {rep(heart, 6)}),
             part(Flute, 0.34f, 0.2f, {".:16 " + A + " .:16 " + B + " .:16 " + C, ".:16 " + B + " .:16 " + A + " .:16 " + C}),
             part(Harp, 0.2f, -0.35f, {".:6 e5:2 .:12 b4:4 .:8 f5:4 .:20 e5:4 .:16 b4:4 .:16"})}};
}

// Ember halls, depths 7-9: C Phrygian, driving. Taiko ostinato with a fill every fourth bar,
// pulsing bass, low brass stabs over an organ drone, a gong at the top, the strings' lament.
Song ember() {
    std::string bar = "c2:1.5 c2:1 g2:.5 c2:1 ", fill = "c2:.5 c2:.5 g2:.5 g2:.5 c2:.5 g2:.5 c3:1 ";
    std::string bass = rep("c2:.5", 64) + rep("db2:.5", 16) + rep("c2:.5", 16) + rep("bb1:.5", 16) + rep("ab1:.5", 8) + rep("g1:.5", 8);
    std::string stabs = "c3+eb3+g3:1 .:7 c3+eb3+g3:1 .:7 c3+eb3+g3:1 .:7 c3+eb3+g3:.5 c3+eb3+g3:.5 .:7 db3+f3+ab3:1 .:7 c3+eb3+g3:1 .:7 "
                        "bb2+db3+f3:1 .:7 ab2+c3+eb3:1 .:3 g2+b2+d3:2 .:2";
    std::string mel1 = "g4:1.5 ab4:.5 g4 f4 eb4:1.5 db4:.5 c4:2 eb4 f4 g4 ab4 bb4:1.5 ab4:.5 g4:2", mel2 = "c5:2 bb4 ab4 g4:3 f4 eb4 f4 db4:2 c4:4";
    return {84, 64,
            {part(Taiko, 0.5f, 0, {rep(rep(bar, 3) + fill, 4)}), part(Bass, 0.3f, -0.15f, {bass}),
             part(Organ, 0.2f, 0, {"c2+g2:32 db2+ab2:8 c2+g2:8 bb1+f2:8 ab1+eb2:4 g1+d2:4"}), part(Brass, 0.3f, 0.2f, {stabs}),
             part(Strings, 0.3f, -0.2f, {".:16 " + mel1 + " .:16 " + mel2, ".:16 " + mel1 + " .:32"}), part(Gong, 0.3f, 0.1f, {"c3:64"})}};
}

// The Lich's floor: the Dies Irae on organ, then again an octave down, over a choir that slides
// into a tritone. Timpani, a pedal in the low strings, a bell tolling D and then G-sharp.
Song lair() {
    std::string dies = "f4 e4 f4 d4 e4 c4 d4:2 f4 f4 g4 f4 e4 d4:3 c4 d4 e4 c4 d4:2 d4:2 .:8";
    std::string low = "f3 e3 f3 d3 e3 c3 d3:2 f3 f3 g3 f3 e3 d3:3 c3 d3 e3 c3 d3:2 d3:2 .:8";
    return {52, 64,
            {part(Organ, 0.38f, 0, {dies + " " + low}),
             part(Choir, 0.3f, 0, {"d3+a3:8 bb2+f3:8 g2+d3:8 a2+e3:4 g#2+d3:4 d3+f3+a3:8 c3+g3:8 bb2+f3:8 a2+c#3+e3:8"}),
             part(Strings, 0.22f, 0, {"d2:32 d2:16 a1:16"}),
             part(Timpani, 0.45f, -0.1f, {"d2:8 d2:8 a1:8 d2:4 a1:4 d2:8 d2:8 a1:8 d2:.5 d2:.5 d2:.5 d2:.5 a1:.5 a1:.5 a1:.5 a1:.5 d2:4"}),
             part(Bell, 0.3f, 0.15f, {"d3:32 g#2:32"})}};
}

// Title: the motif on a music box over the organ, then the flute takes it while the harp
// arpeggiates the Neapolitan turn (E-flat) that closes each phrase.
Song title() {
    std::string cycle = "d3+f3+a3:8 bb2+d3+f3:4 a2+c#3+e3:4 g2+bb2+d3:8 eb3+g3+bb3:4 a2+c#3+e3:4 ";
    std::string answer = "g4 d5 bb4 a4 | g4:1.5 f#4:.5 g4:2 | eb5 d5 bb4 g4 | a4:3 .";
    return {60, 64,
            {part(Organ, 0.26f, 0, {rep(cycle, 2)}), part(MusicBox, 0.4f, -0.25f, {MOTIF + " " + answer + " .:32"}),
             part(Flute, 0.3f, 0.25f, {".:32 " + MOTIF + " " + answer}), part(Harp, 0.22f, 0.35f, {".:32 " + DM + DM + BB + AM + GM + GM + EB + AM}),
             part(Bell, 0.3f, 0.1f, {"d3:32 a2:32"})}};
}

}  // namespace

std::vector<std::function<void()>> instrumentJobs() {
    return {
        [] { table[Harp] = sampled(pluck(hz(60), 3.0f, 0.5f, 2.2f, 1), 60, 0.5f); },
        [] {
            Buf b = fm(hz(72), 2.6f, 3.5f, 1.4f, 1.8f, 7);
            put(b, 0, click(0.004f, 2), 0.15f);  // the comb's tine is plucked
            table[MusicBox] = sampled(std::move(b), 72, 0.6f);
        },
        [] { table[Bell] = sampled(modal(hz(60), 5, {{1, 1, 0.9f}, {2.76f, 0.45f, 1.6f}, {5.4f, 0.2f, 3}, {8.93f, 0.08f, 5}}), 60, 0.7f); },
        [] { table[Organ] = sampled(organ(hz(48), 4, 3), 48, 0.6f, 0.35f), sustain(table[Organ], 1.0f, 3.7f); },
        [] { table[Choir] = sampled(choir(hz(52), 4, AH, 0.5f, 0.05f, 4), 52, 0.7f, 0.9f), sustain(table[Choir], 1.2f, 3.8f); },
        [] { table[Strings] = sampled(pad(hz(48), 4, 3.2f, 0.5f, 0.05f, 5), 48, 0.6f, 0.8f), sustain(table[Strings], 1.2f, 3.8f); },
        [] { table[Flute] = sampled(flute(hz(67), 3, 6), 67, 0.55f, 0.2f), sustain(table[Flute], 1.0f, 2.8f); },
        [] { table[Bass] = sampled(pluck(hz(36), 3.0f, 0.22f, 1.6f, 7), 36, 0.3f); },
        [] { table[Timpani] = sampled(drum(hz(41), 2.6f, 0.35f, 8), 41, 0.45f); },
        [] { table[Taiko] = sampled(drum(hz(36), 1.8f, 1.0f, 9), 36, 0.4f); },
        [] { table[Gong] = sampled(gong(hz(48), 7), 48, 0.7f); },
        [] { table[Brass] = sampled(brass(hz(55), 3.4f), 55, 0.45f, 0.15f), sustain(table[Brass], 1.0f, 3.2f); },
    };
}

const Instrument& instrument(int i) { return table[i]; }

const Song& song(int mood) {
    static const Song songs[SONGS] = {crypt(), catacomb(), ember(), lair(), title()};
    return songs[std::clamp(mood, 0, SONGS - 1)];
}

Buf phrase(int inst, const char* notes, float bpm) {
    std::vector<Note> score = parse(notes);
    float beats = 0;
    for (const Note& n : score) beats = std::max(beats, n.beat + n.beats);
    const Instrument& ins = table[inst];
    float spb = 60 / bpm;
    Buf b = make(beats * spb + float(ins.sample.size()) / SR + ins.release);
    for (const Note& n : score) renderNote(b, n.beat * spb, ins, n.midi, n.beats * spb, 1);
    return normalized(std::move(b));
}

Buf bounce(const Song& s) {
    float spb = 60 / s.bpm;
    Buf b = make(s.beats * spb);
    for (const Part& p : s.parts)
        for (const Note& n : p.phrases[0]) renderNote(b, n.beat * spb, table[p.instrument], n.midi, n.beats * spb, p.gain);
    return b;
}

void Sequencer::start(const Song* s, Noise& rng) {
    song = s, beat = 0;
    if (!s) return;
    chosen.assign(s->parts.size(), 0), next.assign(s->parts.size(), 0);
    for (size_t p = 0; p < s->parts.size(); p++) chosen[p] = std::min(int(s->parts[p].phrases.size()) - 1, int(rng.uniform() * float(s->parts[p].phrases.size())));
}

void Sequencer::advance(int frames, Noise& rng, const std::function<void(const Part&, const Note&, int, int)>& on) {
    if (!song) return;
    double perFrame = song->bpm / 60.0 / RATE;
    int done = 0;
    while (done < frames) {
        double end = beat + double(frames - done) * perFrame, stop = std::min(end, double(song->beats));
        for (size_t p = 0; p < song->parts.size(); p++) {
            const std::vector<Note>& notes = song->parts[p].phrases[size_t(chosen[p])];
            while (next[p] < notes.size() && notes[next[p]].beat < stop) {
                const Note& n = notes[next[p]++];
                int offset = std::clamp(done + int((n.beat - beat) / perFrame), done, frames - 1);
                on(song->parts[p], n, offset, int(n.beats / perFrame));
            }
        }
        if (end < song->beats) {
            beat = end;
            return;
        }
        done += std::max(1, int((song->beats - beat) / perFrame));
        start(song, rng);  // around again, with fresh choices of phrase
    }
}

}  // namespace synth
