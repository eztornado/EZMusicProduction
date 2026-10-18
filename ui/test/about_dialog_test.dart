
// El diálogo "Acerca de" muestra el nombre, la versión generada y la web
// clicable.
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:cadence_ui/generated/version.dart';
import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/ui/project_dialogs.dart';

void main() {
  testWidgets('about dialog: nombre, versión y web', (tester) async {
    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Builder(
          builder: (context) => Center(
            child: OutlinedButton(
              onPressed: () => showAboutAppDialog(context),
              child: const Text('about'),
            ),
          ),
        ),
      ),
    ));
    await tester.tap(find.text('about'));
    await tester.pumpAndSettle();

    expect(find.text('EZMusicProduction'), findsOneWidget);
    expect(find.text('Versión $kAppVersion'), findsOneWidget);
    expect(find.text('Creado por EzTornado — Iván Pastor Simarro'),
        findsOneWidget);
    expect(find.text('www.ezmusicproduction.com'), findsOneWidget);
  });
}
