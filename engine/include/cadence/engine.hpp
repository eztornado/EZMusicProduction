#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cadence/model.hpp>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cadence {

// Plugin scanner/instantiator: VST3 + AudioUnit on macOS (VST2 requires the
// Steinberg headers; add JUCE_VST2_SDK_PATH to enable that format).
class PluginHost
{
public:
    void setup();   // add formats; call once per process
    void scan();    // scan all format locations into the known list
    int pluginCount() const { return known_.getNumTypes(); }
    juce::PluginDescription* descriptionAt(int index)
    {
        return known_.getType(index);
    }
    std::unique_ptr<juce::AudioPluginInstance> create(const juce::String& uid,
                                                      double sampleRate,
                                                      int blockSize);

private:
    juce::AudioPluginFormatManager formatManager_;
    juce::KnownPluginList known_;
    juce::File deadMansPedalFile_ =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Cadence/dead_mans_pedal.txt");
};

// Immutable fx chain snapshot: swapped wholesale under the owning node's
// mutex, so the audio thread renders from a copy without races.
struct FxChain {
    std::vector<std::shared_ptr<juce::AudioProcessor>> processors;
    std::vector<bool> bypass;
    juce::StringArray uids; // change detection
    std::vector<bool> valid; // instantiated successfully
};

// Runtime audio nodes of one track (fx chain + instrument plugin). Built on
// the message thread, swapped into the audio path atomically.
struct TrackAudioNode {
    juce::AudioBuffer<float> chainBuffer;
    std::shared_ptr<const FxChain> fx;
    std::mutex fxMutex;
    std::vector<double> preparedRates; // per chain processor
    std::shared_ptr<juce::AudioProcessor> instrument;
    std::mutex instrumentMutex;
    bool instrumentBypass = false;
    bool instrumentPrepared = false;
};

// Renders project snapshots: every block it picks up the current immutable
// Project, mixes the audible tracks/clips and advances the transport. The
// Engine owns one live instance driven by the audio device; extra instances
// can be created for offline pulls (Engine::renderToFile).
class ProjectAudioSource : public juce::AudioSource {
public:
    // Replaces the project being rendered; the transport position is kept.
    void setProject(std::shared_ptr<const Project> project);
    // Rebuilds fx/instrument plugin nodes that changed (message thread).
    void reconcileNodes(const Project& project, PluginHost& host);
    void setPluginHost(PluginHost* host) { pluginHost_ = host; }

    void play();
    void pause();
    void stop();
    void seek(double seconds);

    bool isPlaying() const { return playing_.load(); }
    double positionSeconds() const;
    std::int64_t positionSamples() const;
    void setRecording(bool r) { recording_.store(r); }
    void setMetronome(bool on) { metronome_.store(on); }
    void allNotesOff();

    // Live MIDI monitoring: notes arriving from the selected MIDI input are
    // played by armed instrument tracks (even with the transport stopped).
    void pushExternalMidi(const juce::MidiMessage& m);

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
    juce::AudioBuffer<float> trackScratch_;
    juce::MidiBuffer blockMidi_;
    juce::SpinLock synthsLock_;
    std::map<TrackId, std::unique_ptr<juce::Synthesiser>> synths_;
    double synthsRate_ = 0.0;
    juce::AudioBuffer<float> masterBuffer_;
    std::atomic<std::uint32_t> masterPeaks_[2] {{0}, {0}};
    std::atomic<std::uint32_t> trackPeaks_[kMeterTracks] {};

    // Live MIDI monitoring (external input → armed instrument tracks).
    std::mutex externalMutex_;
    std::vector<juce::MidiMessage> externalQueue_;
    std::vector<juce::MidiMessage> pendingExternal_;

    std::mutex trackNodesMutex_;
    std::map<TrackId, std::unique_ptr<TrackAudioNode>> trackNodes_;
    PluginHost* pluginHost_ = nullptr;

    juce::Synthesiser* getOrCreateSynth(TrackId trackId, double rate);
    TrackAudioNode* nodeFor(TrackId trackId);
    void processFxChain(TrackAudioNode& node, TrackId trackId, double rate,
                        int numSamples);
    void drainExternalMidi();
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
    std::mutex midiMutex;
    std::atomic<int> played { 0 };
    int loopbackBoundary = 0; // audio thread only (loopback synth)
};
using CaptureList = std::vector<std::shared_ptr<TrackCapture>>;

// Phase 2 engine: owns the project document (published as immutable
// snapshots), the audio device and the project transport. Phase 1's
// single-file "tape" playback survives as a convenience (loadFile builds a
// one-track project).
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
    bool isInitialised() const;

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

    juce::Result setTrackName(TrackId trackId, const juce::String& name);
    juce::Result setTrackColor(TrackId trackId, int colorIndex); // 0..15

    juce::Result setTrackVolume(TrackId trackId, float linearGain); // 0..2
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

    juce::Result addMasterFx(int pluginIndex);
    juce::Result removeMasterFx(int slotIndex);
    juce::Result setMasterFxBypass(int slotIndex, bool bypass);
    int masterFxCount() const;
    const char* masterFxNameAt(int slotIndex) const;
    bool masterFxBypassAt(int slotIndex) const;

    // Recording setup: arm + input are not undoable and not part of history.
    juce::Result setTrackArmed(TrackId trackId, bool armed);
    juce::Result setTrackInputChannel(TrackId trackId, int channel);
    bool trackArmed(TrackId trackId) const;
    int trackInputChannel(TrackId trackId) const;

    void setMetronome(bool on);

    // Recording: startRecord captures every armed track; stopRecord (or
    // stop/pause) finalizes the takes into clips as ONE undo entry. Takes
    // are written to disk under the project's Recordings folder.
    juce::Result startRecord();
    juce::Result stopRecord();
    bool isRecording() const { return recording_.load(); }
    double recordStartSeconds() const { return recordStartSeconds_.load(); }
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
    void activateMidiInput(const juce::String& identifier);
    void onMidiMessage(const juce::MidiMessage& message);

    std::shared_ptr<const Project> makeEmptyProjectLocked(double sampleRate);
    void publish(std::shared_ptr<const Project> project);
    std::shared_ptr<Project> copyForEdit() const;
    double defaultRate() const;
    SourceId loadSourceInto(Project& project, const juce::File& file);
    void pushUndoLocked();
    // Runs mutate() on a snapshot copy; on success records one undo entry and
    // publishes. mutate() returns false after calling fail().
    juce::Result editProject(const std::function<bool(Project&)>& mutate);
    juce::Result fail(const juce::String& message) const;

    juce::AudioDeviceManager deviceManager_;
    std::unique_ptr<ProjectAudioSource> transport_;
    PluginHost pluginHost_;
    juce::AudioFormatManager formatManager_;
    std::shared_ptr<const Project> project_; // accessed with std::atomic_load/store
    juce::File projectFile_;                 // last save/load location
    juce::String selectedMidiInputId_;
    std::vector<std::shared_ptr<const Project>> undoStack_;
    std::vector<std::shared_ptr<const Project>> redoStack_;
    mutable std::string lastErrorUtf8_;
    mutable std::string stringOut_; // backing store for read-access strings
    mutable std::mutex editMutex_;
    std::mutex masterFxMutex_;
    std::shared_ptr<const FxChain> masterFxChain_;
    std::shared_ptr<const CaptureList> captures_; // accessed with std::atomic_*
    std::atomic<bool> recording_ { false };
    std::atomic<double> recordStartSeconds_ { 0.0 };
    std::atomic<std::int64_t> recordStartSamples_ { 0 };
    static std::atomic<bool> debugLoopback_;

    std::unique_ptr<OutputCallback> outputCallback_;
    std::unique_ptr<MidiInCallback> midiInCallback_;
    std::thread messageThread_;
    std::atomic<bool> messageLoopRunning_ { false };
    void startMessageLoop();
    void stopMessageLoop();
};

} // namespace cadence
