// Persistencia del modo debug en ui_settings.json.
import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/debug/debug_settings.dart';

void main() {
  late File file;

  setUp(() {
    file = File(
      '${Directory.systemTemp.path}/cadence_debug_settings_test.json',
    );
    if (file.existsSync()) file.deleteSync();
    DebugSettings.overrideFile(file);
    DebugSettings.save(enabled: false);
  });

  tearDown(() {
    DebugSettings.overrideFile(null);
    if (file.existsSync()) file.deleteSync();
  });

  test('sin fichero, el modo debug está apagado por defecto', () {
    file.deleteSync();
    DebugSettings.load();
    expect(DebugSettings.enabled, isFalse);
    expect(DebugSettings.notifier.value, isFalse);
  });

  test('guardar y releer el modo debug', () {
    DebugSettings.save(enabled: true);
    expect(DebugSettings.notifier.value, isTrue);
    DebugSettings.load();
    expect(DebugSettings.enabled, isTrue);
    expect(DebugSettings.notifier.value, isTrue);
  });

  test('no pisa otras secciones del fichero compartido', () {
    file.writeAsStringSync(
      jsonEncode(<String, dynamic>{'locale': 'ja'}),
    );
    DebugSettings.save(enabled: true);
    final data = jsonDecode(file.readAsStringSync()) as Map<String, dynamic>;
    expect(data['locale'], 'ja');
    expect((data['debug'] as Map)['enabled'], isTrue);
  });
}
