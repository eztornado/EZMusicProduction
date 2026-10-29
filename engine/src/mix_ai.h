// Mix intelligence: offline measurements, musical roles, mix targets, issue
// detection and the small vocabulary of moves the engine can apply.
//
// Everything here is pure signal analysis over sample buffers; the engine
// feeds it from an offline render, never from the audio thread. The result is
// both a human-readable report and a list of actionable moves for the mixer,
// the insert chain and the master bus.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>
#include <memory>
#include <vector>

namespace cadence::mixai {

constexpr float kFloorDb = -144.0f;

// ---------- roles ----------

enum class Role
{
    Kick,
    Drums,
    Perc,
    Bass,
    Keys,
    Guitar,
    Pad,
    Lead,
    Vocal,
    Strings,
    Brass,
    Fx,
    Other
};

const char* roleName(Role role);
Role inferRole(const juce::String& name, const juce::String& instrument,
               const juce::StringArray& hints = {});
float roleBalanceDb(Role role);
float roleHighPassHz(Role role, bool* hasValue = nullptr);
bool roleSpreads(Role role);
float roleOwnership(Role role, int octave);

// ---------- targets ----------

enum class ToneProfile
{
    Pop,
    Electronic,
    Hiphop,
    Rock,
    Acoustic,
    Orchestral
};

const char* profileName(ToneProfile profile);
ToneProfile profileFromGenre(const juce::String& text);
ToneProfile profileFromName(const juce::String& text);

struct ToneCurve
{
    float low = 7.0f;
    float lowMid = 3.0f;
    float highMid = -5.0f;
    float high = -8.0f;
};

ToneCurve profileCurve(ToneProfile profile);

struct MixTargets
{
    float lufs = -14.0f;
    float truePeakDb = -1.0f;
    ToneProfile profile = ToneProfile::Pop;
    juce::String source = "default";
    bool masterOnly = false;
    bool allowEq = true;
    bool allowDynamics = true;
    bool gentle = false;
    std::vector<std::pair<Role, float>> roles;

    float balanceDb(Role role) const;
};

// ---------- measurements ----------

struct BandEnergy
{
    float low = kFloorDb;
    float lowMid = kFloorDb;
    float mid = kFloorDb;
    float highMid = kFloorDb;
    float high = kFloorDb;
};

struct LevelStats
{
    float peakDb = kFloorDb;
    float truePeakDb = kFloorDb;
    float rmsDb = kFloorDb;
    float lufsIntegrated = kFloorDb;
    float lufsShortTermMax = kFloorDb;
    float crestFactorDb = 0.0f;
    std::uint64_t clippedSamples = 0;
    BandEnergy bandEnergy;
    float stereoCorrelation = 0.0f;
};

struct Spectrum
{
    static constexpr int kThirdOctaves = 31;
    static constexpr int kOctaves = 10;
    std::array<float, kThirdOctaves> thirdOctaveDb {};
    std::array<float, kThirdOctaves> sideThirdOctaveDb {};
    std::array<float, kOctaves> octaveDb {};
    std::array<float, kOctaves> octaveShape() const;
    int peakOctave() const;
};

struct Stereo
{
    float width = 0.0f;
    float lowCorrelation = 1.0f;
    float lowSideDb = -60.0f;
};

struct Timeline
{
    double hopSeconds = 0.0;
    std::vector<std::array<float, 10>> frames;
};

struct Detail
{
    LevelStats stats;
    Spectrum spectrum;
    Stereo stereo;
    Timeline timeline;
    bool isSilent() const;
};

class DetailAnalyzer
{
public:
    explicit DetailAnalyzer(double sampleRate);
    ~DetailAnalyzer();
    DetailAnalyzer(const DetailAnalyzer&) = delete;
    DetailAnalyzer& operator=(const DetailAnalyzer&) = delete;

    void push(const float* left, const float* right, int numSamples);
    Detail finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Detail analyzeBuffer(const juce::AudioBuffer<float>& buffer, double sampleRate);

// ---------- moves ----------

enum class EqKind
{
    Bell,
    LowShelf,
    HighShelf,
    HighPass,
    LowPass
};

const char* eqKindName(EqKind kind);
EqKind eqKindFromName(const juce::String& name);
juce::String hzLabel(float hz);
juce::String panLabel(float pan);

struct Move
{
    enum class Kind
    {
        Gain,
        Pan,
        Eq,
        Compressor,
        Limiter
    };

    Kind kind = Kind::Gain;
    int trackId = -1; // -1 = master

    // Gain: deltaDb or absolute db.
    bool hasDelta = false;
    bool hasDb = false;
    float deltaDb = 0.0f;
    float db = 0.0f;

    // Pan.
    float pan = 0.0f;

    // EQ.
    EqKind eqKind = EqKind::Bell;
    float freqHz = 0.0f;
    float gainDb = 0.0f;
    float q = 1.0f;

    // Compressor.
    bool hasThreshold = false;
    bool hasRatio = false;
    bool hasAttack = false;
    bool hasRelease = false;
    bool hasMakeup = false;
    bool hasKnee = false;
    float thresholdDb = 0.0f;
    float ratio = 1.0f;
    float attackMs = 0.0f;
    float releaseMs = 0.0f;
    float makeupDb = 0.0f;
    float kneeDb = 0.0f;

    // Limiter.
    bool hasCeiling = false;
    bool hasInputGain = false;
    float ceilingDb = 0.0f;
    float inputGainDb = 0.0f;

    juce::String describe(const std::function<juce::String(int)>& name) const;
};

struct LimiterState
{
    bool present = false;
    float inputGainDb = 0.0f;
    float ceilingDb = -1.0f;
};

// ---------- issues ----------

enum class Severity
{
    High,
    Medium,
    Low
};

const char* severityName(Severity severity);

enum class IssueKind
{
    Clipping,
    TruePeak,
    Loudness,
    Phase,
    NarrowImage,
    WideLowEnd,
    ToneBalance,
    OverCompressed,
    TrackHot,
    Balance,
    Rumble,
    Masking
};

const char* issueKindName(IssueKind kind);

struct Issue
{
    juce::String id;
    Severity severity = Severity::Low;
    IssueKind kind = IssueKind::Clipping;
    int trackId = -1;
    juce::String message;
    std::vector<Move> moves;
};

struct MaskingPair
{
    int a = 0;
    int b = 0;
    float bandHz = 0.0f;
    float overlap = 0.0f;
    float levelDiffDb = 0.0f;
    float score = 0.0f;
    int cut = 0;
};

struct TrackInput
{
    int trackId = -1;
    juce::String name;
    Role role = Role::Other;
    const Detail* detail = nullptr;
    float pan = 0.0f;
    float highPassHz = 0.0f;
    bool isBus = false;
};

struct MasterInput
{
    const Detail* detail = nullptr;
    LimiterState limiter;
};

std::vector<MaskingPair> findMasking(const std::vector<TrackInput>& tracks);
std::vector<Issue> findIssues(const MasterInput& master,
                              const std::vector<TrackInput>& tracks,
                              const MixTargets& targets,
                              const std::vector<MaskingPair>& masking);

// ---------- request / report ----------

struct Request
{
    juce::String prompt;
    float targetLufs = -14.0f;
    float truePeakDb = -1.0f;
    int profile = -1; // -1 = infer from prompt, else ToneProfile
    bool masterOnly = false;
    int maxIssues = 12;
};

struct ChannelReport
{
    int trackId = -1;
    juce::String name;
    Role role = Role::Other;
    float faderDb = 0.0f;
    float pan = 0.0f;
    bool highPassActive = false;
    Detail detail;
};

struct PlanStage
{
    juce::String id;
    juce::String label;
    std::vector<Move> moves;
};

struct Report
{
    bool ok = false;
    juce::String error;
    juce::String promptNote;
    MixTargets targets;
    Detail master;
    std::vector<ChannelReport> channels;
    std::vector<MaskingPair> masking;
    std::vector<Issue> issues;
    std::vector<PlanStage> plan;
    double durationSeconds = 0.0;
    std::vector<juce::String> warnings;
};

// Parses the free-text request into targets. `note` receives a short summary
// of what was understood.
MixTargets interpretRequest(const juce::String& prompt, MixTargets targets,
                            juce::String& note);

// JSON for the UI and for the C ABI (one getter instead of dozens).
juce::var reportToVar(const Report& report);
juce::var moveToVar(const Move& move);
juce::var issueToVar(const Issue& issue);
bool moveFromVar(const juce::var& value, Move& out, juce::String& error);
bool movesFromVar(const juce::var& value, std::vector<Move>& out, juce::String& error);

// Splits the fixes of a report into ordered pass groups.
std::vector<PlanStage> buildPlan(const std::vector<Issue>& issues);

// ---------- offline render sink ----------

// The engine's offline render source pushes one block per track and the
// master into this sink. It is only installed during an analysis render.
class AnalysisSink
{
public:
    virtual ~AnalysisSink() = default;
    virtual void pushTrack(int trackIndex, const float* left, const float* right,
                           int numSamples, double sampleRate) = 0;
    virtual void pushMaster(const float* left, const float* right,
                            int numSamples, double sampleRate) = 0;
};

} // namespace cadence::mixai
