// Checks that the Atajos preferences section captures and persists a new
// key combination through its dialog.

import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/shortcuts/shortcut_settings.dart';
import 'package:cadence_ui/ui/preferences_dialog.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('shortcut row captures a new key combination', (tester) async {
    final temp = Directory.systemTemp.createTempSync('cadence_shortcuts_ui_');
    ShortcutSettings.overrideFile(File('${temp.path}/ui_settings.json'));
    ShortcutSettings.load();
    final service = EngineService();
    addTearDown(() {
      service.dispose();
      ShortcutSettings.overrideFile(null);
      temp.deleteSync(recursive: true);
    });
    service.initialiseDevice();

    await tester.pumpWidget(
      MaterialApp(
        locale: const Locale('es'),
        localizationsDelegates: AppLocalizations.localizationsDelegates,
        supportedLocales: AppLocalizations.supportedLocales,
        home: Scaffold(
          body: Builder(
            builder:
                (context) => TextButton(
                  onPressed: () => showPreferencesDialog(context, service),
                  child: const Text('open'),
                ),
          ),
        ),
      ),
    );

    await tester.tap(find.text('open'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 300));
    await tester.tap(find.text('Atajos'));
    await tester.pumpAndSettle();

    await tester.tap(find.text('Guardar proyecto'));
    await tester.pumpAndSettle();
    expect(find.text('Nuevo atajo'), findsOneWidget);

    final primaryKey =
        Platform.isMacOS
            ? LogicalKeyboardKey.metaLeft
            : LogicalKeyboardKey.controlLeft;
    await tester.sendKeyDownEvent(primaryKey);
    await tester.sendKeyDownEvent(LogicalKeyboardKey.keyP);
    await tester.sendKeyUpEvent(LogicalKeyboardKey.keyP);
    await tester.sendKeyUpEvent(primaryKey);
    await tester.pumpAndSettle();

    final binding = ShortcutSettings.bindingFor(ShortcutAction.saveProject);
    expect(binding.key, LogicalKeyboardKey.keyP);
    expect(binding.primary, isTrue);
    expect(
      find.text(Platform.isMacOS ? '⌘P' : 'Ctrl+P'),
      findsOneWidget,
    );
  });
}
