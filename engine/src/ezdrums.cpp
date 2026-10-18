// EZDrums — built-in drum machine. See ezdrums.h for the mission brief.
//
// Design notes:
//  · Drums are SYNTHESIZED (808/909-style analog circuits: pitch-swept
//    sines, filtered noise, inharmonic square banks) — zero sample
//    assets, deterministic, and every kit is just a parameter table.
//  · The sequencer is sample-locked to the host transport through the
//    STANDARD AudioPlayHead (bpm, time signature, position): patterns
//    loop in perfect sync with the project, in the project's or an
//    overridden time signature, with swing.
//  · Every note the machine "performs" is stashed and handed to the host
//    through MidiGenerator::drainGeneratedMidi — while the transport
//    records an armed track, the engine writes those events into the
//    track's MIDI take, producing a normal (piano-roll editable) clip.
#include "ezdrums.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

namespace ezdrums {
namespace {

static juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

// ---- GM drum mapping: the 9 GRID lanes (the MIDI notes are GM) -----------
enum Lane
{
    kKick = 0,
    kSnare,
    kClap,
    kHatClosed,
    kHatOpen,
    kTomLow,
    kTomHigh,
    kRide,
    kCrash,
    kLaneCount
};

struct LaneInfo
{
    const char* name;
    std::uint8_t note;
};

constexpr LaneInfo kLanes[kLaneCount] = {
    { "Bombo", 36 },   { "Caja", 38 },    { "Palmas", 39 },
    { "Hi-Hat", 42 },  { "HH Abierto", 46 }, { "Tom grave", 41 },
    { "Tom agudo", 48 }, { "Ride", 51 },  { "Crash", 49 },
};

int laneForNote(const int note)
{
    for (int i = 0; i < kLaneCount; ++i)
        if (kLanes[i].note == note)
            return i;
    return -1;
}

// ---- Kits: per-drum tuning tables ----------------------------------------
// tune in semitones, decay scale (1 = natural), tone 0..1 (dark..bright),
// drive = saturation amount per kit (the kit CHARACTER, not a trim).
struct DrumTuning
{
    float tune, decay, tone;
};

struct Kit
{
    const char* name;
    DrumTuning d[kLaneCount];
    float drive;   // 1 = clean, up to ~4 = crushed
    float click;   // transient click amount on kick/snare
};

// The kits must sound WILDLY different, not like EQ presets of one kit.
constexpr Kit kKits[] = {
    { "808 Clásica",
      { { 0.f, 1.35f, 0.30f }, { 0.f, 1.0f, 0.35f }, { 0.f, 0.9f, 0.5f },
        { 0.f, 1.0f, 0.65f }, { 0.f, 1.1f, 0.65f }, { -1.f, 1.3f, 0.30f },
        { 0.f, 1.2f, 0.35f }, { 0.f, 1.0f, 0.5f }, { 0.f, 1.2f, 0.4f } },
      1.6f, 0.4f },
    { "909",
      { { +1.f, 0.55f, 0.95f }, { +1.f, 0.5f, 1.0f }, { 0.f, 0.7f, 0.85f },
        { +1.f, 0.6f, 1.0f }, { +1.f, 0.7f, 1.0f }, { 0.f, 0.6f, 0.7f },
        { +1.f, 0.6f, 0.75f }, { 0.f, 0.6f, 0.85f }, { 0.f, 0.7f, 0.8f } },
      2.4f, 1.0f },
    { "Acústica",
      { { -3.f, 1.8f, 0.18f }, { -1.f, 1.5f, 0.3f }, { 0.f, 1.2f, 0.35f },
        { -1.f, 1.5f, 0.4f }, { -1.f, 1.6f, 0.45f }, { -4.f, 1.9f, 0.25f },
        { -2.f, 1.7f, 0.3f }, { -1.f, 1.4f, 0.4f }, { -1.f, 1.8f, 0.35f } },
      1.2f, 0.15f },
    { "LoFi",
      { { +2.f, 0.7f, 0.08f }, { +1.f, 0.6f, 0.10f }, { +1.f, 0.55f, 0.15f },
        { +2.f, 0.45f, 0.12f }, { +2.f, 0.5f, 0.15f }, { 0.f, 0.7f, 0.1f },
        { +1.f, 0.7f, 0.12f }, { +1.f, 0.6f, 0.2f }, { +1.f, 0.7f, 0.15f } },
      3.5f, 0.2f },
    { "Trap 808",
      { { -5.f, 2.6f, 0.15f }, { -6.f, 0.45f, 0.15f }, { 0.f, 0.9f, 0.5f },
        { +2.f, 0.45f, 1.0f }, { +2.f, 0.5f, 0.95f }, { -7.f, 1.5f, 0.2f },
        { -5.f, 1.3f, 0.25f }, { 0.f, 0.6f, 0.85f }, { 0.f, 1.0f, 0.5f } },
      2.0f, 0.3f },
    { "SynthWave",
      { { 0.f, 1.0f, 0.6f }, { 0.f, 0.45f, 0.9f }, { 0.f, 0.55f, 0.9f },
        { 0.f, 0.7f, 0.8f }, { 0.f, 1.3f, 0.85f }, { -1.f, 0.7f, 0.5f },
        { 0.f, 0.7f, 0.55f }, { 0.f, 0.9f, 0.75f }, { 0.f, 1.6f, 0.7f } },
      2.2f, 0.8f },
    { "Jazz",
      { { -2.f, 0.6f, 0.15f }, { -1.f, 0.4f, 0.08f }, { -1.f, 0.5f, 0.25f },
        { -1.f, 0.5f, 0.35f }, { -1.f, 0.6f, 0.4f }, { -2.f, 0.6f, 0.2f },
        { -1.f, 0.6f, 0.25f }, { +2.f, 1.0f, 0.95f }, { 0.f, 1.4f, 0.45f } },
      1.1f, 0.05f },
    { "Latina",
      { { -4.f, 0.6f, 0.2f }, { +4.f, 0.22f, 0.1f }, { 0.f, 0.6f, 0.45f },
        { 0.f, 0.5f, 0.5f }, { 0.f, 0.7f, 0.55f }, { +5.f, 0.7f, 0.5f },
        { +6.f, 0.6f, 0.55f }, { 0.f, 0.8f, 0.6f }, { 0.f, 1.0f, 0.5f } },
      1.8f, 0.25f },
};
constexpr int kKitCount = (int) (sizeof(kKits) / sizeof(kKits[0]));

// ---- Patterns -------------------------------------------------------------
constexpr int kMaxSteps = 32;

struct Pattern
{
    const char* name;
    int num, den; // native time signature
    const char* rows[kLaneCount]; // K/S/C/H/O/L/T/R/X: x=full o=ghost .=off
    int kit = 0; // the style's native sound palette (synth kit index)
};

// One solid bar per style; 'o' = ghost/quiet hit.
constexpr Pattern kPatterns[] = {
    { "Rock 1", 4, 4,
      { "x...x...x...x...", "....x.......x...", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "x..............." } },
    { "Rock 2", 4, 4,
      { "x..x....x..x....", "....x.......x...", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "" } },
    { "Rock half", 4, 4,
      { "x.......x.......", "........x.......", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "" } },
    { "Pop 1", 4, 4,
      { "x...x...x...x...", "....x.......x...", "....x.......x...",
        "..x...x...x...x.", "", "", "", "", "" } },
    { "Pop 2", 4, 4,
      { "x.....x.x.......", "....x.......x...", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "" } },
    { "Metal 1", 4, 4,
      { "x.x.x.x.x.x.x.x.", "....x.......x...", "", "", "", "", "",
        "x.x.x.x.x.x.x.x.", "" } },
    { "Metal blast", 4, 4,
      { "x.x.x.x.x.x.x.x.", "..x...x...x...x.", "", "", "", "", "",
        "x.x.x.x.x.x.x.x.", "" } },
    { "Hip-Hop 1", 4, 4,
      { "x......x..x.....", "....x.......x...", "", "x.x.x.x.x.x.x.x.",
        "......x.......x.", "", "", "", "" } },
    { "Boom Bap", 4, 4,
      { "x.......x..x....", "....x.......x...", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "" } },
    { "House 1", 4, 4,
      { "x...x...x...x...", "", "....x.......x...", "x.x.x.x.x.x.x.x.",
        "....x.......x...", "", "", "", "" } },
    { "House 2", 4, 4,
      { "x...x...x...x...", "", "....x.......x...", "..x...x...x...x.",
        "", "", "", "", "" } },
    { "Funk 1", 4, 4,
      { "x..x..x...x.....", "....x..o....x..o", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "" } },
    { "Funk 2", 4, 4,
      { "x.....x...x..x..", "..x.x...x.x.....", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "" } },
    { "Bossa", 4, 4,
      { "x..x....x..x....", "", "...x..x.....x..x", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "" } },
    { "Son clave", 4, 4,
      { "x..x....x...x...", "", "x..x.....x..x...", "", "", "", "", "",
        "" } },
    { "Jazz ride", 4, 4,
      { "x.......x.......", "....o......x..o.", "", "",
        "....x.......x...", "", "", "x.x.x.x.x.x.x.x.", "" } },
    { "Valse 3/4", 3, 4,
      { "x...x...x...", "....x...x...", "", "x.x.x.x.x.x.", "", "",
        "", "", "" } },
    { "6/8 lento", 6, 8,
      { "x..x..x..x..", "....x.....x.", "", "x...x...x...", "", "",
        "", "", "" } },
    { "5/4 take five", 5, 4,
      { "x...x...x...x...x...", "....x.......x...x...",
        "", "x.x.x.x.x.x.x.x.x.x.", "", "", "", "", "" } },

    // ---- Bases para MONTAR canciones: verse/chorus/fill, sitio para bajo
    // y voz. Grooves simples, dos compases donde el fill cierra la frase.

    { "Balada verse", 4, 4,
      { "x.......x.......", "........x.......", "", "..x...x...x...x.",
        "", "", "", "", "" } },
    { "Balada chorus", 4, 4,
      { "x...x...x...x...", "....x.......x...", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "x..............." } },
    { "Balada 2 comp", 4, 4,
      { "x.......x.......x.......x...x...", "........x...............x...x..o",
        "", "..x...x...x...x...x.x.x.x.x.x.x.", "", "", "", "",
        "" } },
    { "Pop verse", 4, 4,
      { "x.....x.x.......", "....x.......x...", "", "..x...x...x...x.",
        "", "", "", "", "" } },
    { "Pop chorus", 4, 4,
      { "x...x...x...x...", "....x.......x...", "....x.......x...",
        "..x...x...x...x.", "......x.........", "", "", "",
        "x..............." } },
    { "Pop fill", 4, 4,
      { "x...x...x...x...", "....x.......x.xo", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "..............x.", "" } },
    { "Dembow", 4, 4,
      { "x..x..x..x..x..x.", "..x..x....x..x..", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "" } },
    { "Dembow chorus", 4, 4,
      { "x..x..x..x..x..x.", "..x..x....x..x..", "....x.......x...",
        "x.x.x.x.x.x.x.x.", "......x.......x.", "", "", "",
        "x..............." } },
    { "Trap 1", 4, 4,
      { "x.....x...x.....", "........x.......", "", "xxxxxxxxxxxxxxxx",
        "......x.......x.", "", "", "", "" } },
    { "Trap 2 comp", 4, 4,
      { "x.....x...x.....x.......x.......", "........x..............x......o",
        "", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "......x.......x.",
        "", "", "", "" } },
    { "Disco", 4, 4,
      { "x...x...x...x...", "", "....x.......x...", "o.o.o.o.o.o.o.o.",
        "..x...x...x...x.", "", "", "", "" } },
    { "Funk 3", 4, 4,
      { "x..x....x..x..o.", "....x..o..x.x..o", "", "x.xox.xox.xox.xo",
        "", "", "", "", "" } },
    { "Rock verse", 4, 4,
      { "x.....x.........", "....x.......x...", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "" } },
    { "Rock chorus 2comp", 4, 4,
      { "x...x...x...x...x...x...x...x...", "....x.......x.......x.....x.x.o",
        "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "", "", "",
        "..............x.", "x..............." } },
    { "Tom fill", 4, 4,
      { "x...............", "x...............", "", "x.x.x.x.x.x.....",
        "", "............x.x.", "..............xx", "", "" } },
    { "Train beat", 4, 4,
      { "x...x...x...x...", "o..x.o...o.x.o..", "", "x...x...x...x...",
        "", "", "", "", "" } },
    { "Ska", 4, 4,
      { "x...x...x...x...", "....x.......x...", "", "",
        "..x...x...x...x.", "", "", "", "" } },
    { "SynthWave 1", 4, 4,
      { "x...x...x...x...", "....x.......x...", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "x..............." } },
    { "SynthWave 2comp", 4, 4,
      { "x...x...x...x...x...x...x...x...", "....x.......x.......x.....x.x.o",
        "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "", "", "",
        "", "x...........x..." } },
    { "Cumbia", 4, 4,
      { "x..x..x.....x...", "....x.......x...", "", "xxxxxxxxxxxxxxxx",
        "", "", "", "", "" } },
    { "Samba lite", 4, 4,
      { "..x...x...x...x.", "....x.......x...", "..x.x...x.x...x.",
        "xxxxxxxxxxxxxxxx", "", "", "", "", "" } },
    { "Bolero", 4, 4,
      { "x.......x.......", "....x.......x..o", "", "x.x.x.x.x.x.x.x.",
        "", "", "", "", "" } },
    { "Rumba", 4, 4,
      { "x..x....x..x....", "", "x..x....x..x..o.", "o.o.o.o.o.o.o.o.",
        "", "", "", "", "" } },
    { "Neo-soul 2comp", 4, 4,
      { "x.....x...x.....x.......x..x....", "....x......o.x..o..........o.x..",
        "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "", "", "", "" } },
    { "Jazz 2comp", 4, 4,
      { "x.......x.......x.......x......x", "....o......x..o.............o...",
        "", "", "....x.......x...", "", "",
        "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "" } },
    { "House build 2comp", 4, 4,
      { "x...x...x...x...x...x...x...x...", "", "....x.......x...x...x.......x.xo",
        "x.x.x.x.x.x.x.x.x.x.x.x.xxxxxxxx", "....x.......x...x...x...x...x.x.",
        "", "", "", "" } },
    { "Half-time drop", 4, 4,
      { "x.....x.....x...", "........x.......", "", "xxxxxxxxxxxxxxxx",
        "", "", "", "", "x..............." } },
    { "Balada 6/8", 6, 8,
      { "x...x...x...", "....x.....x.", "", "x.x.x.x.x.x.", "", "",
        "", "", "" } },
    { "Downbeat solo", 4, 4,
      { "x...............", "", "", "x...............", "", "", "", "",
        "" } },
};
constexpr int kPatternCount =
    (int) (sizeof(kPatterns) / sizeof(kPatterns[0]));
// Each style's native sound palette: picking a pattern while a SYNTH kit
// is selected moves the kit so the style sounds as written (a sample-kit
// choice is respected and stays).
constexpr int kPatternKit[kPatternCount] = { 2, 2, 2, 1, 1, 2, 2, 0, 0, 1, 1, 0, 0, 7, 7, 6, 0, 0, 6, 0, 1, 0, 1, 1, 1, 7, 7, 4, 4, 1, 0, 2, 2, 2, 2, 2, 5, 5, 7, 7, 0, 7, 0, 6, 1, 4, 0, 0 };


// ---- Drum synthesis --------------------------------------------------------
// One voice = one hit; a fixed pool with oldest-steal polyphony.
struct DrumVoice
{
    int lane = -1; // <0 = free
    float tune = 1.f, decay = 1.f, tone = 0.5f, vel = 1.f;
    float drive = 1.f, click = 0.f;
    double phase = 0.0, phase2 = 0.0;
    double mphase[6] = {}; // cymbal: SIX independent square oscillators
    float lp = 0.f, lp2 = 0.f, hpPrev = 0.f, hpState = 0.f, bp = 0.f;
    int age = 0; // samples since trigger
};

class DrumSynth
{
public:
    void setSampleRate(const double sr) { sr_ = sr > 1000.0 ? sr : 44100.0; }

    void setKit(const Kit* k) { kit_ = k; }

    const Kit& kitFor(const int) const { return kit_ != nullptr ? *kit_ : fallback_; }

    void trigger(const int lane, const float velocity)
    {
        DrumVoice* v = findFree();
        const Kit& kit = kitFor(lane);
        const DrumTuning& t = kit.d[lane];
        v->lane = lane;
        v->tune = std::pow(2.0f, t.tune / 12.f);
        v->decay = t.decay;
        v->tone = t.tone;
        v->drive = kit.drive;
        v->click = kit.click;
        v->vel = juce::jlimit(0.05f, 1.f, velocity);
        v->phase = 0.0;
        v->phase2 = 0.0;
        for (auto& mp : v->mphase)
            mp = 0.0;
        v->lp = v->lp2 = v->hpPrev = v->hpState = v->bp = 0.f;
        v->age = 0;
    }

    void allOff()
    {
        for (auto& v : voices_)
            v.lane = -1;
    }

    // Render every active voice into a mono scratch (panning is applied by
    // the caller per lane) — returns nothing, accumulates in `out`.
    void render(juce::AudioBuffer<float>& out, const int startSample,
                const int numSamples, const float level)
    {
        // Lane panning (constant power-ish, subtle).
        static constexpr float kPan[kLaneCount] = { 0.f, 0.05f, 0.15f,
                                                    -0.25f, -0.25f, -0.1f,
                                                    0.1f, 0.3f, -0.4f };
        for (auto& v : voices_)
        {
            if (v.lane < 0)
                continue;
            const float pan = kPan[v.lane];
            const float gl = level * v.vel * (pan < 0.f ? 1.f : 1.f - pan);
            const float gr = level * v.vel * (pan > 0.f ? 1.f : 1.f + pan);
            float* L = out.getWritePointer(0);
            float* R = out.getNumChannels() > 1 ? out.getWritePointer(1)
                                                : nullptr;
            const double invSr = 1.0 / sr_;
            const int end = startSample + numSamples;
            for (int i = startSample; i < end; ++i)
            {
                const float t = (float) ((double) v.age * invSr);
                float s = 0.f;
                switch (v.lane)
                {
                    case kKick: s = kickSample(v, t); break;
                    case kSnare: s = snareSample(v, t); break;
                    case kClap: s = clapSample(v, t); break;
                    case kHatClosed: s = hatSample(v, t, 0.045f); break;
                    case kHatOpen: s = hatSample(v, t, 0.32f); break;
                    case kTomLow: s = tomSample(v, t, 190.f); break;
                    case kTomHigh: s = tomSample(v, t, 300.f); break;
                    case kRide: s = metalSample(v, t, 0.45f); break;
                    case kCrash: s = metalSample(v, t, 1.1f); break;
                    default: v.lane = -1; break;
                }
                s = juce::jlimit(-1.5f, 1.5f, s);
                // Kit character: drive = tanh saturation (normalised so
                // the level stays comparable across kits).
                s = std::tanh(s * v.drive) / std::tanh(v.drive);
                L[i] += s * gl;
                if (R != nullptr)
                    R[i] += s * gr;
                ++v.age;
                if (v.age > (int) (4.0 * sr_))
                    v.lane = -1;
            }
        }
    }

private:
    DrumVoice* findFree()
    {
        for (auto& v : voices_)
            if (v.lane < 0)
                return &v;
        // steal the oldest
        DrumVoice* oldest = &voices_[0];
        for (auto& v : voices_)
            if (v.age > oldest->age)
                oldest = &v;
        return oldest;
    }

    float noise()
    {
        // xorshift white noise, ±1
        static std::uint32_t st = 0x9E3779B9u;
        st ^= st << 13;
        st ^= st >> 17;
        st ^= st << 5;
        return (float) ((double) st / 2147483648.0) - 1.f;
    }

    float kickSample(DrumVoice& v, const float t)
    {
        const float fStart = 160.f * v.tune, fEnd = 44.f * v.tune;
        const float f = fEnd
                        + (fStart - fEnd)
                              * std::exp(-t / (0.035f * v.decay));
        v.phase += (double) (f * (float) (1.0 / sr_));
        if (v.phase >= 1.0)
            v.phase -= 1.0;
        const float env = std::exp(-t / (0.30f * v.decay));
        const float click = (t < 0.004f)
                                ? noise() * (1.f - t / 0.004f) * 0.5f
                                      * (0.3f + 1.7f * v.click)
                                : 0.f;
        const float body = std::sin(v.phase * 2.0 * 3.14159265358979)
                           * env;
        // tone: more click/brightness at 1
        return body + click * (0.4f + v.tone)
               + (body * body * v.tone * 0.4f); // soft saturation
    }

    float snareSample(DrumVoice& v, const float t)
    {
        const float f1 = 185.f * v.tune, f2 = 330.f * v.tune;
        v.phase += (double) (f1 * (float) (1.0 / sr_));
        v.phase2 += (double) (f2 * (float) (1.0 / sr_));
        const float env = std::exp(-t / (0.09f * v.decay));
        const float tone = (std::sin(v.phase * 2.0 * 3.14159265358979) * 0.6f
                            + std::sin(v.phase2 * 2.0 * 3.14159265358979)
                                  * 0.4f)
                           * env;
        // band-passed noise: one-pole LP then HP
        const float n = noise();
        v.lp += (n - v.lp) * (0.35f + 0.3f * v.tone);
        v.hpPrev = v.lp - v.hpState;
        v.hpState = v.lp;
        const float nEnv = std::exp(-t / (0.18f * v.decay))
                           * (0.5f + 1.5f * v.click);
        return tone * 0.7f + v.hpPrev * nEnv * (0.7f + 0.6f * v.tone);
    }

    float clapSample(DrumVoice& v, const float t)
    {
        const float n = noise();
        v.lp += (n - v.lp) * 0.45f;
        v.hpPrev = v.lp - v.hpState;
        v.hpState = v.lp;
        float env = 0.f;
        for (int k = 0; k < 3; ++k)
        {
            const float tk = t - 0.012f * (float) k;
            if (tk >= 0.f)
                env += std::exp(-tk / 0.008f);
        }
        env += std::exp(-t / (0.14f * v.decay)) * 0.6f;
        return v.hpPrev * env * 0.8f;
    }

    float hatSample(DrumVoice& v, const float t, const float dur)
    {
        const float n = noise();
        // highpass ~6 kHz (coef 0.55) + a second pole: bright "tsss".
        v.lp += (n - v.lp) * 0.55f;
        v.hpPrev = n - v.lp;
        v.lp2 += (v.hpPrev - v.lp2) * 0.55f;
        const float hp = v.hpPrev - v.lp2;
        const float env = std::exp(-t / (dur * v.decay));
        return hp * env * (0.5f + 0.5f * v.tone);
    }

    float tomSample(DrumVoice& v, const float t, const float f0)
    {
        const float f = f0 * v.tune
                        * (0.7f + 0.3f * std::exp(-t / 0.12f));
        v.phase += (double) (f * (float) (1.0 / sr_));
        if (v.phase >= 1.0)
            v.phase -= 1.0;
        const float env = std::exp(-t / (0.28f * v.decay));
        return std::sin(v.phase * 2.0 * 3.14159265358979) * env;
    }

    float metalSample(DrumVoice& v, const float t, const float dur)
    {
        // 808-style cymbal: SIX independent inharmonic squares (sharing
        // one accumulator produced a pure 3.7 kHz beep), then a STEEP
        // ~7 kHz highpass that keeps only the metallic sizzle of their
        // upper harmonics.
        static constexpr double ratio[6] = { 1.0, 1.34, 1.62, 2.06, 2.5,
                                             3.01 };
        const double base = 320.0 * (double) v.tune;
        float s = 0.f;
        for (int k = 0; k < 6; ++k)
        {
            v.mphase[k] += base * ratio[k] / sr_;
            if (v.mphase[k] >= 1.0)
                v.mphase[k] -= 1.0;
            s += (v.mphase[k] < 0.5) ? 1.f : -1.f;
        }
        s *= 0.1667f;
        const float a = 0.55f; // one-pole fc ≈ 4.8 kHz @ 44.1k
        v.lp += (s - v.lp) * a;
        const float hp1 = s - v.lp;
        v.lp2 += (hp1 - v.lp2) * a;
        const float hp = hp1 - v.lp2;
        // A real crash is mostly NOISE with tonal shimmer on top — blend
        // bright highpassed noise under the square sizzle.
        const float nz = noise();
        v.hpPrev += (nz - v.hpPrev) * a;
        v.hpState += (v.hpPrev - v.hpState) * a;
        const float nhp = v.hpPrev - v.hpState;
        const float env = std::exp(-t / (dur * v.decay));
        const float ping = 1.f + 1.5f * std::exp(-t / 0.004f);
        return (hp * 0.6f + nhp * 0.55f) * env
               * (0.5f + 0.5f * v.tone) * ping * 0.7f;
    }

    std::array<DrumVoice, 24> voices_ {};
    double sr_ = 44100.0;
    Kit fallback_ { "x", {}, 1.f, 0.3f };
    const Kit* kit_ = nullptr;
};

// ---- Pattern helpers -------------------------------------------------------
inline int stepsPerBar(const int num, const int den)
{
    return juce::jlimit(4, kMaxSteps, num * 16 / den);
}

inline int patternStepCount(const Pattern& p)
{
    return stepsPerBar(p.num, p.den);
}

inline std::uint8_t patternVelocity(const Pattern& p, const int lane,
                                    const int step)
{
    const char c = p.rows[lane][juce::jlimit(0, kMaxSteps - 1, step)];
    if (c == 'x')
        return 110;
    if (c == 'o')
        return 62;
    return 0;
}

// ---- Sample kits (user library: ~/Library/EZMusicProduction/Drums/Name) ----
// Filename conventions map pieces to GM roles; -Softest..-Hardest suffixes
// are velocity layers. Nothing here touches the audio thread: the folder
// scan happens in the ctor (names only) and the WAV reads in prepareToPlay.
struct SampleKitInfo
{
    juce::String name;
    juce::File dir;
};

struct SampleRole
{
    // Velocity layers (Softest..Hardest). Slots may SHARE one buffer when
    // the kit ships fewer layers — shared_ptr keeps that safe.
    std::shared_ptr<juce::AudioBuffer<float>> layer[5];
    double rate[5] = { 0, 0, 0, 0, 0 };
    bool has[5] = {};
};

inline int layerIndexForVelocity(const float vel01)
{
    const float v = vel01 * 127.f;
    if (v < 26) return 0; // Softest
    if (v < 51) return 1; // Soft
    if (v < 76) return 2; // Med
    if (v < 101) return 3; // Hard
    return 4; // Hardest
}

inline int suffixLayer(const juce::String& stemLower)
{
    if (stemLower.endsWith("softest")) return 0;
    if (stemLower.endsWith("soft")) return 1;
    if (stemLower.endsWith("med")) return 2;
    if (stemLower.endsWith("hard")) return 3;
    if (stemLower.endsWith("hardest")) return 4;
    return -1;
}

// Piece-name → grid lane; -1 = ignore.
inline int pieceToLane(juce::String f)
{
    f = f.toLowerCase();
    if (f.contains("kick"))
        return f.contains("short") ? -1 : kKick;
    if (f.contains("snarejazz"))
        return -1; // the rock snare owns the lane
    if (f.contains("snare"))
        return kSnare;
    if (f.contains("clap"))
        return kClap;
    if (f.contains("hatclosed") || f.contains("hat_closed"))
        return kHatClosed;
    if (f.contains("hatopen") || f.contains("hat_open"))
        return kHatOpen;
    if (f.contains("hat"))
        return -1; // pedal / semi-open
    if (f.contains("tomfloor"))
        return kTomLow;
    if (f.contains("tom1") || f.contains("tomhigh"))
        return kTomHigh;
    if (f.contains("tom"))
        return -1;
    if (f.contains("24ride"))
        return -1;
    if (f.contains("ride"))
        return kRide;
    if (f.contains("crash") || f.contains("cymbal"))
        return kCrash;
    return -1;
}

juce::AudioFormatManager& drumFormats()
{
    static juce::AudioFormatManager m;
    static bool registered = false;
    if (! registered)
    {
        registered = true;
        m.registerBasicFormats();
    }
    return m;
}

class SampleKit
{
public:
    juce::String name;
    bool loaded = false;
    SampleRole roles[kLaneCount];

    static std::vector<SampleKitInfo> scanLibrary()
    {
        std::vector<SampleKitInfo> out;
        const auto root = juce::File::getSpecialLocation(
                              juce::File::userApplicationDataDirectory)
                              .getChildFile("EZMusicProduction/Drums");
        for (const auto& dir : root.findChildFiles(
                 juce::File::findDirectories, false))
        {
            SampleKitInfo info;
            info.name = dir.getFileName();
            info.dir = dir;
            out.push_back(info);
        }
        std::sort(out.begin(), out.end(),
                  [](const SampleKitInfo& a, const SampleKitInfo& b)
                  { return a.name < b.name; });
        return out;
    }

    void load(const juce::File& dir)
    {
        name = dir.getFileName();
        auto& formats = drumFormats();
        for (const auto& f : dir.findChildFiles(juce::File::findFiles,
                                                false, "*.wav;*.aiff"))
        {
            const int lane = pieceToLane(f.getFileNameWithoutExtension());
            if (lane < 0)
                continue;
            const int layer = suffixLayer(
                f.getFileNameWithoutExtension().toLowerCase());
            juce::AudioFormatReader* r = formats.createReaderFor(f);
            if (r == nullptr)
                continue;
            auto buf = std::make_shared<juce::AudioBuffer<float>>(
                1, (int) r->lengthInSamples);
            r->read(buf.get(), 0, (int) r->lengthInSamples, 0, true, false);
            const double rate = r->sampleRate;
            delete r;
            const int slot = layer >= 0 ? layer : 2; // no suffix = Med
            roles[lane].layer[slot] = std::move(buf);
            roles[lane].rate[slot] = rate;
            roles[lane].has[slot] = true;
        }
        // Collapse missing layers onto the closest existing one so every
        // velocity finds a buffer.
        for (auto& role : roles)
        {
            int any = -1;
            for (int s = 0; s < 5; ++s)
                if (role.has[s])
                    any = s;
            if (any < 0)
                continue;
            for (int s = 0; s < 5; ++s)
                if (! role.has[s])
                {
                    role.layer[s] = role.layer[any];
                    role.rate[s] = role.rate[any];
                    role.has[s] = true;
                }
        }
        loaded = true;
    }

    // Playback voice for one sample hit.
    struct Voice
    {
        int role = -1, slot = 0;
        double pos = 0.0, ratio = 1.0;
        float gain = 0.f, pan = 0.f;
        const SampleKit* kit = nullptr;
    };

    void render(Voice& v, juce::AudioBuffer<float>& out, const int start,
                const int num) const
    {
        if (v.role < 0 || v.kit == nullptr)
            return;
        const auto& role = v.kit->roles[v.role];
        const auto* buf = role.layer[v.slot].get();
        if (buf == nullptr)
            return;
        const int frames = buf->getNumSamples();
        float* L = out.getWritePointer(0);
        float* R = out.getNumChannels() > 1 ? out.getWritePointer(1)
                                            : nullptr;
        const float gl = v.gain * (v.pan < 0 ? 1.f : 1.f - v.pan);
        const float gr = v.gain * (v.pan > 0 ? 1.f : 1.f + v.pan);
        for (int i = start; i < start + num; ++i)
        {
            const int ip = (int) v.pos;
            if (ip >= frames - 1)
            {
                v.role = -1;
                return;
            }
            const float fr = (float) (v.pos - ip);
            const float s = (buf->getSample(0, ip) * (1.f - fr)
                             + buf->getSample(0, ip + 1) * fr);
            L[i] += s * gl;
            if (R != nullptr)
                R[i] += s * gr;
            v.pos += v.ratio;
        }
    }
};

} // namespace

// ---- Processor --------------------------------------------------------------
class EzDrumsProcessor : public juce::AudioProcessor,
                         public MidiGenerator,
                         private juce::AudioProcessorParameter::Listener
{
public:
    EzDrumsProcessor()
        : AudioProcessor(BusesProperties().withOutput(
              "Output", juce::AudioChannelSet::stereo(), true))
    {
        juce::StringArray kitNames;
        for (const auto& k : kKits)
            kitNames.add(k.name);
        for (const auto& info : SampleKit::scanLibrary())
        {
            kitNames.add(info.name);
            sampleKitDirs_.push_back(info.dir);
            SampleKit kit;
            kit.name = info.name;
            sampleKits_.push_back(std::move(kit));
        }
        kit_ = new juce::AudioParameterChoice(
            juce::ParameterID { "kit", 1 }, u8("Kit"), kitNames, 0);
        addParameter(kit_);

        juce::StringArray patNames;
        for (const auto& p : kPatterns)
            patNames.add(p.name);
        patNames.add(u8("Vacío"));
        pattern_ = new juce::AudioParameterChoice(
            juce::ParameterID { "pat", 1 }, u8("Patrón"), patNames, 0);
        addParameter(pattern_);

        swing_ = new juce::AudioParameterFloat(
            juce::ParameterID { "swing", 1 }, u8("Swing"),
            juce::NormalisableRange<float>(0.f, 0.6f, 0.01f), 0.f);
        addParameter(swing_);

        lvl_ = new juce::AudioParameterFloat(
            juce::ParameterID { "lvl", 1 }, u8("Nivel"),
            juce::NormalisableRange<float>(0.f, 2.f, 0.01f), 0.9f);
        addParameter(lvl_);

        juce::StringArray sigNames { u8("Auto (proyecto)"), "4/4", "3/4",
                                     "6/8", "5/4", "7/8" };
        sig_ = new juce::AudioParameterChoice(
            juce::ParameterID { "sig", 1 }, u8("Compás"), sigNames, 0);
        addParameter(sig_);

        // Working grid: a copy of the selected pattern, editable from the
        // UI (edits persist in the state). Picking another preset reloads.
        for (auto* p : getParameters())
            p->addListener(this);
        loadWorkingGrid();
    }

    ~EzDrumsProcessor() override
    {
        for (auto* p : getParameters())
            p->removeListener(this);
    }

private:
    void parameterValueChanged(int, float) override
    {
        // Pattern change → reload the working grid; kit change → make sure
        // its samples are loaded (message thread: file IO).
        juce::MessageManager::callAsync([this, pat = pattern_->getIndex()] {
            if (pat != loadedPattern_)
                loadWorkingGrid();
            // Style implies palette: a synth kit follows the pattern's
            // native sound; a sample-kit choice is respected.
            const int cur = kit_->getIndex();
            if (cur < kKitCount && pat < kPatternCount
                && cur != kPatternKit[pat])
            {
                kit_->setValueNotifyingHost(
                    kit_->convertTo0to1((float) kPatternKit[pat]));
                ensureSampleKitLoaded();
            }
        });
        juce::MessageManager::callAsync([this] { ensureSampleKitLoaded(); });
    }
    void parameterGestureChanged(int, bool) override {}

public:

    // ---- editor surface ----
    int laneCount() const { return kLaneCount; }
    const char* laneName(const int lane) const
    {
        return kLanes[lane].name;
    }
    int stepCount() const { return stepCount_; }
    int barNum() const { return sigNum_; }
    int barDen() const { return sigDen_; }
    std::uint8_t stepVel(const int lane, const int step) const
    {
        return grid_[lane][step];
    }
    void setStepVel(const int lane, const int step, const std::uint8_t vel)
    {
        if (lane < 0 || lane >= kLaneCount || step < 0
            || step >= kMaxSteps)
            return;
        grid_[lane][step] = vel;
    }
    void clearGrid()
    {
        for (auto& r : grid_)
            r.fill(0);
    }
    int currentStep() const
    {
        return currentStep_.load(std::memory_order_relaxed);
    }
    juce::AudioParameterChoice* kitParam() { return kit_; }
    juce::AudioParameterChoice* patternParam() { return pattern_; }
    juce::AudioParameterChoice* sigParam() { return sig_; }
    juce::AudioParameterFloat* swingParam() { return swing_; }
    juce::AudioParameterFloat* lvlParam() { return lvl_; }
    // True when the working grid diverges from its preset.
    bool gridDirty() const { return gridDirty_; }

    void drainGeneratedMidi(juce::MidiBuffer& out) override
    {
        out = std::move(generated_);
        generated_.clear();
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    void prepareToPlay(const double sr, const int) override
    {
        sr_ = sr > 1000.0 ? sr : 44100.0;
        synth_.setSampleRate(sr_);
        ensureSampleKitLoaded();
    }

    // Message-thread only (file IO). Called by prepareToPlay and on kit
    // changes from the editor.
    void ensureSampleKitLoaded()
    {
        const int idx = kit_->getIndex() - kKitCount;
        if (idx < 0 || idx >= (int) sampleKits_.size())
            return;
        if (sampleKits_[(size_t) idx].loaded)
            return;
        sampleKits_[(size_t) idx].load(sampleKitDirs_[(size_t) idx]);
    }
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet().isDisabled()
               && (l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
                   || l.getMainOutputChannelSet()
                          == juce::AudioChannelSet::mono());
    }

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi) override
    {
        buffer.clear();
        generated_.clear();
        const int numSamples = buffer.getNumSamples();
        synth_.setKit(&kKits[juce::jlimit(0, kKitCount - 1, kit_->getIndex())]);
        const double sr = juce::jmax(8000.0, getSampleRate());
        const float level = juce::jmax(0.f, lvl_->get());

        // 1) User MIDI (manual pads / keyboard) triggers drums directly.
        for (const auto meta : midi)
        {
            const auto& m = meta.getMessage();
            if (m.isNoteOn())
                triggerNote(m.getNoteNumber(), m.getVelocity());
            else if (m.isAllNotesOff())
                synth_.allOff();
        }

        // 2) Sequencer: pattern locked to the host transport.
        bool playing = false;
        std::int64_t timeInSamples = 0;
        double bpm = 120.0;
        int projNum = 4, projDen = 4;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                playing = pos->getIsPlaying();
                if (pos->getBpm().hasValue())
                    bpm = *pos->getBpm();
                if (pos->getTimeInSamples().hasValue())
                    timeInSamples = *pos->getTimeInSamples();
                if (pos->getTimeSignature().hasValue())
                {
                    projNum = juce::jmax(1, pos->getTimeSignature()->numerator);
                    projDen = juce::jmax(1, pos->getTimeSignature()->denominator);
                }
            }
        bpm = juce::jlimit(30.0, 300.0, bpm);

        // Time signature: Auto follows the project, else the override.
        const int sigChoice = sig_->getIndex();
        if (sigChoice == 0)
        {
            sigNum_ = projNum;
            sigDen_ = projDen;
        }
        else
        {
            static constexpr int nums[5] = { 4, 3, 6, 5, 7 };
            static constexpr int dens[5] = { 4, 4, 8, 4, 8 };
            sigNum_ = nums[sigChoice - 1];
            sigDen_ = dens[sigChoice - 1];
        }
        const int barSteps = stepsPerBar(sigNum_, sigDen_);
        stepCount_ = barSteps;
        currentStep_.store(-1, std::memory_order_relaxed);

        if (playing)
        {
            const double sixteenthSec = 60.0 / bpm / 4.0;
            const std::int64_t sixteenthSmp =
                (std::int64_t) juce::jmax(1.0, sixteenthSec * sr);
            const std::int64_t blockStart = timeInSamples;
            const std::int64_t blockEnd = blockStart + numSamples;
            const float swing = juce::jlimit(0.f, 0.6f, swing_->get());

            if (! playing_)
            {
                // (re)anchor to the next sixteenth boundary
                playing_ = true;
                nextStepAbs_ =
                    ((blockStart + sixteenthSmp - 1) / sixteenthSmp + 1)
                    * sixteenthSmp;
            }
            else
            {
                // transport jumped (seek): re-anchor when far off
                if (nextStepAbs_ < blockStart - sixteenthSmp * barSteps * 2
                    || nextStepAbs_ > blockEnd + sixteenthSmp * barSteps * 2)
                    nextStepAbs_ = ((blockStart + sixteenthSmp - 1)
                                        / sixteenthSmp
                                    + 1)
                                   * sixteenthSmp;
            }

            const int patIdx = pattern_->getIndex();
            const Pattern& preset = kPatterns[juce::jlimit(
                0, kPatternCount - 1, patIdx)];

            while (nextStepAbs_ < blockEnd)
            {
                const std::int64_t absStep = nextStepAbs_ / sixteenthSmp;
                const int stepInBar = (int) (absStep
                                             % (std::int64_t) barSteps);
                currentStep_.store(stepInBar, std::memory_order_relaxed);
                // Odd sixteenths swing late by swing% of a sixteenth.
                std::int64_t at = nextStepAbs_ - blockStart;
                if ((stepInBar & 1) == 1)
                    at += (std::int64_t) ((float) sixteenthSmp * swing);
                const int off = (int) juce::jlimit<std::int64_t>(
                    0, numSamples - 1, at);

                for (int lane = 0; lane < kLaneCount; ++lane)
                {
                    const std::uint8_t vel = grid_[lane][stepInBar];
                    if (vel == 0)
                        continue;
                    triggerNote(kLanes[lane].note, vel);
                    const juce::MidiMessage on =
                        juce::MidiMessage::noteOn(1, kLanes[lane].note,
                                                  (float) vel / 127.f);
                    generated_.addEvent(on, off);
                    pendingOffs_.push_back(
                        { blockStart + off + (std::int64_t) (0.09 * sr),
                          kLanes[lane].note });
                }

                nextStepAbs_ += sixteenthSmp;
                (void) preset;
            }

            // Flush pending note-offs (the recorded take must pair).
            for (auto it = pendingOffs_.begin();
                 it != pendingOffs_.end();)
            {
                if (it->first < blockEnd)
                {
                    const int off = (int) juce::jlimit<std::int64_t>(
                        0, numSamples - 1,
                        juce::jmax<std::int64_t>(0, it->first - blockStart));
                    generated_.addEvent(
                        juce::MidiMessage::noteOff(1, it->second), off);
                    it = pendingOffs_.erase(it);
                }
                else
                    ++it;
            }
        }
        else
        {
            playing_ = false;
            pendingOffs_.clear();
        }

        // 3) Render the drum voices (synthesis + samples).
        synth_.render(buffer, 0, numSamples, level);
        renderSampleVoices(buffer, numSamples);
        (void) sr;
    }

private:
    void triggerNote(const int note, const int velocity)
    {
        const int lane = laneForNote(note);
        if (lane < 0)
            return;
        if (triggerSampleVoice(lane, velocity))
            return; // sample kit owns this hit
        // Sample kit selected but not loaded (or a sample failed): fall
        // back to a SYNTH kit so a hit is never silently dropped.
        int synthKit = kit_->getIndex();
        if (synthKit >= kKitCount)
            synthKit %= kKitCount;
        synth_.trigger(lane, (float) velocity / 127.f);
    }

    void loadWorkingGrid()
    {
        const int patIdx = juce::jlimit(0, kPatternCount - 1,
                                        pattern_->getIndex());
        for (int lane = 0; lane < kLaneCount; ++lane)
        {
            grid_[lane].fill(0);
            const char* row = kPatterns[patIdx].rows[lane];
            for (int s = 0; s < kMaxSteps && row[s] != '\0'; ++s)
                grid_[lane][s] = patternVelocity(kPatterns[patIdx], lane, s);
        }
        loadedPattern_ = patIdx;
        gridDirty_ = false;
    }

    const juce::String getName() const override { return { "EZDrums" }; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 3.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return { "Default" }; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override
    {
        juce::XmlElement x("EZDRUMS");
        x.setAttribute("kit", kit_->getIndex());
        x.setAttribute("pat", pattern_->getIndex());
        x.setAttribute("swing", (double) swing_->get());
        x.setAttribute("lvl", (double) lvl_->get());
        x.setAttribute("sig", sig_->getIndex());
        for (int lane = 0; lane < kLaneCount; ++lane)
        {
            juce::String row;
            for (int s = 0; s < kMaxSteps; ++s)
                row += juce::String::toHexString((int) grid_[lane][s]) + ",";
            x.setAttribute("lane" + juce::String(lane), row);
        }
        copyXmlToBinary(x, dest);
    }

    void setStateInformation(const void* data, const int size) override
    {
        if (auto x = getXmlFromBinary(data, size))
        {
            if (x->hasTagName("EZDRUMS"))
            {
                pattern_->setValueNotifyingHost(
                    pattern_->convertTo0to1((float) x->getIntAttribute(
                        "pat", 0)));
                loadWorkingGrid();
                for (int lane = 0; lane < kLaneCount; ++lane)
                {
                    const auto parts = juce::StringArray::fromTokens(
                        x->getStringAttribute("lane" + juce::String(lane)),
                        ",", "");
                    for (int s = 0; s < juce::jmin(kMaxSteps, parts.size());
                         ++s)
                        grid_[lane][s] =
                            (std::uint8_t) juce::jlimit(0, 127,
                                                        parts[s].getIntValue());
                }
            }
        }
    }

    juce::AudioParameterChoice* kit_ = nullptr;
    juce::AudioParameterChoice* pattern_ = nullptr;
    juce::AudioParameterChoice* sig_ = nullptr;
    juce::AudioParameterFloat* swing_ = nullptr;
    juce::AudioParameterFloat* lvl_ = nullptr;

    DrumSynth synth_;
    double sr_ = 44100.0;
    std::vector<SampleKit> sampleKits_;
    std::vector<juce::File> sampleKitDirs_;
    std::array<SampleKit::Voice, 32> sampleVoices_ {};

    bool triggerSampleVoice(const int lane, const int velocity)
    {
        const int kitIdx = kit_->getIndex() - kKitCount;
        if (kitIdx < 0 || kitIdx >= (int) sampleKits_.size())
            return false;
        auto& kit = sampleKits_[(size_t) kitIdx];
        if (! kit.loaded || ! kit.roles[lane].has[0])
            return false;
        SampleKit::Voice* v = nullptr;
        for (auto& cand : sampleVoices_)
            if (cand.role < 0)
            {
                v = &cand;
                break;
            }
        if (v == nullptr)
            v = &sampleVoices_[0]; // steal the first
        const int slot = layerIndexForVelocity((float) velocity / 127.f);
        v->role = lane;
        v->slot = slot;
        v->kit = &kit;
        v->pos = 0.0;
        v->ratio = kit.roles[lane].rate[slot] / sr_;
        v->gain = (float) velocity / 127.f;
        static constexpr float kPan[kLaneCount] = { 0.f, 0.05f, 0.15f,
                                                    -0.25f, -0.25f, -0.1f,
                                                    0.1f, 0.3f, -0.4f };
        v->pan = kPan[lane];
        return true;
    }

    void renderSampleVoices(juce::AudioBuffer<float>& buffer, const int num)
    {
        for (auto& v : sampleVoices_)
            if (v.role >= 0)
                v.kit->render(v, buffer, 0, num);
    }

    std::array<std::array<std::uint8_t, kMaxSteps>, kLaneCount> grid_ {};
    int loadedPattern_ = 0;
    bool gridDirty_ = false;
    int stepCount_ = 16;
    int sigNum_ = 4, sigDen_ = 4;
    std::atomic<int> currentStep_ { -1 };
    bool playing_ = false;
    std::int64_t nextStepAbs_ = 0;
    juce::MidiBuffer generated_;
    std::vector<std::pair<std::int64_t, int>> pendingOffs_;
};

// ---- Editor: step-grid pattern editor ---------------------------------------
class StepCell : public juce::Component
{
public:
    StepCell(EzDrumsProcessor& proc, int* sharedPaint, const int lane,
             const int step)
        : p(proc), paintFlag(sharedPaint), lane_(lane), step_(step)
    {}

    void paint(juce::Graphics& g) override
    {
        const auto vel = p.stepVel(lane_, step_);
        const bool beat = (step_ % 4) == 0;
        g.fillAll(beat ? juce::Colour(0xff2c313a) : juce::Colour(0xff23272e));
        if (vel > 0)
        {
            const float k = vel / 127.f;
            g.setColour(juce::Colour(0xffe8a33d)
                            .withMultipliedAlpha(0.45f + 0.55f * k));
            g.fillRoundedRectangle(getLocalBounds().reduced(3).toFloat(),
                                   3.f);
        }
        g.setColour(juce::Colour(0xff3a404a));
        g.drawRoundedRectangle(getLocalBounds().reduced(3).toFloat(), 3.f,
                               1.f);
        // Playing-step cursor: a bright line at the top edge.
        if (p.currentStep() == step_)
        {
            g.setColour(juce::Colour(0xfff0f4fa));
            g.fillRect(2, 0, getWidth() - 4, 2);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const bool erase = e.mods.isRightButtonDown()
                           || p.stepVel(lane_, step_) > 0;
        *paintFlag = erase ? 0 : 110;
        apply();
    }
    void mouseEnter(const juce::MouseEvent& e) override
    {
        if (*paintFlag >= 0 && e.mods.isAnyMouseButtonDown())
            apply();
    }
    void mouseUp(const juce::MouseEvent&) override { *paintFlag = -1; }

private:
    void apply()
    {
        p.setStepVel(lane_, step_, (std::uint8_t) *paintFlag);
        repaint();
    }

    EzDrumsProcessor& p;
    int* paintFlag;
    int lane_, step_;
};

class EzDrumsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EzDrumsEditor(EzDrumsProcessor& proc)
        : AudioProcessorEditor(proc), p(proc)
    {
        kitBox = std::make_unique<juce::ComboBox>();
        for (int i = 0; i < kKitCount; ++i)
            kitBox->addItem(kKits[i].name, i + 1);
        kitBox->setSelectedId(p.kitParam()->getIndex() + 1,
                              juce::dontSendNotification);
        kitBox->onChange = [this] {
            *p.kitParam() = kitBox->getSelectedItemIndex();
        };
        addAndMakeVisible(*kitBox);

        patBox = std::make_unique<juce::ComboBox>();
        for (const auto& pat : kPatterns)
            patBox->addItem(pat.name, patBox->getNumItems() + 1);
        patBox->addItem(u8("Vacío"), patBox->getNumItems() + 1);
        patBox->setSelectedId(p.patternParam()->getIndex() + 1,
                              juce::dontSendNotification);
        patBox->onChange = [this] {
            *p.patternParam() = patBox->getSelectedItemIndex();
        };
        addAndMakeVisible(*patBox);

        sigBox = std::make_unique<juce::ComboBox>();
        sigBox->addItem(u8("Auto (proyecto)"), 1);
        sigBox->addItem("4/4", 2);
        sigBox->addItem("3/4", 3);
        sigBox->addItem("6/8", 4);
        sigBox->addItem("5/4", 5);
        sigBox->addItem("7/8", 6);
        sigBox->setSelectedId(p.sigParam()->getIndex() + 1,
                              juce::dontSendNotification);
        sigBox->onChange = [this] {
            *p.sigParam() = sigBox->getSelectedItemIndex();
            rebuildGrid();
        };
        addAndMakeVisible(*sigBox);

        clearBtn.setButtonText(u8("Vaciar"));
        clearBtn.onClick = [this] {
            p.clearGrid();
            for (auto& c : cells)
                c->repaint();
        };
        addAndMakeVisible(clearBtn);

        gridViewPort_ = std::make_unique<juce::Viewport>();
        gridViewPort_->setViewedComponent(&gridHost_, false);
        addAndMakeVisible(gridViewPort_.get());

        for (int lane = 0; lane < kLaneCount; ++lane)
        {
            auto& lbl = *laneLabels.emplace_back(
                std::make_unique<juce::Label>(juce::String(),
                                              juce::CharPointer_UTF8(
                                                  kLanes[lane].name)));
            lbl.setFont(juce::FontOptions(12.f));
            lbl.setColour(juce::Label::textColourId,
                          juce::Colour(0xffcfd6e0));
            gridHost_.addAndMakeVisible(lbl);
        }

        hint.setText(
            u8("Click: golpe · arrastrar: pinta · click derecho: borra. "
               "Con la pista en R, grabar escribe el patrón en un clip "
               "nuevo (editable en el piano roll)."),
            juce::dontSendNotification);
        hint.setFont(juce::FontOptions(11.f));
        hint.setColour(juce::Label::textColourId,
                       juce::Colour(0xff8b93a0));
        addAndMakeVisible(hint);

        // The grid must EXIST before setSize() fires resized().
        rebuildGrid();
        setSize(720, 430);
        startTimerHz(30);
    }

    ~EzDrumsEditor() override { stopTimer(); }

    void timerCallback() override
    {
        const int cur = p.currentStep();
        if (cur != lastStep_)
        {
            lastStep_ = cur;
            for (auto& c : cells)
                c->repaint();
        }
    }

    void rebuildGrid()
    {
        cells.clear();
        const int steps = p.stepCount();
        const int cols = juce::jmin(steps, kMaxSteps);
        for (int lane = 0; lane < kLaneCount; ++lane)
            for (int s = 0; s < cols; ++s)
            {
                auto cell = cells.add(
                    std::make_unique<StepCell>(p, &sharedPaint, lane, s));
                gridHost_.addAndMakeVisible(*cell);
            }
        gridHost_.setSize(kLaneLabelW + cols * kCellW + 8,
                          kLaneCount * kCellH + 18);
        resized();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        auto top = area.removeFromTop(28);
        kitBox->setBounds(top.removeFromLeft(150));
        patBox->setBounds(top.removeFromLeft(170).reduced(0, 2));
        sigBox->setBounds(top.removeFromLeft(130).reduced(0, 2));
        clearBtn.setBounds(top.removeFromRight(70));
        area.removeFromTop(6);
        auto gridRect = area.removeFromTop(370);
        hint.setBounds(area.removeFromBottom(20));
        gridViewPort_->setBounds(gridRect);

        const int steps = juce::jmin(p.stepCount(), kMaxSteps);
        if (cells.isEmpty() || steps <= 0)
            return; // ctor calls setSize() before rebuildGrid()
        for (int lane = 0; lane < kLaneCount; ++lane)
        {
            laneLabels[(size_t) lane]->setBounds(
                0, lane * kCellH, kLaneLabelW - 6, kCellH);
            for (int s = 0; s < steps; ++s)
            {
                cells[(size_t) (lane * steps + s)]->setBounds(
                    kLaneLabelW + s * kCellW, lane * kCellH + 2,
                    kCellW - 3, kCellH - 6);
            }
        }
    }

private:
    static constexpr int kCellW = 30, kCellH = 34, kLaneLabelW = 96;

    EzDrumsProcessor& p;
    int sharedPaint = -1;
    std::unique_ptr<juce::ComboBox> kitBox, patBox, sigBox;
    juce::TextButton clearBtn;
    juce::Label hint;
    std::unique_ptr<juce::Viewport> gridViewPort_;
    juce::Component gridHost_;
    juce::OwnedArray<StepCell> cells;
    std::vector<std::unique_ptr<juce::Label>> laneLabels;
    int lastStep_ = -1;
};

juce::AudioProcessorEditor* EzDrumsProcessor::createEditor()
{
    return new EzDrumsEditor(*this);
}

juce::AudioProcessor* create() { return new EzDrumsProcessor(); }

} // namespace ezdrums
