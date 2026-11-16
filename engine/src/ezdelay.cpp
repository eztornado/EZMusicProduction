// EZ Delay — implementation. See ezdelay.h.
//
// The audio thread never allocates: the two lines are sized for two seconds
// in prepareToPlay. Delay time is smoothed at block rate so tempo changes and
// manual time moves do not click, and the feedback path is band-limited by a
// low cut and a high cut.
#include "ezdelay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace ezdelay {
namespace {

constexpr int kMaxChannels = 2;
constexpr float kTwoPi = 6.283185307179586f;

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

class EzDelayProcessor final : public juce::AudioProcessor
{
public:
    EzDelayProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        sync_ = addBool("sync", "Sync", true);
        division_ = addChoice("div", "Division",
                              {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                               "1/4 D", "1/8 D", "1/4 T", "1/8 T"}, 3);
        timeMs_ = addFloat("tim", "Time", 1.0f, 2000.0f, 375.0f, "ms");
        feedback_ = addFloat("fbk", "Feedback", 0.0f, 95.0f, 36.0f, "%");
        stereo_ = addFloat("ste", "Stereo offset", 0.0f, 100.0f, 8.0f, "%");
        lowCut_ = addFloat("lcf", "Low cut", 20.0f, 2000.0f, 120.0f, "Hz");
        highCut_ = addFloat("hcf", "High cut", 1000.0f, 20000.0f, 9000.0f, "Hz");
        mix_ = addFloat("mix", "Mix", 0.0f, 100.0f, 30.0f, "%");
        output_ = addFloat("out", "Output", -24.0f, 12.0f, 0.0f, "dB");
        pingPong_ = addBool("png", "Ping-pong", false);
        freeze_ = addBool("frz", "Freeze", false);
        bypass_ = addBool("byp", "Bypass", false);
    }

    const juce::String getName() const override { return "EZ Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    double getTailLengthSeconds() const override { return 4.0; }

    void prepareToPlay(double sampleRate, int) override
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int maxSamples = (int) std::ceil(2.05 * sampleRate_) + 8;
        lines_[0].prepare(maxSamples);
        lines_[1].prepare(maxSamples);
        lines_[0].clear();
        lines_[1].clear();
        maxDelaySamples_ = (float) maxSamples - 2.0f;
        currentDelayL_ = currentDelayR_ = 0.0f;
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

        const double bpm = currentBpm();
        const double quarterMs = 60000.0 / juce::jmax(20.0, bpm);
        double delayMs = timeMs_->get();
        if (sync_->get())
        {
            const double beats = divisionBeats(division_->getIndex());
            delayMs = quarterMs * beats;
        }
        const float targetL = juce::jlimit(
            1.0f, maxDelaySamples_, (float) (delayMs * 0.001 * sampleRate_));
        const float offset = 1.0f + stereo_->get() * 0.01f * 0.5f;
        const float targetR = juce::jlimit(1.0f, maxDelaySamples_, targetL * offset);

        // One-pole glide at ~30 ms. Long enough to suppress clicks, short
        // enough that a tempo change is heard as a quick portamento.
        const float glide = 1.0f - std::exp(-(float) n / (0.030f * (float) sampleRate_));
        if (currentDelayL_ <= 0.0f)
        {
            currentDelayL_ = targetL;
            currentDelayR_ = targetR;
        }
        currentDelayL_ += glide * (targetL - currentDelayL_);
        currentDelayR_ += glide * (targetR - currentDelayR_);

        const float feedback = juce::jlimit(0.0f, 0.95f, feedback_->get() * 0.01f);
        const float mix = juce::jlimit(0.0f, 1.0f, mix_->get() * 0.01f);
        const float dry = 1.0f - mix;
        const float output = juce::Decibels::decibelsToGain(output_->get());
        const bool pingPong = pingPong_->get();
        const bool frozen = freeze_->get();
        const float lowA = coeffFor(lowCut_->get());
        const float highA = coeffFor(highCut_->get());
        const float inputGain = frozen ? 0.0f : 1.0f;
        const float fb = frozen ? 0.9995f : feedback;

        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        for (int i = 0; i < n; ++i)
        {
            const float inL = left[i];
            const float inR = right[i];
            const float dL = lines_[0].read(currentDelayL_);
            const float dR = lines_[1].read(currentDelayR_);

            float wetL = lowCutState_[0].highpass(dL, lowA);
            wetL = highCutState_[0].lowpass(wetL, highA);
            float wetR = lowCutState_[1].highpass(dR, lowA);
            wetR = highCutState_[1].lowpass(wetR, highA);

            const float fbL = wetL * fb;
            const float fbR = wetR * fb;
            if (pingPong)
            {
                lines_[0].write(inL * inputGain + fbR);
                lines_[1].write(inR * inputGain + fbL);
            }
            else
            {
                lines_[0].write(inL * inputGain + fbL);
                lines_[1].write(inR * inputGain + fbR);
            }

            left[i] = (inL * dry + dL * mix) * output;
            right[i] = (inR * dry + dR * mix) * output;
        }
    }

    void getStateInformation(juce::MemoryBlock& data) override
    {
        juce::XmlElement xml("EZDelay");
        save(xml);
        juce::AudioProcessor::copyXmlToBinary(xml, data);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
        if (xml == nullptr || xml->getTagName() != "EZDelay")
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

    double currentBpm() const
    {
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition())
                if (pos->getBpm().hasValue() && *pos->getBpm() > 0.0)
                    return *pos->getBpm();
        return 120.0;
    }

    static double divisionBeats(int index)
    {
        // The first six are straight note values; the last four are dotted
        // and triplet variants.
        static constexpr double values[] = {
            4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
            1.5, 0.75, 2.0 / 3.0, 1.0 / 3.0
        };
        return values[juce::jlimit(0, 9, index)];
    }

    float coeffFor(float hz) const
    {
        const float x = juce::jlimit(0.0f, 0.49f, hz / (float) sampleRate_);
        return 1.0f - std::exp(-kTwoPi * x);
    }

    void save(juce::XmlElement& xml) const
    {
        xml.setAttribute("sync", sync_->get() ? 1 : 0);
        xml.setAttribute("div", division_->getIndex());
        xml.setAttribute("tim", timeMs_->get());
        xml.setAttribute("fbk", feedback_->get());
        xml.setAttribute("ste", stereo_->get());
        xml.setAttribute("lcf", lowCut_->get());
        xml.setAttribute("hcf", highCut_->get());
        xml.setAttribute("mix", mix_->get());
        xml.setAttribute("out", output_->get());
        xml.setAttribute("png", pingPong_->get() ? 1 : 0);
        xml.setAttribute("frz", freeze_->get() ? 1 : 0);
        xml.setAttribute("byp", bypass_->get() ? 1 : 0);
    }

    void load(const juce::XmlElement& xml)
    {
        const auto set = [this, &xml](juce::AudioParameterFloat* p, const char* id) {
            p->setValueNotifyingHost(p->convertTo0to1(
                (float) xml.getDoubleAttribute(id, p->get())));
        };
        set(timeMs_, "tim");
        set(feedback_, "fbk");
        set(stereo_, "ste");
        set(lowCut_, "lcf");
        set(highCut_, "hcf");
        set(mix_, "mix");
        set(output_, "out");
        sync_->setValueNotifyingHost(xml.getBoolAttribute("sync", sync_->get()) ? 1.0f : 0.0f);
        division_->setValueNotifyingHost(division_->convertTo0to1(
            (float) xml.getIntAttribute("div", division_->getIndex())));
        pingPong_->setValueNotifyingHost(xml.getBoolAttribute("png", pingPong_->get()) ? 1.0f : 0.0f);
        freeze_->setValueNotifyingHost(xml.getBoolAttribute("frz", freeze_->get()) ? 1.0f : 0.0f);
        bypass_->setValueNotifyingHost(xml.getBoolAttribute("byp", bypass_->get()) ? 1.0f : 0.0f);
    }

    juce::AudioParameterBool* sync_ = nullptr;
    juce::AudioParameterChoice* division_ = nullptr;
    juce::AudioParameterFloat* timeMs_ = nullptr;
    juce::AudioParameterFloat* feedback_ = nullptr;
    juce::AudioParameterFloat* stereo_ = nullptr;
    juce::AudioParameterFloat* lowCut_ = nullptr;
    juce::AudioParameterFloat* highCut_ = nullptr;
    juce::AudioParameterFloat* mix_ = nullptr;
    juce::AudioParameterFloat* output_ = nullptr;
    juce::AudioParameterBool* pingPong_ = nullptr;
    juce::AudioParameterBool* freeze_ = nullptr;
    juce::AudioParameterBool* bypass_ = nullptr;

    double sampleRate_ = 48000.0;
    float maxDelaySamples_ = 44100.0f;
    float currentDelayL_ = 0.0f;
    float currentDelayR_ = 0.0f;
    std::array<DelayLine, kMaxChannels> lines_;
    std::array<OnePole, kMaxChannels> lowCutState_;
    std::array<OnePole, kMaxChannels> highCutState_;
};

} // namespace

juce::AudioProcessor* create()
{
    return new EzDelayProcessor();
}

} // namespace ezdelay
