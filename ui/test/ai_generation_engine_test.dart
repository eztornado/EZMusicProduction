// AI generation end-to-end through the real dylib and the C ABI.
//
// Uses `useModel: false` so the test is deterministic and does not need the
// Magenta checkpoints installed: it exercises the async job, result getters,
// atomic apply (one undo entry) and undo.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/cadence_bindings.dart';

void main() {
  late CadenceEngine engine;

  setUpAll(() {
    try {
      engine = CadenceEngine();
    } catch (error) {
      fail('dylib no encontrada — compila el engine primero: $error');
    }
  });

  tearDownAll(() => engine.dispose());

  AiGenerationSnapshotData readResult(CadenceEngine engine) {
    final notes = <(int, double, double, double)>[];
    for (var i = 0; i < engine.aiResultNoteCount; i++) {
      notes.add(engine.aiResultNoteAt(i));
    }
    return AiGenerationSnapshotData(
      ok: engine.aiResultOk,
      error: engine.aiResultString(0),
      provider: engine.aiResultString(1),
      key: engine.aiResultString(2),
      chords: engine.aiResultString(3),
      promptNote: engine.aiResultString(4),
      contextNote: engine.aiResultString(5),
      seed: engine.aiResultSeed,
      length: engine.aiResultClipLength,
      start: engine.aiResultStart,
      clipId: engine.aiResultClipId,
      trackId: engine.aiResultTrackId,
      notes: notes,
    );
  }

  Future<AiGenerationSnapshotData> waitForResult() async {
    final deadline = DateTime.now().add(const Duration(seconds: 20));
    while (engine.aiGenerationState == 1 && DateTime.now().isBefore(deadline)) {
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }
    expect(engine.aiGenerationState, isNot(1), reason: 'generation timed out');
    return readResult(engine);
  }

  test('procedural melody: generate, apply, undo', () async {
    engine.newProject(48000);
    final track = engine.addTrack('AI Lead', 0, type: 1);
    engine.addMidiClip(track, 0, 8.0);
    engine.setAiModelDir('/nonexistent-cadence-models');

    expect(
      engine.startAiGeneration(
        trackId: track,
        anchorSeconds: 0,
        mode: 0,
        bars: 2,
        temperature: 0.9,
        variations: 2,
        seed: 1234,
        prompt: 'más oscuro, con arpegios',
        useModel: false,
      ),
      isTrue,
    );

    final result = await waitForResult();
    expect(result.ok, isTrue, reason: result.error);
    expect(result.notes, isNotEmpty);
    expect(result.provider, isNotEmpty);
    expect(result.key, isNotEmpty);
    // New-clip generations report -1: there is no source clip id.
    expect(result.clipId, -1);
    expect(result.trackId, track);

    final applied = engine.applyAiGeneration();
    expect(applied, greaterThan(0));
    expect(engine.clipNoteCount(applied), result.notes.length);
    expect(engine.canUndo, isTrue);

    expect(engine.undo(), isTrue);
    expect(engine.clipNoteCount(applied), 0);
  });

  test('regenerating an existing clip rewrites it in place', () async {
    engine.newProject(48000);
    final track = engine.addTrack('Keys', 0, type: 1);
    final clip = engine.addMidiClip(track, 0, 8.0);
    for (final pitch in [60, 63, 67]) {
      engine.midiClipAddNote(clip, pitch, 0.8, 0.0, 2.0);
    }
    final before = engine.clipNoteCount(clip);
    expect(before, 3);

    expect(
      engine.startAiGeneration(
        trackId: track,
        clipId: clip,
        anchorSeconds: 0,
        mode: 3,
        bars: 1,
        variations: 1,
        seed: 99,
        replaceClip: true,
        harmonize: true,
        useModel: false,
      ),
      isTrue,
    );

    final result = await waitForResult();
    expect(result.ok, isTrue, reason: result.error);
    expect(result.clipId, clip);

    final applied = engine.applyAiGeneration();
    expect(applied, clip);
    expect(engine.clipNoteCount(clip), result.notes.length);
  });

  test('procedural long form generates a 30-second clip', () async {
    engine.newProject(48000);
    final track = engine.addTrack('AI Lead', 0, type: 1);
    engine.addMidiClip(track, 0, 8.0);
    engine.setAiModelDir('/nonexistent-cadence-models');

    expect(
      engine.startAiGeneration(
        trackId: track,
        anchorSeconds: 0,
        mode: 0,
        bars: 4,
        lengthSeconds: 30,
        variations: 1,
        seed: 4321,
        useModel: false,
      ),
      isTrue,
    );

    final result = await waitForResult();
    expect(result.ok, isTrue, reason: result.error);
    expect(result.length, greaterThanOrEqualTo(29.9));
    expect(result.notes.length, greaterThan(16));
    expect(result.contextNote, contains('bars'));
    final lastStart = result.notes
        .map((n) => n.$3)
        .reduce((a, b) => a > b ? a : b);
    expect(lastStart, greaterThan(25.0));
  });

  test('model availability is reported without loading a model', () {
    final dir = Directory.systemTemp.createTempSync('cadence_ai_models_');
    try {
      engine.setAiModelDir(dir.path);
      expect(engine.aiModelAvailable(0), isFalse);
      expect(engine.aiModelAvailable(1), isFalse);
      expect(engine.aiModelDir, dir.path);
    } finally {
      dir.deleteSync(recursive: true);
    }
  });
}

/// Plain data mirror of the binding getters (no Flutter dependency needed).
class AiGenerationSnapshotData {
  const AiGenerationSnapshotData({
    required this.ok,
    required this.error,
    required this.provider,
    required this.key,
    required this.chords,
    required this.promptNote,
    required this.contextNote,
    required this.seed,
    required this.length,
    required this.start,
    required this.clipId,
    required this.trackId,
    required this.notes,
  });

  final bool ok;
  final String error;
  final String provider;
  final String key;
  final String chords;
  final String promptNote;
  final String contextNote;
  final int seed;
  final double length;
  final double start;
  final int clipId;
  final int trackId;
  final List<(int, double, double, double)> notes;
}
