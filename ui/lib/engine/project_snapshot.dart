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
  });

  final int pitch;
  final double velocity; // 0..1
  final double start;    // seconds within the clip
  final double length;
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
  final List<ClipInfo> clips;
}

class ProjectSnapshot {
  const ProjectSnapshot({
    required this.tracks,
    required this.durationSeconds,
    required this.sampleRate,
    required this.masterGain,
  });

  const ProjectSnapshot.empty()
      : tracks = const [],
        durationSeconds = 0,
        sampleRate = 48000,
        masterGain = 1;

  final List<TrackInfo> tracks;
  final double durationSeconds;
  final double sampleRate;
  final double masterGain;
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
          notes.add(NoteInfo(
              pitch: pitch, velocity: velocity, start: start, length: length));
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
      clips: clips,
    ));
  }
  return ProjectSnapshot(
    tracks: tracks,
    durationSeconds: engine.durationSeconds,
    sampleRate: engine.sampleRate,
    masterGain: engine.masterGain,
  );
}
