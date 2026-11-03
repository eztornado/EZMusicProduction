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
    this.rawPitch,
  });

  final int pitch;
  final double startBeat;
  final double lengthBeats;
  final double velocity;

  /// Original value before the engine-safe 0..127 clamp. Used to report bad
  /// model output instead of silently pretending it was valid.
  final int? rawPitch;
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

  /// Furthest note end on the timeline. Clip lengths can be longer than the
  /// material inside them, so coverage checks must look at the notes too.
  double get noteEndBeat {
    var end = 0.0;
    for (final track in tracks) {
      for (final clip in track.clips) {
        for (final note in clip.notes) {
          final noteEnd = note.startBeat + note.lengthBeats;
          if (noteEnd > end) end = noteEnd;
        }
      }
    }
    return end;
  }

  /// Practical problems the engine would reject or that make the result
  /// unmusical. The dialog uses this to ask the model for one focused repair.
  List<String> issuesForWindow({
    required double startBeat,
    required double endBeat,
  }) {
    final issues = <String>[];
    for (final track in tracks) {
      final notes = track.clips.expand((clip) => clip.notes).toList();
      if (notes.isEmpty) continue;

      var minPitch = notes.first.rawPitch ?? notes.first.pitch;
      var maxPitch = minPitch;
      var minStart = notes.first.startBeat;
      var maxEnd = notes.first.startBeat + notes.first.lengthBeats;
      for (final note in notes.skip(1)) {
        final rawPitch = note.rawPitch ?? note.pitch;
        if (rawPitch < minPitch) minPitch = rawPitch;
        if (rawPitch > maxPitch) maxPitch = rawPitch;
        if (note.startBeat < minStart) minStart = note.startBeat;
        final noteEnd = note.startBeat + note.lengthBeats;
        if (noteEnd > maxEnd) maxEnd = noteEnd;
      }

      if (minPitch < 0 || maxPitch > 127) {
        issues.add(
          "Track '${track.name}' has pitches $minPitch-$maxPitch outside "
          'MIDI 0-127; the engine rejects the whole plan.',
        );
      }
      if (minStart < startBeat - 0.01) {
        issues.add(
          "Track '${track.name}' starts at beat ${minStart.toStringAsFixed(2)} "
          'but the window starts at ${startBeat.toStringAsFixed(2)}.',
        );
      }
      if (maxEnd > endBeat + 0.01) {
        issues.add(
          "Track '${track.name}' reaches beat ${maxEnd.toStringAsFixed(2)} "
          'but the window ends at ${endBeat.toStringAsFixed(2)}.',
        );
      }

      if (_isPiano(track)) {
        final median = _medianPitch(notes);
        final role = track.role.toLowerCase();
        final range = maxPitch - minPitch;
        if ((role == 'melody' || role == 'lead') &&
            range <= 30 &&
            (median < 60 || median > 84)) {
          issues.add(
            "Track '${track.name}' is the melody but its median pitch is "
            '${median.round()}; keep the lead in C4-C6 (60-84).',
          );
        } else if (role == 'bass' && (median < 40 || median > 60)) {
          issues.add(
            "Track '${track.name}' is a bass part but its median pitch is "
            '${median.round()}; keep it in E2-C4 (40-60).',
          );
        }
      }

      final run = _stepwiseRun(notes);
      if (run != null) {
        issues.add(
          "Track '${track.name}' contains a stepwise run of ${run.count} "
          'notes spanning ${run.span} semitones; replace it with chord-tone '
          'arpeggios of the current bar, not a scale or chromatic line.',
        );
      }
    }
    return issues;
  }

  /// Last-resort deterministic guard. It only fixes pitch/register problems;
  /// timing, harmony and orchestration are left to the model. This keeps a
  /// plan playable when the model ignores the MIDI/register rules.
  SongPlan normalizedForPiano({
    double startBeat = 0,
    double beatsPerBar = 4,
  }) {
    final decoded = jsonDecode(extractJsonObject(rawJson));
    if (decoded is! Map<String, dynamic>) return this;

    final tracksValue = decoded['tracks'];
    if (tracksValue is! List) return this;
    final harmony = _parseHarmony('${decoded['harmonic_plan'] ?? ''}');

    for (final trackValue in tracksValue) {
      if (trackValue is! Map) continue;
      final clipsValue = trackValue['clips'];
      if (clipsValue is! List) continue;

      final notes = <Map>[];
      for (final clipValue in clipsValue) {
        if (clipValue is! Map) continue;
        final noteValues = clipValue['notes'];
        if (noteValues is! List) continue;
        for (final noteValue in noteValues) {
          if (noteValue is Map) notes.add(noteValue);
        }
      }
      if (notes.isEmpty) continue;

      final instrument =
          '${trackValue['instrument'] ?? ''} '
                  '${trackValue['instrument_name'] ?? ''}'
              .toLowerCase();
      final isPiano = instrument.contains('piano');
      final role = '${trackValue['role'] ?? ''}'.toLowerCase();

      for (final note in notes) {
        final pitch = note['pitch'];
        if (pitch is! num) continue;
        note['pitch'] = _foldPitch(pitch.round(), 0, 127);
      }
      if (!isPiano) continue;

      final pitches = notes
          .map((note) => (note['pitch'] as num).toInt())
          .toList();
      final low = pitches.reduce((a, b) => a < b ? a : b);
      final high = pitches.reduce((a, b) => a > b ? a : b);
      final range = high - low;

      int targetLow;
      int targetHigh;
      if (role == 'bass') {
        targetLow = 40;
        targetHigh = 60;
      } else if ((role == 'melody' || role == 'lead') && range <= 30) {
        targetLow = 60;
        targetHigh = 84;
      } else if ((role == 'chords' || role == 'arp') && range <= 30) {
        targetLow = 48;
        targetHigh = 72;
      } else {
        targetLow = 21;
        targetHigh = 108;
      }

      for (final note in notes) {
        note['pitch'] = _foldPitch(
          (note['pitch'] as num).toInt(),
          targetLow,
          targetHigh,
        );
      }

      // If the bass is a chromatic/scale run, snap it to the root or fifth of
      // the chord written for that bar. This is the last-resort guard for the
      // exact failure users reported ("arpeggio" from MIDI 22 to 109).
      if (role == 'bass' && harmony.isNotEmpty && beatsPerBar > 0) {
        for (final note in notes) {
          final start = note['start_beat'];
          if (start is! num) continue;
          final bar = ((start.toDouble() - startBeat) / beatsPerBar).floor() + 1;
          final tones = _harmonyAt(harmony, bar);
          if (tones == null) continue;
          note['pitch'] = _nearestChordTone(
            (note['pitch'] as num).toInt(),
            tones.bass,
            targetLow,
            targetHigh,
          );
        }
      }
    }
    return SongPlan.parse(jsonEncode(decoded));
  }

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
              final rawPitch = _rawPitch(noteValue);
              final pitch = (rawPitch ?? 60).clamp(0, 127);
              notes.add(
                SongPlanNote(
                  pitch: pitch,
                  rawPitch: rawPitch,
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

int? _rawPitch(Map<String, dynamic> map) {
  final value = map['pitch'] ?? map['note'] ?? map['midi'];
  if (value is num) return value.round();
  if (value is String) {
    final numeric = int.tryParse(value.trim());
    if (numeric != null) return numeric;
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
      return pitch;
    }
  }
  return null;
}

bool _isPiano(SongPlanTrack track) {
  final text = '${track.instrument} ${track.instrumentName}'.toLowerCase();
  return text.contains('piano');
}

double _medianPitch(List<SongPlanNote> notes) {
  final pitches =
      notes.map((note) => note.rawPitch ?? note.pitch).toList()..sort();
  if (pitches.isEmpty) return 0;
  final middle = pitches.length ~/ 2;
  if (pitches.length.isOdd) return pitches[middle].toDouble();
  return (pitches[middle - 1] + pitches[middle]) / 2;
}

class _StepwiseRun {
  const _StepwiseRun(this.count, this.span);

  final int count;
  final int span;
}

class _HarmonySpan {
  const _HarmonySpan(this.startBar, this.endBar, this.bass);

  final int startBar;
  final int endBar;
  final Set<int> bass;
}

List<_HarmonySpan> _parseHarmony(String text) {
  final spans = <_HarmonySpan>[];
  if (text.trim().isEmpty) return spans;

  for (final part in text.split('|')) {
    final range = RegExp(r'(\d+)(?:\s*-\s*(\d+))?').firstMatch(part);
    if (range == null) continue;
    final start = int.tryParse(range.group(1) ?? '');
    if (start == null) continue;
    final end = int.tryParse(range.group(2) ?? '') ?? start;
    var rest = part.replaceFirst(range.group(0)!, '');
    rest = rest.replaceFirst(
      RegExp(r'^[\s:.\-]*bars?\s*', caseSensitive: false),
      '',
    );
    rest = rest.replaceFirst(RegExp(r'^[\s:.\-]+'), '').trim();
    final tones = _bassTones(rest);
    if (tones.isEmpty) continue;
    spans.add(_HarmonySpan(start, end, tones));
  }
  return spans;
}

Set<int> _bassTones(String symbol) {
  final match = RegExp(r'^([A-Ga-g])([#b]?)').firstMatch(symbol.trim());
  if (match == null) return const <int>{};
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
  var root = base[letter]!;
  final accidental = match.group(2) ?? '';
  if (accidental == '#') root++;
  if (accidental == 'b') root--;

  final rest = symbol.substring(match.end).toLowerCase();
  final isDiminished =
      rest.contains('dim') ||
      rest.contains('°') ||
      rest.contains('ø') ||
      rest.contains('b5');
  final isAugmented = rest.contains('aug') || rest.startsWith('+');
  final fifth = isDiminished ? 6 : (isAugmented ? 8 : 7);
  return {(root % 12 + 12) % 12, (root + fifth) % 12};
}

_HarmonySpan? _harmonyAt(List<_HarmonySpan> spans, int bar) {
  for (final span in spans) {
    if (bar >= span.startBar && bar <= span.endBar) return span;
  }
  return null;
}

int _nearestChordTone(int pitch, Set<int> pitchClasses, int low, int high) {
  var best = pitch.clamp(low, high);
  var bestDistance = 999;
  for (var candidate = low; candidate <= high; candidate++) {
    if (!pitchClasses.contains(candidate % 12)) continue;
    final distance = (candidate - pitch).abs();
    if (distance < bestDistance) {
      bestDistance = distance;
      best = candidate;
    }
  }
  return best;
}

_StepwiseRun? _stepwiseRun(List<SongPlanNote> notes) {
  if (notes.length < 8) return null;
  final ordered = [...notes]
    ..sort((a, b) {
      final byStart = a.startBeat.compareTo(b.startBeat);
      return byStart != 0
          ? byStart
          : (a.rawPitch ?? a.pitch).compareTo(b.rawPitch ?? b.pitch);
    });

  var runCount = 1;
  var runStartIndex = 0;
  var direction = 0;

  for (var i = 1; i < ordered.length; i++) {
    final previous = ordered[i - 1].rawPitch ?? ordered[i - 1].pitch;
    final current = ordered[i].rawPitch ?? ordered[i].pitch;
    final interval = current - previous;
    final sameDirection =
        direction == 0 || interval == 0 || interval.sign == direction;
    if (interval.abs() <= 3 && sameDirection) {
      if (direction == 0 && interval != 0) direction = interval.sign;
      runCount++;
      final start = ordered[runStartIndex].rawPitch ?? ordered[runStartIndex].pitch;
      final span = (current - start).abs();
      if (runCount >= 8 && span >= 12) {
        return _StepwiseRun(runCount, span);
      }
    } else {
      runCount = 1;
      runStartIndex = i;
      direction = 0;
    }
  }
  return null;
}

int _foldPitch(int pitch, int low, int high) {
  if (high < low) return pitch.clamp(0, 127);
  var value = pitch;
  while (value > high) {
    value -= 12;
  }
  while (value < low) {
    value += 12;
  }
  return value.clamp(0, 127);
}
