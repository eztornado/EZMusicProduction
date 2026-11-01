#pragma once

// Song-arrangement plans produced by the AI producer.
//
// A plan is deliberately plain JSON: tracks, MIDI clips and notes in beats.
// The engine validates the whole document before touching the project, then
// applies it in one undo step. No plugin, audio file or network access is
// needed here; the caller only has to supply a well-formed plan.

#include <juce_core/juce_core.h>

#include <vector>

namespace cadence::ai
{

struct PlanNote
{
    int pitch = 60;
    double startBeat = 0.0;   // within the clip
    double lengthBeats = 0.25;
    float velocity = 0.8f;
};

struct PlanClip
{
    double startBeat = 0.0;   // absolute, from the song start
    double lengthBeats = 4.0;
    std::vector<PlanNote> notes;
};

struct PlanTrack
{
    int trackId = -1;         // > 0 reuses an existing instrument track
    juce::String name;
    juce::String role;        // drums, bass, chords, pad, lead, counter, arp...
    juce::String instrument;  // drums, bass, keys, pad, lead, piano, gm:0...
    juce::String instrumentName; // human-readable label for previews
    int color = -1;
    bool hasVolume = false;
    double volumeDb = 0.0;
    bool hasPan = false;
    double pan = 0.0;
    std::vector<PlanClip> clips;
};

struct ArrangementPlan
{
    juce::String title;
    juce::String summary;
    std::vector<PlanTrack> tracks;
    int totalNotes = 0;
};

// Parses and validates a plan. Returns false and fills `error` when the JSON
// is not usable; the project must not be touched in that case.
bool parseArrangementPlan(const juce::String& json, ArrangementPlan& out,
                          juce::String& error);

} // namespace cadence::ai
