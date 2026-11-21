// EZ Chorus — implementation. See ezchorus.h and ezfx.h.
//
// Up to four modulated taps per channel. Each voice has its own LFO phase and
// a slightly different rate, which avoids the metallic "one delay line" sound.
// The right channel uses a quarter-cycle offset for a wide stereo image.
#include "ezchorus.h"

#include "ezfx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ezchorus {
namespace {

constexpr int kMaxVoices = 4;
constexpr float kTwoPi = 6.28318530717958647692f;

class DelayLine
{
public:
    void prepare(int maxSamples)
    {
        buffer_.assign((size_t) std::max(8, maxSamples), 0.0f);
        write_ = 0;
    }

    void clear() { std::fill(buffer_.begin(), buffer_.end(), 0.0f); }

    inline float read(float delaySamples) const
    {
        const int n = (int) buffer_.size();
        float pos = (float) write_ - delaySamples;
        while (pos < 0.0f)
            pos += (float) n;
        while (pos >= (float) n)
            pos -= (float) n;
        const int i0 = (int) pos;
        const float frac = pos - (float) i0;
        const int i1 = (i0 + 1 >= n) ? 0 : i0 + 1;
        return buffer_[(size_t) i0] * (1.0f - frac)
             + buffer_[(size_t) i1] * frac;
    }

    inline void write(float value)
    {
        buffer_[(size_t) write_] = value;
        if (++write_ >= (int) buffer_.size())
            write_ = 0;
    }

private:
    std::vector<float> buffer_;
    int write_ = 0;
};

class EzChorusProcessor final : public ezfx::Effect
{
public:
    EzChorusProcessor()
        : Effect("EZ Chorus", "EZChorus", 0.25)
    {
        mix_ = addFloat("mix", "Mix", 0.0f, 100.0f, 40.0f, "%");
        rate_ = addFloat("rate", "Rate", 0.05f, 5.0f, 0.8f, "Hz", 1.0f);
        depth_ = addFloat("dep", "Depth", 0.0f, 100.0f, 45.0f, "%");
        feedback_ = addFloat("fbk", "Feedback", -95.0f, 95.0f, 10.0f, "%");
        width_ = addFloat("wid", "Width", 0.0f, 200.0f, 100.0f, "%");
        delay_ = addFloat("dly", "Delay", 2.0f, 30.0f, 10.0f, "ms", 10.0f);
        voices_ = addChoice("voc", "Voices",
                            {"2 voices", "3 voices", "4 voices"}, 1);
        mode_ = addChoice("mode", "Mode",
                          {"Chorus", "Flanger", "Ensemble"}, 0);
        output_ = addFloat("out", "Output", -24.0f, 12.0f, 0.0f, "dB");
        bypass_ = addBool("byp", "Bypass", false);
    }

    void prepareDsp(double sampleRate, int) override
    {
        sampleRate_ = sampleRate;
        const int maxSamples = (int) std::ceil(sampleRate * 0.07) + 8;
        for (auto& line : left_)
            line.prepare(maxSamples);
        for (auto& line : right_)
            line.prepare(maxSamples);
        for (auto& line : left_)
            line.clear();
        for (auto& line : right_)
            line.clear();
        lfoPhase_ = 0.0f;
        baseDelay_ = juce::jlimit(1.0f, 45.0f, delay_->get());
        mixState_ = juce::jlimit(0.0f, 1.0f, mix_->get() / 100.0f);
        widthState_ = juce::jlimit(0.0f, 2.0f, width_->get() / 100.0f);
        outputDb_ = output_->get();
    }

    void releaseDsp() override
    {
        for (auto& line : left_)
            line.clear();
        for (auto& line : right_)
            line.clear();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        if (n <= 0 || buffer.getNumChannels() < 2 || bypass_->get())
            return;

        const int voiceCount = juce::jlimit(2, kMaxVoices, voices_->getIndex() + 2);
        const int mode = mode_->getIndex();
        float modeScale = 1.0f;
        float modeDepth = 7.0f;
        if (mode == 1)
        {
            modeScale = 0.35f;
            modeDepth = 3.0f;
        }
        else if (mode == 2)
        {
            modeScale = 1.6f;
            modeDepth = 11.0f;
        }

        const float delayTarget = juce::jlimit(
            2.0f, 45.0f, delay_->get() * modeScale);
        const float mixTarget = juce::jlimit(0.0f, 1.0f, mix_->get() / 100.0f);
        const float widthTarget = juce::jlimit(0.0f, 2.0f, width_->get() / 100.0f);
        const float outTarget = output_->get();
        const float feedback = juce::jlimit(-0.95f, 0.95f, feedback_->get() / 100.0f);
        const float depth = juce::jlimit(0.0f, 1.0f, depth_->get() / 100.0f);
        const float rate = juce::jmax(0.01f, rate_->get());
        const float lfoInc = rate / (float) sampleRate_;
        const float maxDelay = (float) ((int) std::ceil(sampleRate_ * 0.07));

        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            const float dryL = left[i];
            const float dryR = right[i];
            baseDelay_ += 0.0015f * (delayTarget - baseDelay_);
            mixState_ += 0.0015f * (mixTarget - mixState_);
            widthState_ += 0.0015f * (widthTarget - widthState_);
            outputDb_ += 0.0015f * (outTarget - outputDb_);

            const float baseSamples = baseDelay_ * 0.001f * (float) sampleRate_;
            const float depthSamples =
                depth * modeDepth * 0.001f * (float) sampleRate_;

            float wetL = 0.0f;
            float wetR = 0.0f;
            for (int v = 0; v < voiceCount; ++v)
            {
                const float phaseL =
                    lfoPhase_ + (float) v * 0.173f;
                const float phaseR = phaseL + 0.25f;
                const float lfoL = std::sin(kTwoPi * phaseL);
                const float lfoR = std::sin(kTwoPi * phaseR);
                const float dL = juce::jlimit(
                    1.0f, maxDelay, baseSamples + depthSamples * lfoL);
                const float dR = juce::jlimit(
                    1.0f, maxDelay, baseSamples + depthSamples * lfoR);
                const float tapL = left_[v].read(dL);
                const float tapR = right_[v].read(dR);
                wetL += tapL;
                wetR += tapR;
                left_[v].write(dryL + tapL * feedback);
                right_[v].write(dryR + tapR * feedback);
            }
            lfoPhase_ += lfoInc;
            if (lfoPhase_ >= 1.0f)
                lfoPhase_ -= 1.0f;

            wetL /= (float) voiceCount;
            wetR /= (float) voiceCount;
            const float mid = (wetL + wetR) * 0.5f;
            const float side = (wetL - wetR) * 0.5f * widthState_;
            wetL = mid + side;
            wetR = mid - side;

            const float gain = juce::Decibels::decibelsToGain(outputDb_);
            left[i] = (dryL + (wetL - dryL) * mixState_) * gain;
            right[i] = (dryR + (wetR - dryR) * mixState_) * gain;
        }
    }

private:
    juce::AudioParameterFloat* mix_ = nullptr;
    juce::AudioParameterFloat* rate_ = nullptr;
    juce::AudioParameterFloat* depth_ = nullptr;
    juce::AudioParameterFloat* feedback_ = nullptr;
    juce::AudioParameterFloat* width_ = nullptr;
    juce::AudioParameterFloat* delay_ = nullptr;
    juce::AudioParameterChoice* voices_ = nullptr;
    juce::AudioParameterChoice* mode_ = nullptr;
    juce::AudioParameterFloat* output_ = nullptr;
    juce::AudioParameterBool* bypass_ = nullptr;

    std::array<DelayLine, kMaxVoices> left_;
    std::array<DelayLine, kMaxVoices> right_;
    float lfoPhase_ = 0.0f;
    float baseDelay_ = 10.0f;
    float mixState_ = 0.4f;
    float widthState_ = 1.0f;
    float outputDb_ = 0.0f;
};

} // namespace

juce::AudioProcessor* create()
{
    return new EzChorusProcessor();
}

} // namespace ezchorus
