// La consola de debug muestra el fichero compartido en tiempo real.
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/debug/debug_log.dart';
import 'package:cadence_ui/debug/debug_settings.dart';
import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/ui/debug_console_window.dart';

Finder _consoleTextContains(String text) => find.byWidgetPredicate(
  (widget) => widget is SelectableText && (widget.data ?? '').contains(text),
);

void main() {
  late Directory tempDir;
  late File settingsFile;
  late File logFile;

  setUp(() {
    tempDir = Directory.systemTemp.createTempSync('cadence_debug_console');
    settingsFile = File('${tempDir.path}/ui_settings.json');
    logFile = File('${tempDir.path}/debug.log');
    DebugSettings.overrideFile(settingsFile);
    DebugLog.overrideFile(logFile);
    DebugSettings.save(enabled: true);
  });

  tearDown(() {
    DebugSettings.save(enabled: false);
    DebugSettings.overrideFile(null);
    DebugLog.resetForTest();
    if (tempDir.existsSync()) tempDir.deleteSync(recursive: true);
  });

  testWidgets('muestra las líneas nuevas del log', (tester) async {
    DebugLog.write('ia', '→ prompt de prueba');
    await tester.pumpWidget(
      MaterialApp(
        locale: const Locale('es'),
        localizationsDelegates: AppLocalizations.localizationsDelegates,
        supportedLocales: AppLocalizations.supportedLocales,
        home: const DebugConsoleWindow(),
      ),
    );
    await tester.pump(const Duration(milliseconds: 300));

    expect(find.text('Consola de depuración'), findsOneWidget);
    expect(_consoleTextContains('prompt de prueba'), findsOneWidget);

    DebugLog.write('ia', '← respuesta de prueba');
    await tester.pump(const Duration(milliseconds: 300));
    expect(_consoleTextContains('respuesta de prueba'), findsOneWidget);
  });
}
