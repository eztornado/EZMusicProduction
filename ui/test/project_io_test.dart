// Project I/O round trip through the service (§5 toolbar): save as
// .ezmpp under ~/Music/EZMusicProduction/Projects, new empty project,
// open restores tracks and notes. One engine per tester process.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('save / new / open round trip keeps tracks and notes', () async {
    final service = EngineService();
    addTearDown(service.dispose);
    expect(service.outputDeviceNames, isNotEmpty);

    final stamp = DateTime.now().microsecondsSinceEpoch;
    final name = 'io_test_$stamp';
    // A temp projects dir: tests must not litter the user's Projects
    // folder (the Abrir dialog lists it).
    final tempDir =
        await Directory.systemTemp.createTemp('ezmpp_io_test');
    addTearDown(() {
      if (tempDir.existsSync()) tempDir.deleteSync(recursive: true);
    });

    // Build something worth saving: an instrument track with notes.
    service.addTrack('Synth', 6, type: 1);
    final trackId = service.snapshot.tracks.first.id;
    service.addMidiClip(trackId, 0.0, 2.0);
    final clipId = service.snapshot.tracks.first.clips.first.id;
    service.midiClipAddNote(clipId, 64, 0.9, 0.1, 0.3);
    final saveError = service.saveProjectAs(name, inDir: tempDir);
    expect(saveError, isNull, reason: "engine error");
    expect(service.lastProjectPath, endsWith('.ezmpp'));
    final savedPath = service.lastProjectPath!;
    expect(File(savedPath).existsSync(), isTrue);

    // A new empty project must have no tracks (and detaches the file: the
    // next Guardar must not overwrite the previous project).
    expect(service.newEmptyProject(), isNull);
    expect(service.snapshot.tracks, isEmpty);
    expect(service.lastProjectPath, isNull);

    // Reopening restores everything.
    final openError = service.loadProjectFile(savedPath);
    expect(openError, isNull, reason: "engine error");
    expect(service.snapshot.tracks, hasLength(1));
    expect(service.snapshot.tracks.first.name, 'Synth');
    final reloaded =
        service.snapshot.tracks.first.clips.first.notes;
    expect(reloaded, hasLength(1));
    expect(reloaded.first.pitch, 64);
    expect(reloaded.first.start, closeTo(0.1, 1e-6));
  });
}
