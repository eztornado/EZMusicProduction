import 'dart:convert';
import 'dart:io';

import 'package:cadence_ui/ai/ai_settings.dart';
import 'package:cadence_ui/ai/llm_client.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late Directory tempDir;
  late File settingsFile;
  late File credentialsFile;

  setUp(() {
    tempDir = Directory.systemTemp.createTempSync('cadenza_llm_client');
    settingsFile = File('${tempDir.path}/ui_settings.json');
    credentialsFile = File('${tempDir.path}/ai_credentials.json');
    AiSettings.overrideFiles(settingsFile, credentialsFile);
  });

  tearDown(() {
    AiSettings.overrideFiles(null, null);
    if (tempDir.existsSync()) tempDir.deleteSync(recursive: true);
  });

  Future<HttpServer> serve(
    Future<void> Function(HttpRequest request, int index) handler,
  ) async {
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    var index = 0;
    server.listen((request) async {
      await handler(request, index++);
    });
    return server;
  }

  void configure(int port) {
    AiSettings.save(
      baseUrl: 'http://127.0.0.1:$port/v1',
      model: 'test-model',
      newKey: 'test-secret',
      temperature: 0.7,
      maxTokens: 2048,
      timeoutSeconds: 240,
    );
    AiSettings.load();
  }

  String successBody(String content) => jsonEncode({
    'choices': [
      {
        'message': {'role': 'assistant', 'content': content},
      },
    ],
  });

  test(
    'sends the context with auth and parses the assistant content',
    () async {
      final bodies = <Map<String, dynamic>>[];
      final server = await serve((request, index) async {
        expect(request.uri.path, '/v1/chat/completions');
        expect(
          request.headers.value(HttpHeaders.authorizationHeader),
          'Bearer test-secret',
        );
        bodies.add(
          jsonDecode(await utf8.decoder.bind(request).join())
              as Map<String, dynamic>,
        );
        request.response.headers.contentType = ContentType.json;
        request.response.write(successBody('{"title":"ok","tracks":[]}'));
        await request.response.close();
      });
      addTearDown(server.close);

      configure(server.port);
      final result = await LlmClient().createPlan(
        projectContextJson: '{"tempo_bpm":120}',
        brief: 'house',
        bars: 8,
        startAt: 'bar 1',
      );

      expect(result, '{"title":"ok","tracks":[]}');
      final body = bodies.single;
      expect(body['model'], 'test-model');
      expect(body['max_tokens'], 2048);
      expect(body['temperature'], 0.7);
      expect(body['response_format'], {'type': 'json_object'});
      expect(body['messages'], hasLength(2));
      final userMessage = (body['messages'] as List).last['content'] as String;
      expect(userMessage, contains('house'));
      expect(userMessage, contains('MUSICAL PRIORITIES'));
      expect(userMessage, contains('primary motif'));
      expect(userMessage, contains('voice leading'));
    },
  );

  test(
    'connection test sends a minimal request without project data',
    () async {
      final bodies = <Map<String, dynamic>>[];
      final server = await serve((request, index) async {
        expect(
          request.headers.value(HttpHeaders.authorizationHeader),
          'Bearer probe-secret',
        );
        bodies.add(
          jsonDecode(await utf8.decoder.bind(request).join())
              as Map<String, dynamic>,
        );
        request.response.headers.contentType = ContentType.json;
        request.response.write(successBody('OK'));
        await request.response.close();
      });
      addTearDown(server.close);

      final result = await LlmClient(
        baseUrl: 'http://127.0.0.1:${server.port}/v1',
        model: 'probe-model',
        apiKey: 'probe-secret',
        timeoutSeconds: 45,
      ).testConnection();

      expect(result, 'OK');
      final body = bodies.single;
      expect(body['model'], 'probe-model');
      expect(body['max_tokens'], 16);
      expect(body['temperature'], 0);
      expect(body.containsKey('response_format'), isFalse);
      expect(body['messages'], hasLength(2));
    },
  );

  test(
    'retries without response_format when the server rejects JSON mode',
    () async {
      final bodies = <Map<String, dynamic>>[];
      final server = await serve((request, index) async {
        final body =
            jsonDecode(await utf8.decoder.bind(request).join())
                as Map<String, dynamic>;
        bodies.add(body);
        request.response.headers.contentType = ContentType.json;
        if (index == 0) {
          request.response.statusCode = 400;
          request.response.write(
            jsonEncode({
              'error': {
                'message': 'response_format json_object is unsupported',
              },
            }),
          );
        } else {
          request.response.write(successBody('{"tracks":[{"name":"Drums"}]}'));
        }
        await request.response.close();
      });
      addTearDown(server.close);

      configure(server.port);
      final result = await LlmClient().createPlan(
        projectContextJson: '{}',
        brief: '',
        bars: 4,
        startAt: 'start',
      );

      expect(result, contains('Drums'));
      expect(bodies, hasLength(2));
      expect(bodies.first['response_format'], isNotNull);
      expect(bodies.last.containsKey('response_format'), isFalse);
    },
  );

  test('reports an auth failure without echoing the key', () async {
    final server = await serve((request, index) async {
      await utf8.decoder.bind(request).join();
      request.response.statusCode = 401;
      request.response.headers.contentType = ContentType.json;
      request.response.write(
        jsonEncode({
          'error': {'message': 'invalid api key'},
        }),
      );
      await request.response.close();
    });
    addTearDown(server.close);

    configure(server.port);
    await expectLater(
      LlmClient().createPlan(
        projectContextJson: '{}',
        brief: '',
        bars: 4,
        startAt: 'start',
      ),
      throwsA(
        isA<LlmException>().having(
          (e) => e.message,
          'message',
          allOf(contains('Clave de API'), isNot(contains('test-secret'))),
        ),
      ),
    );
  });
}
