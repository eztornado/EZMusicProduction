// Timeline v3 — track headers (§7.2), bar ruler, move/trim/fade gestures,
// track selection with context menu.
//
// Gestures: tap = select clip, drag body = move, edge drag = trim, corner
// drag (selected clip) = fades. Snap to beat, ⌘ bypasses. Each completed
// gesture commits one engine command (one undo entry).

import 'dart:async';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../engine/engine_service.dart';
import '../engine/project_snapshot.dart';
import '../l10n/l10n.dart';
import 'add_track_dialog.dart' show showAddTrackDialog;
import 'ai_generation_dialog.dart'
    show defaultAiModeForTrack, showAiGenerationDialog;
import 'channel_settings_dialog.dart' show showChannelSettingsDialog;
import 'design.dart';
import 'plugin_picker_dialog.dart' show showPluginPickerDialog;

const double kPixelsPerSecond = 90;
const double kTrackRowHeight = 64;
const double kRulerHeight = 28;
const double kTimelineWidthSeconds = 60;
const double kHeaderWidth = 220;
const double kEdgeHotZonePx = 18; // trim from the clip corners (Mixcraft-style)
const double kFadeHandleSize = 10;
const double kMinClipSeconds = 0.05;

// Clips nearly fill their lane (8 px breathing room top/bottom), like the
// remote app and a real console arrangement. Keep this in one place: hit
// testing, painting and the rubber band all derive from clipContentRect.
const double kClipTopInset = 8;
const double kClipBottomInset = 8;

/// Height of the per-track automation strip (volume lane) shown when the
/// timeline overlay is enabled. It is an ADDITION under the clip lane, as in
/// the mobile companion: the arrangement never reflows, it just grows by one
/// compact strip per track.
const double kAutomationLaneHeight = 34;

double trackStride({bool automation = false}) =>
    kTrackRowHeight + (automation ? kAutomationLaneHeight : 0);

const double kSnapBeatSeconds = 0.5; // default; overridden by project tempo

// Rubber-band / edge auto-scroll (§8.1): a drag near the viewport border
// keeps scrolling even while the pointer is stationary.
const double kMarqueeThresholdPx = 3;
const double kAutoScrollEdgePx = 40;
const double kAutoScrollMaxStepPx = 22;

/// Clip rectangle in timeline content coordinates (x = seconds * pps,
/// y = ruler + track row). Shared by painting, hit-testing and the marquee.
Rect clipContentRect(
  ClipInfo clip,
  int trackIndex, {
  double pixelsPerSecond = kPixelsPerSecond,
  bool automationOverlay = false,
}) {
  return Rect.fromLTWH(
    clip.start * pixelsPerSecond,
    kRulerHeight +
        trackIndex * trackStride(automation: automationOverlay) +
        kClipTopInset,
    math.max(4.0, clip.length * pixelsPerSecond),
    kTrackRowHeight - kClipTopInset - kClipBottomInset,
  );
}

/// Ids of every clip whose painted rectangle intersects [rect], in visual
/// order (track row, then clip-list order). Used by the rubber band.
Set<int> clipsIntersecting(
  ProjectSnapshot snapshot,
  Rect rect, {
  double pixelsPerSecond = kPixelsPerSecond,
  bool automationOverlay = false,
}) {
  final hits = <int>{};
  for (var row = 0; row < snapshot.tracks.length; row++) {
    for (final clip in snapshot.tracks[row].clips) {
      if (rect.overlaps(
        clipContentRect(
          clip,
          row,
          pixelsPerSecond: pixelsPerSecond,
          automationOverlay: automationOverlay,
        ),
      )) {
        hits.add(clip.id);
      }
    }
  }
  return hits;
}

/// Track that owns [clipId], or null when the id is stale.
TrackInfo? _trackForClip(ProjectSnapshot snapshot, int clipId) {
  for (final track in snapshot.tracks) {
    for (final clip in track.clips) {
      if (clip.id == clipId) return track;
    }
  }
  return null;
}

ClipInfo? _clipById(ProjectSnapshot snapshot, int clipId) {
  for (final track in snapshot.tracks) {
    for (final clip in track.clips) {
      if (clip.id == clipId) return clip;
    }
  }
  return null;
}

enum ClipEdge { move, left, right }

/// One clip captured at the start of a group drag. Keeps the original row,
/// track and start so the whole group can be moved by a shared delta.
class _ClipDrag {
  const _ClipDrag({
    required this.clipId,
    required this.start,
    required this.length,
    required this.row,
    required this.trackId,
    required this.trackType,
  });

  final int clipId;
  final double start;
  final double length;
  final int row;
  final int trackId;
  final int trackType;
}

/// A clip plus the row it must paint on (which may be a cross-track drag
/// preview target rather than its source row).
class _ClipDraw {
  const _ClipDraw({
    required this.clip,
    required this.track,
    required this.row,
    required this.order,
  });

  final ClipInfo clip;
  final TrackInfo track;
  final int row;
  final int order;
}

({ClipInfo clip, ClipEdge edge})? hitTestClipEdge(
  ProjectSnapshot snapshot,
  Offset local, {
  double pixelsPerSecond = kPixelsPerSecond,
  bool automationOverlay = false,
}) {
  final stride = trackStride(automation: automationOverlay);
  final row = ((local.dy - kRulerHeight) / stride).floor();
  if (row < 0 || row >= snapshot.tracks.length) return null;
  // The automation strip is not a clip: clicks there belong to the lane.
  if (automationOverlay &&
      local.dy - kRulerHeight - row * stride >= kTrackRowHeight) {
    return null;
  }
  final seconds = local.dx / pixelsPerSecond;
  // TOPMOST clip wins: clips paint in list order, so the visually-on-top
  // one is the LAST containing the point (overlapping pastes otherwise
  // make you drag an invisible clip underneath).
  for (final clip in snapshot.tracks[row].clips.reversed) {
    if (seconds >= clip.start && seconds <= clip.end) {
      final leftPx = clip.start * pixelsPerSecond;
      final rightPx = clip.end * pixelsPerSecond;
      // Generous, proportional edge zones: real grabs land 10-40 px inside
      // the border (measured), and a zone this wide makes trimming aimable
      // without pixel precision. The clip's middle stays "move".
      final widthPx = (clip.end - clip.start) * pixelsPerSecond;
      final zone = math.min(40.0, math.max(kEdgeHotZonePx, widthPx * 0.35));
      if (local.dx - leftPx <= zone) return (clip: clip, edge: ClipEdge.left);
      if (rightPx - local.dx <= zone) return (clip: clip, edge: ClipEdge.right);
      return (clip: clip, edge: ClipEdge.move);
    }
  }
  return null;
}

/// Fade-handle hot zone at the selected clip's top corners (§7.3).
/// Returns the clip id and whether it is the fade-in ('in') or out ('out').
(int, bool)? hitTestFadeHandle(
  ProjectSnapshot snapshot,
  int? selectedClipId,
  Offset local, {
  double pixelsPerSecond = kPixelsPerSecond,
  bool automationOverlay = false,
}) {
  if (selectedClipId == null) return null;
  final stride = trackStride(automation: automationOverlay);
  final row = ((local.dy - kRulerHeight) / stride).floor();
  if (row < 0 || row >= snapshot.tracks.length) return null;
  if (automationOverlay &&
      local.dy - kRulerHeight - row * stride >= kTrackRowHeight) {
    return null;
  }
  for (final clip in snapshot.tracks[row].clips) {
    if (clip.id != selectedClipId) continue;
    final top =
        kRulerHeight + row * stride + kClipTopInset;
    final leftPx = clip.start * pixelsPerSecond;
    final rightPx = clip.end * pixelsPerSecond;
    final inZone = Rect.fromLTWH(leftPx, top, kFadeHandleSize, kFadeHandleSize);
    final outZone = Rect.fromLTWH(
      rightPx - kFadeHandleSize,
      top,
      kFadeHandleSize,
      kFadeHandleSize,
    );
    if (inZone.contains(local)) return (clip.id, true);
    if (outZone.contains(local)) return (clip.id, false);
    return null;
  }
  return null;
}

int? hitTestClip(
  ProjectSnapshot snapshot,
  Offset local, {
  double pixelsPerSecond = kPixelsPerSecond,
  bool automationOverlay = false,
}) =>
    hitTestClipEdge(
      snapshot,
      local,
      pixelsPerSecond: pixelsPerSecond,
      automationOverlay: automationOverlay,
    )?.clip.id;

double snapSeconds(
  double raw, {
  required bool bypass,
  double beatSeconds = kSnapBeatSeconds,
}) {
  if (bypass) return raw;
  return (raw / beatSeconds).roundToDouble() * beatSeconds;
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
  final ScrollController _vScroll = ScrollController();
  // Cabeceras: controlador PROPIO espejado por notificación. Un mismo
  // controller en dos scrollables rompe controller.position (assert de
  // posiciones múltiples) y con él, el enrutado de la rueda.
  final ScrollController _headerVScroll = ScrollController();
  double _pps = kPixelsPerSecond;

  /// Per-track volume automation strips under the clips, as in the mobile
  /// companion. Off by default so the arrangement keeps its classic density.
  bool _automationOverlay = false;

  double get _stride =>
      trackStride(automation: _automationOverlay);

  void _openAutomationFor(int trackId) {
    widget.service.selectTrack(trackId);
    if (!widget.service.automationOpen) {
      widget.service.toggleAutomationOpen();
    }
    setState(() {});
  }

  @override
  void initState() {
    super.initState();
    widget.frameTick.addListener(_followPlayhead);
  }

  @override
  void didUpdateWidget(covariant TimelineView oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.frameTick != widget.frameTick) {
      oldWidget.frameTick.removeListener(_followPlayhead);
      widget.frameTick.addListener(_followPlayhead);
    }
  }

  /// Auto-scroll (§7.1): while playing, the view pages forward to keep the
  /// playhead visible (fixed left margin once it enters the last page).
  void _followPlayhead() {
    if (!_hScroll.hasClients || !widget.service.isPlaying) return;
    final viewport = _hScroll.position.viewportDimension;
    final x = widget.service.positionSeconds * _pps;
    const margin = 48.0;
    if (x > _hScroll.offset + viewport - margin || x < _hScroll.offset) {
      _hScroll.jumpTo(
        (x - viewport * 0.12).clamp(0.0, _hScroll.position.maxScrollExtent),
      );
    }
  }

  int? _dragClipId;
  ClipEdge _dragEdge = ClipEdge.move;
  bool _dragFadeIn = false;
  double _origStart = 0, _origLength = 0, _origOffset = 0, _maxEnd = 0;
  double _origFade = 0;
  double _previewFade = -1;
  bool _didMove = false;
  // Gesture coordinates. Content = viewport + current scroll offset; keeping
  // both lets edge auto-scroll advance the gesture while the pointer rests.
  double _panStartContentX = 0;
  double _panStartContentY = 0;
  double _pointerViewportX = 0;
  double _pointerViewportY = 0;
  double _pointerContentX = 0;
  double _pointerContentY = 0;
  int _dragSourceType = 0;
  // Group drag: every selected clip gets a preview start and (optionally) a
  // target row. A single clip uses the same maps.
  List<_ClipDrag> _dragGroup = const [];
  Map<int, double> _previewStarts = const {};
  Map<int, double> _previewLengths = const {};
  Map<int, int> _previewRows = const {};
  // Rubber-band selection.
  bool _marqueeActive = false;
  bool _marqueeMoved = false;
  Offset? _marqueeStartContent;
  Offset? _marqueeCurrentContent;
  Set<int> _marqueeBase = const {};
  int? _selectionAnchorClipId;
  double? _pendingSeekContentX;
  Timer? _autoScrollTimer;
  // Edge hit at the RAW press (pre-slop), consumed by _onPointerDown.
  ({ClipInfo clip, ClipEdge edge})? _pressHit;
  // Trim affordance: the clip edge under the pointer (cursor + highlight).
  int? _hoverClipId;
  ClipEdge? _hoverEdge;

  void _updateHoverEdge(Offset viewportLocal) {
    final hit = hitTestClipEdge(
      widget.service.snapshot,
      Offset(_contentDx(viewportLocal.dx), _contentDy(viewportLocal.dy)),
      pixelsPerSecond: _pps,
      automationOverlay: _automationOverlay,
    );
    final int? newId;
    final ClipEdge? newEdge;
    if (hit == null || hit.edge == ClipEdge.move) {
      newId = null;
      newEdge = null;
    } else {
      newId = hit.clip.id;
      newEdge = hit.edge;
    }
    if (newId != _hoverClipId || newEdge != _hoverEdge) {
      setState(() {
        _hoverClipId = newId;
        _hoverEdge = newEdge;
      });
    }
  }

  int _rowAtContent(double contentDy) =>
      ((contentDy - kRulerHeight) / _stride).floor();

  bool get _snapBypassed =>
      HardwareKeyboard.instance.isLogicalKeyPressed(
        LogicalKeyboardKey.metaLeft,
      ) ||
      HardwareKeyboard.instance.isLogicalKeyPressed(
        LogicalKeyboardKey.metaRight,
      );

  bool get _altHeld =>
      HardwareKeyboard.instance.isLogicalKeyPressed(
        LogicalKeyboardKey.altLeft,
      ) ||
      HardwareKeyboard.instance.isLogicalKeyPressed(
        LogicalKeyboardKey.altRight,
      );

  ClipInfo? _findClip(int clipId) {
    for (final track in widget.service.snapshot.tracks) {
      for (final clip in track.clips) {
        if (clip.id == clipId) return clip;
      }
    }
    return null;
  }

  /// Content-space x of a viewport-local position (the horizontal scroll
  /// shifts every gesture: without this offset, hit tests pointed at the
  /// wrong place as soon as the timeline was scrolled).
  double _contentDx(double viewportDx) => viewportDx + _hScroll.offset;
  // Vertical: the lane area scrolls with the track headers (same
  // controller), so every viewport Y becomes a content Y here.
  double _contentDy(double viewportDy) =>
      viewportDy + (_vScroll.hasClients ? _vScroll.offset : 0.0);

  /// Visual order used by Shift+click: top row first, then clip start.
  List<int> _orderedClipIds(ProjectSnapshot snapshot) {
    final entries = <({int id, int row, double start})>[];
    for (var row = 0; row < snapshot.tracks.length; row++) {
      for (final clip in snapshot.tracks[row].clips) {
        entries.add((id: clip.id, row: row, start: clip.start));
      }
    }
    entries.sort((a, b) {
      final byRow = a.row.compareTo(b.row);
      return byRow != 0 ? byRow : a.start.compareTo(b.start);
    });
    return [for (final e in entries) e.id];
  }

  void _selectRangeTo(int clipId) {
    final ordered = _orderedClipIds(widget.service.snapshot);
    final anchor = _selectionAnchorClipId;
    final anchorIndex = anchor == null ? -1 : ordered.indexOf(anchor);
    final targetIndex = ordered.indexOf(clipId);
    if (anchorIndex < 0 || targetIndex < 0) {
      widget.service.selectClips([clipId], primary: clipId);
      _selectionAnchorClipId = clipId;
      return;
    }
    final lo = math.min(anchorIndex, targetIndex);
    final hi = math.max(anchorIndex, targetIndex);
    widget.service.selectClips(ordered.sublist(lo, hi + 1), primary: clipId);
  }

  List<_ClipDrag> _buildDragGroup(Set<int> ids) {
    final snapshot = widget.service.snapshot;
    final group = <_ClipDrag>[];
    for (var row = 0; row < snapshot.tracks.length; row++) {
      final track = snapshot.tracks[row];
      for (final clip in track.clips) {
        if (!ids.contains(clip.id)) continue;
        group.add(
          _ClipDrag(
            clipId: clip.id,
            start: clip.start,
            length: clip.length,
            row: row,
            trackId: track.id,
            trackType: track.type,
          ),
        );
      }
    }
    return group;
  }

  /// Target row for every dragged clip. If any clip would leave the project
  /// or land on a different track type, the whole group stays on its
  /// original rows (the horizontal move still applies).
  Map<int, int> _targetRowsFor(int rowDelta) {
    final snapshot = widget.service.snapshot;
    final original = <int, int>{for (final g in _dragGroup) g.clipId: g.row};
    if (rowDelta == 0) return original;

    final target = <int, int>{};
    for (final g in _dragGroup) {
      final row = g.row + rowDelta;
      if (row < 0 || row >= snapshot.tracks.length) return original;
      if (snapshot.tracks[row].type != g.trackType) return original;
      target[g.clipId] = row;
    }
    return target;
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

  /// Timer-driven edge scroll: while a clip drag or marquee rests near the
  /// left/right (or top/bottom) border, the view keeps moving and the
  /// gesture continues under the stationary pointer.
  void _autoScrollTick() {
    if (!_marqueeActive && _dragClipId == null) {
      _stopAutoScroll();
      return;
    }
    final h = _hScroll.hasClients ? _hScroll.position : null;
    final v = _vScroll.hasClients ? _vScroll.position : null;
    var stepX = 0.0;
    var stepY = 0.0;

    if (h != null) {
      final fromLeft = _pointerViewportX;
      final fromRight = h.viewportDimension - _pointerViewportX;
      if (fromLeft < kAutoScrollEdgePx) {
        stepX = -_edgeSpeed(fromLeft);
      } else if (fromRight < kAutoScrollEdgePx) {
        stepX = _edgeSpeed(fromRight);
      }
    }
    if (v != null) {
      final fromTop = _pointerViewportY;
      final fromBottom = v.viewportDimension - _pointerViewportY;
      if (fromTop < kAutoScrollEdgePx) {
        stepY = -_edgeSpeed(fromTop);
      } else if (fromBottom < kAutoScrollEdgePx) {
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

    if (_marqueeActive) _marqueeMoved = true;
    _syncGestureAfterScroll();
  }

  void _syncGestureAfterScroll() {
    _pointerContentX = _contentDx(_pointerViewportX);
    _pointerContentY = _contentDy(_pointerViewportY);
    if (_marqueeActive) {
      _marqueeCurrentContent = Offset(_pointerContentX, _pointerContentY);
      _updateMarquee();
    } else if (_dragClipId != null) {
      _updateDragPreviewFromContent();
    }
  }

  double _edgeSpeed(double distance) {
    final t = (1 - distance / kAutoScrollEdgePx).clamp(0.0, 1.0);
    return kAutoScrollMaxStepPx * t;
  }

  void _updateMarquee() {
    final start = _marqueeStartContent;
    final current = _marqueeCurrentContent;
    if (start == null || current == null) return;

    final rect = Rect.fromPoints(start, current);
    if ((current - start).distance > kMarqueeThresholdPx) {
      _marqueeMoved = true;
    }
    final hits = clipsIntersecting(
      widget.service.snapshot,
      rect,
      pixelsPerSecond: _pps,
      automationOverlay: _automationOverlay,
    );
    final next = <int>{..._marqueeBase, ...hits};
    widget.service.selectClips(
      next,
      primary: hits.isNotEmpty ? hits.last : (next.isEmpty ? null : next.last),
    );
    setState(() {});
  }

  void _updateDragPreviewFromContent() {
    if (_dragClipId == null) return;
    final snapshot = widget.service.snapshot;
    const minLen = kMinClipSeconds;

    if (_previewFade >= 0) {
      _previewFade =
          (_origFade + (_pointerContentX - _panStartContentX) / _pps)
              .clamp(0.0, _origLength)
              .toDouble();
      _didMove = _previewFade != _origFade;
      setState(() {});
      return;
    }

    switch (_dragEdge) {
      case ClipEdge.move:
        final rawDelta = (_pointerContentX - _panStartContentX) / _pps;
        final snapped = snapSeconds(
          _origStart + rawDelta,
          bypass: !widget.service.snapToGrid || _snapBypassed,
          beatSeconds: snapshot.beatSeconds,
        );
        var delta = snapped - _origStart;
        final minStart =
            _dragGroup.isEmpty
                ? _origStart
                : _dragGroup.map((g) => g.start).reduce(math.min);
        if (delta < -minStart) delta = -minStart;

        final starts = <int, double>{
          for (final g in _dragGroup) g.clipId: g.start + delta,
        };
        if (starts.isEmpty && _dragClipId != null) {
          starts[_dragClipId!] = _origStart + delta;
        }
        final rowDelta =
            _rowAtContent(_pointerContentY) - _rowAtContent(_panStartContentY);
        final rows = _targetRowsFor(rowDelta);
        final rowsChanged = _dragGroup.any(
          (g) => (rows[g.clipId] ?? g.row) != g.row,
        );
        _previewStarts = starts;
        _previewLengths = const {};
        _previewRows = rows;
        _didMove = delta.abs() > 1e-9 || rowsChanged;
      case ClipEdge.left:
        // Left-extension limit: AUDIO clips stop where their source starts
        // (offset); MIDI clips are free to 0. Values stay ABSOLUTE from the
        // gesture start, so no gesture-slop pixel is lost.
        final minStart =
            _dragSourceType == 1
                ? 0.0
                : (_origStart - _origOffset > 0
                    ? _origStart - _origOffset
                    : 0.0);
        final maxStart = _origStart + _origLength - minLen;
        var raw = _origStart + (_pointerContentX - _panStartContentX) / _pps;
        if (widget.service.snapToGrid && !_snapBypassed) {
          raw = snapSeconds(
            raw,
            bypass: false,
            beatSeconds: snapshot.beatSeconds,
          );
        }
        final previewStart = raw.clamp(minStart, maxStart).toDouble();
        final previewLength = _origLength + (_origStart - previewStart);
        _previewStarts = {_dragClipId!: previewStart};
        _previewLengths = {_dragClipId!: previewLength};
        _previewRows = {
          _dragClipId!:
              _dragGroup.isEmpty
                  ? _rowAtContent(_panStartContentY)
                  : _dragGroup.single.row,
        };
        _didMove = previewStart != _origStart || previewLength != _origLength;
      case ClipEdge.right:
        final minEnd = _origStart + minLen;
        var raw =
            _origStart +
            _origLength +
            (_pointerContentX - _panStartContentX) / _pps;
        if (widget.service.snapToGrid && !_snapBypassed) {
          raw = snapSeconds(
            raw,
            bypass: false,
            beatSeconds: snapshot.beatSeconds,
          );
        }
        final end =
            raw.clamp(minEnd, _maxEnd > minEnd ? _maxEnd : minEnd).toDouble();
        final previewLength = end - _origStart;
        _previewStarts = {_dragClipId!: _origStart};
        _previewLengths = {_dragClipId!: previewLength};
        _previewRows = {
          _dragClipId!:
              _dragGroup.isEmpty
                  ? _rowAtContent(_panStartContentY)
                  : _dragGroup.single.row,
        };
        _didMove = previewLength != _origLength;
    }
    setState(() {});
  }

  void _commitGroupMove() {
    final snapshot = widget.service.snapshot;
    if (_dragGroup.length > 1) {
      final ids = <int>[];
      final trackIds = <int>[];
      final starts = <double>[];
      for (final g in _dragGroup) {
        final row = _previewRows[g.clipId] ?? g.row;
        if (row < 0 || row >= snapshot.tracks.length) continue;
        ids.add(g.clipId);
        trackIds.add(snapshot.tracks[row].id);
        starts.add(_previewStarts[g.clipId] ?? g.start);
      }
      if (ids.isNotEmpty) {
        widget.service.moveClips(ids, trackIds, starts);
        final primaryRow =
            _previewRows[_dragClipId] ?? _rowAtContent(_panStartContentY);
        if (primaryRow >= 0 && primaryRow < snapshot.tracks.length) {
          widget.service.selectTrack(snapshot.tracks[primaryRow].id);
        }
      }
      return;
    }

    final g = _dragGroup.isEmpty ? null : _dragGroup.single;
    if (g == null) return;
    final row = _previewRows[g.clipId] ?? g.row;
    final start = _previewStarts[g.clipId] ?? g.start;
    if (row != g.row && row >= 0 && row < snapshot.tracks.length) {
      final targetTrack = snapshot.tracks[row];
      final error = widget.service.moveClipToTrack(
        g.clipId,
        targetTrack.id,
        start,
      );
      if (error == null) widget.service.selectTrack(targetTrack.id);
    } else {
      widget.service.moveClip(g.clipId, start);
    }
  }

  double _contentSeconds(ProjectSnapshot snapshot) {
    var seconds =
        snapshot.durationSeconds < kTimelineWidthSeconds
            ? kTimelineWidthSeconds
            : snapshot.durationSeconds + 4;
    for (final entry in _previewStarts.entries) {
      final length =
          _previewLengths[entry.key] ?? _findClip(entry.key)?.length ?? 0;
      seconds = math.max(seconds, entry.value + length + 4);
    }
    return seconds;
  }

  /// Right-click on a clip: split at playhead / delete. On empty space the
  /// event is ignored (the track header owns the track menu).
  void _onSecondaryTapUp(TapUpDetails details) {
    final local = Offset(
      _contentDx(details.localPosition.dx),
      _contentDy(details.localPosition.dy),
    );
    final clipId = hitTestClip(
      widget.service.snapshot,
      local,
      pixelsPerSecond: _pps,
      automationOverlay: _automationOverlay,
    );
    if (clipId == null) return;
    // Right-clicking a clip that is already part of a group keeps the group
    // (Delete in the menu then removes the whole selection); otherwise it
    // becomes the new single selection.
    if (!widget.service.isClipSelected(clipId)) {
      widget.service.selectClip(clipId);
    } else {
      widget.service.selectClipForDrag(clipId);
    }
    final clip = _clipById(widget.service.snapshot, clipId);
    final midi = clip?.isMidi ?? false;
    final overlay =
        Overlay.of(context).context.findRenderObject() as RenderBox?;
    showMenu<int>(
      context: context,
      position: RelativeRect.fromRect(
        Rect.fromPoints(
          details.globalPosition,
          overlay?.localToGlobal(overlay.size.bottomRight(Offset.zero)) ??
              details.globalPosition,
        ),
        Offset.zero & overlay!.size,
      ),
      items: [
        PopupMenuItem(
          value: 1,
          child: Text(context.l10n.timelineSplitAtPlayhead),
        ),
        PopupMenuItem(value: 2, child: Text(context.l10n.timelineDeleteClip)),
        if (midi) ...[
          const PopupMenuDivider(),
          PopupMenuItem(value: 3, child: Text(context.l10n.aiGenerateClip)),
          PopupMenuItem(value: 4, child: Text(context.l10n.aiRegenerateClip)),
        ],
      ],
    ).then((value) async {
      if (!mounted) return;
      if (value == 1) {
        widget.service.splitClip(clipId, widget.service.positionSeconds);
      } else if (value == 2) {
        widget.service.deleteSelectedClip();
      } else if (value == 3 || value == 4) {
        final track = _trackForClip(widget.service.snapshot, clipId);
        final currentClip = _clipById(widget.service.snapshot, clipId);
        if (track == null || currentClip == null || !currentClip.isMidi) return;
        await showAiGenerationDialog(
          context,
          widget.service,
          trackId: track.id,
          clipId: value == 4 ? clipId : null,
          startSeconds: currentClip.start,
          replaceClip: value == 4,
        );
      }
    });
  }

  /// ⌘+wheel zooms around the view centre; plain wheel scrolls horizontally.
  void _onPointerSignal(PointerSignalEvent event) {
    if (event is PointerScrollEvent) {
      if (_snapBypassed) {
        setState(() {
          _pps = (_pps * (event.scrollDelta.dy < 0 ? 1.15 : 1 / 1.15)).clamp(
            12.0,
            600.0,
          );
        });
      } else if (HardwareKeyboard.instance.isShiftPressed) {
        // Shift+rueda = horizontal siempre (estándar DAW).
        if (_hScroll.hasClients) {
          final target = (_hScroll.position.pixels + event.scrollDelta.dy)
              .clamp(0.0, _hScroll.position.maxScrollExtent);
          _hScroll.position.jumpTo(target);
        }
      } else if (_vScroll.hasClients && _vScroll.position.maxScrollExtent > 0) {
        // Rueda vertical = desplazar las PISTAS cuando desbordan.
        final target = (_vScroll.offset + event.scrollDelta.dy).clamp(
          0.0,
          _vScroll.position.maxScrollExtent,
        );
        _vScroll.position.jumpTo(target);
      } else if (_hScroll.hasClients) {
        final target = (_hScroll.position.pixels + event.scrollDelta.dy).clamp(
          0.0,
          _hScroll.position.maxScrollExtent,
        );
        _hScroll.position.jumpTo(target);
      }
    }
  }

  // Raw pointer interaction (no gesture arena): the Listener receives every
  // event, so clip drags/marquees are deterministic regardless of what the
  // (inert) scrollable's recognizer does with the arena.
  void _onPointerDown(PointerDownEvent d) {
    final snapshot = widget.service.snapshot;
    if (d.buttons != kPrimaryButton) return;

    _pointerViewportX = d.localPosition.dx;
    _pointerViewportY = d.localPosition.dy;
    _pointerContentX = _contentDx(_pointerViewportX);
    _pointerContentY = _contentDy(_pointerViewportY);
    _panStartContentX = _pointerContentX;
    _panStartContentY = _pointerContentY;
    FocusScope.of(context).requestFocus(_focusNode);

    final keyboard = HardwareKeyboard.instance;
    final additive = keyboard.isShiftPressed || keyboard.isMetaPressed;
    final contentPoint = Offset(_pointerContentX, _pointerContentY);

    // Fade drags live on the corners ONLY with Alt held — the corners are
    // the trim handle (Mixcraft-style); a plain corner grab must trim, not
    // silently become an invisible fade drag.
    final fadeHit =
        _altHeld
            ? hitTestFadeHandle(
              snapshot,
              widget.service.selectedClipId,
              contentPoint,
              pixelsPerSecond: _pps,
              automationOverlay: _automationOverlay,
            )
            : null;
    if (fadeHit != null) {
      final clip = _findClip(fadeHit.$1);
      if (clip != null) {
        _dragClipId = clip.id;
        _dragEdge = ClipEdge.move;
        _dragFadeIn = fadeHit.$2;
        _dragSourceType = clip.isMidi ? 1 : 0;
        _origStart = clip.start;
        _origLength = clip.length;
        _origOffset = clip.offset;
        _maxEnd = clip.start + clip.maxLength;
        _origFade = _dragFadeIn ? clip.fadeIn : clip.fadeOut;
        _previewFade = _origFade;
        _dragGroup = _buildDragGroup({clip.id});
        _previewStarts = const {};
        _previewLengths = const {};
        _previewRows = const {};
        _didMove = false;
        _startAutoScroll();
        return;
      }
    }

    final hit = _pressHit;
    _pressHit = null;

    if (hit == null) {
      // Ruler click: seek on release. Empty lane: rubber-band selection.
      if (_pointerContentY < kRulerHeight) {
        _pendingSeekContentX = _pointerContentX;
        if (!additive) {
          widget.service.selectClip(null);
          _selectionAnchorClipId = null;
        }
        return;
      }

      if (!additive) {
        widget.service.selectClip(null);
        _selectionAnchorClipId = null;
      }
      _marqueeActive = true;
      _marqueeMoved = false;
      _marqueeBase = additive ? widget.service.selectedClipIds : <int>{};
      _marqueeStartContent = contentPoint;
      _marqueeCurrentContent = contentPoint;
      _startAutoScroll();
      setState(() {});
      return;
    }

    // Additive click: Shift extends a range, ⌘ toggles one clip.
    if (additive) {
      if (keyboard.isMetaPressed) {
        widget.service.toggleClipSelection(hit.clip.id);
        if (widget.service.isClipSelected(hit.clip.id)) {
          _selectionAnchorClipId = hit.clip.id;
        }
      } else {
        _selectRangeTo(hit.clip.id);
        _selectionAnchorClipId ??= hit.clip.id;
      }
      setState(() {});
      return;
    }

    if (!widget.service.isClipSelected(hit.clip.id)) {
      widget.service.selectClip(hit.clip.id);
    } else {
      widget.service.selectClipForDrag(hit.clip.id);
    }
    _selectionAnchorClipId = hit.clip.id;

    _dragClipId = hit.clip.id;
    _dragEdge = hit.edge;
    _dragFadeIn = false;
    _dragSourceType = hit.clip.isMidi ? 1 : 0;
    _origStart = hit.clip.start;
    _origLength = hit.clip.length;
    _origOffset = hit.clip.offset;
    _maxEnd = hit.clip.start + hit.clip.maxLength;
    _previewFade = -1;
    _didMove = false;

    // Body drags move the whole selection; edges/fades always edit one clip.
    final ids =
        hit.edge == ClipEdge.move
            ? widget.service.selectedClipIds
            : <int>{hit.clip.id};
    _dragGroup = _buildDragGroup(ids);
    _previewStarts = {for (final g in _dragGroup) g.clipId: g.start};
    _previewLengths = const {};
    _previewRows = {for (final g in _dragGroup) g.clipId: g.row};
    _startAutoScroll();
    setState(() {});
  }

  void _onPointerMove(PointerMoveEvent event) {
    if (event.buttons & kPrimaryButton == 0) return;
    _pointerViewportX = event.localPosition.dx;
    _pointerViewportY = event.localPosition.dy;
    _pointerContentX = _contentDx(_pointerViewportX);
    _pointerContentY = _contentDy(_pointerViewportY);

    if (_marqueeActive) {
      _marqueeCurrentContent = Offset(_pointerContentX, _pointerContentY);
      _updateMarquee();
      return;
    }
    if (_dragClipId == null) return;
    _updateDragPreviewFromContent();
  }

  void _onPointerUp(PointerUpEvent event) {
    _stopAutoScroll();

    final pendingSeek = _pendingSeekContentX;
    if (pendingSeek != null) {
      _pendingSeekContentX = null;
      widget.service.seek((pendingSeek / _pps).clamp(0.0, double.infinity));
      _resetGesture();
      setState(() {});
      return;
    }

    if (_marqueeActive) {
      final moved = _marqueeMoved;
      _marqueeActive = false;
      _marqueeStartContent = null;
      _marqueeCurrentContent = null;
      _marqueeBase = const {};
      _marqueeMoved = false;

      if (!moved) {
        // Plain click on empty space: deselect (unless additive) and seek.
        if (!HardwareKeyboard.instance.isShiftPressed &&
            !HardwareKeyboard.instance.isMetaPressed) {
          widget.service.selectClip(null);
          _selectionAnchorClipId = null;
        }
        widget.service.seek(
          (_pointerContentX / _pps).clamp(0.0, double.infinity),
        );
      }
      setState(() {});
      return;
    }

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
            _commitGroupMove();
          case ClipEdge.left || ClipEdge.right:
            final start = _previewStarts[clipId] ?? _origStart;
            final length = _previewLengths[clipId] ?? _origLength;
            // Offset grows when the left border moves INWARD (the source
            // window slides forward).
            final newOffset = _origOffset + (start - _origStart);
            widget.service.trimClip(clipId, start, newOffset, length);
        }
      }
    }
    _resetGesture();
    setState(() {});
  }

  void _onPointerCancel(PointerCancelEvent event) {
    _stopAutoScroll();
    _marqueeActive = false;
    _marqueeMoved = false;
    _marqueeStartContent = null;
    _marqueeCurrentContent = null;
    _marqueeBase = const {};
    _pendingSeekContentX = null;
    _resetGesture();
    setState(() {});
  }

  void _resetGesture() {
    _dragClipId = null;
    _didMove = false;
    _previewFade = -1;
    _dragGroup = const [];
    _previewStarts = const {};
    _previewLengths = const {};
    _previewRows = const {};
    _pressHit = null;
  }

  /// Opens the native macOS file picker and imports the selected audio.
  Future<void> _importAudioFile(BuildContext context) async {
    final result = await Process.run('osascript', [
      '-e',
      'set f to choose file with prompt "${context.l10n.timelineImportPrompt}"',
      '-e',
      'return POSIX path of f',
    ]);
    final path = result.stdout.toString().trim();
    if (path.isEmpty) return;
    widget.service.addClipToFirstTrack(path);
  }

  void _onKeyEvent(KeyEvent event) {
    if (event is! KeyDownEvent) return;
    final key = event.logicalKey;

    if (key == LogicalKeyboardKey.keyA &&
        HardwareKeyboard.instance.isMetaPressed) {
      widget.service.selectAllClips();
      _selectionAnchorClipId = widget.service.selectedClipId;
      return;
    }
    if (key == LogicalKeyboardKey.escape) {
      widget.service.clearClipSelection();
      _selectionAnchorClipId = null;
      return;
    }
    if (key == LogicalKeyboardKey.delete ||
        key == LogicalKeyboardKey.backspace) {
      widget.service.deleteSelectedClip();
      _selectionAnchorClipId = null;
      return;
    }
    if (key == LogicalKeyboardKey.keyS) {
      final clipId = widget.service.selectedClipId;
      if (clipId != null) {
        widget.service.splitClip(clipId, widget.service.positionSeconds);
      }
    }
  }

  @override
  void dispose() {
    _stopAutoScroll();
    widget.frameTick.removeListener(_followPlayhead);
    _focusNode.dispose();
    _hScroll.dispose();
    _vScroll.dispose();
    _headerVScroll.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: widget.service,
      builder: (context, _) {
        final snapshot = widget.service.snapshot;
        // Include the live preview so a group dragged past the current
        // project end can keep scrolling instead of hitting the old edge.
        final width = _contentSeconds(snapshot) * _pps;
        final rowsHeight = snapshot.tracks.length * _stride;

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
                      decoration: const BoxDecoration(
                        color: AppColors.bgPanel,
                        border: Border(
                          bottom: BorderSide(color: AppColors.border),
                          right: BorderSide(color: AppColors.border),
                        ),
                      ),
                      padding: const EdgeInsets.only(left: 10, right: 8),
                      child: Row(
                        children: [
                          const Icon(
                            Icons.straighten_rounded,
                            size: 11,
                            color: AppColors.textFaint,
                          ),
                          const SizedBox(width: 6),
                          Text(
                            context.l10n.timelineTracks.toUpperCase(),
                            style: const TextStyle(
                              fontSize: 9,
                              fontWeight: FontWeight.w700,
                              letterSpacing: 0.9,
                              color: AppColors.textDim,
                            ),
                          ),
                          const Spacer(),
                          Container(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 5,
                              vertical: 1,
                            ),
                            decoration: BoxDecoration(
                              color: AppColors.surfaceAlt,
                              borderRadius: BorderRadius.circular(4),
                              border: Border.all(color: AppColors.border),
                            ),
                            child: Text(
                              '${snapshot.tracks.length}',
                              style: const TextStyle(
                                fontSize: 8.5,
                                fontWeight: FontWeight.w600,
                                color: AppColors.textFaint,
                                fontFeatures: [FontFeature.tabularFigures()],
                              ),
                            ),
                          ),
                          const SizedBox(width: 6),
                          Tooltip(
                            message: context.l10n.appWindowAutomation,
                            child: MouseRegion(
                              cursor: SystemMouseCursors.click,
                              child: GestureDetector(
                                onTap: () => setState(() {
                                  _automationOverlay = !_automationOverlay;
                                  _hoverClipId = null;
                                  _hoverEdge = null;
                                }),
                                child: Container(
                                  width: 20,
                                  height: 16,
                                  decoration: BoxDecoration(
                                    color:
                                        _automationOverlay
                                            ? AppColors.accentSoft
                                            : Colors.transparent,
                                    borderRadius: BorderRadius.circular(3),
                                    border: Border.all(
                                      color:
                                          _automationOverlay
                                              ? AppColors.accent.withValues(
                                                alpha: 0.65,
                                              )
                                              : AppColors.border,
                                    ),
                                  ),
                                  child: Icon(
                                    Icons.multiline_chart_rounded,
                                    size: 11,
                                    color:
                                        _automationOverlay
                                            ? AppColors.accent
                                            : AppColors.textFaint,
                                  ),
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                    Expanded(
                      child: SingleChildScrollView(
                        // Espejo vertical de las pistas (ver el listener de
                        // notificaciones más abajo).
                        controller: _headerVScroll,
                        physics: const NeverScrollableScrollPhysics(),
                        child: Column(
                          children: [
                            for (var i = 0; i < snapshot.tracks.length; i++)
                              _TrackHeader(
                                index: i,
                                track: snapshot.tracks[i],
                                service: widget.service,
                                isEvenRow: i.isEven,
                                selected:
                                    widget.service.selectedTrackId ==
                                    snapshot.tracks[i].id,
                                automationOverlay: _automationOverlay,
                                onAutomation: () => _openAutomationFor(
                                    snapshot.tracks[i].id),
                              ),
                          ],
                        ),
                      ),
                    ),
                    SizedBox(
                      height: 34,
                      child: GestureDetector(
                        onTap: () async {
                          final result = await showAddTrackDialog(
                            context,
                            snapshot.tracks.length,
                          );
                          if (result != null) {
                            widget.service.addTrack(
                              result.name,
                              result.colorIndex,
                              type: result.type,
                            );
                          }
                        },
                        child: Container(
                          margin: const EdgeInsets.fromLTRB(7, 5, 7, 0),
                          alignment: Alignment.center,
                          decoration: BoxDecoration(
                            color: AppColors.accentSoft.withValues(alpha: 0.65),
                            borderRadius: BorderRadius.circular(6),
                            border: Border.all(
                              color: AppColors.accent.withValues(alpha: 0.4),
                            ),
                          ),
                          child: Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              const Icon(
                                Icons.add_rounded,
                                size: 14,
                                color: AppColors.accent,
                              ),
                              const SizedBox(width: 4),
                              Text(
                                context.l10n.timelineAddTrack,
                                style: const TextStyle(
                                  fontSize: 10.5,
                                  fontWeight: FontWeight.w600,
                                  color: AppColors.accent,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ),
                    SizedBox(
                      height: 34,
                      child: GestureDetector(
                        onTap: () => _importAudioFile(context),
                        child: Container(
                          margin: const EdgeInsets.fromLTRB(7, 5, 7, 6),
                          alignment: Alignment.center,
                          decoration: BoxDecoration(
                            color: AppColors.surfaceAlt,
                            borderRadius: BorderRadius.circular(6),
                            border: Border.all(color: AppColors.border),
                          ),
                          child: Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              const Icon(
                                Icons.audio_file_rounded,
                                size: 14,
                                color: AppColors.success,
                              ),
                              const SizedBox(width: 4),
                              Text(
                                context.l10n.timelineImport,
                                style: const TextStyle(
                                  fontSize: 10.5,
                                  fontWeight: FontWeight.w600,
                                  color: AppColors.textDim,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
              const VerticalDivider(width: 1, color: AppColors.border),
              Expanded(
                child: Column(
                  children: [
                    Expanded(
                      child: MouseRegion(
                        // Trim affordance: resize cursor + accent edge while
                        // hovering a clip border, so trimming is discoverable.
                        onHover: (e) => _updateHoverEdge(e.localPosition),
                        onExit: (_) {
                          if (_hoverClipId != null) {
                            setState(() {
                              _hoverClipId = null;
                              _hoverEdge = null;
                            });
                          }
                        },
                        cursor:
                            _hoverEdge == null
                                ? MouseCursor.defer
                                : (_hoverEdge == ClipEdge.left
                                    ? SystemMouseCursors.resizeLeft
                                    : SystemMouseCursors.resizeRight),
                        child: Listener(
                          onPointerSignal: _onPointerSignal,
                          // Raw down (pre-arena): the edge hit test MUST use
                          // this position — recognizer-based drags fire only
                          // after the gesture slop (~9 px), which shoves a real
                          // edge grab out of the hot zone.
                          onPointerDown: (d) {
                            _pressHit = hitTestClipEdge(
                              widget.service.snapshot,
                              Offset(
                                _contentDx(d.localPosition.dx),
                                _contentDy(d.localPosition.dy),
                              ),
                              pixelsPerSecond: _pps,
                              automationOverlay: _automationOverlay,
                            );
                            _onPointerDown(d);
                          },
                          onPointerMove: _onPointerMove,
                          onPointerUp: _onPointerUp,
                          onPointerCancel: _onPointerCancel,
                          child: GestureDetector(
                            onSecondaryTapUp: _onSecondaryTapUp,
                            child: Container(
                              color: AppColors.bgDeep,
                              child: NotificationListener<ScrollNotification>(
                                onNotification: (n) {
                                  // Las cabeceras siguen el scroll de las
                                  // pistas (mismo offset, otro controller).
                                  if (n.metrics.axis == Axis.vertical &&
                                      _headerVScroll.hasClients) {
                                    _headerVScroll.jumpTo(n.metrics.pixels);
                                  }
                                  return false;
                                },
                                child: ScrollConfiguration(
                                  // Sin arrastre por puntero (los gestos de
                                  // clip mandan): la rueda y el PULGAR del
                                  // scrollbar sí funcionan.
                                  behavior: ScrollConfiguration.of(
                                    context,
                                  ).copyWith(dragDevices: const {}),
                                  child: Scrollbar(
                                    controller: _vScroll,
                                    thumbVisibility: true,
                                    thickness: 8,
                                    child: SingleChildScrollView(
                                      scrollDirection: Axis.vertical,
                                      controller: _vScroll,
                                      physics: const ClampingScrollPhysics(),
                                      child: SingleChildScrollView(
                                        scrollDirection: Axis.horizontal,
                                        controller: _hScroll,
                                        // Drag-to-scroll OFF: every drag on the timeline
                                        // goes to the clip gestures (move/trim). Scroll
                                        // = wheel + nav strip (Cubase-style).
                                        physics:
                                            const NeverScrollableScrollPhysics(),
                                        child: SizedBox(
                                          width: width,
                                          height:
                                              rowsHeight + kRulerHeight + 40,
                                          child: Stack(
                                            children: [
                                              CustomPaint(
                                                size: Size(
                                                  width,
                                                  rowsHeight +
                                                      kRulerHeight +
                                                      40,
                                                ),
                                                painter: _TimelinePainter(
                                                  snapshot: snapshot,
                                                  selectedClipIds:
                                                      widget
                                                          .service
                                                          .selectedClipIds,
                                                  primaryClipId:
                                                      widget
                                                          .service
                                                          .selectedClipId,
                                                  dragClipId: _dragClipId,
                                                  hoverClipId: _hoverClipId,
                                                  hoverIsLeft:
                                                      _hoverEdge ==
                                                      ClipEdge.left,
                                                  previewStarts: _previewStarts,
                                                  previewLengths:
                                                      _previewLengths,
                                                  previewRows: _previewRows,
                                                  previewFade: _previewFade,
                                                  previewFadeIsIn: _dragFadeIn,
                                                  marqueeRect:
                                                      _marqueeMoved &&
                                                              _marqueeStartContent !=
                                                                  null &&
                                                              _marqueeCurrentContent !=
                                                                  null
                                                          ? Rect.fromPoints(
                                                            _marqueeStartContent!,
                                                            _marqueeCurrentContent!,
                                                          )
                                                          : null,
                                                  pixelsPerSecond: _pps,
                                                  barSeconds:
                                                      snapshot.barSeconds,
                                                  beatSeconds:
                                                      snapshot.beatSeconds,
                                                  automationOverlay:
                                                      _automationOverlay,
                                                  selectedTrackId:
                                                      widget
                                                          .service
                                                          .selectedTrackId,
                                                  vScroll: _vScroll,
                                                  peaksFor:
                                                      (clip) => widget.service
                                                          .getClipPeaks(
                                                            clip,
                                                            pixelsPerSecond:
                                                                _pps,
                                                          ),
                                                  repaint: Listenable.merge([
                                                    widget.frameTick,
                                                    _vScroll,
                                                  ]),
                                                ),
                                              ),
                                              RepaintBoundary(
                                                child: CustomPaint(
                                                  size: Size(
                                                    width,
                                                    rowsHeight +
                                                        kRulerHeight +
                                                        40,
                                                  ),
                                                  painter: _PlayheadPainter(
                                                    service: widget.service,
                                                    pixelsPerSecond: _pps,
                                                    automationOverlay:
                                                        _automationOverlay,
                                                    vScroll: _vScroll,
                                                    repaint: Listenable.merge([
                                                      widget.frameTick,
                                                      _vScroll,
                                                    ]),
                                                  ),
                                                ),
                                              ),
                                            ],
                                          ), // Stack
                                        ), // SizedBox (contenido completo)
                                      ), // scroll horizontal
                                    ), // scroll vertical (pistas)
                                  ), // ScrollConfiguration
                                ), // Scrollbar
                              ), // Container
                            ), // GestureDetector
                          ), // Listener
                        ), // MouseRegion
                      ), // NotificationListener
                    ), // Expanded (área de lanes)
                    SizedBox(
                      height: 14,
                      child: _TimelineNavStrip(
                        controller: _hScroll,
                        snapshot: snapshot,
                        contentWidth: width,
                        pixelsPerSecond: _pps,
                      ),
                    ),
                  ],
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
    required this.index,
    required this.track,
    required this.service,
    required this.isEvenRow,
    required this.selected,
    required this.automationOverlay,
    required this.onAutomation,
  });

  final int index;
  final TrackInfo track;
  final EngineService service;
  final bool isEvenRow;
  final bool selected;
  final bool automationOverlay;
  final VoidCallback onAutomation;

  Future<void> _showMenu(BuildContext context, Offset position) async {
    final overlay =
        Overlay.of(context).context.findRenderObject() as RenderBox?;
    final value = await showMenu<String>(
      context: context,
      position: RelativeRect.fromLTRB(
        position.dx,
        position.dy,
        overlay?.size.width ?? 0,
        overlay?.size.height ?? 0,
      ),
      items: [
        PopupMenuItem(
          value: 'rename',
          child: Text(context.l10n.timelineRename),
        ),
        PopupMenuItem(value: 'color', child: Text(context.l10n.timelineColor)),
        PopupMenuItem(
          value: 'canal',
          child: Text(context.l10n.timelineChannel),
        ),
        PopupMenuItem(
          value: 'input',
          child: Text(
            track.inputChannel == 0
                ? context.l10n.timelineInputLeft
                : context.l10n.timelineInputRight,
          ),
        ),
        PopupMenuItem(value: 'mute', child: Text(context.l10n.timelineMute)),
        PopupMenuItem(value: 'solo', child: Text(context.l10n.timelineSolo)),
        const PopupMenuDivider(),
        if (track.type == 1)
          PopupMenuItem(
            value: 'ai',
            child: Text(context.l10n.aiGenerateAtPlayhead),
          ),
        PopupMenuItem(
          value: 'delete',
          child: Text(context.l10n.timelineDeleteTrack),
        ),
      ],
    );
    if (!context.mounted) return;
    switch (value) {
      case 'rename':
        await _renameDialog(context);
      case 'color':
        await _colorDialog(context);
      case 'canal':
        await showChannelSettingsDialog(context, service, track.id);
      case 'input':
        service.setTrackInputChannel(track.id, track.inputChannel == 0 ? 1 : 0);
      case 'mute':
        service.setTrackMute(track.id, !track.mute);
      case 'solo':
        service.setTrackSolo(track.id, !track.solo);
      case 'ai':
        await showAiGenerationDialog(
          context,
          service,
          trackId: track.id,
          startSeconds: service.positionSeconds,
          initialMode: defaultAiModeForTrack(track),
        );
      case 'delete':
        service.removeTrack(track.id);
    }
  }

  Future<void> _renameDialog(BuildContext context) async {
    final controller = TextEditingController(text: track.name);
    final name = await showDialog<String>(
      context: context,
      builder:
          (context) => AlertDialog(
            title: Text(context.l10n.timelineRenameTrack),
            content: TextField(controller: controller, autofocus: true),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(context),
                child: Text(context.l10n.timelineCancel),
              ),
              TextButton(
                onPressed: () => Navigator.pop(context, controller.text),
                child: Text(context.l10n.timelineOk),
              ),
            ],
          ),
    );
    if (name != null && name.isNotEmpty) service.setTrackName(track.id, name);
  }

  Future<void> _colorDialog(BuildContext context) async {
    await showDialog<void>(
      context: context,
      builder:
          (context) => AlertDialog(
            title: Text(context.l10n.timelineTrackColor),
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
                            color:
                                i == track.colorIndex
                                    ? Colors.white
                                    : Colors.transparent,
                            width: 2,
                          ),
                        ),
                      ),
                    ),
                ],
              ),
            ),
          ),
    );
  }

  /// Compact strip under the header showing the volume curve lane. Tapping
  /// it selects the track and opens the full automation editor below.
  Widget _automationStrip(BuildContext context, Color color) {
    final hasAuto = track.volumeAuto.isNotEmpty;
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: onAutomation,
        child: Container(
          height: kAutomationLaneHeight,
          padding: const EdgeInsets.symmetric(horizontal: 7),
          decoration: BoxDecoration(
            color:
                selected
                    ? blend(
                      const Color(0xFF111318),
                      AppColors.accent,
                      0.07,
                    )
                    : const Color(0xFF111318),
            border: Border(
              top: BorderSide(
                color: AppColors.border.withValues(alpha: 0.55),
              ),
            ),
          ),
          child: Row(
            children: [
              Icon(
                Icons.multiline_chart_rounded,
                size: 11,
                color: hasAuto ? color : AppColors.textFaint,
              ),
              const SizedBox(width: 4),
              Text(
                context.l10n.automationVolume.toUpperCase(),
                style: TextStyle(
                  fontSize: 7.5,
                  fontWeight: FontWeight.w700,
                  letterSpacing: 0.7,
                  color: hasAuto ? AppColors.textDim : AppColors.textFaint,
                ),
              ),
              if (hasAuto) ...[
                const SizedBox(width: 5),
                Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 3,
                    vertical: 1,
                  ),
                  decoration: BoxDecoration(
                    color: color.withValues(alpha: 0.2),
                    borderRadius: BorderRadius.circular(3),
                  ),
                  child: Text(
                    'AUTO',
                    style: TextStyle(
                      fontSize: 6.5,
                      fontWeight: FontWeight.w800,
                      color: color,
                    ),
                  ),
                ),
              ],
              const Spacer(),
              Text(
                formatDb(track.volume),
                style: const TextStyle(
                  fontSize: 7.5,
                  fontWeight: FontWeight.w600,
                  color: AppColors.textFaint,
                  fontFeatures: [FontFeature.tabularFigures()],
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
    final fxCount = track.fxNames.length;
    return GestureDetector(
      onTap: () => service.selectTrack(track.id),
      onSecondaryTapUp: (details) => _showMenu(context, details.globalPosition),
      child: MouseRegion(
        cursor: SystemMouseCursors.click,
        child: Container(
          height: trackStride(automation: automationOverlay),
          decoration: BoxDecoration(
            color:
                selected
                    ? blend(AppColors.bgPanel, AppColors.accent, 0.075)
                    : (isEvenRow ? AppColors.bgPanel : const Color(0xFF171A20)),
          ),
          // Painted over the child: a border in `decoration` would deflate
          // the 64 px content box by 1 px and shift every row.
          foregroundDecoration: BoxDecoration(
            border: Border(
              bottom: BorderSide(
                color: AppColors.border.withValues(alpha: 0.75),
              ),
              left: BorderSide(
                color: selected ? AppColors.accent : Colors.transparent,
                width: 2,
              ),
            ),
          ),
          child: Column(
            children: [
              SizedBox(
                height: kTrackRowHeight,
                child: Row(
                  children: [
                    Container(
                      width: 3,
                      color: color.withValues(alpha: selected ? 1 : 0.85),
                    ),
              Expanded(
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(7, 2, 6, 2),
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      SizedBox(
                        height: 16,
                        child: Row(
                          children: [
                            Text(
                              (index + 1).toString().padLeft(2, '0'),
                              style: const TextStyle(
                                fontSize: 8,
                                fontWeight: FontWeight.w700,
                                color: AppColors.textFaint,
                                fontFeatures: [FontFeature.tabularFigures()],
                              ),
                            ),
                            const SizedBox(width: 5),
                            Expanded(
                              child: Text(
                                track.name,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                style: const TextStyle(
                                  color: AppColors.text,
                                  fontSize: 11.5,
                                  fontWeight: FontWeight.w600,
                                ),
                              ),
                            ),
                            if (track.type == 1)
                              Tooltip(
                                message: context.l10n.aiGenerateAtPlayhead,
                                child: InkWell(
                                  onTap:
                                      () => showAiGenerationDialog(
                                        context,
                                        service,
                                        trackId: track.id,
                                        startSeconds: service.positionSeconds,
                                        initialMode: defaultAiModeForTrack(
                                          track,
                                        ),
                                      ),
                                  borderRadius: BorderRadius.circular(4),
                                  child: const Padding(
                                    padding: EdgeInsets.all(2),
                                    child: Icon(
                                      Icons.auto_awesome,
                                      size: 11,
                                      color: AppColors.accent,
                                    ),
                                  ),
                                ),
                              ),
                            Builder(
                              builder:
                                  (buttonContext) => InkWell(
                                    onTap: () {
                                      final box =
                                          buttonContext.findRenderObject()
                                              as RenderBox?;
                                      final origin =
                                          box == null
                                              ? Offset.zero
                                              : box.localToGlobal(Offset.zero);
                                      _showMenu(
                                        buttonContext,
                                        origin +
                                            Offset(box?.size.width ?? 0, 18),
                                      );
                                    },
                                    borderRadius: BorderRadius.circular(4),
                                    child: const Padding(
                                      padding: EdgeInsets.all(2),
                                      child: Icon(
                                        Icons.more_horiz_rounded,
                                        size: 12,
                                        color: AppColors.textFaint,
                                      ),
                                    ),
                                  ),
                            ),
                          ],
                        ),
                      ),
                      SizedBox(
                        height: 13,
                        child: Row(
                          children: [
                            Icon(
                              track.type == 1
                                  ? Icons.piano_rounded
                                  : Icons.mic_none_rounded,
                              size: 9,
                              color: AppColors.textFaint,
                            ),
                            const SizedBox(width: 3),
                            if (track.type == 1)
                              Flexible(
                                child: _InstrumentChip(
                                  track: track,
                                  service: service,
                                ),
                              )
                            else
                              Container(
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 4,
                                  vertical: 1,
                                ),
                                decoration: BoxDecoration(
                                  color: AppColors.surfaceAlt,
                                  borderRadius: BorderRadius.circular(4),
                                  border: Border.all(color: AppColors.border),
                                ),
                                child: Text(
                                  'IN ${track.inputChannel == 0 ? 'L' : 'R'}',
                                  style: const TextStyle(
                                    fontSize: 7.5,
                                    fontWeight: FontWeight.w600,
                                    color: AppColors.textDim,
                                  ),
                                ),
                              ),
                            const Spacer(),
                            if (fxCount > 0)
                              GestureDetector(
                                onTap:
                                    () => showChannelSettingsDialog(
                                      context,
                                      service,
                                      track.id,
                                    ),
                                child: Container(
                                  padding: const EdgeInsets.symmetric(
                                    horizontal: 3,
                                    vertical: 1,
                                  ),
                                  decoration: BoxDecoration(
                                    color: AppColors.accentSoft.withValues(
                                      alpha: 0.65,
                                    ),
                                    borderRadius: BorderRadius.circular(3),
                                  ),
                                  child: Text(
                                    '$fxCount FX',
                                    style: const TextStyle(
                                      fontSize: 7,
                                      fontWeight: FontWeight.w700,
                                      color: AppColors.accent,
                                    ),
                                  ),
                                ),
                              ),
                          ],
                        ),
                      ),
                      SizedBox(
                        height: 18,
                        child: Row(
                          children: [
                            _MSButton(
                              label: 'M',
                              activeColor: AppColors.warning,
                              active: track.mute,
                              onTap:
                                  () => service.setTrackMute(
                                    track.id,
                                    !track.mute,
                                  ),
                            ),
                            const SizedBox(width: 3),
                            _MSButton(
                              label: 'S',
                              activeColor: AppColors.accent,
                              active: track.solo,
                              onTap:
                                  () => service.setTrackSolo(
                                    track.id,
                                    !track.solo,
                                  ),
                            ),
                            const SizedBox(width: 3),
                            _MSButton(
                              label: 'R',
                              activeColor: AppColors.record,
                              active: track.armed,
                              onTap:
                                  () => service.setTrackArmed(
                                    track.id,
                                    !track.armed,
                                  ),
                            ),
                            const SizedBox(width: 6),
                            Expanded(
                              child: _CompactSlider(
                                value: track.volume.clamp(0.0, 2.0).toDouble(),
                                min: 0,
                                max: 2,
                                color: color,
                                onChangeEnd:
                                    (v) => service.setTrackVolume(track.id, v),
                              ),
                            ),
                          ],
                        ),
                      ),
                      SizedBox(
                        height: 12,
                        child: Row(
                          children: [
                            const Text(
                              'L',
                              style: TextStyle(
                                fontSize: 7.5,
                                fontWeight: FontWeight.w600,
                                color: AppColors.textFaint,
                              ),
                            ),
                            Expanded(
                              child: _CompactSlider(
                                value: track.pan.clamp(-1.0, 1.0).toDouble(),
                                min: -1,
                                max: 1,
                                color: AppColors.textDim,
                                centerDetent: true,
                                onChangeEnd:
                                    (v) => service.setTrackPan(track.id, v),
                              ),
                            ),
                            const Text(
                              'R',
                              style: TextStyle(
                                fontSize: 7.5,
                                fontWeight: FontWeight.w600,
                                color: AppColors.textFaint,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
              ),
                  ],
                ),
              ),
              if (automationOverlay) _automationStrip(context, color),
            ],
          ),
        ),
      ),
    );
  }
}

/// Instrument slot chip for instrument tracks: shows the plugin name or
/// 'Sinte' (internal synth). Tap opens the instrument picker; long-press
/// clears a plugin back to the internal synth.
class _InstrumentChip extends StatelessWidget {
  const _InstrumentChip({required this.track, required this.service});

  final TrackInfo track;
  final EngineService service;

  @override
  Widget build(BuildContext context) {
    final hasPlugin = track.instrumentName.isNotEmpty;
    final label =
        hasPlugin && track.instrumentName.length > 10
            ? track.instrumentName.substring(0, 10)
            : hasPlugin
            ? track.instrumentName
            : context.l10n.timelineInternalSynth;
    return GestureDetector(
      onTap: () async {
        final picked = await showPluginPickerDialog(
          context,
          service,
          onlyInstruments: true,
          title: context.l10n.timelineTrackInstrument,
        );
        if (picked != null) {
          service.setTrackInstrumentPlugin(track.id, picked.index);
        }
      },
      onDoubleTap:
          hasPlugin ? () => service.openTrackInstrumentEditor(track.id) : null,
      onLongPress:
          hasPlugin ? () => service.clearTrackInstrument(track.id) : null,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 1),
        decoration: BoxDecoration(
          color:
              hasPlugin
                  ? AppColors.accentSoft.withValues(alpha: 0.7)
                  : AppColors.surfaceAlt,
          borderRadius: BorderRadius.circular(3),
          border: Border.all(
            color:
                hasPlugin
                    ? AppColors.accent.withValues(alpha: 0.45)
                    : AppColors.border,
            width: 1,
          ),
        ),
        child: Text(
          label,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: TextStyle(
            fontSize: 7.5,
            fontWeight: FontWeight.w600,
            color: hasPlugin ? AppColors.accent : AppColors.textDim,
          ),
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
    this.color = AppColors.accent,
    this.centerDetent = false,
  });

  final double value;
  final double min;
  final double max;
  final void Function(double) onChangeEnd;
  final Color color;
  final bool centerDetent;

  @override
  State<_CompactSlider> createState() => _CompactSliderState();
}

class _CompactSliderState extends State<_CompactSlider> {
  double? _dragValue;

  @override
  Widget build(BuildContext context) {
    final slider = SliderTheme(
      data: SliderTheme.of(context).copyWith(
        trackHeight: 2,
        activeTrackColor: widget.color.withValues(alpha: 0.85),
        inactiveTrackColor: AppColors.border,
        thumbColor: AppColors.text,
        overlayColor: widget.color.withValues(alpha: 0.12),
        thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 4),
        overlayShape: const RoundSliderOverlayShape(overlayRadius: 7),
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
    if (!widget.centerDetent) return slider;
    return Stack(
      alignment: Alignment.center,
      children: [
        slider,
        IgnorePointer(
          child: Container(
            width: 1,
            height: 6,
            color: AppColors.textFaint.withValues(alpha: 0.7),
          ),
        ),
      ],
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
    return Tooltip(
      message: switch (label) {
        'M' => context.l10n.timelineMute,
        'S' => context.l10n.timelineSolo,
        _ => context.l10n.transportRecord,
      },
      waitDuration: const Duration(milliseconds: 600),
      child: MouseRegion(
        cursor: SystemMouseCursors.click,
        child: GestureDetector(
          onTap: onTap,
          child: Container(
            width: 21,
            height: 17,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color:
                  active
                      ? activeColor.withValues(alpha: 0.18)
                      : AppColors.surfaceAlt,
              borderRadius: BorderRadius.circular(4),
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

// ---------- Painting ----------

class _TimelinePainter extends CustomPainter {
  _TimelinePainter({
    required this.snapshot,
    required this.selectedClipIds,
    required this.primaryClipId,
    required this.dragClipId,
    this.hoverClipId,
    this.hoverIsLeft = false,
    this.previewStarts = const {},
    this.previewLengths = const {},
    this.previewRows = const {},
    required this.previewFade,
    required this.previewFadeIsIn,
    this.marqueeRect,
    required this.pixelsPerSecond,
    required this.barSeconds,
    required this.beatSeconds,
    required this.automationOverlay,
    required this.selectedTrackId,
    required this.peaksFor,
    this.vScroll,
    this.repaint,
  });

  final ScrollController? vScroll; // live vertical offset for the sticky ruler
  final Listenable? repaint;

  final ProjectSnapshot snapshot;
  final Set<int> selectedClipIds;
  final int? primaryClipId;
  final int? dragClipId;
  final int? hoverClipId; // clip edge under the pointer (trim affordance)
  final bool hoverIsLeft;
  final Map<int, double> previewStarts;
  final Map<int, double> previewLengths;
  final Map<int, int> previewRows;
  final double previewFade; // >= 0 while a fade is being dragged
  final bool previewFadeIsIn;
  final Rect? marqueeRect;
  final double pixelsPerSecond;
  final double barSeconds;
  final double beatSeconds;
  final bool automationOverlay;
  final int? selectedTrackId;
  final PeakData? Function(ClipInfo clip) peaksFor;

  static const _accent = AppColors.accent;

  double get _stride => trackStride(automation: automationOverlay);

  @override
  void paint(Canvas canvas, Size size) {
    final barPx = barSeconds * pixelsPerSecond;
    final beatPx = beatSeconds * pixelsPerSecond;
    final vOffset = (vScroll?.hasClients ?? false) ? vScroll!.offset : 0.0;

    // The ruler is sticky: the outer vertical scroll already moves the
    // content, so painting it at the current scroll offset keeps bar numbers
    // and the playhead cap pinned while the lanes slide underneath.
    _paintRuler(canvas, size, barPx, beatPx, vOffset);

    // Track rows scroll vertically UNDER the pinned ruler: clip to the lane
    // area below it; the scrollable itself applies the vertical offset.
    canvas.save();
    canvas.clipRect(
      Rect.fromLTRB(0, vOffset + kRulerHeight, size.width, size.height),
    );

    _paintLanes(canvas, size, beatPx, barPx, snapshot.timeSigNumerator);

    final items = <_ClipDraw>[];
    var order = 0;
    for (var i = 0; i < snapshot.tracks.length; i++) {
      final track = snapshot.tracks[i];
      for (final clip in track.clips) {
        final row = previewRows[clip.id] ?? i;
        if (row < 0 || row >= snapshot.tracks.length) continue;
        items.add(
          _ClipDraw(clip: clip, track: track, row: row, order: order++),
        );
      }
    }
    items.sort((a, b) {
      final byRow = a.row.compareTo(b.row);
      return byRow != 0 ? byRow : a.order.compareTo(b.order);
    });

    for (final item in items) {
      _paintClip(canvas, item, size);
    }

    // Trim affordance: bright accent on the hovered clip edge.
    if (hoverClipId != null) {
      for (var i = 0; i < snapshot.tracks.length; i++) {
        for (final clip in snapshot.tracks[i].clips) {
          if (clip.id != hoverClipId) continue;
          final top = kRulerHeight + i * _stride + kClipTopInset;
          final height = kTrackRowHeight - kClipTopInset - kClipBottomInset;
          final x = (hoverIsLeft ? clip.start : clip.end) * pixelsPerSecond;
          canvas.drawLine(
            Offset(x, top),
            Offset(x, top + height),
            Paint()
              ..color = _accent.withValues(alpha: 0.22)
              ..strokeWidth = 5,
          );
          canvas.drawLine(
            Offset(x, top),
            Offset(x, top + height),
            Paint()
              ..color = _accent
              ..strokeWidth = 2,
          );
        }
      }
    }

    // Rubber-band rectangle (content coordinates; the canvas is already
    // translated by the vertical scroll and clipped to the lane area).
    final marquee = marqueeRect;
    if (marquee != null && marquee.width > 0 && marquee.height > 0) {
      canvas.drawRect(marquee, Paint()..color = _accent.withValues(alpha: 0.1));
      canvas.drawRect(
        marquee,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1
          ..color = _accent.withValues(alpha: 0.9),
      );
    }

    canvas.restore(); // end of the translated (scrolling) lane area
  }

  void _paintRuler(
    Canvas canvas,
    Size size,
    double barPx,
    double beatPx,
    double vOffset,
  ) {
    canvas.save();
    canvas.translate(0, vOffset);
    final ruler = Rect.fromLTWH(0, 0, size.width, kRulerHeight);
    canvas.drawRect(ruler, Paint()..color = AppColors.bgPanel);
    canvas.drawLine(
      const Offset(0, kRulerHeight - 0.5),
      Offset(size.width, kRulerHeight - 0.5),
      Paint()..color = AppColors.border,
    );

    var labelStep = 1;
    while (barPx * labelStep < 54) {
      labelStep *= 2;
    }

    final barTick =
        Paint()
          ..color = AppColors.borderStrong
          ..strokeWidth = 1;
    final beatTick =
        Paint()
          ..color = AppColors.textFaint.withValues(alpha: 0.5)
          ..strokeWidth = 1;
    final bars = (size.width / barPx).ceil();
    for (var bar = 0; bar <= bars; bar++) {
      final x = bar * barPx + 0.5;
      if (x > size.width + 2) continue;
      canvas.drawLine(
        Offset(x, kRulerHeight - 10),
        Offset(x, kRulerHeight),
        barTick,
      );
      if (bar % labelStep == 0) {
        final painter = TextPainter(
          text: TextSpan(
            text: '${bar + 1}',
            style: const TextStyle(
              color: AppColors.textDim,
              fontSize: 9.5,
              fontWeight: FontWeight.w600,
              fontFeatures: [FontFeature.tabularFigures()],
            ),
          ),
          textDirection: TextDirection.ltr,
        )..layout();
        painter.paint(canvas, Offset(x + 5, 5));
      }
      if (beatPx >= 9) {
        for (var beat = 1; beat < snapshot.timeSigNumerator; beat++) {
          final bx = x + beat * beatPx;
          if (bx > size.width) break;
          canvas.drawLine(
            Offset(bx, kRulerHeight - 5),
            Offset(bx, kRulerHeight),
            beatTick,
          );
        }
      }
    }
    canvas.restore();
  }

  void _paintLanes(
    Canvas canvas,
    Size size,
    double beatPx,
    double barPx,
    int beatsPerBar,
  ) {
    final stride = _stride;
    for (var i = 0; i < snapshot.tracks.length; i++) {
      final top = kRulerHeight + i * stride;
      final track = snapshot.tracks[i];
      final selected = track.id == selectedTrackId;
      final base = i.isEven ? AppColors.bgDeep : const Color(0xFF111318);
      canvas.drawRect(
        Rect.fromLTWH(0, top, size.width, kTrackRowHeight),
        Paint()..color = selected ? blend(base, AppColors.accent, 0.055) : base,
      );
      // A slim colour cue at the far left keeps tracks identifiable while
      // the arrangement is scrolled (the header shows the full bar).
      canvas.drawRect(
        Rect.fromLTWH(0, top + 6, 2, kTrackRowHeight - 12),
        Paint()
          ..color = trackColor(
            track.colorIndex,
          ).withValues(alpha: selected ? 0.95 : 0.4),
      );
      if (automationOverlay) {
        _paintAutomationLane(canvas, size, track, top, selected);
      } else {
        canvas.drawLine(
          Offset(0, top + kTrackRowHeight - 0.5),
          Offset(size.width, top + kTrackRowHeight - 0.5),
          Paint()..color = AppColors.border.withValues(alpha: 0.7),
        );
      }
    }

    // Grid: faint beats, slightly stronger bars, both behind the clips.
    if (beatPx >= 7) {
      final beatPaint =
          Paint()
            ..color = Colors.white.withValues(alpha: 0.028)
            ..strokeWidth = 1;
      final beats = (size.width / beatPx).ceil();
      for (var i = 0; i < beats; i++) {
        if (beatsPerBar > 0 && i % beatsPerBar == 0) continue;
        final x = i * beatPx;
        canvas.drawLine(Offset(x, 0), Offset(x, size.height), beatPaint);
      }
    }
    final barPaint =
        Paint()
          ..color = Colors.white.withValues(alpha: 0.07)
          ..strokeWidth = 1;
    final bars = (size.width / barPx).ceil();
    for (var i = 0; i <= bars; i++) {
      final x = i * barPx;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), barPaint);
    }
  }

  /// Volume automation strip attached under the clip lane. Mirrors the
  /// companion app: a tinted fill, the curve and its breakpoints, with the
  /// manual level as a faint baseline when the lane is empty.
  void _paintAutomationLane(
    Canvas canvas,
    Size size,
    TrackInfo track,
    double top,
    bool selected,
  ) {
    final laneTop = top + kTrackRowHeight;
    final laneRect = Rect.fromLTWH(
      0,
      laneTop,
      size.width,
      kAutomationLaneHeight,
    );
    final base = const Color(0xFF111318);
    canvas.drawRect(
      laneRect,
      Paint()
        ..color =
            selected ? blend(base, AppColors.accent, 0.07) : base,
    );
    canvas.drawLine(
      Offset(0, laneTop + 0.5),
      Offset(size.width, laneTop + 0.5),
      Paint()..color = AppColors.border.withValues(alpha: 0.8),
    );
    canvas.drawLine(
      Offset(0, laneTop + kAutomationLaneHeight - 0.5),
      Offset(size.width, laneTop + kAutomationLaneHeight - 0.5),
      Paint()..color = AppColors.border.withValues(alpha: 0.7),
    );

    final color = trackColor(track.colorIndex);
    double yFor(double value) =>
        laneTop +
        kAutomationLaneHeight -
        5 -
        value.clamp(0.0, 1.0) * (kAutomationLaneHeight - 10);

    final points = [...track.volumeAuto]
      ..sort((a, b) => a.time.compareTo(b.time));
    if (points.isEmpty) {
      final y = yFor(track.volume);
      canvas.drawLine(
        Offset(0, y),
        Offset(size.width, y),
        Paint()
          ..color = color.withValues(alpha: selected ? 0.4 : 0.28)
          ..strokeWidth = 1,
      );
      return;
    }

    final firstX = points.first.time * pixelsPerSecond;
    final path = Path()
      ..moveTo(0, yFor(points.first.value))
      ..lineTo(firstX, yFor(points.first.value));
    for (final point in points.skip(1)) {
      path.lineTo(
        point.time * pixelsPerSecond,
        yFor(point.value),
      );
    }
    final lastX = points.last.time * pixelsPerSecond;
    final fill =
        Path.from(path)
          ..lineTo(lastX, laneTop + kAutomationLaneHeight)
          ..lineTo(0, laneTop + kAutomationLaneHeight)
          ..close();
    canvas.drawPath(
      fill,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [
            color.withValues(alpha: selected ? 0.18 : 0.13),
            color.withValues(alpha: 0.02),
          ],
        ).createShader(laneRect),
    );
    canvas.drawPath(
      path,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.5
        ..strokeJoin = StrokeJoin.round
        ..color = color.withValues(alpha: selected ? 0.95 : 0.7),
    );
    for (final point in points) {
      final center = Offset(
        point.time * pixelsPerSecond,
        yFor(point.value),
      );
      if (center.dx < -4 || center.dx > size.width + 4) continue;
      canvas.drawCircle(center, 2.2, Paint()..color = color);
    }
  }

  void _paintClip(Canvas canvas, _ClipDraw item, Size size) {
    final clip = item.clip;
    final start = previewStarts[clip.id] ?? clip.start;
    final length = previewLengths[clip.id] ?? clip.length;
    final top = kRulerHeight + item.row * _stride;
    final color = trackColor(item.track.colorIndex);
    final dragging =
        previewStarts.containsKey(clip.id) ||
        (clip.id == dragClipId && previewFade >= 0);
    final left = start * pixelsPerSecond;
    final clipWidth =
        (length * pixelsPerSecond).clamp(4.0, size.width).toDouble();
    final rect = Rect.fromLTWH(
      left,
      top + kClipTopInset,
      clipWidth,
      kTrackRowHeight - kClipTopInset - kClipBottomInset,
    );
    final rounded = RRect.fromRectAndRadius(rect, const Radius.circular(6));
    final isPrimary = clip.id == primaryClipId;
    final isSelected = selectedClipIds.contains(clip.id) || dragging;

    // Soft console shadow behind the primary tile only: selecting a clip is
    // information, not decoration — the rest of the group just gets a
    // brighter border.
    if (isPrimary) {
      canvas.drawShadow(Path()..addRRect(rounded), Colors.black, 6, false);
    }

    canvas.drawRRect(
      rounded,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [
            blend(AppColors.surface, color, isSelected ? 0.30 : 0.22),
            blend(AppColors.bgDeep, color, isSelected ? 0.16 : 0.11),
          ],
        ).createShader(rect),
    );

    canvas.save();
    canvas.clipRRect(rounded);
    // Colour bar along the top edge: the strongest read of the track colour.
    canvas.drawRect(
      Rect.fromLTWH(rect.left, rect.top, rect.width, 3),
      Paint()..color = color.withValues(alpha: isSelected ? 1 : 0.82),
    );
    _paintClipLabel(canvas, clip, rect, isSelected);
    if (clip.isMidi) {
      _paintMidiNotes(canvas, clip, rect, color, isSelected);
    } else {
      _paintWaveform(canvas, clip, rect, color, isSelected);
    }
    _paintFades(canvas, clip, rect, dragging);
    canvas.restore();

    final borderColor =
        isPrimary
            ? AppColors.text
            : isSelected
            ? _accent
            : color.withValues(alpha: 0.55);
    canvas.drawRRect(
      rounded,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = isPrimary ? 1.4 : (isSelected ? 1.2 : 1)
        ..color = borderColor,
    );

    if (isPrimary && clipWidth > 32) {
      _paintTrimHandles(canvas, rect);
    }
  }

  void _paintClipLabel(
    Canvas canvas,
    ClipInfo clip,
    Rect rect,
    bool isSelected,
  ) {
    if (rect.width < 44) return;
    final kind = clip.isMidi ? 'MIDI' : 'AUDIO';
    final detail =
        clip.isMidi
            ? '${clip.notes.length}'
            : '${clip.length.toStringAsFixed(clip.length < 10 ? 2 : 1)}s';
    final painter = TextPainter(
      text: TextSpan(
        children: [
          TextSpan(
            text: kind,
            style: TextStyle(
              fontSize: 8.5,
              fontWeight: FontWeight.w700,
              letterSpacing: 0.3,
              color: AppColors.text.withValues(alpha: isSelected ? 0.95 : 0.86),
            ),
          ),
          if (rect.width > 116)
            TextSpan(
              text: '  $detail',
              style: TextStyle(
                fontSize: 8,
                fontWeight: FontWeight.w500,
                color: AppColors.text.withValues(alpha: 0.55),
              ),
            ),
        ],
      ),
      maxLines: 1,
      ellipsis: '…',
      textDirection: TextDirection.ltr,
    )..layout(maxWidth: math.max(1, rect.width - 12));
    painter.paint(canvas, Offset(rect.left + 6, rect.top + 5));
  }

  void _paintMidiNotes(
    Canvas canvas,
    ClipInfo clip,
    Rect rect,
    Color color,
    bool isSelected,
  ) {
    if (clip.notes.isEmpty) return;
    var minP = 127;
    var maxP = 0;
    for (final note in clip.notes) {
      minP = math.min(minP, note.pitch);
      maxP = math.max(maxP, note.pitch);
    }
    final range = math.max(1, maxP - minP + 1);
    final contentTop = rect.top + 16;
    final contentBottom = rect.bottom - 4;
    final slot = (contentBottom - contentTop) / range;
    final noteColor = Color.lerp(color, Colors.white, 0.68)!;
    for (final note in clip.notes) {
      final x = rect.left + note.start * pixelsPerSecond;
      final w = (note.length * pixelsPerSecond).clamp(2.0, rect.width);
      if (x > rect.right || x + w < rect.left) continue;
      final y = contentBottom - (note.pitch - minP + 1) * slot;
      final alpha =
          (0.42 + note.velocity.clamp(0.0, 1.0) * 0.5) *
          (isSelected ? 1.0 : 0.88);
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromLTWH(
            x.clamp(rect.left, rect.right),
            y,
            w,
            math.max(2.2, slot * 0.78),
          ),
          const Radius.circular(1),
        ),
        Paint()..color = noteColor.withValues(alpha: alpha),
      );
    }
  }

  void _paintWaveform(
    Canvas canvas,
    ClipInfo clip,
    Rect rect,
    Color color,
    bool isSelected,
  ) {
    final peaks = peaksFor(clip);
    final contentTop = rect.top + 16;
    final contentBottom = rect.bottom - 4;
    final center = (contentTop + contentBottom) / 2;
    final half = (contentBottom - contentTop) / 2;
    if (peaks == null) {
      canvas.drawLine(
        Offset(rect.left + 4, center),
        Offset(rect.right - 4, center),
        Paint()
          ..color = color.withValues(alpha: 0.35)
          ..strokeWidth = 1,
      );
      return;
    }
    final wavePaint =
        Paint()
          ..color = Color.lerp(
            color,
            Colors.white,
            0.58,
          )!.withValues(alpha: isSelected ? 0.62 : 0.48)
          ..strokeWidth = 1;
    final n = peaks.mins.length;
    for (var px = 0; px < n; px++) {
      final x = rect.left + px + 0.5;
      if (x > rect.right) break;
      final y1 = center - (peaks.maxs[px] * half).clamp(-half, half);
      final y2 = center - (peaks.mins[px] * half).clamp(-half, half);
      canvas.drawLine(Offset(x, y1), Offset(x, y2), wavePaint);
    }
  }

  void _paintFades(Canvas canvas, ClipInfo clip, Rect rect, bool dragging) {
    final fadeIn =
        dragging && previewFade >= 0 && previewFadeIsIn
            ? previewFade
            : clip.fadeIn;
    final fadeOut =
        dragging && previewFade >= 0 && !previewFadeIsIn
            ? previewFade
            : clip.fadeOut;
    final shade = Paint()..color = Colors.black.withValues(alpha: 0.26);

    if (fadeIn > 0) {
      final fx = (fadeIn * pixelsPerSecond).clamp(0.0, rect.width);
      canvas.drawPath(
        Path()
          ..moveTo(rect.left, rect.bottom)
          ..lineTo(rect.left, rect.top)
          ..lineTo(rect.left + fx, rect.top)
          ..close(),
        shade,
      );
      canvas.drawLine(
        Offset(rect.left, rect.bottom),
        Offset(rect.left + fx, rect.top),
        Paint()
          ..color = Colors.white.withValues(
            alpha: dragging && previewFadeIsIn ? 0.55 : 0.28,
          )
          ..strokeWidth = 1,
      );
    }
    if (fadeOut > 0) {
      final fx = (fadeOut * pixelsPerSecond).clamp(0.0, rect.width);
      canvas.drawPath(
        Path()
          ..moveTo(rect.right, rect.bottom)
          ..lineTo(rect.right, rect.top)
          ..lineTo(rect.right - fx, rect.top)
          ..close(),
        shade,
      );
      canvas.drawLine(
        Offset(rect.right, rect.bottom),
        Offset(rect.right - fx, rect.top),
        Paint()
          ..color = Colors.white.withValues(
            alpha: dragging && !previewFadeIsIn ? 0.55 : 0.28,
          )
          ..strokeWidth = 1,
      );
    }
  }

  void _paintTrimHandles(Canvas canvas, Rect rect) {
    final paint = Paint()..color = AppColors.text.withValues(alpha: 0.78);
    final y = rect.center.dy - 8;
    for (final x in [rect.left + 3.5, rect.right - 3.5]) {
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromLTWH(x - 1.5, y, 3, 16),
          const Radius.circular(2),
        ),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(_TimelinePainter oldDelegate) =>
      oldDelegate.snapshot != snapshot ||
      !setEquals(oldDelegate.selectedClipIds, selectedClipIds) ||
      oldDelegate.primaryClipId != primaryClipId ||
      oldDelegate.dragClipId != dragClipId ||
      oldDelegate.hoverClipId != hoverClipId ||
      oldDelegate.hoverIsLeft != hoverIsLeft ||
      !mapEquals(oldDelegate.previewStarts, previewStarts) ||
      !mapEquals(oldDelegate.previewLengths, previewLengths) ||
      !mapEquals(oldDelegate.previewRows, previewRows) ||
      oldDelegate.previewFade != previewFade ||
      oldDelegate.previewFadeIsIn != previewFadeIsIn ||
      oldDelegate.marqueeRect != marqueeRect ||
      oldDelegate.beatSeconds != beatSeconds ||
      oldDelegate.selectedTrackId != selectedTrackId ||
      oldDelegate.pixelsPerSecond != pixelsPerSecond;
}

class _PlayheadPainter extends CustomPainter {
  _PlayheadPainter({
    required this.service,
    required this.pixelsPerSecond,
    required this.automationOverlay,
    required this.vScroll,
    required Listenable repaint,
  }) : super(repaint: repaint);

  final EngineService service;
  final double pixelsPerSecond;
  final bool automationOverlay;
  final ScrollController vScroll;

  @override
  void paint(Canvas canvas, Size size) {
    final x = service.positionSeconds * pixelsPerSecond;
    final color = service.isRecording ? AppColors.record : AppColors.accent;
    final vOffset = (vScroll.hasClients ? vScroll.offset : 0.0).toDouble();

    // Everything but the ruler cap lives under the sticky ruler.
    canvas.save();
    canvas.clipRect(
      Rect.fromLTRB(0, vOffset + kRulerHeight, size.width, size.height),
    );

    // Recording ghost clips on armed tracks (§7.11: live draw). Instrument
    // tracks also show the take's notes as they arrive: waiting for the
    // finalize left the growing clip looking empty for the whole recording.
    if (service.isRecording) {
      final x1 = service.recordStartSeconds * pixelsPerSecond;
      final x2 = service.positionSeconds * pixelsPerSecond;
      final tracks = service.snapshot.tracks;
      for (var i = 0; i < tracks.length; i++) {
        if (!tracks[i].armed) continue;
        final top =
            kRulerHeight +
            i * trackStride(automation: automationOverlay) +
            kClipTopInset;
        final rect = Rect.fromLTWH(
          x1,
          top,
          (x2 - x1).clamp(2.0, size.width),
          kTrackRowHeight - kClipTopInset - kClipBottomInset,
        );
        final rounded = RRect.fromRectAndRadius(rect, const Radius.circular(6));
        canvas.drawRRect(
          rounded,
          Paint()..color = AppColors.record.withValues(alpha: 0.18),
        );
        canvas.drawRRect(
          rounded,
          Paint()
            ..style = PaintingStyle.stroke
            ..strokeWidth = 1
            ..color = AppColors.record.withValues(alpha: 0.75),
        );
        if (tracks[i].type != 1) continue; // audio take: no notes to draw
        _paintLiveNotes(canvas, rect, tracks[i].id, x1);
      }
    }

    // Glow + hairline: visible over both dark lanes and clips.
    canvas.drawLine(
      Offset(x, kRulerHeight),
      Offset(x, size.height),
      Paint()
        ..color = color.withValues(alpha: 0.14)
        ..strokeWidth = 7,
    );
    canvas.drawLine(
      Offset(x, kRulerHeight),
      Offset(x, size.height),
      Paint()
        ..color = color
        ..strokeWidth = 1.5,
    );
    canvas.restore();

    // Ruler cap lives ON the sticky ruler, so it is drawn after the lane
    // clip and pinned to the live scroll offset.
    final rulerBottom = vOffset + kRulerHeight;
    final cap =
        Path()
          ..moveTo(x - 5, rulerBottom - 10)
          ..lineTo(x + 5, rulerBottom - 10)
          ..lineTo(x, rulerBottom - 1)
          ..close();
    canvas.drawPath(cap, Paint()..color = color);
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Rect.fromLTWH(x - 2, rulerBottom - 12, 4, 5),
        const Radius.circular(1.5),
      ),
      Paint()..color = color,
    );
  }

  /// Notes of the in-progress MIDI take, drawn with the same pitch mapping a
  /// finalized MIDI clip uses. [x1] is the record start in content pixels.
  void _paintLiveNotes(Canvas canvas, Rect rect, int trackId, double x1) {
    final count = service.recordingNoteCount(trackId);
    if (count <= 0) return;
    final notes = <(int, double, double)>[];
    var minP = 127;
    var maxP = 0;
    for (var n = 0; n < count; n++) {
      final (pitch, _, start, length) = service.recordingNoteAt(trackId, n);
      notes.add((pitch, start, length));
      minP = math.min(minP, pitch);
      maxP = math.max(maxP, pitch);
    }
    final range = math.max(1, maxP - minP + 1);
    final contentTop = rect.top + 14;
    final contentBottom = rect.bottom - 4;
    final slot = (contentBottom - contentTop) / range;
    final notePaint =
        Paint()
          ..color = Color.lerp(
            AppColors.record,
            Colors.white,
            0.5,
          )!.withValues(alpha: 0.92);
    for (final (pitch, start, length) in notes) {
      final nx = x1 + start * pixelsPerSecond;
      final nw = (length * pixelsPerSecond).clamp(2.0, rect.width);
      final ny = contentBottom - (pitch - minP + 1) * slot;
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          Rect.fromLTWH(nx, ny, nw, math.max(2.0, slot * 0.8)),
          const Radius.circular(1),
        ),
        notePaint,
      );
    }
  }

  @override
  bool shouldRepaint(_PlayheadPainter oldDelegate) => true;
}

/// Always-visible navigation strip under the timeline (§7.1): a minimap of
/// the clips with a draggable viewport window. Custom-drawn because the
/// platform Scrollbar thumb sat on a clipped edge and was never seen.
class _TimelineNavStrip extends StatelessWidget {
  const _TimelineNavStrip({
    required this.controller,
    required this.snapshot,
    required this.contentWidth,
    required this.pixelsPerSecond,
  });

  final ScrollController controller;
  final ProjectSnapshot snapshot;
  final double contentWidth;
  final double pixelsPerSecond;

  void _jumpToCenter(double stripX, double stripWidth) {
    if (!controller.hasClients || contentWidth <= 0) return;
    final viewport = controller.position.viewportDimension;
    final target = (stripX / stripWidth) * contentWidth - viewport / 2;
    controller.jumpTo(
      target.clamp(0.0, controller.position.maxScrollExtent).toDouble(),
    );
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: controller,
      builder:
          (context, _) => LayoutBuilder(
            builder: (context, constraints) {
              final stripWidth = constraints.maxWidth;
              final stripHeight = constraints.maxHeight;
              final viewport =
                  (controller.hasClients &&
                          controller.position.hasViewportDimension)
                      ? controller.position.viewportDimension
                      : stripWidth;
              final maxOffset = (contentWidth - viewport).clamp(
                0.0,
                double.infinity,
              );
              final offset =
                  controller.hasClients
                      ? controller.offset.clamp(0.0, maxOffset)
                      : 0.0;
              final scale = contentWidth > 0 ? stripWidth / contentWidth : 1.0;
              final winX = offset * scale;
              final winW = (viewport * scale).clamp(8.0, stripWidth);

              return GestureDetector(
                onTapUp: (d) => _jumpToCenter(d.localPosition.dx, stripWidth),
                onPanStart:
                    (d) => _jumpToCenter(d.localPosition.dx, stripWidth),
                onPanUpdate:
                    (d) => _jumpToCenter(d.localPosition.dx, stripWidth),
                child: MouseRegion(
                  cursor: SystemMouseCursors.resizeLeftRight,
                  child: CustomPaint(
                    size: Size(stripWidth, stripHeight),
                    painter: _NavStripPainter(
                      snapshot: snapshot,
                      colorFor: trackColor,
                      scale: scale,
                      pixelsPerSecond: pixelsPerSecond,
                      winX: winX,
                      winW: winW,
                    ),
                  ),
                ),
              );
            },
          ),
    );
  }
}

class _NavStripPainter extends CustomPainter {
  _NavStripPainter({
    required this.snapshot,
    required this.colorFor,
    required this.scale,
    required this.pixelsPerSecond,
    required this.winX,
    required this.winW,
  });

  final ProjectSnapshot snapshot;
  final Color Function(int) colorFor;
  final double scale;
  final double pixelsPerSecond;
  final double winX;
  final double winW;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(Offset.zero & size, Paint()..color = AppColors.bgPanel);
    canvas.drawLine(
      Offset.zero,
      Offset(size.width, 0),
      Paint()..color = AppColors.border,
    );

    // Clip minimap marks (track color, one thin block per clip).
    final rows = math.max(1, snapshot.tracks.length);
    final rowH = math.max(1.0, (size.height - 4) / rows);
    for (var t = 0; t < snapshot.tracks.length; t++) {
      final track = snapshot.tracks[t];
      for (final clip in track.clips) {
        final x = clip.start * pixelsPerSecond * scale;
        final w = (clip.length * pixelsPerSecond * scale).clamp(
          2.0,
          size.width,
        );
        canvas.drawRRect(
          RRect.fromRectAndRadius(
            Rect.fromLTWH(x, 2 + t * rowH, w, math.max(2.0, rowH - 1)),
            const Radius.circular(1),
          ),
          Paint()..color = colorFor(track.colorIndex).withValues(alpha: 0.7),
        );
      }
    }

    // Viewport window.
    final window = Rect.fromLTWH(winX, 1, winW, size.height - 2);
    canvas.drawRect(
      window,
      Paint()..color = AppColors.accent.withValues(alpha: 0.08),
    );
    canvas.drawRect(
      window,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1
        ..color = AppColors.accent.withValues(alpha: 0.85),
    );
  }

  @override
  bool shouldRepaint(_NavStripPainter oldDelegate) =>
      oldDelegate.winX != winX ||
      oldDelegate.winW != winW ||
      oldDelegate.snapshot != snapshot ||
      oldDelegate.pixelsPerSecond != pixelsPerSecond;
}
