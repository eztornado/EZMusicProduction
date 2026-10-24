#include "ai_generation.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <sstream>

namespace cadence::ai
{
namespace
{
constexpr int kStepsPerBar = 16;
constexpr int kStepsPerQuarter = 4;

struct AbsNote
{
    int pitch = 60;
    double start = 0.0;
    double end = 0.0;
    float velocity = 0.8f;
    bool drum = false;
    TrackId track = -1;
    juce::String trackName;
    juce::String instrument;
};

struct PitchRange
{
    int low = 48;
    int high = 84;
};

// Krumhansl-Kessler key profiles, shared by key detection and the
// chord-aware refinement below.
constexpr double kMajorProfile[12] = {
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
    2.52, 5.19, 2.39, 3.66, 2.29, 2.88
};
constexpr double kMinorProfile[12] = {
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
    2.54, 4.75, 3.98, 2.69, 3.34, 3.17
};
} // namespace

// The analysis and prompt-interpretation state are shared by the helpers
// below, so they are defined before the anonymous namespace that uses them.
struct Service::Analysis
{
    double anchor = 0.0;
    double barSeconds = 2.0;
    double beatSeconds = 0.5;
    int bars = 4;
    double lengthSeconds = 8.0;   // exact requested clip length
    int root = 0;
    bool minor = false;
    float confidence = 0.0f;
    std::vector<int> chordRoot;
    std::vector<int> chordQuality;
    std::vector<AbsNote> context; // 4 bars before the anchor
    std::vector<AbsNote> window;  // the clip being generated
    std::vector<AbsNote> target;  // target track, context + window
    float density = 2.0f;
    int medianPitch = 60;
    Mode role = Mode::Melody;
    juce::String targetName;
    juce::String instrument;

    // "Listening": audio clips overlapping the context + generation window are
    // mixed down and analysed for chroma/key, bar chords, brightness and onset
    // density. MIDI still wins when there is enough of it.
    bool audioUsed = false;
    bool audioKeyUsed = false;
    double audioSeconds = 0.0;
    float audioConfidence = 0.0f;
    int audioRoot = 0;
    bool audioMinor = false;
    double audioOnsetsPerBeat = 0.0;

    void useDefaultProgression()
    {
        chordRoot.assign((std::size_t) bars, root);
        chordQuality.assign((std::size_t) bars, minor ? 1 : 0);

        static const int majorRoots[4] = { 0, 7, 9, 5 };
        static const int majorQuals[4] = { 0, 0, 1, 0 };
        static const int minorRoots[4] = { 0, 8, 3, 10 };
        static const int minorQuals[4] = { 1, 0, 0, 0 };

        for (int b = 0; b < bars; ++b)
        {
            const int i = b % 4;
            chordRoot[(std::size_t) b] = (root + (minor ? minorRoots[i] : majorRoots[i])) % 12;
            chordQuality[(std::size_t) b] = minor ? minorQuals[i] : majorQuals[i];
        }
    }
};

struct Service::Intent
{
    bool keySpecified = false;
    int root = 0;
    bool minor = false;
    bool forceMinor = false;
    bool forceMajor = false;
    bool hasMode = false;
    Mode mode = Mode::Melody;
    float densityBias = 0.0f;
    int octaveShift = 0;
    bool harmonize = false;
    bool pad = false;
    bool arpeggio = false;
    bool swing = false;
    bool humanize = false;
    float tempBias = 0.0f;
    juce::String note;
};

namespace
{
juce::String noteName(int pc, bool flats)
{
    static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F",
                                    "F#", "G", "G#", "A", "A#", "B" };
    static const char* flatNames[] = { "C", "Db", "D", "Eb", "E", "F",
                                       "Gb", "G", "Ab", "A", "Bb", "B" };
    const int i = ((pc % 12) + 12) % 12;
    return flats ? flatNames[i] : sharps[i];
}

// Pick the spelling that matches the key signature. C minor, for example,
// should read "Ab / Eb / Bb" rather than "G# / D# / A#".
bool keyUsesFlats(int root, bool minor)
{
    static const int flatMajors[] = { 5, 10, 3, 8, 1, 6, 11 };  // F Bb Eb Ab Db Gb Cb
    static const int flatMinors[] = { 2, 7, 0, 5, 10, 3, 8 };   // D G C F Bb Eb Ab
    const int r = ((root % 12) + 12) % 12;
    const int* table = minor ? flatMinors : flatMajors;
    for (int i = 0; i < 7; ++i)
        if (table[i] == r)
            return true;
    return false;
}

juce::String keyName(int root, bool minor)
{
    return noteName(root, keyUsesFlats(root, minor))
         + (minor ? " minor" : " major");
}

juce::String chordName(int root, int quality, bool flats)
{
    const auto suffix = quality == 1 ? "m" : (quality == 2 ? "dim" : "");
    return noteName(root, flats) + suffix;
}

bool isBlackKey(int pc)
{
    const int p = ((pc % 12) + 12) % 12;
    return p == 1 || p == 3 || p == 6 || p == 8 || p == 10;
}

std::vector<int> scaleIntervals(bool minor, bool harmonic)
{
    if (! minor)
        return { 0, 2, 4, 5, 7, 9, 11 };
    if (harmonic)
        return { 0, 2, 3, 5, 7, 8, 11 };
    return { 0, 2, 3, 5, 7, 8, 10 };
}

bool inScale(int pitch, int root, const std::vector<int>& intervals)
{
    const int pc = ((pitch - root) % 12 + 12) % 12;
    return std::find(intervals.begin(), intervals.end(), pc) != intervals.end();
}

int snapToScale(int pitch, int root, const std::vector<int>& intervals)
{
    if (inScale(pitch, root, intervals))
        return pitch;

    for (int d = 1; d <= 3; ++d)
    {
        if (inScale(pitch - d, root, intervals)) return pitch - d;
        if (inScale(pitch + d, root, intervals)) return pitch + d;
    }
    return pitch;
}

int chordTone(const std::vector<int>& tones, int nearPitch, int fallback)
{
    int best = fallback;
    int bestDist = std::numeric_limits<int>::max();
    for (int base : tones)
    {
        for (int oct = -3; oct <= 3; ++oct)
        {
            const int p = base + 12 * oct;
            const int d = std::abs(p - nearPitch);
            if (d < bestDist)
            {
                bestDist = d;
                best = p;
            }
        }
    }
    return best;
}

std::array<double, 12> pitchHistogram(const std::vector<AbsNote>& notes,
                                      double maxWeight,
                                      double barSeconds = 0.0,
                                      bool bassWeight = false)
{
    std::array<double, 12> hist {};
    for (const auto& n : notes)
    {
        if (n.drum)
            continue;
        double w = juce::jlimit(0.05, maxWeight, n.end - n.start);
        if (barSeconds > 0.0)
        {
            // Downbeats carry the harmonic identity; low notes carry the
            // root. Without this, a close-position triad can be mistaken for
            // its relative key.
            const double phase = std::fmod(std::max(0.0, n.start), barSeconds);
            if (phase < 0.06 || phase > barSeconds - 0.06)
                w *= 1.35;
            if (bassWeight && n.pitch < 55)
                w *= 1.4;
        }
        hist[(std::size_t) (((n.pitch % 12) + 12) % 12)] += w;
    }
    return hist;
}

void estimateKeyFromHistogram(const std::array<double, 12>& hist,
                              int& root, bool& minor, float& confidence,
                              float reliability = 1.0f)
{
    root = 0;
    minor = false;
    confidence = 0.0f;

    const double total = std::accumulate(hist.begin(), hist.end(), 0.0);
    if (total <= 1.0e-9)
        return;

    // Correlating against all 24 keys is a tiny problem and works well for
    // the handful of notes a clip provides.
    double best = -1.0e30;
    double second = -1.0e30;
    int bestRoot = 0;
    bool bestMinor = false;
    for (int r = 0; r < 12; ++r)
    {
        for (int m = 0; m < 2; ++m)
        {
            const auto& profile = m == 0 ? kMajorProfile : kMinorProfile;
            double score = 0.0;
            for (int i = 0; i < 12; ++i)
                score += hist[(std::size_t) ((r + i) % 12)] * profile[i];
            score /= total;
            if (score > best)
            {
                second = best;
                best = score;
                bestRoot = r;
                bestMinor = m == 1;
            }
            else if (score > second)
            {
                second = score;
            }
        }
    }

    root = bestRoot;
    minor = bestMinor;
    confidence = (float) juce::jlimit(
        0.0, 1.0, (best - juce::jmax(0.0, second)) * 10.0 * reliability);
}

void estimateKey(const std::vector<AbsNote>& notes, double barSeconds,
                 int& root, bool& minor, float& confidence)
{
    root = 0;
    minor = false;
    confidence = 0.0f;

    const auto hist = pitchHistogram(notes, juce::jmax(0.5, 2.0 * barSeconds),
                                     barSeconds, true);
    estimateKeyFromHistogram(hist, root, minor, confidence,
                             notes.size() < 4 ? 0.5f : 1.0f);
}

// Estimate one bar's triad. Chord-tone energy dominates, the lowest note
// (the bass) gets a strong vote, and non-chord tones are penalised. Without
// the bass and penalty terms a close-position Ab major triad also scores
// well as Eb minor, which is how naive estimators drift a fifth away.
bool estimateChord(const std::vector<AbsNote>& notes, double barSeconds,
                   int& root, int& quality)
{
    if (notes.empty())
        return false;

    const auto hist = pitchHistogram(notes, juce::jmax(0.5, barSeconds), barSeconds);
    const double total = std::accumulate(hist.begin(), hist.end(), 0.0);
    if (total <= 1.0e-9)
        return false;

    int bassPitch = 128;
    for (const auto& n : notes)
        if (! n.drum)
            bassPitch = juce::jmin(bassPitch, n.pitch);
    if (bassPitch == 128)
        return false;
    const int bassPc = ((bassPitch % 12) + 12) % 12;

    double bestScore = -1.0e9;
    int bestRoot = 0;
    int bestQuality = 0;
    for (int r = 0; r < 12; ++r)
    {
        for (int q = 0; q < 3; ++q)
        {
            const int third = q == 2 ? 3 : (q == 1 ? 3 : 4);
            const int fifth = q == 2 ? 6 : 7;
            const std::array<int, 3> pcs { r, (r + third) % 12, (r + fifth) % 12 };

            double toneEnergy = 0.0;
            bool bassIsTone = false;
            for (int pc : pcs)
            {
                toneEnergy += hist[(std::size_t) pc];
                if (pc == bassPc)
                    bassIsTone = true;
            }
            const double nonTone = juce::jmax(0.0, total - toneEnergy);
            const double bassBonus = bassPc == r ? 0.85 : (bassIsTone ? 0.28 : 0.0);
            const double rootBonus = hist[(std::size_t) r] / total * 0.20;
            const double score = toneEnergy / total + bassBonus
                               - nonTone / total * 0.38 + rootBonus;
            if (score > bestScore)
            {
                bestScore = score;
                bestRoot = r;
                bestQuality = q;
            }
        }
    }

    if (bestScore < 0.52)
        return false;
    root = bestRoot;
    quality = bestQuality;
    return true;
}

// Audio version of the same idea. There is no reliable bass pitch after a
// stereo mixdown, so the root and fifth get a small bonus instead of the
// strong lowest-note vote used for MIDI.
bool estimateChordFromChroma(const std::array<double, 12>& chroma,
                             int bassPc, int& root, int& quality)
{
    const double total = std::accumulate(chroma.begin(), chroma.end(), 0.0);
    if (total <= 1.0e-9)
        return false;

    double bestScore = -1.0e9;
    int bestRoot = 0;
    int bestQuality = 0;
    for (int r = 0; r < 12; ++r)
    {
        for (int q = 0; q < 3; ++q)
        {
            const int third = q == 2 ? 3 : (q == 1 ? 3 : 4);
            const int fifth = q == 2 ? 6 : 7;
            const std::array<int, 3> pcs { r, (r + third) % 12, (r + fifth) % 12 };

            double toneEnergy = 0.0;
            bool bassIsTone = false;
            for (int pc : pcs)
            {
                toneEnergy += chroma[(std::size_t) pc];
                if (pc == bassPc)
                    bassIsTone = true;
            }
            const double nonTone = juce::jmax(0.0, total - toneEnergy);
            const double rootBonus = chroma[(std::size_t) r] / total * 0.20;
            const double fifthBonus = chroma[(std::size_t) ((r + fifth) % 12)]
                                    / total * 0.08;
            const double bassBonus = bassPc < 0 ? 0.0
                                   : (bassPc == r ? 0.55
                                   : (bassIsTone ? 0.20 : 0.0));
            const double score = toneEnergy / total + rootBonus + fifthBonus
                               + bassBonus - nonTone / total * 0.32;
            if (score > bestScore)
            {
                bestScore = score;
                bestRoot = r;
                bestQuality = q;
            }
        }
    }

    if (bestScore < 0.52)
        return false;
    root = bestRoot;
    quality = bestQuality;
    return true;
}

// The pitch-class profile cannot tell a key from its relative major/minor
// (C minor and Eb major share almost all material). Once we have bar chords,
// score each key by how many of them are diatonic, prefer a tonic chord in
// the first bar, and combine that with the Krumhansl-Kessler correlation.
void refineKeyWithChords(const std::vector<AbsNote>& notes,
                         double barSeconds,
                         const std::vector<int>& chordRoots,
                         const std::vector<int>& chordQualities,
                         const std::vector<char>& estimated,
                         int& root, bool& minor, float& confidence)
{
    int fallbackRoot = 0;
    bool fallbackMinor = false;
    float fallbackConfidence = 0.0f;
    estimateKey(notes, barSeconds, fallbackRoot, fallbackMinor,
                fallbackConfidence);

    const auto hist = pitchHistogram(notes, juce::jmax(0.5, 2.0 * barSeconds),
                                     barSeconds, true);
    const double total = std::accumulate(hist.begin(), hist.end(), 0.0);
    if (total <= 1.0e-9)
    {
        root = fallbackRoot;
        minor = fallbackMinor;
        confidence = fallbackConfidence;
        return;
    }

    static const int majorDegrees[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int majorQualities[7] = { 0, 1, 1, 0, 0, 1, 2 };
    static const int minorDegrees[7] = { 0, 2, 3, 5, 7, 8, 10 };
    static const int minorQualities[7] = { 1, 2, 0, 1, 1, 0, 0 };

    int validCount = 0;
    for (std::size_t i = 0; i < estimated.size(); ++i)
        if (estimated[i] != 0)
            ++validCount;

    struct Candidate { double kk; double fit; int root; bool minor; };
    std::array<Candidate, 24> candidates {};
    int index = 0;
    double kkMin = 1.0e30;
    double kkMax = -1.0e30;

    for (int r = 0; r < 12; ++r)
    {
        for (int m = 0; m < 2; ++m)
        {
            const auto& profile = m == 0 ? kMajorProfile : kMinorProfile;
            double kk = 0.0;
            for (int i = 0; i < 12; ++i)
                kk += hist[(std::size_t) ((r + i) % 12)] * profile[i];
            kk /= total;

            double fit = 0.0;
            if (validCount > 0)
            {
                const int* degrees = m == 0 ? majorDegrees : minorDegrees;
                const int* qualities = m == 0 ? majorQualities : minorQualities;
                for (std::size_t bar = 0; bar < estimated.size(); ++bar)
                {
                    if (estimated[bar] == 0)
                        continue;
                    const int interval = ((chordRoots[bar] - r) % 12 + 12) % 12;
                    bool matched = false;
                    for (int d = 0; d < 7; ++d)
                    {
                        if (degrees[d] != interval)
                            continue;
                        fit += qualities[d] == chordQualities[bar] ? 1.0 : 0.42;
                        if (interval == 0)
                            fit += 0.12;
                        matched = true;
                        break;
                    }
                    if (! matched)
                        fit += 0.04;
                }
                fit /= (double) validCount;
            }
            if (estimated.size() > 0 && estimated[0] != 0)
            {
                const int firstInterval = ((chordRoots[0] - r) % 12 + 12) % 12;
                if (firstInterval == 0)
                    fit += 0.18;
            }

            candidates[(std::size_t) index++] = { kk, fit, r, m == 1 };
            kkMin = juce::jmin(kkMin, kk);
            kkMax = juce::jmax(kkMax, kk);
        }
    }

    const double span = juce::jmax(1.0e-9, kkMax - kkMin);
    double best = -1.0e30;
    double second = -1.0e30;
    int bestIndex = 0;
    for (int i = 0; i < 24; ++i)
    {
        const double kkNorm = (candidates[(std::size_t) i].kk - kkMin) / span;
        const double combined = kkNorm * 0.62 + candidates[(std::size_t) i].fit * 0.85;
        if (combined > best)
        {
            second = best;
            best = combined;
            bestIndex = i;
        }
        else if (combined > second)
        {
            second = combined;
        }
    }

    root = candidates[(std::size_t) bestIndex].root;
    minor = candidates[(std::size_t) bestIndex].minor;
    confidence = (float) juce::jlimit(0.0, 1.0, (best - juce::jmax(0.0, second)) * 4.0);
    if (notes.size() < 4)
        confidence *= 0.5f;
}

double medianPitch(const std::vector<AbsNote>& notes)
{
    std::vector<int> pitches;
    for (const auto& n : notes)
        if (! n.drum)
            pitches.push_back(n.pitch);
    if (pitches.empty())
        return 60.0;
    std::sort(pitches.begin(), pitches.end());
    return (double) pitches[pitches.size() / 2];
}

Mode roleForTrack(const juce::String& name, const juce::String& instrument,
                  const std::vector<AbsNote>& notes)
{
    const auto s = (name + " " + instrument).toLowerCase();
    if (s.contains("bass") || s.contains("bajo") || s.contains("sub"))
        return Mode::Bass;
    if (s.contains("drum") || s.contains("bater") || s.contains("perc")
        || s.contains("kit"))
        return Mode::Drums;
    if (s.contains("pad") || s.contains("chord") || s.contains("armon")
        || s.contains("keys") || s.contains("piano") || s.contains("organ"))
        return Mode::Harmony;

    if (! notes.empty())
    {
        int drums = 0;
        std::set<int> atSameStart;
        int lastStart = -1;
        int polyphonicStarts = 0;
        for (const auto& n : notes)
        {
            if (n.drum) ++drums;
            const int startKey = (int) std::lround(n.start * 1000.0);
            if (startKey == lastStart)
                ++polyphonicStarts;
            lastStart = startKey;
        }
        if (drums * 2 > (int) notes.size())
            return Mode::Drums;
        if (polyphonicStarts > 2)
            return Mode::Harmony;
        if (medianPitch(notes) < 48.0)
            return Mode::Bass;
    }
    return Mode::Melody;
}

PitchRange rangeFor(Mode mode, double median, int octaveShift)
{
    PitchRange r;
    if (mode == Mode::Bass)
        r = { 28, 55 };
    else if (mode == Mode::Drums)
        r = { 35, 60 };
    else if (mode == Mode::Harmony)
        r = { 48, 84 };
    else
    {
        const int center = (int) juce::jlimit(36.0, 96.0, median);
        r = { center - 10, center + 10 };
        if (median <= 0.0)
            r = { 48, 84 };
    }
    r.low += octaveShift;
    r.high += octaveShift;
    return r;
}

template <typename NoteT>
void fitRegister(std::vector<NoteT>& notes, const PitchRange& range)
{
    std::vector<int> pitches;
    for (const auto& n : notes)
        if (! n.isDrum)
            pitches.push_back(n.pitch);
    if (pitches.empty())
        return;
    std::sort(pitches.begin(), pitches.end());
    const int median = pitches[pitches.size() / 2];

    int shift = 0;
    while (median + shift < range.low && median + shift + 12 <= range.high)
        shift += 12;
    while (median + shift > range.high && median + shift - 12 >= range.low)
        shift -= 12;

    for (auto& n : notes)
    {
        if (n.isDrum) continue;
        n.pitch = juce::jlimit(0, 127, n.pitch + shift);
    }
}

// Long model output drifts harmonically: a contour that fits bar 1 may be a
// semitone away from the chord in bar 9. Pull beat-level notes to the nearest
// chord tone when it is close, keeping the contour and the passing tones.
void anchorStrongBeatsToChords(std::vector<GeneratedNote>& notes,
                               const Service::Analysis& a, const Request& req,
                               std::mt19937& rng, double stepSeconds);

int chordRootAt(const Service::Analysis& a, int bar)
{
    if (a.chordRoot.empty())
        return a.root;
    return a.chordRoot[(std::size_t) (bar % (int) a.chordRoot.size())];
}

int chordQualityAt(const Service::Analysis& a, int bar)
{
    if (a.chordQuality.empty())
        return a.minor ? 1 : 0;
    return a.chordQuality[(std::size_t) (bar % (int) a.chordQuality.size())];
}

std::vector<int> chordTonePcs(int root, int quality)
{
    const int third = quality == 1 ? 3 : (quality == 2 ? 3 : 4);
    const int fifth = quality == 2 ? 6 : 7;
    return { root, root + third, root + fifth };
}

void anchorStrongBeatsToChords(std::vector<GeneratedNote>& notes,
                               const Service::Analysis& a, const Request& req,
                               std::mt19937& rng, double stepSeconds)
{
    if (req.mode == Mode::Drums)
        return;

    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    const float probability = req.mode == Mode::Bass ? 0.9f
                           : (req.mode == Mode::Harmony ? 0.35f : 0.6f);
    const int maxDistance = req.mode == Mode::Bass ? 5 : 3;

    for (auto& n : notes)
    {
        if (n.isDrum)
            continue;
        const int step = (int) std::lround(n.startSeconds / stepSeconds);
        if (step % 4 != 0)
            continue;
        if (dist(rng) > probability)
            continue;

        const int bar = juce::jlimit(
            0, juce::jmax(1, (int) a.chordRoot.size()) - 1,
            (int) (n.startSeconds / a.barSeconds));
        const auto pcs = chordTonePcs(chordRootAt(a, bar),
                                      chordQualityAt(a, bar));
        const int snapped = chordTone(pcs, n.pitch, n.pitch);
        if (snapped != n.pitch && std::abs(snapped - n.pitch) <= maxDistance)
            n.pitch = juce::jlimit(0, 127, snapped);
    }
}

double densityTarget(const Service::Analysis& a, const Service::Intent& intent)
{
    double d = a.density;
    d *= (1.0 + intent.densityBias);
    return juce::jlimit(0.5, 8.0, d);
}

double candidateScore(const Result& r, const Service::Analysis& a,
                      const Request& req, const Service::Intent& intent)
{
    if (r.notes.empty())
        return -1.0e9;

    const int totalSteps = juce::jmax(1, req.bars * kStepsPerBar);
    const double beatsPerBar = a.barSeconds / juce::jmax(1.0e-6, a.beatSeconds);
    const double totalBeats = juce::jmax(1.0, a.bars * beatsPerBar);
    const bool keyMinor = intent.keySpecified ? intent.minor : a.minor;
    const auto intervals = scaleIntervals(keyMinor, keyMinor && intent.forceMinor);
    const int root = intent.keySpecified ? intent.root : a.root;
    const auto targetRange = rangeFor(req.mode, a.medianPitch, intent.octaveShift);

    int melodic = 0;
    int inKey = 0;
    int inRange = 0;
    int leaps = 0;
    int lastPitch = -1;
    for (const auto& n : r.notes)
    {
        if (n.isDrum) continue;
        ++melodic;
        if (inScale(n.pitch, root, intervals)) ++inKey;
        if (n.pitch >= targetRange.low && n.pitch <= targetRange.high) ++inRange;
        if (lastPitch >= 0)
            leaps += std::abs(n.pitch - lastPitch);
        lastPitch = n.pitch;
    }

    const double keyScore = melodic > 0 ? (double) inKey / melodic : 1.0;
    const double rangeScore = melodic > 0 ? (double) inRange / melodic : 1.0;
    const double leapPenalty = melodic > 1 ? (double) leaps / (melodic - 1) / 12.0 : 0.0;
    const double density = (double) r.notes.size() / totalBeats;
    const double densityPenalty = std::abs(std::log((density + 0.1)
                                                    / (densityTarget(a, intent) + 0.1)));

    const double fill = juce::jlimit(0.0, 1.0, (double) r.notes.size() / (double) totalSteps);
    return keyScore * 3.0 + rangeScore * 1.5 + fill * 0.5
           - leapPenalty * 0.8 - densityPenalty * 0.9;
}

struct AudioAnalysis
{
    bool valid = false;
    std::array<double, 12> chroma {};
    std::vector<std::array<double, 12>> barChroma;
    std::vector<int> bassPc; // strongest low-frequency pitch class per bar
    int root = 0;
    bool minor = false;
    float confidence = 0.0f;
    double seconds = 0.0;
    double rms = 0.0;
    double onsetDensity = 0.0;
    double centroidMidi = 60.0;
};

bool projectHasSolo(const Project& project)
{
    for (const auto& track : project.tracks)
        if (track.solo)
            return true;
    return false;
}

// Mixes every audible audio clip in [startSeconds, endSeconds) to mono and
// extracts the information the generator actually needs: a global chroma
// profile (key), a per-bar chroma (chords), brightness and onset density.
// Only ~10-20 MB of mono audio is held for a one-minute window; no files are
// opened, because the project already keeps decoded sources in memory.
AudioAnalysis analyseAudio(const Project& project, double startSeconds,
                           double endSeconds, double windowStart,
                           int bars, double barSeconds, double beatSeconds)
{
    AudioAnalysis out;
    out.barChroma.assign((std::size_t) bars, std::array<double, 12> {});
    out.seconds = juce::jmax(0.0, endSeconds - startSeconds);
    if (out.seconds <= 0.0 || project.sampleRate <= 0.0)
        return out;

    const double sr = project.sampleRate;
    const int numSamples = (int) std::ceil(out.seconds * sr);
    if (numSamples <= 0)
        return out;

    std::vector<float> mix((std::size_t) numSamples, 0.0f);
    bool anyAudio = false;
    const bool anySolo = projectHasSolo(project);

    for (const auto& track : project.tracks)
    {
        if (track.mute || (anySolo && ! track.solo))
            continue;
        for (const auto& clip : track.clips)
        {
            if (clip.isMidi() || clip.muted || clip.lengthSeconds <= 0.0)
                continue;
            const auto srcIt = project.sources.find(clip.sourceId);
            if (srcIt == project.sources.end() || srcIt->second == nullptr)
                continue;
            const auto& source = *srcIt->second;
            if (source.sampleRate <= 0.0 || source.samples.getNumSamples() <= 0)
                continue;

            const double clipStart = clip.startSeconds;
            const double clipEnd = clipStart + clip.lengthSeconds;
            const double from = juce::jmax(startSeconds, clipStart);
            const double to = juce::jmin(endSeconds, clipEnd);
            if (to <= from)
                continue;

            const double srcFrom = from - clipStart + clip.sourceOffsetSeconds;
            if (srcFrom < 0.0)
                continue;
            const std::int64_t srcStart = (std::int64_t) std::llround(
                srcFrom * source.sampleRate);
            const int dstStart = (int) std::llround((from - startSeconds) * sr);
            if (srcStart < 0 || dstStart < 0 || dstStart >= numSamples)
                continue;

            const int count = (int) juce::jmin(
                (std::int64_t) ((to - from) * source.sampleRate),
                (std::int64_t) source.samples.getNumSamples() - srcStart);
            const int available = juce::jmin(count, numSamples - dstStart);
            if (available <= 0)
                continue;

            const int channels = source.samples.getNumChannels();
            const float gain = clip.gain * track.volume;
            for (int i = 0; i < available; ++i)
            {
                float value = 0.0f;
                for (int ch = 0; ch < channels; ++ch)
                    value += source.samples.getSample(ch, (int) srcStart + i);
                mix[(std::size_t) (dstStart + i)]
                    += value / (float) channels * gain;
            }
            anyAudio = true;
        }
    }

    if (! anyAudio)
        return out;

    constexpr int kFftOrder = 12;
    constexpr int kFftSize = 1 << kFftOrder;
    constexpr int kHop = 2048;
    if (numSamples < kFftSize)
        return out;

    juce::dsp::FFT fft(kFftOrder);
    juce::dsp::WindowingFunction<float> window(
        kFftSize, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> fftData((std::size_t) (2 * kFftSize), 0.0f);
    std::vector<float> hann((std::size_t) kFftSize, 1.0f);
    window.multiplyWithWindowingTable(hann.data(), (std::size_t) kFftSize);

    std::array<double, 12> totalChroma {};
    std::vector<double> flux;
    std::vector<float> previousMag((std::size_t) (kFftSize / 2 + 1), 0.0f);
    std::vector<float> magnitude((std::size_t) (kFftSize / 2 + 1), 0.0f);
    std::vector<double> barStrength((std::size_t) bars, 0.0);
    std::vector<std::array<double, 12>> barBass((std::size_t) bars);
    std::vector<double> barBassStrength((std::size_t) bars, 0.0);
    double centroidNumerator = 0.0;
    double centroidDenominator = 0.0;
    double sumSquares = 0.0;

    for (float s : mix)
        sumSquares += (double) s * (double) s;

    for (std::size_t pos = 0; pos + (std::size_t) kFftSize <= mix.size();
         pos += (std::size_t) kHop)
    {
        double frameEnergy = 0.0;
        for (int i = 0; i < kFftSize; ++i)
        {
            const float sample = mix[pos + (std::size_t) i];
            frameEnergy += (double) sample * (double) sample;
            fftData[(std::size_t) i] = sample * hann[(std::size_t) i];
        }
        std::fill(fftData.begin() + kFftSize, fftData.end(), 0.0f);
        fft.performRealOnlyForwardTransform(fftData.data(), true);

        for (int k = 0; k <= kFftSize / 2; ++k)
        {
            const float re = fftData[(std::size_t) (2 * k)];
            const float im = fftData[(std::size_t) (2 * k + 1)];
            magnitude[(std::size_t) k] = std::sqrt(re * re + im * im);
        }

        double frameFlux = 0.0;
        for (int k = 1; k <= kFftSize / 2; ++k)
            frameFlux += juce::jmax(0.0, (double) magnitude[(std::size_t) k]
                                         - (double) previousMag[(std::size_t) k]);

        if (frameEnergy / (double) kFftSize > 1.0e-7)
        {
            const double frameTime = startSeconds + (double) pos / sr;
            int bar = -1;
            if (frameTime >= windowStart
                && frameTime < windowStart + (double) bars * barSeconds)
                bar = juce::jlimit(0, bars - 1,
                                   (int) ((frameTime - windowStart) / barSeconds));

            for (int k = 1; k <= kFftSize / 2; ++k)
            {
                const double freq = (double) k * sr / (double) kFftSize;
                if (freq < 40.0 || freq > 2200.0)
                    continue;

                const double mag = (double) magnitude[(std::size_t) k];
                const double weight = std::sqrt(440.0 / freq);
                const double midi = 69.0 + 12.0 * std::log2(freq / 440.0);
                const int base = (int) std::floor(midi);
                const double frac = midi - (double) base;
                const int pc0 = ((base % 12) + 12) % 12;
                const int pc1 = (pc0 + 1) % 12;

                if (bar >= 0 && freq <= 260.0)
                {
                    barBass[(std::size_t) bar][(std::size_t) pc0]
                        += mag * (1.0 - frac);
                    barBass[(std::size_t) bar][(std::size_t) pc1]
                        += mag * frac;
                    barBassStrength[(std::size_t) bar] += mag;
                }

                if (freq < 60.0)
                    continue;

                totalChroma[(std::size_t) pc0] += mag * weight * (1.0 - frac);
                totalChroma[(std::size_t) pc1] += mag * weight * frac;

                if (bar >= 0)
                {
                    out.barChroma[(std::size_t) bar][(std::size_t) pc0]
                        += mag * weight * (1.0 - frac);
                    out.barChroma[(std::size_t) bar][(std::size_t) pc1]
                        += mag * weight * frac;
                    barStrength[(std::size_t) bar] += mag;
                }

                centroidNumerator += mag * freq;
                centroidDenominator += mag;
            }
        }

        flux.push_back(frameFlux);
        previousMag.swap(magnitude);
    }

    if (flux.empty())
        return out;

    out.rms = std::sqrt(sumSquares / (double) mix.size());
    const double chromaTotal = std::accumulate(totalChroma.begin(),
                                               totalChroma.end(), 0.0);
    if (out.rms <= 1.0e-6 || chromaTotal <= 1.0e-9)
        return out;

    for (double& c : totalChroma)
        c /= chromaTotal;
    out.chroma = totalChroma;
    for (int b = 0; b < bars; ++b)
    {
        const double sum = std::accumulate(out.barChroma[(std::size_t) b].begin(),
                                           out.barChroma[(std::size_t) b].end(),
                                           0.0);
        if (sum <= 1.0e-9 || barStrength[(std::size_t) b] <= 1.0e-7)
            continue;
        for (double& c : out.barChroma[(std::size_t) b])
            c /= sum;
    }

    out.bassPc.assign((std::size_t) bars, -1);
    for (int b = 0; b < bars; ++b)
    {
        if (barBassStrength[(std::size_t) b] <= 1.0e-7)
            continue;
        const auto& bass = barBass[(std::size_t) b];
        const auto it = std::max_element(bass.begin(), bass.end());
        if (it != bass.end() && *it > 1.0e-9)
            out.bassPc[(std::size_t) b] = (int) std::distance(bass.begin(), it);
    }

    estimateKeyFromHistogram(totalChroma, out.root, out.minor,
                             out.confidence, 1.0f);
    if (centroidDenominator > 1.0e-9)
        out.centroidMidi = juce::jlimit(
            36.0, 96.0,
            69.0 + 12.0 * std::log2(
                (centroidNumerator / centroidDenominator) / 440.0));

    const double mean = std::accumulate(flux.begin(), flux.end(), 0.0)
                      / (double) flux.size();
    double variance = 0.0;
    for (double f : flux)
        variance += (f - mean) * (f - mean);
    variance /= (double) flux.size();
    const double threshold = mean + 0.75 * std::sqrt(juce::jmax(0.0, variance));
    int onsets = 0;
    for (double f : flux)
        if (f > threshold && f > 1.0e-5)
            ++onsets;

    const double beats = juce::jmax(1.0, out.seconds / beatSeconds);
    out.onsetDensity = (double) onsets / beats;
    out.valid = true;
    return out;
}

magenta::Sequence buildSeed(const Service::Analysis& a, const Request& req,
                            const Project& project, bool drums)
{
    magenta::Sequence seed;
    seed.numSteps = 64;
    seed.stepsPerQuarter = kStepsPerQuarter;

    const double stepSeconds = a.barSeconds / kStepsPerBar;
    double seedStart = a.anchor - 4.0 * a.barSeconds;
    std::vector<AbsNote> source;

    if (req.replaceClip && req.clipId > 0)
    {
        if (const auto* clip = project.findClip(req.clipId))
        {
            seedStart = clip->startSeconds;
            for (const auto& n : clip->midi->notes)
            {
                if (drums && (n.pitch < 35 || n.pitch > 60))
                    continue;
                AbsNote an;
                an.pitch = n.pitch;
                an.start = clip->startSeconds + n.startSeconds;
                an.end = an.start + n.lengthSeconds;
                an.velocity = n.velocity;
                an.drum = drums;
                source.push_back(an);
            }
        }
    }

    if (source.empty())
    {
        for (const auto& n : a.context)
        {
            if (n.track == req.trackId
                && (n.drum == drums))
                source.push_back(n);
        }
    }
    if (source.empty())
    {
        for (const auto& n : a.context)
            if (n.drum == drums)
                source.push_back(n);
    }

    for (const auto& n : source)
    {
        double s = (n.start - seedStart) / stepSeconds;
        double e = (n.end - seedStart) / stepSeconds;
        if (e <= 0.0 || s >= 64.0)
            continue;
        s = juce::jmax(0.0, s);
        e = juce::jmin(64.0, e);
        const int start = (int) std::lround(s);
        const int end = (int) std::lround(e);
        if (start >= 64)
            continue;

        magenta::Note mn;
        mn.pitch = n.pitch;
        mn.startStep = start;
        mn.lengthSteps = juce::jmax(1.0, (double) (end - start));
        mn.velocity = n.velocity;
        mn.isDrum = drums;
        seed.notes.push_back(mn);
    }
    return seed;
}

magenta::Sequence buildContinuationSeed(const Result& result, int chunkStartBar,
                                        const Service::Analysis& a, bool drums)
{
    magenta::Sequence seed;
    seed.numSteps = 64;
    seed.stepsPerQuarter = kStepsPerQuarter;

    const double stepSeconds = a.barSeconds / kStepsPerBar;
    const double seedStart = (double) (chunkStartBar - 4) * a.barSeconds;
    const double seedEnd = (double) chunkStartBar * a.barSeconds;

    for (const auto& n : result.notes)
    {
        if (n.isDrum != drums)
            continue;
        if (n.startSeconds < seedStart || n.startSeconds >= seedEnd)
            continue;

        const double s = (n.startSeconds - seedStart) / stepSeconds;
        const double e = (n.startSeconds + n.lengthSeconds - seedStart) / stepSeconds;
        if (e <= 0.0 || s >= 64.0)
            continue;

        const int start = (int) std::lround(juce::jlimit(0.0, 63.0, s));
        const int end = (int) std::lround(juce::jlimit(0.0, 64.0, e));
        magenta::Note mn;
        mn.pitch = n.pitch;
        mn.startStep = (double) start;
        mn.lengthSteps = juce::jmax(1.0, (double) (end - start));
        mn.velocity = n.velocity;
        mn.isDrum = drums;
        seed.notes.push_back(mn);
    }
    return seed;
}

void addNoteIfMissing(std::vector<GeneratedNote>& notes, const GeneratedNote& n)
{
    for (const auto& existing : notes)
    {
        if (existing.pitch == n.pitch
            && std::abs(existing.startSeconds - n.startSeconds) < 1.0e-4)
            return;
    }
    notes.push_back(n);
}

void harmonize(Result& result, const Service::Analysis& a, const Request& req,
               const Service::Intent& intent, double stepSeconds)
{
    if (req.mode == Mode::Drums || result.notes.empty())
        return;

    // The VAE emits a monophonic melody. Stack up to two chord tones below
    // each note, using the chord estimated for that bar.
    std::vector<GeneratedNote> extra;
    for (const auto& note : result.notes)
    {
        if (note.isDrum)
            continue;
        const int bar = juce::jlimit(0, req.bars - 1,
                                    (int) (note.startSeconds / a.barSeconds));
        const int root = chordRootAt(a, bar);
        const int quality = chordQualityAt(a, bar);
        const auto pcs = chordTonePcs(root, quality);

        int added = 0;
        for (int pc : pcs)
        {
            if (added >= 2)
                break;
            // Prefer chord tones below the melody: this leaves the lead
            // readable and avoids stacking everything in the same octave.
            int p = pc;
            while (p >= note.pitch)
                p -= 12;
            while (p < note.pitch - 24)
                p += 12;
            if (p < 24 || p > 96)
                continue;

            GeneratedNote h = note;
            h.pitch = juce::jlimit(0, 127, p);
            h.velocity = juce::jlimit(0.22f, 0.75f, note.velocity * 0.68f);
            h.startSeconds = note.startSeconds;
            h.lengthSeconds = note.lengthSeconds;
            addNoteIfMissing(extra, h);
            ++added;
        }
    }
    for (const auto& h : extra)
        addNoteIfMissing(result.notes, h);

    // "Pad" mode: make sure every bar has a sustained chord bed.
    if (intent.pad)
    {
        for (int bar = 0; bar < req.bars; ++bar)
        {
            const int root = chordRootAt(a, bar);
            const int quality = chordQualityAt(a, bar);
            for (int pc : chordTonePcs(root, quality))
            {
                int p = pc;
                while (p < 48) p += 12;
                while (p > 76) p -= 12;
                GeneratedNote n;
                n.pitch = p;
                n.startSeconds = bar * a.barSeconds;
                n.lengthSeconds = a.barSeconds;
                n.velocity = 0.55f;
                addNoteIfMissing(result.notes, n);
            }
        }
    }
    juce::ignoreUnused(stepSeconds);
}

void makeArpeggio(Result& result, const Service::Analysis& a, const Request& req,
                  const Service::Intent& intent, double stepSeconds)
{
    if (req.mode == Mode::Drums || req.mode == Mode::Bass)
        return;

    const auto range = rangeFor(req.mode, a.medianPitch, intent.octaveShift);
    std::vector<GeneratedNote> arp;
    const int stride = densityTarget(a, intent) > 3.5 ? 1 : 2;
    for (int bar = 0; bar < req.bars; ++bar)
    {
        const int root = chordRootAt(a, bar);
        const int quality = chordQualityAt(a, bar);
        auto pcs = chordTonePcs(root, quality);
        pcs.push_back(root + 12);
        for (int step = 0; step < kStepsPerBar; step += stride)
        {
            const int pc = pcs[(std::size_t) ((step / stride) % (int) pcs.size())];
            int p = pc;
            while (p < range.low) p += 12;
            while (p > range.high) p -= 12;
            GeneratedNote n;
            n.pitch = p;
            n.startSeconds = (bar * kStepsPerBar + step) * stepSeconds;
            n.lengthSeconds = stride * stepSeconds;
            n.velocity = (step % 4 == 0) ? 0.9f : 0.7f;
            arp.push_back(n);
        }
    }
    result.notes = std::move(arp);
}

} // namespace

juce::File defaultModelDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable("CADENCE_AI_MODELS", {});
    if (env.isNotEmpty())
        return juce::File(env);
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Music/EZMusicProduction/AI Models");
}

void Service::setModelDir(const juce::File& dir)
{
    modelDir_ = dir;
    melodyModel_.reset();
    drumModel_.reset();
}

juce::File Service::modelDir() const
{
    return modelDir_.isDirectory() ? modelDir_ : defaultModelDir();
}

namespace
{
bool checkpointMatches(const juce::File& dir, bool drums)
{
    const auto config = dir.getChildFile("config.json");
    if (! config.existsAsFile())
        return false;
    const auto parsed = juce::JSON::parse(config);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr)
        return false;
    auto converter = root->getProperty("dataConverter");
    auto* c = converter.getDynamicObject();
    if (c == nullptr)
        return false;
    const auto type = c->getProperty("type").toString();
    if (drums)
        return type == "DrumsConverter" || type == "DrumsOneHotConverter";
    return type == "MelodyConverter";
}

juce::File findCheckpoint(const juce::File& root, bool drums)
{
    if (checkpointMatches(root, drums))
        return root;

    const auto preferred = root.getChildFile(drums ? "drums_4bar_med_lokl"
                                                   : "mel_4bar_med_lokl");
    if (checkpointMatches(preferred, drums))
        return preferred;

    // Users usually extract generate.zip as
    //   AI Models/generate/models/{mel_4bar_med_lokl,drums_4bar_med_lokl}
    // so walk a few levels down looking for the first matching checkpoint.
    std::vector<juce::File> level { root };
    for (int depth = 0; depth < 4 && ! level.empty(); ++depth)
    {
        std::vector<juce::File> next;
        for (const auto& dir : level)
        {
            for (const auto& child : dir.findChildFiles(juce::File::findDirectories,
                                                        false))
            {
                if (checkpointMatches(child, drums))
                    return child;
                next.push_back(child);
            }
        }
        level = std::move(next);
    }
    return {};
}
} // namespace

bool Service::modelAvailable(Mode mode) const
{
    return findCheckpoint(modelDir(), mode == Mode::Drums).isDirectory();
}

magenta::MusicVAE* Service::ensureModel(Mode mode, juce::String& error)
{
    const bool drums = mode == Mode::Drums;
    auto& slot = drums ? drumModel_ : melodyModel_;
    if (slot != nullptr && slot->isLoaded())
        return slot.get();

    const auto dir = findCheckpoint(modelDir(), drums);
    if (! dir.isDirectory())
    {
        error = drums ? "Magenta drums checkpoint not installed"
                      : "Magenta melody checkpoint not installed";
        return nullptr;
    }

    auto model = std::make_unique<magenta::MusicVAE>();
    if (! model->load(dir, error))
    {
        slot.reset();
        return nullptr;
    }
    slot = std::move(model);
    return slot.get();
}

Service::Analysis Service::analyse(const Project& project, const Request& request) const
{
    Analysis a;
    a.barSeconds = (60.0 / juce::jmax(20.0, project.tempoBpm))
                 * juce::jmax(1, project.timeSigNumerator)
                 * 4.0 / juce::jmax(1, project.timeSigDenominator);
    a.beatSeconds = a.barSeconds / juce::jmax(1.0, project.timeSigNumerator
                                                      * 4.0 / project.timeSigDenominator);
    const double requestedLength = request.lengthSeconds > 0.0
        ? request.lengthSeconds
        : (double) juce::jlimit(1, 64, request.bars) * a.barSeconds;
    a.lengthSeconds = juce::jlimit(a.barSeconds, 120.0, requestedLength);
    a.bars = juce::jlimit(1, 64, (int) std::ceil(a.lengthSeconds / a.barSeconds
                                                - 1.0e-6));
    a.lengthSeconds = juce::jmin(a.lengthSeconds,
                                 (double) a.bars * a.barSeconds);
    a.anchor = juce::jmax(0.0, request.anchorSeconds);

    const Track* targetTrack = project.findTrack(request.trackId);
    if (targetTrack != nullptr)
    {
        a.targetName = targetTrack->name;
        a.instrument = targetTrack->instrument.name;
    }

    if (request.replaceClip && request.clipId > 0)
        if (const auto* clip = project.findClip(request.clipId))
            a.anchor = clip->startSeconds;

    a.anchor = std::floor(a.anchor / a.barSeconds) * a.barSeconds;
    const double windowStart = a.anchor;
    const double windowEnd = a.anchor + a.lengthSeconds;
    const double contextStart = a.anchor - 4.0 * a.barSeconds;
    const bool anySolo = projectHasSolo(project);

    for (const auto& track : project.tracks)
    {
        if (track.mute || (anySolo && ! track.solo))
            continue;
        for (const auto& clip : track.clips)
        {
            if (! clip.isMidi() || clip.muted)
                continue;
            for (const auto& n : clip.midi->notes)
            {
                AbsNote an;
                an.pitch = n.pitch;
                an.start = clip.startSeconds + n.startSeconds;
                an.end = an.start + n.lengthSeconds;
                an.velocity = n.velocity;
                an.drum = n.pitch >= 35 && n.pitch <= 60
                          && (track.name.toLowerCase().contains("drum")
                              || track.name.toLowerCase().contains("bater")
                              || track.instrument.name.toLowerCase().contains("drum"));
                an.track = track.id;
                an.trackName = track.name;
                an.instrument = track.instrument.name;

                if (an.end <= contextStart || an.start >= windowEnd)
                    continue;
                if (an.start < windowStart)
                    a.context.push_back(an);
                else
                    a.window.push_back(an);
                if (track.id == request.trackId)
                    a.target.push_back(an);
            }
        }
    }

    a.role = roleForTrack(a.targetName, a.instrument, a.target);

    auto keyNotes = a.window;
    if (keyNotes.size() < 4)
        keyNotes = a.context;
    int midiRoot = 0;
    bool midiMinor = false;
    float midiConfidence = 0.0f;
    estimateKey(keyNotes, a.barSeconds, midiRoot, midiMinor, midiConfidence);
    a.root = midiRoot;
    a.minor = midiMinor;
    a.confidence = midiConfidence;

    // Listen to the audio tracks too. They often carry the harmony (loops,
    // stems, rendered parts) while the target instrument track is empty.
    const auto audio = analyseAudio(project, juce::jmax(0.0, contextStart),
                                    windowEnd, windowStart, a.bars,
                                    a.barSeconds, a.beatSeconds);
    a.audioUsed = audio.valid;
    a.audioSeconds = audio.seconds;
    a.audioConfidence = audio.confidence;
    a.audioRoot = audio.root;
    a.audioMinor = audio.minor;
    a.audioOnsetsPerBeat = audio.onsetDensity;

    bool usedAudioKey = false;
    if (audio.valid)
    {
        const bool midiMissing = keyNotes.empty();
        const bool midiSparse = keyNotes.size() < 8;
        if (midiMissing
            || (midiSparse && audio.confidence > 0.12f)
            || (midiConfidence < 0.18f && audio.confidence > 0.22f))
        {
            a.root = audio.root;
            a.minor = audio.minor;
            a.confidence = audio.confidence;
            usedAudioKey = true;
        }
        else if (midiRoot == audio.root && midiMinor == audio.minor)
        {
            a.confidence = juce::jlimit(
                0.0f, 1.0f,
                juce::jmax(midiConfidence, audio.confidence) + 0.08f);
        }
    }
    a.audioKeyUsed = usedAudioKey;

    a.medianPitch = (int) std::lround(medianPitch(a.target.empty() ? a.context
                                                                 : a.target));
    if (a.medianPitch <= 0)
        a.medianPitch = 60;
    if (a.target.empty() && a.context.empty() && audio.valid)
        a.medianPitch = juce::jlimit(40, 84,
                                     (int) std::lround(audio.centroidMidi));

    const double beatsPerBar = a.barSeconds / juce::jmax(1.0e-6, a.beatSeconds);
    const double beats = juce::jmax(1.0, a.bars * beatsPerBar);
    a.density = (float) juce::jlimit(0.25, 12.0,
                                     (double) a.window.size() / beats);
    if (a.window.empty())
    {
        if (audio.valid)
            a.density = (float) juce::jlimit(0.5, 8.0,
                                             audio.onsetDensity * 0.85);
        else
            a.density = (float) juce::jlimit(0.25, 12.0,
                                             (double) a.context.size() / 16.0);
    }

    // Chords, bar by bar. A bar with too little MIDI falls back to the audio
    // chroma for that bar, then to the default progression for the key.
    a.useDefaultProgression();

    std::vector<int> measuredRoot((std::size_t) a.bars, -1);
    std::vector<int> measuredQuality((std::size_t) a.bars, 0);
    std::vector<char> measured((std::size_t) a.bars, 0);

    for (int b = 0; b < a.bars; ++b)
    {
        const double barStart = windowStart + b * a.barSeconds;
        const double barEnd = barStart + a.barSeconds;
        std::vector<AbsNote> barNotes;
        for (const auto& n : a.window)
            if (n.start < barEnd && n.end > barStart)
                barNotes.push_back(n);

        int r = 0;
        int q = 0;
        bool gotChord = barNotes.size() >= 2
                     && estimateChord(barNotes, a.barSeconds, r, q);
        if (! gotChord && audio.valid && b < (int) audio.barChroma.size())
            gotChord = estimateChordFromChroma(audio.barChroma[(std::size_t) b],
                                               b < (int) audio.bassPc.size()
                                                   ? audio.bassPc[(std::size_t) b]
                                                   : -1,
                                               r, q);
        if (gotChord)
        {
            measuredRoot[(std::size_t) b] = r;
            measuredQuality[(std::size_t) b] = q;
            measured[(std::size_t) b] = 1;
        }
    }

    // Relative keys (C minor / Eb major) share nearly the same notes; the
    // measured chords disambiguate them. When the key came from audio, the
    // chroma already includes the harmony and is left alone.
    if (keyNotes.size() >= 4 && ! usedAudioKey)
        refineKeyWithChords(keyNotes, a.barSeconds, measuredRoot, measuredQuality,
                            measured, a.root, a.minor, a.confidence);

    // Rebuild the fallback progression in the refined key, then put the
    // measured chords back.
    a.useDefaultProgression();
    for (int b = 0; b < a.bars; ++b)
    {
        if (! measured[(std::size_t) b])
            continue;
        a.chordRoot[(std::size_t) b] = measuredRoot[(std::size_t) b];
        a.chordQuality[(std::size_t) b] = measuredQuality[(std::size_t) b];
    }

    if (juce::SystemStats::getEnvironmentVariable("CADENCE_AI_DEBUG", {}).isNotEmpty())
    {
        std::fprintf(stderr,
                     "[ai] key=%d minor=%d conf=%.2f bars=%d len=%.1fs audio=%d"
                     " audioKey=%d/%d conf=%.2f chords:",
                     a.root, a.minor ? 1 : 0, (double) a.confidence,
                     a.bars, a.lengthSeconds, audio.valid ? 1 : 0,
                     audio.root, audio.minor ? 1 : 0,
                     (double) audio.confidence);
        for (int b = 0; b < a.bars; ++b)
            std::fprintf(stderr, " %d/%d%s", a.chordRoot[(std::size_t) b],
                         a.chordQuality[(std::size_t) b],
                         measured[(std::size_t) b] ? "" : "?");
        std::fprintf(stderr, "\n");
    }

    return a;
}

Service::Intent Service::interpretPrompt(const juce::String& prompt,
                                         const Analysis& analysis) const
{
    Intent intent;
    intent.root = analysis.root;
    intent.minor = analysis.minor;

    const auto lower = prompt.toLowerCase();
    if (lower.trim().isEmpty())
        return intent;

    auto tokens = juce::StringArray::fromTokens(lower, " \t\n,.;:!?()[]{}<>|/\\", "\"'");
    tokens.removeEmptyStrings();

    auto noteFromToken = [&] (juce::String token, int& pc, bool& hasQuality,
                              bool& minorQuality) -> bool {
        token = token.trim().toLowerCase();
        if (token.isEmpty())
            return false;

        minorQuality = false;
        hasQuality = false;
        if (token.endsWith("min")) { token = token.dropLastCharacters(3); hasQuality = true; minorQuality = true; }
        else if (token.endsWith("maj")) { token = token.dropLastCharacters(3); hasQuality = true; minorQuality = false; }
        else if (token.endsWith("m") && token.length() > 1)
        {
            token = token.dropLastCharacters(1);
            hasQuality = true;
            minorQuality = true;
        }

        int base = -1;
        if (token == "do") base = 0;
        else if (token == "re") base = 2;
        else if (token == "mi") base = 4;
        else if (token == "fa") base = 5;
        else if (token == "sol" || token == "so") base = 7;
        else if (token == "la") base = 9;
        else if (token == "si" || token == "ti") base = 11;
        else if (token.length() >= 1 && token[0] >= 'a' && token[0] <= 'g')
        {
            static const int map[7] = { 9, 11, 0, 2, 4, 5, 7 };
            base = map[(int) (token[0] - 'a')];
            if (token.length() == 2)
            {
                if (token[1] == '#') base = (base + 1) % 12;
                else if (token[1] == 'b') base = (base + 11) % 12;
            }
        }
        if (base < 0)
            return false;
        if (token.length() >= 2 && token[1] == '#') base = (base + 1) % 12;
        if (token.length() >= 2 && token[1] == 'b') base = (base + 11) % 12;
        pc = (base % 12 + 12) % 12;
        return true;
    };

    bool sawMinorWord = false;
    bool sawMajorWord = false;
    for (int i = 0; i < tokens.size(); ++i)
    {
        const auto t = tokens[i].toLowerCase();
        int pc = 0;
        bool hasQuality = false;
        bool minorQuality = false;
        if (noteFromToken(t, pc, hasQuality, minorQuality))
        {
            bool qualityKnown = hasQuality;
            bool qualityMinor = minorQuality;
            if (! qualityKnown && i + 1 < tokens.size())
            {
                const auto next = tokens[i + 1].toLowerCase();
                if (next == "menor" || next == "minor" || next == "min" || next == "m")
                {
                    qualityKnown = true;
                    qualityMinor = true;
                    ++i;
                }
                else if (next == "mayor" || next == "major" || next == "maj")
                {
                    qualityKnown = true;
                    qualityMinor = false;
                    ++i;
                }
            }
            if (qualityKnown)
            {
                intent.keySpecified = true;
                intent.root = pc;
                intent.minor = qualityMinor;
            }
            else if (t.length() == 1 || (t.length() == 2 && (t[1] == '#' || t[1] == 'b')))
            {
                // Bare note names only count as a key when a mode word is
                // nearby; otherwise "a" (the article) would hijack prompts.
                if (i + 1 < tokens.size())
                {
                    const auto next = tokens[i + 1].toLowerCase();
                    if (next == "menor" || next == "minor" || next == "mayor"
                        || next == "major")
                    {
                        intent.keySpecified = true;
                        intent.root = pc;
                        intent.minor = next.startsWith("men");
                        ++i;
                    }
                }
            }
        }

        const auto startsWith = [&t] (const char* prefix) {
            return t.startsWith(prefix);
        };

        if (t == "menor" || t == "minor" || t == "min" || t == "dark"
            || startsWith("oscuro") || startsWith("triste")
            || startsWith("melanc"))
            sawMinorWord = true;
        if (t == "mayor" || t == "major" || t == "maj" || t == "bright"
            || startsWith("alegre") || startsWith("feliz") || t == "happy")
            sawMajorWord = true;

        if (t == "menos" || t == "pocas" || t == "poco" || t == "sparse"
            || t == "simple" || t == "minimal" || startsWith("minimal")
            || t == "calm" || startsWith("calm") || t == "tranquil"
            || startsWith("tranquil") || t == "suave" || t == "slow"
            || t == "lento" || t == "lenta")
            intent.densityBias -= 0.45f;
        if (t == "más" || t == "mas" || t == "muchas" || t == "dense"
            || t == "denso" || t == "lleno" || t == "busy" || t == "complejo"
            || t == "complex" || startsWith("energ") || t == "energetic"
            || t == "fast" || startsWith("rápid") || startsWith("rapid"))
            intent.densityBias += 0.45f;

        if (t == "bajo" || t == "bass" || t == "low" || t == "grave"
            || t == "graves")
            intent.octaveShift -= 12;
        if (t == "agudo" || t == "agudos" || t == "high" || t == "lead"
            || t == "brillante")
            intent.octaveShift += 12;

        if (t == "pad" || startsWith("acord") || startsWith("chord")
            || startsWith("armon") || startsWith("harmon") || t == "harmony")
        {
            intent.harmonize = true;
            intent.pad = true;
        }

        // Explicit role words retarget the generation mode. They are kept
        // separate from the harmonize/pad flags so "harmonize this" still
        // means "add harmony to the current mode".
        if (t == "drums" || startsWith("drum") || startsWith("bater")
            || startsWith("perc") || t == "beat" || t == "beats"
            || t == "ritmo" || t == "ritmos")
        {
            intent.hasMode = true;
            intent.mode = Mode::Drums;
        }
        if (t == "bass" || startsWith("bassline") || t == "bajo"
            || startsWith("contrabaj") || t == "sub" || t == "subbass")
        {
            intent.hasMode = true;
            intent.mode = Mode::Bass;
        }
        if (t == "chords" || t == "chord" || startsWith("acord")
            || t == "harmony" || t == "harmonia" || t == "armonia"
            || t == juce::String::fromUTF8("armonía"))
        {
            intent.hasMode = true;
            intent.mode = Mode::Harmony;
        }
        if (t == "melody" || startsWith("melod") || t == "lead")
        {
            intent.hasMode = true;
            intent.mode = Mode::Melody;
        }
        if (t == "completo" || t == "completa" || t == "full"
            || startsWith("complet") || t == "harmonize" || t == "harmonized"
            || t == "contramelodia" || t == "counter")
            intent.harmonize = true;
        if (startsWith("arpeg"))
            intent.arpeggio = true;
        if (t == "swing" || t == "groove" || startsWith("groovy")
            || startsWith("human") || t == "humano" || t == "humana")
        {
            intent.swing = true;
            intent.humanize = true;
        }
        if (startsWith("oscuro") || t == "dark" || startsWith("triste"))
        {
            intent.forceMinor = true;
            intent.tempBias += 0.05f;
        }
        if (t == "alegre" || t == "bright" || t == "happy"
            || startsWith("feliz"))
            intent.forceMajor = true;
    }

    if (! intent.keySpecified && sawMinorWord)
    {
        intent.forceMinor = true;
        intent.minor = true;
    }
    if (! intent.keySpecified && sawMajorWord)
    {
        intent.forceMajor = true;
        intent.minor = false;
    }

    // Bass mode already selects a low register, so a prompt word like "bajo"
    // or "bass" must not drop the range another octave.
    if (intent.hasMode && intent.mode == Mode::Bass)
        intent.octaveShift = juce::jmax(0, intent.octaveShift);

    intent.densityBias = juce::jlimit(-0.7f, 0.9f, intent.densityBias);

    juce::StringArray bits;
    if (intent.keySpecified)
        bits.add(keyName(intent.root, intent.minor));
    if (intent.forceMinor) bits.add("minor");
    if (intent.forceMajor) bits.add("major");
    if (intent.densityBias < -0.1f) bits.add("sparse");
    if (intent.densityBias > 0.1f) bits.add("dense");
    if (intent.octaveShift < 0) bits.add("low register");
    if (intent.octaveShift > 0) bits.add("high register");
    if (intent.harmonize) bits.add("harmony");
    if (intent.arpeggio) bits.add("arpeggio");
    if (intent.swing) bits.add("groove");
    if (intent.hasMode)
    {
        static const char* modeNames[] = { "melody", "drums", "bass", "harmony" };
        bits.add(juce::String("mode ") + modeNames[(int) intent.mode]);
    }
    intent.note = bits.joinIntoString(", ");
    return intent;
}

Result Service::generateWithModel(magenta::MusicVAE& model, const Project& project,
                                  const Analysis& analysis,
                                  const Request& request, const Intent& intent,
                                  std::uint32_t seed) const
{
    Result result;
    result.provider = juce::String("Magenta MusicVAE (")
                    + (request.mode == Mode::Drums ? "drums" : "melody") + ")";
    if (request.bars > 4)
        result.provider += " · long form";
    result.key = keyName(analysis.root, analysis.minor);

    const bool drums = request.mode == Mode::Drums;
    const auto seedSeq = buildSeed(analysis, request, project, drums);
    const double stepSeconds = analysis.barSeconds / kStepsPerBar;
    const int chunks = (request.bars + 3) / 4;
    const float temperature = (float) juce::jlimit(
        0.1, 1.8, request.temperature + intent.tempBias);

    // A 60-second request means up to eight 4-bar model windows per candidate.
    // Limit the number of full-length candidates so the job still finishes
    // quickly while leaving the shorter, interactive requests untouched.
    int variations = juce::jlimit(1, 8, request.variations);
    if (request.bars > 8)
        variations = 1;
    else if (request.bars > 4)
        variations = juce::jmin(variations, 3);

    Result best;
    double bestScore = -1.0e30;

    for (int v = 0; v < variations; ++v)
    {
        const std::uint32_t candidateSeed = seed + (std::uint32_t) v * 7919u;
        Result candidate;
        candidate.provider = result.provider;
        candidate.key = result.key;
        candidate.seed = (int) candidateSeed;
        candidate.clipLengthSeconds = analysis.lengthSeconds;

        for (int c = 0; c < chunks; ++c)
        {
            const int chunkStartBar = c * 4;
            const int chunkBars = juce::jmin(4, request.bars - chunkStartBar);
            if (chunkBars <= 0)
                continue;

            magenta::Sequence chunkSeed;
            float similarity = 0.45f;
            if (c == 0)
            {
                chunkSeed = seedSeq;
                if (request.replaceClip)
                    similarity = 1.0f;
                else if (request.mode == Mode::Harmony || intent.harmonize)
                    similarity = 0.35f;
            }
            else
            {
                chunkSeed = buildContinuationSeed(candidate, chunkStartBar,
                                                  analysis, drums);
                // Encode/decode the previous phrase so the next one grows out
                // of it instead of being an unrelated four-bar loop.
                similarity = drums ? 0.55f
                             : (request.mode == Mode::Bass ? 0.70f
                             : (request.mode == Mode::Harmony ? 0.40f : 0.65f));
                if (request.replaceClip)
                    similarity = juce::jmin(0.95f, similarity + 0.25f);
            }

            const bool hasSeed = std::count_if(
                chunkSeed.notes.begin(), chunkSeed.notes.end(),
                [] (const magenta::Note& n) { return n.startStep < 64.0; }) >= 2;

            const std::uint32_t chunkSeedValue = candidateSeed
                                               + (std::uint32_t) c * 104729u;
            const float chunkTemperature = juce::jlimit(
                0.1f, 1.8f, temperature + 0.03f * (float) juce::jmin(c, 4));

            magenta::Sequence generated;
            if (hasSeed)
                generated = model.encodeDecode(chunkSeed, chunkTemperature,
                                               similarity, chunkSeedValue);
            else
                generated = model.sample(chunkTemperature, chunkSeedValue);

            if (generated.notes.empty())
                continue;

            const int baseStep = chunkStartBar * kStepsPerBar;
            const int limitStep = (chunkStartBar + chunkBars) * kStepsPerBar;
            for (const auto& n : generated.notes)
            {
                const double absoluteStep = n.startStep + (double) baseStep;
                if (absoluteStep >= (double) limitStep)
                    continue;
                if (absoluteStep * stepSeconds >= analysis.lengthSeconds)
                    continue;
                const double startSeconds = absoluteStep * stepSeconds;
                GeneratedNote g;
                g.pitch = n.pitch;
                g.startSeconds = startSeconds;
                g.lengthSeconds = juce::jmax(
                    0.02,
                    juce::jmin(n.lengthSteps, (double) limitStep - absoluteStep)
                        * stepSeconds);
                g.lengthSeconds = juce::jmin(
                    g.lengthSeconds,
                    juce::jmax(0.02, analysis.lengthSeconds - startSeconds));
                g.velocity = n.velocity;
                g.isDrum = n.isDrum;
                candidate.notes.push_back(g);
            }
        }

        if (candidate.notes.empty())
            continue;

        const double score = candidateScore(candidate, analysis, request, intent);
        if (score > bestScore)
        {
            bestScore = score;
            best = std::move(candidate);
        }
    }

    if (best.notes.empty())
    {
        result.error = "the model returned an empty clip";
        return result;
    }
    result = std::move(best);
    result.ok = true;
    return result;
}

Result Service::generateProcedural(const Analysis& analysis, const Request& request,
                                   const Intent& intent, std::uint32_t seed) const
{
    Result result;
    result.provider = "Context generator (built-in)";
    result.key = keyName(analysis.root, analysis.minor);
    result.seed = (int) seed;
    result.clipLengthSeconds = analysis.lengthSeconds;
    result.ok = true;

    const double stepSeconds = analysis.barSeconds / kStepsPerBar;
    const auto intervals = scaleIntervals(analysis.minor, false);
    const auto range = rangeFor(request.mode, analysis.medianPitch, intent.octaveShift);
    const int root = analysis.root;
    const auto scalePitch = [&] (int degree) {
        const int oct = (int) std::floor((double) degree / (double) intervals.size());
        const int idx = ((degree % (int) intervals.size()) + (int) intervals.size())
                        % (int) intervals.size();
        return root + intervals[(std::size_t) idx] + 12 * oct;
    };

    std::mt19937 rng(seed);

    if (request.mode == Mode::Drums)
    {
        result.provider += " (drums)";
        const bool dense = densityTarget(analysis, intent) > 3.5;
        const bool sparse = densityTarget(analysis, intent) < 1.5;
        for (int bar = 0; bar < request.bars; ++bar)
        {
            const int base = bar * kStepsPerBar;
            auto add = [&] (int step, int pitch, float vel) {
                GeneratedNote n;
                n.pitch = pitch;
                n.startSeconds = (base + step) * stepSeconds;
                n.lengthSeconds = stepSeconds;
                n.velocity = vel;
                n.isDrum = true;
                result.notes.push_back(n);
            };
            add(0, 36, 0.98f);
            if (! sparse) add(8, 36, 0.9f);
            if (dense) { add(6, 36, 0.78f); add(10, 36, 0.8f); }
            add(4, 38, 0.92f);
            add(12, 38, 0.9f);
            const int hatStride = sparse ? 4
                                 : ((dense || (bar / 4) % 2 == 1) ? 1 : 2);
            for (int s = 0; s < kStepsPerBar; s += hatStride)
                add(s, (s % 4 == 0) ? 46 : 42, (s % 4 == 0) ? 0.62f : 0.5f);
            const bool phraseFill = (bar % 4 == 3 && (! sparse || bar % 8 == 7))
                                 || (bar == request.bars - 1);
            if (phraseFill)
            {
                add(13, 45, 0.8f);
                add(14, 48, 0.85f);
                add(15, 50, 0.9f);
            }
        }
        result.chords = "drums";
        return result;
    }

    const int center = (range.low + range.high) / 2;
    const bool sparse = densityTarget(analysis, intent) < 1.7;
    const bool dense = densityTarget(analysis, intent) > 3.8;

    if (request.mode == Mode::Bass)
    {
        for (int bar = 0; bar < request.bars; ++bar)
        {
            const int barRoot = chordRootAt(analysis, bar);
            const int base = bar * kStepsPerBar;
            const std::vector<int> pattern = sparse ? std::vector<int>{ 0, 8 }
                : dense ? std::vector<int>{ 0, 2, 4, 6, 8, 10, 12, 14 }
                        : std::vector<int>{ 0, 4, 8, 12 };
            for (std::size_t i = 0; i < pattern.size(); ++i)
            {
                int p = barRoot;
                while (p < 28) p += 12;
                while (p > 48) p -= 12;
                if (i % 2 == 1)
                {
                    p += 7; // fifth
                    while (p > 55) p -= 12;
                }
                GeneratedNote n;
                n.pitch = juce::jlimit(0, 127, p);
                n.startSeconds = (base + pattern[i]) * stepSeconds;
                n.lengthSeconds = ((i + 1 < pattern.size())
                                       ? pattern[i + 1] - pattern[i]
                                       : kStepsPerBar - pattern[i])
                                  * stepSeconds;
                n.velocity = pattern[i] % 4 == 0 ? 0.9f : 0.72f;
                result.notes.push_back(n);
            }

            // A small octave lift at the end of each phrase keeps a long bass
            // line from becoming a static root/fifth loop.
            if (bar % 4 == 3 && ! sparse && pattern.back() < 14)
            {
                int p = barRoot;
                while (p < 28) p += 12;
                while (p > 43) p -= 12;
                p += 12;
                GeneratedNote n;
                n.pitch = juce::jlimit(0, 127, p);
                n.startSeconds = (base + 14) * stepSeconds;
                n.lengthSeconds = 2.0 * stepSeconds;
                n.velocity = 0.8f;
                result.notes.push_back(n);
            }
        }
        result.chords = "";
        return result;
    }

    if (request.mode == Mode::Harmony)
    {
        for (int bar = 0; bar < request.bars; ++bar)
        {
            const int barRoot = chordRootAt(analysis, bar);
            const int quality = chordQualityAt(analysis, bar);
            const auto pcs = chordTonePcs(barRoot, quality);
            const int base = bar * kStepsPerBar;
            const std::vector<int> hits = sparse ? std::vector<int>{ 0 }
                : dense ? std::vector<int>{ 0, 4, 8, 12 }
                        : std::vector<int>{ 0, 8 };
            for (int hit : hits)
            {
                for (int pc : pcs)
                {
                    int p = pc;
                    while (p < range.low) p += 12;
                    while (p > range.high) p -= 12;
                    GeneratedNote n;
                    n.pitch = juce::jlimit(0, 127, p);
                    n.startSeconds = (base + hit) * stepSeconds;
                    n.lengthSeconds = (sparse ? 16 : 8) * stepSeconds;
                    n.velocity = hit == 0 ? 0.82f : 0.68f;
                    result.notes.push_back(n);
                }
            }
        }
        result.chords = "";
        return result;
    }

    // Melody: two short motifs, sequenced across the bars with chord anchors
    // on the downbeats. Phrase B is a variation of A, and every fourth phrase
    // inverts the contour, so a one-minute line develops instead of looping
    // the same bar. Deterministic for a given seed.
    const int motifSteps = sparse ? 2 : (dense ? 4 : 3);
    auto makeMotif = [&] (int count) {
        std::vector<int> degrees;
        for (int i = 0; i < count; ++i)
            degrees.push_back((int) (rng() % 5) - 2);
        return degrees;
    };
    const auto motifA = makeMotif(motifSteps);
    const auto motifB = makeMotif(motifSteps);

    for (int bar = 0; bar < request.bars; ++bar)
    {
        const int barRoot = chordRootAt(analysis, bar);
        const auto pcs = chordTonePcs(barRoot, chordQualityAt(analysis, bar));
        const int base = bar * kStepsPerBar;

        const std::vector<int> hits = sparse
            ? std::vector<int>{ 0, 8 }
            : dense ? std::vector<int>{ 0, 2, 4, 6, 8, 10, 12, 14 }
                    : std::vector<int>{ 0, 4, 8, 12 };

        const int phrase = bar / 4;
        const int pos = bar % 4;
        const auto& motif = (phrase % 2 == 0) ? motifA : motifB;
        const int transposition = ((phrase / 2) % 3) - 1;
        const int contour = (phrase % 4 == 3) ? -1 : 1;

        for (std::size_t i = 0; i < hits.size(); ++i)
        {
            const int step = hits[i];
            int degree = motif[(std::size_t) ((i + (std::size_t) pos)
                                              % motif.size())]
                       + transposition;
            degree *= contour;

            int pitch = center;
            if (step % 4 == 0)
            {
                // Strong beat: nearest chord tone to the current contour.
                pitch = scalePitch(degree + (bar % 2 == 0 ? 0 : 1));
                pitch = chordTone(pcs, pitch, pitch);
            }
            else
            {
                pitch = scalePitch(degree + (bar % 2 == 0 ? 1 : 0));
            }
            while (pitch < range.low) pitch += 12;
            while (pitch > range.high) pitch -= 12;

            GeneratedNote n;
            n.pitch = juce::jlimit(0, 127, pitch);
            n.startSeconds = (base + step) * stepSeconds;
            n.lengthSeconds = ((i + 1 < hits.size()) ? hits[i + 1] - step
                                                     : kStepsPerBar - step)
                              * stepSeconds;
            n.velocity = step % 4 == 0 ? 0.9f : 0.72f;
            result.notes.push_back(n);
        }
    }

    result.chords = "";
    return result;
}

void Service::postProcess(Result& result, const Analysis& analysis,
                          const Request& request, const Intent& intent) const
{
    result.clipLengthSeconds = analysis.lengthSeconds;

    if (request.mode == Mode::Drums)
    {
        const double stepSeconds = analysis.barSeconds / kStepsPerBar;
        for (auto& n : result.notes)
        {
            const int step = (int) std::lround(n.startSeconds / stepSeconds);
            n.velocity = step % 4 == 0 ? 0.95f : (step % 2 == 0 ? 0.72f : 0.58f);
            if (intent.swing && (step % 2 != 0))
                n.startSeconds += stepSeconds * 0.18;
            n.startSeconds = juce::jlimit(
                0.0, juce::jmax(0.0, result.clipLengthSeconds - 0.01),
                n.startSeconds);
            n.lengthSeconds = juce::jlimit(
                0.02,
                juce::jmax(0.02, result.clipLengthSeconds - n.startSeconds),
                n.lengthSeconds);
        }
        result.chords = "drums";
        return;
    }

    const double stepSeconds = analysis.barSeconds / kStepsPerBar;
    const int keyRoot = intent.keySpecified ? intent.root : analysis.root;
    const bool keyMinor = intent.keySpecified ? intent.minor : analysis.minor;
    const auto intervals = scaleIntervals(keyMinor, keyMinor && intent.forceMinor);

    // 1) Bring the model's output into the project key. The VAE is not
    // conditioned on key, so transposing the result is what makes it sit with
    // the rest of the arrangement.
    std::vector<AbsNote> asAbs;
    for (const auto& n : result.notes)
    {
        if (n.isDrum) continue;
        AbsNote a;
        a.pitch = n.pitch;
        a.start = n.startSeconds;
        a.end = n.startSeconds + n.lengthSeconds;
        a.velocity = n.velocity;
        asAbs.push_back(a);
    }
    if (asAbs.size() >= 3 && ! result.provider.startsWith("Context generator"))
    {
        int outRoot = 0;
        bool outMinor = false;
        float outConfidence = 0.0f;
        estimateKey(asAbs, analysis.barSeconds, outRoot, outMinor, outConfidence);
        int delta = (keyRoot - outRoot) % 12;
        if (delta > 6) delta -= 12;
        if (delta < -6) delta += 12;
        if (delta != 0)
            for (auto& n : result.notes)
                if (! n.isDrum)
                    n.pitch = juce::jlimit(0, 127, n.pitch + delta);
    }

    // 2) Key snap. Explicit keys snap harder than inferred ones.
    const float snapProbability = intent.keySpecified ? 0.9f : 0.55f;
    std::mt19937 rng((std::uint32_t) result.seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& n : result.notes)
    {
        if (n.isDrum)
            continue;
        if (! inScale(n.pitch, keyRoot, intervals)
            && dist(rng) < snapProbability)
            n.pitch = juce::jlimit(0, 127, snapToScale(n.pitch, keyRoot, intervals));
    }

    // 3) Register.
    fitRegister(result.notes, rangeFor(request.mode, analysis.medianPitch,
                                       intent.octaveShift));

    // 3.5) Long phrases follow the bar-by-bar harmony.
    anchorStrongBeatsToChords(result.notes, analysis, request, rng, stepSeconds);

    // 4) Harmony / arpeggio.
    if (intent.arpeggio)
        makeArpeggio(result, analysis, request, intent, stepSeconds);
    else if (request.harmonize || intent.harmonize
             || request.mode == Mode::Harmony)
        harmonize(result, analysis, request, intent, stepSeconds);

    // 5) Density / groove.
    if (intent.densityBias < -0.1f)
    {
        std::vector<GeneratedNote> filtered;
        for (const auto& n : result.notes)
        {
            const int step = (int) std::lround(n.startSeconds / stepSeconds);
            if (step % 4 == 0 || dist(rng) < 0.45f)
                filtered.push_back(n);
        }
        result.notes = std::move(filtered);
    }
    else if (intent.densityBias > 0.1f && request.mode != Mode::Harmony)
    {
        std::vector<GeneratedNote> extra;
        for (const auto& n : result.notes)
        {
            if (n.isDrum)
                continue;
            const int step = (int) std::lround(n.startSeconds / stepSeconds);
            if (step % 2 == 0 && dist(rng) < 0.4f)
            {
                GeneratedNote g = n;
                g.pitch = snapToScale(n.pitch + (dist(rng) < 0.5f ? 2 : -2),
                                      keyRoot, intervals);
                g.startSeconds = n.startSeconds + stepSeconds;
                g.lengthSeconds = stepSeconds;
                g.velocity = juce::jlimit(0.3f, 0.7f, n.velocity * 0.65f);
                extra.push_back(g);
            }
        }
        result.notes.insert(result.notes.end(), extra.begin(), extra.end());
    }

    if (intent.swing)
        for (auto& n : result.notes)
        {
            const int step = (int) std::lround(n.startSeconds / stepSeconds);
            if (step % 2 != 0)
                n.startSeconds += stepSeconds * 0.18;
        }

    // 6) Velocities and final bounds.
    for (auto& n : result.notes)
    {
        if (n.isDrum)
            continue;
        const int step = (int) std::lround(n.startSeconds / stepSeconds);
        if (step % 16 == 0)
            n.velocity = juce::jmax(n.velocity, 0.88f);
        else if (step % 4 == 0)
            n.velocity = juce::jmax(n.velocity, 0.74f);
        n.velocity = juce::jlimit(0.25f, 1.0f, n.velocity);
        const double maxStart = juce::jmax(0.0, result.clipLengthSeconds - 0.01);
        n.startSeconds = juce::jlimit(0.0, maxStart, n.startSeconds);
        const double maxLength = juce::jmax(0.02, result.clipLengthSeconds - n.startSeconds);
        n.lengthSeconds = juce::jlimit(0.02, maxLength, n.lengthSeconds);
    }

    std::sort(result.notes.begin(), result.notes.end(),
              [] (const GeneratedNote& a, const GeneratedNote& b) {
                  if (std::abs(a.startSeconds - b.startSeconds) > 1.0e-9)
                      return a.startSeconds < b.startSeconds;
                  return a.pitch < b.pitch;
              });

    juce::StringArray chords;
    const bool flats = keyUsesFlats(analysis.root, analysis.minor);
    const int shownBars = juce::jmin(request.bars, 8);
    for (int bar = 0; bar < shownBars; ++bar)
        chords.add(chordName(chordRootAt(analysis, bar),
                             chordQualityAt(analysis, bar), flats));
    if (request.bars > shownBars)
        chords.add("…");
    result.chords = chords.joinIntoString(" | ");
}

Result Service::generate(const Project& project, const Request& request)
{
    Result result;
    result.seed = request.seed != 0 ? request.seed
                                    : (int) juce::Time::getMillisecondCounter();

    const auto* track = project.findTrack(request.trackId);
    if (track == nullptr)
    {
        result.error = "track not found";
        return result;
    }
    if (track->type != Track::Type::Instrument)
    {
        result.error = "generation needs an instrument track";
        return result;
    }
    if (request.replaceClip && request.clipId > 0)
    {
        const auto* clip = project.findClip(request.clipId);
        if (clip == nullptr)
        {
            result.error = "clip not found";
            return result;
        }
        if (! clip->isMidi())
        {
            result.error = "only MIDI clips can be regenerated";
            return result;
        }
    }

    auto analysis = analyse(project, request);
    auto intent = interpretPrompt(request.prompt, analysis);

    if (intent.keySpecified)
    {
        analysis.root = intent.root;
        analysis.minor = intent.minor;
        analysis.useDefaultProgression();
    }
    else if (intent.forceMinor)
    {
        // "Darker / minor" over a major context means its relative minor
        // (C minor for Eb major), not "Eb minor", which would fight the
        // material already in the project.
        if (! analysis.minor)
            analysis.root = (analysis.root + 9) % 12;
        analysis.minor = true;
        analysis.useDefaultProgression();
    }
    else if (intent.forceMajor)
    {
        if (analysis.minor)
            analysis.root = (analysis.root + 3) % 12;
        analysis.minor = false;
        analysis.useDefaultProgression();
    }

    const std::uint32_t seed = (std::uint32_t) result.seed;
    Request effective = request;
    effective.bars = analysis.bars;
    effective.lengthSeconds = analysis.lengthSeconds;
    if (intent.hasMode)
        effective.mode = intent.mode;

    magenta::MusicVAE* model = nullptr;
    if (effective.useModel)
    {
        juce::String error;
        model = ensureModel(effective.mode, error);
        if (model == nullptr)
            result.promptNote = "Magenta model not installed (" + error
                                + "). Using the built-in generator.";
    }

    if (model != nullptr)
        result = generateWithModel(*model, project, analysis, effective, intent, seed);
    else
        result = generateProcedural(analysis, effective, intent, seed);

    if (! result.ok)
        return result;

    result.key = keyName(analysis.root, analysis.minor);
    result.startSeconds = analysis.anchor;
    if (intent.note.isNotEmpty())
        result.promptNote = (result.promptNote.isEmpty() ? "" : result.promptNote + " ")
                          + "Prompt: " + intent.note;

    juce::StringArray contextBits;
    if (analysis.audioUsed)
    {
        juce::String heard = "heard " + juce::String(analysis.audioSeconds, 1)
                           + "s of audio";
        if (analysis.audioKeyUsed)
            heard += " (" + keyName(analysis.audioRoot, analysis.audioMinor) + ")";
        contextBits.add(heard);
    }
    contextBits.add(juce::String((int) analysis.window.size())
                    + " MIDI notes in window");
    contextBits.add(juce::String(analysis.bars) + " bars / "
                    + juce::String(analysis.lengthSeconds, 1) + "s");
    result.contextNote = contextBits.joinIntoString(" · ");

    postProcess(result, analysis, effective, intent);
    result.ok = true;
    return result;
}

} // namespace cadence::ai
