// Pure-logic tests for the timeline: hit-testing and snap grid.

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/project_snapshot.dart';
import 'package:cadence_ui/ui/timeline_view.dart';

ProjectSnapshot _fixture() {
  const track0 = TrackInfo(
    id: 10,
    name: 'A',
    colorIndex: 0,
    type: 0,
    volume: 1,
    pan: 0,
    mute: false,
    solo: false,
    armed: false,
    inputChannel: 0,
    fxNames: <String>[],
    clips: [ClipInfo(id: 1, start: 0, length: 3, offset: 0, maxLength: 3, fadeIn: 0, fadeOut: 0, isMidi: false, notes: [])],
  );
  const track1 = TrackInfo(
    id: 11,
    name: 'B',
    colorIndex: 3,
    type: 0,
    volume: 1,
    pan: 0,
    mute: false,
    solo: false,
    armed: false,
    inputChannel: 1,
    fxNames: <String>[],
    clips: [ClipInfo(id: 2, start: 2, length: 1.5, offset: 0.5, maxLength: 2.5, fadeIn: 0, fadeOut: 0, isMidi: false, notes: [])],
  );
  return const ProjectSnapshot(
      tracks: [track0, track1],
      durationSeconds: 3.5,
      sampleRate: 48000,
      masterGain: 1);
}

void main() {
  group('hitTestClip', () {
    final snapshot = _fixture();

    test('hits a clip in its row and time range', () {
      // 0.5 s on row 0 -> clip 1.
      expect(hitTestClip(snapshot, const Offset(45, kRulerHeight + 30)), 1);
      // 2.7 s on row 1 -> clip 2.
      expect(hitTestClip(snapshot, const Offset(243, kRulerHeight + 64 + 30)), 2);
    });

    test('misses between clips and beyond project', () {
      // 3.9 s on row 0 -> past clip 1.
      expect(hitTestClip(snapshot, const Offset(351, kRulerHeight + 30)), isNull);
      // 0.5 s on row 1 -> before clip 2.
      expect(hitTestClip(snapshot, const Offset(45, kRulerHeight + 64 + 30)), isNull);
    });

    test('misses the ruler and empty rows', () {
      expect(hitTestClip(snapshot, const Offset(45, 5)), isNull);
      expect(hitTestClip(snapshot, const Offset(45, kRulerHeight + 2 * 64 + 30)),
          isNull);
    });
  });

  group('hitTestClipEdge', () {
    final snapshot = _fixture();
    final row0 = kRulerHeight + 30.0;

    test('classifies move inside the clip body', () {
      final hit = hitTestClipEdge(snapshot, Offset(135, row0)); // 1.5 s
      expect(hit, isNotNull);
      expect(hit!.clip.id, 1);
      expect(hit.edge, ClipEdge.move);
    });

    test('left edge hot zone', () {
      final hit = hitTestClipEdge(snapshot, Offset(3, row0)); // clip starts at 0
      expect(hit, isNotNull);
      expect(hit!.edge, ClipEdge.left);
    });

    test('right edge hot zone', () {
      final hit = hitTestClipEdge(snapshot, Offset(268, row0)); // clip ends at 270 px
      expect(hit, isNotNull);
      expect(hit!.edge, ClipEdge.right);
    });
  });

  group('snapSeconds', () {
    test('snaps to the 0.5 s beat grid', () {
      expect(snapSeconds(0.3, bypass: false), 0.5);
      expect(snapSeconds(0.24, bypass: false), 0.0);
      expect(snapSeconds(1.76, bypass: false), 2.0);
    });

    test('bypass keeps raw positions', () {
      expect(snapSeconds(0.3137, bypass: true), 0.3137);
      expect(snapSeconds(0.0, bypass: false), 0.0);
    });
  });
}
