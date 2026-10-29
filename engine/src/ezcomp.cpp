#include "ezcomp.h"

#include <cmath>

namespace ezcomp {

namespace {
constexpr const char* kThreshold = "thr";
constexpr const char* kRatio = "rat";
constexpr const char* kAttack = "att";
constexpr const char* kRelease = "rel";
constexpr const char* kKnee = "kne";
constexpr const char* kMakeup = "mak";
constexpr const char* kMix = "mix";
constexpr const char* kBypass = "byp";

class EzCompProcessor final : public juce::AudioProcessor {
public:
    EzCompProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        threshold_ = add("thr", "Threshold", juce::NormalisableRange<float>(-60.f, 0.f, 0.1f), -18.f);
        ratio_ = add("rat", "Ratio", juce::NormalisableRange<float>(1.f, 20.f, 0.1f), 3.f);
        attack_ = add("att", "Attack", juce::NormalisableRange<float>(0.1f, 200.f, 0.f, 0.35f), 10.f);
        release_ = add("rel", "Release", juce::NormalisableRange<float>(5.f, 1000.f, 0.f, 0.35f), 120.f);
        knee_ = add("kne", "Knee", juce::NormalisableRange<float>(0.f, 24.f, 0.1f), 6.f);
        makeup_ = add("mak", "Makeup", juce::NormalisableRange<float>(-12.f, 24.f, 0.1f), 0.f);
        mix_ = add("mix", "Mix", juce::NormalisableRange<float>(0.f, 100.f, 0.1f), 100.f);
        bypass_ = new juce::AudioParameterBool(juce::ParameterID{kBypass, 1}, "Bypass", false);
        addParameter(bypass_);
    }

    const juce::String getName() const override { return "EZ Compressor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void prepareToPlay(double sampleRate, int) override
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        envelope_ = 0.0f;
        gainDb_ = 0.0f;
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int n = buffer.getNumSamples();
        if (n <= 0 || buffer.getNumChannels() < 2)
            return;
        if (bypass_->get())
            return;

        const float threshold = threshold_->get();
        const float ratio = juce::jmax(1.0f, ratio_->get());
        const float knee = juce::jmax(0.0f, knee_->get());
        const float makeup = juce::Decibels::decibelsToGain(makeup_->get());
        const float mix = juce::jlimit(0.0f, 1.0f, mix_->get() / 100.0f);
        const float attackMs = juce::jmax(0.1f, attack_->get());
        const float releaseMs = juce::jmax(1.0f, release_->get());
        const float attack = std::exp(-1.0f / (float) (attackMs * 0.001 * sampleRate_));
        const float release = std::exp(-1.0f / (float) (releaseMs * 0.001 * sampleRate_));

        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        for (int i = 0; i < n; ++i)
        {
            const float l = left[i];
            const float r = right[i];
            const float peak = juce::jmax(std::abs(l), std::abs(r));
            const float level = juce::Decibels::gainToDecibels(peak, -120.0f);

            // Soft knee curve in the dB domain: reduction is the difference
            // between the input level and the compressed output level.
            float over = level - threshold;
            float reduction = 0.0f;
            if (over > knee * 0.5f)
            {
                reduction = over - over / ratio;
            }
            else if (over > -knee * 0.5f && knee > 0.0f)
            {
                const float t = over + knee * 0.5f;
                reduction = (1.0f - 1.0f / ratio) * t * t / (2.0f * knee);
            }

            const float coeff = reduction > gainDb_ ? attack : release;
            gainDb_ = coeff * gainDb_ + (1.0f - coeff) * reduction;
            const float gain = juce::Decibels::decibelsToGain(-gainDb_) * makeup;
            left[i] = l * (1.0f - mix) + l * gain * mix;
            right[i] = r * (1.0f - mix) + r * gain * mix;
            envelope_ = juce::jmax(envelope_, peak);
        }
    }

    void getStateInformation(juce::MemoryBlock& data) override
    {
        juce::XmlElement xml("EZComp");
        xml.setAttribute("thr", threshold_->get());
        xml.setAttribute("rat", ratio_->get());
        xml.setAttribute("att", attack_->get());
        xml.setAttribute("rel", release_->get());
        xml.setAttribute("kne", knee_->get());
        xml.setAttribute("mak", makeup_->get());
        xml.setAttribute("mix", mix_->get());
        xml.setAttribute("byp", bypass_->get() ? 1 : 0);
        juce::AudioProcessor::copyXmlToBinary(xml, data);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
        if (xml == nullptr || xml->getTagName() != "EZComp")
            return;
        threshold_->setValueNotifyingHost(threshold_->convertTo0to1(
            (float) xml->getDoubleAttribute("thr", threshold_->get())));
        ratio_->setValueNotifyingHost(ratio_->convertTo0to1(
            (float) xml->getDoubleAttribute("rat", ratio_->get())));
        attack_->setValueNotifyingHost(attack_->convertTo0to1(
            (float) xml->getDoubleAttribute("att", attack_->get())));
        release_->setValueNotifyingHost(release_->convertTo0to1(
            (float) xml->getDoubleAttribute("rel", release_->get())));
        knee_->setValueNotifyingHost(knee_->convertTo0to1(
            (float) xml->getDoubleAttribute("kne", knee_->get())));
        makeup_->setValueNotifyingHost(makeup_->convertTo0to1(
            (float) xml->getDoubleAttribute("mak", makeup_->get())));
        mix_->setValueNotifyingHost(mix_->convertTo0to1(
            (float) xml->getDoubleAttribute("mix", mix_->get())));
        bypass_->setValueNotifyingHost(xml->getBoolAttribute("byp", false) ? 1.0f : 0.0f);
    }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

private:
    juce::AudioParameterFloat* add(const juce::String& id, const juce::String& name,
                                   juce::NormalisableRange<float> range, float value)
    {
        auto* p = new juce::AudioParameterFloat(juce::ParameterID{id, 1}, name, range, value);
        addParameter(p);
        return p;
    }

    juce::AudioParameterFloat* threshold_ = nullptr;
    juce::AudioParameterFloat* ratio_ = nullptr;
    juce::AudioParameterFloat* attack_ = nullptr;
    juce::AudioParameterFloat* release_ = nullptr;
    juce::AudioParameterFloat* knee_ = nullptr;
    juce::AudioParameterFloat* makeup_ = nullptr;
    juce::AudioParameterFloat* mix_ = nullptr;
    juce::AudioParameterBool* bypass_ = nullptr;
    double sampleRate_ = 48000.0;
    float envelope_ = 0.0f;
    float gainDb_ = 0.0f;
};

} // namespace

juce::AudioProcessor* create()
{
    return new EzCompProcessor();
}

} // namespace ezcomp
