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
  CadenceEngine({String? dylibPath})
      : _lib = DynamicLibrary.open(resolveEngineDylib(dylibPath)) {
    _create = _lib.lookupFunction<Pointer<Void> Function(), Pointer<Void> Function()>(
        'cadence_engine_create');
    _destroy = _lib.lookupFunction<Void Function(Pointer<Void>),
        void Function(Pointer<Void>)>('cadence_engine_destroy');
    _initialise = _lib.lookupFunction<Int32 Function(Pointer<Void>),
        int Function(Pointer<Void>)>('cadence_engine_initialise');
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
    _lastError = _lib.lookupFunction<Pointer<Utf8> Function(Pointer<Void>),
        Pointer<Utf8> Function(Pointer<Void>)>('cadence_engine_last_error');

    _handle = _create();
  }

  final DynamicLibrary _lib;
  late final Pointer<Void> _handle;

  late final Pointer<Void> Function() _create;
  late final void Function(Pointer<Void>) _destroy;
  late final int Function(Pointer<Void>) _initialise;
  late final int Function(Pointer<Void>, double) _newProject;
  late final int Function(Pointer<Void>, Pointer<Utf8>, int, int) _addTrack;
  late final int Function(Pointer<Void>, int) _trackType;
  late final void Function(Pointer<Void>, int) _selectMidiInput;
  late final void Function(Pointer<Void>, int) _setMetronome;
  late final int Function(Pointer<Void>, int, int) _midiClipRemoveNote;
  late final int Function(Pointer<Void>, int, int, int, double, double) _midiClipSetNote;
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
  late final Pointer<Utf8> Function(Pointer<Void>) _lastError;

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
  bool clipIsMidi(int clipId) => _clipIsMidi(_handle, clipId) != 0;
  int clipNoteCount(int clipId) => _clipNoteCount(_handle, clipId);

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

  int render(String outPath, double maxSeconds) =>
      _withUtf8(outPath, (p) => _render(_handle, p, maxSeconds));

  void dispose() {
    _destroy(_handle);
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
