// Real VST3/AU scan against the dylib — regression for the disabled
// hosting flags (JUCE_PLUGINHOST_VST3/AU were not defined, so the scan
// registered zero formats and every list came back empty).
//
// Uses the blocking scan when there is no on-disk cache yet (first run on
// a big library takes minutes); with the cache it loads instantly.
//
// One engine per tester process (JUCE message loop singleton).
// This machine has system-wide VST3s (/Library/Audio/Plug-Ins/VST3).

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  test('plugin scan finds VST3s with usable names', () {
    final service = EngineService();
    addTearDown(service.dispose);

    service.refreshPluginList(); // cache from a previous scan counts too
    if (service.plugins.isEmpty) {
      service.scanPluginsNow();
      expect(service.scanActive, isFalse);
    }

    expect(service.plugins, isNotEmpty);
    expect(service.plugins.any((p) => p.format == 'VST3'), isTrue,
        reason: 'no VST3 plugins were found by the scan');
    for (final p in service.plugins) {
      expect(p.name, isNotEmpty);
      expect(p.uid, isNotEmpty);
    }
  }, timeout: const Timeout(Duration(minutes: 20)));
}
