// Loads a REAL Cubase .cpr through the engine (guarded by file existence).
//
// Verifies the whole path: pure-Dart reader -> EngineService.importCubaseProject
// -> tracks and MIDI clips present in the engine snapshot. Skips quietly on a
// machine without the sample project.
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/cubase/cpr_reader.dart';
import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('matches Cubase plug-in names against the installed catalogue', () {
    const instruments = [
      'EZInstruments',
      'HALion Sonic',
      'Kontakt 7',
      'Musio',
      'ZENOLOGY',
    ];
    // Cubase stores the plug-in's name, not the track name ("Bassoon" hosts
    // Kontakt 7), and instruments match fuzzily.
    expect(matchPluginName('ZENOLOGY', instruments), 4);
    expect(matchPluginName('Musio', instruments), 3);
    expect(matchPluginName('Kontakt 7', instruments), 2);
    expect(matchPluginName('Bassoon', instruments), -1);
    expect(matchPluginName('', instruments), -1);
    // Effects match exactly: a shared word must not pick an unrelated plug-in.
    const effects = [
      'Compressor',
      'Ozone 11 Elements',
      'Ozone 11 Equalizer',
      'Raum',
    ];
    expect(matchPluginName('Raum', effects, fuzzy: false), 3);
    expect(matchPluginName('Ozone 11 Equalizer', effects, fuzzy: false), 2);
    expect(matchPluginName('Ozone 11', effects, fuzzy: false), -1);
    expect(matchPluginName('Compressor', effects, fuzzy: false), 0);
  });

  const paths = [
    '/Users/ivan/Documents/CUBASE_PROYECTS/HOLO THEME/HOLO THEME.cpr',
    '/Users/ivan/Documents/CUBASE_PROYECTS/7/7.cpr',
  ];

  test('real Cubase .cpr imports through the service', () {
    File? file;
    for (final p in paths) {
      final f = File(p);
      if (f.existsSync()) {
        file = f;
        break;
      }
    }
    if (file == null) return; // nothing to verify on this machine

    final watch = Stopwatch()..start();
    final service = EngineService();
    addTearDown(service.dispose);

    expect(
        service.importCubaseProject(file.path,
            loadInstruments: false, loadEffects: false),
        isNull,
        reason: '${file.path} must import without error');

    final tracks = service.snapshot.tracks;
    expect(tracks, isNotEmpty);
    final notes = tracks.expand((t) => t.clips.expand((c) => c.notes)).length;
    expect(notes, greaterThan(500), reason: 'MIDI notes must reach the engine');

    // Every part must reach the engine at its own Cubase position: the decoded
    // part starts (ticks → seconds) and the engine's clip starts must agree.
    final project = CprReader(file.readAsBytesSync()).read();
    final secondsPerTick = 60.0 / (project.tempoBpm * 480.0);
    final expected = project.midiParts
        .map((p) => p.startTicks * secondsPerTick)
        .toList()
      ..sort();
    final actual = tracks.expand((t) => t.clips).map((c) => c.start).toList()
      ..sort();
    expect(actual, hasLength(expected.length));
    for (var i = 0; i < expected.length; i++) {
      expect(actual[i], closeTo(expected[i], 0.001),
          reason: 'clip $i must keep its part position');
    }
    // ignore: avoid_print
    print('cubase ${file.path.split('/').last}: ${tracks.length} tracks, '
        '$notes notes, ${watch.elapsedMilliseconds} ms');
  });

  // A project with recorded audio: every audio event must reach the engine as
  // an audio clip, trimmed to the Cubase source window.
  test('real Cubase .cpr imports its audio clips', () {
    const path = '/Users/ivan/Documents/CUBASE_PROYECTS/RIKOS COOKING/'
        'RIKOS COOKING.cpr';
    final file = File(path);
    if (!file.existsSync() ||
        !Directory('${file.parent.path}/Audio').existsSync()) {
      return; // nothing to verify on this machine
    }
    final project = CprReader(file.readAsBytesSync()).read();
    expect(project.audioEvents, isNotEmpty);
    expect(project.sampleRate, greaterThan(0));

    final watch = Stopwatch()..start();
    final service = EngineService();
    addTearDown(service.dispose);
    expect(
        service.importCubaseProject(file.path,
            loadInstruments: false, loadEffects: false),
        isNull);

    final secondsPerTick = 60.0 / (project.tempoBpm * 480.0);
    final clips = service.snapshot.tracks
        .expand((t) => t.clips)
        .where((c) => !c.isMidi)
        .toList()
      ..sort((a, b) => a.start.compareTo(b.start));
    expect(clips, hasLength(project.audioEvents.length));

    final expected = project.audioEvents
        .map((e) => (
              start: e.startTicks * secondsPerTick,
              offset: e.sourceOffsetFrames / project.sampleRate,
              length: e.lengthFrames / project.sampleRate,
            ))
        .toList()
      ..sort((a, b) => a.start.compareTo(b.start));
    for (var i = 0; i < expected.length; i++) {
      expect(clips[i].start, closeTo(expected[i].start, 0.002),
          reason: 'audio clip $i keeps its Cubase position');
      expect(clips[i].offset, closeTo(expected[i].offset, 0.002),
          reason: 'audio clip $i keeps its source offset');
      expect(clips[i].length, closeTo(expected[i].length, 0.002),
          reason: 'audio clip $i keeps its source length');
    }
    // ignore: avoid_print
    print('Rikos: ${clips.length} audio clips, '
        '${service.snapshot.tracks.length} tracks, '
        '${watch.elapsedMilliseconds} ms');

    // The project's Stereo Out fader (-2.17 dB here) is part of the mix and is
    // carried onto the engine's master gain.
    expect(project.masterVolumeDb, closeTo(-2.172, 0.01));
    expect(service.snapshot.masterGain, closeTo(project.masterVolumeLinear, 1e-6));

    // Instrument tracks name the VSTi they host (Synth Slot → Plugin).
    final instruments = project.tracks
        .map((t) => t.instrumentName)
        .whereType<String>()
        .toSet();
    expect(instruments, containsAll(['ZENOLOGY', 'Musio', 'Kontakt 7']));

    // Insert chains come from the channels' `InsertFolder` slots, in order, and
    // the output channel's chain from the VST Mixer archive.
    final arranged = project.tracks
        .firstWhere((t) => t.name == 'Audio 01' && t.inserts.isNotEmpty);
    expect(arranged.inserts, [
      'RX 11 Voice De-noise',
      'Ozone 11 Equalizer',
      'Raum',
      'Compressor',
    ]);
    expect(project.masterInserts, ['MultibandCompressor', 'Ozone 11 Elements']);

    // Each plug-in's saved state is decoded next to its name, index-aligned
    // with the chain (and `instrumentState` for the VSTi).
    final zenology =
        project.tracks.firstWhere((t) => t.instrumentName == 'ZENOLOGY');
    expect(zenology.instrumentState, isNotNull);
    expect(zenology.instrumentState!.component.length, greaterThan(1024));
    expect(arranged.insertStates, hasLength(arranged.inserts.length));
    expect(arranged.insertStates.whereType<CubasePluginState>(), isNotEmpty);
    expect(project.masterInsertStates, hasLength(project.masterInserts.length));
    expect(project.masterInsertStates.first, isNotNull);
  });
}
