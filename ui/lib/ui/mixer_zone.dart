// Mixer lower zone (§7.6 v0): channel strips with fader, pan, M/S and
// read-and-reset peak meters polled once per frame; master strip apart.
// Inserts/sends/EQ arrive when the engine grows them.

import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';


import '../engine/engine_service.dart';
import '../l10n/l10n.dart';
import 'channel_settings_dialog.dart' show showChannelSettingsDialog;
import 'plugin_picker_dialog.dart' show showPluginPickerDialog;
import '../engine/project_snapshot.dart';

const double _meterMax = 1.5; // linear full scale for the meters

class MixerZone extends StatefulWidget {
  const MixerZone(
      {super.key,
      required this.service,
      required this.frameTick,
      this.height = 210,
      this.onUndock});

  final EngineService service;
  final ValueListenable<int> frameTick;
  /// null = fill the parent (undocked window); fixed in the main window.
  final double? height;
  final VoidCallback? onUndock;

  @override
  State<MixerZone> createState() => _MixerZoneState();
}

class _MixerZoneState extends State<MixerZone> {
  final ScrollController _hScroll = ScrollController();

  @override
  void dispose() {
    _hScroll.dispose();
    super.dispose();
  }

  /// La rueda desplaza el mixer a lo ancho: un SingleChildScrollView horizontal
  /// ignora scrollDelta.dy, así que sin esto las pistas de la derecha quedaban
  /// inalcanzables con el ratón. El delta horizontal del trackpad ya lo aplica
  /// el propio scrollable (no se estorban: la rueda de ratón solo trae dy).
  void _onPointerSignal(PointerSignalEvent event) {
    if (event is! PointerScrollEvent) return;
    if (!_hScroll.hasClients) return;
    final dy = event.scrollDelta.dy;
    final delta = dy != 0 ? dy : event.scrollDelta.dx;
    if (delta == 0) return;
    final target = (_hScroll.offset + delta)
        .clamp(0.0, _hScroll.position.maxScrollExtent);
    _hScroll.position.jumpTo(target);
  }

  @override
  Widget build(BuildContext context) {
    final service = widget.service;
    final frameTick = widget.frameTick;
    final height = widget.height;
    return AnimatedBuilder(
      animation: service,
      builder: (context, _) {
        final snapshot = service.snapshot;
        final tracks = snapshot.tracks;
        // The zone grows DOWNWARD with the busiest channel's fx count: the
        // fader keeps its fixed height and every strip stays the same size.
        final maxFx = <int>[
          for (final t in tracks) t.fxNames.length,
          snapshot.masterFxNames.length,
        ].fold(0, (a, b) => math.max(a, b));
        // Strip anatomy: name 18 + gap 4 + fader 110 + db 16 + M/S 26 +
        // margins 12 = 186, plus the fx area (rows of ~15 + '+ FX' 24).
        final fxAreaHeight = 24.0 + maxFx * 15.0;
        final stripHeight = 194.0 + fxAreaHeight;
        final zoneHeight = math.max(height ?? 0, stripHeight + 42.0);
        return Container(
          height: zoneHeight,
          color: const Color(0xFF1B1D22),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Undock affordance for the whole mixer (top-right corner).
              Align(
                alignment: Alignment.topRight,
                child: IconButton(
                  tooltip: context.l10n.mixerUndock,
                  visualDensity: VisualDensity.compact,
                  icon: const Icon(Icons.open_in_new, size: 14,
                      color: Color(0xFF9BA1AE)),
                  onPressed: widget.onUndock,
                ),
              ),
              Expanded(
                child: _MixerScrollView(
                  controller: _hScroll,
                  onPointerSignal: _onPointerSignal,
                  child: Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                for (var i = 0; i < tracks.length; i++)
                  _Strip(
                    name: tracks[i].name,
                    fxAreaHeight: fxAreaHeight,
                    color: trackColor(tracks[i].colorIndex),
                    volume: tracks[i].volume.clamp(0.0, 2.0).toDouble(),
                    pan: tracks[i].pan,
                    mute: tracks[i].mute,
                    solo: tracks[i].solo,
                    isMaster: false,
                    frameTick: frameTick,
                    fxNames: tracks[i].fxNames,
                    fxBypass: tracks[i].fxBypass,
                    onToggleFxBypass: (slot) =>
                        service.toggleTrackFxBypass(tracks[i].id, slot),
                    onRemoveFx: (slot) =>
                        service.removeTrackFx(tracks[i].id, slot),
                    onOpenEditor: (slot) {
                      service.openTrackPluginEditor(tracks[i].id, slot);
                    },
                    onOpenChannel: () =>
                        showChannelSettingsDialog(context, service, tracks[i].id),
                    onAddFx: () async {
                      final picked = await showPluginPickerDialog(
                        context,
                        service,
                        onlyEffects: true,
                        title: context.l10n.mixerFxOf(tracks[i].name),
                      );
                      if (picked != null) {
                        // Waits for the engine to apply the add, then opens
                        // the NEW slot (a build-time count would be stale).
                        await service.addTrackFxAndOpen(
                            tracks[i].id, picked.index);
                      }
                    },
                    readPeak: () => service.meterTrackPeak(i),
                    hasVolumeAutomation: tracks[i].volumeAuto.isNotEmpty,
                    readEffectiveVolume: () => service
                        .trackVolumeEffectiveAt(tracks[i].id,
                            service.positionSeconds),
                    onVolume: (v) => service.setTrackVolume(tracks[i].id, v),
                    onPan: (v) => service.setTrackPan(tracks[i].id, v),
                    onMute: () =>
                        service.setTrackMute(tracks[i].id, !tracks[i].mute),
                    onSolo: () =>
                        service.setTrackSolo(tracks[i].id, !tracks[i].solo),
                  ),
                const VerticalDivider(width: 8, color: Color(0xFF32363F)),
                _Strip(
                  name: context.l10n.mixerMaster,
                  fxAreaHeight: fxAreaHeight,
                  color: const Color(0xFF9BA1AE),
                  volume: snapshot.masterGain.clamp(0.0, 2.0).toDouble(),
                  pan: 0,
                  mute: false,
                  solo: false,
                  isMaster: true,
                  frameTick: frameTick,
                  fxNames: snapshot.masterFxNames,
                  fxBypass: snapshot.masterFxBypass,
                  onToggleFxBypass: (slot) =>
                      service.toggleMasterFxBypass(slot),
                  onRemoveFx: (slot) => service.removeMasterFx(slot),
                  onAddFx: () async {
                    final picked = await showPluginPickerDialog(
                      context,
                      service,
                      onlyEffects: true,
                      title: context.l10n.mixerMasterFx,
                    );
                    if (picked != null) {
                      service.addMasterFx(picked.index);
                    }
                  },
                  readPeakL: () => service.meterMasterPeak(0),
                  readPeakR: () => service.meterMasterPeak(1),
                  onVolume: (v) => service.setMasterGain(v),
                ),
              ],
                ),
              ),
              ),
            ],
          ),
        );
      },
    );
  }
}

/// Horizontal scroll for the mixer strips (REQUIRED: a horizontal
/// SingleChildScrollView ignores scrollDelta.dy, so with a mouse the strips
/// past the window edge were unreachable). The wheel is handled by the parent
/// [_MixerZoneState._onPointerSignal]; the Scrollbar thumb is draggable, which
/// needs Clamping (NeverScrollable would reject the drag); pointer-drag stays
/// off so the faders keep their drags — same criteria as the timeline.
class _MixerScrollView extends StatelessWidget {
  const _MixerScrollView({
    required this.controller,
    required this.onPointerSignal,
    required this.child,
  });

  final ScrollController controller;
  final void Function(PointerSignalEvent event) onPointerSignal;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Listener(
      onPointerSignal: onPointerSignal,
      child: ScrollConfiguration(
        behavior: ScrollConfiguration.of(context)
            .copyWith(dragDevices: const {}),
        child: Scrollbar(
          controller: controller,
          thumbVisibility: true,
          thickness: 10,
          scrollbarOrientation: ScrollbarOrientation.bottom,
          child: SingleChildScrollView(
            controller: controller,
            physics: const ClampingScrollPhysics(),
            scrollDirection: Axis.horizontal,
            child: child,
          ),
        ),
      ),
    );
  }
}

class _Strip extends StatelessWidget {
  const _Strip({
    required this.name,
    required this.color,
    required this.volume,
    required this.pan,
    required this.mute,
    required this.solo,
    required this.isMaster,
    required this.frameTick,
    required this.onVolume,
    required this.fxNames,
    this.fxBypass = const [],
    required this.onToggleFxBypass,
    this.onRemoveFx,
    this.onOpenEditor,
    this.onOpenChannel,
    this.onAddFx,
    this.hasVolumeAutomation = false,
    this.readEffectiveVolume,
    this.readPeak,
    this.readPeakL,
    this.readPeakR,
    this.onPan,
    this.onMute,
    this.onSolo,
    this.fxAreaHeight = 46.0,
  });

  final String name;
  final Color color;
  final double volume;
  final double pan;
  final bool mute;
  final bool solo;
  final bool isMaster;
  final ValueListenable<int> frameTick;
  final void Function(double) onVolume;
  final List<String> fxNames;
  final List<bool> fxBypass;
  final void Function(int slotIndex)? onToggleFxBypass;
  final void Function(int slotIndex)? onRemoveFx;
  final void Function(int slotIndex)? onOpenEditor; // double-tap: plugin GUI
  final VoidCallback? onOpenChannel; // tap the strip name: channel settings
  final Future<void> Function()? onAddFx;
  final bool hasVolumeAutomation; // Read mode: the fader mirrors the curve
  final double Function()? readEffectiveVolume;
  /// Fixed height of the fx list area: every strip reserves the same space
  /// so all channels keep identical dimensions as fx are added/removed.
  final double fxAreaHeight;
  final void Function(double)? onPan;
  final VoidCallback? onMute;
  final VoidCallback? onSolo;
  final double Function()? readPeak;
  final double Function()? readPeakL;
  final double Function()? readPeakR;

  String _db(double linear) =>
      linear <= 0.001 ? '-inf' : (20 * math.log(linear)).toStringAsFixed(1);

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: frameTick,
      builder: (context, _) {
        // Read mode: with volume automation the fader mirrors the automated
        // gain in real time instead of the static volume.
        final effectiveVolume =
            hasVolumeAutomation && readEffectiveVolume != null
                ? readEffectiveVolume!().clamp(0.0, 2.0)
                : volume;
        return Container(
          width: 96,
          margin: const EdgeInsets.symmetric(horizontal: 3, vertical: 6),
          decoration: BoxDecoration(
            color: const Color(0xFF252830),
            borderRadius: BorderRadius.circular(6),
            border: Border.all(color: const Color(0xFF32363F)),
          ),
          child: Column(
            children: [
              GestureDetector(
                onTap: onOpenChannel,
                child: Container(
                  height: 18,
                  alignment: Alignment.center,
                  decoration: BoxDecoration(
                    color: color.withValues(alpha: 0.25),
                    borderRadius: const BorderRadius.vertical(
                      top: Radius.circular(5),
                    ),
                  ),
                  child: Text(
                    name,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      fontSize: 10,
                      fontWeight: FontWeight.w600,
                      color: Color(0xFFE9EBF0),
                    ),
                  ),
                ),
              ),
              const SizedBox(height: 4),
              // Fixed fader height: adding fx rows grows the strip DOWNWARD,
              // the fader never shrinks.
              SizedBox(
                height: 110,
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    // Vertical fader (rotated slider).
                    SizedBox(
                      width: 26,
                      child: RotatedBox(
                        quarterTurns: 3,
                        child: SliderTheme(
                          data: SliderTheme.of(context).copyWith(
                            trackHeight: 3,
                            thumbShape: const RoundSliderThumbShape(
                              enabledThumbRadius: 7,
                            ),
                            overlayShape: const RoundSliderOverlayShape(
                              overlayRadius: 10,
                            ),
                            padding: EdgeInsets.zero,
                          ),
                          child: Slider(
                            min: 0,
                            max: 2,
                            value: effectiveVolume.clamp(0.0, 2.0),
                            onChanged: onVolume,
                          ),
                        ),
                      ),
                    ),
                    const SizedBox(width: 4),
                    if (isMaster)
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 6),
                        child: Row(
                          children: [
                            _MeterBar(readPeak: readPeakL ?? () => 0.0),
                            const SizedBox(width: 2),
                            _MeterBar(readPeak: readPeakR ?? () => 0.0),
                          ],
                        ),
                      )
                    else
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 6),
                        child: _MeterBar(readPeak: readPeak ?? () => 0.0),
                      ),
                  ],
                ),
              ),
              Text(
                _db(volume),
                style: TextStyle(
                  fontSize: 9,
                  fontFamily: 'monospace',
                  color: const Color(0xFF9BA1AE),
                ),
              ),
              // FX chips: tap = bypass, x = remove, double-tap = editor;
              // the "+ FX" chip must render even with an empty chain — it
              // is the only entry point for adding the first plugin.
              // The area EXPANDS: with the zone sized to the busiest
              // channel, every strip fits its rows and grows as fx are
              // added (the zone itself grows downward).
              Expanded(
                child: Padding(
                padding: const EdgeInsets.only(top: 2, left: 6, right: 6),
                child: Align(
                  alignment: Alignment.topCenter,
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      // ONE FX PER ROW (full width): name + bypass + editor
                      // + remove, readable without hovering.
                      for (var i = 0; i < fxNames.length; i++)
                        GestureDetector(
                          onTap: onToggleFxBypass == null
                              ? null
                              : () => onToggleFxBypass!(i),
                          onDoubleTap: onOpenEditor == null
                              ? null
                              : () => onOpenEditor!(i),
                          child: Container(
                            width: double.infinity,
                            margin: const EdgeInsets.only(bottom: 2),
                            padding: const EdgeInsets.symmetric(
                              horizontal: 5,
                              vertical: 2,
                            ),
                            decoration: BoxDecoration(
                              color: const Color(0xFF2C303A),
                              borderRadius: BorderRadius.circular(3),
                              border: Border.all(
                                color: const Color(0xFF3E4350),
                                width: 0.5,
                              ),
                            ),
                            child: Builder(
                              builder: (context) {
                                final bypassed =
                                    i < fxBypass.length && fxBypass[i];
                                return Row(
                                  children: [
                                    Expanded(
                                      child: Text(
                                        fxNames[i],
                                        maxLines: 1,
                                        overflow: TextOverflow.ellipsis,
                                        style: TextStyle(
                                          fontSize: 8.5,
                                          color: bypassed
                                              ? Colors.white24
                                              : Colors.white70,
                                          decoration: bypassed
                                              ? TextDecoration.lineThrough
                                              : null,
                                          decorationColor: Colors.redAccent,
                                        ),
                                      ),
                                    ),
                                    GestureDetector(
                                      onTap: onRemoveFx == null
                                          ? null
                                          : () => onRemoveFx!(i),
                                      child: const Text(
                                        'x',
                                        style: TextStyle(
                                          fontSize: 8.5,
                                          fontWeight: FontWeight.w700,
                                          color: Color(0xFFE5484D),
                                        ),
                                      ),
                                    ),
                                  ],
                                );
                              },
                            ),
                          ),
                        ),
                      if (onAddFx != null)
                        GestureDetector(
                          onTap: onAddFx,
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 6,
                              vertical: 2,
                            ),
                            decoration: BoxDecoration(
                              color: const Color(
                                0xFF4D9DF7,
                              ).withValues(alpha: 0.2),
                              borderRadius: BorderRadius.circular(3),
                              border: Border.all(
                                color: const Color(0xFF4D9DF7),
                                width: 1,
                              ),
                            ),
                            child: const Text(
                              '+ FX',
                              style: TextStyle(
                                fontSize: 9,
                                fontWeight: FontWeight.w600,
                                color: Color(0xFF4D9DF7),
                              ),
                            ),
                          ),
                        ),
                    ],
                  ),
                ),
              ),
              ),
              if (!isMaster)
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 6),
                  child: Row(
                    children: [
                      _TinyButton(
                        label: 'M',
                        color: const Color(0xFFF5A524),
                        active: mute,
                        onTap: onMute,
                      ),
                      const SizedBox(width: 3),
                      _TinyButton(
                        label: 'S',
                        color: const Color(0xFF4D9DF7),
                        active: solo,
                        onTap: onSolo,
                      ),
                    ],
                  ),
                ),
              SizedBox(
                height: 16,
                child: onPan != null
                    ? SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape: const RoundSliderThumbShape(
                            enabledThumbRadius: 4,
                          ),
                          overlayShape: const RoundSliderOverlayShape(
                            overlayRadius: 6,
                          ),
                          padding: EdgeInsets.zero,
                        ),
                        child: Slider(
                          min: -1,
                          max: 1,
                          value: pan,
                          onChanged: onPan,
                        ),
                      )
                    : const SizedBox(),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _MeterBar extends StatelessWidget {
  const _MeterBar({required this.readPeak});

  final double Function() readPeak;

  static const double _floorDb = -48.0;

  // Amplitud lineal -> fracción de barra en dB: -48 dBFS abajo, 0 arriba.
  // La escala lineal pintaba migaja: un nivel sano de mezcla (-12 dBFS)
  // es solo 0.25 de amplitud = 17% de barra.
  static double _fraction(double peak) {
    if (peak <= 0) return 0.0;
    final dB = 20.0 * math.log(peak) / math.ln10;
    return ((dB - _floorDb) / -_floorDb).clamp(0.0, 1.0);
  }

  static double _dbOf(double peak) {
    if (peak <= 0) return -999.0;
    return 20.0 * math.log(peak) / math.ln10;
  }

  Color _color(double v) {
    final dB = _dbOf(v);
    if (dB > -3) return const Color(0xFFE5484D); // casi/sobre 0 dBFS
    if (dB > -12) return const Color(0xFFF5A524); // zona caliente
    return const Color(0xFF3DD68C);
  }

  @override
  Widget build(BuildContext context) {
    // Read-and-reset: this build IS the poll (driven by frameTick upstream).
    final peak = readPeak().clamp(0.0, _meterMax);
    return Container(
      width: 7,
      decoration: BoxDecoration(
        color: const Color(0xFF16171B),
        borderRadius: BorderRadius.circular(2),
      ),
      child: Align(
        alignment: Alignment.bottomCenter,
        child: FractionallySizedBox(
          heightFactor: _fraction(peak),
          child: Container(
            decoration: BoxDecoration(
              color: _color(peak),
              borderRadius: BorderRadius.circular(2),
            ),
          ),
        ),
      ),
    );
  }
}

class _TinyButton extends StatelessWidget {
  const _TinyButton({
    required this.label,
    required this.color,
    required this.active,
    required this.onTap,
  });

  final String label;
  final Color color;
  final bool active;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    return Expanded(
      child: GestureDetector(
        onTap: onTap,
        child: Container(
          height: 18,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: active ? color : const Color(0xFF2C303A),
            borderRadius: BorderRadius.circular(3),
          ),
          child: Text(
            label,
            style: TextStyle(
              fontSize: 10,
              fontWeight: FontWeight.w600,
              color: active ? Colors.black : const Color(0xFF9BA1AE),
            ),
          ),
        ),
      ),
    );
  }
}
