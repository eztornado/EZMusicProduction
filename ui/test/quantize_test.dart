// Quantize engine coverage against the real dylib (§7.5.2 non-destructive
// model): quantize moves starts toward the grid while keeping the original
// reference; Reset restores it; Freeze bakes it. One engine per process.

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('quantize is non-destructive, resettable and freezable', () {
    final service = EngineService();
    addTearDown(service.dispose);

    service.addTrack('Synth', 6, type: 1);
    final trackId = service.snapshot.tracks.first.id;
    service.addMidiClip(trackId, 0.0, 4.0);
    final clipId = service.snapshot.tracks.first.clips.first.id;

    // Off-grid starts (deliberately not near the 1/8 grid of 0.25 s).
    final originals = [0.03, 0.31, 0.62, 1.04];
    for (var i = 0; i < originals.length; i++) {
      service.midiClipAddNote(clipId, 60 + i, 0.9, originals[i], 0.2);
    }

    // Quantize to 1/8: every start lands on a 0.25 s multiple.
    service.quantizeClip(clipId, 0.25, 0, 0, 1, false, false, 1);
    var notes = service.snapshot.tracks.first.clips.first.notes;
    for (final n in notes) {
      expect(n.start / 0.25, closeTo((n.start / 0.25).round(), 0.001),
          reason: 'starts must sit on the grid');
      expect(n.origStart, isNotNull,
          reason: 'quantized notes keep their original reference');
    }
    // The originals survive the move.
    for (var i = 0; i < originals.length; i++) {
      expect(notes[i].origStart, closeTo(originals[i], 1e-6));
    }

    // Reset puts every note back where it was.
    service.resetClipQuantize(clipId);
    notes = service.snapshot.tracks.first.clips.first.notes;
    for (var i = 0; i < originals.length; i++) {
      expect(notes[i].start, closeTo(originals[i], 1e-6));
      expect(notes[i].origStart, isNull,
          reason: 'back at the reference: no ghost');
    }

    // Quantize + Freeze bakes the grid positions as the new reference.
    service.quantizeClip(clipId, 0.25, 0, 0, 1, false, false, 1);
    service.freezeClipQuantize(clipId);
    notes = service.snapshot.tracks.first.clips.first.notes;
    for (final n in notes) {
      expect(n.origStart, isNull, reason: 'freeze clears the reference');
      expect(n.start / 0.25, closeTo((n.start / 0.25).round(), 0.001));
    }
  });

  test('catch range and max move protect far and near notes', () {
    final service = EngineService();
    addTearDown(service.dispose);

    service.addTrack('Synth', 6, type: 1);
    final trackId = service.snapshot.tracks.first.id;
    service.addMidiClip(trackId, 0.0, 4.0);
    final clipId = service.snapshot.tracks.first.clips.first.id;

    service.midiClipAddNote(clipId, 60, 0.9, 0.24, 0.2); // 0.01 from grid
    service.midiClipAddNote(clipId, 62, 0.9, 0.5, 0.2); // dead on the grid
    service.midiClipAddNote(clipId, 64, 0.9, 0.2499, 0.2); // 0.0001 from grid

    // Catch 100%: anything within a full grid step of its target stays.
    service.quantizeClip(clipId, 0.25, 0, 1, 1, false, false, 1);
    final notes = service.snapshot.tracks.first.clips.first.notes;
    expect(notes[0].start, closeTo(0.24, 1e-6));
    expect(notes[1].start, closeTo(0.5, 1e-6));
    expect(notes[2].start, closeTo(0.2499, 1e-6));
  });
}
