// Configuration for the optional external arrangement model.
//
// This is interface-only configuration: the engine never reads it. The API
// key lives in a separate file with owner-only permissions, not in the shared
// ui_settings.json and never in project files or logs.

import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

const String kDefaultAiBaseUrl = 'https://openrouter.ai/api/v1';
const String kDefaultAiModel = 'openai/gpt-4.1-mini';
const double kDefaultAiTemperature = 0.8;
const int kDefaultAiMaxTokens = 8192;
const int kDefaultAiTimeoutSeconds = 180;
const int kMinAiTimeoutSeconds = 30;
const int kMaxAiTimeoutSeconds = 1800;
const bool kDefaultAnalyzeBrief = true;
const bool kDefaultAutoRepair = true;

class AiSettings {
  AiSettings._();

  static File? _file;
  static File? _credentials;
  static File? _overrideFile;
  static File? _overrideCredentials;

  @visibleForTesting
  static void overrideFiles(File? settings, File? credentials) {
    _overrideFile = settings;
    _overrideCredentials = credentials;
    _file = null;
    _credentials = null;
  }

  static File get file {
    if (_overrideFile != null) return _overrideFile!;
    _file ??= File('${_home()}/Music/Cadence/ui_settings.json');
    return _file!;
  }

  static File get credentialsFile {
    if (_overrideCredentials != null) return _overrideCredentials!;
    _credentials ??= File('${_home()}/Music/Cadence/ai_credentials.json');
    return _credentials!;
  }

  static String _home() => Platform.environment['HOME'] ?? '.';

  static String baseUrl = kDefaultAiBaseUrl;
  static String model = kDefaultAiModel;
  static String apiKey = '';
  static double temperature = kDefaultAiTemperature;
  static int maxTokens = kDefaultAiMaxTokens;
  static int timeoutSeconds = kDefaultAiTimeoutSeconds;
  static bool analyzeBrief = kDefaultAnalyzeBrief;
  static bool autoRepair = kDefaultAutoRepair;

  static bool get hasKey => apiKey.trim().isNotEmpty;

  /// Reads the saved settings, then lets environment variables override them
  /// (useful for local servers and CI).
  static void load() {
    baseUrl = kDefaultAiBaseUrl;
    model = kDefaultAiModel;
    apiKey = '';
    temperature = kDefaultAiTemperature;
    maxTokens = kDefaultAiMaxTokens;
    timeoutSeconds = kDefaultAiTimeoutSeconds;
    analyzeBrief = kDefaultAnalyzeBrief;
    autoRepair = kDefaultAutoRepair;

    try {
      final f = file;
      if (f.existsSync()) {
        final raw = f.readAsStringSync();
        if (raw.trim().isNotEmpty) {
          final data = jsonDecode(raw);
          if (data is Map && data['ai'] is Map) {
            final ai = data['ai'] as Map;
            final storedBase = ai['base_url'];
            if (storedBase is String && storedBase.trim().isNotEmpty) {
              baseUrl = storedBase.trim();
            }
            final storedModel = ai['model'];
            if (storedModel is String && storedModel.trim().isNotEmpty) {
              model = storedModel.trim();
            }
            final storedTemp = ai['temperature'];
            if (storedTemp is num) {
              temperature = storedTemp.toDouble().clamp(0.0, 2.0);
            }
            final storedTokens = ai['max_tokens'];
            if (storedTokens is num) {
              maxTokens = storedTokens.toInt().clamp(256, 32768);
            }
            final storedTimeout = ai['timeout_seconds'];
            if (storedTimeout is num) {
              timeoutSeconds = storedTimeout.toInt().clamp(
                kMinAiTimeoutSeconds,
                kMaxAiTimeoutSeconds,
              );
            }
            final storedAnalyze = ai['analyze_brief'];
            if (storedAnalyze is bool) analyzeBrief = storedAnalyze;
            final storedRepair = ai['auto_repair'];
            if (storedRepair is bool) autoRepair = storedRepair;
          }
        }
      }
    } catch (e) {
      debugPrint('AiSettings.load: $e');
    }

    try {
      final f = credentialsFile;
      if (f.existsSync()) {
        final raw = f.readAsStringSync();
        if (raw.trim().isNotEmpty) {
          final data = jsonDecode(raw);
          if (data is Map && data['api_key'] is String) {
            apiKey = (data['api_key'] as String).trim();
          }
        }
      }
    } catch (e) {
      debugPrint('AiSettings.load credentials: $e');
    }

    final envBase = Platform.environment['EZ_AI_BASE_URL'];
    if (envBase != null && envBase.trim().isNotEmpty) baseUrl = envBase.trim();
    final envModel = Platform.environment['EZ_AI_MODEL'];
    if (envModel != null && envModel.trim().isNotEmpty) model = envModel.trim();
    final envKey = Platform.environment['EZ_AI_API_KEY'];
    if (envKey != null && envKey.trim().isNotEmpty) apiKey = envKey.trim();
  }

  /// Saves the connection settings. Changing [newKey] also updates the key;
  /// pass an empty string to forget it.
  static void save({
    String? baseUrl,
    String? model,
    String? newKey,
    double? temperature,
    int? maxTokens,
    int? timeoutSeconds,
    bool? analyzeBrief,
    bool? autoRepair,
  }) {
    if (baseUrl != null) AiSettings.baseUrl = baseUrl.trim();
    if (model != null) AiSettings.model = model.trim();
    if (newKey != null) apiKey = newKey.trim();
    if (temperature != null) {
      AiSettings.temperature = temperature.clamp(0.0, 2.0);
    }
    if (maxTokens != null) {
      AiSettings.maxTokens = maxTokens.clamp(256, 32768);
    }
    if (timeoutSeconds != null) {
      AiSettings.timeoutSeconds = timeoutSeconds.clamp(
        kMinAiTimeoutSeconds,
        kMaxAiTimeoutSeconds,
      );
    }
    if (analyzeBrief != null) AiSettings.analyzeBrief = analyzeBrief;
    if (autoRepair != null) AiSettings.autoRepair = autoRepair;

    try {
      final f = file;
      f.parent.createSync(recursive: true);
      Map<String, dynamic> data = <String, dynamic>{};
      if (f.existsSync()) {
        try {
          final decoded = jsonDecode(f.readAsStringSync());
          if (decoded is Map) data = Map<String, dynamic>.from(decoded);
        } catch (_) {
          // A corrupt file is regenerated; the rest of the app does the same.
        }
      }
      data['ai'] = <String, dynamic>{
        'base_url': AiSettings.baseUrl,
        'model': AiSettings.model,
        'temperature': AiSettings.temperature,
        'max_tokens': AiSettings.maxTokens,
        'timeout_seconds': AiSettings.timeoutSeconds,
        'analyze_brief': AiSettings.analyzeBrief,
        'auto_repair': AiSettings.autoRepair,
      };
      const encoder = JsonEncoder.withIndent('  ');
      f.writeAsStringSync('${encoder.convert(data)}\n');
    } catch (e) {
      debugPrint('AiSettings.save: $e');
    }

    try {
      final f = credentialsFile;
      f.parent.createSync(recursive: true);
      final data = <String, dynamic>{if (apiKey.isNotEmpty) 'api_key': apiKey};
      const encoder = JsonEncoder.withIndent('  ');
      f.writeAsStringSync('${encoder.convert(data)}\n');
      if (!Platform.isWindows) {
        // The key is readable only by the current user.
        Process.runSync('chmod', ['600', f.path]);
      }
    } catch (e) {
      debugPrint('AiSettings.save credentials: $e');
    }
  }

  /// True when the endpoint looks like a local server, so the UI can tell the
  /// user a key is optional.
  static bool get isLocalEndpoint {
    final uri = Uri.tryParse(baseUrl);
    final host = uri?.host ?? '';
    return host == 'localhost' || host == '127.0.0.1' || host == '::1';
  }
}
