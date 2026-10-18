// EngineService: the single point where the UI talks to the native engine.
//
// Every mutation refreshes the Dart-side snapshot; the playhead is read
// directly from the engine at frame rate (atomic read, never per-block FFI).

import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'cadence_bindings.dart';
import 'cubase/cpr_reader.dart';
import 'project_snapshot.dart';
import '../l10n/l10n.dart';

/// Index in [installed] of the plug-in whose name matches [cubaseName], or -1.
///
/// Cubase stores a plug-in's own name — not the track name — so the exported
/// name is what has to be matched. Instruments match fuzzily (either name
/// containing the other), because the same plug-in is written differently by
/// different versions ("ZENOLOGY" vs "ZENOLOGY (VST3)"); effects match exactly,
/// since a loose match could pick an unrelated plug-in that shares a word.
/// Pure, so it can be tested without the engine.
int matchPluginName(String cubaseName, List<String> installed,
    {bool fuzzy = true}) {
  final want = cubaseName.trim().toLowerCase();
  if (want.isEmpty) return -1;
  for (var i = 0; i < installed.length; i++) {
    final got = installed[i].trim().toLowerCase();
    if (got.isEmpty) continue;
    if (got == want) return i;
    if (fuzzy && (got.contains(want) || want.contains(got))) return i;
  }
  return -1;
}

class PeakData {
  const PeakData(this.mins, this.maxs);
  final Float32List mins;
  final Float32List maxs;
}

class PluginInfo {
  const PluginInfo({
    required this.index,
    required this.name,
    required this.uid,
    required this.isInstrument,
    required this.format,
  });

  final int index;
  final String name;
  final String uid;
  final bool isInstrument;
  final String format;
}

class FxSlotInfo {
  const FxSlotInfo({
    required this.name,
    required this.bypass,
  });

  final String name;
  final bool bypass;
}

const String kDefaultProjectsDir =
    '/Users/ivan/Music/EZMusicProduction/Projects';

class EngineService extends ChangeNotifier {
  /// Sub-windows: attaches to the MAIN window's engine (same process, same
  /// project, same audio) — edits from any window hit the same engine.
  EngineService.attachEngine(int handleAddress)
      : this(dylibPath: null, attachHandleAddress: handleAddress);

  EngineService({String? dylibPath, int? attachHandleAddress})
      : _engine = CadenceEngine(
            dylibPath: dylibPath, attachHandleAddress: attachHandleAddress);

  int get engineHandleAddress => _engine.handleAddress;

  static const _windowChannel =
      MethodChannel('com.ezmusicproduction/window');

  /// Sets the macOS window title: 'project - EZMusicProduction'.
  void updateWindowTitle() {
    String? name;
    if (lastProjectPath != null) {
      final base = lastProjectPath!.split('/').last;
      name = base.endsWith('.ezmpp')
          ? base.substring(0, base.length - 6)
          : base;
    } else {
      for (final t in snapshot.tracks) {
        if (t.name.isNotEmpty) {
          // Keep engine name if the project was saved under it — handled by
          // lastProjectPath above; empty tracks don't change it.
        }
      }
    }
    final title = name != null && name.isNotEmpty
        ? '$name - EZMusicProduction'
        : 'EZMusicProduction';
    _windowChannel
        .invokeMethod('setWindowTitle', title)
        .catchError((_) {});
  }

  /// Drains ONE deferred plugin-load job. Called from the app's frame
  /// ticker — the engine's JUCE callAsync is not always pumped in this
  /// host, so without this FX chains stay empty after loading a project.
  void pumpPluginJobs() {
    if (!_engine.hasPendingPluginJobs) return;
    _engine.pumpOnePluginJob();
    if (!_engine.hasPendingPluginJobs) {
      statusMessage = L10n.current.servicePluginsLoaded;
      refresh();
    }
  }

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
    _automationOpen = false;
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
        return L10n.current.serviceAddFailed(path, _engine.lastError);
      }
      _engine.setTrackPan(trackId, t.$5);
    }

    refresh();
    statusMessage = null;
    notifyListeners();
    return null;
  }

  /// Closes and re-opens the audio device: recovers from CoreAudio
  /// client kills (overrun disabling) without restarting the app.
  String? restartAudio() {
    stop();
    final result = _engine.restartAudio();
    if (!result) {
      statusMessage = L10n.current.serviceAudioDeviceError(_engine.lastError);
      notifyListeners();
      return L10n.current.serviceAudioDeviceError(_engine.lastError);
    }
    notifyListeners();
    return null;
  }

  String? initialiseDevice() {
    final result = _engine.initialise();
    if (result != CadenceResult.ok) {
      statusMessage = L10n.current.serviceAudioDeviceError(_engine.lastError);
      notifyListeners();
      return L10n.current.serviceAudioDeviceError(_engine.lastError);
    }
    return null;
  }

  /// Opens the audio device on a background thread (non-blocking).
  /// Poll [audioInitialised] to know when it's ready.
  void initialiseAudioInBackground() {
    _engine.initialiseAudioInBackground();
  }

  bool get audioInitialised => _engine.audioInitialised;

  bool _audioOpened = false;

  /// Opens the audio device on first use. Returns the error (or null).
  String? _ensureAudio() {
    if (_audioOpened) return null;
    _audioOpened = true;
    return initialiseDevice();
  }

  /// Starts playback. Returns an error message or null: sin esto, un fallo al
  /// abrir el dispositivo (p. ej. "en uso") se perdia en silencio al dar Play.
  String? play() {
    final audio = _ensureAudio();
    if (audio != null) return audio;
    final result = _engine.play();
    if (result != CadenceResult.ok) return _engine.lastError;
    notifyListeners();
    return null;
  }

  /// Moves the playhead (timeline click on the ruler or empty space).
  void seek(double seconds) {
    _engine.seek(seconds);
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

  /// Imports an audio file and adds it as a clip to the first track.
  /// Returns the new clip id, or -1 on failure.
  int addClipToFirstTrack(String path) {
    if (snapshot.tracks.isEmpty) return -1;
    final trackId = snapshot.tracks.first.id;
    final clipId = _engine.addClip(trackId, path, 0.0, 1.0);
    refresh();
    return clipId;
  }

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

  /// Moves a clip to another track (same type required) and position.
  /// Returns an error message or null.
  String? moveClipToTrack(int clipId, int trackId, double startSeconds) {
    final result = _engine.moveClipToTrack(clipId, trackId, startSeconds);
    if (!result) return _engine.lastError;
    refresh();
    return null;
  }

  /// Splits a clip at the given timeline position (one undo entry).
  /// Returns an error message or null.
  String? splitClip(int clipId, double timeSeconds) {
    final result = _engine.splitClip(clipId, timeSeconds);
    if (!result) return _engine.lastError;
    refresh();
    return null;
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

  /// Automation Write mode (§7.12 v1.5, the W button): fader moves write
  /// points into the volume curve at the playhead instead of the static
  /// volume. One undo entry per fader release.
  bool automationWrite = false;

  /// Snap to grid (§7.4): when on, clip moves/trims (timeline) and note
  /// drags/creates (piano roll) land exclusively on grid positions; when
  /// off, everything is free. ⌘ bypasses snap per-gesture either way.
  bool snapToGrid = true;
  void toggleSnapToGrid() {
    snapToGrid = !snapToGrid;
    notifyListeners();
  }

  /// Called on slider release: one drag = one undo entry.
  void setTrackVolume(int trackId, double linearGain) {
    if (automationWrite) {
      final time = positionSeconds.clamp(0.0, double.infinity);
      _engine.setTrackVolumeAutoPointAt(
          trackId, time, linearGain.clamp(0.0, 1.0));
    } else {
      _engine.setTrackVolume(trackId, linearGain);
    }
    refresh();
  }

  double get inputQuantizeSeconds => _engine.inputQuantizeSeconds;

  /// Deferred plugin-load queue (project load): tests (and diagnostics)
  /// can drain it synchronously — the app pumps it via the message loop.
  bool get engineHasPendingPluginJobs => _engine.hasPendingPluginJobs;
  void enginePumpOnePluginJob() => _engine.pumpOnePluginJob();

  /// MIDI input latency compensation (ms): recorded takes shift this much
  /// earlier at finalize. Persisted with the device settings.
  double get midiInputLatencyMs => _engine.midiInputLatencyMs;
  void setMidiInputLatencyMs(double ms) {
    _engine.setMidiInputLatencyMs(ms);
    saveDeviceSettings();
    notifyListeners();
  }

  // ---- clip clipboard (§7.4): ⌘C / ⌘V ----

  /// Copies the selected clip (audio or MIDI). Silently ignores no-selection.
  void copySelectedClip() {
    final id = selectedClipId;
    if (id == null) return;
    if (_engine.copyClip(id)) {
      statusMessage = L10n.current.serviceClipCopied;
      notifyListeners();
    }
  }

  /// Pastes the clipboard content on the selected track at the playhead.
  /// Returns an error message or null.
  String? pasteClipboard() {
    final trackId = selectedTrackId;
    if (trackId == null) return L10n.current.serviceSelectTrackToPaste;
    if (!_engine.pasteClip(trackId, positionSeconds)) {
      return _engine.lastError;
    }
    // Select the pasted clip (the newest on the target track).
    final clips = snapshot.tracks
        .where((t) => t.id == trackId)
        .expand((t) => t.clips)
        .toList();
    if (clips.isNotEmpty) {
      clips.sort((a, b) => a.start.compareTo(b.start));
      for (final c in clips) {
        if (c.start <= positionSeconds && positionSeconds <= c.end) {
          selectedClipId = c.id;
          break;
        }
      }
    }
    statusMessage = L10n.current.serviceClipPasted;
    refresh();
    return null;
  }

  /// Effective gain at the playhead: the automation curve evaluated there,
  /// or the static volume. The mixer fader mirrors this in Read mode.
  double trackVolumeEffectiveAt(int trackId, double timeSeconds) =>
      _engine.trackVolumeEffectiveAt(trackId, timeSeconds);

  /// Generic automation lane (§7.12 v1.5): lane 0 = volume, 1 = pan,
  /// 2 = sustain (CC64). Whole-curve replace, one undo entry.
  void setTrackLaneAutomation(
      int trackId, int lane, List<AutomationPointInfo> points) {
    _engine.setTrackLaneAutomation(
        trackId,
        lane,
        [for (final p in points) p.time],
        [for (final p in points) p.value]);
    refresh();
  }

  /// Plugin-parameter automation lane (§7.12 v1.5): keyed by
  /// (slotKind, slotIndex, paramIndex). Whole-curve replace, one undo entry.
  void setTrackParamAutomation(int trackId, int slotKind, int slotIndex,
      int paramIndex, String name, List<AutomationPointInfo> points) {
    _engine.setTrackParamAutomation(
        trackId,
        slotKind,
        slotIndex,
        paramIndex,
        name,
        [for (final p in points) p.time],
        [for (final p in points) p.value]);
    refresh();
  }

  int pluginParamCount(int trackId, int slotKind, int slotIndex) =>
      _engine.pluginParamCount(trackId, slotKind, slotIndex);

  String pluginParamName(
          int trackId, int slotKind, int slotIndex, int paramIndex) =>
      _engine.pluginParamName(trackId, slotKind, slotIndex, paramIndex);

  /// Input quantize grid for recorded MIDI takes, in seconds (0 = off).
  /// Returns an error message or null.
  String? setInputQuantize(double gridSeconds) {
    final result = _engine.setInputQuantize(gridSeconds);
    if (!result) return _engine.lastError;
    refresh();
    return null;
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

  /// Quantize (§7.5.2): moves note starts toward the nearest grid position.
  /// [swing]/[catchRange]/[maxMove] in 0..1; iterative applies one strength
  /// step from the current position instead of the full move.
  void quantizeClip(int clipId, double gridSeconds, double swing,
      double catchRange, double maxMove, bool nonGridProtect, bool iterative,
      double iterStrength) {
    _engine.quantizeClipNotes(clipId, gridSeconds, swing, catchRange, maxMove,
        nonGridProtect, iterative, iterStrength);
    refresh();
  }

  void resetClipQuantize(int clipId) {
    _engine.resetClipQuantize(clipId);
    refresh();
  }

  void freezeClipQuantize(int clipId) {
    _engine.freezeClipQuantize(clipId);
    refresh();
  }

  /// Read-and-reset meter peaks; poll once per frame at most.
  double meterTrackPeak(int trackIndex) => _engine.meterTrackPeak(trackIndex);
  double meterMasterPeak(int channel) => _engine.meterMasterPeak(channel);

  // ---- plugins (§7.7) ----

  List<PluginInfo> _plugins = [];
  bool _pluginsScanned = false;

  List<PluginInfo> get plugins => _plugins;
  bool get pluginsScanned => _pluginsScanned;

  /// True while the engine's chunked scan is still walking plugin files.
  bool get scanActive => _engine.pluginScanActive;

  /// Re-reads the engine's current plugin list (cache + whatever a running
  /// scan has already merged). Cheap; safe to call at frame rate.
  void refreshPluginList() {
    final list = <PluginInfo>[];
    for (var i = 0; i < _engine.pluginCount; i++) {
      list.add(PluginInfo(
        index: i,
        name: _engine.pluginNameAt(i),
        uid: _engine.pluginUidAt(i),
        isInstrument: _engine.pluginIsInstrumentAt(i),
        format: _engine.pluginFormatAt(i),
      ));
    }
    _plugins = list;
    _pluginsScanned = true;
    notifyListeners();
  }

  /// Kicks a full background scan (chunked on the engine's message thread)
  /// and refreshes the visible list. First scan on a big library takes
  /// minutes — poll [scanActive] / call [refreshPluginList] to track it.
  void scanPlugins() {
    _engine.rescanPlugins();
    refreshPluginList();
  }

  /// Blocking full scan. Only for tests/tools: blocks the calling isolate
  /// for as long as the library takes to scan.
  void scanPluginsNow() {
    _engine.scanPluginsNow();
    refreshPluginList();
  }

  void addTrackFx(int trackId, int pluginIndex) {
    _engine.trackAddFx(trackId, pluginIndex);
    refresh();
  }

  /// Adds an FX to a track and opens the new plugin's editor once the engine
  /// has actually applied the add (adds are queued plugin jobs that the frame
  /// ticker pumps — reading the chain length before that targets an older
  /// slot and opens the wrong plugin).
  /// Returns the editor-open error, or null on success.
  Future<String?> addTrackFxAndOpen(int trackId, int pluginIndex) async {
    final before = _trackFxCount(trackId);
    _engine.trackAddFx(trackId, pluginIndex);
    refresh();
    final deadline = DateTime.now().add(const Duration(seconds: 5));
    var count = before;
    while (DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 40));
      count = _trackFxCount(trackId);
      if (count > before) break;
    }
    if (count <= before) return L10n.current.serviceFxAddFailed;
    return openTrackPluginEditor(trackId, count - 1);
  }

  int _trackFxCount(int trackId) {
    for (final t in snapshot.tracks) {
      if (t.id == trackId) return t.fxNames.length;
    }
    return 0;
  }

  void removeTrackFx(int trackId, int slotIndex) {
    _engine.trackRemoveFx(trackId, slotIndex);
    refresh();
  }

  void toggleTrackFxBypass(int trackId, int slotIndex) {
    final bypass = _engine.trackFxBypassAt(trackId, slotIndex);
    _engine.trackSetFxBypass(trackId, slotIndex, !bypass);
    refresh();
  }

  void setTrackInstrumentPlugin(int trackId, int pluginIndex) {
    _engine.trackSetInstrumentPlugin(trackId, pluginIndex);
    refresh();
  }

  /// Hands a VSTi the state Cubase saved for it (the `audioComponent` /
  /// `editController` blobs), so the project's patch comes up instead of the
  /// plug-in's default. Returns whether the plug-in accepted it.
  bool setTrackInstrumentState(
    int trackId,
    Uint8List component, [
    Uint8List? controller,
  ]) =>
      _engine.trackSetInstrumentState(
          trackId, component, controller ?? Uint8List(0)) ==
      CadenceResult.ok;

  void setTrackMidiFx(int trackId, int pluginIndex) {
    _engine.trackSetMidifx(trackId, pluginIndex);
    refresh();
  }

  void clearTrackMidiFx(int trackId) {
    _engine.trackClearMidifx(trackId);
    refresh();
  }

  String trackMidifxName(int trackId) => _engine.trackMidifxName(trackId);

  double get tempoBpm => _engine.tempoBpm;
  int get timeSigNumerator => _engine.timeSigNumerator;
  int get timeSigDenominator => _engine.timeSigDenominator;

  /// Sets the project tempo (one undo entry). Returns an error or null.
  String? setTempo(double bpm) {
    final result = _engine.setTempoBpm(bpm);
    if (!result) return _engine.lastError;
    refresh();
    return null;
  }

  /// Sets the time signature (one undo entry). Returns an error or null.
  String? setTimeSignature(int numerator, int denominator) {
    final result = _engine.setTimeSignature(numerator, denominator);
    if (!result) return _engine.lastError;
    refresh();
    return null;
  }

  /// Volume automation (§7.12): replaces the curve (empty list clears it).
  /// One undo entry. Read mode is always active in the engine.
  void setTrackVolumeAutomation(
      int trackId, List<AutomationPointInfo> points) {
    _engine.setTrackVolumeAutomation(
        trackId,
        [for (final p in points) p.time],
        [for (final p in points) p.value]);
    refresh();
  }

  /// Opens the plugin's own editor window (fx slot). Returns an error
  /// message or null on success.
  String? openTrackPluginEditor(int trackId, int slotIndex) {
    final result = _engine.openPluginEditor(trackId, slotIndex);
    return result == CadenceResult.ok ? null : _engine.lastError;
  }

  /// Opens the instrument plugin's editor window (instrument tracks).
  /// EZSynth receives the piano-roll clip as its editing target.
  String? openTrackInstrumentEditor(int trackId) {
    final result = _engine.openInstrumentEditorWithClip(
        trackId, editorClipId ?? 0);
    return result == CadenceResult.ok ? null : _engine.lastError;
  }

  // ---- Biblioteca de sonidos (catálogo CC descargable) ----

  String libraryDir() {
    final home = Platform.environment['HOME'] ?? '.';
    return '$home/Music/EZMusicProduction/Library';
  }

  File _libraryStateFile() => File('${libraryDir()}/library_state.json');

  Map<String, dynamic> libraryState() {
    final f = _libraryStateFile();
    if (!f.existsSync()) return {};
    try {
      return jsonDecode(f.readAsStringSync()) as Map<String, dynamic>;
    } catch (_) {
      return {};
    }
  }

  void saveLibraryState(Map<String, dynamic> state) {
    final dir = Directory(libraryDir());
    if (!dir.existsSync()) dir.createSync(recursive: true);
    _libraryStateFile().writeAsStringSync(jsonEncode(state));
  }

  /// Descarga [url] en Library/<category>/<fileName> con progreso 0..1.
  /// Devuelve un error o null. Escribe <nombre>.license.txt junto al audio
  /// (atribución CC: autor + licencia + fuente).
  Future<String?> downloadLibrarySound({
    required String url,
    required String category,
    required String fileName,
    required String attribution,
    void Function(double)? onProgress,
  }) async {
    HttpClient? client;
    try {
      final dir = Directory('${libraryDir()}/$category');
      if (!dir.existsSync()) dir.createSync(recursive: true);
      final target = File('${dir.path}/$fileName');
      final tmp = File('${target.path}.part');
      client = HttpClient();
      client.userAgent = 'EZMusicProduction/1.0';
      final req = await client.getUrl(Uri.parse(url));
      final res = await req.close();
      if (res.statusCode != 200) return 'HTTP ${res.statusCode}';
      final total = res.contentLength;
      final sink = tmp.openWrite();
      var received = 0;
      await for (final chunk in res) {
        received += chunk.length;
        sink.add(chunk);
        if (total > 0 && onProgress != null) {
          onProgress(received / total);
        }
      }
      await sink.flush();
      await sink.close();
      if (target.existsSync()) target.deleteSync();
      tmp.renameSync(target.path);
      File('${dir.path}/$fileName.license.txt')
          .writeAsStringSync(attribution);
      return null;
    } catch (e) {
      return e.toString();
    } finally {
      client?.close();
    }
  }

  /// Añade un fichero descargado como clip en la pista seleccionada al
  /// playhead. Devuelve un error o null.
  String? addLibraryClip(String filePath) {
    final trackId = selectedTrackId;
    if (trackId == null) return L10n.current.serviceSelectTrackFirst;
    final id = _engine.addClip(trackId, filePath, positionSeconds, 1.0);
    if (id == 0) return _engine.lastError;
    refresh();
    return null;
  }

  /// Auditions a downloaded library sound without touching the project.
  /// Returns the engine error, or null when playback started.
  String? previewLibrarySound(String filePath) {
    final code = _engine.previewSound(filePath);
    return code == 0 ? null : _engine.lastError;
  }

  void stopLibraryPreview() => _engine.stopPreview();

  bool isPreviewingLibrarySound() => _engine.isPreviewing();

  void clearTrackInstrument(int trackId) {
    _engine.trackClearInstrument(trackId);
    refresh();
  }

  void addMasterFx(int pluginIndex) {
    _engine.masterAddFx(pluginIndex);
    refresh();
  }

  void removeMasterFx(int slotIndex) {
    _engine.masterRemoveFx(slotIndex);
    refresh();
  }

  void toggleMasterFxBypass(int slotIndex) {
    final bypass = _engine.masterFxBypassAt(slotIndex);
    _engine.masterSetFxBypass(slotIndex, !bypass);
    refresh();
  }

  List<FxSlotInfo> trackFxSlots(int trackId) {
    final out = <FxSlotInfo>[];
    final count = _engine.trackFxCount(trackId);
    for (var i = 0; i < count; i++) {
      out.add(FxSlotInfo(
          name: _engine.trackFxNameAt(trackId, i),
          bypass: _engine.trackFxBypassAt(trackId, i)));
    }
    return out;
  }

  /// Diagnostic/test hook: the state the live plug-in in a slot currently
  /// reports, base64-encoded (empty when the slot holds no plug-in).
  /// [slotKind] 0 = fx chain slot, 1 = the track's instrument.
  String pluginStateBase64(int trackId, int slotKind, int slotIndex) =>
      _engine.pluginStateBase64(trackId, slotKind, slotIndex);

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

  bool _mixerOpen = true; // open at launch: fx/instrument management is visible
  bool get mixerOpen => _mixerOpen;

  void toggleMixerOpen() {
    _mixerOpen = !_mixerOpen;
    if (_mixerOpen) closeEditor();
    _automationOpen = false;
    notifyListeners();
  }

  void closeMixer() {
    _mixerOpen = false;
    notifyListeners();
  }

  // ---- automation lane (§7.12) ----

  bool _automationOpen = false;
  bool get automationOpen => _automationOpen;

  void toggleAutomationOpen() {
    _automationOpen = !_automationOpen;
    if (_automationOpen) {
      closeEditor();
      _mixerOpen = false;
    }
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
    final audio = _ensureAudio();
    if (audio != null) return audio;
    final result = _engine.startRecord();
    notifyListeners();
    return result == CadenceResult.ok ? null : _engine.lastError;
  }

  /// Finalizes takes into clips and pauses the transport.
  void stopRecord() {
    _engine.stopRecord();
    refresh();
  }

  /// Live MIDI take of an armed track while recording (§7.11): the timeline
  /// draws these notes as they arrive. Read the count once per frame and then
  /// the notes by index. Starts are relative to the record start, exactly like
  /// a MIDI clip's note starts. 0 when the track is not being recorded.
  int recordingNoteCount(int trackId) => _engine.recordingNoteCount(trackId);
  (int, double, double, double) recordingNoteAt(int trackId, int index) =>
      _engine.recordingNoteAt(trackId, index);

  // ---- device configuration (§7.14) ----
  //
  // Device lists are CACHED: re-enumerating goes through CoreAudio and can
  // block the main thread if the HAL is in a transitional state (e.g. a
  // device was just connected). Use refreshDeviceLists() to re-scan.

  List<String>? _cachedInputs;
  List<String>? _cachedOutputs;
  List<double>? _cachedRates;
  List<int>? _cachedBuffers;
  List<String>? _cachedMidi;

  List<String> get inputDeviceNames =>
      _cachedInputs ??= [
        for (var i = 0; i < _engine.inputDeviceCount; i++)
          _engine.inputDeviceNameAt(i)
      ];
  List<String> get outputDeviceNames => _cachedOutputs ??= _engine.outputDeviceNames;
  List<double> get availableSampleRates =>
      _cachedRates ??= _engine.availableSampleRates;
  List<int> get availableBufferSizes =>
      _cachedBuffers ??= _engine.availableBufferSizes;
  List<String> get midiInputNames => _cachedMidi ??= _engine.midiInputNames;

  /// Forces a re-enumeration of devices from CoreAudio.
  void refreshDeviceLists() {
    _cachedInputs = null;
    _cachedOutputs = null;
    _cachedRates = null;
    _cachedBuffers = null;
    _cachedMidi = null;
  }

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

  /// Offline export (§7.13 v1): renders the whole project to a 16 or 24-bit
  /// stereo WAV at the project sample rate, faster than realtime. Returns
  /// an error message or null.
  String? exportWav(String path, {int bitsPerSample = 16}) {
    if (isRecording) return L10n.current.serviceRecordingInProgress;
    final result = _engine.renderEx(path, 9999, bitsPerSample);
    if (result != CadenceResult.ok) return _engine.lastError;
    statusMessage = L10n.current.serviceExportedTo(path);
    notifyListeners();
    return null;
  }

  void saveProject(String path) {
    final result = _engine.saveProject(path);
    statusMessage = result == CadenceResult.ok
        ? L10n.current.serviceSavedIn(path)
        : _engine.lastError;
    notifyListeners();
  }

  // ---- project management (§5 toolbar: Nuevo / Abrir / Guardar) ----

  /// Sessions remember the last saved/opened path; Guardar reuses it and
  /// Guardar-como offers the name dialog. Not persisted across launches yet.
  String? lastProjectPath;

  /// Starts an empty project at the current device sample rate and resets
  /// the UI selection state. Returns an error message or null.
  String? newEmptyProject() {
    if (isRecording) return L10n.current.serviceRecordingInProgress;
    final rate = sampleRate > 0 ? sampleRate : 48000.0;
    final result = _engine.newProject(rate);
    if (result != CadenceResult.ok) return _engine.lastError;
    _resetSelection();
    lastProjectPath = null;
    statusMessage = L10n.current.serviceNewProject;
    refresh();
    updateWindowTitle();
    return null;
  }

  /// Imports a Cubase project (`.cpr`) directly, read-only. Recreates the
  /// tracks (name, instrument/audio kind, volume, pan), the tempo and time
  /// signature, the MIDI parts as clips with their notes and the audio events
  /// as audio clips trimmed to their Cubase window. Nothing is ever written
  /// back to the `.cpr`. Returns an error message, or null on success.
  ///
  /// With [loadInstruments] (the default) the VSTi each instrument track hosts
  /// is loaded when the same plugin is installed here, and with [loadEffects]
  /// its insert chain is recreated from the same catalogue. Both take seconds
  /// per plugin, so they run *after* the import, one plugin per event-loop turn
  /// (see [pluginLoadPending]).
  ///
  /// Each loaded plug-in gets the exact state Cubase saved for it, so the
  /// project sounds like it does in Cubase. An insert whose state could not be
  /// restored is left *bypassed* instead of playing a wrong default preset.
  String? importCubaseProject(String path,
      {bool loadInstruments = true, bool loadEffects = true}) {
    if (isRecording) return L10n.current.serviceRecordingInProgress;
    final file = File(path);
    if (!file.existsSync()) {
      return L10n.current.serviceFileNotFound(path);
    }

    final CubaseProject cpr;
    try {
      cpr = CprReader(file.readAsBytesSync(), projectName: _cubaseBaseName(path))
          .read();
    } on FormatException catch (e) {
      return L10n.current.serviceNotCubaseProject(e.message);
    }
    if (cpr.tracks.isEmpty) {
      return L10n.current.serviceCprNoTracks;
    }

    final rate = sampleRate > 0 ? sampleRate : 48000.0;
    if (_engine.newProject(rate) != CadenceResult.ok) return _engine.lastError;
    if (!_engine.setTempoBpm(cpr.tempoBpm)) return _engine.lastError;
    if (!_engine.setTimeSignature(
        cpr.timeSigNumerator, cpr.timeSigDenominator)) {
      return _engine.lastError;
    }
    // Cubase's "Stereo Out" fader is part of the mix; a new project starts at
    // unity, so carry it over (see CubaseProject.masterVolumeDb).
    _engine.setGain(cpr.masterVolumeLinear);

    var created = 0;
    final createdIds = <int>[];
    final createdNames = <String>[];
    final pending =
        <({int trackId, int plugin, bool isFx, CubasePluginState? state})>[];
    for (final t in cpr.tracks) {
      final id = _engine.addTrack(
        t.name,
        created % 16,
        type: t.isInstrument ? 1 : 0,
      );
      if (id < 0) continue;
      if (t.volumeLinear != 1.0) {
        _engine.setTrackVolume(id, t.volumeLinear.clamp(0.0, 2.0));
      }
      if (t.pan != 0.0) _engine.setTrackPan(id, t.pan);
      if (t.mute) _engine.setTrackMute(id, true);
      if (t.solo) _engine.setTrackSolo(id, true);
      // Cubase instrument tracks carry the name of the VSTi they host. When
      // the same plugin is installed here, load it (after the import) with the
      // state Cubase saved, so the MIDI plays through the original instrument.
      final instrument = t.instrumentName;
      if (loadInstruments && instrument != null) {
        final slot = _matchingPlugin(instrument, instrument: true);
        if (slot >= 0) {
          pending.add((trackId: id, plugin: slot, isFx: false,
              state: t.instrumentState));
        }
      }
      // Inserts: the chain is recreated (same plug-ins, same order) with each
      // plug-in's saved state, so it sounds as in Cubase.
      if (loadEffects) {
        for (var i = 0; i < t.inserts.length; i++) {
          final slot = _matchingPlugin(t.inserts[i], instrument: false);
          if (slot >= 0) {
            pending.add((trackId: id, plugin: slot, isFx: true,
                state: i < t.insertStates.length ? t.insertStates[i] : null));
          }
        }
      }
      createdIds.add(id);
      createdNames.add(t.name);
      created++;
    }

    final notes = _importCubaseMidi(cpr, createdIds, createdNames);
    final audio = _importCubaseAudio(
        cpr, createdIds, createdNames, File(path).parent.path);

    // The output channel's own chain (Cubase's "Stereo Out" inserts).
    if (loadEffects) {
      for (var i = 0; i < cpr.masterInserts.length; i++) {
        final slot = _matchingPlugin(cpr.masterInserts[i], instrument: false);
        if (slot >= 0) {
          pending.add((trackId: _masterBus, plugin: slot, isFx: true,
              state: i < cpr.masterInsertStates.length
                  ? cpr.masterInsertStates[i]
                  : null));
        }
      }
    }

    _resetSelection();
    lastProjectPath = null; // Save still offers .ezmpp
    final bpm = cpr.tempoBpm == cpr.tempoBpm.roundToDouble()
        ? cpr.tempoBpm.round().toString()
        : cpr.tempoBpm.toStringAsFixed(2);
    final audioNote = audio.placed > 0
        ? L10n.current.serviceCubaseAudioClips('${audio.placed}')
        : '';
    final missingNote = audio.missing > 0
        ? L10n.current.serviceCubaseMissingAudio('${audio.missing}')
        : '';
    final summary =
        '${L10n.current.serviceCubaseImported('$created', '$notes')}'
        '$audioNote$missingNote · '
        '$bpm BPM · ${cpr.timeSigNumerator}/${cpr.timeSigDenominator}';
    statusMessage = summary;
    refresh();
    updateWindowTitle();
    // Loading a sampled VSTi costs seconds and a project can name dozens: do it
    // after the import, one plugin per turn, so the UI keeps painting.
    if (pending.isNotEmpty) {
      _pluginLoad = _loadPlugins(pending, summary);
    }
    return null;
  }

  /// Completes when the plug-in loading of the last Cubase import is done; null
  /// when no import is pending. Tests and tools await this.
  Future<void>? get pluginLoadPending => _pluginLoad;
  Future<void>? _pluginLoad;
  bool _disposed = false;

  /// Slot of the mixer's output channel, used for master-chain actions.
  static const int _masterBus = -1;

  /// Plugin list index of an installed plug-in matching a Cubase name — an
  /// *instrument* (VSTi) or an *effect* — or -1 when none matches, in which
  /// case the engine keeps its internal synth / adds no insert.
  ///
  /// Cubase stores VST3 processor state, so when a name exists as both VST3
  /// and AudioUnit the VST3 instance wins — an AU's state lives in a different
  /// format and could not be restored.
  int _matchingPlugin(String cubaseName, {required bool instrument}) {
    if (!_pluginsScanned) refreshPluginList();
    final installed = <String>[];
    final indices = <int>[];
    for (final wantVst3 in const [true, false]) {
      for (final p in _plugins) {
        if (p.isInstrument != instrument) continue;
        if ((p.format == 'VST3') != wantVst3) continue;
        installed.add(p.name);
        indices.add(p.index);
      }
    }
    final hit = matchPluginName(cubaseName, installed, fuzzy: instrument);
    return hit < 0 ? -1 : indices[hit];
  }

  /// Format ('VST3', 'AudioUnit', …) of an installed plug-in list index.
  String _pluginFormat(int index) =>
      index >= 0 && index < _plugins.length ? _plugins[index].format : '';

  /// Recreates the plug-ins an import matched — instruments and insert chains —
  /// each with the state Cubase saved for it, yielding between them so the event
  /// loop (and the UI) runs in between; reports progress in [statusMessage] and
  /// restores [summary] at the end. Instantiating a sampled VSTi costs seconds,
  /// hence the yield.
  Future<void> _loadPlugins(
    List<({int trackId, int plugin, bool isFx, CubasePluginState? state})>
        pending,
    String summary,
  ) async {
    // Yield first: instantiating the first plugin (cold) is the slowest of the
    // lot, and it must not delay the import's return.
    await Future<void>.delayed(Duration.zero);
    var loaded = 0;
    var restored = 0;
    for (var i = 0; i < pending.length; i++) {
      if (_disposed) return;
      final a = pending[i];
      final state = a.state;
      // Only a VST3 instance understands the state Cubase stored; an AU
      // matched by the same name is left on its own default.
      final vst3 = _pluginFormat(a.plugin) == 'VST3';
      if (!a.isFx) {
        _engine.trackSetInstrumentPlugin(a.trackId, a.plugin);
        if (vst3 && state != null && !state.isEmpty) {
          // Cubase saved this VSTi's state: hand it back so the patch the
          // project used comes up, not the plug-in's own default.
          if (setTrackInstrumentState(
              a.trackId, state.component, state.controller)) {
            restored++;
          }
        }
        loaded++;
      } else if (a.trackId == _masterBus) {
        // Master inserts are not instantiated by the engine yet: the slot is
        // recorded (so the chain is visible) but there is nothing to restore.
        final slot = _engine.masterFxCount;
        _engine.masterAddFx(a.plugin);
        if (_engine.masterFxCount > slot) {
          _engine.masterSetFxBypass(slot, true);
          loaded++;
        }
      } else {
        final slot = _engine.trackFxCount(a.trackId);
        _engine.trackAddFx(a.trackId, a.plugin);
        if (_engine.trackFxCount(a.trackId) > slot) {
          var applied = false;
          if (vst3 && state != null && !state.isEmpty) {
            applied = _engine.trackSetFxState(
                    a.trackId, slot, state.component, state.controller) ==
                CadenceResult.ok;
            if (applied) restored++;
          }
          // A plug-in whose history we could not restore would come up on its
          // default preset and change the mix (plainly wrong for a de-noiser or
          // a compressor), so it stays bypassed — the user enables it by hand.
          _engine.trackSetFxBypass(a.trackId, slot, !applied);
          loaded++;
        }
      }
      statusMessage = L10n.current
          .serviceLoadingCubasePlugins('${i + 1}', '${pending.length}');
      refresh();
      await Future<void>.delayed(Duration.zero);
    }
    if (_disposed) return;
    final stateNote =
        restored > 0 ? L10n.current.serviceCubasePluginsRestored('$restored') : '';
    statusMessage =
        '$summary · ${L10n.current.servicePluginsLoadedCount('$loaded')}$stateNote';
    refresh();
  }

  /// Recreates Cubase MIDI parts as Cadence MIDI clips (one clip per part).
  /// Returns the number of notes imported.
  int _importCubaseMidi(
    CubaseProject cpr,
    List<int> trackIds,
    List<String> trackNames,
  ) {
    if (cpr.midiParts.isEmpty || trackIds.isEmpty) return 0;
    const ticksPerBeat = 480.0;
    if (cpr.tempoBpm <= 0) return 0;
    final secondsPerTick = 60.0 / (cpr.tempoBpm * ticksPerBeat);

    // Match each part to a track, ranking by base name so parts sharing a name
    // (several MIDI tracks on one VSTi, or a track duplicated with " (D)")
    // spread over the matching tracks in order.
    final targets = assignMidiPartsToTracks(cpr.midiParts, trackNames);
    var imported = 0;

    for (var i = 0; i < cpr.midiParts.length; i++) {
      final part = cpr.midiParts[i];
      if (part.notes.isEmpty) continue;
      final target = targets[i];
      if (target < 0 || target >= trackIds.length) continue;
      final trackId = trackIds[target];

      final startSec = part.startTicks * secondsPerTick;
      final lenSec = part.lengthTicks * secondsPerTick;
      final clipId = _engine.addMidiClip(
        trackId,
        startSec,
        lenSec > 0.05 ? lenSec : 0.05,
      );
      if (clipId < 0) continue;
      for (final n in part.notes) {
        if (n.pitch < 0 || n.pitch > 127) continue;
        final relStart = (n.startTicks - part.startTicks) * secondsPerTick;
        var noteLen = n.lengthTicks * secondsPerTick;
        if (noteLen < 0.01) noteLen = 0.01;
        _engine.midiClipAddNote(
            clipId, n.pitch, n.velocity / 127.0, relStart, noteLen);
        imported++;
      }
    }
    return imported;
  }

  /// Recreates Cubase audio events as Cadence audio clips (one clip per event),
  /// resolving each source file next to the `.cpr`. Cubase stores event
  /// positions in ticks but lengths and source offsets in sample frames at the
  /// project sample rate, which [CubaseProject.sampleRate] reports.
  /// Returns how many clips were placed and how many events had no local file.
  ({int placed, int missing}) _importCubaseAudio(
    CubaseProject cpr,
    List<int> trackIds,
    List<String> trackNames,
    String projectDir,
  ) {
    const none = (placed: 0, missing: 0);
    if (cpr.audioEvents.isEmpty || trackIds.isEmpty) return none;
    const ticksPerBeat = 480.0;
    if (cpr.tempoBpm <= 0 || cpr.sampleRate <= 0) return none;
    final secondsPerTick = 60.0 / (cpr.tempoBpm * ticksPerBeat);

    final targets = assignAudioEventsToTracks(cpr.audioEvents, trackNames);
    var placed = 0;
    var missing = 0;
    for (var i = 0; i < cpr.audioEvents.length; i++) {
      final event = cpr.audioEvents[i];
      final name = event.sourceFile;
      if (name == null || name.isEmpty) continue;
      final path = _locateCubaseAudio(projectDir, name);
      if (path == null) {
        missing++;
        continue;
      }
      final target = targets[i];
      if (target < 0 || target >= trackIds.length) continue;
      final clipId = _engine.addClip(
          trackIds[target], path, 0.0, event.gain.clamp(0.0, 4.0));
      if (clipId < 0) {
        missing++;
        continue;
      }
      _engine.setClipBounds(
        clipId,
        event.startTicks * secondsPerTick,
        event.sourceOffsetFrames / cpr.sampleRate,
        event.lengthFrames / cpr.sampleRate,
      );
      placed++;
    }
    return (placed: placed, missing: missing);
  }

  /// Locates a pool file next to the project. Cubase keeps audio in an `Audio`
  /// subfolder; the fallback search is case-insensitive because the stored name
  /// comes from whatever machine authored the project.
  static String? _locateCubaseAudio(String projectDir, String name) {
    const subdirs = ['', 'Audio', 'audio', 'Media', 'media'];
    for (final sub in subdirs) {
      final path = sub.isEmpty ? '$projectDir/$name' : '$projectDir/$sub/$name';
      if (File(path).existsSync()) return path;
    }
    final lower = name.toLowerCase();
    for (final sub in subdirs) {
      final dir = Directory(sub.isEmpty ? projectDir : '$projectDir/$sub');
      if (!dir.existsSync()) continue;
      for (final entry in dir.listSync()) {
        if (entry is File &&
            entry.uri.pathSegments.last.toLowerCase() == lower) {
          return entry.path;
        }
      }
    }
    return null;
  }

  static String _cubaseBaseName(String path) {
    final name = path.split('/').last;
    return name.toLowerCase().endsWith('.cpr')
        ? name.substring(0, name.length - 4)
        : name;
  }

  /// Opens a .cadence file and resets the UI selection state. Returns an
  /// error message or null.
  /// Abre CUALQUIER fichero soportado por extensión:
  /// .ezmpp proyecto · .mid/.midi import (Cubase/Live/Logic) ·
  /// .cpr lector propio (y, si falla, un .mid hermano) · .wav/.aiff audio.
  String? openAnyFile(String path) {
    final lower = path.toLowerCase();
    if (lower.endsWith('.ezmpp')) {
      return loadProjectFile(path);
    }
    if (lower.endsWith('.mid') || lower.endsWith('.midi')) {
      if (_engine.importMidiProject(path) != 0) return _engine.lastError;
      refresh();
      return null;
    }
    if (lower.endsWith('.cpr')) {
      // Read the Cubase project directly (pure-Dart reader, read-only).
      final direct = importCubaseProject(path);
      if (direct == null) return null;
      // If the .cpr cannot be parsed, fall back to Cubase's own MIDI export
      // when it was saved next to the project with the same name.
      final base = path.substring(0, path.length - 4);
      for (final ext in ['.mid', '.midi']) {
        final candidate = '$base$ext';
        if (File(candidate).existsSync()) {
          if (_engine.importMidiProject(candidate) != 0) {
            return _engine.lastError;
          }
          refresh();
          return null;
        }
      }
      return direct;
    }
    if (lower.endsWith('.wav') ||
        lower.endsWith('.aiff') ||
        lower.endsWith('.aif')) {
      if (_engine.importAudioFile(path) != 0) return _engine.lastError;
      refresh();
      return null;
    }
    return L10n.current.serviceUnrecognizedFormat(path.split('/').last);
  }

  /// Explorador nativo (NSOpenPanel) arrancando en la carpeta de
  /// proyectos. Devuelve el error o null ("" = cancelado).
  String? pickAndOpenFile() {
    final path =
        _engine.pickOpenFile(kDefaultProjectsDir, '*.ezmpp;*.mid;*.midi;*.cpr;*.wav;*.aiff;*.aif');
    if (path.isEmpty) return null; // cancelled
    return openAnyFile(path);
  }

  /// Importa un .mid como proyecto completo. Devuelve el error o null.
  String? importMidiProject(String path) {
    if (_engine.importMidiProject(path) != 0) return _engine.lastError;
    refresh();
    return null;
  }

  /// Importa una carpeta de stems como pistas de audio.
  String? importStemsFolder(String path) {
    if (_engine.importStemsFolder(path) != 0) return _engine.lastError;
    refresh();
    return null;
  }

  String? loadProjectFile(String path) {
    if (isRecording) return L10n.current.serviceRecordingInProgress;
    final result = _engine.loadProject(path);
    if (result != CadenceResult.ok) return _engine.lastError;
    _resetSelection();
    lastProjectPath = path;
    statusMessage = L10n.current.serviceProjectLoaded(path);
    refresh();
    updateWindowTitle();
    return null;
  }

  /// Saves to [name] inside the projects folder (sanitized), remembers it
  /// as the current file. Returns an error message or null. Tests pass
  /// [inDir] so they never pollute the user's projects folder.
  String? saveProjectAs(String name, {Directory? inDir}) {
    final safe = name
        .trim()
        .replaceAll(RegExp(r'[/:\\]'), '-');
    if (safe.isEmpty) return L10n.current.serviceProjectNeedsName;
    final dir = inDir ??
        Directory('/Users/ivan/Music/EZMusicProduction/Projects');
    if (!dir.existsSync()) dir.createSync(recursive: true);
    final path = '${dir.path}/$safe.ezmpp';
    final result = _engine.saveProject(path);
    if (result != CadenceResult.ok) return _engine.lastError;
    lastProjectPath = path;
    statusMessage = L10n.current.serviceSaved(path);
    refresh();
    updateWindowTitle();
    return null;
  }

  void _resetSelection() {
    selectedClipId = null;
    selectedTrackId = null;
    editorClipId = null;
    _mixerOpen = false;
    _automationOpen = false;
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
    _disposed = true;
    _engine.dispose();
    super.dispose();
  }

}
