// Empty MIDI clips can be created from the timeline without recording:
// double-click an empty instrument lane, or right-click it and choose the
// menu action. Both paths select the new clip and open the piano roll.

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/l10n/l10n.dart';
import 'package:cadence_ui/ui/timeline_view.dart';

Future<void> _pumpTimeline(
  WidgetTester tester,
  EngineService service, {
  void Function(int clipId)? onOpenMidiEditor,
  ValueNotifier<int>? frameTick,
}) async {
  final tick = frameTick ?? ValueNotifier<int>(0);
  addTearDown(tick.dispose);
  await tester.pumpWidget(
    MaterialApp(
      locale: const Locale('es'),
      localizationsDelegates: AppLocalizations.localizationsDelegates,
      supportedLocales: AppLocalizations.supportedLocales,
      home: Scaffold(
        body: Align(
          alignment: Alignment.topLeft,
          child: SizedBox(
            width: 1200,
            height: 300,
            child: TimelineView(
              service: service,
              frameTick: tick,
              onOpenMidiEditor: onOpenMidiEditor,
            ),
          ),
        ),
      ),
    ),
  );
  await tester.pump();
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('double-click empty instrument lane creates an empty MIDI clip', (
    tester,
  ) async {
    final service = EngineService();
    addTearDown(service.dispose);
    service.addTrack('Inst', 8, type: 1);
    final track = service.snapshot.tracks.single;

    int? openedClipId;
    await _pumpTimeline(
      tester,
      service,
      onOpenMidiEditor: (clipId) => openedClipId = clipId,
    );

    // 1.05 s snaps to beat 2 (1.0 s at 120 BPM). The content starts after the
    // fixed track-header column plus the 1 px divider.
    final at = Offset(
      kHeaderWidth + 1 + 1.05 * kPixelsPerSecond,
      kRulerHeight + 30,
    );
    await tester.tapAt(at);
    await tester.pump(const Duration(milliseconds: 50));
    await tester.tapAt(at);
    await tester.pumpAndSettle();

    final clips = service.snapshot.tracks.single.clips;
    expect(clips, hasLength(1));
    final clip = clips.single;
    expect(clip.isMidi, isTrue);
    expect(clip.notes, isEmpty);
    expect(clip.start, closeTo(service.snapshot.beatSeconds * 2, 1e-6));
    expect(clip.length, closeTo(service.snapshot.barSeconds, 1e-6));
    expect(openedClipId, clip.id);
    expect(service.selectedClipId, clip.id);
    expect(service.selectedTrackId, track.id);
  });

  testWidgets('track header can create a MIDI clip at the playhead', (
    tester,
  ) async {
    final service = EngineService();
    addTearDown(service.dispose);
    service.addTrack('Inst', 8, type: 1);
    service.seek(4.0);

    int? openedClipId;
    await _pumpTimeline(
      tester,
      service,
      onOpenMidiEditor: (clipId) => openedClipId = clipId,
    );

    final gesture = await tester.startGesture(
      const Offset(50, kRulerHeight + 30),
      kind: PointerDeviceKind.mouse,
      buttons: kSecondaryButton,
    );
    await gesture.up();
    await tester.pumpAndSettle();

    await tester.tap(find.text('Crear clip MIDI en el playhead'));
    await tester.pumpAndSettle();

    final clip = service.snapshot.tracks.single.clips.single;
    expect(clip.isMidi, isTrue);
    expect(clip.notes, isEmpty);
    expect(clip.start, closeTo(4.0, 1e-6));
    expect(openedClipId, clip.id);
  });

  testWidgets(
    'right-click empty instrument lane offers to create a MIDI clip',
    (tester) async {
      final service = EngineService();
      addTearDown(service.dispose);
      service.addTrack('Inst', 8, type: 1);

      int? openedClipId;
      await _pumpTimeline(
        tester,
        service,
        onOpenMidiEditor: (clipId) => openedClipId = clipId,
      );

      final at = Offset(
        kHeaderWidth + 1 + 2 * kPixelsPerSecond,
        kRulerHeight + 30,
      );
      final gesture = await tester.startGesture(
        at,
        kind: PointerDeviceKind.mouse,
        buttons: kSecondaryButton,
      );
      await gesture.up();
      await tester.pumpAndSettle();

      await tester.tap(find.text('Crear clip MIDI aquí'));
      await tester.pumpAndSettle();

      final clips = service.snapshot.tracks.single.clips;
      expect(clips, hasLength(1));
      final clip = clips.single;
      expect(clip.isMidi, isTrue);
      expect(clip.notes, isEmpty);
      expect(clip.start, closeTo(2.0, 1e-6));
      expect(openedClipId, clip.id);
    },
  );
}
