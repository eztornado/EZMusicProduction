// EZArpeggio — MIDI-FX arpeggiator. Sits in the instrument track's MIDI
// insert slot: consumes the notes the track's clips (or a live keyboard)
// feed it and re-emits them arpeggiated, sample-locked to the host
// transport (project tempo, configurable time signature). With the track
// armed and recording, the host captures the ARPEGGIATED stream, so the
// take rewrites the part with the new pattern.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ezarp {

juce::AudioProcessor* create();

} // namespace ezarp
