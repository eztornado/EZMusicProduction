// EZ Limiter — built-in look-ahead brick-wall limiter for the master bus.
//
// A stereo-linked peak detector drives a short look-ahead delay so the gain
// is already down when the loudest sample arrives. The output is hard-clamped
// at the ceiling as a safety net; the parameter ids are stable so the mix
// assistant can set drive and ceiling without opening an editor.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ezlim {

juce::AudioProcessor* create();

} // namespace ezlim
