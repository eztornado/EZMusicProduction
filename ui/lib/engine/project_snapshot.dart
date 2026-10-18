// UI-side snapshot of the engine document.
//
// Contract (ui-design.md §9): the C++ engine owns the document; Flutter pulls
// immutable snapshots after edits and reads the playhead at frame rate.
// This file is the "pull" half of that contract.

import 'package:flutter/material.dart';

import 'cadence_bindings.dart';

/// 16-color track palette — docs/design/ui-design.md §6.3.
const List<Color> kTrackPalette = [
  Color(0xFFE5484D), Color(0xFFE5622E), Color(0xFFF0A020), Color(0xFFB8C22A),
  Color(0xFF46A758), Color(0xFF26A69A), Color(0xFF22B3D1), Color(0xFF3E8FDB),
  Color(0xFF5B6EE1), Color(0xFF6E56CF), Color(0xFF9B59B6), Color(0xFFD6409F),
  Color(0xFFE93D82), Color(0xFF8E6A4E), Color(0xFF7C8B2D), Color(0xFF6E7191),
];

Color trackColor(int index) => kTrackPalette[index.clamp(0, kTrackPalette.length - 1)];

class NoteInfo {
  const NoteInfo({
    required this.pitch,
    required this.velocity,
    required this.start,
    required this.length,
    this.origStart,
  });

  final int pitch;
  final double velocity; // 0..1
  final double start;    // seconds within the clip
  final double length;
  final double? origStart; // pre-quantize reference (null = not quantized)
}

class ClipInfo {
  const ClipInfo({
    required this.id,
    required this.start,
    required this.length,
    required this.offset,
    required this.maxLength,
    required this.fadeIn,
    required this.fadeOut,
    required this.isMidi,
    required this.notes,
  });

  final int id;
  final double start;    // seconds on the timeline
  final double length;   // seconds
  final double offset;   // position inside the source file
  final double maxLength; // source duration - offset (trim limit)
  final double fadeIn;   // linear fade seconds
  final double fadeOut;
  final bool isMidi;
  final List<NoteInfo> notes; // MIDI clips only

  double get end => start + length;
}

class AutomationPointInfo {
  const AutomationPointInfo({required this.time, required this.value});

  final double time; // project seconds
  final double value; // 0..1
}

/// An automation lane targeting one plugin parameter (§7.12 v1.5).
class ParamLaneInfo {
  const ParamLaneInfo({
    required this.slotKind,
    required this.slotIndex,
    required this.paramIndex,
    required this.name,
    required this.points,
  });

  final int slotKind; // 0 = fx slot, 1 = instrument
  final int slotIndex;
  final int paramIndex;
  final String name;
  final List<AutomationPointInfo> points;
}

class TrackInfo {
  const TrackInfo({
    required this.id,
    required this.name,
    required this.colorIndex,
    required this.type,
    required this.volume,
    required this.pan,
    required this.mute,
    required this.solo,
    required this.armed,
    required this.inputChannel,
    required this.fxNames,
    this.fxBypass = const [],
    this.instrumentName = '',
    this.midifxName = '',
    this.volumeAuto = const [],
    this.panAuto = const [],
    this.sustainAuto = const [],
    this.paramLanes = const [],
    required this.clips,
  });

  final int id;
  final String name;
  final int colorIndex;
  final int type; // 0 = audio, 1 = instrument (MIDI)
  final double volume;
  final double pan;
  final bool mute;
  final bool solo;
  final bool armed;
  final int inputChannel; // 0 = L, 1 = R
  final List<String> fxNames;
  final List<bool> fxBypass; // parallel to fxNames
  final String instrumentName; // '' = internal synth (instrument tracks)
  final String midifxName; // '' = no MIDI insert (v1: EZArpeggio)
  final List<AutomationPointInfo> volumeAuto; // §7.12 volume lane
  final List<AutomationPointInfo> panAuto; // §7.12 pan lane
  final List<AutomationPointInfo> sustainAuto; // §7.12 sustain (CC64) lane
  final List<ParamLaneInfo> paramLanes; // plugin-parameter lanes
  final List<ClipInfo> clips;
}

class ProjectSnapshot {
  const ProjectSnapshot({
    required this.tracks,
    required this.durationSeconds,
    required this.sampleRate,
    required this.masterGain,
    this.masterFxNames = const [],
    this.masterFxBypass = const [],
    this.tempoBpm = 120,
    this.timeSigNumerator = 4,
    this.timeSigDenominator = 4,
  });

  const ProjectSnapshot.empty()
      : tracks = const [],
        durationSeconds = 0,
        sampleRate = 48000,
        masterGain = 1,
        masterFxNames = const [],
        masterFxBypass = const [],
        tempoBpm = 120,
        timeSigNumerator = 4,
        timeSigDenominator = 4;

  final List<TrackInfo> tracks;
  final double durationSeconds;
  final double sampleRate;
  final double masterGain;
  final List<String> masterFxNames;
  final List<bool> masterFxBypass; // parallel to masterFxNames
  final double tempoBpm;
  final int timeSigNumerator;
  final int timeSigDenominator;

  /// Seconds per beat at the project tempo (the quantize/snap unit).
  double get beatSeconds => 60.0 / tempoBpm;
  /// Seconds per bar at the project tempo + time signature.
  double get barSeconds =>
      beatSeconds * timeSigNumerator * 4 / timeSigDenominator;
}

ProjectSnapshot pullSnapshot(CadenceEngine engine) {
  final tracks = <TrackInfo>[];
  final trackCount = engine.trackCount;
  for (var i = 0; i < trackCount; i++) {
    final trackId = engine.trackIdAt(i);
    final clips = <ClipInfo>[];
    final clipCount = engine.trackClipCount(trackId);
    for (var c = 0; c < clipCount; c++) {
      final clipId = engine.clipIdAt(trackId, c);
      final notes = <NoteInfo>[];
      final isMidi = engine.clipIsMidi(clipId);
      if (isMidi) {
        final noteCount = engine.clipNoteCount(clipId);
        for (var n = 0; n < noteCount; n++) {
          final (pitch, velocity, start, length) = engine.clipNoteAt(clipId, n);
          final orig = engine.clipNoteOrigStartAt(clipId, n);
          notes.add(NoteInfo(
              pitch: pitch,
              velocity: velocity,
              start: start,
              length: length,
              origStart:
                  (orig - start).abs() > 1e-9 ? orig : null));
        }
      }
      clips.add(ClipInfo(
        id: clipId,
        start: engine.clipStart(clipId),
        length: engine.clipLength(clipId),
        offset: engine.clipOffset(clipId),
        maxLength: engine.clipLengthMax(clipId),
        fadeIn: engine.clipFadeIn(clipId),
        fadeOut: engine.clipFadeOut(clipId),
        isMidi: isMidi,
        notes: notes,
      ));
    }
    final fxNames = <String>[];
    final fxBypass = <bool>[];
    final fxCount = engine.trackFxCount(trackId);
    for (var f = 0; f < fxCount; f++) {
      fxNames.add(engine.trackFxNameAt(trackId, f));
      fxBypass.add(engine.trackFxBypassAt(trackId, f));
    }
    List<AutomationPointInfo> readLane(int lane) {
      final out = <AutomationPointInfo>[];
      final count = engine.trackLaneAutoCount(trackId, lane);
      for (var a = 0; a < count; a++) {
        final (time, value) = engine.trackLaneAutoAt(trackId, lane, a);
        out.add(AutomationPointInfo(time: time, value: value));
      }
      return out;
    }

    final volumeAuto = readLane(0);
    final panAuto = readLane(1);
    final sustainAuto = readLane(2);
    final paramLanes = <ParamLaneInfo>[];
    final laneCount = engine.trackParamLaneCount(trackId);
    for (var l = 0; l < laneCount; l++) {
      final (slotKind, slotIndex, paramIndex) =
          engine.trackParamLaneInfo(trackId, l);
      final name = engine.trackParamLaneName(trackId, l);
      final points = <AutomationPointInfo>[];
      final pointCount = engine.trackParamLanePointCount(trackId, l);
      for (var p = 0; p < pointCount; p++) {
        final (time, value) =
            engine.trackParamLanePointAt(trackId, l, p);
        points.add(AutomationPointInfo(time: time, value: value));
      }
      paramLanes.add(ParamLaneInfo(
          slotKind: slotKind,
          slotIndex: slotIndex,
          paramIndex: paramIndex,
          name: name,
          points: points));
    }
    tracks.add(TrackInfo(
      id: trackId,
      name: engine.trackName(trackId),
      colorIndex: engine.trackColorIndex(trackId),
      type: engine.trackType(trackId),
      volume: engine.trackVolume(trackId),
      pan: engine.trackPan(trackId),
      mute: engine.trackMute(trackId),
      solo: engine.trackSolo(trackId),
      armed: engine.trackArmed(trackId),
      inputChannel: engine.trackInputChannel(trackId),
      fxNames: fxNames,
      fxBypass: fxBypass,
      instrumentName: engine.trackInstrumentName(trackId),
      midifxName: engine.trackMidifxName(trackId),
      volumeAuto: volumeAuto,
      panAuto: panAuto,
      sustainAuto: sustainAuto,
      paramLanes: paramLanes,
      clips: clips,
    ));
  }
  final masterFxNames = <String>[];
  final masterFxBypass = <bool>[];
  final masterFxCount = engine.masterFxCount;
  for (var f = 0; f < masterFxCount; f++) {
    masterFxNames.add(engine.masterFxNameAt(f));
    masterFxBypass.add(engine.masterFxBypassAt(f));
  }
  return ProjectSnapshot(
    tracks: tracks,
    durationSeconds: engine.durationSeconds,
    sampleRate: engine.sampleRate,
    masterGain: engine.masterGain,
    masterFxNames: masterFxNames,
    masterFxBypass: masterFxBypass,
    tempoBpm: engine.tempoBpm,
    timeSigNumerator: engine.timeSigNumerator,
    timeSigDenominator: engine.timeSigDenominator,
  );
}
