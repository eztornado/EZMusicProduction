#pragma once

// AI clip generation — the engine-side "musical intelligence" layer.
//
// It answers one question: given a track, a point on the timeline and (optionally)
// a natural-language hint, what should the next clip contain?
//
// Pipeline:
//   1. Listen to the project around the anchor: MIDI plus the audible audio
//      clips in the context/generation range. Extract key, bar chords,
//      density, register and neighbouring motifs.
//   2. Interpret the prompt ("más oscuro", "menos notas", "bajo", "La menor",
//      "pad", "arpegio"...) into musical constraints.
//   3. Generate with the real Magenta Studio MusicVAE checkpoints when they
//      are installed, or with the built-in contextual generator otherwise.
//      Requests longer than the 4-bar checkpoints are generated in windows
//      that continue from the previous phrase.
//   4. Post-process: key snap, register fit, harmony, groove, velocity, exact
//      duration.
//
// The result is a list of clip-relative notes. Engine applies it atomically
// (one undo step) to a new clip or over an existing one.

#include <cadence/model.hpp>

#include "magenta_vae.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace cadence::ai
{

enum class Mode
{
    Melody = 0,
    Drums = 1,
    Bass = 2,
    Harmony = 3
};

struct Request
{
    TrackId trackId = -1;
    ClipId clipId = -1;
    double anchorSeconds = 0.0;
    Mode mode = Mode::Melody;
    int bars = 4;                 // total bars when lengthSeconds is 0
    double lengthSeconds = 0.0;   // desired clip length; 0 = bars * bar length
    double temperature = 0.95;
    int variations = 1;           // candidates evaluated; best is returned
    int seed = 0;                 // 0 = derive from clock
    juce::String prompt;
    bool replaceClip = false;     // rewrite clipId instead of adding a clip
    bool harmonize = false;       // add chord tones under the melody
    bool useModel = true;         // false forces the procedural path
};

struct GeneratedNote
{
    int pitch = 60;
    double startSeconds = 0.0;  // within the clip
    double lengthSeconds = 0.25;
    float velocity = 0.8f;
    bool isDrum = false;
};

struct Result
{
    bool ok = false;
    juce::String error;
    juce::String provider;        // "Magenta MusicVAE (melody)" / "Context"
    juce::String key;             // "A minor"
    juce::String chords;          // "Am | F | C | G"
    juce::String promptNote;      // how the prompt was interpreted
    juce::String contextNote;     // what was heard (audio/MIDI) and how long
    int seed = 0;
    double clipLengthSeconds = 0.0;
    double startSeconds = 0.0;   // timeline position the clip belongs at
    std::vector<GeneratedNote> notes;
};

// Default model folder: $CADENCE_AI_MODELS or
// ~/Music/EZMusicProduction/AI Models.
juce::File defaultModelDir();

class Service
{
public:
    Service() = default;
    ~Service() = default;

    void setModelDir(const juce::File& dir);
    juce::File modelDir() const;

    // True when the checkpoint for this mode is present on disk.
    bool modelAvailable(Mode mode) const;

    // Runs one generation. Thread-safe against a single caller; the engine
    // serialises jobs. Never touches the live project.
    Result generate(const Project& project, const Request& request);

    struct Analysis;
    struct Intent;

private:
    Analysis analyse(const Project& project, const Request& request) const;
    Intent interpretPrompt(const juce::String& prompt, const Analysis& analysis) const;

    Result generateWithModel(magenta::MusicVAE& model, const Project& project,
                             const Analysis& analysis,
                             const Request& request, const Intent& intent,
                             std::uint32_t seed) const;
    Result generateProcedural(const Analysis& analysis, const Request& request,
                              const Intent& intent, std::uint32_t seed) const;

    void postProcess(Result& result, const Analysis& analysis,
                     const Request& request, const Intent& intent) const;

    magenta::MusicVAE* ensureModel(Mode mode, juce::String& error);

    juce::File modelDir_;
    std::unique_ptr<magenta::MusicVAE> melodyModel_;
    std::unique_ptr<magenta::MusicVAE> drumModel_;
};

} // namespace cadence::ai
