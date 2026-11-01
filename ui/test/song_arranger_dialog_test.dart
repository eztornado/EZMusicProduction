// Widget smoke test: the arranger dialog renders, offers the missing-key path
// and opens its settings without layout assertions. No HTTP request is made.

import 'dart:io';

import 'package:cadence_ui/ai/ai_settings.dart';
import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/ui/song_arranger_dialog.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  testWidgets('arranger dialog renders and opens AI settings', (tester) async {
    final dir = Directory.systemTemp.createTempSync('cadenza_arranger_ui');
    final settings = File('${dir.path}/ui_settings.json');
    final credentials = File('${dir.path}/ai_credentials.json');
    AiSettings.overrideFiles(settings, credentials);
    AiSettings.load();
    addTearDown(() {
      AiSettings.overrideFiles(null, null);
      if (dir.existsSync()) dir.deleteSync(recursive: true);
    });

    final service = EngineService();
    addTearDown(service.dispose);
    service.addTrack('Keys', 0);
    final track = service.snapshot.tracks.last.id;

    await tester.pumpWidget(
      MaterialApp(
        locale: const Locale('es'),
        localizationsDelegates: AppLocalizations.localizationsDelegates,
        supportedLocales: AppLocalizations.supportedLocales,
        home: Scaffold(
          body: Builder(
            builder:
                (context) => TextButton(
                  onPressed:
                      () => showSongArrangerDialog(
                        context,
                        service,
                        trackId: track,
                        anchorSeconds: 0,
                      ),
                  child: const Text('open'),
                ),
          ),
        ),
      ),
    );

    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();

    expect(find.text('Arreglista IA'), findsOneWidget);
    expect(find.text('Crear arreglo'), findsOneWidget);
    // No key is configured in the temporary settings, and the default
    // endpoint is remote, so the missing-key notice must be visible.
    expect(find.textContaining('Configura una clave de API'), findsOneWidget);
    expect(find.text('Ajustes de IA'), findsAtLeastNWidgets(1));

    await tester.tap(find.text('Ajustes de IA').first);
    await tester.pumpAndSettle();
    expect(find.text('URL del servidor'), findsOneWidget);
    expect(find.text('Clave de API'), findsOneWidget);
    expect(find.text('Probar conexión'), findsOneWidget);
    expect(find.textContaining('Tiempo de espera'), findsOneWidget);
  });
}
