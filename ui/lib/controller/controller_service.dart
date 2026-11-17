// Hardware controller layer (MiniLab 3 first, generic MIDI learn too).
//
// The engine mirrors every incoming MIDI message into a small ring buffer
// (`cadence_engine_poll_midi_events`). This service drains that buffer on a
// UI timer and maps CCs to the selected track's device parameters, volume,
// pan and master gain. Absolute controls use soft takeover so a physical knob
// never jumps a parameter; relative encoders are supported too. Learned pads,
// buttons and CCs can also fire transport/performance actions.
//
// It is deliberately UI-side: mapping rules, persistence and the performance
// panel can evolve without touching the audio graph. Parameter writes go
// through the same C ABI the device editor uses.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

import '../engine/cadence_bindings.dart';
import '../engine/engine_service.dart';
import '../engine/project_snapshot.dart';
import '../ui/design.dart';

/// Relative encoders in two's-complement mode: 1..63 clockwise, 65..127
/// counter-clockwise, 0/64 no movement.
double relativeEncoderDelta(int value) {
  if (value == 0 || value == 64) return 0;
  final raw = value < 64 ? value : value - 128;
  return raw / 64.0;
}

/// Soft takeover rule: an absolute CC may change the target only after it has
/// caught the current value (or when the control was already armed).
bool softTakeoverAllows(
  double current,
  double incoming,
  bool armed, {
  double threshold = 0.02,
}) =>
    armed || (incoming - current).abs() <= threshold;

/// One physical control on the controller.
enum ControllerControlKind { encoder, fader }

enum ControllerTargetKind {
  deviceParam,
  trackVolume,
  trackPan,
  masterVolume,
}

class ControllerControl {
  ControllerControl({
    required this.id,
    required this.kind,
    required this.index,
    required this.cc,
    this.aliases = const [],
    required this.targetKind,
    this.paramIndex = -1,
  });

  final String id;
  final ControllerControlKind kind;
  final int index;

  /// Primary CC number, or -1 when the control is not assigned.
  int cc;

  /// Alternate CCs used by the MiniLab 3 DAW/Arturia modes. Cleared when the
  /// user learns a custom CC.
  List<int> aliases;

  final ControllerTargetKind targetKind;
  final int paramIndex;

  bool armed = false;
  bool catching = false;
  double ccValue = 0;
  double targetValue = 0;
  String targetName = '';
  String valueText = '—';

  bool matches(int controller) =>
      controller >= 0 && (cc == controller || aliases.contains(controller));
}

/// A device that can receive mapped parameters: a track FX, the instrument
/// slot, or a master FX slot.
class ControllerDevice {
  const ControllerDevice({
    required this.slotKind,
    required this.slotIndex,
    required this.label,
  });

  final int slotKind; // 0 = track FX, 1 = instrument, 2 = master FX
  final int slotIndex;
  final String label;

  String get key => '$slotKind:$slotIndex';
}

/// Transport and performance actions that can be triggered from a controller
/// pad, button or CC. They are separate from the continuous control map.
enum ControllerAction {
  playPause,
  stop,
  record,
  metronome,
  tapTempo,
  undo,
  muteTrack,
  soloTrack,
}

enum ControllerTriggerKind { cc, note }

class ControllerTrigger {
  const ControllerTrigger(this.kind, this.number);

  final ControllerTriggerKind kind;
  final int number;

  String get label =>
      kind == ControllerTriggerKind.cc ? 'CC $number' : 'Note $number';

  Map<String, Object> toJson() => {
        'type': kind.name,
        'number': number,
      };

  static ControllerTrigger? fromJson(Object? value) {
    if (value is! Map) return null;
    final type = value['type'];
    final number = value['number'];
    if (number is! num) return null;
    final n = number.toInt();
    if (n < 0 || n > 127) return null;
    if (type == 'cc') return ControllerTrigger(ControllerTriggerKind.cc, n);
    if (type == 'note') return ControllerTrigger(ControllerTriggerKind.note, n);
    return null;
  }
}

class ControllerActionBinding {
  ControllerActionBinding(this.action, {this.trigger});

  final ControllerAction action;
  ControllerTrigger? trigger;
  DateTime? lastTrigger;
}

/// Persisted controller preferences. Kept outside the project (like the
/// locale/remote settings) so a controller map survives opening another song.
class ControllerSettings {
  ControllerSettings({
    required this.encoderCcs,
    required this.faderCcs,
    required this.relative,
    required this.softTakeover,
    this.actions = const {},
  });

  // Verified against a physical MiniLab 3: encoders 1..8 emit these CCs in
  // this exact order, and faders 1..4 emit the second list. The encoders send
  // absolute values by default (0..127), so relative mode is opt-in for
  // users who reprogram them in Arturia MIDI Control Center.
  static const defaultEncoderCcs = <int>[74, 71, 76, 77, 93, 18, 19, 16];
  static const defaultFaderCcs = <int>[82, 83, 85, 17];

  final List<int> encoderCcs;
  final List<int> faderCcs;
  final bool relative;
  final bool softTakeover;
  final Map<ControllerAction, ControllerTrigger> actions;

  factory ControllerSettings.defaults() => ControllerSettings(
        encoderCcs: List<int>.from(defaultEncoderCcs),
        faderCcs: List<int>.from(defaultFaderCcs),
        relative: false,
        softTakeover: true,
      );

  static File get file {
    final home = Platform.environment['HOME'] ?? '.';
    return File('$home/Music/Cadence/controller_settings.json');
  }

  static ControllerSettings load({File? from}) {
    final source = from ?? file;
    try {
      if (!source.existsSync()) return ControllerSettings.defaults();
      final raw = source.readAsStringSync();
      if (raw.trim().isEmpty) return ControllerSettings.defaults();
      final data = jsonDecode(raw);
      if (data is! Map) return ControllerSettings.defaults();
      List<int> readCcs(Object? value, List<int> fallback) {
        if (value is! List || value.length != fallback.length) {
          return List<int>.from(fallback);
        }
        final out = <int>[];
        for (final item in value) {
          if (item is! num) return List<int>.from(fallback);
          final cc = item.toInt();
          if (cc < -1 || cc > 127) return List<int>.from(fallback);
          out.add(cc);
        }
        return out;
      }

      final actions = <ControllerAction, ControllerTrigger>{};
      final rawActions = data['actions'];
      if (rawActions is Map) {
        for (final action in ControllerAction.values) {
          final trigger =
              ControllerTrigger.fromJson(rawActions[action.name]);
          if (trigger != null) actions[action] = trigger;
        }
      }

      return ControllerSettings(
        encoderCcs: readCcs(data['encoders'], defaultEncoderCcs),
        faderCcs: readCcs(data['faders'], defaultFaderCcs),
        relative: data['relative'] == true,
        softTakeover: data['softTakeover'] != false,
        actions: actions,
      );
    } catch (_) {
      return ControllerSettings.defaults();
    }
  }

  void save({File? to}) {
    final target = to ?? file;
    try {
      target.parent.createSync(recursive: true);
      final data = <String, Object>{
        'encoders': encoderCcs,
        'faders': faderCcs,
        'relative': relative,
        'softTakeover': softTakeover,
        'actions': {
          for (final entry in actions.entries)
            entry.key.name: entry.value.toJson(),
        },
      };
      const encoder = JsonEncoder.withIndent('  ');
      target.writeAsStringSync('${encoder.convert(data)}\n');
    } catch (error) {
      debugPrint('ControllerSettings.save: $error');
    }
  }
}

/// A resolved target (device parameter or mixer control).
abstract class _ControllerTarget {
  double read();
  void write(double normalized);
  String get parameterName;
  String get valueText;
  bool get touchesMixer;
}

class _ParamTarget implements _ControllerTarget {
  _ParamTarget(this.service, this.trackId, this.slotKind, this.slotIndex,
      this.paramIndex);

  final EngineService service;
  final int trackId;
  final int slotKind;
  final int slotIndex;
  final int paramIndex;

  @override
  double read() => service
      .pluginParamValue(trackId, slotKind, slotIndex, paramIndex)
      .clamp(0.0, 1.0);

  @override
  void write(double normalized) => service.setPluginParamValue(
      trackId, slotKind, slotIndex, paramIndex, normalized.clamp(0.0, 1.0));

  @override
  String get parameterName => service.pluginParamName(
      trackId, slotKind, slotIndex, paramIndex);

  @override
  String get valueText {
    final text =
        service.pluginParamText(trackId, slotKind, slotIndex, paramIndex);
    if (text.trim().isNotEmpty) return text;
    return '${(read() * 100).round()}%';
  }

  @override
  bool get touchesMixer => false;
}

class _TrackVolumeTarget implements _ControllerTarget {
  _TrackVolumeTarget(this.service, this.trackId);

  final EngineService service;
  final int trackId;

  @override
  double read() => gainToFader(service.liveTrackVolume(trackId));

  @override
  void write(double normalized) =>
      service.liveSetTrackVolume(trackId, faderToGain(normalized));

  @override
  String get parameterName => 'Track volume';

  @override
  String get valueText => formatDb(service.liveTrackVolume(trackId));

  @override
  bool get touchesMixer => true;
}

class _TrackPanTarget implements _ControllerTarget {
  _TrackPanTarget(this.service, this.trackId);

  final EngineService service;
  final int trackId;

  @override
  double read() => ((service.liveTrackPan(trackId) + 1) / 2).clamp(0.0, 1.0);

  @override
  void write(double normalized) =>
      service.liveSetTrackPan(trackId, normalized.clamp(0.0, 1.0) * 2 - 1);

  @override
  String get parameterName => 'Track pan';

  @override
  String get valueText => formatPan(service.liveTrackPan(trackId));

  @override
  bool get touchesMixer => true;
}

class _MasterTarget implements _ControllerTarget {
  _MasterTarget(this.service);

  final EngineService service;

  @override
  double read() => gainToFader(service.liveMasterGain);

  @override
  void write(double normalized) =>
      service.liveSetMasterGain(faderToGain(normalized));

  @override
  String get parameterName => 'Master volume';

  @override
  String get valueText => formatDb(service.liveMasterGain);

  @override
  bool get touchesMixer => true;
}

class ControllerService extends ChangeNotifier {
  ControllerService(
    this.engine, {
    ControllerSettings? settings,
    File? settingsFile,
  }) : _settingsFile = settingsFile {
    final loaded = settings ?? ControllerSettings.load(from: settingsFile);
    _relative = loaded.relative;
    _softTakeover = loaded.softTakeover;
    for (var i = 0; i < 8; i++) {
      final cc = i < loaded.encoderCcs.length
          ? loaded.encoderCcs[i]
          : ControllerSettings.defaultEncoderCcs[i];
      controls.add(ControllerControl(
        id: 'enc${i + 1}',
        kind: ControllerControlKind.encoder,
        index: i,
        cc: cc,
        aliases: _encoderAliasesFor(i, cc),
        targetKind: ControllerTargetKind.deviceParam,
        paramIndex: i,
      ));
    }
    for (var i = 0; i < 4; i++) {
      final cc = i < loaded.faderCcs.length
          ? loaded.faderCcs[i]
          : ControllerSettings.defaultFaderCcs[i];
      controls.add(_makeFader(i, cc));
    }
    for (final action in ControllerAction.values) {
      actionBindings.add(
        ControllerActionBinding(action, trigger: loaded.actions[action]),
      );
    }
    _syncConsumedNotes();
  }

  final EngineService engine;
  final File? _settingsFile;
  final List<ControllerControl> controls = <ControllerControl>[];
  final List<ControllerActionBinding> actionBindings =
      <ControllerActionBinding>[];
  final Set<int> _consumedNotes = <int>{};
  final List<DateTime> _tapTimes = <DateTime>[];

  Timer? _timer;
  bool _relative = false;
  bool _softTakeover = true;
  int? _lastTrackId;
  ControllerDevice? _device;
  List<ControllerDevice>? _deviceCache;
  String? _paramMapKey;
  List<int> _paramMap = const [];
  ControllerControl? _learnTarget;
  ControllerActionBinding? _learnAction;
  EngineMidiEvent? _lastEvent;
  EngineMidiEvent? _lastControllerEvent;
  int _pumpCount = 0;
  bool _dirty = false;
  bool _mixerDirty = false;

  static const _encoderAliases = <List<int>>[
    [86],
    [87],
    [89],
    [90],
    [110],
    [111],
    [116],
    [117],
  ];
  static const _faderAliases = <List<int>>[
    [14],
    [15],
    [30],
    [31],
  ];

  bool get relative => _relative;
  bool get softTakeover => _softTakeover;
  ControllerControl? get learnTarget => _learnTarget;
  ControllerActionBinding? get learnAction => _learnAction;
  EngineMidiEvent? get lastEvent => _lastEvent;
  EngineMidiEvent? get lastControllerEvent => _lastControllerEvent;

  List<ControllerControl> get encoders =>
      controls.where((c) => c.kind == ControllerControlKind.encoder).toList();
  List<ControllerControl> get faders =>
      controls.where((c) => c.kind == ControllerControlKind.fader).toList();

  void start() {
    _timer ??=
        Timer.periodic(const Duration(milliseconds: 16), (_) => _pump());
  }

  void stop() {
    _timer?.cancel();
    _timer = null;
  }

  @override
  void dispose() {
    stop();
    for (final note in _consumedNotes) {
      engine.setControllerNoteConsumed(note, false);
    }
    _consumedNotes.clear();
    super.dispose();
  }

  ControllerControl? controlById(String id) {
    for (final control in controls) {
      if (control.id == id) return control;
    }
    return null;
  }

  /// Track used by the controller. Falls back to the first instrument track
  /// (then the first track) so hardware works even without a timeline click.
  TrackInfo? get activeTrack {
    final selected = engine.selectedTrack;
    if (selected != null) return selected;
    final tracks = engine.snapshot.tracks;
    for (final track in tracks) {
      if (track.type == 1) return track;
    }
    return tracks.isEmpty ? null : tracks.first;
  }

  List<ControllerDevice> get availableDevices =>
      _deviceCache ??= _buildDevices();

  List<ControllerDevice> _buildDevices() {
    final track = activeTrack;
    if (track == null) return const [];
    final out = <ControllerDevice>[];
    final instrumentCount = engine.pluginParamCount(track.id, 1, 0);
    if (instrumentCount > 0) {
      out.add(ControllerDevice(
        slotKind: 1,
        slotIndex: 0,
        label: track.instrumentName.isEmpty
            ? 'Internal instrument'
            : track.instrumentName,
      ));
    }
    for (var i = 0; i < track.fxNames.length; i++) {
      if (engine.pluginParamCount(track.id, 0, i) > 0) {
        out.add(ControllerDevice(
          slotKind: 0,
          slotIndex: i,
          label: track.fxNames[i],
        ));
      }
    }
    final masterFx = engine.snapshot.masterFxNames;
    for (var i = 0; i < masterFx.length; i++) {
      if (engine.pluginParamCount(track.id, 2, i) > 0) {
        out.add(ControllerDevice(
          slotKind: 2,
          slotIndex: i,
          label: 'Master · ${masterFx[i]}',
        ));
      }
    }
    return out;
  }

  /// Currently focused device, falling back to the first available one.
  ControllerDevice? get currentDevice {
    final devices = availableDevices;
    if (devices.isEmpty) {
      _device = null;
      return null;
    }
    final current = _device;
    if (current != null) {
      for (final device in devices) {
        if (device.key == current.key) return current;
      }
    }
    final next = devices.first;
    _device = next;
    _paramMapKey = null;
    _resetArmed();
    return next;
  }

  void setDevice(ControllerDevice? device) {
    if (_device?.key == device?.key) return;
    _device = device;
    _paramMapKey = null;
    _resetArmed();
    _refreshReadouts();
    notifyListeners();
  }

  /// Called after switching MIDI inputs: physical positions changed, so every
  /// absolute control must catch its target again.
  void resetTakeover() {
    _resetArmed();
    _dirty = true;
    notifyListeners();
  }

  int get currentParamCount => _mappableParamIndices().length;

  String? parameterName(ControllerControl control) {
    if (control.targetKind != ControllerTargetKind.deviceParam) return null;
    final device = currentDevice;
    final track = activeTrack;
    if (device == null || track == null) return null;
    final indices = _mappableParamIndices();
    if (control.paramIndex < 0 || control.paramIndex >= indices.length) {
      return null;
    }
    final actual = indices[control.paramIndex];
    if (control.kind == ControllerControlKind.fader &&
        (engine.pluginParamFlags(
                  track.id, device.slotKind, device.slotIndex, actual) &
              3) !=
            0) {
      return null;
    }
    return engine.pluginParamName(
        track.id, device.slotKind, device.slotIndex, actual);
  }

  double? parameterDefault(ControllerControl control) {
    if (control.targetKind != ControllerTargetKind.deviceParam) return null;
    final device = currentDevice;
    final track = activeTrack;
    if (device == null || track == null) return null;
    final indices = _mappableParamIndices();
    if (control.paramIndex < 0 || control.paramIndex >= indices.length) {
      return null;
    }
    return engine.pluginParamDefault(
        track.id, device.slotKind, device.slotIndex,
        indices[control.paramIndex]);
  }

  /// Parameters exposed to the controller, in engine order. Bypass is omitted
  /// on purpose: an encoder or fader should never switch the whole device off
  /// by accident. Empty placeholder parameters are skipped too.
  List<int> _mappableParamIndices() {
    final device = currentDevice;
    final track = activeTrack;
    if (device == null || track == null) return const [];
    final key = _deviceMapKey(track, device);
    if (key == _paramMapKey) return _paramMap;

    final count =
        engine.pluginParamCount(track.id, device.slotKind, device.slotIndex);
    final indices = <int>[];
    for (var i = 0; i < count; i++) {
      final name =
          engine.pluginParamName(track.id, device.slotKind, device.slotIndex, i);
      final trimmed = name.trim();
      if (trimmed.isEmpty || trimmed.toLowerCase() == 'bypass') continue;
      indices.add(i);
    }
    _paramMapKey = key;
    _paramMap = indices;
    return indices;
  }

  String _deviceMapKey(TrackInfo track, ControllerDevice device) {
    switch (device.slotKind) {
      case 1:
        return '${track.id}:1:${engine.trackInstrumentUid(track.id)}';
      case 2:
        return '2:${device.slotIndex}:${engine.masterFxUidAt(device.slotIndex)}';
      default:
        return '${track.id}:0:${device.slotIndex}:'
            '${engine.trackFxUidAt(track.id, device.slotIndex)}';
    }
  }

  bool get isMiniLab {
    final index = engine.selectedMidiInputIndex;
    if (index < 0) return false;
    final names = engine.midiInputNames;
    if (index >= names.length) return false;
    return names[index].toLowerCase().contains('minilab');
  }

  String get inputName {
    final index = engine.selectedMidiInputIndex;
    if (index < 0) return '';
    final names = engine.midiInputNames;
    return index < names.length ? names[index] : '';
  }

  void setRelative(bool value) {
    if (_relative == value) return;
    _relative = value;
    _resetArmed();
    _saveSettings();
    notifyListeners();
  }

  void setSoftTakeover(bool value) {
    if (_softTakeover == value) return;
    _softTakeover = value;
    _resetArmed();
    _saveSettings();
    notifyListeners();
  }

  void startLearn(ControllerControl control) {
    _learnAction = null;
    _learnTarget = _learnTarget?.id == control.id ? null : control;
    notifyListeners();
  }

  void startActionLearn(ControllerActionBinding binding) {
    _learnTarget = null;
    _learnAction =
        _learnAction?.action == binding.action ? null : binding;
    notifyListeners();
  }

  void clearAction(ControllerActionBinding binding) {
    if (binding.trigger == null) return;
    binding.trigger = null;
    binding.lastTrigger = null;
    if (_learnAction?.action == binding.action) _learnAction = null;
    _syncConsumedNotes();
    _saveSettings();
    _dirty = true;
    notifyListeners();
  }

  void cancelLearn() {
    if (_learnTarget == null && _learnAction == null) return;
    _learnTarget = null;
    _learnAction = null;
    notifyListeners();
  }

  /// Runs an action from the panel (mouse) or a mapped controller trigger.
  void runAction(ControllerActionBinding binding) {
    switch (binding.action) {
      case ControllerAction.playPause:
        if (engine.isPlaying) {
          engine.pause();
        } else {
          engine.play();
        }
        break;
      case ControllerAction.stop:
        engine.stop();
        break;
      case ControllerAction.record:
        if (engine.isRecording) {
          engine.stopRecord();
        } else {
          engine.startRecord();
        }
        break;
      case ControllerAction.metronome:
        engine.toggleMetronome();
        break;
      case ControllerAction.tapTempo:
        tapTempo();
        break;
      case ControllerAction.undo:
        engine.undo();
        break;
      case ControllerAction.muteTrack:
        final track = activeTrack;
        if (track != null) engine.setTrackMute(track.id, !track.mute);
        break;
      case ControllerAction.soloTrack:
        final track = activeTrack;
        if (track != null) engine.setTrackSolo(track.id, !track.solo);
        break;
    }
    _dirty = true;
    notifyListeners();
  }

  /// Tap tempo: the average of the last up-to-four intervals, reset after a
  /// two-second pause. Clamped to the project's supported 20–300 BPM.
  void tapTempo() {
    final now = DateTime.now();
    if (_tapTimes.isNotEmpty &&
        now.difference(_tapTimes.last) > const Duration(seconds: 2)) {
      _tapTimes.clear();
    }
    _tapTimes.add(now);
    while (_tapTimes.length > 5) {
      _tapTimes.removeAt(0);
    }
    if (_tapTimes.length < 2) return;
    var total = 0.0;
    for (var i = 1; i < _tapTimes.length; i++) {
      total += _tapTimes[i].difference(_tapTimes[i - 1]).inMicroseconds /
          Duration.microsecondsPerSecond;
    }
    final average = total / (_tapTimes.length - 1);
    if (average <= 0) return;
    final bpm = (60.0 / average).clamp(20.0, 300.0);
    engine.setTempo(bpm);
    _dirty = true;
    notifyListeners();
  }

  void _syncConsumedNotes() {
    final next = <int>{
      for (final binding in actionBindings)
        if (binding.trigger?.kind == ControllerTriggerKind.note)
          binding.trigger!.number,
    };
    for (final note in _consumedNotes.difference(next)) {
      engine.setControllerNoteConsumed(note, false);
    }
    for (final note in next.difference(_consumedNotes)) {
      engine.setControllerNoteConsumed(note, true);
    }
    _consumedNotes
      ..clear()
      ..addAll(next);
  }

  /// Re-reads all mapped values so the panel reflects editor/automation edits.
  void refreshValues() {
    _refreshReadouts(forceDirty: true);
  }

  /// Direct mouse/touch edit from the performance panel (same target path as
  /// a hardware CC).
  void setControlValue(ControllerControl control, double normalized) {
    final target = _targetFor(control);
    if (target == null) return;
    final value = normalized.clamp(0.0, 1.0);
    target.write(value);
    control.targetValue = value;
    control.valueText = target.valueText;
    control.ccValue = value;
    control.armed = true;
    control.catching = false;
    if (target.touchesMixer) _mixerDirty = true;
    _dirty = true;
    notifyListeners();
  }

  /// Double-click reset value used by knobs/faders in the panel.
  double defaultFor(ControllerControl control) {
    switch (control.targetKind) {
      case ControllerTargetKind.deviceParam:
        return parameterDefault(control) ?? 0.5;
      case ControllerTargetKind.trackVolume:
      case ControllerTargetKind.masterVolume:
        return gainToFader(1.0);
      case ControllerTargetKind.trackPan:
        return 0.5;
    }
  }

  void resetControl(ControllerControl control) =>
      setControlValue(control, defaultFor(control));

  bool _isActionTriggerEvent(EngineMidiEvent event) =>
      (event.type == EngineMidiEvent.controller && event.data2 >= 64) ||
      (event.type == EngineMidiEvent.noteOn && event.data2 > 0);

  ControllerTrigger? _triggerFromEvent(EngineMidiEvent event) {
    if (event.type == EngineMidiEvent.controller) {
      return ControllerTrigger(ControllerTriggerKind.cc, event.data1);
    }
    if (event.type == EngineMidiEvent.noteOn) {
      return ControllerTrigger(ControllerTriggerKind.note, event.data1);
    }
    return null;
  }

  ControllerActionBinding? _actionForEvent(EngineMidiEvent event) {
    for (final binding in actionBindings) {
      final trigger = binding.trigger;
      if (trigger == null) continue;
      if (trigger.kind == ControllerTriggerKind.cc &&
          event.type == EngineMidiEvent.controller &&
          event.data1 == trigger.number &&
          event.data2 >= 64) {
        return binding;
      }
      if (trigger.kind == ControllerTriggerKind.note &&
          event.type == EngineMidiEvent.noteOn &&
          event.data1 == trigger.number &&
          event.data2 > 0) {
        return binding;
      }
    }
    return null;
  }

  void _pump() {
    final events = engine.pollMidiEvents(256);
    var mixerTouched = false;
    for (final event in events) {
      _lastEvent = event;
      if (event.type == EngineMidiEvent.controller) _lastControllerEvent = event;

      final learningAction = _learnAction;
      if (learningAction != null && _isActionTriggerEvent(event)) {
        final trigger = _triggerFromEvent(event);
        if (trigger != null) {
          learningAction.trigger = trigger;
          learningAction.lastTrigger = null;
          _learnAction = null;
          _syncConsumedNotes();
          _saveSettings();
          _dirty = true;
        }
        continue;
      }

      final action = _actionForEvent(event);
      if (action != null) {
        final now = DateTime.now();
        final last = action.lastTrigger;
        if (last == null ||
            now.difference(last) > const Duration(milliseconds: 120)) {
          action.lastTrigger = now;
          runAction(action);
        }
        continue;
      }

      if (event.type != EngineMidiEvent.controller) continue;

      final learning = _learnTarget;
      if (learning != null) {
        learning.cc = event.data1;
        learning.aliases = const [];
        learning.armed = false;
        learning.catching = false;
        _learnTarget = null;
        _saveSettings();
        _refreshReadouts(forceDirty: true);
        _dirty = true;
        continue;
      }

      for (final control in controls) {
        if (!control.matches(event.data1)) continue;
        final target = _targetFor(control);
        if (target == null) {
          control.catching = false;
          continue;
        }
        if (_apply(control, target, event.data2)) mixerTouched = true;
        break;
      }
    }

    _syncTrack();
    _pumpCount = (_pumpCount + 1) % 6000;
    if (_pumpCount % 30 == 0) {
      _deviceCache = null;
      _paramMapKey = null;
    }
    if (_pumpCount % 4 == 0) _refreshReadouts();
    if (mixerTouched) _mixerDirty = true;
    if (_mixerDirty && _pumpCount % 6 == 0) {
      // Keep the mixer visible while a hardware fader moves, without a full
      // snapshot refresh for every CC event.
      _mixerDirty = false;
      engine.refresh();
    }
    if (_dirty) {
      _dirty = false;
      notifyListeners();
    }
  }

  bool _apply(ControllerControl control, _ControllerTarget target, int ccValue) {
    final current = target.read().clamp(0.0, 1.0);
    control.targetName = target.parameterName;
    control.targetValue = current;
    control.valueText = target.valueText;

    // The hardware faders are absolute even when the encoders are configured
    // in relative mode.
    if (_relative && control.kind == ControllerControlKind.encoder) {
      final delta = relativeEncoderDelta(ccValue);
      if (delta == 0) return false;
      final next = (current + delta).clamp(0.0, 1.0);
      target.write(next);
      control.targetValue = next;
      control.valueText = target.valueText;
      control.armed = true;
      control.catching = false;
      control.ccValue = next;
    } else {
      final incoming = (ccValue / 127.0).clamp(0.0, 1.0);
      control.ccValue = incoming;
      if (!softTakeoverAllows(current, incoming, control.armed)) {
        control.catching = true;
        _dirty = true;
        return false;
      }
      control.armed = true;
      control.catching = false;
      target.write(incoming);
      control.targetValue = incoming;
      control.valueText = target.valueText;
    }
    _dirty = true;
    return target.touchesMixer;
  }

  _ControllerTarget? _targetFor(ControllerControl control) {
    final track = activeTrack;
    if (track == null) return null;
    switch (control.targetKind) {
      case ControllerTargetKind.trackVolume:
        return _TrackVolumeTarget(engine, track.id);
      case ControllerTargetKind.trackPan:
        return _TrackPanTarget(engine, track.id);
      case ControllerTargetKind.masterVolume:
        return _MasterTarget(engine);
      case ControllerTargetKind.deviceParam:
        final device = currentDevice;
        if (device == null) return null;
        final indices = _mappableParamIndices();
        if (control.paramIndex < 0 || control.paramIndex >= indices.length) {
          return null;
        }
        final actual = indices[control.paramIndex];
        if (control.kind == ControllerControlKind.fader &&
            (engine.pluginParamFlags(
                      track.id, device.slotKind, device.slotIndex, actual) &
                  3) !=
                0) {
          return null;
        }
        return _ParamTarget(
            engine, track.id, device.slotKind, device.slotIndex, actual);
    }
  }

  void _syncTrack() {
    final track = activeTrack;
    final id = track?.id;
    if (id == _lastTrackId) return;
    _lastTrackId = id;
    _device = null;
    _deviceCache = null;
    _paramMapKey = null;
    _resetArmed();
    _refreshReadouts(forceDirty: true);
    _dirty = true;
  }

  void _resetArmed() {
    for (final control in controls) {
      control.armed = false;
      control.catching = false;
    }
  }

  void _refreshReadouts({bool forceDirty = false}) {
    for (final control in controls) {
      final target = _targetFor(control);
      if (target == null) {
        if (control.valueText != '—' || forceDirty) _dirty = true;
        control.targetValue = 0;
        control.targetName = '';
        control.valueText = '—';
        continue;
      }
      final value = target.read().clamp(0.0, 1.0);
      final text = target.valueText;
      final name = target.parameterName;
      if (forceDirty ||
          value != control.targetValue ||
          text != control.valueText ||
          name != control.targetName) {
        _dirty = true;
      }
      control.targetValue = value;
      control.targetName = name;
      control.valueText = text;
    }
  }

  ControllerControl _makeFader(int index, int cc) {
    switch (index) {
      case 0:
        return ControllerControl(
          id: 'fader1',
          kind: ControllerControlKind.fader,
          index: 0,
          cc: cc,
          aliases: _faderAliases[0],
          targetKind: ControllerTargetKind.trackVolume,
        );
      case 1:
        return ControllerControl(
          id: 'fader2',
          kind: ControllerControlKind.fader,
          index: 1,
          cc: cc,
          aliases: _faderAliases[1],
          targetKind: ControllerTargetKind.trackPan,
        );
      case 2:
        return ControllerControl(
          id: 'fader3',
          kind: ControllerControlKind.fader,
          index: 2,
          cc: cc,
          aliases: _faderAliases[2],
          targetKind: ControllerTargetKind.deviceParam,
          paramIndex: 8,
        );
      default:
        return ControllerControl(
          id: 'fader4',
          kind: ControllerControlKind.fader,
          index: 3,
          cc: cc,
          aliases: _faderAliases[3],
          targetKind: ControllerTargetKind.masterVolume,
        );
    }
  }

  static List<int> _encoderAliasesFor(int index, int cc) {
    if (cc != ControllerSettings.defaultEncoderCcs[index]) return const [];
    return _encoderAliases[index];
  }

  void _saveSettings() {
    ControllerSettings(
      encoderCcs: [for (final c in encoders) c.cc],
      faderCcs: [for (final c in faders) c.cc],
      relative: _relative,
      softTakeover: _softTakeover,
      actions: {
        for (final binding in actionBindings)
          if (binding.trigger != null) binding.action: binding.trigger!,
      },
    ).save(to: _settingsFile);
  }
}
