// Piano roll v0 (§7.5 subset): view and edit the notes of a MIDI clip.
// Tap empty grid = add note (snapped, one beat long) · drag = move ·
// drag right edge = resize · drag empty grid = select an area · Delete =
// remove. Every edit commits one engine command (one undo entry).

import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../engine/engine_service.dart';
import '../engine/project_snapshot.dart';
import '../l10n/l10n.dart';

const double kPrPixelsPerSecond = 140;
const double kPrRowHeight = 10;
const int kPrTopPitch = 95; // B7; rows descend to 24 (C1)
const int kPrBottomPitch = 24;
const double kPrKeysWidth = 44;
// Snap unit derived from the project tempo: an eighth note.
double _prSnapSeconds(double tempoBpm) => (60.0 / tempoBpm) / 2;

int _yToPitch(double dy, double top) => kPrTopPitch - ((dy - top) / kPrRowHeight).floor();

double _pitchToY(int pitch, double top) => top + (kPrTopPitch - pitch) * kPrRowHeight;

double _snapPr(double raw, double snapSeconds) =>
    (raw / snapSeconds).roundToDouble() * snapSeconds;

enum _PrTool { select, draw, eraser }

bool _isBlackKey(int pitch) {
  const set = {1, 3, 6, 8, 10};
  return set.contains(pitch % 12);
}

/// Immutable snapshot of one note at the start of a drag gesture.
class _NoteEdit {
  const _NoteEdit(this.index, this.pitch, this.start, this.length);

  final int index;
  final int pitch;
  final double start;
  final double length;
}

class PianoRoll extends StatefulWidget {
  const PianoRoll(
      {super.key,
      required this.service,
      required this.clipId,
      required this.frameTick,
      this.onUndock});

  final EngineService service;
  final int clipId;
  final ValueListenable<int> frameTick;
  final VoidCallback? onUndock;

  @override
  State<PianoRoll> createState() => _PianoRollState();
}

class _PianoRollState extends State<PianoRoll> {
  final FocusNode _focusNode = FocusNode();
  final ScrollController _hScroll = ScrollController();
  final ScrollController _vScroll = ScrollController();

  @override
  void initState() {
    super.initState();
    widget.frameTick.addListener(_followPlayhead);
    // Center the view on the clip's content: vertical on the average note
    // pitch, horizontal on the first note.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!_vScroll.hasClients || !_hScroll.hasClients) return;
      final clip = _clip;
      if (clip == null) return;
      double avgPitch = 60;
      if (clip.notes.isNotEmpty) {
        avgPitch = clip.notes.map((n) => n.pitch).reduce((a, b) => a + b) /
            clip.notes.length;
      }
      final targetY =
          (_pitchToY(avgPitch.round(), 0) - _vScroll.position.viewportDimension / 2)
              .clamp(0.0, _vScroll.position.maxScrollExtent);
      _vScroll.jumpTo(targetY);
      // Horizontal: if the transport playhead sits inside the clip, open
      // looking at it (what the timeline shows); otherwise at the notes.
      final viewport = _hScroll.position.viewportDimension;
      final playheadLocal =
          (widget.service.positionSeconds - clip.start) * kPrPixelsPerSecond;
      double? targetX;
      if (playheadLocal >= 0 &&
          playheadLocal <= (clip.length + 1) * kPrPixelsPerSecond) {
        targetX = playheadLocal - viewport * 0.35;
      } else if (clip.notes.isNotEmpty) {
        final first = clip.notes.map((n) => n.start).reduce(math.min);
        targetX = first * kPrPixelsPerSecond - 100;
      }
      if (targetX != null) {
        _hScroll.jumpTo(
            targetX.clamp(0.0, _hScroll.position.maxScrollExtent));
      }
    });
  }

  @override
  void didUpdateWidget(covariant PianoRoll oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.frameTick != widget.frameTick) {
      oldWidget.frameTick.removeListener(_followPlayhead);
      widget.frameTick.addListener(_followPlayhead);
    }
  }

  @override
  void dispose() {
    _autoScrollTimer?.cancel();
    _hScroll.dispose();
    _vScroll.dispose();
    _focusNode.dispose();
    widget.frameTick.removeListener(_followPlayhead);
    super.dispose();
  }

  /// Auto-scroll: while playing, keep the playhead visible. While paused,
  /// follow seeks (the user moved the playhead in the timeline — show it).
  double _lastSeenPos = -1;
  void _followPlayhead() {
    if (!_hScroll.hasClients) return;
    final pos = widget.service.positionSeconds;
    final prev = _lastSeenPos;
    _lastSeenPos = pos;
    final playhead =
        (pos - _clipStartSeconds()) * kPrPixelsPerSecond;
    final viewport = _hScroll.position.viewportDimension;
    const margin = 32.0;
    if (widget.service.isPlaying) {
      if (playhead < 0) return;
      if (playhead > _hScroll.offset + viewport - margin ||
          playhead < _hScroll.offset) {
        _hScroll.jumpTo((playhead - viewport * 0.1)
            .clamp(0.0, _hScroll.position.maxScrollExtent));
      }
    } else if (prev >= 0 && (pos - prev).abs() > 1e-9) {
      if (playhead >= 0) {
        _hScroll.jumpTo((playhead - viewport * 0.35)
            .clamp(0.0, _hScroll.position.maxScrollExtent));
      }
    }
  }

  double _clipStartSeconds() {
    for (final track in widget.service.snapshot.tracks) {
      for (final clip in track.clips) {
        if (clip.id == widget.clipId) return clip.start;
      }
    }
    return 0.0;
  }
  // Notes selected by click or rubber-band. Indices refer to the snapshot
  // order, which the engine keeps stable across note edits.
  final Set<int> _selectedNotes = <int>{};
  // Drag: one note (move/resize) or the whole selection moved together.
  int? _dragNoteIndex; // reference note under the pointer
  bool _resizing = false;
  double _origStart = 0, _origLen = 0;
  int _origPitch = 0;
  double _previewStart = 0, _previewLen = 0;
  double _grabOffsetSeconds = 0;
  bool _didEdit = false;
  List<_NoteEdit> _dragGroup = const [];
  Map<int, (double, double, int)> _dragPreviews = const {};
  // Rubber-band selection rectangle, in grid content pixels.
  bool _marqueeActive = false;
  Offset? _marqueeStart;
  Offset? _marqueeCurrent;
  Set<int> _marqueeBase = const {};
  // Edge auto-scroll: while selecting or dragging, scrolling the view under
  // a stationary pointer keeps extending the gesture past the visible area.
  Timer? _autoScrollTimer;
  double _pointerX = 0; // viewport-relative (grid area)
  double _pointerY = 0; // content-relative (grid rows)
  static const double _autoScrollEdge = 36;
  static const double _autoScrollMaxStep = 18;

  ClipInfo? get _clip {
    for (final track in widget.service.snapshot.tracks) {
      for (final clip in track.clips) {
        if (clip.id == widget.clipId) return clip;
      }
    }
    return null;
  }

  double get _hOffset => _hScroll.hasClients ? _hScroll.offset : 0;

  /// Snap helper gated by the "Rejilla" toggle: positions land on the
  /// eighth-note grid when snap is on, free when off.
  double _snapPos(double raw) {
    if (!widget.service.snapToGrid) return raw;
    return _snapPr(raw, _prSnapSeconds(widget.service.tempoBpm));
  }

  /// The snap unit (used as a minimum length when drawing, on or off).
  double get _snapUnit => _prSnapSeconds(widget.service.tempoBpm);

  // ---- tools (§7.5.1): pointer (move/resize), pencil (draw), eraser ----
  _PrTool _tool = _PrTool.select;
  // Pencil drag preview: a new note being stretched (committed on release).
  bool _drawing = false;
  int _drawPitch = 60;
  double _drawStart = 0, _drawLen = 0;

  int? _hitNoteIndex(ClipInfo clip, double seconds, int pitch) {
    // All in SECONDS — comparing seconds against pixels here silently
    // matched nothing (the reason note drags never registered). The pitch
    // row matters too: without it every note sharing a time column was hit
    // no matter which key row the pointer was on.
    for (var i = clip.notes.length - 1; i >= 0; i--) {
      final note = clip.notes[i];
      if (note.pitch == pitch &&
          seconds >= note.start &&
          seconds <= note.start + note.length) {
        return i;
      }
    }
    return null;
  }

  // ---- Raw pointer interaction (no gesture arena) ----
  // The grid sits inside scrollables; GestureDetector pan gestures there
  // compete with the scroll arena and lose. Raw Listener events always
  // arrive, so note drags are deterministic. Scroll drag is disabled
  // (NeverScrollable) and scrolling happens via wheel, the scrollbar, or
  // edge auto-scroll while selecting/dragging.
  void _onPointerDown(PointerDownEvent event, ClipInfo clip) {
    if (event.buttons != kPrimaryButton) return;
    _focusNode.requestFocus();
    // Content coords: x is viewport-relative (this box wraps the h-scroll),
    // y is already content-relative (the box spans the grid height).
    _pointerX = event.localPosition.dx;
    _pointerY = event.localPosition.dy;
    _startAutoScroll();
    final contentX = _pointerX + _hOffset;
    final seconds = contentX / kPrPixelsPerSecond;
    final pitch = _yToPitch(event.localPosition.dy, 0)
        .clamp(kPrBottomPitch, kPrTopPitch);
    final hit = _hitNoteIndex(clip, seconds, pitch);
    // Shift or ⌘ extends/toggles the selection (DAW convention).
    final keyboard = HardwareKeyboard.instance;
    final additive = keyboard.isShiftPressed || keyboard.isMetaPressed;

    switch (_tool) {
      case _PrTool.eraser:
        if (hit != null) {
          widget.service.midiClipRemoveNote(widget.clipId, hit);
          _selectedNotes.clear();
          setState(() {});
        }
        return;
      case _PrTool.draw:
        if (hit != null) {
          _selectOnly(hit);
          _beginNoteDrag(clip, hit, seconds);
          return;
        }
        // Pencil: press starts a one-snap note, drag stretches it, release
        // commits — one undo entry.
        _drawPitch = pitch;
        _drawStart = _snapPos(seconds);
        _drawLen = _snapUnit;
        _drawing = true;
        _selectedNotes.clear();
        setState(() {});
        return;
      case _PrTool.select:
        if (hit != null) {
          // Shift toggles a note in/out of the selection without dragging.
          if (additive) {
            setState(() {
              if (!_selectedNotes.remove(hit)) _selectedNotes.add(hit);
            });
            return;
          }
          if (!_selectedNotes.contains(hit)) _selectOnly(hit);
          _beginNoteDrag(clip, hit, seconds);
          return;
        }
        // Empty space: rubber-band selection. Shift keeps the current
        // selection and adds every note the rectangle touches.
        if (!additive) _selectedNotes.clear();
        _marqueeBase = Set<int>.of(_selectedNotes);
        _marqueeActive = true;
        _marqueeStart = Offset(contentX, event.localPosition.dy);
        _marqueeCurrent = _marqueeStart;
        setState(() {});
        return;
    }
  }

  void _selectOnly(int index) {
    _selectedNotes
      ..clear()
      ..add(index);
  }

  void _onPointerMove(PointerMoveEvent event) {
    if (event.buttons & kPrimaryButton == 0) return;
    final clip = _clip;
    if (clip == null) return;
    _pointerX = event.localPosition.dx;
    _pointerY = event.localPosition.dy;
    if (_marqueeActive) {
      _marqueeCurrent = Offset(_pointerX + _hOffset, _pointerY);
      _updateMarquee();
      return;
    }
    // Eraser: keep deleting notes the pointer passes over.
    if (_tool == _PrTool.eraser) {
      final seconds = (_pointerX + _hOffset) / kPrPixelsPerSecond;
      final pitch =
          _yToPitch(_pointerY, 0).clamp(kPrBottomPitch, kPrTopPitch);
      final hit = _hitNoteIndex(clip, seconds, pitch);
      if (hit != null) {
        widget.service.midiClipRemoveNote(widget.clipId, hit);
        _selectedNotes.clear();
      }
      return;
    }
    // Pencil: stretch the note being drawn.
    if (_drawing) {
      final seconds = (_pointerX + _hOffset) / kPrPixelsPerSecond;
      final end = math.max(_drawStart + _snapUnit, _snapPos(seconds));
      setState(() => _drawLen = end - _drawStart);
      return;
    }
    if (_dragGroup.isEmpty) return;
    _updateDragPreview(_pointerX + _hOffset, _pointerY);
  }

  void _updateMarquee() {
    final clip = _clip;
    final start = _marqueeStart;
    final current = _marqueeCurrent;
    if (clip == null || start == null || current == null) return;
    final rect = Rect.fromPoints(start, current);
    final hits = <int>{};
    for (var i = 0; i < clip.notes.length; i++) {
      final note = clip.notes[i];
      final noteRect = Rect.fromLTWH(
        note.start * kPrPixelsPerSecond,
        _pitchToY(note.pitch, 0),
        math.max(1.0, note.length * kPrPixelsPerSecond),
        kPrRowHeight,
      );
      if (rect.overlaps(noteRect)) hits.add(i);
    }
    _selectedNotes
      ..clear()
      ..addAll(_marqueeBase)
      ..addAll(hits);
    setState(() {});
  }

  void _updateDragPreview(double contentX, double contentY) {
    final seconds = contentX / kPrPixelsPerSecond;
    const minLen = 0.05;

    if (_resizing) {
      final end = math.max(_origStart + minLen, _snapPos(seconds));
      _previewStart = _origStart;
      _previewLen = end - _origStart;
      _dragPreviews = {
        _dragNoteIndex!: (_previewStart, _previewLen, _origPitch)
      };
      _didEdit = _previewStart != _origStart || _previewLen != _origLen;
    } else {
      final ref = _dragGroup.firstWhere((s) => s.index == _dragNoteIndex,
          orElse: () => _dragGroup.first);
      // grabOffset = press - origStart, so this is origStart + delta.
      // (Adding _origStart here as well doubled it: notes far from the
      // clip head jumped 2x their position on the first move — the
      // "dragged notes disappear" bug.)
      final start = math.max(0.0, _snapPos(seconds - _grabOffsetSeconds));
      // One delta for every note: clamping each note independently would
      // collapse the rhythm at the clip start.
      var delta = start - ref.start;
      final minStart = _dragGroup.map((s) => s.start).reduce(math.min);
      if (delta < -minStart) delta = -minStart;

      final pitch =
          _yToPitch(contentY, 0).clamp(kPrBottomPitch, kPrTopPitch);
      final minPitch = _dragGroup.map((s) => s.pitch).reduce(math.min);
      final maxPitch = _dragGroup.map((s) => s.pitch).reduce(math.max);
      var deltaPitch = pitch - ref.pitch;
      deltaPitch =
          deltaPitch.clamp(kPrBottomPitch - minPitch, kPrTopPitch - maxPitch);

      _previewStart = ref.start + delta;
      _dragPreviews = {
        for (final s in _dragGroup)
          s.index: (s.start + delta, s.length, s.pitch + deltaPitch)
      };
      _didEdit = delta.abs() > 1e-9 || deltaPitch != 0;
    }
    setState(() {});
  }

  void _startAutoScroll() {
    _autoScrollTimer ??= Timer.periodic(
      const Duration(milliseconds: 16),
      (_) => _autoScrollTick(),
    );
  }

  void _stopAutoScroll() {
    _autoScrollTimer?.cancel();
    _autoScrollTimer = null;
  }

  /// Timer-driven scroll so a marquee or note drag keeps going when the
  /// pointer rests near an edge. While the view moves there are no pointer
  /// events, so the gesture's content coordinate has to be advanced here.
  void _autoScrollTick() {
    if (!_marqueeActive && !_drawing && _dragGroup.isEmpty) {
      _stopAutoScroll();
      return;
    }
    final h = _hScroll.hasClients ? _hScroll.position : null;
    final v = _vScroll.hasClients ? _vScroll.position : null;
    var stepX = 0.0;
    var stepY = 0.0;
    if (h != null) {
      final fromLeft = _pointerX;
      final fromRight = h.viewportDimension - _pointerX;
      if (fromLeft < _autoScrollEdge) {
        stepX = -_edgeSpeed(fromLeft);
      } else if (fromRight < _autoScrollEdge) {
        stepX = _edgeSpeed(fromRight);
      }
    }
    if (v != null) {
      final viewportY = _pointerY - v.pixels;
      final fromTop = viewportY;
      final fromBottom = v.viewportDimension - viewportY;
      if (fromTop < _autoScrollEdge) {
        stepY = -_edgeSpeed(fromTop);
      } else if (fromBottom < _autoScrollEdge) {
        stepY = _edgeSpeed(fromBottom);
      }
    }
    if (stepX == 0 && stepY == 0) return;

    final oldX = h?.pixels ?? 0;
    final oldY = v?.pixels ?? 0;
    if (h != null && stepX != 0) {
      _hScroll.jumpTo((oldX + stepX).clamp(0.0, h.maxScrollExtent));
    }
    if (v != null && stepY != 0) {
      _vScroll.jumpTo((oldY + stepY).clamp(0.0, v.maxScrollExtent));
    }
    final dx = (h?.pixels ?? oldX) - oldX;
    final dy = (v?.pixels ?? oldY) - oldY;
    if (dx == 0 && dy == 0) return;
    _syncGestureAfterScroll(dy);
  }

  /// Re-evaluates the active gesture after the view scrolled under a
  /// stationary pointer. [dy] is the content movement for vertical scroll.
  void _syncGestureAfterScroll(double dy) {
    if (!_marqueeActive && !_drawing && _dragGroup.isEmpty) return;
    // The pointer did not move, but the content did: keep its content
    // coordinate in sync so the gesture grows past the visible area.
    _pointerY += dy;
    if (_marqueeActive) {
      _marqueeCurrent = Offset(_pointerX + _hOffset, _pointerY);
      _updateMarquee();
    } else if (_drawing) {
      final seconds = (_pointerX + _hOffset) / kPrPixelsPerSecond;
      final end = math.max(_drawStart + _snapUnit, _snapPos(seconds));
      setState(() => _drawLen = end - _drawStart);
    } else {
      _updateDragPreview(_pointerX + _hOffset, _pointerY);
    }
  }

  double _edgeSpeed(double distance) {
    final t = (1 - distance / _autoScrollEdge).clamp(0.0, 1.0);
    return _autoScrollMaxStep * t;
  }

  void _onPointerUp(PointerUpEvent event) {
    _stopAutoScroll();
    if (_marqueeActive) {
      _marqueeActive = false;
      _marqueeStart = null;
      _marqueeCurrent = null;
      _marqueeBase = const {};
      setState(() {});
      return;
    }
    if (_drawing) {
      // One undo entry per drawn note.
      widget.service.midiClipAddNote(
          widget.clipId, _drawPitch, 0.8, _drawStart, _drawLen);
      _drawing = false;
      _drawLen = 0;
      setState(() {});
      return;
    }
    if (_dragGroup.isNotEmpty && _didEdit) _commitDrag();
    _dragGroup = const [];
    _dragPreviews = const {};
    _dragNoteIndex = null;
    _resizing = false;
    _didEdit = false;
    setState(() {});
  }

  /// Commits every note moved by the gesture (the engine records the whole
  /// batch as one undo entry).
  void _commitDrag() {
    final indices = <int>[];
    final pitches = <int>[];
    final starts = <double>[];
    final lengths = <double>[];
    for (final edit in _dragGroup) {
      final preview = _dragPreviews[edit.index];
      if (preview == null) continue;
      final (start, length, pitch) = preview;
      if ((start - edit.start).abs() < 1e-9 &&
          (length - edit.length).abs() < 1e-9 &&
          pitch == edit.pitch) {
        continue; // unchanged note: don't dirty the undo history
      }
      indices.add(edit.index);
      pitches.add(pitch);
      starts.add(start);
      lengths.add(length);
    }
    if (indices.isEmpty) return;
    widget.service.midiClipSetNotes(
        widget.clipId, indices, pitches, starts, lengths);
  }

  void _onPointerCancel(PointerCancelEvent event) {
    _stopAutoScroll();
    _drawing = false;
    _drawLen = 0;
    _dragGroup = const [];
    _dragPreviews = const {};
    _dragNoteIndex = null;
    _resizing = false;
    _didEdit = false;
    _marqueeActive = false;
    _marqueeStart = null;
    _marqueeCurrent = null;
    _marqueeBase = const {};
    setState(() {});
  }

  /// Wheel / trackpad scroll: both axes, since the scrollables themselves
  /// have drag disabled. Shift turns a vertical wheel into horizontal pan,
  /// which is how a plain mouse reaches the rest of a long clip.
  void _onPointerSignal(PointerSignalEvent event) {
    if (event is! PointerScrollEvent) return;
    var dx = event.scrollDelta.dx;
    var dy = event.scrollDelta.dy;
    if (HardwareKeyboard.instance.isShiftPressed && dx == 0 && dy != 0) {
      dx = dy;
      dy = 0;
    }
    final beforeX = _hScroll.hasClients ? _hScroll.offset : 0.0;
    final beforeY = _vScroll.hasClients ? _vScroll.offset : 0.0;
    if (dx != 0 && _hScroll.hasClients) {
      _hScroll.jumpTo(
          (_hScroll.offset + dx).clamp(0.0, _hScroll.position.maxScrollExtent));
    }
    if (dy != 0 && _vScroll.hasClients) {
      _vScroll.jumpTo(
          (_vScroll.offset + dy).clamp(0.0, _vScroll.position.maxScrollExtent));
    }
    final movedX =
        (_hScroll.hasClients ? _hScroll.offset : beforeX) - beforeX;
    final movedY =
        (_vScroll.hasClients ? _vScroll.offset : beforeY) - beforeY;
    if (movedX != 0 || movedY != 0) _syncGestureAfterScroll(movedY);
  }

  void _beginNoteDrag(ClipInfo clip, int index, double seconds) {
    final note = clip.notes[index];
    if (!_selectedNotes.contains(index)) _selectOnly(index);
    _dragGroup = [
      for (final i in _selectedNotes.toList()..sort())
        if (i >= 0 && i < clip.notes.length)
          _NoteEdit(i, clip.notes[i].pitch, clip.notes[i].start,
              clip.notes[i].length),
    ];
    _dragNoteIndex = index;
    // Right-edge hot zone (6 px) — only on a single note wide enough to
    // have a body besides the edge: on short notes every grab would resize.
    _resizing = _dragGroup.length == 1 &&
        note.length * kPrPixelsPerSecond >= 14 &&
        seconds >= note.start + note.length - 6.0 / kPrPixelsPerSecond;
    _origStart = note.start;
    _origLen = note.length;
    _origPitch = note.pitch;
    _grabOffsetSeconds = seconds - note.start;
    _previewStart = note.start;
    _previewLen = note.length;
    _dragPreviews = {index: (note.start, note.length, note.pitch)};
    _didEdit = false;
    setState(() {});
  }

  /// Deletes every selected note. Remove from the highest index down so the
  /// remaining indices stay valid after each engine call.
  void _deleteSelectedNotes() {
    final clip = _clip;
    if (clip == null) return;
    final indices = _selectedNotes
        .where((i) => i >= 0 && i < clip.notes.length)
        .toList()
      ..sort((a, b) => b.compareTo(a));
    for (final index in indices) {
      widget.service.midiClipRemoveNote(widget.clipId, index);
    }
    _selectedNotes.clear();
    setState(() {});
  }

  Widget _qSlider(String label, double value, ValueChanged<double> onChanged) {
    return Row(children: [
      Text('$label ${(value * 100).round()}%',
          style: const TextStyle(fontSize: 9, color: Color(0xFF9BA1AE))),
      SizedBox(
        width: 60,
        child: SliderTheme(
          data: SliderTheme.of(context).copyWith(
            trackHeight: 2,
            thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 5),
            overlayShape: const RoundSliderOverlayShape(overlayRadius: 8),
            padding: EdgeInsets.zero,
          ),
          child: Slider(
            min: 0,
            max: 1,
            value: value,
            onChanged: onChanged,
          ),
        ),
      ),
    ]);
  }

  Widget _toolButton(_PrTool tool, IconData icon, String tooltip) {
    final active = _tool == tool;
    return Padding(
      padding: const EdgeInsets.only(right: 2),
      child: IconButton(
        tooltip: tooltip,
        visualDensity: VisualDensity.compact,
        icon: Icon(icon,
            size: 16,
            color: active ? const Color(0xFF4D9DF7) : const Color(0xFF9BA1AE)),
        onPressed: () => setState(() => _tool = tool),
        style: IconButton.styleFrom(
          backgroundColor: active
              ? const Color(0xFF4D9DF7).withValues(alpha: 0.15)
              : Colors.transparent,
        ),
      ),
    );
  }

  Widget _qButton(String label, VoidCallback onTap) {
    return Padding(
      padding: const EdgeInsets.only(left: 4),
      child: GestureDetector(
        onTap: onTap,
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
          decoration: BoxDecoration(
            color: const Color(0xFF2C303A),
            borderRadius: BorderRadius.circular(3),
            border: Border.all(color: const Color(0xFF3E4350)),
          ),
          child: Text(label,
              style: const TextStyle(
                  fontSize: 10,
                  fontWeight: FontWeight.w600,
                  color: Color(0xFFE9EBF0))),
        ),
      ),
    );
  }

  // Quantize panel state (§7.5.2). Grid = beat multipliers (1 = 1/4), so
  // the preset follows the project tempo; T = triplet, . = dotted.
  static const List<(String, double)> kQuantizeGrids = [
    ('1/4', 1.0),
    ('1/8', 0.5),
    ('1/8T', 1.0 / 3),
    ('1/8.', 1.5),
    ('1/16', 0.25),
    ('1/16T', 1.0 / 6),
    ('1/16.', 0.75),
    ('1/32', 0.125),
    ('1/32T', 1.0 / 12),
  ];
  int _gridIndex = 4; // 1/16
  double _swing = 0; // 0..1
  double _catch = 0.0; // 0..1 of grid: notes this close stay put
  double _maxMove = 1.0; // 0..1 of grid: displacement cap
  // v1.5 controls (doc §7.5.2): UI for both lands when the panel grows.
  final bool _nonGrid = false; // protect notes exactly between grid lines
  final double _iterStrength = 0.5; // iQ step size

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: widget.service,
      builder: (context, _) {
        final clip = _clip;
        if (clip == null) {
          return Center(
              child: Text(context.l10n.pianoRollClipUnavailable,
                  style: const TextStyle(color: Color(0xFF9BA1AE))));
        }

        // Grid covers the whole clip (the old fixed 30 s cut longer takes).
        final gridWidth = math.max(30 * kPrPixelsPerSecond,
            (clip.length + 8) * kPrPixelsPerSecond);
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
                  Text(context.l10n.pianoRollTitle(_clipName()),
                      style: const TextStyle(
                          fontSize: 11, color: Color(0xFFE9EBF0))),
                  const Spacer(),
                  Expanded(
                    child: Text(
                        context.l10n.pianoRollHint,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                            fontSize: 9, color: Color(0xFF5C6270))),
                  ),
                  IconButton(
                    tooltip: context.l10n.pianoRollUndock,
                    visualDensity: VisualDensity.compact,
                    icon: const Icon(Icons.open_in_new, size: 14,
                        color: Color(0xFF9BA1AE)),
                    onPressed: widget.onUndock,
                  ),
                  IconButton(
                    visualDensity: VisualDensity.compact,
                    icon: const Icon(Icons.close, size: 14,
                        color: Color(0xFF9BA1AE)),
                    onPressed: () => widget.service.closeEditor(),
                  ),
                ],
              ),
            ),
            // Quantize panel (§7.5.2 v1): grid, swing, catch, max move and
            // the Q / iQ / Reset / Freeze actions. Scrolls on narrow
            // windows instead of overflowing.
            Container(
              height: 34,
              color: const Color(0xFF1B1D22),
              padding: const EdgeInsets.symmetric(horizontal: 10),
              child: SingleChildScrollView(
                scrollDirection: Axis.horizontal,
                child: Row(
                  children: [
                  _toolButton(_PrTool.select, Icons.near_me,
                      context.l10n.pianoRollToolMove),
                  const SizedBox(width: 2),
                  _toolButton(
                      _PrTool.draw, Icons.edit, context.l10n.pianoRollToolDraw),
                  const SizedBox(width: 2),
                  _toolButton(_PrTool.eraser, Icons.back_hand_outlined,
                      context.l10n.pianoRollToolErase),
                  const SizedBox(width: 12),
                  Text(context.l10n.pianoRollQuantize,
                      style: const TextStyle(
                          fontSize: 9,
                          fontWeight: FontWeight.w600,
                          color: Color(0xFF5C6270))),
                  const SizedBox(width: 8),
                  DropdownButton<int>(
                    value: _gridIndex,
                    dropdownColor: const Color(0xFF252830),
                    underline: const SizedBox(),
                    style: const TextStyle(
                        fontSize: 11, color: Color(0xFFE9EBF0)),
                    items: [
                      for (var i = 0; i < kQuantizeGrids.length; i++)
                        DropdownMenuItem(
                            value: i, child: Text(kQuantizeGrids[i].$1)),
                    ],
                    onChanged: (v) => setState(() => _gridIndex = v ?? 4),
                  ),
                  _qSlider(context.l10n.pianoRollSwing, _swing, (v) {
                    _swing = v;
                    setState(() {});
                  }),
                  _qSlider(context.l10n.pianoRollCatch, _catch, (v) {
                    _catch = v;
                    setState(() {});
                  }),
                  _qSlider(context.l10n.pianoRollMax, _maxMove, (v) {
                    _maxMove = v;
                    setState(() {});
                  }),
                  const SizedBox(width: 12),
                  _qButton('Q', () {
                    final beat = widget.service.tempoBpm > 0
                        ? 60.0 / widget.service.tempoBpm
                        : 0.5;
                    widget.service.quantizeClip(
                        widget.clipId,
                        kQuantizeGrids[_gridIndex].$2 * beat,
                        _swing,
                        _catch,
                        _maxMove,
                        _nonGrid,
                        false,
                        1);
                  }),
                  _qButton(context.l10n.pianoRollIQ, () {
                    final beat = widget.service.tempoBpm > 0
                        ? 60.0 / widget.service.tempoBpm
                        : 0.5;
                    widget.service.quantizeClip(
                        widget.clipId,
                        kQuantizeGrids[_gridIndex].$2 * beat,
                        _swing,
                        _catch,
                        _maxMove,
                        _nonGrid,
                        true,
                        _iterStrength);
                  }),
                  _qButton(context.l10n.pianoRollReset,
                      () => widget.service.resetClipQuantize(widget.clipId)),
                  _qButton(context.l10n.pianoRollFreeze,
                      () => widget.service.freezeClipQuantize(widget.clipId)),
                  ],
                ),
              ),
            ),
            Expanded(
              child: KeyboardListener(
                focusNode: _focusNode,
                onKeyEvent: (event) {
                  if (event is KeyDownEvent &&
                      (event.logicalKey == LogicalKeyboardKey.delete ||
                          event.logicalKey == LogicalKeyboardKey.backspace) &&
                      _selectedNotes.isNotEmpty) {
                    _deleteSelectedNotes();
                  }
                },
                child: Column(children: [
                  // Bar ruler (§7.3), pinned above the grid; scrolls in sync
                  // by translating with the grid's h-scroll offset.
                  Container(
                    height: 16,
                    color: const Color(0xFF14161A),
                    child: Row(children: [
                      const SizedBox(width: kPrKeysWidth),
                      Expanded(
                        child: ClipRect(
                          child: AnimatedBuilder(
                            animation: _hScroll,
                            builder: (context, _) => CustomPaint(
                              size: Size.infinite,
                              painter: _PrRulerPainter(
                                barSeconds:
                                    widget.service.snapshot.barSeconds,
                                beatSeconds:
                                    widget.service.snapshot.beatSeconds,
                                pixelsPerSecond: kPrPixelsPerSecond,
                                offsetX:
                                    _hScroll.hasClients ? _hScroll.offset : 0,
                                clipStartSeconds: clip.start,
                              ),
                            ),
                          ),
                        ),
                      ),
                    ]),
                  ),
                  Expanded(
                    child: Scrollbar(
                      controller: _vScroll,
                      thumbVisibility: true,
                      thickness: 8,
                      child: SingleChildScrollView(
                      // Vertical scroll carries keys + grid together.
                      controller: _vScroll,
                      physics: const NeverScrollableScrollPhysics(),
                      child: SizedBox(
                        height: gridHeight,
                        child: Row(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            SizedBox(
                              width: kPrKeysWidth,
                              height: gridHeight,
                              child: CustomPaint(
                                size: Size(kPrKeysWidth, gridHeight),
                                painter: _KeysPainter(
                                    topPitch: kPrTopPitch, rows: rows),
                              ),
                            ),
                            Expanded(
                              // Raw pointer events: no gesture arena, note
                              // drags always arrive (drag-to-scroll is off;
                              // scrolling = wheel + scrollbar).
                              child: Listener(
                                onPointerDown: (d) =>
                                    _onPointerDown(d, clip),
                                onPointerMove: _onPointerMove,
                                onPointerUp: _onPointerUp,
                                onPointerCancel: _onPointerCancel,
                                onPointerSignal: _onPointerSignal,
                                child: SingleChildScrollView(
                                  scrollDirection: Axis.horizontal,
                                  controller: _hScroll,
                                  physics:
                                      const NeverScrollableScrollPhysics(),
                                  child: SizedBox(
                                    width: gridWidth,
                                    height: gridHeight,
                                    child: Stack(children: [
                                      CustomPaint(
                                        size: Size(gridWidth, gridHeight),
                                        painter: _GridPainter(
                                          clip: clip,
                                          // Copy: the painter compares by
                                          // identity, and the live set is
                                          // mutated in place.
                                          selectedNoteIndices:
                                              Set<int>.of(_selectedNotes),
                                          snapSeconds: _prSnapSeconds(
                                              widget.service.tempoBpm),
                                          beatSeconds: widget
                                              .service.snapshot.beatSeconds,
                                          barSeconds: widget
                                              .service.snapshot.barSeconds,
                                          clipStartSeconds: clip.start,
                                          dragPreviews: _dragPreviews,
                                          marqueeRect: _marqueeActive &&
                                                  _marqueeStart != null &&
                                                  _marqueeCurrent != null
                                              ? Rect.fromPoints(
                                                  _marqueeStart!,
                                                  _marqueeCurrent!)
                                              : null,
                                          drawPreview: _drawing
                                              ? (_drawStart, _drawLen,
                                                  _drawPitch)
                                              : null,
                                        ),
                                      ),
                                      // Playhead inside the clip: the painter
                                      // itself listens to the frame tick.
                                      RepaintBoundary(
                                        child: CustomPaint(
                                          size: Size(gridWidth, gridHeight),
                                          painter: _PrPlayheadPainter(
                                            clip: clip,
                                            service: widget.service,
                                            repaint: widget.frameTick,
                                          ),
                                        ),
                                      ),
                                    ]),
                                  ),
                                ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ),
                  ),
                  ),
                  // Horizontal navigation for the grid (drag the thumb; the
                  // content itself does not drag-scroll).
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
                ]),
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

/// Bar ruler over the piano roll grid: numbered with PROJECT bar numbers
/// (same numbers as the timeline; x=0 in the grid is the clip start, which
/// sits at project time `clipStartSeconds`), translated by the grid's
/// h-scroll offset.
class _PrRulerPainter extends CustomPainter {
  _PrRulerPainter({
    required this.barSeconds,
    required this.beatSeconds,
    required this.pixelsPerSecond,
    required this.offsetX,
    required this.clipStartSeconds,
  });

  final double barSeconds;
  final double beatSeconds;
  final double pixelsPerSecond;
  final double offsetX;
  final double clipStartSeconds;

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

    // Visible clip-local time range.
    final t0 = offsetX / pixelsPerSecond;
    final t1 = (offsetX + size.width) / pixelsPerSecond;

    // Project-grid beats: k*beatSeconds in project time.
    for (var k = ((clipStartSeconds + t0) / beatSeconds).ceil();
        k * beatSeconds <= clipStartSeconds + t1;
        k++) {
      if (k < 0) continue;
      final x = (k * beatSeconds - clipStartSeconds) * pixelsPerSecond -
          offsetX;
      canvas.drawLine(
          Offset(x + 0.5, size.height - 4), Offset(x + 0.5, size.height), beatTick);
    }
    for (var k = ((clipStartSeconds + t0) / barSeconds).ceil();
        k * barSeconds <= clipStartSeconds + t1;
        k++) {
      if (k < 0) continue;
      final x = (k * barSeconds - clipStartSeconds) * pixelsPerSecond - offsetX;
      canvas.drawLine(Offset(x + 0.5, 0), Offset(x + 0.5, size.height), barTick);
      final tp = TextPainter(
        text: TextSpan(text: '${k + 1}', style: style),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(x + 3, 2));
    }
  }

  @override
  bool shouldRepaint(_PrRulerPainter oldDelegate) =>
      oldDelegate.offsetX != offsetX ||
      oldDelegate.barSeconds != barSeconds ||
      oldDelegate.beatSeconds != beatSeconds ||
      oldDelegate.clipStartSeconds != clipStartSeconds;
}

class _GridPainter extends CustomPainter {
  _GridPainter({
    required this.clip,
    required this.selectedNoteIndices,
    required this.snapSeconds,
    required this.beatSeconds,
    required this.barSeconds,
    required this.clipStartSeconds,
    this.dragPreviews = const {},
    this.marqueeRect,
    this.drawPreview,
  });

  final ClipInfo clip;
  final Set<int> selectedNoteIndices;
  final double snapSeconds;
  final double beatSeconds;
  final double barSeconds;
  final double clipStartSeconds;
  final Map<int, (double, double, int)> dragPreviews; // index -> live position
  final Rect? marqueeRect; // rubber-band rectangle, content pixels
  final (double, double, int)? drawPreview; // start, length, pitch

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(Offset.zero & size, Paint()..color = const Color(0xFF191B20));

    // Beat and bar lines on the PROJECT musical grid (they line up with the
    // ruler numbers and with the timeline), phase-shifted into clip-local x.
    final beatLine = Paint()
      ..color = const Color(0xFF2A2D35)
      ..strokeWidth = 1;
    final barLine = Paint()
      ..color = const Color(0xFF3E4350)
      ..strokeWidth = 1;
    if (beatSeconds > 0 && barSeconds > 0) {
      final beatsPerBar = (barSeconds / beatSeconds).round();
      final endT = size.width / kPrPixelsPerSecond;
      for (var k = (clipStartSeconds / beatSeconds).ceil();
          k * beatSeconds <= clipStartSeconds + endT;
          k++) {
        if (k < 0) continue;
        final x = (k * beatSeconds - clipStartSeconds) * kPrPixelsPerSecond;
        canvas.drawLine(Offset(x + 0.5, 0), Offset(x + 0.5, size.height),
            k % (beatsPerBar > 0 ? beatsPerBar : 4) == 0 ? barLine : beatLine);
      }
    } else {
      for (var b = 0;
          b * snapSeconds * kPrPixelsPerSecond < size.width;
          b++) {
        final x = b * snapSeconds * kPrPixelsPerSecond + 0.5;
        canvas.drawLine(Offset(x, 0), Offset(x, size.height),
            b % 4 == 0 ? barLine : beatLine);
      }
    }

    // Pencil preview: the note being drawn, translucent.
    final preview = drawPreview;
    if (preview != null && preview.$2 > 0) {
      final px = preview.$1 * kPrPixelsPerSecond;
      final pw = (preview.$2 * kPrPixelsPerSecond).clamp(3.0, size.width);
      final py = _pitchToY(preview.$3, 0);
      canvas.drawRRect(
          RRect.fromRectAndRadius(
              Rect.fromLTWH(px, py + 1, pw, kPrRowHeight - 2),
              const Radius.circular(2)),
          Paint()..color = const Color(0xFF7FB2F0).withValues(alpha: 0.45));
    }

    // Notes (pitch mapped to rows; black keys shaded). Quantized notes draw
    // a ghost outline at their pre-quantize position (§7.5.2 ghost display).
    // Notes being dragged draw at their LIVE preview position so the whole
    // group follows the pointer instead of jumping on release.
    for (var i = 0; i < clip.notes.length; i++) {
      final note = clip.notes[i];
      final drag = dragPreviews[i];
      final isDrag = drag != null;
      final left = (drag?.$1 ?? note.start) * kPrPixelsPerSecond;
      final width = ((drag?.$2 ?? note.length) * kPrPixelsPerSecond)
          .clamp(3.0, size.width);
      final top = _pitchToY(drag?.$3 ?? note.pitch, 0);
      if (!isDrag && note.origStart != null) {
        final gLeft = note.origStart! * kPrPixelsPerSecond;
        final gWidth = (note.length * kPrPixelsPerSecond).clamp(3.0, size.width);
        canvas.drawRRect(
            RRect.fromRectAndRadius(
                Rect.fromLTWH(gLeft, top + 2, gWidth, kPrRowHeight - 4),
                const Radius.circular(2)),
            Paint()
              ..style = PaintingStyle.stroke
              ..strokeWidth = 1
              ..color = const Color(0xFF5C6270));
      }
      final rect = Rect.fromLTWH(left, top + 1, width, kPrRowHeight - 2);
      canvas.drawRRect(
          RRect.fromRectAndRadius(rect, const Radius.circular(2)),
          Paint()..color = const Color(0xFF7FB2F0));
    }

    // Selection outline, following the drag preview when the note is moving.
    final selectionPaint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..color = const Color(0xFF4D9DF7);
    for (final index in selectedNoteIndices) {
      if (index < 0 || index >= clip.notes.length) continue;
      final note = clip.notes[index];
      final drag = dragPreviews[index];
      final left = (drag?.$1 ?? note.start) * kPrPixelsPerSecond;
      final width = ((drag?.$2 ?? note.length) * kPrPixelsPerSecond)
          .clamp(3.0, size.width);
      final top = _pitchToY(drag?.$3 ?? note.pitch, 0);
      canvas.drawRect(
          Rect.fromLTWH(left, top + 1, width, kPrRowHeight - 2),
          selectionPaint);
    }

    // Rubber-band rectangle for area selection.
    final marquee = marqueeRect;
    if (marquee != null && marquee.width > 0 && marquee.height > 0) {
      final clipped = marquee.intersect(Offset.zero & size);
      if (!clipped.isEmpty) {
        canvas.drawRect(
            clipped,
            Paint()
              ..color = const Color(0xFF4D9DF7).withValues(alpha: 0.12));
        canvas.drawRect(
            clipped,
            Paint()
              ..style = PaintingStyle.stroke
              ..strokeWidth = 1
              ..color = const Color(0xFF4D9DF7));
      }
    }
  }

  @override
  bool shouldRepaint(_GridPainter oldDelegate) =>
      oldDelegate.clip != clip ||
      oldDelegate.selectedNoteIndices != selectedNoteIndices ||
      oldDelegate.dragPreviews != dragPreviews ||
      oldDelegate.marqueeRect != marqueeRect ||
      oldDelegate.drawPreview != drawPreview;
}

/// Playhead line inside the piano roll (clip-local position). Listens to
/// the frame tick itself (timeline pattern) — no widget rebuilds needed.
class _PrPlayheadPainter extends CustomPainter {
  _PrPlayheadPainter({
    required this.clip,
    required this.service,
    required ValueListenable<int> repaint,
  }) : super(repaint: repaint);

  final ClipInfo clip;
  final EngineService service;

  @override
  void paint(Canvas canvas, Size size) {
    final x = (service.positionSeconds - clip.start) * kPrPixelsPerSecond;
    if (x < 0 || x > size.width) return;
    canvas.drawLine(
        Offset(x, 0),
        Offset(x, size.height),
        Paint()
          ..color = const Color(0xFFE5484D)
          ..strokeWidth = 1.5);
  }

  @override
  bool shouldRepaint(_PrPlayheadPainter oldDelegate) => true; // frame-driven
}
