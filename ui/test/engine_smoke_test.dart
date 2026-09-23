// Engine smoke test — runs on the host (flutter test), no GUI.
//
// Verifies the FFI binding against the real dylib: document ops, undo/redo,
// read accessors, save/load round-trip and the clip edit lifecycle
// (add/move/undo/remove/redo) with a WAV generated in Dart.

import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/cadence_bindings.dart';
import 'package:cadence_ui/engine/project_snapshot.dart';

ProjectSnapshot pullSnapshotForTest(CadenceEngine engine) {
  // Local copy of pullSnapshot to avoid importing the Flutter-dependent
  // project_snapshot in a pure engine test... project_snapshot imports
  // material, which is fine inside flutter_test.
  return pullSnapshot(engine);
}

/// Writes a minimal mono 16-bit PCM WAV with a 440 Hz sine.
File writeSineWav(String path, {double seconds = 1.0, int sampleRate = 44100}) {
  final numSamples = (seconds * sampleRate).round();
  final data = ByteData(numSamples * 2);
  for (var i = 0; i < numSamples; i++) {
    final value = (math.sin(2 * math.pi * 440 * i / sampleRate) * 12000).round();
    data.setInt16(i * 2, value, Endian.little);
  }
  final bytes = BytesBuilder();
  void ascii4(String s) => bytes.add(s.codeUnits);
  void u32(int v) {
    final b = ByteData(4)..setUint32(0, v, Endian.little);
    bytes.add(b.buffer.asUint8List());
  }

  void u16(int v) {
    final b = ByteData(2)..setUint16(0, v, Endian.little);
    bytes.add(b.buffer.asUint8List());
  }

  final dataSize = data.lengthInBytes;
  ascii4('RIFF');
  u32(dataSize + 36);
  ascii4('WAVE');
  ascii4('fmt ');
  u32(16);
  u16(1); // PCM
  u16(1); // mono
  u32(sampleRate);
  u32(sampleRate * 2); // byte rate
  u16(2); // block align
  u16(16); // bits
  ascii4('data');
  u32(dataSize);
  bytes.add(data.buffer.asUint8List());

  return File(path)..writeAsBytesSync(bytes.toBytes());
}

void main() {
  late CadenceEngine engine;

  setUpAll(() {
    try {
      engine = CadenceEngine();
    } catch (error) {
      fail('dylib no encontrada — compila el engine primero: $error');
    }
  });

  tearDownAll(() {
    engine.dispose();
  });

  test('project ops build the document', () {
    expect(engine.newProject(48000), CadenceResult.ok);

    final drums = engine.addTrack('Drums', 0);
    final bass = engine.addTrack('Bass', 3);
    expect(drums, 1);
    expect(bass, 2);
    expect(engine.trackCount, 2);
    expect(engine.durationSeconds, 0.0);
  });

  test('read accessors expose track data', () {
    expect(engine.trackIdAt(0), 1);
    expect(engine.trackIdAt(1), 2);
    expect(engine.trackIdAt(5), -1);
    expect(engine.trackName(1), 'Drums');
    expect(engine.trackName(2), 'Bass');
    expect(engine.trackName(99), '');
    expect(engine.trackColorIndex(2), 3);
    expect(engine.trackClipCount(1), 0);
  });

  test('undo/redo restores document state', () {
    engine.newProject(48000);
    engine.addTrack('A', 0);
    expect(engine.trackCount, 1);

    engine.addTrack('B', 3);
    expect(engine.trackCount, 2);

    expect(engine.undo(), isTrue);
    expect(engine.trackCount, 1);

    expect(engine.redo(), isTrue);
    expect(engine.trackCount, 2);

    expect(engine.undo(), isTrue);
    expect(engine.trackName(engine.trackIdAt(0)), 'A');
    expect(engine.undo(), isTrue);
    expect(engine.trackCount, 0); // empty state before first addTrack
    expect(engine.canUndo, isFalse);
  });

  test('missing clips do not create undo entries', () {
    engine.newProject(48000);
    engine.addTrack('A', 0);
    expect(engine.removeClip(999), CadenceResult.errorNotFound);
    expect(engine.removeTrack(999), CadenceResult.errorNotFound);
    expect(engine.trackCount, 1);
  });

  test('clip lifecycle: add, move, undo, remove, redo', () {
    engine.newProject(48000);
    final trackId = engine.addTrack('X', 0);
    final wav = writeSineWav('/tmp/cadence_test_clip.wav');

    final clipId = engine.addClip(trackId, wav.path, 0.0, 1.0);
    expect(clipId, greaterThan(0));
    expect(engine.trackClipCount(trackId), 1);
    expect(engine.durationSeconds, closeTo(1.0, 0.02));

    // Move (what a timeline drag commits) — one undo entry.
    expect(engine.setClipStart(clipId, 2.0), CadenceResult.ok);
    expect(engine.clipStart(clipId), 2.0);
    expect(engine.undo(), isTrue);
    expect(engine.clipStart(clipId), 0.0);
    expect(engine.redo(), isTrue);
    expect(engine.clipStart(clipId), 2.0);

    // Trim (what an edge drag commits): start 2.5, offset 0.5, length 0.5.
    expect(engine.clipLengthMax(clipId), closeTo(1.0, 0.02));
    expect(engine.setClipBounds(clipId, 2.5, 0.5, 0.5), CadenceResult.ok);
    expect(engine.clipStart(clipId), 2.5);
    expect(engine.clipLength(clipId), closeTo(0.5, 0.02));
    expect(engine.clipOffset(clipId), closeTo(0.5, 0.02));
    expect(engine.durationSeconds, closeTo(3.0, 0.02));
    expect(engine.undo(), isTrue);
    expect(engine.clipLength(clipId), closeTo(1.0, 0.02));

    // Fades: set, read back, persist through save/load.
    expect(engine.setClipFades(clipId, 0.25, 0.1), CadenceResult.ok);
    expect(engine.clipFadeIn(clipId), closeTo(0.25, 0.001));
    expect(engine.clipFadeOut(clipId), closeTo(0.1, 0.001));
    engine.saveProject('/tmp/cadence_test_fades.cadence');
    engine.loadProject('/tmp/cadence_test_fades.cadence');
    expect(engine.clipFadeIn(clipId), closeTo(0.25, 0.001));
    expect(engine.clipFadeOut(clipId), closeTo(0.1, 0.001));

    // Track name/color edits.
    expect(engine.setTrackName(trackId, 'Renamed'), CadenceResult.ok);
    expect(engine.trackName(trackId), 'Renamed');
    expect(engine.setTrackColor(trackId, 7), CadenceResult.ok);
    expect(engine.trackColorIndex(trackId), 7);

    // Remove + undo restores the clip.
    expect(engine.removeClip(clipId), CadenceResult.ok);
    expect(engine.trackClipCount(trackId), 0);
    expect(engine.undo(), isTrue);
    expect(engine.trackClipCount(trackId), 1);
    expect(engine.clipStart(clipId), 2.0);

    wav.deleteSync();
  });

  test('clip waveform peaks are served per pixel', () {
    engine.newProject(48000);
    final trackId = engine.addTrack('W', 0);
    final wav = writeSineWav('/tmp/cadence_test_peaks.wav');
    final clipId = engine.addClip(trackId, wav.path, 0.0, 1.0);
    expect(clipId, greaterThan(0));

    final mins = Float32List(100);
    final maxs = Float32List(100);
    expect(engine.clipPeaks(clipId, 0, 1.0, mins, maxs), 0);

    var peak = 0.0;
    for (var i = 0; i < 100; i++) {
      peak = math.max(peak, maxs[i]);
      expect(mins[i] >= -1.0 && maxs[i] <= 1.0, isTrue,
          reason: 'pixel $i out of range');
    }
    expect(peak, greaterThan(0.1), reason: 'sine peaks should be audible');

    wav.deleteSync();
  });

  test('midi clips: create, notes, snapshot, save/load', () {
    engine.newProject(48000);
    final trackId = engine.addTrack('Synth', 6, type: 1);
    expect(engine.trackType(trackId), 1);

    final clipId = engine.addMidiClip(trackId, 2.0, 2.0);
    expect(clipId, greaterThan(0));
    expect(engine.clipIsMidi(clipId), isTrue);
    expect(engine.clipNoteCount(clipId), 0);

    expect(engine.midiClipAddNote(clipId, 60, 0.9, 0.0, 0.5), CadenceResult.ok);
    expect(engine.midiClipAddNote(clipId, 64, 0.8, 0.5, 0.5), CadenceResult.ok);
    expect(engine.midiClipAddNote(clipId, 67, 0.7, 1.0, 0.5), CadenceResult.ok);

    final snapshot = pullSnapshotForTest(engine);
    final synth = snapshot.tracks.firstWhere((t) => t.id == trackId);
    expect(synth.clips.length, 1);
    expect(synth.clips.first.notes.length, 3);
    expect(synth.clips.first.notes.first.pitch, 60);

    engine.saveProject('/tmp/cadence_test_midi.cadence');
    engine.loadProject('/tmp/cadence_test_midi.cadence');
    expect(engine.clipNoteCount(clipId), 3);
    expect(engine.trackType(trackId), 1);
  });
}
