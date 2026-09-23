#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace cadence {

using TrackId = std::int64_t;
using ClipId = std::int64_t;
using SourceId = std::int64_t;

// A decoded audio source, resampled to the project rate once at load time and
// kept in memory. Prototype trade-off: no disk I/O on the audio thread, at
// the cost of RAM for long files. Per-track streaming read-ahead is the
// planned upgrade once the project model settles.
struct AudioSourceData {
    juce::AudioBuffer<float> samples; // at project rate; channels as loaded
    double sampleRate = 0.0;
    double durationSeconds = 0.0;
};
using AudioSourcePtr = std::shared_ptr<const AudioSourceData>;

struct MidiNote {
    double startSeconds = 0.0;  // within the clip
    double lengthSeconds = 0.0;
    int pitch = 60;
    float velocity = 0.8f;      // 0..1
};

// Note data of a MIDI clip (audio clips have a source instead).
struct MidiClipData {
    std::vector<MidiNote> notes;
};
using MidiClipDataPtr = std::shared_ptr<const MidiClipData>;

struct Clip {
    ClipId id = -1;
    SourceId sourceId = -1;         // audio clips: >= 0; MIDI clips: -1
    double startSeconds = 0.0;      // timeline position of the clip start
    double lengthSeconds = 0.0;     // visible length (<= source length - offset)
    double sourceOffsetSeconds = 0.0;
    float gain = 1.0f;              // linear
    float fadeInSeconds = 0.0f;     // linear gain ramp at the clip head
    float fadeOutSeconds = 0.0f;    // linear gain ramp at the clip tail
    bool muted = false;
    MidiClipDataPtr midi;           // non-null = MIDI clip

    double endSeconds() const { return startSeconds + lengthSeconds; }
    bool isMidi() const { return midi != nullptr; }
};

// A configured plugin slot (fx or instrument). `pluginUid` is the JUCE
// plugin identifier string used to re-create the instance.
struct FxSlot {
    juce::String pluginUid;
    juce::String name;
    bool bypass = false;
    juce::String state; // base64 plugin state (optional)
};

struct Track {
    enum class Type { Audio, Instrument };

    TrackId id = -1;
    Type type = Type::Audio;
    juce::String name;
    int colorIndex = 0;             // index into the UI track palette (ui-design.md §6.3)
    float volume = 1.0f;            // linear, 0..2
    float pan = 0.0f;               // -1 .. +1
    bool mute = false;
    bool solo = false;
    int inputChannel = 0;           // recording input (0 = L, 1 = R); persisted
    bool armed = false;             // transient: not persisted, not undoable
    std::vector<FxSlot> fx;         // insert chain, in order
    FxSlot instrument;              // instrument plugin (Instrument tracks); empty = internal synth
    std::vector<Clip> clips;
};

// Immutable project snapshot: edits build a modified copy and publish it
// atomically; the audio thread only ever reads one of these. ID counters and
// the source-path registry live in the document so undo/redo restores them.
struct Project {
    juce::String name = "Untitled";
    double sampleRate = 48000.0;
    double tempoBpm = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    float masterGain = 1.0f;
    std::vector<Track> tracks;
    std::vector<FxSlot> masterFx;
    std::map<SourceId, juce::String> sourceFiles; // absolute paths (engine form)
    std::map<SourceId, AudioSourcePtr> sources;   // decoded audio, shared across snapshots
    TrackId nextTrackId = 1;
    ClipId nextClipId = 1;
    SourceId nextSourceId = 1;

    double durationSeconds() const;
    const Track* findTrack(TrackId id) const;
    Track* findTrack(TrackId id);
    const Clip* findClip(ClipId id, const Track** ownerTrack = nullptr) const;
    Clip* findClip(ClipId id);
};

// Loads an audio file fully into memory at the project rate (resampling once
// if needed). Used by Engine::addClip.
juce::Result loadAudioSourceData(juce::AudioFormatManager& formats,
                                 const juce::File& file,
                                 double projectRate,
                                 AudioSourceData& out);

// JSON (de)serialization of the project document. projectToVar stores source
// paths relative to projectDir when possible; projectFromVar leaves them as
// stored (resolution to files is the caller's job).
juce::var projectToVar(const Project& project, const juce::File& projectDir);
juce::Result projectFromVar(const juce::var& value, Project& out);

} // namespace cadence
