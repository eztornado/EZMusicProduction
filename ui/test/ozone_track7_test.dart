// Reproduces Iván's case: project "dont miss that train again", Pista 7 =
// LABS instrument + Ozone 10 Elements in the FX chain. Renders the track
// alone and measures whether the FX chain passes audio.
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';

double _sqrt(double x) {
  var r = x;
  for (var i = 0; i < 40; i++) {
    r = 0.5 * (r + x / r);
  }
  return r;
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('Pista 7 (LABS + Ozone) renders audio through the fx chain', () {
    final src = File(
        '/Users/ivan/Music/EZMusicProduction/Projects/dont miss that train again.ezmpp');
    if (!src.existsSync()) return;
    final tmp = Directory.systemTemp.createTempSync('ezmpp_ozone');
    final copy = File('${tmp.path}/proj.ezmpp');
    src.copySync(copy.path);

    final service = EngineService();
    addTearDown(service.dispose);
    expect(service.loadProjectFile(copy.path), isNull);
    while (service.engineHasPendingPluginJobs) {
      service.enginePumpOnePluginJob();
    }

    final tracks = service.snapshot.tracks;
    // Find the Ozone track by its fx (track names/order change in edits).
    final t7 = tracks.firstWhere(
        (t) => t.fxNames.contains('Ozone 10 Elements'));
    // ignore: avoid_print
    print('ozone track: ${t7.name} (inst=${t7.instrumentName})');

    // Solo Pista 7 so the render is only its signal.
    for (final t in tracks) {
      service.setTrackSolo(t.id, t.id == t7.id);
    }
    service.seek(0.0);
    final err = service.exportWav('${tmp.path}/t7.wav');
    expect(err, isNull, reason: err ?? '');
    final wav = File('${tmp.path}/t7.wav');
    final bytes = wav.readAsBytesSync();
    // Find the data chunk and compute RMS of channel 0 (16-bit PCM).
    var rms = 0.0;
    for (var i = 0; i < bytes.length - 8; i++) {
      if (bytes[i] == 0x64 && bytes[i + 1] == 0x61 && bytes[i + 2] == 0x74
          && bytes[i + 3] == 0x61) {
        final dataLen = bytes[i + 4] | (bytes[i + 5] << 8)
            | (bytes[i + 6] << 16) | (bytes[i + 7] << 24);
        var sumSq = 0.0;
        var n = 0;
        for (var j = i + 8; j + 1 < i + 8 + dataLen && j + 1 < bytes.length;
            j += 2) {
          final sample = (bytes[j] | (bytes[j + 1] << 8)).toSigned(16) / 32768.0;
          sumSq += sample * sample;
          n++;
        }
        rms = n > 0 ? _sqrt(sumSq / n) : 0.0;
        break;
      }
    }
    // ignore: avoid_print
    print('rendered rms=$rms bytes=${wav.lengthSync()}');
    expect(rms, greaterThan(0.01),
        reason: 'Ozone must pass the LABS audio through (rms=$rms = silence '
            'means the fx still receives no input)');
    tmp.deleteSync(recursive: true);
  });
}
