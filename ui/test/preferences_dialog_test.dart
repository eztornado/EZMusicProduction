// Reproduces the preferences dialog crash with real device lists and checks
// that the vertical section rail switches between all panels.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/generated/version.dart';
import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/ui/preferences_dialog.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('preferences dialog opens and navigates every section', (
    tester,
  ) async {
    final service = EngineService();
    addTearDown(service.dispose);
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

    // Audio is the initial section.
    expect(find.text('Preferencias'), findsOneWidget);
    expect(find.text('Dispositivo de salida'), findsOneWidget);
    expect(find.text('Dispositivo de entrada'), findsOneWidget);

    await tester.tap(find.text('Plugins'));
    await tester.pumpAndSettle();
    expect(find.text('Directorios indexados'), findsOneWidget);
    expect(find.text('Re-escanear plugins'), findsOneWidget);

    await tester.tap(find.text('MIDI'));
    await tester.pumpAndSettle();
    expect(find.text('Entrada MIDI (elige un dispositivo)'), findsOneWidget);

    await tester.tap(find.text('Control remoto'));
    await tester.pumpAndSettle();
    expect(find.text('Activar control remoto por LAN'), findsOneWidget);

    await tester.tap(find.text('Interfaz'));
    await tester.pumpAndSettle();
    expect(find.text('Modo debug'), findsOneWidget);
    expect(find.text('Seguir al sistema'), findsOneWidget);

    await tester.tap(find.text('Versión'));
    await tester.pumpAndSettle();
    expect(find.text('Versión $kAppVersion'), findsOneWidget);
    expect(
      find.text('Creado por EzTornado — Iván Pastor Simarro'),
      findsOneWidget,
    );
    expect(find.text('www.ezmusicproduction.com'), findsOneWidget);
  });
}
