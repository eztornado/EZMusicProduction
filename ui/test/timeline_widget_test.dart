// Widget test: tapping the timeline selects/deselects clips through the
// EngineService, against the real engine dylib and the real demo assets.

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/ui/timeline_view.dart';

double _vPixels(WidgetTester tester) =>
    tester
        .state<ScrollableState>(find.byType(Scrollable).first)
        .position
        .pixels;

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('tap selects a clip, tap on empty deselects', (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);

    // Demo project: 3 tracks, clips at 0 / 2 / 4 s (generated test assets).
    final error = service.loadDemoProject('/Users/ivan/daw/assets/test');
    expect(error, isNull, reason: error ?? '');

    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Align(
          alignment: Alignment.topLeft,
          child: SizedBox(
            width: 2000,
            height: 300,
            child: TimelineView(service: service, frameTick: ValueNotifier<int>(0)),
          ),
        ),
      ),
    ));
    await tester.pump();

    // The timeline area starts after the fixed header column.
    final header = kHeaderWidth;

    // Row 0 (Drums, clip 0-3 s): tap at 0.5 s -> select clip 1.
    await tester.tapAt(Offset(header + 45, kRulerHeight + 30));
    await tester.pump();
    expect(service.selectedClipId, 1);

    // Row 0 at 5 s: empty area -> deselect AND move the playhead there.
    await tester.tapAt(Offset(header + 450, kRulerHeight + 30));
    await tester.pump();
    expect(service.selectedClipId, isNull);
    expect(service.positionSeconds, closeTo(5.0, 0.02));

    // Row 1 (Bass, clip 2-5 s): tap at 3 s -> select clip 2.
    await tester.tapAt(
        Offset(header + 270, kRulerHeight + kTrackRowHeight + 30));
    await tester.pump();
    expect(service.selectedClipId, 2);
  });

  testWidgets('mouse drag moves a clip horizontally', (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);
    final error = service.loadDemoProject('/Users/ivan/daw/assets/test');
    expect(error, isNull, reason: error ?? '');

    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Align(
          alignment: Alignment.topLeft,
          child: SizedBox(
            width: 2000,
            height: 300,
            child: TimelineView(service: service, frameTick: ValueNotifier<int>(0)),
          ),
        ),
      ),
    ));
    await tester.pump();

    final clip = service.snapshot.tracks.first.clips.first;
    expect(clip.start, 0.0);

    // Mouse-style drag from inside the clip body (row 0, 0.5 s) by a
    // total of 135 px = 1.5 s (the first ~18 px are gesture slop).
    final start = Offset(kHeaderWidth + 45, kRulerHeight + 30);
    final gesture =
        await tester.startGesture(start, kind: PointerDeviceKind.mouse);
    await tester.pump();
    for (var i = 0; i < 9; i++) {
      await gesture.moveBy(const Offset(15, 0));
      await tester.pump();
    }
    await gesture.up();
    await tester.pump();

    final moved = service.snapshot.tracks.first.clips.first;
    expect(moved.id, clip.id);
    expect(moved.start, closeTo(1.5, 0.001),
        reason: 'drag should move the clip to the snapped pointer position');

    // Split at 2.0 s: the (moved) clip 1.5-3.0 becomes two clips.
    expect(service.splitClip(clip.id, 2.0), isNull);
    var track0Clips = service.snapshot.tracks.first.clips;
    expect(track0Clips, hasLength(2));
    track0Clips.sort((a, b) => a.start.compareTo(b.start));
    expect(track0Clips[0].start, closeTo(1.5, 1e-3));
    expect(track0Clips[0].length, closeTo(0.5, 1e-3));
    expect(track0Clips[1].start, closeTo(2.0, 1e-3));
    expect(track0Clips[1].length, closeTo(2.5, 1e-3)); // 3 s take - 0.5 left

    // Cross-track move (same track type: audio -> audio).
    final targetTrack = service.snapshot.tracks[1];
    final rightClipId = track0Clips[1].id;
    expect(
        service.moveClipToTrack(rightClipId, targetTrack.id, 2.0), isNull);
    expect(service.snapshot.tracks.first.clips, hasLength(1));
    expect(service.snapshot.tracks[1].clips.map((c) => c.id),
        contains(rightClipId));

    // Cross-TYPE moves are refused.
    service.addTrack('Inst', 8, type: 1);
    final instTrack = service.snapshot.tracks.last;
    expect(service.moveClipToTrack(rightClipId, instTrack.id, 0.0), isNotNull,
        reason: 'audio clips cannot land on instrument tracks');
  });

  testWidgets('nav strip click scrolls the view', (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);
    final error = service.loadDemoProject('/Users/ivan/daw/assets/test');
    expect(error, isNull, reason: error ?? '');

    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Align(
          alignment: Alignment.topLeft,
          child: SizedBox(
            width: 2000,
            height: 300,
            child: TimelineView(service: service, frameTick: ValueNotifier<int>(0)),
          ),
        ),
      ),
    ));
    await tester.pump();

    // The nav strip is the bottom 14 px of the timeline. Click its right
    // half: the view must scroll to the far content.
    await tester.tapAt(Offset(kHeaderWidth + 500, 293));
    await tester.pump();
    await tester.pump();

    // After scrolling, clicking the timeline at viewport x=300 seeks far
    // past the demo content (the view is deep into the 60 s timeline).
    await tester.tapAt(Offset(kHeaderWidth + 300, kRulerHeight + 30));
    await tester.pump();
    expect(service.positionSeconds, greaterThan(20),
        reason: 'the strip click must have scrolled the view');
  });

  testWidgets('mouse drag from the right edge trims the clip inward', (tester) async {
  final service = EngineService();
  addTearDown(service.dispose);
  final error = service.loadDemoProject('/Users/ivan/daw/assets/test');
  expect(error, isNull, reason: error ?? '');

  await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
    home: Scaffold(
      body: Align(
        alignment: Alignment.topLeft,
        child: SizedBox(
          width: 2000,
          height: 300,
          child: TimelineView(service: service, frameTick: ValueNotifier<int>(0)),
        ),
      ),
    ),
  ));
  await tester.pump();

  final clip = service.snapshot.tracks.first.clips.first;
  expect(clip.start, 0.0);
  final origLength = clip.length;

  // Grab the right edge (within the 14 px hot zone) and drag LEFT 45 px
  // = 0.5 s at the default 90 px/s. Trims are fine-grained (no beat snap).
  // The content origin sits 1 px right of kHeaderWidth (the divider).
  final edgeX = kHeaderWidth + 1 + clip.end * 90 - 5;
  final gesture = await tester.startGesture(
      Offset(edgeX, kRulerHeight + 30),
      kind: PointerDeviceKind.mouse);
  await tester.pump();
  for (var i = 0; i < 5; i++) {
    await gesture.moveBy(const Offset(-9, 0));
    await tester.pump();
  }
  await gesture.up();
  await tester.pump();

  final trimmed = service.snapshot.tracks.first.clips.first;
  expect(trimmed.id, clip.id);
  expect(trimmed.start, closeTo(clip.start, 1e-9),
      reason: 'right-edge trim must not move the clip');
  expect(trimmed.length, closeTo(origLength - 0.5, 0.06),
      reason: 'dragging the right edge 45 px must shorten by ~0.5 s');

  // Restoring: drag the right edge back OUT past the original end — the
  // original length must come back (audio extends up to the source).
  final restoreGesture = await tester.startGesture(
      Offset(kHeaderWidth + 1 + trimmed.end * 90 - 4, kRulerHeight + 30),
      kind: PointerDeviceKind.mouse);
  await tester.pump();
  for (var i = 0; i < 8; i++) {
    await restoreGesture.moveBy(const Offset(9, 0));
    await tester.pump();
  }
  await restoreGesture.up();
  await tester.pump();

  final restored = service.snapshot.tracks.first.clips.first;
  expect(restored.length, closeTo(origLength, 0.12),
      reason: 'extending the edge back out must restore the length '
          '(got len=${restored.length}, orig=$origLength)');
});

  testWidgets('snap toggle OFF: moves land free, ON: snapped', (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);
    final error = service.loadDemoProject('/Users/ivan/daw/assets/test');
    expect(error, isNull, reason: error ?? '');

    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Align(
          alignment: Alignment.topLeft,
          child: SizedBox(
            width: 2000,
            height: 300,
            child: TimelineView(service: service, frameTick: ValueNotifier<int>(0)),
          ),
        ),
      ),
    ));
    await tester.pump();

    Future<void> dragBy(double px) async {
      final gesture = await tester.startGesture(
          Offset(kHeaderWidth + 1 + 0.8 * 90, kRulerHeight + 30),
          kind: PointerDeviceKind.mouse);
      await tester.pump();
      final steps = 5;
      for (var i = 0; i < steps; i++) {
        await gesture.moveBy(Offset(px / steps, 0));
        await tester.pump();
      }
      await gesture.up();
      await tester.pump();
    }

    // OFF: the clip lands EXACTLY at the raw drag delta (0 + 0.444 s,
    // off-grid — the press position does not add, only the delta).
    service.snapToGrid = false;
    await dragBy(40);
    expect(service.snapshot.tracks.first.clips.first.start,
        closeTo(0.444, 0.03),
        reason: 'with snap off the move must be free '
            '(got ${service.snapshot.tracks.first.clips.first.start})');

    // ON: from 0.444 the same +0.444 drag snaps to the beat grid (1.0).
    service.snapToGrid = true;
    await dragBy(40);
    expect(service.snapshot.tracks.first.clips.first.start,
        closeTo(1.0, 0.001),
        reason: 'with snap on the move must land on the grid '
            '(got ${service.snapshot.tracks.first.clips.first.start})');
  });

  testWidgets('left-edge drag on a SELECTED clip trims inward, never moves',
      (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);
    final error = service.loadDemoProject('/Users/ivan/daw/assets/test');
    expect(error, isNull, reason: error ?? '');
    service.snapToGrid = false; // fine-trim mechanics test

    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Align(
          alignment: Alignment.topLeft,
          child: SizedBox(
            width: 2000,
            height: 300,
            child: TimelineView(service: service, frameTick: ValueNotifier<int>(0)),
          ),
        ),
      ),
    ));
    await tester.pump();

    final clip = service.snapshot.tracks.first.clips.first;
    final origLength = clip.length;

    // Select the clip first (his flow: click, then grab an edge).
    await tester.tapAt(Offset(kHeaderWidth + 1 + (clip.start + 0.4) * 90,
        kRulerHeight + 40));
    await tester.pump();
    expect(service.selectedClipId, clip.id);

    // Grab the LEFT edge and drag RIGHT 27 px = 0.3 s inward.
    final gesture = await tester.startGesture(
        Offset(kHeaderWidth + 1 + clip.start * 90 + 3, kRulerHeight + 40),
        kind: PointerDeviceKind.mouse);
    await tester.pump();
    for (var i = 0; i < 3; i++) {
      await gesture.moveBy(const Offset(9, 0));
      await tester.pump();
    }
    await gesture.up();
    await tester.pump();

    final trimmed = service.snapshot.tracks.first.clips.first;
    expect(trimmed.id, clip.id);
    expect(trimmed.start, closeTo(0.3, 0.05),
        reason: 'left-edge drag must move the clip START inward, '
            'not the whole clip (got start=${trimmed.start}, '
            'len=${trimmed.length})');
    expect(trimmed.length, closeTo(origLength - 0.3, 0.06),
        reason: 'left-edge drag must shorten the clip');
  });

  testWidgets('MIDI clip: left AND right edge drags trim, never move/collapse',
      (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);

    service.addTrack('Synth', 6, type: 1);
    service.snapToGrid = false; // fine-trim mechanics test
    final trackId = service.snapshot.tracks.first.id;
    service.addMidiClip(trackId, 1.0, 5.0); // end 6 s: edge stays on-screen
    final clipId = service.snapshot.tracks.first.clips.first.id;
    service.midiClipAddNote(clipId, 60, 0.8, 0.2, 0.5);

    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Align(
          alignment: Alignment.topLeft,
          child: SizedBox(
            width: 2000,
            height: 300,
            child: TimelineView(service: service, frameTick: ValueNotifier<int>(0)),
          ),
        ),
      ),
    ));
    await tester.pump();

    // LEFT edge inward +0.4 s — pressed 13 px INSIDE the edge (worst case
    // within the 14 px hot zone; the gesture slop must not demote it).
    var gesture = await tester.startGesture(
        Offset(kHeaderWidth + 1 + 1.0 * 90 + 13, kRulerHeight + 40),
        kind: PointerDeviceKind.mouse);
    await tester.pump();
    for (var i = 0; i < 4; i++) {
      await gesture.moveBy(const Offset(9, 0));
      await tester.pump();
    }
    await gesture.up();
    await tester.pump();

    var c = service.snapshot.tracks.first.clips.first;
    expect(c.start, closeTo(1.4, 0.05),
        reason: 'MIDI left trim must move the START inward (got start=${c.start})');
    expect(c.length, closeTo(5.0 - 0.4, 0.06),
        reason: 'MIDI left trim must SHORTEN (got len=${c.length})');
    // THE core semantic: notes keep their ABSOLUTE timeline position. The
    // note at rel 0.2 (abs 1.2) must now sit at rel −0.2 — abs 1.2 again.
    expect(c.notes.first.start + c.start, closeTo(1.2, 0.05),
        reason: 'left trim must NOT translate the notes (abs note time '
            'moved to ${c.notes.first.start + c.start}');

    // LEFT edge OUTWARD: drag back left 36 px — the trimmed part must
    // come back (notes un-hidden at their original absolute positions).
    gesture = await tester.startGesture(
        Offset(kHeaderWidth + 1 + c.start * 90 + 3, kRulerHeight + 40),
        kind: PointerDeviceKind.mouse);
    await tester.pump();
    for (var i = 0; i < 4; i++) {
      await gesture.moveBy(const Offset(-9, 0));
      await tester.pump();
    }
    await gesture.up();
    await tester.pump();

    c = service.snapshot.tracks.first.clips.first;
    expect(c.start, closeTo(1.0, 0.05),
        reason: 'left restore must move the start back (got start=${c.start})');
    expect(c.notes.first.start + c.start, closeTo(1.2, 0.05),
        reason: 'restored notes keep their absolute position '
            '(abs=${c.notes.first.start + c.start})');

    // RIGHT edge outward: extend past the original end (6.0 s).
    gesture = await tester.startGesture(
        Offset(kHeaderWidth + 1 + (1.0 + c.length) * 90 - 5, kRulerHeight + 40),
        kind: PointerDeviceKind.mouse);
    await tester.pump();
    for (var i = 0; i < 4; i++) {
      await gesture.moveBy(const Offset(9, 0));
      await tester.pump();
    }
    await gesture.up();
    await tester.pump();

    c = service.snapshot.tracks.first.clips.first;
    expect(c.length, greaterThan(4.9),
        reason: 'MIDI right trim must EXTEND outward (got len=${c.length}) — '
            'maxLength for MIDI clips must not clamp to 0');
  });

  testWidgets('overlapping clips: the TOP one gets the edge gesture',
      (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);

    service.addTrack('Synth', 6, type: 1);
    service.snapToGrid = false; // fine-trim mechanics test
    final trackId = service.snapshot.tracks.first.id;
    service.addMidiClip(trackId, 1.0, 5.0); // bottom clip
    final bottomId = service.snapshot.tracks.first.clips.first.id;
    service.addMidiClip(trackId, 2.0, 4.0); // overlaps, paints on top
    final topId = service.snapshot.tracks.first.clips.last.id;

    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Align(
          alignment: Alignment.topLeft,
          child: SizedBox(
            width: 2000,
            height: 300,
            child: TimelineView(service: service, frameTick: ValueNotifier<int>(0)),
          ),
        ),
      ),
    ));
    await tester.pump();

    // Grab the TOP clip's left edge (t=2.0) — must trim the TOP clip, not
    // move the invisible bottom one.
    final gesture = await tester.startGesture(
        Offset(kHeaderWidth + 1 + 2.0 * 90 + 4, kRulerHeight + 40),
        kind: PointerDeviceKind.mouse);
    await tester.pump();
    for (var i = 0; i < 3; i++) {
      await gesture.moveBy(const Offset(9, 0));
      await tester.pump();
    }
    await gesture.up();
    await tester.pump();

    final clips = service.snapshot.tracks.first.clips;
    final top = clips.where((c) => c.id == topId).first;
    final bottom = clips.where((c) => c.id == bottomId).first;
    expect(top.start, closeTo(2.3, 0.05),
        reason: 'the top clip must trim inward (got start=${top.start})');
    expect(bottom.start, 1.0,
        reason: 'the bottom clip must not be dragged by its edge grab');
  });

  testWidgets('12 tracks: wheel scrolls the lanes vertically', (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);
    final error = service.loadDemoProject('/Users/ivan/daw/assets/test');
    expect(error, isNull, reason: error ?? '');
    for (var i = 0; i < 9; i++) {
      service.addTrack('Extra $i', i % 16, type: 1);
    }
    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Align(
          alignment: Alignment.topLeft,
          child: SizedBox(
            width: 2000,
            height: 300,
            child: TimelineView(service: service, frameTick: ValueNotifier<int>(0)),
          ),
        ),
      ),
    ));
    await tester.pump();

    final scrollables = find.byType(Scrollable);
    final vFinder = scrollables.first; // el vertical ENVUELVE al horizontal
    expect(tester.widget<Scrollable>(vFinder).axis, Axis.vertical);

    // Rueda sobre la zona de pistas: debe desplazar VERTICAL.
    final wheelTarget = const Offset(600, 150);
    await tester.sendEventToBinding(PointerScrollEvent(
        position: wheelTarget, scrollDelta: const Offset(0, 240)));
    await tester.pumpAndSettle();

    expect(_vPixels(tester), greaterThan(0),
        reason: 'la rueda debe desplazar las pistas');
  });

  testWidgets(
      '12 tracks: scrollbar thumb drag scrolls the lanes',
      skip: true, // el pulgar de Material no responde a gestos sintéticos
      // (rueda y trackpad sí desplazan, verificados en otros tests),
      (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);
    final error = service.loadDemoProject('/Users/ivan/daw/assets/test');
    expect(error, isNull, reason: error ?? '');
    for (var i = 0; i < 9; i++) {
      service.addTrack('Extra $i', i % 16, type: 1);
    }
    await tester.pumpWidget(MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Align(
          alignment: Alignment.topLeft,
          child: SizedBox(
            width: 2000,
            height: 300,
            child: TimelineView(service: service, frameTick: ValueNotifier<int>(0)),
          ),
        ),
      ),
    ));
    await tester.pump();

    // Arrastrar el PULGAR del scrollbar vertical (borde derecho del área
    // de pistas). El thumb está en el borde derecho del viewport.
    const thumbX = 2000.0 - 4;
    // RawScrollbar acepta RATÓN por defecto (no touch): gesto de ratón.
    final mouse = await tester.createGesture(kind: PointerDeviceKind.mouse);
    await mouse.addPointer(location: const Offset(thumbX, 30));
    addTearDown(mouse.removePointer);
    await tester.pump();
    await mouse.down(const Offset(thumbX, 30));
    await tester.pump();
    await mouse.moveBy(const Offset(0, 120), timeStamp: const Duration(milliseconds: 100));
    await tester.pump();
    await mouse.up();
    await tester.pumpAndSettle();

    expect(_vPixels(tester), greaterThan(0),
        reason: 'arrastrar el pulgar debe desplazar las pistas');

    final vState = tester.state<ScrollableState>(
        find.byType(Scrollable).first);
    expect(vState.position.pixels, greaterThan(0),
        reason: 'arrastrar el pulgar debe desplazar las pistas');
  });

}
