// OpenAI-compatible chat client for the song arranger.
//
// The engine never talks to the network. This client sends the project
// context plus the creative brief and returns the model's JSON document; the
// engine validates and applies it atomically afterwards.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import '../debug/debug_log.dart';
import 'ai_settings.dart';

/// The brief-analysis call is intentionally short: it only produces a compact
/// musical direction. The arranger call keeps the user's configured timeout.
const int kBriefAnalysisTimeoutSeconds = 180;
const int kBriefAnalysisMaxTokens = 1400;
const double kBriefAnalysisTemperature = 0.55;

/// Rough budget used only for the debug warning. A real model context depends
/// on the server's n_ctx/num_ctx setting; this is the common local default.
const int kTelemetryContextWarningTokens = 8192;
const double kTelemetryCharsPerToken = 4.0;

class LlmException implements Exception {
  const LlmException(this.message);
  final String message;

  @override
  String toString() => message;
}

class _ArrangerWindow {
  const _ArrangerWindow({
    required this.startBeat,
    required this.endBeat,
    required this.lengthBeats,
  });

  final double startBeat;
  final double endBeat;
  final double lengthBeats;
}

_ArrangerWindow _arrangerWindow(String contextJson, int bars) {
  Map<String, dynamic>? context;
  try {
    final decoded = jsonDecode(contextJson);
    if (decoded is Map<String, dynamic>) context = decoded;
  } on FormatException {
    // The engine always sends valid JSON; keep the arranger usable if a test
    // or a future client sends a partial context.
  }

  var beatsPerBar = (context?['beats_per_bar'] as num?)?.toDouble() ?? 4.0;
  if (!beatsPerBar.isFinite || beatsPerBar <= 0) beatsPerBar = 4.0;
  final startBeat = (context?['anchor_beat'] as num?)?.toDouble() ?? 0.0;
  final lengthBeats =
      (context?['window_beats'] as num?)?.toDouble() ?? bars * beatsPerBar;
  final endBeat =
      (context?['window_end_beat'] as num?)?.toDouble() ??
      startBeat + lengthBeats;

  return _ArrangerWindow(
    startBeat: startBeat,
    endBeat: endBeat,
    lengthBeats: lengthBeats,
  );
}

String _beatLabel(double value) {
  if ((value - value.roundToDouble()).abs() < 0.0001) {
    return value.toStringAsFixed(0);
  }
  return value.toStringAsFixed(2);
}

/// The analyst does not need every GM program or every long VST token. Keep
/// the first call small and focused on musical decisions; the arranger call
/// still receives the full catalog and exact tokens.
String _briefAnalysisContext(String contextJson) {
  try {
    final decoded = jsonDecode(contextJson);
    if (decoded is! Map<String, dynamic>) return contextJson;

    final instruments = decoded['available_instruments'];
    final instrumentMap =
        instruments is Map<String, dynamic> ? instruments : null;
    final vstNames =
        (instrumentMap?['vst_instruments'] as List?)
            ?.whereType<Map>()
            .map((plugin) => plugin['name'])
            .whereType<String>()
            .toList() ??
        const <String>[];

    final tracks =
        (decoded['tracks'] as List?)
            ?.whereType<Map>()
            .map(
              (track) => {
                'name': track['name'],
                'role': track['role'],
                'instrument': track['instrument'],
                'instrument_name': track['instrument_name'],
              },
            )
            .toList() ??
        const <Map<String, dynamic>>[];

    final compact = <String, dynamic>{
      'tempo_bpm': decoded['tempo_bpm'],
      'time_signature':
          '${decoded['time_signature_numerator'] ?? 4}/'
          '${decoded['time_signature_denominator'] ?? 4}',
      'beats_per_bar': decoded['beats_per_bar'],
      'requested_bars': decoded['requested_bars'],
      'has_material': decoded['has_material'],
      'key': decoded['key'],
      'chord_line': decoded['chord_line'],
      'track_count': decoded['track_count'],
      'tracks': tracks,
      'available_instruments': {
        'ezsynth_presets': instrumentMap?['ezsynth_presets'],
        'ezorq_programs': instrumentMap?['ezorq_programs'],
        'vst_names': vstNames,
        'gm_program_count':
            (instrumentMap?['ezinst_programs'] as List?)?.length ?? 128,
      },
    };
    return const JsonEncoder.withIndent('  ').convert(compact);
  } catch (_) {
    return contextJson;
  }
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
    DebugLog.write('ia', '✗ solicitud cancelada por el usuario');
    _client?.close(force: true);
    _client = null;
  }

  /// First pass: turns the user's brief into a compact musical direction.
  ///
  /// This is deliberately a separate, shorter call. It does not write MIDI;
  /// it researches the requested style and gives the arranger concrete
  /// decisions (instrumentation, scale, harmonic language, patterns, form and
  /// constraints). The second call receives this document as a contract.
  Future<String> analyzeBrief({
    required String projectContextJson,
    required String brief,
    required int bars,
    required String startAt,
  }) {
    final window = _arrangerWindow(projectContextJson, bars);
    final user =
        StringBuffer()
          ..writeln('PROJECT CONTEXT (compact, read-only):')
          ..writeln(_briefAnalysisContext(projectContextJson))
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
          ..writeln(
            '- Cover exactly $bars bars = ${_beatLabel(window.lengthBeats)} '
            'quarter-note beats.',
          )
          ..writeln(
            '- Beat span: ${_beatLabel(window.startBeat)} to '
            '${_beatLabel(window.endBeat)}.',
          )
          ..writeln()
          ..writeln(
            'Research the reference from your musical knowledge and return the '
            'compact musical direction JSON now. Do not write notes or a track '
            'plan.',
          );

    final timeout =
        _timeoutSeconds < kBriefAnalysisTimeoutSeconds
            ? _timeoutSeconds
            : kBriefAnalysisTimeoutSeconds;
    final maxTokens =
        _maxTokens < kBriefAnalysisMaxTokens
            ? _maxTokens
            : kBriefAnalysisMaxTokens;
    return _send(
      [
        {'role': 'system', 'content': briefAnalysisPrompt},
        {'role': 'user', 'content': user.toString()},
      ],
      maxTokens: maxTokens,
      temperature: kBriefAnalysisTemperature,
      timeoutSeconds: timeout,
      operation: 'analysis',
    );
  }

  /// Asks the model for a complete arrangement plan.
  Future<String> createPlan({
    required String projectContextJson,
    required String brief,
    required int bars,
    required String startAt,
    String? musicDirectionJson,
  }) {
    final window = _arrangerWindow(projectContextJson, bars);
    final beatsPerBar = window.lengthBeats / bars;
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
          ..writeln('MUSIC DIRECTION (from the brief analysis — follow it):')
          ..writeln(
            (musicDirectionJson ?? '').trim().isEmpty
                ? '(none; use the creative brief directly)'
                : musicDirectionJson!.trim(),
          )
          ..writeln(
            'If this direction contradicts the explicit brief or the project '
            'context, the brief/context wins.',
          )
          ..writeln()
          ..writeln('ARRANGEMENT WINDOW:')
          ..writeln('- Start at: $startAt')
          ..writeln(
            '- Cover exactly $bars bars = ${_beatLabel(window.lengthBeats)} '
            'quarter-note beats.',
          )
          ..writeln(
            '- Beat span: ${_beatLabel(window.startBeat)} (inclusive) to '
            '${_beatLabel(window.endBeat)} (exclusive). The furthest note or '
            'clip must reach beat ${_beatLabel(window.endBeat)}.',
          )
          ..writeln(
            '- One bar is ${_beatLabel(beatsPerBar)} beats. Do not use the bar '
            'count as a beat count; a ${_beatLabel(beatsPerBar)}-beat clip is '
            'only one bar.',
          )
          ..writeln(
            '- Use project quarter-note beats for every position and length.',
          )
          ..writeln()
          ..writeln('MUSICAL PRIORITIES:')
          ..writeln(
            '- Decide key/mode, form, harmonic rhythm and a primary motif before '
            'writing notes.',
          )
          ..writeln(
            '- If the context has no key or chord_line, choose a coherent key/mode '
            'and progression and state them in the output.',
          )
          ..writeln(
            '- Use voice leading, contrast, counterpoint and space; write '
            'idiomatically for the featured instruments.',
          )
          ..writeln(
            '- Make the requested bar count a real arc, not a copied loop.',
          )
          ..writeln()
          ..writeln('Return the complete JSON arrangement plan now.');

    return _send([
      {'role': 'system', 'content': systemPrompt},
      {'role': 'user', 'content': user.toString()},
    ], operation: 'arranger');
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
    String? musicDirectionJson,
  }) {
    final window = _arrangerWindow(projectContextJson, bars);
    final beatsPerBar = window.lengthBeats / bars;
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
          ..writeln('MUSIC DIRECTION (from the brief analysis — follow it):')
          ..writeln(
            (musicDirectionJson ?? '').trim().isEmpty
                ? '(none; use the creative brief directly)'
                : musicDirectionJson!.trim(),
          )
          ..writeln(
            'If this direction contradicts the explicit brief or the project '
            'context, the brief/context wins.',
          )
          ..writeln()
          ..writeln('ARRANGEMENT WINDOW:')
          ..writeln('- Start at: $startAt')
          ..writeln(
            '- Cover exactly $bars bars = ${_beatLabel(window.lengthBeats)} '
            'quarter-note beats.',
          )
          ..writeln(
            '- Beat span: ${_beatLabel(window.startBeat)} (inclusive) to '
            '${_beatLabel(window.endBeat)} (exclusive). The furthest note or '
            'clip must reach beat ${_beatLabel(window.endBeat)}.',
          )
          ..writeln(
            '- One bar is ${_beatLabel(beatsPerBar)} beats. Do not use the bar '
            'count as a beat count.',
          )
          ..writeln()
          ..writeln('MUSICAL PRIORITIES:')
          ..writeln(
            '- Preserve the identity, but deepen the motif, harmony, voice '
            'leading, counterpoint, register balance and energy arc.',
          )
          ..writeln(
            '- If the context has no key or chord_line, keep one coherent '
            'key/mode and progression and state them.',
          )
          ..writeln()
          ..writeln('PREVIOUS PLAN (JSON):')
          ..writeln(previousPlanJson)
          ..writeln()
          ..writeln('REFINEMENT REQUEST:')
          ..writeln(
            feedback.trim().isEmpty
                ? 'Improve the arrangement as a composer: stronger motif and '
                    'development, more purposeful harmonic motion and voice '
                    'leading, clearer counterpoint and register balance, better '
                    'idiomatic writing for each instrument, and a more convincing '
                    'energy arc. Keep the identity and what already works.'
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
    ], operation: 'repair');
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
      operation: 'test',
    );
  }

  static int _requestSequence = 0;

  String _estimateTokens(int chars) =>
      (chars / kTelemetryCharsPerToken).ceil().toString();

  Future<String> _send(
    List<Map<String, dynamic>> messages, {
    bool jsonMode = true,
    int? maxTokens,
    double? temperature,
    int? timeoutSeconds,
    String? operation,
  }) async {
    if (_cancelled) throw const LlmException('Solicitud cancelada.');
    if (_baseUrl.isEmpty) {
      throw const LlmException('Configura la URL del servidor de IA.');
    }
    if (_model.isEmpty) {
      throw const LlmException('Configura el modelo de IA.');
    }

    final waitSeconds = timeoutSeconds ?? _timeoutSeconds;
    final uri = _chatUri(_baseUrl);
    final requestId = ++_requestSequence;
    final stage = operation ?? 'request';
    final clock = Stopwatch()..start();
    final deadline = Duration(seconds: waitSeconds);
    final client =
        HttpClient()
          ..connectionTimeout = const Duration(seconds: 20)
          ..idleTimeout = const Duration(seconds: 30);
    _client = client;
    var outcome = 'error';

    DebugLog.write(
      'ia',
      'telemetría · id=$requestId · etapa=$stage · inicio · modelo=$_model · '
          'endpoint=$uri · timeout=${waitSeconds}s · json=${jsonMode ? 'on' : 'off'}',
    );

    try {
      // The deadline is for the whole request, not per HTTP phase. Without
      // this a header wait plus a body wait (and a JSON-mode retry) could take
      // 4x the configured timeout and look like a hang.
      var content = await _post(
        client,
        uri,
        messages,
        includeJsonMode: jsonMode,
        maxTokens: maxTokens,
        temperature: temperature,
        requestId: requestId,
        stage: stage,
      ).timeout(deadline);
      // Some OpenAI-compatible servers reject response_format. Retry once
      // without it; the plan parser still recovers a fenced JSON object.
      if (content == null && jsonMode) {
        final remaining = deadline.inMilliseconds - clock.elapsedMilliseconds;
        if (remaining <= 0) {
          throw TimeoutException(
            'Sin tiempo para reintentar sin JSON mode.',
            deadline,
          );
        }
        DebugLog.write(
          'ia',
          'telemetría · id=$requestId · etapa=$stage · reintento sin JSON '
              'mode · restante≈${(remaining / 1000).toStringAsFixed(1)}s',
        );
        content = await _post(
          client,
          uri,
          messages,
          includeJsonMode: false,
          maxTokens: maxTokens,
          temperature: temperature,
          requestId: requestId,
          stage: stage,
        ).timeout(Duration(milliseconds: remaining));
      }
      if (content == null || content.trim().isEmpty) {
        DebugLog.write('ia', '✗ respuesta vacía');
        throw const LlmException('El modelo devolvió una respuesta vacía.');
      }
      outcome = 'ok';
      DebugLog.write('ia', '← assistant\n$content');
      return content;
    } on TimeoutException {
      outcome = 'timeout';
      DebugLog.write('ia', '✗ timeout tras $waitSeconds s esperando a $uri');
      throw LlmException(
        'El servidor de IA no respondió en $waitSeconds s. '
        'Revisa la conexión o prueba otro modelo.',
      );
    } on SocketException catch (e) {
      outcome = 'socket_error';
      DebugLog.write('ia', '✗ conexión fallida con $uri: ${e.message}');
      throw LlmException(
        'No se pudo conectar con el servidor de IA: ${e.message}',
      );
    } catch (e) {
      outcome = e is LlmException ? 'error' : 'unexpected_error';
      DebugLog.write(
        'ia',
        'telemetría · id=$requestId · etapa=$stage · error=${e.runtimeType} · '
            '$e',
      );
      rethrow;
    } finally {
      DebugLog.write(
        'ia',
        'telemetría · id=$requestId · etapa=$stage · fin=$outcome · '
            'total_ms=${clock.elapsedMilliseconds}',
      );
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
    required int requestId,
    required String stage,
    int? maxTokens,
    double? temperature,
  }) async {
    final connectClock = Stopwatch()..start();
    final request = await client.postUrl(uri);
    request.headers.contentType = ContentType.json;
    // Local servers (especially llama.cpp) can leave a keep-alive connection
    // half-busy after a long non-streaming completion. Force a fresh socket
    // for every attempt so the second stage never inherits a stale one.
    request.headers.set(HttpHeaders.connectionHeader, 'close');
    final key = _apiKey;
    if (key.isNotEmpty) {
      request.headers.set(HttpHeaders.authorizationHeader, 'Bearer $key');
    }
    // Harmless for generic servers; used by some gateways for attribution.
    request.headers.set('X-Title', 'EZMusicProduction');
    request.headers.set('HTTP-Referer', 'https://ezmusicproduction.local');

    final maxOutputTokens = maxTokens ?? _maxTokens;
    final effectiveTemperature = temperature ?? _temperature;
    final body = <String, dynamic>{
      'model': _model,
      'messages': messages,
      'temperature': effectiveTemperature,
      'max_tokens': maxOutputTokens,
      'stream': false,
      if (includeJsonMode) 'response_format': {'type': 'json_object'},
    };
    final bodyJson = jsonEncode(body);
    var promptChars = 0;
    for (final message in messages) {
      final content = message['content'];
      if (content is String) promptChars += content.length;
    }
    final promptTokens = (promptChars / kTelemetryCharsPerToken).ceil();
    DebugLog.write(
      'ia',
      'telemetría · id=$requestId · etapa=$stage · conexión_ms='
          '${connectClock.elapsedMilliseconds} · prompt_chars=$promptChars · '
          'prompt_tokens≈$promptTokens · max_tokens=$maxOutputTokens · '
          'temperatura=${effectiveTemperature.toStringAsFixed(2)} · '
          'cuerpo_chars=${bodyJson.length}',
    );
    if (promptTokens + maxOutputTokens > kTelemetryContextWarningTokens) {
      DebugLog.write(
        'ia',
        '⚠ telemetría · id=$requestId · etapa=$stage · presupuesto≈'
            '${promptTokens + maxOutputTokens} tokens (prompt≈$promptTokens + '
            'salida=$maxOutputTokens). Si n_ctx=8192, no cabe: reduce el contexto '
            'o max_tokens, o sube n_ctx en el servidor.',
      );
    }
    // El modo debug registra el prompt completo (sin cabeceras ni API key).
    DebugLog.write(
      'ia',
      '→ $_model · $uri · JSON ${includeJsonMode ? 'on' : 'off'}\n'
          '${const JsonEncoder.withIndent('  ').convert(body)}',
    );
    request.write(bodyJson);

    final headersClock = Stopwatch()..start();
    final response = await request.close();
    DebugLog.write(
      'ia',
      'telemetría · id=$requestId · etapa=$stage · cabeceras_http='
          '${response.statusCode} · cabeceras_ms=${headersClock.elapsedMilliseconds}',
    );

    final textBuffer = StringBuffer();
    var firstByteMs = -1;
    final bodyClock = Stopwatch()..start();
    await for (final chunk in response.transform(utf8.decoder)) {
      if (firstByteMs < 0) {
        firstByteMs = bodyClock.elapsedMilliseconds;
        DebugLog.write(
          'ia',
          'telemetría · id=$requestId · etapa=$stage · primer_byte_ms='
              '$firstByteMs',
        );
      }
      textBuffer.write(chunk);
    }
    final text = textBuffer.toString();
    DebugLog.write(
      'ia',
      'telemetría · id=$requestId · etapa=$stage · cuerpo_ms='
          '${bodyClock.elapsedMilliseconds} · primer_byte_ms=$firstByteMs · '
          'respuesta_chars=${text.length} · respuesta_tokens≈'
          '${_estimateTokens(text.length)}',
    );
    DebugLog.write('ia', '← HTTP ${response.statusCode} · $uri\n$text');

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
    final usage = decoded['usage'];
    final finishReason = first['finish_reason'];
    DebugLog.write(
      'ia',
      'telemetría · id=$requestId · etapa=$stage · finish='
          '${finishReason ?? 'n/a'} · '
          'usage=${usage is Map ? jsonEncode(usage) : 'n/a'}',
    );
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

const String briefAnalysisPrompt = '''
You are a music director, style analyst and orchestrator. You do not write
MIDI or a track plan. Your job is to turn a short user brief plus the project
context into a precise musical direction for a separate MIDI arranger.

WHAT TO DO
- Extract every explicit constraint: requested instruments, ensemble size,
  section or role (intro, verse, drop, outro), length, mood, era, references
  and things to avoid. Explicit user instructions win over your taste.
- If the brief names an artist, film, game, composer or style, translate it
  into concrete musical traits: genre, harmonic language, scales/modes,
  characteristic progressions, melodic contours, rhythmic patterns,
  instrumentation, orchestration, texture, dynamics and form. Describe
  devices and stylistic grammar, not a specific copyrighted melody. If you do
  not know the named reference, use the closest style family and say so
  instead of inventing facts.
- If the project context is blank or key is "unspecified", choose a key/mode,
  a tempo feel and a harmonic plan that fit the brief. If the project already
  has a key, chord line or material, respect it.
- Be specific enough for MIDI: give a bar map, register plan, voicings,
  rhythmic density, velocity/articulation character and orchestration. Avoid
  vague adjectives such as "emotional" or "cinematic" unless you say exactly
  what produces them.
- Respect the scope. A piano intro stays a self-sufficient solo-piano piece;
  do not add strings, brass, choir, drums or pads unless the brief or the
  project requires them. Do not turn an intro into a full climax by default.
  For solo piano, plan one piano part with a two-hand texture, not a separate
  left-hand track and right-hand track.
- The project tempo is authoritative. If the style wants a slower feel,
  describe a half-time or long-note feel; do not pretend the tempo changed.
- Do not invent installed sounds. You may name instrument families and
  describe timbres; the arranger will map them to the available catalog.

OUTPUT
Return exactly one JSON object. No markdown, no code fences, no commentary.
{
  "interpretation": "what the user actually asked for, including explicit constraints",
  "genre": "specific genre or style",
  "mood": "concrete mood and energy",
  "tempo_feel": "e.g. slow half-time at 120 BPM, or 68-76 BPM",
  "key_mode": "B minor / D dorian / ...",
  "scale": "mode, characteristic degrees and color tones",
  "harmonic_language": "functional/modal/planing..., tensions and cadences",
  "characteristic_progressions": ["i-VI-III-VII", "..."],
  "melodic_devices": ["motif contour, sequence, ..."],
  "rhythmic_patterns": ["..."],
  "instrumentation": ["solo piano"],
  "arrangement_scope": "solo piano intro; no strings/brass/drums",
  "form": "bars 1-4 ...; bars 5-8 ...",
  "texture": "register separation, density and space",
  "register_plan": "melody C4-C6; bass E2-C4; ...",
  "velocity_and_articulation": "how the parts should be played",
  "avoid": ["..."],
  "arranger_brief": "a precise paragraph the arranger must follow; include the explicit constraints and the musical identity",
  "confidence": 0.0
}
The arranger receives this object, not your reasoning. Make every field
useful and internally consistent. Do not add fields that contradict the
explicit brief.
''';

const String systemPrompt = '''
You are a professional composer, orchestrator, arranger and MIDI programmer
working inside a DAW. You combine conservatory-level harmony, counterpoint and
orchestration with the instincts of a modern producer. You receive a read-only
project context and a creative brief. Turn the brief into a complete, original,
idiomatic multi-track arrangement that sounds written by a musician, not
assembled from loops.

MUSICAL APPROACH
- The user message may include a MUSIC DIRECTION document from a style
  analyst. Treat its concrete decisions as a contract, but the explicit brief
  and project context win if they conflict.
- Interpret the brief rather than literalising it. Infer genre, mood, era,
  energy, instrumentation, texture and production style. If it references an
  artist, film, game or style, translate it into concrete musical traits
  (harmonic language, rhythm, orchestration, dynamics, timbre, form) and use
  original material. Never reproduce an identifiable existing melody. A short
  brief is normal: make coherent musical decisions instead of asking for
  clarification.
- The brief's explicit instrumentation and scope are constraints, not
  suggestions. If it asks for a piano intro, write a self-sufficient solo
  piano piece and do not add strings, brass, choir, pads, drums or percussion
  unless the brief also asks for them. Do not turn an intro into a full
  orchestral climax by default. Add an instrument only when the brief or an
  existing track requires it.
- Establish a tonal centre and a real harmonic plan. When the context provides
  a key and chord_line, respect them; you may use functional substitutions,
  secondary dominants, modal interchange, suspensions, pedal points and
  chromatic passing chords when they serve the direction. When the context is
  empty or key is "unspecified", choose the key/mode and progression that best
  fit the brief and state them in the output.
- Map the harmonic plan to concrete bars before writing notes (for example
  "bars 1-2 Bm | 3-4 G | 5-6 D | 7-8 F#m"). Every track must follow that grid.
  The bass should state the current root or a deliberate inversion, and chord
  or arpeggio notes must be chord tones of that span. Non-chord tones must
  resolve by step. Do not write a chromatic scale and call it an arpeggio.
- Write harmony with tension and release, not one chord per bar repeated
  mechanically. Vary harmonic rhythm, inversions and bass motion. Let cadences
  mark phrases.
- Invent a primary motif: a recognisable rhythmic and intervallic cell. Develop
  it through repetition, sequence, inversion, augmentation, diminution,
  fragmentation and call-and-response. Give the melody rests, a clear peak and
  a sense of destination. The lead line should be singable or hummable, not a
  scale run or an arpeggio exercise.
- Apply professional voice leading: keep common tones, prefer stepwise motion,
  resolve tendency tones, and use suspensions, appoggiaturas, passing tones and
  anticipations for expression. Avoid parallel fifths/octaves in exposed outer
  voices unless the style deliberately uses them (power chords, planing).
  Avoid muddy low-register intervals. Think in independent layers: bass
  foundation, harmonic bed, melody, countermelody, rhythmic texture and accents.
- Write idiomatically for every instrument: range, breath/bow/stick limits,
  chord voicing, and articulation implied by note length and velocity. For a
  featured piano, use a real two-hand texture: melody, bass, arpeggios or broken
  chords, pedal-like sustain through note lengths, and textural contrast between
  phrases.
- For a solo piano brief, write ONE piano track with a real two-hand texture.
  Do not split the left and right hands into separate tracks unless the brief
  explicitly asks for multiple pianos. For a piano-led cue, keep the melody in
  the right hand, usually between C4 and C6 (MIDI 60-84), and the
  bass/harmony in the left hand from E2 to C4 (40-60). Do not put the lead line
  below C4 unless the brief explicitly asks for a dark low-register texture.
  Spell the current chord in arpeggios and broken chords; do not write a
  chromatic run, glissando or one-note-per-subdivision scale as an arpeggio.
  Give the piano at least one harmonic event per bar and a melody phrase every
  2 or 4 bars; do not leave a long intro as isolated single low notes.
- Shape form and energy across the requested window: intro, exposition,
  development, contrast, climax, release/outro as appropriate. Use transitions,
  fills, drops, suspensions and cadences. The requested bar count is exact; do
  not silently shorten it.
- Count the window in beats, not bars. A 16-bar 4/4 window is 64 quarter-note
  beats, not 16. The user message gives the exact start and end beat; every
  clip and note must fit that span. A 32-beat clip covers 8 bars at 4/4, not
  32 bars.
- For windows longer than 8 bars, think in 2-, 4- and 8-bar phrases. Repeat
  ideas for identity, then vary the last bar, add fills, re-orchestrate or
  reharmonise so the music develops instead of copy-pasting.
- Use velocity, note length, small off-grid timing, swing and ghost notes for
  human phrasing when the style calls for it.
- Respect the context tempo. At 120 BPM an eighth note is fast; a contemplative
  intro needs longer note values and more space. Do not use "rubato" as an
  excuse for notes that ignore the shared beat grid; use only small timing
  offsets for expression.
- Leave frequency and rhythmic space. Do not make every instrument play all the
  time, and do not bury the idea under a constant wall of 16th notes. A few
  memorable, well-placed ideas beat dense filler.
- Before returning, check: clear tonal centre? forward harmonic motion?
  memorable motif and development? independent bass/melody/harmony? dynamic
  arc? idiomatic writing? space and contrast? Revise anything that is only
  technically valid but musically empty.
- Final arithmetic check: does the furthest note or clip end exactly at the
  requested end beat? Does every bar follow the chord you listed? Is the lead
  in a singable register? Did you add instruments the brief did not ask for?
  If not, fix the plan before returning.

OUTPUT CONTRACT
Return exactly one JSON object. No markdown, no code fences, no commentary.
{
  "title": "short arrangement name",
  "summary": "the musical concept in one or two sentences",
  "key": "D minor",
  "form": "intro 8 | A 8 | B 8 | outro 8",
  "harmonic_plan": "bars 1-2 Dm | 3-4 Bb | 5-6 F | 7-8 C",
  "motif": "short description of the rhythmic/intervallic cell",
  "tracks": [
    {
      "track_id": -1,
      "name": "Piano",
      "role": "melody",
      "instrument": "piano",
      "instrument_name": "Piano",
      "color": 2,
      "volume_db": -3,
      "pan": 0,
      "clips": [
        {
          "start_beat": 0,
          "length_beats": 64,
          "notes": [
            {"pitch": 72, "start_beat": 0, "length_beats": 1, "velocity": 0.8}
          ]
        }
      ]
    }
  ]
}
The example clip is 64 beats = 16 bars at 4/4; always use the exact beat span
in the user message. The key/form/harmonic_plan/motif fields are compact
planning notes; fill them before writing notes. The engine reads
tracks/clips/notes.

TECHNICAL RULES
- All positions and lengths are quarter-note beats on the project timeline,
  not seconds and not bars. The user message gives the exact start and end
  beat. A 4/4 bar is 4 beats: 16 bars = 64 beats. Never use the bar count as
  a beat length.
- To reuse an existing track, pass its numeric track_id from the context and
  write new clips for it. Omit track_id (or use -1) to create a new track.
- Never delete or rewrite existing clips. Add material; leave space for it.
- Keep the project key and harmony unless the brief explicitly asks to change
  them. When the context has a chord_line, follow it or use clear functional
  substitutions.
- Start at or after the requested start beat and cover the requested number of
  bars. Do not put notes outside a clip. A clip's length_beats must be at least
  as long as its last note.
- Pitches are MIDI note numbers 0-127 (middle C = 60). This is a hard limit:
  a single pitch of 128 or 190 makes the whole plan invalid and the engine
  rejects it. Before returning, scan every pitch and fold/fix any value
  outside 0-127. Velocity is 0.01-1.
- For bass, chord and arpeggio tracks, every pitch class must be a chord tone
  of the current bar (root, third, fifth, or a listed seventh/tension). A
  descending or ascending chromatic line is not an arpeggio. If a run moves by
  semitone for more than two notes, stop and rewrite it using the chord.
- A solo piano plan has one piano track, even if it contains both hands. Do
  not create a second "left hand" or "bass piano" track for the same piano.
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
- Do not mix GM names with the `ezorq:` prefix. For example,
  `ezorq:string_ensemble_1` is invalid: use `gm:48` for a GM string ensemble or
  an exact entry from `ezorq_programs`.
- `instrument_name` is a short human-readable label for the UI. For a VST use
  its listed name; for an EZ token use the preset or program name.
- Roles: drums, bass, chords, melody, pad, arp, texture, fx, strings, brass,
  woodwinds, percussion.
- Make parts idiomatic and playable: bass with roots and motion, chords voiced
  in a useful register, melody with motifs and rests, drums with groove and
  fills. Leave frequency space between parts.
- The engine accepts up to 16 tracks, 16 clips per track and 12,000 notes
  total. These are safety limits, not targets. The response must also fit the
  model's output-token budget: use the smallest number of tracks and notes that
  fully expresses the idea, and expand only when the arrangement gains
  contrast, counterpoint or orchestration. A solo piano intro may be one
  complete two-hand track; a full orchestral cue may use many.
- Organize the requested window into a coherent arc (intro, verse, chorus,
  bridge, outro or a brief-specific form). Do not repeat an identical dense
  bar for the whole window; develop it.
- Use the existing tracks when they already serve a role. If a role is missing,
  create a track for it. Do not add a second track with the same role unless
  the brief asks for doubling.
- Only include instruments and tracks that the arrangement actually needs.
''';
