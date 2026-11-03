/* Minimal do-nothing VST3 used by ui/test/plugins_scan_test.dart: the scan
 * test needs a real VST3 bundle on the machine. Build once and copy the
 * produced bundle into ~/.vst3/ (Linux), e.g.:
 *   cmake -S engine/tools/test-vst3 -B build/test-vst3 -DCADENCE_JUCE_DIR=build/_deps/juce-src
 *   cmake --build build/test-vst3 --config Release
 *   cp -r build/test-vst3/cadence_test_vst3_artefacts/Release/VST3/CadenceTestTone.vst3 ~/.vst3/
 */
#include <juce_audio_processors/juce_audio_processors.h>

class CadenceTestToneProcessor : public juce::AudioProcessor
{
public:
    CadenceTestToneProcessor()
        : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override { buffer.clear(); }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "CadenceTestTone"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CadenceTestToneProcessor();
}
