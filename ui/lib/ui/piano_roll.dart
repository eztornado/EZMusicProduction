// Piano roll v0 (§7.5 subset): view and edit the notes of a MIDI clip.
// Tap empty grid = add note (snapped, one beat long) · drag = move ·
// drag right edge = resize · Delete = remove. Every edit commits one
// engine command (one undo entry).

import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../engine/engine_service.dart';
import '../engine/project_snapshot.dart';

const double kPrPixelsPerSecond = 140;
const double kPrRowHeight = 10;
const int kPrTopPitch = 95; // B7; rows descend to 24 (C1)
const int kPrBottomPitch = 24;
const double kPrKeysWidth = 44;
const double kPrSnapSeconds = 0.25;

int _yToPitch(double dy, double top) => kPrTopPitch - ((dy - top) / kPrRowHeight).floor();

double _pitchToY(int pitch, double top) => top + (kPrTopPitch - pitch) * kPrRowHeight;

double _snapPr(double raw) => (raw / kPrSnapSeconds).roundToDouble() * kPrSnapSeconds;

bool _isBlackKey(int pitch) {
  const set = {1, 3, 6, 8, 10};
  return set.contains(pitch % 12);
}

class PianoRoll extends StatefulWidget {
  const PianoRoll({super.key, required this.service, required this.clipId});

  final EngineService service;
  final int clipId;

  @override
  State<PianoRoll> createState() => _PianoRollState();
}

class _PianoRollState extends State<PianoRoll> {
  final ScrollController _hScroll = ScrollController();
  final ScrollController _vScroll = ScrollController();
  int? _dragNoteIndex;
  bool _resizing = false;
  double _origStart = 0, _origLen = 0;
  int _origPitch = 0;
  double _previewStart = 0, _previewLen = 0;
  int _previewPitch = 0;
  int? _selectedNoteIndex;

  ClipInfo? get _clip {
    for (final track in widget.service.snapshot.tracks) {
      for (final clip in track.clips) {
        if (clip.id == widget.clipId) return clip;
      }
    }
    return null;
  }

  double get _hOffset => _hScroll.hasClients ? _hScroll.offset : 0;
  double get _vOffset => _vScroll.hasClients ? _vScroll.offset : 0;

  void _onPointerDown(PointerDownEvent event, ClipInfo clip) {
    final x = event.localPosition.dx + _hOffset - kPrKeysWidth;
    final seconds = x / kPrPixelsPerSecond;
    final y = event.localPosition.dy + _vOffset;

    // Note hit-test (topmost = last in list).
    for (var i = clip.notes.length - 1; i >= 0; i--) {
      final note = clip.notes[i];
      final left = note.start * kPrPixelsPerSecond;
      final right = left + note.length * kPrPixelsPerSecond;
      if (seconds >= left && seconds <= right) {
        _selectedNoteIndex = i;
        _resizing = seconds >= right - 6;
        _dragNoteIndex = i;
        _origStart = note.start;
        _origLen = note.length;
        _origPitch = _yToPitch(y, 0);
        _previewStart = note.start;
        _previewLen = note.length;
        _previewPitch = note.pitch;
        return;
      }
    }

    // Empty cell: create a snapped one-beat note.
    final pitch = _yToPitch(y, 0).clamp(kPrBottomPitch, kPrTopPitch);
    final start = _snapPr(seconds);
    widget.service.midiClipAddNote(widget.clipId, pitch, 0.8, start, 0.25);
  }

  void _onPanStart(Offset local) {
    final snapshot = widget.service.snapshot;
    ClipInfo? clip;
    for (final track in snapshot.tracks) {
      for (final c in track.clips) {
        if (c.id == widget.clipId) clip = c;
      }
    }
    if (clip == null) return;

    final x = local.dx + _hOffset;
    final seconds = x / kPrPixelsPerSecond;
    for (var i = clip.notes.length - 1; i >= 0; i--) {
      final note = clip.notes[i];
      final left = note.start * kPrPixelsPerSecond;
      final right = left + note.length * kPrPixelsPerSecond;
      if (seconds >= left && seconds <= right) {
        _selectedNoteIndex = i;
        _dragNoteIndex = i;
        _resizing = right - seconds <= 6;
        _origStart = note.start;
        _origLen = note.length;
        _origPitch = note.pitch;
        _grabOffsetSeconds = seconds - note.start;
        _previewStart = note.start;
        _previewLen = note.length;
        _previewPitch = note.pitch;
        _didEdit = false;
        setState(() {});
        return;
      }
    }
    _selectedNoteIndex = null;
    setState(() {});
  }

  void _onPanUpdate(Offset local) {
    if (_dragNoteIndex == null) return;
    final seconds = (local.dx + _hOffset) / kPrPixelsPerSecond;
    final y = local.dy + _vOffset;
    const minLen = 0.05;

    if (_resizing) {
      final end = math.max(_origStart + minLen, _snapPr(seconds));
      _previewStart = _origStart;
      _previewLen = end - _origStart;
    } else {
      _previewStart =
          math.max(0.0, _snapPr(_origStart + seconds - _grabOffsetSeconds));
      _previewLen = _origLen;
      _previewPitch = _yToPitch(y, 0).clamp(kPrBottomPitch, kPrTopPitch);
    }
    _didEdit = _previewStart != _origStart ||
        _previewLen != _origLen ||
        _previewPitch != _origPitch;
    setState(() {});
  }

  void _onPanEnd() {
    final index = _dragNoteIndex;
    if (index != null && _didEdit) {
      widget.service.midiClipSetNote(
          widget.clipId, index, _previewPitch, _previewStart, _previewLen);
    }
    _dragNoteIndex = null;
    _didEdit = false;
    setState(() {});
  }

  double _grabOffsetSeconds = 0;
  bool _didEdit = false;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: widget.service,
      builder: (context, _) {
        final clip = _clip;
        if (clip == null) {
          return const Center(
              child: Text('Clip no disponible',
                  style: TextStyle(color: Color(0xFF9BA1AE))));
        }

        const gridWidth = 30 * kPrPixelsPerSecond;
        final rows = kPrTopPitch - kPrBottomPitch + 1;
        final gridHeight = rows * kPrRowHeight;

        return Column(
          children: [
            // Editor header.
            Container(
              height: 30,
              color: const Color(0xFF1E2026),
              padding: const EdgeInsets.symmetric(horizontal: 10),
              child: Row(
                children: [
                  const Icon(Icons.piano, size: 14, color: Color(0xFF9BA1AE)),
                  const SizedBox(width: 6),
                  Text('Piano roll — ${_clipName()}',
                      style: const TextStyle(
                          fontSize: 11, color: Color(0xFFE9EBF0))),
                  const Spacer(),
                  const Text('click: añadir · arrastrar: mover · borde: largo · Supr: borrar',
                      style: TextStyle(fontSize: 9, color: Color(0xFF5C6270))),
                  IconButton(
                    visualDensity: VisualDensity.compact,
                    icon: const Icon(Icons.close, size: 14,
                        color: Color(0xFF9BA1AE)),
                    onPressed: () => widget.service.closeEditor(),
                  ),
                ],
              ),
            ),
            Expanded(
              child: KeyboardListener(
                focusNode: FocusNode()..requestFocus(),
                onKeyEvent: (event) {
                  if (event is KeyDownEvent &&
                      (event.logicalKey == LogicalKeyboardKey.delete ||
                          event.logicalKey == LogicalKeyboardKey.backspace) &&
                      _selectedNoteIndex != null) {
                    widget.service
                        .midiClipRemoveNote(widget.clipId, _selectedNoteIndex!);
                    _selectedNoteIndex = null;
                  }
                },
                child: Row(
                  children: [
                    // Piano keys (scrolls vertically with the grid).
                    Expanded(
                      child: SingleChildScrollView(
                        controller: _vScroll,
                        child: SizedBox(
                          height: gridHeight,
                          child: CustomPaint(
                            size: Size(kPrKeysWidth, gridHeight),
                            painter: _KeysPainter(
                                topPitch: kPrTopPitch, rows: rows),
                          ),
                        ),
                      ),
                    ),
                    Expanded(
                      child: GestureDetector(
                        onTapDown: (d) {
                          _onPointerDown(
                              PointerDownEvent(position: d.localPosition),
                              clip);
                        },
                        onPanStart: (d) => _onPanStart(d.localPosition),
                        onPanUpdate: (d) => _onPanUpdate(d.localPosition),
                        onPanEnd: (_) => _onPanEnd(),
                        child: SingleChildScrollView(
                          controller: _vScroll,
                          child: SizedBox(
                            width: gridWidth,
                            height: gridHeight,
                            child: CustomPaint(
                              size: Size(gridWidth, gridHeight),
                              painter: _GridPainter(
                                clip: clip,
                                selectedNoteIndex: _selectedNoteIndex,
                              ),
                            ),
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ],
        );
      },
    );
  }

  String _clipName() {
    for (final track in widget.service.snapshot.tracks) {
      for (final clip in track.clips) {
        if (clip.id == widget.clipId) return track.name;
      }
    }
    return '';
  }
}

class _KeysPainter extends CustomPainter {
  _KeysPainter({required this.topPitch, required this.rows});

  final int topPitch;
  final int rows;

  @override
  void paint(Canvas canvas, Size size) {
    for (var i = 0; i < rows; i++) {
      final pitch = topPitch - i;
      final isBlack = _isBlackKey(pitch);
      final rect = Rect.fromLTWH(0, i * kPrRowHeight, size.width, kPrRowHeight);
      canvas.drawRect(
          rect,
          Paint()
            ..color = isBlack ? const Color(0xFF252830) : const Color(0xFFE9EBF0));
      if (pitch % 12 == 0) {
        const style = TextStyle(fontSize: 8, color: Color(0xFF5C6270));
        TextPainter(
          text: TextSpan(text: 'C${pitch ~/ 12 - 1}', style: style),
          textDirection: TextDirection.ltr,
        )
          ..layout()
          ..paint(canvas, Offset(size.width - 16, i * kPrRowHeight + 1));
      }
    }
  }

  @override
  bool shouldRepaint(_KeysPainter oldDelegate) => false;
}

class _GridPainter extends CustomPainter {
  _GridPainter({
    required this.clip,
    required this.selectedNoteIndex,
  });

  final ClipInfo clip;
  final int? selectedNoteIndex;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(Offset.zero & size, Paint()..color = const Color(0xFF191B20));

    // Beat and bar lines.
    final beatLine = Paint()
      ..color = const Color(0xFF2A2D35)
      ..strokeWidth = 1;
    final barLine = Paint()
      ..color = const Color(0xFF3E4350)
      ..strokeWidth = 1;
    for (var b = 0; b * kPrSnapSeconds * kPrPixelsPerSecond < size.width; b++) {
      final x = b * kPrSnapSeconds * kPrPixelsPerSecond + 0.5;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height),
          b % 4 == 0 ? barLine : beatLine);
    }

    // Notes (pitch mapped to rows; black keys shaded).
    for (final note in clip.notes) {
      final left = note.start * kPrPixelsPerSecond;
      final width = (note.length * kPrPixelsPerSecond).clamp(3.0, size.width);
      final top = _pitchToY(note.pitch, 0);
      final rect = Rect.fromLTWH(left, top + 1, width, kPrRowHeight - 2);
      canvas.drawRRect(
          RRect.fromRectAndRadius(rect, const Radius.circular(2)),
          Paint()..color = const Color(0xFF7FB2F0));
    }

    if (selectedNoteIndex != null &&
        selectedNoteIndex! >= 0 &&
        selectedNoteIndex! < clip.notes.length) {
      final note = clip.notes[selectedNoteIndex!];
      final left = note.start * kPrPixelsPerSecond;
      final width = (note.length * kPrPixelsPerSecond).clamp(3.0, size.width);
      final top = _pitchToY(note.pitch, 0);
      canvas.drawRect(
          Rect.fromLTWH(left, top + 1, width, kPrRowHeight - 2),
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeWidth = 2
            ..color = const Color(0xFF4D9DF7));
    }
  }

  @override
  bool shouldRepaint(_GridPainter oldDelegate) =>
      oldDelegate.clip != clip || oldDelegate.selectedNoteIndex != selectedNoteIndex;
}
