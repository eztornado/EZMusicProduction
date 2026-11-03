import 'dart:convert';
import 'dart:io';

import 'package:cadence_ui/ai/ai_settings.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('stores the API key separately and preserves other settings', () {
    final dir = Directory.systemTemp.createTempSync('cadenza_ai_settings');
    addTearDown(() {
      AiSettings.overrideFiles(null, null);
      if (dir.existsSync()) dir.deleteSync(recursive: true);
    });

    final settings = File('${dir.path}/ui_settings.json')
      ..writeAsStringSync('{"locale": "en"}');
    final credentials = File('${dir.path}/ai_credentials.json');
    AiSettings.overrideFiles(settings, credentials);

    AiSettings.save(
      baseUrl: 'http://localhost:1234/v1',
      model: 'local-model',
      newKey: 'super-secret',
      temperature: 0.55,
      maxTokens: 4096,
      timeoutSeconds: 420,
      analyzeBrief: false,
      autoRepair: false,
    );

    final config = jsonDecode(settings.readAsStringSync()) as Map;
    expect(config['locale'], 'en');
    expect(config['ai']['base_url'], 'http://localhost:1234/v1');
    expect(config['ai']['model'], 'local-model');
    expect(config['ai']['temperature'], 0.55);
    expect(config['ai']['max_tokens'], 4096);
    expect(config['ai']['timeout_seconds'], 420);
    expect(config['ai']['analyze_brief'], isFalse);
    expect(config['ai']['auto_repair'], isFalse);
    expect(settings.readAsStringSync(), isNot(contains('super-secret')));

    final creds = jsonDecode(credentials.readAsStringSync()) as Map;
    expect(creds['api_key'], 'super-secret');
    if (!Platform.isWindows) {
      final mode = credentials.statSync().mode & 0x1ff;
      expect(mode, 0x180); // 0600
    }

    AiSettings.load();
    expect(AiSettings.baseUrl, 'http://localhost:1234/v1');
    expect(AiSettings.model, 'local-model');
    expect(AiSettings.apiKey, 'super-secret');
    expect(AiSettings.hasKey, isTrue);
    expect(AiSettings.timeoutSeconds, 420);
    expect(AiSettings.analyzeBrief, isFalse);
    expect(AiSettings.autoRepair, isFalse);

    AiSettings.save(timeoutSeconds: 5);
    expect(AiSettings.timeoutSeconds, kMinAiTimeoutSeconds);
    AiSettings.save(timeoutSeconds: 9999);
    expect(AiSettings.timeoutSeconds, kMaxAiTimeoutSeconds);

    AiSettings.save(newKey: '');
    expect(credentials.readAsStringSync(), isNot(contains('super-secret')));
    AiSettings.load();
    expect(AiSettings.hasKey, isFalse);
  });

  test('analyzes the brief and repairs plans by default', () {
    final dir = Directory.systemTemp.createTempSync('cadenza_ai_defaults');
    addTearDown(() {
      AiSettings.overrideFiles(null, null);
      if (dir.existsSync()) dir.deleteSync(recursive: true);
    });

    final settings = File('${dir.path}/ui_settings.json')
      ..writeAsStringSync(
        '{"ai": {"base_url": "http://localhost:1234/v1", '
        '"model": "local-model"}}',
      );
    AiSettings.overrideFiles(settings, File('${dir.path}/creds.json'));

    AiSettings.load();
    expect(AiSettings.analyzeBrief, isTrue);
    expect(AiSettings.autoRepair, isTrue);
  });
}
