// Widget coverage for the dedicated EZSynth Studio. It uses the real engine
// dylib so the test also proves that the generic plug-in parameter/program
// ABI exposes the full EZSynth 2.0 surface (layers, pages, preset bank).

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/ui/ezsynth_studio.dart';

void main() {
  testWidgets('EZSynth Studio loads, navigates and applies a preset',
      (tester) async {
    tester.view.physicalSize = const Size(1000, 760);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(() {
      tester.view.resetPhysicalSize();
      tester.view.resetDevicePixelRatio();
    });

    final service = EngineService();
    addTearDown(service.dispose);
    service.refreshPluginList();

    service.addTrack('Keys', 0, type: 1);
    final track = service.snapshot.tracks.last.id;
    final matches = service.plugins
        .where((p) => p.name == 'EZSynth' && p.format == 'Internal')
        .toList();
    expect(matches, hasLength(1));
    service.setTrackInstrumentPlugin(track, matches.single.index);

    final deadline = DateTime.now().add(const Duration(seconds: 8));
    var paramCount = 0;
    var programCount = 0;
    while (DateTime.now().isBefore(deadline)) {
      paramCount = service.pluginParamCount(track, 1, 0);
      programCount = service.pluginProgramCount(track, 1, 0);
      if (paramCount > 300 && programCount >= 150) break;
      await Future<void>.delayed(const Duration(milliseconds: 50));
    }
    expect(service.isEzSynthInstrument(track), isTrue);
    expect(paramCount, greaterThan(300));
    expect(programCount, greaterThanOrEqualTo(150));

    await tester.pumpWidget(MaterialApp(
      theme: ThemeData.dark(useMaterial3: true),
      home: Scaffold(
        body: Builder(
          builder: (context) => Center(
            child: TextButton(
              onPressed: () {
                showEzSynthStudio(
                  context,
                  service,
                  trackId: track,
                  title: 'EZSynth',
                );
              },
              child: const Text('Abrir Studio'),
            ),
          ),
        ),
      ),
    ));
    await tester.tap(find.text('Abrir Studio'));

    // The dialog loads parameters synchronously; pump the route transition.
    for (var i = 0; i < 40; i++) {
      await tester.pump(const Duration(milliseconds: 50));
      if (find.text('2.0').evaluate().isNotEmpty) break;
    }
    expect(find.text('2.0'), findsOneWidget);
    expect(find.text('Resumen'), findsOneWidget);
    expect(tester.takeException(), isNull);

    // Source page: parameter-backed oscillator cards.
    await tester.tap(find.text('Capa A'));
    await tester.pumpAndSettle();
    expect(find.text('Oscilador'), findsWidgets);
    expect(find.text('Mezcla de capa'), findsOneWidget);
    expect(tester.takeException(), isNull);

    // Modulation and FX pages are the dense ones; catch layout overflows.
    await tester.tap(find.text('Modulación').first);
    await tester.pumpAndSettle();
    expect(find.text('Matriz de modulación'), findsOneWidget);
    expect(tester.takeException(), isNull);

    await tester.tap(find.text('FX Rack'));
    await tester.pumpAndSettle();
    expect(find.text('FX Rack'), findsNWidgets(2));
    expect(tester.takeException(), isNull);

    // Every remaining page resolves its parameter IDs and lays out.
    for (final entry in const [
      ('Auxiliar', 'Capa auxiliar'),
      ('Filtros', 'Filtros'),
      ('Envolventes', 'Envolventes'),
      ('Arp / Seq', 'Arp / Seq'),
      ('Motion / LINES', 'Motion / LINES'),
      ('Master', 'Master'),
    ]) {
      await tester.tap(find.text(entry.$1).first);
      await tester.pumpAndSettle();
      expect(find.text(entry.$2), findsWidgets);
      expect(tester.takeException(), isNull);
    }

    // Preset browser: search and apply a real program.
    await tester.tap(find.text('Resumen'));
    await tester.pumpAndSettle();
    await tester.tap(find.byIcon(Icons.unfold_more_rounded).first);
    await tester.pumpAndSettle();
    expect(find.text('Banco EZSynth'), findsOneWidget);
    await tester.enterText(find.byType(TextField), 'Aurora');
    await tester.pumpAndSettle();
    final auroraRow = find.descendant(
      of: find.byType(ListView),
      matching: find.text('Aurora'),
    );
    expect(auroraRow, findsOneWidget);
    await tester.tap(auroraRow);
    await tester.pumpAndSettle();
    expect(find.text('Aurora'), findsWidgets);
    expect(find.text('Banco EZSynth'), findsNothing);
    expect(tester.takeException(), isNull);

    // Wide layout: the macro rail appears and pages reflow without overflow.
    tester.view.physicalSize = const Size(1450, 950);
    await tester.pumpAndSettle();
    expect(find.text('MACROS'), findsOneWidget);
    await tester.tap(find.text('Macros').first);
    await tester.pumpAndSettle();
    expect(find.text('Macros & Performance'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
