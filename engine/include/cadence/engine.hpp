#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cadence/model.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cadence {

struct TrackCapture;
using CaptureList = std::vector<std::shared_ptr<TrackCapture>>;

// ---- AI clip generation ----

enum class AiMode
{
    Melody = 0,
    Drums = 1,
    Bass = 2,
    Harmony = 3
};

// What the UI asks for. The engine snapshots the project, analyses the
// musical context around anchorSeconds and generates a clip of `bars` bars.
struct AiGenerationRequest
{
    TrackId trackId = -1;
    ClipId clipId = -1;
    double anchorSeconds = 0.0;
    AiMode mode = AiMode::Melody;
    int bars = 4;                // total bars; the model runs in 4-bar windows
    double lengthSeconds = 0.0;  // exact duration; 0 = bars * bar length
    double temperature = 0.95;   // model sampling temperature
    int variations = 1;          // candidates evaluated; best is kept
    int seed = 0;                // 0 = derive from the clock
    juce::String prompt;         // free text, Spanish or English
    bool replaceClip = false;    // rewrite clipId instead of adding a clip
    bool harmonize = false;      // add chord tones under the melody
    bool useModel = true;        // false forces the built-in generator
};

// Result of one generation. Notes are clip-relative; startSeconds is the
// timeline position where applyAiGeneration places a new clip.
struct AiGenerationResult
{
    bool ok = false;
    juce::String error;
    juce::String provider;       // "Magenta MusicVAE (melody)" / "Context..."
    juce::String key;            // "A minor"
    juce::String chords;         // "Am | F | C | G"
    juce::String promptNote;     // how the prompt was interpreted
    juce::String contextNote;    // audio/MIDI context the generator heard
    int seed = 0;
    double clipLengthSeconds = 0.0;
    double startSeconds = 0.0;
    TrackId trackId = -1;
    ClipId clipId = -1;
    bool replaceClip = false;
    std::vector<MidiNote> notes;
};

// Plugin scanner/instantiator: VST3 + AudioUnit on macOS (VST2 requires the
// Steinberg headers; add JUCE_VST2_SDK_PATH to enable that format).
//
// Scanning runs CHUNKED on the JUCE message thread: beginScan() builds the
// file list (fast) and each AsyncUpdater event scans ONE file, merging the
// result into the known list under a mutex as soon as it lands. A full scan
// of a large plugin folder takes minutes and instantiates real plugin code,
// so blocking a thread (or the host UI) is not viable; results are cached
// to disk and reloaded at setup().
class PluginHost : private juce::AsyncUpdater
{
public:
    void setup();     // add formats + load cached descriptions; once per process
    void beginScan(); // kick a chunked scan (message thread); no-op if running
    void scanNow();   // blocking full scan (message thread; CLI/tests)
    bool scanActive() const { return scanning_.load(); }

    int pluginCount() const;
    int builtinCount() const { return (int) builtins_.size(); }
    std::unique_ptr<juce::PluginDescription> descriptionAt(int index) const;
    std::unique_ptr<juce::AudioProcessor> create(const juce::String& uid,
                                                 double sampleRate,
                                                 int blockSize);

private:
    void handleAsyncUpdate() override;
    void buildScanners();
    bool scanStep(); // scans one file; false when every format is exhausted
    void mergeNewTypes();
    void saveCache() const;
    void writeInFlightFile(const juce::String& file) const;
    void appendToDeadMansPedal(const juce::String& file) const;

    juce::AudioPluginFormatManager formatManager_;
    mutable std::mutex knownMutex_;
    juce::KnownPluginList known_;
    std::unique_ptr<juce::KnownPluginList> scanList_;
    std::vector<std::unique_ptr<juce::PluginDirectoryScanner>> scanners_;
    size_t scannerIndex_ = 0;
    std::atomic<bool> scanning_ {false};
    std::atomic<juce::uint64> stepDeadline_ {0}; // watchdog deadline, ms clock
    juce::File deadMansPedalFile_ =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Cadence/dead_mans_pedal.txt");
    juce::File inFlightFile_ =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Cadence/scan_inflight.txt");
    juce::File cacheFile_ =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Cadence/plugin_cache.xml");

    // Built-in DSP shipped with the app (EZ EQ Pro): same FX-chain path as
    // hosted plugins, but instantiated directly — no binary on disk.
    std::vector<juce::PluginDescription> builtins_;
};

// Immutable fx chain snapshot: swapped wholesale into the audio path. The
// prepared* fields live in the snapshot too, so the audio thread never reads
// mutable node fields while the message thread rebuilds the chain.
struct FxChain {
    std::vector<std::shared_ptr<juce::AudioProcessor>> processors;
    std::vector<bool> bypass;
    juce::StringArray uids; // change detection
    std::vector<bool> valid; // instantiated successfully
    std::vector<double> preparedRates; // per chain processor
    double preparedRate = 0.0;         // rate the chain was prepared at
    int preparedBlock = 0;             // maximum block size it was prepared for
};

// Immutable per-track plugin snapshot. The audio thread loads one of these
// atomically at the start of a block; the message thread never mutates a
// published state, it builds a new one and swaps it. The shared_ptr keeps a
// plugin instance alive for as long as the audio thread may be inside
// processBlock, which is the fix for the Analog Lab V crashes: the old code
// published/replaced/destroyed live instances while the audio thread used
// them.
struct TrackNodeState {
    std::shared_ptr<const FxChain> fx;
    std::shared_ptr<juce::AudioProcessor> instrument;
    bool instrumentBypass = false;
    juce::String instrumentUid;
    double instrumentPreparedRate = 0.0;
    int instrumentPreparedBlock = 0;
    std::shared_ptr<juce::AudioProcessor> midiFx;
    bool midiFxBypass = false;
    juce::String midiFxUid;
    double midiFxPreparedRate = 0.0;
    int midiFxPreparedBlock = 0;
};

// Runtime audio node of one track. The map entry is NEVER erased while the
// engine lives: nodeFor() hands raw pointers to the audio thread, so erasing
// a node would be a use-after-free. Removed tracks get an empty state
// instead, which releases their plugins once the audio thread drops its last
// snapshot reference.
struct TrackAudioNode {
    std::shared_ptr<const TrackNodeState> state; // atomic_load/store
};

// Renders project snapshots: every block it picks up the current immutable
// Project, mixes the audible tracks/clips and advances the transport. The
// Engine owns one live instance driven by the audio device; extra instances
// can be created for offline pulls (Engine::renderToFile).
class ProjectAudioSource : public juce::AudioSource,
                           public juce::AudioPlayHead {
public:
    // Replaces the project being rendered; the transport position is kept.
    void setProject(std::shared_ptr<const Project> project);
    // Rebuilds fx/instrument plugin nodes that changed (message thread).
    // With deferHeavyPlugins, NEW plugin instances are queued and applied
    // one per message-loop pass (project load stays UI-responsive); false
    // builds everything inline (tests, offline render).
    void reconcileNodes(const Project& project, PluginHost& host,
                        int blockSize, bool deferHeavyPlugins = false);
    // Drains one queued plugin job inline (message thread).
    void pumpOnePluginJob();
    bool hasPendingPluginJobs() const;
    std::shared_ptr<juce::AudioProcessor> fxProcessorFor(TrackId trackId,
                                                         int slotIndex);
    std::shared_ptr<juce::AudioProcessor> instrumentProcessorFor(TrackId trackId);
    std::shared_ptr<juce::AudioProcessor> midiFxProcessorFor(TrackId trackId);
    std::shared_ptr<juce::AudioProcessor> masterFxProcessorFor(int slotIndex);
    // Processors currently referenced by any live track/master snapshot.
    // Used by the Engine to close editor windows for removed plugins.
    std::vector<juce::AudioProcessor*> liveProcessors() const;
    // Called on the message thread after a reconcile/job swaps snapshots.
    void setPluginsChangedCallback(std::function<void()> cb)
    {
        pluginsChangedCallback_ = std::move(cb);
    }
    // Plugin-parameter automation (§7.12 v1.5): applies every param lane of
    // the track to its target processor at the block position (audio path).
    void applyParamAutomation(const Track& track, const TrackNodeState* state,
                              std::int64_t blockStart, double rate);
    // Hard-stop every voice now (message thread, device stopped/restarting).
    // Unlike requestPanic() this does not depend on another audio callback:
    // plugins get a silent panic block and the internal synths are silenced.
    void flushAllNotes();
    void setPluginHost(PluginHost* host) { pluginHost_ = host; }

    void play();
    void pause();
    void stop();
    void seek(double seconds);

    bool isPlaying() const { return playing_.load(); }
    double positionSeconds() const;
    std::int64_t positionSamples() const;
    void setRecording(bool r) { recording_.store(r); }
    // Recording context for MIDI-GENERATOR instruments (EZDrums): the
    // transport start and the capture list let the render path route the
    // plugin-performed notes into the armed track's take.
    void setRecording(bool r, std::int64_t startSamples,
                      std::shared_ptr<const CaptureList> caps);
    void setMetronome(bool on) { metronome_.store(on); }
    void allNotesOff();
    // True when the track's node holds a prepared instrument plugin —
    // false means MIDI falls back to the internal synth.
    bool trackInstrumentLive(TrackId trackId) const;

    // Live MIDI monitoring: notes arriving from the selected MIDI input are
    // played by armed instrument tracks (even with the transport stopped).
    void pushExternalMidi(const juce::MidiMessage& m);

    // AudioPlayHead: plugins (iZotope etc.) gate their meters/DSP on the
    // host transport. Without this they idle forever.
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override;

    // Queue a full MIDI panic for the next audio block: sustain-off, explicit
    // note-offs for every pitch, All-Sound-Off and All-Notes-Off. The audio
    // thread compares a generation counter, so a request can never be lost
    // between a block that started before stop/seek and the next one.
    void requestPanic()
    {
        panicGeneration_.fetch_add(1, std::memory_order_release);
    }

    // Metering: read-and-reset peaks (float bits in an atomic; the audio
    // thread is the only writer). trackIndex is the position in the snapshot.
    static constexpr int kMeterTracks = 64;
    float masterPeak(int channel);
    float trackPeak(int trackIndex);

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

private:
    std::shared_ptr<const Project> project_; // accessed with std::atomic_load/store
    std::atomic<bool> playing_ { false };
    std::atomic<bool> recording_ { false };
    std::atomic<bool> metronome_ { false };
    std::atomic<std::int64_t> positionSamples_ { 0 };
    // Generator recording context (see setRecording overload).
    std::atomic<std::int64_t> recordStartSamples_ { 0 };
    std::shared_ptr<const CaptureList> captureList_; // atomic_load/store
    juce::AudioBuffer<float> trackScratch_;
    juce::AudioBuffer<float> midiFxSilent_; // zero audio passed to the MIDI insert
    juce::MidiBuffer blockMidi_;
    juce::MidiBuffer panicMidi_; // prebuilt on prepareToPlay (audio-thread safe)
    juce::MidiBuffer fxEmptyMidi_; // reused by processFxChain (no alloc/block)
    mutable juce::SpinLock synthsLock_;
    std::map<TrackId, std::unique_ptr<juce::Synthesiser>> synths_;
    double synthsRate_ = 0.0;
    // Fixed-size (no allocation on the audio thread). Indexed by meter slot,
    // which is also the track's position in the project snapshot.
    std::array<int, kMeterTracks> lastSustainCc_ {};
    std::atomic<std::uint64_t> panicGeneration_ { 0 };
    std::uint64_t lastPanicGeneration_ = 0; // audio thread only
    juce::AudioBuffer<float> masterBuffer_;
    std::shared_ptr<const FxChain> masterFxChain_; // atomic_load/store
    std::atomic<double> deviceRate_ { 0.0 };
    std::atomic<int> deviceBlock_ { 0 };
    // Cached once: getenv() may lock and is not real-time safe.
    const bool debugMidi_ = std::getenv("CADENCE_MIDI_DBG") != nullptr;
    const bool debugArp_ = std::getenv("CADENCE_ARP_DBG") != nullptr;
    std::atomic<std::uint32_t> masterPeaks_[2] {{0}, {0}};
    std::atomic<std::uint32_t> trackPeaks_[kMeterTracks] {};

    // Live MIDI monitoring (external input → armed instrument tracks).
    std::mutex externalMutex_;
    std::vector<juce::MidiMessage> externalQueue_;
    std::vector<juce::MidiMessage> pendingExternal_;

    // Serialises message-thread rebuilds. The audio thread never takes this:
    // it only loads the node's immutable state snapshot.
    mutable std::mutex trackNodesMutex_;
    std::map<TrackId, std::unique_ptr<TrackAudioNode>> trackNodes_;
    PluginHost* pluginHost_ = nullptr;

    // Deferred plugin instantiation (project load): one job per track,
    // applied one per message-loop pass so the UI keeps painting.
    struct PluginJob {
        TrackId trackId = -1;
        std::vector<FxSlot> fx;
        FxSlot instrument;
        double sampleRate = 0.0;
        int maxBlock = 0;
    };
    std::vector<PluginJob> pluginJobs_;      // message thread only
    bool pluginJobsPumping_ = false;         // message thread only
    std::function<void()> pluginsChangedCallback_; // message thread only
    void enqueuePluginJob(const Track& track, double rate, int maxBlock);
    bool jobQueuedFor(const Track& track, double rate, int maxBlock) const;
    void applyPluginJob(const PluginJob& job, PluginHost& host);

    juce::Synthesiser* getOrCreateSynth(TrackId trackId, double rate);
    void silenceSynthForTrack(TrackId trackId);
    TrackAudioNode* nodeFor(TrackId trackId) const;
    // Message-thread helper: creates the node once and returns it. Map
    // insertion is guarded by synthsLock_ because nodeFor() reads the map
    // from the audio thread.
    TrackAudioNode& ensureNode(TrackId trackId);
    // Message-thread helper: make sure every instrument track already has a
    // synthesiser. Creating one (and its voices) on the audio thread is an
    // allocation and a potential dropout.
    void prepareInternalSynths(const Project& project, double rate);
    void processFxChain(const FxChain& chain, juce::AudioBuffer<float>& buffer,
                        double rate, int numSamples);
    void drainExternalMidi();
    // Sends the full panic to EVERY instrument track (including muted,
    // soloed and non-armed ones) even while the transport is stopped. Without
    // this, a plugin on a non-armed track never sees the panic and its voices
    // survive until the next play.
    void panicAllInstrumentTracks(const Project& project, int numSamples);
    void renderArmedInstrumentMonitoring(const juce::AudioSourceChannelInfo& info,
                                         const Project& project);
};

// Capture buffer for one armed track while recording. The audio thread
// writes; capacity is preallocated on arm (message thread).
struct TrackCapture {
    TrackCapture(TrackId t, int ch, double rate, int capacitySamples, bool midi)
        : trackId(t), inputChannel(ch), sampleRate(rate), isMidi(midi),
          buffer(1, midi ? 1 : capacitySamples) {}

    const TrackId trackId;
    const int inputChannel;
    const double sampleRate; // the DEVICE rate the capture runs at
    const bool isMidi;       // instrument track: captures MIDI events instead
    juce::AudioBuffer<float> buffer;
    std::atomic<int> written { 0 };

    // MIDI capture (only for isMidi captures).
    struct RecordedMidiEvent {
        int sampleFromRecordStart = 0;
        std::uint8_t status = 0, d1 = 0, d2 = 0;
    };
    std::vector<RecordedMidiEvent> midiEvents; // guarded by midiMutex
    // mutable: the guard is locked from const contexts too (locking does not
    // change the logical state of the capture).
    mutable std::mutex midiMutex;
    std::atomic<int> played { 0 };
    int loopbackBoundary = 0; // audio thread only (loopback synth)
};
using CaptureList = std::vector<std::shared_ptr<TrackCapture>>;

// One note edit inside a batch (multi-note move from the piano roll).
struct MidiNoteEdit {
    int index = -1;
    int pitch = 60;
    double startSeconds = 0.0;
    double lengthSeconds = 0.0;
};

// Phase 2 engine: owns the project document (published as immutable
// snapshots), the audio device and the project transport. Phase 1's
// single-file "tape" playback survives as a convenience (loadFile builds a
// one-track project).
namespace ai { class Service; }

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Runs fn on the JUCE message thread (waits for completion).
    static void runOnMessageThread(const std::function<void()>& fn);

    // ---- device ----

    // Opens the default output device (stereo, two inputs for recording).
    // Required for live playback/recording.
    juce::Result initialise();
    // Closes and re-opens the audio device: recovers from CoreAudio client
    // kills (overrun disabling) without restarting the app.
    juce::Result restartAudio();
    // Opens the audio device on a background thread (non-blocking). Poll
    // audioInitialised() to check when it's ready.
    void initialiseAudioInBackground();
    bool audioInitialised() const { return audioReady_.load(); }
    bool isInitialised() const;
    // Health/diagnostics. Safe from any thread; the strings are stable until
    // the next call on this engine.
    bool audioDeviceRunning() const { return audioDeviceRunning_.load(); }
    int audioXRunCount() const;
    int audioRecoveryCount() const { return audioRecoveryCount_.load(); }
    const char* lastAudioErrorUtf8();

    juce::StringArray outputDeviceNames();
    juce::StringArray inputDeviceNames();
    juce::String currentOutputDeviceName() const;
    juce::String currentInputDeviceName() const;
    int currentBufferSizeSamples() const;

    // UTF-8 accessors for FFI: strings are stable until the next read
    // (backed by stringOut_), unlike toRawUTF8() on temporaries.
    const char* outputDeviceNameAtUtf8(int index);
    const char* inputDeviceNameAtUtf8(int index);
    const char* midiInputNameAtUtf8(int index);
    const char* currentOutputDeviceNameUtf8();
    const char* currentInputDeviceNameUtf8();

    // ---- device configuration (§7.14) ----
    // Empty strings keep the current device; rate/buffer 0 keeps current.
    juce::Result setDeviceConfig(const juce::String& outputDevice,
                                 const juce::String& inputDevice,
                                 double sampleRate, int bufferSize);
    juce::Array<double> availableSampleRates() const;
    juce::Array<int> availableBufferSizes() const;
    juce::StringArray midiInputNames() const;
    int deviceInputChannelCount() const; // active inputs of the open device (0 if none)

    // MIDI input selection (v1: single global input). Index into
    // midiInputNames(); -1 clears the selection.
    void selectMidiInput(int index);
    int selectedMidiInputIndex() const;

    // App-level settings file: AudioDeviceManager state + MIDI enable list.
    juce::Result saveDeviceSettings(const juce::File& file) const;
    juce::Result loadDeviceSettings(const juce::File& file);

    // ---- plugins (§7.7) ----

    void rescanPlugins();
    void scanPluginsNow(); // blocking (message thread); CLI/tests
    bool pluginScanActive() const;

    // ---- plugin editor windows (§7.7) ----
    // Each open editor is a native window around the plugin's own editor
    // component (or a generic parameter view if it has none). Windows are
    // created on the message thread and kept alive until engine shutdown.
    juce::Result openPluginEditor(TrackId trackId, int slotIndex);
    juce::Result openInstrumentEditor(TrackId trackId, ClipId clipId = 0);
    int pluginCount() const;
    const char* pluginNameAt(int index) const;
    const char* pluginUidAt(int index) const;
    bool pluginIsInstrumentAt(int index) const;
    const char* pluginFormatAt(int index) const;

    // ---- project document ----

    // Replaces the project with an empty one. Stops playback, clears history.
    juce::Result newProject(double sampleRate);
    TrackId addTrack(const juce::String& name, int colorIndex,
                     Track::Type type = Track::Type::Audio);
    ClipId addClip(TrackId trackId, const juce::File& sourceFile,
                   double startSeconds, float gain);

    // All of these record one undo entry; a failed lookup records none.
    juce::Result removeTrack(TrackId trackId);
    juce::Result removeClip(ClipId clipId);
    juce::Result setClipStart(ClipId clipId, double seconds);
    juce::Result setClipGain(ClipId clipId, float gain);
    // Trim/move in one command (one undo entry): what an edge-drag commits.
    // Values are clamped to valid ranges, not rejected.
    juce::Result setClipBounds(ClipId clipId, double startSeconds,
                               double sourceOffsetSeconds, double lengthSeconds);
    juce::Result setClipFades(ClipId clipId, double fadeInSeconds, double fadeOutSeconds);

    // Moves a clip to another track (same track type required) and start
    // position in one undo entry. Fades/offset are preserved.
    juce::Result moveClipToTrack(ClipId clipId, TrackId targetTrackId,
                                 double startSeconds);
    // Splits a clip at the given timeline position into two clips (the right
    // part gets a new id); MIDI notes distribute by start position, crossing
    // notes truncate on the left side. One undo entry.
    juce::Result splitClipAt(ClipId clipId, double timeSeconds);

    juce::Result setTrackName(TrackId trackId, const juce::String& name);
    juce::Result setTrackColor(TrackId trackId, int colorIndex); // 0..15

    juce::Result setTrackVolume(TrackId trackId, float linearGain); // 0..2

    // Volume automation (§7.12 v1, Read always active): replaces the whole
    // curve (empty = clears it, track falls back to its static volume).
    // Points are sorted by time; one undo entry per call.
    juce::Result setTrackVolumeAutomation(
        TrackId trackId, const std::vector<AutomationPoint>& points);
    // Plugin-parameter automation lane (§7.12 v1.5): keyed by
    // (slotKind 0=fx/1=instrument, slotIndex, paramIndex). Whole-curve
    // replace, one undo entry; the display name is stored for the UI.
    juce::Result setTrackParamAutomation(TrackId trackId, int slotKind,
                                         int slotIndex, int paramIndex,
                                         const juce::String& name,
                                         const std::vector<AutomationPoint>& points);
    int trackParamLaneCount(TrackId trackId) const;
    juce::Result trackParamLaneInfo(TrackId trackId, int laneIndex,
                                    int* slotKind, int* slotIndex,
                                    int* paramIndex) const;
    const char* trackParamLaneName(TrackId trackId, int laneIndex) const;
    int trackParamLanePointCount(TrackId trackId, int laneIndex) const;
    AutomationPoint trackParamLanePointAt(TrackId trackId, int laneIndex,
                                          int pointIndex) const;
    juce::Result removeTrackParamLane(TrackId trackId, int laneIndex);
    // Parameter enumeration for the chooser UI (requires the node).
    int pluginParamCount(TrackId trackId, int slotKind, int slotIndex) const;
    const char* pluginParamName(TrackId trackId, int slotKind, int slotIndex,
                                int paramIndex) const;
    int trackVolumeAutoCount(TrackId trackId) const;
    AutomationPoint trackVolumeAutoAt(TrackId trackId, int index) const;

    // Generic lane access (§7.12 v1.5): lane 0 = volume, 1 = pan,
    // 2 = sustain (CC64). Same whole-curve replace semantics.
    juce::Result setTrackLaneAutomation(
        TrackId trackId, int lane, const std::vector<AutomationPoint>& points);
    int trackLaneAutomationCount(TrackId trackId, int lane) const;
    AutomationPoint trackLaneAutomationAt(TrackId trackId, int lane,
                                          int index) const;
    AutomationCurvePtr trackLaneAutomation(TrackId trackId, int lane) const;
    // Effective gain at a timeline position: the automation curve evaluated
    // there, or the static volume when the track has no curve. The UI fader
    // mirrors this while playing (Read mode).
    float trackVolumeEffectiveAt(TrackId trackId, double timeSeconds) const;
    // Write mode (§7.12 v1.5): inserts or replaces ONE point at the given
    // time (matches within 1 ms) — used by the W button while playing.
    juce::Result setTrackVolumeAutomationPointAt(TrackId trackId,
                                                 double timeSeconds,
                                                 double value);

    // Input quantize (§7.5.2 v1.5): grid in seconds for recorded MIDI takes
    // (0 = off). Applied to note starts when the take is finalized,
    // quantized against the project timeline.
    double inputQuantizeSeconds() const;
    juce::Result setInputQuantize(double gridSeconds);
    // MIDI input latency compensation: recorded notes are shifted this many
    // ms EARLIER at finalize (keyboard + USB + CoreMIDI delivery delay).
    // Persisted in the device settings file.
    double midiInputLatencyMs() const { return midiInputLatencyMs_; }
    void setMidiInputLatencyMs(double ms) {
        midiInputLatencyMs_ = juce::jlimit(-100.0, 100.0, ms);
    }
    juce::Result setTrackPan(TrackId trackId, float pan);           // -1..+1
    juce::Result setTrackMute(TrackId trackId, bool muted);
    juce::Result setTrackSolo(TrackId trackId, bool soloed);
    juce::Result setMasterGain(float linearGain); // 0..4

    juce::Result addTrackFx(TrackId trackId, int pluginIndex);   // appends an fx slot
    juce::Result removeTrackFx(TrackId trackId, int slotIndex);
    juce::Result setTrackFxBypass(TrackId trackId, int slotIndex, bool bypass);
    int trackFxCount(TrackId trackId) const;
    const char* trackFxNameAt(TrackId trackId, int slotIndex) const;
    bool trackFxBypassAt(TrackId trackId, int slotIndex) const;

    // Instrument plugin for instrument tracks (replaces the internal synth).
    juce::Result setTrackInstrumentPlugin(TrackId trackId, int pluginIndex);
    juce::Result clearTrackInstrument(TrackId trackId);
    const char* trackInstrumentName(TrackId trackId) const;

    // Restores a plug-in's saved state on its LIVE instance (an insert slot or
    // the track's instrument). Cubase (.cpr) stores the VST3 component and
    // controller states apart; the wrapper state JUCE's setStateInformation
    // expects is rebuilt here. Either pointer may be null/empty. Returns
    // failure when the slot holds no live plug-in.
    juce::Result setTrackFxState(TrackId trackId, int slotIndex,
                                 const void* component, int componentSize,
                                 const void* controller, int controllerSize);
    juce::Result setTrackInstrumentState(TrackId trackId,
                                         const void* component,
                                         int componentSize,
                                         const void* controller,
                                         int controllerSize);
    // Test hook: the state the live plug-in currently reports, as the base64
    // blob `getStateInformation` produces (empty when the slot is empty).
    // slotKind: 0 = fx chain, 1 = instrument. Backed by stringOut_.
    const char* pluginStateBase64(TrackId trackId, int slotKind,
                                  int slotIndex) const;

    // MIDI insert (EZArpeggio): transforms the track's MIDI stream.
    juce::Result setTrackMidiFx(TrackId trackId, int pluginIndex);
    juce::Result clearTrackMidiFx(TrackId trackId);
    const char* trackMidiFxName(TrackId trackId) const;
    // True when the node holds a prepared instrument plugin instance
    // (false = playback falls back to the internal synth).
    bool trackInstrumentLive(TrackId trackId) const;

    // Clip clipboard (§7.4): copy stores a snapshot of the clip (audio
    // clips keep their source reference; MIDI clips share their note data).
    // Paste inserts a duplicate on a same-kind track at the given time.
    juce::Result copyClip(ClipId clipId);
    juce::Result pasteClip(TrackId trackId, double startSeconds);
    // Deferred plugin-load pump (see ProjectAudioSource).
    bool hasPendingPluginJobs() const
    {
        return transport_->hasPendingPluginJobs();
    }
    void pumpOnePluginJob() { transport_->pumpOnePluginJob(); }

    juce::Result addMasterFx(int pluginIndex);
    juce::Result removeMasterFx(int slotIndex);
    juce::Result setMasterFxBypass(int slotIndex, bool bypass);
    int masterFxCount() const;
    const char* masterFxNameAt(int slotIndex) const;
    bool masterFxBypassAt(int slotIndex) const;
    // Native editor for a master insert (message thread).
    juce::Result openMasterFxEditor(int slotIndex);

    // Recording setup: arm + input are not undoable and not part of history.
    juce::Result setTrackArmed(TrackId trackId, bool armed);
    juce::Result setTrackInputChannel(TrackId trackId, int channel);
    bool trackArmed(TrackId trackId) const;
    int trackInputChannel(TrackId trackId) const;

    void setMetronome(bool on);

    // ---- tempo / time signature (project-wide; feeds ruler, metronome,
    // quantize grid and snapping). Undoable edits. ----
    double tempoBpm() const;
    juce::Result setTempo(double bpm);              // 20..300
    int timeSigNumerator() const;
    int timeSigDenominator() const;
    juce::Result setTimeSignature(int numerator, int denominator);

    // Test hook: injects a live MIDI note as if it came from the selected
    // input — exercises the armed-track monitoring path without hardware.
    void debugPushLiveMidi(int pitch, bool noteOn);

    // Recording: startRecord captures every armed track; stopRecord (or
    // stop/pause) finalizes the takes into clips as ONE undo entry. Takes
    // are written to disk under the project's Recordings folder.
    juce::Result startRecord();
    juce::Result stopRecord();
    bool isRecording() const { return recording_.load(); }
    double recordStartSeconds() const { return recordStartSeconds_.load(); }
    // Live MIDI take of an armed instrument track while recording (§7.11):
    // the timeline draws the notes as they arrive instead of showing an empty
    // clip until the take is finalized. Starts are relative to the record
    // start (same as a finalized MIDI clip's note starts). Notes still held
    // down are reported with their length up to now. The count call refreshes
    // the cache; the accessor reads it (message thread only).
    int recordingNoteCount(TrackId trackId) const;
    void recordingNoteAt(TrackId trackId, int index, int* pitch, float* velocity,
                         double* startSeconds, double* lengthSeconds) const;
    // Test hook: synthesize a tone into captures instead of reading inputs,
    // so the whole record pipeline is verifiable without hardware.
    static void setDebugLoopback(bool enabled) { debugLoopback_.store(enabled); }

    int trackCount() const;

    // ---- MIDI clips (editing API for the piano roll) ----
    ClipId addMidiClip(TrackId trackId, double startSeconds, double lengthSeconds);
    juce::Result midiClipAddNote(ClipId clipId, int pitch, float velocity,
                                 double startInClipSeconds, double lengthSeconds);
    juce::Result midiClipRemoveNote(ClipId clipId, int noteIndex);
    juce::Result midiClipSetNote(ClipId clipId, int noteIndex, int pitch,
                                 double startInClipSeconds, double lengthSeconds);
    // Applies several note edits in one undo entry (group move/resize).
    juce::Result midiClipSetNotes(ClipId clipId,
                                  const std::vector<MidiNoteEdit>& edits);

    // ---- AI clip generation (Magenta MusicVAE + contextual generator) ----
    // Generation runs on a worker thread: startAiGeneration returns
    // immediately, aiGenerationState() reports progress, and
    // aiGenerationResult() becomes valid when it reaches kAiReady.
    enum AiState { kAiIdle = 0, kAiRunning = 1, kAiReady = 2, kAiFailed = 3 };

    void setAiModelDir(const juce::File& dir);
    juce::File aiModelDir() const;
    bool aiModelAvailable(AiMode mode) const;
    const char* aiModelDirUtf8() const;

    bool startAiGeneration(const AiGenerationRequest& request);
    int aiGenerationState() const { return aiState_.load(std::memory_order_acquire); }
    bool aiGenerationRunning() const { return aiGenerationState() == kAiRunning; }
    AiGenerationResult aiGenerationResult() const;
    void clearAiGenerationResult();
    void cancelAiGeneration();
    // Applies the current result as ONE undo entry. A new clip is created at
    // result.startSeconds, or the target clip is rewritten when replaceClip
    // is set. Returns the resulting clip id through outClipId.
    juce::Result applyAiGeneration(ClipId* outClipId = nullptr);
    // Strings of the current result, valid until the next call on this engine.
    const char* aiResultString(int field) const; // 0 error 1 provider 2 key 3 chords 4 prompt

    // ---- quantize (§7.5.2, non-destructive) ----
    // Moves note starts toward the nearest grid position. Non-cumulative:
    // re-quantizing always works from each note's original position unless
    // iterative is set (then one strength step from the current position).
    // swing 0..1 shifts odd grid positions toward the next; catchRange 0..1
    // leaves notes already within that fraction of the grid untouched;
    // maxMove 0..1 caps the displacement; nonGridProtect skips notes that
    // sit exactly between two grid positions.
    juce::Result quantizeClipNotes(ClipId clipId, double gridSeconds,
                                   double swing, double catchRange,
                                   double maxMove, bool nonGridProtect,
                                   bool iterative, double iterStrength);
    juce::Result resetClipQuantize(ClipId clipId);  // back to original starts
    juce::Result freezeClipQuantize(ClipId clipId); // bake current as original
    double clipNoteOrigStartAt(ClipId clipId, int index) const;
    bool clipIsMidi(ClipId clipId) const;
    int clipNoteCount(ClipId clipId) const;
    void clipNoteAt(ClipId clipId, int index, int* pitch, float* velocity,
                    double* startSeconds, double* lengthSeconds) const;

    // Waveform peaks for display: fills numPixels min/max pairs for the clip
    // region [startSeconds, startSeconds+lengthSeconds) (clip-relative).
    int clipPeaks(ClipId clipId, double startSeconds, double lengthSeconds,
                  int numPixels, float* outMins, float* outMaxs) const;

    // ---- undo / redo (engine-owned) ----
    bool canUndo() const;
    bool canRedo() const;
    bool undo();
    bool redo();

    // ---- persistence ----
    juce::Result saveProject(const juce::File& file);
    juce::Result loadProject(const juce::File& file);

    // ---- transport / render / misc ----
    juce::Result loadFile(const juce::File& file);
    // Import a standard MIDI file (Type 0/1) as a project: every track with
    // notes becomes an instrument track with one clip; tempo and time
    // signature come from the file's meta events. Replaces the project.
    juce::Result importMidiProject(const juce::File& file);
    // Import a folder of audio files (stems): one audio track per file,
    // named by the file stem, clip at 0. Appends to the project.
    juce::Result importStemsFolder(const juce::File& folder);
    // One audio file -> one audio track + clip at 0 (the unified "open"
    // flow dispatches .wav/.aiff here).
    juce::Result importAudioFile(const juce::File& file);
    // NATIVE open panel (NSOpenPanel via JUCE): blocks the calling thread
    // (the Flutter main thread IS the JUCE message thread) until the user
    // picks or cancels. Returns the chosen path or "" when cancelled.
    // filters: juce pattern list, e.g. "*.ezmpp;*.mid;*.cpr".
    const char* pickOpenFile(const juce::File& startDir,
                             const juce::String& filters);

    // ---- sound-library audition preview --------------------------------
    // Plays one file on a dedicated channel, mixed on top of the transport
    // and independent of it: previewing never touches the project. Starting a
    // new preview replaces the previous one.
    juce::Result previewSound(const juce::File& file);
    void stopPreview();
    bool isPreviewing() const { return previewActive_.load(std::memory_order_relaxed); }

    // ---- document read access (UI snapshot pulls) ----
    // Cheap, allocation-free reads; strings are stable until the next read.
    TrackId trackIdAt(int index) const;
    const char* trackName(TrackId trackId) const;
    int trackColorIndex(TrackId trackId) const;
    float trackVolume(TrackId trackId) const;
    float trackPan(TrackId trackId) const;
    bool trackMute(TrackId trackId) const;
    bool trackSolo(TrackId trackId) const;
    int trackType(TrackId trackId) const; // 0 audio, 1 instrument
    int trackClipCount(TrackId trackId) const;
    ClipId clipIdAt(TrackId trackId, int index) const;
    double clipStart(ClipId clipId) const;
    double clipLength(ClipId clipId) const;
    double clipOffset(ClipId clipId) const;
    double clipLengthMax(ClipId clipId) const; // source duration - offset
    double clipFadeIn(ClipId clipId) const;
    double clipFadeOut(ClipId clipId) const;

    // Metering: read-and-reset peaks (float bits in an atomic; the audio
    // thread is the only writer). trackIndex is the position in the snapshot.
    float meterTrackPeak(int trackIndex);
    float meterMasterPeak(int channel);

    // ---- transport ----

    void play();
    void pause(); // keeps position
    void stop();  // rewinds to 0
    void seek(double seconds);

    bool isPlaying() const { return transport_->isPlaying(); }
    double positionSeconds() const { return transport_->positionSeconds(); }
    double durationSeconds() const;
    double sampleRate() const; // project rate when a project exists

    // ---- offline render ----

    // Renders from position 0 to a 16-bit stereo WAV. Does not touch the
    // live transport and does not need an audio device.
    juce::Result renderToFile(const juce::File& outFile, double maxSeconds);
    // Offline render at the project sample rate with 16 or 24 bits.
    juce::Result renderToFileEx(const juce::File& outFile, double maxSeconds,
                                int bitsPerSample);
    // Adds an audio file as a clip to the given track at the given position.
    juce::Result addClipFromPath(TrackId trackId, const juce::String& path,
                                 double startSeconds);
    // Returns the id of the first track (or selected track).

    std::shared_ptr<const Project> currentProject() const
    {
        return std::atomic_load(&project_);
    }

    // UTF-8 text of the most recent failure; stable until the next failure.
    const char* lastErrorUtf8() const { return lastErrorUtf8_.c_str(); }

private:
    class OutputCallback;   // single audio callback: input capture + output mix
    class MidiInCallback;   // routes the selected MIDI input into captures
    void finalizeRecording();
    // Pairs a capture's raw MIDI events into notes (shared by the live take
    // view and finalizeRecording). NOTE: the caller must not hold
    // capture.midiMutex. Starts are relative to the record start.
    std::vector<MidiNote> pairCapturedNotes(const TrackCapture& capture,
                                            double elapsed) const;
    void activateMidiInput(const juce::String& identifier);
    void onMidiMessage(const juce::MidiMessage& message);

    std::shared_ptr<const Project> makeEmptyProjectLocked(double sampleRate);
    void publish(std::shared_ptr<const Project> project,
                 bool deferHeavyPlugins = false);
    std::shared_ptr<Project> copyForEdit() const;
    double defaultRate() const;
    SourceId loadSourceInto(Project& project, const juce::File& file);
    void pushUndoLocked();
    // Runs mutate() on a snapshot copy; on success records one undo entry and
    // publishes. mutate() returns false after calling fail().
    juce::Result editProject(const std::function<bool(Project&)>& mutate);
    juce::Result fail(const juce::String& message) const;

    std::atomic<bool> audioReady_ {false};
    // Cancellation token for the deferred audio-device open. It is shared
    // with the message-loop lambda so the lambda can bail out safely after
    // the Engine is gone (Flutter may quit before the device opens).
    std::shared_ptr<std::atomic<bool>> initCancel_
        = std::make_shared<std::atomic<bool>>(false);
    juce::AudioDeviceManager deviceManager_;
    std::unique_ptr<ProjectAudioSource> transport_;
    PluginHost pluginHost_;
    juce::AudioFormatManager formatManager_;

    // Sound-library preview (see previewSound). previewReader_ owns the
    // reader source; both live behind previewLock_ because the audio thread
    // reads them while the message thread may swap the source in.
    juce::AudioTransportSource previewTransport_;
    std::unique_ptr<juce::AudioFormatReaderSource> previewReader_;
    juce::AudioBuffer<float> previewBuffer_;
    mutable juce::CriticalSection previewLock_;
    std::atomic<bool> previewActive_ {false};
    // Ends the preview when the source runs out (message-thread timer).
    class PreviewWatchdog;
    std::unique_ptr<PreviewWatchdog> previewWatchdog_;

    void preparePreview(int blockSize, double sampleRate);
    void releasePreview();
    void renderPreview(float* const* outputChannelData, int numOutputChannels,
                       int numFrames);
    std::shared_ptr<const Project> project_; // accessed with std::atomic_load/store
    juce::File projectFile_;                 // last save/load location
    juce::String selectedMidiInputId_;
    double midiInputLatencyMs_ = 0.0;        // persisted in device settings
    Clip clipboardClip_;                     // copy/paste (§7.4)
    bool clipboardValid_ = false;
    std::vector<std::shared_ptr<const Project>> undoStack_;
    std::vector<std::shared_ptr<const Project>> redoStack_;
    mutable std::string lastErrorUtf8_;
    mutable std::string stringOut_; // backing store for read-access strings
    mutable std::mutex editMutex_;
    std::shared_ptr<const CaptureList> captures_; // accessed with std::atomic_*
    // Open plugin editor windows (message thread only). Key: track<<16 | slot
    // (slot 0xFFFF = instrument slot). Values keep the processor alive too.
    // Plugin editor windows, keyed by (track<<16)|slot. The entry carries
    // the processor it was opened for: chain edits (remove/reorder) shift
    // slots, and a stale window would show the WRONG plugin's editor and
    // keep the removed instance alive.
    struct EditorEntry {
        std::shared_ptr<juce::DocumentWindow> window;
        std::shared_ptr<juce::AudioProcessor> proc;
    };
    std::map<std::uint64_t, EditorEntry> editorWindows_;
    // Windows removed while a plugin editor callback may still be on the
    // stack are hidden immediately but destroyed on the next message-loop
    // turn. Destroying them synchronously from openInstrumentEditor() crashed
    // Analog Lab V (VST3PluginWindow::componentVisibilityChanged).
    struct RetiredEditorList {
        std::vector<std::shared_ptr<juce::DocumentWindow>> windows;
    };
    std::shared_ptr<RetiredEditorList> retiredEditors_
        = std::make_shared<RetiredEditorList>();
    void retireEditorWindow(std::shared_ptr<juce::DocumentWindow> window);
    void closeOrphanedEditorWindows();
    void closeAllEditorWindows();
    std::atomic<bool> recording_ { false };
    std::atomic<double> recordStartSeconds_ { 0.0 };
    std::atomic<std::int64_t> recordStartSamples_ { 0 };
    // Live-take cache filled by recordingNoteCount (message thread only).
    mutable std::vector<MidiNote> liveTakeNotes_;
    mutable TrackId liveTakeTrack_ = -1;
    static std::atomic<bool> debugLoopback_;

    // AI generation worker (one job at a time).
    std::unique_ptr<ai::Service> aiService_;
    std::thread aiThread_;
    mutable std::mutex aiMutex_;
    std::atomic<int> aiState_ {kAiIdle};
    std::atomic<bool> aiCancel_ {false};
    AiGenerationResult aiResult_;
    mutable std::string aiStringOut_;
    void runAiGeneration(std::shared_ptr<const Project> project,
                         AiGenerationRequest request);

    std::unique_ptr<OutputCallback> outputCallback_;
    std::unique_ptr<MidiInCallback> midiInCallback_;
    std::atomic<bool> messageLoopRunning_ { false };
    void startMessageLoop();
    void stopMessageLoop();

    // ---- device health / automatic recovery ----
    class AudioHealthWatchdog;
    std::unique_ptr<AudioHealthWatchdog> audioHealthWatchdog_;
    std::atomic<bool> audioDeviceRunning_ { false };
    std::atomic<bool> intentionalDeviceStop_ { false };
    std::atomic<bool> audioRecoveryPending_ { false };
    std::atomic<bool> audioConfigDirty_ { false };
    std::atomic<bool> shuttingDown_ { false };
    std::atomic<std::uint64_t> audioCallbackCount_ { 0 };
    std::atomic<std::uint64_t> audioCallbackHeartbeat_ { 0 };
    std::atomic<int> audioRecoveryCount_ { 0 };
    std::atomic<int> audioXRunCount_ { 0 };
    int audioStallTicks_ = 0; // watchdog only (message thread)
    mutable std::mutex audioErrorMutex_;
    juce::String lastAudioError_;
    std::string lastAudioErrorUtf8_;

    void noteAudioCallback() noexcept
    {
        audioCallbackCount_.fetch_add(1, std::memory_order_relaxed);
    }
    void prepareTransportForDevice(juce::AudioIODevice* device);
    void handleAudioDeviceAboutToStart(juce::AudioIODevice* device);
    void handleAudioDeviceStopped(bool intentional);
    void handleAudioDeviceError(const juce::String& message);
    void scheduleAudioRecovery(const juce::String& reason);
    void recoverAudio();
    void reconcileAfterDeviceChange();
    void checkAudioHealth();
};

} // namespace cadence
