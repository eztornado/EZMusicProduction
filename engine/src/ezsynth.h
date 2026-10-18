// EZSynth — built-in modern subtractive synth with a clip-bound piano roll
// and the LINES feature: polyphonic pitch-distortion areas drawn over the
// roll.
//
//  · Engines ("Motor"): "Piano" (shared PianoVoice, piano_voice.h) and
//    "Synth" (3 oscillators + unísono + sub + ruido, multimode ladder
//    filter with its own envelope, 2 LFOs, glide, velocity). A preset bank
//    gives the "varios sonidos de sintetizador".
//  · Clip page: a piano-roll canvas bound to a REAL DAW MIDI clip — notes
//    drawn here are engine notes (undoable); no internal loop.
//  · LINES: distortion areas (pitch paths) drawn over the roll. While the
//    playhead crosses an area, EVERY sounding voice is pulled toward the
//    path by its own distance — a genuinely POLYPHONIC distortion (each
//    note moves differently; a chord compresses its intervals instead of
//    collapsing to one pitch).
//
// Because builtins run in-process, the plugin receives a host context
// (engine + track + clip) through bindHostContext.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>
#include <vector>

namespace cadence { class Engine; }

namespace ezsynth {

juce::AudioProcessor* create();

// Gives an EZSynth instance access to its own track's clips (engine +
// track + bound clip; clipId 0 = unbound). Called by the host right after
// opening the instrument editor. No-op for other processor types.
void bindHostContext(juce::AudioProcessor* proc, cadence::Engine* engine,
                     std::int64_t trackId, std::int64_t clipId);

// Test hook: forces one LINES distortion curve (flat [t0,note0,t1,note1,…]
// in clip-relative seconds + MIDI note) at a fixed clip time, bypassing the
// host context. Lets the CLI verify the polyphonic warp without an Engine.
void setWarpForTest(juce::AudioProcessor* proc,
                    const std::vector<std::pair<float, float>>& nodes,
                    double clipTimeSeconds, float amount, float rangeSemis);

// Test hook: selects the editor's page (0 synth, 1 clip/LINES) so a
// headless layout check can exercise both pages.
void setEditorPageForTest(juce::AudioProcessorEditor* editor, int page);

// Preset bank test hooks.
void applyPresetForTest(juce::AudioProcessor* proc, int index);
int presetCountForTest();

} // namespace ezsynth
