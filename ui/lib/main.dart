// Cadence UI — v0.1: app shell, demo project loader, timeline + transport.
// Incremental development (one tested increment at a time, per the plan).

import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import 'engine/engine_service.dart';
import 'ui/mixer_zone.dart';
import 'ui/piano_roll.dart';
import 'ui/preferences_dialog.dart';
import 'ui/timeline_view.dart';
import 'ui/transport_bar.dart';

Future<void> main() async {
  runApp(const CadenceApp());
}

class ToggleButton extends StatelessWidget {
  const ToggleButton({
    super.key,
    required this.active,
    required this.onTap,
    required this.label,
  });

  final bool active;
  final VoidCallback onTap;
  final String label;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        height: 26,
        padding: const EdgeInsets.symmetric(horizontal: 12),
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: active ? const Color(0xFF4D9DF7) : const Color(0xFF2C303A),
          borderRadius: BorderRadius.circular(5),
          border: Border.all(color: const Color(0xFF3E4350)),
        ),
        child: Text(label,
            style: TextStyle(
                fontSize: 11,
                fontWeight: FontWeight.w600,
                color: active ? Colors.black : const Color(0xFFE9EBF0))),
      ),
    );
  }
}

class CadenceApp extends StatelessWidget {
  const CadenceApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Cadence',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        brightness: Brightness.dark,
        scaffoldBackgroundColor: const Color(0xFF16171B),
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF4D9DF7),
          brightness: Brightness.dark,
        ),
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage>
    with SingleTickerProviderStateMixin {
  EngineService? _service;
  late final Ticker _ticker;
  final ValueNotifier<int> _frameTick = ValueNotifier(0);
  String? _bootError;
  bool get _editorOpen => _service?.editorClipId != null;

  @override
  void initState() {
    super.initState();
    try {
      _service = EngineService();
    } catch (error) {
      _bootError =
          'No se pudo cargar libcadence_engine.dylib: $error\n'
          'Compila el engine: cmake --build ~/daw/build';
    }
    _ticker = createTicker((_) => _frameTick.value++)..start();
    WidgetsBinding.instance.addPostFrameCallback((_) => _boot());
  }

  Future<void> _boot() async {
    final service = _service;
    if (service == null) return;

    service.loadDeviceSettings(); // may be absent on first run
    final deviceError = service.initialiseDevice();
    if (deviceError != null) {
      setState(() => _bootError = 'Device: $deviceError');
    }

    final assetsDir = Platform.environment['CADENCE_ASSETS_DIR'] ??
        '/Users/ivan/daw/assets/test';
    final demoError = service.loadDemoProject(assetsDir);
    setState(() => _bootError = demoError);
  }

  @override
  void dispose() {
    _ticker.dispose();
    _frameTick.dispose();
    _service?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final service = _service;
    if (service == null) {
      return Scaffold(
        body: Center(
          child: Padding(
            padding: const EdgeInsets.all(32),
            child: Text(_bootError ?? 'Engine no disponible',
                textAlign: TextAlign.center,
                style: const TextStyle(color: Color(0xFFE5484D))),
          ),
        ),
      );
    }

    return Scaffold(
      body: Column(
        children: [
          // Toolbar v0: demo project + status. Menú real llega con §5.2.
          Container(
            height: 40,
            color: const Color(0xFF1E2026),
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Row(
              children: [
                const Text('Cadence',
                    style: TextStyle(fontWeight: FontWeight.w600)),
                const SizedBox(width: 16),
                OutlinedButton(
                  onPressed: () {
                    final assetsDir = Platform.environment['CADENCE_ASSETS_DIR'] ??
                        '/Users/ivan/daw/assets/test';
                    final error = service.loadDemoProject(assetsDir);
                    setState(() => _bootError = error);
                  },
                  child: const Text('Demo project'),
                ),
                const SizedBox(width: 8),
                ToggleButton(
                  active: service.mixerOpen,
                  onTap: () {
                    service.toggleMixerOpen();
                    setState(() {});
                  },
                  label: 'Mixer',
                ),
                const SizedBox(width: 6),
                ToggleButton(
                  active: _editorOpen,
                  onTap: () {
                    final service = _service;
                    if (service == null) return;
                    if (_editorOpen) {
                      service.closeEditor();
                    } else if (service.selectedClipIsMidi) {
                      service.openEditor(service.selectedClipId!);
                    }
                    setState(() {});
                  },
                  label: 'Piano roll',
                ),
                IconButton(
                  tooltip: 'Preferencias (audio, MIDI)',
                  icon: const Icon(Icons.settings, size: 18,
                      color: Color(0xFFE9EBF0)),
                  onPressed: () =>
                      showPreferencesDialog(context, service),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    _bootError ??
                        '${service.trackCount} pistas · '
                            '${service.snapshot.durationSeconds.toStringAsFixed(2)} s · '
                            'dylib ok',
                    style: TextStyle(
                        fontSize: 11,
                        color: _bootError != null
                            ? const Color(0xFFE5484D)
                            : const Color(0xFF9BA1AE)),
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              ],
            ),
          ),
          const Divider(height: 1, color: Color(0xFF32363F)),
          Expanded(
            child: TimelineView(
              service: service,
              frameTick: _frameTick,
              onOpenMidiEditor: (clipId) {
                service.openEditor(clipId);
                service.closeMixer();
                setState(() {});
              },
            ),
          ),
          const Divider(height: 1, color: Color(0xFF32363F)),
          if (_editorOpen && service.editorClipId != null) ...[
            SizedBox(
              height: 320,
              child: PianoRoll(
                  service: service,
                  clipId: service.editorClipId!),
            ),
            const Divider(height: 1, color: Color(0xFF32363F)),
          ] else if (service.mixerOpen) ...[
            SizedBox(
              height: 210,
              child: MixerZone(service: service, frameTick: _frameTick),
            ),
            const Divider(height: 1, color: Color(0xFF32363F)),
          ],
          TransportBar(service: service, frameTick: _frameTick,
              onRecordError: (error) => setState(() => _bootError = error)),
        ],
      ),
    );
  }
}
