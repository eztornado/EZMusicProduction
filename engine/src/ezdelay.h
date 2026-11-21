// EZ Delay — built-in tempo-syncable stereo delay (EZMusicProduction DSP).
//
// Two circular delay lines with filtered feedback, optional ping-pong routing
// and a small stereo offset. In Sync mode the time follows the host transport
// (the engine exposes a JUCE AudioPlayHead to every inserted processor).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ezdelay {

juce::AudioProcessor* create();

} // namespace ezdelay
