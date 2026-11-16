import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  test('filter, modulation, drive and utility effects are registered',
      () async {
    final service = EngineService();
    addTearDown(service.dispose);
    service.refreshPluginList();

    for (final name in const [
      'EZ AutoFilter',
      'EZ Chorus',
      'EZ Drive',
      'EZ Utility',
    ]) {
      final matches = service.plugins.where((p) => p.name == name).toList();
      expect(matches, hasLength(1), reason: name);
      expect(matches.single.format, 'Internal', reason: name);
    }

    final drive = service.plugins.firstWhere((p) => p.name == 'EZ Drive');
    service.addTrack('Drive Test', 1);
    final track = service.snapshot.tracks.last.id;
    service.addTrackFx(track, drive.index);
    final deadline = DateTime.now().add(const Duration(seconds: 5));
    var count = 0;
    while (DateTime.now().isBefore(deadline)) {
      count = service.pluginParamCount(track, 0, 0);
      if (count > 0) break;
      await Future<void>.delayed(const Duration(milliseconds: 40));
    }
    expect(count, greaterThanOrEqualTo(8));

    var modeIndex = -1;
    var bypassIndex = -1;
    for (var i = 0; i < count; i++) {
      final name = service.pluginParamName(track, 0, 0, i);
      if (name == 'Mode') modeIndex = i;
      if (name == 'Bypass') bypassIndex = i;
    }
    expect(modeIndex, greaterThanOrEqualTo(0));
    expect(bypassIndex, greaterThanOrEqualTo(0));
    expect(service.pluginParamFlags(track, 0, 0, modeIndex) & 2, 2);
    expect(service.pluginParamNumSteps(track, 0, 0, modeIndex), 4);
    expect(
      service.pluginParamTextAtValue(track, 0, 0, modeIndex, 0.5),
      'Tube',
    );
    expect(service.pluginParamFlags(track, 0, 0, bypassIndex) & 1, 1);
  });

  test(
    'built-in effect metadata is exposed through the Flutter bindings',
    () async {
      final service = EngineService();
      addTearDown(service.dispose);
      service.refreshPluginList();

      final reverb =
          service.plugins.where((p) => p.name == 'EZ Reverb').toList();
      final delay = service.plugins.where((p) => p.name == 'EZ Delay').toList();
      expect(reverb, hasLength(1));
      expect(delay, hasLength(1));
      expect(reverb.single.format, 'Internal');
      expect(delay.single.format, 'Internal');

      service.addTrack('FX Test', 0);
      final track = service.snapshot.tracks.last.id;
      service.addTrackFx(track, reverb.single.index);
      final deadline = DateTime.now().add(const Duration(seconds: 5));
      var count = 0;
      while (DateTime.now().isBefore(deadline)) {
        count = service.pluginParamCount(track, 0, 0);
        if (count > 0) break;
        await Future<void>.delayed(const Duration(milliseconds: 40));
      }
      expect(count, greaterThan(10));
      expect(service.pluginParamName(track, 0, 0, 0), 'Mix');
      expect(service.trackFxUidAt(track, 0), contains('EZ Reverb'));

      // Bypass is boolean (flag 1), mode is a choice (flag 2), and the choice
      // labels can be enumerated without mutating the parameter.
      var bypassIndex = -1;
      var modeIndex = -1;
      for (var i = 0; i < count; i++) {
        final name = service.pluginParamName(track, 0, 0, i);
        if (name == 'Bypass') bypassIndex = i;
        if (name == 'Mode') modeIndex = i;
      }
      expect(bypassIndex, greaterThanOrEqualTo(0));
      expect(modeIndex, greaterThanOrEqualTo(0));
      expect(service.pluginParamFlags(track, 0, 0, bypassIndex) & 1, 1);
      expect(service.pluginParamFlags(track, 0, 0, modeIndex) & 2, 2);
      expect(service.pluginParamNumSteps(track, 0, 0, modeIndex), 3);
      expect(
        service.pluginParamTextAtValue(track, 0, 0, modeIndex, 0.5),
        'Room',
      );

      final before = service.pluginParamValue(track, 0, 0, 0);
      service.setPluginParamValue(track, 0, 0, 0, before > 0.5 ? 0.25 : 0.75);
      final after = service.pluginParamValue(track, 0, 0, 0);
      expect(after, isNot(before));
    },
  );
}
