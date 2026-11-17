// EZSynth 2.0 — hybrid performance synthesizer.
//
// The public surface stays identical to the original EZSynth built-in: the
// host creates an AudioProcessor and serializes its state. The implementation
// below is a clean-room engine with two full layers, an auxiliary texture
// layer, four synthesis engines per layer (analog, wavetable, FM and
// granular), dual filters, four envelopes, four LFOs, random sources, a
// 16-slot modulation matrix, eight macros, a six-slot FX rack, an arpeggiator
// and MPE.
//
// Legacy projects continue to load: state stored as EZSYNTH2 is read
// attribute-by-attribute (the core parameter IDs are preserved) and the
// original Piano motor is still available through the `motor` parameter.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>
#include <vector>

namespace cadence { class Engine; }

namespace ezsynth2 {

juce::AudioProcessor* create();

// Preset catalog used by the AI arranger and by hosts that create a track
// without opening the editor. Names are category-prefixed ("Bass · …").
juce::StringArray presetNamesForCatalog();
int presetCountForTest();
juce::String makePresetState(const juce::String& presetName);
void applyPresetForTest(juce::AudioProcessor* proc, int index);

// Host context for LINES and tempo-aware arpeggiator timing.
void bindHostContext(juce::AudioProcessor* proc, cadence::Engine* engine,
                     std::int64_t trackId, std::int64_t clipId);
void setWarpForTest(juce::AudioProcessor* proc,
                    const std::vector<std::pair<float, float>>& nodes,
                    double clipTimeSeconds, float amount, float rangeSemis);

// Metadata helpers used by the C ABI. They are no-ops for non-EZSynth2
// processors.
juce::String parameterIdAt(juce::AudioProcessor* proc, int index);
juce::String parameterGroupAt(juce::AudioProcessor* proc, int index);

} // namespace ezsynth2
