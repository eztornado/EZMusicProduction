// Widget test: tapping the timeline selects/deselects clips through the
// EngineService, against the real engine dylib and the real demo assets.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';
import 'package:cadence_ui/ui/timeline_view.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('tap selects a clip, tap on empty deselects', (tester) async {
    final service = EngineService();
    addTearDown(service.dispose);

    // Demo project: 3 tracks, clips at 0 / 2 / 4 s (generated test assets).
    final error = service.loadDemoProject('/Users/ivan/daw/assets/test');
    expect(error, isNull, reason: error ?? '');

    await tester.pumpWidget(MaterialApp(
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

    // Row 0 at 5 s: empty area -> deselect.
    await tester.tapAt(Offset(header + 450, kRulerHeight + 30));
    await tester.pump();
    expect(service.selectedClipId, isNull);

    // Row 1 (Bass, clip 2-5 s): tap at 3 s -> select clip 2.
    await tester.tapAt(
        Offset(header + 270, kRulerHeight + kTrackRowHeight + 30));
    await tester.pump();
    expect(service.selectedClipId, 2);
  });
}
