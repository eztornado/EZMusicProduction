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
    );

    final config = jsonDecode(settings.readAsStringSync()) as Map;
    expect(config['locale'], 'en');
    expect(config['ai']['base_url'], 'http://localhost:1234/v1');
    expect(config['ai']['model'], 'local-model');
    expect(config['ai']['temperature'], 0.55);
    expect(config['ai']['max_tokens'], 4096);
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

    AiSettings.save(newKey: '');
    expect(credentials.readAsStringSync(), isNot(contains('super-secret')));
    AiSettings.load();
    expect(AiSettings.hasKey, isFalse);
  });
}
