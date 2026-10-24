// Dart FFI bindings for the Cadence engine C API.
//
// Mirrors engine/include/cadence/cadence_engine.h — keep in sync.
// Canonical location: ui/lib/engine/ (the copy under bindings/dart/ is the
// standalone reference; ui is the live consumer).
//
// Depends on package:ffi (Utf8 + malloc) — declared in pubspec.yaml.

import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

const String defaultDylibName = 'libcadence_engine.dylib';

/// Resolves the engine dylib: explicit arg > CADENCE_ENGINE_DYLIB env >
/// next to the app bundle executable / in Frameworks > the dev build output >
/// system lookup.
String resolveEngineDylib(String? explicitPath) {
  if (explicitPath != null && explicitPath.isNotEmpty) return explicitPath;
  final env = Platform.environment['CADENCE_ENGINE_DYLIB'];
  if (env != null && env.isNotEmpty) return env;

  final exeDir = File(Platform.resolvedExecutable).parent;
  final candidates = [
    '${exeDir.path}/libcadence_engine.dylib',
    '${exeDir.parent.path}/Frameworks/libcadence_engine.dylib',
    '/Users/ivan/daw/build/engine/libcadence_engine.dylib',
  ];
  for (final candidate in candidates) {
    if (File(candidate).existsSync()) return candidate;
  }
  return defaultDylibName;
}

/// Mirrors the CadenceResult enum in cadence_engine.h.
abstract final class CadenceResult {
  static const ok = 0;
  static const errorNullArgument = 1;
  static const errorNotInitialised = 2;
  static const errorLoadFailed = 3;
  static const errorAudioDevice = 4;
  static const errorInvalidArgument = 5;
  static const errorNotFound = 6;
  static const errorRenderFailed = 7;
}

/// High-level handle around one CadenceEngine instance.
final class CadenceEngine {
  /// [attachHandleAddress]: wraps an engine created in ANOTHER isolate of
  /// this process (sub-windows attach to the main window's engine — the
  /// dylib and its globals are process-wide).
  CadenceEngine({String? dylibPath, int? attachHandleAddress})
      : _lib = DynamicLibrary.open(resolveEngineDylib(dylibPath)),
        _ownsHandle = attachHandleAddress == null {

    _create = _lib.lookupFunction<Pointer<Void> Function(), Pointer<Void> Function()>(
        'cadence_engine_create');
    _destroy = _lib.lookupFunction<Void Function(Pointer<Void>),
        void Function(Pointer<Void>)>('cadence_engine_destroy');
    _initialise = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_initialise');
    _restartAudio = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_restart_audio');
    _initialiseAudioBg = _lib.lookupFunction<Void Function(Pointer<Void>),
        void Function(Pointer<Void>)>('cadence_engine_initialise_audio_background');
    _audioInitialised = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_audio_initialised');
    _audioDeviceRunning = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_audio_device_running');
    _audioXRunCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_audio_xrun_count');
    _audioRecoveryCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_audio_recovery_count');
    _lastAudioError = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>),
        Pointer<Utf8> Function(Pointer<Void>)>('cadence_engine_last_audio_error');
    _newProject = _lib.lookupFunction<Int32 Function(Pointer<Void>, Double),
        int Function(Pointer<Void>, double)>('cadence_engine_new_project');
    _addTrack = _lib.lookupFunction<
        Int64 Function(Pointer<Void>, Pointer<Utf8>, Int32, Int32),
        int Function(Pointer<Void>, Pointer<Utf8>, int, int)>('cadence_engine_add_track');
    _trackType = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_type');
    _selectMidiInput = _lib.lookupFunction<Void Function(Pointer<Void>, Int32),
        void Function(Pointer<Void>, int)>('cadence_engine_select_midi_input');
    _setMetronome = _lib.lookupFunction<Void Function(Pointer<Void>, Int32),
        void Function(Pointer<Void>, int)>('cadence_engine_set_metronome');
    _midiClipRemoveNote = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_midi_clip_remove_note');
    _midiClipSetNote = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32, Int32, Double, Double),
        int Function(Pointer<Void>, int, int, int, double, double)>(
        'cadence_engine_midi_clip_set_note');
    _midiClipSetNotes = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Pointer<Int32>, Pointer<Int32>,
            Pointer<Double>, Pointer<Double>, Int32),
        int Function(Pointer<Void>, int, Pointer<Int32>, Pointer<Int32>,
            Pointer<Double>, Pointer<Double>, int)>(
        'cadence_engine_midi_clip_set_notes');
    _selectedMidiInputIndex = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_selected_midi_input_index');
    _addClip = _lib.lookupFunction<
        Int64 Function(Pointer<Void>, Int64, Pointer<Utf8>, Double, Float),
        int Function(Pointer<Void>, int, Pointer<Utf8>, double, double)>(
        'cadence_engine_add_clip');
    _setTrackVolume = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Float),
        int Function(Pointer<Void>, int, double)>('cadence_engine_set_track_volume');
    _setTrackPan = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Float),
        int Function(Pointer<Void>, int, double)>('cadence_engine_set_track_pan');
    _setTrackMute = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_set_track_mute');
    _setTrackSolo = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_set_track_solo');
    _trackCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_track_count');
    _trackIdAt = _lib.lookupFunction<Int64 Function(Pointer<Void>, Int32),
        int Function(Pointer<Void>, int)>('cadence_engine_track_id_at');
    _trackName = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int64),
        Pointer<Utf8> Function(Pointer<Void>, int)>('cadence_engine_track_name');
    _trackColorIndex = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_color_index');
    _trackVolume = _lib.lookupFunction<Float Function(Pointer<Void>, Int64),
        double Function(Pointer<Void>, int)>('cadence_engine_track_volume');
    _trackPan = _lib.lookupFunction<Float Function(Pointer<Void>, Int64),
        double Function(Pointer<Void>, int)>('cadence_engine_track_pan');
    _trackMute = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_mute');
    _trackSolo = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_solo');
    _trackArmed = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_armed');
    _trackInputChannel = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_input_channel');
    _setTrackArmed = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_set_track_armed');
    _setTrackInputChannel = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_set_track_input_channel');
    _startRecord = _lib
        .lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
            'cadence_engine_start_record');
    _stopRecord = _lib
        .lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
            'cadence_engine_stop_record');
    _isRecording = _lib
        .lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
            'cadence_engine_is_recording');
    _recordStartSeconds = _lib.lookupFunction<Double Function(Pointer<Void>),
        double Function(Pointer<Void>)>('cadence_engine_record_start_seconds');
    _setDebugLoopback = _lib.lookupFunction<Void Function(Int32),
        void Function(int)>('cadence_engine_set_debug_loopback');
    _rescanPlugins = _lib.lookupFunction<Void Function(Pointer<Void>),
        void Function(Pointer<Void>)>('cadence_engine_rescan_plugins');
    _scanPluginsNow = _lib.lookupFunction<Void Function(Pointer<Void>),
        void Function(Pointer<Void>)>('cadence_engine_scan_plugins_now');
    _pluginScanActive = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_plugin_scan_active');
    _pluginCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_plugin_count');
    _pluginNameAt = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int)>('cadence_engine_plugin_name_at');
    _pluginUidAt = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int)>('cadence_engine_plugin_uid_at');
    _pluginIsInstrumentAt = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int32),
        int Function(Pointer<Void>, int)>('cadence_engine_plugin_is_instrument_at');
    _pluginFormatAt = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int)>('cadence_engine_plugin_format_at');
    _trackAddFx = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_track_add_fx');
    _trackRemoveFx = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_track_remove_fx');
    _trackSetFxBypass = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32, Int32),
        int Function(Pointer<Void>, int, int, int)>('cadence_engine_track_set_fx_bypass');
    _trackFxCount = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_fx_count');
    _trackFxNameAt = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int64, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int, int)>('cadence_engine_track_fx_name_at');
    _trackFxBypassAt = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_track_fx_bypass_at');
    _trackSetInstrumentPlugin = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_track_set_instrument_plugin');
    _trackClearInstrument = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_clear_instrument');
    _trackSetFxState = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32, Pointer<Uint8>, Int32,
            Pointer<Uint8>, Int32),
        int Function(Pointer<Void>, int, int, Pointer<Uint8>, int,
            Pointer<Uint8>, int)>('cadence_engine_track_set_fx_state');
    _trackSetInstrumentState = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Pointer<Uint8>, Int32,
            Pointer<Uint8>, Int32),
        int Function(Pointer<Void>, int, Pointer<Uint8>, int,
            Pointer<Uint8>, int)>('cadence_engine_track_set_instrument_state');
    _pluginStateBase64 = _lib.lookupFunction<
        Pointer<Utf8> Function(Pointer<Void>, Int64, Int32, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int, int, int)>(
        'cadence_engine_plugin_state_base64');
    _openPluginEditor = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_open_plugin_editor');
    _openInstrumentEditor = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_open_instrument_editor');
    _openInstrumentEditorWithClip = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int64),
        int Function(Pointer<Void>, int, int)>(
        'cadence_engine_open_instrument_editor_with_clip');
    _trackInstrumentName = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int64),
        Pointer<Utf8> Function(Pointer<Void>, int)>('cadence_engine_track_instrument_name');
    _trackSetMidifx = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_track_set_midifx');
    _trackClearMidifx = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_clear_midifx');
    _trackMidifxName = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int64),
        Pointer<Utf8> Function(Pointer<Void>, int)>('cadence_engine_track_midifx_name');
    _masterAddFx = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int32),
        int Function(Pointer<Void>, int)>('cadence_engine_master_add_fx');
    _masterRemoveFx = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int32),
        int Function(Pointer<Void>, int)>('cadence_engine_master_remove_fx');
    _masterSetFxBypass = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int32, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_master_set_fx_bypass');
    _masterFxCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_master_fx_count');
    _masterFxNameAt = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int)>('cadence_engine_master_fx_name_at');
    _masterFxBypassAt = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int32),
        int Function(Pointer<Void>, int)>('cadence_engine_master_fx_bypass_at');
    _masterOpenFxEditor = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int32),
        int Function(Pointer<Void>, int)>('cadence_engine_open_master_fx_editor');
    _currentOutputDeviceName = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>),
        Pointer<Utf8> Function(Pointer<Void>)>('cadence_engine_current_output_device_name');
    _currentInputDeviceName = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>),
        Pointer<Utf8> Function(Pointer<Void>)>('cadence_engine_current_input_device_name');
    _outputDeviceCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_output_device_count');
    _outputDeviceNameAt = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int)>('cadence_engine_output_device_name_at');
    _inputDeviceCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_input_device_count');
    _inputDeviceNameAt = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int)>('cadence_engine_input_device_name_at');
    _sampleRateCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_available_sample_rate_count');
    _sampleRateAt = _lib.lookupFunction<Double Function(Pointer<Void>, Int32),
        double Function(Pointer<Void>, int)>('cadence_engine_available_sample_rate_at');
    _bufferSizeCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_available_buffer_size_count');
    _bufferSizeAt = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int32),
        int Function(Pointer<Void>, int)>('cadence_engine_available_buffer_size_at');
    _setDeviceConfig = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, Double, Int32),
        int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, double, int)>(
        'cadence_engine_set_device_config');
    _midiInputCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_midi_input_count');
    _deviceInputChannelCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_device_input_channel_count');
    _midiInputNameAt = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int)>('cadence_engine_midi_input_name_at');
    _saveDeviceSettings = _lib.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>),
        int Function(Pointer<Void>, Pointer<Utf8>)>('cadence_engine_save_device_settings');
    _loadDeviceSettings = _lib.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>),
        int Function(Pointer<Void>, Pointer<Utf8>)>('cadence_engine_load_device_settings');
    _trackClipCount = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_clip_count');
    _clipIdAt = _lib.lookupFunction<Int64 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_clip_id_at');
    _clipStart = _lib.lookupFunction<Double Function(Pointer<Void>, Int64),
        double Function(Pointer<Void>, int)>('cadence_engine_clip_start');
    _clipLength = _lib.lookupFunction<Double Function(Pointer<Void>, Int64),
        double Function(Pointer<Void>, int)>('cadence_engine_clip_length');
    _clipLengthMax = _lib.lookupFunction<Double Function(Pointer<Void>, Int64),
        double Function(Pointer<Void>, int)>('cadence_engine_clip_length_max');
    _clipOffset = _lib.lookupFunction<Double Function(Pointer<Void>, Int64),
        double Function(Pointer<Void>, int)>('cadence_engine_clip_offset');
    _setClipBounds = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Double, Double, Double),
        int Function(Pointer<Void>, int, double, double, double)>(
        'cadence_engine_set_clip_bounds');
    _setClipFades = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Double, Double),
        int Function(Pointer<Void>, int, double, double)>('cadence_engine_set_clip_fades');
    _setTrackName = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Pointer<Utf8>),
        int Function(Pointer<Void>, int, Pointer<Utf8>)>('cadence_engine_set_track_name');
    _setTrackColor = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_set_track_color');
    _clipFadeIn = _lib.lookupFunction<Double Function(Pointer<Void>, Int64),
        double Function(Pointer<Void>, int)>('cadence_engine_clip_fade_in');
    _clipFadeOut = _lib.lookupFunction<Double Function(Pointer<Void>, Int64),
        double Function(Pointer<Void>, int)>('cadence_engine_clip_fade_out');
    _addMidiClip = _lib.lookupFunction<Int64 Function(Pointer<Void>, Int64, Double, Double),
        int Function(Pointer<Void>, int, double, double)>('cadence_engine_add_midi_clip');
    _midiClipAddNote = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32, Float, Double, Double),
        int Function(Pointer<Void>, int, int, double, double, double)>(
        'cadence_engine_midi_clip_add_note');
    _clipIsMidi = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_clip_is_midi');
    _clipNoteCount = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_clip_note_count');
    _clipNoteAt = _lib.lookupFunction<
        Void Function(Pointer<Void>, Int64, Int32, Pointer<Int32>, Pointer<Float>,
            Pointer<Double>, Pointer<Double>),
        void Function(Pointer<Void>, int, int, Pointer<Int32>, Pointer<Float>,
            Pointer<Double>, Pointer<Double>)>('cadence_engine_clip_note_at');
    _clipNoteOrigStartAt = _lib.lookupFunction<Double Function(Pointer<Void>, Int64, Int32),
        double Function(Pointer<Void>, int, int)>('cadence_engine_clip_note_orig_start_at');
    _recordingNoteCount = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_recording_note_count');
    _recordingNoteAt = _lib.lookupFunction<
        Void Function(Pointer<Void>, Int64, Int32, Pointer<Int32>, Pointer<Float>,
            Pointer<Double>, Pointer<Double>),
        void Function(Pointer<Void>, int, int, Pointer<Int32>, Pointer<Float>,
            Pointer<Double>, Pointer<Double>)>('cadence_engine_recording_note_at');
    _tempoBpm = _lib.lookupFunction<Double Function(Pointer<Void>),
        double Function(Pointer<Void>)>('cadence_engine_tempo_bpm');
    _setTempoBpm = _lib.lookupFunction<Int32 Function(Pointer<Void>, Double),
        int Function(Pointer<Void>, double)>('cadence_engine_set_tempo_bpm');
    _timeSigNum = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_time_sig_numerator');
    _timeSigDen = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_time_sig_denominator');
    _setTimeSig = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int32, Int32),
        int Function(Pointer<Void>, int, int)>('cadence_engine_set_time_sig');
    _setTrackVolumeAutomation = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Pointer<Double>, Pointer<Double>, Int32),
        int Function(Pointer<Void>, int, Pointer<Double>, Pointer<Double>,
            int)>('cadence_engine_set_track_volume_automation');
    _trackVolumeAutoCount = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_volume_auto_count');
    _setTrackLaneAutomation = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32, Pointer<Double>,
            Pointer<Double>, Int32),
        int Function(Pointer<Void>, int, int, Pointer<Double>, Pointer<Double>,
            int)>('cadence_engine_set_track_lane_automation');
    _trackLaneAutoCount = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int,
            int)>('cadence_engine_track_lane_auto_count');
    _setTrackParamAutomation = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32, Int32, Int32, Pointer<Utf8>,
            Pointer<Double>, Pointer<Double>, Int32),
        int Function(Pointer<Void>, int, int, int, int, Pointer<Utf8>,
            Pointer<Double>, Pointer<Double>,
            int)>('cadence_engine_set_track_param_automation');
    _trackParamLaneCount = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_track_param_lane_count');
    _trackParamLaneInfo = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32, Pointer<Int32>, Pointer<Int32>,
            Pointer<Int32>),
        int Function(Pointer<Void>, int, int, Pointer<Int32>, Pointer<Int32>,
            Pointer<Int32>)>('cadence_engine_track_param_lane_info');
    _trackParamLaneName = _lib.lookupFunction<
        Pointer<Utf8> Function(Pointer<Void>, Int64, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int,
            int)>('cadence_engine_track_param_lane_name');
    _trackParamLanePointCount = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int,
            int)>('cadence_engine_track_param_lane_point_count');
    _trackParamLanePointAt = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32, Int32, Pointer<Double>,
            Pointer<Double>),
        int Function(Pointer<Void>, int, int, int, Pointer<Double>,
            Pointer<Double>)>('cadence_engine_track_param_lane_point_at');
    _removeTrackParamLane = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32),
        int Function(Pointer<Void>, int,
            int)>('cadence_engine_remove_track_param_lane');
    _pluginParamCount = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32, Int32),
        int Function(Pointer<Void>, int, int,
            int)>('cadence_engine_plugin_param_count');
    _pluginParamName = _lib.lookupFunction<
        Pointer<Utf8> Function(Pointer<Void>, Int64, Int32, Int32, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int, int, int,
            int)>('cadence_engine_plugin_param_name');
    _trackLaneAutoAt = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32, Int32, Pointer<Double>,
            Pointer<Double>),
        int Function(Pointer<Void>, int, int, int, Pointer<Double>,
            Pointer<Double>)>('cadence_engine_track_lane_auto_at');
    _setTrackVolumeAutoPointAt = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Double, Double),
        int Function(Pointer<Void>, int, double,
            double)>('cadence_engine_set_track_volume_auto_point_at');
    _inputQuantize = _lib.lookupFunction<Double Function(Pointer<Void>),
        double Function(Pointer<Void>)>('cadence_engine_input_quantize');
    _setInputQuantize = _lib.lookupFunction<Int32 Function(Pointer<Void>, Double),
        int Function(Pointer<Void>, double)>('cadence_engine_set_input_quantize');
    _midiInputLatency = _lib.lookupFunction<Double Function(Pointer<Void>),
        double Function(Pointer<Void>)>('cadence_engine_midi_input_latency');
    _setMidiInputLatency = _lib.lookupFunction<Int32 Function(Pointer<Void>, Double),
        int Function(Pointer<Void>, double)>('cadence_engine_set_midi_input_latency');
    _copyClip = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_copy_clip');
    _hasPendingPluginJobs = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_has_pending_plugin_jobs');
    _pumpOnePluginJob = _lib.lookupFunction<Void Function(Pointer<Void>),
        void Function(Pointer<Void>)>('cadence_engine_pump_one_plugin_job');
    _pasteClip = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Double),
        int Function(Pointer<Void>, int, double)>('cadence_engine_paste_clip');
    _moveClipToTrack = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int64, Double),
        int Function(Pointer<Void>, int, int,
            double)>('cadence_engine_move_clip_to_track');
    _splitClip = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Double),
        int Function(Pointer<Void>, int,
            double)>('cadence_engine_split_clip');
    _trackVolumeEffectiveAt = _lib.lookupFunction<
        Float Function(Pointer<Void>, Int64, Double),
        double Function(Pointer<Void>, int,
            double)>('cadence_engine_track_volume_effective_at');
    _trackVolumeAutoAt = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int32, Pointer<Double>, Pointer<Double>),
        int Function(Pointer<Void>, int, int, Pointer<Double>,
            Pointer<Double>)>('cadence_engine_track_volume_auto_at');
    _quantizeClipNotes = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Double, Double, Double, Double, Int32,
            Int32, Double),
        int Function(Pointer<Void>, int, double, double, double, double, int, int,
            double)>('cadence_engine_quantize_clip_notes');
    _resetClipQuantize = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_reset_clip_quantize');
    _freezeClipQuantize = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_freeze_clip_quantize');
    _meterTrackPeak = _lib.lookupFunction<Float Function(Pointer<Void>, Int32),
        double Function(Pointer<Void>, int)>('cadence_engine_meter_track_peak');
    _meterMasterPeak = _lib.lookupFunction<Float Function(Pointer<Void>, Int32),
        double Function(Pointer<Void>, int)>('cadence_engine_meter_master_peak');
    _masterGain = _lib.lookupFunction<Double Function(Pointer<Void>),
        double Function(Pointer<Void>)>('cadence_engine_master_gain');
    _bufferSizeSamples = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_buffer_size_samples');
    _clipPeaks = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Double, Double, Int32,
            Pointer<Float>, Pointer<Float>),
        int Function(Pointer<Void>, int, double, double, int,
            Pointer<Float>, Pointer<Float>)>('cadence_engine_clip_peaks');
    _removeTrack = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_remove_track');
    _removeClip = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64),
        int Function(Pointer<Void>, int)>('cadence_engine_remove_clip');
    _setClipStart = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Double),
        int Function(Pointer<Void>, int, double)>('cadence_engine_set_clip_start');
    _setClipGain = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int64, Float),
        int Function(Pointer<Void>, int, double)>('cadence_engine_set_clip_gain');
    _canUndo = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_can_undo');
    _canRedo = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_can_redo');
    _undo = _lib
        .lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
            'cadence_engine_undo');
    _redo = _lib
        .lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
            'cadence_engine_redo');
    _saveProject = _lib.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>),
        int Function(Pointer<Void>, Pointer<Utf8>)>('cadence_engine_save_project');
    _loadProject = _lib.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>),
        int Function(Pointer<Void>, Pointer<Utf8>)>('cadence_engine_load_project');
    _importMidiProject = _lib.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>),
        int Function(Pointer<Void>, Pointer<Utf8>)>('cadence_engine_import_midi_project');
    _importStemsFolder = _lib.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>),
        int Function(Pointer<Void>, Pointer<Utf8>)>('cadence_engine_import_stems_folder');
    _importAudioFile = _lib.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>),
        int Function(Pointer<Void>, Pointer<Utf8>)>('cadence_engine_import_audio_file');
    _previewSound = _lib.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>),
        int Function(Pointer<Void>, Pointer<Utf8>)>('cadence_engine_preview_sound');
    _stopPreview = _lib.lookupFunction<Void Function(Pointer<Void>),
        void Function(Pointer<Void>)>('cadence_engine_stop_preview');
    _isPreviewing = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_is_previewing');
    _pickOpenFile = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>),
        Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>)>('cadence_engine_pick_open_file');
    _loadFile = _lib.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>),
        int Function(Pointer<Void>, Pointer<Utf8>)>('cadence_engine_load_file');
    _play = _lib
        .lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
            'cadence_engine_play');
    _pause = _lib
        .lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
            'cadence_engine_pause');
    _stop = _lib
        .lookupFunction<Int32 Function(Pointer<Void>), int Function(Pointer<Void>)>(
            'cadence_engine_stop');
    _seek = _lib.lookupFunction<Int32 Function(Pointer<Void>, Double),
        int Function(Pointer<Void>, double)>('cadence_engine_seek');
    _setGain = _lib.lookupFunction<Int32 Function(Pointer<Void>, Float),
        int Function(Pointer<Void>, double)>('cadence_engine_set_gain');
    _isPlaying = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_is_playing');
    _positionSeconds = _lib.lookupFunction<Double Function(Pointer<Void>),
        double Function(Pointer<Void>)>('cadence_engine_position_seconds');
    _durationSeconds = _lib.lookupFunction<Double Function(Pointer<Void>),
        double Function(Pointer<Void>)>('cadence_engine_duration_seconds');
    _sampleRate = _lib.lookupFunction<Double Function(Pointer<Void>),
        double Function(Pointer<Void>)>('cadence_engine_sample_rate');
    _render = _lib.lookupFunction<Int32 Function(Pointer<Void>, Pointer<Utf8>, Double),
        int Function(Pointer<Void>, Pointer<Utf8>, double)>('cadence_engine_render');
    _renderEx = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Utf8>, Double, Int32),
        int Function(Pointer<Void>, Pointer<Utf8>, double,
            int)>('cadence_engine_render_ex');
    _lastError = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>),
        Pointer<Utf8> Function(Pointer<Void>)>('cadence_engine_last_error');

    // ---- AI clip generation ----
    _setAiModelDir = _lib.lookupFunction<Void Function(Pointer<Void>, Pointer<Utf8>),
        void Function(Pointer<Void>, Pointer<Utf8>)>(
        'cadence_engine_set_ai_model_dir');
    _aiModelDir = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>),
        Pointer<Utf8> Function(Pointer<Void>)>('cadence_engine_ai_model_dir');
    _aiModelAvailable = _lib.lookupFunction<Int32 Function(Pointer<Void>, Int32),
        int Function(Pointer<Void>, int)>('cadence_engine_ai_model_available');
    _startAiGeneration = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Int64, Int64, Double, Int32, Int32,
            Double, Double, Int32, Int32, Pointer<Utf8>, Int32, Int32, Int32),
        int Function(Pointer<Void>, int, int, double, int, int, double, double,
            int, int, Pointer<Utf8>, int, int, int)>(
        'cadence_engine_start_ai_generation_long');
    _aiGenerationState = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_ai_generation_state');
    _cancelAiGeneration = _lib.lookupFunction<Void Function(Pointer<Void>),
        void Function(Pointer<Void>)>('cadence_engine_cancel_ai_generation');
    _clearAiGeneration = _lib.lookupFunction<Void Function(Pointer<Void>),
        void Function(Pointer<Void>)>('cadence_engine_clear_ai_generation');
    _aiResultOk = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_ai_result_ok');
    _aiResultString = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>, Int32),
        Pointer<Utf8> Function(Pointer<Void>, int)>(
        'cadence_engine_ai_result_string');
    _aiResultSeed = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_ai_result_seed');
    _aiResultClipLength = _lib.lookupFunction<Double Function(Pointer<Void>),
        double Function(Pointer<Void>)>('cadence_engine_ai_result_clip_length');
    _aiResultStart = _lib.lookupFunction<Double Function(Pointer<Void>),
        double Function(Pointer<Void>)>('cadence_engine_ai_result_start');
    _aiResultClipId = _lib.lookupFunction<Int64 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_ai_result_clip_id');
    _aiResultTrackId = _lib.lookupFunction<Int64 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_ai_result_track_id');
    _aiResultNoteCount = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_ai_result_note_count');
    _aiResultNoteAt = _lib.lookupFunction<
        Void Function(Pointer<Void>, Int32, Pointer<Int32>, Pointer<Float>,
            Pointer<Double>, Pointer<Double>),
        void Function(Pointer<Void>, int, Pointer<Int32>, Pointer<Float>,
            Pointer<Double>, Pointer<Double>)>('cadence_engine_ai_result_note_at');
    _applyAiGeneration = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Int64>),
        int Function(Pointer<Void>, Pointer<Int64>)>(
        'cadence_engine_apply_ai_generation');

    _handle = attachHandleAddress != null
        ? Pointer<Void>.fromAddress(attachHandleAddress)
        : _create();
  }

  final DynamicLibrary _lib;
  final bool _ownsHandle;
  bool _disposed = false;
  late final Pointer<Void> _handle;

  /// Address of the wrapped engine (sub-windows attach to it).
  int get handleAddress => _handle.address;

  late final Pointer<Void> Function() _create;
  late final void Function(Pointer<Void>) _destroy;
  late final int Function(Pointer<Void>) _initialise;
  late final int Function(Pointer<Void>) _restartAudio;
  late final void Function(Pointer<Void>) _initialiseAudioBg;
  late final int Function(Pointer<Void>) _audioInitialised;
  late final int Function(Pointer<Void>) _audioDeviceRunning;
  late final int Function(Pointer<Void>) _audioXRunCount;
  late final int Function(Pointer<Void>) _audioRecoveryCount;
  late final Pointer<Utf8> Function(Pointer<Void>) _lastAudioError;
  late final int Function(Pointer<Void>, double) _newProject;
  late final int Function(Pointer<Void>, Pointer<Utf8>, int, int) _addTrack;
  late final int Function(Pointer<Void>, int) _trackType;
  late final void Function(Pointer<Void>, int) _selectMidiInput;
  late final void Function(Pointer<Void>, int) _setMetronome;
  late final void Function(Pointer<Void>) _rescanPlugins;
  late final void Function(Pointer<Void>) _scanPluginsNow;
  late final int Function(Pointer<Void>) _pluginScanActive;
  late final int Function(Pointer<Void>) _pluginCount;
  late final Pointer<Utf8> Function(Pointer<Void>, int) _pluginNameAt;
  late final Pointer<Utf8> Function(Pointer<Void>, int) _pluginUidAt;
  late final int Function(Pointer<Void>, int) _pluginIsInstrumentAt;
  late final Pointer<Utf8> Function(Pointer<Void>, int) _pluginFormatAt;
  late final int Function(Pointer<Void>, int, int) _trackAddFx;
  late final int Function(Pointer<Void>, int, int) _trackRemoveFx;
  late final int Function(Pointer<Void>, int, int, int) _trackSetFxBypass;
  late final int Function(Pointer<Void>, int) _trackFxCount;
  late final Pointer<Utf8> Function(Pointer<Void>, int, int) _trackFxNameAt;
  late final int Function(Pointer<Void>, int, int) _trackFxBypassAt;
  late final int Function(Pointer<Void>, int, int) _trackSetInstrumentPlugin;
  late final int Function(Pointer<Void>, int) _trackClearInstrument;
  late final int Function(Pointer<Void>, int, int, Pointer<Uint8>, int,
      Pointer<Uint8>, int) _trackSetFxState;
  late final int Function(Pointer<Void>, int, Pointer<Uint8>, int,
      Pointer<Uint8>, int) _trackSetInstrumentState;
  late final Pointer<Utf8> Function(Pointer<Void>, int, int, int)
      _pluginStateBase64;
  late final int Function(Pointer<Void>, int, int) _openPluginEditor;
  late final int Function(Pointer<Void>, int) _openInstrumentEditor;
  late final int Function(Pointer<Void>, int, int) _openInstrumentEditorWithClip;
  late final Pointer<Utf8> Function(Pointer<Void>, int) _trackInstrumentName;
  late final int Function(Pointer<Void>, int, int) _trackSetMidifx;
  late final int Function(Pointer<Void>, int) _trackClearMidifx;
  late final Pointer<Utf8> Function(Pointer<Void>, int) _trackMidifxName;
  late final int Function(Pointer<Void>, int) _masterAddFx;
  late final int Function(Pointer<Void>, int) _masterRemoveFx;
  late final int Function(Pointer<Void>, int, int) _masterSetFxBypass;
  late final int Function(Pointer<Void>) _masterFxCount;
  late final Pointer<Utf8> Function(Pointer<Void>, int) _masterFxNameAt;
  late final int Function(Pointer<Void>, int) _masterFxBypassAt;
  late final int Function(Pointer<Void>, int) _masterOpenFxEditor;
  late final int Function(Pointer<Void>, int, int) _midiClipRemoveNote;
  late final int Function(Pointer<Void>, int, int, int, double, double) _midiClipSetNote;
  late final int Function(Pointer<Void>, int, Pointer<Int32>, Pointer<Int32>,
      Pointer<Double>, Pointer<Double>, int) _midiClipSetNotes;
  late final int Function(Pointer<Void>) _selectedMidiInputIndex;
  late final int Function(Pointer<Void>, int, Pointer<Utf8>, double, double) _addClip;
  late final int Function(Pointer<Void>, int, double) _setTrackVolume;
  late final int Function(Pointer<Void>, int, double) _setTrackPan;
  late final int Function(Pointer<Void>, int, int) _setTrackMute;
  late final int Function(Pointer<Void>, int, int) _setTrackSolo;
  late final int Function(Pointer<Void>) _trackCount;
  late final int Function(Pointer<Void>, int) _trackIdAt;
  late final Pointer<Utf8> Function(Pointer<Void>, int) _trackName;
  late final int Function(Pointer<Void>, int) _trackColorIndex;
  late final double Function(Pointer<Void>, int) _trackVolume;
  late final double Function(Pointer<Void>, int) _trackPan;
  late final int Function(Pointer<Void>, int) _trackMute;
  late final int Function(Pointer<Void>, int) _trackSolo;
  late final int Function(Pointer<Void>, int) _trackArmed;
  late final int Function(Pointer<Void>, int) _trackInputChannel;
  late final int Function(Pointer<Void>, int, int) _setTrackArmed;
  late final int Function(Pointer<Void>, int, int) _setTrackInputChannel;
  late final int Function(Pointer<Void>) _startRecord;
  late final int Function(Pointer<Void>) _stopRecord;
  late final int Function(Pointer<Void>) _isRecording;
  late final double Function(Pointer<Void>) _recordStartSeconds;
  late final void Function(int) _setDebugLoopback;
  late final Pointer<Utf8> Function(Pointer<Void>) _currentOutputDeviceName;
  late final Pointer<Utf8> Function(Pointer<Void>) _currentInputDeviceName;
  late final int Function(Pointer<Void>) _outputDeviceCount;
  late final Pointer<Utf8> Function(Pointer<Void>, int) _outputDeviceNameAt;
  late final int Function(Pointer<Void>) _inputDeviceCount;
  late final Pointer<Utf8> Function(Pointer<Void>, int) _inputDeviceNameAt;
  late final int Function(Pointer<Void>) _sampleRateCount;
  late final double Function(Pointer<Void>, int) _sampleRateAt;
  late final int Function(Pointer<Void>) _bufferSizeCount;
  late final int Function(Pointer<Void>, int) _bufferSizeAt;
  late final int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, double, int) _setDeviceConfig;
  late final int Function(Pointer<Void>) _midiInputCount;
  late final int Function(Pointer<Void>) _deviceInputChannelCount;
  late final Pointer<Utf8> Function(Pointer<Void>, int) _midiInputNameAt;
  late final int Function(Pointer<Void>, Pointer<Utf8>) _saveDeviceSettings;
  late final int Function(Pointer<Void>, Pointer<Utf8>) _loadDeviceSettings;
  late final int Function(Pointer<Void>, int) _trackClipCount;
  late final int Function(Pointer<Void>, int, int) _clipIdAt;
  late final double Function(Pointer<Void>, int) _clipStart;
  late final double Function(Pointer<Void>, int) _clipLength;
  late final double Function(Pointer<Void>, int) _clipLengthMax;
  late final double Function(Pointer<Void>, int) _clipOffset;
  late final int Function(Pointer<Void>, int, double, double, double) _setClipBounds;
  late final int Function(Pointer<Void>, int, double, double) _setClipFades;
  late final int Function(Pointer<Void>, int, Pointer<Utf8>) _setTrackName;
  late final int Function(Pointer<Void>, int, int) _setTrackColor;
  late final double Function(Pointer<Void>, int) _clipFadeIn;
  late final double Function(Pointer<Void>, int) _clipFadeOut;
  late final int Function(Pointer<Void>, int, double, double) _addMidiClip;
  late final int Function(Pointer<Void>, int, int, double, double, double) _midiClipAddNote;
  late final int Function(Pointer<Void>, int) _clipIsMidi;
  late final int Function(Pointer<Void>, int) _clipNoteCount;
  late final void Function(Pointer<Void>, int, int, Pointer<Int32>, Pointer<Float>,
      Pointer<Double>, Pointer<Double>) _clipNoteAt;
  late final double Function(Pointer<Void>) _tempoBpm;
  late final int Function(Pointer<Void>, double) _setTempoBpm;
  late final int Function(Pointer<Void>) _timeSigNum;
  late final int Function(Pointer<Void>) _timeSigDen;
  late final int Function(Pointer<Void>, int, int) _setTimeSig;
  late final double Function(Pointer<Void>, int, int) _clipNoteOrigStartAt;
  late final int Function(Pointer<Void>, int) _recordingNoteCount;
  late final void Function(Pointer<Void>, int, int, Pointer<Int32>, Pointer<Float>,
      Pointer<Double>, Pointer<Double>) _recordingNoteAt;
  late final int Function(Pointer<Void>, int, Pointer<Double>, Pointer<Double>, int)
      _setTrackVolumeAutomation;
  late final int Function(Pointer<Void>, int) _trackVolumeAutoCount;
  late final int Function(Pointer<Void>, int, double, double)
      _setTrackVolumeAutoPointAt;
  late final double Function(Pointer<Void>) _inputQuantize;
  late final int Function(Pointer<Void>, double) _setInputQuantize;
  late final double Function(Pointer<Void>) _midiInputLatency;
  late final int Function(Pointer<Void>, double) _setMidiInputLatency;
  late final int Function(Pointer<Void>, int) _copyClip;
  late final int Function(Pointer<Void>) _hasPendingPluginJobs;
  late final void Function(Pointer<Void>) _pumpOnePluginJob;
  late final int Function(Pointer<Void>, int, double) _pasteClip;
  late final int Function(Pointer<Void>, int, int, double) _moveClipToTrack;
  late final int Function(Pointer<Void>, int, double) _splitClip;
  late final double Function(Pointer<Void>, int, double)
      _trackVolumeEffectiveAt;
  late final int Function(Pointer<Void>, int, int, Pointer<Double>, Pointer<Double>, int)
      _setTrackLaneAutomation;
  late final int Function(Pointer<Void>, int, int) _trackLaneAutoCount;
  late final int Function(Pointer<Void>, int, int, int, Pointer<Double>,
      Pointer<Double>) _trackLaneAutoAt;
  late final int Function(Pointer<Void>, int, int, int, int, Pointer<Utf8>,
      Pointer<Double>, Pointer<Double>, int) _setTrackParamAutomation;
  late final int Function(Pointer<Void>, int) _trackParamLaneCount;
  late final int Function(Pointer<Void>, int, int, Pointer<Int32>, Pointer<Int32>,
      Pointer<Int32>) _trackParamLaneInfo;
  late final Pointer<Utf8> Function(Pointer<Void>, int, int) _trackParamLaneName;
  late final int Function(Pointer<Void>, int, int) _trackParamLanePointCount;
  late final int Function(Pointer<Void>, int, int, int, Pointer<Double>,
      Pointer<Double>) _trackParamLanePointAt;
  late final int Function(Pointer<Void>, int, int) _removeTrackParamLane;
  late final int Function(Pointer<Void>, int, int, int) _pluginParamCount;
  late final Pointer<Utf8> Function(Pointer<Void>, int, int, int, int)
      _pluginParamName;

  late final int Function(Pointer<Void>, int, int, Pointer<Double>, Pointer<Double>)
      _trackVolumeAutoAt;
  late final int Function(Pointer<Void>, int, double, double, double, double, int,
      int, double) _quantizeClipNotes;
  late final int Function(Pointer<Void>, int) _resetClipQuantize;
  late final int Function(Pointer<Void>, int) _freezeClipQuantize;
  late final double Function(Pointer<Void>, int) _meterTrackPeak;
  late final double Function(Pointer<Void>, int) _meterMasterPeak;
  late final double Function(Pointer<Void>) _masterGain;
  late final int Function(Pointer<Void>) _bufferSizeSamples;
  late final int Function(Pointer<Void>, int, double, double, int,
      Pointer<Float>, Pointer<Float>) _clipPeaks;
  late final int Function(Pointer<Void>, int) _removeTrack;
  late final int Function(Pointer<Void>, int) _removeClip;
  late final int Function(Pointer<Void>, int, double) _setClipStart;
  late final int Function(Pointer<Void>, int, double) _setClipGain;
  late final int Function(Pointer<Void>) _canUndo;
  late final int Function(Pointer<Void>) _canRedo;
  late final int Function(Pointer<Void>) _undo;
  late final int Function(Pointer<Void>) _redo;
  late final int Function(Pointer<Void>, Pointer<Utf8>) _saveProject;
  late final int Function(Pointer<Void>, Pointer<Utf8>) _loadProject;
  late final int Function(Pointer<Void>, Pointer<Utf8>) _importMidiProject;
  late final int Function(Pointer<Void>, Pointer<Utf8>) _importStemsFolder;
  late final int Function(Pointer<Void>, Pointer<Utf8>) _importAudioFile;
  late final int Function(Pointer<Void>, Pointer<Utf8>) _previewSound;
  late final void Function(Pointer<Void>) _stopPreview;
  late final int Function(Pointer<Void>) _isPreviewing;
  late final Pointer<Utf8> Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>) _pickOpenFile;
  late final int Function(Pointer<Void>, Pointer<Utf8>) _loadFile;
  late final int Function(Pointer<Void>) _play;
  late final int Function(Pointer<Void>) _pause;
  late final int Function(Pointer<Void>) _stop;
  late final int Function(Pointer<Void>, double) _seek;
  late final int Function(Pointer<Void>, double) _setGain;
  late final int Function(Pointer<Void>) _isPlaying;
  late final double Function(Pointer<Void>) _positionSeconds;
  late final double Function(Pointer<Void>) _durationSeconds;
  late final double Function(Pointer<Void>) _sampleRate;
  late final int Function(Pointer<Void>, Pointer<Utf8>, double) _render;
  late final int Function(Pointer<Void>, Pointer<Utf8>, double, int) _renderEx;
  late final Pointer<Utf8> Function(Pointer<Void>) _lastError;

  // ---- AI clip generation ----
  late final void Function(Pointer<Void>, Pointer<Utf8>) _setAiModelDir;
  late final Pointer<Utf8> Function(Pointer<Void>) _aiModelDir;
  late final int Function(Pointer<Void>, int) _aiModelAvailable;
  late final int Function(Pointer<Void>, int, int, double, int, int, double,
      double, int, int, Pointer<Utf8>, int, int, int) _startAiGeneration;
  late final int Function(Pointer<Void>) _aiGenerationState;
  late final void Function(Pointer<Void>) _cancelAiGeneration;
  late final void Function(Pointer<Void>) _clearAiGeneration;
  late final int Function(Pointer<Void>) _aiResultOk;
  late final Pointer<Utf8> Function(Pointer<Void>, int) _aiResultString;
  late final int Function(Pointer<Void>) _aiResultSeed;
  late final double Function(Pointer<Void>) _aiResultClipLength;
  late final double Function(Pointer<Void>) _aiResultStart;
  late final int Function(Pointer<Void>) _aiResultClipId;
  late final int Function(Pointer<Void>) _aiResultTrackId;
  late final int Function(Pointer<Void>) _aiResultNoteCount;
  late final void Function(Pointer<Void>, int, Pointer<Int32>, Pointer<Float>,
      Pointer<Double>, Pointer<Double>) _aiResultNoteAt;
  late final int Function(Pointer<Void>, Pointer<Int64>) _applyAiGeneration;

  String get lastError => _lastError(_handle).toDartString();

  int initialise() => _initialise(_handle);

  // ---- project document ----

  int newProject(double sampleRate) => _newProject(_handle, sampleRate);

  int addTrack(String name, int colorIndex, {int type = 0}) =>
      _withUtf8(name, (p) => _addTrack(_handle, p, colorIndex, type));
  int trackType(int trackId) => _trackType(_handle, trackId);

  int addClip(int trackId, String path, double startSeconds, double gain) =>
      _withUtf8(path, (p) => _addClip(_handle, trackId, p, startSeconds, gain));

  int setTrackVolume(int trackId, double linearGain) =>
      _setTrackVolume(_handle, trackId, linearGain);
  int setTrackPan(int trackId, double pan) => _setTrackPan(_handle, trackId, pan);
  int setTrackMute(int trackId, bool muted) =>
      _setTrackMute(_handle, trackId, muted ? 1 : 0);
  int setTrackSolo(int trackId, bool soloed) =>
      _setTrackSolo(_handle, trackId, soloed ? 1 : 0);

  // Edits: each records one undo entry; a failed lookup records none.
  int removeTrack(int trackId) => _removeTrack(_handle, trackId);
  int removeClip(int clipId) => _removeClip(_handle, clipId);
  int setClipStart(int clipId, double seconds) =>
      _setClipStart(_handle, clipId, seconds);
  int setClipGain(int clipId, double gain) => _setClipGain(_handle, clipId, gain);

  /// Trim/move in one command (one undo entry). Values are clamped engine-side.
  int setClipBounds(int clipId, double startSeconds, double sourceOffsetSeconds,
          double lengthSeconds) =>
      _setClipBounds(_handle, clipId, startSeconds, sourceOffsetSeconds, lengthSeconds);

  /// Linear fades in seconds; clamped to [0, clip length] engine-side.
  int setClipFades(int clipId, double fadeInSeconds, double fadeOutSeconds) =>
      _setClipFades(_handle, clipId, fadeInSeconds, fadeOutSeconds);

  // ---- MIDI clips ----

  int addMidiClip(int trackId, double startSeconds, double lengthSeconds) =>
      _addMidiClip(_handle, trackId, startSeconds, lengthSeconds);
  int midiClipAddNote(int clipId, int pitch, double velocity, double startSeconds,
          double lengthSeconds) =>
      _midiClipAddNote(_handle, clipId, pitch, velocity, startSeconds, lengthSeconds);
  int midiClipRemoveNote(int clipId, int noteIndex) =>
      _midiClipRemoveNote(_handle, clipId, noteIndex);
  int midiClipSetNote(int clipId, int noteIndex, int pitch, double startSeconds,
          double lengthSeconds) =>
      _midiClipSetNote(_handle, clipId, noteIndex, pitch, startSeconds, lengthSeconds);

  /// Applies several note edits as one undoable engine command.
  bool midiClipSetNotes(int clipId, List<int> noteIndices, List<int> pitches,
      List<double> startSeconds, List<double> lengthSeconds) {
    assert(noteIndices.length == pitches.length &&
        noteIndices.length == startSeconds.length &&
        noteIndices.length == lengthSeconds.length);
    final indicesPtr = calloc<Int32>(noteIndices.length);
    final pitchesPtr = calloc<Int32>(pitches.length);
    final startsPtr = calloc<Double>(startSeconds.length);
    final lengthsPtr = calloc<Double>(lengthSeconds.length);
    try {
      for (var i = 0; i < noteIndices.length; i++) {
        indicesPtr[i] = noteIndices[i];
        pitchesPtr[i] = pitches[i];
        startsPtr[i] = startSeconds[i];
        lengthsPtr[i] = lengthSeconds[i];
      }
      return _midiClipSetNotes(_handle, clipId, indicesPtr, pitchesPtr,
              startsPtr, lengthsPtr, noteIndices.length) ==
          0;
    } finally {
      calloc.free(indicesPtr);
      calloc.free(pitchesPtr);
      calloc.free(startsPtr);
      calloc.free(lengthsPtr);
    }
  }

  bool clipIsMidi(int clipId) => _clipIsMidi(_handle, clipId) != 0;
  int clipNoteCount(int clipId) => _clipNoteCount(_handle, clipId);
  double clipNoteOrigStartAt(int clipId, int index) =>
      _clipNoteOrigStartAt(_handle, clipId, index);

  /// Volume automation (§7.12): replaces the whole curve (empty clears).
  bool setTrackVolumeAutomation(
      int trackId, List<double> times, List<double> values) {
    assert(times.length == values.length);
    final timesPtr = calloc<Double>(times.length);
    final valuesPtr = calloc<Double>(values.length);
    try {
      for (var i = 0; i < times.length; i++) {
        timesPtr[i] = times[i];
        valuesPtr[i] = values[i];
      }
      return _setTrackVolumeAutomation(
              _handle, trackId, timesPtr, valuesPtr, times.length) ==
          0;
    } finally {
      calloc.free(timesPtr);
      calloc.free(valuesPtr);
    }
  }

  int trackVolumeAutoCount(int trackId) =>
      _trackVolumeAutoCount(_handle, trackId);
  double trackVolumeEffectiveAt(int trackId, double timeSeconds) =>
      _trackVolumeEffectiveAt(_handle, trackId, timeSeconds);
  bool setTrackLaneAutomation(
      int trackId, int lane, List<double> times, List<double> values) {
    final timesPtr = calloc<Double>(times.length);
    final valuesPtr = calloc<Double>(values.length);
    try {
      for (var i = 0; i < times.length; i++) {
        timesPtr[i] = times[i];
        valuesPtr[i] = values[i];
      }
      return _setTrackLaneAutomation(
              _handle, trackId, lane, timesPtr, valuesPtr, times.length) ==
          0;
    } finally {
      calloc.free(timesPtr);
      calloc.free(valuesPtr);
    }
  }

  int trackLaneAutoCount(int trackId, int lane) =>
      _trackLaneAutoCount(_handle, trackId, lane);
  bool setTrackParamAutomation(int trackId, int slotKind, int slotIndex,
      int paramIndex, String name, List<double> times, List<double> values) {
    final namePtr = name.toNativeUtf8();
    final timesPtr = calloc<Double>(times.length);
    final valuesPtr = calloc<Double>(values.length);
    try {
      for (var i = 0; i < times.length; i++) {
        timesPtr[i] = times[i];
        valuesPtr[i] = values[i];
      }
      return _setTrackParamAutomation(_handle, trackId, slotKind, slotIndex,
              paramIndex, namePtr, timesPtr, valuesPtr, times.length) ==
          0;
    } finally {
      malloc.free(namePtr);
      calloc.free(timesPtr);
      calloc.free(valuesPtr);
    }
  }

  int trackParamLaneCount(int trackId) =>
      _trackParamLaneCount(_handle, trackId);

  /// (slotKind, slotIndex, paramIndex) of the param lane at [laneIndex].
  (int, int, int) trackParamLaneInfo(int trackId, int laneIndex) {
    final kind = calloc<Int32>();
    final slot = calloc<Int32>();
    final param = calloc<Int32>();
    try {
      _trackParamLaneInfo(_handle, trackId, laneIndex, kind, slot, param);
      return (kind.value, slot.value, param.value);
    } finally {
      calloc.free(kind);
      calloc.free(slot);
      calloc.free(param);
    }
  }

  String trackParamLaneName(int trackId, int laneIndex) =>
      _trackParamLaneName(_handle, trackId, laneIndex).toDartString();
  int trackParamLanePointCount(int trackId, int laneIndex) =>
      _trackParamLanePointCount(_handle, trackId, laneIndex);

  /// (time, value) of the point at [pointIndex] in the param [laneIndex].
  (double, double) trackParamLanePointAt(
      int trackId, int laneIndex, int pointIndex) {
    final timePtr = calloc<Double>();
    final valuePtr = calloc<Double>();
    try {
      _trackParamLanePointAt(
          _handle, trackId, laneIndex, pointIndex, timePtr, valuePtr);
      return (timePtr.value, valuePtr.value);
    } finally {
      calloc.free(timePtr);
      calloc.free(valuePtr);
    }
  }

  bool removeTrackParamLane(int trackId, int laneIndex) =>
      _removeTrackParamLane(_handle, trackId, laneIndex) == 0;
  int pluginParamCount(int trackId, int slotKind, int slotIndex) =>
      _pluginParamCount(_handle, trackId, slotKind, slotIndex);
  String pluginParamName(
          int trackId, int slotKind, int slotIndex, int paramIndex) =>
      _pluginParamName(_handle, trackId, slotKind, slotIndex, paramIndex)
          .toDartString();

  /// (time, value) of the automation point at [index] in [lane].
  (double, double) trackLaneAutoAt(int trackId, int lane, int index) {
    final timePtr = calloc<Double>();
    final valuePtr = calloc<Double>();
    try {
      _trackLaneAutoAt(_handle, trackId, lane, index, timePtr, valuePtr);
      return (timePtr.value, valuePtr.value);
    } finally {
      calloc.free(timePtr);
      calloc.free(valuePtr);
    }
  }
  bool setTrackVolumeAutoPointAt(int trackId, double timeSeconds, double value) =>
      _setTrackVolumeAutoPointAt(_handle, trackId, timeSeconds, value) == 0;
  double get inputQuantizeSeconds => _inputQuantize(_handle);
  bool setInputQuantize(double gridSeconds) =>
      _setInputQuantize(_handle, gridSeconds) == 0;
  double get midiInputLatencyMs => _midiInputLatency(_handle);
  bool setMidiInputLatencyMs(double ms) =>
      _setMidiInputLatency(_handle, ms) == 0;
  bool copyClip(int clipId) => _copyClip(_handle, clipId) == 0;
  bool get hasPendingPluginJobs => _hasPendingPluginJobs(_handle) != 0;
  void pumpOnePluginJob() => _pumpOnePluginJob(_handle);
  bool pasteClip(int trackId, double startSeconds) =>
      _pasteClip(_handle, trackId, startSeconds) == 0;
  bool moveClipToTrack(int clipId, int trackId, double startSeconds) =>
      _moveClipToTrack(_handle, clipId, trackId, startSeconds) == 0;
  bool splitClip(int clipId, double timeSeconds) =>
      _splitClip(_handle, clipId, timeSeconds) == 0;

  /// (time, value) of the automation point at [index].
  (double, double) trackVolumeAutoAt(int trackId, int index) {
    final timePtr = calloc<Double>();
    final valuePtr = calloc<Double>();
    try {
      _trackVolumeAutoAt(_handle, trackId, index, timePtr, valuePtr);
      return (timePtr.value, valuePtr.value);
    } finally {
      calloc.free(timePtr);
      calloc.free(valuePtr);
    }
  }
  int quantizeClipNotes(int clipId, double gridSeconds, double swing,
          double catchRange, double maxMove, bool nonGridProtect, bool iterative,
          double iterStrength) =>
      _quantizeClipNotes(_handle, clipId, gridSeconds, swing, catchRange, maxMove,
          nonGridProtect ? 1 : 0, iterative ? 1 : 0, iterStrength);
  int resetClipQuantize(int clipId) => _resetClipQuantize(_handle, clipId);

  // ---- AI clip generation ----

  void setAiModelDir(String path) =>
      _withUtf8(path, (p) => _setAiModelDir(_handle, p));
  String get aiModelDir => _aiModelDir(_handle).toDartString();

  /// [mode]: 0 melody, 1 drums, 2 bass, 3 harmony/chords.
  bool aiModelAvailable(int mode) => _aiModelAvailable(_handle, mode) != 0;

  /// Queues a generation job on the engine worker thread. Returns false when
  /// another job is running or the target track/clip is invalid.
  bool startAiGeneration({
    required int trackId,
    int clipId = -1,
    required double anchorSeconds,
    required int mode,
    int bars = 4,
    double lengthSeconds = 0.0,
    double temperature = 0.9,
    int variations = 1,
    int seed = 0,
    String? prompt,
    bool replaceClip = false,
    bool harmonize = true,
    bool useModel = true,
  }) =>
      _withUtf8(
          prompt ?? '',
          (p) => _startAiGeneration(
                  _handle,
                  trackId,
                  clipId,
                  anchorSeconds,
                  mode,
                  bars,
                  lengthSeconds,
                  temperature,
                  variations,
                  seed,
                  p,
                  replaceClip ? 1 : 0,
                  harmonize ? 1 : 0,
                  useModel ? 1 : 0) !=
              0);

  /// 0 idle, 1 running, 2 ready, 3 failed.
  int get aiGenerationState => _aiGenerationState(_handle);
  void cancelAiGeneration() => _cancelAiGeneration(_handle);
  void clearAiGeneration() => _clearAiGeneration(_handle);

  bool get aiResultOk => _aiResultOk(_handle) != 0;

  /// [field]: 0 error, 1 provider, 2 key, 3 chords, 4 prompt interpretation,
  /// 5 heard context (audio/MIDI).
  String aiResultString(int field) =>
      _aiResultString(_handle, field).toDartString();
  int get aiResultSeed => _aiResultSeed(_handle);
  double get aiResultClipLength => _aiResultClipLength(_handle);
  double get aiResultStart => _aiResultStart(_handle);
  int get aiResultClipId => _aiResultClipId(_handle);
  int get aiResultTrackId => _aiResultTrackId(_handle);
  int get aiResultNoteCount => _aiResultNoteCount(_handle);

  /// (pitch, velocity, start, length) of a preview result note.
  (int, double, double, double) aiResultNoteAt(int index) {
    final pitch = calloc<Int32>();
    final vel = calloc<Float>();
    final start = calloc<Double>();
    final length = calloc<Double>();
    try {
      _aiResultNoteAt(_handle, index, pitch, vel, start, length);
      return (pitch.value, vel.value, start.value, length.value);
    } finally {
      calloc.free(pitch);
      calloc.free(vel);
      calloc.free(start);
      calloc.free(length);
    }
  }

  /// Applies the ready result atomically. Returns the created/rewritten clip
  /// id, or -1 on failure.
  int applyAiGeneration() {
    final out = calloc<Int64>();
    try {
      return _applyAiGeneration(_handle, out) == 0 ? out.value : -1;
    } finally {
      calloc.free(out);
    }
  }

  // ---- tempo / time signature ----
  double get tempoBpm => _tempoBpm(_handle);
  bool setTempoBpm(double bpm) => _setTempoBpm(_handle, bpm) == 0;
  int get timeSigNumerator => _timeSigNum(_handle);
  int get timeSigDenominator => _timeSigDen(_handle);
  bool setTimeSignature(int numerator, int denominator) =>
      _setTimeSig(_handle, numerator, denominator) == 0;

  int freezeClipQuantize(int clipId) => _freezeClipQuantize(_handle, clipId);

  /// (pitch, velocity, start, length) of the note at [index].
  (int, double, double, double) clipNoteAt(int clipId, int index) {
    final pitch = calloc<Int32>();
    final vel = calloc<Float>();
    final start = calloc<Double>();
    final length = calloc<Double>();
    _clipNoteAt(_handle, clipId, index, pitch, vel, start, length);
    final result = (pitch.value, vel.value, start.value, length.value);
    calloc.free(pitch);
    calloc.free(vel);
    calloc.free(start);
    calloc.free(length);
    return result;
  }

  /// Live MIDI take of an armed track while recording (§7.11). The count
  /// refreshes the engine-side cache, so read it once per frame and then use
  /// [recordingNoteAt] for each index. Starts are relative to the record
  /// start (like a MIDI clip's note starts). 0 when not recording.
  int recordingNoteCount(int trackId) => _recordingNoteCount(_handle, trackId);

  (int, double, double, double) recordingNoteAt(int trackId, int index) {
    final pitch = calloc<Int32>();
    final vel = calloc<Float>();
    final start = calloc<Double>();
    final length = calloc<Double>();
    _recordingNoteAt(_handle, trackId, index, pitch, vel, start, length);
    final result = (pitch.value, vel.value, start.value, length.value);
    calloc.free(pitch);
    calloc.free(vel);
    calloc.free(start);
    calloc.free(length);
    return result;
  }

  // ---- recording ----

  bool trackArmed(int trackId) => _trackArmed(_handle, trackId) != 0;
  int trackInputChannel(int trackId) => _trackInputChannel(_handle, trackId);
  int setTrackArmed(int trackId, bool armed) =>
      _setTrackArmed(_handle, trackId, armed ? 1 : 0);
  int setTrackInputChannel(int trackId, int channel) =>
      _setTrackInputChannel(_handle, trackId, channel);

  int startRecord() => _startRecord(_handle);
  int stopRecord() => _stopRecord(_handle);
  bool get isRecording => _isRecording(_handle) != 0;
  double get recordStartSeconds => _recordStartSeconds(_handle);
  void setDebugLoopback(bool enabled) => _setDebugLoopback(enabled ? 1 : 0);

  // ---- plugins (§7.7) ----

  void rescanPlugins() => _rescanPlugins(_handle);
  void scanPluginsNow() => _scanPluginsNow(_handle);
  bool get pluginScanActive => _pluginScanActive(_handle) != 0;
  int get pluginCount => _pluginCount(_handle);
  String pluginNameAt(int index) => _pluginNameAt(_handle, index).toDartString();
  String pluginUidAt(int index) => _pluginUidAt(_handle, index).toDartString();
  bool pluginIsInstrumentAt(int index) => _pluginIsInstrumentAt(_handle, index) != 0;
  String pluginFormatAt(int index) => _pluginFormatAt(_handle, index).toDartString();

  int trackAddFx(int trackId, int pluginIndex) =>
      _trackAddFx(_handle, trackId, pluginIndex);
  int trackRemoveFx(int trackId, int slotIndex) =>
      _trackRemoveFx(_handle, trackId, slotIndex);
  int trackSetFxBypass(int trackId, int slotIndex, bool bypass) =>
      _trackSetFxBypass(_handle, trackId, slotIndex, bypass ? 1 : 0);
  int trackFxCount(int trackId) => _trackFxCount(_handle, trackId);
  String trackFxNameAt(int trackId, int slotIndex) =>
      _trackFxNameAt(_handle, trackId, slotIndex).toDartString();
  bool trackFxBypassAt(int trackId, int slotIndex) =>
      _trackFxBypassAt(_handle, trackId, slotIndex) != 0;
  int trackSetInstrumentPlugin(int trackId, int pluginIndex) =>
      _trackSetInstrumentPlugin(_handle, trackId, pluginIndex);
  int trackClearInstrument(int trackId) => _trackClearInstrument(_handle, trackId);

  /// Restores a saved state on the live plug-in in insert [slotIndex].
  /// [component] is the VST3 component state, [controller] the (often empty)
  /// controller state, both exactly as a Cubase project stored them.
  int trackSetFxState(int trackId, int slotIndex, Uint8List component,
          Uint8List controller) =>
      _withStateBlobs(component, controller,
          (c, cs, k, ks) => _trackSetFxState(_handle, trackId, slotIndex, c, cs, k, ks));

  /// Restores a saved state on the track's live instrument plug-in.
  int trackSetInstrumentState(int trackId, Uint8List component, Uint8List controller) =>
      _withStateBlobs(component, controller,
          (c, cs, k, ks) => _trackSetInstrumentState(_handle, trackId, c, cs, k, ks));

  /// Test hook: the state the live plug-in reports, base64-encoded (empty when
  /// the slot holds no plug-in). [slotKind] 0 = fx chain, 1 = instrument.
  String pluginStateBase64(int trackId, int slotKind, int slotIndex) =>
      _pluginStateBase64(_handle, trackId, slotKind, slotIndex).toDartString();

  /// Copies two optional byte blobs into native memory for one call, freeing
  /// them afterwards. An empty blob is passed as a null pointer.
  T _withStateBlobs<T>(
      Uint8List component,
      Uint8List controller,
      T Function(Pointer<Uint8>, int, Pointer<Uint8>, int) body) {
    final c = component.isEmpty ? nullptr : malloc<Uint8>(component.length);
    final k = controller.isEmpty ? nullptr : malloc<Uint8>(controller.length);
    try {
      if (component.isNotEmpty) {
        c.asTypedList(component.length).setAll(0, component);
      }
      if (controller.isNotEmpty) {
        k.asTypedList(controller.length).setAll(0, controller);
      }
      return body(c, component.length, k, controller.length);
    } finally {
      if (c != nullptr) malloc.free(c);
      if (k != nullptr) malloc.free(k);
    }
  }
  int openPluginEditor(int trackId, int slotIndex) =>
      _openPluginEditor(_handle, trackId, slotIndex);
  int openInstrumentEditor(int trackId) =>
      _openInstrumentEditor(_handle, trackId);

  int openInstrumentEditorWithClip(int trackId, int clipId) =>
      _openInstrumentEditorWithClip(_handle, trackId, clipId);
  String trackInstrumentName(int trackId) =>
      _trackInstrumentName(_handle, trackId).toDartString();

  int trackSetMidifx(int trackId, int pluginIndex) =>
      _trackSetMidifx(_handle, trackId, pluginIndex);
  int trackClearMidifx(int trackId) => _trackClearMidifx(_handle, trackId);
  String trackMidifxName(int trackId) =>
      _trackMidifxName(_handle, trackId).toDartString();

  int masterAddFx(int pluginIndex) => _masterAddFx(_handle, pluginIndex);
  int masterRemoveFx(int slotIndex) => _masterRemoveFx(_handle, slotIndex);
  int masterSetFxBypass(int slotIndex, bool bypass) =>
      _masterSetFxBypass(_handle, slotIndex, bypass ? 1 : 0);
  int get masterFxCount => _masterFxCount(_handle);
  String masterFxNameAt(int slotIndex) =>
      _masterFxNameAt(_handle, slotIndex).toDartString();
  bool masterFxBypassAt(int slotIndex) => _masterFxBypassAt(_handle, slotIndex) != 0;
  int openMasterFxEditor(int slotIndex) =>
      _masterOpenFxEditor(_handle, slotIndex);

  // ---- device configuration ----

  String get currentOutputDeviceName =>
      _currentOutputDeviceName(_handle).toDartString();
  String get currentInputDeviceName =>
      _currentInputDeviceName(_handle).toDartString();
  int get outputDeviceCount => _outputDeviceCount(_handle);
  String outputDeviceNameAt(int index) =>
      _outputDeviceNameAt(_handle, index).toDartString();
  List<String> get outputDeviceNames => [
        for (var i = 0; i < outputDeviceCount; i++) outputDeviceNameAt(i)
      ];
  int get inputDeviceCount => _inputDeviceCount(_handle);
  String inputDeviceNameAt(int index) =>
      _inputDeviceNameAt(_handle, index).toDartString();
  int get sampleRateCount => _sampleRateCount(_handle);
  double sampleRateAt(int index) => _sampleRateAt(_handle, index);
  List<double> get availableSampleRates => [
        for (var i = 0; i < sampleRateCount; i++) sampleRateAt(i)
      ];
  int get bufferSizeCount => _bufferSizeCount(_handle);
  int bufferSizeAt(int index) => _bufferSizeAt(_handle, index);
  List<int> get availableBufferSizes => [
        for (var i = 0; i < bufferSizeCount; i++) bufferSizeAt(i)
      ];
  int setDeviceConfig(String? output, String? input, double sampleRate, int bufferSize) =>
      _withUtf8(output ?? '', (out) => _withUtf8(input ?? '',
          (inp) => _setDeviceConfig(_handle, out, inp, sampleRate, bufferSize)));
  int get midiInputCount => _midiInputCount(_handle);
  int get deviceInputChannelCount => _deviceInputChannelCount(_handle);
  String midiInputNameAt(int index) =>
      _midiInputNameAt(_handle, index).toDartString();
  List<String> get midiInputNames => [
        for (var i = 0; i < midiInputCount; i++) midiInputNameAt(i)
      ];
  void selectMidiInput(int index) => _selectMidiInput(_handle, index);
  void setMetronome(bool enabled) => _setMetronome(_handle, enabled ? 1 : 0);
  int get selectedMidiInputIndex => _selectedMidiInputIndex(_handle);
  int saveDeviceSettings(String path) =>
      _withUtf8(path, (p) => _saveDeviceSettings(_handle, p));
  int loadDeviceSettings(String path) =>
      _withUtf8(path, (p) => _loadDeviceSettings(_handle, p));

  int setTrackName(int trackId, String name) =>
      _withUtf8(name, (p) => _setTrackName(_handle, trackId, p));
  int setTrackColor(int trackId, int colorIndex) =>
      _setTrackColor(_handle, trackId, colorIndex);

  double clipFadeIn(int clipId) => _clipFadeIn(_handle, clipId);
  double clipFadeOut(int clipId) => _clipFadeOut(_handle, clipId);

  /// Read-and-reset meter peaks (linear). Poll once per frame at most.
  double meterTrackPeak(int trackIndex) => _meterTrackPeak(_handle, trackIndex);
  double meterMasterPeak(int channel) => _meterMasterPeak(_handle, channel);

  /// Waveform peaks for a clip region; fills both lists (same length).
  /// Returns 0 on success.
  int clipPeaks(int clipId, double startSeconds, double lengthSeconds,
      Float32List mins, Float32List maxs) {
    final n = mins.length;
    final pMin = calloc<Float>(n);
    final pMax = calloc<Float>(n);
    final result =
        _clipPeaks(_handle, clipId, startSeconds, lengthSeconds, n, pMin, pMax);
    if (result == 0) {
      mins.setAll(0, pMin.asTypedList(n));
      maxs.setAll(0, pMax.asTypedList(n));
    }
    calloc.free(pMin);
    calloc.free(pMax);
    return result;
  }

  int get trackCount => _trackCount(_handle);

  // ---- document read access (UI snapshot pulls) ----

  int trackIdAt(int index) => _trackIdAt(_handle, index);
  String trackName(int trackId) => _trackName(_handle, trackId).toDartString();
  int trackColorIndex(int trackId) => _trackColorIndex(_handle, trackId);
  double trackVolume(int trackId) => _trackVolume(_handle, trackId);
  double trackPan(int trackId) => _trackPan(_handle, trackId);
  bool trackMute(int trackId) => _trackMute(_handle, trackId) != 0;
  bool trackSolo(int trackId) => _trackSolo(_handle, trackId) != 0;
  int trackClipCount(int trackId) => _trackClipCount(_handle, trackId);
  int clipIdAt(int trackId, int index) => _clipIdAt(_handle, trackId, index);
  double clipStart(int clipId) => _clipStart(_handle, clipId);
  double clipLength(int clipId) => _clipLength(_handle, clipId);
  double clipLengthMax(int clipId) => _clipLengthMax(_handle, clipId);
  double clipOffset(int clipId) => _clipOffset(_handle, clipId);

  // ---- undo / redo (engine-owned history) ----

  bool get canUndo => _canUndo(_handle) != 0;
  bool get canRedo => _canRedo(_handle) != 0;
  bool undo() => _undo(_handle) == 0;
  bool redo() => _redo(_handle) == 0;

  // ---- persistence ----

  int saveProject(String path) =>
      _withUtf8(path, (p) => _saveProject(_handle, p));
  int loadProject(String path) =>
      _withUtf8(path, (p) => _loadProject(_handle, p));
  int importMidiProject(String path) =>
      _withUtf8(path, (p) => _importMidiProject(_handle, p));
  int importStemsFolder(String path) =>
      _withUtf8(path, (p) => _importStemsFolder(_handle, p));
  int importAudioFile(String path) =>
      _withUtf8(path, (p) => _importAudioFile(_handle, p));

  /// Auditions one file on the engine's preview channel (project untouched).
  /// Returns 0 on success. Stops by itself at EOF.
  int previewSound(String path) =>
      _withUtf8(path, (p) => _previewSound(_handle, p));

  void stopPreview() => _stopPreview(_handle);

  bool isPreviewing() => _isPreviewing(_handle) != 0;
  String pickOpenFile(String startDir, String filters) => _withUtf8(
      startDir,
      (dir) => _withUtf8(
          filters, (fl) => _pickOpenFile(_handle, dir, fl).toDartString()));

  /// Convenience: a one-track project holding the whole file.
  int loadFile(String path) => _withUtf8(path, (p) => _loadFile(_handle, p));

  // ---- transport ----

  int play() => _play(_handle);
  int pause() => _pause(_handle);
  int stop() => _stop(_handle);
  int seek(double seconds) => _seek(_handle, seconds);
  int setGain(double linearGain) => _setGain(_handle, linearGain);

  bool get isPlaying => _isPlaying(_handle) != 0;
  double get positionSeconds => _positionSeconds(_handle);
  double get durationSeconds => _durationSeconds(_handle);
  double get sampleRate => _sampleRate(_handle);
  double get masterGain => _masterGain(_handle);
  int get currentBufferSizeSamples => _bufferSizeSamples(_handle);

  // ---- offline render ----

  /// Offline export with a chosen bit depth (16 or 24), at the project
  /// sample rate.
  int renderEx(String outPath, double maxSeconds, int bitsPerSample) {
    return _withUtf8(outPath,
        (p) => _renderEx(_handle, p, maxSeconds, bitsPerSample));
  }

  /// Closes and re-opens the audio device: recovers from CoreAudio
  /// client kills (overrun disabling) without restarting the app.
  bool restartAudio() => _restartAudio(_handle) == 0;
  void initialiseAudioInBackground() => _initialiseAudioBg(_handle);
  bool get audioInitialised => _audioInitialised(_handle) != 0;
  bool get audioDeviceRunning => _audioDeviceRunning(_handle) != 0;
  int get audioXRunCount => _audioXRunCount(_handle);
  int get audioRecoveryCount => _audioRecoveryCount(_handle);
  String get lastAudioError => _lastAudioError(_handle).toDartString();

  int render(String outPath, double maxSeconds) =>
      _withUtf8(outPath, (p) => _render(_handle, p, maxSeconds));

  void dispose() {
    if (_disposed) return;
    _disposed = true;
    // Sub-windows attach to the main window's engine: only the owner may
    // destroy it. Closing an undocked mixer must not kill playback.
    if (_ownsHandle) _destroy(_handle);
  }
}

/// NUL-terminated UTF-8 buffer for a call; freed right after.
R _withUtf8<R>(String s, R Function(Pointer<Utf8>) action) {
  final units = utf8.encode(s);
  final buffer = malloc<Uint8>(units.length + 1);
  buffer.asTypedList(units.length).setAll(0, units);
  buffer[units.length] = 0;
  final result = action(buffer.cast());
  malloc.free(buffer);
  return result;
}
