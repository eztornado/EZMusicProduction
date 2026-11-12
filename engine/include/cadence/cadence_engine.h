// Cadence engine — C ABI. Stable surface for FFI bindings (Dart/Flutter).
// Phase 2: project model (tracks + clips), project transport, offline render.
// Keep this header pure C.

#ifndef CADENCE_ENGINE_H
#define CADENCE_ENGINE_H

#include <stdint.h>

/* DLL export/import for Windows; no-op on other platforms. */
#if defined(_WIN32)
  #ifdef CADENCE_EXPORTS
    #define CADENCE_API __declspec(dllexport)
  #else
    #define CADENCE_API __declspec(dllimport)
  #endif
#else
  #define CADENCE_API
#endif

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
    kCadenceErrorRenderFailed = 7,
    kCadenceErrorNetwork = 8
} CadenceResult;

CADENCE_API CadenceEngine* cadence_engine_create(void);
CADENCE_API void cadence_engine_destroy(CadenceEngine* engine);
/* Destroys the process-wide active engine, if any. Safe to call twice and
   from the macOS AppDelegate on applicationWillTerminate. */
CADENCE_API void cadence_engine_destroy_active(void);
/* Returns the last created engine (for native drop handlers in Swift). */
CADENCE_API CadenceEngine* cadence_engine_get_active(void);
/* Adds an audio file as a clip to the first track at the playhead position.
   Called from Swift when the user drops an audio file on the timeline. */
CADENCE_API CadenceResult cadence_engine_drop_file(const char* utf8_path);

/* ---- device ---- */

/* Opens the default OS output device. Required before play(). */
CADENCE_API CadenceResult cadence_engine_initialise(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_restart_audio(CadenceEngine* engine);
/* Opens the audio device on a background thread (non-blocking).
   Poll audio_initialised() to check when it's ready. */
CADENCE_API void cadence_engine_initialise_audio_background(CadenceEngine* engine);
CADENCE_API int cadence_engine_audio_initialised(CadenceEngine* engine);
/* Health/diagnostics. Safe from any thread. */
CADENCE_API int cadence_engine_audio_device_running(CadenceEngine* engine);
CADENCE_API int cadence_engine_audio_xrun_count(CadenceEngine* engine);
CADENCE_API int cadence_engine_audio_recovery_count(CadenceEngine* engine);
CADENCE_API const char* cadence_engine_last_audio_error(CadenceEngine* engine);

/* ---- project document ---- */

/* Replaces the project with an empty one (stops playback). */
CADENCE_API CadenceResult cadence_engine_new_project(CadenceEngine* engine, double sample_rate);

/* Returns the new track id, or -1 on failure. */
/* type: 0 = audio, 1 = instrument (MIDI). */
CADENCE_API CadenceTrackId cadence_engine_add_track(CadenceEngine* engine,
                                        const char* utf8_name,
                                        int color_index,
                                        int type);

/* Adds a clip holding the whole audio file at start_seconds.
   Returns the new clip id, or -1 on failure. */
CADENCE_API CadenceClipId cadence_engine_add_clip(CadenceEngine* engine,
                                      CadenceTrackId track_id,
                                      const char* utf8_path,
                                      double start_seconds,
                                      float gain);

CADENCE_API CadenceResult cadence_engine_set_track_volume(CadenceEngine* engine, CadenceTrackId track_id, float linear_gain);

/* Volume automation (§7.12 v1, Read always active): replaces the whole
   curve — count 0 clears it. times in project seconds, values 0..1. One
   undo entry per call. */
CADENCE_API CadenceResult cadence_engine_set_track_volume_automation(CadenceEngine* engine,
                                                         CadenceTrackId track_id,
                                                         const double* times,
                                                         const double* values,
                                                         int count);
CADENCE_API int cadence_engine_track_volume_auto_count(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API float cadence_engine_track_volume_effective_at(CadenceEngine* engine,
                                               CadenceTrackId track_id,
                                               double time_seconds);

/* Generic automation lanes (§7.12 v1.5): lane 0 = volume, 1 = pan,
   2 = sustain (CC64). Whole-curve replace; times in project seconds,
   values 0..1. */
CADENCE_API CadenceResult cadence_engine_set_track_lane_automation(CadenceEngine* engine,
                                                       CadenceTrackId track_id,
                                                       int lane,
                                                       const double* times,
                                                       const double* values,
                                                       int count);
CADENCE_API int cadence_engine_track_lane_auto_count(CadenceEngine* engine,
                                         CadenceTrackId track_id, int lane);
CADENCE_API CadenceResult cadence_engine_track_lane_auto_at(CadenceEngine* engine,
                                                CadenceTrackId track_id, int lane,
                                                int index, double* out_time,
                                                double* out_value);
CADENCE_API CadenceResult cadence_engine_track_volume_auto_at(CadenceEngine* engine,
                                                  CadenceTrackId track_id, int index,
                                                  double* out_time, double* out_value);

/* Plugin-parameter automation lanes (§7.12 v1.5). slot_kind: 0 = fx chain
   slot, 1 = instrument. set replaces the whole lane (one undo). The lane
   list is per track; name is captured for display. */
CADENCE_API CadenceResult cadence_engine_set_track_param_automation(CadenceEngine* engine,
                                                        CadenceTrackId track_id,
                                                        int slot_kind,
                                                        int slot_index,
                                                        int param_index,
                                                        const char* utf8_name,
                                                        const double* times,
                                                        const double* values,
                                                        int count);
CADENCE_API int cadence_engine_track_param_lane_count(CadenceEngine* engine,
                                          CadenceTrackId track_id);
CADENCE_API CadenceResult cadence_engine_track_param_lane_info(CadenceEngine* engine,
                                                   CadenceTrackId track_id,
                                                   int lane_index,
                                                   int* out_slot_kind,
                                                   int* out_slot_index,
                                                   int* out_param_index);
CADENCE_API const char* cadence_engine_track_param_lane_name(CadenceEngine* engine,
                                                 CadenceTrackId track_id,
                                                 int lane_index);
CADENCE_API int cadence_engine_track_param_lane_point_count(CadenceEngine* engine,
                                                CadenceTrackId track_id,
                                                int lane_index);
CADENCE_API CadenceResult cadence_engine_track_param_lane_point_at(CadenceEngine* engine,
                                                       CadenceTrackId track_id,
                                                       int lane_index,
                                                       int point_index,
                                                       double* out_time,
                                                       double* out_value);
CADENCE_API CadenceResult cadence_engine_remove_track_param_lane(CadenceEngine* engine,
                                                     CadenceTrackId track_id,
                                                     int lane_index);
/* Parameter enumeration for the chooser UI (requires a live plugin node). */
CADENCE_API int cadence_engine_plugin_param_count(CadenceEngine* engine,
                                      CadenceTrackId track_id, int slot_kind,
                                      int slot_index);
CADENCE_API const char* cadence_engine_plugin_param_name(CadenceEngine* engine,
                                             CadenceTrackId track_id,
                                             int slot_kind, int slot_index,
                                             int param_index);
/* Normalised (0..1) parameter value / human-readable text. Used by the remote
   control API to adjust effects and instruments. */
CADENCE_API float cadence_engine_plugin_param_value(CadenceEngine* engine,
                                        CadenceTrackId track_id,
                                        int slot_kind, int slot_index,
                                        int param_index);
CADENCE_API const char* cadence_engine_plugin_param_text(CadenceEngine* engine,
                                             CadenceTrackId track_id,
                                             int slot_kind, int slot_index,
                                             int param_index);
/* Sets a normalised (0..1) value on the live plug-in. Message thread only. */
CADENCE_API CadenceResult cadence_engine_set_plugin_param_value(CadenceEngine* engine,
                                                    CadenceTrackId track_id,
                                                    int slot_kind,
                                                    int slot_index,
                                                    int param_index,
                                                    float value);
/* Write mode (§7.12 v1.5, the W button): inserts or replaces one point at
   the given time. */
CADENCE_API CadenceResult cadence_engine_set_track_volume_auto_point_at(CadenceEngine* engine,
                                                            CadenceTrackId track_id,
                                                            double time_seconds,
                                                            double value);

/* Input quantize (§7.5.2 v1.5): grid in seconds for recorded MIDI takes
   (0 = off). */
CADENCE_API double cadence_engine_input_quantize(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_set_input_quantize(CadenceEngine* engine,
                                                double grid_seconds);

/* MIDI input latency compensation (ms): recorded takes shift this many ms
   earlier at finalize. Persisted with the device settings. */
CADENCE_API double cadence_engine_midi_input_latency(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_set_midi_input_latency(CadenceEngine* engine,
                                                    double ms);

/* Clip clipboard (§7.4): copy a snapshot, paste a duplicate on a
   same-kind track at the given timeline position. */
CADENCE_API CadenceResult cadence_engine_copy_clip(CadenceEngine* engine,
                                       CadenceClipId clip_id);
CADENCE_API CadenceResult cadence_engine_paste_clip(CadenceEngine* engine,
                                        CadenceTrackId track_id,
                                        double start_seconds);

/* Deferred plugin-load queue (project load). */
CADENCE_API int cadence_engine_has_pending_plugin_jobs(CadenceEngine* engine);
CADENCE_API void cadence_engine_pump_one_plugin_job(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_set_track_pan(CadenceEngine* engine, CadenceTrackId track_id, float pan);
CADENCE_API CadenceResult cadence_engine_set_track_mute(CadenceEngine* engine, CadenceTrackId track_id, int muted);
CADENCE_API CadenceResult cadence_engine_set_track_solo(CadenceEngine* engine, CadenceTrackId track_id, int soloed);
CADENCE_API CadenceResult cadence_engine_set_master_gain(CadenceEngine* engine, float linear_gain);

/* Edits: each records one undo entry; a failed lookup records none. */
CADENCE_API CadenceResult cadence_engine_remove_track(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API CadenceResult cadence_engine_remove_clip(CadenceEngine* engine, CadenceClipId clip_id);
CADENCE_API CadenceResult cadence_engine_set_clip_start(CadenceEngine* engine, CadenceClipId clip_id, double seconds);
CADENCE_API CadenceResult cadence_engine_set_clip_gain(CadenceEngine* engine, CadenceClipId clip_id, float gain);
/* Trim/move in one command (one undo entry). Values are clamped. */
CADENCE_API CadenceResult cadence_engine_set_clip_bounds(CadenceEngine* engine, CadenceClipId clip_id,
                                             double start_seconds, double source_offset_seconds,
                                             double length_seconds);
/* Linear fades in seconds; clamped to [0, clip length]. One undo entry. */
CADENCE_API CadenceResult cadence_engine_set_clip_fades(CadenceEngine* engine, CadenceClipId clip_id,
                                            double fade_in_seconds, double fade_out_seconds);
CADENCE_API CadenceResult cadence_engine_move_clip_to_track(CadenceEngine* engine, CadenceClipId clip_id,
                                                CadenceTrackId track_id, double start_seconds);
/* Group edits (timeline multi-selection): all clips are moved/removed as ONE
   undo entry. Arrays must have `count` elements; track_ids[i] is the target
   track for clip_ids[i]. */
CADENCE_API CadenceResult cadence_engine_move_clips(CadenceEngine* engine,
                                        const CadenceClipId* clip_ids,
                                        const CadenceTrackId* track_ids,
                                        const double* start_seconds,
                                        int count);
CADENCE_API CadenceResult cadence_engine_remove_clips(CadenceEngine* engine,
                                          const CadenceClipId* clip_ids,
                                          int count);
CADENCE_API CadenceResult cadence_engine_split_clip(CadenceEngine* engine, CadenceClipId clip_id,
                                        double time_seconds);

/* MIDI clips: creation and note editing (one undo entry per call). */
CADENCE_API CadenceClipId cadence_engine_add_midi_clip(CadenceEngine* engine, CadenceTrackId track_id,
                                           double start_seconds, double length_seconds);
CADENCE_API CadenceResult cadence_engine_midi_clip_add_note(CadenceEngine* engine, CadenceClipId clip_id,
                                                int pitch, float velocity,
                                                double start_seconds, double length_seconds);
CADENCE_API CadenceResult cadence_engine_midi_clip_remove_note(CadenceEngine* engine, CadenceClipId clip_id,
                                                   int note_index);
CADENCE_API CadenceResult cadence_engine_midi_clip_set_note(CadenceEngine* engine, CadenceClipId clip_id,
                                                int note_index, int pitch,
                                                double start_seconds, double length_seconds);
/* Applies several note edits in one undo entry (multi-note move/resize). */
CADENCE_API CadenceResult cadence_engine_midi_clip_set_notes(CadenceEngine* engine, CadenceClipId clip_id,
                                                 const int* note_indices,
                                                 const int* pitches,
                                                 const double* start_seconds,
                                                 const double* length_seconds,
                                                 int count);
CADENCE_API int cadence_engine_clip_is_midi(CadenceEngine* engine, CadenceClipId clip_id);
CADENCE_API int cadence_engine_clip_note_count(CadenceEngine* engine, CadenceClipId clip_id);
CADENCE_API void cadence_engine_clip_note_at(CadenceEngine* engine, CadenceClipId clip_id, int index,
                                 int* out_pitch, float* out_velocity,
                                 double* out_start, double* out_length);
CADENCE_API double cadence_engine_clip_note_orig_start_at(CadenceEngine* engine, CadenceClipId clip_id,
                                              int index);

/* Quantize (§7.5.2, non-destructive): moves note starts toward the nearest
   grid position. swing/catch_range/max_move in 0..1; non_grid/iterative as
   booleans; iter_strength 0..1 for the iQ step. reset restores original
   starts, freeze bakes the current ones as the new reference. */
CADENCE_API CadenceResult cadence_engine_quantize_clip_notes(CadenceEngine* engine, CadenceClipId clip_id,
                                                 double grid_seconds, double swing,
                                                 double catch_range, double max_move,
                                                 int non_grid, int iterative,
                                                 double iter_strength);
CADENCE_API CadenceResult cadence_engine_reset_clip_quantize(CadenceEngine* engine, CadenceClipId clip_id);
CADENCE_API CadenceResult cadence_engine_freeze_clip_quantize(CadenceEngine* engine, CadenceClipId clip_id);

/* ---- AI clip generation ----
   Generation runs on an engine worker thread. Start returns 1 when the job
   was queued, 0 when another job is already running / the track is invalid.
   Poll cadence_engine_ai_generation_state:
     0 idle, 1 running, 2 ready, 3 failed.
   When it reaches 2 or 3 read the result through the ai_result_* getters,
   then apply it with cadence_engine_apply_ai_generation (one undo entry).
   mode: 0 melody, 1 drums, 2 bass, 3 harmony/chords.
   bars: 1..4 for the legacy call, 1..64 total for _long. temperature:
   ~0.4..1.4. variations: 1..8.
   length_seconds (only in _long): exact clip duration, capped at 120 s;
   0 makes the engine use bars * bar length. Long requests are generated in
   4-bar model windows that continue from the previous phrase.
   prompt: free text in Spanish or English (may be NULL).
   replace_clip: rewrite clip_id instead of inserting a new clip.
   harmonize: add chord tones under the generated lead when appropriate.
   use_model: 0 forces the built-in procedural generator (no checkpoints). */
CADENCE_API void cadence_engine_set_ai_model_dir(CadenceEngine* engine, const char* utf8_path);
CADENCE_API const char* cadence_engine_ai_model_dir(CadenceEngine* engine);
CADENCE_API int cadence_engine_ai_model_available(CadenceEngine* engine, int mode);

CADENCE_API int cadence_engine_start_ai_generation(CadenceEngine* engine,
                                       CadenceTrackId track_id,
                                       CadenceClipId clip_id,
                                       double anchor_seconds,
                                       int mode, int bars,
                                       double temperature, int variations,
                                       int seed, const char* utf8_prompt,
                                       int replace_clip, int harmonize,
                                       int use_model);
CADENCE_API int cadence_engine_start_ai_generation_long(CadenceEngine* engine,
                                            CadenceTrackId track_id,
                                            CadenceClipId clip_id,
                                            double anchor_seconds,
                                            int mode, int bars,
                                            double length_seconds,
                                            double temperature, int variations,
                                            int seed, const char* utf8_prompt,
                                            int replace_clip, int harmonize,
                                            int use_model);
CADENCE_API int cadence_engine_ai_generation_state(CadenceEngine* engine);
CADENCE_API void cadence_engine_cancel_ai_generation(CadenceEngine* engine);
CADENCE_API void cadence_engine_clear_ai_generation(CadenceEngine* engine);

CADENCE_API int cadence_engine_ai_result_ok(CadenceEngine* engine);
/* field: 0 error, 1 provider, 2 key, 3 chords, 4 prompt interpretation,
   5 heard context (audio/MIDI). */
CADENCE_API const char* cadence_engine_ai_result_string(CadenceEngine* engine, int field);
CADENCE_API int cadence_engine_ai_result_seed(CadenceEngine* engine);
CADENCE_API double cadence_engine_ai_result_clip_length(CadenceEngine* engine);
CADENCE_API double cadence_engine_ai_result_start(CadenceEngine* engine);
CADENCE_API CadenceClipId cadence_engine_ai_result_clip_id(CadenceEngine* engine);
CADENCE_API CadenceTrackId cadence_engine_ai_result_track_id(CadenceEngine* engine);
CADENCE_API int cadence_engine_ai_result_note_count(CadenceEngine* engine);
CADENCE_API void cadence_engine_ai_result_note_at(CadenceEngine* engine, int index,
                                      int* out_pitch, float* out_velocity,
                                      double* out_start, double* out_length);

/* Applies the ready result atomically (one undo entry). When out_clip_id is
   non-NULL it receives the created/rewritten clip id. */
CADENCE_API CadenceResult cadence_engine_apply_ai_generation(CadenceEngine* engine,
                                                 CadenceClipId* out_clip_id);

/* ---- AI song arranger (multiple tracks in one undo entry) ----
   The UI builds the creative brief and the model returns a JSON plan; these
   calls only expose the project context and apply the plan. No network code
   lives in the engine.
   cadence_engine_ai_context_json returns a compact, read-only description of
   the project (tempo, meter, key/chords, tracks, instruments and short note
   excerpts) for the selected track and anchor. selected_track_id may be -1.
   cadence_engine_apply_ai_arrangement validates the whole plan first and then
   creates missing instrument tracks and MIDI clips as one undo entry. The
   result summary (created/reused tracks, clip ids, note count) is available as
   JSON through cadence_engine_ai_arrangement_result_json. On failure the last
   error is returned by cadence_engine_last_error. */
CADENCE_API const char* cadence_engine_ai_context_json(CadenceEngine* engine,
                                           CadenceTrackId selected_track_id,
                                           double anchor_seconds, int bars);
CADENCE_API CadenceResult cadence_engine_apply_ai_arrangement(CadenceEngine* engine,
                                                  const char* utf8_json);
CADENCE_API const char* cadence_engine_ai_arrangement_result_json(CadenceEngine* engine);

/* ---- AI mix analysis / mastering ----
   Renders the project offline and analyses levels, loudness, spectrum, stereo
   image and cross-track masking. Poll cadence_engine_mix_analysis_state:
     0 idle, 1 running, 2 ready, 3 failed.
   The report (valid when the state is 2 or 3) is returned as JSON by
   cadence_engine_mix_analysis_json. Apply its fixes with
   cadence_engine_apply_mix_plan (one undo entry), one issue by id, or an
   explicit JSON array of moves. Applied moves produce a JSON summary through
   cadence_engine_mix_apply_json.

   prompt: free text in Spanish or English (may be NULL). It can set the
   loudness target ("-14 LUFS"), true-peak ceiling ("-1 dBTP"), balance
   profile ("pop", "rock", "hiphop", "electronic", "acoustic", "orchestral"),
   "master only" and "gentle"/"no EQ"/"no dynamics" constraints.
   profile: -1 infers it from the prompt, otherwise 0 pop, 1 electronic,
   2 hiphop, 3 rock, 4 acoustic, 5 orchestral.
   master_only: limit the fixes to the master bus.
   max_issues: 0 for no limit, otherwise the report is capped. */
CADENCE_API int cadence_engine_start_mix_analysis(CadenceEngine* engine,
                                      const char* utf8_prompt,
                                      double target_lufs, double true_peak_db,
                                      int profile, int master_only,
                                      int max_issues);
CADENCE_API int cadence_engine_mix_analysis_state(CadenceEngine* engine);
CADENCE_API void cadence_engine_cancel_mix_analysis(CadenceEngine* engine);
CADENCE_API void cadence_engine_clear_mix_analysis(CadenceEngine* engine);
CADENCE_API const char* cadence_engine_mix_analysis_json(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_apply_mix_plan(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_apply_mix_issue(CadenceEngine* engine,
                                             const char* utf8_issue_id);
CADENCE_API CadenceResult cadence_engine_apply_mix_moves(CadenceEngine* engine,
                                             const char* utf8_moves_json);
CADENCE_API const char* cadence_engine_mix_apply_json(CadenceEngine* engine);

CADENCE_API CadenceResult cadence_engine_set_track_name(CadenceEngine* engine, CadenceTrackId track_id,
                                            const char* utf8_name);
CADENCE_API CadenceResult cadence_engine_set_track_color(CadenceEngine* engine, CadenceTrackId track_id,
                                             int color_index);

CADENCE_API int cadence_engine_track_count(CadenceEngine* engine);

/* ---- document read access (UI snapshot pulls) ----
   Reads are cheap and never allocate; strings are valid until the next
   read call on the same engine. Missing ids yield -1 / "" / 0. */

CADENCE_API CadenceTrackId cadence_engine_track_id_at(CadenceEngine* engine, int index);
CADENCE_API const char* cadence_engine_track_name(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API int cadence_engine_track_color_index(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API float cadence_engine_track_volume(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API float cadence_engine_track_pan(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API int cadence_engine_track_mute(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API int cadence_engine_track_solo(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API int cadence_engine_track_type(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API int cadence_engine_track_clip_count(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API CadenceClipId cadence_engine_clip_id_at(CadenceEngine* engine, CadenceTrackId track_id, int index);
CADENCE_API double cadence_engine_clip_start(CadenceEngine* engine, CadenceClipId clip_id);
CADENCE_API double cadence_engine_clip_length(CadenceEngine* engine, CadenceClipId clip_id);
CADENCE_API double cadence_engine_clip_offset(CadenceEngine* engine, CadenceClipId clip_id);
/* Max visible length: source duration - current offset. */
CADENCE_API double cadence_engine_clip_length_max(CadenceEngine* engine, CadenceClipId clip_id);
CADENCE_API double cadence_engine_clip_fade_in(CadenceEngine* engine, CadenceClipId clip_id);
CADENCE_API double cadence_engine_clip_fade_out(CadenceEngine* engine, CadenceClipId clip_id);

/* Waveform display peaks: fills out_mins/out_maxs (num_pixels floats each)
   for the clip-relative region. Returns 0 on success, -1 on failure. */
CADENCE_API int cadence_engine_clip_peaks(CadenceEngine* engine, CadenceClipId clip_id,
                              double start_seconds, double length_seconds,
                              int num_pixels, float* out_mins, float* out_maxs);

/* Metering: read-and-reset peak (linear, 0..~1.5) since the last poll.
   track_index is the track position in the snapshot. */
CADENCE_API float cadence_engine_meter_track_peak(CadenceEngine* engine, int track_index);
CADENCE_API float cadence_engine_meter_master_peak(CadenceEngine* engine, int channel);

/* ---- device configuration (§7.14) ---- */

CADENCE_API const char* cadence_engine_current_output_device_name(CadenceEngine* engine);
CADENCE_API const char* cadence_engine_current_input_device_name(CadenceEngine* engine);
CADENCE_API int cadence_engine_output_device_count(CadenceEngine* engine);
CADENCE_API const char* cadence_engine_output_device_name_at(CadenceEngine* engine, int index);
CADENCE_API int cadence_engine_input_device_count(CadenceEngine* engine);
CADENCE_API const char* cadence_engine_input_device_name_at(CadenceEngine* engine, int index);
CADENCE_API int cadence_engine_available_sample_rate_count(CadenceEngine* engine);
CADENCE_API double cadence_engine_available_sample_rate_at(CadenceEngine* engine, int index);
CADENCE_API int cadence_engine_available_buffer_size_count(CadenceEngine* engine);
CADENCE_API int cadence_engine_available_buffer_size_at(CadenceEngine* engine, int index);

/* Applies a device configuration. Empty strings / 0 keep current values.
   Reopens the device; playback stops briefly. */
CADENCE_API CadenceResult cadence_engine_set_device_config(CadenceEngine* engine,
                                               const char* utf8_output,
                                               const char* utf8_input,
                                               double sample_rate,
                                               int buffer_size);

CADENCE_API int cadence_engine_midi_input_count(CadenceEngine* engine);
CADENCE_API int cadence_engine_device_input_channel_count(CadenceEngine* engine);

/* MIDI input selection (v1: single global input; -1 clears). */
CADENCE_API void cadence_engine_select_midi_input(CadenceEngine* engine, int index);
CADENCE_API int cadence_engine_selected_midi_input_index(CadenceEngine* engine);
CADENCE_API const char* cadence_engine_midi_input_name_at(CadenceEngine* engine, int index);

/* Device/MIDI settings file (JSON). Save/load round-trips the
   AudioDeviceManager state plus the enabled MIDI inputs. */
CADENCE_API CadenceResult cadence_engine_save_device_settings(CadenceEngine* engine, const char* utf8_path);
CADENCE_API CadenceResult cadence_engine_load_device_settings(CadenceEngine* engine, const char* utf8_path);

/* ---- recording ---- */

/* Arm + input channel: workflow state, no undo entries. */
CADENCE_API CadenceResult cadence_engine_set_track_armed(CadenceEngine* engine, CadenceTrackId track_id, int armed);
CADENCE_API CadenceResult cadence_engine_set_track_input_channel(CadenceEngine* engine, CadenceTrackId track_id, int channel);
CADENCE_API int cadence_engine_track_armed(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API int cadence_engine_track_input_channel(CadenceEngine* engine, CadenceTrackId track_id);

/* ---- plugins (§7.7): VST3 + AudioUnit ---- */

CADENCE_API void cadence_engine_rescan_plugins(CadenceEngine* engine);
CADENCE_API void cadence_engine_scan_plugins_now(CadenceEngine* engine);
CADENCE_API int cadence_engine_plugin_scan_active(CadenceEngine* engine);
CADENCE_API int cadence_engine_plugin_count(CadenceEngine* engine);
CADENCE_API const char* cadence_engine_plugin_name_at(CadenceEngine* engine, int index);
CADENCE_API const char* cadence_engine_plugin_uid_at(CadenceEngine* engine, int index);
CADENCE_API int cadence_engine_plugin_is_instrument_at(CadenceEngine* engine, int index);
CADENCE_API const char* cadence_engine_plugin_format_at(CadenceEngine* engine, int index);

CADENCE_API CadenceResult cadence_engine_track_add_fx(CadenceEngine* engine, CadenceTrackId track_id,
                                          int plugin_index);
CADENCE_API CadenceResult cadence_engine_track_remove_fx(CadenceEngine* engine, CadenceTrackId track_id,
                                             int slot_index);
CADENCE_API CadenceResult cadence_engine_track_set_fx_bypass(CadenceEngine* engine, CadenceTrackId track_id,
                                                 int slot_index, int bypass);
CADENCE_API int cadence_engine_track_fx_count(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API const char* cadence_engine_track_fx_name_at(CadenceEngine* engine, CadenceTrackId track_id,
                                            int slot_index);
CADENCE_API int cadence_engine_track_fx_bypass_at(CadenceEngine* engine, CadenceTrackId track_id,
                                      int slot_index);

CADENCE_API CadenceResult cadence_engine_track_set_instrument_plugin(CadenceEngine* engine,
                                                         CadenceTrackId track_id,
                                                         int plugin_index);
CADENCE_API CadenceResult cadence_engine_track_clear_instrument(CadenceEngine* engine,
                                                    CadenceTrackId track_id);
CADENCE_API CadenceResult cadence_engine_track_set_midifx(CadenceEngine* engine,
                                              CadenceTrackId track_id,
                                              int plugin_index);
CADENCE_API CadenceResult cadence_engine_track_clear_midifx(CadenceEngine* engine,
                                                CadenceTrackId track_id);
CADENCE_API const char* cadence_engine_track_midifx_name(CadenceEngine* engine,
                                             CadenceTrackId track_id);
CADENCE_API const char* cadence_engine_track_instrument_name(CadenceEngine* engine,
                                                 CadenceTrackId track_id);

/* Restores a saved plug-in state on the LIVE instance in an insert slot /
   instrument. `component`/`controller` are the raw VST3 component and
   controller states as stored in a Cubase project (either may be NULL/0).
   Returns kCadenceErrorNotFound when the slot holds no live plug-in. */
CADENCE_API CadenceResult cadence_engine_track_set_fx_state(CadenceEngine* engine,
                                                CadenceTrackId track_id,
                                                int slot_index,
                                                const void* component,
                                                int component_size,
                                                const void* controller,
                                                int controller_size);
CADENCE_API CadenceResult cadence_engine_track_set_instrument_state(CadenceEngine* engine,
                                                        CadenceTrackId track_id,
                                                        const void* component,
                                                        int component_size,
                                                        const void* controller,
                                                        int controller_size);
/* Test hook: the state the live plug-in currently reports, base64-encoded
   (empty when the slot is empty). slot_kind: 0 = fx chain, 1 = instrument. */
CADENCE_API const char* cadence_engine_plugin_state_base64(CadenceEngine* engine,
                                               CadenceTrackId track_id,
                                               int slot_kind, int slot_index);

/* Opens the plugin's own editor window (or a generic parameter view).
   Returns kCadenceErrorNotFound when the slot/track has no plugin. */
CADENCE_API CadenceResult cadence_engine_open_plugin_editor(CadenceEngine* engine,
                                                CadenceTrackId track_id,
                                                int slot_index);
CADENCE_API CadenceResult cadence_engine_open_instrument_editor(CadenceEngine* engine,
                                                    CadenceTrackId track_id);
/* Opens the instrument's editor bound to a MIDI clip (EZSynth edits that
   clip from its own piano roll; other instruments ignore the clip). */
CADENCE_API CadenceResult cadence_engine_open_instrument_editor_with_clip(
    CadenceEngine* engine, CadenceTrackId track_id, long long clip_id);

CADENCE_API int cadence_engine_master_fx_count(CadenceEngine* engine);
CADENCE_API const char* cadence_engine_master_fx_name_at(CadenceEngine* engine, int slot_index);
CADENCE_API CadenceResult cadence_engine_master_add_fx(CadenceEngine* engine, int plugin_index);
CADENCE_API CadenceResult cadence_engine_master_remove_fx(CadenceEngine* engine, int slot_index);
CADENCE_API CadenceResult cadence_engine_master_set_fx_bypass(CadenceEngine* engine, int slot_index,
                                                  int bypass);
CADENCE_API int cadence_engine_master_fx_bypass_at(CadenceEngine* engine, int slot_index);
/* Opens the master insert's native editor window (message thread). */
CADENCE_API CadenceResult cadence_engine_open_master_fx_editor(CadenceEngine* engine,
                                                    int slot_index);

/* Starts capture on every armed track (requires device + armed tracks).
   stop/pause/stop_record finalize the takes as clips (one undo entry);
   takes are written as 24-bit WAVs under Recordings/. */
CADENCE_API CadenceResult cadence_engine_start_record(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_stop_record(CadenceEngine* engine);
CADENCE_API int cadence_engine_is_recording(CadenceEngine* engine);
CADENCE_API double cadence_engine_record_start_seconds(CadenceEngine* engine);

/* Live MIDI take of an armed instrument track while recording (§7.11): the
   timeline draws the notes as they arrive instead of showing an empty clip
   until the take is finalized. Note starts are relative to the record start,
   exactly like a finalized MIDI clip's; keys still held are reported with
   their length up to the current position. The count call refreshes the
   engine-side cache, so call it once per frame and then read the notes.
   Both return 0 / do nothing when that track is not being recorded. */
CADENCE_API int cadence_engine_recording_note_count(CadenceEngine* engine, CadenceTrackId track_id);
CADENCE_API void cadence_engine_recording_note_at(CadenceEngine* engine, CadenceTrackId track_id, int index,
                                      int* out_pitch, float* out_velocity,
                                      double* out_start, double* out_length);

/* Test hook: when enabled, record captures synthesize a tone instead of
   reading hardware inputs (deterministic pipeline verification). */
CADENCE_API void cadence_engine_set_debug_loopback(int enabled);

/* Metronome click on every beat (accent on bar start, 4/4 fixed). */
CADENCE_API void cadence_engine_set_metronome(CadenceEngine* engine, int enabled);

/* ---- tempo / time signature (project-wide) ---- */

CADENCE_API double cadence_engine_tempo_bpm(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_set_tempo_bpm(CadenceEngine* engine, double bpm);
CADENCE_API int cadence_engine_time_sig_numerator(CadenceEngine* engine);
CADENCE_API int cadence_engine_time_sig_denominator(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_set_time_sig(CadenceEngine* engine, int numerator,
                                          int denominator);

/* ---- undo / redo (engine-owned) ---- */

CADENCE_API int cadence_engine_can_undo(CadenceEngine* engine);
CADENCE_API int cadence_engine_can_redo(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_undo(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_redo(CadenceEngine* engine);

/* ---- persistence ---- */

/* Saving records no undo entry; loading replaces the document and
   clears the history. Source paths are stored relative to the project
   file when possible. */
CADENCE_API CadenceResult cadence_engine_save_project(CadenceEngine* engine, const char* utf8_path);
CADENCE_API CadenceResult cadence_engine_load_project(CadenceEngine* engine, const char* utf8_path);
CADENCE_API CadenceResult cadence_engine_import_midi_project(CadenceEngine* engine, const char* utf8_path);
CADENCE_API CadenceResult cadence_engine_import_audio_file(CadenceEngine* engine, const char* utf8_path);
// Native macOS open panel (blocks until picked/cancelled). Returns the
// chosen path (caller copies) or "" when cancelled.
CADENCE_API const char* cadence_engine_pick_open_file(CadenceEngine* engine,
                                          const char* utf8_start_dir,
                                          const char* utf8_filters);
CADENCE_API CadenceResult cadence_engine_import_stems_folder(CadenceEngine* engine, const char* utf8_path);

/* ---- sound-library preview ----
   Auditions one audio file on a dedicated channel, mixed over the transport
   and independent of the project (the project is never modified). Playing a
   new file replaces the previous preview; playback stops by itself at EOF. */
CADENCE_API CadenceResult cadence_engine_preview_sound(CadenceEngine* engine, const char* utf8_path);
CADENCE_API void cadence_engine_stop_preview(CadenceEngine* engine);
CADENCE_API int cadence_engine_is_previewing(CadenceEngine* engine);

/* Convenience (phase 1 API): a one-track project with the whole file. */
CADENCE_API CadenceResult cadence_engine_load_file(CadenceEngine* engine, const char* utf8_path);

/* ---- remote control (WebSocket + JSON, LAN) ----
   Starts a control server for the companion remote app. Pass port 0 for an
   ephemeral port and an empty/NULL token to let the engine generate a 6-digit
   pairing code. The token and the actual port can be read with the getters.
   The protocol is documented in docs/design/remote-control.md. */
CADENCE_API CadenceResult cadence_engine_remote_start(CadenceEngine* engine, int port,
                                          const char* utf8_token);
CADENCE_API void cadence_engine_remote_stop(CadenceEngine* engine);
CADENCE_API int cadence_engine_remote_running(CadenceEngine* engine);
CADENCE_API int cadence_engine_remote_port(CadenceEngine* engine);
CADENCE_API int cadence_engine_remote_client_count(CadenceEngine* engine);
/* Pairing code (6 digits) shown by the host UI. */
CADENCE_API const char* cadence_engine_remote_token(CadenceEngine* engine);
CADENCE_API const char* cadence_engine_remote_last_error(CadenceEngine* engine);
/* Drains queued remote commands on the calling thread. The macOS host calls
   this from its UI frame ticker; headless tests call it while waiting. Must
   run on the engine's message thread. Returns the number of commands run. */
CADENCE_API int cadence_engine_pump_remote(CadenceEngine* engine);
/* Monotonic document revision; changes on every edit/undo/load. */
CADENCE_API uint64_t cadence_engine_project_revision(CadenceEngine* engine);

/* ---- transport ---- */

CADENCE_API CadenceResult cadence_engine_play(CadenceEngine* engine);
CADENCE_API CadenceResult cadence_engine_pause(CadenceEngine* engine); /* keeps position */
CADENCE_API CadenceResult cadence_engine_stop(CadenceEngine* engine);   /* rewinds to 0 */
CADENCE_API CadenceResult cadence_engine_seek(CadenceEngine* engine, double seconds);
CADENCE_API CadenceResult cadence_engine_set_gain(CadenceEngine* engine, float linear_gain); /* master */

CADENCE_API int    cadence_engine_is_playing(CadenceEngine* engine);
CADENCE_API double cadence_engine_position_seconds(CadenceEngine* engine);
CADENCE_API double cadence_engine_duration_seconds(CadenceEngine* engine);
CADENCE_API double cadence_engine_sample_rate(CadenceEngine* engine);
CADENCE_API int    cadence_engine_buffer_size_samples(CadenceEngine* engine);
CADENCE_API double cadence_engine_master_gain(CadenceEngine* engine);

/* ---- offline render ---- */

/* Renders the project from position 0 to a 16-bit stereo WAV.
   max_seconds caps the length; pass a large value for the full project. */
CADENCE_API CadenceResult cadence_engine_render(CadenceEngine* engine,
                                    const char* utf8_out_path,
                                    double max_seconds);
/* Offline render with a chosen bit depth (16 or 24). MP3 needs an external
   LAME encoder and is planned separately. */
CADENCE_API CadenceResult cadence_engine_render_ex(CadenceEngine* engine,
                                       const char* utf8_out_path,
                                       double max_seconds,
                                       int bits_per_sample);

/* UTF-8 description of the last failed call on this engine.
   The pointer is valid until the next failing call on the same engine. */
CADENCE_API const char* cadence_engine_last_error(CadenceEngine* engine);

#ifdef __cplusplus
}
#endif

#endif /* CADENCE_ENGINE_H */
