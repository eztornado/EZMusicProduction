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
}
