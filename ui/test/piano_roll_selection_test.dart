// Piano roll multi-note selection: dragging on empty grid draws a marquee,
// and dragging any selected note moves the whole selection by the same
// delta. Delete then removes every selected note.

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/ui/piano_roll.dart';

void main() {
  testWidgets('rubber-band selection moves and deletes the whole group', (
    tester,
  ) async {
    // Tall window: the grid keeps its whole pitch range visible, so row
    // positions are deterministic (no vertical scroll).
    await tester.binding.setSurfaceSize(const Size(1000, 1200));
    addTearDown(() => tester.binding.setSurfaceSize(null));

    final service = EngineService();
    addTearDown(service.dispose);

    service.addTrack('Synth', 6, type: 1);
    final trackId = service.snapshot.tracks.first.id;
    // Recorded-style clip far into the project (bar 29 at 120 bpm = 56 s).
    service.addMidiClip(trackId, 56.0, 8.0);
    final clipId = service.snapshot.tracks.first.clips.first.id;
    service.midiClipAddNote(clipId, 64, 0.8, 2.0, 0.3);
    service.midiClipAddNote(clipId, 64, 0.8, 2.5, 0.3);
    service.midiClipAddNote(clipId, 64, 0.8, 6.0, 0.3);

    await tester.pumpWidget(
      MaterialApp(
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
              frameTick: ValueNotifier<int>(0),
            ),
          ),
        ),
      ),
    );
    await tester.pump();

    // initState centres the h-scroll on the first note: 2.0*140 - 100.
    const hOffset = 180.0;
    // Grid starts below header (30) + quantize panel (34) + ruler (16).
    const gridTop = 80.0;
    double screenX(double contentSeconds) =>
        44.0 + contentSeconds * 140 - hOffset;
    const noteRowY = gridTop + (95 - 64) * 10;

    // Rubber band over the two notes at 2.0 s and 2.5 s (not the one at 6 s).
    final select = await tester.startGesture(
      Offset(screenX(1.5), noteRowY - 10),
      kind: PointerDeviceKind.mouse,
    );
    await select.moveTo(Offset(screenX(3.0), noteRowY + 20));
    await tester.pump();
    await select.up();
    await tester.pump();

    // Drag the selected group +1 s by grabbing one of its notes.
    final drag = await tester.startGesture(
      Offset(screenX(2.1), noteRowY + 5),
      kind: PointerDeviceKind.mouse,
    );
    await tester.pump();
    for (var i = 0; i < 7; i++) {
      await drag.moveBy(const Offset(20, 0)); // 140 px = +1 s total
      await tester.pump();
    }
    await drag.up();
    await tester.pump();

    var notes = service.snapshot.tracks.first.clips.first.notes;
    expect(notes, hasLength(3), reason: 'the group move must not drop notes');
    final starts = notes.map((n) => n.start).toList()..sort();
    expect(starts[0], closeTo(3.0, 0.01));
    expect(
      starts[1],
      closeTo(3.5, 0.01),
      reason: 'the selection keeps its internal spacing',
    );
    expect(
      starts[2],
      closeTo(6.0, 0.01),
      reason: 'unselected notes must stay put',
    );

    // Selection survives the drag: Delete removes both moved notes.
    await tester.sendKeyEvent(LogicalKeyboardKey.delete);
    await tester.pump();
    notes = service.snapshot.tracks.first.clips.first.notes;
    expect(notes, hasLength(1));
    expect(notes.single.start, closeTo(6.0, 0.01));
  });

  testWidgets('edge auto-scroll extends a marquee past the viewport', (
    tester,
  ) async {
    await tester.binding.setSurfaceSize(const Size(1000, 1200));
    addTearDown(() => tester.binding.setSurfaceSize(null));

    final service = EngineService();
    addTearDown(service.dispose);

    service.addTrack('Synth', 6, type: 1);
    final trackId = service.snapshot.tracks.first.id;
    // Clip at the project head so the roll opens at h-offset 0, with one
    // note visible and another far beyond the right edge.
    service.addMidiClip(trackId, 0.0, 12.0);
    final clipId = service.snapshot.tracks.first.clips.first.id;
    service.midiClipAddNote(clipId, 64, 0.8, 0.5, 0.3);
    service.midiClipAddNote(clipId, 64, 0.8, 10.0, 0.3);

    await tester.pumpWidget(
      MaterialApp(
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
              frameTick: ValueNotifier<int>(0),
            ),
          ),
        ),
      ),
    );
    await tester.pump();

    const gridTop = 80.0;
    const noteRowY = gridTop + (95 - 64) * 10;

    // Press empty grid before the first note, then park the pointer at the
    // right edge. The marquee must keep growing while the view scrolls on
    // its own, so the note at 10 s gets selected without ever being visible
    // at press time.
    final select = await tester.startGesture(
      Offset(72, noteRowY - 10),
      kind: PointerDeviceKind.mouse,
    );
    await tester.pump();
    await select.moveTo(Offset(990, noteRowY + 5));
    await tester.pump();
    for (var i = 0; i < 200; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }
    await select.up();
    await tester.pump();

    await tester.sendKeyEvent(LogicalKeyboardKey.delete);
    await tester.pump();
    expect(
      service.snapshot.tracks.first.clips.first.notes,
      isEmpty,
      reason: 'auto-scroll must extend the selection to off-screen notes',
    );
  });

  testWidgets('shift + wheel scrolls the grid horizontally', (tester) async {
    await tester.binding.setSurfaceSize(const Size(1000, 1200));
    addTearDown(() => tester.binding.setSurfaceSize(null));

    final service = EngineService();
    addTearDown(service.dispose);

    service.addTrack('Synth', 6, type: 1);
    final trackId = service.snapshot.tracks.first.id;
    service.addMidiClip(trackId, 0.0, 12.0);
    final clipId = service.snapshot.tracks.first.clips.first.id;
    service.midiClipAddNote(clipId, 64, 0.8, 0.5, 0.3);

    await tester.pumpWidget(
      MaterialApp(
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
              frameTick: ValueNotifier<int>(0),
            ),
          ),
        ),
      ),
    );
    await tester.pump();

    final scrollables = tester
        .stateList<ScrollableState>(find.byType(Scrollable))
        .toList();
    final position = scrollables
        .firstWhere((s) =>
            s.position.axis == Axis.horizontal &&
            s.position.maxScrollExtent > 0)
        .position;
    expect(position.pixels, 0);

    await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
    await tester.sendEventToBinding(
      const PointerScrollEvent(
        position: Offset(500, 400),
        scrollDelta: Offset(0, 120),
      ),
    );
    await tester.pump();
    await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);

    expect(position.pixels, closeTo(120, 0.01));
  });
}
