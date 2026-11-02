// El modo debug registra los prompts enviados y recibidos por Arreglo IA.
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/ai/ai_settings.dart';
import 'package:cadence_ui/ai/llm_client.dart';
import 'package:cadence_ui/debug/debug_log.dart';
import 'package:cadence_ui/debug/debug_settings.dart';

void main() {
  late Directory tempDir;
  late File settingsFile;
  late File credentialsFile;
  late File logFile;

  setUp(() {
    tempDir = Directory.systemTemp.createTempSync('cadence_debug_log');
    settingsFile = File('${tempDir.path}/ui_settings.json');
    credentialsFile = File('${tempDir.path}/ai_credentials.json');
    logFile = File('${tempDir.path}/debug.log');
    AiSettings.overrideFiles(settingsFile, credentialsFile);
    DebugSettings.overrideFile(settingsFile);
    DebugLog.overrideFile(logFile);
    DebugSettings.save(enabled: true);
  });

  tearDown(() {
    DebugSettings.save(enabled: false);
    DebugSettings.overrideFile(null);
    DebugLog.resetForTest();
    AiSettings.overrideFiles(null, null);
    if (tempDir.existsSync()) tempDir.deleteSync(recursive: true);
  });

  test('con el modo debug apagado no se escribe nada', () {
    DebugSettings.save(enabled: false);
    DebugLog.write('ia', 'no debería aparecer');
    expect(logFile.existsSync(), isFalse);
  });

  test('captura debugPrint con el modo debug activo', () {
    DebugLog.configure();
    debugPrint('mensaje de consola');
    expect(logFile.readAsStringSync(), contains('mensaje de consola'));
  });

  test('registra el prompt de petición y la respuesta del modelo', () async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(server.close);
    server.listen((request) async {
      await utf8.decoder.bind(request).join();
      expect(
        request.headers.value(HttpHeaders.authorizationHeader),
        'Bearer test-secret',
      );
      request.response.headers.contentType = ContentType.json;
      request.response.write(
        jsonEncode({
          'choices': [
            {
              'message': {
                'role': 'assistant',
                'content': '{"title":"ok","tracks":[]}',
              },
            },
          ],
        }),
      );
      await request.response.close();
    });

    AiSettings.save(
      baseUrl: 'http://127.0.0.1:${server.port}/v1',
      model: 'debug-model',
      newKey: 'test-secret',
    );

    final result = await LlmClient().createPlan(
      projectContextJson: '{"tempo_bpm":120}',
      brief: 'orchestral sketch',
      bars: 8,
      startAt: 'bar 1',
    );
    expect(result, '{"title":"ok","tracks":[]}');

    final log = logFile.readAsStringSync();
    expect(log, contains('[ia'));
    expect(log, contains('PROJECT CONTEXT'));
    expect(log, contains('orchestral sketch'));
    expect(log, contains('← HTTP 200'));
    expect(log, contains('← assistant'));
    expect(log, contains('{"title":"ok","tracks":[]}'));
    expect(log, isNot(contains('test-secret')));
  });
}
