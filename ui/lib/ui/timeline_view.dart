// Timeline v3 — track headers (§7.2), bar ruler, move/trim/fade gestures,
// track selection with context menu.
//
// Gestures: tap = select clip, drag body = move, edge drag = trim, corner
// drag (selected clip) = fades. Snap to beat, ⌘ bypasses. Each completed
// gesture commits one engine command (one undo entry).

import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../engine/engine_service.dart';
import '../engine/project_snapshot.dart';
import 'add_track_dialog.dart' show showAddTrackDialog;

const double kPixelsPerSecond = 90;
const double kTrackRowHeight = 64;
const double kRulerHeight = 26;
const double kTimelineWidthSeconds = 60;
const double kHeaderWidth = 220;
const double kEdgeHotZonePx = 6;
const double kFadeHandleSize = 10;
const double kMinClipSeconds = 0.05;

const double kSnapBeatSeconds = 0.5; // 120 bpm; tempo track lands in v1.5
const double kSecondsPerBar = 2.0;

enum ClipEdge { move, left, right }

({ClipInfo clip, ClipEdge edge})? hitTestClipEdge(
    ProjectSnapshot snapshot, Offset local,
    {double pixelsPerSecond = kPixelsPerSecond}) {
  final row = ((local.dy - kRulerHeight) / kTrackRowHeight).floor();
  if (row < 0 || row >= snapshot.tracks.length) return null;
  final seconds = local.dx / pixelsPerSecond;
  for (final clip in snapshot.tracks[row].clips) {
    if (seconds >= clip.start && seconds <= clip.end) {
      final leftPx = clip.start * pixelsPerSecond;
      final rightPx = clip.end * pixelsPerSecond;
      if (local.dx - leftPx <= kEdgeHotZonePx) return (clip: clip, edge: ClipEdge.left);
      if (rightPx - local.dx <= kEdgeHotZonePx) return (clip: clip, edge: ClipEdge.right);
      return (clip: clip, edge: ClipEdge.move);
    }
  }
  return null;
}

/// Fade-handle hot zone at the selected clip's top corners (§7.3).
/// Returns the clip id and whether it is the fade-in ('in') or out ('out').
(int, bool)? hitTestFadeHandle(
    ProjectSnapshot snapshot, int? selectedClipId, Offset local,
    {double pixelsPerSecond = kPixelsPerSecond}) {
  if (selectedClipId == null) return null;
  final row = ((local.dy - kRulerHeight) / kTrackRowHeight).floor();
  if (row < 0 || row >= snapshot.tracks.length) return null;
  for (final clip in snapshot.tracks[row].clips) {
    if (clip.id != selectedClipId) continue;
    final top = kRulerHeight + row * kTrackRowHeight + 22;
    final leftPx = clip.start * pixelsPerSecond;
    final rightPx = clip.end * pixelsPerSecond;
    final inZone = Rect.fromLTWH(leftPx, top, kFadeHandleSize, kFadeHandleSize);
    final outZone =
        Rect.fromLTWH(rightPx - kFadeHandleSize, top, kFadeHandleSize, kFadeHandleSize);
    if (inZone.contains(local)) return (clip.id, true);
    if (outZone.contains(local)) return (clip.id, false);
    return null;
  }
  return null;
}

int? hitTestClip(ProjectSnapshot snapshot, Offset local,
        {double pixelsPerSecond = kPixelsPerSecond}) =>
    hitTestClipEdge(snapshot, local, pixelsPerSecond: pixelsPerSecond)?.clip.id;

double snapSeconds(double raw, {required bool bypass}) {
  if (bypass) return raw;
  return (raw / kSnapBeatSeconds).roundToDouble() * kSnapBeatSeconds;
}

class TimelineView extends StatefulWidget {
  const TimelineView({
    super.key,
    required this.service,
    required this.frameTick,
    this.onOpenMidiEditor,
  });

  final EngineService service;
  final ValueListenable<int> frameTick;
  final void Function(int clipId)? onOpenMidiEditor;

  @override
  State<TimelineView> createState() => _TimelineViewState();
}

class _TimelineViewState extends State<TimelineView> {
  final FocusNode _focusNode = FocusNode();
  final ScrollController _hScroll = ScrollController();
  double _pps = kPixelsPerSecond;

  int? _dragClipId;
  ClipEdge _dragEdge = ClipEdge.move;
  bool _dragFadeIn = false;
  double _origStart = 0, _origLength = 0, _origOffset = 0, _maxEnd = 0;
  double _origFade = 0;
  double _previewStart = 0, _previewLength = 0, _previewFade = -1;
  bool _didMove = false;

  bool get _snapBypassed =>
      HardwareKeyboard.instance.isLogicalKeyPressed(LogicalKeyboardKey.metaLeft) ||
      HardwareKeyboard.instance.isLogicalKeyPressed(LogicalKeyboardKey.metaRight);

  ClipInfo? _findClip(int clipId) {
    for (final track in widget.service.snapshot.tracks) {
      for (final clip in track.clips) {
        if (clip.id == clipId) return clip;
      }
    }
    return null;
  }

  void _onTapUp(TapUpDetails details) {
    widget.service.selectClip(
        hitTestClip(widget.service.snapshot, details.localPosition, pixelsPerSecond: _pps));
  }

  /// ⌘+wheel zooms around the view centre; plain wheel scrolls horizontally.
  void _onPointerSignal(PointerSignalEvent event) {
    if (event is PointerScrollEvent) {
      if (_snapBypassed) {
        setState(() {
          _pps = (_pps * (event.scrollDelta.dy < 0 ? 1.15 : 1 / 1.15))
              .clamp(12.0, 600.0);
        });
      } else if (_hScroll.hasClients) {
        final target =
            (_hScroll.position.pixels + event.scrollDelta.dy).clamp(0.0, _hScroll.position.maxScrollExtent);
        _hScroll.position.jumpTo(target);
      }
    }
  }

  void _onPanStart(DragStartDetails details) {
    final snapshot = widget.service.snapshot;

    // Fade handles take priority (selected clip only).
    final fadeHit = hitTestFadeHandle(
        snapshot, widget.service.selectedClipId, details.localPosition,
        pixelsPerSecond: _pps);
    if (fadeHit != null) {
      final clip = _findClip(fadeHit.$1);
      if (clip != null) {
        _dragClipId = clip.id;
        _dragEdge = ClipEdge.move;
        _dragFadeIn = fadeHit.$2;
        _origFade = _dragFadeIn ? clip.fadeIn : clip.fadeOut;
        _previewFade = _origFade;
        _didMove = false;
        return;
      }
    }

    final hit = hitTestClipEdge(snapshot, details.localPosition, pixelsPerSecond: _pps);
    if (hit == null) {
      widget.service.selectClip(null);
      return;
    }
    widget.service.selectClip(hit.clip.id);
    _dragClipId = hit.clip.id;
    _dragEdge = hit.edge;
    _dragFadeIn = false;
    _origStart = hit.clip.start;
    _origLength = hit.clip.length;
    _origOffset = hit.clip.offset;
    _maxEnd = hit.clip.start + hit.clip.maxLength;
    _previewStart = hit.clip.start;
    _previewLength = hit.clip.length;
    _previewFade = -1;
    _didMove = false;
  }

  void _onPanUpdate(DragUpdateDetails details) {
    if (_dragClipId == null) return;
    final deltaSeconds = details.delta.dx / _pps;
    const minLen = kMinClipSeconds;

    if (_previewFade >= 0) {
      _previewFade =
          (_origFade + deltaSeconds).clamp(0.0, _origLength).toDouble();
      _didMove = _previewFade != _origFade;
      setState(() {});
      return;
    }

    switch (_dragEdge) {
      case ClipEdge.move:
        _previewStart = snapSeconds(
            (_origStart + deltaSeconds).clamp(0.0, double.maxFinite).toDouble(),
            bypass: _snapBypassed);
        _previewLength = _origLength;
      case ClipEdge.left:
        final minStart = _origStart - _origOffset > 0 ? _origStart - _origOffset : 0.0;
        final maxStart = _origStart + _origLength - minLen;
        final raw = snapSeconds(_origStart + deltaSeconds, bypass: _snapBypassed);
        _previewStart = raw.clamp(minStart, maxStart).toDouble();
        _previewLength = _origLength + (_origStart - _previewStart);
      case ClipEdge.right:
        final minEnd = _origStart + minLen;
        final raw = snapSeconds(_origStart + _origLength + deltaSeconds,
            bypass: _snapBypassed);
        final end = raw.clamp(minEnd, _maxEnd > minEnd ? _maxEnd : minEnd).toDouble();
        _previewStart = _origStart;
        _previewLength = end - _origStart;
    }
    if (_previewStart != _origStart || _previewLength != _origLength) {
      _didMove = true;
    }
    setState(() {});
  }

  void _onPanEnd(DragEndDetails details) {
    final clipId = _dragClipId;
    if (clipId != null && _didMove) {
      if (_previewFade >= 0) {
        // Fade drag: one command, one undo entry.
        final clip = _findClip(clipId);
        if (clip != null) {
          widget.service.setClipFades(
            clipId,
            _dragFadeIn ? _previewFade : clip.fadeIn,
            _dragFadeIn ? clip.fadeOut : _previewFade,
          );
        }
      } else {
        switch (_dragEdge) {
          case ClipEdge.move:
            widget.service.moveClip(clipId, _previewStart);
          case ClipEdge.left || ClipEdge.right:
            widget.service.trimClip(clipId, _previewStart,
                _origOffset + (_origStart - _previewStart), _previewLength);
        }
      }
    }
    _dragClipId = null;
    _didMove = false;
    _previewFade = -1;
    setState(() {});
  }

  void _onKeyEvent(KeyEvent event) {
    if (event is! KeyDownEvent) return;
    final key = event.logicalKey;
    if (key == LogicalKeyboardKey.delete || key == LogicalKeyboardKey.backspace) {
      widget.service.deleteSelectedClip();
    }
  }

  @override
  void dispose() {
    _focusNode.dispose();
    _hScroll.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: widget.service,
      builder: (context, _) {
        final snapshot = widget.service.snapshot;
        final seconds = snapshot.durationSeconds < kTimelineWidthSeconds
            ? kTimelineWidthSeconds
            : snapshot.durationSeconds + 4;
        final width = seconds * _pps;
        final rowsHeight = snapshot.tracks.length * kTrackRowHeight;

        return KeyboardListener(
          focusNode: _focusNode,
          autofocus: true,
          onKeyEvent: _onKeyEvent,
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              SizedBox(
                width: kHeaderWidth,
                child: Column(
                  children: [
                    Container(
                      height: kRulerHeight,
                      color: const Color(0xFF1E2026),
                      alignment: Alignment.centerLeft,
                      padding: const EdgeInsets.only(left: 12),
                      child: const Text('Tracks',
                          style: TextStyle(
                              fontSize: 10, color: Color(0xFF9BA1AE))),
                    ),
                    for (var i = 0; i < snapshot.tracks.length; i++)
                      _TrackHeader(
                        track: snapshot.tracks[i],
                        service: widget.service,
                        isEvenRow: i.isEven,
                        selected: widget.service.selectedTrackId == snapshot.tracks[i].id,
                      ),
                    SizedBox(
                      height: 34,
                      child: GestureDetector(
                        onTap: () async {
                          final result = await showAddTrackDialog(
                              context, snapshot.tracks.length);
                          if (result != null) {
                            widget.service.addTrack(result.name, result.colorIndex,
                                type: result.type);
                          }
                        },
                        child: Container(
                          margin: const EdgeInsets.all(6),
                          alignment: Alignment.center,
                          decoration: BoxDecoration(
                            color: const Color(0xFF2C303A),
                            borderRadius: BorderRadius.circular(5),
                            border: Border.all(color: const Color(0xFF3E4350)),
                          ),
                          child: const Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              Icon(Icons.add, size: 14, color: Color(0xFFE9EBF0)),
                              SizedBox(width: 4),
                              Text('Añadir pista',
                                  style: TextStyle(
                                      fontSize: 11, color: Color(0xFFE9EBF0))),
                            ],
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
              const VerticalDivider(width: 1, color: Color(0xFF32363F)),
              Expanded(
                child: Listener(
                  onPointerSignal: _onPointerSignal,
                  child: GestureDetector(
                  onTapUp: _onTapUp,
                  onPanStart: _onPanStart,
                  onPanUpdate: _onPanUpdate,
                  onPanEnd: _onPanEnd,
                  child: Container(
                    color: const Color(0xFF16171B),
                    child: SingleChildScrollView(
                      controller: _hScroll,
                      child: SizedBox(
                        width: width,
                        height: rowsHeight + kRulerHeight + 40,
                        child: Stack(
                          children: [
                            CustomPaint(
                              size: Size(width, rowsHeight + kRulerHeight + 40),
                              painter: _TimelinePainter(
                                snapshot: snapshot,
                                selectedClipId: widget.service.selectedClipId,
                                dragClipId: _dragClipId,
                                previewStart: _previewStart,
                                previewLength: _previewLength,
                                previewFade: _previewFade,
                                previewFadeIsIn: _dragFadeIn,
                                pixelsPerSecond: _pps,
                                peaksFor: (clip) => widget.service
                                    .getClipPeaks(clip, pixelsPerSecond: _pps),
                              ),
                            ),
                            RepaintBoundary(
                              child: CustomPaint(
                                size: Size(width, rowsHeight + kRulerHeight + 40),
                                painter: _PlayheadPainter(
                                    service: widget.service,
                                    pixelsPerSecond: _pps,
                                    repaint: widget.frameTick),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                  ),
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

// ---------- Track header ----------

class _TrackHeader extends StatelessWidget {
  const _TrackHeader({
    required this.track,
    required this.service,
    required this.isEvenRow,
    required this.selected,
  });

  final TrackInfo track;
  final EngineService service;
  final bool isEvenRow;
  final bool selected;

  Future<void> _showMenu(BuildContext context, Offset position) async {
    final overlay =
        Overlay.of(context).context.findRenderObject() as RenderBox?;
    final value = await showMenu<String>(
      context: context,
      position: RelativeRect.fromLTRB(
          position.dx, position.dy, overlay?.size.width ?? 0, overlay?.size.height ?? 0),
      items: [
        PopupMenuItem(value: 'rename', child: const Text('Rename…')),
        PopupMenuItem(value: 'color', child: const Text('Color…')),
        PopupMenuItem(
            value: 'input',
            child: Text(track.inputChannel == 0 ? 'Input: Left' : 'Input: Right')),
        PopupMenuItem(value: 'mute', child: const Text('Mute')),
        PopupMenuItem(value: 'solo', child: const Text('Solo')),
        PopupMenuItem(value: 'delete', child: const Text('Delete track')),
      ],
    );
    if (!context.mounted) return;
    switch (value) {
      case 'rename':
        await _renameDialog(context);
      case 'color':
        await _colorDialog(context);
      case 'input':
        service.setTrackInputChannel(track.id, track.inputChannel == 0 ? 1 : 0);
      case 'mute':
        service.setTrackMute(track.id, !track.mute);
      case 'solo':
        service.setTrackSolo(track.id, !track.solo);
      case 'delete':
        service.removeTrack(track.id);
    }
  }

  Future<void> _renameDialog(BuildContext context) async {
    final controller = TextEditingController(text: track.name);
    final name = await showDialog<String>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Rename track'),
        content: TextField(controller: controller, autofocus: true),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(context), child: const Text('Cancel')),
          TextButton(
              onPressed: () => Navigator.pop(context, controller.text),
              child: const Text('OK')),
        ],
      ),
    );
    if (name != null && name.isNotEmpty) service.setTrackName(track.id, name);
  }

  Future<void> _colorDialog(BuildContext context) async {
    await showDialog<void>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Track color'),
        content: SizedBox(
          width: 260,
          child: Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              for (var i = 0; i < kTrackPalette.length; i++)
                GestureDetector(
                  onTap: () {
                    service.setTrackColor(track.id, i);
                    Navigator.pop(context);
                  },
                  child: Container(
                    width: 26,
                    height: 26,
                    decoration: BoxDecoration(
                      color: kTrackPalette[i],
                      shape: BoxShape.circle,
                      border: Border.all(
                          color: i == track.colorIndex
                              ? Colors.white
                              : Colors.transparent,
                          width: 2),
                    ),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final color = trackColor(track.colorIndex);
    return GestureDetector(
      onTap: () => service.selectTrack(track.id),
      onSecondaryTapUp: (details) => _showMenu(context, details.globalPosition),
      child: Container(
        height: kTrackRowHeight,
        decoration: BoxDecoration(
          color: isEvenRow ? const Color(0xFF1B1D22) : const Color(0xFF1E2026),
          border: track.armed
              ? Border.all(color: const Color(0xFFE5484D), width: 1)
              : selected
                  ? Border.all(color: const Color(0xFF4D9DF7), width: 1)
                  : null,
        ),
        child: Row(
          children: [
            Container(width: 4, color: color),
            Expanded(
              child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 8),
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        Icon(
                            track.type == 1 ? Icons.piano : Icons.mic,
                            size: 12,
                            color: const Color(0xFF9BA1AE)),
                        const SizedBox(width: 4),
                        Expanded(
                          child: Text(
                            track.name,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(
                                color: Color(0xFFE9EBF0),
                                fontSize: 12,
                                fontWeight: FontWeight.w500),
                          ),
                        ),
                      ],
                    ),
                    SizedBox(
                      height: 24,
                      child: Row(
                        children: [
                          _MSButton(
                            label: 'M',
                            activeColor: const Color(0xFFF5A524),
                            active: track.mute,
                            onTap: () =>
                                service.setTrackMute(track.id, !track.mute),
                          ),
                          const SizedBox(width: 4),
                          _MSButton(
                            label: 'S',
                            activeColor: const Color(0xFF4D9DF7),
                            active: track.solo,
                            onTap: () =>
                                service.setTrackSolo(track.id, !track.solo),
                          ),
                          const SizedBox(width: 4),
                          _MSButton(
                            label: 'R',
                            activeColor: const Color(0xFFE5484D),
                            active: track.armed,
                            onTap: () =>
                                service.setTrackArmed(track.id, !track.armed),
                          ),
                          const SizedBox(width: 6),
                          Expanded(child: _CompactSlider(
                            value: track.volume.clamp(0.0, 2.0).toDouble(),
                            min: 0,
                            max: 2,
                            onChangeEnd: (v) => service.setTrackVolume(track.id, v),
                          )),
                        ],
                      ),
                    ),
                    SizedBox(
                      height: 18,
                      child: Row(children: [
                        const Text('L',
                            style: TextStyle(
                                fontSize: 8, color: Color(0xFF5C6270))),
                        Expanded(
                          child: _CompactSlider(
                            value: track.pan.clamp(-1.0, 1.0).toDouble(),
                            min: -1,
                            max: 1,
                            onChangeEnd: (v) => service.setTrackPan(track.id, v),
                          ),
                        ),
                        const Text('R',
                            style: TextStyle(
                                fontSize: 8, color: Color(0xFF5C6270))),
                      ]),
                    ),
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Slider that only notifies on release: one drag = one undo entry.
class _CompactSlider extends StatefulWidget {
  const _CompactSlider({
    required this.value,
    required this.min,
    required this.max,
    required this.onChangeEnd,
  });

  final double value;
  final double min;
  final double max;
  final void Function(double) onChangeEnd;

  @override
  State<_CompactSlider> createState() => _CompactSliderState();
}

class _CompactSliderState extends State<_CompactSlider> {
  double? _dragValue;

  @override
  Widget build(BuildContext context) {
    return SliderTheme(
      data: SliderTheme.of(context).copyWith(
        trackHeight: 2,
        thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 5),
        overlayShape: const RoundSliderOverlayShape(overlayRadius: 8),
        padding: EdgeInsets.zero,
      ),
      child: Slider(
        min: widget.min,
        max: widget.max,
        value: _dragValue ?? widget.value,
        onChanged: (value) => setState(() => _dragValue = value),
        onChangeEnd: (value) {
          widget.onChangeEnd(value);
          setState(() => _dragValue = null);
        },
      ),
    );
  }
}

class _MSButton extends StatelessWidget {
  const _MSButton({
    required this.label,
    required this.activeColor,
    required this.active,
    required this.onTap,
  });

  final String label;
  final Color activeColor;
  final bool active;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        width: 22,
        height: 20,
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: active ? activeColor : const Color(0xFF2C303A),
          borderRadius: BorderRadius.circular(4),
          border: Border.all(color: const Color(0xFF3E4350)),
        ),
        child: Text(label,
            style: TextStyle(
                fontSize: 11,
                fontWeight: FontWeight.w600,
                color: active ? Colors.black : const Color(0xFF9BA1AE))),
      ),
    );
  }
}

// ---------- Painting ----------

class _TimelinePainter extends CustomPainter {
  _TimelinePainter({
    required this.snapshot,
    required this.selectedClipId,
    required this.dragClipId,
    required this.previewStart,
    required this.previewLength,
    required this.previewFade,
    required this.previewFadeIsIn,
    required this.pixelsPerSecond,
    required this.peaksFor,
  });

  final ProjectSnapshot snapshot;
  final int? selectedClipId;
  final int? dragClipId;
  final double previewStart;
  final double previewLength;
  final double previewFade; // >= 0 while a fade is being dragged
  final bool previewFadeIsIn;
  final double pixelsPerSecond;
  final PeakData? Function(ClipInfo clip) peaksFor;

  static const _accent = Color(0xFF4D9DF7);

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(
        Offset.zero & Size(size.width, kRulerHeight),
        Paint()..color = const Color(0xFF1E2026));

    final tickPaint = Paint()
      ..color = const Color(0xFF3E4350)
      ..strokeWidth = 1;
    const textStyle = TextStyle(
        color: Color(0xFF9BA1AE), fontSize: 10, fontFamily: 'monospace');
    final bars = (size.width / (kSecondsPerBar * pixelsPerSecond)).ceil();
    for (var bar = 0; bar <= bars; bar++) {
      final x = bar * kSecondsPerBar * pixelsPerSecond + 0.5;
      canvas.drawLine(
          Offset(x, kRulerHeight - 12), Offset(x, kRulerHeight), tickPaint);
      TextPainter(
        text: TextSpan(text: '${bar + 1}', style: textStyle),
        textDirection: TextDirection.ltr,
      )
        ..layout()
        ..paint(canvas, Offset(x + 4, 4));
      for (var beat = 1; beat < 4; beat++) {
        final bx = x + beat * kSnapBeatSeconds * pixelsPerSecond;
        if (bx < size.width) {
          canvas.drawLine(Offset(bx, kRulerHeight - 6),
              Offset(bx, kRulerHeight), tickPaint);
        }
      }
    }

    for (var i = 0; i < snapshot.tracks.length; i++) {
      final track = snapshot.tracks[i];
      final top = kRulerHeight + i * kTrackRowHeight;
      final rowColor =
          i.isEven ? const Color(0xFF1B1D22) : const Color(0xFF1E2026);
      canvas.drawRect(Offset(0, top) & Size(size.width, kTrackRowHeight),
          Paint()..color = rowColor);
      canvas.drawLine(
          Offset(0, top + kTrackRowHeight),
          Offset(size.width, top + kTrackRowHeight),
          Paint()..color = const Color(0xFF23262E));

      final color = trackColor(track.colorIndex);
      for (final clip in track.clips) {
        final dragging = clip.id == dragClipId;
        final start = dragging ? previewStart : clip.start;
        final length = dragging ? previewLength : clip.length;
        final left = start * pixelsPerSecond;
        final clipWidth = (length * pixelsPerSecond).clamp(4.0, size.width).toDouble();
        final rect = Rect.fromLTWH(left, top + 22, clipWidth, kTrackRowHeight - 32);
        final rounded = RRect.fromRectAndRadius(rect, const Radius.circular(4));
        canvas.drawRRect(
            rounded, Paint()..color = color.withValues(alpha: 0.82));

        // Content: waveform for audio clips, mini piano-roll for MIDI.
        if (clip.isMidi) {
          var minP = 127;
          var maxP = 0;
          for (final note in clip.notes) {
            minP = math.min(minP, note.pitch);
            maxP = math.max(maxP, note.pitch);
          }
          if (minP > maxP) {
            minP = 48;
            maxP = 84;
          }
          final range = math.max(1, maxP - minP + 1);
          final slot = rect.height / range;
          final notePaint = Paint()..color = Colors.white.withValues(alpha: 0.8);
          for (final note in clip.notes) {
            final nx = left + note.start * pixelsPerSecond;
            final nw = (note.length * pixelsPerSecond).clamp(2.0, clipWidth);
            final ny = rect.bottom - (note.pitch - minP + 1) * slot;
            canvas.drawRect(
                Rect.fromLTWH(nx, ny, nw, math.max(2.0, slot)), notePaint);
          }
        } else {
          final peaks = peaksFor(clip);
          if (peaks != null) {
            final wavePaint = Paint()
              ..color = Colors.white.withValues(alpha: 0.55)
              ..strokeWidth = 1;
            final center = rect.center.dy;
            final half = rect.height / 2 - 1;
            final n = peaks.mins.length;
            for (var px = 0; px < n; px++) {
              final x = left + px + 0.5;
              if (x > left + clipWidth) break;
              final y1 = center - (peaks.maxs[px] * half).clamp(-half, half);
              final y2 = center - (peaks.mins[px] * half).clamp(-half, half);
              canvas.drawLine(Offset(x, y1), Offset(x, y2), wavePaint);
            }
          }
        }

        // Fade wedges (dark overlay triangle from full height to zero).
        final fadeIn = dragging && previewFade >= 0 && previewFadeIsIn
            ? previewFade
            : clip.fadeIn;
        final fadeOut = dragging && previewFade >= 0 && !previewFadeIsIn
            ? previewFade
            : clip.fadeOut;
        final fadePaint = Paint()..color = const Color(0xE616171B);
        if (fadeIn > 0) {
          final fx = (fadeIn * pixelsPerSecond).clamp(0.0, clipWidth);
          canvas.drawPath(
            Path()
              ..moveTo(left, rect.bottom)
              ..lineTo(left, rect.top)
              ..lineTo(left + fx, rect.top)
              ..close(),
            fadePaint,
          );
        }
        if (fadeOut > 0) {
          final fx = (fadeOut * pixelsPerSecond).clamp(0.0, clipWidth);
          canvas.drawPath(
            Path()
              ..moveTo(left + clipWidth, rect.bottom)
              ..lineTo(left + clipWidth, rect.top)
              ..lineTo(left + clipWidth - fx, rect.top)
              ..close(),
            fadePaint,
          );
        }

        final isSelected = clip.id == selectedClipId || dragging;
        canvas.drawRRect(
            rounded,
            Paint()
              ..style = PaintingStyle.stroke
              ..strokeWidth = isSelected ? 2 : 1
              ..color = isSelected ? _accent : color);

        if (isSelected) {
          // Trim grips.
          final grip = Paint()
            ..color = _accent.withValues(alpha: 0.9)
            ..strokeWidth = 2;
          canvas.drawLine(Offset(left + 3, top + 26),
              Offset(left + 3, top + kTrackRowHeight - 10), grip);
          canvas.drawLine(Offset(left + clipWidth - 3, top + 26),
              Offset(left + clipWidth - 3, top + kTrackRowHeight - 10), grip);
          // Fade handles (top corners, §7.3).
          final handlePaint = Paint()..color = _accent;
          canvas.drawRect(
              Offset(left, rect.top) &
                  const Size(kFadeHandleSize, kFadeHandleSize),
              handlePaint);
          canvas.drawRect(
              Offset(left + clipWidth - kFadeHandleSize, rect.top) &
                  const Size(kFadeHandleSize, kFadeHandleSize),
              handlePaint);
        }
      }
    }
  }

  @override
  bool shouldRepaint(_TimelinePainter oldDelegate) =>
      oldDelegate.snapshot != snapshot ||
      oldDelegate.selectedClipId != selectedClipId ||
      oldDelegate.dragClipId != dragClipId ||
      oldDelegate.previewStart != previewStart ||
      oldDelegate.previewLength != previewLength ||
      oldDelegate.previewFade != previewFade ||
      oldDelegate.previewFadeIsIn != previewFadeIsIn ||
      oldDelegate.pixelsPerSecond != pixelsPerSecond;
}

class _PlayheadPainter extends CustomPainter {
  _PlayheadPainter({
    required this.service,
    required this.pixelsPerSecond,
    required ValueListenable<int> repaint,
  }) : super(repaint: repaint);

  final EngineService service;
  final double pixelsPerSecond;

  @override
  void paint(Canvas canvas, Size size) {
    final x = service.positionSeconds * pixelsPerSecond;

    // Recording ghost clips on armed tracks (§7.11: live draw).
    if (service.isRecording) {
      final x1 = service.recordStartSeconds * pixelsPerSecond;
      final x2 = service.positionSeconds * pixelsPerSecond;
      final recPaint = Paint()..color = const Color(0x59E5484D);
      final tracks = service.snapshot.tracks;
      for (var i = 0; i < tracks.length; i++) {
        if (!tracks[i].armed) continue;
        final top = kRulerHeight + i * kTrackRowHeight + 22;
        canvas.drawRect(
            Rect.fromLTWH(x1, top, (x2 - x1).clamp(2.0, size.width), kTrackRowHeight - 32),
            recPaint);
      }
    }

    final paint = Paint()
      ..color = const Color(0xFF4D9DF7)
      ..strokeWidth = 2;
    canvas.drawLine(Offset(x, 0), Offset(x, size.height), paint);
    canvas.drawCircle(Offset(x, 5), 4, Paint()..color = const Color(0xFF4D9DF7));
  }

  @override
  bool shouldRepaint(_PlayheadPainter oldDelegate) => true;
}
