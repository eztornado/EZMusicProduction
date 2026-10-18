// EZSynth — implementation. See ezsynth.h for the mission brief.
//
// Layout of this file:
//   1. Parameter table (single source of truth for params, UI and state).
//   2. Preset bank ("varios sonidos de sintetizador").
//   3. SynthVoice  — the modern subtractive engine (3 osc + unísono + sub +
//      ruido → ladder filter + env → amp), per-voice LFOs and glide, and
//      the per-voice LINES warp.
//   4. EzSynthProcessor — parameter plumbing, host bridge, MIDI cleanup and
//      the polyphonic warp scheduler.
//   5. ClipCanvas — clip-bound piano roll + LINES areas.
//   6. EzSynthEditor — parameter grid + clip page.
#include "ezsynth.h"

#include "piano_voice.h"

#include <cadence/engine.hpp>

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace ezsynth {
namespace {

static juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

// ---- Antialiasing for naive saw/square/pulse discontinuities -------------
inline float polyBlep(double t, double dt)
{
    if (dt <= 0.0)
        return 0.f;
    if (t < dt)
    {
        const double x = t / dt;
        return (float) (x * x - 2.0 * x + 1.0);
    }
    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt;
        return (float) (x * x + 2.0 * x + 1.0);
    }
    return 0.f;
}

inline float oscSample(int wave, double t, double dt)
{
    switch (wave)
    {
        case 0:
            return (float) std::sin(juce::MathConstants<double>::twoPi * t);
        case 1:
            return (float) (4.0 * std::abs(t - 0.5) - 1.0);
        case 2: // saw
            return (float) (2.0 * t - 1.0) - polyBlep(t, dt);
        case 3: // square
            return (t < 0.5 ? 1.f : -1.f) - polyBlep(t, dt)
                   + polyBlep(std::fmod(t + 0.5, 1.0), dt);
        default: // pulse 25 %
            return (t < 0.25 ? 1.f : -1.f) - polyBlep(t, dt)
                   + polyBlep(std::fmod(t + 0.75, 1.0), dt);
    }
}

inline float lfoValue(int shape, double phase, float sampleHold)
{
    switch (shape)
    {
        case 0:
            return (float) std::sin(juce::MathConstants<double>::twoPi
                                    * phase);
        case 1:
            return (float) (4.0 * std::abs(phase - 0.5) - 1.0);
        case 2:
            return (float) (2.0 * phase - 1.0);
        case 3:
            return phase < 0.5 ? 1.f : -1.f;
        default:
            return sampleHold;
    }
}

// =========================================================================
// 1. Parameter table
// =========================================================================
enum
{
    p_motor, p_preset,
    p_o1wave, p_o1oct, p_o1semi, p_o1det, p_o1lvl,
    p_o2wave, p_o2oct, p_o2semi, p_o2det, p_o2lvl,
    p_o3wave, p_o3oct, p_o3semi, p_o3det, p_o3lvl,
    p_uni, p_unispr, p_sublvl, p_subwave, p_noise,
    p_flttype, p_fltcut, p_fltres, p_fltkey, p_fltenv, p_fltdrive,
    p_fenvA, p_fenvD, p_fenvS, p_fenvR,
    p_aenvA, p_aenvD, p_aenvS, p_aenvR,
    p_l1shape, p_l1rate, p_l1depth, p_l1dest,
    p_l2shape, p_l2rate, p_l2depth, p_l2dest,
    p_glide, p_vel, p_level, p_pan,
    p_warpamt, p_warpR,
    kNumParams
};

struct ParamSpec
{
    const char* id;
    const char* label;
    int type; // 0 float, 1 int, 2 choice
    float lo, hi, skew, def;
    const char* choices;
    int group;
};

#define SPECF(id, label, lo, hi, skew, def, grp) \
    { id, label, 0, lo, hi, skew, def, nullptr, grp }
#define SPECI(id, label, lo, hi, def, grp) \
    { id, label, 1, lo, hi, 1.f, def, nullptr, grp }
#define SPECC(id, label, choices, def, grp) \
    { id, label, 2, 0.f, 0.f, 1.f, def, choices, grp }

static const ParamSpec kSpecs[kNumParams] = {
    SPECC("motor", "Motor", "Piano|Synth", 1, 0),
    SPECC("preset", "Preset", "", 0, 0),

    SPECC("o1wave", "Onda", "Sine|Triangle|Saw|Square|Pulse", 2, 1),
    SPECI("o1oct", "Octava", -3, 3, 0, 1),
    SPECI("o1semi", "Semitonos", -12, 12, 0, 1),
    SPECF("o1det", "Detune (c)", -50, 50, 1.f, 0, 1),
    SPECF("o1lvl", "Nivel", 0, 1, 1.f, 0.8f, 1),

    SPECC("o2wave", "Onda", "Sine|Triangle|Saw|Square|Pulse", 2, 2),
    SPECI("o2oct", "Octava", -3, 3, 0, 2),
    SPECI("o2semi", "Semitonos", -12, 12, 0, 2),
    SPECF("o2det", "Detune (c)", -50, 50, 1.f, 0, 2),
    SPECF("o2lvl", "Nivel", 0, 1, 1.f, 0.f, 2),

    SPECC("o3wave", "Onda", "Sine|Triangle|Saw|Square|Pulse", 2, 3),
    SPECI("o3oct", "Octava", -3, 3, 0, 3),
    SPECI("o3semi", "Semitonos", -12, 12, 0, 3),
    SPECF("o3det", "Detune (c)", -50, 50, 1.f, 0, 3),
    SPECF("o3lvl", "Nivel", 0, 1, 1.f, 0.f, 3),

    SPECI("uni", "Voces unísono", 1, 7, 1, 4),
    SPECF("unispr", "Apertura (c)", 0, 50, 1.f, 12.f, 4),
    SPECF("sublvl", "Sub nivel", 0, 1, 1.f, 0.f, 4),
    SPECC("subwave", "Sub onda", "Sine|Square", 0, 4),
    SPECF("noise", "Ruido", 0, 1, 1.f, 0.f, 4),

    SPECC("flttype", "Tipo", "LP 12|LP 24|HP 12|HP 24|BP 12", 1, 5),
    SPECF("fltcut", "Corte (Hz)", 40, 16000, 0.3f, 12000.f, 5),
    SPECF("fltres", "Resonancia", 0, 1, 1.f, 0.15f, 5),
    SPECF("fltkey", "Keytrack", 0, 1, 1.f, 0.f, 5),
    SPECF("fltenv", "Env. corte (st)", -48, 48, 1.f, 0.f, 5),
    SPECF("fltdrive", "Drive", 1, 8, 1.f, 1.f, 5),

    SPECF("fenvA", "Ataque", 0.001, 4, 0.3f, 0.005f, 6),
    SPECF("fenvD", "Decay", 0.01, 4, 0.3f, 0.3f, 6),
    SPECF("fenvS", "Sustain", 0, 1, 1.f, 0.5f, 6),
    SPECF("fenvR", "Release", 0.01, 4, 0.3f, 0.3f, 6),

    SPECF("aenvA", "Ataque", 0.001, 4, 0.3f, 0.005f, 7),
    SPECF("aenvD", "Decay", 0.01, 4, 0.3f, 0.25f, 7),
    SPECF("aenvS", "Sustain", 0, 1, 1.f, 0.7f, 7),
    SPECF("aenvR", "Release", 0.01, 6, 0.3f, 0.4f, 7),

    SPECC("l1shape", "Forma", "Sine|Triangle|Saw|Square|S&H", 0, 8),
    SPECF("l1rate", "Velocidad (Hz)", 0.02, 20, 0.3f, 5.f, 8),
    SPECF("l1depth", "Profundidad", 0, 1, 1.f, 0.f, 8),
    SPECC("l1dest", "Destino", "Pitch|Filtro|Amplitud|Pan", 0, 8),

    SPECC("l2shape", "Forma", "Sine|Triangle|Saw|Square|S&H", 0, 9),
    SPECF("l2rate", "Velocidad (Hz)", 0.02, 20, 0.3f, 0.5f, 9),
    SPECF("l2depth", "Profundidad", 0, 1, 1.f, 0.f, 9),
    SPECC("l2dest", "Destino", "Pitch|Filtro|Amplitud|Pan", 1, 9),

    SPECF("glide", "Portamento (s)", 0, 0.5, 1.f, 0.f, 10),
    SPECF("vel", "Sens. velocidad", 0, 1, 1.f, 0.3f, 10),
    SPECF("level", "Nivel", 0, 2, 1.f, 0.8f, 10),
    SPECF("pan", "Pan", -1, 1, 1.f, 0.f, 10),

    SPECF("warpamt", "Cantidad", 0, 1, 1.f, 0.8f, 11),
    SPECF("warpR", "Rango (st)", 1, 24, 1.f, 12.f, 11),
};

#undef SPECF
#undef SPECI
#undef SPECC

static const char* kGroupNames[] = {
    "Motor / Preset", "Oscilador 1", "Oscilador 2", "Oscilador 3",
    "Unísono · Sub · Ruido", "Filtro", "Env. filtro", "Env. amplitud",
    "LFO 1", "LFO 2", "Global", "LINES"
};
static constexpr int kNumGroups = 12;
static constexpr int kSynthGroups = 11; // LINES lives on the clip page

// =========================================================================
// 2. Preset bank
// =========================================================================
struct PresetSetting { int id; float value; };
struct PresetDef { const char* name; std::vector<PresetSetting> set; };

static const std::vector<PresetDef>& presets()
{
    static const std::vector<PresetDef> list = {
        { "Init", {} },
        { "Bajo Gordo",
          { { p_o1wave, 2 }, { p_o1lvl, 0.9f }, { p_o2wave, 2 },
            { p_o2lvl, 0.6f }, { p_o2det, 10 }, { p_sublvl, 0.5f },
            { p_flttype, 1 }, { p_fltcut, 700 }, { p_fltres, 0.35f },
            { p_fltenv, 18 }, { p_fenvD, 0.2f }, { p_fenvS, 0.f },
            { p_aenvA, 0.003f }, { p_aenvD, 0.4f }, { p_aenvS, 0.85f },
            { p_aenvR, 0.2f }, { p_fltdrive, 1.4f } } },
        { "Bajo Ácido",
          { { p_o1wave, 2 }, { p_o1lvl, 0.9f }, { p_flttype, 1 },
            { p_fltcut, 300 }, { p_fltres, 0.7f }, { p_fltenv, 30 },
            { p_fenvD, 0.12f }, { p_fenvS, 0.f }, { p_aenvA, 0.002f },
            { p_aenvD, 0.3f }, { p_aenvS, 0.5f }, { p_aenvR, 0.15f },
            { p_glide, 0.05f }, { p_fltdrive, 2.f } } },
        { "Lead Sincero",
          { { p_o1wave, 2 }, { p_o1lvl, 0.9f }, { p_o2wave, 2 },
            { p_o2lvl, 0.5f }, { p_o2det, -12 }, { p_uni, 4 },
            { p_unispr, 18 }, { p_fltcut, 6000 }, { p_fltres, 0.2f },
            { p_aenvA, 0.01f }, { p_aenvD, 0.3f }, { p_aenvS, 0.85f },
            { p_aenvR, 0.25f } } },
        { "Lead Detune",
          { { p_o1wave, 2 }, { p_o1lvl, 0.8f }, { p_o2wave, 2 },
            { p_o2lvl, 0.8f }, { p_o2det, 14 }, { p_o3wave, 2 },
            { p_o3lvl, 0.5f }, { p_o3det, -20 }, { p_uni, 5 },
            { p_unispr, 25 }, { p_fltcut, 9000 }, { p_aenvA, 0.02f },
            { p_aenvD, 0.4f }, { p_aenvS, 0.9f }, { p_aenvR, 0.3f },
            { p_l1rate, 5.5f }, { p_l1depth, 0.12f }, { p_l1dest, 0 } } },
        { "Pad Etéreo",
          { { p_o1wave, 2 }, { p_o1lvl, 0.7f }, { p_o2wave, 2 },
            { p_o2lvl, 0.7f }, { p_o2det, 12 }, { p_o3wave, 1 },
            { p_o3lvl, 0.6f }, { p_o3oct, -1 }, { p_o3det, -8 },
            { p_uni, 6 }, { p_unispr, 30 }, { p_fltcut, 3500 },
            { p_fltres, 0.15f }, { p_fenvA, 1.2f }, { p_fenvD, 2.f },
            { p_fenvS, 0.6f }, { p_fenvR, 2.f }, { p_aenvA, 0.8f },
            { p_aenvD, 1.5f }, { p_aenvS, 0.8f }, { p_aenvR, 2.5f },
            { p_l2rate, 0.3f }, { p_l2depth, 0.3f }, { p_l2dest, 1 } } },
        { "Pluck Cristal",
          { { p_o1wave, 1 }, { p_o1lvl, 0.85f }, { p_o1oct, 1 },
            { p_o2wave, 3 }, { p_o2lvl, 0.3f }, { p_fltcut, 8000 },
            { p_fltres, 0.4f }, { p_fltenv, 40 }, { p_fenvA, 0.001f },
            { p_fenvD, 0.18f }, { p_fenvS, 0.f }, { p_fenvR, 0.15f },
            { p_aenvA, 0.002f }, { p_aenvD, 0.35f }, { p_aenvS, 0.f },
            { p_aenvR, 0.3f } } },
        { "Teclas Eléctricas",
          { { p_o1wave, 0 }, { p_o1lvl, 0.9f }, { p_o2wave, 1 },
            { p_o2lvl, 0.5f }, { p_o2oct, 1 }, { p_o2det, 4 },
            { p_fltcut, 9000 }, { p_aenvA, 0.005f }, { p_aenvD, 0.9f },
            { p_aenvS, 0.35f }, { p_aenvR, 0.5f }, { p_vel, 0.5f },
            { p_level, 0.85f } } },
        { "Órgano",
          { { p_o1wave, 3 }, { p_o1lvl, 0.7f }, { p_o2wave, 3 },
            { p_o2lvl, 0.5f }, { p_o2oct, 1 }, { p_o3wave, 0 },
            { p_o3lvl, 0.6f }, { p_o3oct, -1 }, { p_flttype, 0 },
            { p_fltcut, 12000 }, { p_aenvA, 0.01f }, { p_aenvD, 0.1f },
            { p_aenvS, 1.f }, { p_aenvR, 0.15f }, { p_l1rate, 6.f },
            { p_l1depth, 0.06f }, { p_l1dest, 0 } } },
        { "Cuerdas Analógicas",
          { { p_o1wave, 2 }, { p_o1lvl, 0.8f }, { p_o2wave, 2 },
            { p_o2lvl, 0.8f }, { p_o2det, 16 }, { p_o3wave, 2 },
            { p_o3lvl, 0.6f }, { p_o3det, -16 }, { p_o3oct, -1 },
            { p_uni, 5 }, { p_unispr, 22 }, { p_fltcut, 5000 },
            { p_fltres, 0.12f }, { p_fenvA, 1.f }, { p_fenvD, 2.f },
            { p_fenvS, 0.7f }, { p_fenvR, 1.5f }, { p_aenvA, 0.5f },
            { p_aenvD, 1.2f }, { p_aenvS, 0.85f }, { p_aenvR, 1.2f },
            { p_l2rate, 0.35f }, { p_l2depth, 0.25f }, { p_l2dest, 1 } } },
        { "FX Riser",
          { { p_o1wave, 2 }, { p_o1lvl, 0.7f }, { p_noise, 0.6f },
            { p_fltcut, 800 }, { p_fltres, 0.5f }, { p_fltenv, 40 },
            { p_fenvA, 3.f }, { p_fenvD, 4.f }, { p_fenvS, 1.f },
            { p_fenvR, 1.f }, { p_aenvA, 0.5f }, { p_aenvD, 3.f },
            { p_aenvS, 0.9f }, { p_aenvR, 0.8f }, { p_l1shape, 2 },
            { p_l1rate, 0.5f }, { p_l1depth, 0.5f }, { p_l1dest, 0 } } },
        { "Bajo Sub",
          { { p_o1wave, 0 }, { p_o1lvl, 0.8f }, { p_sublvl, 0.9f },
            { p_flttype, 0 }, { p_fltcut, 5000 }, { p_aenvA, 0.005f },
            { p_aenvD, 0.6f }, { p_aenvS, 0.8f }, { p_aenvR, 0.3f },
            { p_level, 0.9f } } },
        { "Campanas",
          { { p_o1wave, 0 }, { p_o1lvl, 0.8f }, { p_o2wave, 0 },
            { p_o2lvl, 0.5f }, { p_o2oct, 1 }, { p_o2det, 6 },
            { p_o3wave, 0 }, { p_o3lvl, 0.35f }, { p_o3oct, 2 },
            { p_fltcut, 9000 }, { p_aenvA, 0.002f }, { p_aenvD, 2.f },
            { p_aenvS, 0.f }, { p_aenvR, 2.f }, { p_l1rate, 7.f },
            { p_l1depth, 0.08f }, { p_l1dest, 0 } } },
    };
    return list;
}

static juce::StringArray presetNames()
{
    juce::StringArray a;
    for (const auto& p : presets())
        a.add(u8(p.name));
    return a;
}

// =========================================================================
// 3. Synth engine
// =========================================================================
constexpr int kMaxOsc = 3;
constexpr int kMaxUni = 7;

struct OscP { int wave, octave, semi; float detune, level; };

struct SynthParams
{
    int motor = 1;
    OscP osc[kMaxOsc] = {};
    int uniVoices = 1;
    float uniSpread = 12.f;
    float subLvl = 0.f;
    int subWave = 0;
    float noiseLvl = 0.f;

    int fltType = 1;
    float cutoff = 12000.f, reso = 0.15f, keytrack = 0.f, fltEnvAmt = 0.f;
    float drive = 1.f;
    juce::ADSR::Parameters fenv { 0.005f, 0.3f, 0.5f, 0.3f };
    juce::ADSR::Parameters aenv { 0.005f, 0.25f, 0.7f, 0.4f };

    int l1shape = 0, l1dest = 0; float l1rate = 5.f, l1depth = 0.f;
    int l2shape = 0, l2dest = 1; float l2rate = 0.5f, l2depth = 0.f;

    float glide = 0.f, velSens = 0.3f, level = 0.8f, pan = 0.f;
    float warpAmt = 0.8f, warpR = 12.f;
};

// Mutable audio-thread-only state shared between voices.
struct SharedRuntime
{
    double lastNoteFreq = 0.0;
    bool everPlayed = false;
};

class SynthVoice : public juce::SynthesiserVoice
{
public:
    SynthVoice(const SynthParams* p, SharedRuntime* rt) : P_(p), rt_(rt) {}

    bool canPlaySound(juce::SynthesiserSound*) override { return true; }

    void setCurrentPlaybackSampleRate(double rate) override
    {
        sr_ = rate > 0.0 ? rate : 48000.0;
        adsr_.setSampleRate(sr_);
        fenv_.setSampleRate(sr_);
        filtered_ = false;
        f1_.prepare({ sr_, 512u, 2u });
        f2_.prepare({ sr_, 512u, 2u });
        f1_.reset();
        f2_.reset();
        warpCoef_ = 1.0 - std::exp(-1.0 / (0.008 * sr_));
    }

    void startNote(int midiNoteNumber, float velocity,
                   juce::SynthesiserSound*, int) override
    {
        const auto& P = *P_;
        note_ = midiNoteNumber;
        const double target =
            440.0 * std::pow(2.0, (midiNoteNumber - 69) / 12.0);

        if (P.glide > 0.001f && rt_->everPlayed)
        {
            glideFrom_ = rt_->lastNoteFreq;
            glideTarget_ = target;
            glidePos_ = 0.0;
            glideTime_ = P.glide;
            gliding_ = glideFrom_ > 0.0
                       && std::abs(target - glideFrom_) > 0.5;
            freq_ = gliding_ ? glideFrom_ : target;
        }
        else
        {
            gliding_ = false;
            freq_ = target;
        }
        rt_->lastNoteFreq = target;
        rt_->everPlayed = true;

        velGain_ = juce::jlimit(0.f, 1.f, (1.f - P.velSens)
                                                + P.velSens * velocity);
        // Deterministic per-note seed: avoids juce::Random's global lock on
        // the audio thread and gives S&H / noise per voice.
        rng_ = 0x9E3779B9u ^ ((std::uint32_t) (midiNoteNumber + 1) * 2654435761u);

        for (int o = 0; o < kMaxOsc; ++o)
            for (int u = 0; u < kMaxUni; ++u)
                phase_[o][u] = std::fmod(u * 0.6180339887, 1.0);
        subPhase_ = 0.0;
        l1phase_ = l2phase_ = 0.0;
        sh1_ = sh2_ = 0.f;
        warpSmoothed_ = 0.f;
        warpTarget_ = 0.f;
        sustained_ = false;
        pedalDown_ = false;

        juce::ADSR::Parameters amp;
        amp.attack = P.aenv.attack;
        amp.decay = P.aenv.decay;
        amp.sustain = P.aenv.sustain;
        amp.release = P.aenv.release;
        adsr_.setParameters(amp);
        adsr_.reset();
        adsr_.noteOn();

        juce::ADSR::Parameters fe;
        fe.attack = P.fenv.attack;
        fe.decay = P.fenv.decay;
        fe.sustain = P.fenv.sustain;
        fe.release = P.fenv.release;
        fenv_.setParameters(fe);
        fenv_.reset();
        fenv_.noteOn();

        configureFilter(P);
        f1_.reset();
        f2_.reset();
        filtered_ = false;

        active_ = true;
    }

    void stopNote(float, bool allowTailOff) override
    {
        if (pedalDown_)
        {
            sustained_ = true;
            return;
        }
        if (! allowTailOff)
        {
            adsr_.reset();
            fenv_.reset();
            active_ = false;
            clearCurrentNote();
            return;
        }
        adsr_.noteOff();
        fenv_.noteOff();
    }

    void pitchWheelMoved(int) override {}

    void controllerMoved(int controllerNumber, int value) override
    {
        if (controllerNumber != 64)
            return;
        const bool down = value >= 64;
        if (pedalDown_ && ! down && sustained_)
        {
            adsr_.noteOff();
            fenv_.noteOff();
            sustained_ = false;
        }
        pedalDown_ = down;
    }

    bool isVoiceActive() const override { return active_; }

    // LINES: per-voice pitch offset (semitones), set by the processor every
    // block from the drawn distortion areas.
    void setWarpTarget(float semis) { warpTarget_ = semis; }

    void renderNextBlock(juce::AudioBuffer<float>& output, int startSample,
                         int numSamples) override
    {
        if (! active_)
            return;
        const auto& P = *P_;
        prepareBlock(P);

        const int nch = output.getNumChannels();
        float* L = nch > 0 ? output.getWritePointer(0) + startSample : nullptr;
        float* R = nch > 1 ? output.getWritePointer(1) + startSample : nullptr;
        const double invSr = 1.0 / sr_;

        for (int i = 0; i < numSamples; ++i)
        {
            const float ampEnv = adsr_.getNextSample();
            const float fltEnv = fenv_.getNextSample();

            l1phase_ += l1rate_ * invSr;
            if (l1phase_ >= 1.0) { l1phase_ -= 1.0; sh1_ = nextNoise(); }
            l2phase_ += l2rate_ * invSr;
            if (l2phase_ >= 1.0) { l2phase_ -= 1.0; sh2_ = nextNoise(); }
            const float l1 = lfoValue(l1shape_, l1phase_, sh1_);
            const float l2 = lfoValue(l2shape_, l2phase_, sh2_);

            const float pitchMod =
                (l1dest_ == 0 ? l1depth_ * 12.f * l1 : 0.f)
                + (l2dest_ == 0 ? l2depth_ * 12.f * l2 : 0.f);

            double f = freq_;
            if (gliding_)
            {
                glidePos_ += invSr;
                const double fr =
                    juce::jlimit(0.0, 1.0, glidePos_ / glideTime_);
                f = glideFrom_ * std::pow(glideTarget_ / glideFrom_, fr);
                if (fr >= 1.0)
                    gliding_ = false;
            }

            // LINES polyphonic warp: smooth this voice's own offset.
            warpSmoothed_ += (warpTarget_ - warpSmoothed_) * (float) warpCoef_;
            const double f2 = f * std::exp2(
                (double) (pitchMod + warpSmoothed_) / 12.0);

            float sigL = 0.f, sigR = 0.f;
            for (int o = 0; o < kMaxOsc; ++o)
            {
                if (! oscOn_[o])
                    continue;
                const float lv = oscLvl_[o];
                const int w = oscWave_[o];
                for (int u = 0; u < uniCount_; ++u)
                {
                    double& ph = phase_[o][u];
                    const double inc = f2 * ratio_[o][u] * invSr;
                    const float v = oscSample(w, ph, inc);
                    ph += inc;
                    if (ph >= 1.0 || ph < 0.0)
                        ph -= std::floor(ph);
                    sigL += v * lv * ugL_[o][u];
                    sigR += v * lv * ugR_[o][u];
                }
            }
            if (subOn_)
            {
                const double inc = f2 * 0.5 * invSr;
                subPhase_ += inc;
                if (subPhase_ >= 1.0)
                    subPhase_ -= std::floor(subPhase_);
                const float v =
                    subWave_ == 0
                        ? (float) std::sin(juce::MathConstants<double>::twoPi
                                           * subPhase_)
                        : (subPhase_ < 0.5 ? 1.f : -1.f);
                sigL += v * subLvl_;
                sigR += v * subLvl_;
            }
            if (noiseOn_)
            {
                const float nz = nextNoise() * noiseLvl_;
                sigL += nz;
                sigR += nz;
            }

            sigL *= layerGain_;
            sigR *= layerGain_;
            if (! filtered_ || (i & 7) == 0)
            {
                const float fmod =
                    (float) (fltEnvAmt_ * fltEnv)
                    + (l1dest_ == 1 ? l1depth_ * 24.f * l1 : 0.f)
                    + (l2dest_ == 1 ? l2depth_ * 24.f * l2 : 0.f);
                const double cut = baseCut_ * std::exp2((double) fmod / 12.0);
                const float hz = (float) juce::jlimit(20.0, 18000.0, cut);
                f1_.setCutoffFrequency(hz);
                if (twoStage_)
                    f2_.setCutoffFrequency(hz);
                filtered_ = true;
            }
            float xl = f1_.processSample(0, driveSat(sigL));
            if (twoStage_)
                xl = f2_.processSample(0, xl);
            float xr = f1_.processSample(1, driveSat(sigR));
            if (twoStage_)
                xr = f2_.processSample(1, xr);

            float amp = ampEnv * velGain_ * level_;
            if (l1dest_ == 2)
                amp *= 1.f - l1depth_ * 0.5f * (1.f - l1);
            if (l2dest_ == 2)
                amp *= 1.f - l2depth_ * 0.5f * (1.f - l2);

            float pan = basePan_;
            if (l1dest_ == 3) pan += l1depth_ * 0.8f * l1;
            if (l2dest_ == 3) pan += l2depth_ * 0.8f * l2;
            pan = juce::jlimit(-1.f, 1.f, pan);
            const float ml = pan <= 0.f ? 1.f : 1.f - pan;
            const float mr = pan >= 0.f ? 1.f : 1.f + pan;

            const float outl = xl * amp * ml;
            const float outr = xr * amp * mr;
            if (L != nullptr)
                L[i] += R != nullptr ? outl : (outl + outr) * 0.5f;
            if (R != nullptr)
                R[i] += outr;
        }

        if (! adsr_.isActive())
        {
            active_ = false;
            clearCurrentNote();
        }
    }

private:
    void configureFilter(const SynthParams& P)
    {
        const int t = P.fltType;
        const bool bp = (t == 4);
        const bool hp = (t == 2 || t == 3);
        const auto type = bp ? juce::dsp::StateVariableTPTFilterType::bandpass
                             : (hp ? juce::dsp::StateVariableTPTFilterType::highpass
                                   : juce::dsp::StateVariableTPTFilterType::lowpass);
        f1_.setType(type);
        f2_.setType(type);
        twoStage_ = (t == 1 || t == 3); // LP 24 / HP 24
        const float res = juce::jlimit(0.707f, 4.f, 0.707f + P.reso * 3.3f);
        f1_.setResonance(res);
        f2_.setResonance(res);
        drive_ = juce::jmax(1.f, P.drive);
        baseCut_ = juce::jlimit(20.0, 18000.0,
                                (double) P.cutoff
                                    * std::exp2((double) P.keytrack
                                                * (note_ - 60) / 12.0));
        fltEnvAmt_ = P.fltEnvAmt;
    }

    float driveSat(float x) const
    {
        return drive_ > 1.0001f ? std::tanh(x * drive_) / drive_ : x;
    }

    void prepareBlock(const SynthParams& P)
    {
        uniCount_ = juce::jlimit(1, kMaxUni, P.uniVoices);
        for (int o = 0; o < kMaxOsc; ++o)
        {
            oscWave_[o] = P.osc[o].wave;
            oscLvl_[o] = P.osc[o].level;
            oscOn_[o] = P.osc[o].level > 1e-4f;
            const int uni = uniCount_;
            for (int u = 0; u < uni; ++u)
            {
                const double uniNorm =
                    uni > 1 ? (u - (uni - 1) / 2.0) / ((uni - 1) / 2.0)
                            : 0.0;
                const double cents = P.osc[o].detune + uniNorm * P.uniSpread;
                const double semis =
                    P.osc[o].octave * 12.0 + P.osc[o].semi + cents / 100.0;
                ratio_[o][u] = std::exp2(semis / 12.0);
                const float up = (float) uniNorm * 0.6f;
                const float ang = (up + 1.f) * juce::MathConstants<float>::pi
                                  * 0.25f;
                ugL_[o][u] = std::cos(ang);
                ugR_[o][u] = std::sin(ang);
            }
        }
        subOn_ = P.subLvl > 1e-4f;
        subLvl_ = P.subLvl;
        subWave_ = P.subWave;
        noiseOn_ = P.noiseLvl > 1e-4f;
        noiseLvl_ = P.noiseLvl;

        l1shape_ = P.l1shape; l1rate_ = P.l1rate;
        l1depth_ = P.l1depth; l1dest_ = P.l1dest;
        l2shape_ = P.l2shape; l2rate_ = P.l2rate;
        l2depth_ = P.l2depth; l2dest_ = P.l2dest;

        basePan_ = P.pan;
        level_ = P.level;
        // Loudness compensation: stacking oscillators / unison voices /
        // sub / noise must not multiply the level. 1/sqrt(n) keeps RMS
        // roughly constant as layers pile up (and prevents clipping).
        int layers = 0;
        for (int o = 0; o < kMaxOsc; ++o)
            if (oscOn_[o])
                layers += uniCount_;
        if (subOn_)
            ++layers;
        if (noiseOn_)
            ++layers;
        layerGain_ = 1.f / std::sqrt((float) juce::jmax(1, layers));
        configureFilter(P);
        filtered_ = false;
    }

    float nextNoise()
    {
        std::uint32_t x = rng_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rng_ = x;
        return (float) ((x >> 8) & 0xFFFFFFu) / 8388608.f - 1.f;
    }

    const SynthParams* P_;
    SharedRuntime* rt_;
    double sr_ = 48000.0;
    int note_ = 60;

    double freq_ = 440.0;
    double glideFrom_ = 0.0, glideTarget_ = 0.0, glidePos_ = 0.0,
           glideTime_ = 0.0;
    bool gliding_ = false;

    double phase_[kMaxOsc][kMaxUni] = {};
    double ratio_[kMaxOsc][kMaxUni] = {};
    float ugL_[kMaxOsc][kMaxUni] = {}, ugR_[kMaxOsc][kMaxUni] = {};
    int uniCount_ = 1;
    bool oscOn_[kMaxOsc] = {};
    int oscWave_[kMaxOsc] = {};
    float oscLvl_[kMaxOsc] = {};
    double subPhase_ = 0.0;
    bool subOn_ = false; float subLvl_ = 0.f; int subWave_ = 0;
    bool noiseOn_ = false; float noiseLvl_ = 0.f;

    double l1phase_ = 0.0, l2phase_ = 0.0;
    float sh1_ = 0.f, sh2_ = 0.f;
    int l1shape_ = 0, l1dest_ = 0; float l1rate_ = 5.f, l1depth_ = 0.f;
    int l2shape_ = 0, l2dest_ = 1; float l2rate_ = 0.5f, l2depth_ = 0.f;

    float basePan_ = 0.f, level_ = 0.8f;
    float layerGain_ = 1.f;
    double baseCut_ = 12000.0;
    float fltEnvAmt_ = 0.f;
    bool filtered_ = false;

    float velGain_ = 1.f;
    float warpSmoothed_ = 0.f, warpTarget_ = 0.f;
    double warpCoef_ = 0.01;
    std::uint32_t rng_ = 0x1234567u;
    bool active_ = false;
    bool pedalDown_ = false, sustained_ = false;

    juce::ADSR adsr_, fenv_;
    juce::dsp::StateVariableTPTFilter<float> f1_, f2_;
    bool twoStage_ = true;
    float drive_ = 1.f;
};

// ---- LINES distortion geometry ------------------------------------------
struct LineNode { float t; float note; }; // t: clip-relative SECONDS
struct WarpCurve { std::vector<LineNode> nodes; };

// The polyphonic rule: a voice at base pitch B, while the playhead is over a
// curve whose pitch there is P, is pulled toward P by a triangular window of
// half-width R. Notes near the path move a lot, notes far away don't move at
// all, and — crucially — every voice uses ITS OWN B, so simultaneus notes
// move by different amounts (the distortion is polyphonic).
inline float curvePitchAt(const WarpCurve& c, double t, bool& ok)
{
    const auto& n = c.nodes;
    if (n.size() < 2 || t < (double) n.front().t || t > (double) n.back().t)
    {
        ok = false;
        return 0.f;
    }
    std::size_t k = 0;
    while (k + 1 < n.size() && (double) n[k + 1].t < t)
        ++k;
    const auto& a = n[k];
    const auto& b = n[juce::jmin(n.size() - 1, k + 1)];
    const double d = (double) b.t - (double) a.t;
    const double f = d > 1e-9 ? juce::jlimit(0.0, 1.0, (t - a.t) / d) : 0.0;
    ok = true;
    return (float) ((double) a.note + ((double) b.note - a.note) * f);
}

class EzSynthEditor;
class ClipCanvas;

class EzSynthProcessor : public juce::AudioProcessor
{
public:
    EzSynthProcessor()
        : AudioProcessor(BusesProperties().withOutput(
              "Output", juce::AudioChannelSet::stereo(), true))
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& s = kSpecs[i];
            juce::AudioProcessorParameter* p = nullptr;
            const juce::ParameterID pid { s.id, 1 };
            if (i == p_preset)
                p = new juce::AudioParameterChoice(
                    pid, u8(s.label), presetNames(), (int) s.def);
            else if (s.type == 2)
                p = new juce::AudioParameterChoice(
                    pid, u8(s.label),
                    juce::StringArray::fromTokens(s.choices, "|", ""),
                    (int) s.def);
            else if (s.type == 1)
                p = new juce::AudioParameterInt(pid, u8(s.label), (int) s.lo,
                                                (int) s.hi, (int) s.def);
            else
                p = new juce::AudioParameterFloat(
                    pid, u8(s.label),
                    juce::NormalisableRange<float>(s.lo, s.hi, 0.f, s.skew),
                    s.def);
            addParameter(p);
            params_.push_back(p);
        }

        for (int v = 0; v < kPianoVoices; ++v)
            pianoSynth_.addVoice(new cadence::PianoVoice(&pPiano_));
        pianoSynth_.addSound(new cadence::PianoSound());
        pianoSynth_.setNoteStealingEnabled(true);

        for (int v = 0; v < kSynthVoices; ++v)
            synthSynth_.addVoice(new SynthVoice(&P_, &rt_));
        synthSynth_.addSound(new AnySound());
        synthSynth_.setNoteStealingEnabled(true);
    }

    const juce::String getName() const override { return {"EZSynth"}; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {"Default"}; }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double sr, int) override
    {
        sr_ = sr > 0 ? sr : 48000.0;
        pianoSynth_.setCurrentPlaybackSampleRate(sr_);
        synthSynth_.setCurrentPlaybackSampleRate(sr_);
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
        fillParams();
        if (testWarp_)
        {
            P_.warpAmt = testAmt_;
            P_.warpR = testRange_;
        }
        publishWarp();
        cleanMidi(midi);
        if (P_.motor == 1)
            synthSynth_.renderNextBlock(buffer, midi, 0,
                                        buffer.getNumSamples());
        else
            pianoSynth_.renderNextBlock(buffer, midi, 0,
                                        buffer.getNumSamples());
    }

    // ---- state ----
    void getStateInformation(juce::MemoryBlock& dest) override
    {
        juce::XmlElement x("EZSYNTH2");
        for (int i = 0; i < kNumParams; ++i)
            if (i != p_preset)
                x.setAttribute(kSpecs[i].id, (double) rawOf(i));
        auto* as = new juce::XmlElement("areas");
        for (const auto& c : *areasShared())
        {
            auto* e = new juce::XmlElement("curve");
            juce::String nodes;
            for (const auto& nd : c.nodes)
                nodes << juce::String(nd.t, 4) << "," << juce::String(nd.note, 2)
                      << ";";
            e->setAttribute("n", nodes);
            as->addChildElement(e);
        }
        x.addChildElement(as);
        copyXmlToBinary(x, dest);
    }

    void setStateInformation(const void* data, int size) override
    {
        auto x = getXmlFromBinary(data, size);
        if (x == nullptr || x->getTagName() != "EZSYNTH2")
            return;
        for (int i = 0; i < kNumParams; ++i)
        {
            if (i == p_preset)
                continue;
            const auto& s = kSpecs[i];
            if (x->hasAttribute(s.id))
                setRaw(i, (float) x->getDoubleAttribute(s.id, s.def));
        }
        std::vector<WarpCurve> curves;
        if (auto* as = x->getChildByName("areas"))
            for (auto* e : as->getChildWithTagNameIterator("curve"))
            {
                WarpCurve c;
                const auto parts = juce::StringArray::fromTokens(
                    e->getStringAttribute("n"), ";", "");
                for (const auto& part : parts)
                {
                    const auto tn =
                        juce::StringArray::fromTokens(part, ",", "");
                    if (tn.size() == 2)
                        c.nodes.push_back({ tn[0].getFloatValue(),
                                            tn[1].getFloatValue() });
                }
                if (c.nodes.size() >= 2)
                    curves.push_back(std::move(c));
            }
        publishAreas(std::move(curves));
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ---- host bridge ----
    void setHostContext(cadence::Engine* engine, std::int64_t trackId,
                        std::int64_t clipId)
    {
        host_ = engine;
        trackId_ = trackId;
        clipId_ = clipId;
    }

    struct ClipNoteInfo { int pitch; float vel; double start; double len; };

    bool hasClip() const { return host_ != nullptr && clipId_ != 0; }

    std::vector<ClipNoteInfo> clipNotes() const
    {
        std::vector<ClipNoteInfo> out;
        if (! hasClip())
            return out;
        const int n = host_->clipNoteCount(clipId_);
        for (int i = 0; i < n; ++i)
        {
            int pitch = -1;
            float vel = 0;
            double start = 0, len = 0;
            host_->clipNoteAt(clipId_, i, &pitch, &vel, &start, &len);
            if (pitch >= 0)
                out.push_back({ pitch, vel, start, len });
        }
        return out;
    }

    double clipSpan() const
    {
        return hasClip() ? juce::jmax(1.0, host_->clipLength(clipId_)) : 4.0;
    }
    double clipOffset() const
    {
        return hasClip() ? host_->clipStart(clipId_) : 0.0;
    }
    double beatSeconds() const
    {
        return host_ != nullptr ? 60.0 / juce::jmax(20.0, host_->tempoBpm())
                                : 0.5;
    }
    double playheadSeconds() const
    {
        return host_ != nullptr ? host_->positionSeconds() : 0.0;
    }

    std::int64_t createClipAtPlayhead()
    {
        if (host_ == nullptr || trackId_ == 0)
            return 0;
        const double beat = beatSeconds();
        const auto id = host_->addMidiClip(trackId_, host_->positionSeconds(),
                                           beat * 16.0);
        if (id == 0)
            return 0;
        clipId_ = id;
        return id;
    }

    bool addClipNote(int pitch, double start, double len, float vel)
    {
        return hasClip()
               && ! host_->midiClipAddNote(clipId_, pitch, vel, start, len)
                       .failed();
    }
    bool removeClipNote(int pitch, double start)
    {
        if (! hasClip())
            return false;
        const int idx = indexOf(pitch, start);
        return idx >= 0
               && ! host_->midiClipRemoveNote(clipId_, idx).failed();
    }
    bool moveClipNote(int pitch, double start, int newPitch, double newStart,
                      double newLen)
    {
        if (! hasClip())
            return false;
        const int idx = indexOf(pitch, start);
        if (idx < 0)
            return false;
        float vel = 0.8f;
        int oldPitch = 0;
        double oldStart = 0, oldLen = 0;
        host_->clipNoteAt(clipId_, idx, &oldPitch, &vel, &oldStart, &oldLen);
        return ! host_
                    ->midiClipSetNote(clipId_, idx, newPitch, newStart,
                                      newLen > 0.05 ? newLen : oldLen)
                    .failed();
    }

    // ---- LINES areas ----
    std::shared_ptr<const std::vector<WarpCurve>> areasShared() const
    {
        juce::SpinLock::ScopedLockType lock(areasLock_);
        return areasRef_;
    }
    void publishAreas(std::vector<WarpCurve> areas)
    {
        auto p =
            std::make_shared<const std::vector<WarpCurve>>(std::move(areas));
        juce::SpinLock::ScopedLockType lock(areasLock_);
        areasRef_ = std::move(p);
    }
    float warpAmount() const
    {
        return ((juce::AudioParameterFloat*) params_[p_warpamt])->get();
    }
    float warpRange() const
    {
        return ((juce::AudioParameterFloat*) params_[p_warpR])->get();
    }

    // ---- parameter access for the editor ----
    juce::AudioProcessorParameter* paramAt(int i) const { return params_[i]; }
    float rawOf(int i) const
    {
        const auto& s = kSpecs[i];
        if (i == p_preset || s.type == 2)
            return (float) ((juce::AudioParameterChoice*) params_[i])
                ->getIndex();
        if (s.type == 1)
            return (float) ((juce::AudioParameterInt*) params_[i])->get();
        return ((juce::AudioParameterFloat*) params_[i])->get();
    }
    void setRaw(int i, float raw)
    {
        auto* p = params_[i];
        const auto& s = kSpecs[i];
        if (i == p_preset || s.type == 2)
        {
            auto* c = (juce::AudioParameterChoice*) p;
            c->setValueNotifyingHost(c->convertTo0to1(raw));
        }
        else if (s.type == 1)
        {
            auto* n = (juce::AudioParameterInt*) p;
            n->setValueNotifyingHost(n->convertTo0to1(raw));
        }
        else
        {
            auto* f = (juce::AudioParameterFloat*) p;
            f->setValueNotifyingHost(f->convertTo0to1(raw));
        }
    }

    void applyPreset(int index)
    {
        if (index < 0 || index >= (int) presets().size())
            return;
        for (int i = 0; i < kNumParams; ++i)
            if (i != p_preset)
                setRaw(i, kSpecs[i].def);
        for (const auto& s : presets()[(size_t) index].set)
            setRaw(s.id, s.value);
    }

    // Test hook (see ezsynth.h).
    void setWarpForTest(const std::vector<std::pair<float, float>>& nodes,
                        double clipTime, float amount, float range)
    {
        WarpCurve c;
        for (const auto& n : nodes)
            c.nodes.push_back({ n.first, n.second });
        publishAreas({ c });
        testWarp_ = true;
        testCt_ = clipTime;
        testAmt_ = amount;
        testRange_ = range;
    }

    friend class EzSynthEditor;
    friend class ClipCanvas;

private:
    class AnySound : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote(int) override { return true; }
        bool appliesToChannel(int) override { return true; }
    };

    int indexOf(int pitch, double start) const
    {
        const int n = host_->clipNoteCount(clipId_);
        for (int i = 0; i < n; ++i)
        {
            int p = -1;
            float v = 0;
            double s = 0, l = 0;
            host_->clipNoteAt(clipId_, i, &p, &v, &s, &l);
            if (p == pitch && std::abs(s - start) < 1e-3)
                return i;
        }
        return -1;
    }

    void fillParams()
    {
        auto F = [&](int i) { return ((juce::AudioParameterFloat*) params_[i])->get(); };
        auto I = [&](int i) { return ((juce::AudioParameterInt*) params_[i])->get(); };
        auto C = [&](int i) { return ((juce::AudioParameterChoice*) params_[i])->getIndex(); };

        P_.motor = C(p_motor);
        for (int o = 0; o < kMaxOsc; ++o)
        {
            const int b = p_o1wave + o * 5;
            P_.osc[o].wave = C(b + 0);
            P_.osc[o].octave = I(b + 1);
            P_.osc[o].semi = I(b + 2);
            P_.osc[o].detune = F(b + 3);
            P_.osc[o].level = F(b + 4);
        }
        P_.uniVoices = I(p_uni);
        P_.uniSpread = F(p_unispr);
        P_.subLvl = F(p_sublvl);
        P_.subWave = C(p_subwave);
        P_.noiseLvl = F(p_noise);

        P_.fltType = C(p_flttype);
        P_.cutoff = F(p_fltcut);
        P_.reso = F(p_fltres);
        P_.keytrack = F(p_fltkey);
        P_.fltEnvAmt = F(p_fltenv);
        P_.drive = F(p_fltdrive);

        P_.fenv.attack = F(p_fenvA);
        P_.fenv.decay = F(p_fenvD);
        P_.fenv.sustain = F(p_fenvS);
        P_.fenv.release = F(p_fenvR);
        P_.aenv.attack = F(p_aenvA);
        P_.aenv.decay = F(p_aenvD);
        P_.aenv.sustain = F(p_aenvS);
        P_.aenv.release = F(p_aenvR);

        P_.l1shape = C(p_l1shape); P_.l1rate = F(p_l1rate);
        P_.l1depth = F(p_l1depth); P_.l1dest = C(p_l1dest);
        P_.l2shape = C(p_l2shape); P_.l2rate = F(p_l2rate);
        P_.l2depth = F(p_l2depth); P_.l2dest = C(p_l2dest);

        P_.glide = F(p_glide);
        P_.velSens = F(p_vel);
        P_.level = F(p_level);
        P_.pan = F(p_pan);
        P_.warpAmt = F(p_warpamt);
        P_.warpR = F(p_warpR);

        // Piano engine shares the amp-envelope decay/release/level.
        pPiano_.decay.store(juce::jlimit(0.2f, 4.f, P_.aenv.decay / 0.25f));
        pPiano_.release.store(juce::jlimit(0.25f, 4.f, P_.aenv.release / 0.4f));
        pPiano_.level.store(P_.level);
    }

    void publishWarp()
    {
        auto curves = areasShared();
        float amt = P_.warpAmt, R = P_.warpR;
        double ct = 0.0;
        bool haveCt = false;
        if (testWarp_) { ct = testCt_; haveCt = true; }
        else if (hasClip())
        {
            ct = host_->positionSeconds() - clipOffset();
            haveCt = true;
        }
        const bool on = haveCt && curves != nullptr && ! curves->empty()
                        && amt > 1e-4f;
        if (! on)
        {
            forEachVoice([](juce::SynthesiserVoice* v, int) {
                setVoiceWarp(v, 0.f);
            });
            return;
        }
        forEachVoice([&](juce::SynthesiserVoice* v, int note) {
            setVoiceWarp(v, warpFor(*curves, ct, note, amt, R));
        });
    }

    static void setVoiceWarp(juce::SynthesiserVoice* v, float semis)
    {
        if (auto* sv = dynamic_cast<SynthVoice*>(v))
            sv->setWarpTarget(semis);
        else if (auto* pv = dynamic_cast<cadence::PianoVoice*>(v))
            pv->setWarpTarget(semis);
    }

    template <typename Fn>
    void forEachVoice(Fn&& fn)
    {
        juce::Synthesiser* synths[2] = { &synthSynth_, &pianoSynth_ };
        for (auto* s : synths)
            for (int i = 0; i < s->getNumVoices(); ++i)
                if (auto* v = s->getVoice(i))
                {
                    const int note = v->getCurrentlyPlayingNote();
                    if (note >= 0 && v->isVoiceActive())
                        fn(v, note);
                }
    }

    static float warpFor(const std::vector<WarpCurve>& cs, double ct,
                         int baseNote, float amt, float R)
    {
        float off = 0.f;
        for (const auto& c : cs)
        {
            bool ok = false;
            const float P = curvePitchAt(c, ct, ok);
            if (! ok)
                continue;
            const double diff = (double) P - baseNote;
            if (std::abs(diff) < R)
                off += amt
                       * (float) (diff * (1.0 - std::abs(diff) / R));
        }
        return juce::jlimit(-24.f, 24.f, off);
    }

    void cleanMidi(juce::MidiBuffer& midi)
    {
        // Reuse a member buffer so no heap allocation happens per block.
        midiScratch_.clear();
        bool panic = false;
        for (const auto md : midi)
        {
            const auto msg = md.getMessage();
            if (msg.isNoteOn() && msg.getVelocity() > 0)
                midiScratch_.addEvent(msg, md.samplePosition);
            else if (msg.isNoteOnOrOff())
                midiScratch_.addEvent(juce::MidiMessage::noteOff(msg.getChannel(),
                                                                 msg.getNoteNumber()),
                                      md.samplePosition);
            else
            {
                if (msg.isController()
                    && (msg.getControllerNumber() == 120
                        || msg.getControllerNumber() == 123))
                    panic = true;
                midiScratch_.addEvent(msg, md.samplePosition);
            }
        }
        midi.swapWith(midiScratch_);
        if (panic)
        {
            pianoSynth_.allNotesOff(0, false);
            synthSynth_.allNotesOff(0, false);
        }
    }

    static constexpr int kPianoVoices = 16;
    static constexpr int kSynthVoices = 12;

    std::vector<juce::AudioProcessorParameter*> params_;
    SynthParams P_;
    SharedRuntime rt_;
    juce::MidiBuffer midiScratch_;
    cadence::PianoParams pPiano_;
    juce::Synthesiser pianoSynth_, synthSynth_;

    cadence::Engine* host_ = nullptr;
    std::int64_t trackId_ = 0, clipId_ = 0;

    mutable juce::SpinLock areasLock_;
    std::shared_ptr<const std::vector<WarpCurve>> areasRef_ =
        std::make_shared<const std::vector<WarpCurve>>();

    bool testWarp_ = false;
    double testCt_ = 0.0;
    float testAmt_ = 0.8f, testRange_ = 12.f;
    double sr_ = 48000.0;
};

// =========================================================================
// 5. Clip piano-roll canvas
// =========================================================================
class ClipCanvas : public juce::Component
{
public:
    explicit ClipCanvas(EzSynthProcessor& proc) : proc_(proc)
    {
        setWantsKeyboardFocus(true);
    }

    int tool_ = 0; // 0 = notes, 1 = LINES areas
    bool follow_ = false;
    double viewSecs_ = 8.0;
    void setView(bool follow, double secs)
    {
        follow_ = follow;
        viewSecs_ = secs;
        repaint();
    }

    static constexpr int kKeysW = 40;
    static constexpr int kNoteTop = 96; // C7
    static constexpr int kNoteBot = 36; // C2

    void paint(juce::Graphics& g) override
    {
        const float w = (float) getWidth();
        const float h = (float) getHeight();
        const double span = proc_.clipSpan();

        for (int n = kNoteBot; n <= kNoteTop; ++n)
        {
            const float y = yOf((double) n);
            const float rh = yOf((double) n + 1.0) - y;
            const bool black = juce::MidiMessage::isMidiNoteBlack(n);
            g.setColour(black ? juce::Colour(0xFF101318)
                              : juce::Colour(0xFF171B22));
            g.fillRect((float) kKeysW, y, w - kKeysW, rh);
        }
        const double beat = proc_.beatSeconds();
        const double eighth = beat * 0.5;
        double vw0 = 0.0, vw1 = span;
        viewWindow(vw0, vw1);
        for (double t = vw0; t <= vw1 + 1e-6; t += eighth)
        {
            const bool isBar =
                std::abs(std::fmod(t / beat + 1e-6, 4.0)) < 0.01;
            g.setColour(isBar ? juce::Colour(0xFF3A4050)
                              : juce::Colour(0xFF232833));
            g.drawVerticalLine((int) xOf(t), 0.f, h);
        }

        // LINES distortion areas: a translucent band ±range around the path.
        const float R = proc_.warpRange();
        const auto curves = proc_.areasShared();
        const double pos = proc_.playheadSeconds() - proc_.clipOffset();
        for (const auto& c : *curves)
        {
            if (c.nodes.size() < 2)
                continue;
            juce::Path band, stroke;
            for (std::size_t i = 0; i < c.nodes.size(); ++i)
            {
                const float x = xOf(c.nodes[i].t);
                const float yu = yOf((double) c.nodes[i].note + R);
                const float yl = yOf((double) c.nodes[i].note - R);
                const float yc = yOf((double) c.nodes[i].note);
                if (i == 0)
                {
                    band.startNewSubPath(x, yu);
                    stroke.startNewSubPath(x, yc);
                }
                else
                {
                    band.lineTo(x, yu);
                    stroke.lineTo(x, yc);
                }
                // store lower edge by walking back later
                if (i == c.nodes.size() - 1)
                {
                    band.lineTo(x, yl);
                    for (std::size_t j = c.nodes.size(); j-- > 1;)
                    {
                        const auto& nd = c.nodes[j - 1];
                        band.lineTo(xOf(nd.t),
                                    yOf((double) nd.note - R));
                    }
                    band.closeSubPath();
                }
            }
            const bool active = proc_.hasClip() && pos >= 0
                                && pos <= span
                                && pos >= (double) c.nodes.front().t
                                && pos <= (double) c.nodes.back().t;
            g.setColour(active ? juce::Colour(0x44FFD24D)
                               : juce::Colour(0x337FE8C8));
            g.fillPath(band);
            g.setColour(active ? juce::Colour(0xFFFFD24D)
                               : juce::Colour(0xFF7FE8C8));
            g.strokePath(stroke, juce::PathStrokeType(active ? 3.f : 2.f));
            for (const auto& nd : c.nodes)
            {
                g.setColour(juce::Colour(0xFFE8F5C8));
                g.fillEllipse(xOf(nd.t) - 3.5f, yOf((double) nd.note) - 3.5f,
                              7, 7);
                g.setColour(juce::Colour(0xFF101318));
                g.drawEllipse(xOf(nd.t) - 3.5f, yOf((double) nd.note) - 3.5f,
                              7, 7, 1.f);
            }
        }
        if (areaDraft_.size() >= 2)
        {
            juce::Path draft;
            for (std::size_t i = 0; i < areaDraft_.size(); ++i)
            {
                const float x = xOf(areaDraft_[i].t);
                const float y = yOf((double) areaDraft_[i].note);
                if (i == 0)
                    draft.startNewSubPath(x, y);
                else
                    draft.lineTo(x, y);
            }
            g.setColour(juce::Colour(0xAAFFD24D));
            g.strokePath(draft, juce::PathStrokeType(2.f));
        }

        for (const auto& n : proc_.clipNotes())
        {
            const float x = xOf(n.start);
            const float x2 = xOf(n.start + n.len);
            const float y = yOf((double) n.pitch + 1.0);
            const float rh = juce::jmax(3.f, yOf((double) n.pitch) - y);
            const bool sel = n.pitch == selPitch_
                             && std::abs(n.start - selStart_) < 1e-3;
            g.setColour(sel ? juce::Colour(0xFFFFD24D)
                            : juce::Colour(0xFF7FE8C8));
            g.fillRect(x, y, juce::jmax(4.f, x2 - x), rh);
            g.setColour(juce::Colour(0xFF0D0F13));
            g.drawRect(x, y, juce::jmax(4.f, x2 - x), rh, 1.f);
        }

        if (proc_.hasClip())
        {
            const double p = proc_.playheadSeconds() - proc_.clipOffset();
            if (p >= 0.0 && p <= span)
            {
                g.setColour(juce::Colour(0xFFE8D24D));
                g.drawVerticalLine((int) xOf(p), 0.f, h);
            }
        }

        g.setColour(juce::Colour(0xFF0D0F13));
        g.fillRect(0, 0, kKeysW - 2, (int) h);
        g.setColour(juce::Colour(0xFF5C6270));
        g.setFont(9.f);
        for (int n = kNoteBot; n <= kNoteTop; ++n)
        {
            if (n % 12 != 0)
                continue;
            const float y = yOf((double) n);
            g.drawText(juce::MidiMessage::getMidiNoteName(n, true, false, 4),
                       0, (int) y - 6, kKeysW - 4, 12,
                       juce::Justification::centredRight);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        if (e.mods.isRightButtonDown())
        {
            auto curves = *proc_.areasShared();
            for (int ci = (int) curves.size() - 1; ci >= 0; --ci)
            {
                const auto& nodes = curves[(size_t) ci].nodes;
                for (std::size_t k = 0; k + 1 < nodes.size(); ++k)
                {
                    const auto a = nodes[k], b = nodes[k + 1];
                    const double t = juce::jlimit((double) a.t, (double) b.t,
                                                  tAt(e.x));
                    const double frac = (t - a.t)
                                        / juce::jmax(1e-6,
                                                     (double) (b.t - a.t));
                    const double ny = a.note + (b.note - a.note) * frac;
                    const float dx = e.x - xOf(t);
                    const float dy = e.y - yOf(ny);
                    if (std::sqrt(dx * dx + dy * dy) < 9.f)
                    {
                        curves.erase(curves.begin() + ci);
                        proc_.publishAreas(std::move(curves));
                        repaint();
                        return;
                    }
                }
            }
            if (const auto hit = hitNote(e.x, e.y))
            {
                proc_.removeClipNote(hit->pitch, hit->start);
                selected_ = false;
                repaint();
            }
            return;
        }
        if (tool_ == 1)
        {
            mode_ = Mode::area;
            areaDraft_.clear();
            areaDraft_.push_back({ (float) tAt(e.x), (float) noteAt(e.y) });
            repaint();
            return;
        }
        if (const auto hit = hitNote(e.x, e.y))
        {
            selected_ = true;
            selPitch_ = hit->pitch;
            selStart_ = hit->start;
            dragLen_ = hit->len;
            const double edgeX = xOf(hit->start + hit->len);
            mode_ = (e.x > edgeX - 7.f) ? Mode::resize : Mode::move;
            dragNote_ = { hit->pitch, 0.8f, hit->start, hit->len };
            repaint();
            return;
        }
        mode_ = Mode::draw;
        drawStart_ = snap(e.x);
        drawPitch_ = (int) juce::jlimit((double) kNoteBot, (double) kNoteTop,
                                        noteAt(e.y));
        drawLen_ = proc_.beatSeconds() * 0.5;
        selected_ = false;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (mode_ == Mode::area)
        {
            const LineNode nd { (float) tAt(e.x), (float) noteAt(e.y) };
            if (nd.t > areaDraft_.back().t + 0.01f)
                areaDraft_.push_back(nd);
            repaint();
            return;
        }
        if (mode_ == Mode::draw)
        {
            const double end = snap(e.x);
            drawLen_ = juce::jmax(proc_.beatSeconds() * 0.5, end - drawStart_);
            repaint();
            return;
        }
        if (mode_ == Mode::none || ! selected_)
            return;
        const int pitch = (int) juce::jlimit((double) kNoteBot,
                                             (double) kNoteTop, noteAt(e.y));
        const double start = snap(e.x - 4.f);
        if (mode_ == Mode::move)
        {
            if (pitch != dragNote_.pitch
                || std::abs(start - dragNote_.start) > 1e-3)
                if (proc_.moveClipNote(dragNote_.pitch, dragNote_.start, pitch,
                                       start, dragLen_))
                {
                    dragNote_.pitch = pitch;
                    dragNote_.start = start;
                    selPitch_ = pitch;
                    selStart_ = start;
                }
        }
        else if (mode_ == Mode::resize)
        {
            const double len =
                juce::jmax(proc_.beatSeconds() * 0.5,
                           snap(e.x) - dragNote_.start);
            if (std::abs(len - dragNote_.len) > 1e-3)
                if (proc_.moveClipNote(dragNote_.pitch, dragNote_.start,
                                       dragNote_.pitch, dragNote_.start, len))
                    dragNote_.len = len;
        }
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (mode_ == Mode::area)
        {
            if (areaDraft_.size() >= 2)
            {
                auto curves = *proc_.areasShared();
                if ((int) curves.size() < 8)
                {
                    curves.push_back({ areaDraft_ });
                    proc_.publishAreas(std::move(curves));
                }
            }
            areaDraft_.clear();
        }
        if (mode_ == Mode::draw && drawLen_ > 0.0)
        {
            proc_.addClipNote(drawPitch_, drawStart_, drawLen_, 0.8f);
            selected_ = true;
            selPitch_ = drawPitch_;
            selStart_ = drawStart_;
        }
        mode_ = Mode::none;
        repaint();
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const int code = key.getKeyCode();
        if ((code == juce::KeyPress::backspaceKey
             || code == juce::KeyPress::deleteKey)
            && selected_)
        {
            proc_.removeClipNote(selPitch_, selStart_);
            selected_ = false;
            repaint();
            return true;
        }
        return false;
    }

private:
    enum class Mode { none, draw, move, resize, area };
    struct Hit { int pitch; double start; double len; };

    std::optional<Hit> hitNote(float x, float y) const
    {
        const double t = tAt(x);
        const double note = noteAt(y);
        for (const auto& n : proc_.clipNotes())
            if (t >= n.start && t <= n.start + n.len
                && note >= (double) n.pitch && note < (double) n.pitch + 1.0)
                return Hit { n.pitch, n.start, n.len };
        return std::nullopt;
    }

    double snap(float x) const
    {
        const double t = tAt(x);
        const double eighth = proc_.beatSeconds() * 0.5;
        return juce::jmax(0.0, std::round(t / eighth) * eighth);
    }
    void viewWindow(double& w0, double& w1) const
    {
        const double span = proc_.clipSpan();
        if (! follow_ || viewSecs_ <= 0 || viewSecs_ >= span)
        {
            w0 = 0.0;
            w1 = span;
            return;
        }
        const double pos =
            juce::jlimit(0.0, span, proc_.playheadSeconds()
                                        - proc_.clipOffset());
        const double c =
            juce::jlimit(viewSecs_ * 0.5, span - viewSecs_ * 0.5, pos);
        w0 = c - viewSecs_ * 0.5;
        w1 = c + viewSecs_ * 0.5;
    }
    float xOf(double t) const
    {
        double w0 = 0.0, w1 = 1.0;
        viewWindow(w0, w1);
        const double span = w1 - w0;
        return (float) (kKeysW
                        + juce::jlimit(0.0, span, t - w0) / span
                              * (getWidth() - kKeysW));
    }
    float yOf(double note) const
    {
        return (float) ((kNoteTop
                         - juce::jlimit((double) kNoteBot, (double) kNoteTop,
                                        note))
                        / (kNoteTop - kNoteBot))
               * getHeight();
    }
    double tAt(float x) const
    {
        double w0 = 0.0, w1 = 1.0;
        viewWindow(w0, w1);
        return juce::jlimit(w0, w1,
                            w0 + (x - kKeysW) / (getWidth() - kKeysW)
                                     * (w1 - w0));
    }
    double noteAt(float y) const
    {
        return juce::jlimit((double) kNoteBot, (double) kNoteTop,
                            (double) (kNoteTop
                                      - (y / getHeight())
                                            * (kNoteTop - kNoteBot)));
    }

    EzSynthProcessor& proc_;
    Mode mode_ = Mode::none;
    bool selected_ = false;
    std::vector<LineNode> areaDraft_;
    int selPitch_ = 0, drawPitch_ = 0;
    double selStart_ = 0, drawStart_ = 0, drawLen_ = 0, dragLen_ = 0;
    struct { int pitch; float vel; double start; double len; } dragNote_ {};
};

// =========================================================================
// 6. Editor
// =========================================================================
class ParamControl : public juce::Component
{
public:
    ParamControl(EzSynthProcessor& proc, int index)
        : proc_(proc), index_(index)
    {
        const auto& s = kSpecs[index];
        label_.setText(u8(s.label), juce::dontSendNotification);
        label_.setFont(juce::FontOptions(10.5f));
        label_.setColour(juce::Label::textColourId, juce::Colour(0xFF9AA2B1));
        label_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(label_);

        isChoice_ = (index == p_preset || s.type == 2);
        if (isChoice_)
        {
            if (index == p_preset)
                combo_.addItemList(presetNames(), 1);
            else
                combo_.addItemList(
                    juce::StringArray::fromTokens(s.choices, "|", ""), 1);
            combo_.onChange = [this] {
                proc_.setRaw(index_, (float) (combo_.getSelectedId() - 1));
                if (index_ == p_preset)
                    proc_.applyPreset(combo_.getSelectedId() - 1);
            };
            addAndMakeVisible(combo_);
        }
        else
        {
            slider_.setSliderStyle(juce::Slider::LinearBar);
            slider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 16);
            slider_.setRange(s.lo, s.hi, s.type == 1 ? 1.0 : 0.0);
            if (s.skew > 0.f && std::abs(s.skew - 1.f) > 1e-4f)
                slider_.setSkewFactor(s.skew);
            slider_.onValueChange = [this] {
                proc_.setRaw(index_, (float) slider_.getValue());
            };
            addAndMakeVisible(slider_);
        }
    }

    void update()
    {
        if (isChoice_)
            combo_.setSelectedId((int) proc_.rawOf(index_) + 1,
                                 juce::dontSendNotification);
        else
            slider_.setValue((double) proc_.rawOf(index_),
                             juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(1);
        label_.setBounds(r.removeFromTop(13));
        if (isChoice_)
            combo_.setBounds(r.reduced(2, 3));
        else
            slider_.setBounds(r.reduced(1, 3));
    }

private:
    EzSynthProcessor& proc_;
    int index_;
    bool isChoice_ = false;
    juce::Label label_;
    juce::Slider slider_;
    juce::ComboBox combo_;
};

class Section : public juce::Component
{
public:
    Section(EzSynthProcessor& proc, const juce::String& title,
            const std::vector<int>& params)
        : title_(title)
    {
        title_.setFont(juce::FontOptions(12.0f).withStyle("Bold"));
        title_.setColour(juce::Label::textColourId, juce::Colour(0xFFE8EAEE));
        title_.setText(title, juce::dontSendNotification);
        addAndMakeVisible(title_);
        for (int p : params)
        {
            auto* c = new ParamControl(proc, p);
            items_.add(c);
            addAndMakeVisible(c);
        }
    }

    const juce::OwnedArray<ParamControl>& items() const { return items_; }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xFF1E2129));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.f);
        g.setColour(juce::Colour(0xFF2C3140));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.f,
                               1.f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(6);
        title_.setBounds(r.removeFromTop(16));
        const int n = items_.size();
        if (n == 0)
            return;
        const int cols = juce::jmin(4, n);
        const int rows = (n + cols - 1) / cols;
        const int cw = r.getWidth() / cols;
        const int ch = r.getHeight() / juce::jmax(1, rows);
        for (int i = 0; i < n; ++i)
        {
            const int cx = i % cols, cy = i / cols;
            items_[i]->setBounds(r.getX() + cx * cw, r.getY() + cy * ch,
                                 cw - 2, ch - 2);
        }
    }

private:
    juce::Label title_;
    juce::OwnedArray<ParamControl> items_;
};

class EzSynthEditor : public juce::AudioProcessorEditor,
                      private juce::AudioProcessorParameter::Listener,
                      private juce::Timer
{
public:
    explicit EzSynthEditor(EzSynthProcessor& proc)
        : juce::AudioProcessorEditor(&proc), proc_(proc), canvas_(proc)
    {
        for (int k = 0; k < kNumParams; ++k)
            proc_.paramAt(k)->addListener(this);

        title_.setText(u8("EZSynth"), juce::dontSendNotification);
        title_.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
        title_.setColour(juce::Label::textColourId, juce::Colour(0xFFE8EAEE));
        addAndMakeVisible(title_);

        pageSynth_.setButtonText(u8("Sintetizador"));
        pageSynth_.setClickingTogglesState(true);
        pageSynth_.setRadioGroupId(77);
        pageSynth_.onClick = [this] { switchPage(0); };
        addAndMakeVisible(pageSynth_);
        pageClip_.setButtonText("Clip / LINES");
        pageClip_.setClickingTogglesState(true);
        pageClip_.setRadioGroupId(77);
        pageClip_.onClick = [this] { switchPage(1); };
        addAndMakeVisible(pageClip_);

        // Synth page: a scrollable grid of parameter sections.
        synthViewport_.setViewedComponent(&synthContent_, false);
        synthViewport_.setScrollBarsShown(true, false);
        addAndMakeVisible(synthViewport_);
        for (int g = 0; g < kSynthGroups; ++g)
        {
            std::vector<int> ps;
            for (int i = 0; i < kNumParams; ++i)
                if (kSpecs[i].group == g)
                    ps.push_back(i);
            auto* sec = new Section(proc_, u8(kGroupNames[g]), ps);
            sections_.add(sec);
            synthContent_.addAndMakeVisible(sec);
            for (auto* c : sec->items())
                allControls_.push_back(c);
        }

        // Clip page controls.
        createClip_.setButtonText(u8("Crear clip"));
        createClip_.onClick = [this] {
            hint_.setText(proc_.createClipAtPlayhead() != 0
                              ? u8("Clip creado — dibuja notas")
                              : u8("Sin pista: abre el editor desde la pista"),
                          juce::dontSendNotification);
            refreshControls();
        };
        addAndMakeVisible(createClip_);

        toolNote_.setButtonText("Nota");
        toolNote_.setClickingTogglesState(true);
        toolNote_.setRadioGroupId(99);
        toolNote_.setToggleState(true, juce::dontSendNotification);
        toolNote_.onClick = [this] { canvas_.tool_ = 0; };
        addAndMakeVisible(toolNote_);
        toolArea_.setButtonText(u8("Área LINES"));
        toolArea_.setClickingTogglesState(true);
        toolArea_.setRadioGroupId(99);
        toolArea_.onClick = [this] { canvas_.tool_ = 1; };
        addAndMakeVisible(toolArea_);

        view_.addItemList(juce::StringArray { u8("Todo el clip"), u8("8 s"),
                                              u8("16 s") },
                          1);
        view_.onChange = [this] {
            const int id = view_.getSelectedId();
            canvas_.setView(id != 1, id == 2 ? 8.0 : 16.0);
        };
        addAndMakeVisible(view_);

        hint_.setFont(juce::FontOptions(11.0f));
        hint_.setColour(juce::Label::textColourId, juce::Colour(0xFF9AA2B1));
        addAndMakeVisible(hint_);

        for (int i : { p_warpamt, p_warpR })
        {
            auto* c = new ParamControl(proc_, i);
            clipParams_.add(c);
            addAndMakeVisible(c);
            allControls_.push_back(c);
        }

        addAndMakeVisible(canvas_);

        refreshControls();
        pageClip_.setToggleState(proc_.hasClip(),
                                 juce::dontSendNotification);
        pageSynth_.setToggleState(! proc_.hasClip(),
                                  juce::dontSendNotification);
        page_ = proc_.hasClip() ? 1 : 0;
        updateHint();
        startTimerHz(20);
        setResizable(true, false);
        setResizeLimits(760, 520, 2400, 1600);
        setSize(1020, 700);
    }

    ~EzSynthEditor() override
    {
        stopTimer();
        synthViewport_.setViewedComponent(nullptr, false);
        for (int k = 0; k < kNumParams; ++k)
            proc_.paramAt(k)->removeListener(this);
    }

    void parameterValueChanged(int, float) override { refreshControls(); }
    void parameterGestureChanged(int, bool) override {}

    void timerCallback() override
    {
        if (! isShowing())
            return;
        if (page_ == 1)
        {
            canvas_.repaint();
            if (canvas_.tool_ != lastTool_)
            {
                lastTool_ = canvas_.tool_;
                updateHint();
            }
        }
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF16171B));
    }

    // Test hook.
    void showPageForTest(int p) { switchPage(p); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        auto header = area.removeFromTop(30);
        title_.setBounds(header.removeFromLeft(110));
        pageSynth_.setBounds(header.removeFromLeft(120).reduced(0, 3));
        pageClip_.setBounds(header.removeFromLeft(120).reduced(0, 3));
        area.removeFromTop(6);

        synthViewport_.setVisible(page_ == 0);
        const bool clip = page_ == 1;
        canvas_.setVisible(clip);
        createClip_.setVisible(clip);
        toolNote_.setVisible(clip);
        toolArea_.setVisible(clip);
        view_.setVisible(clip);
        hint_.setVisible(clip);
        for (auto* c : clipParams_)
            c->setVisible(clip);

        if (clip)
        {
            auto bar = area.removeFromTop(28);
            createClip_.setBounds(bar.removeFromLeft(96));
            bar.removeFromLeft(6);
            toolNote_.setBounds(bar.removeFromLeft(60));
            toolArea_.setBounds(bar.removeFromLeft(104));
            view_.setBounds(bar.removeFromRight(120));
            hint_.setBounds(bar.reduced(6, 0));
            auto lines = area.removeFromTop(56);
            const int w = lines.getWidth() / 2;
            clipParams_[0]->setBounds(lines.removeFromLeft(w).reduced(3));
            clipParams_[1]->setBounds(lines.removeFromLeft(w).reduced(3));
            area.removeFromTop(4);
            canvas_.setBounds(area);
        }
        else
        {
            synthViewport_.setBounds(area);
            layoutSections(synthViewport_.getMaximumVisibleWidth());
        }
    }

private:
    void layoutSections(int width)
    {
        const int cols = 3;
        const int gap = 8;
        const int secW = juce::jmax(200, (width - (cols + 1) * gap) / cols);
        const int secH = 158;
        const int n = sections_.size();
        const int rows = (n + cols - 1) / cols;
        for (int i = 0; i < n; ++i)
        {
            const int cx = i % cols, cy = i / cols;
            sections_[i]->setBounds(gap + cx * (secW + gap),
                                    gap + cy * (secH + gap), secW, secH);
        }
        synthContent_.setSize(width, gap + rows * (secH + gap));
    }

    void switchPage(int p)
    {
        page_ = p;
        pageSynth_.setToggleState(p == 0, juce::dontSendNotification);
        pageClip_.setToggleState(p == 1, juce::dontSendNotification);
        resized();
    }

    void updateHint()
    {
        if (! proc_.hasClip())
            hint_.setText(u8("Sin clip: pulsa Crear clip o abre el editor "
                             "con un clip MIDI seleccionado"),
                          juce::dontSendNotification);
        else if (canvas_.tool_ == 1)
            hint_.setText(
                u8("Área LINES: dibuja una curva; las notas bajo sus bandas "
                   "se tuercen hacia ella — cada voz por su cuenta "
                   "(polifónico). Clic derecho: borrar"),
                juce::dontSendNotification);
        else
            hint_.setText(
                u8("Nota: dibuja/mueve/estira · clic derecho: borrar"),
                juce::dontSendNotification);
    }

    void refreshControls()
    {
        for (auto* c : allControls_)
            c->update();
    }

    EzSynthProcessor& proc_;
    juce::Label title_;
    juce::ToggleButton pageSynth_, pageClip_;
    juce::Viewport synthViewport_;
    juce::Component synthContent_;
    juce::OwnedArray<Section> sections_;
    juce::OwnedArray<ParamControl> clipParams_;
    juce::TextButton createClip_;
    juce::ToggleButton toolNote_, toolArea_;
    juce::ComboBox view_;
    juce::Label hint_;
    ClipCanvas canvas_;
    std::vector<ParamControl*> allControls_;
    int page_ = 0, lastTool_ = 0;
};

} // namespace

void bindHostContext(juce::AudioProcessor* proc, cadence::Engine* engine,
                     std::int64_t trackId, std::int64_t clipId)
{
    if (auto* p = dynamic_cast<EzSynthProcessor*>(proc))
        p->setHostContext(engine, trackId, clipId);
}

void setWarpForTest(juce::AudioProcessor* proc,
                    const std::vector<std::pair<float, float>>& nodes,
                    double clipTimeSeconds, float amount, float rangeSemis)
{
    if (auto* p = dynamic_cast<EzSynthProcessor*>(proc))
        p->setWarpForTest(nodes, clipTimeSeconds, amount, rangeSemis);
}

void setEditorPageForTest(juce::AudioProcessorEditor* editor, int page)
{
    if (auto* e = dynamic_cast<EzSynthEditor*>(editor))
        e->showPageForTest(page);
}

void applyPresetForTest(juce::AudioProcessor* proc, int index)
{
    if (auto* p = dynamic_cast<EzSynthProcessor*>(proc))
        p->applyPreset(index);
}

int presetCountForTest() { return (int) presets().size(); }

juce::AudioProcessorEditor* EzSynthProcessor::createEditor()
{
    return new EzSynthEditor(*this);
}

juce::AudioProcessor* create() { return new EzSynthProcessor(); }

} // namespace ezsynth
