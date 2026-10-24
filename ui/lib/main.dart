// Cadence UI — app shell, demo project loader, timeline + transport.
// Multi-window: lower zones (piano roll / automation / mixer) can UNDOCK
// into their own OS windows (desktop_multi_window, one FlutterEngine per
// window) — every window attaches to the SAME engine handle, so edits work
// from anywhere (multi-monitor, Cubase/Mixcraft style).

import 'dart:async';
import 'dart:ui' show AppExitResponse;
import 'dart:convert';

import 'package:desktop_multi_window/desktop_multi_window.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';

import 'engine/engine_service.dart';
import 'l10n/l10n.dart';
import 'l10n/locale_store.dart';
import 'ui/ai_generation_dialog.dart'
    show defaultAiModeForTrack, showAiGenerationDialog;
import 'ui/app_alert.dart';
import 'ui/automation_lane.dart';
import 'ui/export_dialog.dart';
import 'ui/sound_library_dialog.dart';
import 'ui/mixer_zone.dart';
import 'ui/project_dialogs.dart';
import 'ui/piano_roll.dart';
import 'ui/preferences_dialog.dart';
import 'ui/timeline_view.dart';
import 'ui/transport_bar.dart';

Future<void> main(List<String> args) async {
  // Idioma elegido en Preferencias (~/Music/Cadence/ui_settings.json). Cada
  // ventana es un proceso aparte, asi que lo lee tambien la principal.
  LocaleStore.load();
  // Sub-window engines relaunch main with ['multi_window', windowId, json].
  if (args.isNotEmpty && args[0] == 'multi_window') {
    final conf = args.length > 2 && args[2].isNotEmpty
        ? (jsonDecode(args[2]) as Map<String, dynamic>)
        : <String, dynamic>{};
    final handle = (conf['engine'] as num).toInt();
    final service = EngineService.attachEngine(handle);
    service.refresh();
    // Own frame tick + periodic snapshot refresh (this isolate has no UI
    // ticker tied to the main window).
    final frameTick = ValueNotifier<int>(0);
    Timer.periodic(const Duration(milliseconds: 33),
        (_) => frameTick.value++);
    Timer.periodic(const Duration(milliseconds: 150), (_) {
      service.pumpPluginJobs();
      service.refresh();
    });
    WindowController.fromCurrentEngine(); // binds this engine to its window
    runApp(SubWindowApp(
        panel: conf['panel'] as String? ?? 'mixer',
        clipId: conf['clipId'] as int? ?? -1,
        service: service,
        frameTick: frameTick));
    return;
  }
  runApp(const CadenceApp());
}

/// Full-window host for one undocked panel.
class SubWindowApp extends StatelessWidget {
  const SubWindowApp({
    super.key,
    required this.panel,
    required this.clipId,
    required this.service,
    required this.frameTick,
  });

  final String panel;
  final int clipId;
  final EngineService service;
  final ValueListenable<int> frameTick;

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<Locale?>(
      valueListenable: appLocale,
      builder: (context, locale, _) => MaterialApp(
        debugShowCheckedModeBanner: false,
        locale: locale,
        localizationsDelegates: AppLocalizations.localizationsDelegates,
        // `supportedUiLocales` (y no la lista generada, que va en orden
        // alfabetico) para que el idioma de reserva — sistema en un idioma que
        // no hablamos — sea el espanol, no el ingles.
        supportedLocales: supportedUiLocales,
        onGenerateTitle: (context) => context.l10n.appTitle,
        builder: (context, child) {
          L10n.bind(context.l10n);
          return child ?? const SizedBox.shrink();
        },
        theme: cadenceTheme(),
        // El cuerpo se construye DENTRO del MaterialApp: necesita contexto de
        // traducciones (y `panel` puede ser 'piano_roll' sin clip).
        home: Builder(builder: (context) => Scaffold(body: _body(context))),
      ),
    );
  }

  Widget _body(BuildContext context) {
    switch (panel) {
      case 'piano_roll':
        return clipId > 0
            ? PianoRoll(
                service: service, clipId: clipId, frameTick: frameTick)
            : Center(child: Text(context.l10n.appNoClip));
      case 'automation':
        return AutomationLane(
            service: service, frameTick: frameTick, height: null);
      default:
        return MixerZone(service: service, frameTick: frameTick, height: null);
    }
  }
}

/// Tema oscuro comun a la ventana principal y a las sub-ventanas.
ThemeData cadenceTheme() => ThemeData(
      brightness: Brightness.dark,
      scaffoldBackgroundColor: const Color(0xFF16171B),
      colorScheme: ColorScheme.fromSeed(
        seedColor: const Color(0xFF4D9DF7),
        brightness: Brightness.dark,
      ),
    );

class ToggleButton extends StatelessWidget {
  const ToggleButton({
    super.key,
    required this.active,
    required this.onTap,
    required this.label,
    this.icon,
  });

  final bool active;
  final VoidCallback onTap;
  final String label;
  final IconData? icon;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        height: 26,
        padding: const EdgeInsets.symmetric(horizontal: 10),
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: active ? const Color(0xFF4D9DF7) : const Color(0xFF2C303A),
          borderRadius: BorderRadius.circular(5),
          border: Border.all(color: const Color(0xFF3E4350)),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (icon != null) ...[
              Icon(icon,
                  size: 13,
                  color: active ? Colors.black : const Color(0xFFE9EBF0)),
              const SizedBox(width: 4),
            ],
            Text(label,
                style: TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.w600,
                    color: active ? Colors.black : const Color(0xFFE9EBF0))),
          ],
        ),
      ),
    );
  }
}

class CadenceApp extends StatelessWidget {
  const CadenceApp({super.key});

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<Locale?>(
      valueListenable: appLocale,
      builder: (context, locale, _) => MaterialApp(
        debugShowCheckedModeBanner: false,
        locale: locale,
        localizationsDelegates: AppLocalizations.localizationsDelegates,
        // `supportedUiLocales` (y no la lista generada, que va en orden
        // alfabetico) para que el idioma de reserva — sistema en un idioma que
        // no hablamos — sea el espanol, no el ingles.
        supportedLocales: supportedUiLocales,
        onGenerateTitle: (context) => context.l10n.appTitle,
        builder: (context, child) {
          L10n.bind(context.l10n);
          return child ?? const SizedBox.shrink();
        },
        theme: cadenceTheme(),
        home: const HomePage(),
      ),
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
  late final AppLifecycleListener _lifecycle;
  final ValueNotifier<int> _frameTick = ValueNotifier(0);
  /// Fatal: la dylib del motor no se pudo cargar (sin servicio no hay app).
  /// Se guarda el error CRUDO; el texto traducido se compone en build().
  String? _dylibError;
  bool get _editorOpen => _service?.editorClipId != null;
  // Undocked panels: 'piano_roll' | 'automation' | 'mixer' → window id.
  final Map<String, String> _undocked = {};
  // Vertical size of the docked lower zone, per panel (drag the divider).
  final Map<String, double> _zoneHeights = {
    'piano_roll': 320,
    'automation': 190,
    'mixer': 260,
  };
  String? _visiblePanel; // which zone is docked right now

  double _zoneHeight(String panel) {
    final h = _zoneHeights[panel] ?? 260;
    // The zone never eats more than ~65% of the window; tracks keep space.
    final maxZone = MediaQuery.of(context).size.height * 0.65;
    return h.clamp(_minZoneHeight, maxZone);
  }

  static const double _minZoneHeight = 120;

  @override
  void initState() {
    super.initState();
    try {
      _service = EngineService();
    } catch (error) {
      _dylibError = error.toString();
    }
    // Flutter does not dispose the widget tree when the macOS window is
    // closed. Destroy the engine (and every live VST3 instance) explicitly on
    // exit; otherwise plugin static destructors run with live instances and
    // Analog Lab V aborts inside __cxa_finalize_ranges.
    _lifecycle = AppLifecycleListener(
      onExitRequested: () async {
        _service?.dispose();
        _service = null;
        return AppExitResponse.exit;
      },
      onDetach: () {
        _service?.dispose();
        _service = null;
      },
    );
    var frameCount = 0;
    _ticker = createTicker((_) {
      frameCount++;
      if (frameCount % 2 == 0) {
        _frameTick.value++; // ~30 fps for UI updates
        _service?.pumpPluginJobs(); // deferred plugin loads (project open)
      }
      if (frameCount % 60 == 0) _pruneClosedSubWindows();
    })..start();
    WidgetsBinding.instance.addPostFrameCallback((_) => _boot());
  }

  /// Opens [panel] in its own window and hides the docked copy.
  Future<void> _undock(String panel, String title, {int clipId = -1}) async {
    final service = _service;
    if (service == null || _undocked.containsKey(panel)) return;
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
    setState(() {
      _undocked[panel] = controller.windowId;
    });
  }

  /// Sub-windows that the user closed re-dock into the main window.
  Future<void> _pruneClosedSubWindows() async {
    if (_undocked.isEmpty) return;
    try {
      final all = await WindowController.getAll();
      final alive = all.map((w) => w.windowId).toSet();
      final closed = _undocked.entries
          .where((e) => !alive.contains(e.value))
          .map((e) => e.key)
          .toList();
      if (closed.isEmpty) return;
      setState(() {
        for (final panel in closed) {
          _undocked.remove(panel);
        }
      });
    } catch (_) {}
  }

  Future<void> _boot() async {
    final service = _service;
    if (service == null) return;

    service.loadDeviceSettings(); // may be absent on first run
    // The audio device opens lazily on the first play/record call.
    // The project starts EMPTY: open yours with Abrir (⌘O).
    service.updateWindowTitle();
  }

  /// Toolbar entry point: generate on the active instrument track at the
  /// playhead. The dialog itself handles polling, preview and apply/undo.
  Future<void> _openAiGeneration() async {
    final service = _service;
    if (service == null) return;
    final track = service.aiTargetTrack;
    if (track == null) {
      showAppError(context, context.l10n.aiNoInstrumentTrack);
      return;
    }
    await showAiGenerationDialog(
      context,
      service,
      trackId: track.id,
      startSeconds: service.positionSeconds,
      initialMode: defaultAiModeForTrack(track),
    );
  }

  @override
  void dispose() {
    _lifecycle.dispose();
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
            child: Text(
                _dylibError == null
                    ? context.l10n.appEngineUnavailable
                    : context.l10n.appDylibError(_dylibError!),
                textAlign: TextAlign.center,
                style: const TextStyle(color: Color(0xFFE5484D))),
          ),
        ),
      );
    }

    // Which lower zone is docked right now (divider + per-panel heights).
    _visiblePanel = _editorOpen && service.editorClipId != null
        ? 'piano_roll'
        : service.mixerOpen
            ? 'mixer'
            : service.automationOpen
                ? 'automation'
                : null;

    return Scaffold(
      body: CallbackShortcuts(
        bindings: {
          const SingleActivator(LogicalKeyboardKey.keyS, meta: true): () =>
              _saveProject(context),
          const SingleActivator(LogicalKeyboardKey.keyO, meta: true): () =>
              showOpenDialog(context, service),
          const SingleActivator(LogicalKeyboardKey.keyN, meta: true): () {
            final error = service.newEmptyProject();
            if (error != null) showAppError(context, error);
          },
          const SingleActivator(LogicalKeyboardKey.keyC, meta: true): () {
            service.copySelectedClip();
          },
          const SingleActivator(LogicalKeyboardKey.keyV, meta: true): () {
            final error = service.pasteClipboard();
            if (error != null) showAppError(context, error);
          },
          // Cubase-style transport keys.
          const SingleActivator(LogicalKeyboardKey.space): () {
            // Play/pausa también cierra la toma: no debería hacer falta
            // volver a pulsar R para ver lo que se acaba de tocar.
            if (service.isRecording) {
              service.stopRecord();
            } else if (service.isPlaying) {
              service.pause();
            } else {
              final error = service.play();
              if (error != null) showAppError(context, error);
            }
          },
          const SingleActivator(LogicalKeyboardKey.keyR): () {
            if (service.isRecording) {
              service.stopRecord();
            } else {
              final error = service.startRecord();
              if (error != null) showAppError(context, error);
            }
          },
        },
        child: Focus(
          autofocus: true,
          child: Column(
        children: [
          // Toolbar v0: project actions + views + status. Menú real §5.2.
          Container(
            height: 40,
            color: const Color(0xFF1E2026),
            padding: const EdgeInsets.symmetric(horizontal: 12),
            child: Row(
              children: [
                GestureDetector(
                  onTap: () => showAboutAppDialog(context),
                  child: MouseRegion(
                    cursor: SystemMouseCursors.click,
                    child: Text(context.l10n.projectAppName,
                        style: const TextStyle(fontWeight: FontWeight.w600)),
                  ),
                ),
                const SizedBox(width: 12),
                OutlinedButton(
                  onPressed: () {
                    final error = service.newEmptyProject();
                    if (error != null) showAppError(context, error);
                  },
                  child: Text(context.l10n.appMenuNew),
                ),
                const SizedBox(width: 6),
                OutlinedButton(
                  onPressed: () => showOpenDialog(context, service),
                  child: Text(context.l10n.appMenuOpen),
                ),
                const SizedBox(width: 6),
                OutlinedButton(
                  onPressed: () => _saveProject(context),
                  child: Text(context.l10n.appMenuSave),
                ),
                OutlinedButton(
                  onPressed: () => showExportDialog(context, service),
                  child: Text(context.l10n.appMenuExport),
                ),

                const SizedBox(width: 8),
                OutlinedButton.icon(
                  onPressed: () => showSoundLibraryDialog(context, service),
                  icon: const Icon(Icons.library_music, size: 18),
                  label: Text(context.l10n.appMenuSounds),
                ),

                const SizedBox(width: 8),
                Tooltip(
                  message: context.l10n.aiToolbarTooltip,
                  child: OutlinedButton.icon(
                    onPressed: () => _openAiGeneration(),
                    style: OutlinedButton.styleFrom(
                      foregroundColor: const Color(0xFF7FB7FF),
                      side: const BorderSide(color: Color(0xFF3A5F8A)),
                    ),
                    icon: const Icon(Icons.auto_awesome,
                        size: 18, color: Color(0xFF7FB7FF)),
                    label: Text(context.l10n.aiToolbarButton),
                  ),
                ),

                const SizedBox(width: 8),
                ToggleButton(
                  active: service.snapToGrid,
                  onTap: () {
                    service.toggleSnapToGrid();
                    setState(() {});
                  },
                  label: context.l10n.appMenuGrid,
                  icon: Icons.grid_on,
                ),
                const SizedBox(width: 6),
                ToggleButton(
                  active: service.mixerOpen,
                  onTap: () {
                    service.toggleMixerOpen();
                    setState(() {});
                  },
                  label: context.l10n.appMenuMixer,
                  icon: Icons.equalizer,
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
                  label: context.l10n.appMenuPianoRoll,
                  icon: Icons.piano,
                ),
                const SizedBox(width: 6),
                ToggleButton(
                  active: service.automationOpen,
                  onTap: () {
                    service.toggleAutomationOpen();
                    setState(() {});
                  },
                  label: context.l10n.appMenuAutomation,
                  icon: Icons.show_chart,
                ),
                IconButton(
                  tooltip: context.l10n.appMenuPreferences,
                  icon: const Icon(Icons.settings, size: 18,
                      color: Color(0xFFE9EBF0)),
                  onPressed: () =>
                      showPreferencesDialog(context, service),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Text(
                    context.l10n.appStatusLine(
                        '${service.trackCount}',
                        service.snapshot.durationSeconds.toStringAsFixed(2)),
                    style: const TextStyle(
                        fontSize: 11, color: Color(0xFF9BA1AE)),
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
          // Undocked zones live in their own window; the main window hides
          // them so the tracks get the space. Closing the sub-window re-docks.
          if (_visiblePanel != null) ...[
            // Divider between tracks and the lower zone: DRAG vertically to
            // give the zone more/less vertical space (Cubase-style).
            GestureDetector(
              onVerticalDragUpdate: (d) {
                final panel = _visiblePanel!;
                setState(() {
                  _zoneHeights[panel] =
                      ((_zoneHeights[panel] ?? 260) - d.delta.dy)
                          .clamp(_minZoneHeight, double.infinity);
                });
              },
              child: MouseRegion(
                cursor: SystemMouseCursors.resizeRow,
                child: Container(
                  height: 8,
                  color: const Color(0xFF32363F),
                  alignment: Alignment.center,
                  child: Container(
                    width: 44,
                    height: 2,
                    color: const Color(0xFF5C6270),
                  ),
                ),
              ),
            ),
            if (_visiblePanel == 'piano_roll' &&
                !_undocked.containsKey('piano_roll'))
              SizedBox(
                height: _zoneHeight('piano_roll'),
                child: PianoRoll(
                    service: service,
                    clipId: service.editorClipId!,
                    frameTick: _frameTick,
                    onUndock: () => _undock(
                        'piano_roll', context.l10n.appWindowPianoRoll,
                        clipId: service.editorClipId!)),
              )
            else if (_visiblePanel == 'mixer' &&
                !_undocked.containsKey('mixer'))
              SizedBox(
                height: _zoneHeight('mixer'),
                child: MixerZone(
                    service: service,
                    frameTick: _frameTick,
                    onUndock: () => _undock(
                        'mixer', context.l10n.appWindowMixer)),
              )
            else if (_visiblePanel == 'automation' &&
                !_undocked.containsKey('automation'))
              SizedBox(
                height: _zoneHeight('automation'),
                child: AutomationLane(
                    service: service,
                    frameTick: _frameTick,
                    onUndock: () => _undock(
                        'automation', context.l10n.appWindowAutomation)),
              ),
            const Divider(height: 1, color: Color(0xFF32363F)),
          ],
          TransportBar(service: service, frameTick: _frameTick,
              onError: (error) => showAppError(context, error)),
        ],
          ),
        ),
      ),
    );
  }

  /// Guardar: reuses the current file when there is one, otherwise the
  /// name dialog. ⌘S routes here too.
  void _saveProject(BuildContext context) {
    final service = _service;
    if (service == null) return;
    if (service.lastProjectPath != null) {
      service.saveProject(service.lastProjectPath!);
    } else {
      showSaveAsDialog(context, service);
    }
  }
}
