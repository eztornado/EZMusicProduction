// Controller-adapted performance panel: eight endless encoders, four faders,
// MIDI learn, relative encoders and soft takeover. Designed for the Arturia
// MiniLab 3 while remaining useful with any generic MIDI controller.

import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../controller/controller_service.dart';
import '../engine/engine_service.dart';
import '../engine/project_snapshot.dart';
import '../l10n/l10n.dart';
import 'design.dart';

Future<void> showPerformancePanel(
  BuildContext context,
  EngineService service,
  ControllerService controller,
) {
  return showDialog<void>(
    context: context,
    barrierDismissible: false,
    builder: (_) => _PerformancePanel(service: service, controller: controller),
  );
}

class _PerformancePanel extends StatefulWidget {
  const _PerformancePanel({required this.service, required this.controller});

  final EngineService service;
  final ControllerService controller;

  @override
  State<_PerformancePanel> createState() => _PerformancePanelState();
}

class _PerformancePanelState extends State<_PerformancePanel> {
  @override
  void initState() {
    super.initState();
    widget.controller.refreshValues();
  }

  @override
  Widget build(BuildContext context) {
    final service = widget.service;
    final controller = widget.controller;
    return AnimatedBuilder(
      animation: Listenable.merge([service, controller]),
      builder: (context, _) {
        final l10n = context.l10n;
        final track = controller.activeTrack;
        final devices = controller.availableDevices;
        final device = controller.currentDevice;
        final paramCount = controller.currentParamCount;
        final learning = controller.learnTarget;
        final learningAction = controller.learnAction;
        final screen = MediaQuery.sizeOf(context);

        return Dialog(
          backgroundColor: Colors.transparent,
          insetPadding: const EdgeInsets.all(22),
          child: Container(
            width: math.min(1050, screen.width - 44),
            height: math.min(688, screen.height - 44),
            decoration: BoxDecoration(
              color: AppColors.bgPanel,
              borderRadius: BorderRadius.circular(16),
              border: Border.all(color: AppColors.borderStrong),
              boxShadow: const [
                BoxShadow(
                  color: Color(0x66000000),
                  blurRadius: 30,
                  offset: Offset(0, 14),
                ),
              ],
            ),
            child: Column(
              children: [
                _header(context),
                _topBar(context, track, devices, device, paramCount),
                if (learning != null)
                  _learnBanner(
                    context,
                    controller,
                    label: _controlLabel(l10n, learning),
                    hint: l10n.performanceLearning,
                  )
                else if (learningAction != null)
                  _learnBanner(
                    context,
                    controller,
                    label: _actionLabel(l10n, learningAction.action),
                    hint: l10n.performanceLearningAction,
                  ),
                Expanded(
                  child: track == null
                      ? _empty(context, l10n.performanceNoTrack)
                      : SingleChildScrollView(
                          padding: const EdgeInsets.fromLTRB(16, 12, 16, 8),
                          child: Column(
                            children: [
                              _actionsSection(context, controller),
                              const SizedBox(height: 12),
                              GridView.builder(
                                shrinkWrap: true,
                                physics: const NeverScrollableScrollPhysics(),
                                itemCount: controller.encoders.length,
                                gridDelegate:
                                    const SliverGridDelegateWithFixedCrossAxisCount(
                                  crossAxisCount: 4,
                                  mainAxisExtent: 142,
                                  crossAxisSpacing: 10,
                                  mainAxisSpacing: 10,
                                ),
                                itemBuilder: (context, index) =>
                                    _EncoderCard(
                                  controller: controller,
                                  control: controller.encoders[index],
                                  parameter: controller.parameterName(
                                      controller.encoders[index]),
                                ),
                              ),
                              const SizedBox(height: 10),
                              SizedBox(
                                height: 190,
                                child: Row(
                                  children: [
                                    for (var i = 0;
                                        i < controller.faders.length;
                                        i++) ...[
                                      if (i > 0) const SizedBox(width: 10),
                                      Expanded(
                                        child: _FaderCard(
                                          controller: controller,
                                          control: controller.faders[i],
                                          parameter: controller.parameterName(
                                              controller.faders[i]),
                                        ),
                                      ),
                                    ],
                                  ],
                                ),
                              ),
                            ],
                          ),
                        ),
                ),
                _footer(context, controller),
              ],
            ),
          ),
        );
      },
    );
  }

  Widget _header(BuildContext context) {
    final l10n = context.l10n;
    final controller = widget.controller;
    return Container(
      padding: const EdgeInsets.fromLTRB(18, 14, 12, 12),
      decoration: const BoxDecoration(
        border: Border(bottom: BorderSide(color: AppColors.border)),
      ),
      child: Row(
        children: [
          Container(
            width: 38,
            height: 38,
            decoration: BoxDecoration(
              color: AppColors.accentSoft,
              borderRadius: BorderRadius.circular(10),
              border: Border.all(
                color: AppColors.accent.withValues(alpha: 0.45),
              ),
            ),
            child: const Icon(
              Icons.graphic_eq_rounded,
              color: AppColors.accent,
              size: 21,
            ),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  l10n.performanceTitle,
                  style: const TextStyle(
                    color: AppColors.text,
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    letterSpacing: 0.2,
                  ),
                ),
                const SizedBox(height: 2),
                Text(
                  l10n.performanceSubtitle,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    color: AppColors.textDim,
                    fontSize: 10.5,
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(width: 10),
          _InputSelector(
            service: widget.service,
            controller: controller,
          ),
          const SizedBox(width: 6),
          IconButton(
            tooltip: l10n.performanceClose,
            onPressed: () => Navigator.of(context).pop(),
            icon: const Icon(Icons.close_rounded, size: 18),
            color: AppColors.textDim,
          ),
        ],
      ),
    );
  }

  Widget _actionsSection(BuildContext context, ControllerService controller) {
    final l10n = context.l10n;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Text(
              l10n.performanceActions,
              style: const TextStyle(
                color: AppColors.text,
                fontSize: 11,
                fontWeight: FontWeight.w700,
                letterSpacing: 0.3,
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                l10n.performanceActionHint,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  color: AppColors.textFaint,
                  fontSize: 9.5,
                ),
              ),
            ),
          ],
        ),
        const SizedBox(height: 7),
        GridView.builder(
          shrinkWrap: true,
          physics: const NeverScrollableScrollPhysics(),
          itemCount: controller.actionBindings.length,
          gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
            crossAxisCount: 4,
            mainAxisExtent: 64,
            crossAxisSpacing: 8,
            mainAxisSpacing: 8,
          ),
          itemBuilder: (context, index) => _ActionCard(
            controller: controller,
            binding: controller.actionBindings[index],
          ),
        ),
      ],
    );
  }

  Widget _topBar(
    BuildContext context,
    TrackInfo? track,
    List<ControllerDevice> devices,
    ControllerDevice? device,
    int paramCount,
  ) {
    final l10n = context.l10n;
    final service = widget.service;
    final controller = widget.controller;
    final tracks = service.snapshot.tracks;
    final currentTrackId = track?.id;
    final lastCc = controller.lastControllerEvent;
    final compact = MediaQuery.sizeOf(context).width < 980;
    final gap = compact ? 7.0 : 12.0;

    return Container(
      padding: EdgeInsets.fromLTRB(compact ? 12 : 16, 10, compact ? 12 : 16, 10),
      color: AppColors.bgDeep.withValues(alpha: 0.35),
      child: Row(
        children: [
          SizedBox(
            width: compact ? 125 : 165,
            child: _Field(
              label: l10n.performanceTrack,
              child: tracks.isEmpty
                  ? Text(
                      l10n.performanceNoTrack,
                      style: const TextStyle(
                        color: AppColors.textFaint,
                        fontSize: 11,
                      ),
                    )
                  : DropdownButton<int>(
                      value: currentTrackId,
                      isExpanded: true,
                      underline: const SizedBox.shrink(),
                      style: const TextStyle(
                        color: AppColors.text,
                        fontSize: 11.5,
                        fontWeight: FontWeight.w600,
                      ),
                      dropdownColor: AppColors.surfaceHigh,
                      borderRadius: BorderRadius.circular(8),
                      items: [
                        for (final item in tracks)
                          DropdownMenuItem<int>(
                            value: item.id,
                            child: Text(
                              item.name.isEmpty
                                  ? '${l10n.performanceTrack} ${item.id}'
                                  : item.name,
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                      ],
                      onChanged: (id) {
                        if (id == null) return;
                        service.selectTrack(id);
                        controller.refreshValues();
                      },
                    ),
            ),
          ),
          SizedBox(width: gap),
          Expanded(
            child: _Field(
              label: l10n.performanceDevice,
              child: devices.isEmpty
                  ? Text(
                      l10n.performanceNoDevice,
                      style: const TextStyle(
                        color: AppColors.textFaint,
                        fontSize: 11,
                      ),
                    )
                  : DropdownButton<String>(
                      value: device?.key,
                      isExpanded: true,
                      underline: const SizedBox.shrink(),
                      style: const TextStyle(
                        color: AppColors.text,
                        fontSize: 11.5,
                        fontWeight: FontWeight.w600,
                      ),
                      dropdownColor: AppColors.surfaceHigh,
                      borderRadius: BorderRadius.circular(8),
                      items: [
                        for (final item in devices)
                          DropdownMenuItem<String>(
                            value: item.key,
                            child: Text(
                              compact
                                  ? item.label
                                  : '${item.label}  ·  ${service.pluginParamCount(track?.id ?? 0, item.slotKind, item.slotIndex)} params',
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                      ],
                      onChanged: (key) {
                        for (final item in devices) {
                          if (item.key == key) {
                            controller.setDevice(item);
                            break;
                          }
                        }
                      },
                    ),
            ),
          ),
          SizedBox(width: gap),
          _ToggleChip(
            icon: Icons.swap_vert_rounded,
            label: l10n.performanceSoftTakeover,
            active: controller.softTakeover,
            compact: compact,
            onTap: () =>
                controller.setSoftTakeover(!controller.softTakeover),
          ),
          const SizedBox(width: 7),
          _ToggleChip(
            icon: Icons.sync_rounded,
            label: l10n.performanceRelative,
            active: controller.relative,
            compact: compact,
            onTap: () => controller.setRelative(!controller.relative),
          ),
          SizedBox(width: gap),
          _StatusChip(
            icon: Icons.sensors_rounded,
            label: lastCc == null
                ? '${l10n.performanceMidiActivity} —'
                : compact
                    ? 'CC ${lastCc.data1}'
                    : 'CC ${lastCc.data1} · ${lastCc.data2}',
            active: lastCc != null,
            accent: lastCc != null,
          ),
          if (!compact && paramCount > 0) ...[
            const SizedBox(width: 8),
            Text(
              l10n.performanceParamCount(paramCount),
              style: const TextStyle(
                color: AppColors.textFaint,
                fontSize: 10,
              ),
            ),
          ],
        ],
      ),
    );
  }

  Widget _learnBanner(
    BuildContext context,
    ControllerService controller, {
    required String label,
    required String hint,
  }) {
    final l10n = context.l10n;
    return Container(
      margin: const EdgeInsets.fromLTRB(16, 8, 16, 0),
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: AppColors.accentSoft,
        borderRadius: BorderRadius.circular(9),
        border: Border.all(color: AppColors.accent.withValues(alpha: 0.5)),
      ),
      child: Row(
        children: [
          const Icon(Icons.ads_click_rounded,
              size: 16, color: AppColors.accent),
          const SizedBox(width: 8),
          Text(
            '$hint $label',
            style: const TextStyle(
              color: AppColors.text,
              fontSize: 11.5,
              fontWeight: FontWeight.w600,
            ),
          ),
          const Spacer(),
          TextButton(
            onPressed: controller.cancelLearn,
            child: Text(l10n.performanceCancelLearn),
          ),
        ],
      ),
    );
  }

  Widget _empty(BuildContext context, String text) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          const Icon(Icons.piano_off_rounded,
              size: 40, color: AppColors.textFaint),
          const SizedBox(height: 10),
          Text(
            text,
            style: const TextStyle(color: AppColors.textDim, fontSize: 12),
          ),
        ],
      ),
    );
  }

  Widget _footer(BuildContext context, ControllerService controller) {
    final l10n = context.l10n;
    return Container(
      padding: const EdgeInsets.fromLTRB(18, 9, 18, 11),
      decoration: const BoxDecoration(
        border: Border(top: BorderSide(color: AppColors.border)),
      ),
      child: Row(
        children: [
          const Icon(Icons.info_outline_rounded,
              size: 14, color: AppColors.textFaint),
          const SizedBox(width: 7),
          Expanded(
            child: Text(
              '${l10n.performanceHintCatch}  ${l10n.performanceHintLearn}',
              style: const TextStyle(
                color: AppColors.textFaint,
                fontSize: 9.5,
              ),
            ),
          ),
          if (controller.learnTarget != null)
            Text(
              l10n.performanceLearning,
              style: const TextStyle(
                color: AppColors.accent,
                fontSize: 10,
                fontWeight: FontWeight.w600,
              ),
            ),
        ],
      ),
    );
  }

  String _controlLabel(AppLocalizations l10n, ControllerControl control) {
    final prefix = control.kind == ControllerControlKind.encoder
        ? l10n.performanceEncoder
        : l10n.performanceFader;
    return '$prefix ${control.index + 1}';
  }
}

class _EncoderCard extends StatelessWidget {
  const _EncoderCard({
    required this.controller,
    required this.control,
    required this.parameter,
  });

  final ControllerService controller;
  final ControllerControl control;
  final String? parameter;

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    final learning = controller.learnTarget?.id == control.id;
    final enabled = parameter != null;

    return _DeviceCard(
      learning: learning,
      child: Column(
        children: [
          Row(
            children: [
              Expanded(
                child: Text(
                  '${l10n.performanceEncoder} ${control.index + 1}',
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    color: AppColors.textDim,
                    fontSize: 9.5,
                    fontWeight: FontWeight.w700,
                    letterSpacing: 1.1,
                  ),
                ),
              ),
              _CcChip(cc: control.cc),
              const SizedBox(width: 2),
              _LearnButton(
                active: learning,
                enabled: enabled,
                onTap: () => controller.startLearn(control),
              ),
            ],
          ),
          const SizedBox(height: 2),
          Expanded(
            child: Row(
              children: [
                _Rotary(
                  value: control.targetValue,
                  enabled: enabled,
                  catching: control.catching,
                  onChanged: (value) =>
                      controller.setControlValue(control, value),
                  onReset: () => controller.resetControl(control),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Text(
                        parameter ?? l10n.performanceNoParameter,
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          color:
                              enabled ? AppColors.text : AppColors.textFaint,
                          fontSize: 11,
                          height: 1.2,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                      const SizedBox(height: 4),
                      Text(
                        control.valueText,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          color:
                              enabled ? AppColors.accent : AppColors.textFaint,
                          fontSize: 12,
                          fontWeight: FontWeight.w700,
                          fontFeatures: const [
                            FontFeature.tabularFigures(),
                          ],
                        ),
                      ),
                      if (control.catching) ...[
                        const SizedBox(height: 4),
                        _CatchBadge(label: l10n.performanceCatch),
                      ],
                    ],
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

class _FaderCard extends StatelessWidget {
  const _FaderCard({
    required this.controller,
    required this.control,
    required this.parameter,
  });

  final ControllerService controller;
  final ControllerControl control;
  final String? parameter;

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    final learning = controller.learnTarget?.id == control.id;
    final enabled = _targetLabel(l10n, control) != null || parameter != null;
    final name = _targetLabel(l10n, control) ??
        parameter ??
        l10n.performanceNoParameter;

    return _DeviceCard(
      learning: learning,
      child: Column(
        children: [
          Row(
            children: [
              Expanded(
                child: Text(
                  '${l10n.performanceFader} ${control.index + 1}',
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    color: AppColors.textDim,
                    fontSize: 9.5,
                    fontWeight: FontWeight.w700,
                    letterSpacing: 1.1,
                  ),
                ),
              ),
              _CcChip(cc: control.cc),
              const SizedBox(width: 2),
              _LearnButton(
                active: learning,
                enabled: enabled,
                onTap: () => controller.startLearn(control),
              ),
            ],
          ),
          const SizedBox(height: 2),
          Expanded(
            child: Row(
              children: [
                _VerticalFader(
                  value: control.targetValue,
                  enabled: enabled,
                  catching: control.catching,
                  onChanged: (value) =>
                      controller.setControlValue(control, value),
                  onReset: () => controller.resetControl(control),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Text(
                        name,
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          color:
                              enabled ? AppColors.text : AppColors.textFaint,
                          fontSize: 11,
                          height: 1.2,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                      const SizedBox(height: 5),
                      Text(
                        control.valueText,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                          color:
                              enabled ? AppColors.accent : AppColors.textFaint,
                          fontSize: 14,
                          fontWeight: FontWeight.w700,
                          fontFeatures: const [
                            FontFeature.tabularFigures(),
                          ],
                        ),
                      ),
                      if (control.catching) ...[
                        const SizedBox(height: 5),
                        _CatchBadge(label: l10n.performanceCatch),
                      ],
                    ],
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  String? _targetLabel(AppLocalizations l10n, ControllerControl control) {
    switch (control.targetKind) {
      case ControllerTargetKind.trackVolume:
        return l10n.performanceTrackVolume;
      case ControllerTargetKind.trackPan:
        return l10n.performanceTrackPan;
      case ControllerTargetKind.masterVolume:
        return l10n.performanceMasterVolume;
      case ControllerTargetKind.deviceParam:
        return null;
    }
  }
}

class _ActionCard extends StatelessWidget {
  const _ActionCard({required this.controller, required this.binding});

  final ControllerService controller;
  final ControllerActionBinding binding;

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    final learning = controller.learnAction?.action == binding.action;
    final trigger = binding.trigger;
    final active = _actionActive(controller, binding.action);

    return _DeviceCard(
      learning: learning,
      active: active,
      child: InkWell(
        onTap: learning ? null : () => controller.runAction(binding),
        borderRadius: BorderRadius.circular(7),
        child: Row(
          children: [
            Container(
              width: 28,
              height: 28,
              decoration: BoxDecoration(
                color: active ? AppColors.accentSoft : AppColors.bgDeep,
                borderRadius: BorderRadius.circular(7),
                border: Border.all(
                  color: active
                      ? AppColors.accent.withValues(alpha: 0.45)
                      : AppColors.border,
                ),
              ),
              child: Icon(
                _actionIcon(binding.action),
                size: 15,
                color: active ? AppColors.accent : AppColors.textDim,
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    _actionLabel(l10n, binding.action),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      color: active ? AppColors.text : AppColors.textDim,
                      fontSize: 10.5,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                  const SizedBox(height: 2),
                  Text(
                    trigger?.label ?? l10n.performanceActionUnassigned,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      color:
                          trigger == null ? AppColors.textFaint : AppColors.accent,
                      fontSize: 8.5,
                      fontWeight: FontWeight.w600,
                      fontFeatures: const [FontFeature.tabularFigures()],
                    ),
                  ),
                ],
              ),
            ),
            if (trigger != null && !learning)
              Tooltip(
                message: l10n.performanceActionClear,
                child: InkWell(
                  onTap: () => controller.clearAction(binding),
                  borderRadius: BorderRadius.circular(5),
                  child: const SizedBox(
                    width: 18,
                    height: 22,
                    child: Icon(
                      Icons.close_rounded,
                      size: 12,
                      color: AppColors.textFaint,
                    ),
                  ),
                ),
              ),
            _LearnButton(
              active: learning,
              enabled: true,
              onTap: () => controller.startActionLearn(binding),
            ),
          ],
        ),
      ),
    );
  }
}

bool _actionActive(ControllerService controller, ControllerAction action) {
  final track = controller.activeTrack;
  switch (action) {
    case ControllerAction.playPause:
      return controller.engine.isPlaying;
    case ControllerAction.record:
      return controller.engine.isRecording;
    case ControllerAction.metronome:
      return controller.engine.metronomeOn;
    case ControllerAction.muteTrack:
      return track?.mute ?? false;
    case ControllerAction.soloTrack:
      return track?.solo ?? false;
    case ControllerAction.stop:
    case ControllerAction.tapTempo:
    case ControllerAction.undo:
      return false;
  }
}

String _actionLabel(AppLocalizations l10n, ControllerAction action) {
  switch (action) {
    case ControllerAction.playPause:
      return l10n.transportPlayPause;
    case ControllerAction.stop:
      return l10n.transportStop;
    case ControllerAction.record:
      return l10n.transportRecord;
    case ControllerAction.metronome:
      return l10n.transportMetronome;
    case ControllerAction.tapTempo:
      return l10n.performanceTapTempo;
    case ControllerAction.undo:
      return l10n.transportUndo;
    case ControllerAction.muteTrack:
      return l10n.timelineMute;
    case ControllerAction.soloTrack:
      return l10n.timelineSolo;
  }
}

IconData _actionIcon(ControllerAction action) {
  switch (action) {
    case ControllerAction.playPause:
      return Icons.play_arrow_rounded;
    case ControllerAction.stop:
      return Icons.stop_rounded;
    case ControllerAction.record:
      return Icons.fiber_manual_record_rounded;
    case ControllerAction.metronome:
      return Icons.av_timer_rounded;
    case ControllerAction.tapTempo:
      return Icons.touch_app_rounded;
    case ControllerAction.undo:
      return Icons.undo_rounded;
    case ControllerAction.muteTrack:
      return Icons.volume_off_rounded;
    case ControllerAction.soloTrack:
      return Icons.headphones_rounded;
  }
}

class _DeviceCard extends StatefulWidget {
  const _DeviceCard({
    required this.child,
    required this.learning,
    this.active = false,
  });

  final Widget child;
  final bool learning;
  final bool active;

  @override
  State<_DeviceCard> createState() => _DeviceCardState();
}

class _DeviceCardState extends State<_DeviceCard> {
  bool _hovered = false;

  @override
  Widget build(BuildContext context) {
    final borderColor = widget.learning
        ? AppColors.accent
        : widget.active
            ? AppColors.accent.withValues(alpha: 0.55)
            : _hovered
                ? AppColors.borderStrong
                : AppColors.border;
    return MouseRegion(
      onEnter: (_) => setState(() => _hovered = true),
      onExit: (_) => setState(() => _hovered = false),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 120),
        padding: const EdgeInsets.fromLTRB(12, 9, 12, 10),
        decoration: BoxDecoration(
          color: widget.learning
              ? blend(AppColors.surface, AppColors.accent, 0.08)
              : widget.active
                  ? blend(AppColors.surface, AppColors.accent, 0.035)
                  : AppColors.surface,
          borderRadius: BorderRadius.circular(11),
          border: Border.all(
            color: borderColor,
            width: widget.learning ? 1.5 : 1,
          ),
        ),
        child: widget.child,
      ),
    );
  }
}

class _CcChip extends StatelessWidget {
  const _CcChip({required this.cc});

  final int cc;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 2),
      decoration: BoxDecoration(
        color: AppColors.bgDeep,
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: AppColors.border),
      ),
      child: Text(
        cc < 0 ? '—' : 'CC $cc',
        style: const TextStyle(
          color: AppColors.textFaint,
          fontSize: 8.5,
          fontWeight: FontWeight.w600,
          fontFeatures: [FontFeature.tabularFigures()],
        ),
      ),
    );
  }
}

class _LearnButton extends StatelessWidget {
  const _LearnButton({
    required this.active,
    required this.enabled,
    required this.onTap,
  });

  final bool active;
  final bool enabled;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: context.l10n.performanceLearn,
      child: InkWell(
        onTap: enabled ? onTap : null,
        borderRadius: BorderRadius.circular(5),
        child: Container(
          width: 22,
          height: 22,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: active ? AppColors.accentSoft : Colors.transparent,
            borderRadius: BorderRadius.circular(5),
          ),
          child: Icon(
            Icons.ads_click_rounded,
            size: 13,
            color: active
                ? AppColors.accent
                : enabled
                    ? AppColors.textFaint
                    : AppColors.borderStrong,
          ),
        ),
      ),
    );
  }
}

class _CatchBadge extends StatelessWidget {
  const _CatchBadge({required this.label});

  final String label;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 2),
      decoration: BoxDecoration(
        color: AppColors.warning.withValues(alpha: 0.15),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(
          color: AppColors.warning.withValues(alpha: 0.45),
        ),
      ),
      child: Text(
        label,
        style: const TextStyle(
          color: AppColors.warning,
          fontSize: 7.5,
          fontWeight: FontWeight.w800,
          letterSpacing: 0.7,
        ),
      ),
    );
  }
}

class _Rotary extends StatelessWidget {
  const _Rotary({
    required this.value,
    required this.enabled,
    required this.catching,
    required this.onChanged,
    required this.onReset,
  });

  final double value;
  final bool enabled;
  final bool catching;
  final ValueChanged<double> onChanged;
  final VoidCallback onReset;

  @override
  Widget build(BuildContext context) {
    final size = 72.0;
    return MouseRegion(
      cursor: enabled
          ? SystemMouseCursors.resizeUpDown
          : SystemMouseCursors.basic,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onVerticalDragUpdate: enabled
            ? (details) => onChanged(
                  (value - details.delta.dy / 180).clamp(0.0, 1.0),
                )
            : null,
        onDoubleTap: enabled ? onReset : null,
        child: SizedBox(
          width: size,
          height: size,
          child: CustomPaint(
            painter: _KnobPainter(
              value: value.clamp(0.0, 1.0),
              enabled: enabled,
              catching: catching,
            ),
          ),
        ),
      ),
    );
  }
}

class _KnobPainter extends CustomPainter {
  _KnobPainter({
    required this.value,
    required this.enabled,
    required this.catching,
  });

  final double value;
  final bool enabled;
  final bool catching;

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final radius = math.min(size.width, size.height) / 2 - 6;
    const start = math.pi * 0.75;
    const sweep = math.pi * 1.5;
    final arcRect = Rect.fromCircle(center: center, radius: radius);

    final track = Paint()
      ..color = enabled ? AppColors.border : AppColors.border.withValues(alpha: 0.45)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 5
      ..strokeCap = StrokeCap.round;
    canvas.drawArc(arcRect, start, sweep, false, track);

    final accent = catching
        ? AppColors.warning
        : enabled
            ? AppColors.accent
            : AppColors.textFaint.withValues(alpha: 0.4);
    final fill = Paint()
      ..color = accent
      ..style = PaintingStyle.stroke
      ..strokeWidth = 5
      ..strokeCap = StrokeCap.round;
    canvas.drawArc(arcRect, start, sweep * value, false, fill);

    final pointerAngle = start + sweep * value;
    final pointerStart = center +
        Offset(math.cos(pointerAngle), math.sin(pointerAngle)) * (radius - 9);
    final pointerEnd = center +
        Offset(math.cos(pointerAngle), math.sin(pointerAngle)) * (radius - 1);
    final pointer = Paint()
      ..color = enabled ? AppColors.text : AppColors.textFaint
      ..strokeWidth = 2.2
      ..strokeCap = StrokeCap.round;
    canvas.drawLine(pointerStart, pointerEnd, pointer);

    final hub = Paint()..color = enabled ? AppColors.surfaceAlt : AppColors.bgDeep;
    canvas.drawCircle(center, radius * 0.46, hub);
    canvas.drawCircle(
      center,
      radius * 0.46,
      Paint()
        ..color = AppColors.border
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1,
    );
  }

  @override
  bool shouldRepaint(_KnobPainter oldDelegate) =>
      oldDelegate.value != value ||
      oldDelegate.enabled != enabled ||
      oldDelegate.catching != catching;
}

class _VerticalFader extends StatelessWidget {
  const _VerticalFader({
    required this.value,
    required this.enabled,
    required this.catching,
    required this.onChanged,
    required this.onReset,
  });

  final double value;
  final bool enabled;
  final bool catching;
  final ValueChanged<double> onChanged;
  final VoidCallback onReset;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: enabled
          ? SystemMouseCursors.resizeUpDown
          : SystemMouseCursors.basic,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onVerticalDragUpdate: enabled
            ? (details) => onChanged(
                  (value - details.delta.dy / 120).clamp(0.0, 1.0),
                )
            : null,
        onDoubleTap: enabled ? onReset : null,
        child: SizedBox(
          width: 34,
          height: double.infinity,
          child: CustomPaint(
            painter: _FaderPainter(
              value: value.clamp(0.0, 1.0),
              enabled: enabled,
              catching: catching,
            ),
          ),
        ),
      ),
    );
  }
}

class _FaderPainter extends CustomPainter {
  _FaderPainter({
    required this.value,
    required this.enabled,
    required this.catching,
  });

  final double value;
  final bool enabled;
  final bool catching;

  @override
  void paint(Canvas canvas, Size size) {
    const top = 8.0;
    final bottom = size.height - 8;
    final travel = bottom - top;
    final x = size.width / 2;
    final track = Paint()
      ..color = AppColors.bgDeep
      ..strokeWidth = 7
      ..strokeCap = StrokeCap.round;
    canvas.drawLine(Offset(x, top), Offset(x, bottom), track);

    final accent = catching
        ? AppColors.warning
        : enabled
            ? AppColors.accent
            : AppColors.textFaint.withValues(alpha: 0.4);
    final filledTop = bottom - travel * value;
    canvas.drawLine(
      Offset(x, filledTop),
      Offset(x, bottom),
      Paint()
        ..color = accent
        ..strokeWidth = 7
        ..strokeCap = StrokeCap.round,
    );

    final thumbRect = RRect.fromRectAndRadius(
      Rect.fromCenter(
        center: Offset(x, filledTop),
        width: 26,
        height: 11,
      ),
      const Radius.circular(4),
    );
    canvas.drawRRect(
      thumbRect,
      Paint()..color = enabled ? AppColors.surfaceHigh : AppColors.surfaceAlt,
    );
    canvas.drawRRect(
      thumbRect,
      Paint()
        ..color = accent
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.4,
    );
    canvas.drawLine(
      Offset(x - 6, filledTop),
      Offset(x + 6, filledTop),
      Paint()
        ..color = accent
        ..strokeWidth = 1.8
        ..strokeCap = StrokeCap.round,
    );

    final tick = Paint()
      ..color = AppColors.border
      ..strokeWidth = 1;
    for (var i = 0; i <= 4; i++) {
      final y = top + travel * i / 4;
      canvas.drawLine(Offset(x + 18, y), Offset(x + 23, y), tick);
    }
  }

  @override
  bool shouldRepaint(_FaderPainter oldDelegate) =>
      oldDelegate.value != value ||
      oldDelegate.enabled != enabled ||
      oldDelegate.catching != catching;
}

class _Field extends StatelessWidget {
  const _Field({required this.label, required this.child});

  final String label;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label.toUpperCase(),
          style: const TextStyle(
            color: AppColors.textFaint,
            fontSize: 7.5,
            fontWeight: FontWeight.w700,
            letterSpacing: 0.9,
          ),
        ),
        const SizedBox(height: 1),
        child,
      ],
    );
  }
}

class _ToggleChip extends StatelessWidget {
  const _ToggleChip({
    required this.icon,
    required this.label,
    required this.active,
    required this.onTap,
    this.compact = false,
  });

  final IconData icon;
  final String label;
  final bool active;
  final VoidCallback onTap;
  final bool compact;

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: label,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(7),
        child: Container(
          height: 27,
          width: compact ? 30 : null,
          padding: EdgeInsets.symmetric(horizontal: compact ? 0 : 8),
          alignment: compact ? Alignment.center : null,
          decoration: BoxDecoration(
            color: active ? AppColors.accentSoft : AppColors.surfaceAlt,
            borderRadius: BorderRadius.circular(7),
            border: Border.all(
              color: active
                  ? AppColors.accent.withValues(alpha: 0.55)
                  : AppColors.border,
            ),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                icon,
                size: 13,
                color: active ? AppColors.accent : AppColors.textFaint,
              ),
              if (!compact) ...[
                const SizedBox(width: 5),
                Text(
                  label,
                  style: TextStyle(
                    color: active ? AppColors.text : AppColors.textDim,
                    fontSize: 9.5,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

class _InputSelector extends StatelessWidget {
  const _InputSelector({required this.service, required this.controller});

  final EngineService service;
  final ControllerService controller;

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    final names = service.midiInputNames;
    final selected = service.selectedMidiInputIndex;
    final hasInput = selected >= 0 && selected < names.length;
    final label = hasInput ? names[selected] : l10n.performanceNoInput;
    final isMiniLab = hasInput && label.toLowerCase().contains('minilab');
    final color = hasInput
        ? (isMiniLab ? AppColors.accent : AppColors.success)
        : AppColors.textFaint;

    return PopupMenuButton<int>(
      tooltip: l10n.performanceInput,
      position: PopupMenuPosition.under,
      color: AppColors.surfaceHigh,
      onSelected: (index) {
        service.selectMidiInput(index);
        controller.resetTakeover();
      },
      itemBuilder: (context) => [
        PopupMenuItem<int>(
          value: -1,
          child: Row(
            children: [
              Icon(
                selected < 0
                    ? Icons.check_rounded
                    : Icons.usb_off_rounded,
                size: 14,
                color: selected < 0 ? AppColors.accent : AppColors.textFaint,
              ),
              const SizedBox(width: 8),
              Text(
                l10n.performanceNoInput,
                style: const TextStyle(
                  color: AppColors.textDim,
                  fontSize: 11,
                ),
              ),
            ],
          ),
        ),
        if (names.isNotEmpty) const PopupMenuDivider(),
        for (var i = 0; i < names.length; i++)
          PopupMenuItem<int>(
            value: i,
            child: Row(
              children: [
                Icon(
                  selected == i
                      ? Icons.check_rounded
                      : Icons.usb_rounded,
                  size: 14,
                  color: selected == i ? AppColors.accent : AppColors.textFaint,
                ),
                const SizedBox(width: 8),
                Flexible(
                  child: Text(
                    names[i],
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      color: selected == i
                          ? AppColors.text
                          : AppColors.textDim,
                      fontSize: 11,
                      fontWeight: selected == i
                          ? FontWeight.w600
                          : FontWeight.w400,
                    ),
                  ),
                ),
              ],
            ),
          ),
      ],
      child: Container(
        constraints: const BoxConstraints(maxWidth: 220),
        height: 29,
        padding: const EdgeInsets.symmetric(horizontal: 9),
        decoration: BoxDecoration(
          color: AppColors.surface,
          borderRadius: BorderRadius.circular(7),
          border: Border.all(
            color: hasInput
                ? (isMiniLab
                    ? AppColors.accent.withValues(alpha: 0.45)
                    : AppColors.borderStrong)
                : AppColors.border,
          ),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              isMiniLab ? Icons.piano_rounded : Icons.usb_rounded,
              size: 13,
              color: color,
            ),
            const SizedBox(width: 6),
            Flexible(
              child: Text(
                label,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: hasInput ? AppColors.text : AppColors.textDim,
                  fontSize: 9.5,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ),
            const SizedBox(width: 3),
            const Icon(
              Icons.expand_more_rounded,
              size: 14,
              color: AppColors.textFaint,
            ),
          ],
        ),
      ),
    );
  }
}

class _StatusChip extends StatelessWidget {
  const _StatusChip({
    required this.icon,
    required this.label,
    required this.active,
    this.accent = false,
  });

  final IconData icon;
  final String label;
  final bool active;
  final bool accent;

  @override
  Widget build(BuildContext context) {
    final color = active
        ? (accent ? AppColors.accent : AppColors.success)
        : AppColors.textFaint;
    return Container(
      constraints: const BoxConstraints(maxWidth: 160),
      height: 27,
      padding: const EdgeInsets.symmetric(horizontal: 8),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(7),
        border: Border.all(color: AppColors.border),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 12, color: color),
          const SizedBox(width: 5),
          Flexible(
            child: Text(
              label,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(
                color: AppColors.textDim,
                fontSize: 9.5,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
