// Engine-path check behind the piano roll drag: midiClipSetNote must move
// the note and the refreshed snapshot must show it at the new position.
// (No synthetic gestures — pure service calls; one engine per process.)

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('engine: setNote moves a note; removeNote deletes; snapshot follows', () {
    final service = EngineService();
    addTearDown(service.dispose);

    service.addTrack('Synth', 6, type: 1);
    final trackId = service.snapshot.tracks.first.id;
    service.addMidiClip(trackId, 0.0, 8.0);
    final clipId = service.snapshot.tracks.first.clips.first.id;
    service.midiClipAddNote(clipId, 64, 0.8, 0.1, 0.3);
    service.midiClipAddNote(clipId, 70, 0.8, 1.0, 0.2);
    expect(
        service.snapshot.tracks.first.clips.first.notes, hasLength(2));

    // Drag-equivalent commit: move note 0, keep pitch and length.
    service.midiClipSetNote(clipId, 0, 64, 3.0, 0.3);
    final notes = service.snapshot.tracks.first.clips.first.notes;
    expect(notes, hasLength(2), reason: 'moving must not drop notes');
    expect(notes[0].start, closeTo(3.0, 1e-9),
        reason: 'note 0 must sit at the committed start');
    expect(notes[0].length, closeTo(0.3, 1e-9));
    expect(notes[0].pitch, 64);

    // Recorded-style start (unquantized grid position) survives too.
    service.midiClipSetNote(clipId, 1, 70, 2.309, 0.25);
    expect(
        service.snapshot.tracks.first.clips.first.notes[1].start,
        closeTo(2.309, 1e-9));

    service.midiClipRemoveNote(clipId, 0);
    expect(service.snapshot.tracks.first.clips.first.notes, hasLength(1));
  });
}
