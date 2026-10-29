// EZ Compressor — built-in stereo-linked dynamics processor.
//
// One detector drives both channels, with soft knee, attack/release and
// parallel mix. It is a plain AudioProcessor so it can live in any insert
// chain (channel or master) and be driven by the mix assistant through its
// stable parameter ids.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ezcomp {

juce::AudioProcessor* create();

} // namespace ezcomp
