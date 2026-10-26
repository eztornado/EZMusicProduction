#include <cadence/cadence_engine.h>

#include <cmath>
#include <vector>

#include "cadence/engine.hpp"

namespace {

cadence::Engine* asEngine(CadenceEngine* handle)
{
    return reinterpret_cast<cadence::Engine*>(handle);
}

} // namespace

static CadenceEngine* g_active_engine = nullptr;

CadenceEngine* cadence_engine_create(void)
{
    auto* e = new cadence::Engine();
    g_active_engine = reinterpret_cast<CadenceEngine*>(e);
    return reinterpret_cast<CadenceEngine*>(e);
}

void cadence_engine_destroy(CadenceEngine* engine)
{
    // Idempotent: the macOS AppDelegate may have destroyed the active engine
    // in applicationWillTerminate before Dart's dispose() runs. Only the
    // active handle is ever owned by the app.
    if (engine == nullptr || g_active_engine != engine)
        return;
    g_active_engine = nullptr;
    delete asEngine(engine);
}

void cadence_engine_destroy_active(void)
{
    auto* engine = g_active_engine;
    if (engine == nullptr)
        return;
    g_active_engine = nullptr;
    delete asEngine(engine);
}

CadenceEngine* cadence_engine_get_active(void)
{
    return g_active_engine;
}

CadenceResult cadence_engine_drop_file(const char* utf8_path)
{
    auto* e = g_active_engine;
    if (e == nullptr || utf8_path == nullptr)
        return kCadenceErrorNullArgument;
    // Add to the first track at position 0. Iván can then drag the clip
    // to the desired track/position on the timeline.
    auto trackId = asEngine(e)->trackIdAt(0);
    auto clipId = asEngine(e)->addClip(trackId,
        juce::File(juce::String::fromUTF8(utf8_path)), 0.0, 1.0f);
    return clipId >= 0 ? kCadenceOk : kCadenceErrorNotFound;
}

CadenceResult cadence_engine_initialise(CadenceEngine* engine)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (asEngine(engine)->initialise().failed())
        return kCadenceErrorAudioDevice;
    return kCadenceOk;
}

CadenceResult cadence_engine_restart_audio(CadenceEngine* engine)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->restartAudio().failed()
               ? kCadenceErrorAudioDevice : kCadenceOk;
}

void cadence_engine_initialise_audio_background(CadenceEngine* engine)
{
    if (engine != nullptr)
        asEngine(engine)->initialiseAudioInBackground();
}

int cadence_engine_audio_initialised(CadenceEngine* engine)
{
    return (engine != nullptr && asEngine(engine)->audioInitialised()) ? 1 : 0;
}

int cadence_engine_audio_device_running(CadenceEngine* engine)
{
    return (engine != nullptr && asEngine(engine)->audioDeviceRunning()) ? 1 : 0;
}

int cadence_engine_audio_xrun_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->audioXRunCount() : 0;
}

int cadence_engine_audio_recovery_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->audioRecoveryCount() : 0;
}

const char* cadence_engine_last_audio_error(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->lastAudioErrorUtf8() : "";
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

CadenceResult cadence_engine_set_track_volume_automation(CadenceEngine* engine,
                                                         CadenceTrackId track_id,
                                                         const double* times,
                                                         const double* values,
                                                         int count)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (count < 0 || (count > 0 && (times == nullptr || values == nullptr)))
        return kCadenceErrorInvalidArgument;
    std::vector<cadence::AutomationPoint> points;
    points.reserve((size_t) count);
    for (int i = 0; i < count; ++i)
        points.push_back({ times[i], values[i] });
    return asEngine(engine)->setTrackVolumeAutomation(track_id, points).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

int cadence_engine_track_volume_auto_count(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackVolumeAutoCount(track_id) : 0;
}

CadenceResult cadence_engine_set_track_lane_automation(CadenceEngine* engine,
                                                       CadenceTrackId track_id,
                                                       int lane,
                                                       const double* times,
                                                       const double* values,
                                                       int count)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (lane < 0 || lane > 2 || count < 0
        || (count > 0 && (times == nullptr || values == nullptr)))
        return kCadenceErrorInvalidArgument;
    std::vector<cadence::AutomationPoint> points;
    points.reserve((size_t) count);
    for (int i = 0; i < count; ++i)
        points.push_back({ times[i], values[i] });
    return asEngine(engine)->setTrackLaneAutomation(track_id, lane, points).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

int cadence_engine_track_lane_auto_count(CadenceEngine* engine,
                                         CadenceTrackId track_id, int lane)
{
    return engine != nullptr ? asEngine(engine)->trackLaneAutomationCount(track_id, lane) : 0;
}

CadenceResult cadence_engine_set_track_param_automation(CadenceEngine* engine,
                                                        CadenceTrackId track_id,
                                                        int slot_kind,
                                                        int slot_index,
                                                        int param_index,
                                                        const char* utf8_name,
                                                        const double* times,
                                                        const double* values,
                                                        int count)
{
    if (engine == nullptr || utf8_name == nullptr)
        return kCadenceErrorNullArgument;
    if (count < 0 || (count > 0 && (times == nullptr || values == nullptr)))
        return kCadenceErrorInvalidArgument;
    std::vector<cadence::AutomationPoint> points;
    points.reserve((size_t) count);
    for (int i = 0; i < count; ++i)
        points.push_back({ times[i], values[i] });
    return asEngine(engine)
        ->setTrackParamAutomation(track_id, slot_kind, slot_index, param_index,
                                  juce::String::fromUTF8(utf8_name), points)
        .failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

int cadence_engine_track_param_lane_count(CadenceEngine* engine,
                                          CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackParamLaneCount(track_id) : 0;
}

CadenceResult cadence_engine_track_param_lane_info(CadenceEngine* engine,
                                                   CadenceTrackId track_id,
                                                   int lane_index,
                                                   int* out_slot_kind,
                                                   int* out_slot_index,
                                                   int* out_param_index)
{
    if (engine == nullptr || out_slot_kind == nullptr || out_slot_index == nullptr
        || out_param_index == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)
        ->trackParamLaneInfo(track_id, lane_index, out_slot_kind, out_slot_index,
                             out_param_index)
        .failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

const char* cadence_engine_track_param_lane_name(CadenceEngine* engine,
                                                 CadenceTrackId track_id,
                                                 int lane_index)
{
    return engine != nullptr
               ? asEngine(engine)->trackParamLaneName(track_id, lane_index)
               : "";
}

int cadence_engine_track_param_lane_point_count(CadenceEngine* engine,
                                                CadenceTrackId track_id,
                                                int lane_index)
{
    return engine != nullptr
               ? asEngine(engine)->trackParamLanePointCount(track_id, lane_index)
               : 0;
}

CadenceResult cadence_engine_track_param_lane_point_at(CadenceEngine* engine,
                                                       CadenceTrackId track_id,
                                                       int lane_index,
                                                       int point_index,
                                                       double* out_time,
                                                       double* out_value)
{
    if (engine == nullptr || out_time == nullptr || out_value == nullptr)
        return kCadenceErrorNullArgument;
    const auto point =
        asEngine(engine)->trackParamLanePointAt(track_id, lane_index, point_index);
    *out_time = point.timeSeconds;
    *out_value = point.value;
    return kCadenceOk;
}

CadenceResult cadence_engine_remove_track_param_lane(CadenceEngine* engine,
                                                     CadenceTrackId track_id,
                                                     int lane_index)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->removeTrackParamLane(track_id, lane_index).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

int cadence_engine_plugin_param_count(CadenceEngine* engine,
                                      CadenceTrackId track_id, int slot_kind,
                                      int slot_index)
{
    return engine != nullptr
               ? asEngine(engine)->pluginParamCount(track_id, slot_kind, slot_index)
               : 0;
}

const char* cadence_engine_plugin_param_name(CadenceEngine* engine,
                                             CadenceTrackId track_id,
                                             int slot_kind, int slot_index,
                                             int param_index)
{
    return engine != nullptr
               ? asEngine(engine)->pluginParamName(track_id, slot_kind,
                                                   slot_index, param_index)
               : "";
}

float cadence_engine_plugin_param_value(CadenceEngine* engine,
                                        CadenceTrackId track_id,
                                        int slot_kind, int slot_index,
                                        int param_index)
{
    return engine != nullptr
               ? asEngine(engine)->pluginParamValue(track_id, slot_kind,
                                                    slot_index, param_index)
               : 0.0f;
}

const char* cadence_engine_plugin_param_text(CadenceEngine* engine,
                                             CadenceTrackId track_id,
                                             int slot_kind, int slot_index,
                                             int param_index)
{
    return engine != nullptr
               ? asEngine(engine)->pluginParamText(track_id, slot_kind,
                                                   slot_index, param_index)
               : "";
}

CadenceResult cadence_engine_set_plugin_param_value(CadenceEngine* engine,
                                                    CadenceTrackId track_id,
                                                    int slot_kind,
                                                    int slot_index,
                                                    int param_index,
                                                    float value)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(value))
        return kCadenceErrorInvalidArgument;
    const auto r = asEngine(engine)->setPluginParamValue(
        track_id, slot_kind, slot_index, param_index, value);
    return r.failed() ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_track_lane_auto_at(CadenceEngine* engine,
                                                CadenceTrackId track_id, int lane,
                                                int index, double* out_time,
                                                double* out_value)
{
    if (engine == nullptr || out_time == nullptr || out_value == nullptr)
        return kCadenceErrorNullArgument;
    const auto point = asEngine(engine)->trackLaneAutomationAt(track_id, lane, index);
    *out_time = point.timeSeconds;
    *out_value = point.value;
    return kCadenceOk;
}

float cadence_engine_track_volume_effective_at(CadenceEngine* engine,
                                               CadenceTrackId track_id,
                                               double time_seconds)
{
    if (engine == nullptr || ! std::isfinite(time_seconds))
        return 0.0f;
    return asEngine(engine)->trackVolumeEffectiveAt(track_id, time_seconds);
}

CadenceResult cadence_engine_track_volume_auto_at(CadenceEngine* engine,
                                                  CadenceTrackId track_id, int index,
                                                  double* out_time, double* out_value)
{
    if (engine == nullptr || out_time == nullptr || out_value == nullptr)
        return kCadenceErrorNullArgument;
    const auto point = asEngine(engine)->trackVolumeAutoAt(track_id, index);
    *out_time = point.timeSeconds;
    *out_value = point.value;
    return kCadenceOk;
}

CadenceResult cadence_engine_set_track_volume_auto_point_at(CadenceEngine* engine,
                                                            CadenceTrackId track_id,
                                                            double time_seconds,
                                                            double value)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(time_seconds) || ! std::isfinite(value))
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)
        ->setTrackVolumeAutomationPointAt(track_id, time_seconds, value)
        .failed()
               ? kCadenceErrorInvalidArgument : kCadenceOk;
}

double cadence_engine_input_quantize(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->inputQuantizeSeconds() : 0.0;
}

CadenceResult cadence_engine_set_input_quantize(CadenceEngine* engine,
                                                double grid_seconds)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(grid_seconds) || grid_seconds < 0.0)
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->setInputQuantize(grid_seconds).failed()
               ? kCadenceErrorInvalidArgument : kCadenceOk;
}

double cadence_engine_midi_input_latency(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->midiInputLatencyMs() : 0.0;
}

CadenceResult cadence_engine_set_midi_input_latency(CadenceEngine* engine,
                                                    double ms)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(ms))
        return kCadenceErrorInvalidArgument;
    asEngine(engine)->setMidiInputLatencyMs(ms);
    return kCadenceOk;
}

CadenceResult cadence_engine_copy_clip(CadenceEngine* engine,
                                       CadenceClipId clip_id)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->copyClip(clip_id).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_paste_clip(CadenceEngine* engine,
                                        CadenceTrackId track_id,
                                        double start_seconds)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(start_seconds))
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->pasteClip(track_id, start_seconds).failed()
               ? kCadenceErrorInvalidArgument : kCadenceOk;
}

int cadence_engine_has_pending_plugin_jobs(CadenceEngine* engine)
{
    return engine != nullptr
        ? (asEngine(engine)->hasPendingPluginJobs() ? 1 : 0) : 0;
}

void cadence_engine_pump_one_plugin_job(CadenceEngine* engine)
{
    if (engine != nullptr)
        asEngine(engine)->pumpOnePluginJob();
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

CadenceResult cadence_engine_remote_start(CadenceEngine* engine, int port,
                                          const char* utf8_token)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (port < 0 || port > 65535)
        return kCadenceErrorInvalidArgument;
    const auto token = utf8_token != nullptr ? juce::String::fromUTF8(utf8_token)
                                             : juce::String();
    const auto r = asEngine(engine)->startRemoteServer(port, token);
    return r.failed() ? kCadenceErrorNetwork : kCadenceOk;
}

void cadence_engine_remote_stop(CadenceEngine* engine)
{
    if (engine != nullptr)
        asEngine(engine)->stopRemoteServer();
}

int cadence_engine_remote_running(CadenceEngine* engine)
{
    return (engine != nullptr && asEngine(engine)->remoteServerRunning()) ? 1 : 0;
}

int cadence_engine_remote_port(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->remoteServerPort() : 0;
}

int cadence_engine_remote_client_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->remoteServerClientCount() : 0;
}

const char* cadence_engine_remote_token(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->remoteServerToken() : "";
}

const char* cadence_engine_remote_last_error(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->remoteServerLastError() : "";
}

int cadence_engine_pump_remote(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->pumpRemoteCommands() : 0;
}

uint64_t cadence_engine_project_revision(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->projectRevision() : 0;
}

CadenceResult cadence_engine_play(CadenceEngine* engine)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    auto* e = asEngine(engine);
    // Self-healing: if the device was never opened (or CoreAudio dropped it
    // after a UR22 hiccup), try to reopen it before refusing to play. The
    // previous behaviour required an app restart.
    if (! e->isInitialised())
    {
        if (e->initialise().failed())
            return kCadenceErrorAudioDevice;
    }
    if (! e->isInitialised())
        return kCadenceErrorNotInitialised;
    e->play();
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

CadenceResult cadence_engine_midi_clip_set_notes(CadenceEngine* engine, CadenceClipId clip_id,
                                                 const int* note_indices,
                                                 const int* pitches,
                                                 const double* start_seconds,
                                                 const double* length_seconds,
                                                 int count)
{
    if (engine == nullptr || count < 0)
        return kCadenceErrorNullArgument;
    if (count > 0 && (note_indices == nullptr || pitches == nullptr
                      || start_seconds == nullptr || length_seconds == nullptr))
        return kCadenceErrorNullArgument;

    std::vector<cadence::MidiNoteEdit> edits;
    edits.reserve((size_t) count);
    for (int i = 0; i < count; ++i)
    {
        if (! std::isfinite(start_seconds[i]) || ! std::isfinite(length_seconds[i]))
            return kCadenceErrorInvalidArgument;
        cadence::MidiNoteEdit edit;
        edit.index = note_indices[i];
        edit.pitch = pitches[i];
        edit.startSeconds = start_seconds[i];
        edit.lengthSeconds = length_seconds[i];
        edits.push_back(edit);
    }
    return asEngine(engine)->midiClipSetNotes(clip_id, edits).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

double cadence_engine_clip_note_orig_start_at(CadenceEngine* engine, CadenceClipId clip_id,
                                              int index)
{
    return engine != nullptr ? asEngine(engine)->clipNoteOrigStartAt(clip_id, index) : 0.0;
}

CadenceResult cadence_engine_quantize_clip_notes(CadenceEngine* engine, CadenceClipId clip_id,
                                                 double grid_seconds, double swing,
                                                 double catch_range, double max_move,
                                                 int non_grid, int iterative,
                                                 double iter_strength)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(grid_seconds) || grid_seconds <= 0.0
        || ! std::isfinite(swing) || ! std::isfinite(catch_range)
        || ! std::isfinite(max_move) || ! std::isfinite(iter_strength))
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)
        ->quantizeClipNotes(clip_id, grid_seconds, swing, catch_range, max_move,
                            non_grid != 0, iterative != 0, iter_strength)
        .failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_reset_clip_quantize(CadenceEngine* engine, CadenceClipId clip_id)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->resetClipQuantize(clip_id).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_freeze_clip_quantize(CadenceEngine* engine, CadenceClipId clip_id)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->freezeClipQuantize(clip_id).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

/* ---- AI clip generation ---- */

void cadence_engine_set_ai_model_dir(CadenceEngine* engine, const char* utf8_path)
{
    if (engine == nullptr || utf8_path == nullptr)
        return;
    asEngine(engine)->setAiModelDir(juce::File(juce::String::fromUTF8(utf8_path)));
}

const char* cadence_engine_ai_model_dir(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->aiModelDirUtf8() : "";
}

int cadence_engine_ai_model_available(CadenceEngine* engine, int mode)
{
    if (engine == nullptr)
        return 0;
    const int m = mode < 0 || mode > 3 ? 0 : mode;
    return asEngine(engine)->aiModelAvailable(static_cast<cadence::AiMode>(m)) ? 1 : 0;
}

int cadence_engine_start_ai_generation_long(CadenceEngine* engine,
                                            CadenceTrackId track_id,
                                            CadenceClipId clip_id,
                                            double anchor_seconds,
                                            int mode, int bars,
                                            double length_seconds,
                                            double temperature, int variations,
                                            int seed, const char* utf8_prompt,
                                            int replace_clip, int harmonize,
                                            int use_model)
{
    if (engine == nullptr)
        return 0;

    cadence::AiGenerationRequest request;
    request.trackId = track_id;
    request.clipId = clip_id;
    request.anchorSeconds = anchor_seconds;
    request.mode = static_cast<cadence::AiMode>(
        juce::jlimit(0, 3, mode));
    request.bars = juce::jlimit(1, 64, bars);
    request.lengthSeconds = juce::jlimit(0.0, 120.0, length_seconds);
    request.temperature = juce::jlimit(0.1, 1.8, temperature);
    request.variations = juce::jlimit(1, 8, variations);
    request.seed = seed;
    if (utf8_prompt != nullptr)
        request.prompt = juce::String::fromUTF8(utf8_prompt);
    request.replaceClip = replace_clip != 0;
    request.harmonize = harmonize != 0;
    request.useModel = use_model != 0;

    return asEngine(engine)->startAiGeneration(request) ? 1 : 0;
}

int cadence_engine_start_ai_generation(CadenceEngine* engine,
                                       CadenceTrackId track_id,
                                       CadenceClipId clip_id,
                                       double anchor_seconds,
                                       int mode, int bars,
                                       double temperature, int variations,
                                       int seed, const char* utf8_prompt,
                                       int replace_clip, int harmonize,
                                       int use_model)
{
    // Legacy entry point: bars are the total length, capped at the original
    // interactive range.
    return cadence_engine_start_ai_generation_long(
        engine, track_id, clip_id, anchor_seconds, mode,
        juce::jlimit(1, 4, bars), 0.0, temperature, variations, seed,
        utf8_prompt, replace_clip, harmonize, use_model);
}

int cadence_engine_ai_generation_state(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->aiGenerationState() : 0;
}

void cadence_engine_cancel_ai_generation(CadenceEngine* engine)
{
    if (engine != nullptr)
        asEngine(engine)->cancelAiGeneration();
}

void cadence_engine_clear_ai_generation(CadenceEngine* engine)
{
    if (engine != nullptr)
        asEngine(engine)->clearAiGenerationResult();
}

int cadence_engine_ai_result_ok(CadenceEngine* engine)
{
    return (engine != nullptr && asEngine(engine)->aiGenerationResult().ok) ? 1 : 0;
}

const char* cadence_engine_ai_result_string(CadenceEngine* engine, int field)
{
    return engine != nullptr ? asEngine(engine)->aiResultString(field) : "";
}

int cadence_engine_ai_result_seed(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->aiGenerationResult().seed : 0;
}

double cadence_engine_ai_result_clip_length(CadenceEngine* engine)
{
    return engine != nullptr
        ? asEngine(engine)->aiGenerationResult().clipLengthSeconds : 0.0;
}

double cadence_engine_ai_result_start(CadenceEngine* engine)
{
    return engine != nullptr
        ? asEngine(engine)->aiGenerationResult().startSeconds : 0.0;
}

CadenceClipId cadence_engine_ai_result_clip_id(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->aiGenerationResult().clipId : -1;
}

CadenceTrackId cadence_engine_ai_result_track_id(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->aiGenerationResult().trackId : -1;
}

int cadence_engine_ai_result_note_count(CadenceEngine* engine)
{
    return engine != nullptr
        ? (int) asEngine(engine)->aiGenerationResult().notes.size() : 0;
}

void cadence_engine_ai_result_note_at(CadenceEngine* engine, int index,
                                      int* out_pitch, float* out_velocity,
                                      double* out_start, double* out_length)
{
    if (engine == nullptr || index < 0)
        return;
    const auto result = asEngine(engine)->aiGenerationResult();
    if (index >= (int) result.notes.size())
        return;
    const auto& note = result.notes[(std::size_t) index];
    if (out_pitch != nullptr) *out_pitch = note.pitch;
    if (out_velocity != nullptr) *out_velocity = note.velocity;
    if (out_start != nullptr) *out_start = note.startSeconds;
    if (out_length != nullptr) *out_length = note.lengthSeconds;
}

CadenceResult cadence_engine_apply_ai_generation(CadenceEngine* engine,
                                                 CadenceClipId* out_clip_id)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    cadence::ClipId clipId = -1;
    if (asEngine(engine)->applyAiGeneration(&clipId).failed())
        return kCadenceErrorInvalidArgument;
    if (out_clip_id != nullptr)
        *out_clip_id = clipId;
    return kCadenceOk;
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

CadenceResult cadence_engine_move_clip_to_track(CadenceEngine* engine, CadenceClipId clip_id,
                                                CadenceTrackId track_id, double start_seconds)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(start_seconds))
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->moveClipToTrack(clip_id, track_id, start_seconds).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_move_clips(CadenceEngine* engine,
                                        const CadenceClipId* clip_ids,
                                        const CadenceTrackId* track_ids,
                                        const double* start_seconds,
                                        int count)
{
    if (engine == nullptr || clip_ids == nullptr || track_ids == nullptr
        || start_seconds == nullptr)
        return kCadenceErrorNullArgument;
    if (count < 0)
        return kCadenceErrorInvalidArgument;
    if (count == 0)
        return kCadenceOk;
    for (int i = 0; i < count; ++i)
        if (! std::isfinite(start_seconds[i]))
            return kCadenceErrorInvalidArgument;

    const std::vector<cadence::ClipId> clipIds(clip_ids, clip_ids + count);
    const std::vector<cadence::TrackId> trackIds(track_ids, track_ids + count);
    const std::vector<double> starts(start_seconds, start_seconds + count);
    return asEngine(engine)->moveClips(clipIds, trackIds, starts).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_remove_clips(CadenceEngine* engine,
                                          const CadenceClipId* clip_ids,
                                          int count)
{
    if (engine == nullptr || clip_ids == nullptr)
        return kCadenceErrorNullArgument;
    if (count < 0)
        return kCadenceErrorInvalidArgument;
    if (count == 0)
        return kCadenceOk;

    const std::vector<cadence::ClipId> clipIds(clip_ids, clip_ids + count);
    return asEngine(engine)->removeClips(clipIds).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_split_clip(CadenceEngine* engine, CadenceClipId clip_id,
                                       double time_seconds)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(time_seconds))
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->splitClipAt(clip_id, time_seconds).failed()
               ? kCadenceErrorInvalidArgument : kCadenceOk;
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

int cadence_engine_recording_note_count(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->recordingNoteCount(track_id) : 0;
}

void cadence_engine_recording_note_at(CadenceEngine* engine, CadenceTrackId track_id,
                                      int index, int* out_pitch, float* out_velocity,
                                      double* out_start, double* out_length)
{
    if (engine == nullptr)
        return;
    asEngine(engine)->recordingNoteAt(track_id, index, out_pitch, out_velocity,
                                      out_start, out_length);
}

void cadence_engine_set_debug_loopback(int enabled)
{
    cadence::Engine::setDebugLoopback(enabled != 0);
}

void cadence_engine_rescan_plugins(CadenceEngine* engine)
{
    if (engine != nullptr)
        asEngine(engine)->rescanPlugins();
}

void cadence_engine_scan_plugins_now(CadenceEngine* engine)
{
    if (engine != nullptr)
        asEngine(engine)->scanPluginsNow();
}

int cadence_engine_plugin_scan_active(CadenceEngine* engine)
{
    return (engine != nullptr && asEngine(engine)->pluginScanActive()) ? 1 : 0;
}

int cadence_engine_plugin_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->pluginCount() : 0;
}

const char* cadence_engine_plugin_name_at(CadenceEngine* engine, int index)
{
    return engine != nullptr ? asEngine(engine)->pluginNameAt(index) : "";
}

const char* cadence_engine_plugin_uid_at(CadenceEngine* engine, int index)
{
    return engine != nullptr ? asEngine(engine)->pluginUidAt(index) : "";
}

int cadence_engine_plugin_is_instrument_at(CadenceEngine* engine, int index)
{
    return (engine != nullptr && asEngine(engine)->pluginIsInstrumentAt(index)) ? 1 : 0;
}

const char* cadence_engine_plugin_format_at(CadenceEngine* engine, int index)
{
    return engine != nullptr ? asEngine(engine)->pluginFormatAt(index) : "";
}

CadenceResult cadence_engine_track_add_fx(CadenceEngine* engine, CadenceTrackId track_id,
                                          int plugin_index)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->addTrackFx(track_id, plugin_index).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_track_remove_fx(CadenceEngine* engine, CadenceTrackId track_id,
                                             int slot_index)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->removeTrackFx(track_id, slot_index).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_track_set_fx_bypass(CadenceEngine* engine, CadenceTrackId track_id,
                                                 int slot_index, int bypass)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->setTrackFxBypass(track_id, slot_index, bypass != 0).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

int cadence_engine_track_fx_count(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackFxCount(track_id) : 0;
}

const char* cadence_engine_track_fx_name_at(CadenceEngine* engine, CadenceTrackId track_id,
                                            int slot_index)
{
    return engine != nullptr ? asEngine(engine)->trackFxNameAt(track_id, slot_index) : "";
}

int cadence_engine_track_fx_bypass_at(CadenceEngine* engine, CadenceTrackId track_id,
                                      int slot_index)
{
    return (engine != nullptr && asEngine(engine)->trackFxBypassAt(track_id, slot_index)) ? 1 : 0;
}

CadenceResult cadence_engine_track_set_instrument_plugin(CadenceEngine* engine,
                                                         CadenceTrackId track_id,
                                                         int plugin_index)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->setTrackInstrumentPlugin(track_id, plugin_index).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_track_clear_instrument(CadenceEngine* engine, CadenceTrackId track_id)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->clearTrackInstrument(track_id).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_track_set_fx_state(CadenceEngine* engine,
                                                CadenceTrackId track_id,
                                                int slot_index,
                                                const void* component,
                                                int component_size,
                                                const void* controller,
                                                int controller_size)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)
                   ->setTrackFxState(track_id, slot_index, component,
                                     component_size, controller, controller_size)
                   .failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_track_set_instrument_state(CadenceEngine* engine,
                                                        CadenceTrackId track_id,
                                                        const void* component,
                                                        int component_size,
                                                        const void* controller,
                                                        int controller_size)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)
                   ->setTrackInstrumentState(track_id, component, component_size,
                                             controller, controller_size)
                   .failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

const char* cadence_engine_plugin_state_base64(CadenceEngine* engine,
                                               CadenceTrackId track_id,
                                               int slot_kind, int slot_index)
{
    return engine != nullptr
               ? asEngine(engine)->pluginStateBase64(track_id, slot_kind, slot_index)
               : "";
}

const char* cadence_engine_track_instrument_name(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackInstrumentName(track_id) : "";
}

CadenceResult cadence_engine_track_set_midifx(CadenceEngine* engine,
                                              CadenceTrackId track_id,
                                              int plugin_index)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->setTrackMidiFx(track_id, plugin_index).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_track_clear_midifx(CadenceEngine* engine, CadenceTrackId track_id)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->clearTrackMidiFx(track_id).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

const char* cadence_engine_track_midifx_name(CadenceEngine* engine, CadenceTrackId track_id)
{
    return engine != nullptr ? asEngine(engine)->trackMidiFxName(track_id) : "";
}

CadenceResult cadence_engine_open_plugin_editor(CadenceEngine* engine,
                                                CadenceTrackId track_id,
                                                int slot_index)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->openPluginEditor(track_id, slot_index).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_open_instrument_editor(CadenceEngine* engine,
                                                    CadenceTrackId track_id)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->openInstrumentEditor(track_id).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_open_instrument_editor_with_clip(
    CadenceEngine* engine, CadenceTrackId track_id, std::int64_t clip_id)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)
        ->openInstrumentEditor(track_id, (cadence::ClipId) clip_id)
        .failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

int cadence_engine_master_fx_count(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->masterFxCount() : 0;
}

const char* cadence_engine_master_fx_name_at(CadenceEngine* engine, int slot_index)
{
    return engine != nullptr ? asEngine(engine)->masterFxNameAt(slot_index) : "";
}

CadenceResult cadence_engine_master_add_fx(CadenceEngine* engine, int plugin_index)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->addMasterFx(plugin_index).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_master_remove_fx(CadenceEngine* engine, int slot_index)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->removeMasterFx(slot_index).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

CadenceResult cadence_engine_master_set_fx_bypass(CadenceEngine* engine, int slot_index,
                                                  int bypass)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->setMasterFxBypass(slot_index, bypass != 0).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

int cadence_engine_master_fx_bypass_at(CadenceEngine* engine, int slot_index)
{
    return (engine != nullptr && asEngine(engine)->masterFxBypassAt(slot_index)) ? 1 : 0;
}

CadenceResult cadence_engine_open_master_fx_editor(CadenceEngine* engine,
                                                    int slot_index)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->openMasterFxEditor(slot_index).failed()
               ? kCadenceErrorNotFound : kCadenceOk;
}

void cadence_engine_set_metronome(CadenceEngine* engine, int enabled)
{
    if (engine != nullptr)
        asEngine(engine)->setMetronome(enabled != 0);
}

double cadence_engine_tempo_bpm(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->tempoBpm() : 120.0;
}

CadenceResult cadence_engine_set_tempo_bpm(CadenceEngine* engine, double bpm)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(bpm))
        return kCadenceErrorInvalidArgument;
    return asEngine(engine)->setTempo(bpm).failed()
               ? kCadenceErrorInvalidArgument : kCadenceOk;
}

int cadence_engine_time_sig_numerator(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->timeSigNumerator() : 4;
}

int cadence_engine_time_sig_denominator(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->timeSigDenominator() : 4;
}

CadenceResult cadence_engine_set_time_sig(CadenceEngine* engine, int numerator,
                                          int denominator)
{
    if (engine == nullptr)
        return kCadenceErrorNullArgument;
    return asEngine(engine)->setTimeSignature(numerator, denominator).failed()
               ? kCadenceErrorInvalidArgument : kCadenceOk;
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

CadenceResult cadence_engine_import_midi_project(CadenceEngine* engine, const char* utf8_path)
{
    if (engine == nullptr || utf8_path == nullptr)
        return kCadenceErrorNullArgument;
    const auto r = asEngine(engine)->importMidiProject(juce::File(juce::String::fromUTF8(utf8_path)));
    return r.failed() ? kCadenceErrorLoadFailed : kCadenceOk;
}

CadenceResult cadence_engine_import_stems_folder(CadenceEngine* engine, const char* utf8_path)
{
    if (engine == nullptr || utf8_path == nullptr)
        return kCadenceErrorNullArgument;
    const auto r = asEngine(engine)->importStemsFolder(juce::File(juce::String::fromUTF8(utf8_path)));
    return r.failed() ? kCadenceErrorLoadFailed : kCadenceOk;
}

CadenceResult cadence_engine_import_audio_file(CadenceEngine* engine, const char* utf8_path)
{
    if (engine == nullptr || utf8_path == nullptr)
        return kCadenceErrorNullArgument;
    const auto r = asEngine(engine)->importAudioFile(juce::File(juce::String::fromUTF8(utf8_path)));
    return r.failed() ? kCadenceErrorLoadFailed : kCadenceOk;
}

CadenceResult cadence_engine_preview_sound(CadenceEngine* engine, const char* utf8_path)
{
    if (engine == nullptr || utf8_path == nullptr)
        return kCadenceErrorNullArgument;
    const auto r = asEngine(engine)->previewSound(
        juce::File(juce::String::fromUTF8(utf8_path)));
    return r.failed() ? kCadenceErrorLoadFailed : kCadenceOk;
}

void cadence_engine_stop_preview(CadenceEngine* engine)
{
    if (engine != nullptr)
        asEngine(engine)->stopPreview();
}

int cadence_engine_is_previewing(CadenceEngine* engine)
{
    return (engine != nullptr && asEngine(engine)->isPreviewing()) ? 1 : 0;
}

const char* cadence_engine_pick_open_file(CadenceEngine* engine,
                                          const char* utf8_start_dir,
                                          const char* utf8_filters)
{
    if (engine == nullptr)
        return "";
    const auto start = juce::File(utf8_start_dir != nullptr
                                      ? juce::String::fromUTF8(utf8_start_dir)
                                      : juce::String());
    const auto filters = juce::String::fromUTF8(utf8_filters != nullptr
                                                    ? utf8_filters
                                                    : "");
    return asEngine(engine)->pickOpenFile(start, filters);
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

CadenceResult cadence_engine_render_ex(CadenceEngine* engine, const char* utf8_out_path,
                                       double max_seconds, int bits_per_sample)
{
    if (engine == nullptr || utf8_out_path == nullptr)
        return kCadenceErrorNullArgument;
    if (! std::isfinite(max_seconds) || max_seconds <= 0.0)
        return kCadenceErrorInvalidArgument;
    const auto r = asEngine(engine)->renderToFileEx(
        juce::File(juce::String::fromUTF8(utf8_out_path)), max_seconds,
        bits_per_sample);
    return r.failed() ? kCadenceErrorRenderFailed : kCadenceOk;
}

const char* cadence_engine_last_error(CadenceEngine* engine)
{
    return engine != nullptr ? asEngine(engine)->lastErrorUtf8() : "";
}
