// EZ Reverb — implementation. See ezreverb.h.
//
// The reverb is deliberately allocation-free in processBlock: every buffer is
// sized in prepareToPlay and the controls are read once per block. The comb
// delay lengths follow the classic Freeverb tuning, scaled by Size and the
// selected Mode, and a slow LFO adds a small amount of movement to the tail.
#include "ezreverb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ezreverb {
namespace {

constexpr int kCombs = 8;
constexpr int kAllpasses = 4;
constexpr int kMaxChannels = 2;
constexpr float kTwoPi = 6.283185307179586f;

// Freeverb's tunings at 44.1 kHz. The right channel uses the same lengths
// plus a small offset, which is what gives the tail its stereo width.
constexpr int kCombTuning[kCombs] = {1116, 1188, 1277, 1356,
                                     1422, 1491, 1557, 1617};
constexpr int kAllpassTuning[kAllpasses] = {556, 441, 341, 225};
constexpr int kStereoSpread = 23;

inline int wrapIndex(int i, int n)
{
    while (i < 0)
        i += n;
    while (i >= n)
        i -= n;
    return i;
}

// Fractional circular delay line. The read tap is independent of the write
// position, so delay length can change without reallocating or resetting.
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

class Comb
{
public:
    void prepare(int maxSamples) { line_.prepare(maxSamples); filter_ = 0.0f; }

    inline float process(float input, float delay, float feedback, float damp)
    {
        const float out = line_.read(delay);
        filter_ = out + damp * (filter_ - out);
        line_.write(input + filter_ * feedback);
        return out;
    }

private:
    DelayLine line_;
    float filter_ = 0.0f;
};

class Allpass
{
public:
    void prepare(int maxSamples) { line_.prepare(maxSamples); }

    inline float process(float input, float delay, float g)
    {
        const float delayed = line_.read(delay);
        const float out = delayed - g * input;
        line_.write(input + g * out);
        return out;
    }

private:
    DelayLine line_;
};

class OnePole
{
public:
    void reset() { z_ = 0.0f; }

    inline float lowpass(float x, float a)
    {
        z_ += a * (x - z_);
        return z_;
    }

    inline float highpass(float x, float a) { return x - lowpass(x, a); }

private:
    float z_ = 0.0f;
};

struct CombBank
{
    std::array<Comb, kCombs> combs;
    std::array<float, kCombs> delay {};
};

class EzReverbProcessor final : public juce::AudioProcessor
{
public:
    EzReverbProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        mix_ = addFloat("mix", "Mix", 0.0f, 100.0f, 32.0f, "%");
        predelay_ = addFloat("pre", "Pre-delay", 0.0f, 200.0f, 22.0f, "ms");
        decay_ = addFloat("dec", "Decay", 0.2f, 12.0f, 2.8f, "s");
        size_ = addFloat("siz", "Size", 20.0f, 100.0f, 62.0f, "%");
        damp_ = addFloat("dmp", "Damping", 0.0f, 100.0f, 45.0f, "%");
        lowCut_ = addFloat("lcf", "Low cut", 20.0f, 1000.0f, 90.0f, "Hz");
        highCut_ = addFloat("hcf", "High cut", 1000.0f, 20000.0f, 12000.0f, "Hz");
        width_ = addFloat("wid", "Width", 0.0f, 100.0f, 100.0f, "%");
        mod_ = addFloat("mod", "Movement", 0.0f, 100.0f, 22.0f, "%");
        early_ = addFloat("erl", "Early level", 0.0f, 100.0f, 0.0f, "%");
        diffusion_ = addFloat("dif", "Diffusion", 0.0f, 100.0f, 70.0f, "%");
        output_ = addFloat("out", "Output", -24.0f, 12.0f, 0.0f, "dB");
        mode_ = addChoice("mode", "Mode", {"Hall", "Room", "Plate"}, 0);
        freeze_ = addBool("frz", "Freeze", false);
        bypass_ = addBool("byp", "Bypass", false);
    }

    const juce::String getName() const override { return "EZ Reverb"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    double getTailLengthSeconds() const override { return 12.0; }

    void prepareToPlay(double sampleRate, int) override
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        const double scale = sampleRate_ / 44100.0;
        const int combMax = (int) std::ceil((kCombTuning[kCombs - 1] + kStereoSpread)
                                            * 1.6 * scale) + 64;
        const int allpassMax = (int) std::ceil((kAllpassTuning[0] + kStereoSpread)
                                               * 1.6 * scale) + 64;
        const int preMax = (int) std::ceil(0.25 * sampleRate_) + 8;
        maxCombSamples_ = (float) combMax - 2.0f;
        predelaySamplesMax_ = preMax;

        for (auto& bank : banks_)
        {
            for (auto& c : bank.combs)
                c.prepare(combMax);
        }
        for (auto& line : allpasses_)
            line.prepare(allpassMax);
        for (auto& line : predelayLines_)
        {
            line.prepare(preMax);
            line.clear();
        }

        lfoPhase_ = 0.0f;
        lowCutState_[0].reset();
        lowCutState_[1].reset();
        highCutState_[0].reset();
        highCutState_[1].reset();
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        if (n <= 0 || buffer.getNumChannels() < 2)
            return;
        if (bypass_->get())
            return;

        const float mix = juce::jlimit(0.0f, 1.0f, mix_->get() / 100.0f);
        const float wet = mix;
        const float dry = 1.0f - mix;
        const float output = juce::Decibels::decibelsToGain(output_->get());
        const float decay = juce::jmax(0.1f, decay_->get());
        const float size = juce::jlimit(0.2f, 1.0f, size_->get() / 100.0f);
        const float damping = juce::jlimit(0.0f, 1.0f, damp_->get() / 100.0f) * 0.72f;
        const float width = juce::jlimit(0.0f, 1.0f, width_->get() / 100.0f);
        const float movement = juce::jlimit(0.0f, 1.0f, mod_->get() / 100.0f);
        const float early = juce::jlimit(0.0f, 1.0f, early_->get() / 100.0f);
        const float diffusion = juce::jlimit(0.0f, 1.0f, diffusion_->get() / 100.0f);
        const float preDelaySamples = juce::jlimit(
            0.0f, (float) juce::jmax(0, predelaySamplesMax_ - 2),
            predelay_->get() * 0.001f * (float) sampleRate_);
        const bool frozen = freeze_->get();

        const float lowA = coeffFor(lowCut_->get());
        const float highA = coeffFor(highCut_->get());

        // Mode changes the room's physical scale: a plate is small and dense,
        // a hall is large and slow, a room sits in between.
        float modeScale = 1.0f;
        float modeDecay = 1.0f;
        switch (mode_->getIndex())
        {
            case 1: modeScale = 0.68f; modeDecay = 0.72f; break;
            case 2: modeScale = 0.48f; modeDecay = 0.86f; break;
            default: break;
        }

        const double srScale = sampleRate_ / 44100.0;
        const float baseScale = size * modeScale * (float) srScale;
        const float decayTime = juce::jmax(0.15f, decay * modeDecay);
        const float modDepth = movement * 7.0f * (float) srScale;
        const float allpassG = 0.28f + 0.52f * diffusion;

        // Per-block comb delays and feedback. The modulation is evaluated at
        // block rate: reverb movement is slow, so this is enough and keeps the
        // inner loop cheap.
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            for (int c = 0; c < kCombs; ++c)
            {
                float base = (float) (kCombTuning[c] + ch * kStereoSpread);
                float d = base * baseScale;
                if (modDepth > 0.0f)
                    d += modDepth * std::sin(lfoPhase_ + (float) c * 0.73f
                                             + (float) ch * 1.31f);
                d = juce::jlimit(2.0f, maxCombSamples_, d);
                banks_[(size_t) ch].delay[(size_t) c] = d;
            }
        }
        lfoPhase_ += kTwoPi * 0.11f * (float) n / (float) sampleRate_;
        if (lfoPhase_ > kTwoPi)
            lfoPhase_ -= kTwoPi;

        float allpassDelay[kMaxChannels][kAllpasses];
        for (int ch = 0; ch < kMaxChannels; ++ch)
            for (int a = 0; a < kAllpasses; ++a)
                allpassDelay[ch][a] = (float) (kAllpassTuning[a] + ch * kStereoSpread)
                                      * baseScale;

        // Feedback is a per-comb constant for this block. Computing the pow()
        // here (instead of once per sample) is what keeps the reverb cheap
        // enough to sit on every channel of a large session.
        float combFeedback[kMaxChannels][kCombs];
        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            for (int c = 0; c < kCombs; ++c)
            {
                const float d = banks_[(size_t) ch].delay[(size_t) c];
                combFeedback[ch][c] = frozen
                    ? 0.9995f
                    : juce::jlimit(0.0f, 0.9995f,
                        std::pow(0.001f, d / (decayTime * (float) sampleRate_)));
            }
        }

        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        const float inputGain = frozen ? 0.0f : 0.12f;

        for (int i = 0; i < n; ++i)
        {
            const float inL = left[i];
            const float inR = right[i];

            // Stereo pre-delay. The same line is written once per sample and
            // read with the current predelay time.
            predelayLines_[0].write(inL);
            predelayLines_[1].write(inR);
            const float preL = predelayLines_[0].read(preDelaySamples);
            const float preR = predelayLines_[1].read(preDelaySamples);

            const float mid = (preL + preR) * 0.5f;
            const float side = (preL - preR) * 0.5f;
            const float feedL = (mid + side * 0.35f) * inputGain;
            const float feedR = (mid - side * 0.35f) * inputGain;

            float wetL = 0.0f;
            float wetR = 0.0f;
            for (int c = 0; c < kCombs; ++c)
            {
                const float dL = banks_[0].delay[(size_t) c];
                const float dR = banks_[1].delay[(size_t) c];
                wetL += banks_[0].combs[(size_t) c].process(
                    feedL, dL, combFeedback[0][c], damping);
                wetR += banks_[1].combs[(size_t) c].process(
                    feedR, dR, combFeedback[1][c], damping);
            }

            // Early level is a small amount of the pre-delayed input mixed
            // before diffusion, giving the first transient more definition.
            wetL += preL * early * 0.25f;
            wetR += preR * early * 0.25f;

            for (int a = 0; a < kAllpasses; ++a)
            {
                wetL = allpasses_[(size_t) a].process(wetL, allpassDelay[0][a], allpassG);
                wetR = allpasses_[(size_t) a].process(wetR, allpassDelay[1][a], allpassG);
            }

            wetL = lowCutState_[0].highpass(wetL, lowA);
            wetR = lowCutState_[1].highpass(wetR, lowA);
            wetL = highCutState_[0].lowpass(wetL, highA);
            wetR = highCutState_[1].lowpass(wetR, highA);

            const float midWet = (wetL + wetR) * 0.5f;
            const float sideWet = (wetL - wetR) * 0.5f * width;
            const float outL = ((midWet + sideWet) * wet + inL * dry) * output;
            const float outR = ((midWet - sideWet) * wet + inR * dry) * output;
            left[i] = outL;
            right[i] = outR;
        }
    }

    void getStateInformation(juce::MemoryBlock& data) override
    {
        juce::XmlElement xml("EZReverb");
        save(xml);
        juce::AudioProcessor::copyXmlToBinary(xml, data);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
        if (xml == nullptr || xml->getTagName() != "EZReverb")
            return;
        load(*xml);
    }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

private:
    juce::AudioParameterFloat* addFloat(const char* id, const char* name,
                                        float minValue, float maxValue, float defaultValue,
                                        const juce::String& unit = {})
    {
        juce::AudioParameterFloatAttributes attributes;
        if (unit.isNotEmpty())
            attributes = attributes.withLabel(unit)
                .withStringFromValueFunction(
                    [unit](float value, int) { return formatValue(value, unit); });
        auto* p = new juce::AudioParameterFloat(
            juce::ParameterID{id, 1}, name,
            juce::NormalisableRange<float>(minValue, maxValue), defaultValue,
            attributes);
        addParameter(p);
        return p;
    }

    static juce::String formatValue(float value, const juce::String& unit)
    {
        if (unit == "%")
            return juce::String(value, 1) + " %";
        if (unit == "dB")
            return (value > 0.0f ? "+" : "") + juce::String(value, 1) + " dB";
        if (unit == "Hz")
        {
            if (value >= 1000.0f)
                return juce::String(value / 1000.0f, 2) + " kHz";
            return juce::String(value, 0) + " Hz";
        }
        if (unit == "ms")
        {
            if (value >= 1000.0f)
                return juce::String(value / 1000.0f, 2) + " s";
            return juce::String(value, 0) + " ms";
        }
        if (unit == "s")
            return juce::String(value, 2) + " s";
        return juce::String(value, 2);
    }

    juce::AudioParameterChoice* addChoice(const char* id, const char* name,
                                          const juce::StringArray& choices,
                                          int defaultIndex)
    {
        auto* p = new juce::AudioParameterChoice(
            juce::ParameterID{id, 1}, name, choices, defaultIndex);
        addParameter(p);
        return p;
    }

    juce::AudioParameterBool* addBool(const char* id, const char* name, bool defaultValue)
    {
        auto* p = new juce::AudioParameterBool(
            juce::ParameterID{id, 1}, name, defaultValue);
        addParameter(p);
        return p;
    }

    float coeffFor(float hz) const
    {
        const float x = juce::jlimit(0.0f, 0.49f,
                                     hz / (float) sampleRate_);
        return 1.0f - std::exp(-kTwoPi * x);
    }

    void save(juce::XmlElement& xml) const
    {
        xml.setAttribute("mix", mix_->get());
        xml.setAttribute("pre", predelay_->get());
        xml.setAttribute("dec", decay_->get());
        xml.setAttribute("siz", size_->get());
        xml.setAttribute("dmp", damp_->get());
        xml.setAttribute("lcf", lowCut_->get());
        xml.setAttribute("hcf", highCut_->get());
        xml.setAttribute("wid", width_->get());
        xml.setAttribute("mod", mod_->get());
        xml.setAttribute("erl", early_->get());
        xml.setAttribute("dif", diffusion_->get());
        xml.setAttribute("out", output_->get());
        xml.setAttribute("mode", mode_->getIndex());
        xml.setAttribute("frz", freeze_->get() ? 1 : 0);
        xml.setAttribute("byp", bypass_->get() ? 1 : 0);
    }

    void load(const juce::XmlElement& xml)
    {
        const auto set = [this, &xml](juce::AudioParameterFloat* p, const char* id) {
            p->setValueNotifyingHost(p->convertTo0to1(
                (float) xml.getDoubleAttribute(id, p->get())));
        };
        set(mix_, "mix");
        set(predelay_, "pre");
        set(decay_, "dec");
        set(size_, "siz");
        set(damp_, "dmp");
        set(lowCut_, "lcf");
        set(highCut_, "hcf");
        set(width_, "wid");
        set(mod_, "mod");
        set(early_, "erl");
        set(diffusion_, "dif");
        set(output_, "out");
        mode_->setValueNotifyingHost(mode_->convertTo0to1(
            (float) xml.getIntAttribute("mode", mode_->getIndex())));
        freeze_->setValueNotifyingHost(xml.getBoolAttribute("frz", freeze_->get()) ? 1.0f : 0.0f);
        bypass_->setValueNotifyingHost(xml.getBoolAttribute("byp", bypass_->get()) ? 1.0f : 0.0f);
    }

    juce::AudioParameterFloat* mix_ = nullptr;
    juce::AudioParameterFloat* predelay_ = nullptr;
    juce::AudioParameterFloat* decay_ = nullptr;
    juce::AudioParameterFloat* size_ = nullptr;
    juce::AudioParameterFloat* damp_ = nullptr;
    juce::AudioParameterFloat* lowCut_ = nullptr;
    juce::AudioParameterFloat* highCut_ = nullptr;
    juce::AudioParameterFloat* width_ = nullptr;
    juce::AudioParameterFloat* mod_ = nullptr;
    juce::AudioParameterFloat* early_ = nullptr;
    juce::AudioParameterFloat* diffusion_ = nullptr;
    juce::AudioParameterFloat* output_ = nullptr;
    juce::AudioParameterChoice* mode_ = nullptr;
    juce::AudioParameterBool* freeze_ = nullptr;
    juce::AudioParameterBool* bypass_ = nullptr;

    double sampleRate_ = 48000.0;
    int predelaySamplesMax_ = 1;
    float maxCombSamples_ = 1024.0f;
    float lfoPhase_ = 0.0f;
    std::array<CombBank, kMaxChannels> banks_;
    std::array<Allpass, kAllpasses> allpasses_;
    std::array<DelayLine, kMaxChannels> predelayLines_;
    std::array<OnePole, kMaxChannels> lowCutState_;
    std::array<OnePole, kMaxChannels> highCutState_;
};

} // namespace

juce::AudioProcessor* create()
{
    return new EzReverbProcessor();
}

} // namespace ezreverb
