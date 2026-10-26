// Remote-control protocol test — real WebSocket client against the engine.
//
// The server queues mutating commands for the engine message thread. In this
// headless host the JUCE async queue is not pumped, so the test drains it with
// cadence_engine_pump_remote() from a Dart timer, exactly like the app's frame
// ticker does.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/cadence_bindings.dart';

void main() {
  late CadenceEngine engine;
  late WebSocket socket;
  late Map<String, dynamic> welcome;
  final pending = <int, Completer<Map<String, dynamic>>>{};
  Timer? pump;
  var nextId = 1;

  Future<Map<String, dynamic>> request(String cmd,
      [Map<String, Object?> args = const {}]) {
    final id = nextId++;
    final completer = Completer<Map<String, dynamic>>();
    pending[id] = completer;
    socket.add(jsonEncode(<String, Object?>{
      'v': 1,
      'id': id,
      'cmd': cmd,
      ...args,
    }));
    return completer.future.timeout(const Duration(seconds: 5));
  }

  setUpAll(() async {
    engine = CadenceEngine();
    expect(engine.newProject(48000), CadenceResult.ok);
    expect(engine.addTrack('Drums', 0), 1);

    expect(engine.startRemoteServer(port: 0, token: '123456'),
        CadenceResult.ok);
    expect(engine.remoteServerRunning, isTrue);
    expect(engine.remoteServerToken, '123456');

    socket = await WebSocket.connect(
        'ws://127.0.0.1:${engine.remoteServerPort}');
    socket.listen((data) {
      final message = jsonDecode(data as String) as Map<String, dynamic>;
      final id = message['id'];
      if (id is int) pending.remove(id)?.complete(message);
    });
    pump = Timer.periodic(const Duration(milliseconds: 10),
        (_) => engine.pumpRemoteCommands());

    welcome = await request('hello', <String, Object?>{
      'token': '123456',
      'client': <String, Object?>{'name': 'flutter-test'},
    });
  });

  tearDownAll(() async {
    pump?.cancel();
    await socket.close();
    engine.stopRemoteServer();
    engine.dispose();
  });

  test('hello authenticates and returns the initial state', () {
    expect(welcome['ok'], isTrue);
    final result = welcome['result'] as Map<String, dynamic>;
    expect(result['protocol'], 1);
    expect(result['clientId'], greaterThan(0));
    final state = result['state'] as Map<String, dynamic>;
    expect(state['tracks'], hasLength(1));
    expect(state['revision'], isA<int>());
  });

  test('a wrong pairing code is rejected', () async {
    final bad = await WebSocket.connect(
        'ws://127.0.0.1:${engine.remoteServerPort}');
    addTearDown(bad.close);
    bad.add(jsonEncode(<String, Object?>{
      'v': 1,
      'id': 1,
      'cmd': 'hello',
      'token': '000000',
    }));
    final raw = await bad.first.timeout(const Duration(seconds: 5));
    final reply = jsonDecode(raw as String) as Map<String, dynamic>;
    expect(reply['ok'], isFalse);
    expect((reply['error'] as Map)['code'], 'auth_failed');
  });

  test('ping and state.get answer inline', () async {
    final pong = await request('ping');
    expect(pong['ok'], isTrue);
    expect((pong['result'] as Map)['pong'], isTrue);

    final state = await request('state.get');
    expect(state['ok'], isTrue);
    final result = state['result'] as Map<String, dynamic>;
    expect((result['state'] as Map)['name'], 'Untitled');
    expect((result['transport'] as Map)['playing'], isFalse);
  });

  test('mixer.setTrack mutates the document without undo spam', () async {
    final reply = await request('mixer.setTrack', <String, Object?>{
      'trackId': 1,
      'volume': 0.42,
      'pan': -0.3,
      'mute': true,
      'name': 'Drums remote',
      'undo': false,
    });
    expect(reply['ok'], isTrue);
    expect((reply['result'] as Map)['trackId'], 1);

    final state = await request('state.get');
    final track =
        ((state['result'] as Map)['state'] as Map)['tracks'][0] as Map;
    expect(track['volume'], closeTo(0.42, 1e-6));
    expect(track['pan'], closeTo(-0.3, 1e-6));
    expect(track['mute'], isTrue);
    expect(track['name'], 'Drums remote');
  });

  test('transport edits bump the revision', () async {
    final before = await request('transport.get');
    final beforeRevision = (before['result'] as Map)['revision'] as int;

    final tempo = await request('transport.tempo', <String, Object?>{
      'bpm': 133,
    });
    expect(tempo['ok'], isTrue);
    expect((tempo['result'] as Map)['tempoBpm'], 133.0);
    expect((tempo['result'] as Map)['revision'] as int,
        greaterThan(beforeRevision));

    final metronome =
        await request('transport.metronome', <String, Object?>{'enabled': true});
    expect((metronome['result'] as Map)['metronome'], isTrue);
  });

  test('unknown track returns a structured error', () async {
    final reply = await request('mixer.setTrack', <String, Object?>{
      'trackId': 9999,
      'volume': 0.5,
    });
    expect(reply['ok'], isFalse);
    final error = reply['error'] as Map;
    expect(error['code'], 'not_found');
    expect(error['message'], isNotEmpty);
  });

  test('subscribe toggles the meter stream', () async {
    final reply = await request('subscribe', <String, Object?>{
      'meters': false,
      'metersHz': 5,
    });
    expect(reply['ok'], isTrue);
    expect((reply['result'] as Map)['meters'], isFalse);
    expect((reply['result'] as Map)['metersHz'], 5);
  });
}
