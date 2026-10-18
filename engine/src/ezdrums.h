// EZDrums — built-in drum machine instrument. Synthesized drum kits
// (808/909-style, zero assets), a bank of preset patterns across styles,
// project-tempo-locked looping with configurable time signature, and the
// generated MIDI exposed to the host: with the track armed (R), recording
// writes the pattern into a REAL DAW MIDI clip — as if a player had
// performed it — editable afterwards in the normal piano roll.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ezdrums {

juce::AudioProcessor* create();

// A processor that COMPOSES its own MIDI (drum patterns) exposes it here:
// the host drains what the plugin "performed" this block and, while the
// transport records an armed track, writes it into the track's MIDI take.
class MidiGenerator
{
public:
    virtual ~MidiGenerator() = default;
    // Called once per processBlock ON THE SAME AUDIO THREAD, right after
    // processBlock: hand over the events this plugin generated.
    virtual void drainGeneratedMidi(juce::MidiBuffer& out) = 0;
};

} // namespace ezdrums
