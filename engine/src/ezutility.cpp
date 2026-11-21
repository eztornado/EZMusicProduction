// EZ Utility — implementation. See ezutility.h and ezfx.h.
//
// The pan law is a stereo balance (unity at centre), which is what a utility
// insert should do: it must not attenuate a centred signal just because it is
// in the chain. Width uses mid/side and mono forces the side component to zero.
#include "ezutility.h"

#include "ezfx.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ezutility {
namespace {

constexpr float kHalfPi = 1.57079632679489661923f;

struct OnePole
{
    inline float lowpass(float input, float a)
    {
        z += a * (input - z);
        return z;
    }

    inline float highpass(float input, float a)
    {
        return input - lowpass(input, a);
    }

    void reset() { z = 0.0f; }

    float z = 0.0f;
};

class EzUtilityProcessor final : public ezfx::Effect
{
public:
    EzUtilityProcessor()
        : Effect("EZ Utility", "EZUtility")
    {
        gain_ = addFloat("gain", "Gain", -24.0f, 24.0f, 0.0f, "dB");
        pan_ = addFloat("pan", "Pan", -100.0f, 100.0f, 0.0f, "%");
        width_ = addFloat("wid", "Width", 0.0f, 200.0f, 100.0f, "%");
        lowCut_ = addFloat("lcf", "Low cut", 20.0f, 500.0f, 20.0f, "Hz", 100.0f);
        highCut_ = addFloat("hcf", "High cut", 1000.0f, 20000.0f, 20000.0f,
                            "Hz", 8000.0f);
        mono_ = addBool("mono", "Mono", false);
        invertL_ = addBool("invl", "Invert left", false);
        invertR_ = addBool("invr", "Invert right", false);
        bypass_ = addBool("byp", "Bypass", false);
    }

    void prepareDsp(double, int) override
    {
        for (auto& s : lowCutState_)
            s.reset();
        for (auto& s : highCutState_)
            s.reset();
        gainDb_ = gain_->get();
        panState_ = juce::jlimit(-1.0f, 1.0f, pan_->get() / 100.0f);
        widthState_ = juce::jlimit(0.0f, 2.0f, width_->get() / 100.0f);
    }

    void releaseDsp() override
    {
        for (auto& s : lowCutState_)
            s.reset();
        for (auto& s : highCutState_)
            s.reset();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        if (n <= 0 || buffer.getNumChannels() < 2 || bypass_->get())
            return;

        const float gainTarget = gain_->get();
        const float panTarget = juce::jlimit(-1.0f, 1.0f, pan_->get() / 100.0f);
        const float widthTarget =
            juce::jlimit(0.0f, 2.0f, width_->get() / 100.0f);
        const bool mono = mono_->get();
        const bool invertL = invertL_->get();
        const bool invertR = invertR_->get();
        const float lowA = ezfx::Effect::coeffFor(lowCut_->get(), sampleRate_);
        const float highA = ezfx::Effect::coeffFor(highCut_->get(), sampleRate_);
        const float smooth = 0.0015f;

        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            gainDb_ += smooth * (gainTarget - gainDb_);
            panState_ += smooth * (panTarget - panState_);
            widthState_ += smooth * (widthTarget - widthState_);

            float inL = left[i];
            float inR = right[i];

            const float mid = (inL + inR) * 0.5f;
            const float side = (inL - inR) * 0.5f
                             * (mono ? 0.0f : widthState_);
            inL = mid + side;
            inR = mid - side;

            const float pan = juce::jlimit(-1.0f, 1.0f, panState_);
            const float leftGain =
                pan <= 0.0f ? 1.0f : std::cos(pan * kHalfPi);
            const float rightGain =
                pan >= 0.0f ? 1.0f : std::cos(-pan * kHalfPi);
            inL *= leftGain;
            inR *= rightGain;

            if (invertL)
                inL = -inL;
            if (invertR)
                inR = -inR;

            inL = lowCutState_[0].highpass(inL, lowA);
            inR = lowCutState_[1].highpass(inR, lowA);
            inL = highCutState_[0].lowpass(inL, highA);
            inR = highCutState_[1].lowpass(inR, highA);

            const float gain = juce::Decibels::decibelsToGain(gainDb_);
            left[i] = inL * gain;
            right[i] = inR * gain;
        }
    }

private:
    juce::AudioParameterFloat* gain_ = nullptr;
    juce::AudioParameterFloat* pan_ = nullptr;
    juce::AudioParameterFloat* width_ = nullptr;
    juce::AudioParameterFloat* lowCut_ = nullptr;
    juce::AudioParameterFloat* highCut_ = nullptr;
    juce::AudioParameterBool* mono_ = nullptr;
    juce::AudioParameterBool* invertL_ = nullptr;
    juce::AudioParameterBool* invertR_ = nullptr;
    juce::AudioParameterBool* bypass_ = nullptr;

    std::array<OnePole, 2> lowCutState_ {};
    std::array<OnePole, 2> highCutState_ {};
    float gainDb_ = 0.0f;
    float panState_ = 0.0f;
    float widthState_ = 1.0f;
};

} // namespace

juce::AudioProcessor* create()
{
    return new EzUtilityProcessor();
}

} // namespace ezutility
