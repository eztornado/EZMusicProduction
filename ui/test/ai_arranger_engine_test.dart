// Song-arranger path through the real dylib and stable C ABI.
//
// A plan is pure JSON here: no network and no model are involved. The test
// verifies context extraction, multi-track apply, reuse of an existing track,
// result summary and one-step undo.

import 'dart:convert';

import 'package:cadence_ui/engine/cadence_bindings.dart';
import 'package:flutter_test/flutter_test.dart';

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

  test('context JSON and atomic multi-track apply', () {
    engine.newProject(48000);
    engine.setTempoBpm(96);
    final existing = engine.addTrack('Existing Keys', 5, type: 1);
    engine.addMidiClip(existing, 0, 8);

    final context = engine.aiContextJson(
      selectedTrackId: existing,
      anchorSeconds: 0,
      bars: 8,
    );
    final contextJson = jsonDecode(context) as Map<String, dynamic>;
    expect(contextJson['key'], isA<String>());
    expect(contextJson['tracks'], isA<List>());
    expect((contextJson['analysis'] as Map)['chord_line'], isA<String>());

    final available =
        contextJson['available_instruments'] as Map<String, dynamic>;
    expect(available['ezsynth_presets'], isA<List>());
    expect((available['ezinst_programs'] as List), hasLength(128));
    expect(available['ezorq_programs'], isA<List>());
    expect(available['ezdrums_kits'], isA<List>());
    final vst =
        (available['vst_instruments'] as List).cast<Map<String, dynamic>>();
    for (final plugin in vst) {
      expect(plugin['token'], startsWith('vst:'));
      expect(plugin['name'], isA<String>());
    }

    final plan = jsonEncode({
      'title': 'Test arrangement',
      'tracks': [
        {
          'name': 'Drums',
          'role': 'drums',
          'instrument': 'drums',
          'clips': [
            {
              'start_beat': 0,
              'length_beats': 16,
              'notes': [
                {
                  'pitch': 36,
                  'start_beat': 0,
                  'length_beats': 0.25,
                  'velocity': 0.9,
                },
                {
                  'pitch': 38,
                  'start_beat': 1,
                  'length_beats': 0.25,
                  'velocity': 0.8,
                },
              ],
            },
          ],
        },
        {
          'name': 'Bass',
          'role': 'bass',
          'instrument': 'gm:40',
          'instrument_name': 'Violin',
          'clips': [
            {
              'start_beat': 0,
              'length_beats': 16,
              'notes': [
                {
                  'pitch': 'A1',
                  'start_beat': 0,
                  'length_beats': 2,
                  'velocity': 100,
                },
              ],
            },
          ],
        },
        {
          'track_id': existing,
          'name': 'Existing Keys',
          'role': 'chords',
          'clips': [
            {
              'start_beat': 0,
              'length_beats': 8,
              'notes': [
                {
                  'pitch': 60,
                  'start_beat': 0,
                  'length_beats': 4,
                  'velocity': 0.7,
                },
                {
                  'pitch': 64,
                  'start_beat': 0,
                  'length_beats': 4,
                  'velocity': 0.7,
                },
              ],
            },
          ],
        },
      ],
    });

    expect(engine.applyAiArrangement(plan), isTrue, reason: engine.lastError);
    expect(engine.trackCount, 3);
    expect(engine.trackClipCount(existing), 2);

    final summary =
        jsonDecode(engine.aiArrangementResultJson) as Map<String, dynamic>;
    expect(summary['ok'], isTrue);
    expect(summary['created_tracks'], 2);
    expect(summary['note_count'], 5);
    expect(summary['first_track_id'], greaterThan(0));
    expect(summary['first_clip_id'], greaterThan(0));

    final summaryTracks =
        (summary['tracks'] as List).cast<Map<String, dynamic>>();
    final bass = summaryTracks.firstWhere((track) => track['name'] == 'Bass');
    expect(
      engine.trackInstrumentName(bass['track_id'] as int),
      'EZInstruments',
    );

    expect(engine.canUndo, isTrue);
    expect(engine.undo(), isTrue);
    expect(engine.trackCount, 1);
    expect(engine.trackClipCount(existing), 1);
  });

  test('EZOrquestra programs can be selected by token', () {
    engine.newProject(48000);
    final context =
        jsonDecode(
              engine.aiContextJson(
                selectedTrackId: -1,
                anchorSeconds: 0,
                bars: 4,
              ),
            )
            as Map<String, dynamic>;
    final available = context['available_instruments'] as Map<String, dynamic>;
    final programs = (available['ezorq_programs'] as List).cast<String>();
    if (programs.isEmpty) return;

    final program = programs.first;
    final plan = jsonEncode({
      'title': 'Orchestra token',
      'tracks': [
        {
          'name': 'Viola',
          'role': 'strings',
          'instrument': 'ezorq:$program',
          'instrument_name': program,
          'clips': [
            {
              'start_beat': 0,
              'length_beats': 4,
              'notes': [
                {
                  'pitch': 60,
                  'start_beat': 0,
                  'length_beats': 4,
                  'velocity': 0.7,
                },
              ],
            },
          ],
        },
      ],
    });

    expect(engine.applyAiArrangement(plan), isTrue, reason: engine.lastError);
    final summary =
        jsonDecode(engine.aiArrangementResultJson) as Map<String, dynamic>;
    final tracks = (summary['tracks'] as List).cast<Map<String, dynamic>>();
    expect(
      engine.trackInstrumentName(tracks.single['track_id'] as int),
      'EZOrquestra',
    );
  });

  test('orchestral short names resolve instead of failing the save', () {
    engine.newProject(48000);
    final plan = jsonEncode({
      'title': 'Short orchestral names',
      'tracks': [
        {
          'name': 'Flute',
          'role': 'melody',
          'instrument': 'ezorq:flute',
          'clips': [
            {
              'start_beat': 0,
              'length_beats': 4,
              'notes': [
                {
                  'pitch': 72,
                  'start_beat': 0,
                  'length_beats': 4,
                  'velocity': 0.7,
                },
              ],
            },
          ],
        },
        {
          'name': 'Violin',
          'role': 'melody',
          'instrument': 'ezorq:violin',
          'clips': [
            {
              'start_beat': 0,
              'length_beats': 4,
              'notes': [
                {
                  'pitch': 76,
                  'start_beat': 0,
                  'length_beats': 4,
                  'velocity': 0.7,
                },
              ],
            },
          ],
        },
      ],
    });

    expect(engine.applyAiArrangement(plan), isTrue, reason: engine.lastError);
    final summary =
        jsonDecode(engine.aiArrangementResultJson) as Map<String, dynamic>;
    final tracks = (summary['tracks'] as List).cast<Map<String, dynamic>>();
    final flute = tracks.firstWhere((track) => track['name'] == 'Flute');
    final violin = tracks.firstWhere((track) => track['name'] == 'Violin');
    expect(
      engine.trackInstrumentName(flute['track_id'] as int),
      anyOf('EZOrquestra', 'EZInstruments'),
    );
    // There is no violin program in the shipped EZOrquestra library, so the
    // resolver must fall back to GM 40 rather than reject the whole plan.
    expect(
      engine.trackInstrumentName(violin['track_id'] as int),
      'EZInstruments',
    );
  });

  test('unknown EZOrquestra names still fail atomically', () {
    engine.newProject(48000);
    expect(
      engine.applyAiArrangement(
        '{"tracks":[{"name":"Ghost","instrument":"ezorq:not_a_program",'
        '"clips":[{"start_beat":0,"length_beats":4,"notes":[]}]}]}',
      ),
      isFalse,
    );
    expect(engine.lastError, contains('EZOrquestra'));
    expect(engine.trackCount, 0);
  });

  test('invalid plans leave the project untouched', () {
    engine.newProject(48000);
    engine.addTrack('Lead', 0, type: 1);
    final before = engine.trackCount;

    expect(engine.applyAiArrangement('{"tracks": []}'), isFalse);
    expect(engine.lastError, isNotEmpty);
    expect(engine.trackCount, before);

    expect(
      engine.applyAiArrangement(
        '{"tracks":[{"name":"Ghost","instrument":"vst:missing",'
        '"clips":[{"start_beat":0,"length_beats":4,"notes":[]}]}]}',
      ),
      isFalse,
    );
    expect(engine.lastError, contains('not installed'));
    expect(engine.trackCount, before);
  });
}
