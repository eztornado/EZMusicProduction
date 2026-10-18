// Widget test: the "+ FX" chip must render on every strip even when the
// chain is empty — it is the only entry point for adding a first plugin.
// (Regression: it used to be nested inside `if (fxNames.isNotEmpty)` and
// vanished on fresh projects, leaving no way to add FX from the UI.)
//
// Also covers the horizontal scroll with many tracks (regression: a
// horizontal SingleChildScrollView ignores the wheel's vertical delta, so
// strips past the window edge were unreachable with a mouse).
//
// One engine per tester process (JUCE message loop singleton).

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/ui/mixer_zone.dart';

void main() {
  testWidgets('+ FX chip is present on strips with an empty fx chain',
      (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);
    expect(service.outputDeviceNames, isNotEmpty);

    service.addTrack('Drums', 0);

    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: MixerZone(
            service: service, frameTick: ValueNotifier<int>(0)),
      ),
    ));
    await tester.pumpAndSettle();

    // One chip on the track strip + one on the master strip.
    expect(find.text('+ FX'), findsNWidgets(2));

    // ---- horizontal scroll: many tracks must stay reachable ----
    for (var i = 0; i < 20; i++) {
      service.addTrack('Track $i', i);
    }
    await tester.pumpAndSettle();

    final controller = tester
        .widget<SingleChildScrollView>(find.byType(SingleChildScrollView))
        .controller!;
    expect(controller.position.maxScrollExtent, greaterThan(0),
        reason: '21 canales deben desbordar el ancho de la ventana');

    // La rueda del ratón (delta vertical) debe desplazar el mixer a lo ancho.
    final pointer = TestPointer(1, PointerDeviceKind.mouse);
    pointer.hover(tester.getCenter(find.byType(MixerZone)));
    await tester.sendEventToBinding(pointer.scroll(const Offset(0, 120)));
    await tester.pump();
    expect(controller.offset, greaterThan(0),
        reason: 'la rueda vertical debe desplazar el mixer en horizontal');
  });
}
