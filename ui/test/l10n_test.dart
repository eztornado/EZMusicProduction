// i18n: los tres idiomas montan y resuelven, el cuadro de alerta se pinta
// (es el sustituto de los SnackBar de error) y el idioma elegido persiste en
// ui_settings.json.
import 'dart:io';

import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/l10n/locale_store.dart';
import 'package:cadence_ui/ui/app_alert.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

Widget _host(Locale locale, Widget child) => MaterialApp(
      locale: locale,
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(body: child),
    );

void main() {
  testWidgets('es/en/ja resuelven las mismas claves', (tester) async {
    const expected = {
      'es': 'Añadir pista',
      'en': 'Add track',
      'ja': 'トラックを追加',
    };
    for (final entry in expected.entries) {
      await tester.pumpWidget(_host(
        Locale(entry.key),
        Builder(
          builder: (context) => Text(context.l10n.timelineAddTrack),
        ),
      ));
      expect(find.text(entry.value), findsOneWidget);
    }
  });

  testWidgets('el cuadro de alerta muestra el error y se cierra',
      (tester) async {
    await tester.pumpWidget(_host(
      const Locale('es'),
      Builder(
        builder: (context) => TextButton(
          onPressed: () => showAppError(context, 'no existe el fichero: /x'),
          child: const Text('boom'),
        ),
      ),
    ));

    await tester.tap(find.text('boom'));
    await tester.pumpAndSettle();

    // Titulo traducido + mensaje del error + boton de cerrar.
    expect(find.text('Error'), findsOneWidget);
    expect(find.text('no existe el fichero: /x'), findsOneWidget);

    await tester.tap(find.text('Cerrar'));
    await tester.pumpAndSettle();
    expect(find.text('no existe el fichero: /x'), findsNothing);
  });

  test('el idioma de reserva es el español (orden de la lista)', () {
    // MaterialApp usa el PRIMER idioma soportado cuando el del sistema no
    // coincide con ninguno: debe ser español, el idioma de referencia.
    expect(supportedUiLocales.map((l) => l.languageCode), ['es', 'en', 'ja']);
  });

  test('el idioma elegido persiste y "seguir al sistema" lo borra', () {
    final tmp = File(
        '${Directory.systemTemp.path}/cadence_ui_settings_test.json');
    addTearDown(() {
      LocaleStore.overrideFile(null);
      if (tmp.existsSync()) tmp.deleteSync();
      appLocale.value = null;
    });
    LocaleStore.overrideFile(tmp);

    LocaleStore.save(const Locale('ja'));
    expect(tmp.existsSync(), isTrue);
    appLocale.value = null;
    LocaleStore.load();
    expect(appLocale.value?.languageCode, 'ja');

    LocaleStore.save(null);
    appLocale.value = null;
    LocaleStore.load();
    expect(appLocale.value, isNull);
  });
}
