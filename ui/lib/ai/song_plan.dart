// Preview model for an AI song arrangement plan.
//
// The engine owns validation and application; this file only turns the model's
// JSON into something the dialog can summarise (tracks, roles, clip and note
// counts) and keeps the original document for the atomic apply call.

import 'dart:convert';

class SongPlanNote {
  const SongPlanNote({
    required this.pitch,
    required this.startBeat,
    required this.lengthBeats,
    required this.velocity,
  });

  final int pitch;
  final double startBeat;
  final double lengthBeats;
  final double velocity;
}

class SongPlanClip {
  const SongPlanClip({
    required this.startBeat,
    required this.lengthBeats,
    required this.notes,
  });

  final double startBeat;
  final double lengthBeats;
  final List<SongPlanNote> notes;
}

class SongPlanTrack {
  const SongPlanTrack({
    required this.trackId,
    required this.name,
    required this.role,
    required this.instrument,
    required this.instrumentName,
    required this.clips,
  });

  /// Existing engine track id, or -1 for a new track.
  final int trackId;
  final String name;
  final String role;
  final String instrument;

  /// Optional readable label; [instrument] may be a long VST UID token.
  final String instrumentName;
  final List<SongPlanClip> clips;

  bool get reusesExisting => trackId > 0;
  int get noteCount => clips.fold(0, (sum, c) => sum + c.notes.length);
  double get lengthBeats => clips.fold<double>(
    0,
    (end, c) =>
        end < c.startBeat + c.lengthBeats ? c.startBeat + c.lengthBeats : end,
  );
}

class SongPlan {
  const SongPlan({
    required this.title,
    required this.summary,
    required this.tracks,
    required this.rawJson,
  });

  final String title;
  final String summary;
  final List<SongPlanTrack> tracks;

  /// The exact document that will be sent to the engine.
  final String rawJson;

  int get noteCount => tracks.fold(0, (sum, t) => sum + t.noteCount);
  int get clipCount => tracks.fold(0, (sum, t) => sum + t.clips.length);
  int get newTrackCount => tracks.where((t) => !t.reusesExisting).length;
  int get reusedTrackCount => tracks.where((t) => t.reusesExisting).length;
  double get lengthBeats => tracks.fold<double>(
    0,
    (end, t) => end < t.lengthBeats ? t.lengthBeats : end,
  );

  static SongPlan parse(String content) {
    final jsonText = extractJsonObject(content);
    final dynamic decoded;
    try {
      decoded = jsonDecode(jsonText);
    } on FormatException catch (e) {
      throw FormatException('El modelo no devolvió JSON válido: ${e.message}');
    }
    if (decoded is! Map<String, dynamic>) {
      throw const FormatException('El plan no es un objeto JSON.');
    }

    var root = decoded;
    for (final key in const ['plan', 'arrangement']) {
      final nested = root[key];
      if (nested is Map<String, dynamic> && nested['tracks'] is List) {
        root = nested;
        break;
      }
    }

    final tracksValue = root['tracks'];
    if (tracksValue is! List || tracksValue.isEmpty) {
      throw const FormatException('El plan no contiene pistas.');
    }

    final tracks = <SongPlanTrack>[];
    for (var i = 0; i < tracksValue.length; i++) {
      final value = tracksValue[i];
      if (value is! Map<String, dynamic>) {
        throw FormatException('tracks[$i] no es un objeto.');
      }
      final clips = <SongPlanClip>[];
      final clipsValue = value['clips'];
      if (clipsValue is List) {
        for (var c = 0; c < clipsValue.length; c++) {
          final clipValue = clipsValue[c];
          if (clipValue is! Map<String, dynamic>) {
            throw FormatException('tracks[$i].clips[$c] no es un objeto.');
          }
          final notes = <SongPlanNote>[];
          final notesValue = clipValue['notes'];
          if (notesValue is List) {
            for (var n = 0; n < notesValue.length; n++) {
              final noteValue = notesValue[n];
              if (noteValue is! Map<String, dynamic>) {
                throw FormatException(
                  'tracks[$i].clips[$c].notes[$n] no es un objeto.',
                );
              }
              final pitch = _pitch(noteValue);
              notes.add(
                SongPlanNote(
                  pitch: pitch,
                  startBeat: _number(noteValue, const [
                    'start_beat',
                    'start_beats',
                    'start',
                    'startBeat',
                  ]),
                  lengthBeats: _number(noteValue, const [
                    'length_beats',
                    'lengthBeats',
                    'length',
                  ], fallback: 0.25),
                  velocity: _velocity(noteValue),
                ),
              );
            }
          }
          clips.add(
            SongPlanClip(
              startBeat: _number(clipValue, const [
                'start_beat',
                'start_beats',
                'start',
                'startBeat',
              ]),
              lengthBeats: _number(clipValue, const [
                'length_beats',
                'lengthBeats',
                'length',
              ], fallback: 4),
              notes: notes,
            ),
          );
        }
      }

      tracks.add(
        SongPlanTrack(
          trackId: _int(value, const ['track_id', 'trackId', 'id']),
          name: _string(value, const ['name'], fallback: 'Pista ${i + 1}'),
          role: _string(value, const ['role']).toLowerCase(),
          instrument:
              _string(value, const ['instrument', 'sound']).toLowerCase(),
          instrumentName: _string(value, const [
            'instrument_name',
            'instrumentName',
          ]),
          clips: clips,
        ),
      );
    }

    return SongPlan(
      title: _string(root, const ['title', 'name']),
      summary: _string(root, const ['summary', 'description', 'notes']),
      tracks: tracks,
      rawJson: jsonEncode(root),
    );
  }
}

/// Strips Markdown fences and prose around the first complete JSON object.
String extractJsonObject(String content) {
  var text = content.trim();
  if (text.startsWith('```')) {
    final firstBreak = text.indexOf('\n');
    if (firstBreak >= 0) text = text.substring(firstBreak + 1);
    if (text.endsWith('```')) {
      text = text.substring(0, text.length - 3);
    }
  }
  text = text.trim();

  final start = text.indexOf('{');
  if (start < 0) {
    throw const FormatException('La respuesta no contiene un objeto JSON.');
  }

  // Find the matching closing brace, ignoring braces inside strings.
  var depth = 0;
  var inString = false;
  var escaped = false;
  for (var i = start; i < text.length; i++) {
    final ch = text[i];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        inString = false;
      }
      continue;
    }
    if (ch == '"') {
      inString = true;
    } else if (ch == '{') {
      depth++;
    } else if (ch == '}') {
      depth--;
      if (depth == 0) return text.substring(start, i + 1);
    }
  }
  throw const FormatException('El JSON del plan está incompleto.');
}

int _int(Map<String, dynamic> map, List<String> keys, {int fallback = -1}) {
  for (final key in keys) {
    final value = map[key];
    if (value is num) return value.toInt();
    if (value is String) {
      final parsed = int.tryParse(value.trim());
      if (parsed != null) return parsed;
    }
  }
  return fallback;
}

double _number(
  Map<String, dynamic> map,
  List<String> keys, {
  double fallback = 0,
}) {
  for (final key in keys) {
    final value = map[key];
    if (value is num) return value.toDouble();
    if (value is String) {
      final parsed = double.tryParse(value.trim());
      if (parsed != null) return parsed;
    }
  }
  return fallback;
}

double _velocity(Map<String, dynamic> map) {
  var value = _number(map, const ['velocity', 'vel'], fallback: 0.8);
  if (value > 1) value /= 127;
  return value.clamp(0.01, 1.0);
}

String _string(
  Map<String, dynamic> map,
  List<String> keys, {
  String fallback = '',
}) {
  for (final key in keys) {
    final value = map[key];
    if (value is String && value.trim().isNotEmpty) return value.trim();
    if (value is num) return '$value';
  }
  return fallback;
}

int _pitch(Map<String, dynamic> map) {
  final value = map['pitch'] ?? map['note'] ?? map['midi'];
  if (value is num) return value.round().clamp(0, 127);
  if (value is String) {
    final numeric = int.tryParse(value.trim());
    if (numeric != null) return numeric.clamp(0, 127);
    final text = value.trim();
    final match = RegExp(r'^([A-Ga-g])([#b]?)(-?\d+)?').firstMatch(text);
    if (match != null) {
      const base = <String, int>{
        'C': 0,
        'D': 2,
        'E': 4,
        'F': 5,
        'G': 7,
        'A': 9,
        'B': 11,
      };
      final letter = match.group(1)!.toUpperCase();
      final accidental = match.group(2) ?? '';
      final octave = int.tryParse(match.group(3) ?? '') ?? 4;
      var pitch = (octave + 1) * 12 + base[letter]!;
      if (accidental == '#') pitch++;
      if (accidental == 'b') pitch--;
      return pitch.clamp(0, 127);
    }
  }
  return 60;
}
