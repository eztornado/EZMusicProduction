// EZSynth 2.0 — implementation. See ezsynth2.h.
//
// This file is organised in six sections:
//   1. Parameter table (one source of truth for the C ABI, state and UI).
//   2. Preset bank and preset builders.
//   3. DSP helpers: oscillators, wavetables, filters, envelopes, FX.
//   4. SynthVoice — the MPE voice that renders the two layers.
//   5. EzSynth2Processor — parameters, state, host bridge and MIDI.
//   6. Test/catalog entry points.
#include "ezsynth2.h"

#include "piano_voice.h"

#include <cadence/engine.hpp>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

namespace ezsynth2 {
namespace {

static juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

constexpr int kMaxVoices = 24;
constexpr int kMaxUnison = 7;
constexpr int kMaxMatrix = 16;
constexpr int kMaxMacros = 8;
constexpr int kMaxMacroRoutes = 4;
constexpr int kMaxFx = 6;
constexpr int kArpSteps = 16;

// =========================================================================
// 1. Parameter table
// =========================================================================
enum ParamIndex
{
    p_preset, p_motor, p_quality, p_bend, p_level, p_pan, p_glide, p_vel,
    p_morph, p_morphMode, p_width, p_limiter, p_mpe, p_mpeBend, p_mpePress,
    p_mpeTimbre,

    // Layer A — analog core (legacy IDs kept so EZSYNTH2 states load).
    p_o1wave, p_o1oct, p_o1semi, p_o1det, p_o1lvl,
    p_o2wave, p_o2oct, p_o2semi, p_o2det, p_o2lvl,
    p_o3wave, p_o3oct, p_o3semi, p_o3det, p_o3lvl,
    p_uni, p_unispr, p_sublvl, p_subwave, p_noise,

    // Layer A — engine selector and engine-specific controls.
    p_a_engine, p_a_wtTable, p_a_wtPos, p_a_wtWarp, p_a_fmRatio,
    p_a_fmIndex, p_a_fmFeedback, p_a_fmMode, p_a_grainSize, p_a_grainDensity,
    p_a_grainSpray, p_a_grainTone, p_a_sampleTable, p_a_level, p_a_pan,
    p_a_tune, p_a_fine,

    // Layer B — analog core.
    p_b_o1wave, p_b_o1oct, p_b_o1semi, p_b_o1det, p_b_o1lvl,
    p_b_o2wave, p_b_o2oct, p_b_o2semi, p_b_o2det, p_b_o2lvl,
    p_b_o3wave, p_b_o3oct, p_b_o3semi, p_b_o3det, p_b_o3lvl,
    p_b_uni, p_b_unispr, p_b_sublvl, p_b_subwave, p_b_noise,

    // Layer B — engine selector and engine-specific controls.
    p_b_engine, p_b_wtTable, p_b_wtPos, p_b_wtWarp, p_b_fmRatio,
    p_b_fmIndex, p_b_fmFeedback, p_b_fmMode, p_b_grainSize, p_b_grainDensity,
    p_b_grainSpray, p_b_grainTone, p_b_sampleTable, p_b_level, p_b_pan,
    p_b_tune, p_b_fine,

    // Auxiliary texture layer.
    p_x_engine, p_x_level, p_x_pan, p_x_tune, p_x_decay, p_x_color,

    // Dual filter. Filter 1 keeps the legacy IDs.
    p_flttype, p_fltcut, p_fltres, p_fltkey, p_fltenv, p_fltdrive,
    p_f2type, p_f2cut, p_f2res, p_f2key, p_f2env, p_f2drive,
    p_froute, p_fbalance,

    // Four envelopes. Env 1/2 keep the legacy IDs.
    p_fenvA, p_fenvD, p_fenvS, p_fenvR,
    p_aenvA, p_aenvD, p_aenvS, p_aenvR,
    p_e3A, p_e3D, p_e3S, p_e3R, p_e4A, p_e4D, p_e4S, p_e4R,

    // LFOs and random sources. LFO 1/2 keep the legacy IDs.
    p_l1shape, p_l1rate, p_l1depth, p_l1dest,
    p_l2shape, p_l2rate, p_l2depth, p_l2dest,
    p_l3shape, p_l3rate, p_l3depth, p_l3dest,
    p_l4shape, p_l4rate, p_l4depth, p_l4dest,
    p_rnd1rate, p_rnd1depth, p_rnd2rate, p_rnd2depth,

    // 16-slot modulation matrix: source, destination, amount.
    p_m1src, p_m1dst, p_m1amt,
    p_m2src, p_m2dst, p_m2amt,
    p_m3src, p_m3dst, p_m3amt,
    p_m4src, p_m4dst, p_m4amt,
    p_m5src, p_m5dst, p_m5amt,
    p_m6src, p_m6dst, p_m6amt,
    p_m7src, p_m7dst, p_m7amt,
    p_m8src, p_m8dst, p_m8amt,
    p_m9src, p_m9dst, p_m9amt,
    p_m10src, p_m10dst, p_m10amt,
    p_m11src, p_m11dst, p_m11amt,
    p_m12src, p_m12dst, p_m12amt,
    p_m13src, p_m13dst, p_m13amt,
    p_m14src, p_m14dst, p_m14amt,
    p_m15src, p_m15dst, p_m15amt,
    p_m16src, p_m16dst, p_m16amt,

    // Eight macros and four routes each.
    p_macro1, p_macro2, p_macro3, p_macro4,
    p_macro5, p_macro6, p_macro7, p_macro8,
    p_macro1r1dst, p_macro1r1amt, p_macro1r2dst, p_macro1r2amt,
    p_macro1r3dst, p_macro1r3amt, p_macro1r4dst, p_macro1r4amt,
    p_macro2r1dst, p_macro2r1amt, p_macro2r2dst, p_macro2r2amt,
    p_macro2r3dst, p_macro2r3amt, p_macro2r4dst, p_macro2r4amt,
    p_macro3r1dst, p_macro3r1amt, p_macro3r2dst, p_macro3r2amt,
    p_macro3r3dst, p_macro3r3amt, p_macro3r4dst, p_macro3r4amt,
    p_macro4r1dst, p_macro4r1amt, p_macro4r2dst, p_macro4r2amt,
    p_macro4r3dst, p_macro4r3amt, p_macro4r4dst, p_macro4r4amt,
    p_macro5r1dst, p_macro5r1amt, p_macro5r2dst, p_macro5r2amt,
    p_macro5r3dst, p_macro5r3amt, p_macro5r4dst, p_macro5r4amt,
    p_macro6r1dst, p_macro6r1amt, p_macro6r2dst, p_macro6r2amt,
    p_macro6r3dst, p_macro6r3amt, p_macro6r4dst, p_macro6r4amt,
    p_macro7r1dst, p_macro7r1amt, p_macro7r2dst, p_macro7r2amt,
    p_macro7r3dst, p_macro7r3amt, p_macro7r4dst, p_macro7r4amt,
    p_macro8r1dst, p_macro8r1amt, p_macro8r2dst, p_macro8r2amt,
    p_macro8r3dst, p_macro8r3amt, p_macro8r4dst, p_macro8r4amt,

    // Six FX slots. Type 0 = Off.
    p_fx1type, p_fx1mix, p_fx1p1, p_fx1p2, p_fx1p3, p_fx1p4,
    p_fx2type, p_fx2mix, p_fx2p1, p_fx2p2, p_fx2p3, p_fx2p4,
    p_fx3type, p_fx3mix, p_fx3p1, p_fx3p2, p_fx3p3, p_fx3p4,
    p_fx4type, p_fx4mix, p_fx4p1, p_fx4p2, p_fx4p3, p_fx4p4,
    p_fx5type, p_fx5mix, p_fx5p1, p_fx5p2, p_fx5p3, p_fx5p4,
    p_fx6type, p_fx6mix, p_fx6p1, p_fx6p2, p_fx6p3, p_fx6p4,

    // Arpeggiator.
    p_arpOn, p_arpMode, p_arpRate, p_arpOct, p_arpGate, p_arpSwing,
    p_arpLatch, p_arpSteps,
    p_arpStep1, p_arpStep2, p_arpStep3, p_arpStep4,
    p_arpStep5, p_arpStep6, p_arpStep7, p_arpStep8,
    p_arpStep9, p_arpStep10, p_arpStep11, p_arpStep12,
    p_arpStep13, p_arpStep14, p_arpStep15, p_arpStep16,

    // LINES.
    p_warpamt, p_warpR,

    kNumParams
};

struct ParamSpec
{
    const char* id;
    const char* label;
    int type; // 0 float, 1 int, 2 choice, 3 bool
    float lo, hi, skew, def;
    const char* choices;
    const char* group;
};

#define SPECF(id, label, lo, hi, skew, def, grp) \
    { id, label, 0, lo, hi, skew, def, nullptr, grp }
#define SPECI(id, label, lo, hi, def, grp) \
    { id, label, 1, lo, hi, 1.f, def, nullptr, grp }
#define SPECC(id, label, choices, def, grp) \
    { id, label, 2, 0.f, 0.f, 1.f, def, choices, grp }
#define SPECB(id, label, def, grp) \
    { id, label, 3, 0.f, 1.f, 1.f, def, nullptr, grp }

static const char* kEngineChoices = "Off|Analog|Wavetable|FM|Sample";
static const char* kTableChoices = "Sine|Saw|Square|Harmonic";
static const char* kFmModeChoices = "2-op|4-op";
static const char* kAuxChoices = "Off|Noise|Sine|Click|Texture";
static const char* kFilterChoices = "LP 12|LP 24|HP 12|HP 24|BP 12|Notch";
static const char* kRouteChoices = "Single|Serial|Parallel";
static const char* kLfoChoices = "Sine|Triangle|Saw|Square|S&H";
static const char* kLfoDestChoices = "Pitch|Filter|Amplitude|Pan|Off";
static const char* kMorphChoices = "Crossfade|Spectral|Fold";
static const char* kQualityChoices = "Eco|Studio|Ultra";
static const char* kFxChoices = "Off|Drive|Chorus|Phaser|Delay|Reverb|Comp|EQ";
static const char* kArpModeChoices = "Up|Down|Up/Down|Random|Chord";
static const char* kArpRateChoices = "1/4|1/8|1/8T|1/16|1/16T|1/32";
static const char* kOffOn = "Off|On";

// Mod sources and destinations. Keep the order in sync with ModSource and
// ModDest below; the choice indices are persisted in project state.
static const char* kModSourceChoices =
    "Off|LFO 1|LFO 2|LFO 3|LFO 4|Random 1|Random 2|Env 1|Env 2|Env 3|Env 4|"
    "Velocity|Keytrack|Mod Wheel|Aftertouch|Timbre|Pitch Bend|Macro 1|Macro 2|"
    "Macro 3|Macro 4|Macro 5|Macro 6|Macro 7|Macro 8|Expression|Note";
static const char* kModDestChoices =
    "Off|Pitch|Filter 1 Cutoff|Filter 1 Reso|Filter 2 Cutoff|Filter 2 Reso|"
    "Amp|Pan|Layer A Level|Layer B Level|Layer A WT Pos|Layer B WT Pos|"
    "Layer A FM Index|Layer B FM Index|Noise|Drive|FX 1 Mix|FX 2 Mix|"
    "FX 3 Mix|FX 4 Mix|FX 5 Mix|FX 6 Mix|Morph|LFO 1 Rate|LFO 2 Rate|"
    "Aux Level|Filter Balance|Width";

static const ParamSpec kSpecs[kNumParams] = {
    SPECC("preset", "Preset", "", 0, "global"),
    SPECC("motor", "Motor", "Piano|Synth", 1, "global"),
    SPECC("quality", "Calidad", kQualityChoices, 1, "global"),
    SPECF("bend", "Rango de bend (st)", 0, 24, 1.f, 2.f, "global"),
    SPECF("level", "Nivel", 0, 2, 1.f, 0.8f, "global"),
    SPECF("pan", "Pan", -1, 1, 1.f, 0.f, "global"),
    SPECF("glide", "Portamento (s)", 0, 1, 0.45f, 0.f, "global"),
    SPECF("vel", "Sens. velocidad", 0, 1, 1.f, 0.3f, "global"),
    SPECF("morph", "Morph A/B", 0, 1, 1.f, 0.f, "global"),
    SPECC("morphMode", "Modo morph", kMorphChoices, 0, "global"),
    SPECF("width", "Ancho estéreo", 0, 1, 1.f, 0.5f, "global"),
    SPECC("limiter", "Limitador", kOffOn, 1, "global"),
    SPECC("mpe", "MPE", kOffOn, 0, "mpe"),
    SPECF("mpeBend", "MPE bend (st)", 0, 96, 1.f, 48.f, "mpe"),
    SPECF("mpePress", "MPE presión", 0, 1, 1.f, 0.5f, "mpe"),
    SPECF("mpeTimbre", "MPE timbre", 0, 1, 1.f, 0.5f, "mpe"),

    SPECC("o1wave", "Osc 1 · Onda", "Sine|Triangle|Saw|Square|Pulse", 2, "a"),
    SPECI("o1oct", "Osc 1 · Octava", -3, 3, 1, "a"),
    SPECI("o1semi", "Osc 1 · Semitonos", -12, 12, 0, "a"),
    SPECF("o1det", "Osc 1 · Detune (c)", -50, 50, 1.f, 0.f, "a"),
    SPECF("o1lvl", "Osc 1 · Nivel", 0, 1, 1.f, 0.8f, "a"),
    SPECC("o2wave", "Osc 2 · Onda", "Sine|Triangle|Saw|Square|Pulse", 2, "a"),
    SPECI("o2oct", "Osc 2 · Octava", -3, 3, 0, "a"),
    SPECI("o2semi", "Osc 2 · Semitonos", -12, 12, 0, "a"),
    SPECF("o2det", "Osc 2 · Detune (c)", -50, 50, 1.f, 0.f, "a"),
    SPECF("o2lvl", "Osc 2 · Nivel", 0, 1, 1.f, 0.f, "a"),
    SPECC("o3wave", "Osc 3 · Onda", "Sine|Triangle|Saw|Square|Pulse", 2, "a"),
    SPECI("o3oct", "Osc 3 · Octava", -3, 3, 0, "a"),
    SPECI("o3semi", "Osc 3 · Semitonos", -12, 12, 0, "a"),
    SPECF("o3det", "Osc 3 · Detune (c)", -50, 50, 1.f, 0.f, "a"),
    SPECF("o3lvl", "Osc 3 · Nivel", 0, 1, 1.f, 0.f, "a"),
    SPECI("uni", "Layer A · Unísono", 1, 7, 1, "a"),
    SPECF("unispr", "Layer A · Apertura (c)", 0, 50, 1.f, 12.f, "a"),
    SPECF("sublvl", "Layer A · Sub", 0, 1, 1.f, 0.f, "a"),
    SPECC("subwave", "Layer A · Sub onda", "Sine|Square", 0, "a"),
    SPECF("noise", "Layer A · Ruido", 0, 1, 1.f, 0.f, "a"),

    SPECC("a_engine", "Layer A · Motor", kEngineChoices, 1, "a"),
    SPECC("a_wtTable", "Layer A · Tabla WT", kTableChoices, 1, "a"),
    SPECF("a_wtPos", "Layer A · Posición WT", 0, 1, 1.f, 0.25f, "a"),
    SPECF("a_wtWarp", "Layer A · Warp WT", 0, 1, 1.f, 0.f, "a"),
    SPECF("a_fmRatio", "Layer A · FM ratio", 0.5f, 16, 0.35f, 2.f, "a"),
    SPECF("a_fmIndex", "Layer A · FM índice", 0, 12, 1.f, 1.5f, "a"),
    SPECF("a_fmFeedback", "Layer A · FM feedback", 0, 1, 1.f, 0.f, "a"),
    SPECC("a_fmMode", "Layer A · FM modo", kFmModeChoices, 0, "a"),
    SPECF("a_grainSize", "Layer A · Grain size", 0.01f, 0.5f, 0.5f, 0.12f, "a"),
    SPECF("a_grainDensity", "Layer A · Grain density", 1, 60, 1.f, 16.f, "a"),
    SPECF("a_grainSpray", "Layer A · Grain spray", 0, 1, 1.f, 0.2f, "a"),
    SPECF("a_grainTone", "Layer A · Grain tone", 0, 1, 1.f, 0.5f, "a"),
    SPECC("a_sampleTable", "Layer A · Tabla sample", kTableChoices, 0, "a"),
    SPECF("a_level", "Layer A · Nivel", 0, 1.5f, 1.f, 0.85f, "a"),
    SPECF("a_pan", "Layer A · Pan", -1, 1, 1.f, 0.f, "a"),
    SPECF("a_tune", "Layer A · Tono (st)", -24, 24, 1.f, 0.f, "a"),
    SPECF("a_fine", "Layer A · Fine (c)", -100, 100, 1.f, 0.f, "a"),

    SPECC("b_o1wave", "Osc B1 · Onda", "Sine|Triangle|Saw|Square|Pulse", 2, "b"),
    SPECI("b_o1oct", "Osc B1 · Octava", -3, 3, 0, "b"),
    SPECI("b_o1semi", "Osc B1 · Semitonos", -12, 12, 0, "b"),
    SPECF("b_o1det", "Osc B1 · Detune (c)", -50, 50, 1.f, 0.f, "b"),
    SPECF("b_o1lvl", "Osc B1 · Nivel", 0, 1, 1.f, 0.8f, "b"),
    SPECC("b_o2wave", "Osc B2 · Onda", "Sine|Triangle|Saw|Square|Pulse", 2, "b"),
    SPECI("b_o2oct", "Osc B2 · Octava", -3, 3, 0, "b"),
    SPECI("b_o2semi", "Osc B2 · Semitonos", -12, 12, 0, "b"),
    SPECF("b_o2det", "Osc B2 · Detune (c)", -50, 50, 1.f, 0.f, "b"),
    SPECF("b_o2lvl", "Osc B2 · Nivel", 0, 1, 1.f, 0.f, "b"),
    SPECC("b_o3wave", "Osc B3 · Onda", "Sine|Triangle|Saw|Square|Pulse", 2, "b"),
    SPECI("b_o3oct", "Osc B3 · Octava", -3, 3, 0, "b"),
    SPECI("b_o3semi", "Osc B3 · Semitonos", -12, 12, 0, "b"),
    SPECF("b_o3det", "Osc B3 · Detune (c)", -50, 50, 1.f, 0.f, "b"),
    SPECF("b_o3lvl", "Osc B3 · Nivel", 0, 1, 1.f, 0.f, "b"),
    SPECI("b_uni", "Layer B · Unísono", 1, 7, 1, "b"),
    SPECF("b_unispr", "Layer B · Apertura (c)", 0, 50, 1.f, 10.f, "b"),
    SPECF("b_sublvl", "Layer B · Sub", 0, 1, 1.f, 0.f, "b"),
    SPECC("b_subwave", "Layer B · Sub onda", "Sine|Square", 0, "b"),
    SPECF("b_noise", "Layer B · Ruido", 0, 1, 1.f, 0.f, "b"),

    SPECC("b_engine", "Layer B · Motor", kEngineChoices, 0, "b"),
    SPECC("b_wtTable", "Layer B · Tabla WT", kTableChoices, 0, "b"),
    SPECF("b_wtPos", "Layer B · Posición WT", 0, 1, 1.f, 0.5f, "b"),
    SPECF("b_wtWarp", "Layer B · Warp WT", 0, 1, 1.f, 0.f, "b"),
    SPECF("b_fmRatio", "Layer B · FM ratio", 0.5f, 16, 0.35f, 3.f, "b"),
    SPECF("b_fmIndex", "Layer B · FM índice", 0, 12, 1.f, 1.f, "b"),
    SPECF("b_fmFeedback", "Layer B · FM feedback", 0, 1, 1.f, 0.f, "b"),
    SPECC("b_fmMode", "Layer B · FM modo", kFmModeChoices, 0, "b"),
    SPECF("b_grainSize", "Layer B · Grain size", 0.01f, 0.5f, 0.5f, 0.15f, "b"),
    SPECF("b_grainDensity", "Layer B · Grain density", 1, 60, 1.f, 12.f, "b"),
    SPECF("b_grainSpray", "Layer B · Grain spray", 0, 1, 1.f, 0.3f, "b"),
    SPECF("b_grainTone", "Layer B · Grain tone", 0, 1, 1.f, 0.5f, "b"),
    SPECC("b_sampleTable", "Layer B · Tabla sample", kTableChoices, 1, "b"),
    SPECF("b_level", "Layer B · Nivel", 0, 1.5f, 1.f, 0.f, "b"),
    SPECF("b_pan", "Layer B · Pan", -1, 1, 1.f, 0.f, "b"),
    SPECF("b_tune", "Layer B · Tono (st)", -24, 24, 1.f, 0.f, "b"),
    SPECF("b_fine", "Layer B · Fine (c)", -100, 100, 1.f, 0.f, "b"),

    SPECC("x_engine", "Aux · Motor", kAuxChoices, 0, "x"),
    SPECF("x_level", "Aux · Nivel", 0, 1, 1.f, 0.f, "x"),
    SPECF("x_pan", "Aux · Pan", -1, 1, 1.f, 0.f, "x"),
    SPECF("x_tune", "Aux · Tono (st)", -24, 24, 1.f, 0.f, "x"),
    SPECF("x_decay", "Aux · Decay (s)", 0.01f, 4, 0.35f, 0.4f, "x"),
    SPECF("x_color", "Aux · Color", 0, 1, 1.f, 0.5f, "x"),

    SPECC("flttype", "Filter 1 · Tipo", kFilterChoices, 1, "f1"),
    SPECF("fltcut", "Filter 1 · Corte (Hz)", 30, 20000, 0.28f, 12000.f, "f1"),
    SPECF("fltres", "Filter 1 · Resonancia", 0, 1, 1.f, 0.12f, "f1"),
    SPECF("fltkey", "Filter 1 · Keytrack", 0, 1, 1.f, 0.f, "f1"),
    SPECF("fltenv", "Filter 1 · Env (st)", -60, 60, 1.f, 0.f, "f1"),
    SPECF("fltdrive", "Filter 1 · Drive", 1, 8, 1.f, 1.f, "f1"),
    SPECC("f2type", "Filter 2 · Tipo", kFilterChoices, 0, "f2"),
    SPECF("f2cut", "Filter 2 · Corte (Hz)", 30, 20000, 0.28f, 3000.f, "f2"),
    SPECF("f2res", "Filter 2 · Resonancia", 0, 1, 1.f, 0.1f, "f2"),
    SPECF("f2key", "Filter 2 · Keytrack", 0, 1, 1.f, 0.f, "f2"),
    SPECF("f2env", "Filter 2 · Env (st)", -60, 60, 1.f, 0.f, "f2"),
    SPECF("f2drive", "Filter 2 · Drive", 1, 8, 1.f, 1.f, "f2"),
    SPECC("froute", "Ruteo de filtros", kRouteChoices, 0, "f2"),
    SPECF("fbalance", "Balance F1/F2", 0, 1, 1.f, 0.5f, "f2"),

    SPECF("fenvA", "Env 1 · Ataque", 0.001f, 8, 0.3f, 0.005f, "e1"),
    SPECF("fenvD", "Env 1 · Decay", 0.01f, 8, 0.3f, 0.3f, "e1"),
    SPECF("fenvS", "Env 1 · Sustain", 0, 1, 1.f, 0.5f, "e1"),
    SPECF("fenvR", "Env 1 · Release", 0.01f, 12, 0.3f, 0.3f, "e1"),
    SPECF("aenvA", "Env 2 · Ataque", 0.001f, 8, 0.3f, 0.005f, "e2"),
    SPECF("aenvD", "Env 2 · Decay", 0.01f, 8, 0.3f, 0.25f, "e2"),
    SPECF("aenvS", "Env 2 · Sustain", 0, 1, 1.f, 0.7f, "e2"),
    SPECF("aenvR", "Env 2 · Release", 0.01f, 12, 0.3f, 0.4f, "e2"),
    SPECF("e3A", "Env 3 · Ataque", 0.001f, 8, 0.3f, 0.01f, "e3"),
    SPECF("e3D", "Env 3 · Decay", 0.01f, 8, 0.3f, 0.4f, "e3"),
    SPECF("e3S", "Env 3 · Sustain", 0, 1, 1.f, 0.5f, "e3"),
    SPECF("e3R", "Env 3 · Release", 0.01f, 12, 0.3f, 0.4f, "e3"),
    SPECF("e4A", "Env 4 · Ataque", 0.001f, 8, 0.3f, 0.01f, "e4"),
    SPECF("e4D", "Env 4 · Decay", 0.01f, 8, 0.3f, 0.4f, "e4"),
    SPECF("e4S", "Env 4 · Sustain", 0, 1, 1.f, 0.5f, "e4"),
    SPECF("e4R", "Env 4 · Release", 0.01f, 12, 0.3f, 0.4f, "e4"),

    SPECC("l1shape", "LFO 1 · Forma", kLfoChoices, 0, "l1"),
    SPECF("l1rate", "LFO 1 · Velocidad (Hz)", 0.01f, 40, 0.3f, 5.f, "l1"),
    SPECF("l1depth", "LFO 1 · Profundidad", 0, 1, 1.f, 0.f, "l1"),
    SPECC("l1dest", "LFO 1 · Destino", kLfoDestChoices, 0, "l1"),
    SPECC("l2shape", "LFO 2 · Forma", kLfoChoices, 0, "l2"),
    SPECF("l2rate", "LFO 2 · Velocidad (Hz)", 0.01f, 40, 0.3f, 0.5f, "l2"),
    SPECF("l2depth", "LFO 2 · Profundidad", 0, 1, 1.f, 0.f, "l2"),
    SPECC("l2dest", "LFO 2 · Destino", kLfoDestChoices, 1, "l2"),
    SPECC("l3shape", "LFO 3 · Forma", kLfoChoices, 0, "l3"),
    SPECF("l3rate", "LFO 3 · Velocidad (Hz)", 0.01f, 40, 0.3f, 0.25f, "l3"),
    SPECF("l3depth", "LFO 3 · Profundidad", 0, 1, 1.f, 0.f, "l3"),
    SPECC("l3dest", "LFO 3 · Destino", kLfoDestChoices, 4, "l3"),
    SPECC("l4shape", "LFO 4 · Forma", kLfoChoices, 0, "l4"),
    SPECF("l4rate", "LFO 4 · Velocidad (Hz)", 0.01f, 40, 0.3f, 0.1f, "l4"),
    SPECF("l4depth", "LFO 4 · Profundidad", 0, 1, 1.f, 0.f, "l4"),
    SPECC("l4dest", "LFO 4 · Destino", kLfoDestChoices, 4, "l4"),
    SPECF("rnd1rate", "Random 1 · Rate (Hz)", 0.01f, 20, 0.3f, 2.f, "rnd"),
    SPECF("rnd1depth", "Random 1 · Depth", 0, 1, 1.f, 0.f, "rnd"),
    SPECF("rnd2rate", "Random 2 · Rate (Hz)", 0.01f, 20, 0.3f, 0.3f, "rnd"),
    SPECF("rnd2depth", "Random 2 · Depth", 0, 1, 1.f, 0.f, "rnd"),

    SPECC("m1src", "Matrix 1 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m1dst", "Matrix 1 · Destino", kModDestChoices, 0, "m"),
    SPECF("m1amt", "Matrix 1 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m2src", "Matrix 2 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m2dst", "Matrix 2 · Destino", kModDestChoices, 0, "m"),
    SPECF("m2amt", "Matrix 2 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m3src", "Matrix 3 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m3dst", "Matrix 3 · Destino", kModDestChoices, 0, "m"),
    SPECF("m3amt", "Matrix 3 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m4src", "Matrix 4 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m4dst", "Matrix 4 · Destino", kModDestChoices, 0, "m"),
    SPECF("m4amt", "Matrix 4 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m5src", "Matrix 5 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m5dst", "Matrix 5 · Destino", kModDestChoices, 0, "m"),
    SPECF("m5amt", "Matrix 5 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m6src", "Matrix 6 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m6dst", "Matrix 6 · Destino", kModDestChoices, 0, "m"),
    SPECF("m6amt", "Matrix 6 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m7src", "Matrix 7 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m7dst", "Matrix 7 · Destino", kModDestChoices, 0, "m"),
    SPECF("m7amt", "Matrix 7 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m8src", "Matrix 8 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m8dst", "Matrix 8 · Destino", kModDestChoices, 0, "m"),
    SPECF("m8amt", "Matrix 8 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m9src", "Matrix 9 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m9dst", "Matrix 9 · Destino", kModDestChoices, 0, "m"),
    SPECF("m9amt", "Matrix 9 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m10src", "Matrix 10 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m10dst", "Matrix 10 · Destino", kModDestChoices, 0, "m"),
    SPECF("m10amt", "Matrix 10 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m11src", "Matrix 11 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m11dst", "Matrix 11 · Destino", kModDestChoices, 0, "m"),
    SPECF("m11amt", "Matrix 11 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m12src", "Matrix 12 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m12dst", "Matrix 12 · Destino", kModDestChoices, 0, "m"),
    SPECF("m12amt", "Matrix 12 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m13src", "Matrix 13 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m13dst", "Matrix 13 · Destino", kModDestChoices, 0, "m"),
    SPECF("m13amt", "Matrix 13 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m14src", "Matrix 14 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m14dst", "Matrix 14 · Destino", kModDestChoices, 0, "m"),
    SPECF("m14amt", "Matrix 14 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m15src", "Matrix 15 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m15dst", "Matrix 15 · Destino", kModDestChoices, 0, "m"),
    SPECF("m15amt", "Matrix 15 · Cantidad", -1, 1, 1.f, 0.f, "m"),
    SPECC("m16src", "Matrix 16 · Fuente", kModSourceChoices, 0, "m"),
    SPECC("m16dst", "Matrix 16 · Destino", kModDestChoices, 0, "m"),
    SPECF("m16amt", "Matrix 16 · Cantidad", -1, 1, 1.f, 0.f, "m"),

    SPECF("macro1", "Macro 1 · Valor", 0, 1, 1.f, 0.5f, "macro"),
    SPECF("macro2", "Macro 2 · Valor", 0, 1, 1.f, 0.5f, "macro"),
    SPECF("macro3", "Macro 3 · Valor", 0, 1, 1.f, 0.5f, "macro"),
    SPECF("macro4", "Macro 4 · Valor", 0, 1, 1.f, 0.5f, "macro"),
    SPECF("macro5", "Macro 5 · Valor", 0, 1, 1.f, 0.5f, "macro"),
    SPECF("macro6", "Macro 6 · Valor", 0, 1, 1.f, 0.5f, "macro"),
    SPECF("macro7", "Macro 7 · Valor", 0, 1, 1.f, 0.5f, "macro"),
    SPECF("macro8", "Macro 8 · Valor", 0, 1, 1.f, 0.5f, "macro"),
    SPECC("macro1r1dst", "Macro 1 · Ruta 1 dest", kModDestChoices, 0, "macro"),
    SPECF("macro1r1amt", "Macro 1 · Ruta 1 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro1r2dst", "Macro 1 · Ruta 2 dest", kModDestChoices, 0, "macro"),
    SPECF("macro1r2amt", "Macro 1 · Ruta 2 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro1r3dst", "Macro 1 · Ruta 3 dest", kModDestChoices, 0, "macro"),
    SPECF("macro1r3amt", "Macro 1 · Ruta 3 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro1r4dst", "Macro 1 · Ruta 4 dest", kModDestChoices, 0, "macro"),
    SPECF("macro1r4amt", "Macro 1 · Ruta 4 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro2r1dst", "Macro 2 · Ruta 1 dest", kModDestChoices, 0, "macro"),
    SPECF("macro2r1amt", "Macro 2 · Ruta 1 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro2r2dst", "Macro 2 · Ruta 2 dest", kModDestChoices, 0, "macro"),
    SPECF("macro2r2amt", "Macro 2 · Ruta 2 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro2r3dst", "Macro 2 · Ruta 3 dest", kModDestChoices, 0, "macro"),
    SPECF("macro2r3amt", "Macro 2 · Ruta 3 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro2r4dst", "Macro 2 · Ruta 4 dest", kModDestChoices, 0, "macro"),
    SPECF("macro2r4amt", "Macro 2 · Ruta 4 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro3r1dst", "Macro 3 · Ruta 1 dest", kModDestChoices, 0, "macro"),
    SPECF("macro3r1amt", "Macro 3 · Ruta 1 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro3r2dst", "Macro 3 · Ruta 2 dest", kModDestChoices, 0, "macro"),
    SPECF("macro3r2amt", "Macro 3 · Ruta 2 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro3r3dst", "Macro 3 · Ruta 3 dest", kModDestChoices, 0, "macro"),
    SPECF("macro3r3amt", "Macro 3 · Ruta 3 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro3r4dst", "Macro 3 · Ruta 4 dest", kModDestChoices, 0, "macro"),
    SPECF("macro3r4amt", "Macro 3 · Ruta 4 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro4r1dst", "Macro 4 · Ruta 1 dest", kModDestChoices, 0, "macro"),
    SPECF("macro4r1amt", "Macro 4 · Ruta 1 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro4r2dst", "Macro 4 · Ruta 2 dest", kModDestChoices, 0, "macro"),
    SPECF("macro4r2amt", "Macro 4 · Ruta 2 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro4r3dst", "Macro 4 · Ruta 3 dest", kModDestChoices, 0, "macro"),
    SPECF("macro4r3amt", "Macro 4 · Ruta 3 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro4r4dst", "Macro 4 · Ruta 4 dest", kModDestChoices, 0, "macro"),
    SPECF("macro4r4amt", "Macro 4 · Ruta 4 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro5r1dst", "Macro 5 · Ruta 1 dest", kModDestChoices, 0, "macro"),
    SPECF("macro5r1amt", "Macro 5 · Ruta 1 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro5r2dst", "Macro 5 · Ruta 2 dest", kModDestChoices, 0, "macro"),
    SPECF("macro5r2amt", "Macro 5 · Ruta 2 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro5r3dst", "Macro 5 · Ruta 3 dest", kModDestChoices, 0, "macro"),
    SPECF("macro5r3amt", "Macro 5 · Ruta 3 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro5r4dst", "Macro 5 · Ruta 4 dest", kModDestChoices, 0, "macro"),
    SPECF("macro5r4amt", "Macro 5 · Ruta 4 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro6r1dst", "Macro 6 · Ruta 1 dest", kModDestChoices, 0, "macro"),
    SPECF("macro6r1amt", "Macro 6 · Ruta 1 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro6r2dst", "Macro 6 · Ruta 2 dest", kModDestChoices, 0, "macro"),
    SPECF("macro6r2amt", "Macro 6 · Ruta 2 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro6r3dst", "Macro 6 · Ruta 3 dest", kModDestChoices, 0, "macro"),
    SPECF("macro6r3amt", "Macro 6 · Ruta 3 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro6r4dst", "Macro 6 · Ruta 4 dest", kModDestChoices, 0, "macro"),
    SPECF("macro6r4amt", "Macro 6 · Ruta 4 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro7r1dst", "Macro 7 · Ruta 1 dest", kModDestChoices, 0, "macro"),
    SPECF("macro7r1amt", "Macro 7 · Ruta 1 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro7r2dst", "Macro 7 · Ruta 2 dest", kModDestChoices, 0, "macro"),
    SPECF("macro7r2amt", "Macro 7 · Ruta 2 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro7r3dst", "Macro 7 · Ruta 3 dest", kModDestChoices, 0, "macro"),
    SPECF("macro7r3amt", "Macro 7 · Ruta 3 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro7r4dst", "Macro 7 · Ruta 4 dest", kModDestChoices, 0, "macro"),
    SPECF("macro7r4amt", "Macro 7 · Ruta 4 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro8r1dst", "Macro 8 · Ruta 1 dest", kModDestChoices, 0, "macro"),
    SPECF("macro8r1amt", "Macro 8 · Ruta 1 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro8r2dst", "Macro 8 · Ruta 2 dest", kModDestChoices, 0, "macro"),
    SPECF("macro8r2amt", "Macro 8 · Ruta 2 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro8r3dst", "Macro 8 · Ruta 3 dest", kModDestChoices, 0, "macro"),
    SPECF("macro8r3amt", "Macro 8 · Ruta 3 cant", -1, 1, 1.f, 0.f, "macro"),
    SPECC("macro8r4dst", "Macro 8 · Ruta 4 dest", kModDestChoices, 0, "macro"),
    SPECF("macro8r4amt", "Macro 8 · Ruta 4 cant", -1, 1, 1.f, 0.f, "macro"),

    SPECC("fx1type", "FX 1 · Tipo", kFxChoices, 0, "fx"),
    SPECF("fx1mix", "FX 1 · Mezcla", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx1p1", "FX 1 · P1", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx1p2", "FX 1 · P2", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx1p3", "FX 1 · P3", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx1p4", "FX 1 · P4", 0, 1, 1.f, 0.5f, "fx"),
    SPECC("fx2type", "FX 2 · Tipo", kFxChoices, 0, "fx"),
    SPECF("fx2mix", "FX 2 · Mezcla", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx2p1", "FX 2 · P1", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx2p2", "FX 2 · P2", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx2p3", "FX 2 · P3", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx2p4", "FX 2 · P4", 0, 1, 1.f, 0.5f, "fx"),
    SPECC("fx3type", "FX 3 · Tipo", kFxChoices, 0, "fx"),
    SPECF("fx3mix", "FX 3 · Mezcla", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx3p1", "FX 3 · P1", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx3p2", "FX 3 · P2", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx3p3", "FX 3 · P3", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx3p4", "FX 3 · P4", 0, 1, 1.f, 0.5f, "fx"),
    SPECC("fx4type", "FX 4 · Tipo", kFxChoices, 0, "fx"),
    SPECF("fx4mix", "FX 4 · Mezcla", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx4p1", "FX 4 · P1", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx4p2", "FX 4 · P2", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx4p3", "FX 4 · P3", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx4p4", "FX 4 · P4", 0, 1, 1.f, 0.5f, "fx"),
    SPECC("fx5type", "FX 5 · Tipo", kFxChoices, 0, "fx"),
    SPECF("fx5mix", "FX 5 · Mezcla", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx5p1", "FX 5 · P1", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx5p2", "FX 5 · P2", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx5p3", "FX 5 · P3", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx5p4", "FX 5 · P4", 0, 1, 1.f, 0.5f, "fx"),
    SPECC("fx6type", "FX 6 · Tipo", kFxChoices, 0, "fx"),
    SPECF("fx6mix", "FX 6 · Mezcla", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx6p1", "FX 6 · P1", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx6p2", "FX 6 · P2", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx6p3", "FX 6 · P3", 0, 1, 1.f, 0.5f, "fx"),
    SPECF("fx6p4", "FX 6 · P4", 0, 1, 1.f, 0.5f, "fx"),

    SPECC("arpOn", "Arp · Activo", kOffOn, 0, "arp"),
    SPECC("arpMode", "Arp · Modo", kArpModeChoices, 0, "arp"),
    SPECC("arpRate", "Arp · Rate", kArpRateChoices, 3, "arp"),
    SPECI("arpOct", "Arp · Octavas", 1, 4, 1, "arp"),
    SPECF("arpGate", "Arp · Gate", 0.05f, 1, 1.f, 0.7f, "arp"),
    SPECF("arpSwing", "Arp · Swing", 0, 1, 1.f, 0.f, "arp"),
    SPECC("arpLatch", "Arp · Latch", kOffOn, 0, "arp"),
    SPECI("arpSteps", "Arp · Pasos", 1, kArpSteps, kArpSteps, "arp"),
    SPECI("arpStep1", "Arp · Paso 1", 0, 1, 1, "arp"),
    SPECI("arpStep2", "Arp · Paso 2", 0, 1, 1, "arp"),
    SPECI("arpStep3", "Arp · Paso 3", 0, 1, 1, "arp"),
    SPECI("arpStep4", "Arp · Paso 4", 0, 1, 1, "arp"),
    SPECI("arpStep5", "Arp · Paso 5", 0, 1, 1, "arp"),
    SPECI("arpStep6", "Arp · Paso 6", 0, 1, 1, "arp"),
    SPECI("arpStep7", "Arp · Paso 7", 0, 1, 1, "arp"),
    SPECI("arpStep8", "Arp · Paso 8", 0, 1, 1, "arp"),
    SPECI("arpStep9", "Arp · Paso 9", 0, 1, 1, "arp"),
    SPECI("arpStep10", "Arp · Paso 10", 0, 1, 1, "arp"),
    SPECI("arpStep11", "Arp · Paso 11", 0, 1, 1, "arp"),
    SPECI("arpStep12", "Arp · Paso 12", 0, 1, 1, "arp"),
    SPECI("arpStep13", "Arp · Paso 13", 0, 1, 1, "arp"),
    SPECI("arpStep14", "Arp · Paso 14", 0, 1, 1, "arp"),
    SPECI("arpStep15", "Arp · Paso 15", 0, 1, 1, "arp"),
    SPECI("arpStep16", "Arp · Paso 16", 0, 1, 1, "arp"),

    SPECF("warpamt", "LINES · Cantidad", 0, 1, 1.f, 0.8f, "lines"),
    SPECF("warpR", "LINES · Rango (st)", 1, 24, 1.f, 12.f, "lines"),
};

#undef SPECF
#undef SPECI
#undef SPECC
#undef SPECB

static_assert((int) std::size(kSpecs) == (int) kNumParams, "parameter table");

// =========================================================================
// 2. Presets
// =========================================================================
struct PresetSetting { int id; float value; };

struct PresetDef
{
    juce::String name;
    std::vector<PresetSetting> set;
};

// Small helper used by the preset table: keep the definitions readable.
struct PresetBuilder
{
    std::vector<PresetSetting> v;
    PresetBuilder& set(int id, float value)
    {
        v.push_back({ id, value });
        return *this;
    }
};

static std::vector<PresetDef> buildPresets();

static const std::vector<PresetDef>& presets()
{
    static const std::vector<PresetDef> list = buildPresets();
    return list;
}

static juce::StringArray presetNames()
{
    juce::StringArray a;
    for (const auto& p : presets())
        a.add(p.name);
    return a;
}

static int presetIndexOf(const juce::String& name)
{
    const auto names = presetNames();
    for (int i = 0; i < names.size(); ++i)
        if (names[i].equalsIgnoreCase(name.trim()))
            return i;
    for (int i = 0; i < names.size(); ++i)
        if (names[i].containsIgnoreCase(name.trim()))
            return i;
    return -1;
}

// Layer/engine values used by the builders. Keep in sync with the choice
// lists above.
enum EngineType { kOff = 0, kAnalog = 1, kWavetable = 2, kFm = 3, kSample = 4 };
enum FilterType { kLp12 = 0, kLp24 = 1, kHp12 = 2, kHp24 = 3, kBp12 = 4, kNotch = 5 };
enum FxType { kFxOff = 0, kFxDrive, kFxChorus, kFxPhaser, kFxDelay, kFxReverb, kFxComp, kFxEq };
enum ModSource
{
    kSrcOff = 0, kSrcLfo1, kSrcLfo2, kSrcLfo3, kSrcLfo4, kSrcRnd1, kSrcRnd2,
    kSrcEnv1, kSrcEnv2, kSrcEnv3, kSrcEnv4, kSrcVel, kSrcKey, kSrcModWheel,
    kSrcAftertouch, kSrcTimbre, kSrcPitchBend, kSrcMacro1, kSrcMacro2,
    kSrcMacro3, kSrcMacro4, kSrcMacro5, kSrcMacro6, kSrcMacro7, kSrcMacro8,
    kSrcExpression, kSrcNote
};
enum ModDest
{
    kDstOff = 0, kDstPitch, kDstF1Cut, kDstF1Res, kDstF2Cut, kDstF2Res,
    kDstAmp, kDstPan, kDstALevel, kDstBLevel, kDstAWtPos, kDstBWtPos,
    kDstAFmIndex, kDstBFmIndex, kDstNoise, kDstDrive, kDstFx1, kDstFx2,
    kDstFx3, kDstFx4, kDstFx5, kDstFx6, kDstMorph, kDstLfo1Rate,
    kDstLfo2Rate, kDstAuxLevel, kDstFilterBalance, kDstWidth
};

static PresetBuilder make(const char* name)
{
    return PresetBuilder{}.set(p_motor, 1).set(p_preset, 0)
        .set(p_a_engine, (float) kAnalog).set(p_a_level, 0.85f)
        .set(p_b_level, 0.f).set(p_x_level, 0.f);
}

static PresetDef mk(const char* name, PresetBuilder b)
{
    return PresetDef{ u8(name), std::move(b.v) };
}

static std::vector<PresetDef> buildPresets()
{
    std::vector<PresetDef> list;
    list.reserve(180);

    list.push_back(mk("Init", make("Init")));

    // ---- Bass -----------------------------------------------------------
    list.push_back(mk("Bass · Sub Zero", make("Bass · Sub Zero")
        .set(p_o1wave, 2).set(p_o1lvl, 0.9f)
        .set(p_o2wave, 2).set(p_o2lvl, 0.5f).set(p_o2det, 9)
        .set(p_sublvl, 0.55f).set(p_flttype, kLp24).set(p_fltcut, 650)
        .set(p_fltres, 0.35f).set(p_fltenv, 22).set(p_fenvD, 0.22f)
        .set(p_fenvS, 0.f).set(p_aenvA, 0.003f).set(p_aenvD, 0.35f)
        .set(p_aenvS, 0.85f).set(p_aenvR, 0.2f).set(p_fltdrive, 1.6f)
        .set(p_fx1type, (float) kFxComp).set(p_fx1mix, 0.45f)
        .set(p_fx1p1, 0.35f).set(p_fx1p2, 0.65f)));

    list.push_back(mk("Bass · Acid Line", make("Bass · Acid Line")
        .set(p_o1wave, 2).set(p_o1lvl, 0.92f).set(p_o2wave, 3)
        .set(p_o2lvl, 0.18f).set(p_flttype, kLp24).set(p_fltcut, 280)
        .set(p_fltres, 0.72f).set(p_fltenv, 34).set(p_fenvD, 0.11f)
        .set(p_fenvS, 0.f).set(p_aenvA, 0.002f).set(p_aenvD, 0.28f)
        .set(p_aenvS, 0.5f).set(p_aenvR, 0.13f).set(p_glide, 0.055f)
        .set(p_fltdrive, 2.2f).set(p_fx1type, (float) kFxDrive)
        .set(p_fx1mix, 0.3f).set(p_fx1p1, 0.45f)));

    list.push_back(mk("Bass · Growl", make("Bass · Growl")
        .set(p_o1wave, 2).set(p_o1lvl, 0.8f).set(p_o2wave, 4)
        .set(p_o2lvl, 0.4f).set(p_o2det, -8).set(p_sublvl, 0.35f)
        .set(p_flttype, kLp24).set(p_fltcut, 520).set(p_fltres, 0.45f)
        .set(p_fltenv, 28).set(p_fenvD, 0.4f).set(p_fenvS, 0.25f)
        .set(p_aenvA, 0.01f).set(p_aenvD, 0.5f).set(p_aenvS, 0.65f)
        .set(p_aenvR, 0.25f).set(p_l1shape, 0).set(p_l1rate, 6.4f)
        .set(p_l1depth, 0.16f).set(p_l1dest, 1).set(p_fltdrive, 2.8f)
        .set(p_m1src, (float) kSrcLfo1).set(p_m1dst, (float) kDstAFmIndex)
        .set(p_m1amt, 0.25f)));

    list.push_back(mk("Bass · Rubber", make("Bass · Rubber")
        .set(p_o1wave, 1).set(p_o1lvl, 0.9f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.35f).set(p_o2oct, -1).set(p_flttype, kLp12)
        .set(p_fltcut, 420).set(p_fltres, 0.55f).set(p_fltenv, 38)
        .set(p_fenvA, 0.002f).set(p_fenvD, 0.35f).set(p_fenvS, 0.f)
        .set(p_aenvA, 0.004f).set(p_aenvD, 0.5f).set(p_aenvS, 0.55f)
        .set(p_aenvR, 0.18f).set(p_l1rate, 3.2f).set(p_l1depth, 0.1f)
        .set(p_l1dest, 1)));

    list.push_back(mk("Bass · FM Punch", make("Bass · FM Punch")
        .set(p_a_engine, (float) kFm).set(p_a_fmRatio, 1.f)
        .set(p_a_fmIndex, 3.2f).set(p_a_fmFeedback, 0.15f)
        .set(p_a_level, 0.9f).set(p_sublvl, 0.3f).set(p_flttype, kLp12)
        .set(p_fltcut, 3000).set(p_fltenv, 12).set(p_fenvD, 0.18f)
        .set(p_fenvS, 0.f).set(p_aenvA, 0.002f).set(p_aenvD, 0.6f)
        .set(p_aenvS, 0.5f).set(p_aenvR, 0.2f)
        .set(p_fx1type, (float) kFxComp).set(p_fx1mix, 0.4f)));

    list.push_back(mk("Bass · Wavetable", make("Bass · Wavetable")
        .set(p_a_engine, (float) kWavetable).set(p_a_wtTable, 1)
        .set(p_a_wtPos, 0.28f).set(p_a_wtWarp, 0.18f)
        .set(p_o1wave, 2).set(p_o1lvl, 0.2f).set(p_sublvl, 0.5f)
        .set(p_flttype, kLp24).set(p_fltcut, 700).set(p_fltres, 0.3f)
        .set(p_fltenv, 20).set(p_fenvD, 0.3f).set(p_fenvS, 0.1f)
        .set(p_aenvA, 0.005f).set(p_aenvD, 0.4f).set(p_aenvS, 0.8f)
        .set(p_aenvR, 0.25f).set(p_l2rate, 0.18f).set(p_l2depth, 0.12f)
        .set(p_l2dest, 1)));

    list.push_back(mk("Bass · Reese", make("Bass · Reese")
        .set(p_o1wave, 2).set(p_o1lvl, 0.75f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.75f).set(p_o2det, -24).set(p_o3wave, 2)
        .set(p_o3lvl, 0.45f).set(p_o3det, 22).set(p_uni, 4)
        .set(p_unispr, 18).set(p_sublvl, 0.45f).set(p_flttype, kLp24)
        .set(p_fltcut, 1200).set(p_fltres, 0.22f).set(p_aenvA, 0.01f)
        .set(p_aenvD, 0.4f).set(p_aenvS, 0.9f).set(p_aenvR, 0.3f)
        .set(p_l1rate, 0.22f).set(p_l1depth, 0.18f).set(p_l1dest, 1)));

    list.push_back(mk("Bass · Pluck", make("Bass · Pluck")
        .set(p_o1wave, 1).set(p_o1lvl, 0.9f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.35f).set(p_o2oct, 1).set(p_flttype, kLp24)
        .set(p_fltcut, 900).set(p_fltres, 0.35f).set(p_fltenv, 42)
        .set(p_fenvA, 0.001f).set(p_fenvD, 0.13f).set(p_fenvS, 0.f)
        .set(p_aenvA, 0.001f).set(p_aenvD, 0.28f).set(p_aenvS, 0.f)
        .set(p_aenvR, 0.18f).set(p_fx1type, (float) kFxDelay)
        .set(p_fx1mix, 0.18f).set(p_fx1p1, 0.25f)));

    // ---- Leads ----------------------------------------------------------
    list.push_back(mk("Lead · Neon", make("Lead · Neon")
        .set(p_o1wave, 2).set(p_o1lvl, 0.8f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.55f).set(p_o2det, -12).set(p_o3wave, 4)
        .set(p_o3lvl, 0.25f).set(p_uni, 5).set(p_unispr, 20)
        .set(p_flttype, kLp24).set(p_fltcut, 7200).set(p_fltres, 0.18f)
        .set(p_aenvA, 0.01f).set(p_aenvD, 0.35f).set(p_aenvS, 0.85f)
        .set(p_aenvR, 0.28f).set(p_l1rate, 5.3f).set(p_l1depth, 0.1f)
        .set(p_l1dest, 0).set(p_fx1type, (float) kFxChorus)
        .set(p_fx1mix, 0.22f).set(p_fx2type, (float) kFxDelay)
        .set(p_fx2mix, 0.18f).set(p_fx2p1, 0.3f)));

    list.push_back(mk("Lead · Screamer", make("Lead · Screamer")
        .set(p_o1wave, 2).set(p_o1lvl, 0.85f).set(p_o2wave, 3)
        .set(p_o2lvl, 0.35f).set(p_o2det, 16).set(p_uni, 3)
        .set(p_unispr, 12).set(p_flttype, kLp24).set(p_fltcut, 5400)
        .set(p_fltres, 0.38f).set(p_fltenv, 16).set(p_fenvD, 0.18f)
        .set(p_fenvS, 0.35f).set(p_aenvA, 0.005f).set(p_aenvD, 0.3f)
        .set(p_aenvS, 0.8f).set(p_aenvR, 0.24f).set(p_fltdrive, 2.4f)
        .set(p_fx1type, (float) kFxDrive).set(p_fx1mix, 0.35f)
        .set(p_fx2type, (float) kFxDelay).set(p_fx2mix, 0.16f)));

    list.push_back(mk("Lead · Detune", make("Lead · Detune")
        .set(p_o1wave, 2).set(p_o1lvl, 0.8f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.8f).set(p_o2det, 14).set(p_o3wave, 2)
        .set(p_o3lvl, 0.55f).set(p_o3det, -20).set(p_uni, 6)
        .set(p_unispr, 28).set(p_fltcut, 9500).set(p_aenvA, 0.02f)
        .set(p_aenvD, 0.4f).set(p_aenvS, 0.9f).set(p_aenvR, 0.35f)
        .set(p_l1rate, 5.5f).set(p_l1depth, 0.12f).set(p_l1dest, 0)));

    list.push_back(mk("Lead · FM Glass", make("Lead · FM Glass")
        .set(p_a_engine, (float) kFm).set(p_a_fmRatio, 2.f)
        .set(p_a_fmIndex, 2.4f).set(p_a_fmFeedback, 0.08f)
        .set(p_a_level, 0.82f).set(p_flttype, kLp12).set(p_fltcut, 9000)
        .set(p_aenvA, 0.003f).set(p_aenvD, 1.2f).set(p_aenvS, 0.25f)
        .set(p_aenvR, 0.8f).set(p_l1rate, 4.8f).set(p_l1depth, 0.08f)
        .set(p_l1dest, 0).set(p_fx1type, (float) kFxDelay)
        .set(p_fx1mix, 0.22f).set(p_fx1p1, 0.37f)
        .set(p_fx2type, (float) kFxReverb).set(p_fx2mix, 0.2f)));

    list.push_back(mk("Lead · Wavetable", make("Lead · Wavetable")
        .set(p_a_engine, (float) kWavetable).set(p_a_wtTable, 2)
        .set(p_a_wtPos, 0.62f).set(p_a_wtWarp, 0.3f)
        .set(p_uni, 4).set(p_unispr, 16).set(p_flttype, kLp24)
        .set(p_fltcut, 7800).set(p_fltres, 0.16f).set(p_aenvA, 0.01f)
        .set(p_aenvD, 0.35f).set(p_aenvS, 0.85f).set(p_aenvR, 0.3f)
        .set(p_l1rate, 0.4f).set(p_l1depth, 0.2f).set(p_l1dest, 1)
        .set(p_m1src, (float) kSrcLfo3).set(p_m1dst, (float) kDstAWtPos)
        .set(p_m1amt, 0.25f)));

    list.push_back(mk("Lead · Ribbon", make("Lead · Ribbon")
        .set(p_o1wave, 0).set(p_o1lvl, 0.75f).set(p_o2wave, 1)
        .set(p_o2lvl, 0.4f).set(p_o2oct, 1).set(p_o3wave, 2)
        .set(p_o3lvl, 0.25f).set(p_fltcut, 6000).set(p_aenvA, 0.08f)
        .set(p_aenvD, 0.7f).set(p_aenvS, 0.75f).set(p_aenvR, 0.6f)
        .set(p_l1rate, 4.2f).set(p_l1depth, 0.2f).set(p_l1dest, 0)
        .set(p_m1src, (float) kSrcAftertouch).set(p_m1dst, (float) kDstF1Cut)
        .set(p_m1amt, 0.4f).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.22f)));

    list.push_back(mk("Lead · Sync Cut", make("Lead · Sync Cut")
        .set(p_o1wave, 4).set(p_o1lvl, 0.8f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.4f).set(p_o2semi, 7).set(p_flttype, kLp24)
        .set(p_fltcut, 4200).set(p_fltres, 0.5f).set(p_fltenv, 30)
        .set(p_fenvD, 0.22f).set(p_fenvS, 0.1f).set(p_aenvA, 0.005f)
        .set(p_aenvD, 0.4f).set(p_aenvS, 0.7f).set(p_aenvR, 0.25f)
        .set(p_m1src, (float) kSrcEnv3).set(p_m1dst, (float) kDstPitch)
        .set(p_m1amt, -0.08f)));

    // ---- Pads -----------------------------------------------------------
    list.push_back(mk("Pad · Aurora", make("Pad · Aurora")
        .set(p_o1wave, 2).set(p_o1lvl, 0.65f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.65f).set(p_o2det, 12).set(p_o3wave, 1)
        .set(p_o3lvl, 0.55f).set(p_o3oct, -1).set(p_o3det, -8)
        .set(p_uni, 6).set(p_unispr, 30).set(p_fltcut, 3200)
        .set(p_fltres, 0.15f).set(p_fenvA, 1.2f).set(p_fenvD, 2.f)
        .set(p_fenvS, 0.6f).set(p_fenvR, 2.f).set(p_aenvA, 0.8f)
        .set(p_aenvD, 1.5f).set(p_aenvS, 0.8f).set(p_aenvR, 2.5f)
        .set(p_l2rate, 0.28f).set(p_l2depth, 0.3f).set(p_l2dest, 1)
        .set(p_fx1type, (float) kFxChorus).set(p_fx1mix, 0.3f)
        .set(p_fx2type, (float) kFxReverb).set(p_fx2mix, 0.38f)));

    list.push_back(mk("Pad · Warm Analog", make("Pad · Warm Analog")
        .set(p_o1wave, 2).set(p_o1lvl, 0.7f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.7f).set(p_o2det, -9).set(p_o3wave, 2)
        .set(p_o3lvl, 0.4f).set(p_o3det, 18).set(p_uni, 5)
        .set(p_unispr, 22).set(p_flttype, kLp24).set(p_fltcut, 2600)
        .set(p_fltres, 0.12f).set(p_fenvA, 0.8f).set(p_fenvD, 2.f)
        .set(p_fenvS, 0.65f).set(p_fenvR, 2.4f).set(p_aenvA, 0.55f)
        .set(p_aenvD, 1.6f).set(p_aenvS, 0.85f).set(p_aenvR, 2.2f)
        .set(p_l1rate, 0.22f).set(p_l1depth, 0.12f).set(p_l1dest, 1)
        .set(p_fx1type, (float) kFxChorus).set(p_fx1mix, 0.25f)
        .set(p_fx2type, (float) kFxReverb).set(p_fx2mix, 0.3f)));

    list.push_back(mk("Pad · Glass", make("Pad · Glass")
        .set(p_a_engine, (float) kWavetable).set(p_a_wtTable, 0)
        .set(p_a_wtPos, 0.4f).set(p_a_wtWarp, 0.25f).set(p_a_level, 0.7f)
        .set(p_b_engine, (float) kWavetable).set(p_b_wtTable, 3)
        .set(p_b_wtPos, 0.7f).set(p_b_level, 0.35f).set(p_b_tune, 12)
        .set(p_morph, 0.35f).set(p_fltcut, 7000).set(p_fltres, 0.1f)
        .set(p_aenvA, 1.4f).set(p_aenvD, 2.f).set(p_aenvS, 0.8f)
        .set(p_aenvR, 3.f).set(p_l3rate, 0.16f).set(p_l3depth, 0.25f)
        .set(p_m1src, (float) kSrcLfo3).set(p_m1dst, (float) kDstBWtPos)
        .set(p_m1amt, 0.35f).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.45f).set(p_fx2type, (float) kFxChorus)
        .set(p_fx2mix, 0.3f)));

    list.push_back(mk("Pad · Evolving WT", make("Pad · Evolving WT")
        .set(p_a_engine, (float) kWavetable).set(p_a_wtTable, 1)
        .set(p_a_wtPos, 0.2f).set(p_a_wtWarp, 0.4f).set(p_uni, 4)
        .set(p_unispr, 18).set(p_fltcut, 4200).set(p_aenvA, 0.6f)
        .set(p_aenvD, 2.5f).set(p_aenvS, 0.65f).set(p_aenvR, 3.2f)
        .set(p_l1rate, 0.12f).set(p_l1depth, 0.35f).set(p_l1dest, 1)
        .set(p_l2rate, 0.09f).set(p_l2depth, 0.3f).set(p_l2dest, 0)
        .set(p_m1src, (float) kSrcLfo4).set(p_m1dst, (float) kDstAWtPos)
        .set(p_m1amt, 0.4f).set(p_fx1type, (float) kFxPhaser)
        .set(p_fx1mix, 0.28f).set(p_fx2type, (float) kFxReverb)
        .set(p_fx2mix, 0.4f)));

    list.push_back(mk("Pad · Granular Air", make("Pad · Granular Air")
        .set(p_a_engine, (float) kSample).set(p_a_sampleTable, 3)
        .set(p_a_grainSize, 0.16f).set(p_a_grainDensity, 22.f)
        .set(p_a_grainSpray, 0.35f).set(p_a_grainTone, 0.6f)
        .set(p_a_level, 0.7f).set(p_noise, 0.08f).set(p_fltcut, 5600)
        .set(p_aenvA, 1.1f).set(p_aenvD, 2.f).set(p_aenvS, 0.75f)
        .set(p_aenvR, 3.f).set(p_l2rate, 0.18f).set(p_l2depth, 0.2f)
        .set(p_l2dest, 1).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.5f)));

    list.push_back(mk("Pad · Choir", make("Pad · Choir")
        .set(p_o1wave, 0).set(p_o1lvl, 0.55f).set(p_o2wave, 1)
        .set(p_o2lvl, 0.5f).set(p_o2oct, 1).set(p_o3wave, 0)
        .set(p_o3lvl, 0.3f).set(p_o3oct, 2).set(p_uni, 7)
        .set(p_unispr, 12).set(p_fltcut, 4200).set(p_aenvA, 0.9f)
        .set(p_aenvD, 1.5f).set(p_aenvS, 0.8f).set(p_aenvR, 2.f)
        .set(p_l1rate, 0.35f).set(p_l1depth, 0.08f).set(p_l1dest, 0)
        .set(p_fx1type, (float) kFxChorus).set(p_fx1mix, 0.35f)
        .set(p_fx2type, (float) kFxReverb).set(p_fx2mix, 0.32f)));

    list.push_back(mk("Pad · Dark Matter", make("Pad · Dark Matter")
        .set(p_a_engine, (float) kFm).set(p_a_fmRatio, 0.5f)
        .set(p_a_fmIndex, 1.8f).set(p_a_fmFeedback, 0.2f)
        .set(p_b_engine, (float) kWavetable).set(p_b_wtTable, 1)
        .set(p_b_wtPos, 0.1f).set(p_b_level, 0.4f).set(p_morph, 0.4f)
        .set(p_flttype, kLp24).set(p_fltcut, 1400).set(p_fltres, 0.25f)
        .set(p_aenvA, 1.6f).set(p_aenvD, 2.f).set(p_aenvS, 0.7f)
        .set(p_aenvR, 3.5f).set(p_l1rate, 0.07f).set(p_l1depth, 0.18f)
        .set(p_l1dest, 1).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.45f)));

    list.push_back(mk("Pad · Shimmer", make("Pad · Shimmer")
        .set(p_o1wave, 0).set(p_o1lvl, 0.7f).set(p_o1oct, 1)
        .set(p_o2wave, 1).set(p_o2lvl, 0.45f).set(p_o2oct, 2)
        .set(p_o3wave, 2).set(p_o3lvl, 0.2f).set(p_fltcut, 9000)
        .set(p_aenvA, 1.2f).set(p_aenvD, 2.f).set(p_aenvS, 0.55f)
        .set(p_aenvR, 4.f).set(p_l1rate, 0.3f).set(p_l1depth, 0.2f)
        .set(p_l1dest, 0).set(p_fx1type, (float) kFxChorus)
        .set(p_fx1mix, 0.4f).set(p_fx2type, (float) kFxReverb)
        .set(p_fx2mix, 0.55f)));

    // ---- Keys -----------------------------------------------------------
    list.push_back(mk("Keys · Electric", make("Keys · Electric")
        .set(p_o1wave, 0).set(p_o1lvl, 0.9f).set(p_o2wave, 1)
        .set(p_o2lvl, 0.45f).set(p_o2oct, 1).set(p_o2det, 4)
        .set(p_fltcut, 9000).set(p_aenvA, 0.005f).set(p_aenvD, 0.9f)
        .set(p_aenvS, 0.3f).set(p_aenvR, 0.5f).set(p_vel, 0.55f)
        .set(p_fx1type, (float) kFxChorus).set(p_fx1mix, 0.18f)));

    list.push_back(mk("Keys · Bell", make("Keys · Bell")
        .set(p_a_engine, (float) kFm).set(p_a_fmRatio, 3.5f)
        .set(p_a_fmIndex, 2.1f).set(p_a_level, 0.75f)
        .set(p_b_engine, (float) kFm).set(p_b_fmRatio, 7.f)
        .set(p_b_fmIndex, 0.8f).set(p_b_level, 0.22f).set(p_b_tune, 12)
        .set(p_morph, 0.2f).set(p_aenvA, 0.001f).set(p_aenvD, 2.2f)
        .set(p_aenvS, 0.f).set(p_aenvR, 2.5f).set(p_l1rate, 6.5f)
        .set(p_l1depth, 0.06f).set(p_l1dest, 0).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.28f)));

    list.push_back(mk("Keys · Marimba", make("Keys · Marimba")
        .set(p_o1wave, 0).set(p_o1lvl, 0.9f).set(p_o2wave, 0)
        .set(p_o2lvl, 0.4f).set(p_o2oct, 2).set(p_o2det, 3)
        .set(p_o3wave, 1).set(p_o3lvl, 0.2f).set(p_o3oct, 1)
        .set(p_fltcut, 7000).set(p_aenvA, 0.001f).set(p_aenvD, 0.45f)
        .set(p_aenvS, 0.f).set(p_aenvR, 0.35f).set(p_vel, 0.6f)));

    list.push_back(mk("Keys · Organ", make("Keys · Organ")
        .set(p_o1wave, 3).set(p_o1lvl, 0.65f).set(p_o2wave, 3)
        .set(p_o2lvl, 0.45f).set(p_o2oct, 1).set(p_o3wave, 0)
        .set(p_o3lvl, 0.55f).set(p_o3oct, -1).set(p_flttype, kLp12)
        .set(p_fltcut, 12000).set(p_aenvA, 0.01f).set(p_aenvD, 0.1f)
        .set(p_aenvS, 1.f).set(p_aenvR, 0.12f).set(p_l1rate, 6.f)
        .set(p_l1depth, 0.05f).set(p_l1dest, 0)
        .set(p_fx1type, (float) kFxChorus).set(p_fx1mix, 0.2f)));

    list.push_back(mk("Keys · Clav", make("Keys · Clav")
        .set(p_o1wave, 4).set(p_o1lvl, 0.8f).set(p_o2wave, 3)
        .set(p_o2lvl, 0.25f).set(p_o2det, 7).set(p_flttype, kLp12)
        .set(p_fltcut, 3800).set(p_fltres, 0.3f).set(p_fltenv, 25)
        .set(p_fenvA, 0.001f).set(p_fenvD, 0.15f).set(p_fenvS, 0.f)
        .set(p_aenvA, 0.001f).set(p_aenvD, 0.5f).set(p_aenvS, 0.15f)
        .set(p_aenvR, 0.2f).set(p_vel, 0.65f)));

    list.push_back(mk("Keys · Toy Piano", make("Keys · Toy Piano")
        .set(p_o1wave, 0).set(p_o1lvl, 0.8f).set(p_o1oct, 1)
        .set(p_o2wave, 1).set(p_o2lvl, 0.35f).set(p_o2oct, 2)
        .set(p_noise, 0.05f).set(p_fltcut, 8500).set(p_aenvA, 0.001f)
        .set(p_aenvD, 0.32f).set(p_aenvS, 0.f).set(p_aenvR, 0.25f)
        .set(p_fx1type, (float) kFxDelay).set(p_fx1mix, 0.12f)));

    // ---- Plucks ---------------------------------------------------------
    list.push_back(mk("Pluck · Crystal", make("Pluck · Crystal")
        .set(p_o1wave, 1).set(p_o1lvl, 0.85f).set(p_o2wave, 3)
        .set(p_o2lvl, 0.25f).set(p_o2oct, 1).set(p_a_engine, (float) kWavetable)
        .set(p_a_wtTable, 0).set(p_a_wtPos, 0.25f).set(p_a_level, 0.65f)
        .set(p_fltcut, 8600).set(p_fltres, 0.35f).set(p_fltenv, 38)
        .set(p_fenvA, 0.001f).set(p_fenvD, 0.14f).set(p_fenvS, 0.f)
        .set(p_aenvA, 0.001f).set(p_aenvD, 0.32f).set(p_aenvS, 0.f)
        .set(p_aenvR, 0.28f).set(p_fx1type, (float) kFxDelay)
        .set(p_fx1mix, 0.2f).set(p_fx2type, (float) kFxReverb)
        .set(p_fx2mix, 0.2f)));

    list.push_back(mk("Pluck · Wood", make("Pluck · Wood")
        .set(p_o1wave, 1).set(p_o1lvl, 0.8f).set(p_o2wave, 0)
        .set(p_o2lvl, 0.25f).set(p_o2oct, 1).set(p_o3wave, 2)
        .set(p_o3lvl, 0.15f).set(p_flttype, kLp24).set(p_fltcut, 2200)
        .set(p_fltres, 0.3f).set(p_fltenv, 30).set(p_fenvA, 0.001f)
        .set(p_fenvD, 0.16f).set(p_fenvS, 0.f).set(p_aenvA, 0.001f)
        .set(p_aenvD, 0.4f).set(p_aenvS, 0.05f).set(p_aenvR, 0.25f)));

    list.push_back(mk("Pluck · Nylon", make("Pluck · Nylon")
        .set(p_o1wave, 1).set(p_o1lvl, 0.85f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.2f).set(p_fltcut, 4800).set(p_fltres, 0.25f)
        .set(p_aenvA, 0.002f).set(p_aenvD, 0.6f).set(p_aenvS, 0.15f)
        .set(p_aenvR, 0.3f).set(p_vel, 0.5f)
        .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.18f)));

    list.push_back(mk("Pluck · Digital", make("Pluck · Digital")
        .set(p_a_engine, (float) kWavetable).set(p_a_wtTable, 2)
        .set(p_a_wtPos, 0.45f).set(p_a_wtWarp, 0.2f).set(p_a_level, 0.8f)
        .set(p_fltcut, 6500).set(p_fltres, 0.2f).set(p_fltenv, 35)
        .set(p_fenvD, 0.12f).set(p_fenvS, 0.f).set(p_aenvA, 0.001f)
        .set(p_aenvD, 0.28f).set(p_aenvS, 0.f).set(p_aenvR, 0.22f)
        .set(p_fx1type, (float) kFxDelay).set(p_fx1mix, 0.22f)));

    list.push_back(mk("Pluck · Grain", make("Pluck · Grain")
        .set(p_a_engine, (float) kSample).set(p_a_sampleTable, 2)
        .set(p_a_grainSize, 0.06f).set(p_a_grainDensity, 30.f)
        .set(p_a_grainSpray, 0.15f).set(p_a_grainTone, 0.55f)
        .set(p_a_level, 0.8f).set(p_fltcut, 5200).set(p_aenvA, 0.002f)
        .set(p_aenvD, 0.3f).set(p_aenvS, 0.f).set(p_aenvR, 0.3f)
        .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.25f)));

    // ---- Arps and sequences --------------------------------------------
    list.push_back(mk("Arp · Pulse", make("Arp · Pulse")
        .set(p_o1wave, 3).set(p_o1lvl, 0.8f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.4f).set(p_flttype, kLp24).set(p_fltcut, 4200)
        .set(p_fltres, 0.3f).set(p_aenvA, 0.002f).set(p_aenvD, 0.2f)
        .set(p_aenvS, 0.4f).set(p_aenvR, 0.15f).set(p_arpOn, 1)
        .set(p_arpRate, 3).set(p_arpGate, 0.6f).set(p_arpOct, 2)
        .set(p_fx1type, (float) kFxDelay).set(p_fx1mix, 0.18f)));

    list.push_back(mk("Arp · Binary", make("Arp · Binary")
        .set(p_a_engine, (float) kWavetable).set(p_a_wtTable, 3)
        .set(p_a_wtPos, 0.55f).set(p_a_level, 0.75f).set(p_fltcut, 5200)
        .set(p_fltres, 0.25f).set(p_aenvA, 0.001f).set(p_aenvD, 0.18f)
        .set(p_aenvS, 0.2f).set(p_aenvR, 0.12f).set(p_arpOn, 1)
        .set(p_arpMode, 2).set(p_arpRate, 3).set(p_arpGate, 0.45f)
        .set(p_arpOct, 3).set(p_fx1type, (float) kFxDelay)
        .set(p_fx1mix, 0.21f).set(p_fx2type, (float) kFxReverb)
        .set(p_fx2mix, 0.16f)));

    list.push_back(mk("Arp · Trance Gate", make("Arp · Trance Gate")
        .set(p_o1wave, 2).set(p_o1lvl, 0.75f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.75f).set(p_o2det, 12).set(p_uni, 5)
        .set(p_unispr, 24).set(p_fltcut, 4800).set(p_aenvA, 0.01f)
        .set(p_aenvD, 0.3f).set(p_aenvS, 0.85f).set(p_aenvR, 0.3f)
        .set(p_arpOn, 1).set(p_arpRate, 3).set(p_arpGate, 0.35f)
        .set(p_arpSteps, 8).set(p_arpStep8, 0)
        .set(p_fx1type, (float) kFxChorus).set(p_fx1mix, 0.3f)
        .set(p_fx2type, (float) kFxDelay).set(p_fx2mix, 0.2f)));

    list.push_back(mk("Arp · Random Walk", make("Arp · Random Walk")
        .set(p_a_engine, (float) kFm).set(p_a_fmRatio, 1.5f)
        .set(p_a_fmIndex, 1.4f).set(p_a_level, 0.75f)
        .set(p_fltcut, 6500).set(p_aenvA, 0.001f).set(p_aenvD, 0.25f)
        .set(p_aenvS, 0.1f).set(p_aenvR, 0.2f).set(p_arpOn, 1)
        .set(p_arpMode, 3).set(p_arpRate, 4).set(p_arpGate, 0.5f)
        .set(p_arpOct, 2).set(p_fx1type, (float) kFxDelay)
        .set(p_fx1mix, 0.3f).set(p_fx1p1, 0.43f)));

    list.push_back(mk("Arp · 16th Pluck", make("Arp · 16th Pluck")
        .set(p_o1wave, 1).set(p_o1lvl, 0.8f).set(p_o2wave, 4)
        .set(p_o2lvl, 0.2f).set(p_fltcut, 5600).set(p_fltres, 0.28f)
        .set(p_aenvA, 0.001f).set(p_aenvD, 0.18f).set(p_aenvS, 0.f)
        .set(p_aenvR, 0.1f).set(p_arpOn, 1).set(p_arpRate, 3)
        .set(p_arpGate, 0.4f).set(p_arpOct, 2)
        .set(p_fx1type, (float) kFxDelay).set(p_fx1mix, 0.16f)));

    // ---- Textures and FX ------------------------------------------------
    list.push_back(mk("Texture · Riser", make("Texture · Riser")
        .set(p_o1wave, 2).set(p_o1lvl, 0.55f).set(p_noise, 0.55f)
        .set(p_x_engine, 1).set(p_x_level, 0.3f)
        .set(p_flttype, kLp24).set(p_fltcut, 700).set(p_fltres, 0.5f)
        .set(p_fltenv, 48).set(p_fenvA, 4.f).set(p_fenvD, 5.f)
        .set(p_fenvS, 1.f).set(p_aenvA, 0.8f).set(p_aenvD, 4.f)
        .set(p_aenvS, 0.9f).set(p_aenvR, 1.2f).set(p_l1shape, 2)
        .set(p_l1rate, 0.4f).set(p_l1depth, 0.5f).set(p_l1dest, 0)
        .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.45f)));

    list.push_back(mk("Texture · Downlifter", make("Texture · Downlifter")
        .set(p_o1wave, 2).set(p_o1lvl, 0.4f).set(p_noise, 0.35f)
        .set(p_fltcut, 9000).set(p_fltenv, -48).set(p_fenvA, 0.2f)
        .set(p_fenvD, 2.5f).set(p_fenvS, 0.f).set(p_aenvA, 0.01f)
        .set(p_aenvD, 2.f).set(p_aenvS, 0.4f).set(p_aenvR, 1.2f)
        .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.5f)));

    list.push_back(mk("Texture · Noise Bed", make("Texture · Noise Bed")
        .set(p_o1lvl, 0.f).set(p_noise, 0.5f).set(p_x_engine, 1)
        .set(p_x_level, 0.55f).set(p_x_color, 0.25f)
        .set(p_flttype, kLp12).set(p_fltcut, 2600).set(p_fltres, 0.15f)
        .set(p_aenvA, 2.f).set(p_aenvD, 3.f).set(p_aenvS, 0.8f)
        .set(p_aenvR, 4.f).set(p_l1rate, 0.08f).set(p_l1depth, 0.2f)
        .set(p_l1dest, 1).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.45f)));

    list.push_back(mk("Texture · Metallic", make("Texture · Metallic")
        .set(p_a_engine, (float) kFm).set(p_a_fmRatio, 7.3f)
        .set(p_a_fmIndex, 3.8f).set(p_a_fmFeedback, 0.35f)
        .set(p_a_level, 0.6f).set(p_b_engine, (float) kFm)
        .set(p_b_fmRatio, 11.f).set(p_b_fmIndex, 1.2f).set(p_b_level, 0.3f)
        .set(p_b_tune, 7).set(p_morph, 0.4f).set(p_fltcut, 8000)
        .set(p_fltres, 0.35f).set(p_aenvA, 0.001f).set(p_aenvD, 1.8f)
        .set(p_aenvS, 0.f).set(p_aenvR, 2.2f).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.4f)));

    list.push_back(mk("Texture · Drone", make("Texture · Drone")
        .set(p_a_engine, (float) kSample).set(p_a_sampleTable, 3)
        .set(p_a_grainSize, 0.25f).set(p_a_grainDensity, 8.f)
        .set(p_a_grainSpray, 0.5f).set(p_a_level, 0.55f)
        .set(p_b_engine, (float) kAnalog).set(p_b_level, 0.25f)
        .set(p_b_tune, -12).set(p_morph, 0.3f).set(p_fltcut, 1800)
        .set(p_aenvA, 2.f).set(p_aenvD, 3.f).set(p_aenvS, 0.9f)
        .set(p_aenvR, 5.f).set(p_l1rate, 0.05f).set(p_l1depth, 0.3f)
        .set(p_l1dest, 1).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.55f).set(p_fx2type, (float) kFxPhaser)
        .set(p_fx2mix, 0.25f)));

    list.push_back(mk("Texture · Granular Cloud", make("Texture · Granular Cloud")
        .set(p_a_engine, (float) kSample).set(p_a_sampleTable, 0)
        .set(p_a_grainSize, 0.2f).set(p_a_grainDensity, 28.f)
        .set(p_a_grainSpray, 0.7f).set(p_a_grainTone, 0.65f)
        .set(p_a_level, 0.55f).set(p_b_engine, (float) kWavetable)
        .set(p_b_wtTable, 3).set(p_b_wtPos, 0.35f).set(p_b_level, 0.3f)
        .set(p_morph, 0.4f).set(p_fltcut, 4800).set(p_aenvA, 1.5f)
        .set(p_aenvD, 2.5f).set(p_aenvS, 0.7f).set(p_aenvR, 4.f)
        .set(p_l2rate, 0.13f).set(p_l2depth, 0.3f).set(p_l2dest, 1)
        .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.5f)));

    list.push_back(mk("Texture · Impact", make("Texture · Impact")
        .set(p_o1wave, 0).set(p_o1lvl, 0.7f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.3f).set(p_noise, 0.2f).set(p_x_engine, 2)
        .set(p_x_level, 0.4f).set(p_x_decay, 0.18f).set(p_fltcut, 12000)
        .set(p_fltenv, -40).set(p_fenvD, 0.4f).set(p_fenvS, 0.f)
        .set(p_aenvA, 0.001f).set(p_aenvD, 1.4f).set(p_aenvS, 0.f)
        .set(p_aenvR, 1.8f).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.4f)));

    // ---- Brass, strings and winds --------------------------------------
    list.push_back(mk("Brass · Analog", make("Brass · Analog")
        .set(p_o1wave, 2).set(p_o1lvl, 0.75f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.6f).set(p_o2det, -8).set(p_o3wave, 3)
        .set(p_o3lvl, 0.2f).set(p_uni, 3).set(p_unispr, 10)
        .set(p_flttype, kLp24).set(p_fltcut, 2800).set(p_fltres, 0.2f)
        .set(p_fltenv, 18).set(p_fenvA, 0.08f).set(p_fenvD, 0.6f)
        .set(p_fenvS, 0.65f).set(p_aenvA, 0.04f).set(p_aenvD, 0.5f)
        .set(p_aenvS, 0.85f).set(p_aenvR, 0.4f).set(p_l1rate, 5.f)
        .set(p_l1depth, 0.06f).set(p_l1dest, 0)
        .set(p_fx1type, (float) kFxChorus).set(p_fx1mix, 0.15f)));

    list.push_back(mk("Strings · Machine", make("Strings · Machine")
        .set(p_o1wave, 2).set(p_o1lvl, 0.7f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.7f).set(p_o2det, 16).set(p_o3wave, 2)
        .set(p_o3lvl, 0.5f).set(p_o3det, -16).set(p_o3oct, -1)
        .set(p_uni, 6).set(p_unispr, 24).set(p_fltcut, 4200)
        .set(p_fltres, 0.1f).set(p_fenvA, 0.9f).set(p_fenvD, 1.5f)
        .set(p_fenvS, 0.7f).set(p_fenvR, 1.6f).set(p_aenvA, 0.5f)
        .set(p_aenvD, 1.2f).set(p_aenvS, 0.85f).set(p_aenvR, 1.4f)
        .set(p_fx1type, (float) kFxChorus).set(p_fx1mix, 0.28f)
        .set(p_fx2type, (float) kFxReverb).set(p_fx2mix, 0.26f)));

    list.push_back(mk("Strings · Octave", make("Strings · Octave")
        .set(p_o1wave, 2).set(p_o1lvl, 0.7f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.55f).set(p_o2oct, 1).set(p_o3wave, 2)
        .set(p_o3lvl, 0.35f).set(p_o3oct, -1).set(p_uni, 5)
        .set(p_unispr, 18).set(p_fltcut, 5000).set(p_aenvA, 0.35f)
        .set(p_aenvD, 1.f).set(p_aenvS, 0.8f).set(p_aenvR, 1.f)
        .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.2f)));

    list.push_back(mk("Horn · Warm", make("Horn · Warm")
        .set(p_o1wave, 2).set(p_o1lvl, 0.7f).set(p_o2wave, 0)
        .set(p_o2lvl, 0.4f).set(p_o2oct, -1).set(p_o3wave, 1)
        .set(p_o3lvl, 0.25f).set(p_flttype, kLp12).set(p_fltcut, 2200)
        .set(p_fltres, 0.18f).set(p_fenvA, 0.12f).set(p_fenvD, 0.8f)
        .set(p_fenvS, 0.6f).set(p_aenvA, 0.08f).set(p_aenvD, 0.6f)
        .set(p_aenvS, 0.8f).set(p_aenvR, 0.5f).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.18f)));

    // ---- Hybrids and MPE ------------------------------------------------
    list.push_back(mk("Hybrid · Split Bass", make("Hybrid · Split Bass")
        .set(p_a_engine, (float) kAnalog).set(p_a_level, 0.8f)
        .set(p_b_engine, (float) kWavetable).set(p_b_wtTable, 1)
        .set(p_b_wtPos, 0.2f).set(p_b_level, 0.45f).set(p_b_tune, 12)
        .set(p_morph, 0.3f).set(p_flttype, kLp24).set(p_fltcut, 900)
        .set(p_fltres, 0.3f).set(p_fenvD, 0.3f).set(p_fenvS, 0.2f)
        .set(p_aenvA, 0.003f).set(p_aenvD, 0.5f).set(p_aenvS, 0.7f)
        .set(p_aenvR, 0.3f).set(p_fx1type, (float) kFxComp)
        .set(p_fx1mix, 0.3f)));

    list.push_back(mk("Hybrid · Morph Pad", make("Hybrid · Morph Pad")
        .set(p_a_engine, (float) kWavetable).set(p_a_wtTable, 0)
        .set(p_a_wtPos, 0.2f).set(p_a_level, 0.65f)
        .set(p_b_engine, (float) kSample).set(p_b_sampleTable, 3)
        .set(p_b_grainSize, 0.16f).set(p_b_grainDensity, 20.f)
        .set(p_b_grainSpray, 0.4f).set(p_b_level, 0.55f)
        .set(p_morph, 0.5f).set(p_fltcut, 4200).set(p_aenvA, 1.f)
        .set(p_aenvD, 2.f).set(p_aenvS, 0.75f).set(p_aenvR, 3.f)
        .set(p_l1rate, 0.15f).set(p_l1depth, 0.22f).set(p_l1dest, 1)
        .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.4f)));

    list.push_back(mk("Hybrid · Crossfade Keys", make("Hybrid · Crossfade Keys")
        .set(p_a_engine, (float) kAnalog).set(p_a_level, 0.7f)
        .set(p_o1wave, 0).set(p_o1lvl, 0.7f).set(p_o2wave, 2)
        .set(p_o2lvl, 0.25f).set(p_b_engine, (float) kFm)
        .set(p_b_fmRatio, 2.0f).set(p_b_fmIndex, 1.2f).set(p_b_level, 0.45f)
        .set(p_b_tune, 12).set(p_morph, 0.3f).set(p_fltcut, 7500)
        .set(p_aenvA, 0.005f).set(p_aenvD, 1.2f).set(p_aenvS, 0.25f)
        .set(p_aenvR, 0.8f).set(p_fx1type, (float) kFxChorus)
        .set(p_fx1mix, 0.18f)));

    list.push_back(mk("Hybrid · Dual Bass", make("Hybrid · Dual Bass")
        .set(p_a_engine, (float) kAnalog).set(p_a_level, 0.7f)
        .set(p_o1wave, 2).set(p_o1lvl, 0.8f).set(p_sublvl, 0.4f)
        .set(p_b_engine, (float) kFm).set(p_b_fmRatio, 0.5f)
        .set(p_b_fmIndex, 2.f).set(p_b_level, 0.4f).set(p_b_tune, -12)
        .set(p_morph, 0.35f).set(p_flttype, kLp24).set(p_fltcut, 750)
        .set(p_fltres, 0.3f).set(p_aenvA, 0.003f).set(p_aenvD, 0.4f)
        .set(p_aenvS, 0.8f).set(p_aenvR, 0.22f)
        .set(p_fx1type, (float) kFxDrive).set(p_fx1mix, 0.25f)));

    list.push_back(mk("MPE · Lead", make("MPE · Lead")
        .set(p_mpe, 1).set(p_mpeBend, 48).set(p_o1wave, 2)
        .set(p_o1lvl, 0.8f).set(p_o2wave, 2).set(p_o2lvl, 0.5f)
        .set(p_o2det, -10).set(p_uni, 3).set(p_unispr, 12)
        .set(p_flttype, kLp24).set(p_fltcut, 6000).set(p_fltres, 0.2f)
        .set(p_aenvA, 0.01f).set(p_aenvD, 0.4f).set(p_aenvS, 0.8f)
        .set(p_aenvR, 0.3f).set(p_m1src, (float) kSrcAftertouch)
        .set(p_m1dst, (float) kDstF1Cut).set(p_m1amt, 0.45f)
        .set(p_m2src, (float) kSrcTimbre).set(p_m2dst, (float) kDstAFmIndex)
        .set(p_m2amt, 0.4f).set(p_fx1type, (float) kFxDelay)
        .set(p_fx1mix, 0.2f)));

    list.push_back(mk("MPE · Pad", make("MPE · Pad")
        .set(p_mpe, 1).set(p_mpeBend, 48).set(p_o1wave, 2)
        .set(p_o1lvl, 0.65f).set(p_o2wave, 2).set(p_o2lvl, 0.65f)
        .set(p_o2det, 14).set(p_uni, 6).set(p_unispr, 28)
        .set(p_fltcut, 3600).set(p_aenvA, 1.f).set(p_aenvD, 2.f)
        .set(p_aenvS, 0.8f).set(p_aenvR, 3.f).set(p_m1src, (float) kSrcAftertouch)
        .set(p_m1dst, (float) kDstF1Cut).set(p_m1amt, 0.5f)
        .set(p_m2src, (float) kSrcTimbre).set(p_m2dst, (float) kDstPan)
        .set(p_m2amt, 0.4f).set(p_fx1type, (float) kFxChorus)
        .set(p_fx1mix, 0.3f).set(p_fx2type, (float) kFxReverb)
        .set(p_fx2mix, 0.4f)));

    list.push_back(mk("MPE · Texture", make("MPE · Texture")
        .set(p_mpe, 1).set(p_a_engine, (float) kSample)
        .set(p_a_sampleTable, 3).set(p_a_grainSize, 0.2f)
        .set(p_a_grainDensity, 18.f).set(p_a_grainSpray, 0.5f)
        .set(p_a_level, 0.65f).set(p_fltcut, 4200).set(p_aenvA, 1.5f)
        .set(p_aenvD, 2.f).set(p_aenvS, 0.7f).set(p_aenvR, 3.5f)
        .set(p_m1src, (float) kSrcTimbre).set(p_m1dst, (float) kDstAWtPos)
        .set(p_m1amt, 0.4f).set(p_m2src, (float) kSrcAftertouch)
        .set(p_m2dst, (float) kDstF1Cut).set(p_m2amt, 0.5f)
        .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.5f)));

    list.push_back(mk("MPE · Bell", make("MPE · Bell")
        .set(p_mpe, 1).set(p_a_engine, (float) kFm).set(p_a_fmRatio, 3.f)
        .set(p_a_fmIndex, 2.f).set(p_a_level, 0.75f)
        .set(p_aenvA, 0.001f).set(p_aenvD, 2.5f).set(p_aenvS, 0.f)
        .set(p_aenvR, 3.f).set(p_m1src, (float) kSrcAftertouch)
        .set(p_m1dst, (float) kDstAFmIndex).set(p_m1amt, 0.5f)
        .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.35f)));

    // ---- Drum/short percussion -----------------------------------------
    list.push_back(mk("Perc · Kick", make("Perc · Kick")
        .set(p_o1wave, 0).set(p_o1lvl, 1.0f).set(p_o1oct, -2)
        .set(p_o2wave, 0).set(p_o2lvl, 0.f).set(p_sublvl, 0.4f)
        .set(p_flttype, kLp12).set(p_fltcut, 1200).set(p_fltenv, -30)
        .set(p_fenvA, 0.001f).set(p_fenvD, 0.08f).set(p_fenvS, 0.f)
        .set(p_aenvA, 0.001f).set(p_aenvD, 0.32f).set(p_aenvS, 0.f)
        .set(p_aenvR, 0.12f).set(p_vel, 0.5f)));

    list.push_back(mk("Perc · Tom", make("Perc · Tom")
        .set(p_o1wave, 0).set(p_o1lvl, 0.85f).set(p_o1oct, -1)
        .set(p_o2wave, 1).set(p_o2lvl, 0.15f).set(p_fltcut, 1800)
        .set(p_aenvA, 0.001f).set(p_aenvD, 0.3f).set(p_aenvS, 0.f)
        .set(p_aenvR, 0.2f).set(p_l1rate, 3.f).set(p_l1depth, 0.1f)
        .set(p_l1dest, 0)));

    list.push_back(mk("Perc · Clave", make("Perc · Clave")
        .set(p_o1wave, 0).set(p_o1lvl, 0.7f).set(p_o1oct, 2)
        .set(p_o2wave, 0).set(p_o2lvl, 0.3f).set(p_o2oct, 3)
        .set(p_noise, 0.08f).set(p_fltcut, 7000).set(p_aenvA, 0.001f)
        .set(p_aenvD, 0.08f).set(p_aenvS, 0.f).set(p_aenvR, 0.05f)
        .set(p_vel, 0.65f)));

    list.push_back(mk("Perc · Snare", make("Perc · Snare")
        .set(p_o1lvl, 0.f).set(p_noise, 0.65f).set(p_x_engine, 2)
        .set(p_x_level, 0.3f).set(p_flttype, kHp12).set(p_fltcut, 1200)
        .set(p_fltres, 0.15f).set(p_aenvA, 0.001f).set(p_aenvD, 0.22f)
        .set(p_aenvS, 0.f).set(p_aenvR, 0.15f).set(p_aenvR, 0.15f)
        .set(p_fx1type, (float) kFxComp).set(p_fx1mix, 0.35f)));

    list.push_back(mk("Perc · Hat", make("Perc · Hat")
        .set(p_o1lvl, 0.f).set(p_noise, 0.7f).set(p_x_engine, 1)
        .set(p_x_level, 0.2f).set(p_x_decay, 0.05f)
        .set(p_flttype, kHp24).set(p_fltcut, 6000).set(p_aenvA, 0.001f)
        .set(p_aenvD, 0.06f).set(p_aenvS, 0.f).set(p_aenvR, 0.04f)
        .set(p_vel, 0.7f)));

    // ---- Extra bread-and-butter presets --------------------------------
    list.push_back(mk("Keys · Wurli", make("Keys · Wurli")
        .set(p_a_engine, (float) kFm).set(p_a_fmRatio, 1.0f)
        .set(p_a_fmIndex, 1.1f).set(p_a_level, 0.8f).set(p_o1wave, 0)
        .set(p_o1lvl, 0.2f).set(p_fltcut, 6000).set(p_aenvA, 0.004f)
        .set(p_aenvD, 1.2f).set(p_aenvS, 0.2f).set(p_aenvR, 0.6f)
        .set(p_vel, 0.55f).set(p_l1rate, 5.f).set(p_l1depth, 0.12f)
        .set(p_l1dest, 2)));

    list.push_back(mk("Keys · Harpsichord", make("Keys · Harpsichord")
        .set(p_o1wave, 4).set(p_o1lvl, 0.75f).set(p_o2wave, 3)
        .set(p_o2lvl, 0.3f).set(p_o2oct, 1).set(p_fltcut, 9000)
        .set(p_aenvA, 0.001f).set(p_aenvD, 0.4f).set(p_aenvS, 0.f)
        .set(p_aenvR, 0.2f).set(p_vel, 0.45f)));

    list.push_back(mk("Bass · 808 Long", make("Bass · 808 Long")
        .set(p_a_engine, (float) kAnalog).set(p_o1wave, 0)
        .set(p_o1lvl, 0.9f).set(p_o1oct, -1).set(p_sublvl, 0.8f)
        .set(p_flttype, kLp12).set(p_fltcut, 500).set(p_fltenv, 0)
        .set(p_aenvA, 0.002f).set(p_aenvD, 2.5f).set(p_aenvS, 0.15f)
        .set(p_aenvR, 0.4f).set(p_glide, 0.08f)
        .set(p_fx1type, (float) kFxDrive).set(p_fx1mix, 0.2f)));

    list.push_back(mk("Pad · Vocal", make("Pad · Vocal")
        .set(p_a_engine, (float) kWavetable).set(p_a_wtTable, 3)
        .set(p_a_wtPos, 0.5f).set(p_a_wtWarp, 0.2f).set(p_uni, 5)
        .set(p_unispr, 14).set(p_flttype, kLp12).set(p_fltcut, 3000)
        .set(p_fltres, 0.2f).set(p_aenvA, 0.7f).set(p_aenvD, 1.5f)
        .set(p_aenvS, 0.8f).set(p_aenvR, 2.f).set(p_l1rate, 0.28f)
        .set(p_l1depth, 0.12f).set(p_l1dest, 1)
        .set(p_fx1type, (float) kFxChorus).set(p_fx1mix, 0.28f)
        .set(p_fx2type, (float) kFxReverb).set(p_fx2mix, 0.3f)));

    list.push_back(mk("Lead · Square Solo", make("Lead · Square Solo")
        .set(p_o1wave, 3).set(p_o1lvl, 0.85f).set(p_o2wave, 3)
        .set(p_o2lvl, 0.4f).set(p_o2det, -6).set(p_flttype, kLp24)
        .set(p_fltcut, 5200).set(p_fltres, 0.25f).set(p_aenvA, 0.005f)
        .set(p_aenvD, 0.3f).set(p_aenvS, 0.85f).set(p_aenvR, 0.25f)
        .set(p_glide, 0.04f).set(p_fx1type, (float) kFxDelay)
        .set(p_fx1mix, 0.15f)));

    list.push_back(mk("Pluck · Koto", make("Pluck · Koto")
        .set(p_o1wave, 1).set(p_o1lvl, 0.8f).set(p_o2wave, 3)
        .set(p_o2lvl, 0.2f).set(p_o2oct, 1).set(p_flttype, kLp24)
        .set(p_fltcut, 3600).set(p_fltres, 0.35f).set(p_aenvA, 0.001f)
        .set(p_aenvD, 0.55f).set(p_aenvS, 0.f).set(p_aenvR, 0.4f)
        .set(p_l1rate, 6.f).set(p_l1depth, 0.1f).set(p_l1dest, 0)));

    list.push_back(mk("Texture · Wind", make("Texture · Wind")
        .set(p_o1lvl, 0.f).set(p_noise, 0.55f).set(p_x_engine, 4)
        .set(p_x_level, 0.3f).set(p_x_color, 0.35f)
        .set(p_flttype, kLp24).set(p_fltcut, 1800).set(p_fltres, 0.2f)
        .set(p_fenvA, 2.f).set(p_fenvD, 3.f).set(p_fenvS, 0.7f)
        .set(p_aenvA, 1.5f).set(p_aenvD, 2.f).set(p_aenvS, 0.8f)
        .set(p_aenvR, 3.f).set(p_l1rate, 0.11f).set(p_l1depth, 0.2f)
        .set(p_l1dest, 1).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.4f)));

    list.push_back(mk("Texture · Alien", make("Texture · Alien")
        .set(p_a_engine, (float) kFm).set(p_a_fmRatio, 5.3f)
        .set(p_a_fmIndex, 4.5f).set(p_a_fmFeedback, 0.4f)
        .set(p_b_engine, (float) kSample).set(p_b_sampleTable, 1)
        .set(p_b_grainSize, 0.08f).set(p_b_grainDensity, 34.f)
        .set(p_b_grainSpray, 0.8f).set(p_b_level, 0.35f)
        .set(p_morph, 0.45f).set(p_fltcut, 6500).set(p_fltres, 0.3f)
        .set(p_aenvA, 0.001f).set(p_aenvD, 1.f).set(p_aenvS, 0.2f)
        .set(p_aenvR, 1.2f).set(p_l1rate, 7.f).set(p_l1depth, 0.25f)
        .set(p_l1dest, 0).set(p_fx1type, (float) kFxPhaser)
        .set(p_fx1mix, 0.4f).set(p_fx2type, (float) kFxDelay)
        .set(p_fx2mix, 0.25f)));

    list.push_back(mk("Pad · Cinematic", make("Pad · Cinematic")
        .set(p_a_engine, (float) kAnalog).set(p_a_level, 0.6f)
        .set(p_b_engine, (float) kSample).set(p_b_sampleTable, 3)
        .set(p_b_grainSize, 0.24f).set(p_b_grainDensity, 10.f)
        .set(p_b_grainSpray, 0.45f).set(p_b_level, 0.4f)
        .set(p_morph, 0.4f).set(p_fltcut, 2600).set(p_fltres, 0.18f)
        .set(p_aenvA, 1.8f).set(p_aenvD, 2.f).set(p_aenvS, 0.75f)
        .set(p_aenvR, 4.f).set(p_l1rate, 0.06f).set(p_l1depth, 0.25f)
        .set(p_l1dest, 1).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.55f)));

    list.push_back(mk("Bass · Wobble", make("Bass · Wobble")
        .set(p_o1wave, 2).set(p_o1lvl, 0.8f).set(p_o2wave, 3)
        .set(p_o2lvl, 0.35f).set(p_sublvl, 0.4f).set(p_flttype, kLp24)
        .set(p_fltcut, 420).set(p_fltres, 0.55f).set(p_aenvA, 0.005f)
        .set(p_aenvD, 0.4f).set(p_aenvS, 0.9f).set(p_l1shape, 2)
        .set(p_l1rate, 2.f).set(p_l1depth, 0.6f).set(p_l1dest, 1)
        .set(p_fltdrive, 2.f).set(p_fx1type, (float) kFxDrive)
        .set(p_fx1mix, 0.3f)));

    list.push_back(mk("Lead · MPE Flute", make("Lead · MPE Flute")
        .set(p_mpe, 1).set(p_a_engine, (float) kWavetable)
        .set(p_a_wtTable, 0).set(p_a_wtPos, 0.1f).set(p_a_level, 0.7f)
        .set(p_o1wave, 0).set(p_o1lvl, 0.15f).set(p_noise, 0.04f)
        .set(p_flttype, kLp12).set(p_fltcut, 4800).set(p_aenvA, 0.05f)
        .set(p_aenvD, 0.4f).set(p_aenvS, 0.9f).set(p_aenvR, 0.3f)
        .set(p_l1rate, 5.f).set(p_l1depth, 0.06f).set(p_l1dest, 0)
        .set(p_m1src, (float) kSrcAftertouch).set(p_m1dst, (float) kDstF1Cut)
        .set(p_m1amt, 0.5f).set(p_fx1type, (float) kFxReverb)
        .set(p_fx1mix, 0.25f)));

    list.push_back(mk("Pad · Frozen", make("Pad · Frozen")
        .set(p_a_engine, (float) kWavetable).set(p_a_wtTable, 3)
        .set(p_a_wtPos, 0.7f).set(p_a_wtWarp, 0.45f).set(p_a_level, 0.65f)
        .set(p_uni, 2).set(p_unispr, 6).set(p_flttype, kLp24)
        .set(p_fltcut, 5200).set(p_aenvA, 1.5f).set(p_aenvD, 3.f)
        .set(p_aenvS, 0.7f).set(p_aenvR, 5.f).set(p_l1rate, 0.04f)
        .set(p_l1depth, 0.3f).set(p_l1dest, 1).set(p_l2rate, 0.09f)
        .set(p_l2depth, 0.2f).set(p_l2dest, 0)
        .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.55f)));

    // Curated character families: every base sound gets a musically useful
    // set of variations. The transforms are category-aware (a bass does not
    // get a four-second pad attack) and each one changes timbre, envelopes
    // or the FX chain in a distinct direction.
    const std::array<const char*, 12> baseNames = {
        "Bass · Sub Zero", "Bass · Reese", "Lead · Neon", "Lead · Wavetable",
        "Pad · Aurora", "Pad · Glass", "Keys · Electric", "Keys · Wurli",
        "Pluck · Crystal", "Texture · Wind", "Hybrid · Morph Pad", "MPE · Pad"
    };
    const std::array<const char*, 9> characters = {
        "Air", "Tape", "Pulse", "Velvet", "Shimmer", "Deep", "Bloom", "Rust", "Drift"
    };

    for (const auto* baseName : baseNames)
    {
        std::vector<PresetSetting> base;
        for (const auto& p : list)
            if (p.name.equalsIgnoreCase(u8(baseName)))
                base = p.set;
        if (base.empty())
            continue;

        const juce::String baseString(u8(baseName));
        const bool isBass = baseString.startsWith("Bass");
        const bool isSustained = baseString.startsWith("Pad")
                              || baseString.startsWith("Texture")
                              || baseString.startsWith("Hybrid")
                              || baseString.startsWith("MPE");

        for (const auto* character : characters)
        {
            PresetBuilder b;
            b.v = base;
            const juce::String ch(character);
            const bool air = ch == "Air";
            const bool tape = ch == "Tape";
            const bool pulse = ch == "Pulse";
            const bool velvet = ch == "Velvet";
            const bool shimmer = ch == "Shimmer";
            const bool deep = ch == "Deep";
            const bool bloom = ch == "Bloom";
            const bool rust = ch == "Rust";

            if (air)
            {
                b.set(p_fltcut, 11000).set(p_fltres, 0.18f)
                 .set(p_width, 0.9f).set(p_uni, 5).set(p_unispr, 30)
                 .set(p_fx1type, (float) kFxChorus).set(p_fx1mix, 0.3f)
                 .set(p_fx2type, (float) kFxReverb).set(p_fx2mix, 0.35f);
            }
            else if (tape)
            {
                b.set(p_fltcut, 2400).set(p_fltdrive, 1.5f)
                 .set(p_width, 0.62f).set(p_l1shape, 1)
                 .set(p_l1rate, 0.08f).set(p_l1depth, 0.08f).set(p_l1dest, 0)
                 .set(p_fx1type, (float) kFxDrive).set(p_fx1mix, 0.18f)
                 .set(p_fx2type, (float) kFxComp).set(p_fx2mix, 0.22f);
            }
            else if (pulse)
            {
                b.set(p_l1shape, 3).set(p_l1rate, 4.f).set(p_l1depth, 0.45f)
                 .set(p_l1dest, 1)
                 .set(p_fx1type, (float) kFxPhaser).set(p_fx1mix, 0.2f)
                 .set(p_fx2type, (float) kFxDelay).set(p_fx2mix, 0.18f);
            }
            else if (velvet)
            {
                b.set(p_fltcut, 1600).set(p_fltres, 0.2f)
                 .set(p_aenvA, isBass ? 0.02f : (isSustained ? 1.4f : 0.12f))
                 .set(p_aenvR, isBass ? 0.18f : (isSustained ? 3.8f : 1.2f))
                 .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.4f);
            }
            else if (shimmer)
            {
                b.set(p_fltcut, 8500).set(p_uni, 6).set(p_unispr, 24)
                 .set(p_l2shape, 0).set(p_l2rate, 0.18f).set(p_l2depth, 0.18f)
                 .set(p_l2dest, 0)
                 .set(p_fx1type, (float) kFxDelay).set(p_fx1mix, 0.25f)
                 .set(p_fx2type, (float) kFxReverb).set(p_fx2mix, 0.45f);
            }
            else if (deep)
            {
                b.set(p_fltcut, 620).set(p_sublvl, 0.5f).set(p_width, 0.7f)
                 .set(p_aenvA, isBass ? 0.01f : (isSustained ? 0.8f : 0.05f))
                 .set(p_aenvR, isBass ? 0.2f : (isSustained ? 5.f : 1.5f))
                 .set(p_l1rate, 0.05f).set(p_l1depth, 0.18f).set(p_l1dest, 1)
                 .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.3f);
            }
            else if (bloom)
            {
                b.set(p_morph, 0.55f)
                 .set(p_aenvA, isBass ? 0.03f : (isSustained ? 1.6f : 0.35f))
                 .set(p_aenvD, isBass ? 0.6f : 2.4f)
                 .set(p_aenvS, 0.72f)
                 .set(p_aenvR, isBass ? 0.25f : (isSustained ? 5.5f : 2.2f))
                 .set(p_l1rate, 0.045f).set(p_l1depth, 0.3f).set(p_l1dest, 1)
                 .set(p_fx1type, (float) kFxReverb).set(p_fx1mix, 0.5f);
            }
            else if (rust)
            {
                b.set(p_fltcut, 3200).set(p_fltdrive, 2.5f).set(p_noise, 0.08f)
                 .set(p_l1shape, 4).set(p_l1rate, 0.32f).set(p_l1depth, 0.15f)
                 .set(p_l1dest, 1)
                 .set(p_fx1type, (float) kFxPhaser).set(p_fx1mix, 0.35f)
                 .set(p_fx2type, (float) kFxDelay).set(p_fx2mix, 0.15f);
            }
            else // Drift
            {
                b.set(p_l1rate, 0.07f).set(p_l1depth, 0.18f).set(p_l1dest, 0)
                 .set(p_l2rate, 0.11f).set(p_l2depth, 0.16f).set(p_l2dest, 1)
                 .set(p_width, 0.88f).set(p_uni, 5).set(p_unispr, 22)
                 .set(p_fx1type, (float) kFxChorus).set(p_fx1mix, 0.25f)
                 .set(p_fx2type, (float) kFxReverb).set(p_fx2mix, 0.3f);
            }

            const auto name = baseString + " " + ch;
            list.push_back(mk(name.toRawUTF8(), std::move(b)));
        }
    }

    return list;
}

// =========================================================================
// 3. DSP helpers
// =========================================================================
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
        case 0: return (float) std::sin(juce::MathConstants<double>::twoPi * t);
        case 1: return (float) (4.0 * std::abs(t - 0.5) - 1.0);
        case 2: return (float) (2.0 * t - 1.0) - polyBlep(t, dt);
        case 3: return (t < 0.5 ? 1.f : -1.f) - polyBlep(t, dt)
                       + polyBlep(std::fmod(t + 0.5, 1.0), dt);
        default: return (t < 0.25 ? 1.f : -1.f) - polyBlep(t, dt)
                        + polyBlep(std::fmod(t + 0.75, 1.0), dt);
    }
}

inline float lfoShape(int shape, double phase, float sampleHold)
{
    switch (shape)
    {
        case 0: return (float) std::sin(juce::MathConstants<double>::twoPi * phase);
        case 1: return (float) (4.0 * std::abs(phase - 0.5) - 1.0);
        case 2: return (float) (2.0 * phase - 1.0);
        case 3: return phase < 0.5 ? 1.f : -1.f;
        default: return sampleHold;
    }
}

inline std::uint32_t nextRand(std::uint32_t& s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

inline float randBipolar(std::uint32_t& s)
{
    return (float) ((nextRand(s) >> 8) & 0xFFFFFFu) / 8388608.f - 1.f;
}

struct OscParams
{
    int wave = 2;
    int octave = 0;
    int semi = 0;
    float detune = 0.f;
    float level = 0.f;
};

struct LayerParams
{
    int engine = kOff; // Off, Analog, Wavetable, FM, Sample
    OscParams osc[3];
    int uni = 1;
    float spread = 12.f;
    float sub = 0.f;
    int subWave = 0;
    float noise = 0.f;
    int wtTable = 1;
    float wtPos = 0.25f;
    float wtWarp = 0.f;
    float fmRatio = 2.f;
    float fmIndex = 1.5f;
    float fmFeedback = 0.f;
    int fmMode = 0;
    float grainSize = 0.12f;
    float grainDensity = 16.f;
    float grainSpray = 0.2f;
    float grainTone = 0.5f;
    int sampleTable = 0;
    float level = 0.8f;
    float pan = 0.f;
    float tune = 0.f;
    float fine = 0.f;
};

struct AuxParams
{
    int engine = 0;
    float level = 0.f;
    float pan = 0.f;
    float tune = 0.f;
    float decay = 0.4f;
    float color = 0.5f;
};

struct FilterParams
{
    int type = kLp12;
    float cutoff = 12000.f;
    float reso = 0.12f;
    float keytrack = 0.f;
    float envAmount = 0.f;
    float drive = 1.f;
};

struct EnvParams { float attack = 0.005f, decay = 0.3f, sustain = 0.5f, release = 0.3f; };

struct LfoParams
{
    int shape = 0;
    float rate = 5.f;
    float depth = 0.f;
    int dest = 4; // Off
};

struct ModSlot
{
    int source = kSrcOff;
    int dest = kDstOff;
    float amount = 0.f;
    bool active() const { return source != kSrcOff && dest != kDstOff && std::abs(amount) > 1e-6f; }
};

struct MacroRoute
{
    int dest = kDstOff;
    float amount = 0.f;
};

struct MacroParams
{
    float value = 0.5f;
    MacroRoute routes[kMaxMacroRoutes];
};

struct FxParams
{
    int type = kFxOff;
    float mix = 0.5f;
    float p1 = 0.5f, p2 = 0.5f, p3 = 0.5f, p4 = 0.5f;
};

struct ArpParams
{
    bool on = false;
    int mode = 0;
    int rate = 3;
    int octaves = 1;
    float gate = 0.7f;
    float swing = 0.f;
    bool latch = false;
    int steps = kArpSteps;
    bool stepOn[kArpSteps] = {};
};

struct SynthParams
{
    int motor = 1;
    int quality = 1;
    float bend = 2.f;
    float level = 0.8f;
    float pan = 0.f;
    float glide = 0.f;
    float velSens = 0.3f;
    float morph = 0.f;
    int morphMode = 0;
    float width = 0.5f;
    bool limiter = true;
    bool mpe = false;
    float mpeBend = 48.f;
    float mpePress = 0.5f;
    float mpeTimbre = 0.5f;

    LayerParams a, b;
    AuxParams aux;
    FilterParams f1, f2;
    int filterRoute = 0;
    float filterBalance = 0.5f;
    EnvParams env[4];
    LfoParams lfo[4];
    float rndRate[2] = { 2.f, 0.3f };
    float rndDepth[2] = { 0.f, 0.f };
    ModSlot matrix[kMaxMatrix];
    MacroParams macros[kMaxMacros];
    FxParams fx[kMaxFx];
    ArpParams arp;
    float warpAmt = 0.8f;
    float warpR = 12.f;

    std::atomic<float>* modWheel = nullptr;
    std::atomic<float>* expression = nullptr;
    std::atomic<float>* channelPressure = nullptr;
    std::atomic<float>* pitchBend = nullptr;
};

struct WavetableBank
{
    static constexpr int kTables = 4;
    static constexpr int kMips = 6;
    static constexpr int kSize = 2048;

    std::array<std::array<std::vector<float>, kMips>, kTables> tables;
    bool ready = false;

    void prepare()
    {
        if (ready)
            return;
        for (int t = 0; t < kTables; ++t)
        {
            for (int m = 0; m < kMips; ++m)
            {
                auto& table = tables[(size_t) t][(size_t) m];
                table.resize(kSize + 1);
                const int maxHarm = juce::jmax(1, (kSize / 2) >> m);
                double peak = 0.0;
                for (int i = 0; i < kSize; ++i)
                {
                    const double ph = (double) i / kSize;
                    double v = 0.0;
                    for (int n = 1; n <= maxHarm; ++n)
                    {
                        double amp = 0.0;
                        if (t == 0) amp = n == 1 ? 1.0 : 0.0;
                        else if (t == 1) amp = 1.0 / n;
                        else if (t == 2) amp = (n & 1) ? 1.0 / n : 0.0;
                        else
                        {
                            // Bell-like, odd-weighted spectrum.
                            if ((n & 1) == 0) continue;
                            amp = 1.0 / std::pow((double) n, 1.6);
                        }
                        if (amp != 0.0)
                            v += amp * std::sin(juce::MathConstants<double>::twoPi
                                                * n * ph);
                    }
                    table[(size_t) i] = (float) v;
                    peak = juce::jmax(peak, std::abs(v));
                }
                if (peak > 1e-9)
                    for (auto& x : table)
                        x = (float) (x / peak * 0.9);
                table[kSize] = table[0];
            }
        }
        ready = true;
    }

    static int mipFor(double hz, double sr)
    {
        if (hz <= 1.0 || sr <= 0.0)
            return kMips - 1;
        const double maxHarm = juce::jmax(1.0, sr / (2.0 * hz));
        const double desired = 1024.0 / maxHarm;
        const int m = (int) std::ceil(std::log2(juce::jmax(1.0, desired)));
        return juce::jlimit(0, kMips - 1, m);
    }

    float sample(int table, float position, int mip, double phase,
                 float warpAmt) const
    {
        if (! ready)
            return 0.f;
        table = juce::jlimit(0, kTables - 1, table);
        const int next = (table + 1) % kTables;
        phase -= std::floor(phase);
        if (warpAmt > 1e-5f)
            phase += warpAmt * 0.12
                     * std::sin(juce::MathConstants<double>::twoPi * phase);
        phase -= std::floor(phase);
        const double x = phase * kSize;
        const int i = juce::jlimit(0, kSize - 1, (int) x);
        const float f = (float) (x - i);
        const auto& a = tables[(size_t) table][(size_t) mip];
        const auto& b = tables[(size_t) next][(size_t) mip];
        const float s0 = a[(size_t) i] + (a[(size_t) i + 1] - a[(size_t) i]) * f;
        const float s1 = b[(size_t) i] + (b[(size_t) i + 1] - b[(size_t) i]) * f;
        return s0 + (s1 - s0) * juce::jlimit(0.f, 1.f, position);
    }
};

inline float driveShape(float x, float amount)
{
    const float g = 1.f + juce::jmax(0.f, amount - 1.f) * 0.5f;
    return std::tanh(x * g) / std::tanh(juce::jmax(1.f, g));
}

inline float softClip(float x)
{
    return x / (1.f + std::abs(x));
}

// The polyphonic LINES rule: every voice is pulled toward a pitch curve by
// its own distance to that curve.
struct LineNode { float t; float note; };
struct WarpCurve { std::vector<LineNode> nodes; };

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

// -------------------------------------------------------------------------
// Wavetable / FM / granular state lives per voice. The analog path uses the
// same phase arrays so switching engine while a note is held is click-free
// enough for a musical performance.
// -------------------------------------------------------------------------
struct LayerRuntime
{
    double phase[3][kMaxUnison] = {};
    double wtPhase[2][kMaxUnison] = {};
    double fmPhase[2] = {};
    double fmPrev = 0.0;
    double grainPhase = 0.0;
    double grainPos = 0.0;
    double grainRandom = 0.0;
    double subPhase = 0.0;
    float toneState = 0.f;
    std::uint32_t rng = 0x9E3779B9u;

    void reset(int note)
    {
        rng = 0x9E3779B9u ^ ((std::uint32_t) (note + 1) * 2654435761u);
        for (int o = 0; o < 3; ++o)
            for (int u = 0; u < kMaxUnison; ++u)
            {
                phase[o][u] = std::fmod(u * 0.6180339887, 1.0);
                wtPhase[o == 0 ? 0 : 1][u] = std::fmod(u * 0.7548776662, 1.0);
            }
        fmPhase[0] = fmPhase[1] = 0.0;
        fmPrev = 0.0;
        grainPhase = 0.0;
        grainPos = 0.0;
        grainRandom = 0.0;
        subPhase = 0.0;
        toneState = 0.f;
    }
};

// =========================================================================
// 4. Voice
// =========================================================================
class SynthVoice : public juce::MPESynthesiserVoice
{
public:
    explicit SynthVoice(const WavetableBank* bank) : bank_(bank) {}

    void setParams(const SynthParams& p) { P_ = p; }

    void setCurrentSampleRate(double newRate) override
    {
        juce::MPESynthesiserVoice::setCurrentSampleRate(newRate);
        const auto sr = newRate > 0 ? newRate : 48000.0;
        for (auto& e : env_)
            e.setSampleRate(sr);
        juce::dsp::ProcessSpec spec { sr, 64, 1 };
        f1_.prepare(spec);
        f2_.prepare(spec);
        f1_.reset();
        f2_.reset();
        warpCoef_ = 1.0 - std::exp(-1.0 / (0.008 * sr));
        filterCounter_ = 0;
    }

    void noteStarted() override
    {
        const auto note = getCurrentlyPlayingNote();
        note_ = (int) note.initialNote;
        velocity_ = juce::jlimit(0.f, 1.f, note.noteOnVelocity.asUnsignedFloat());
        press_ = juce::jlimit(0.f, 1.f, note.pressure.asUnsignedFloat());
        timbre_ = juce::jlimit(0.f, 1.f, note.timbre.asUnsignedFloat());
        const double target = 440.0 * std::pow(2.0, (note_ - 69) / 12.0);
        if (P_.glide > 0.001f && lastFreq_ > 0.0
            && std::abs(target - lastFreq_) > 0.5)
        {
            glideFrom_ = lastFreq_;
            glideTarget_ = target;
            glidePos_ = 0.0;
            glideTime_ = P_.glide;
            gliding_ = true;
            baseFreq_ = glideFrom_;
        }
        else
        {
            gliding_ = false;
            baseFreq_ = target;
        }
        lastFreq_ = target;
        for (int i = 0; i < 4; ++i)
        {
            env_[i].setParameters(paramsFor(i));
            env_[i].reset();
            env_[i].noteOn();
        }
        layerA_.reset(note_);
        layerB_.reset(note_);
        auxEnv_ = 1.f;
        auxPhase_ = 0.0;
        warpSmoothed_ = 0.f;
        rng_ = 0x9E3779B9u ^ ((std::uint32_t) (note_ + 1) * 2654435761u);
        for (int i = 0; i < 4; ++i)
            lfoPhase_[i] = 0.0;
        for (int i = 0; i < 2; ++i)
        {
            rndPhase_[i] = 0.0;
            rndValue_[i] = 0.f;
        }
        active_ = true;
        configureFilters();
        f1_.reset();
        f2_.reset();
    }

    void noteStopped(bool allowTailOff) override
    {
        if (! allowTailOff)
        {
            for (auto& e : env_)
                e.reset();
            active_ = false;
            clearCurrentNote();
            return;
        }
        for (auto& e : env_)
            e.noteOff();
    }

    void notePressureChanged() override
    {
        press_ = juce::jlimit(0.f, 1.f, getCurrentlyPlayingNote().pressure.asUnsignedFloat());
    }
    void notePitchbendChanged() override {}
    void noteTimbreChanged() override
    {
        timbre_ = juce::jlimit(0.f, 1.f, getCurrentlyPlayingNote().timbre.asUnsignedFloat());
    }
    void noteKeyStateChanged() override {}

    void setWarpTarget(float semis) { warpTarget_ = semis; }

    void renderNextBlock(juce::AudioBuffer<float>& output, int startSample,
                         int numSamples) override
    {
        if (! active_)
            return;
        const int nch = output.getNumChannels();
        float* L = nch > 0 ? output.getWritePointer(0) + startSample : nullptr;
        float* R = nch > 1 ? output.getWritePointer(1) + startSample : nullptr;
        const double invSr = 1.0 / juce::jmax(1.0, getSampleRate());

        // Envelope parameters are cheap to re-apply and make automation
        // audible without a note retrigger.
        for (int i = 0; i < 4; ++i)
            env_[i].setParameters(paramsFor(i));

        for (int i = 0; i < numSamples; ++i)
        {
            const float e1 = env_[0].getNextSample();
            const float e2 = env_[1].getNextSample();
            const float e3 = env_[2].getNextSample();
            const float e4 = env_[3].getNextSample();

            // LFOs and sample-and-hold random sources.
            float lfo[4] = {};
            for (int l = 0; l < 4; ++l)
            {
                lfoPhase_[l] += P_.lfo[l].rate * invSr;
                if (lfoPhase_[l] >= 1.0)
                {
                    lfoPhase_[l] -= std::floor(lfoPhase_[l]);
                    if (P_.lfo[l].shape == 4)
                        sh_[l] = randBipolar(rng_);
                }
                lfo[l] = lfoShape(P_.lfo[l].shape, lfoPhase_[l], sh_[l])
                         * P_.lfo[l].depth;
            }
            float rnd[2] = {};
            for (int r = 0; r < 2; ++r)
            {
                rndPhase_[r] += P_.rndRate[r] * invSr;
                if (rndPhase_[r] >= 1.0)
                {
                    rndPhase_[r] -= std::floor(rndPhase_[r]);
                    rndValue_[r] = randBipolar(rng_);
                }
                rnd[r] = rndValue_[r] * P_.rndDepth[r];
            }

            // Glide without pitch bend; MPE bend is added afterwards.
            if (gliding_)
            {
                glidePos_ += invSr;
                const double f = juce::jlimit(0.0, 1.0, glidePos_ / glideTime_);
                baseFreq_ = glideFrom_ * std::pow(glideTarget_ / glideFrom_, f);
                if (f >= 1.0)
                    gliding_ = false;
            }

            const auto mpeNote = getCurrentlyPlayingNote();
            const double bendSemis = mpeNote.totalPitchbendInSemitones;
            const double freqNoMod = baseFreq_ * std::exp2(bendSemis / 12.0);

            // Build modulation source values once per sample.
            float src[(int) kSrcNote + 1] = {};
            src[kSrcLfo1] = lfo[0];
            src[kSrcLfo2] = lfo[1];
            src[kSrcLfo3] = lfo[2];
            src[kSrcLfo4] = lfo[3];
            src[kSrcRnd1] = rnd[0];
            src[kSrcRnd2] = rnd[1];
            src[kSrcEnv1] = e1;
            src[kSrcEnv2] = e2;
            src[kSrcEnv3] = e3;
            src[kSrcEnv4] = e4;
            src[kSrcVel] = velocity_;
            src[kSrcKey] = juce::jlimit(-1.f, 1.f, (note_ - 60) / 60.f);
            src[kSrcModWheel] = P_.modWheel->load(std::memory_order_relaxed);
            src[kSrcAftertouch] = press_;
            src[kSrcTimbre] = timbre_;
            src[kSrcPitchBend] = (float) juce::jlimit(-1.0, 1.0,
                mpeNote.totalPitchbendInSemitones / juce::jmax(1.f, P_.bend));
            for (int m = 0; m < kMaxMacros; ++m)
                src[kSrcMacro1 + m] = P_.macros[m].value;
            src[kSrcExpression] = P_.expression->load(std::memory_order_relaxed);
            src[kSrcNote] = note_ / 127.f;

            float dst[(int) kDstWidth + 1] = {};
            for (const auto& slot : P_.matrix)
                if (slot.active())
                    dst[slot.dest] += src[slot.source] * slot.amount;
            for (const auto& macro : P_.macros)
                for (const auto& route : macro.routes)
                    if (route.dest != kDstOff && std::abs(route.amount) > 1e-6f)
                        dst[route.dest] += macro.value * route.amount;

            // Legacy direct LFO destinations coexist with the matrix.
            const double pitchLfo = (P_.lfo[0].dest == 0 ? lfo[0] * 12.0 : 0.0)
                                    + (P_.lfo[1].dest == 0 ? lfo[1] * 12.0 : 0.0)
                                    + (P_.lfo[2].dest == 0 ? lfo[2] * 12.0 : 0.0)
                                    + (P_.lfo[3].dest == 0 ? lfo[3] * 12.0 : 0.0);
            const double pitchMod = dst[kDstPitch] * 24.0 + pitchLfo;
            warpSmoothed_ += (warpTarget_ - warpSmoothed_) * (float) warpCoef_;
            const double freq = freqNoMod
                                * std::exp2((pitchMod + warpSmoothed_) / 12.0);

            // Layer A/B crossfade. Equal-power keeps the perceived level
            // stable as the morph macro travels.
            float morph = juce::jlimit(0.f, 1.f, P_.morph + dst[kDstMorph]);
            if (P_.morphMode == 1) // spectral: bias the B layer's table position
                morph = juce::jlimit(0.f, 1.f, morph + 0.12f * std::sin(
                    juce::MathConstants<float>::pi * morph));
            const float aGain = std::cos(morph * juce::MathConstants<float>::halfPi);
            const float bGain = std::sin(morph * juce::MathConstants<float>::halfPi);

            const float aLevel = P_.a.level * (1.f + dst[kDstALevel])
                                 * (P_.a.engine == kOff ? 0.f : 1.f);
            const float bLevel = P_.b.level * (1.f + dst[kDstBLevel])
                                 * (P_.b.engine == kOff ? 0.f : 1.f);
            const float auxLevel = P_.aux.level * (1.f + dst[kDstAuxLevel]);

            float sig = 0.f;
            sig += renderLayer(P_.a, layerA_, freq, aGain * aLevel, e1,
                               dst[kDstAWtPos], dst[kDstAFmIndex], dst[kDstNoise]);
            sig += renderLayer(P_.b, layerB_, freq, bGain * bLevel, e1,
                               dst[kDstBWtPos], dst[kDstBFmIndex], dst[kDstNoise]);
            sig += renderAux(freq, auxLevel);

            // Filter envelope and routing.
            const float f1Semis = e1 * P_.f1.envAmount
                                  + dst[kDstF1Cut] * 60.f
                                  + (P_.lfo[0].dest == 1 ? lfo[0] * 24.f : 0.f)
                                  + (P_.lfo[1].dest == 1 ? lfo[1] * 24.f : 0.f)
                                  + (P_.lfo[2].dest == 1 ? lfo[2] * 24.f : 0.f)
                                  + (P_.lfo[3].dest == 1 ? lfo[3] * 24.f : 0.f);
            const float f2Semis = e3 * P_.f2.envAmount + dst[kDstF2Cut] * 60.f;
            if ((filterCounter_++ & 15) == 0)
                updateFilterCutoffs(f1Semis, f2Semis, dst);

            const float drive = juce::jmax(0.2f, P_.f1.drive + dst[kDstDrive] * 4.f);
            sig = driveShape(sig, drive);
            sig = applyFilters(sig, dst);

            float amp = e2 * (1.f - P_.velSens + P_.velSens * velocity_)
                        * P_.level * (1.f + dst[kDstAmp]);
            for (int l = 0; l < 4; ++l)
                if (P_.lfo[l].dest == 2)
                    amp *= 1.f - 0.5f * (1.f - lfo[l] / juce::jmax(1e-6f, P_.lfo[l].depth));
            amp = juce::jmax(0.f, amp);

            float pan = juce::jlimit(-1.f, 1.f, P_.pan + dst[kDstPan]);
            for (int l = 0; l < 4; ++l)
                if (P_.lfo[l].dest == 3)
                    pan += 0.8f * lfo[l];
            pan = juce::jlimit(-1.f, 1.f, pan);
            const float ml = pan <= 0.f ? 1.f : 1.f - pan;
            const float mr = pan >= 0.f ? 1.f : 1.f + pan;

            const float out = sig * amp;
            if (L != nullptr)
                L[i] += R != nullptr ? out * ml : out * 0.5f;
            if (R != nullptr)
                R[i] += out * mr;

            if (! env_[1].isActive())
            {
                active_ = false;
                clearCurrentNote();
                break;
            }
        }
    }

private:
    juce::ADSR::Parameters paramsFor(int i) const
    {
        juce::ADSR::Parameters p;
        p.attack = P_.env[i].attack;
        p.decay = P_.env[i].decay;
        p.sustain = P_.env[i].sustain;
        p.release = P_.env[i].release;
        return p;
    }

    float renderLayer(const LayerParams& L, LayerRuntime& rt, double freq,
                      float gain, float env1, float wtPosMod,
                      float fmIndexMod, float noiseMod)
    {
        if (L.engine == kOff || gain <= 1e-6f)
            return 0.f;
        const double sr = juce::jmax(1.0, getSampleRate());
        const double invSr = 1.0 / sr;
        const float tune = (L.tune + L.fine / 100.f) / 12.f;
        float sig = 0.f;
        const int uni = juce::jlimit(1, kMaxUnison, L.uni);
        const float uniNorm = uni > 1 ? 1.f / std::sqrt((float) uni) : 1.f;

        if (L.engine == kAnalog)
        {
            for (int o = 0; o < 3; ++o)
            {
                const auto& osc = L.osc[o];
                if (osc.level <= 1e-4f)
                    continue;
                for (int u = 0; u < uni; ++u)
                {
                    const double spread = uni > 1
                        ? (u - (uni - 1) / 2.0) / ((uni - 1) / 2.0) * L.spread
                        : 0.0;
                    const double semis = osc.octave * 12.0 + osc.semi
                                         + osc.detune / 100.0 + spread / 100.0;
                    const double f = freq * std::exp2((semis / 12.0) + tune);
                    const double inc = f * invSr;
                    auto& ph = rt.phase[o][u];
                    sig += oscSample(osc.wave, ph, inc) * osc.level;
                    ph += inc;
                    if (ph >= 1.0 || ph < 0.0)
                        ph -= std::floor(ph);
                }
            }
            if (L.sub > 1e-4f)
            {
                const double inc = freq * 0.5 * invSr;
                rt.subPhase += inc;
                if (rt.subPhase >= 1.0)
                    rt.subPhase -= std::floor(rt.subPhase);
                const float sub = L.subWave == 0
                    ? (float) std::sin(juce::MathConstants<double>::twoPi * rt.subPhase)
                    : (rt.subPhase < 0.5 ? 1.f : -1.f);
                sig += sub * L.sub;
            }
            const float noiseLvl = juce::jmax(0.f, L.noise + noiseMod);
            if (noiseLvl > 1e-4f)
                sig += randBipolar(rt.rng) * noiseLvl;
            // Loudness compensation keeps the level stable as layers stack.
            sig *= uniNorm;
        }
        else if (L.engine == kWavetable)
        {
            const int mip = WavetableBank::mipFor(freq, sr);
            const float pos = juce::jlimit(0.f, 1.f, L.wtPos + wtPosMod);
            for (int o = 0; o < 2; ++o)
            {
                for (int u = 0; u < uni; ++u)
                {
                    const double spread = uni > 1
                        ? (u - (uni - 1) / 2.0) / ((uni - 1) / 2.0) * L.spread
                        : 0.0;
                    const double f = freq * std::exp2(tune + (spread + (o == 1 ? -7.0 : 7.0)) / 1200.0);
                    const double inc = f * invSr;
                    auto& ph = rt.wtPhase[o][u];
                    sig += bank_->sample(L.wtTable, pos, mip, ph, L.wtWarp) * 0.5f;
                    ph += inc;
                    if (ph >= 1.0 || ph < 0.0)
                        ph -= std::floor(ph);
                }
            }
            sig *= uniNorm;
        }
        else if (L.engine == kFm)
        {
            const float index = juce::jmax(0.f, L.fmIndex + fmIndexMod * 12.f)
                                * (0.3f + 0.7f * env1);
            const int ops = L.fmMode == 0 ? 1 : 2;
            for (int u = 0; u < uni; ++u)
            {
                const double spread = uni > 1
                    ? (u - (uni - 1) / 2.0) / ((uni - 1) / 2.0) * L.spread
                    : 0.0;
                const double f = freq * std::exp2(tune + spread / 1200.0);
                const double modInc = f * L.fmRatio * invSr;
                const double carInc = f * invSr;
                rt.fmPhase[0] += modInc;
                rt.fmPhase[1] += carInc;
                if (rt.fmPhase[0] >= 1.0) rt.fmPhase[0] -= std::floor(rt.fmPhase[0]);
                if (rt.fmPhase[1] >= 1.0) rt.fmPhase[1] -= std::floor(rt.fmPhase[1]);
                const double mod = std::sin(juce::MathConstants<double>::twoPi
                                            * rt.fmPhase[0]
                                            + L.fmFeedback * rt.fmPrev);
                rt.fmPrev = mod;
                float v = (float) std::sin(juce::MathConstants<double>::twoPi
                                           * rt.fmPhase[1] + index * mod);
                if (ops > 1)
                {
                    rt.fmPhase[0] += 0.0; // second operator shares the pair
                    const double mod2 = std::sin(juce::MathConstants<double>::twoPi
                                                 * (rt.fmPhase[0] * 1.5 + 0.25)
                                                 + index * 0.5 * mod);
                    v += 0.5f * (float) std::sin(juce::MathConstants<double>::twoPi
                                                 * (rt.fmPhase[1] + 0.5) + mod2);
                    v *= 0.7f;
                }
                sig += v;
            }
            sig *= uniNorm;
        }
        else // kSample — granular playback of the wavetable bank
        {
            const int mip = WavetableBank::mipFor(freq, sr);
            const double grainRate = juce::jmax(0.2, (double) L.grainDensity);
            rt.grainPhase += grainRate * invSr;
            if (rt.grainPhase >= 1.0)
            {
                rt.grainPhase -= std::floor(rt.grainPhase);
                rt.grainPos = juce::jlimit(0.0, 1.0,
                    rt.grainPos + 0.02 + (double) L.grainSpray * randBipolar(rt.rng) * 0.15);
                rt.grainRandom = randBipolar(rt.rng);
            }
            const double window = std::sin(juce::MathConstants<double>::pi
                                           * juce::jlimit(0.0, 1.0, rt.grainPhase));
            const float pos = juce::jlimit(0.f, 1.f,
                L.wtPos + wtPosMod + (float) rt.grainPos * 0.4f
                + (float) rt.grainRandom * L.grainSpray * 0.1f);
            for (int u = 0; u < uni; ++u)
            {
                const double spread = uni > 1
                    ? (u - (uni - 1) / 2.0) / ((uni - 1) / 2.0) * L.spread
                    : 0.0;
                const double f = freq * std::exp2(tune + spread / 1200.0);
                const double inc = f * invSr;
                auto& ph = rt.wtPhase[0][u];
                sig += bank_->sample(L.sampleTable, pos, mip, ph, L.wtWarp)
                       * (float) window * 0.7f;
                ph += inc;
                if (ph >= 1.0 || ph < 0.0)
                    ph -= std::floor(ph);
            }
            // Tone is a one-pole lowpass over the granular output.
            const float cutoff = 400.f + L.grainTone * 12000.f;
            const float coef = 1.f - std::exp(-juce::MathConstants<float>::twoPi
                                              * cutoff / (float) sr);
            rt.toneState += coef * (sig - rt.toneState);
            sig = rt.toneState;
            sig *= uniNorm;
        }
        return sig * gain;
    }

    float renderAux(double freq, float level)
    {
        if (P_.aux.engine == 0 || level <= 1e-6f)
            return 0.f;
        const double sr = juce::jmax(1.0, getSampleRate());
        const double f = freq * std::exp2(P_.aux.tune / 12.0);
        auxPhase_ += f / sr;
        if (auxPhase_ >= 1.0)
            auxPhase_ -= std::floor(auxPhase_);
        const float decayCoef = 1.f - std::exp(-1.0 / (P_.aux.decay * sr));
        auxEnv_ += (0.f - auxEnv_) * decayCoef;
        float v = 0.f;
        switch (P_.aux.engine)
        {
            case 1: // noise
                v = randBipolar(rng_) * (0.3f + 0.7f * P_.aux.color);
                break;
            case 2: // sine
                v = (float) std::sin(juce::MathConstants<double>::twoPi * auxPhase_);
                break;
            case 3: // click
                v = auxEnv_ * (0.7f + 0.3f * randBipolar(rng_));
                break;
            default: // texture: filtered noise plus a quiet sine
                v = randBipolar(rng_) * 0.7f
                    + (float) std::sin(juce::MathConstants<double>::twoPi * auxPhase_) * 0.3f;
                break;
        }
        return v * auxEnv_ * level;
    }

    void configureFilters()
    {
        setFilterType(f1_, P_.f1.type);
        setFilterType(f2_, P_.f2.type);
        f1_.setResonance(juce::jlimit(0.707f, 4.f, 0.707f + P_.f1.reso * 3.3f));
        f2_.setResonance(juce::jlimit(0.707f, 4.f, 0.707f + P_.f2.reso * 3.3f));
    }

    static void setFilterType(juce::dsp::StateVariableTPTFilter<float>& f,
                              int type)
    {
        using Type = juce::dsp::StateVariableTPTFilterType;
        switch (type)
        {
            case kHp12:
            case kHp24: f.setType(Type::highpass); break;
            case kBp12:
            case kNotch: f.setType(Type::bandpass); break;
            default: f.setType(Type::lowpass); break;
        }
    }

    void updateFilterCutoffs(float f1Semis, float f2Semis, const float* dst)
    {
        const float key = (note_ - 60) / 12.f;
        const double c1 = P_.f1.cutoff * std::exp2(
            (P_.f1.keytrack * key * 12.0 + f1Semis) / 12.0);
        const double c2 = P_.f2.cutoff * std::exp2(
            (P_.f2.keytrack * key * 12.0 + f2Semis) / 12.0);
        const float nyq = (float) (getSampleRate() * 0.45);
        f1_.setCutoffFrequency(juce::jlimit(20.f, nyq, (float) c1));
        f2_.setCutoffFrequency(juce::jlimit(20.f, nyq, (float) c2));
        f1_.setResonance(juce::jlimit(0.707f, 4.f,
            0.707f + (P_.f1.reso + dst[kDstF1Res]) * 3.3f));
        f2_.setResonance(juce::jlimit(0.707f, 4.f,
            0.707f + (P_.f2.reso + dst[kDstF2Res]) * 3.3f));
    }

    float applyFilters(float x, const float* dst)
    {
        const int route = P_.filterRoute;
        const bool twoStage = P_.f1.type == kLp24 || P_.f1.type == kHp24;
        if (route == 0)
        {
            float y = f1_.processSample(0, x);
            if (twoStage)
                y = f1_.processSample(0, y);
            return y;
        }
        if (route == 1)
        {
            float y = f1_.processSample(0, x);
            if (twoStage)
                y = f1_.processSample(0, y);
            return f2_.processSample(0, y);
        }
        const float balance = juce::jlimit(0.f, 1.f,
            P_.filterBalance + dst[kDstFilterBalance]);
        const float a = f1_.processSample(0, x);
        const float b = f2_.processSample(0, x);
        return a * balance + b * (1.f - balance);
    }

    const WavetableBank* bank_ = nullptr;
    SynthParams P_;
    LayerRuntime layerA_, layerB_;
    juce::ADSR env_[4];
    juce::dsp::StateVariableTPTFilter<float> f1_, f2_;
    int filterCounter_ = 0;

    int note_ = 60;
    float velocity_ = 0.8f, press_ = 0.f, timbre_ = 0.5f;
    double baseFreq_ = 440.0, lastFreq_ = 0.0;
    double glideFrom_ = 0.0, glideTarget_ = 0.0, glidePos_ = 0.0,
           glideTime_ = 0.0;
    bool gliding_ = false;

    double lfoPhase_[4] = {};
    float sh_[4] = {};
    double rndPhase_[2] = {};
    float rndValue_[2] = {};
    std::uint32_t rng_ = 0x1234567u;

    double auxPhase_ = 0.0;
    float auxEnv_ = 0.f;
    float warpSmoothed_ = 0.f, warpTarget_ = 0.f;
    double warpCoef_ = 0.01;
    bool active_ = false;
};

// -------------------------------------------------------------------------
// Per-synth FX rack. Six serial slots; every slot has its own state so
// switching type does not leave a delayed tail in the wrong processor.
// -------------------------------------------------------------------------
class SynthFx
{
public:
    void prepare(double sampleRate, int maxBlock)
    {
        sr_ = sampleRate > 0 ? sampleRate : 48000.0;
        maxBlock_ = juce::jmax(16, maxBlock);
        scratch_.setSize(2, maxBlock_, false, false, true);
        const int delaySize = (int) std::ceil(sr_ * 2.5) + 8;
        for (auto& d : delay_)
        {
            d.setSize(2, delaySize, false, false, true);
            d.clear();
        }
        for (auto& r : reverb_)
        {
            r.setSampleRate(sr_);
            r.reset();
        }
        reset();
    }

    void reset()
    {
        for (auto& d : delay_)
            d.clear();
        for (auto& r : reverb_)
            r.reset();
        for (auto& s : state_)
            s = {};
    }

    void process(juce::AudioBuffer<float>& buf, const SynthParams& P,
                 int numSamples)
    {
        if (buf.getNumChannels() < 1 || numSamples <= 0)
            return;
        if (scratch_.getNumSamples() < numSamples)
            scratch_.setSize(2, numSamples, false, false, true);

        const int nch = juce::jmin(2, buf.getNumChannels());
        for (int slot = 0; slot < kMaxFx; ++slot)
        {
            const auto& fx = P.fx[slot];
            if (fx.type == kFxOff || fx.mix <= 1e-4f)
                continue;
            const float mix = juce::jlimit(0.f, 1.f, fx.mix);
            for (int ch = 0; ch < nch; ++ch)
                scratch_.copyFrom(ch, 0, buf, ch, 0, numSamples);

            auto& st = state_[(size_t) slot];
            switch (fx.type)
            {
                case kFxDrive: processDrive(buf, st, fx, numSamples); break;
                case kFxChorus: processChorus(buf, st, fx, slot, numSamples); break;
                case kFxPhaser: processPhaser(buf, st, fx, numSamples); break;
                case kFxDelay: processDelay(buf, st, fx, slot, numSamples); break;
                case kFxReverb: processReverb(buf, fx, slot, numSamples); break;
                case kFxComp: processComp(buf, st, fx, numSamples); break;
                case kFxEq: processEq(buf, st, fx, numSamples); break;
                default: break;
            }
            for (int ch = 0; ch < nch; ++ch)
            {
                auto* out = buf.getWritePointer(ch);
                const auto* dry = scratch_.getReadPointer(ch);
                for (int i = 0; i < numSamples; ++i)
                    out[i] = dry[i] * (1.f - mix) + out[i] * mix;
            }
        }
    }

private:
    struct SlotState
    {
        float lfo = 0.f;
        float ap[2][4] = {};
        float tone[2] = {};
        float compEnv = 0.f;
        float eqState[2] = {};
        int writePos = 0;
    };

    static float readInterp(const juce::AudioBuffer<float>& b, int ch,
                            double pos)
    {
        const int n = b.getNumSamples();
        if (n <= 0)
            return 0.f;
        while (pos < 0.0)
            pos += n;
        while (pos >= n)
            pos -= n;
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % n;
        const float f = (float) (pos - i0);
        const auto* p = b.getReadPointer(ch);
        return p[i0] + (p[i1] - p[i0]) * f;
    }

    void processDrive(juce::AudioBuffer<float>& buf, SlotState& st,
                      const FxParams& fx, int n)
    {
        const float gain = 1.f + fx.p1 * 18.f;
        const float tone = 500.f + fx.p2 * 12000.f;
        const float coef = 1.f - std::exp(-juce::MathConstants<float>::twoPi
                                          * tone / (float) sr_);
        for (int ch = 0; ch < juce::jmin(2, buf.getNumChannels()); ++ch)
        {
            auto* p = buf.getWritePointer(ch);
            for (int i = 0; i < n; ++i)
            {
                float x = std::tanh(p[i] * gain);
                st.tone[ch] += coef * (x - st.tone[ch]);
                p[i] = st.tone[ch];
            }
        }
    }

    void processChorus(juce::AudioBuffer<float>& buf, SlotState& st,
                       const FxParams& fx, int slot, int n)
    {
        auto& d = delay_[(size_t) slot];
        const int size = d.getNumSamples();
        const double base = (0.004 + fx.p1 * 0.022) * sr_;
        const double depth = (0.001 + fx.p2 * 0.006) * sr_;
        const double rate = 0.05 + fx.p3 * 2.5;
        const double inc = rate / sr_;
        for (int i = 0; i < n; ++i)
        {
            st.lfo += (float) inc;
            if (st.lfo >= 1.f)
                st.lfo -= 1.f;
            const double lfo = std::sin(juce::MathConstants<double>::twoPi * st.lfo);
            for (int ch = 0; ch < juce::jmin(2, buf.getNumChannels()); ++ch)
            {
                auto* p = buf.getWritePointer(ch);
                const double dly = base + depth * (lfo * (ch == 0 ? 1.0 : -1.0));
                const double readPos = st.writePos - dly;
                const float wet = readInterp(d, ch, readPos);
                d.getWritePointer(ch)[st.writePos] = p[i];
                p[i] = wet;
            }
            st.writePos = (st.writePos + 1) % size;
        }
    }

    void processPhaser(juce::AudioBuffer<float>& buf, SlotState& st,
                       const FxParams& fx, int n)
    {
        const float rate = 0.03f + fx.p1 * 1.5f;
        const float depth = fx.p2;
        const float feedback = fx.p3 * 0.7f;
        for (int i = 0; i < n; ++i)
        {
            st.lfo += rate / (float) sr_;
            if (st.lfo >= 1.f)
                st.lfo -= 1.f;
            const float mod = 0.5f + 0.5f * std::sin(juce::MathConstants<float>::twoPi * st.lfo);
            const float freq = 200.f + (2000.f + depth * 5000.f) * mod;
            const float t = std::tan(juce::MathConstants<float>::pi * freq / (float) sr_);
            const float a = (1.f - t) / (1.f + t);
            for (int ch = 0; ch < juce::jmin(2, buf.getNumChannels()); ++ch)
            {
                auto* p = buf.getWritePointer(ch);
                float x = p[i] + st.ap[ch][0] * feedback;
                for (int s = 0; s < 4; ++s)
                {
                    const float y = -a * x + st.ap[ch][s];
                    st.ap[ch][s] = x + a * y;
                    x = y;
                }
                p[i] = x;
            }
        }
    }

    void processDelay(juce::AudioBuffer<float>& buf, SlotState& st,
                      const FxParams& fx, int slot, int n)
    {
        auto& d = delay_[(size_t) slot];
        const int size = d.getNumSamples();
        const double delay = (0.04 + fx.p1 * 1.2) * sr_;
        const float feedback = fx.p2 * 0.88f;
        const float tone = 800.f + fx.p3 * 9000.f;
        const float coef = 1.f - std::exp(-juce::MathConstants<float>::twoPi
                                          * tone / (float) sr_);
        for (int i = 0; i < n; ++i)
        {
            const double readPos = st.writePos - delay;
            for (int ch = 0; ch < juce::jmin(2, buf.getNumChannels()); ++ch)
            {
                auto* p = buf.getWritePointer(ch);
                const float wet = readInterp(d, ch, readPos);
                st.tone[ch] += coef * (wet - st.tone[ch]);
                const float in = p[i];
                d.getWritePointer(ch)[st.writePos] = in + st.tone[ch] * feedback;
                p[i] = wet;
            }
            st.writePos = (st.writePos + 1) % size;
        }
    }

    void processReverb(juce::AudioBuffer<float>& buf, const FxParams& fx,
                       int slot, int n)
    {
        auto& r = reverb_[(size_t) slot];
        juce::Reverb::Parameters p;
        p.roomSize = juce::jlimit(0.f, 1.f, fx.p1);
        p.damping = juce::jlimit(0.f, 1.f, fx.p2);
        p.wetLevel = 1.f;
        p.dryLevel = 0.f;
        p.width = juce::jlimit(0.f, 1.f, fx.p3);
        p.freezeMode = fx.p4 > 0.5f ? 1.f : 0.f;
        r.setParameters(p);
        if (buf.getNumChannels() >= 2)
            r.processStereo(buf.getWritePointer(0), buf.getWritePointer(1), n);
        else
            r.processMono(buf.getWritePointer(0), n);
    }

    void processComp(juce::AudioBuffer<float>& buf, SlotState& st,
                     const FxParams& fx, int n)
    {
        const float threshold = -48.f + fx.p1 * 42.f;
        const float ratio = 1.f + fx.p2 * 11.f;
        const float attack = 0.001f + fx.p3 * 0.2f;
        const float release = 0.02f + fx.p4 * 1.2f;
        const float aCoef = 1.f - std::exp(-1.f / (attack * (float) sr_));
        const float rCoef = 1.f - std::exp(-1.f / (release * (float) sr_));
        for (int i = 0; i < n; ++i)
        {
            float peak = 0.f;
            for (int ch = 0; ch < juce::jmin(2, buf.getNumChannels()); ++ch)
                peak = juce::jmax(peak, std::abs(buf.getWritePointer(ch)[i]));
            const float db = peak > 1e-9f ? 20.f * std::log10(peak) : -120.f;
            float target = 1.f;
            if (db > threshold)
                target = std::pow(10.f, (threshold + (db - threshold) / ratio - db) / 20.f);
            st.compEnv += (target - st.compEnv)
                          * (target < st.compEnv ? aCoef : rCoef);
            for (int ch = 0; ch < juce::jmin(2, buf.getNumChannels()); ++ch)
                buf.getWritePointer(ch)[i] *= st.compEnv;
        }
    }

    void processEq(juce::AudioBuffer<float>& buf, SlotState& st,
                   const FxParams& fx, int n)
    {
        const float lowGain = 1.f + (fx.p1 - 0.5f) * 2.f;
        const float highGain = 1.f + (fx.p3 - 0.5f) * 2.f;
        const float freq = 200.f + fx.p2 * 8000.f;
        const float coef = 1.f - std::exp(-juce::MathConstants<float>::twoPi
                                          * freq / (float) sr_);
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < juce::jmin(2, buf.getNumChannels()); ++ch)
            {
                auto* p = buf.getWritePointer(ch);
                st.eqState[ch] += coef * (p[i] - st.eqState[ch]);
                const float low = st.eqState[ch];
                const float high = p[i] - low;
                p[i] = low * lowGain + high * highGain;
            }
        }
    }

    double sr_ = 48000.0;
    int maxBlock_ = 512;
    juce::AudioBuffer<float> scratch_;
    std::array<juce::AudioBuffer<float>, kMaxFx> delay_;
    std::array<juce::Reverb, kMaxFx> reverb_;
    std::array<SlotState, kMaxFx> state_;
};

// -------------------------------------------------------------------------
// Arpeggiator / step sequencer. It intercepts note events and emits a
// generated monophonic (or chord) pattern. CC and expression pass through.
// -------------------------------------------------------------------------
class Arpeggiator
{
public:
    void prepare(double sampleRate)
    {
        sr_ = sampleRate > 0 ? sampleRate : 48000.0;
        reset();
    }

    void reset()
    {
        for (auto& h : held_)
            h = false;
        for (auto& h : physical_)
            h = false;
        heldOrder_.clear();
        activeNotes_.clear();
        samplesUntilStep_ = 0.0;
        stepSamples_ = 0.0;
        stepIndex_ = 0;
        nextNoteOff_ = 0.0;
        playing_ = false;
    }

    void process(const juce::MidiBuffer& in, juce::MidiBuffer& out,
                 int numSamples, double bpm, const ArpParams& p)
    {
        out.clear();
        if (bpm <= 0.0)
            bpm = 120.0;
        if (! p.on)
        {
            out = in;
            reset();
            return;
        }

        events_.clear();
        for (const auto meta : in)
            events_.push_back({ meta.samplePosition, meta.getMessage() });

        const double beatsPerStep = rateToBeats(p.rate);
        stepSamples_ = juce::jmax(1.0, sr_ * 60.0 / juce::jmax(20.0, bpm)
                                       * beatsPerStep);
        if (stepSamples_ <= 0.0)
            stepSamples_ = sr_ * 0.125;

        const int limit = juce::jmin(p.steps, kArpSteps);
        size_t eventIndex = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            while (eventIndex < events_.size()
                   && events_[eventIndex].position == i)
            {
                handleEvent(events_[eventIndex].msg, out, p, i);
                ++eventIndex;
            }

            if (nextNoteOff_ <= 0.0 && ! activeNotes_.empty())
            {
                for (int note : activeNotes_)
                    out.addEvent(juce::MidiMessage::noteOff(1, note), i);
                activeNotes_.clear();
            }
            if (! activeNotes_.empty())
                nextNoteOff_ -= 1.0;

            if (samplesUntilStep_ <= 0.0)
            {
                samplesUntilStep_ += stepSamples_;
                if (p.steps > 0 && (stepIndex_ % juce::jmax(1, limit)) < limit)
                {
                    if (p.stepOn[stepIndex_ % kArpSteps])
                        triggerStep(out, p, i, stepIndex_);
                }
                ++stepIndex_;
                if (stepIndex_ >= limit)
                    stepIndex_ = 0;
                // Swing: alternating steps get a slightly longer/shorter
                // interval. Keep the average tempo stable.
                if (p.swing > 1e-4f)
                {
                    const double swing = (double) p.swing * 0.5;
                    if ((stepIndex_ & 1) == 0)
                        samplesUntilStep_ *= 1.0 - swing;
                    else
                        samplesUntilStep_ *= 1.0 + swing;
                }
            }
            samplesUntilStep_ -= 1.0;
        }
    }

private:
    struct Event { int position; juce::MidiMessage msg; };

    static double rateToBeats(int rate)
    {
        switch (rate)
        {
            case 0: return 1.0;
            case 1: return 0.5;
            case 2: return 1.0 / 3.0;
            case 3: return 0.25;
            case 4: return 1.0 / 6.0;
            default: return 0.125;
        }
    }

    void handleEvent(const juce::MidiMessage& msg, juce::MidiBuffer& out,
                     const ArpParams& p, int sample)
    {
        if (msg.isNoteOn() && msg.getVelocity() > 0)
        {
            const int n = msg.getNoteNumber();
            if (p.latch)
            {
                bool anyPhysical = false;
                for (bool b : physical_)
                    anyPhysical = anyPhysical || b;
                if (! anyPhysical)
                    releaseAll(out, sample);
                held_[n] = true;
            }
            else
            {
                held_[n] = true;
            }
            physical_[n] = true;
            rebuildHeldOrder();
            return;
        }
        if (msg.isNoteOff() || (msg.isNoteOn() && msg.getVelocity() == 0))
        {
            const int n = msg.getNoteNumber();
            physical_[n] = false;
            if (! p.latch)
            {
                held_[n] = false;
                rebuildHeldOrder();
                if (heldOrder_.empty())
                    releaseAll(out, sample);
            }
            return;
        }
        if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            releaseAll(out, sample);
            return;
        }
        out.addEvent(msg, sample);
    }

    void releaseAll(juce::MidiBuffer& out, int sample)
    {
        for (int note : activeNotes_)
            out.addEvent(juce::MidiMessage::noteOff(1, note), sample);
        activeNotes_.clear();
        for (auto& h : held_)
            h = false;
        for (auto& h : physical_)
            h = false;
        heldOrder_.clear();
        nextNoteOff_ = 0.0;
    }

    void rebuildHeldOrder()
    {
        heldOrder_.clear();
        for (int n = 0; n < 128; ++n)
            if (held_[n])
                heldOrder_.push_back(n);
    }

    void triggerStep(juce::MidiBuffer& out, const ArpParams& p, int sample,
                     int step)
    {
        if (heldOrder_.empty())
            return;
        for (int note : activeNotes_)
            out.addEvent(juce::MidiMessage::noteOff(1, note), sample);
        activeNotes_.clear();

        const int octaves = juce::jlimit(1, 4, p.octaves);
        const int count = (int) heldOrder_.size();
        int index = 0;
        switch (p.mode)
        {
            case 1: index = count - 1 - (step % count); break;
            case 2:
            {
                const int period = juce::jmax(1, count * 2 - 2);
                const int pos = step % period;
                index = pos < count ? pos : period - pos;
                break;
            }
            case 3: index = (int) (nextRand(rng_) % (std::uint32_t) count); break;
            case 4: index = 0; break;
            default: index = step % count; break;
        }
        const int octave = p.mode == 4 ? 0 : (step / juce::jmax(1, count)) % octaves;
        const float vel = 0.78f;
        if (p.mode == 4)
        {
            for (int n : heldOrder_)
                for (int o = 0; o < octaves; ++o)
                {
                    const int note = juce::jlimit(0, 127, n + o * 12);
                    out.addEvent(juce::MidiMessage::noteOn(1, note, vel), sample);
                    activeNotes_.push_back(note);
                }
        }
        else
        {
            const int note = juce::jlimit(0, 127, heldOrder_[(size_t) index] + octave * 12);
            out.addEvent(juce::MidiMessage::noteOn(1, note, vel), sample);
            activeNotes_.push_back(note);
        }
        nextNoteOff_ = stepSamples_ * juce::jlimit(0.05f, 1.f, p.gate);
    }

    double sr_ = 48000.0;
    double samplesUntilStep_ = 0.0;
    double stepSamples_ = 0.0;
    double nextNoteOff_ = 0.0;
    int stepIndex_ = 0;
    bool playing_ = false;
    bool held_[128] = {};
    bool physical_[128] = {};
    std::vector<int> heldOrder_;
    std::vector<int> activeNotes_;
    std::vector<Event> events_;
    std::uint32_t rng_ = 0x9E3779B9u;
};

// -------------------------------------------------------------------------
// Small editor shown only if a host asks for the native view. Cadence's
// Flutter client opens EZSynth Studio instead; this keeps the AudioProcessor
// contract intact for CLIs and other hosts.
// -------------------------------------------------------------------------
class EzSynth2Processor;
class EzSynth2Editor;

class MpeSynth : public juce::MPESynthesiser
{
public:
    std::atomic<float>* modWheel = nullptr;
    std::atomic<float>* expression = nullptr;

    void handleController(int, int controllerNumber, int value) override
    {
        const float v = juce::jlimit(0.f, 1.f, value / 127.f);
        if (controllerNumber == 1 && modWheel != nullptr)
            modWheel->store(v, std::memory_order_relaxed);
        else if (controllerNumber == 11 && expression != nullptr)
            expression->store(v, std::memory_order_relaxed);
    }
};

class EzSynth2Processor : public juce::AudioProcessor,
                          public juce::AudioProcessorParameter::Listener,
                          private juce::AsyncUpdater
{
public:
    EzSynth2Processor()
        : AudioProcessor(BusesProperties().withOutput(
              "Output", juce::AudioChannelSet::stereo(), true))
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& s = kSpecs[i];
            juce::AudioProcessorParameter* p = nullptr;
            const juce::ParameterID pid { s.id, 1 };
            if (i == p_preset)
                p = new juce::AudioParameterChoice(pid, u8(s.label), presetNames(),
                                                   (int) s.def);
            else if (s.type == 2)
                p = new juce::AudioParameterChoice(
                    pid, u8(s.label),
                    juce::StringArray::fromTokens(s.choices, "|", ""),
                    (int) s.def);
            else if (s.type == 3)
                p = new juce::AudioParameterBool(pid, u8(s.label), s.def > 0.5f);
            else if (s.type == 1)
                p = new juce::AudioParameterInt(pid, u8(s.label), (int) s.lo,
                                                (int) s.hi, (int) s.def);
            else
                p = new juce::AudioParameterFloat(
                    pid, u8(s.label),
                    juce::NormalisableRange<float>(s.lo, s.hi, 0.f, s.skew),
                    s.def);
            addParameter(p);
            p->addListener(this);
            params_.push_back(p);
        }

        P_.modWheel = &modWheel_;
        P_.expression = &expression_;
        P_.channelPressure = &channelPressure_;
        P_.pitchBend = &pitchBend_;

        bank_.prepare();

        for (int v = 0; v < kMaxVoices; ++v)
            synth_.addVoice(new SynthVoice(&bank_));
        synth_.setVoiceStealingEnabled(true);
        synth_.enableLegacyMode(2, juce::Range<int>(1, 16));
        synth_.modWheel = &modWheel_;
        synth_.expression = &expression_;

        for (int v = 0; v < 16; ++v)
            pianoSynth_.addVoice(new cadence::PianoVoice(&pPiano_));
        pianoSynth_.addSound(new cadence::PianoSound());
        pianoSynth_.setNoteStealingEnabled(true);

        currentPreset_ = 0;
        fillParams();
    }

    const juce::String getName() const override { return "EZSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return (int) presets().size(); }
    int getCurrentProgram() override { return currentPreset_; }
    void setCurrentProgram(int index) override { applyPreset(index); }
    const juce::String getProgramName(int index) override
    {
        if (index < 0 || index >= (int) presets().size())
            return {};
        return presets()[(size_t) index].name;
    }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double sr, int blockSize) override
    {
        sr_ = sr > 0 ? sr : 48000.0;
        bank_.prepare();
        synth_.setCurrentPlaybackSampleRate(sr_);
        for (int i = 0; i < synth_.getNumVoices(); ++i)
            synth_.getVoice(i)->setCurrentSampleRate(sr_);
        pianoSynth_.setCurrentPlaybackSampleRate(sr_);
        fx_.prepare(sr_, blockSize);
        arp_.prepare(sr_);
        midiScratch_.ensureSize(256);
        renderMidi_.ensureSize(256);
        arpMidi_.ensureSize(256);
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
        makeSureMpeLayout();

        if (P_.motor == 0)
        {
            cleanMidi(midi);
            publishWarp();
            pianoSynth_.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());
            fx_.process(buffer, P_, buffer.getNumSamples());
            finishBuffer(buffer);
            return;
        }

        for (int i = 0; i < synth_.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*>(synth_.getVoice(i)))
                v->setParams(P_);

        cleanMidi(midi);
        publishWarp();

        if (P_.arp.on)
        {
            const double bpm = host_ != nullptr
                ? juce::jmax(20.0, host_->tempoBpm()) : 120.0;
            arp_.process(midi, arpMidi_, buffer.getNumSamples(), bpm, P_.arp);
            synth_.renderNextBlock(buffer, arpMidi_, 0,
                                   buffer.getNumSamples());
        }
        else
        {
            if (arpActive_)
            {
                arp_.reset();
                arpActive_ = false;
            }
            synth_.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());
        }
        arpActive_ = P_.arp.on;

        fx_.process(buffer, P_, buffer.getNumSamples());
        finishBuffer(buffer);
    }

    void getStateInformation(juce::MemoryBlock& dest) override
    {
        juce::XmlElement x("EZSYNTH3");
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
        if (x == nullptr || (x->getTagName() != "EZSYNTH2"
                             && x->getTagName() != "EZSYNTH3"))
            return;
        loadingState_ = true;
        const bool legacy = x->getTagName() == "EZSYNTH2";
        for (int i = 0; i < kNumParams; ++i)
        {
            if (i == p_preset)
                continue;
            const auto& s = kSpecs[i];
            if (x->hasAttribute(s.id))
                setRaw(i, (float) x->getDoubleAttribute(s.id, s.def));
        }
        if (legacy && ! x->hasAttribute("a_engine"))
            setRaw(p_a_engine, (float) kAnalog);
        if (legacy && ! x->hasAttribute("motor"))
            setRaw(p_motor, 1.f);
        currentPreset_ = -1;
        loadingState_ = false;

        std::vector<WarpCurve> curves;
        if (auto* as = x->getChildByName("areas"))
            for (auto* e : as->getChildWithTagNameIterator("curve"))
            {
                WarpCurve c;
                const auto parts = juce::StringArray::fromTokens(
                    e->getStringAttribute("n"), ";", "");
                for (const auto& part : parts)
                {
                    const auto tn = juce::StringArray::fromTokens(part, ",", "");
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

    void parameterValueChanged(int index, float) override
    {
        if (index == p_preset && ! loadingState_ && ! applyingPreset_)
        {
            pendingPreset_.store((int) rawOf(p_preset));
            if (juce::MessageManager::getInstanceWithoutCreating() != nullptr)
                triggerAsyncUpdate();
            else
                applyPreset(pendingPreset_.exchange(-1));
        }
    }

    void parameterGestureChanged(int, bool) override {}

    ~EzSynth2Processor() override
    {
        for (auto* p : params_)
            p->removeListener(this);
    }

    // ---- host bridge ----
    void setHostContext(cadence::Engine* engine, std::int64_t trackId,
                        std::int64_t clipId)
    {
        host_ = engine;
        trackId_ = trackId;
        clipId_ = clipId;
    }

    bool hasClip() const { return host_ != nullptr && clipId_ != 0; }
    double clipOffset() const
    {
        return hasClip() ? host_->clipStart(clipId_) : 0.0;
    }

    // ---- LINES ----
    std::shared_ptr<const std::vector<WarpCurve>> areasShared() const
    {
        juce::SpinLock::ScopedLockType lock(areasLock_);
        return areasRef_;
    }

    void publishAreas(std::vector<WarpCurve> areas)
    {
        auto p = std::make_shared<const std::vector<WarpCurve>>(std::move(areas));
        juce::SpinLock::ScopedLockType lock(areasLock_);
        areasRef_ = std::move(p);
    }

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

    // ---- metadata ----
    juce::String getParameterID(int index) override
    {
        if (index >= 0 && index < kNumParams)
            return kSpecs[index].id;
        return {};
    }

    juce::String parameterGroupAt(int index) const
    {
        if (index >= 0 && index < kNumParams)
            return kSpecs[index].group;
        return {};
    }

    juce::String parameterIdAt(int index) const
    {
        if (index >= 0 && index < kNumParams)
            return kSpecs[index].id;
        return {};
    }

    void applyPreset(int index)
    {
        if (index < 0 || index >= (int) presets().size())
            return;
        applyingPreset_ = true;
        for (int i = 0; i < kNumParams; ++i)
            if (i != p_preset)
                setRaw(i, kSpecs[i].def);
        const auto& preset = presets()[(size_t) index];
        for (const auto& setting : preset.set)
            if (setting.id >= 0 && setting.id < kNumParams
                && setting.id != p_preset)
                setRaw(setting.id, setting.value);
        setRaw(p_preset, (float) index);
        currentPreset_ = index;
        applyingPreset_ = false;
        pendingPreset_.store(-1);
        fillParams();
    }

    float rawOf(int i) const
    {
        const auto& s = kSpecs[i];
        if (i == p_preset || s.type == 2)
            return (float) ((juce::AudioParameterChoice*) params_[(size_t) i])
                ->getIndex();
        if (s.type == 3)
            return ((juce::AudioParameterBool*) params_[(size_t) i])->get()
                       ? 1.f : 0.f;
        if (s.type == 1)
            return (float) ((juce::AudioParameterInt*) params_[(size_t) i])->get();
        return ((juce::AudioParameterFloat*) params_[(size_t) i])->get();
    }

    void setRaw(int i, float raw)
    {
        auto* p = params_[(size_t) i];
        const auto& s = kSpecs[i];
        if (i == p_preset || s.type == 2)
        {
            auto* c = (juce::AudioParameterChoice*) p;
            c->setValueNotifyingHost(c->convertTo0to1(raw));
        }
        else if (s.type == 3)
        {
            auto* b = (juce::AudioParameterBool*) p;
            b->setValueNotifyingHost(raw > 0.5f ? 1.f : 0.f);
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

private:
    void handleAsyncUpdate() override
    {
        const int preset = pendingPreset_.exchange(-1);
        if (preset >= 0 && preset < (int) presets().size())
            applyPreset(preset);
        if (mpeLayoutPending_.exchange(false))
            applyMpeLayout();
    }

    void makeSureMpeLayout()
    {
        const int mode = P_.mpe ? 1 : 0;
        const int bend = (int) std::round(P_.bend);
        const int mpeBend = (int) std::round(P_.mpeBend);
        if (mode == lastMpe_ && bend == lastBend_ && mpeBend == lastMpeBend_)
            return;
        lastMpe_ = mode;
        lastBend_ = bend;
        lastMpeBend_ = mpeBend;
        mpeMode_.store(mode);
        legBend_.store(bend);
        mpeRange_.store(mpeBend);
        if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
            && ! juce::MessageManager::existsAndIsCurrentThread())
        {
            mpeLayoutPending_.store(true);
            triggerAsyncUpdate();
        }
        else
        {
            applyMpeLayout();
        }
    }

    void applyMpeLayout()
    {
        if (mpeMode_.load() != 0)
        {
            juce::MPEZoneLayout layout;
            layout.setLowerZone(15, mpeRange_.load(), 2);
            synth_.setZoneLayout(layout);
        }
        else
        {
            synth_.enableLegacyMode(legBend_.load(), juce::Range<int>(1, 16));
        }
    }

    void fillParams()
    {
        auto F = [&](int i) { return ((juce::AudioParameterFloat*) params_[(size_t) i])->get(); };
        auto I = [&](int i) { return ((juce::AudioParameterInt*) params_[(size_t) i])->get(); };
        auto C = [&](int i) { return ((juce::AudioParameterChoice*) params_[(size_t) i])->getIndex(); };


        P_.motor = C(p_motor);
        P_.quality = C(p_quality);
        P_.bend = F(p_bend);
        P_.level = F(p_level);
        P_.pan = F(p_pan);
        P_.glide = F(p_glide);
        P_.velSens = F(p_vel);
        P_.morph = F(p_morph);
        P_.morphMode = C(p_morphMode);
        P_.width = F(p_width);
        P_.limiter = C(p_limiter) != 0;
        P_.mpe = C(p_mpe) != 0;
        P_.mpeBend = F(p_mpeBend);
        P_.mpePress = F(p_mpePress);
        P_.mpeTimbre = F(p_mpeTimbre);

        for (int o = 0; o < 3; ++o)
        {
            const int a = p_o1wave + o * 5;
            P_.a.osc[o] = { C(a), I(a + 1), I(a + 2), F(a + 3), F(a + 4) };
            const int b = p_b_o1wave + o * 5;
            P_.b.osc[o] = { C(b), I(b + 1), I(b + 2), F(b + 3), F(b + 4) };
        }
        P_.a.uni = I(p_uni);
        P_.a.spread = F(p_unispr);
        P_.a.sub = F(p_sublvl);
        P_.a.subWave = C(p_subwave);
        P_.a.noise = F(p_noise);
        P_.a.engine = C(p_a_engine);
        P_.a.wtTable = C(p_a_wtTable);
        P_.a.wtPos = F(p_a_wtPos);
        P_.a.wtWarp = F(p_a_wtWarp);
        P_.a.fmRatio = F(p_a_fmRatio);
        P_.a.fmIndex = F(p_a_fmIndex);
        P_.a.fmFeedback = F(p_a_fmFeedback);
        P_.a.fmMode = C(p_a_fmMode);
        P_.a.grainSize = F(p_a_grainSize);
        P_.a.grainDensity = F(p_a_grainDensity);
        P_.a.grainSpray = F(p_a_grainSpray);
        P_.a.grainTone = F(p_a_grainTone);
        P_.a.sampleTable = C(p_a_sampleTable);
        P_.a.level = F(p_a_level);
        P_.a.pan = F(p_a_pan);
        P_.a.tune = F(p_a_tune);
        P_.a.fine = F(p_a_fine);

        P_.b.uni = I(p_b_uni);
        P_.b.spread = F(p_b_unispr);
        P_.b.sub = F(p_b_sublvl);
        P_.b.subWave = C(p_b_subwave);
        P_.b.noise = F(p_b_noise);
        P_.b.engine = C(p_b_engine);
        P_.b.wtTable = C(p_b_wtTable);
        P_.b.wtPos = F(p_b_wtPos);
        P_.b.wtWarp = F(p_b_wtWarp);
        P_.b.fmRatio = F(p_b_fmRatio);
        P_.b.fmIndex = F(p_b_fmIndex);
        P_.b.fmFeedback = F(p_b_fmFeedback);
        P_.b.fmMode = C(p_b_fmMode);
        P_.b.grainSize = F(p_b_grainSize);
        P_.b.grainDensity = F(p_b_grainDensity);
        P_.b.grainSpray = F(p_b_grainSpray);
        P_.b.grainTone = F(p_b_grainTone);
        P_.b.sampleTable = C(p_b_sampleTable);
        P_.b.level = F(p_b_level);
        P_.b.pan = F(p_b_pan);
        P_.b.tune = F(p_b_tune);
        P_.b.fine = F(p_b_fine);

        P_.aux = { C(p_x_engine), F(p_x_level), F(p_x_pan), F(p_x_tune),
                   F(p_x_decay), F(p_x_color) };

        P_.f1 = { C(p_flttype), F(p_fltcut), F(p_fltres), F(p_fltkey),
                  F(p_fltenv), F(p_fltdrive) };
        P_.f2 = { C(p_f2type), F(p_f2cut), F(p_f2res), F(p_f2key),
                  F(p_f2env), F(p_f2drive) };
        P_.filterRoute = C(p_froute);
        P_.filterBalance = F(p_fbalance);

        P_.env[0] = { F(p_fenvA), F(p_fenvD), F(p_fenvS), F(p_fenvR) };
        P_.env[1] = { F(p_aenvA), F(p_aenvD), F(p_aenvS), F(p_aenvR) };
        P_.env[2] = { F(p_e3A), F(p_e3D), F(p_e3S), F(p_e3R) };
        P_.env[3] = { F(p_e4A), F(p_e4D), F(p_e4S), F(p_e4R) };

        for (int l = 0; l < 4; ++l)
        {
            const int base = p_l1shape + l * 4;
            P_.lfo[l] = { C(base), F(base + 1), F(base + 2), C(base + 3) };
        }
        P_.rndRate[0] = F(p_rnd1rate);
        P_.rndDepth[0] = F(p_rnd1depth);
        P_.rndRate[1] = F(p_rnd2rate);
        P_.rndDepth[1] = F(p_rnd2depth);

        for (int m = 0; m < kMaxMatrix; ++m)
        {
            const int base = p_m1src + m * 3;
            P_.matrix[m] = { C(base), C(base + 1), F(base + 2) };
        }
        for (int m = 0; m < kMaxMacros; ++m)
        {
            P_.macros[m].value = F(p_macro1 + m);
            const int base = p_macro1r1dst + m * 8;
            for (int r = 0; r < kMaxMacroRoutes; ++r)
                P_.macros[m].routes[r] = { C(base + r * 2), F(base + r * 2 + 1) };
        }
        for (int f = 0; f < kMaxFx; ++f)
        {
            const int base = p_fx1type + f * 6;
            P_.fx[f] = { C(base), F(base + 1), F(base + 2), F(base + 3),
                         F(base + 4), F(base + 5) };
        }

        P_.arp.on = C(p_arpOn) != 0;
        P_.arp.mode = C(p_arpMode);
        P_.arp.rate = C(p_arpRate);
        P_.arp.octaves = I(p_arpOct);
        P_.arp.gate = F(p_arpGate);
        P_.arp.swing = F(p_arpSwing);
        P_.arp.latch = C(p_arpLatch) != 0;
        P_.arp.steps = I(p_arpSteps);
        for (int s = 0; s < kArpSteps; ++s)
            P_.arp.stepOn[s] = I(p_arpStep1 + s) != 0;

        P_.warpAmt = F(p_warpamt);
        P_.warpR = F(p_warpR);

        pPiano_.decay.store(juce::jlimit(0.2f, 4.f, P_.env[1].decay / 0.25f));
        pPiano_.release.store(juce::jlimit(0.25f, 4.f, P_.env[1].release / 0.4f));
        pPiano_.level.store(P_.level);
    }

    void cleanMidi(juce::MidiBuffer& midi)
    {
        midiScratch_.clear();
        bool panic = false;
        for (const auto md : midi)
        {
            const auto msg = md.getMessage();
            if (msg.isController())
            {
                if (msg.getControllerNumber() == 1)
                    modWheel_.store(msg.getControllerValue() / 127.f);
                else if (msg.getControllerNumber() == 11)
                    expression_.store(msg.getControllerValue() / 127.f);
                else if (msg.getControllerNumber() == 120
                         || msg.getControllerNumber() == 123)
                    panic = true;
            }
            if (msg.isChannelPressure())
                channelPressure_.store(msg.getChannelPressureValue() / 127.f);
            if (msg.isPitchWheel())
                pitchBend_.store((msg.getPitchWheelValue() - 8192) / 8192.f);

            if (msg.isNoteOn() && msg.getVelocity() > 0)
                midiScratch_.addEvent(msg, md.samplePosition);
            else if (msg.isNoteOnOrOff())
                midiScratch_.addEvent(juce::MidiMessage::noteOff(
                                          msg.getChannel(), msg.getNoteNumber()),
                                      md.samplePosition);
            else
                midiScratch_.addEvent(msg, md.samplePosition);
        }
        midi.swapWith(midiScratch_);
        if (panic)
        {
            synth_.turnOffAllVoices(false);
            pianoSynth_.allNotesOff(0, false);
            arp_.reset();
        }
    }

    void publishWarp()
    {
        auto curves = areasShared();
        float amt = P_.warpAmt, R = P_.warpR;
        double ct = 0.0;
        bool haveCt = false;
        if (testWarp_)
        {
            ct = testCt_;
            haveCt = true;
        }
        else if (hasClip())
        {
            ct = host_->positionSeconds() - clipOffset();
            haveCt = true;
        }
        const bool on = haveCt && curves != nullptr && ! curves->empty()
                        && amt > 1e-4f;
        if (! on)
        {
            forEachVoice([](auto* v, int) { v->setWarpTarget(0.f); });
            return;
        }
        forEachVoice([&](auto* v, int note) {
            v->setWarpTarget(warpFor(*curves, ct, note, amt, R));
        });
    }

    template <typename Fn>
    void forEachVoice(Fn&& fn)
    {
        for (int i = 0; i < synth_.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<SynthVoice*>(synth_.getVoice(i)))
            {
                const int note = (int) v->getCurrentlyPlayingNote().initialNote;
                if (v->isActive())
                    fn(v, note);
            }
        for (int i = 0; i < pianoSynth_.getNumVoices(); ++i)
            if (auto* v = pianoSynth_.getVoice(i))
            {
                const int note = v->getCurrentlyPlayingNote();
                if (note >= 0 && v->isVoiceActive())
                {
                    if (auto* pv = dynamic_cast<cadence::PianoVoice*>(v))
                        fn(pv, note);
                }
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
                off += amt * (float) (diff * (1.0 - std::abs(diff) / R));
        }
        return juce::jlimit(-24.f, 24.f, off);
    }

    void finishBuffer(juce::AudioBuffer<float>& buffer)
    {
        const int nch = juce::jmin(2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();
        if (nch == 2)
        {
            auto* L = buffer.getWritePointer(0);
            auto* R = buffer.getWritePointer(1);
            const float sideGain = juce::jlimit(0.f, 2.f, P_.width * 2.f);
            for (int i = 0; i < n; ++i)
            {
                const float mid = (L[i] + R[i]) * 0.5f;
                const float side = (L[i] - R[i]) * 0.5f * sideGain;
                L[i] = mid + side;
                R[i] = mid - side;
            }
        }
        if (P_.limiter)
        {
            float peak = 0.f;
            for (int ch = 0; ch < nch; ++ch)
            {
                const auto* p = buffer.getReadPointer(ch);
                for (int i = 0; i < n; ++i)
                    peak = juce::jmax(peak, std::abs(p[i]));
            }
            if (peak > 0.98f)
            {
                const float g = 0.98f / peak;
                buffer.applyGain(g);
            }
        }
    }

    friend class EzSynth2Editor;

    std::vector<juce::AudioProcessorParameter*> params_;
    SynthParams P_;
    WavetableBank bank_;
    MpeSynth synth_;
    cadence::PianoParams pPiano_;
    juce::Synthesiser pianoSynth_;
    SynthFx fx_;
    Arpeggiator arp_;

    juce::MidiBuffer midiScratch_, renderMidi_, arpMidi_;
    std::atomic<float> modWheel_ { 0.f }, expression_ { 1.f },
        channelPressure_ { 0.f }, pitchBend_ { 0.f };
    std::atomic<int> pendingPreset_ { -1 };
    std::atomic<int> mpeMode_ { 0 }, legBend_ { 2 }, mpeRange_ { 48 };
    std::atomic<bool> mpeLayoutPending_ { false };
    int lastMpe_ = -1, lastBend_ = -1, lastMpeBend_ = -1;
    int currentPreset_ = 0;
    bool applyingPreset_ = false;
    bool loadingState_ = false;
    bool arpActive_ = false;

    cadence::Engine* host_ = nullptr;
    std::int64_t trackId_ = 0, clipId_ = 0;
    mutable juce::SpinLock areasLock_;
    std::shared_ptr<const std::vector<WarpCurve>> areasRef_ =
        std::make_shared<const std::vector<WarpCurve>>();
    bool testWarp_ = false;
    double testCt_ = 0.0;
    float testAmt_ = 0.8f, testRange_ = 12.f;
    double sr_ = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EzSynth2Processor)
};

class EzSynth2Editor : public juce::AudioProcessorEditor
{
public:
    explicit EzSynth2Editor(EzSynth2Processor& p)
        : AudioProcessorEditor(&p), proc_(p)
    {
        setSize(360, 100);
        auto* label = new juce::Label({}, "EZSynth Studio lives in Cadence");
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible(label);
        label->setBounds(12, 16, 336, 28);
        auto* hint = new juce::Label({}, "Open the instrument from the track strip.");
        hint->setJustificationType(juce::Justification::centred);
        hint->setColour(juce::Label::textColourId,
                        juce::Colour(0xFFA1A8B5));
        addAndMakeVisible(hint);
        hint->setBounds(12, 48, 336, 24);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF14161B));
    }

private:
    EzSynth2Processor& proc_;
};

juce::AudioProcessorEditor* EzSynth2Processor::createEditor()
{
    return new EzSynth2Editor(*this);
}

} // namespace

// =========================================================================
// 6. Public entry points
// =========================================================================
juce::AudioProcessor* create() { return new EzSynth2Processor(); }

juce::StringArray presetNamesForCatalog() { return presetNames(); }
int presetCountForTest() { return (int) presets().size(); }

juce::String makePresetState(const juce::String& presetName)
{
    const int index = presetIndexOf(presetName);
    if (index < 0)
        return {};
    std::unique_ptr<juce::AudioProcessor> proc(create());
    proc->setCurrentProgram(index);
    juce::MemoryBlock block;
    proc->getStateInformation(block);
    return block.toBase64Encoding();
}

void applyPresetForTest(juce::AudioProcessor* proc, int index)
{
    if (auto* p = dynamic_cast<EzSynth2Processor*>(proc))
        p->applyPreset(index);
}

void bindHostContext(juce::AudioProcessor* proc, cadence::Engine* engine,
                     std::int64_t trackId, std::int64_t clipId)
{
    if (auto* p = dynamic_cast<EzSynth2Processor*>(proc))
        p->setHostContext(engine, trackId, clipId);
}

void setWarpForTest(juce::AudioProcessor* proc,
                    const std::vector<std::pair<float, float>>& nodes,
                    double clipTimeSeconds, float amount, float rangeSemis)
{
    if (auto* p = dynamic_cast<EzSynth2Processor*>(proc))
        p->setWarpForTest(nodes, clipTimeSeconds, amount, rangeSemis);
}

juce::String parameterIdAt(juce::AudioProcessor* proc, int index)
{
    if (auto* p = dynamic_cast<EzSynth2Processor*>(proc))
        return p->parameterIdAt(index);
    return {};
}

juce::String parameterGroupAt(juce::AudioProcessor* proc, int index)
{
    if (auto* p = dynamic_cast<EzSynth2Processor*>(proc))
        return p->parameterGroupAt(index);
    return {};
}

} // namespace ezsynth2
