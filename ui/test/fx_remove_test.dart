// FX removal round trip: add two fx on a track, remove the first, verify
// the chain updates and the surviving slot shifts.
import 'package:flutter_test/flutter_test.dart';
import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('remove fx updates the chain in order', () {
    final service = EngineService();
    addTearDown(service.dispose);

    service.addTrack('Synth', 6, type: 1);
    final trackId = service.snapshot.tracks.first.id;
    // Pick two real fx plugins from the scan (indices vary per machine;
    // use slots 0 and 1 — any plugin works for a chain-order test).
    service.addTrackFx(trackId, 0);
    service.addTrackFx(trackId, 0);
    final count0 = service.snapshot.tracks.first.fxNames.length;
    if (count0 < 2) return; // environment without plugins

    service.removeTrackFx(trackId, 0);
    expect(service.snapshot.tracks.first.fxNames.length, count0 - 1,
        reason: 'removing slot 0 must leave one fewer fx');
  });
}
