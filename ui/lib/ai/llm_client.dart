// OpenAI-compatible chat client for the song arranger.
//
// The engine never talks to the network. This client sends the project
// context plus the creative brief and returns the model's JSON document; the
// engine validates and applies it atomically afterwards.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'ai_settings.dart';

class LlmException implements Exception {
  const LlmException(this.message);
  final String message;

  @override
  String toString() => message;
}

class LlmClient {
  LlmClient({
    String? baseUrl,
    String? model,
    String? apiKey,
    double? temperature,
    int? maxTokens,
    int? timeoutSeconds,
  }) : _baseUrlOverride = baseUrl,
       _modelOverride = model,
       _apiKeyOverride = apiKey,
       _temperatureOverride = temperature,
       _maxTokensOverride = maxTokens,
       _timeoutOverride = timeoutSeconds;

  final String? _baseUrlOverride;
  final String? _modelOverride;
  final String? _apiKeyOverride;
  final double? _temperatureOverride;
  final int? _maxTokensOverride;
  final int? _timeoutOverride;

  HttpClient? _client;
  bool _cancelled = false;

  bool get isBusy => _client != null;
  bool get isCancelled => _cancelled;

  String get _baseUrl => (_baseUrlOverride ?? AiSettings.baseUrl).trim();
  String get _model => (_modelOverride ?? AiSettings.model).trim();
  String get _apiKey => (_apiKeyOverride ?? AiSettings.apiKey).trim();
  double get _temperature => _temperatureOverride ?? AiSettings.temperature;
  int get _maxTokens => _maxTokensOverride ?? AiSettings.maxTokens;
  int get _timeoutSeconds => (_timeoutOverride ?? AiSettings.timeoutSeconds)
      .clamp(kMinAiTimeoutSeconds, kMaxAiTimeoutSeconds);

  /// Stops an in-flight request. Its future completes with [LlmException].
  void cancel() {
    _cancelled = true;
    _client?.close(force: true);
    _client = null;
  }

  /// Asks the model for a complete arrangement plan.
  Future<String> createPlan({
    required String projectContextJson,
    required String brief,
    required int bars,
    required String startAt,
  }) {
    final user =
        StringBuffer()
          ..writeln('PROJECT CONTEXT (read-only JSON):')
          ..writeln(projectContextJson)
          ..writeln()
          ..writeln('CREATIVE BRIEF:')
          ..writeln(
            brief.trim().isEmpty
                ? 'Make a complete, musical arrangement that fits the project.'
                : brief.trim(),
          )
          ..writeln()
          ..writeln('ARRANGEMENT WINDOW:')
          ..writeln('- Start at: $startAt')
          ..writeln('- Cover $bars bars from that point.')
          ..writeln(
            '- Use project quarter-note beats for every position and length.',
          )
          ..writeln()
          ..writeln('Return the complete JSON arrangement plan now.');

    return _send([
      {'role': 'system', 'content': systemPrompt},
      {'role': 'user', 'content': user.toString()},
    ]);
  }

  /// Second pass: sends the previous plan back with feedback and asks for a
  /// complete replacement. Used by the dialog's "Refine" action.
  Future<String> refinePlan({
    required String projectContextJson,
    required String brief,
    required int bars,
    required String startAt,
    required String previousPlanJson,
    required String feedback,
  }) {
    final user =
        StringBuffer()
          ..writeln('PROJECT CONTEXT (read-only JSON):')
          ..writeln(projectContextJson)
          ..writeln()
          ..writeln('CREATIVE BRIEF:')
          ..writeln(
            brief.trim().isEmpty
                ? 'Make a complete, musical arrangement that fits the project.'
                : brief.trim(),
          )
          ..writeln()
          ..writeln('ARRANGEMENT WINDOW:')
          ..writeln('- Start at: $startAt')
          ..writeln('- Cover $bars bars from that point.')
          ..writeln()
          ..writeln('PREVIOUS PLAN (JSON):')
          ..writeln(previousPlanJson)
          ..writeln()
          ..writeln('REFINEMENT REQUEST:')
          ..writeln(
            feedback.trim().isEmpty
                ? 'Improve the arrangement: stronger groove, better voice leading, '
                    'clearer contrast between sections and more idiomatic parts. '
                    'Keep what already works.'
                : feedback.trim(),
          )
          ..writeln()
          ..writeln(
            'Return a COMPLETE replacement plan in the same schema. '
            'Do not return a diff or commentary.',
          );

    return _send([
      {'role': 'system', 'content': systemPrompt},
      {'role': 'user', 'content': user.toString()},
    ]);
  }

  /// Minimal request used by the settings dialog to verify the endpoint, the
  /// model and the credentials without sending any project data.
  Future<String> testConnection() {
    return _send(
      [
        {
          'role': 'system',
          'content':
              'You are a connectivity test. Reply with the single word OK.',
        },
        {'role': 'user', 'content': 'Reply with OK.'},
      ],
      jsonMode: false,
      maxTokens: 16,
      temperature: 0,
    );
  }

  Future<String> _send(
    List<Map<String, dynamic>> messages, {
    bool jsonMode = true,
    int? maxTokens,
    double? temperature,
  }) async {
    if (_cancelled) throw const LlmException('Solicitud cancelada.');
    if (_baseUrl.isEmpty) {
      throw const LlmException('Configura la URL del servidor de IA.');
    }
    if (_model.isEmpty) {
      throw const LlmException('Configura el modelo de IA.');
    }

    final uri = _chatUri(_baseUrl);
    final client =
        HttpClient()
          ..connectionTimeout = const Duration(seconds: 20)
          ..idleTimeout = const Duration(seconds: 30);
    _client = client;

    try {
      var content = await _post(
        client,
        uri,
        messages,
        includeJsonMode: jsonMode,
        maxTokens: maxTokens,
        temperature: temperature,
      );
      // Some OpenAI-compatible servers reject response_format. Retry once
      // without it; the plan parser still recovers a fenced JSON object.
      if (content == null && jsonMode) {
        content = await _post(
          client,
          uri,
          messages,
          includeJsonMode: false,
          maxTokens: maxTokens,
          temperature: temperature,
        );
      }
      if (content == null || content.trim().isEmpty) {
        throw const LlmException('El modelo devolvió una respuesta vacía.');
      }
      return content;
    } on TimeoutException {
      throw LlmException(
        'El servidor de IA no respondió en $_timeoutSeconds s. '
        'Sube el tiempo de espera en Ajustes de IA o prueba otro modelo.',
      );
    } on SocketException catch (e) {
      throw LlmException(
        'No se pudo conectar con el servidor de IA: ${e.message}',
      );
    } finally {
      if (identical(_client, client)) _client = null;
      client.close(force: true);
    }
  }

  /// One HTTP attempt. Returns the assistant text, or null when the server
  /// rejected the JSON response mode (the caller retries without it).
  Future<String?> _post(
    HttpClient client,
    Uri uri,
    List<Map<String, dynamic>> messages, {
    required bool includeJsonMode,
    int? maxTokens,
    double? temperature,
  }) async {
    final request = await client.postUrl(uri);
    request.headers.contentType = ContentType.json;
    final key = _apiKey;
    if (key.isNotEmpty) {
      request.headers.set(HttpHeaders.authorizationHeader, 'Bearer $key');
    }
    // Harmless for generic servers; used by some gateways for attribution.
    request.headers.set('X-Title', 'EZMusicProduction');
    request.headers.set('HTTP-Referer', 'https://ezmusicproduction.local');

    final body = <String, dynamic>{
      'model': _model,
      'messages': messages,
      'temperature': temperature ?? _temperature,
      'max_tokens': maxTokens ?? _maxTokens,
      'stream': false,
      if (includeJsonMode) 'response_format': {'type': 'json_object'},
    };
    request.write(jsonEncode(body));

    final wait = Duration(seconds: _timeoutSeconds);
    final response = await request.close().timeout(wait);
    final text = await response.transform(utf8.decoder).join().timeout(wait);

    if (response.statusCode < 200 || response.statusCode >= 300) {
      if (includeJsonMode && _isJsonModeRejection(response.statusCode, text)) {
        return null;
      }
      throw LlmException(_httpError(response.statusCode, text));
    }

    final dynamic decoded;
    try {
      decoded = jsonDecode(text);
    } on FormatException {
      throw LlmException(
        'El servidor devolvió algo que no es JSON (HTTP ${response.statusCode}).',
      );
    }

    if (decoded is! Map<String, dynamic>) {
      throw const LlmException('Respuesta inesperada del servidor de IA.');
    }
    final error = decoded['error'];
    if (error != null) {
      throw LlmException(
        'El servidor de IA devolvió un error: ${_short(error)}',
      );
    }

    final choices = decoded['choices'];
    if (choices is! List || choices.isEmpty) {
      throw const LlmException('La respuesta no contiene ninguna elección.');
    }
    final first = choices.first;
    if (first is! Map) {
      throw const LlmException('La respuesta del modelo está mal formada.');
    }
    final message = first['message'];
    if (message is! Map) {
      throw const LlmException('La respuesta no contiene ningún mensaje.');
    }

    final rawContent = message['content'];
    if (rawContent is String) return rawContent;
    if (rawContent is List) {
      final buffer = StringBuffer();
      for (final part in rawContent) {
        if (part is Map && part['text'] is String) {
          buffer.write(part['text']);
        }
      }
      if (buffer.isNotEmpty) return buffer.toString();
    }
    final refusal = message['refusal'];
    if (refusal is String && refusal.trim().isNotEmpty) {
      throw LlmException('El modelo rechazó la petición: $refusal');
    }
    throw const LlmException('El modelo no devolvió contenido de texto.');
  }

  Uri _chatUri(String rawBase) {
    var base = rawBase.trim();
    while (base.endsWith('/')) {
      base = base.substring(0, base.length - 1);
    }
    if (base.endsWith('/chat/completions')) return Uri.parse(base);
    return Uri.parse('$base/chat/completions');
  }

  bool _isJsonModeRejection(int status, String body) {
    if (status != 400 && status != 404 && status != 422) return false;
    final lower = body.toLowerCase();
    return lower.contains('response_format') ||
        lower.contains('json_object') ||
        lower.contains('json mode');
  }

  String _httpError(int status, String body) {
    final detail = _short(_extractServerMessage(body));
    final hint = switch (status) {
      401 || 403 => 'Clave de API rechazada. Revísala en Ajustes de IA.',
      404 => 'Endpoint o modelo no encontrado. Revisa la URL y el modelo.',
      429 => 'El servidor está limitando las peticiones. Espera y reintenta.',
      >= 500 => 'El servidor de IA falló. Reintenta en unos segundos.',
      _ => 'La petición de IA falló.',
    };
    return detail.isEmpty
        ? '$hint (HTTP $status)'
        : '$hint (HTTP $status): $detail';
  }

  String _extractServerMessage(String body) {
    try {
      final decoded = jsonDecode(body);
      if (decoded is Map) {
        final error = decoded['error'];
        if (error is Map) {
          final message = error['message'];
          if (message is String) return message;
        }
        if (error is String) return error;
        final message = decoded['message'];
        if (message is String) return message;
      }
    } catch (_) {
      // Plain text error body.
    }
    return body;
  }

  String _short(Object? value) {
    final text = value?.toString().trim() ?? '';
    if (text.length <= 500) return text;
    return '${text.substring(0, 500)}…';
  }
}

const String systemPrompt = '''
You are an expert music producer, arranger and MIDI programmer working inside
a DAW. You receive a read-only project context and a creative brief. Compose a
complete multi-track arrangement that fits the existing material.

OUTPUT CONTRACT
Return exactly one JSON object. No markdown, no code fences, no commentary.
{
  "title": "short arrangement name",
  "summary": "one or two sentences about the musical idea",
  "tracks": [
    {
      "track_id": 7,
      "name": "Drums",
      "role": "drums",
      "instrument": "drums",
      "instrument_name": "EZDrums",
      "color": 2,
      "volume_db": -3,
      "pan": 0,
      "clips": [
        {
          "start_beat": 0,
          "length_beats": 32,
          "notes": [
            {"pitch": 36, "start_beat": 0, "length_beats": 0.25, "velocity": 0.95}
          ]
        }
      ]
    }
  ]
}

RULES
- All positions and lengths are quarter-note beats on the project timeline,
  not seconds and not bars. A 4/4 bar is 4 beats unless the context says
  otherwise.
- To reuse an existing track, pass its numeric track_id from the context and
  write new clips for it. Omit track_id (or use -1) to create a new track.
- Never delete or rewrite existing clips. Add material; leave space for it.
- Keep the project key and harmony unless the brief explicitly asks to change
  them. The context lists the chord per bar; follow it or use clear
  substitutions.
- Start at or after the requested start beat and cover the requested number of
  bars. Do not put notes outside a clip. A clip's length_beats must be at least
  as long as its last note.
- Pitches are MIDI note numbers 0-127 (middle C = 60). Velocity is 0.01-1.
- Drums use GM notes: 36 kick, 38 snare, 39 clap, 42 closed hat, 44 pedal hat,
  46 open hat, 49 crash, 51 ride, 37 side stick, 41/43/45/47 toms.
- Instrument tokens: drums, piano, bass, sub, keys, pad, lead, pluck, organ,
  bell, acid, riser, ezsynth:<preset>, gm:<0-127>, ezorq:<program> and
  vst:<uid>. The context's `available_instruments` object is the authoritative
  catalog of sounds installed on this computer: use its exact names and tokens,
  and never invent a VST or preset that is not listed.
- For orchestral music use `ezorq:<program>` for every instrument the
  EZOrquestra catalog provides (violas, cellos, basses, flute, oboe, clarinet,
  bassoon, horn, trumpet, trombone, tuba, sax, piccolo) and `gm:` for the rest
  (for example gm:40 violin, gm:48 string ensemble, gm:47 timpani, gm:46 harp).
  Prefer an exact string from `ezorq_programs`; short names such as
  `ezorq:flute`, `ezorq:oboe` or `ezorq:cello` also resolve to the nearest
  installed program or a GM fallback. Do not use EZSynth pads or synth strings
  for orchestral parts unless the brief explicitly asks for a synth/hybrid
  orchestra.
- `instrument_name` is a short human-readable label for the UI. For a VST use
  its listed name; for an EZ token use the preset or program name.
- Roles: drums, bass, chords, melody, pad, arp, texture, fx, strings, brass,
  woodwinds, percussion.
- Make parts idiomatic and playable: bass with roots and motion, chords voiced
  in a useful register, melody with motifs and rests, drums with groove and
  fills. Leave frequency space between parts.
- Organize the requested number of bars into a coherent arc (intro, verse,
  chorus, bridge, outro) when the window allows it. The requested count is
  explicit in the user message; do not silently shorten it.
- Be economical: at most 8 tracks, 24 clips, 1600 notes total. Do not repeat
  identical dense patterns beyond what is needed to state the idea.
- Use the existing tracks when they already serve a role. If a role is missing,
  create a track for it. Do not add a second track with the same role unless
  the brief asks for doubling.
- Only include instruments and tracks that the arrangement actually needs.
''';
