// EZOrquestra — implementation.
//  · scanLibrary: walks an Alpine-Project-style tree and generates one
//    JSON program per (instrument, articulation). Note names in filenames
//    (C4, Bb0, G5) are used directly; files without note names (freesound
//    strings) get their pitch from autocorrelation. Dynamics (_f/_ff/…)
//    become velocity layers; repeated roots become round-robins.
//  · Processor: juce::Synthesiser with custom sampler voices (velocity
//    layers + RR + resampled nearest-sample mapping).
#include "ezorq.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <dirent.h>

#include <map>
#include <memory>

namespace ezorq {
namespace {

static juce::String u8(const char* s) { return juce::String::fromUTF8(s); }

juce::AudioFormatManager& sharedFormats()
{
    static juce::AudioFormatManager m;
    static bool registered = false;
    if (! registered)
    {
        registered = true;
        m.registerBasicFormats();
        m.registerFormat(new juce::OggVorbisAudioFormat(), true);
#ifdef JUCE_USE_MP3AUDIOFORMAT
        m.registerFormat(new juce::MP3AudioFormat(), true);
#endif
    }
    return m;
}

} // namespace

// ---- pitch detection v3: Goertzel scan + harmonic-sum fit ------------------
// Loudest window (attack-skipped for plucks), 1 Hz Goertzel power scan
// 28..2100 Hz, candidate fundamentals from every strong peak / k, scored
// by summed harmonic power. Immune to formant capture and HPS octave
// locks. At ezorq:: scope (declared in ezorq.h): root correction and the
// chromatic audits share this one function, so tests measure with the
// exact accuracy the sampler tunes with.
double detectPitchHzEx(const float* data, int numSamples, double sampleRate,
                       double labelHz, double* covMeasuredOut,
                       double* covLabelOut)
{
    constexpr int N = 8192;
    if (data == nullptr || numSamples < N / 2 || sampleRate < 8000.0)
        return 0.0;

    // Loudest N/2-sample window (skips silence and recording heads).
    const int win = N / 2;
    int bestWin = 0;
    double bestRms = 0.0;
    for (int wstart = 0; wstart + win <= numSamples; wstart += win)
    {
        double r = 0.0;
        for (int i = wstart; i < wstart + win; ++i)
            r += (double) data[i] * data[i];
        if (r > bestRms)
        {
            bestRms = r;
            bestWin = wstart;
        }
    }
    if (bestRms / win < 4e-6)
        return 0.0; // near-silence

    // Pluck/pizz recordings bend SHARP in the attack (string tension) —
    // measure the window ~80 ms after the loudest one instead (the ring,
    // not the hit). Sustains are unaffected (every window reads the same).
    if (bestWin + win <= numSamples - (int) (0.08 * sampleRate))
    {
        const int from = bestWin + (int) (0.08 * sampleRate);
        int best2 = -1;
        double bestRms2 = 0.0;
        for (int wstart = from; wstart + win <= numSamples;
             wstart += win / 2)
        {
            double r = 0.0;
            for (int i = wstart; i < wstart + win; ++i)
                r += (double) data[i] * data[i];
            if (r > bestRms2)
            {
                bestRms2 = r;
                best2 = wstart;
            }
        }
        if (best2 >= 0 && bestRms2 / win > 2e-6)
            bestWin = best2;
    }

    // ---- Robust measurement: Goertzel power scan + harmonic-sum fit ----
    // FFT+HPS failed twice in the field: it locked onto BROAD FORMANTS
    // (bassoon/tuba read +12..+30 st) and octave-up on rich timbres. A
    // true fundamental has ALL its harmonics aligned with real partials;
    // a formant is one broad bump with no harmonic series — so candidate
    // fundamentals are scored by their summed, decay-weighted harmonic
    // power. Self-consistent by construction: root correction AND the
    // chromatic audits use this same function.
    constexpr int kFlo = 28, kFhi = 2100;
    const int nFreq = kFhi - kFlo + 1;
    // Analyse DEEP in the steady state: attacks/transitions carry
    // broadband bumps that spawn phantom sub-harmonic candidates.
    if (bestWin + (int) (0.15 * sampleRate) + (1 << 14) <= numSamples)
        bestWin += (int) (0.15 * sampleRate);
    int anaStart = bestWin;
    int anaLen = juce::jmin(1 << 14, numSamples - anaStart);
    if (anaLen < 1 << 13)
    {
        anaStart = juce::jmax(0, numSamples - (1 << 14));
        anaLen = juce::jmin(1 << 14, numSamples - anaStart);
    }
    if (anaLen < 4096)
        return 0.0;

    std::vector<float> pw((size_t) nFreq);
    for (int fi = 0; fi < nFreq; ++fi)
    {
        const double f = (double) (kFlo + fi);
        const double coeff = 2.0
                             * std::cos(2.0
                                        * juce::MathConstants<double>::pi
                                        * f / sampleRate);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < anaLen; ++i)
        {
            const double s0 = (double) data[anaStart + i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        pw[(size_t) fi] = (float) (s1 * s1 + s2 * s2 - coeff * s1 * s2);
    }

    // Noise-floor subtraction is what makes the harmonic sum unbiased:
    // a SUB-harmonic candidate (f0/2) otherwise wins on weak-fundamental
    // sources by collecting NOISE in its odd slots — with the floor at
    // ~0, score(f0) is exactly double score(f0/2) and can never lose.
    {
        std::vector<float> sorted(pw);
        const size_t mid = sorted.size() / 2;
        std::nth_element(sorted.begin(), sorted.begin() + (long) mid,
                         sorted.end());
        const float floorV = sorted[mid] * 1.5f;
        for (int i = 0; i < nFreq; ++i)
            pw[(size_t) i] = juce::jmax(0.f, pw[(size_t) i] - floorV);
    }

    float pmax = 0.f;
    for (int i = 0; i < nFreq; ++i)
        pmax = juce::jmax(pmax, pw[(size_t) i]);
    if (pmax <= 0.f)
        return 0.0;

    // Peak picking: local maxima over a small noise floor.
    std::vector<std::pair<double, double>> peaks;
    for (int i = 1; i + 1 < nFreq; ++i)
        if (pw[(size_t) i] > pw[(size_t) (i - 1)]
            && pw[(size_t) i] >= pw[(size_t) (i + 1)]
            && pw[(size_t) i] > pmax * 0.02f)
            peaks.push_back({ (double) (kFlo + i), (double) pw[(size_t) i] });
    std::sort(peaks.begin(), peaks.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (peaks.size() > 16)
        peaks.resize(16);

    auto scoreAt = [&](double f0) {
        // EXPLANATORY COVERAGE: a true fundamental explains EVERY strong
        // observed peak as one of its harmonics. A 2f0-dominant spectrum
        // (flutes: weak f0, strong 2nd harmonic) can only be claimed by
        // f0 — the 2f0 candidate leaves the odd partials unexplained. A
        // broad formant explains itself but not the series. Unexplained
        // peak power is penalised half-weight.
        const double tol = juce::jmax(4.0, f0 * 0.015);
        double covered = 0.0, unexplained = 0.0;
        for (const auto& pk : peaks)
        {
            const double h = juce::jmax(1.0, std::round(pk.first / f0));
            if (std::abs(pk.first - h * f0) <= tol)
                covered += pk.second;
            else
                unexplained += pk.second;
        }
        return covered - 0.5 * unexplained;
    };

    // Candidates: each strong peak divided by k = a possible fundamental.
    std::vector<double> cand;
    for (const auto& pk : peaks)
        for (int k = 1; k <= 6; ++k)
        {
            const double f0 = pk.first / (double) k;
            if (f0 < (double) kFlo || f0 > 1600.0)
                continue;
            bool dup = false;
            for (const double c : cand)
                if (std::abs(c - f0) < 4.0)
                {
                    dup = true;
                    break;
                }
            if (! dup)
                cand.push_back(f0);
        }
    if (cand.empty())
        return 0.0;

    double bestF = cand[0], bestS = scoreAt(bestF);
    for (const double c : cand)
    {
        const double s = scoreAt(c);
        // NOTE: no tie toward higher octaves — weak-f0 spectra (flutes)
        // make f0/2 explain a superset, but the coverage comparison in
        // the ROOT CORRECTION (covRatio vs the label) is what rejects
        // detector octave locks; preferring higher here re-broke them.
        if (s > bestS)
        {
            bestS = s;
            bestF = c;
        }
    }

    // Label-vs-measured coverage: how much peak power each series explains.
    // A ratio >= 1.3 means the label's series is NOT what's in the file
    // (real mislabel) while a ratio near 1 means the measured candidate
    // merely explained the label's own partials (detector octave lock).
    if (covMeasuredOut != nullptr)
        *covMeasuredOut = scoreAt(bestF);
    if (covLabelOut != nullptr)
        *covLabelOut = labelHz > 0.0 ? scoreAt(labelHz) : 0.0;

    // Parabolic refinement on the score curve (points 1.5 Hz apart).
    const double s0 = scoreAt(bestF - 1.5), s1v = scoreAt(bestF),
                 s2v = scoreAt(bestF + 1.5);
    const double denom = s0 - 2.0 * s1v + s2v;
    if (std::abs(denom) > 1e-12)
        bestF += 1.5 * 0.5 * (s0 - s2v) / denom;

    const double hz = juce::jlimit((double) kFlo, (double) kFhi, bestF);
    return hz;
}

double detectPitchHz(const float* data, int numSamples, double sampleRate)
{
    return detectPitchHzEx(data, numSamples, sampleRate, 0.0, nullptr,
                           nullptr);
}

namespace {

int noteFromHz(double hz)
{
    return (int) std::lround(69.0 + 12.0 * std::log2(hz / 440.0));
}

int pcFromName(char letter)
{
    switch (letter)
    {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
        default: return -1;
    }
}

// Parses "C4", "Bb0", "F#3" → MIDI note, or -1.
int noteFromToken(const juce::String& tok)
{
    static const std::map<juce::String, int> pc = {
        {"c", 0},  {"c#", 1},  {"db", 1}, {"d", 2},  {"d#", 3}, {"eb", 4},
        {"e", 4},  {"f", 5},   {"f#", 6}, {"gb", 7}, {"g", 7},  {"g#", 8},
        {"ab", 8}, {"a", 9},   {"a#", 10},{"bb", 10},{"b", 11}};
    const juce::String t = tok.trim().toLowerCase();
    if (t.length() < 2 || t.length() > 4)
        return -1;
    int digitPos = -1;
    for (int i = 1; i < t.length(); ++i)
        if (juce::CharacterFunctions::isDigit(t[i]))
        {
            digitPos = i;
            break;
        }
    if (digitPos <= 0)
        return -1;
    const juce::String letters = t.substring(0, digitPos);
    const int octave = t.substring(digitPos).getIntValue();
    const auto it = pc.find(letters);
    if (it == pc.end() || octave < -1 || octave > 9)
        return -1;
    return (octave + 1) * 12 + it->second;
}

// Standard orchestral low notes (MIDI) — used to anchor numbered sample
// sets: a chromatic run starting at the instrument's lowest note.
int instrumentRangeLow(const juce::String& instrumentLower)
{
    struct P { const char* key; int low; };
    static const P table[] = {
        { "piccolo", 72 }, { "flute", 60 }, { "oboe", 58 },
        { "bass clarinet", 46 }, { "clarinet", 50 }, { "bassoon", 34 },
        { "e horn", 47 }, { "horn", 47 }, { "bass trombone", 34 },
        { "trombone", 40 }, { "tuba", 28 }, { "trumpet", 52 },
        { "sax", 48 }, { "violin", 55 }, { "viola", 48 },
        { "cello", 36 }, { "basses", 28 }, { "bass", 28 },
    };
    for (const auto& p : table)
        if (instrumentLower.contains(p.key))
            return p.low;
    return -1;
}

// Transposing-instrument tables are NOT applied here: sample libraries are
// labelled in CONCERT pitch (a recording has no "written" pitch — that
// concept belongs to notation import, not to sample mapping). Applying
// them shifted every horn/clarinet pack exactly a fifth/second off.

int velocityFromDynamic(const juce::String& dyn)
{
    if (dyn == "pp") return 24;
    if (dyn == "p") return 48;
    if (dyn == "mp") return 72;
    if (dyn == "mf") return 88;
    if (dyn == "f") return 104;
    if (dyn == "ff") return 120;
    return 100;
}

// Articulation guess from the path/name tokens.
juce::String articulationFrom(const juce::String& lowerPath)
{
    if (lowerPath.contains("pizz")) return "Pizzicato";
    if (lowerPath.contains("stacc")) return "Staccato";
    if (lowerPath.contains("sustain") || lowerPath.contains("long")
        || lowerPath.contains("sust"))
        return "Sustain";
    if (lowerPath.contains("non-vib") || lowerPath.contains("nonvib"))
        return "Longs";
    if (lowerPath.contains("vib")) return "Vibrato";
    if (lowerPath.contains("trem")) return "Tremolo";
    if (lowerPath.contains("harmon")) return "Harmon mute";
    if (lowerPath.contains("straight")) return "Straight mute";
    return "Longs";
}

struct SampleInfo
{
    juce::File file;
    int note = -1;
    int vel = 100;
    juce::String rrKey;
    int index = -1;      // trailing number in the filename, if any
    bool noteFromName = false;
    float peakRms = 0.f; // reliability of the pitch detection
};

struct Program
{
    juce::String family, instrument, artic;
    std::vector<SampleInfo> samples;

    juce::String name() const
    {
        return instrument + " - " + artic;
    }
};

} // namespace

int scanLibrary(const juce::File& root, const juce::File& outDir)
{
    outDir.createDirectory();
    // NOTE: never wipe the dir here — tests scan into the REAL programs
    // dir, and wiping it would delete the user's programs. Programs not
    // regenerated this pass simply stay.
    auto& formats = sharedFormats();

    // family directories: anything containing Strings/Brass/Woodwinds.
    juce::Array<juce::File> familyDirs;
    for (const auto& d : root.findChildFiles(juce::File::findDirectories,
                                             false, "*"))
    {
        const auto n = d.getFileName().toLowerCase();
        if (n.contains("string") || n.contains("brass")
            || n.contains("woodwind"))
            familyDirs.add(d);
    }

    std::map<juce::String, Program> programs;
    int detected = 0, named = 0;
    float detectRms_ = 0.f;

    for (const auto& famDir : familyDirs)
    {
        const juce::String family = famDir.getFileName().contains("String")
                                        ? "Cuerdas"
                                    : famDir.getFileName().contains("Brass")
                                        ? "Metal"
                                        : "Viento";

        for (const auto& f : famDir.findChildFiles(
                 juce::File::findFiles, true, "*.wav;*.ogg;*.aiff"))
        {
            const auto lowerPath = f.getRelativePathFrom(root).toLowerCase();
            // Skip non-sample clutter.
            if (lowerPath.contains(".DS_"))
                continue;

            const juce::String stem = f.getFileNameWithoutExtension();
            // Alpine-style names separate with SPACES ("FL FF SUS 002"),
            // others with _/- — tokenize with all three.
            const auto tokens = juce::StringArray::fromTokens(stem, "_- ", "");

            // Note: explicit note token (C4 / Bb0 / F#3) or pitch detection.
            // A bare trailing NUMBER token is kept as a chromatic index
            // ("FL FF NOVIB 022" → 22) — packs number their samples
            // consecutively; the real note is calibrated per program below.
            int note = -1, index = -1;
            for (const auto& tok : tokens)
            {
                const int n = noteFromToken(tok);
                if (n >= 0 && n <= 108)
                {
                    note = n;
                    named++;
                    break;
                }
            }
            for (int ti = (int) tokens.size() - 1; ti >= 0; --ti)
            {
                const juce::String t = tokens[ti];
                if (t.length() >= 2 && t.length() <= 3
                    && t.containsOnly("0123456789"))
                {
                    index = t.getIntValue();
                    break;
                }
            }
            if (note < 0)
            {
                auto* reader = formats.createReaderFor(f);
                if (reader == nullptr)
                    continue;
                const double sr = reader->sampleRate;
                juce::AudioBuffer<float> buf(
                    juce::jmin(2, (int) reader->numChannels),
                    (int) juce::jmin((juce::int64) (sr * 1.0),
                                     reader->lengthInSamples));
                reader->read(&buf, 0, buf.getNumSamples(), 0, true, true);
                delete reader;
                float peakRms = 0.f;
                for (int c = 0; c < buf.getNumChannels(); ++c)
                    for (int i = 0; i < buf.getNumSamples(); ++i)
                        peakRms = juce::jmax(peakRms,
                                             std::abs(buf.getSample(c, i)));
                const double hz =
                    detectPitchHz(buf.getReadPointer(0), buf.getNumSamples(),
                                  sr);
                if (hz <= 0)
                    continue;
                note = juce::jlimit(12, 108, noteFromHz(hz));
                detected++;
                detectRms_ = peakRms;
                std::fprintf(stderr, "[ezorq] detectado %s -> nota %d (%.1f Hz)\n",
                             f.getFileName().toRawUTF8(), note, hz);
            }

            // Dynamics token (_ff/_f/_p/_pp/_mf/_mp) if present.
            int vel = 100;
            for (const auto& tok : tokens)
            {
                const juce::String d = tok.toLowerCase();
                if (d == "pp" || d == "p" || d == "mp" || d == "mf"
                    || d == "f" || d == "ff")
                {
                    vel = velocityFromDynamic(d);
                    break;
                }
            }

            // Articulation from path tokens; instrument = the file's parent
            // folder ("Trumpet 1 Samples" → "Trumpet 1", "Violins" → …).
            const juce::String artic = articulationFrom(lowerPath);
            juce::String instrument;
            {
                auto dir = f.getParentDirectory();
                juce::String dn = dir.getFileName();
                if (dn.toLowerCase().contains("sample")
                    || dn.equalsIgnoreCase("Instruments")
                    || dn.toLowerCase().contains("muted"))
                {
                    dn = dir.getParentDirectory().getFileName();
                }
                instrument = dn.replace(" Samples", "").trim();
                if (instrument.equalsIgnoreCase(famDir.getFileName())
                    || instrument.isEmpty())
                    instrument = family;
            }

            const juce::String key =
                family + "|" + instrument + "|" + artic;
            auto& prog = programs[key];
            prog.family = family;
            prog.instrument = instrument;
            prog.artic = artic;
            prog.samples.push_back({ f, note, vel, stem, index,
                                     note >= 0, detectRms_ });
        }
    }

    int written = 0;
    for (auto& [key, prog] : programs)
    {
        if (prog.samples.size() < 2)
            continue;

        // Chromatic-number calibration: for packs numbering their samples
        // consecutively ("… 002/003/004"), autocorrelation has ±1-semitone
        // and octave errors — the FILE index is the truth, detection only
        // calibrates the group's base offset (median = outlier-proof).
        {
            // Numbered chromatic sets: the pack's trailing number indexes
            // a chromatic run. Anchor the base on the LOUDEST sample's
            // detected pitch (best SNR), snapped to the instrument's
            // standard octave. Pure-detection or pure-prior both drift;
            // the combination is exact.
            int minIndex = 1 << 30;
            std::vector<std::tuple<float, int, int>> detections; // rms, det, idx
            for (const auto& smp : prog.samples)
            {
                if (smp.index >= 0)
                    minIndex = juce::jmin(minIndex, smp.index);
                if (smp.index >= 0 && ! smp.noteFromName && smp.note >= 0)
                    detections.push_back({ smp.peakRms, smp.note,
                                           smp.index });
            }
            int base = 1 << 30;
            const int prior = instrumentRangeLow(
                prog.instrument.toLowerCase());
            if (minIndex < (1 << 30) && ! detections.empty())
            {
                std::sort(detections.begin(), detections.end(),
                          [](const auto& a, const auto& b) {
                              return std::get<0>(a) > std::get<0>(b);
                          });
                // detections[0] = (rms, detectedMidi, fileIndex).
                base = std::get<1>(detections[0])
                       - std::get<2>(detections[0]);
                if (prior >= 0)
                {
                    // Octave snap: the anchor may be an octave off; the
                    // prior knows the right octave, not the semitone.
                    while (minIndex + base > prior + 6)
                        base -= 12;
                    while (minIndex + base < prior - 6)
                        base += 12;
                }
            }
            else if (prior >= 0 && minIndex < (1 << 30))
            {
                base = prior - minIndex;
            }
            else if (detections.size() >= 3)
            {
                std::vector<int> offs;
                for (const auto& d : detections)
                    offs.push_back(std::get<1>(d) - std::get<2>(d));
                std::sort(offs.begin(), offs.end());
                base = offs[(size_t) (offs.size() / 2)];
            }
            if (base != (1 << 30))
            if (base != (1 << 30))
                for (auto& smp : prog.samples)
                    if (smp.index >= 0)
                        smp.note = smp.index + base;
        }

        // Sanity gate: a real instrument spans <= 4 octaves inside MIDI
        // 12..120. Anything else means the naming guess failed (mixed
        // sources, non-chromatic indices) — random-note salvos.
        {
            int lo = 1 << 30, hi = -1;
            for (const auto& smp : prog.samples)
            {
                lo = juce::jmin(lo, smp.note);
                hi = juce::jmax(hi, smp.note);
            }
            if (hi > 120 || lo < 12 || (hi - lo) > 48)
            {
                juce::Logger::writeToLog("[ezorq] descartado " + key
                                         + " (mapeo no fiable)");
                continue;
            }
        }
        std::sort(prog.samples.begin(), prog.samples.end(),
                  [](const SampleInfo& a, const SampleInfo& b) {
                      if (a.note != b.note)
                          return a.note < b.note;
                      return a.vel < b.vel;
                  });
        auto x = std::make_unique<juce::XmlElement>("program");
        x->setAttribute("family", prog.family);
        x->setAttribute("instrument", prog.instrument);
        x->setAttribute("artic", prog.artic);
        auto* ss = new juce::XmlElement("samples");
        for (const auto& s : prog.samples)
        {
            auto* e = new juce::XmlElement("s");
            e->setAttribute("f", s.file.getFullPathName());
            e->setAttribute("n", s.note);
            e->setAttribute("v", s.vel);
            e->setAttribute("rr", s.rrKey);
            ss->addChildElement(e);
        }
        x->addChildElement(ss);
        auto writeProgram = [&](const juce::File& out, bool shortVariant) {
            if (shortVariant)
                x->setAttribute("short", 1);
            else
                x->removeAttribute("short");
            if (out.existsAsFile())
                out.deleteFile();
            auto stream = out.createOutputStream();
            if (stream != nullptr)
            {
                stream->writeText(x->toString(), false, false, "\n");
                return true;
            }
            return false;
        };

        juce::String fname = prog.family + "_" + prog.instrument + "_"
                             + prog.artic;
        fname = fname.replace(" ", "_") + ".json";
        if (writeProgram(outDir.getChildFile(fname), false))
            written++;

        // Alpine's script plays sustains SHORT via fast envelopes — provide
        // the synthetic staccato variant for every long articulation.
        if (prog.artic == "Longs" || prog.artic == "Sustain"
            || prog.artic == "Vibrato")
        {
            juce::String sname = prog.family + "_" + prog.instrument + "_St";
            sname = sname.replace(" ", "_") + ".json";
            auto xs = std::make_unique<juce::XmlElement>(*x);
            if (writeProgram(outDir.getChildFile(sname), true))
                written++;
        }
    }

    juce::Logger::writeToLog(
        "[ezorq] scan: " + juce::String(programs.size())
        + " programas, " + juce::String(written) + " escritos ("
        + juce::String(named) + " por nombre, " + juce::String(detected)
        + " por detección)");
    return written;
}

// ---- Runtime: processor ----------------------------------------------------
namespace {

// Synthesiser::noteOn only starts voices for registered sounds — a bare
// marker sound unlocks our custom voices (the same gotcha as EZInstruments).
struct AnySound : juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

struct OrqSound
{
    juce::String name;
    std::unique_ptr<juce::AudioBuffer<float>> data;
    double sampleRate = 48000.0;
    int onset = 0; // first audible sample (field recordings have head noise)
    int rootNote = 60;
    // Fine tuning from the measured pitch (Kontakt-style root + tune):
    // the sample is stretched this many cents so a pressed note sounds
    // EXACTLY in tune even when the recording sits a fraction of a
    // semitone off (field packs drift ±50 cents).
    float tuneCents = 0.f;
    int keyLo = 0, keyHi = 127;
    int velLo = 0, velHi = 127;
    juce::String rrKey;
    bool shortMode = false;
};

using Snd = std::shared_ptr<OrqSound>;
using SndList = std::vector<Snd>;

// The published sound table. Voices copy the list (cheap shared_ptr
// copies) at note-start; a program swap therefore never frees a sound a
// voice is still playing — the old list dies with its last user.
struct TableRef
{
    juce::SpinLock lock;
    std::shared_ptr<const SndList> list;
    const void* lista() const { return list.get(); }

    SndList copy() const
    {
        juce::SpinLock::ScopedLockType l(lock);
        return list ? *list : SndList {};
    }
    void publish(SndList&& v)
    {
        auto p = std::make_shared<const SndList>(std::move(v));
        juce::SpinLock::ScopedLockType l(lock);
        list = std::move(p);
    }
};

// Decode-once program cache shared by every instance (live + offline
// renders). Keyed by program file name.
static std::mutex g_cacheMtx;
static std::map<juce::String, std::shared_ptr<const SndList>> g_programCache;

// Test hook: when >= 0, every new instance loads this program instead of
// the default flute (see setProbeProgram in ezorq.h).
static std::atomic<int> g_probeProgram { -1 };

struct OrqVoice : juce::SynthesiserVoice
{
    bool canPlaySound(juce::SynthesiserSound*) override { return true; }

    void startNote(int midiNote, float velocity,
                   juce::SynthesiserSound*, int) override
    {
        if (tables_ == nullptr)
        {
            active_ = false;
            return;
        }
        local_ = tables_->copy(); // own the sounds for this note's lifetime
        current_ = pick(local_, midiNote, (int) (velocity * 127.f));
        if (current_ == nullptr)
        {
            active_ = false;
            clearCurrentNote();
            return;
        }
        // Nearest-sample resampling ratio: note interval, fine tuning in
        // cents, plus the file's rate converted INTO the engine clock — a
        // 48 kHz file must be consumed FASTER (48000/44100 frames per
        // output sample), not slower; the inverted factor played
        // everything ~3 semitones flat.
        ratio_ = std::pow(2.0, (midiNote - current_->rootNote) / 12.0
                                + current_->tuneCents / 1200.0)
                 * current_->sampleRate / sampleRate_;
        std::fprintf(stderr,
                     "[ezorq] nota %d vel=%d -> %s root=%d cents=%+.0f "
                     "onset=%d len=%d keys=%d-%d velr=%d-%d\n",
                     midiNote, (int) (velocity * 127.f),
                     current_->name.toRawUTF8(), current_->rootNote,
                     current_->tuneCents, current_->onset,
                     current_->data->getNumSamples(), current_->keyLo,
                     current_->keyHi, current_->velLo, current_->velHi);
        note_ = midiNote;
        setKeyBit(true);
        const double hz = 440.0
                          * std::pow(2.0, (midiNote - 69) / 12.0);
        pos_ = current_->onset;
        active_ = true;
        releasing_ = false;
        // ADSR: attack ramps 0→1 linearly, decay settles exponentially on
        // the sustain level, release fades from wherever the env is. Short
        // (staccato) programs override the sustain target to 0 with a fast
        // tau — the recording dies like a real staccato take.
        if (adsr_ != nullptr)
        {
            const float atk = juce::jmax(0.001f, (*adsr_)[0].load());
            const float dec = juce::jmax(0.005f, (*adsr_)[1].load());
            const float sus = juce::jlimit(0.f, 1.f, (*adsr_)[2].load());
            const float rel = juce::jmax(0.005f, (*adsr_)[3].load());
            env_ = 0.f;
            envPhase_ = 0; // attack
            atkRate_ = 1.f / (atk * (float) sampleRate_);
            decCoef_ = std::exp(-1.0 / (dec * 0.35 * sampleRate_));
            susTarget_ = current_->shortMode ? 0.f : sus;
            shortCoef_ = current_->shortMode
                             ? std::exp(-1.0 / (0.15 * sampleRate_))
                             : 1.0f;
            releaseCoef_ = std::exp(-1.0 / (rel * sampleRate_));
        }
        else
        {
            env_ = 1.0;
            envPhase_ = 2; // straight sustain
            releaseCoef_ =
                std::exp(-1.0 / (0.08 * juce::jmax(1.0, sampleRate_)));
        }
        // Phase-coherent sustain loop: length = whole periods of the played
        // note (in INPUT frames), so the wrap lands on the same waveform
        // phase — no click. While the note is held the sound sustains
        // indefinitely; on release it continues to the natural tail.
        {
            const double periodSmp = sampleRate_ / hz; // output smp/period
            const double target = 0.5 * sampleRate_;   // ~0.5s of loop
            const double periods =
                juce::jmax(1.0, std::floor(target / periodSmp));
            loopLen_ = (int) (periodSmp * periods * ratio_);
            if (current_->shortMode)
                loopLen_ = 0; // staccato: let the recording die naturally
            // Wrap just before the file's final noise floor.
            loopEnd_ = juce::jmax(
                current_->onset + 2,
                current_->data->getNumSamples() - (int) (0.05 * sampleRate_)
                    - 1);
            if (loopLen_ <= 0 || loopLen_ > loopEnd_ - current_->onset)
                loopLen_ = 0; // too short to loop — play through
        }
    }

    void stopNote(float, bool allowTailOff) override
    {
        releasing_ = true;
        if (! allowTailOff)
        {
            active_ = false;
            clearCurrentNote();
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}
    bool isVoiceActive() const override { return active_; }

    void setCurrentPlaybackSampleRate(double rate) override
    {
        sampleRate_ = rate > 0.0 ? rate : 48000.0;
    }

    void renderNextBlock(juce::AudioBuffer<float>& out, int startSample,
                         int numSamples) override
    {
        if (! active_ || current_ == nullptr)
            return;
        const auto& d = *current_->data;
        const int ch = d.getNumChannels();
        const int len = d.getNumSamples();
        const float lvl = level_->load(std::memory_order_relaxed);

        // Natural tail: fade out over the last 1s of the sample so it
        // doesn't end abruptly. This is how a wind player runs out of air.
        const int fadeStart = juce::jmax(0, len - (int) sampleRate_);

        for (int i = 0; i < numSamples; ++i)
        {
            int ip = (int) pos_;
            // Sustain: while held, wrap back a whole-period length instead
            // of running off the end of the recording.
            if (! releasing_ && loopLen_ > 0 && ip >= loopEnd_)
            {
                pos_ -= loopLen_;
                ip = (int) pos_;
            }
            if (ip >= len - 1)
            {
                active_ = false;
                clearCurrentNote();
                break;
            }
            const float frac = (float) (pos_ - ip);
            float sm = 0.f;
            for (int c = 0; c < ch; ++c)
                sm += d.getSample(c, ip) * (1.f - frac)
                      + d.getSample(c, ip + 1) * frac;
            if (ch > 1)
                sm *= 0.5f;
            // Natural tail fade at the end of the recording.
            float tail = 1.f;
            if (ip >= fadeStart)
                tail = (float) (len - ip) / (float) (len - fadeStart);
            sm *= tail;
            sm *= env_ * lvl;
            for (int c = 0; c < out.getNumChannels(); ++c)
                out.addSample(c, startSample + i, sm);
            pos_ += ratio_;
            // Envelope advance (attack → decay/sustain; release fade).
            if (envPhase_ == 0)
            {
                env_ += atkRate_;
                if (env_ >= 1.f)
                {
                    env_ = 1.f;
                    envPhase_ = 1;
                }
            }
            else if (envPhase_ == 1)
                env_ = susTarget_ + (env_ - susTarget_) * decCoef_;
            if (shortCoef_ < 1.f)
                env_ *= shortCoef_;
            if (releasing_)
                env_ *= releaseCoef_;
            if (env_ < 0.0005)
            {
                active_ = false;
                clearCurrentNote();
                break;
            }
        }
    }

    Snd pick(const SndList& sounds, int note, int vel) const
    {
        auto keyDist = [&](const Snd& s) {
            return s->keyLo <= note && note <= s->keyHi
                       ? 0
                       : (note < s->keyLo ? s->keyLo - note
                                          : note - s->keyHi);
        };

        // 1) Exact: key in range AND velocity layer matches.
        Snd best;
        int bestKey = 1 << 30;
        for (const auto& s : sounds)
        {
            if (note < s->keyLo || note > s->keyHi)
                continue;
            if (vel < s->velLo || vel > s->velHi)
                continue;
            const int d = keyDist(s);
            if (d < bestKey
                || (d == bestKey
                    && std::abs(s->rootNote - note)
                           < std::abs(best->rootNote - note)))
            {
                best = s;
                bestKey = d;
            }
        }
        if (best != nullptr)
        {
            // Round-robin among equal candidates.
            std::vector<Snd> same;
            for (const auto& s : sounds)
                if (s->keyLo == best->keyLo && s->keyHi == best->keyHi
                    && s->velLo == best->velLo && s->velHi == best->velHi
                    && s->rootNote == best->rootNote)
                    same.push_back(s);
            if (same.size() > 1)
            {
                rr_ = (rr_ + 1) % (int) same.size();
                best = same[(size_t) rr_];
            }
            return best;
        }

        // 2) Fallback: nearest-key sample ignoring velocity — a sparse
        // dynamics layer beats silence.
        for (const auto& s : sounds)
        {
            const int d = keyDist(s);
            if (best == nullptr || d < bestKey)
            {
                best = s;
                bestKey = d;
            }
        }
        return best;
    }

public:
    void bind(TableRef* tables, const std::atomic<float>* level,
              std::atomic<std::uint64_t>* mask,
              const std::array<std::atomic<float>, 4>* adsr = nullptr)
    {
        tables_ = tables;
        level_ = level;
        mask_ = mask;
        adsr_ = adsr;
    }

private:
    void setKeyBit(bool on)
    {
        if (mask_ == nullptr || note_ < 36 || note_ >= 100)
            return;
        const std::uint64_t bit = 1ull << (note_ - 36);
        if (on)
            mask_->fetch_or(bit, std::memory_order_relaxed);
        else
            mask_->fetch_and(~bit, std::memory_order_relaxed);
    }
    double sampleRate_ = 48000.0;
    double pos_ = 0.0, ratio_ = 1.0;
    int loopLen_ = 0, loopEnd_ = 0;
    float env_ = 1.0f, releaseCoef_ = 0.999f;
    float atkRate_ = 0.f, decCoef_ = 0.999f, susTarget_ = 1.f;
    float shortCoef_ = 1.f;
    int envPhase_ = 2; // 0 attack, 1 decay/sustain, 2 plain
    const std::array<std::atomic<float>, 4>* adsr_ = nullptr;
    float stacDecay_ = 1.0f;
    TableRef* tables_ = nullptr;
    const std::atomic<float>* level_ = nullptr;
    std::atomic<std::uint64_t>* mask_ = nullptr;
    SndList local_;
    Snd current_;
    int note_ = 60;
    mutable int rr_ = 0;
    bool active_ = false, releasing_ = false;
};

class EzOrqProcessor : public juce::AudioProcessor
{
public:
    static inline juce::SpinLock& liveLock()
    {
        static juce::SpinLock l;
        return l;
    }
    static inline std::vector<EzOrqProcessor*>& liveList()
    {
        static std::vector<EzOrqProcessor*> v;
        return v;
    }
    bool ready() const
    {
        return loadedProgram_.load(std::memory_order_relaxed)
               == pendingProgram_.load(std::memory_order_relaxed);
    }
    EzOrqProcessor()
        : AudioProcessor(BusesProperties().withOutput(
              "Output", juce::AudioChannelSet::stereo(), true))
    {
        juce::StringArray names{ u8("— sin programas —") };
        auto programs = listPrograms();
        {
            const auto dirs = programsDirs();
            for (const auto& d : dirs)
            {
                const auto probe = d.getChildFile("Viento_Flute_Longs.json");
                std::fprintf(
                    stderr,
                    "[ezorq] ctor dir %s -> readdir=%ju exists=%d dir=%d\n",
                    d.getFullPathName().toRawUTF8(),
                    (unsigned long) d.findChildFiles(
                        juce::File::findFiles, false, "*.json")
                        .size(),
                    (int) probe.existsAsFile(), (int) d.isDirectory());
            }
        }
        std::fprintf(stderr, "[ezorq] ctor: %lu programas\n",
                     (unsigned long) programs.size());
        if (! programs.isEmpty())
            names = programs;

        program_ = new juce::AudioParameterChoice(
            juce::ParameterID{"prog", 1}, u8("Programa"), names, 0);
        addParameter(program_);
        lvl_ = new juce::AudioParameterFloat(
            juce::ParameterID{"lvl", 1}, u8("Nivel"),
            juce::NormalisableRange<float>(0.f, 2.f, 0.01f), 1.f);
        addParameter(lvl_);
        atk_ = new juce::AudioParameterFloat(
            juce::ParameterID{"atk", 1}, u8("Ataque"),
            juce::NormalisableRange<float>(0.f, 1.f, 0.001f), 0.f);
        addParameter(atk_);
        rel_ = new juce::AudioParameterFloat(
            juce::ParameterID{"rel", 1}, u8("Release"),
            juce::NormalisableRange<float>(0.01f, 4.f, 0.01f, 0.3f), 0.15f);
        addParameter(rel_);
        dec_ = new juce::AudioParameterFloat(
            juce::ParameterID{"dec", 1}, u8("Decay"),
            juce::NormalisableRange<float>(0.01f, 3.f, 0.01f, 0.4f), 0.35f);
        addParameter(dec_);
        sus_ = new juce::AudioParameterFloat(
            juce::ParameterID{"sus", 1}, u8("Sustain"),
            juce::NormalisableRange<float>(0.f, 1.f, 0.01f), 0.85f);
        addParameter(sus_);
        mono_ = new juce::AudioParameterBool(
            juce::ParameterID{"mono", 1}, u8("Mono legato"), false);
        addParameter(mono_);

        synth_.addSound(new AnySound());
        for (int v = 0; v < 16; ++v)
        {
            auto* voice = new OrqVoice();
            voice->bind(&tableRef_, &voiceLevel_, &activeMask_, &adsr_);
            synth_.addVoice(voice);
        }
        {
            juce::SpinLock::ScopedLockType lk(liveLock());
            liveList().push_back(this);
        }
        loader_ = std::thread([this] { loaderLoop(); });

        // Default to the verified flute program — set on the PARAM too, or
        // the first mismatch would load whatever sorts first. Tests can
        // override with setProbeProgram().
        const int probeIdx = g_probeProgram.load(std::memory_order_relaxed);
        bool chosen = false;
        if (probeIdx >= 0 && probeIdx < names.size())
        {
            pendingProgram_.store(probeIdx, std::memory_order_relaxed);
            program_->setValueNotifyingHost(
                program_->convertTo0to1((float) probeIdx));
            chosen = true;
        }
        if (! chosen)
            for (int i = 0; i < names.size(); ++i)
                if (names[i].containsIgnoreCase("Flute")
                    && names[i].containsIgnoreCase("Longs"))
                {
                    pendingProgram_.store(i, std::memory_order_relaxed);
                    program_->setValueNotifyingHost(
                        program_->convertTo0to1((float) i));
                    break;
                }
        // The default program loads synchronously in prepareToPlay (called
        // by reconcile/offline render — deterministic there); only LIVE
        // program changes load on a background thread.
    }

    std::atomic<int> loadedProgram_ { -1 };
    std::atomic<int> pendingProgram_ { 0 };
    std::atomic<bool> loading_ { false };
    std::atomic<float> voiceLevel_ { 0.9f };
    std::atomic<std::uint64_t> activeMask_ { 0 };
    std::atomic<int> rangeLo_ { -1 };
    std::atomic<int> rangeHi_ { -1 };
    // Keys 36..99 that have their OWN sample (bit = note-36).
    std::atomic<std::uint64_t> rootsMask_ { 0 };

    // Persistent loader thread: the ONLY place loadProgram runs.
    std::thread loader_;
    std::mutex loaderMutex_;
    std::condition_variable loaderCv_;
    bool loaderWake_ = false;
    bool loaderExit_ = false;

    ~EzOrqProcessor() override
    {
        {
            juce::SpinLock::ScopedLockType lk(liveLock());
            std::erase(liveList(), this);
        }
        {
            std::lock_guard<std::mutex> lk(loaderMutex_);
            loaderExit_ = true;
            loaderWake_ = true;
        }
        loaderCv_.notify_one();
        if (loader_.joinable())
            loader_.join();
    }

    // Editor keyboard preview.
    void previewNote(int note, bool on)
    {
        if (on)
            synth_.noteOn(1, note, 0.8f);
        else
            synth_.noteOff(1, note, 0.8f, false);
    }

    void requestLoad(int index)
    {
        pendingProgram_.store(index, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(loaderMutex_);
            loaderWake_ = true;
        }
        loaderCv_.notify_one();
    }

    void loaderLoop()
    {
        for (;;)
        {
            bool exit = false;
            {
                std::unique_lock<std::mutex> lk(loaderMutex_);
                loaderCv_.wait(lk, [&] {
                    return loaderExit_
                           || loaderWake_
                                  && pendingProgram_.load()
                                         != loadedProgram_.load();
                });
                if (loaderExit_)
                    return;
                loaderWake_ = false;
            }
            const int want = pendingProgram_.load(std::memory_order_relaxed);
            if (want != loadedProgram_.load(std::memory_order_relaxed))
            {
                loading_.store(true, std::memory_order_release);
                loadProgram(want);
                loading_.store(false, std::memory_order_release);
            }
        }
    }

public:
    // ---- editor-facing state ----
    bool isLoading() const { return loading_.load(std::memory_order_relaxed); }
    int rangeLo() const { return rangeLo_.load(std::memory_order_relaxed); }
    int rangeHi() const { return rangeHi_.load(std::memory_order_relaxed); }
    std::uint64_t activeMask() const
    {
        return activeMask_.load(std::memory_order_relaxed);
    }
    std::uint64_t rootsMask() const
    {
        return rootsMask_.load(std::memory_order_relaxed);
    }
    juce::StringArray programNames() const { return listPrograms(); }
    int loadedIndex() const { return loadedProgram_.load(); }
    juce::AudioParameterChoice* programParam() { return program_; }
    juce::AudioParameterFloat* lvlParam() { return lvl_; }
    juce::AudioParameterFloat* atkParam() { return atk_; }
    juce::AudioParameterFloat* relParam() { return rel_; }
    juce::AudioParameterFloat* decParam() { return dec_; }
    juce::AudioParameterFloat* susParam() { return sus_; }

private:

    const juce::String getName() const override { return {"EZOrquestra"}; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {"Default"}; }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double sr, int) override
    {
        synth_.setCurrentPlaybackSampleRate(sr);
        // Decoding must NEVER run on the UI/audio threads (a program is
        // tens of MB of WAV): the loader thread does it.
        requestLoad(pendingProgram_.load());
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
        // Live program changes reload OFF the audio/UI thread: the
        // persistent loader thread does it (requestLoad just nudges it).
        // The old detached-thread path ran loadProgram with no join — a
        // processor destroyed mid-load touched freed memory (SIGSEGV in
        // the offline audits).
        const int want = program_->getIndex();
        if (want != pendingProgram_.load(std::memory_order_relaxed))
            pendingProgram_.store(want, std::memory_order_relaxed);
        if (want != loadedProgram_.load(std::memory_order_relaxed))
            requestLoad(want);
        // Expression: CC1 scales the level between 25% and 100%.
        const float expr = 0.25f
                           + 0.75f * cc1_.load(std::memory_order_relaxed);
        voiceLevel_.store(juce::jmax(0.f, lvl_->get()) * expr,
                          std::memory_order_relaxed);
        adsr_[0].store(atk_->get(), std::memory_order_relaxed);
        adsr_[1].store(dec_->get(), std::memory_order_relaxed);
        adsr_[2].store(sus_->get(), std::memory_order_relaxed);
        adsr_[3].store(rel_->get(), std::memory_order_relaxed);
        synth_.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());
    }

    // ---- state: program index + level (ADSR live per block) ----
    void getStateInformation(juce::MemoryBlock& dest) override
    {
        juce::XmlElement x("EZORQ");
        x.setAttribute("prog", program_->getIndex());
        x.setAttribute("progname", program_->getCurrentChoiceName());
        x.setAttribute("lvl", lvl_->get());
        x.setAttribute("atk", atk_->get());
        x.setAttribute("rel", rel_->get());
        x.setAttribute("dec", dec_->get());
        x.setAttribute("sus", sus_->get());
        x.setAttribute("mono", (int) (mono_->get() ? 1 : 0));
        copyXmlToBinary(x, dest);
    }
    void setStateInformation(const void* data, int size) override
    {
        auto x = getXmlFromBinary(data, size);
        if (x == nullptr || x->getTagName() != "EZORQ")
            return;
        auto set = [](auto* p, double v) {
            p->setValueNotifyingHost(p->convertTo0to1((float) v));
        };
        program_->setValueNotifyingHost(program_->convertTo0to1(
            (float) juce::jlimit(
                0, juce::jmax(0, (int) program_->choices.size() - 1),
                x->getIntAttribute("prog", 0))));
        set(lvl_, x->getDoubleAttribute("lvl", 1.0));
        set(atk_, x->getDoubleAttribute("atk", 0.0));
        set(rel_, x->getDoubleAttribute("rel", 0.15));
        set(dec_, x->getDoubleAttribute("dec", 0.35));
        set(sus_, x->getDoubleAttribute("sus", 0.85));
        mono_->setValueNotifyingHost(mono_->convertTo0to1(
            (float) x->getIntAttribute("mono", 0)));
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    friend class EzOrqEditor;

private:
    // The CLI scanner writes to the JUCE dir; the BUNDLED app resolves
    // userApplicationDataDirectory to Application Support — search both.
    std::vector<juce::File> programsDirs() const
    {
        return {
            juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile("Library/EZMusicProduction/Orchestra"),
            juce::File::getSpecialLocation(
                juce::File::userApplicationDataDirectory)
                .getChildFile("EZMusicProduction")
                .getChildFile("Orchestra"),
        };
    }

    juce::File fileForProgram(const juce::String& name) const
    {
        for (const auto& dir : programsDirs())
        {
            const auto f = dir.getChildFile(name.replace(" ", "_") + ".json");
            if (f.existsAsFile())
                return f;
        }
        return {};
    }

    std::vector<juce::File> soundFilesForProgram(const juce::String& name) const
    {
        const auto f = fileForProgram(name);
        return f.existsAsFile() ? std::vector<juce::File>{ f }
                                : std::vector<juce::File>{};
    }

    juce::StringArray listPrograms() const
    {
        // RAW stems (underscores intact) — the editor parses family /
        // instrument / articulation back out of them.
        juce::StringArray out;
        for (const auto& dir : programsDirs())
        {
            // Manual readdir: JUCE's findChildFiles returned 0 entries in
            // the bundled app for a directory the shell sees populated.
            DIR* dp = opendir(dir.getFullPathName().toRawUTF8());
            if (dp == nullptr)
                continue;
            while (auto* ent = readdir(dp))
            {
                const juce::String n = juce::String::fromUTF8(ent->d_name);
                if (! n.endsWithIgnoreCase(".json"))
                    continue;
                const auto stem = n.upToLastOccurrenceOf(".", false, true);
                if (! out.contains(stem))
                    out.add(stem);
            }
            closedir(dp);
        }
        out.sort(true);
        return out;
    }

    void loadProgram(int index)
    {
        const auto names = listPrograms();
        if (index < 0 || index >= names.size())
        {
            std::fprintf(stderr, "[ezorq] loadProgram(%d) indice fuera (%d nombres)\n",
                         index, names.size());
            return;
        }
        const auto file = fileForProgram(names[index]);
        std::fprintf(stderr, "[ezorq] loadProgram(%d) -> %s (%lu bytes)\n",
                     index, file.getFullPathName().toRawUTF8(),
                     (unsigned long) file.getSize());

        // Global cache: decode+correct each program ONCE per process.
        // The live instance and every offline render source share it —
        // without this each render re-decoded ~1 GB of WAVs (and the
        // bounded waitProgramsLoaded expired mid-load: silent notes).
        {
            std::lock_guard<std::mutex> l(g_cacheMtx);
            const auto it = g_programCache.find(file.getFileName());
            if (it != g_programCache.end())
            {
                SndList copy = *it->second;
                tableRef_.publish(std::move(copy));
                loadedProgram_ = index;
                std::fprintf(stderr, "[ezorq] loadProgram(%d) cache\n", index);
                return;
            }
        }

        auto x = juce::parseXML(file);
        if (x == nullptr)
        {
            std::fprintf(stderr, "[ezorq] parseXML FALLO\n");
            return;
        }

        // Decode everything first, then swap atomically for the voices.
        const bool shortProgram = x->getBoolAttribute("short", false);
        SndList fresh;
        auto& formats = sharedFormats();

        struct Entry
        {
            int note, vel;
            OrqSound* sound;
        };
        std::vector<Entry> entries;

        if (auto* ss = x->getChildByName("samples"))
            for (auto* e : ss->getChildWithTagNameIterator("s"))
            {
                const juce::File f(e->getStringAttribute("f"));
                auto* reader = formats.createReaderFor(f);
                if (reader == nullptr)
                    continue;
                auto snd = std::make_shared<OrqSound>();
                snd->name = f.getFileName();
                snd->rootNote = e->getIntAttribute("n", 60);
                snd->velLo = e->getIntAttribute("v", 100) - 16;
                snd->velHi = e->getIntAttribute("v", 100) + 16;
                snd->shortMode = shortProgram;
                // AudioBuffer::read does NOT resize the destination —
                // an unsized buffer reads 0 samples (silent instrument).
                snd->data = std::make_unique<juce::AudioBuffer<float>>(
                    juce::jmin(2, (int) reader->numChannels),
                    (int) reader->lengthInSamples);
                reader->read(snd->data.get(), 0,
                             (int) reader->lengthInSamples, 0, true, true);
                snd->sampleRate = reader->sampleRate;
                delete reader;
                // Trim head noise: field-recording samples start with room
                // tone of varying length — without this every note fires
                // late by a different amount and the rhythm falls apart.
                {
                    auto* d = snd->data.get();
                    float peak = 0.f;
                    for (int c = 0; c < d->getNumChannels(); ++c)
                        for (int i = 0; i < d->getNumSamples(); ++i)
                            peak = juce::jmax(peak,
                                              std::abs(d->getSample(c, i)));
                    const float thr = peak * 0.05f;
                    int onset = 0;
                    while (onset < d->getNumSamples())
                    {
                        bool hot = false;
                        for (int c = 0; c < d->getNumChannels(); ++c)
                            if (std::abs(d->getSample(c, onset)) > thr)
                            {
                                hot = true;
                                break;
                            }
                        if (hot)
                            break;
                        ++onset;
                    }
                    // Cap: never start so late the note has no body left.
                    snd->onset = juce::jlimit(
                        0, juce::jmax(0, d->getNumSamples() - 240),
                        onset - (int) (0.005 * snd->sampleRate));
                }
                entries.push_back({ snd->rootNote, snd->velLo, snd.get() });
                fresh.push_back(std::move(snd));
            }

        // Measured roots: verify every decoded sample's ACTUAL pitch and
        // correct its rootNote — this is the ONLY guarantee that pressing
        // note N produces note N. The FFT+HPS detector is accurate to
        // ~±0.1 semitone on monophonic sources.
        //
        // OCTAVE POLICY: HPS locks an octave UP on rich string/brass timbres
        // (isolated +12/+24 deltas = artefact, ignored). But packs labelled
        // an octave off (the freesound strings) show the SAME +12 on every
        // sample — a systematic octave delta is real and must apply.
        {
            struct RootMeasure
            {
                OrqSound* snd;
                int measured, delta;
                double hz;
                double measuredCont; // continuous note (e.g. 73.56)
                double covRatio;     // measured coverage / label coverage
            };
            const auto hzOfNote = [](int n) {
                return 440.0 * std::pow(2.0, (n - 69) / 12.0);
            };
            std::vector<RootMeasure> measures;
            for (const auto& snd : fresh)
            {
                const int nSmp = snd->data->getNumSamples();
                if (snd->data->getNumChannels() < 1)
                    continue;
                if (nSmp < 4096)
                    continue;
                const double labelHz =
                    hzOfNote(snd->rootNote);
                double covM = 0.0, covL = 0.0;
                const double hz = detectPitchHzEx(
                    snd->data->getReadPointer(0), nSmp, snd->sampleRate,
                    labelHz, &covM, &covL);
                const double covRatio =
                    covL > 1e-9 ? covM / covL : (covM > 1e-9 ? 99.0 : 1.0);
                if (hz <= 0)
                    continue;
                const double cont =
                    69.0 + 12.0 * std::log2(hz / 440.0);
                const int measured =
                    juce::jlimit(24, 108, (int) std::lround(cont));
                measures.push_back({ snd.get(), measured,
                                     measured - snd->rootNote, hz, cont,
                                     covRatio });
            }
            // CORRECTION POLICY v3:
            //  - within 3/4 tone of the label: keep the integer root, tune
            //    the residual in cents (always safe).
            //  - an OCTAVE delta shared by a RUN of >= 4 consecutive
            //    samples (root order) = a series labelled an octave off:
            //    real, apply. Scattered octaves are HPS up-locks or
            //    sub-harmonic reads on pedal-register material: artefacts.
            //  - |delta| <= 3 non-octave: nearest-ladder tweak, apply.
            //  - anything else (scattered ±5..±11, ±19…): the label wins —
            //    packs with precise names (BASS_TROMBONE_SOFT_C1) are right
            //    and the detector is guessing below its 55 Hz floor.
            std::sort(measures.begin(), measures.end(),
                      [](const RootMeasure& a, const RootMeasure& b) {
                          return a.snd->rootNote < b.snd->rootNote;
                      });
            std::map<int, int> octaveRuns;
            {
                int i = 0;
                while (i < (int) measures.size())
                {
                    int j = i;
                    while (j < (int) measures.size()
                           && measures[j].delta == measures[i].delta)
                        ++j;
                    if (measures[i].delta != 0
                        && measures[i].delta % 12 == 0)
                        octaveRuns[measures[i].delta]
                            = std::max(octaveRuns[measures[i].delta], j - i);
                    i = j;
                }
            }
            for (const auto& m : measures)
            {
                const double fine = m.snd->rootNote - m.measuredCont;
                if (std::abs(fine) <= 0.75)
                {
                    m.snd->tuneCents = (float) (fine * 100.0);
                    continue;
                }
                // Octave deltas (either direction) are real when a RUN of
                // >= 4 consecutive samples shares them (whole mislabelled
                // series) OR the measured series explains far more peak
                // power than the label's (covRatio: the piccolo's top
                // sample is a true mislabel; the flute's FF 034 sub-octave
                // detector lock scores ratio ≈ 1 and keeps its label).
                const bool octaveReal =
                    m.delta % 12 == 0
                    && (octaveRuns[m.delta] >= 4
                        || m.covRatio >= 1.3);
                const bool ladderTweak = m.delta != 0
                                         && std::abs(m.delta) <= 3;
                if (! octaveReal && ! ladderTweak)
                    continue; // label wins; detector untrustworthy here
                std::fprintf(stderr,
                             "[ezorq] raiz medida %d -> %d (%.1f Hz) %s\n",
                             m.snd->rootNote, m.measured, m.hz,
                             m.snd->name.toRawUTF8());
                m.snd->rootNote = m.measured;
                m.snd->tuneCents =
                    (float) juce::jlimit(-100.0, 100.0,
                                         (m.snd->rootNote - m.measuredCont)
                                             * 100.0);
            }
        }

        // Key ranges and velocity tiles are built from the LABEL note
        // (entries were captured pre-correction): the trigger zone stays
        // where the pack put the sample, while rootNote+tuneCents tune it
        // to its true measured pitch. Correcting the root moved the zones
        // and left whole key regions with NO sample (silent notes).

        std::fprintf(stderr, "[ezorq] loadProgram(%d): %zu sounds, keys %d-%d\n",
                     index, fresh.size(),
                     fresh.empty() ? -1 : fresh.front()->rootNote,
                     fresh.empty() ? -1 : fresh.back()->rootNote);
        {
            int lo = 127, hi = 0;
            for (const auto& snd : fresh)
            {
                lo = juce::jmin(lo, snd->rootNote);
                hi = juce::jmax(hi, snd->rootNote);
            }
            rangeLo_.store(fresh.empty() ? -1 : lo, std::memory_order_relaxed);
            rangeHi_.store(fresh.empty() ? -1 : hi, std::memory_order_relaxed);
        }

        // Key ranges: per UNIQUE root note — every velocity layer of a note
        // shares its span. Ranges per SAMPLE gave the 2nd..Nth layers an
        // inverted (empty) span, so the exact-match pick never saw them and
        // every loud note fell back to the quiet pp layer.
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) {
                      if (a.note != b.note)
                          return a.note < b.note;
                      return a.vel < b.vel;
                  });
        {
            size_t i = 0;
            while (i < entries.size())
            {
                size_t j = i;
                while (j < entries.size() && entries[j].note == entries[i].note)
                    ++j;
                const int lo = i == 0
                                   ? 0
                                   : (entries[i - 1].note + entries[i].note) / 2 + 1;
                const int hi = j == entries.size()
                                   ? 127
                                   : (entries[i].note + entries[j].note) / 2;
                for (size_t k = i; k < j; ++k)
                {
                    entries[k].sound->keyLo = lo;
                    entries[k].sound->keyHi = hi;
                }
                i = j;
            }
        }

        // Loudness normalization: field-recording packs sit at wildly
        // different levels (Alpine flutes peak at -34 dB). Scale the whole
        // program to 0.5 peak, preserving relative dynamics.
        {
            float peak = 0.f;
            for (const auto& snd : fresh)
                for (int c = 0; c < snd->data->getNumChannels(); ++c)
                    for (int i = 0; i < snd->data->getNumSamples(); ++i)
                        peak = juce::jmax(
                            peak, std::abs(snd->data->getSample(c, i)));
            if (peak > 0.0001f)
            {
                const float g = 0.5f / peak;
                for (const auto& snd : fresh)
                    snd->data->applyGain(g);
                std::fprintf(stderr, "[ezorq] normalizado x%.1f (peak %.4f)\n",
                             g, peak);
            }
        }

        // Velocity layers must TILE 0..127 per root note — packs ship sparse
        // dynamics (pp=24, ff=120) and a velocity in the gap (e.g. 100)
        // matches NO layer: silent notes.
        {
            size_t i = 0;
            while (i < entries.size())
            {
                size_t j = i;
                while (j < entries.size() && entries[j].note == entries[i].note)
                    ++j;
                const size_t layers = j - i;
                for (size_t k = 0; k < layers; ++k)
                {
                    const int vlo = (int) ((k * 128) / layers);
                    const int vhi = (int) (((k + 1) * 128) / layers) - 1;
                    auto* snd = entries[i + k].sound;
                    snd->velLo = vlo;
                    snd->velHi = vhi;
                }
                i = j;
            }
        }

        {
            std::lock_guard<std::mutex> l(g_cacheMtx);
            g_programCache[file.getFileName()]
                = std::make_shared<const SndList>(fresh);
        }
        // Old sounds stay alive (shared_ptr) until no voice uses them.
        tableRef_.publish(std::move(fresh));
        loadedProgram_ = index;
    }

    juce::AudioParameterChoice* program_ = nullptr;
    juce::AudioParameterFloat* lvl_ = nullptr;
    juce::AudioParameterFloat* atk_ = nullptr;
    juce::AudioParameterFloat* rel_ = nullptr;
    juce::AudioParameterFloat* dec_ = nullptr;
    juce::AudioParameterFloat* sus_ = nullptr;
    std::array<std::atomic<float>, 4> adsr_ { { 0.f, 0.35f, 0.85f, 0.15f } };

    juce::Synthesiser synth_;
    TableRef tableRef_;
    juce::AudioParameterBool* mono_ = nullptr;
    std::atomic<float> cc1_ { 1.f };
    int lastMonoNote_ = -1;
};

// ---- Editor: browser + Kontakt-style keyboard -----------------------------
struct ProgramRef
{
    juce::String family, instrument, artic;
    int index = -1;
};

// Parses "Family_Instrument_Artic" (first _ and last _ are separators).
ProgramRef parseProgramName(const juce::String& name)
{
    ProgramRef r;
    const int first = name.indexOf("_");
    const int last = name.lastIndexOf("_");
    if (first < 0 || last <= first)
    {
        r.family = name;
        return r;
    }
    r.family = name.substring(0, first);
    r.instrument = name.substring(first + 1, last);
    r.artic = name.substring(last + 1);
    return r;
}

class PianoKeys : public juce::Component
{
public:
    PianoKeys(ezorq::EzOrqProcessor& proc) : proc_(proc) {}

    void paint(juce::Graphics& g) override
    {
        const float w = (float) getWidth();
        const float h = (float) getHeight();
        constexpr int kLo = 36, kHi = 107; // C2..B7

        const int rlo = proc_.rangeLo(), rhi = proc_.rangeHi();
        const std::uint64_t mask = proc_.activeMask();
        const std::uint64_t roots = proc_.rootsMask();
        auto lit = [&](int n) {
            return n >= 36 && n < 100 && (mask & (1ull << (n - 36))) != 0;
        };
        auto hasOwn = [&](int n) {
            return n >= 36 && n < 100 && (roots & (1ull << (n - 36))) != 0;
        };
        auto inRange = [&](int n) {
            return rlo >= 0 && n >= rlo && n <= rhi;
        };

        const int whiteCount = whiteKeysBelow(kHi + 1);
        const float ww = w / (float) whiteCount;
        const float bw = ww * 0.62f;

        // White keys. Keys without their own sample are dimmed: they still
        // play (resampled from the nearest sample) but with borrowed tone.
        int wi = 0;
        for (int n = kLo; n <= kHi; ++n)
        {
            if (juce::MidiMessage::isMidiNoteBlack(n))
                continue;
            auto base = inRange(n) ? juce::Colour(0xFFE9ECEF)
                                   : juce::Colour(0xFF848892);
            if (! hasOwn(n) && inRange(n))
                base = juce::Colour(0xFFC2C7D0);
            if (lit(n))
                base = juce::Colour(0xFF4D9DF7);
            g.setColour(base);
            g.fillRect(wi * ww, 0.f, ww - 1.f, h);
            ++wi;
        }
        // Black keys, aligned to the boundary between their neighbours.
        for (int n = kLo; n <= kHi; ++n)
        {
            if (! juce::MidiMessage::isMidiNoteBlack(n))
                continue;
            const float x = (float) whiteKeysBelow(n) * ww - bw * 0.5f;
            auto base = inRange(n) ? juce::Colour(0xFF22252C)
                                   : juce::Colour(0xFF4A4E58);
            if (! hasOwn(n) && inRange(n))
                base = juce::Colour(0xFF33363E);
            if (lit(n))
                base = juce::Colour(0xFF2E7CD6);
            g.setColour(base);
            g.fillRect(x, 0.f, bw, h * 0.62f);
        }
        // Range extent strip along the top.
        if (rlo >= 0 && rhi >= rlo)
        {
            const float x0 = xOfNote(rlo, ww);
            const float x1 = xOfNote(rhi, ww) + ww * 0.5f;
            g.setColour(juce::Colour(0xFF7FE8C8));
            g.fillRect(x0, 0.f, juce::jmax(2.f, x1 - x0), 3.f);
        }
    }

private:
    static int whiteKeysBelow(int note)
    {
        int count = 0;
        for (int m = 36; m < note; ++m)
            if (! juce::MidiMessage::isMidiNoteBlack(m))
                ++count;
        return count;
    }
    float xOfNote(int note, float ww) const
    {
        return (float) whiteKeysBelow(note) * ww;
    }

    ezorq::EzOrqProcessor& proc_;
};

class EzOrqEditor : public juce::AudioProcessorEditor,
                    private juce::AudioProcessorParameter::Listener,
                    private juce::Timer
{
public:
    explicit EzOrqEditor(ezorq::EzOrqProcessor& proc)
        : juce::AudioProcessorEditor(&proc), proc_(proc)
    {
        for (int k = 0; k < 6; ++k)
            param(k)->addListener(this);

        titleLabel_.setText(u8("EZOrquestra"),
                            juce::dontSendNotification);
        titleLabel_.setFont(juce::FontOptions(15.0f).withStyle("Bold"));
        titleLabel_.setColour(juce::Label::textColourId,
                              juce::Colour(0xFFE8EAEE));
        addAndMakeVisible(titleLabel_);
        status_.setFont(juce::FontOptions(11.0f));
        status_.setColour(juce::Label::textColourId, juce::Colour(0xFF9AA2B1));
        addAndMakeVisible(status_);

        famList_.setModel(&famModel_);
        insList_.setModel(&insModel_);
        artList_.setModel(&artModel_);
        for (auto* l : { &famList_, &insList_, &artList_ })
        {
            l->setRowHeight(18);
            l->setColour(juce::ListBox::backgroundColourId,
                         juce::Colour(0xFF1B1D22));
            addAndMakeVisible(*l);
        }
        famModel_.e = this;
        insModel_.e = this;
        artModel_.e = this;

        rebuildFamilies();

        styleCell(lvlL_, lvlS_, u8("Nivel"), 0.0, 2.0, 1.0);
        styleCell(atkL_, atkS_, u8("Ataque"), 0.0, 1.0, 1.0);
        styleCell(decL_, decS_, u8("Decay"), 0.01, 3.0, 0.4);
        styleCell(susL_, susS_, u8("Sustain"), 0.0, 1.0, 1.0);
        styleCell(relL_, relS_, u8("Release"), 0.01, 4.0, 0.3);
        wire(lvlS_, proc_.lvlParam());
        wire(atkS_, proc_.atkParam());
        wire(decS_, proc_.decParam());
        wire(susS_, proc_.susParam());
        wire(relS_, proc_.relParam());
        addAndMakeVisible(adsrCanvas_);

        addAndMakeVisible(keys_);

        updateFromParam();
        startTimerHz(30);
        setSize(940, 560);
    }

    ~EzOrqEditor() override
    {
        stopTimer();
        for (int k = 0; k < 6; ++k)
            param(k)->removeListener(this);
    }

    void parameterValueChanged(int, float) override
    {
        juce::MessageManager::callAsync([this] { updateFromParam(); });
    }
    void parameterGestureChanged(int, bool) override {}

    void timerCallback() override
    {
        keys_.repaint();
        const bool loading = proc_.isLoading();
        const juce::String st =
            loading ? u8("Cargando programa…")
                    : (proc_.loadedIndex() >= 0 ? u8("Listo")
                                                : u8("Sin programa"));
        if (st != status_.getText())
            status_.setText(st, juce::dontSendNotification);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xFF16171B));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        auto header = area.removeFromTop(26);
        titleLabel_.setBounds(header.removeFromLeft(150));
        status_.setBounds(header.removeFromRight(200));

        auto body = area;
        keys_.setBounds(body.removeFromBottom(120));
        body.removeFromTop(6);

        const int listW = (body.getWidth() - 170) / 3;
        famList_.setBounds(body.removeFromLeft(listW));
        insList_.setBounds(body.removeFromLeft(listW));
        artList_.setBounds(body.removeFromLeft(listW));
        auto settings = body.removeFromLeft(190).reduced(6, 0);
        adsrCanvas_.setBounds(settings.removeFromTop(96));
        settings.removeFromTop(4);
        layoutCol(settings, lvlL_, lvlS_);
        layoutCol(settings, atkL_, atkS_);
        layoutCol(settings, decL_, decS_);
        layoutCol(settings, susL_, susS_);
        layoutCol(settings, relL_, relS_);
    }

private:
    juce::AudioProcessorParameter* param(int k)
    {
        static juce::AudioProcessorParameter* const table[6] = {
            proc_.programParam(), proc_.lvlParam(), proc_.atkParam(),
            proc_.relParam(), proc_.decParam(), proc_.susParam()
        };
        return table[(size_t) k];
    }

    static juce::String u8(const char* s)
    {
        return juce::String::fromUTF8(s);
    }

    void styleCell(juce::Label& l, juce::Slider& s, const juce::String& title,
                   double lo, double hi, double skew)
    {
        l.setText(title, juce::dontSendNotification);
        l.setFont(juce::FontOptions(11.0f));
        l.setColour(juce::Label::textColourId, juce::Colour(0xFF9AA2B1));
        addAndMakeVisible(l);
        s.setSliderStyle(juce::Slider::LinearBar);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
        s.setRange(lo, hi, 0.01);
        s.setSkewFactor(skew);
        addAndMakeVisible(s);
    }
    void wire(juce::Slider& s, auto* p)
    {
        s.onValueChange = [this, &s, p] {
            p->setValueNotifyingHost(p->convertTo0to1((float) s.getValue()));
        };
    }
    void layoutCol(juce::Rectangle<int> c, juce::Label& l, juce::Slider& s)
    {
        l.setBounds(c.removeFromTop(14));
        s.setBounds(c.removeFromTop(24).reduced(0, 4));
    }

    // ---- ADSR curve canvas ----
    struct AdsrCanvas : juce::Component
    {
        explicit AdsrCanvas(ezorq::EzOrqProcessor& proc) : p(proc) {}
        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xFF1B1D22));
            const float a = p.atkParam()->get();
            const float d = p.decParam()->get();
            const float su = p.susParam()->get();
            const float r = p.relParam()->get();
            // Time axis: A+D+R share the width; log-ish scaling.
            const float total = a + d + r + 0.05f;
            auto w = (float) getWidth();
            auto h = (float) getHeight();
            const float ax = a / total * w;
            const float dx = d / total * w;
            const float rx = r / total * w;
            juce::Path path;
            path.startNewSubPath(0, h);
            path.lineTo(ax, 2);
            // exponential-ish decay to the sustain level
            const float susY = h - su * (h - 6) - 2;
            path.quadraticTo(ax + dx * 0.3f, 2 + (susY - 2) * 0.55f,
                             ax + dx, susY);
            const float sx = ax + dx;
            path.lineTo(w - rx, susY);
            path.quadraticTo(w - rx * 0.4f, h - (h - susY) * 0.2f, w, h);
            g.setColour(juce::Colour(0xFFE8A33D));
            g.strokePath(path, juce::PathStrokeType(2.f));
            g.setColour(juce::Colour(0xFFE8A33D).withAlpha(0.18f));
            juce::Path fill(path);
            fill.lineTo(w, h);
            fill.lineTo(0, h);
            fill.closeSubPath();
            g.fillPath(fill);
            g.setColour(juce::Colour(0xFF3A404A));
            g.drawRect(getLocalBounds(), 1);
        }
        ezorq::EzOrqProcessor& p;
    };

    juce::Label decL_, susL_;
    juce::Slider decS_, susS_;

    // ---- browser model ----
    juce::StringArray names_;      // all program names (raw, with underscores)
    juce::StringArray families_;
    juce::StringArray instruments_; // display (prettified)
    juce::StringArray artics_;      // display (prettified)
    juce::StringArray instrumentsRaw_;
    juce::StringArray articsRaw_;
    juce::String curFamily_, curInstrument_, curArtic_;
    bool updating_ = false;        // suppress model callbacks during syncs
    static juce::String pretty(juce::String v)
    {
        return v.replace("_", " ");
    }

    void rebuildFamilies()
    {
        names_ = proc_.programNames();
        families_.clear();
        for (const auto& n : names_)
        {
            const auto r = parseProgramName(n);
            if (! families_.contains(r.family))
                families_.add(r.family);
        }
        famList_.updateContent();
        if (families_.size() > 0)
        {
            const int row = juce::jmax(0, families_.indexOf(curFamily_));
            famList_.selectRow(row, true, true);
        }
    }

    void rebuildInstruments()
    {
        instruments_.clear();
        instrumentsRaw_.clear();
        for (const auto& n : names_)
        {
            const auto r = parseProgramName(n);
            if (r.family == curFamily_
                && ! instrumentsRaw_.contains(r.instrument))
            {
                instrumentsRaw_.add(r.instrument);
                instruments_.add(pretty(r.instrument));
            }
        }
        insList_.updateContent();
        const int row = instrumentsRaw_.indexOf(curInstrument_);
        if (row >= 0)
            insList_.selectRow(row, true, true);
    }

    void rebuildArtics()
    {
        artics_.clear();
        articsRaw_.clear();
        for (const auto& n : names_)
        {
            const auto r = parseProgramName(n);
            if (r.family == curFamily_ && r.instrument == curInstrument_)
            {
                articsRaw_.add(r.artic);
                artics_.add(pretty(r.artic));
            }
        }
        artList_.updateContent();
        const int row = articsRaw_.indexOf(curArtic_);
        if (row >= 0)
            artList_.selectRow(row, true, true);
    }

    void chooseProgram(const juce::String& family,
                       const juce::String& instrument,
                       const juce::String& artic)
    {
        const juce::String target = family + "_" + instrument + "_" + artic;
        const auto names = proc_.programNames();
        for (int i = 0; i < names.size(); ++i)
            if (names[i] == target)
            {
                auto* p = proc_.programParam();
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->convertTo0to1((float) i));
                p->endChangeGesture();
                break;
            }
    }

    void updateFromParam()
    {
        if (updating_)
            return;
        juce::ScopedValueSetter<bool> guard { updating_, true };
        const auto names = proc_.programNames();
        const int idx = juce::jlimit(0, juce::jmax(0, names.size() - 1),
                                     proc_.programParam()->getIndex());
        if (names.isEmpty())
            return;
        const auto r = parseProgramName(names[idx]);
        curFamily_ = r.family;
        curInstrument_ = r.instrument;
        curArtic_ = r.artic;
        rebuildInstruments();
        rebuildArtics();
        lvlS_.setValue(proc_.lvlParam()->get(), juce::dontSendNotification);
        atkS_.setValue(proc_.atkParam()->get(), juce::dontSendNotification);
        decS_.setValue(proc_.decParam()->get(), juce::dontSendNotification);
        susS_.setValue(proc_.susParam()->get(), juce::dontSendNotification);
        relS_.setValue(proc_.relParam()->get(), juce::dontSendNotification);
        adsrCanvas_.repaint();
        famList_.repaint();
        insList_.repaint();
        artList_.repaint();
    }

    struct FamModel : juce::ListBoxModel
    {
        EzOrqEditor* e = nullptr;
        int getNumRows() override
        {
            return e != nullptr ? e->families_.size() : 0;
        }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                              bool sel) override
        {
            if (e == nullptr || row >= e->families_.size())
                return;
            if (sel)
            {
                g.setColour(juce::Colour(0xFF4D9DF7));
                g.fillRect(0, 0, w, h);
            }
            g.setColour(sel ? juce::Colours::white : juce::Colour(0xFFC7CBD4));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(e->families_[row], 6, 0, w - 8, h,
                       juce::Justification::centredLeft);
        }
        void selectedRowsChanged(int row) override
        {
            if (e == nullptr || row < 0 || e->updating_)
                return;
            e->curFamily_ = e->families_[row];
            e->curInstrument_ = "";
            e->curArtic_ = "";
            e->rebuildInstruments();
        }
    } famModel_;

    struct InsModel : juce::ListBoxModel
    {
        EzOrqEditor* e = nullptr;
        int getNumRows() override
        {
            return e != nullptr ? e->instruments_.size() : 0;
        }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                              bool sel) override
        {
            if (e == nullptr || row >= e->instruments_.size())
                return;
            if (sel)
            {
                g.setColour(juce::Colour(0xFF4D9DF7));
                g.fillRect(0, 0, w, h);
            }
            g.setColour(sel ? juce::Colours::white : juce::Colour(0xFFC7CBD4));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(e->instruments_[row], 6, 0, w - 8, h,
                       juce::Justification::centredLeft);
        }
        void selectedRowsChanged(int row) override
        {
            if (e == nullptr || row < 0 || e->updating_)
                return;
            e->curInstrument_ = e->instrumentsRaw_[row];
            e->curArtic_ = "";
            e->rebuildArtics();
        }
    } insModel_;

    struct ArtModel : juce::ListBoxModel
    {
        EzOrqEditor* e = nullptr;
        int getNumRows() override
        {
            return e != nullptr ? e->artics_.size() : 0;
        }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                              bool sel) override
        {
            if (e == nullptr || row >= e->artics_.size())
                return;
            if (sel)
            {
                g.setColour(juce::Colour(0xFF7FE8C8));
                g.fillRect(0, 0, w, h);
            }
            g.setColour(sel ? juce::Colour(0xFF101318)
                            : juce::Colour(0xFFC7CBD4));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(e->artics_[row], 6, 0, w - 8, h,
                       juce::Justification::centredLeft);
        }
        void selectedRowsChanged(int row) override
        {
            if (e == nullptr || row < 0 || row >= e->artics_.size()
                || e->updating_)
                return;
            e->curArtic_ = e->articsRaw_[row];
            e->chooseProgram(e->curFamily_, e->curInstrument_, e->curArtic_);
        }
    } artModel_;

    ezorq::EzOrqProcessor& proc_;
    // MUST be declared after proc_: the canvas binds the reference at
    // construction (member init order follows declaration order — an
    // earlier declaration captured garbage and crashed on paint).
    AdsrCanvas adsrCanvas_ { proc_ };
    juce::Label titleLabel_, status_;
    juce::ListBox famList_, insList_, artList_;
    juce::Slider lvlS_, atkS_, relS_;
    juce::Label lvlL_, atkL_, relL_;
    PianoKeys keys_ { proc_ };
};





} // namespace



void waitProgramsLoaded()
{
    // Generous bound: the FIRST decode of a program streams ~0.5 GB of
    // WAVs; the old 7.5 s bound expired mid-load and rendered silence.
    for (int attempt = 0; attempt < 1200; ++attempt)
    {
        bool all = true;
        {
            juce::SpinLock::ScopedLockType lk(EzOrqProcessor::liveLock());
            for (auto* p : EzOrqProcessor::liveList())
                if (! p->ready())
                    all = false;
        }
        if (all)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

void setProbeProgram(int index)
{
    g_probeProgram.store(index, std::memory_order_relaxed);
}

void clearProgramCache()
{
    std::lock_guard<std::mutex> l(g_cacheMtx);
    g_programCache.clear();
}

bool probeRange(int* lo, int* hi)
{
    juce::SpinLock::ScopedLockType lk(EzOrqProcessor::liveLock());
    for (auto* p : EzOrqProcessor::liveList())
        if (p->ready())
        {
            const int l = p->rangeLo(), h = p->rangeHi();
            if (l >= 0 && h >= l)
            {
                if (lo != nullptr)
                    *lo = l;
                if (hi != nullptr)
                    *hi = h;
                return true;
            }
        }
    return false;
}

juce::AudioProcessor* create() { return new EzOrqProcessor(); }

juce::AudioProcessorEditor* EzOrqProcessor::createEditor()
{
    return new EzOrqEditor(*this);
}
} // namespace ezorq
