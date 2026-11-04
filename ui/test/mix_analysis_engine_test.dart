// AI mix analysis end-to-end through the real dylib and the C ABI.
//
// Loads the demo project, pushes the master hot, runs the offline analysis,
// checks the JSON report and applies the plan as one undo entry.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';

// ponytail: flutter test runs from ui/, so build/test-assets is the gitignored fixture dir.
final testAssetsDir =
    Platform.environment['CADENCE_ASSETS_DIR'] ?? 'build/test-assets';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('mix analysis reports issues and applies one undo entry', () async {
    final service = EngineService();
    addTearDown(service.dispose);

    final error = service.loadDemoProject(testAssetsDir);
    expect(error, isNull, reason: error ?? '');

    // A deliberately hot master produces loudness / true-peak fixes.
    service.setMasterGain(4.0);
    final masterFxBefore = service.snapshot.masterFxNames.length;

    expect(
      service.startMixAnalysis(prompt: '-14 LUFS, -1 dBTP, pop'),
      isTrue,
      reason: service.statusMessage ?? '',
    );

    final deadline = DateTime.now().add(const Duration(seconds: 30));
    while (service.mixAnalysis.isRunning && DateTime.now().isBefore(deadline)) {
      service.pollMixAnalysis();
      await Future<void>.delayed(const Duration(milliseconds: 20));
    }

    final snapshot = service.mixAnalysis;
    expect(snapshot.isRunning, isFalse, reason: 'analysis timed out');
    expect(snapshot.isReady, isTrue, reason: snapshot.error);
    expect(snapshot.targets['lufs'], -14.0);
    expect(snapshot.masterStats['lufs_integrated'], isA<num>());
    expect(snapshot.channels, isNotEmpty);
    expect(snapshot.issues, isNotEmpty);
    expect(snapshot.plan, isNotEmpty);
    expect(snapshot.promptNote, isNotEmpty);

    expect(service.applyMixPlan(), isTrue,
        reason: service.statusMessage ?? '');
    final applied = service.snapshot;
    expect(applied.masterFxNames.length, greaterThan(masterFxBefore));
    expect(service.canUndo, isTrue);
    service.undo();
    expect(service.snapshot.masterFxNames.length, masterFxBefore);
  });
}
