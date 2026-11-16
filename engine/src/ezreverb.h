// EZ Reverb — built-in stereo algorithmic reverb (EZMusicProduction DSP).
//
// A Schroeder-Moorer topology: eight damped comb filters per channel feeding
// four series allpass diffusers, with a stereo pre-delay, low/high cuts,
// modulation, freeze and dry/wet. It is a plain AudioProcessor so it can sit
// in any insert chain; the UI is drawn by the Flutter device editor.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ezreverb {

juce::AudioProcessor* create();

} // namespace ezreverb
