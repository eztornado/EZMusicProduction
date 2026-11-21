import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/controller/controller_service.dart';
import 'package:cadence_ui/engine/cadence_bindings.dart';
import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  test('controller math supports relative encoders and soft takeover', () {
    expect(relativeEncoderDelta(1), closeTo(1 / 64, 1e-9));
    expect(relativeEncoderDelta(63), closeTo(63 / 64, 1e-9));
    expect(relativeEncoderDelta(64), 0);
    expect(relativeEncoderDelta(0), 0);
    expect(relativeEncoderDelta(65), closeTo(-63 / 64, 1e-9));
    expect(relativeEncoderDelta(127), closeTo(-1 / 64, 1e-9));

    expect(softTakeoverAllows(0.50, 0.51, false), isTrue);
    expect(softTakeoverAllows(0.50, 0.90, false), isFalse);
    expect(softTakeoverAllows(0.50, 0.90, true), isTrue);
  });

  test('controller settings round-trip through JSON', () {
    final dir = Directory.systemTemp.createTempSync('cadence-controller-');
    addTearDown(() => dir.deleteSync(recursive: true));
    final file = File('${dir.path}/controller.json');

    final saved = ControllerSettings(
      encoderCcs: const [1, 2, 3, 4, 5, 6, 7, 8],
      faderCcs: const [11, 12, 13, 14],
      relative: true,
      softTakeover: false,
      actions: const {
        ControllerAction.playPause:
            ControllerTrigger(ControllerTriggerKind.note, 36),
        ControllerAction.metronome:
            ControllerTrigger(ControllerTriggerKind.cc, 40),
      },
    )..save(to: file);

    final loaded = ControllerSettings.load(from: file);
    expect(loaded.encoderCcs, saved.encoderCcs);
    expect(loaded.faderCcs, saved.faderCcs);
    expect(loaded.relative, isTrue);
    expect(loaded.softTakeover, isFalse);
    expect(loaded.actions[ControllerAction.playPause]?.kind,
        ControllerTriggerKind.note);
    expect(loaded.actions[ControllerAction.playPause]?.number, 36);
    expect(loaded.actions[ControllerAction.metronome]?.kind,
        ControllerTriggerKind.cc);
    expect(loaded.actions[ControllerAction.metronome]?.number, 40);
  });

  test('controller action learns a pad and triggers transport', () async {
    final service = EngineService();
    final dir = Directory.systemTemp.createTempSync('cadence-controller-');
    final controller = ControllerService(
      service,
      settings: ControllerSettings.defaults(),
      settingsFile: File('${dir.path}/controller.json'),
    );
    addTearDown(() {
      controller.dispose();
      service.dispose();
      dir.deleteSync(recursive: true);
    });

    final metronome = controller.actionBindings
        .firstWhere((b) => b.action == ControllerAction.metronome);
    controller.start();
    addTearDown(controller.stop);
    controller.startActionLearn(metronome);
    expect(controller.learnAction?.action, ControllerAction.metronome);

    service.injectMidiMessage([0x90, 36, 100]);
    final deadline = DateTime.now().add(const Duration(seconds: 3));
    while (DateTime.now().isBefore(deadline) && metronome.trigger == null) {
      await Future<void>.delayed(const Duration(milliseconds: 30));
    }
    expect(metronome.trigger?.kind, ControllerTriggerKind.note);
    expect(metronome.trigger?.number, 36);
    expect(controller.learnAction, isNull);

    final before = service.metronomeOn;
    service.injectMidiMessage([0x90, 36, 100]);
    await Future<void>.delayed(const Duration(milliseconds: 80));
    expect(service.metronomeOn, !before);
  });

  test('engine mirrors incoming CC messages for the performance layer', () {
    final service = EngineService();
    addTearDown(service.dispose);

    expect(service.injectMidiMessage([0xB0, 74, 42]), isTrue);
    final events = service.pollMidiEvents(8);
    expect(events, hasLength(1));
    expect(events.single.type, EngineMidiEvent.controller);
    expect(events.single.channel, 1);
    expect(events.single.data1, 74);
    expect(events.single.data2, 42);
    expect(service.pollMidiEvents(8), isEmpty);
  });

  test('controller mapping skips bypass and keeps faders continuous',
      () async {
    final service = EngineService();
    final dir = Directory.systemTemp.createTempSync('cadence-controller-');
    final controller = ControllerService(
      service,
      settings: ControllerSettings.defaults(),
      settingsFile: File('${dir.path}/controller.json'),
    );
    addTearDown(() {
      controller.dispose();
      service.dispose();
      dir.deleteSync(recursive: true);
    });

    service.refreshPluginList();
    service.addTrack('Mapping Test', 0);
    final track = service.snapshot.tracks.last.id;
    service.selectTrack(track);

    final drive = service.plugins.where((p) => p.name == 'EZ Drive').toList();
    expect(drive, hasLength(1));
    service.addTrackFx(track, drive.single.index);

    final deadline = DateTime.now().add(const Duration(seconds: 5));
    while (DateTime.now().isBefore(deadline)) {
      if (service.pluginParamCount(track, 0, 0) > 0) break;
      await Future<void>.delayed(const Duration(milliseconds: 40));
    }

    controller.setDevice(const ControllerDevice(
      slotKind: 0,
      slotIndex: 0,
      label: 'EZ Drive',
    ));
    final names = [
      for (final encoder in controller.encoders)
        controller.parameterName(encoder),
    ].whereType<String>().toList();

    // EZ Drive has seven mappable parameters plus Bypass. Encoder 8 and fader 3
    // stay empty instead of silently toggling the whole device.
    expect(controller.currentParamCount, 7);
    expect(names, hasLength(7));
    expect(names, isNot(contains('Bypass')));
    expect(controller.parameterName(controller.encoders[7]), isNull);
    expect(controller.parameterName(controller.faders[2]), isNull);
  });

  test('MiniLab encoder maps to the focused device with soft takeover',
      () async {
    final service = EngineService();
    final dir = Directory.systemTemp.createTempSync('cadence-controller-');
    final controller = ControllerService(
      service,
      settings: ControllerSettings.defaults(),
      settingsFile: File('${dir.path}/controller.json'),
    );
    addTearDown(() {
      controller.dispose();
      service.dispose();
      dir.deleteSync(recursive: true);
    });

    service.refreshPluginList();
    service.addTrack('Controller Test', 0);
    final track = service.snapshot.tracks.last.id;
    service.selectTrack(track);

    final reverb =
        service.plugins.where((p) => p.name == 'EZ Reverb').toList();
    expect(reverb, hasLength(1));
    service.addTrackFx(track, reverb.single.index);

    var count = 0;
    final deadline = DateTime.now().add(const Duration(seconds: 5));
    while (DateTime.now().isBefore(deadline)) {
      count = service.pluginParamCount(track, 0, 0);
      if (count > 0) break;
      await Future<void>.delayed(const Duration(milliseconds: 40));
    }
    expect(count, greaterThan(0));

    const device = ControllerDevice(
      slotKind: 0,
      slotIndex: 0,
      label: 'EZ Reverb',
    );
    controller.setDevice(device);
    controller.start();
    addTearDown(controller.stop);

    service.setPluginParamValue(track, 0, 0, 0, 0.50);
    service.injectMidiMessage([0xB0, 74, 64]);
    await Future<void>.delayed(const Duration(milliseconds: 80));
    expect(service.pluginParamValue(track, 0, 0, 0), closeTo(64 / 127, 0.01));

    // Moving far from the current value must not jump the parameter.
    controller.setDevice(null);
    service.setPluginParamValue(track, 0, 0, 0, 0.90);
    service.injectMidiMessage([0xB0, 74, 0]);
    await Future<void>.delayed(const Duration(milliseconds: 80));
    expect(service.pluginParamValue(track, 0, 0, 0), closeTo(0.90, 0.01));

    // Crossing the current value arms the control, then it follows.
    service.injectMidiMessage([0xB0, 74, 114]);
    await Future<void>.delayed(const Duration(milliseconds: 80));
    expect(service.pluginParamValue(track, 0, 0, 0), closeTo(114 / 127, 0.01));
  });
}
