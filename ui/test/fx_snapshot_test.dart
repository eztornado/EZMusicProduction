// FX/instrument snapshot coverage against the real dylib: track fx slots
// (name + bypass in parallel), master fx chain fields, and the per-track
// instrument name (empty = internal synth).
//
// One test only: a second CadenceEngine in the same process would spin a
// second JUCE message loop and crash flutter_tester (NSEvent assertion).

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  test('snapshot carries fx slots, instrument name and master fx fields', () {
    final service = EngineService();
    addTearDown(service.dispose);
    expect(service.outputDeviceNames, isNotEmpty);

    service.addTrack('A', 0);
    service.addTrack('Keys', 6, type: 1);

    final snap = service.snapshot;
    expect(snap.tracks, hasLength(2));
    for (final t in snap.tracks) {
      expect(t.fxNames.length, t.fxBypass.length);
    }
    expect(snap.tracks.last.type, 1);
    expect(snap.tracks.last.instrumentName, ''); // internal synth
    expect(snap.masterFxNames, isEmpty);
    expect(snap.masterFxBypass, isEmpty);

    // Undo keeps the derived lists consistent.
    service.undo();
    expect(service.snapshot.tracks, hasLength(1));
    expect(service.snapshot.masterFxNames, isEmpty);
  });
}
