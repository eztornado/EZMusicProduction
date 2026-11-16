// EZ AutoFilter — implementation. See ezautofilter.h and ezfx.h.
//
// A TPT state-variable filter (Zavalishin) gives stable low/band/high outputs
// at any cutoff. Low-pass 24 dB cascades two stages. The envelope follower is
// linked across channels so stereo material does not make the filter chatter.
#include "ezautofilter.h"

#include "ezfx.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ezautofilter {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

struct Svf
{
    struct Output
    {
        float low = 0.0f;
        float band = 0.0f;
        float high = 0.0f;
    };

    inline Output process(float input, float g, float k)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = input - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, input - k * v1 - v2 };
    }

    void reset()
    {
        ic1 = 0.0f;
        ic2 = 0.0f;
    }

    float ic1 = 0.0f;
    float ic2 = 0.0f;
};

class EzAutoFilterProcessor final : public ezfx::Effect
{
public:
    EzAutoFilterProcessor()
        : Effect("EZ AutoFilter", "EZAutoFilter")
    {
        mix_ = addFloat("mix", "Mix", 0.0f, 100.0f, 100.0f, "%");
        cutoff_ = addFloat("cut", "Cutoff", 20.0f, 20000.0f, 1200.0f, "Hz", 1000.0f);
        resonance_ = addFloat("res", "Resonance", 0.0f, 100.0f, 25.0f, "%");
        mode_ = addChoice("mode", "Mode",
                          {"Low-pass 12", "Low-pass 24", "Band-pass",
                           "High-pass"},
                          0);
        drive_ = addFloat("drv", "Drive", 0.0f, 24.0f, 0.0f, "dB");
        envAmount_ = addFloat("env", "Env amount", -100.0f, 100.0f, 0.0f, "%");
        attack_ = addFloat("atk", "Attack", 0.5f, 200.0f, 8.0f, "ms", 20.0f);
        release_ = addFloat("rel", "Release", 5.0f, 2000.0f, 180.0f, "ms", 200.0f);
        lfoRate_ = addFloat("lfo", "LFO rate", 0.05f, 10.0f, 0.35f, "Hz", 1.0f);
        lfoAmount_ = addFloat("lam", "LFO amount", 0.0f, 100.0f, 0.0f, "%");
        lfoShape_ = addChoice("lsh", "LFO shape",
                              {"Sine", "Triangle", "Saw", "Square"}, 0);
        output_ = addFloat("out", "Output", -24.0f, 12.0f, 0.0f, "dB");
        bypass_ = addBool("byp", "Bypass", false);
    }

    void prepareDsp(double sampleRate, int) override
    {
        sampleRate_ = sampleRate;
        for (auto& s : filters_)
            for (auto& f : s)
                f.reset();
        cutoffLog_ = std::log2(juce::jmax(20.0f, cutoff_->get()));
        resonanceState_ = juce::jlimit(0.0f, 1.0f, resonance_->get() / 100.0f);
        driveDb_ = drive_->get();
        mixState_ = juce::jlimit(0.0f, 1.0f, mix_->get() / 100.0f);
        outputDb_ = output_->get();
        envelope_ = 0.0f;
        lfoPhase_ = 0.0f;
    }

    void releaseDsp() override
    {
        for (auto& s : filters_)
            for (auto& f : s)
                f.reset();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        if (n <= 0 || buffer.getNumChannels() < 2 || bypass_->get())
            return;

        const float mixTarget = juce::jlimit(0.0f, 1.0f, mix_->get() / 100.0f);
        const float baseCutoff = juce::jlimit(20.0f, 20000.0f, cutoff_->get());
        const float resTarget = juce::jlimit(0.0f, 1.0f, resonance_->get() / 100.0f);
        const float driveTarget = drive_->get();
        const float outTarget = output_->get();
        const float envAmount = juce::jlimit(-1.0f, 1.0f, envAmount_->get() / 100.0f);
        const float lfoAmount = juce::jlimit(0.0f, 1.0f, lfoAmount_->get() / 100.0f);
        const float attackMs = juce::jmax(0.1f, attack_->get());
        const float releaseMs = juce::jmax(0.5f, release_->get());
        const float lfoRate = juce::jmax(0.01f, lfoRate_->get());
        const int shape = lfoShape_->getIndex();
        const int mode = mode_->getIndex();

        const float attackCoeff =
            1.0f - std::exp(-1.0f / (attackMs * 0.001f * (float) sampleRate_));
        const float releaseCoeff =
            1.0f - std::exp(-1.0f / (releaseMs * 0.001f * (float) sampleRate_));
        const float lfoInc = lfoRate / (float) sampleRate_;
        const float smooth = 0.0015f;
        const float nyquist = (float) sampleRate_ * 0.45f;

        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            const float dryL = left[i];
            const float dryR = right[i];
            const float envTarget = std::max(std::abs(dryL), std::abs(dryR));
            envelope_ += (envTarget - envelope_)
                         * (envTarget > envelope_ ? attackCoeff : releaseCoeff);

            float lfo = 0.0f;
            switch (shape)
            {
                case 1: lfo = 1.0f - 4.0f * std::abs(lfoPhase_ - 0.5f); break;
                case 2: lfo = 2.0f * lfoPhase_ - 1.0f; break;
                case 3: lfo = lfoPhase_ < 0.5f ? 1.0f : -1.0f; break;
                default: lfo = std::sin(kTwoPi * lfoPhase_); break;
            }
            lfoPhase_ += lfoInc;
            if (lfoPhase_ >= 1.0f)
                lfoPhase_ -= 1.0f;

            const float targetLog =
                std::log2(baseCutoff) + envAmount * envelope_ * 4.0f
                + lfoAmount * lfo * 3.0f;
            cutoffLog_ += smooth * (targetLog - cutoffLog_);
            resonanceState_ += smooth * (resTarget - resonanceState_);
            driveDb_ += smooth * (driveTarget - driveDb_);
            mixState_ += smooth * (mixTarget - mixState_);
            outputDb_ += smooth * (outTarget - outputDb_);

            const float cutoff =
                juce::jlimit(20.0f, nyquist, std::exp2(cutoffLog_));
            const float g = std::tan(kPi * cutoff / (float) sampleRate_);
            const float k =
                2.0f - 1.98f * juce::jlimit(0.0f, 1.0f, resonanceState_);
            const float driveGain = juce::Decibels::decibelsToGain(driveDb_);
            const float driveComp = 1.0f / std::sqrt(juce::jmax(1.0f, driveGain));

            const float inL = std::tanh(dryL * driveGain) * driveComp;
            const float inR = std::tanh(dryR * driveGain) * driveComp;

            const auto l1 = filters_[0][0].process(inL, g, k);
            const auto r1 = filters_[1][0].process(inR, g, k);
            float wetL = 0.0f;
            float wetR = 0.0f;
            if (mode == 1)
            {
                const auto l2 = filters_[0][1].process(l1.low, g, k);
                const auto r2 = filters_[1][1].process(r1.low, g, k);
                wetL = l2.low * 0.8f;
                wetR = r2.low * 0.8f;
            }
            else if (mode == 2)
            {
                wetL = l1.band;
                wetR = r1.band;
            }
            else if (mode == 3)
            {
                wetL = l1.high;
                wetR = r1.high;
            }
            else
            {
                wetL = l1.low;
                wetR = r1.low;
            }

            const float gain =
                juce::Decibels::decibelsToGain(outputDb_);
            left[i] = (dryL + (wetL - dryL) * mixState_) * gain;
            right[i] = (dryR + (wetR - dryR) * mixState_) * gain;
        }
    }

private:
    juce::AudioParameterFloat* mix_ = nullptr;
    juce::AudioParameterFloat* cutoff_ = nullptr;
    juce::AudioParameterFloat* resonance_ = nullptr;
    juce::AudioParameterChoice* mode_ = nullptr;
    juce::AudioParameterFloat* drive_ = nullptr;
    juce::AudioParameterFloat* envAmount_ = nullptr;
    juce::AudioParameterFloat* attack_ = nullptr;
    juce::AudioParameterFloat* release_ = nullptr;
    juce::AudioParameterFloat* lfoRate_ = nullptr;
    juce::AudioParameterFloat* lfoAmount_ = nullptr;
    juce::AudioParameterChoice* lfoShape_ = nullptr;
    juce::AudioParameterFloat* output_ = nullptr;
    juce::AudioParameterBool* bypass_ = nullptr;

    std::array<std::array<Svf, 2>, 2> filters_ {};
    float cutoffLog_ = 10.0f;
    float resonanceState_ = 0.25f;
    float driveDb_ = 0.0f;
    float mixState_ = 1.0f;
    float outputDb_ = 0.0f;
    float envelope_ = 0.0f;
    float lfoPhase_ = 0.0f;
};

} // namespace

juce::AudioProcessor* create()
{
    return new EzAutoFilterProcessor();
}

} // namespace ezautofilter
