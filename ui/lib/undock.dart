// Undock helper: opens a lower-zone panel (piano roll / automation /
// mixer) in its own OS window via desktop_multi_window. The sub-window's
// engine attaches to the main window's engine handle (same process), so
// both views read/write the same project and audio.

import 'dart:convert';

import 'package:desktop_multi_window/desktop_multi_window.dart';

import 'engine/engine_service.dart';

/// Opens [panel] ('piano_roll' | 'automation' | 'mixer') in a new window.
Future<void> undockPanel(
    EngineService service, String panel, String title,
    {int clipId = -1}) async {
  final conf = jsonEncode({
    'panel': panel,
    'clipId': clipId,
    'engine': service.engineHandleAddress,
    'title': title,
  });
  final controller = await WindowController.create(WindowConfiguration(
    arguments: conf,
    hiddenAtLaunch: false,
  ));
  await controller.show();
}
