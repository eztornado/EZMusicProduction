// Shared piano-ish voice: 5 harmonic partials, velocity-scaled, piano-like
// decay (bass rings longer), CC64 sustain pedal, short musical release.
// ONE implementation backs the engine's zero-config track fallback AND the
// EZSynth built-in instrument. All param defaults reproduce the original
// sound exactly (bright=1, decay=1, release=1, level=1).
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cmath>

namespace cadence {

struct PianoParams
{
    std::atomic<float> bright { 1.0f };  // partial tilt: amp[p] *= bright^p
    std::atomic<float> decay { 1.0f };   // ring-time multiplier
    std::atomic<float> release { 1.0f }; // release-tail multiplier
    std::atomic<float> level { 1.0f };   // output scale
};

struct PianoSound : juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

struct PianoVoice : juce::SynthesiserVoice
{
    static constexpr int kPartials = 5;

    explicit PianoVoice(const PianoParams* p = nullptr) : params_(p) {}

    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<PianoSound*>(sound) != nullptr;
    }

    void startNote(int midiNoteNumber, float velocity,
                   juce::SynthesiserSound*, int) override
    {
        const float bright = p().bright.load(std::memory_order_relaxed);
        const float decayMul = p().decay.load(std::memory_order_relaxed);
        const float lvl = p().level.load(std::memory_order_relaxed);

        const double hz = 440.0 * std::pow(2.0, (midiNoteNumber - 69) / 12.0);
        for (int i = 0; i < kPartials; ++i)
        {
            angle[i] = 0.0;
            delta[i] = juce::MathConstants<double>::twoPi * hz * (i + 1)
                       / sampleRate_;
        }
        // Harmonic mix (fixed), tilted by `bright`, velocity scales it all.
        const double base[kPartials] = { 1.00, 0.55, 0.32, 0.20, 0.12 };
        for (int i = 0; i < kPartials; ++i)
            amp[i] = base[i] * std::pow((double) bright, (double) i);
        level = velocity * 0.4 * lvl;

        // Bass notes ring longer than treble notes (piano-like).
        const double tau = juce::jlimit(0.8, 6.0,
                                        6.0 - (midiNoteNumber - 21) * 0.06)
                           * decayMul;
        decayPerSample = std::exp(-1.0 / (tau * sampleRate_));

        env = 1.0;
        releasing = false;
        active = true;
        warpSemis_ = 0.0;
    }

    void stopNote(float, bool allowTailOff) override
    {
        if (pedalDown_)
        {
            sustained_ = true; // hold until the pedal releases
            return;
        }
        releasing = true;
        if (! allowTailOff)
        {
            env *= 0.05;
            // JUCE convention: a hard stop frees the note claim at once —
            // otherwise the voice keeps matching its old pitch forever.
            clearCurrentNote();
        }
    }

    void pitchWheelMoved(int) override {}

    void controllerMoved(int controllerNumber, int value) override
    {
        if (controllerNumber != 64)
            return;
        const bool down = value >= 64;
        if (pedalDown_ && ! down && sustained_)
        {
            // Pedal released: sustained voices start their natural release.
            releasing = true;
            sustained_ = false;
        }
        pedalDown_ = down;
    }

    bool isVoiceActive() const override { return active; }

    // EZSynth LINES: per-voice pitch offset in semitones (default 0 leaves
    // the engine's fallback piano untouched).
    void setWarpTarget(float semis) { warpTarget_ = semis; }

    void setCurrentPlaybackSampleRate(double rate) override
    {
        sampleRate_ = rate > 0.0 ? rate : 48000.0;
        warpCoef_ = 1.0 - std::exp(-1.0 / (0.008 * sampleRate_));
    }

    void renderNextBlock(juce::AudioBuffer<float>& output, int startSample,
                         int numSamples) override
    {
        if (! active)
            return;

        // Piano-damper release with a short musical tail: -20 dB at ~0.35 s,
        // -60 dB (inaudible) at ~1.0 s. (History: 1.5 s masked timing under
        // a fake "pedal" feel; 0.06 s cut notes off abruptly.)
        const float relMul = p().release.load(std::memory_order_relaxed);
        const double releaseDecay =
            std::exp(-1.0 / (0.15 * relMul * sampleRate_));

        for (int i = 0; i < numSamples; ++i)
        {
            warpSemis_ += (warpTarget_ - warpSemis_) * warpCoef_;
            const double mul = std::exp2(warpSemis_ / 12.0);
            double s = 0.0;
            for (int p = 0; p < kPartials; ++p)
            {
                s += amp[p] * std::sin(angle[p]);
                angle[p] += delta[p] * mul;
            }
            const float sample = (float) (s * level * env);
            for (int ch = 0; ch < output.getNumChannels(); ++ch)
                output.addSample(ch, startSample + i, sample);

            env *= decayPerSample;
            if (releasing)
                env *= releaseDecay;
        }

        if (env < 0.0004)
        {
            active = false;
            clearCurrentNote();
        }
    }

private:
    const PianoParams& p() const
    {
        static PianoParams defaults;
        return params_ != nullptr ? *params_ : defaults;
    }

    const PianoParams* params_;
    double sampleRate_ = 48000.0;
    double angle[kPartials] = {};
    double delta[kPartials] = {};
    double amp[kPartials] = {};
    double env = 1.0, decayPerSample = 1.0, level = 0.0;
    bool releasing = false, active = false;
    bool pedalDown_ = false;   // CC64 held: noteOffs sustain
    bool sustained_ = false;   // this voice was held by the pedal
    double warpSemis_ = 0.0, warpTarget_ = 0.0, warpCoef_ = 0.01;
};

} // namespace cadence
