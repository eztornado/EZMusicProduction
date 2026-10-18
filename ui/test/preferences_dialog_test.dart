// Reproduces the preferences dialog crash with real device lists.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/ui/preferences_dialog.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('preferences dialog opens without assertion', (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);
    service.initialiseDevice();

    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Builder(
          builder: (context) => TextButton(
            onPressed: () => showPreferencesDialog(context, service),
            child: const Text('open'),
          ),
        ),
      ),
    ));

    await tester.tap(find.text('open'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 300));

    expect(find.text('Preferencias — Audio y MIDI'), findsOneWidget);
  });
}
