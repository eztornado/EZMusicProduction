// Clip clipboard (§7.4): copy a MIDI clip, paste it on another instrument
// track at the playhead; audio clips paste on audio tracks. Plain service
// calls (no widgets) — one engine per process.

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('copy/paste duplicates a MIDI clip with its notes', () {
    final service = EngineService();
    addTearDown(service.dispose);

    service.addTrack('A', 6, type: 1);
    service.addTrack('B', 5, type: 1);
    final tracks = service.snapshot.tracks;
    final trackA = tracks[0].id;
    final trackB = tracks[1].id;

    service.addMidiClip(trackA, 0.0, 2.0);
    final clipId = service.snapshot.tracks[0].clips.first.id;
    service.midiClipAddNote(clipId, 64, 0.9, 0.1, 0.3);
    service.midiClipAddNote(clipId, 71, 0.8, 0.5, 0.2);

    // Copy + paste on the other track at t=4.
    service.selectClip(clipId);
    service.copySelectedClip();
    service.selectTrack(trackB);
    service.seek(4.0);
    expect(service.pasteClipboard(), isNull);

    final pasted = service.snapshot.tracks[1].clips;
    expect(pasted, hasLength(1));
    expect(pasted.first.start, closeTo(4.0, 1e-9));
    expect(pasted.first.notes, hasLength(2));
    expect(pasted.first.notes[0].pitch, 64);
    expect(pasted.first.notes[1].pitch, 71);
    // The original stays untouched.
    expect(service.snapshot.tracks[0].clips, hasLength(1));

    // Cross-kind paste is rejected with a message (not a crash): a MIDI
    // clip must not paste onto an audio track.
    service.addTrack('C', 4); // audio track
    service.selectTrack(service.snapshot.tracks[2].id);
    expect(service.pasteClipboard(), isNotNull);
  });
}
