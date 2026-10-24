// Piano roll tool gestures against the real engine: pointer moves a
// recorded-style note, pencil draws new notes, eraser removes them.
// Raw Listener events need no gesture arena; one engine per process.

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/ui/piano_roll.dart';

void main() {
  testWidgets('pointer moves a note, pencil draws, eraser deletes',
      (tester) async {
    // Tall window: the whole pitch range fits, so note rows are at fixed
    // screen positions (no vertical scroll) and clicks hit the right note.
    await tester.binding.setSurfaceSize(const Size(1000, 1200));
    addTearDown(() => tester.binding.setSurfaceSize(null));

    final service = EngineService();
    addTearDown(service.dispose);

    service.addTrack('Synth', 6, type: 1);
    final trackId = service.snapshot.tracks.first.id;
    // Recorded-style take: the clip sits far into the project (bar 29 at
    // 120 bpm 4/4 = 56 s) and the notes are unquantized.
    service.addMidiClip(trackId, 56.0, 8.0);
    final clipId = service.snapshot.tracks.first.clips.first.id;
    service.midiClipAddNote(clipId, 64, 0.8, 2.309, 0.3);
    service.midiClipAddNote(clipId, 70, 0.8, 3.0, 0.2);

    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: SizedBox(
          width: 1000,
          height: 1200,
          child: PianoRoll(
              service: service,
              clipId: clipId,
              frameTick: ValueNotifier<int>(0)),
        ),
      ),
    ));
    await tester.pump();

    // initState centers the h-scroll on the first note (target =
    // first*140 - 100, clamped): mirror that to map content seconds to
    // screen x. Listener-local x = screen - keys(44); content =
    // local + hOffset.
    final hOffset = (2.309 * 140 - 100).clamp(0.0, double.infinity);
    double screenX(double contentSeconds) =>
        44.0 + contentSeconds * 140 - hOffset;
    final pressX = screenX(2.45); // inside note 0 [2.309, 2.609]
    // Grid starts below header (30) + quantize panel (34) + ruler (16);
    // note 0 is pitch 64 -> row (95 - 64) * 10.
    final pressY = 80.0 + (95 - 64) * 10 + 5;

    final gesture = await tester.startGesture(
        Offset(pressX, pressY),
        kind: PointerDeviceKind.mouse);
    await tester.pump();
    for (var i = 0; i < 7; i++) {
      await gesture.moveBy(const Offset(20, 0)); // +140 px = +1 s total
      await tester.pump();
    }
    await gesture.up();
    await tester.pump();

    final notes = service.snapshot.tracks.first.clips.first.notes;
    expect(notes, hasLength(2),
        reason: 'a drag must move a note, never drop any');
    // Note 1 is untouched at 3.0 s; note 0 must now sit at the snapped
    // end of a +1 s drag (2.309 + ~1 -> 3.25 on a 1/8 grid at 120 bpm).
    final starts = notes.map((n) => n.start).toList()..sort();
    expect(starts[0], closeTo(3.0, 0.01),
        reason: 'note 1 must stay put');
    expect(starts[1], closeTo(3.25, 0.3),
        reason: 'snapped drag of +1 s from 2.309 s');

    // PENCIL: click on empty grid creates a note at the snapped position.
    await tester.tap(find.byTooltip('Dibujar notas'));
    await tester.pump();
    await tester.tapAt(Offset(screenX(5.0), pressY));
    await tester.pump();
    final afterDraw =
        service.snapshot.tracks.first.clips.first.notes;
    expect(
        afterDraw.where((n) => n.start == 5.0), isNotEmpty,
        reason: 'pencil click must create a note on the grid');

    // ERASER: click the drawn note removes it.
    await tester.tap(find.byTooltip('Borrar notas'));
    await tester.pump();
    final before = afterDraw.length;
    await tester.tapAt(Offset(screenX(5.0) + 5, pressY));
    await tester.pump();
    expect(
        service.snapshot.tracks.first.clips.first.notes.length,
        lessThan(before),
        reason: 'eraser must remove the note under the pointer');
  });
}
