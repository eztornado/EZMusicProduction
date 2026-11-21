import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/controller/controller_service.dart';
import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/l10n/app_localizations.dart';
import 'package:cadence_ui/l10n/locale_store.dart';
import 'package:cadence_ui/ui/performance_panel.dart';

void main() {
  for (final size in const [Size(800, 600), Size(1400, 900)]) {
    testWidgets(
        'performance panel renders at ${size.width.toInt()}x${size.height.toInt()}',
        (tester) async {
      tester.view.devicePixelRatio = 1.0;
      tester.view.physicalSize = size;
      addTearDown(tester.view.reset);

      final service = EngineService();
      final controller = ControllerService(
        service,
        settings: ControllerSettings.defaults(),
      );
      addTearDown(() {
        controller.dispose();
        service.dispose();
      });

      service.refreshPluginList();
      service.addTrack('Panel Test', 0, type: 1);
      final track = service.snapshot.tracks.last.id;
      service.selectTrack(track);
      final reverb =
          service.plugins.where((p) => p.name == 'EZ Reverb').toList();
      service.addTrackFx(track, reverb.single.index);

      final deadline = DateTime.now().add(const Duration(seconds: 5));
      while (DateTime.now().isBefore(deadline)) {
        if (service.pluginParamCount(track, 0, 0) > 0) break;
        await Future<void>.delayed(const Duration(milliseconds: 40));
      }

      await tester.pumpWidget(
        MaterialApp(
          localizationsDelegates: AppLocalizations.localizationsDelegates,
          supportedLocales: supportedUiLocales,
          home: Builder(
            builder: (context) => Scaffold(
              body: Center(
                child: ElevatedButton(
                  onPressed: () =>
                      showPerformancePanel(context, service, controller),
                  child: const Text('open'),
                ),
              ),
            ),
          ),
        ),
      );
      await tester.tap(find.text('open'));
      await tester.pumpAndSettle();

      expect(find.text('Performance'), findsOneWidget);
      expect(find.text('Transport & actions'), findsOneWidget);
      expect(find.text('Play / Pause'), findsOneWidget);
      expect(find.text('ENC 1'), findsOneWidget);
      expect(find.text('ENC 8'), findsOneWidget);
      expect(find.text('FADER 1'), findsOneWidget);
      expect(find.text('FADER 4'), findsOneWidget);
      if (size.width >= 1000) {
        expect(find.text('Soft takeover'), findsOneWidget);
        expect(find.text('Relative encoders'), findsOneWidget);
      }
    });
  }
}
