// EZ EQ Pro — built-in parametric equalizer (EZMusicProduction's own DSP).
// 8 bands, Pro-Q-style workflow: per-band enable/type/freq/gain/Q, 24 dB/oct
// cuts, natural-phase (IIR) filtering. NOT derived from FabFilter code —
// original RBJ-cookbook biquads on our own engine.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ezq4 {

constexpr int kNumBands = 8;
constexpr int kNumTypes = 6; // Bell, LowCut, LowShelf, HighShelf, HighCut, Notch

juce::AudioProcessor* create();

} // namespace ezq4
