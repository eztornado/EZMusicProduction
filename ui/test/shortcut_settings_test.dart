// Persistence and conflict checks for the editable keyboard shortcuts.

import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/shortcuts/shortcut_settings.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late Directory temp;

  setUp(() {
    temp = Directory.systemTemp.createTempSync('cadence_shortcuts_');
    ShortcutSettings.overrideFile(File('${temp.path}/ui_settings.json'));
    ShortcutSettings.load();
  });

  tearDown(() {
    ShortcutSettings.overrideFile(null);
    temp.deleteSync(recursive: true);
  });

  test('defaults cover every action', () {
    for (final action in ShortcutAction.values) {
      expect(ShortcutSettings.bindingFor(action), isNotNull);
      expect(ShortcutSettings.isDefault(action), isTrue);
    }
    final save = ShortcutSettings.bindingFor(ShortcutAction.saveProject);
    expect(save.key, LogicalKeyboardKey.keyS);
    expect(save.primary, isTrue);
  });

  test('primary uses the platform main modifier', () {
    const binding = ShortcutBinding(
      LogicalKeyboardKey.keyP,
      primary: true,
    );
    final activator = binding.activator;
    expect(activator.meta, Platform.isMacOS);
    expect(activator.control, !Platform.isMacOS);
  });

  test('custom bindings survive a reload', () {
    const binding = ShortcutBinding(
      LogicalKeyboardKey.keyP,
      primary: true,
      shift: true,
    );
    expect(
      ShortcutSettings.setBinding(ShortcutAction.saveProject, binding),
      isTrue,
    );

    final file = File('${temp.path}/ui_settings.json');
    final json = jsonDecode(file.readAsStringSync()) as Map<String, dynamic>;
    final stored = (json['shortcuts'] as Map)['saveProject'] as Map;
    expect(stored['keyId'], LogicalKeyboardKey.keyP.keyId);
    expect(stored['primary'], isTrue);
    expect(stored['shift'], isTrue);

    ShortcutSettings.load();
    final loaded = ShortcutSettings.bindingFor(ShortcutAction.saveProject);
    expect(loaded, binding);
  });

  test('conflicting stored bindings keep their defaults', () {
    File('${temp.path}/ui_settings.json').writeAsStringSync(
      jsonEncode(<String, dynamic>{
        'shortcuts': <String, dynamic>{
          'saveProject': <String, dynamic>{
            'keyId': LogicalKeyboardKey.keyO.keyId,
            'primary': true,
          },
        },
      }),
    );

    ShortcutSettings.load();

    expect(
      ShortcutSettings.bindingFor(ShortcutAction.saveProject),
      ShortcutSettings.defaultFor(ShortcutAction.saveProject),
    );
  });

  test('duplicate bindings are rejected', () {
    final save = ShortcutSettings.bindingFor(ShortcutAction.saveProject);
    expect(
      ShortcutSettings.setBinding(ShortcutAction.openProject, save),
      isFalse,
    );
    expect(
      ShortcutSettings.bindingFor(ShortcutAction.openProject),
      ShortcutSettings.defaultFor(ShortcutAction.openProject),
    );
  });

  test('reset restores one action and keeps the rest', () {
    const binding = ShortcutBinding(LogicalKeyboardKey.keyP);
    ShortcutSettings.setBinding(ShortcutAction.playPause, binding);
    expect(ShortcutSettings.isDefault(ShortcutAction.playPause), isFalse);

    expect(ShortcutSettings.reset(ShortcutAction.playPause), isNull);
    expect(ShortcutSettings.isDefault(ShortcutAction.playPause), isTrue);
  });

  test('reset all restores every default', () {
    ShortcutSettings.setBinding(
      ShortcutAction.saveProject,
      const ShortcutBinding(LogicalKeyboardKey.keyP, primary: true),
    );
    ShortcutSettings.setBinding(
      ShortcutAction.record,
      const ShortcutBinding(LogicalKeyboardKey.keyJ),
    );

    ShortcutSettings.resetAll();

    for (final action in ShortcutAction.values) {
      expect(ShortcutSettings.isDefault(action), isTrue);
    }
  });
}
