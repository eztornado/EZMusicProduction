// Cadence engine — C ABI. Stable surface for FFI bindings (Dart/Flutter).
// Phase 2: project model (tracks + clips), project transport, offline render.
// Keep this header pure C.

#ifndef CADENCE_ENGINE_H
#define CADENCE_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CadenceEngine CadenceEngine;

typedef int64_t CadenceTrackId;
typedef int64_t CadenceClipId;

typedef enum CadenceResult {
    kCadenceOk = 0,
    kCadenceErrorNullArgument = 1,
    kCadenceErrorNotInitialised = 2,
    kCadenceErrorLoadFailed = 3,
    kCadenceErrorAudioDevice = 4,
    kCadenceErrorInvalidArgument = 5,
    kCadenceErrorNotFound = 6,
    kCadenceErrorRenderFailed = 7
} CadenceResult;

CadenceEngine* cadence_engine_create(void);
void cadence_engine_destroy(CadenceEngine* engine);

/* ---- device ---- */

/* Opens the default OS output device. Required before play(). */
CadenceResult cadence_engine_initialise(CadenceEngine* engine);

/* ---- project document ---- */

/* Replaces the project with an empty one (stops playback). */
CadenceResult cadence_engine_new_project(CadenceEngine* engine, double sample_rate);

/* Returns the new track id, or -1 on failure. */
/* type: 0 = audio, 1 = instrument (MIDI). */
CadenceTrackId cadence_engine_add_track(CadenceEngine* engine,
                                        const char* utf8_name,
                                        int color_index,
                                        int type);

/* Adds a clip holding the whole audio file at start_seconds.
   Returns the new clip id, or -1 on failure. */
CadenceClipId cadence_engine_add_clip(CadenceEngine* engine,
                                      CadenceTrackId track_id,
                                      const char* utf8_path,
                                      double start_seconds,
                                      float gain);

CadenceResult cadence_engine_set_track_volume(CadenceEngine* engine, CadenceTrackId track_id, float linear_gain);
CadenceResult cadence_engine_set_track_pan(CadenceEngine* engine, CadenceTrackId track_id, float pan);
CadenceResult cadence_engine_set_track_mute(CadenceEngine* engine, CadenceTrackId track_id, int muted);
CadenceResult cadence_engine_set_track_solo(CadenceEngine* engine, CadenceTrackId track_id, int soloed);
CadenceResult cadence_engine_set_master_gain(CadenceEngine* engine, float linear_gain);

/* Edits: each records one undo entry; a failed lookup records none. */
CadenceResult cadence_engine_remove_track(CadenceEngine* engine, CadenceTrackId track_id);
CadenceResult cadence_engine_remove_clip(CadenceEngine* engine, CadenceClipId clip_id);
CadenceResult cadence_engine_set_clip_start(CadenceEngine* engine, CadenceClipId clip_id, double seconds);
CadenceResult cadence_engine_set_clip_gain(CadenceEngine* engine, CadenceClipId clip_id, float gain);
/* Trim/move in one command (one undo entry). Values are clamped. */
CadenceResult cadence_engine_set_clip_bounds(CadenceEngine* engine, CadenceClipId clip_id,
                                             double start_seconds, double source_offset_seconds,
                                             double length_seconds);
/* Linear fades in seconds; clamped to [0, clip length]. One undo entry. */
CadenceResult cadence_engine_set_clip_fades(CadenceEngine* engine, CadenceClipId clip_id,
                                            double fade_in_seconds, double fade_out_seconds);

/* MIDI clips: creation and note editing (one undo entry per call). */
CadenceClipId cadence_engine_add_midi_clip(CadenceEngine* engine, CadenceTrackId track_id,
                                           double start_seconds, double length_seconds);
CadenceResult cadence_engine_midi_clip_add_note(CadenceEngine* engine, CadenceClipId clip_id,
                                                int pitch, float velocity,
                                                double start_seconds, double length_seconds);
CadenceResult cadence_engine_midi_clip_remove_note(CadenceEngine* engine, CadenceClipId clip_id,
                                                   int note_index);
CadenceResult cadence_engine_midi_clip_set_note(CadenceEngine* engine, CadenceClipId clip_id,
                                                int note_index, int pitch,
                                                double start_seconds, double length_seconds);
int cadence_engine_clip_is_midi(CadenceEngine* engine, CadenceClipId clip_id);
int cadence_engine_clip_note_count(CadenceEngine* engine, CadenceClipId clip_id);
void cadence_engine_clip_note_at(CadenceEngine* engine, CadenceClipId clip_id, int index,
                                 int* out_pitch, float* out_velocity,
                                 double* out_start, double* out_length);

CadenceResult cadence_engine_set_track_name(CadenceEngine* engine, CadenceTrackId track_id,
                                            const char* utf8_name);
CadenceResult cadence_engine_set_track_color(CadenceEngine* engine, CadenceTrackId track_id,
                                             int color_index);

int cadence_engine_track_count(CadenceEngine* engine);

/* ---- document read access (UI snapshot pulls) ----
   Reads are cheap and never allocate; strings are valid until the next
   read call on the same engine. Missing ids yield -1 / "" / 0. */

CadenceTrackId cadence_engine_track_id_at(CadenceEngine* engine, int index);
const char* cadence_engine_track_name(CadenceEngine* engine, CadenceTrackId track_id);
int cadence_engine_track_color_index(CadenceEngine* engine, CadenceTrackId track_id);
float cadence_engine_track_volume(CadenceEngine* engine, CadenceTrackId track_id);
float cadence_engine_track_pan(CadenceEngine* engine, CadenceTrackId track_id);
int cadence_engine_track_mute(CadenceEngine* engine, CadenceTrackId track_id);
int cadence_engine_track_solo(CadenceEngine* engine, CadenceTrackId track_id);
int cadence_engine_track_type(CadenceEngine* engine, CadenceTrackId track_id);
int cadence_engine_track_clip_count(CadenceEngine* engine, CadenceTrackId track_id);
CadenceClipId cadence_engine_clip_id_at(CadenceEngine* engine, CadenceTrackId track_id, int index);
double cadence_engine_clip_start(CadenceEngine* engine, CadenceClipId clip_id);
double cadence_engine_clip_length(CadenceEngine* engine, CadenceClipId clip_id);
double cadence_engine_clip_offset(CadenceEngine* engine, CadenceClipId clip_id);
/* Max visible length: source duration - current offset. */
double cadence_engine_clip_length_max(CadenceEngine* engine, CadenceClipId clip_id);
double cadence_engine_clip_fade_in(CadenceEngine* engine, CadenceClipId clip_id);
double cadence_engine_clip_fade_out(CadenceEngine* engine, CadenceClipId clip_id);

/* Waveform display peaks: fills out_mins/out_maxs (num_pixels floats each)
   for the clip-relative region. Returns 0 on success, -1 on failure. */
int cadence_engine_clip_peaks(CadenceEngine* engine, CadenceClipId clip_id,
                              double start_seconds, double length_seconds,
                              int num_pixels, float* out_mins, float* out_maxs);

/* Metering: read-and-reset peak (linear, 0..~1.5) since the last poll.
   track_index is the track position in the snapshot. */
float cadence_engine_meter_track_peak(CadenceEngine* engine, int track_index);
float cadence_engine_meter_master_peak(CadenceEngine* engine, int channel);

/* ---- device configuration (§7.14) ---- */

const char* cadence_engine_current_output_device_name(CadenceEngine* engine);
const char* cadence_engine_current_input_device_name(CadenceEngine* engine);
int cadence_engine_output_device_count(CadenceEngine* engine);
const char* cadence_engine_output_device_name_at(CadenceEngine* engine, int index);
int cadence_engine_input_device_count(CadenceEngine* engine);
const char* cadence_engine_input_device_name_at(CadenceEngine* engine, int index);
int cadence_engine_available_sample_rate_count(CadenceEngine* engine);
double cadence_engine_available_sample_rate_at(CadenceEngine* engine, int index);
int cadence_engine_available_buffer_size_count(CadenceEngine* engine);
int cadence_engine_available_buffer_size_at(CadenceEngine* engine, int index);

/* Applies a device configuration. Empty strings / 0 keep current values.
   Reopens the device; playback stops briefly. */
CadenceResult cadence_engine_set_device_config(CadenceEngine* engine,
                                               const char* utf8_output,
                                               const char* utf8_input,
                                               double sample_rate,
                                               int buffer_size);

int cadence_engine_midi_input_count(CadenceEngine* engine);
int cadence_engine_device_input_channel_count(CadenceEngine* engine);

/* MIDI input selection (v1: single global input; -1 clears). */
void cadence_engine_select_midi_input(CadenceEngine* engine, int index);
int cadence_engine_selected_midi_input_index(CadenceEngine* engine);
const char* cadence_engine_midi_input_name_at(CadenceEngine* engine, int index);

/* Device/MIDI settings file (JSON). Save/load round-trips the
   AudioDeviceManager state plus the enabled MIDI inputs. */
CadenceResult cadence_engine_save_device_settings(CadenceEngine* engine, const char* utf8_path);
CadenceResult cadence_engine_load_device_settings(CadenceEngine* engine, const char* utf8_path);

/* ---- recording ---- */

/* Arm + input channel: workflow state, no undo entries. */
CadenceResult cadence_engine_set_track_armed(CadenceEngine* engine, CadenceTrackId track_id, int armed);
CadenceResult cadence_engine_set_track_input_channel(CadenceEngine* engine, CadenceTrackId track_id, int channel);
int cadence_engine_track_armed(CadenceEngine* engine, CadenceTrackId track_id);
int cadence_engine_track_input_channel(CadenceEngine* engine, CadenceTrackId track_id);

/* Starts capture on every armed track (requires device + armed tracks).
   stop/pause/stop_record finalize the takes as clips (one undo entry);
   takes are written as 24-bit WAVs under Recordings/. */
CadenceResult cadence_engine_start_record(CadenceEngine* engine);
CadenceResult cadence_engine_stop_record(CadenceEngine* engine);
int cadence_engine_is_recording(CadenceEngine* engine);
double cadence_engine_record_start_seconds(CadenceEngine* engine);

/* Test hook: when enabled, record captures synthesize a tone instead of
   reading hardware inputs (deterministic pipeline verification). */
void cadence_engine_set_debug_loopback(int enabled);

/* Metronome click on every beat (accent on bar start, 4/4 fixed). */
void cadence_engine_set_metronome(CadenceEngine* engine, int enabled);

/* ---- undo / redo (engine-owned) ---- */

int cadence_engine_can_undo(CadenceEngine* engine);
int cadence_engine_can_redo(CadenceEngine* engine);
CadenceResult cadence_engine_undo(CadenceEngine* engine);
CadenceResult cadence_engine_redo(CadenceEngine* engine);

/* ---- persistence ---- */

/* Saving records no undo entry; loading replaces the document and
   clears the history. Source paths are stored relative to the project
   file when possible. */
CadenceResult cadence_engine_save_project(CadenceEngine* engine, const char* utf8_path);
CadenceResult cadence_engine_load_project(CadenceEngine* engine, const char* utf8_path);

/* Convenience (phase 1 API): a one-track project with the whole file. */
CadenceResult cadence_engine_load_file(CadenceEngine* engine, const char* utf8_path);

/* ---- transport ---- */

CadenceResult cadence_engine_play(CadenceEngine* engine);
CadenceResult cadence_engine_pause(CadenceEngine* engine); /* keeps position */
CadenceResult cadence_engine_stop(CadenceEngine* engine);   /* rewinds to 0 */
CadenceResult cadence_engine_seek(CadenceEngine* engine, double seconds);
CadenceResult cadence_engine_set_gain(CadenceEngine* engine, float linear_gain); /* master */

int    cadence_engine_is_playing(CadenceEngine* engine);
double cadence_engine_position_seconds(CadenceEngine* engine);
double cadence_engine_duration_seconds(CadenceEngine* engine);
double cadence_engine_sample_rate(CadenceEngine* engine);
int    cadence_engine_buffer_size_samples(CadenceEngine* engine);
double cadence_engine_master_gain(CadenceEngine* engine);

/* ---- offline render ---- */

/* Renders the project from position 0 to a 16-bit stereo WAV.
   max_seconds caps the length; pass a large value for the full project. */
CadenceResult cadence_engine_render(CadenceEngine* engine,
                                    const char* utf8_out_path,
                                    double max_seconds);

/* UTF-8 description of the last failed call on this engine.
   The pointer is valid until the next failing call on the same engine. */
const char* cadence_engine_last_error(CadenceEngine* engine);

#ifdef __cplusplus
}
#endif

#endif /* CADENCE_ENGINE_H */
