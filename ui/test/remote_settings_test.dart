// Persistencia de los ajustes de control remoto en ui_settings.json.
import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/remote/remote_settings.dart';

void main() {
  late File file;

  setUp(() {
    file = File(
        '${Directory.systemTemp.path}/cadence_remote_settings_test.json');
    if (file.existsSync()) file.deleteSync();
    RemoteSettings.overrideFile(file);
  });

  tearDown(() {
    RemoteSettings.overrideFile(null);
    if (file.existsSync()) file.deleteSync();
  });

  test('sin fichero, el control remoto está apagado por defecto', () {
    RemoteSettings.load();
    expect(RemoteSettings.enabled, isFalse);
    expect(RemoteSettings.port, kDefaultRemotePort);
    expect(RemoteSettings.token, isEmpty);
  });

  test('guardar y releer enabled/port/token', () {
    RemoteSettings.save(enabled: true, port: 50123, token: ' 654321 ');
    RemoteSettings.load();
    expect(RemoteSettings.enabled, isTrue);
    expect(RemoteSettings.port, 50123);
    expect(RemoteSettings.token, '654321');
  });

  test('no pisa otras secciones del fichero compartido', () {
    file.writeAsStringSync(jsonEncode(<String, dynamic>{
      'locale': 'ja',
    }));
    RemoteSettings.save(enabled: true, port: 47823, token: '111111');
    final data = jsonDecode(file.readAsStringSync()) as Map<String, dynamic>;
    expect(data['locale'], 'ja');
    expect((data['remote'] as Map)['token'], '111111');
  });

  test('un puerto inválido cae al valor por defecto', () {
    RemoteSettings.save(port: 70000);
    expect(RemoteSettings.port, kDefaultRemotePort);
  });
}
