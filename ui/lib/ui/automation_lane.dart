// Automation lane (§7.12 v1): volume curve of the selected track.
// Points + linear segments; tap empty = add (snapped to the beat grid),
// drag = move (one undo on release), right-click a point = delete.
// Read mode is always active in the engine; Write/Touch arrive in v1.5.


import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import '../engine/project_snapshot.dart';
import '../l10n/l10n.dart';
import 'app_alert.dart';

class AutomationLane extends StatefulWidget {
  const AutomationLane(
      {super.key, required this.service, required this.frameTick,
      this.height = 190, this.onUndock});

  final EngineService service;
  final ValueListenable<int> frameTick;
  /// null = fill the parent (undocked window).
  final double? height;
  final VoidCallback? onUndock;

  @override
  State<AutomationLane> createState() => _AutomationLaneState();
}
class _AutomationLaneState extends State<AutomationLane> {
  static const double _pps = 90; // matches the default timeline zoom
  static const double _hitRadius = 9;

  final ScrollController _hScroll = ScrollController();

  @override
  void initState() {
    super.initState();
    widget.frameTick.addListener(_followPlayhead);
  }

  @override
  void didUpdateWidget(covariant AutomationLane oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.frameTick != widget.frameTick) {
      oldWidget.frameTick.removeListener(_followPlayhead);
      widget.frameTick.addListener(_followPlayhead);
    }
  }

  @override
  void dispose() {
    widget.frameTick.removeListener(_followPlayhead);
    _hScroll.dispose();
    super.dispose();
  }

  /// Auto-scroll: while playing, keep the playhead visible. While paused,
  /// follow seeks (the playhead moved in the timeline — show it).
  double _lastSeenPos = -1;
  void _followPlayhead() {
    if (!_hScroll.hasClients) return;
    final pos = widget.service.positionSeconds;
    final prev = _lastSeenPos;
    _lastSeenPos = pos;
    final x = pos * _pps;
    final viewport = _hScroll.position.viewportDimension;
    const margin = 48.0;
    if (widget.service.isPlaying) {
      if (x > _hScroll.offset + viewport - margin || x < _hScroll.offset) {
        _hScroll.jumpTo((x - viewport * 0.1)
            .clamp(0.0, _hScroll.position.maxScrollExtent));
      }
    } else if (prev >= 0 && (pos - prev).abs() > 1e-9) {
      _hScroll.jumpTo((x - viewport * 0.35)
          .clamp(0.0, _hScroll.position.maxScrollExtent));
    }
  }

  /// Snap = an eighth note at the project tempo.
  double get _snapSeconds => (60.0 / widget.service.tempoBpm) / 2;

  /// Selected automation lane key: 'vol' | 'pan' | 'sus' |
  /// 'p:slotKind:slotIndex:paramIndex'.
  String _laneKey = 'vol';
  int? _dragIndex;
  List<AutomationPointInfo> _preview = const [];
  double _laneHeight = 160;

  /// Content x of a viewport-local pointer position.
  double _contentDx(double dx) =>
      dx + (_hScroll.hasClients ? _hScroll.offset : 0);

  TrackInfo? _track() {
    final id = widget.service.selectedTrackId;
    if (id == null) return null;
    for (final t in widget.service.snapshot.tracks) {
      if (t.id == id) return t;
    }
    return null;
  }

  ParamLaneInfo? _paramLaneOf(TrackInfo track, String key) {
    if (!key.startsWith('p:')) return null;
    final parts = key.split(':');
    if (parts.length != 4) return null;
    final kind = int.tryParse(parts[1]);
    final slot = int.tryParse(parts[2]);
    final param = int.tryParse(parts[3]);
    if (kind == null || slot == null || param == null) return null;
    for (final lane in track.paramLanes) {
      if (lane.slotKind == kind &&
          lane.slotIndex == slot &&
          lane.paramIndex == param) {
        return lane;
      }
    }
    return null;
  }

  List<AutomationPointInfo> _pointsOf(TrackInfo track) {
    final live = switch (_laneKey) {
      'pan' => track.panAuto,
      'sus' => track.sustainAuto,
      'vol' => track.volumeAuto,
      _ => _paramLanePoints(track, _laneKey),
    };
    return _dragIndex == null ? live : _preview;
  }

  List<AutomationPointInfo> _paramLanePoints(TrackInfo track, String key) {
    final lane = _paramLaneOf(track, key);
    return lane?.points ?? const [];
  }

  void _commit(TrackInfo track, List<AutomationPointInfo> points) {
    points.sort((a, b) => a.time.compareTo(b.time));
    if (_laneKey.startsWith('p:')) {
      final parts = _laneKey.split(':');
      final kind = int.parse(parts[1]);
      final slot = int.parse(parts[2]);
      final param = int.parse(parts[3]);
      final lane = _paramLaneOf(track, _laneKey);
      widget.service.setTrackParamAutomation(track.id, kind, slot, param,
          lane?.name ?? L10n.current.automationParamLaneDefault, points);
      return;
    }
    widget.service.setTrackLaneAutomation(track.id,
        switch (_laneKey) { 'pan' => 1, 'sus' => 2, _ => 0 }, points);
  }

  double _yFor(double value, double height) =>
      (1 - value.clamp(0.0, 1.0)) * (height - 20) + 10;

  double _valueAt(double dy, double height) =>
      (1 - (dy - 10) / (height - 20)).clamp(0.0, 1.0);

  int? _hitPoint(List<AutomationPointInfo> points, Offset content) {
    for (var i = 0; i < points.length; i++) {
      final dx = points[i].time * _pps - content.dx;
      final dy = _yFor(points[i].value, _laneHeight) - content.dy;
      if (dx * dx + dy * dy <= _hitRadius * _hitRadius) return i;
    }
    return null;
  }

  // ---- Raw pointer interaction (no gesture arena) ----
  // Same as the piano roll: Listener events always arrive regardless of the
  // scroll; drag-to-scroll is off and scrolling happens via wheel + scrollbar.
  Offset? _pressPos;
  bool _didDrag = false;

  void _onPointerDown(PointerDownEvent d, TrackInfo track) {
    if (d.buttons != kPrimaryButton) return;
    final content = Offset(_contentDx(d.localPosition.dx), d.localPosition.dy);
    _pressPos = content;
    _didDrag = false;
    _dragIndex = _hitPoint(_pointsOf(track), content);
  }

  void _onPointerMove(PointerMoveEvent d, TrackInfo track) {
    if (_dragIndex == null || d.buttons & kPrimaryButton == 0) return;
    if (_dragIndex! >= _pointsOf(track).length) return;
    final points = List.of(_pointsOf(track));
    final time = (_contentDx(d.localPosition.dx) / _pps / _snapSeconds)
            .round() *
        _snapSeconds;
    points[_dragIndex!] = AutomationPointInfo(
        time: time.clamp(0.0, double.infinity),
        value: _valueAt(d.localPosition.dy, _laneHeight));
    _preview = points;
    _didDrag = true;
    setState(() {});
  }

  void _onPointerUp(PointerUpEvent d, TrackInfo track) {
    final content = _pressPos;
    _pressPos = null;
    if (_dragIndex != null) {
      if (_didDrag) {
        final points = List.of(_preview);
        _preview = const [];
        _commit(track, points);
      }
      _dragIndex = null;
      _didDrag = false;
      setState(() {});
      return;
    }
    // Click (no drag) on empty space: add a point there.
    if (content != null) {
      final points = List.of(_pointsOf(track));
      final time = (content.dx / _pps / _snapSeconds).round() * _snapSeconds;
      points.add(AutomationPointInfo(
          time: time, value: _valueAt(content.dy, _laneHeight)));
      _commit(track, points);
      setState(() {});
    }
  }

  void _onPointerCancel(PointerCancelEvent d) {
    _pressPos = null;
    _didDrag = false;
    _dragIndex = null;
    _preview = const [];
    setState(() {});
  }

  /// Wheel / trackpad scroll (drag-to-scroll is off).
  void _onPointerSignal(PointerSignalEvent event) {
    if (event is! PointerScrollEvent) return;
    final dx = event.scrollDelta.dx;
    if (dx != 0 && _hScroll.hasClients) {
      _hScroll.jumpTo(
          (_hScroll.offset + dx).clamp(0.0, _hScroll.position.maxScrollExtent));
    }
  }

  void _onSecondaryTapDown(TapDownDetails d, TrackInfo track) {
    final content = Offset(_contentDx(d.localPosition.dx), d.localPosition.dy);
    final points = List.of(_pointsOf(track));
    final hit = _hitPoint(points, content);
    if (hit == null) return;
    points.removeAt(hit);
    _commit(track, points);
    setState(() {});
  }

  String _dropdownLaneValue(TrackInfo track) {
    if (_laneKey.startsWith('p:') && _paramLaneOf(track, _laneKey) != null) {
      return _laneKey;
    }
    return _laneKey;
  }

  /// Two-step chooser: pick a plugin slot, then one of its parameters. The
  /// lane starts empty and fills as you draw.
  Future<void> _openParamChooser(TrackInfo track) async {
    final slots = <(int, int, String)>[
      for (var i = 0; i < track.fxNames.length; i++)
        (0, i, track.fxNames[i]),
      if (track.type == 1 && track.instrumentName.isNotEmpty)
        (1, 0, track.instrumentName),
    ];
    if (slots.isEmpty) {
      showAppError(context, context.l10n.automationAddPluginHint);
      return;
    }

    final picked = await showDialog<(int, int, int, String)>(
      context: context,
      builder: (dialogContext) {
        int? chosenSlotKind;
        int? chosenSlotIndex;
        String? chosenSlotName;
        return StatefulBuilder(
          builder: (dialogContext, setDialogState) => AlertDialog(
            backgroundColor: const Color(0xFF252830),
            title: Text(
                chosenSlotKind == null
                    ? context.l10n.automationChoosePlugin
                    : context.l10n.automationParams(chosenSlotName!),
                style: const TextStyle(fontSize: 15)),
            content: SizedBox(
              width: 320,
              height: 320,
              child: chosenSlotKind == null
                  ? ListView.builder(
                      itemCount: slots.length,
                      itemBuilder: (context, i) {
                        final (kind, slot, name) = slots[i];
                        return ListTile(
                          dense: true,
                          leading: Icon(
                              kind == 1 ? Icons.piano : Icons.graphic_eq,
                              size: 18,
                              color: const Color(0xFF4D9DF7)),
                          title: Text(name,
                              style: const TextStyle(fontSize: 12)),
                          onTap: () {
                            setDialogState(() {
                              chosenSlotKind = kind;
                              chosenSlotIndex = slot;
                              chosenSlotName = name;
                            });
                          },
                        );
                      },
                    )
                  : ListView.builder(
                      itemCount: widget.service.pluginParamCount(
                          track.id, chosenSlotKind!, chosenSlotIndex!),
                      itemBuilder: (context, i) {
                        final name = widget.service.pluginParamName(
                            track.id, chosenSlotKind!, chosenSlotIndex!, i);
                        return ListTile(
                          dense: true,
                          title: Text(name,
                              style: const TextStyle(fontSize: 12)),
                          onTap: () {
                            Navigator.pop(dialogContext,
                                (chosenSlotKind!, chosenSlotIndex!, i, name));
                          },
                        );
                      },
                    ),
            ),
            actions: [
              TextButton(
                  onPressed: () => Navigator.pop(dialogContext),
                  child: Text(context.l10n.automationCancel)),
            ],
          ),
        );
      },
    );

    if (picked == null) return;
    final (slotKind, slotIndex, paramIndex, name) = picked;
    widget.service.setTrackParamAutomation(
        track.id, slotKind, slotIndex, paramIndex, name, const []);
    setState(() {
      _laneKey = 'p:$slotKind:$slotIndex:$paramIndex';
    });
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: widget.service,
      builder: (context, _) {
        final track = _track();
        final duration = widget.service.snapshot.durationSeconds;
        final width = ((duration + 8) * _pps).clamp(600.0, double.infinity);

        return Container(
          height: widget.height,
          color: const Color(0xFF16171B),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Container(
                height: 26,
                padding: const EdgeInsets.symmetric(horizontal: 10),
                color: const Color(0xFF1E2026),
                child: Row(children: [
                  const Icon(Icons.show_chart,
                      size: 14, color: Color(0xFF4D9DF7)),
                  const SizedBox(width: 6),
                  Text(
                    track == null
                        ? context.l10n.automationTitleNoTrack
                        : context.l10n.automationTitle(track.name),
                    style: const TextStyle(
                        fontSize: 11, color: Color(0xFFE9EBF0)),
                  ),
                  const SizedBox(width: 10),
                  if (track != null)
                    DropdownButton<String>(
                      value: _dropdownLaneValue(track),
                      dropdownColor: const Color(0xFF252830),
                      underline: const SizedBox(),
                      style: const TextStyle(
                          fontSize: 11, color: Color(0xFFE9EBF0)),
                      items: [
                        DropdownMenuItem(
                            value: 'vol',
                            child: Text(context.l10n.automationVolume)),
                        DropdownMenuItem(
                            value: 'pan',
                            child: Text(context.l10n.automationPan)),
                        DropdownMenuItem(
                            value: 'sus',
                            child: Text(context.l10n.automationSustain)),
                        for (final lane in track.paramLanes)
                          DropdownMenuItem(
                              value:
                                  'p:${lane.slotKind}:${lane.slotIndex}:${lane.paramIndex}',
                              child: Text(lane.name,
                                  overflow: TextOverflow.ellipsis)),
                      ],
                      onChanged: (v) {
                        if (v == null) return;
                        if (v == 'add') {
                          _openParamChooser(track);
                          return;
                        }
                        setState(() => _laneKey = v);
                      },
                    ),
                  IconButton(
                    tooltip: context.l10n.automationAddParamLane,
                    visualDensity: VisualDensity.compact,
                    icon: const Icon(Icons.add,
                        size: 16, color: Color(0xFF4D9DF7)),
                    onPressed:
                        track == null ? null : () => _openParamChooser(track),
                  ),
                  const Spacer(),
                  // Write mode (§7.12 v1.5): with W armed, fader moves write
                  // automation points at the playhead.
                  GestureDetector(
                    onTap: () {
                      widget.service.automationWrite =
                          !widget.service.automationWrite;
                      setState(() {});
                    },
                    child: Container(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 8, vertical: 2),
                      decoration: BoxDecoration(
                        color: widget.service.automationWrite
                            ? const Color(0xFFE5484D)
                            : const Color(0xFF2C303A),
                        borderRadius: BorderRadius.circular(3),
                        border: Border.all(
                            color: widget.service.automationWrite
                                ? const Color(0xFFE5484D)
                                : const Color(0xFF3E4350)),
                      ),
                      child: Text('W',
                          style: TextStyle(
                              fontSize: 11,
                              fontWeight: FontWeight.w700,
                              color: widget.service.automationWrite
                                  ? Colors.white
                                  : const Color(0xFF9BA1AE))),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      context.l10n.automationHint,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                          fontSize: 9, color: Color(0xFF5C6270)),
                    ),
                  ),
                  IconButton(
                    tooltip: context.l10n.automationUndock,
                    visualDensity: VisualDensity.compact,
                    icon: const Icon(Icons.open_in_new, size: 14,
                        color: Color(0xFF9BA1AE)),
                    onPressed: (track == null || widget.onUndock == null)
                        ? null
                        : widget.onUndock,
                  ),
                ]),
              ),
              Expanded(
                child: track == null
                    ? Center(
                        child: Text(
                            context.l10n.automationNoSelection,
                            style: const TextStyle(
                                fontSize: 11, color: Color(0xFF5C6270))))
                    : Column(children: [
                        // Bar ruler, pinned above the lane, in sync with the
                        // lane's h-scroll (same pattern as the piano roll).
                        Container(
                          height: 16,
                          color: const Color(0xFF14161A),
                          child: ClipRect(
                            child: AnimatedBuilder(
                              animation: _hScroll,
                              builder: (context, _) => CustomPaint(
                                size: Size.infinite,
                                painter: _LaneRulerPainter(
                                  barSeconds:
                                      widget.service.snapshot.barSeconds,
                                  beatSeconds:
                                      widget.service.snapshot.beatSeconds,
                                  pixelsPerSecond: _pps,
                                  offsetX: _hScroll.hasClients
                                      ? _hScroll.offset
                                      : 0,
                                ),
                              ),
                            ),
                          ),
                        ),
                        Expanded(
                          child: LayoutBuilder(builder: (context, constraints) {
                            _laneHeight = constraints.maxHeight;
                            final points = _pointsOf(track);
                            // Raw pointer events (no gesture arena) + right
                            // click via GestureDetector; scroll = wheel +
                            // the bottom scrollbar.
                            return Column(children: [
                              Expanded(
                                child: Listener(
                                  onPointerDown: (d) =>
                                      _onPointerDown(d, track),
                                  onPointerMove: (d) =>
                                      _onPointerMove(d, track),
                                  onPointerUp: (d) => _onPointerUp(d, track),
                                  onPointerCancel: _onPointerCancel,
                                  onPointerSignal: _onPointerSignal,
                                  child: GestureDetector(
                                    onSecondaryTapDown: (d) =>
                                        _onSecondaryTapDown(d, track),
                                    child: SingleChildScrollView(
                                      scrollDirection: Axis.horizontal,
                                      controller: _hScroll,
                                      physics:
                                          const NeverScrollableScrollPhysics(),
                                      child: SizedBox(
                                        width: width,
                                        height: _laneHeight,
                                        child: CustomPaint(
                                          size: Size(width, _laneHeight),
                                          painter: _LanePainter(
                                            points: points,
                                            color: trackColor(
                                                track.colorIndex),
                                            service: widget.service,
                                            height: _laneHeight,
                                            stepMode: _laneKey == 'sus',
                                            repaint: widget.frameTick,
                                          ),
                                        ),
                                      ),
                                    ),
                                  ),
                                ),
                              ),
                              // Horizontal scrollbar (thumb drag).
                              SizedBox(
                                height: 14,
                                child: Scrollbar(
                                  controller: _hScroll,
                                  thumbVisibility: true,
                                  thickness: 10,
                                  interactive: true,
                                  child: const SizedBox(),
                                ),
                              ),
                            ]);
                          }),
                        ),
                      ]),
              ),
            ],
          ),
        );
      },
    );
  }
}

/// Bar ruler over the automation lane (same style as the piano roll's).
class _LaneRulerPainter extends CustomPainter {
  _LaneRulerPainter({
    required this.barSeconds,
    required this.beatSeconds,
    required this.pixelsPerSecond,
    required this.offsetX,
  });

  final double barSeconds;
  final double beatSeconds;
  final double pixelsPerSecond;
  final double offsetX;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(
        Offset.zero & size, Paint()..color = const Color(0xFF14161A));
    if (barSeconds <= 0 || beatSeconds <= 0) return;

    final beatTick = Paint()
      ..color = const Color(0xFF2A2D35)
      ..strokeWidth = 1;
    final barTick = Paint()
      ..color = const Color(0xFF3E4350)
      ..strokeWidth = 1;
    const style = TextStyle(fontSize: 8, color: Color(0xFF9BA1AE));

    final t0 = offsetX / pixelsPerSecond;
    final t1 = (offsetX + size.width) / pixelsPerSecond;

    for (var b = (t0 / beatSeconds).floor(); b * beatSeconds <= t1; b++) {
      if (b < 0) continue;
      final x = b * beatSeconds * pixelsPerSecond - offsetX + 0.5;
      canvas.drawLine(Offset(x, size.height - 4), Offset(x, size.height), beatTick);
    }
    for (var bar = (t0 / barSeconds).floor(); bar * barSeconds <= t1; bar++) {
      if (bar < 0) continue;
      final x = bar * barSeconds * pixelsPerSecond - offsetX + 0.5;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), barTick);
      final tp = TextPainter(
        text: TextSpan(text: '${bar + 1}', style: style),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(x + 3, 2));
    }
  }

  @override
  bool shouldRepaint(_LaneRulerPainter oldDelegate) =>
      oldDelegate.offsetX != offsetX ||
      oldDelegate.barSeconds != barSeconds ||
      oldDelegate.beatSeconds != beatSeconds;
}

class _LanePainter extends CustomPainter {
  _LanePainter({
    required this.points,
    required this.color,
    required this.service,
    required this.height,
    required ValueListenable<int> repaint,
    this.stepMode = false,
  }) : super(repaint: repaint);

  final List<AutomationPointInfo> points;
  final Color color;
  final EngineService service;
  final double height;
  final bool stepMode; // on/off lanes draw square steps (sustain pedal)

  double _yFor(double value) =>
      (1 - value.clamp(0.0, 1.0)) * (height - 20) + 10;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(
        Offset.zero & size, Paint()..color = const Color(0xFF191B20));

    final grid = Paint()
      ..color = const Color(0xFF2A2D35)
      ..strokeWidth = 1;
    for (var b = 0; b * 0.5 * _kPps < size.width; b++) {
      final x = b * 0.5 * _kPps + 0.5;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), grid);
    }
    canvas.drawLine(Offset(0, _yFor(1.0)), Offset(size.width, _yFor(1.0)),
        grid..color = const Color(0xFF3E4350));
    canvas.drawLine(Offset(0, _yFor(0.0)), Offset(size.width, _yFor(0.0)),
        grid..color = const Color(0xFF3E4350));

    if (points.isEmpty) {
      final hint = TextPainter(
        text: TextSpan(
            text: L10n.current.automationEmpty,
            style: const TextStyle(fontSize: 10, color: Color(0xFF5C6270))),
        textDirection: TextDirection.ltr,
      );
      hint.layout();
      hint.paint(canvas, const Offset(8, 8));
      _drawPlayhead(canvas, size);
      return;
    }

    final line = Paint()
      ..color = color
      ..strokeWidth = 2;
    final dot = Paint()..color = color;

    if (stepMode) {
      // Square steps: each value holds until the next point, then jumps.
      Offset? previous;
      for (final p in points) {
        final center = Offset(p.time * _kPps, _yFor(p.value));
        if (previous != null) {
          canvas.drawLine(previous, Offset(center.dx, previous.dy), line);
          canvas.drawLine(Offset(center.dx, previous.dy), center, line);
        }
        canvas.drawCircle(center, 4, dot);
        previous = center;
      }
      if (previous != null) {
        canvas.drawLine(previous,
            Offset(size.width, previous.dy), line);
      }
      _drawPlayhead(canvas, size);
      return;
    }

    Offset? previousLin;
    for (final p in points) {
      final center = Offset(p.time * _kPps, _yFor(p.value));
      if (previousLin != null) canvas.drawLine(previousLin, center, line);
      canvas.drawCircle(center, 4, dot);
      previousLin = center;
    }

    _drawPlayhead(canvas, size);
  }

  void _drawPlayhead(Canvas canvas, Size size) {
    // Read the position HERE: a value captured at construction would be
    // stale forever — repaints between rebuilds re-paint the same painter.
    final playheadX = service.positionSeconds * _kPps;
    if (playheadX < 0 || playheadX > size.width) return;
    canvas.drawLine(
        Offset(playheadX, 0),
        Offset(playheadX, size.height),
        Paint()
          ..color = const Color(0xFFE5484D)
          ..strokeWidth = 1);
  }

  @override
  bool shouldRepaint(_LanePainter oldDelegate) =>
      oldDelegate.points != points ||
      oldDelegate.service != service ||
      oldDelegate.color != color;
}

const double _kPps = 90;
