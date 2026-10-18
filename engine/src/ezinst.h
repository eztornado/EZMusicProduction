// EZInstruments — General MIDI instrument collection powered by FluidSynth
// (libfluidsynth is LGPL 2.1+, linked dynamically; this file is our own
// original glue). One processor = the whole GM catalog: 128 melodic
// programs + the percussion kit. Reverb/chorus are disabled — space and
// tone shaping belong to the track's FX chain, not the instrument.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ezinst {

juce::AudioProcessor* create();

} // namespace ezinst
