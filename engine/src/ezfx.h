// Shared base for the built-in EZ effect processors.
//
// Keeping parameter creation, value formatting and state serialization in one
// place makes the small utility effects auditable: their DSP files only contain
// the audio path and the parameter list. The generic Flutter device editor
// discovers these parameters through the normal AudioProcessor API.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <vector>

namespace ezfx {

inline juce::String formatValue(float value, const juce::String& unit)
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
    if (unit == "x")
        return juce::String(value, 2) + "x";
    if (unit == "st")
        return juce::String(value, 1) + " st";
    if (unit == "ct")
        return juce::String(value, 0) + " ct";
    if (unit == "deg")
        return juce::String(value, 1) + " deg";
    return juce::String(value, 2);
}

class Effect : public juce::AudioProcessor
{
public:
    Effect(const juce::String& name, const juce::String& stateTag,
           double tailSeconds = 0.0)
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          name_(name),
          stateTag_(stateTag),
          tailSeconds_(tailSeconds)
    {
    }

    const juce::String getName() const override { return name_; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return tailSeconds_; }

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        maximumBlockSize_ = juce::jmax(1, maximumExpectedSamplesPerBlock);
        prepareDsp(sampleRate_, maximumBlockSize_);
    }

    void releaseResources() override { releaseDsp(); }

    void getStateInformation(juce::MemoryBlock& data) override
    {
        juce::XmlElement xml(stateTag_);
        const auto& params = getParameters();
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (auto* p = params[i])
                xml.setAttribute(parameterId(i), p->getValue());
        }
        juce::AudioProcessor::copyXmlToBinary(xml, data);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
        if (xml == nullptr || xml->getTagName() != stateTag_)
            return;
        const auto& params = getParameters();
        for (size_t i = 0; i < params.size(); ++i)
        {
            auto* p = params[i];
            if (p == nullptr)
                continue;
            const auto id = parameterId(i);
            if (xml->hasAttribute(id))
                p->setValueNotifyingHost((float) juce::jlimit(
                    0.0, 1.0, xml->getDoubleAttribute(id, p->getValue())));
        }
    }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

protected:
    virtual void prepareDsp(double, int) {}
    virtual void releaseDsp() {}

    juce::AudioParameterFloat* addFloat(const char* id, const char* name,
                                        float minValue, float maxValue,
                                        float defaultValue,
                                        const juce::String& unit = {},
                                        float skewCentre = 0.0f)
    {
        juce::NormalisableRange<float> range(minValue, maxValue);
        if (skewCentre > 0.0f && skewCentre > minValue && skewCentre < maxValue)
            range.setSkewForCentre(skewCentre);
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withLabel(unit)
                              .withStringFromValueFunction(
                                  [unit](float value, int) {
                                      return formatValue(value, unit);
                                  });
        auto* p = new juce::AudioParameterFloat(
            juce::ParameterID{id, 1}, name, range, defaultValue, attributes);
        addParameter(p);
        parameterIds_.push_back(id);
        return p;
    }

    juce::AudioParameterChoice* addChoice(const char* id, const char* name,
                                          const juce::StringArray& choices,
                                          int defaultIndex)
    {
        auto* p = new juce::AudioParameterChoice(
            juce::ParameterID{id, 1}, name, choices, defaultIndex);
        addParameter(p);
        parameterIds_.push_back(id);
        return p;
    }

    juce::AudioParameterBool* addBool(const char* id, const char* name,
                                      bool defaultValue)
    {
        auto* p = new juce::AudioParameterBool(
            juce::ParameterID{id, 1}, name, defaultValue);
        addParameter(p);
        parameterIds_.push_back(id);
        return p;
    }

    static float coeffFor(float hz, double sampleRate)
    {
        const float x = juce::jlimit(
            0.0f, 0.49f, hz / (float) sampleRate);
        return 1.0f - std::exp(-6.283185307179586f * x);
    }

    double sampleRate_ = 48000.0;
    int maximumBlockSize_ = 512;

private:
    juce::String parameterId(size_t index) const
    {
        return index < parameterIds_.size() ? parameterIds_[index]
                                            : juce::String(index);
    }

    juce::String name_;
    juce::String stateTag_;
    double tailSeconds_;
    std::vector<juce::String> parameterIds_;
};

} // namespace ezfx
