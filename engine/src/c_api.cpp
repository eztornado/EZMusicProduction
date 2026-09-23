#include <cadence/cadence_engine.h>

#include <cmath>

#include "cadence/engine.hpp"

namespace {

cadence::Engine* asEngine(CadenceEngine* handle)
{
    return reinterpret_cast<cadence::Engine*>(handle);
}

} // namespace

CadenceEngine* cadence_engine_create(void)
{
    return reinterpret_cast<CadenceEngine*>(new cadence::Engine());
}

void cadence_engine_destroy(CadenceEngine* engine)
{
    delete asEngine(engine);
}

CadenceResult cadence_engine_initialise(CadenceEngine* engine)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (asEngine(engine)->initialise().failed())
        return kCadenceErrorAudioDevice;
    return kCadenceOk;
}

CadenceResult cadence_engine_new_project(CadenceEngine* engine, double sample_rate)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (asEngine(engine)->newProject(sample_rate).failed())
        return kCadenceErrorInvalidArgument;
    return kCadenceOk;
}

CadenceTrackId cadence_engine_add_track(CadenceEngine* engine, const char* utf8_name,
                                        int color_index, int type)
{
    if (engine == nullptr || utf8_name == nullptr)
        return -1;
    const auto trackType = type == 1 ? cadence::Track::Type::Instrument
                                     : cadence::Track::Type::Audio;
    return asEngine(engine)->addTrack(juce::String::fromUTF8(utf8_name),
                                      color_index, trackType);
}

CadenceClipId cadence_engine_add_clip(CadenceEngine* engine, CadenceTrackId track_id,
                                      const char* utf8_path, double start_seconds, float gain)
{
    if (engine == nullptr || utf8_path == nullptr)
        return -1;
    return asEngine(engine)->addClip(track_id, juce::File(juce::String::fromUTF8(utf8_path)),
                                     start_seconds, gain);
}

CadenceResult cadence_engine_set_track_volume(CadenceEngine* engine, CadenceTrackId track_id, float linear_gain)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(linear_gain) || linear_gain < 0.0f)
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->setTrackVolume(track_id, linear_gain).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_set_track_pan(CadenceEngine* engine, CadenceTrackId track_id, float pan)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(pan))
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->setTrackPan(track_id, pan).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_set_track_mute(CadenceEngine* engine, CadenceTrackId track_id, int muted)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->setTrackMute(track_id, muted != 0).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_set_track_solo(CadenceEngine* engine, CadenceTrackId track_id, int soloed)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->setTrackSolo(track_id, soloed != 0).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

int cadence_engine_track_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->trackCount() : 0;
}

CadenceTrackId cadence_engine_track_id_at(CadenceEngine* engine, int index)
{
    return engine != nullptr ? asEngine(engine)->trackIdAt(index) : -1;
}

const char* cadence_engine_track_name(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackName(track_id) : "";
}

int cadence_engine_track_color_index(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackColorIndex(track_id) : -1;
}

float cadence_engine_track_volume(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackVolume(track_id) : 0.0f;
}

float cadence_engine_track_pan(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackPan(track_id) : 0.0f;
}

int cadence_engine_track_mute(CadenceEngine* engine, CadenceTrackId track_id)
{
    return (engine != nullptr && asEngine(engine)->trackMute(track_id)) ? 1 : 0;
}

int cadence_engine_track_solo(CadenceEngine* engine, CadenceTrackId track_id)
{
    return (engine != nullptr && asEngine(engine)->trackSolo(track_id)) ? 1 : 0;
}

int cadence_engine_track_type(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackType(track_id) : -1;
}

int cadence_engine_track_clip_count(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackClipCount(track_id) : 0;
}

CadenceClipId cadence_engine_clip_id_at(CadenceEngine* engine, CadenceTrackId track_id, int index)
{
    return engine != nullptr ? asEngine(engine)->clipIdAt(track_id, index) : -1;
}

double cadence_engine_clip_start(CadenceEngine* engine, CadenceClipId clip_id)
{
    return engine != nullptr ? asEngine(engine)->clipStart(clip_id) : -1.0;
}

double cadence_engine_clip_length(CadenceEngine* engine, CadenceClipId clip_id)
{
    return engine != nullptr ? asEngine(engine)->clipLength(clip_id) : -1.0;
}

double cadence_engine_clip_offset(CadenceEngine* engine, CadenceClipId clip_id)
{
    return engine != nullptr ? asEngine(engine)->clipOffset(clip_id) : -1.0;
}

double cadence_engine_clip_length_max(CadenceEngine* engine, CadenceClipId clip_id)
{
    return engine != nullptr ? asEngine(engine)->clipLengthMax(clip_id) : -1.0;
}

double cadence_engine_clip_fade_in(CadenceEngine* engine, CadenceClipId clip_id)
{
    return engine != nullptr ? asEngine(engine)->clipFadeIn(clip_id) : -1.0;
}

double cadence_engine_clip_fade_out(CadenceEngine* engine, CadenceClipId clip_id)
{
    return engine != nullptr ? asEngine(engine)->clipFadeOut(clip_id) : -1.0;
}

float cadence_engine_meter_track_peak(CadenceEngine* engine, int track_index)
{
    return engine != nullptr ? asEngine(engine)->meterTrackPeak(track_index) : 0.0f;
}

int cadence_engine_clip_peaks(CadenceEngine* engine, CadenceClipId clip_id,
                              double start_seconds, double length_seconds,
                              int num_pixels, float* out_mins, float* out_maxs)
{
    return engine != nullptr
               ? asEngine(engine)->clipPeaks(clip_id, start_seconds, length_seconds,
                                             num_pixels, out_mins, out_maxs)
               : -1;
}

float cadence_engine_meter_master_peak(CadenceEngine* engine, int channel)
{
    return engine != nullptr ? asEngine(engine)->meterMasterPeak(channel) : 0.0f;
}

const char* cadence_engine_current_output_device_name(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->currentOutputDeviceNameUtf8() : "";
}

const char* cadence_engine_current_input_device_name(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->currentInputDeviceNameUtf8() : "";
}

int cadence_engine_output_device_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->outputDeviceNames().size() : 0;
}

const char* cadence_engine_output_device_name_at(CadenceEngine* engine, int index)
{
    return engine != nullptr ? asEngine(engine)->outputDeviceNameAtUtf8(index) : "";
}

int cadence_engine_input_device_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->inputDeviceNames().size() : 0;
}

const char* cadence_engine_input_device_name_at(CadenceEngine* engine, int index)
{
    return engine != nullptr ? asEngine(engine)->inputDeviceNameAtUtf8(index) : "";
}

int cadence_engine_available_sample_rate_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->availableSampleRates().size() : 0;
}

double cadence_engine_available_sample_rate_at(CadenceEngine* engine, int index)
{
    if (engine == nullptr)
        return 0.0;
    const auto rates = asEngine(engine)->availableSampleRates();
    return (index >= 0 && index < rates.size()) ? rates[index] : 0.0;
}

int cadence_engine_available_buffer_size_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->availableBufferSizes().size() : 0;
}

int cadence_engine_available_buffer_size_at(CadenceEngine* engine, int index)
{
    if (engine == nullptr)
        return 0;
    const auto sizes = asEngine(engine)->availableBufferSizes();
    return (index >= 0 && index < sizes.size()) ? sizes[index] : 0;
}

CadenceResult cadence_engine_set_device_config(CadenceEngine* engine,
                                               const char* utf8_output,
                                               const char* utf8_input,
                                               double sample_rate,
                                               int buffer_size)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(sample_rate) || sample_rate < 0.0)
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->setDeviceConfig(
               utf8_output == nullptr ? juce::String() : juce::String::fromUTF8(utf8_output),
               utf8_input == nullptr ? juce::String() : juce::String::fromUTF8(utf8_input),
               sample_rate, buffer_size).failed()
               ? kCadenceErrorAudioDevice : kCadenceOk;
}

void cadence_engine_select_midi_input(CadenceEngine* engine, int index)
{
    if (engine != nullptr)
        asEngine(engine)->selectMidiInput(index);
}

int cadence_engine_selected_midi_input_index(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->selectedMidiInputIndex() : -1;
}

int cadence_engine_device_input_channel_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->deviceInputChannelCount() : 0;
}

int cadence_engine_midi_input_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->midiInputNames().size() : 0;
}

const char* cadence_engine_midi_input_name_at(CadenceEngine* engine, int index)
{
    return engine != nullptr ? asEngine(engine)->midiInputNameAtUtf8(index) : "";
}

CadenceResult cadence_engine_save_device_settings(CadenceEngine* engine, const char* utf8_path)
{
    if (engine == nullptr || utf8_path == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->saveDeviceSettings(juce::File(juce::String::fromUTF8(utf8_path))).failed()
               ? kCadenceErrorRenderFailed : kCadenceOk;
}

CadenceResult cadence_engine_load_device_settings(CadenceEngine* engine, const char* utf8_path)
{
    if (engine == nullptr || utf8_path == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->loadDeviceSettings(juce::File(juce::String::fromUTF8(utf8_path))).failed()
               ? kCadenceErrorLoadFailed : kCadenceOk;
}

CadenceResult cadence_engine_load_file(CadenceEngine* engine, const char* utf8_path)
{
    if (engine == nullptr || utf8_path == nullptr)
        return kCadenceErrorNullArgument;
    const auto r = asEngine(engine)->loadFile(juce::File(juce::String::fromUTF8(utf8_path)));
    return r.failed() ? kCadenceErrorLoadFailed : kCadenceOk;
}

CadenceResult cadence_engine_play(CadenceEngine* engine)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! asEngine(engine)->isInitialised())
        return kCadenceErrorNotInitialised;
    asEngine(engine)->play();
    return kCadenceOk;
}

CadenceResult cadence_engine_pause(CadenceEngine* engine)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! asEngine(engine)->isInitialised())
        return kCadenceErrorNotInitialised;
    asEngine(engine)->pause();
    return kCadenceOk;
}

CadenceResult cadence_engine_stop(CadenceEngine* engine)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! asEngine(engine)->isInitialised())
        return kCadenceErrorNotInitialised;
    asEngine(engine)->stop();
    return kCadenceOk;
}

CadenceResult cadence_engine_seek(CadenceEngine* engine, double seconds)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(seconds) || seconds < 0.0)
        return kCadenceErrorInvalidArgument;
    asEngine(engine)->seek(seconds);
    return kCadenceOk;
}

CadenceResult cadence_engine_set_master_gain(CadenceEngine* engine, float linear_gain)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(linear_gain) || linear_gain < 0.0f)
        return kCadenceErrorInvalidArgument;
    asEngine(engine)->setMasterGain(linear_gain);
    return kCadenceOk;
}

CadenceResult cadence_engine_remove_track(CadenceEngine* engine, CadenceTrackId track_id)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->removeTrack(track_id).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_remove_clip(CadenceEngine* engine, CadenceClipId clip_id)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->removeClip(clip_id).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_set_clip_start(CadenceEngine* engine, CadenceClipId clip_id, double seconds)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(seconds) || seconds < 0.0)
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->setClipStart(clip_id, seconds).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_set_clip_gain(CadenceEngine* engine, CadenceClipId clip_id, float gain)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(gain) || gain < 0.0f)
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->setClipGain(clip_id, gain).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_set_clip_bounds(CadenceEngine* engine, CadenceClipId clip_id,
                                             double start_seconds, double source_offset_seconds,
                                             double length_seconds)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(start_seconds) || ! std::isfinite(source_offset_seconds)
        || ! std::isfinite(length_seconds))
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->setClipBounds(clip_id, start_seconds, source_offset_seconds,
                                           length_seconds).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceClipId cadence_engine_add_midi_clip(CadenceEngine* engine, CadenceTrackId track_id,
                                           double start_seconds, double length_seconds)
{
    if (engine == nullptr)
        return -1;
    if (! std::isfinite(start_seconds) || ! std::isfinite(length_seconds))
        return -1;
    return asEngine(engine)->addMidiClip(track_id, start_seconds, length_seconds);
}

CadenceResult cadence_engine_midi_clip_add_note(CadenceEngine* engine, CadenceClipId clip_id,
                                                int pitch, float velocity,
                                                double start_seconds, double length_seconds)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(velocity) || ! std::isfinite(start_seconds)
        || ! std::isfinite(length_seconds))
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->midiClipAddNote(clip_id, pitch, velocity, start_seconds,
                                             length_seconds).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_midi_clip_remove_note(CadenceEngine* engine, CadenceClipId clip_id,
                                                   int note_index)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->midiClipRemoveNote(clip_id, note_index).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_midi_clip_set_note(CadenceEngine* engine, CadenceClipId clip_id,
                                                int note_index, int pitch,
                                                double start_seconds, double length_seconds)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(start_seconds) || ! std::isfinite(length_seconds))
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->midiClipSetNote(clip_id, note_index, pitch, start_seconds,
                                             length_seconds).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

int cadence_engine_clip_is_midi(CadenceEngine* engine, CadenceClipId clip_id)
{
    return (engine != nullptr && asEngine(engine)->clipIsMidi(clip_id)) ? 1 : 0;
}

int cadence_engine_clip_note_count(CadenceEngine* engine, CadenceClipId clip_id)
{
    return engine != nullptr ? asEngine(engine)->clipNoteCount(clip_id) : 0;
}

void cadence_engine_clip_note_at(CadenceEngine* engine, CadenceClipId clip_id, int index,
                                 int* out_pitch, float* out_velocity,
                                 double* out_start, double* out_length)
{
    if (engine == nullptr)
        return;
    asEngine(engine)->clipNoteAt(clip_id, index, out_pitch, out_velocity,
                                 out_start, out_length);
}

CadenceResult cadence_engine_set_clip_fades(CadenceEngine* engine, CadenceClipId clip_id,
                                            double fade_in_seconds, double fade_out_seconds)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(fade_in_seconds) || ! std::isfinite(fade_out_seconds))
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->setClipFades(clip_id, fade_in_seconds, fade_out_seconds).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_set_track_name(CadenceEngine* engine, CadenceTrackId track_id,
                                            const char* utf8_name)
{
    if (engine == nullptr || utf8_name == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->setTrackName(track_id, juce::String::fromUTF8(utf8_name)).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_set_track_color(CadenceEngine* engine, CadenceTrackId track_id,
                                             int color_index)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->setTrackColor(track_id, color_index).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_set_track_armed(CadenceEngine* engine, CadenceTrackId track_id, int armed)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->setTrackArmed(track_id, armed != 0).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_set_track_input_channel(CadenceEngine* engine, CadenceTrackId track_id, int channel)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->setTrackInputChannel(track_id, channel).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

int cadence_engine_track_armed(CadenceEngine* engine, CadenceTrackId track_id)
{
    return (engine != nullptr && asEngine(engine)->trackArmed(track_id)) ? 1 : 0;
}

int cadence_engine_track_input_channel(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackInputChannel(track_id) : 0;
}

CadenceResult cadence_engine_start_record(CadenceEngine* engine)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->startRecord().failed() ? kCadenceErrorInvalidArgument : kCadenceOk;
}

CadenceResult cadence_engine_stop_record(CadenceEngine* engine)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    asEngine(engine)->stopRecord();
    return kCadenceOk;
}

int cadence_engine_is_recording(CadenceEngine* engine)
{
    return (engine != nullptr && asEngine(engine)->isRecording()) ? 1 : 0;
}

double cadence_engine_record_start_seconds(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->recordStartSeconds() : 0.0;
}

void cadence_engine_set_debug_loopback(int enabled)
{
    cadence::Engine::setDebugLoopback(enabled != 0);
}

void cadence_engine_set_metronome(CadenceEngine* engine, int enabled)
{
    if (engine != nullptr)
        asEngine(engine)->setMetronome(enabled != 0);
}

int cadence_engine_can_undo(CadenceEngine* engine)
{
    return (engine != nullptr && asEngine(engine)->canUndo()) ? 1 : 0;
}

int cadence_engine_can_redo(CadenceEngine* engine)
{
    return (engine != nullptr && asEngine(engine)->canRedo()) ? 1 : 0;
}

CadenceResult cadence_engine_undo(CadenceEngine* engine)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->undo() ? kCadenceOk : kCadenceErrorNotFound;
}

CadenceResult cadence_engine_redo(CadenceEngine* engine)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->redo() ? kCadenceOk : kCadenceErrorNotFound;
}

CadenceResult cadence_engine_save_project(CadenceEngine* engine, const char* utf8_path)
{
    if (engine == nullptr || utf8_path == nullptr)
        return kCadenceErrorNullArgument;
    const auto r = asEngine(engine)->saveProject(juce::File(juce::String::fromUTF8(utf8_path)));
    return r.failed() ? kCadenceErrorRenderFailed : kCadenceOk;
}

CadenceResult cadence_engine_load_project(CadenceEngine* engine, const char* utf8_path)
{
    if (engine == nullptr || utf8_path == nullptr)
        return kCadenceErrorNullArgument;
    const auto r = asEngine(engine)->loadProject(juce::File(juce::String::fromUTF8(utf8_path)));
    return r.failed() ? kCadenceErrorLoadFailed : kCadenceOk;
}

CadenceResult cadence_engine_set_gain(CadenceEngine* engine, float linear_gain)
{
    // Legacy name for the master gain.
    return cadence_engine_set_master_gain(engine, linear_gain);
}

int cadence_engine_is_playing(CadenceEngine* engine)
{
    return (engine != nullptr && asEngine(engine)->isPlaying()) ? 1 : 0;
}

double cadence_engine_position_seconds(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->positionSeconds() : 0.0;
}

double cadence_engine_duration_seconds(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->durationSeconds() : 0.0;
}

double cadence_engine_sample_rate(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->sampleRate() : 0.0;
}

int cadence_engine_buffer_size_samples(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->currentBufferSizeSamples() : 0;
}

double cadence_engine_master_gain(CadenceEngine* engine)
{
    if (engine == nullptr) return 1.0;
    if (auto project = asEngine(engine)->currentProject()) return project->masterGain;
    return 1.0;
}

CadenceResult cadence_engine_render(CadenceEngine* engine, const char* utf8_out_path, double max_seconds)
{
    if (engine == nullptr || utf8_out_path == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(max_seconds) || max_seconds <= 0.0)
        return kCadenceErrorInvalidArgument;
    const auto r = asEngine(engine)->renderToFile(juce::File(juce::String::fromUTF8(utf8_out_path)), max_seconds);
    return r.failed() ? kCadenceErrorRenderFailed : kCadenceOk;
}

const char* cadence_engine_last_error(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->lastErrorUtf8() : "";
}
