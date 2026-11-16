// Generic Flutter editor for Cadence's built-in devices.
//
// The engine already exposes every AudioProcessor parameter through the
// plugin-parameter C ABI. That is enough to draw a real editor without
// opening a native window: continuous parameters become knobs, booleans
// become switches, choices become menus, and every value is edited through
// the same normalized setter used by automation.
//
// External VST3/AU plug-ins keep their own native editor; this dialog is for
// the "Internal" devices where Cadence owns both the DSP and the UI.

import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import 'design.dart';

Future<void> showDeviceEditorDialog(
  BuildContext context,
  EngineService service, {
  required int trackId,
  required int slotKind,
  required int slotIndex,
  required String title,
}) {
  return showDialog<void>(
    context: context,
    barrierDismissible: true,
    builder: (context) {
      final media = MediaQuery.sizeOf(context);
      return Dialog(
        backgroundColor: AppColors.bgPanel,
        insetPadding: const EdgeInsets.all(24),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
        child: SizedBox(
          width: math.min(840, media.width - 48),
          height: math.min(620, media.height - 48),
          child: DeviceEditorBody(
            service: service,
            trackId: trackId,
            slotKind: slotKind,
            slotIndex: slotIndex,
            title: title,
          ),
        ),
      );
    },
  );
}

/// Opens the right editor for an insert: the Flutter device editor for a
/// Cadence-owned effect, or the plug-in's own native window for VST3/AU.
Future<void> editTrackFx(
  BuildContext context,
  EngineService service,
  int trackId,
  int slotIndex,
  String title,
) async {
  final uid = service.trackFxUidAt(trackId, slotIndex);
  if (service.isInternalDeviceUid(uid)) {
    await showDeviceEditorDialog(
      context,
      service,
      trackId: trackId,
      slotKind: 0,
      slotIndex: slotIndex,
      title: title,
    );
    return;
  }
  service.openTrackPluginEditor(trackId, slotIndex);
}

Future<void> editMasterFx(
  BuildContext context,
  EngineService service,
  int slotIndex,
  String title,
) async {
  final uid = service.masterFxUidAt(slotIndex);
  if (service.isInternalDeviceUid(uid)) {
    await showDeviceEditorDialog(
      context,
      service,
      trackId: 0,
      slotKind: 2,
      slotIndex: slotIndex,
      title: title,
    );
    return;
  }
  service.openMasterFxEditor(slotIndex);
}

class _DeviceParam {
  _DeviceParam({
    required this.index,
    required this.name,
    required this.value,
    required this.text,
    required this.flags,
    required this.steps,
    required this.defaultValue,
    required this.labels,
  });

  final int index;
  final String name;
  double value;
  String text;
  final int flags;
  final int steps;
  final double defaultValue;
  final List<String> labels;

  bool get isBoolean => (flags & 1) != 0;
  bool get isChoice => (flags & 2) != 0;
  bool get isInteger => (flags & 4) != 0;
  bool get isContinuous => !isBoolean && !isChoice && !isInteger;
}

class DeviceEditorBody extends StatefulWidget {
  const DeviceEditorBody({
    super.key,
    required this.service,
    required this.trackId,
    required this.slotKind,
    required this.slotIndex,
    required this.title,
  });

  final EngineService service;
  final int trackId;
  final int slotKind;
  final int slotIndex;
  final String title;

  @override
  State<DeviceEditorBody> createState() => _DeviceEditorBodyState();
}

class _DeviceEditorBodyState extends State<DeviceEditorBody> {
  final List<_DeviceParam> _params = [];
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    _readParameters();
  }

  void _readParameters() {
    final count = widget.service.pluginParamCount(
      widget.trackId,
      widget.slotKind,
      widget.slotIndex,
    );
    final params = <_DeviceParam>[];
    for (var i = 0; i < count; i++) {
      final name = widget.service.pluginParamName(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      if (name.trim().isEmpty) continue;

      final flags = widget.service.pluginParamFlags(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      final steps = widget.service.pluginParamNumSteps(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      final value = widget.service.pluginParamValue(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      final text = widget.service.pluginParamText(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );
      final defaultValue = widget.service.pluginParamDefault(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        i,
      );

      final labels = <String>[];
      if ((flags & 2) != 0 && steps > 0) {
        for (var s = 0; s < steps; s++) {
          final normalised = steps <= 1 ? 0.0 : s / (steps - 1);
          labels.add(
            widget.service.pluginParamTextAtValue(
              widget.trackId,
              widget.slotKind,
              widget.slotIndex,
              i,
              normalised,
            ),
          );
        }
      } else if ((flags & 1) != 0) {
        labels.add(
          widget.service.pluginParamTextAtValue(
            widget.trackId,
            widget.slotKind,
            widget.slotIndex,
            i,
            0.0,
          ),
        );
        labels.add(
          widget.service.pluginParamTextAtValue(
            widget.trackId,
            widget.slotKind,
            widget.slotIndex,
            i,
            1.0,
          ),
        );
      }

      params.add(
        _DeviceParam(
          index: i,
          name: name,
          value: value,
          text: text,
          flags: flags,
          steps: steps,
          defaultValue: defaultValue,
          labels: labels,
        ),
      );
    }

    if (!mounted) return;
    setState(() {
      _params
        ..clear()
        ..addAll(params);
      _loading = false;
    });
  }

  _DeviceParam? get _bypass {
    for (final p in _params) {
      if (p.isBoolean && p.name.toLowerCase() == 'bypass') return p;
    }
    return null;
  }

  void _setValue(_DeviceParam param, double value) {
    final clamped = value.clamp(0.0, 1.0).toDouble();
    widget.service.setPluginParamValue(
      widget.trackId,
      widget.slotKind,
      widget.slotIndex,
      param.index,
      clamped,
    );
    setState(() {
      param.value = clamped;
      param.text = widget.service.pluginParamText(
        widget.trackId,
        widget.slotKind,
        widget.slotIndex,
        param.index,
      );
    });
  }

  @override
  Widget build(BuildContext context) {
    final bypass = _bypass;
    final visible = _params.where((p) => !identical(p, bypass)).toList();

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _Header(
          title: widget.title,
          count: _params.length,
          bypass: bypass,
          onBypass:
              bypass == null ? null : (on) => _setValue(bypass, on ? 1.0 : 0.0),
          onClose: () => Navigator.of(context).pop(),
        ),
        const Divider(height: 1, color: AppColors.border),
        Expanded(
          child:
              _loading
                  ? const Center(
                    child: SizedBox(
                      width: 22,
                      height: 22,
                      child: CircularProgressIndicator(strokeWidth: 2),
                    ),
                  )
                  : visible.isEmpty
                  ? const Center(
                    child: Text(
                      'This device has no editable parameters.',
                      style: TextStyle(color: AppColors.textDim, fontSize: 12),
                    ),
                  )
                  : GridView.builder(
                    padding: const EdgeInsets.all(12),
                    gridDelegate:
                        const SliverGridDelegateWithMaxCrossAxisExtent(
                          maxCrossAxisExtent: 132,
                          mainAxisExtent: 132,
                          crossAxisSpacing: 8,
                          mainAxisSpacing: 8,
                        ),
                    itemCount: visible.length,
                    itemBuilder: (context, i) {
                      final p = visible[i];
                      if (p.isBoolean) {
                        return _BoolTile(
                          param: p,
                          onChanged: (on) => _setValue(p, on ? 1.0 : 0.0),
                        );
                      }
                      if (p.isChoice) {
                        return _ChoiceTile(
                          param: p,
                          onChanged: (index) {
                            final n =
                                p.steps <= 1 ? 0.0 : index / (p.steps - 1);
                            _setValue(p, n);
                          },
                        );
                      }
                      return _KnobTile(
                        param: p,
                        onChanged: (v) => _setValue(p, v),
                        onReset: () => _setValue(p, p.defaultValue),
                      );
                    },
                  ),
        ),
        const Divider(height: 1, color: AppColors.border),
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
          child: Row(
            children: [
              const Icon(Icons.auto_awesome, size: 13, color: AppColors.accent),
              const SizedBox(width: 6),
              Expanded(
                child: Text(
                  'Drag a knob to edit · double-click to reset · changes apply instantly',
                  style: const TextStyle(
                    color: AppColors.textFaint,
                    fontSize: 10,
                  ),
                ),
              ),
              Text(
                '${visible.length} parameters',
                style: const TextStyle(
                  color: AppColors.textFaint,
                  fontSize: 10,
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _Header extends StatelessWidget {
  const _Header({
    required this.title,
    required this.count,
    required this.bypass,
    required this.onBypass,
    required this.onClose,
  });

  final String title;
  final int count;
  final _DeviceParam? bypass;
  final ValueChanged<bool>? onBypass;
  final VoidCallback onClose;

  @override
  Widget build(BuildContext context) {
    final bypassed = (bypass?.value ?? 0) >= 0.5;
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 12, 8, 10),
      child: Row(
        children: [
          Container(
            width: 30,
            height: 30,
            decoration: BoxDecoration(
              color: AppColors.accentSoft,
              borderRadius: BorderRadius.circular(7),
            ),
            child: const Icon(Icons.tune, size: 17, color: AppColors.accent),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w600,
                    color: AppColors.text,
                  ),
                ),
                const SizedBox(height: 2),
                Text(
                  '$count parameters · built-in device',
                  style: const TextStyle(
                    fontSize: 10,
                    color: AppColors.textFaint,
                  ),
                ),
              ],
            ),
          ),
          if (bypass != null)
            Tooltip(
              message: bypassed ? 'Enable device' : 'Bypass device',
              child: TextButton.icon(
                onPressed: onBypass == null ? null : () => onBypass!(!bypassed),
                icon: Icon(
                  Icons.power_settings_new,
                  size: 15,
                  color: bypassed ? AppColors.textFaint : AppColors.success,
                ),
                label: Text(
                  bypassed ? 'Bypassed' : 'Active',
                  style: TextStyle(
                    fontSize: 11,
                    color: bypassed ? AppColors.textFaint : AppColors.success,
                  ),
                ),
              ),
            ),
          IconButton(
            onPressed: onClose,
            icon: const Icon(Icons.close, size: 17, color: AppColors.textDim),
          ),
        ],
      ),
    );
  }
}

class _TileShell extends StatelessWidget {
  const _TileShell({required this.child, this.highlight = false});

  final Widget child;
  final bool highlight;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.fromLTRB(8, 7, 8, 7),
      decoration: BoxDecoration(
        color: highlight ? AppColors.surfaceAlt : AppColors.surface,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(
          color: highlight ? AppColors.borderStrong : AppColors.border,
        ),
      ),
      child: child,
    );
  }
}

class _KnobTile extends StatelessWidget {
  const _KnobTile({
    required this.param,
    required this.onChanged,
    required this.onReset,
  });

  final _DeviceParam param;
  final ValueChanged<double> onChanged;
  final VoidCallback onReset;

  @override
  Widget build(BuildContext context) {
    return _TileShell(
      child: Column(
        children: [
          Expanded(
            child: Center(
              child: _Knob(
                value: param.value,
                onChanged: onChanged,
                onReset: onReset,
              ),
            ),
          ),
          const SizedBox(height: 3),
          Text(
            param.name,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            textAlign: TextAlign.center,
            style: const TextStyle(fontSize: 9.5, color: AppColors.textDim),
          ),
          const SizedBox(height: 1),
          Text(
            param.text,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            textAlign: TextAlign.center,
            style: const TextStyle(
              fontSize: 10,
              fontWeight: FontWeight.w600,
              color: AppColors.text,
            ),
          ),
        ],
      ),
    );
  }
}

class _Knob extends StatefulWidget {
  const _Knob({
    required this.value,
    required this.onChanged,
    required this.onReset,
  });

  final double value;
  final ValueChanged<double> onChanged;
  final VoidCallback onReset;

  @override
  State<_Knob> createState() => _KnobState();
}

class _KnobState extends State<_Knob> {
  bool _dragging = false;

  void _begin() {
    setState(() => _dragging = true);
  }

  void _end() {
    setState(() => _dragging = false);
  }

  @override
  Widget build(BuildContext context) {
    return Semantics(
      slider: true,
      label: 'Parameter value',
      value: widget.value.toStringAsFixed(3),
      child: MouseRegion(
        cursor: SystemMouseCursors.resizeUpDown,
        child: GestureDetector(
          onVerticalDragStart: (_) => _begin(),
          onVerticalDragUpdate: (details) {
            final delta = -details.delta.dy * 0.006;
            widget.onChanged((widget.value + delta).clamp(0.0, 1.0));
          },
          onVerticalDragEnd: (_) => _end(),
          onVerticalDragCancel: _end,
          onDoubleTap: widget.onReset,
          child: SizedBox(
            width: 64,
            height: 64,
            child: CustomPaint(
              painter: _KnobPainter(value: widget.value, dragging: _dragging),
            ),
          ),
        ),
      ),
    );
  }
}

class _KnobPainter extends CustomPainter {
  _KnobPainter({required this.value, required this.dragging});

  final double value;
  final bool dragging;

  static const _start = math.pi * 0.75;
  static const _sweep = math.pi * 1.5;

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final radius = math.min(size.width, size.height) * 0.38;
    final rect = Rect.fromCircle(center: center, radius: radius);

    final track =
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 5
          ..strokeCap = StrokeCap.round
          ..color = AppColors.bgDeep;
    canvas.drawArc(rect, _start, _sweep, false, track);

    final fill =
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 5
          ..strokeCap = StrokeCap.round
          ..color =
              dragging
                  ? AppColors.accent
                  : AppColors.accent.withValues(alpha: 0.8);
    canvas.drawArc(rect, _start, _sweep * value.clamp(0.0, 1.0), false, fill);

    final angle = _start + _sweep * value.clamp(0.0, 1.0);
    final inner =
        center + Offset(math.cos(angle), math.sin(angle)) * (radius - 9);
    final outer =
        center + Offset(math.cos(angle), math.sin(angle)) * (radius + 5);
    canvas.drawLine(
      inner,
      outer,
      Paint()
        ..color = AppColors.text
        ..strokeWidth = 2
        ..strokeCap = StrokeCap.round,
    );

    canvas.drawCircle(center, 3, Paint()..color = AppColors.borderStrong);
  }

  @override
  bool shouldRepaint(covariant _KnobPainter old) =>
      old.value != value || old.dragging != dragging;
}

class _BoolTile extends StatelessWidget {
  const _BoolTile({required this.param, required this.onChanged});

  final _DeviceParam param;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    final on = param.value >= 0.5;
    return _TileShell(
      highlight: on,
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(
            on ? Icons.check_circle : Icons.radio_button_unchecked,
            size: 26,
            color: on ? AppColors.accent : AppColors.textFaint,
          ),
          const SizedBox(height: 8),
          Text(
            param.name,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
            textAlign: TextAlign.center,
            style: const TextStyle(fontSize: 10, color: AppColors.textDim),
          ),
          const SizedBox(height: 4),
          Switch(
            value: on,
            onChanged: onChanged,
            materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
          ),
        ],
      ),
    );
  }
}

class _ChoiceTile extends StatelessWidget {
  const _ChoiceTile({required this.param, required this.onChanged});

  final _DeviceParam param;
  final ValueChanged<int> onChanged;

  int get _index {
    if (param.steps <= 1) return 0;
    return (param.value * (param.steps - 1)).round().clamp(0, param.steps - 1);
  }

  @override
  Widget build(BuildContext context) {
    final labels =
        param.labels.isEmpty
            ? List<String>.generate(param.steps, (i) => '${i + 1}')
            : param.labels;
    return _TileShell(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Expanded(
            child: Center(
              child: Text(
                param.text,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                textAlign: TextAlign.center,
                style: const TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.w600,
                  color: AppColors.text,
                ),
              ),
            ),
          ),
          const SizedBox(height: 4),
          Text(
            param.name,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(fontSize: 9.5, color: AppColors.textDim),
          ),
          const SizedBox(height: 2),
          DecoratedBox(
            decoration: BoxDecoration(
              color: AppColors.bgDeep,
              borderRadius: BorderRadius.circular(4),
              border: Border.all(color: AppColors.border),
            ),
            child: DropdownButtonHideUnderline(
              child: DropdownButton<int>(
                value: _index,
                isDense: true,
                isExpanded: true,
                padding: const EdgeInsets.symmetric(horizontal: 6),
                dropdownColor: AppColors.surfaceHigh,
                style: const TextStyle(fontSize: 10, color: AppColors.text),
                icon: const Icon(
                  Icons.expand_more,
                  size: 14,
                  color: AppColors.textDim,
                ),
                items: [
                  for (var i = 0; i < labels.length; i++)
                    DropdownMenuItem(
                      value: i,
                      child: Text(
                        labels[i],
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                ],
                onChanged: (v) {
                  if (v != null) onChanged(v);
                },
              ),
            ),
          ),
        ],
      ),
    );
  }
}
