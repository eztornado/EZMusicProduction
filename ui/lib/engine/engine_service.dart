// EngineService: the single point where the UI talks to the native engine.
//
// Every mutation refreshes the Dart-side snapshot; the playhead is read
// directly from the engine at frame rate (atomic read, never per-block FFI).

import 'package:flutter/foundation.dart';

import 'cadence_bindings.dart';
import 'project_snapshot.dart';

class PeakData {
  const PeakData(this.mins, this.maxs);
  final Float32List mins;
  final Float32List maxs;
}

class EngineService extends ChangeNotifier {
  EngineService({String? dylibPath}) : _engine = CadenceEngine(dylibPath: dylibPath);

  final CadenceEngine _engine;
  ProjectSnapshot _snapshot = const ProjectSnapshot.empty();
  String? statusMessage;
  final Map<String, PeakData> _peakCache = {};
  int _peakGeneration = 0;

  /// UI-side selection (owned by Flutter per ui-design.md §9).
  int? selectedClipId;
  int? selectedTrackId;

  /// Clip currently open in the piano roll editor (null = closed).
  int? editorClipId;

  void openEditor(int clipId) {
    selectedClipId = clipId;
    editorClipId = clipId;
    _mixerOpen = false;
    notifyListeners();
  }

  void closeEditor() {
    if (editorClipId == null) return;
    editorClipId = null;
    notifyListeners();
  }

  ProjectSnapshot get snapshot => _snapshot;
  double get positionSeconds => _engine.positionSeconds;
  double get durationSeconds => _engine.durationSeconds;
  double get sampleRate => _engine.sampleRate;
  int get currentBuffer => _engine.currentBufferSizeSamples;
  bool get isPlaying => _engine.isPlaying;

  /// True when the selected clip exists and is a MIDI clip.
  bool get selectedClipIsMidi {
    if (selectedClipId == null) return false;
    for (final track in _snapshot.tracks) {
      for (final clip in track.clips) {
        if (clip.id == selectedClipId) return clip.isMidi;
      }
    }
    return false;
  }
  bool get canUndo => _engine.canUndo;
  bool get canRedo => _engine.canRedo;
  int get trackCount => _engine.trackCount;

  /// Builds the demo project (3 tracks, staggered tone clips) from the
  /// generated test assets. Returns an error message, or null on success.
  String? loadDemoProject(String assetsDir) {
    final tones = {
      0: ('Drums', 'demo_tone_440.wav', 0.0, 1.0, 0.0),
      1: ('Bass', 'demo_tone_660.wav', 2.0, 0.8, -0.6),
      2: ('Keys', 'demo_tone_330.wav', 4.0, 0.9, 0.6),
    };

    _engine.newProject(48000);
    for (final entry in tones.entries) {
      final t = entry.value;
      final path = '$assetsDir/${t.$2}';
      final trackId = _engine.addTrack(t.$1, entry.key * 3);
      if (trackId < 0) return _engine.lastError;
      if (_engine.addClip(trackId, path, t.$3, t.$4) < 0) {
        return 'No se pudo añadir $path — genera los tonos con '
            '`cadence-cli tone` o revisa CADENCE_ASSETS_DIR. '
            '(${_engine.lastError})';
      }
      _engine.setTrackPan(trackId, t.$5);
    }

    refresh();
    statusMessage = null;
    notifyListeners();
    return null;
  }

  String? initialiseDevice() {
    final result = _engine.initialise();
    if (result != CadenceResult.ok) {
      statusMessage = 'audio device: ${_engine.lastError}';
      notifyListeners();
      return _engine.lastError;
    }
    return null;
  }

  void play() {
    _engine.play();
    notifyListeners();
  }

  void pause() {
    _engine.pause();
    notifyListeners();
  }

  void stop() {
    _engine.stop();
    notifyListeners();
  }

  void undo() {
    _engine.undo();
    selectedClipId = null; // the clip may no longer exist in this state
    refresh();
  }

  void redo() {
    _engine.redo();
    selectedClipId = null;
    refresh();
  }

  // ---- editing (each commits one undo entry in the engine) ----

  /// Creates a track (type: 0 audio, 1 instrument). One undo entry.
  void addTrack(String name, int colorIndex, {int type = 0}) {
    _engine.addTrack(name, colorIndex, type: type);
    refresh();
  }

  void selectMidiInput(int index) {
    _engine.selectMidiInput(index);
    _engine.saveDeviceSettings(defaultSettingsPath);
    notifyListeners();
  }

  int get selectedMidiInputIndex => _engine.selectedMidiInputIndex;

  void selectClip(int? clipId) {
    if (selectedClipId == clipId) return;
    selectedClipId = clipId;
    notifyListeners();
  }

  void moveClip(int clipId, double startSeconds) {
    _engine.setClipStart(clipId, startSeconds);
    refresh();
  }

  /// Trim (edge drag) — one command, one undo entry.
  void trimClip(int clipId, double startSeconds, double sourceOffsetSeconds,
      double lengthSeconds) {
    _engine.setClipBounds(clipId, startSeconds, sourceOffsetSeconds, lengthSeconds);
    refresh();
  }

  void setTrackMute(int trackId, bool muted) {
    _engine.setTrackMute(trackId, muted);
    refresh();
  }

  void setTrackSolo(int trackId, bool soloed) {
    _engine.setTrackSolo(trackId, soloed);
    refresh();
  }

  /// Called on slider release: one drag = one undo entry.
  void setTrackVolume(int trackId, double linearGain) {
    _engine.setTrackVolume(trackId, linearGain);
    refresh();
  }

  void setTrackPan(int trackId, double pan) {
    _engine.setTrackPan(trackId, pan);
    refresh();
  }

  void setTrackName(int trackId, String name) {
    _engine.setTrackName(trackId, name);
    refresh();
  }

  void setTrackColor(int trackId, int colorIndex) {
    _engine.setTrackColor(trackId, colorIndex);
    refresh();
  }

  void setMasterGain(double linearGain) {
    _engine.setGain(linearGain);
    refresh();
  }

  void setClipFades(int clipId, double fadeIn, double fadeOut) {
    _engine.setClipFades(clipId, fadeIn, fadeOut);
    refresh();
  }

  // ---- MIDI clips ----

  void addMidiClip(int trackId, double startSeconds, double lengthSeconds) {
    _engine.addMidiClip(trackId, startSeconds, lengthSeconds);
    refresh();
  }

  void midiClipAddNote(int clipId, int pitch, double velocity,
      double startSeconds, double lengthSeconds) {
    _engine.midiClipAddNote(clipId, pitch, velocity, startSeconds, lengthSeconds);
    refresh();
  }

  void midiClipRemoveNote(int clipId, int noteIndex) {
    _engine.midiClipRemoveNote(clipId, noteIndex);
    refresh();
  }

  void midiClipSetNote(int clipId, int noteIndex, int pitch, double startSeconds,
      double lengthSeconds) {
    _engine.midiClipSetNote(clipId, noteIndex, pitch, startSeconds, lengthSeconds);
    refresh();
  }

  /// Read-and-reset meter peaks; poll once per frame at most.
  double meterTrackPeak(int trackIndex) => _engine.meterTrackPeak(trackIndex);
  double meterMasterPeak(int channel) => _engine.meterMasterPeak(channel);

  /// Cached waveform peaks for painting, keyed per clip and pixel width.
  /// The cache clears on every document refresh (edits are cheap to recompute).
  PeakData getClipPeaks(ClipInfo clip, {required double pixelsPerSecond}) {
    final px = (clip.length * pixelsPerSecond).round().clamp(8, 8000);
    final key = '$_peakGeneration:${clip.id}:$px:${clip.offset}';
    return _peakCache.putIfAbsent(key, () {
      final mins = Float32List(px);
      final maxs = Float32List(px);
      _engine.clipPeaks(clip.id, 0, clip.length, mins, maxs);
      return PeakData(mins, maxs);
    });
  }

  void selectTrack(int? trackId) {
    if (selectedTrackId == trackId) return;
    selectedTrackId = trackId;
    notifyListeners();
  }

  void removeTrack(int trackId) {
    _engine.removeTrack(trackId);
    if (selectedTrackId == trackId) selectedTrackId = null;
    refresh();
  }

  // ---- metronome ----

  bool _metronomeOn = false;
  bool get metronomeOn => _metronomeOn;

  void toggleMetronome() {
    _metronomeOn = !_metronomeOn;
    _engine.setMetronome(_metronomeOn);
    notifyListeners();
  }

  bool _mixerOpen = false;
  bool get mixerOpen => _mixerOpen;

  void toggleMixerOpen() {
    _mixerOpen = !_mixerOpen;
    if (_mixerOpen) closeEditor();
    notifyListeners();
  }

  void closeMixer() {
    _mixerOpen = false;
    notifyListeners();
  }

  // ---- recording (§7.11) ----

  static const String defaultSettingsPath =
      '/Users/ivan/Music/Cadence/settings.json';

  bool get isRecording => _engine.isRecording;
  double get recordStartSeconds => _engine.recordStartSeconds;

  void setTrackArmed(int trackId, bool armed) {
    _engine.setTrackArmed(trackId, armed);
    refresh();
  }

  void setTrackInputChannel(int trackId, int channel) {
    _engine.setTrackInputChannel(trackId, channel);
    refresh();
  }

  /// Starts capture on armed tracks. Returns an error message or null.
  String? startRecord() {
    final result = _engine.startRecord();
    notifyListeners();
    return result == CadenceResult.ok ? null : _engine.lastError;
  }

  /// Finalizes takes into clips and pauses the transport.
  void stopRecord() {
    _engine.stopRecord();
    refresh();
  }

  // ---- device configuration (§7.14) ----

  List<String> get inputDeviceNames => [
        for (var i = 0; i < _engine.inputDeviceCount; i++)
          _engine.inputDeviceNameAt(i)
      ];
  List<String> get outputDeviceNames => _engine.outputDeviceNames;
  List<double> get availableSampleRates => _engine.availableSampleRates;
  List<int> get availableBufferSizes => _engine.availableBufferSizes;
  List<String> get midiInputNames => _engine.midiInputNames;
  int get deviceInputChannelCount => _engine.deviceInputChannelCount;
  String get currentOutputDevice => _engine.currentOutputDeviceName;
  String get currentInputDevice => _engine.currentInputDeviceName;

  String? setDeviceConfig(String? output, String? input, double sampleRate, int bufferSize) {
    final result = _engine.setDeviceConfig(output, input, sampleRate, bufferSize);
    if (result != CadenceResult.ok) return _engine.lastError;
    refresh();
    return null;
  }

  String? saveDeviceSettings([String? path]) {
    final result = _engine.saveDeviceSettings(path ?? defaultSettingsPath);
    return result == CadenceResult.ok ? null : _engine.lastError;
  }

  String? loadDeviceSettings([String? path]) {
    final result = _engine.loadDeviceSettings(path ?? defaultSettingsPath);
    return result == CadenceResult.ok ? null : null; // missing file is fine
  }

  void deleteSelectedClip() {
    final id = selectedClipId;
    if (id == null) return;
    _engine.removeClip(id);
    selectedClipId = null;
    refresh();
  }

  void saveProject(String path) {
    final result = _engine.saveProject(path);
    statusMessage =
        result == CadenceResult.ok ? 'Guardado en $path' : _engine.lastError;
    notifyListeners();
  }

  /// Re-pulls the immutable document snapshot after any edit.
  void refresh() {
    _snapshot = pullSnapshot(_engine);
    _peakGeneration++;
    _peakCache.clear();
    notifyListeners();
  }

  @override
  void dispose() {
    _engine.dispose();
    super.dispose();
  }
}
