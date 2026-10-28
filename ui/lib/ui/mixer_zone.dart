// Mixer lower zone: console-style channel strips with a decibel fader,
// pan knob, gradient peak meters with hold and compact insert chips.
//
// The interaction surface is unchanged (one strip per track + master, wheel
// scrolls the console horizontally, +FX is always reachable); only the
// rendering now follows the same language as the mobile remote: rounded
// cards, a colour bar per channel, dB-scaled controls and tinted content.

import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import '../engine/project_snapshot.dart';
import '../l10n/l10n.dart';
import 'channel_settings_dialog.dart' show showChannelSettingsDialog;
import 'design.dart';
import 'plugin_picker_dialog.dart' show showPluginPickerDialog;

class MixerZone extends StatefulWidget {
  const MixerZone({
    super.key,
    required this.service,
    required this.frameTick,
    this.height = 210,
    this.onUndock,
  });

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
    final target = (_hScroll.offset + delta).clamp(
      0.0,
      _hScroll.position.maxScrollExtent,
    );
    _hScroll.position.jumpTo(target);
  }

  @override
  Widget build(BuildContext context) {
    final service = widget.service;
    final frameTick = widget.frameTick;
    return AnimatedBuilder(
      animation: service,
      builder: (context, _) {
        final snapshot = service.snapshot;
        final tracks = snapshot.tracks;
        final maxFx = <int>[
          for (final t in tracks) t.fxNames.length,
          snapshot.masterFxNames.length,
        ].fold(0, math.max);

        return Container(
          height: widget.height,
          decoration: const BoxDecoration(
            color: AppColors.bgDeep,
            border: Border(top: BorderSide(color: AppColors.border)),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _MixerHeader(
                channelCount: tracks.length,
                onUndock: widget.onUndock,
              ),
              Expanded(
                child: LayoutBuilder(
                  builder: (context, constraints) {
                    // Strip height follows the docked zone; the fader is the
                    // flexible part, so short zones stay usable instead of
                    // overflowing.
                    final stripHeight = math.max(
                      172.0,
                      constraints.maxHeight - 14,
                    );
                    final tight = stripHeight < 250;
                    final fxRows = math.min(maxFx, tight ? 2 : 3);

                    return _MixerScrollView(
                      controller: _hScroll,
                      onPointerSignal: _onPointerSignal,
                      child: SizedBox(
                        height: stripHeight,
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          children: [
                            const SizedBox(width: 8),
                            for (var i = 0; i < tracks.length; i++)
                              Padding(
                                padding: const EdgeInsets.only(right: 6),
                                child: _Strip(
                                  index: i,
                                  name: tracks[i].name,
                                  subtitle: _trackSubtitle(context, tracks[i]),
                                  color: trackColor(tracks[i].colorIndex),
                                  volume:
                                      tracks[i].volume
                                          .clamp(0.0, 2.0)
                                          .toDouble(),
                                  pan: tracks[i].pan,
                                  mute: tracks[i].mute,
                                  solo: tracks[i].solo,
                                  armed: tracks[i].armed,
                                  isMaster: false,
                                  frameTick: frameTick,
                                  fxNames: tracks[i].fxNames,
                                  fxBypass: tracks[i].fxBypass,
                                  fxRows: fxRows,
                                  automationActive:
                                      tracks[i].volumeAuto.isNotEmpty ||
                                      tracks[i].panAuto.isNotEmpty ||
                                      tracks[i].sustainAuto.isNotEmpty ||
                                      tracks[i].paramLanes.isNotEmpty,
                                  onToggleFxBypass:
                                      (slot) => service.toggleTrackFxBypass(
                                        tracks[i].id,
                                        slot,
                                      ),
                                  onRemoveFx:
                                      (slot) => service.removeTrackFx(
                                        tracks[i].id,
                                        slot,
                                      ),
                                  onOpenEditor:
                                      (slot) => service.openTrackPluginEditor(
                                        tracks[i].id,
                                        slot,
                                      ),
                                  onOpenChannel:
                                      () => showChannelSettingsDialog(
                                        context,
                                        service,
                                        tracks[i].id,
                                      ),
                                  onAddFx: () async {
                                    final picked = await showPluginPickerDialog(
                                      context,
                                      service,
                                      onlyEffects: true,
                                      title: context.l10n.mixerFxOf(
                                        tracks[i].name,
                                      ),
                                    );
                                    if (picked != null) {
                                      // Waits for the engine to apply the add,
                                      // then opens the NEW slot (a build-time
                                      // count would be stale).
                                      await service.addTrackFxAndOpen(
                                        tracks[i].id,
                                        picked.index,
                                      );
                                    }
                                  },
                                  readPeak: () => service.meterTrackPeak(i),
                                  hasVolumeAutomation:
                                      tracks[i].volumeAuto.isNotEmpty,
                                  readEffectiveVolume:
                                      () => service.trackVolumeEffectiveAt(
                                        tracks[i].id,
                                        service.positionSeconds,
                                      ),
                                  onVolume:
                                      (v) => service.setTrackVolume(
                                        tracks[i].id,
                                        v,
                                      ),
                                  onPan:
                                      (v) =>
                                          service.setTrackPan(tracks[i].id, v),
                                  onMute:
                                      () => service.setTrackMute(
                                        tracks[i].id,
                                        !tracks[i].mute,
                                      ),
                                  onSolo:
                                      () => service.setTrackSolo(
                                        tracks[i].id,
                                        !tracks[i].solo,
                                      ),
                                  onArm:
                                      () => service.setTrackArmed(
                                        tracks[i].id,
                                        !tracks[i].armed,
                                      ),
                                ),
                              ),
                            Container(
                              width: 1,
                              margin: const EdgeInsets.symmetric(vertical: 4),
                              color: AppColors.border,
                            ),
                            Padding(
                              padding: const EdgeInsets.only(
                                left: 6,
                                right: 10,
                              ),
                              child: _Strip(
                                index: tracks.length,
                                name: context.l10n.mixerMaster,
                                subtitle: 'Stereo Out',
                                color: AppColors.warning,
                                volume:
                                    snapshot.masterGain
                                        .clamp(0.0, 4.0)
                                        .toDouble(),
                                pan: 0,
                                mute: false,
                                solo: false,
                                armed: false,
                                isMaster: true,
                                frameTick: frameTick,
                                fxNames: snapshot.masterFxNames,
                                fxBypass: snapshot.masterFxBypass,
                                fxRows: fxRows,
                                onToggleFxBypass:
                                    (slot) =>
                                        service.toggleMasterFxBypass(slot),
                                onRemoveFx:
                                    (slot) => service.removeMasterFx(slot),
                                onOpenEditor:
                                    (slot) => service.openMasterFxEditor(slot),
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
                            ),
                          ],
                        ),
                      ),
                    );
                  },
                ),
              ),
            ],
          ),
        );
      },
    );
  }

  String _trackSubtitle(BuildContext context, TrackInfo track) {
    if (track.type == 1) {
      return track.instrumentName.isEmpty
          ? context.l10n.timelineInternalSynth
          : track.instrumentName;
    }
    return 'IN ${track.inputChannel == 0 ? 'L' : 'R'}';
  }
}

class _MixerHeader extends StatelessWidget {
  const _MixerHeader({required this.channelCount, this.onUndock});

  final int channelCount;
  final VoidCallback? onUndock;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 24,
      padding: const EdgeInsets.only(left: 10, right: 2),
      decoration: const BoxDecoration(
        color: AppColors.bgPanel,
        border: Border(bottom: BorderSide(color: AppColors.border)),
      ),
      child: Row(
        children: [
          const Icon(
            Icons.equalizer_rounded,
            size: 13,
            color: AppColors.accent,
          ),
          const SizedBox(width: 6),
          Text(
            context.l10n.appMenuMixer.toUpperCase(),
            style: const TextStyle(
              fontSize: 9,
              fontWeight: FontWeight.w700,
              letterSpacing: 0.9,
              color: AppColors.textDim,
            ),
          ),
          const SizedBox(width: 7),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 1),
            decoration: BoxDecoration(
              color: AppColors.surfaceAlt,
              borderRadius: BorderRadius.circular(4),
              border: Border.all(color: AppColors.border),
            ),
            child: Text(
              '$channelCount',
              style: const TextStyle(
                fontSize: 8.5,
                fontWeight: FontWeight.w600,
                color: AppColors.textFaint,
                fontFeatures: [FontFeature.tabularFigures()],
              ),
            ),
          ),
          const Spacer(),
          IconButton(
            tooltip: context.l10n.mixerUndock,
            visualDensity: VisualDensity.compact,
            padding: EdgeInsets.zero,
            constraints: const BoxConstraints.tightFor(width: 24, height: 22),
            icon: const Icon(
              Icons.open_in_new,
              size: 13,
              color: AppColors.textFaint,
            ),
            onPressed: onUndock,
          ),
        ],
      ),
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
        behavior: ScrollConfiguration.of(
          context,
        ).copyWith(dragDevices: const {}),
        child: Scrollbar(
          controller: controller,
          thumbVisibility: true,
          thickness: 8,
          scrollbarOrientation: ScrollbarOrientation.bottom,
          child: SingleChildScrollView(
            controller: controller,
            physics: const ClampingScrollPhysics(),
            scrollDirection: Axis.horizontal,
            padding: const EdgeInsets.only(bottom: 8),
            child: child,
          ),
        ),
      ),
    );
  }
}

class _Strip extends StatelessWidget {
  const _Strip({
    required this.index,
    required this.name,
    required this.subtitle,
    required this.color,
    required this.volume,
    required this.pan,
    required this.mute,
    required this.solo,
    required this.armed,
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
    this.onArm,
    this.fxRows = 0,
    this.automationActive = false,
  });

  final int index;
  final String name;
  final String subtitle;
  final Color color;
  final double volume;
  final double pan;
  final bool mute;
  final bool solo;
  final bool armed;
  final bool isMaster;
  final ValueListenable<int> frameTick;
  final void Function(double) onVolume;
  final List<String> fxNames;
  final List<bool> fxBypass;
  final Function(int slotIndex)? onToggleFxBypass;
  final Function(int slotIndex)? onRemoveFx;
  final Function(int slotIndex)? onOpenEditor; // double-tap: plugin GUI
  final VoidCallback? onOpenChannel; // tap the strip name: channel settings
  final Future<void> Function()? onAddFx;
  final bool hasVolumeAutomation; // Read mode: the fader mirrors the curve
  final double Function()? readEffectiveVolume;
  final int fxRows; // how many insert chips fit in the fixed fx block
  final bool automationActive;
  final void Function(double)? onPan;
  final VoidCallback? onMute;
  final VoidCallback? onSolo;
  final VoidCallback? onArm;
  final double Function()? readPeak;
  final double Function()? readPeakL;
  final double Function()? readPeakR;

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

        return LayoutBuilder(
          builder: (context, constraints) {
            final tight = constraints.maxHeight < 250;
            final stripWidth = isMaster ? 114.0 : 106.0;
            final showSubtitle = !tight || isMaster;
            final visibleFx = fxNames.take(fxRows).toList();
            final hiddenFx = fxNames.length - visibleFx.length;

            return MouseRegion(
              cursor: SystemMouseCursors.basic,
              child: Container(
                width: stripWidth,
                margin: const EdgeInsets.symmetric(vertical: 6),
                decoration: BoxDecoration(
                  color:
                      isMaster
                          ? blend(AppColors.surface, AppColors.warning, 0.035)
                          : AppColors.surface,
                  borderRadius: BorderRadius.circular(10),
                  border: Border.all(
                    color:
                        isMaster
                            ? AppColors.warning.withValues(alpha: 0.38)
                            : AppColors.border,
                  ),
                ),
                clipBehavior: Clip.antiAlias,
                child: Column(
                  children: [
                    Container(
                      height: 3,
                      color:
                          isMaster
                              ? AppColors.warning
                              : color.withValues(alpha: 0.92),
                    ),
                    _header(context, tight, showSubtitle),
                    const Divider(height: 1, color: AppColors.border),
                    _panArea(context, tight),
                    Expanded(
                      child: Padding(
                        padding: const EdgeInsets.fromLTRB(7, 2, 7, 0),
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          children: [
                            Expanded(
                              child: Center(
                                child: _GainFader(
                                  value: effectiveVolume,
                                  maxGain: isMaster ? 4 : 2,
                                  label: name,
                                  onChanged: onVolume,
                                ),
                              ),
                            ),
                            const SizedBox(width: 4),
                            _meters(),
                          ],
                        ),
                      ),
                    ),
                    const SizedBox(height: 1),
                    Text(
                      formatDb(volume, minusInf: context.l10n.mixerMinusInf),
                      style: const TextStyle(
                        fontSize: 8.5,
                        fontWeight: FontWeight.w600,
                        color: AppColors.textDim,
                        fontFeatures: [FontFeature.tabularFigures()],
                      ),
                    ),
                    const SizedBox(height: 3),
                    _toggles(context),
                    _fxBlock(context, visibleFx, hiddenFx),
                    const SizedBox(height: 5),
                    _addFx(context),
                    const SizedBox(height: 7),
                  ],
                ),
              ),
            );
          },
        );
      },
    );
  }

  Widget _header(BuildContext context, bool tight, bool showSubtitle) {
    return MouseRegion(
      cursor:
          onOpenChannel == null ? MouseCursor.defer : SystemMouseCursors.click,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onOpenChannel,
        onLongPress: onOpenChannel,
        child: Padding(
          padding: EdgeInsets.fromLTRB(7, tight ? 5 : 7, 3, tight ? 4 : 5),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  _TrackDot(color: isMaster ? AppColors.warning : color),
                  const SizedBox(width: 5),
                  Text(
                    (index + 1).toString().padLeft(2, '0'),
                    style: const TextStyle(
                      fontSize: 8,
                      fontWeight: FontWeight.w700,
                      color: AppColors.textFaint,
                      fontFeatures: [FontFeature.tabularFigures()],
                    ),
                  ),
                  const SizedBox(width: 4),
                  Expanded(
                    child: Text(
                      name,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w600,
                        color: AppColors.text,
                      ),
                    ),
                  ),
                  if (!isMaster)
                    InkWell(
                      onTap: onOpenChannel,
                      borderRadius: BorderRadius.circular(4),
                      child: const Padding(
                        padding: EdgeInsets.all(2),
                        child: Icon(
                          Icons.more_horiz_rounded,
                          size: 13,
                          color: AppColors.textFaint,
                        ),
                      ),
                    ),
                ],
              ),
              if (showSubtitle) ...[
                const SizedBox(height: 2),
                Row(
                  children: [
                    Icon(
                      isMaster
                          ? Icons.surround_sound_rounded
                          : (subtitle.startsWith('IN ')
                              ? Icons.mic_none_rounded
                              : Icons.piano_rounded),
                      size: 9,
                      color: isMaster ? AppColors.warning : AppColors.textFaint,
                    ),
                    const SizedBox(width: 3),
                    Expanded(
                      child: Text(
                        subtitle,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                          fontSize: 8,
                          color: AppColors.textFaint,
                        ),
                      ),
                    ),
                    if (automationActive)
                      const Padding(
                        padding: EdgeInsets.only(left: 2),
                        child: Icon(
                          Icons.multiline_chart_rounded,
                          size: 10,
                          color: AppColors.accent,
                        ),
                      ),
                  ],
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }

  Widget _panArea(BuildContext context, bool tight) {
    if (isMaster) {
      return Padding(
        padding: EdgeInsets.symmetric(vertical: tight ? 3 : 4),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Container(
              width: tight ? 26 : 30,
              height: tight ? 26 : 30,
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: AppColors.surfaceAlt,
                border: Border.all(color: AppColors.borderStrong),
              ),
              child: Icon(
                Icons.surround_sound_rounded,
                size: tight ? 12 : 14,
                color: AppColors.warning,
              ),
            ),
            const SizedBox(width: 6),
            const Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  'STEREO',
                  style: TextStyle(
                    fontSize: 7,
                    fontWeight: FontWeight.w700,
                    letterSpacing: 0.7,
                    color: AppColors.textFaint,
                  ),
                ),
                Text(
                  'L  R',
                  style: TextStyle(
                    fontSize: 8,
                    fontWeight: FontWeight.w600,
                    letterSpacing: 1.6,
                    color: AppColors.textDim,
                  ),
                ),
              ],
            ),
          ],
        ),
      );
    }

    return Padding(
      padding: EdgeInsets.symmetric(vertical: tight ? 2 : 3),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          _PanKnob(
            value: pan,
            size: tight ? 26 : 30,
            onChanged: onPan ?? (_) {},
          ),
          const SizedBox(width: 5),
          Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            children: [
              const Text(
                'PAN',
                style: TextStyle(
                  fontSize: 7,
                  fontWeight: FontWeight.w700,
                  letterSpacing: 0.7,
                  color: AppColors.textFaint,
                ),
              ),
              Text(
                formatPan(pan),
                style: const TextStyle(
                  fontSize: 8,
                  fontWeight: FontWeight.w600,
                  color: AppColors.textDim,
                  fontFeatures: [FontFeature.tabularFigures()],
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _meters() {
    if (isMaster) {
      return Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          SizedBox(
            width: 8,
            child: _PeakMeter(readPeak: readPeakL ?? () => 0.0),
          ),
          const SizedBox(width: 2),
          SizedBox(
            width: 8,
            child: _PeakMeter(readPeak: readPeakR ?? () => 0.0),
          ),
        ],
      );
    }
    return SizedBox(
      width: 8,
      child: _PeakMeter(readPeak: readPeak ?? () => 0.0),
    );
  }

  Widget _toggles(BuildContext context) {
    if (isMaster) {
      return Container(
        height: 19,
        margin: const EdgeInsets.symmetric(horizontal: 7),
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: AppColors.surfaceAlt,
          borderRadius: BorderRadius.circular(5),
          border: Border.all(color: AppColors.border),
        ),
        child: const Text(
          'STEREO OUT',
          style: TextStyle(
            fontSize: 7.5,
            fontWeight: FontWeight.w700,
            letterSpacing: 1,
            color: AppColors.textFaint,
          ),
        ),
      );
    }
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 7),
      child: Row(
        children: [
          Expanded(
            child: _ToggleTag(
              label: 'M',
              active: mute,
              activeColor: AppColors.warning,
              tooltip: context.l10n.timelineMute,
              onTap: onMute,
            ),
          ),
          const SizedBox(width: 3),
          Expanded(
            child: _ToggleTag(
              label: 'S',
              active: solo,
              activeColor: AppColors.accent,
              tooltip: context.l10n.timelineSolo,
              onTap: onSolo,
            ),
          ),
          const SizedBox(width: 3),
          Expanded(
            child: _ToggleTag(
              label: 'R',
              active: armed,
              activeColor: AppColors.record,
              tooltip: context.l10n.transportRecord,
              onTap: onArm,
            ),
          ),
        ],
      ),
    );
  }

  Widget _fxBlock(BuildContext context, List<String> visibleFx, int hiddenFx) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(7, 4, 7, 0),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          for (var i = 0; i < visibleFx.length; i++)
            _FxRow(
              name: visibleFx[i],
              bypassed: i < fxBypass.length && fxBypass[i],
              onTap:
                  onToggleFxBypass == null ? null : () => onToggleFxBypass!(i),
              onDoubleTap: onOpenEditor == null ? null : () => onOpenEditor!(i),
              onRemove: onRemoveFx == null ? null : () => onRemoveFx!(i),
            ),
          if (hiddenFx > 0)
            Padding(
              padding: const EdgeInsets.only(top: 2),
              child: Text(
                '+$hiddenFx FX',
                style: const TextStyle(
                  fontSize: 7.5,
                  fontWeight: FontWeight.w600,
                  color: AppColors.textFaint,
                ),
              ),
            ),
        ],
      ),
    );
  }

  Widget _addFx(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 7),
      child: MouseRegion(
        cursor: SystemMouseCursors.click,
        child: GestureDetector(
          onTap: onAddFx == null ? null : () => onAddFx!(),
          child: Container(
            height: 19,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: AppColors.accentSoft.withValues(alpha: 0.7),
              borderRadius: BorderRadius.circular(5),
              border: Border.all(
                color: AppColors.accent.withValues(alpha: 0.4),
              ),
            ),
            child: const Text(
              '+ FX',
              style: TextStyle(
                fontSize: 8,
                fontWeight: FontWeight.w700,
                color: AppColors.accent,
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _TrackDot extends StatelessWidget {
  const _TrackDot({required this.color});

  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 6,
      height: 6,
      decoration: BoxDecoration(
        color: color,
        shape: BoxShape.circle,
        boxShadow: [
          BoxShadow(
            color: color.withValues(alpha: 0.45),
            blurRadius: 4,
            spreadRadius: -1,
          ),
        ],
      ),
    );
  }
}

class _FxRow extends StatelessWidget {
  const _FxRow({
    required this.name,
    required this.bypassed,
    this.onTap,
    this.onDoubleTap,
    this.onRemove,
  });

  final String name;
  final bool bypassed;
  final VoidCallback? onTap;
  final VoidCallback? onDoubleTap;
  final VoidCallback? onRemove;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: onTap,
        onDoubleTap: onDoubleTap,
        child: Container(
          height: 15,
          margin: const EdgeInsets.only(bottom: 2),
          padding: const EdgeInsets.only(left: 4, right: 3),
          decoration: BoxDecoration(
            color: AppColors.surfaceAlt,
            borderRadius: BorderRadius.circular(4),
            border: Border.all(color: AppColors.border),
          ),
          child: Row(
            children: [
              Container(
                width: 4,
                height: 4,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: bypassed ? AppColors.textFaint : AppColors.accent,
                ),
              ),
              const SizedBox(width: 4),
              Expanded(
                child: Text(
                  name,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: 8,
                    color: bypassed ? AppColors.textFaint : AppColors.textDim,
                    decoration: bypassed ? TextDecoration.lineThrough : null,
                    decorationColor: AppColors.danger,
                  ),
                ),
              ),
              GestureDetector(
                onTap: onRemove,
                child: const Padding(
                  padding: EdgeInsets.symmetric(horizontal: 2),
                  child: Icon(
                    Icons.close_rounded,
                    size: 9,
                    color: AppColors.textFaint,
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _ToggleTag extends StatelessWidget {
  const _ToggleTag({
    required this.label,
    required this.active,
    required this.activeColor,
    required this.tooltip,
    required this.onTap,
  });

  final String label;
  final bool active;
  final Color activeColor;
  final String tooltip;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: tooltip,
      child: MouseRegion(
        cursor: SystemMouseCursors.click,
        child: GestureDetector(
          onTap: onTap,
          child: Container(
            height: 19,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color:
                  active
                      ? activeColor.withValues(alpha: 0.18)
                      : AppColors.surfaceAlt,
              borderRadius: BorderRadius.circular(5),
              border: Border.all(
                color:
                    active
                        ? activeColor.withValues(alpha: 0.7)
                        : AppColors.border,
              ),
            ),
            child: Text(
              label,
              style: TextStyle(
                fontSize: 8.5,
                fontWeight: FontWeight.w700,
                color: active ? activeColor : AppColors.textFaint,
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// Vertical console fader with a decibel law, unity marker and dB ticks.
class _GainFader extends StatefulWidget {
  const _GainFader({
    required this.value,
    required this.onChanged,
    this.maxGain = 2,
    this.label,
  });

  final double value;
  final ValueChanged<double> onChanged;
  final double maxGain;
  final String? label;

  @override
  State<_GainFader> createState() => _GainFaderState();
}

class _GainFaderState extends State<_GainFader> {
  bool _dragging = false;
  double _dragNorm = 0;

  double get _maxDb => widget.maxGain > 2.5 ? 12 : 6;

  double get _norm =>
      _dragging ? _dragNorm : gainToFader(widget.value, maxDb: _maxDb);

  void _start(DragStartDetails details) {
    setState(() {
      _dragging = true;
      _dragNorm = gainToFader(widget.value, maxDb: _maxDb);
    });
  }

  void _update(DragUpdateDetails details) {
    final height = context.size?.height ?? 1;
    final next =
        (_dragNorm - details.delta.dy / math.max(1, height))
            .clamp(0.0, 1.0)
            .toDouble();
    setState(() => _dragNorm = next);
    widget.onChanged(faderToGain(next, maxDb: _maxDb));
  }

  void _end(DragEndDetails details) {
    setState(() => _dragging = false);
  }

  void _cancel() {
    setState(() => _dragging = false);
  }

  void _reset() {
    widget.onChanged(1);
  }

  @override
  Widget build(BuildContext context) {
    return Semantics(
      slider: true,
      label: widget.label,
      value: formatDb(widget.value),
      child: Tooltip(
        message: formatDb(widget.value),
        waitDuration: const Duration(milliseconds: 500),
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onVerticalDragStart: _start,
          onVerticalDragUpdate: _update,
          onVerticalDragEnd: _end,
          onVerticalDragCancel: _cancel,
          onDoubleTap: _reset,
          child: MouseRegion(
            cursor: SystemMouseCursors.resizeUpDown,
            child: SizedBox(
              width: 46,
              child: CustomPaint(
                painter: _FaderPainter(
                  norm: _norm,
                  maxDb: _maxDb,
                  dragging: _dragging,
                ),
                size: Size.infinite,
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _FaderPainter extends CustomPainter {
  _FaderPainter({
    required this.norm,
    required this.maxDb,
    required this.dragging,
  });

  final double norm;
  final double maxDb;
  final bool dragging;

  static const _ticks = <double>[-60, -48, -36, -24, -12, -6, 0];

  @override
  void paint(Canvas canvas, Size size) {
    final centerX = size.width / 2;
    const thumbHeight = 15.0;
    const top = 7.0;
    final bottom = size.height - 7;

    final groove = Rect.fromLTWH(
      centerX - 3,
      top,
      6,
      math.max(1, bottom - top),
    );
    canvas.drawRRect(
      RRect.fromRectAndRadius(groove, const Radius.circular(3)),
      Paint()..color = const Color(0xFF090B0E),
    );
    canvas.drawRRect(
      RRect.fromRectAndRadius(groove, const Radius.circular(3)),
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = AppColors.border,
    );

    final thumbCenter = bottom - (bottom - top) * norm;
    final fillRect = Rect.fromLTRB(
      centerX - 2,
      thumbCenter,
      centerX + 2,
      bottom,
    );
    if (fillRect.height > 0) {
      canvas.drawRRect(
        RRect.fromRectAndRadius(fillRect, const Radius.circular(2)),
        Paint()
          ..shader = const LinearGradient(
            begin: Alignment.bottomCenter,
            end: Alignment.topCenter,
            colors: [Color(0xFF2D6DB5), AppColors.accent],
          ).createShader(fillRect),
      );
    }

    final tickPaint =
        Paint()
          ..color = AppColors.textFaint.withValues(alpha: 0.45)
          ..strokeWidth = 1;
    for (final db in _ticks) {
      final y =
          bottom - (bottom - top) * gainToFader(_dbToGain(db), maxDb: maxDb);
      final long = db == 0 || db == -60;
      canvas.drawLine(
        Offset(centerX - 13, y),
        Offset(centerX - 13 + (long ? 4 : 2), y),
        tickPaint,
      );
      canvas.drawLine(
        Offset(centerX + 9, y),
        Offset(centerX + 9 + (long ? 4 : 2), y),
        tickPaint,
      );
      if (long) {
        final label = TextPainter(
          text: TextSpan(
            text: db == 0 ? '0' : '-60',
            style: const TextStyle(
              color: AppColors.textFaint,
              fontSize: 6.5,
              fontWeight: FontWeight.w600,
            ),
          ),
          textDirection: TextDirection.ltr,
        )..layout();
        label.paint(canvas, Offset(centerX + 14, y - label.height / 2));
      }
    }

    final unityY = bottom - (bottom - top) * gainToFader(1, maxDb: maxDb);
    canvas.drawLine(
      Offset(centerX - 19, unityY),
      Offset(centerX + 19, unityY),
      Paint()
        ..color = AppColors.textDim.withValues(alpha: 0.3)
        ..strokeWidth = 1,
    );

    final thumbRect = RRect.fromRectAndRadius(
      Rect.fromCenter(
        center: Offset(centerX, thumbCenter),
        width: math.max(18, size.width - 8),
        height: thumbHeight,
      ),
      const Radius.circular(4),
    );
    canvas.drawRRect(
      thumbRect.shift(const Offset(0, 2)),
      Paint()..color = Colors.black.withValues(alpha: 0.45),
    );
    canvas.drawRRect(
      thumbRect,
      Paint()
        ..shader = const LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [Color(0xFF343A45), Color(0xFF21262E)],
        ).createShader(thumbRect.outerRect),
    );
    canvas.drawRRect(
      thumbRect,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = dragging ? AppColors.accent : AppColors.borderStrong,
    );
    canvas.drawLine(
      Offset(centerX - 8, thumbCenter),
      Offset(centerX + 8, thumbCenter),
      Paint()
        ..color = dragging ? AppColors.accent : AppColors.textDim
        ..strokeWidth = 2
        ..strokeCap = StrokeCap.round,
    );
  }

  double _dbToGain(double db) => math.pow(10, db / 20).toDouble();

  @override
  bool shouldRepaint(covariant _FaderPainter oldDelegate) {
    return oldDelegate.norm != norm ||
        oldDelegate.maxDb != maxDb ||
        oldDelegate.dragging != dragging;
  }
}

/// Compact pan knob. Drag vertically, double-tap to centre.
class _PanKnob extends StatefulWidget {
  const _PanKnob({
    required this.value,
    required this.onChanged,
    this.size = 30,
  });

  final double value;
  final ValueChanged<double> onChanged;
  final double size;

  @override
  State<_PanKnob> createState() => _PanKnobState();
}

class _PanKnobState extends State<_PanKnob> {
  bool _dragging = false;
  double _dragValue = 0;

  double get _value => _dragging ? _dragValue : widget.value.clamp(-1.0, 1.0);

  void _start(DragStartDetails details) {
    setState(() {
      _dragging = true;
      _dragValue = widget.value.clamp(-1.0, 1.0);
    });
  }

  void _update(DragUpdateDetails details) {
    final next = (_dragValue - details.delta.dy / 110).clamp(-1.0, 1.0);
    setState(() => _dragValue = next);
    widget.onChanged(next);
  }

  void _end(DragEndDetails details) {
    setState(() => _dragging = false);
  }

  void _cancel() {
    setState(() => _dragging = false);
  }

  void _center() {
    widget.onChanged(0);
  }

  @override
  Widget build(BuildContext context) {
    return Semantics(
      slider: true,
      label: 'Pan',
      value: formatPan(_value),
      child: MouseRegion(
        cursor: SystemMouseCursors.resizeUpDown,
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onVerticalDragStart: _start,
          onVerticalDragUpdate: _update,
          onVerticalDragEnd: _end,
          onVerticalDragCancel: _cancel,
          onDoubleTap: _center,
          child: SizedBox(
            width: widget.size,
            height: widget.size,
            child: CustomPaint(
              painter: _PanPainter(value: _value, active: _dragging),
            ),
          ),
        ),
      ),
    );
  }
}

class _PanPainter extends CustomPainter {
  _PanPainter({required this.value, required this.active});

  final double value;
  final bool active;

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final radius = math.min(size.width, size.height) / 2 - 2;
    final rect = Rect.fromCircle(center: center, radius: radius);
    const start = math.pi * 0.75;
    const sweep = math.pi * 1.5;

    canvas.drawArc(
      rect,
      start,
      sweep,
      false,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2.5
        ..strokeCap = StrokeCap.round
        ..color = AppColors.surfaceHigh,
    );

    final norm = (value + 1) / 2;
    const centerNorm = 0.5;
    final from = norm < centerNorm ? norm : centerNorm;
    final to = norm > centerNorm ? norm : centerNorm;
    if ((to - from).abs() > 0.001) {
      canvas.drawArc(
        rect,
        start + sweep * from,
        sweep * (to - from),
        false,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2.5
          ..strokeCap = StrokeCap.round
          ..color = AppColors.accent.withValues(alpha: 0.85),
      );
    }

    canvas.drawCircle(
      center,
      radius - 6,
      Paint()
        ..shader = const LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [Color(0xFF303641), Color(0xFF1A1E25)],
        ).createShader(Rect.fromCircle(center: center, radius: radius)),
    );
    canvas.drawCircle(
      center,
      radius - 6,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = active ? AppColors.accent : AppColors.borderStrong,
    );

    final angle = start + sweep * norm;
    canvas.drawLine(
      Offset(
        center.dx + math.cos(angle) * (radius - 11),
        center.dy + math.sin(angle) * (radius - 11),
      ),
      Offset(
        center.dx + math.cos(angle) * (radius - 4),
        center.dy + math.sin(angle) * (radius - 4),
      ),
      Paint()
        ..color = active ? AppColors.accent : AppColors.text
        ..strokeWidth = 2
        ..strokeCap = StrokeCap.round,
    );
    canvas.drawCircle(
      Offset(center.dx, center.dy - radius + 1.5),
      1.0,
      Paint()..color = AppColors.textFaint,
    );
  }

  @override
  bool shouldRepaint(covariant _PanPainter oldDelegate) {
    return oldDelegate.value != value || oldDelegate.active != active;
  }
}

/// Peak meter with a green→amber→red gradient, peak hold and clip LED.
class _PeakMeter extends StatefulWidget {
  const _PeakMeter({required this.readPeak});

  final double Function() readPeak;

  @override
  State<_PeakMeter> createState() => _PeakMeterState();
}

class _PeakMeterState extends State<_PeakMeter> {
  double _hold = 0;
  int _holdAge = 0;

  @override
  Widget build(BuildContext context) {
    // Read-and-reset: this build IS the poll (driven by frameTick upstream).
    final peak = widget.readPeak().clamp(0.0, 1.0).toDouble();
    if (peak >= _hold) {
      _hold = peak;
      _holdAge = 0;
    } else {
      _holdAge++;
      // ~1.5 s hold at 30 fps, then a slow fall.
      if (_holdAge > 45) {
        _hold = math.max(peak, _hold - 0.012);
      }
    }
    return RepaintBoundary(
      child: CustomPaint(
        painter: _MeterPainter(peak: peak, hold: _hold),
        size: Size.infinite,
      ),
    );
  }
}

class _MeterPainter extends CustomPainter {
  _MeterPainter({required this.peak, required this.hold});

  final double peak;
  final double hold;

  @override
  void paint(Canvas canvas, Size size) {
    final outer = RRect.fromRectAndRadius(
      Offset.zero & size,
      const Radius.circular(3),
    );
    canvas.drawRRect(outer, Paint()..color = const Color(0xFF08090C));
    canvas.drawRRect(
      outer.deflate(0.5),
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = AppColors.border,
    );

    final inner = Rect.fromLTWH(
      2,
      2,
      math.max(1, size.width - 4),
      math.max(1, size.height - 4),
    );
    final radius = RRect.fromRectAndRadius(inner, const Radius.circular(1.5));
    canvas.save();
    canvas.clipRRect(radius);

    final level = meterToFader(peak);
    final fillHeight = inner.height * level;
    if (fillHeight > 0.5) {
      final fillRect = Rect.fromLTWH(
        inner.left,
        inner.bottom - fillHeight,
        inner.width,
        fillHeight,
      );
      canvas.drawRect(
        fillRect,
        Paint()
          ..shader = const LinearGradient(
            begin: Alignment.bottomCenter,
            end: Alignment.topCenter,
            colors: [
              Color(0xFF2FBF71),
              Color(0xFF57C84D),
              Color(0xFFE7C93D),
              Color(0xFFF08A2A),
              Color(0xFFFF453A),
            ],
            stops: [0, 0.48, 0.72, 0.87, 1],
          ).createShader(inner),
      );
    }

    final segment =
        Paint()
          ..color = const Color(0x55000000)
          ..strokeWidth = 0.7;
    for (var y = inner.top + 3; y < inner.bottom; y += 5) {
      canvas.drawLine(Offset(inner.left, y), Offset(inner.right, y), segment);
    }

    if (hold > 0.005) {
      final y = inner.bottom - inner.height * meterToFader(hold);
      canvas.drawRect(
        Rect.fromLTWH(inner.left, y, inner.width, 1.4),
        Paint()..color = Colors.white.withValues(alpha: 0.7),
      );
    }

    if (peak >= 0.995) {
      canvas.drawRect(
        Rect.fromLTWH(inner.left, inner.top, inner.width, 2.5),
        Paint()..color = AppColors.record,
      );
    }
    canvas.restore();
  }

  @override
  bool shouldRepaint(covariant _MeterPainter oldDelegate) {
    return oldDelegate.peak != peak || oldDelegate.hold != hold;
  }
}
