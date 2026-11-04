import 'package:cadence_ui/ai/song_plan.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('parses a complete plan and counts material', () {
    final plan = SongPlan.parse('''
      {
        "title": "Night drive",
        "summary": "drums, bass and keys",
        "tracks": [
          {
            "name": "Drums",
            "role": "drums",
            "instrument": "drums",
            "instrument_name": "EZDrums",
            "clips": [
              {
                "start_beat": 4,
                "length_beats": 16,
                "notes": [
                  {"pitch": 36, "start_beat": 0, "length_beats": 0.25, "velocity": 0.9},
                  {"pitch": 38, "start_beat": 1, "length_beats": 0.25, "velocity": 0.8}
                ]
              }
            ]
          },
          {
            "track_id": 7,
            "name": "Keys",
            "role": "chords",
            "instrument": "keys",
            "clips": [
              {
                "start_beat": 4,
                "length_beats": 8,
                "notes": [
                  {"pitch": "C4", "start_beat": 0, "length_beats": 4, "velocity": 100}
                ]
              }
            ]
          }
        ]
      }
    ''');

    expect(plan.title, 'Night drive');
    expect(plan.tracks, hasLength(2));
    expect(plan.tracks.first.instrumentName, 'EZDrums');
    expect(plan.tracks.last.instrumentName, isEmpty);
    expect(plan.newTrackCount, 1);
    expect(plan.reusedTrackCount, 1);
    expect(plan.clipCount, 2);
    expect(plan.noteCount, 3);
    expect(plan.lengthBeats, 20);

    final keys = plan.tracks.last;
    expect(keys.trackId, 7);
    expect(keys.reusesExisting, isTrue);
    expect(keys.clips.single.notes.single.pitch, 60);
    expect(keys.clips.single.notes.single.velocity, closeTo(100 / 127, 0.0001));
  });

  test('accepts a nested arrangement wrapper and Markdown fences', () {
    final plan = SongPlan.parse('''
      Here is the plan:
      ```json
      {
        "arrangement": {
          "tracks": [
            {"name": "Lead", "role": "melody", "instrument": "lead",
             "clips": [{"start_beat": 0, "length_beats": 4,
                        "notes": [{"pitch": "F#3", "start_beat": 0,
                                   "length_beats": 1}]}]}
          ]
        }
      }
      ```
    ''');

    expect(plan.tracks.single.name, 'Lead');
    expect(plan.tracks.single.clips.single.notes.single.pitch, 54);
    expect(plan.rawJson, contains('"tracks"'));
    expect(plan.rawJson, isNot(contains('```')));
  });

  test('extractJsonObject ignores braces inside strings', () {
    const text = 'text {"summary": "a { b } c", "tracks": []} tail';
    expect(extractJsonObject(text), '{"summary": "a { b } c", "tracks": []}');
  });

  test('rejects plans without tracks', () {
    expect(
      () => SongPlan.parse('{"title": "empty"}'),
      throwsA(isA<FormatException>()),
    );
    expect(() => SongPlan.parse('not json'), throwsA(isA<FormatException>()));
  });

  test('reports invalid MIDI, low melody and stepwise runs', () {
    final plan = SongPlan.parse('''
      {
        "tracks": [
          {"name": "Melody", "role": "melody", "instrument": "piano",
           "clips": [{"start_beat": 0, "length_beats": 8,
             "notes": [
               {"pitch": 40, "start_beat": 0, "length_beats": 1},
               {"pitch": 43, "start_beat": 1, "length_beats": 1},
               {"pitch": 45, "start_beat": 2, "length_beats": 1},
               {"pitch": 47, "start_beat": 3, "length_beats": 1},
               {"pitch": 48, "start_beat": 4, "length_beats": 1},
               {"pitch": 47, "start_beat": 5, "length_beats": 1},
               {"pitch": 45, "start_beat": 6, "length_beats": 1},
               {"pitch": 43, "start_beat": 7, "length_beats": 1}
             ]}]},
          {"name": "Bass", "role": "bass", "instrument": "piano",
           "clips": [{"start_beat": 0, "length_beats": 8,
             "notes": [
               {"pitch": 130, "start_beat": 0, "length_beats": 1},
               {"pitch": 132, "start_beat": 1, "length_beats": 1},
               {"pitch": 135, "start_beat": 2, "length_beats": 1},
               {"pitch": 137, "start_beat": 3, "length_beats": 1},
               {"pitch": 140, "start_beat": 4, "length_beats": 1},
               {"pitch": 142, "start_beat": 5, "length_beats": 1},
               {"pitch": 145, "start_beat": 6, "length_beats": 1},
               {"pitch": 147, "start_beat": 7, "length_beats": 1}
             ]}]}
        ]
      }
    ''');

    final issues = plan.issuesForWindow(startBeat: 0, endBeat: 8);
    expect(issues.any((issue) => issue.contains('outside MIDI')), isTrue);
    expect(issues.any((issue) => issue.contains('median pitch')), isTrue);
    expect(issues.any((issue) => issue.contains('stepwise run')), isTrue);
    expect(plan.tracks.last.clips.single.notes.first.pitch, 127);
    expect(plan.tracks.last.clips.single.notes.first.rawPitch, 130);
  });

  test('normalizes piano register and folds invalid MIDI pitches', () {
    final plan = SongPlan.parse('''
      {
        "harmonic_plan": "bars 1-2 Bm | 3-4 G",
        "tracks": [
          {"name": "Melody", "role": "melody", "instrument": "piano",
           "clips": [{"start_beat": 0, "length_beats": 4,
             "notes": [
               {"pitch": 40, "start_beat": 0, "length_beats": 1},
               {"pitch": 43, "start_beat": 1, "length_beats": 1},
               {"pitch": 47, "start_beat": 2, "length_beats": 1}
             ]}]},
          {"name": "Bass", "role": "bass", "instrument": "piano",
           "clips": [{"start_beat": 0, "length_beats": 8,
             "notes": [
               {"pitch": 130, "start_beat": 0, "length_beats": 1},
               {"pitch": 132, "start_beat": 1, "length_beats": 1},
               {"pitch": 135, "start_beat": 2, "length_beats": 1},
               {"pitch": 137, "start_beat": 3, "length_beats": 1},
               {"pitch": 140, "start_beat": 4, "length_beats": 1},
               {"pitch": 142, "start_beat": 5, "length_beats": 1},
               {"pitch": 145, "start_beat": 6, "length_beats": 1},
               {"pitch": 147, "start_beat": 7, "length_beats": 1}
             ]}]}
        ]
      }
    ''');

    final normalized = plan.normalizedForPiano(
      startBeat: 0,
      beatsPerBar: 4,
    );
    final melodyNotes = normalized.tracks.first.clips.single.notes;
    final bassNotes = normalized.tracks.last.clips.single.notes;
    expect(melodyNotes.every((n) => n.pitch >= 60 && n.pitch <= 84), isTrue);
    expect(bassNotes.every((n) => n.pitch >= 40 && n.pitch <= 60), isTrue);
    expect(
      bassNotes.every((n) => const {11, 6}.contains(n.pitch % 12)),
      isTrue,
    );
    expect(normalized.rawJson, contains('"pitch"'));
  });
}
