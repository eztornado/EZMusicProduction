#include "ezlim.h"

#include <cmath>
#include <deque>
#include <vector>

namespace ezlim {

namespace {
constexpr const char* kInputGain = "in";
constexpr const char* kCeiling = "cei";
constexpr const char* kRelease = "rel";
constexpr const char* kLookahead = "look";
constexpr const char* kBypass = "byp";

class EzLimProcessor final : public juce::AudioProcessor {
public:
    EzLimProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    {
        inputGain_ = add("in", "Input gain", juce::NormalisableRange<float>(-12.f, 24.f, 0.1f), 0.f);
        ceiling_ = add("cei", "Ceiling", juce::NormalisableRange<float>(-20.f, 0.f, 0.1f), -1.f);
        release_ = add("rel", "Release", juce::NormalisableRange<float>(10.f, 500.f, 0.f, 0.35f), 80.f);
        lookahead_ = add("look", "Look-ahead", juce::NormalisableRange<float>(0.f, 10.f, 0.1f), 3.f);
        bypass_ = new juce::AudioParameterBool(juce::ParameterID{kBypass, 1}, "Bypass", false);
        addParameter(bypass_);
    }

    const juce::String getName() const override { return "EZ Limiter"; }
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
        const int maxLook = (int) std::ceil(sampleRate_ * 0.010) + 2;
        delayL_.assign((size_t) maxLook, 0.0f);
        delayR_.assign((size_t) maxLook, 0.0f);
        writePos_ = 0;
        absPos_ = 0;
        currentGain_ = 1.0f;
        window_.clear();
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int n = buffer.getNumSamples();
        if (n <= 0 || buffer.getNumChannels() < 2)
            return;
        if (bypass_->get())
            return;

        const float drive = juce::Decibels::decibelsToGain(inputGain_->get());
        const float ceiling = juce::Decibels::decibelsToGain(ceiling_->get());
        const int look = juce::jlimit(0, (int) delayL_.size() - 1,
                                      (int) std::lround(lookahead_->get() * 0.001 * sampleRate_));
        const float releaseMs = juce::jmax(1.0f, release_->get());
        const float release = std::exp(-1.0f / (float) (releaseMs * 0.001 * sampleRate_));
        const float attack = look > 0 ? 0.65f : 0.99f;

        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        if ((int) delayL_.size() < look + n + 2)
        {
            delayL_.resize((size_t) (look + n + 2), 0.0f);
            delayR_.resize((size_t) (look + n + 2), 0.0f);
            writePos_ = 0;
        }
        const int size = (int) delayL_.size();

        for (int i = 0; i < n; ++i)
        {
            const float l = left[i] * drive;
            const float r = right[i] * drive;
            const float peak = juce::jmax(std::abs(l), std::abs(r));
            const int pos = writePos_;
            const std::uint64_t now = absPos_++;

            // Monotonic queue keeps the window maximum in O(1) amortised.
            while (! window_.empty() && window_.back().second <= peak)
                window_.pop_back();
            window_.emplace_back(now, peak);
            while (! window_.empty() && window_.front().first + (std::uint64_t) look < now)
                window_.pop_front();
            const float windowPeak = window_.empty() ? 0.0f : window_.front().second;

            const float target = windowPeak > ceiling && windowPeak > 0.0f
                                     ? ceiling / windowPeak
                                     : 1.0f;
            const float coeff = target < currentGain_ ? attack : release;
            currentGain_ = coeff * currentGain_ + (1.0f - coeff) * target;

            delayL_[(size_t) pos] = l;
            delayR_[(size_t) pos] = r;
            const int read = (pos - look + size) % size;
            left[i] = juce::jlimit(-ceiling, ceiling, delayL_[(size_t) read] * currentGain_);
            right[i] = juce::jlimit(-ceiling, ceiling, delayR_[(size_t) read] * currentGain_);
            writePos_ = (pos + 1) % size;
        }
    }

    void getStateInformation(juce::MemoryBlock& data) override
    {
        juce::XmlElement xml("EZLim");
        xml.setAttribute("in", inputGain_->get());
        xml.setAttribute("cei", ceiling_->get());
        xml.setAttribute("rel", release_->get());
        xml.setAttribute("look", lookahead_->get());
        xml.setAttribute("byp", bypass_->get() ? 1 : 0);
        juce::AudioProcessor::copyXmlToBinary(xml, data);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
        if (xml == nullptr || xml->getTagName() != "EZLim")
            return;
        inputGain_->setValueNotifyingHost(inputGain_->convertTo0to1(
            (float) xml->getDoubleAttribute("in", inputGain_->get())));
        ceiling_->setValueNotifyingHost(ceiling_->convertTo0to1(
            (float) xml->getDoubleAttribute("cei", ceiling_->get())));
        release_->setValueNotifyingHost(release_->convertTo0to1(
            (float) xml->getDoubleAttribute("rel", release_->get())));
        lookahead_->setValueNotifyingHost(lookahead_->convertTo0to1(
            (float) xml->getDoubleAttribute("look", lookahead_->get())));
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

    juce::AudioParameterFloat* inputGain_ = nullptr;
    juce::AudioParameterFloat* ceiling_ = nullptr;
    juce::AudioParameterFloat* release_ = nullptr;
    juce::AudioParameterFloat* lookahead_ = nullptr;
    juce::AudioParameterBool* bypass_ = nullptr;
    double sampleRate_ = 48000.0;
    std::vector<float> delayL_, delayR_;
    int writePos_ = 0;
    std::uint64_t absPos_ = 0;
    float currentGain_ = 1.0f;
    std::deque<std::pair<std::uint64_t, float>> window_;
};

} // namespace

juce::AudioProcessor* create()
{
    return new EzLimProcessor();
}

} // namespace ezlim
