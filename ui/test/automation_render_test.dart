// Volume automation end-to-end (§7.12 v1): the drawn curve must actually
// shape the rendered audio. One engine per tester process (JUCE singleton).

import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/cadence_bindings.dart';

/// Writes a minimal mono 16-bit PCM WAV with a 440 Hz sine.
File writeSineWav(String path, {double seconds = 1.0, int sampleRate = 44100}) {
  final numSamples = (seconds * sampleRate).round();
  final data = ByteData(numSamples * 2);
  for (var i = 0; i < numSamples; i++) {
    final value =
        (math.sin(2 * math.pi * 440 * i / sampleRate) * 12000).round();
    data.setInt16(i * 2, value, Endian.little);
  }
  final header = ByteData(44);
  void ascii(int at, String s) {
    for (var i = 0; i < s.length; i++) {
      header.setUint8(at + i, s.codeUnitAt(i));
    }
  }

  void u32(int at, int v) => header.setUint32(at, v, Endian.little);
  void u16(int at, int v) => header.setUint16(at, v, Endian.little);
  ascii(0, 'RIFF');
  u32(4, 36 + data.lengthInBytes);
  ascii(8, 'WAVE');
  ascii(12, 'fmt ');
  u32(16, 16);
  u16(20, 1);
  u16(22, 1);
  u32(24, sampleRate);
  u32(28, sampleRate * 2);
  u16(32, 2);
  u16(34, 16);
  ascii(36, 'data');
  u32(40, data.lengthInBytes);

  final file = File(path);
  file.writeAsBytesSync([...header.buffer.asUint8List(), ...data.buffer.asUint8List()]);
  return file;
}

/// RMS of the rendered 16-bit stereo WAV in [t0, t1] (project seconds).
double renderedRms(String path, double t0, double t1, {int rate = 44100}) {
  final bytes = File(path).readAsBytesSync();
  const headerBytes = 44;
  final frames = (bytes.length - headerBytes) ~/ 4; // stereo 16-bit
  final data = ByteData.sublistView(bytes);
  var sum = 0.0;
  var count = 0;
  for (var f = 0; f < frames; f++) {
    final t = f / rate;
    if (t < t0 || t >= t1) continue;
    for (var ch = 0; ch < 2; ch++) {
      final v = data.getInt16(headerBytes + f * 4 + ch * 2, Endian.little);
      sum += v * v;
      count++;
    }
  }
  return count == 0 ? 0.0 : math.sqrt(sum / count);
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('volume automation shapes the offline render', () {
    final engine = CadenceEngine();
    addTearDown(engine.dispose);

    engine.newProject(44100);
    final wav = writeSineWav('/tmp/cadence_auto_tone.wav', seconds: 2.0);
    final trackId = engine.addTrack('Auto', 0);
    expect(engine.addClip(trackId, wav.path, 0.0, 1.0) >= 0, isTrue);

    // Baseline: constant level everywhere.
    final plain = '/tmp/cadence_auto_plain.wav';
    expect(engine.render(plain, 4), isNotNull);
    final plainRms = renderedRms(plain, 1.0, 1.9);

    // Mute everything from 1.0 s on: the second half must fall to (near)
    // silence while the first half keeps playing. Points: 1 until 0.9,
    // ramp down to 0 at 1.0, stay silent.
    engine.setTrackVolumeAutomation(
        trackId, [0.0, 0.9, 1.0, 2.0], [1.0, 1.0, 0.0, 0.0]);
    final auto = '/tmp/cadence_auto_on.wav';
    expect(engine.render(auto, 4), isNotNull);

    final firstHalf = renderedRms(auto, 0.0, 0.8);
    final secondHalf = renderedRms(auto, 1.1, 1.9);
    expect(firstHalf, closeTo(plainRms, plainRms * 0.3),
        reason: 'first half must keep playing');
    expect(secondHalf, lessThan(firstHalf * 0.05),
        reason: 'automation must silence the second half');

    // Write mode (§7.12 v1.5, the W button): a point written at an existing
    // time REPLACES it; a new time inserts sorted.
    expect(engine.setTrackVolumeAutoPointAt(trackId, 0.9, 0.5), isTrue);
    expect(engine.trackVolumeAutoCount(trackId), 4);
    expect(engine.setTrackVolumeAutoPointAt(trackId, 0.9, 0.8), isTrue);
    expect(engine.trackVolumeAutoCount(trackId), 4); // replaced, not added
    final (t, v) = engine.trackVolumeAutoAt(trackId, 1);
    expect(t, closeTo(0.9, 1e-6));
    expect(v, closeTo(0.8, 1e-6));
  });
}
