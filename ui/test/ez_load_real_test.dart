// Loads Iván's REAL saved project (copied to temp) through the service.
import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('real saved project loads through the service', () {
    final src = File(
        '/Users/ivan/Music/EZMusicProduction/Projects/29092026.ezmpp');
    if (!src.existsSync()) return; // nothing to verify on this machine
    final tmp = Directory.systemTemp
        .createTempSync('ezmpp_real');
    final copy = File('${tmp.path}/real.ezmpp');
    src.copySync(copy.path);

    final service = EngineService();
    addTearDown(service.dispose);
    expect(service.loadProjectFile(copy.path), isNull,
        reason: 'his saved session must open cleanly');
    final tracks = service.snapshot.tracks;
    expect(tracks, isNotEmpty);
    // ignore: avoid_print
    print('real project: ${tracks.length} tracks, '
        'notes: ${tracks.expand((t) => t.clips.expand((c) => c.notes)).length}');
    tmp.deleteSync(recursive: true);
  });
}
