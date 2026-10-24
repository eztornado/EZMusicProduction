// Timeline v3 — track headers (§7.2), bar ruler, move/trim/fade gestures,
// track selection with context menu.
//
// Gestures: tap = select clip, drag body = move, edge drag = trim, corner
// drag (selected clip) = fades. Snap to beat, ⌘ bypasses. Each completed
// gesture commits one engine command (one undo entry).

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
import 'plugin_picker_dialog.dart' show showPluginPickerDialog;

const double kPixelsPerSecond = 90;
const double kTrackRowHeight = 64;
const double kRulerHeight = 26;
const double kTimelineWidthSeconds = 60;
const double kHeaderWidth = 220;
const double kEdgeHotZonePx = 18; // trim from the clip corners (Mixcraft-style)
const double kFadeHandleSize = 10;
const double kMinClipSeconds = 0.05;

const double kSnapBeatSeconds = 0.5; // default; overridden by project tempo

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

({ClipInfo clip, ClipEdge edge})? hitTestClipEdge(
  ProjectSnapshot snapshot,
  Offset local, {
  double pixelsPerSecond = kPixelsPerSecond,
}) {
  final row = ((local.dy - kRulerHeight) / kTrackRowHeight).floor();
  if (row < 0 || row >= snapshot.tracks.length) return null;
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
}) {
  if (selectedClipId == null) return null;
  final row = ((local.dy - kRulerHeight) / kTrackRowHeight).floor();
  if (row < 0 || row >= snapshot.tracks.length) return null;
  for (final clip in snapshot.tracks[row].clips) {
    if (clip.id != selectedClipId) continue;
    final top = kRulerHeight + row * kTrackRowHeight + 22;
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
}) =>
    hitTestClipEdge(snapshot, local, pixelsPerSecond: pixelsPerSecond)?.clip.id;

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
  double _previewStart = 0, _previewLength = 0, _previewFade = -1;
  bool _didMove = false;
  double _panStartDx = 0; // absolute drag: pointer x at gesture start
  // Edge hit at the RAW press (pre-slop), consumed by _onPanStart.
  ({ClipInfo clip, ClipEdge edge})? _pressHit;
  // Trim affordance: the clip edge under the pointer (cursor + highlight).
  int? _hoverClipId;
  ClipEdge? _hoverEdge;

  void _updateHoverEdge(Offset viewportLocal) {
    final hit = hitTestClipEdge(
      widget.service.snapshot,
      Offset(_contentDx(viewportLocal.dx), _contentDy(viewportLocal.dy)),
      pixelsPerSecond: _pps,
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

  int _dragTargetRow = -1; // row the clip is being dragged over
  int _dragSourceType = 0;

  int _rowAt(double viewportDy) =>
      ((_contentDy(viewportDy) - kRulerHeight) / kTrackRowHeight).floor();

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

  void _onTapUp(TapUpDetails details) {
    final local = Offset(
      _contentDx(details.localPosition.dx),
      _contentDy(details.localPosition.dy),
    );
    final clipId = hitTestClip(
      widget.service.snapshot,
      local,
      pixelsPerSecond: _pps,
    );
    if (clipId != null) {
      widget.service.selectClip(clipId);
      // Clicking the timeline moves key focus here so S / Delete work.
      FocusScope.of(context).requestFocus(_focusNode);
      return;
    }
    widget.service.selectClip(null);
    // Ruler or empty space: move the playhead to the clicked position.
    final seconds = (_contentDx(details.localPosition.dx) / _pps).clamp(
      0.0,
      double.infinity,
    );
    widget.service.seek(seconds);
    FocusScope.of(context).requestFocus(_focusNode);
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
    );
    if (clipId == null) return;
    widget.service.selectClip(clipId);
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
          PopupMenuItem(
            value: 3,
            child: Text(context.l10n.aiGenerateClip),
          ),
          PopupMenuItem(
            value: 4,
            child: Text(context.l10n.aiRegenerateClip),
          ),
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
  // event, so clip drags are deterministic regardless of what the (inert)
  // scrollable's recognizer does with the arena. Begin on DOWN, update on
  // MOVE, commit on UP — absolute positioning from the press point.
  void _onPointerDown(PointerDownEvent d) {
    final snapshot = widget.service.snapshot;
    if (d.buttons != kPrimaryButton) return;
    // _panStartDx/_pressHit were captured by the raw onPointerDown above
    // (this IS that handler — begin the drag here, pre-slop).

    // Fade drags live on the corners ONLY with Alt held — the corners are
    // the trim handle (Mixcraft-style); a plain corner grab must trim, not
    // silently become an invisible fade drag.
    final fadeHit = _altHeld
        ? hitTestFadeHandle(
            snapshot,
            widget.service.selectedClipId,
            Offset(
              _contentDx(d.localPosition.dx),
              _contentDy(d.localPosition.dy),
            ),
            pixelsPerSecond: _pps,
          )
        : null;
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

    final hit = _pressHit;
    _pressHit = null;
    if (hit == null) {
      widget.service.selectClip(null);
      return;
    }
    widget.service.selectClip(hit.clip.id);
    _dragClipId = hit.clip.id;
    _dragEdge = hit.edge;
    _dragFadeIn = false;
    _dragTargetRow = _rowAt(d.localPosition.dy);
    _dragSourceType = hit.clip.isMidi ? 1 : 0;
    _origStart = hit.clip.start;
    _origLength = hit.clip.length;
    _origOffset = hit.clip.offset;
    _maxEnd = hit.clip.start + hit.clip.maxLength;
    _previewStart = hit.clip.start;
    _previewLength = hit.clip.length;
    _previewFade = -1;
    _didMove = false;
  }

  void _onPointerMove(PointerMoveEvent event) {
    if (_dragClipId == null) return;
    if (event.buttons & kPrimaryButton == 0) return;
    final snapshot = widget.service.snapshot;
    const minLen = kMinClipSeconds;

    if (_previewFade >= 0) {
      _previewFade = (_origFade + (event.localPosition.dx - _panStartDx) / _pps)
          .clamp(0.0, _origLength)
          .toDouble();
      _didMove = _previewFade != _origFade;
      setState(() {});
      return;
    }

    switch (_dragEdge) {
      case ClipEdge.move:
        // Absolute positioning from the gesture start: no pixel loss from
        // gesture slop, and the pointer's row drives cross-track moves.
        _previewStart = snapSeconds(
          _origStart + (event.localPosition.dx - _panStartDx) / _pps,
          bypass: !widget.service.snapToGrid || _snapBypassed,
          beatSeconds: snapshot.beatSeconds,
        );
        _previewStart = _previewStart.clamp(0.0, double.infinity);
        _previewLength = _origLength;
        final row = _rowAt(event.localPosition.dy);
        if (row >= 0 && row < snapshot.tracks.length) {
          final target = snapshot.tracks[row];
          if (target.type == _dragSourceType) _dragTargetRow = row;
        }
      case ClipEdge.left:
        // Trims snap to the grid when "Rejilla" is on (⌘ bypasses); free
        // otherwise. ABSOLUTE from the gesture start.
        // Left-extension limit: AUDIO clips stop where their source starts
        // (offset); MIDI clips are free to 0 — their hidden notes come back
        // when the border is extended (the old clamp locked the border at
        // the current start forever, so a left trim could never be undone
        // by dragging outward).
        final minStart = _dragSourceType == 1
            ? 0.0
            : (_origStart - _origOffset > 0 ? _origStart - _origOffset : 0.0);
        final maxStart = _origStart + _origLength - minLen;
        var raw = _origStart + (event.localPosition.dx - _panStartDx) / _pps;
        if (widget.service.snapToGrid && !_snapBypassed) {
          raw = snapSeconds(
            raw,
            bypass: false,
            beatSeconds: snapshot.beatSeconds,
          );
        }
        _previewStart = raw.clamp(minStart, maxStart).toDouble();
        _previewLength = _origLength + (_origStart - _previewStart);
      case ClipEdge.right:
        final minEnd = _origStart + minLen;
        var raw =
            _origStart +
            _origLength +
            (event.localPosition.dx - _panStartDx) / _pps;
        if (widget.service.snapToGrid && !_snapBypassed) {
          raw = snapSeconds(
            raw,
            bypass: false,
            beatSeconds: snapshot.beatSeconds,
          );
        }
        final end = raw
            .clamp(minEnd, _maxEnd > minEnd ? _maxEnd : minEnd)
            .toDouble();
        _previewStart = _origStart;
        _previewLength = end - _origStart;
    }
    if (_previewStart != _origStart || _previewLength != _origLength) {
      _didMove = true;
    }
    setState(() {});
  }

  void _onPointerUp(PointerUpEvent event) {
    final clipId = _dragClipId;
    final snapshot = widget.service.snapshot;
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
            final originalRow = _rowAtOfClip(clipId);
            if (_dragTargetRow >= 0 &&
                _dragTargetRow < snapshot.tracks.length &&
                _dragTargetRow != originalRow) {
              final targetTrack = snapshot.tracks[_dragTargetRow];
              final error = widget.service.moveClipToTrack(
                clipId,
                targetTrack.id,
                _previewStart,
              );
              if (error == null) {
                widget.service.selectTrack(targetTrack.id);
              }
            } else {
              widget.service.moveClip(clipId, _previewStart);
            }
          case ClipEdge.left || ClipEdge.right:
            // Offset grows when the left border moves INWARD (the source
            // window slides forward) — the old inverted sign made the
            // engine clamp it to 0 and the whole content translated.
            final newOffset = _origOffset + (_previewStart - _origStart);
            widget.service.trimClip(
              clipId,
              _previewStart,
              newOffset,
              _previewLength,
            );
        }
      }
    }
    _dragClipId = null;
    _didMove = false;
    _dragTargetRow = -1;
    _previewFade = -1;
    setState(() {});
  }

  void _onPointerCancel(PointerCancelEvent event) {
    _dragClipId = null;
    _didMove = false;
    _dragTargetRow = -1;
    _previewFade = -1;
    _pressHit = null;
    setState(() {});
  }

  int _rowAtOfClip(int clipId) {
    final tracks = widget.service.snapshot.tracks;
    for (var i = 0; i < tracks.length; i++) {
      for (final c in tracks[i].clips) {
        if (c.id == clipId) return i;
      }
    }
    return -1;
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
    if (key == LogicalKeyboardKey.delete ||
        key == LogicalKeyboardKey.backspace) {
      widget.service.deleteSelectedClip();
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
                      child: Text(
                        context.l10n.timelineTracks,
                        style: const TextStyle(
                          fontSize: 10,
                          color: Color(0xFF9BA1AE),
                        ),
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
                                track: snapshot.tracks[i],
                                service: widget.service,
                                isEvenRow: i.isEven,
                                selected:
                                    widget.service.selectedTrackId ==
                                    snapshot.tracks[i].id,
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
                          margin: const EdgeInsets.all(6),
                          alignment: Alignment.center,
                          decoration: BoxDecoration(
                            color: const Color(0xFF2C303A),
                            borderRadius: BorderRadius.circular(5),
                            border: Border.all(color: const Color(0xFF3E4350)),
                          ),
                          child: Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              const Icon(
                                Icons.add,
                                size: 14,
                                color: Color(0xFFE9EBF0),
                              ),
                              const SizedBox(width: 4),
                              Text(
                                context.l10n.timelineAddTrack,
                                style: const TextStyle(
                                  fontSize: 11,
                                  color: Color(0xFFE9EBF0),
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
                          margin: const EdgeInsets.all(6),
                          alignment: Alignment.center,
                          decoration: BoxDecoration(
                            color: const Color(0xFF2C303A),
                            borderRadius: BorderRadius.circular(5),
                            border: Border.all(color: const Color(0xFF3E4350)),
                          ),
                          child: Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              const Icon(
                                Icons.audio_file,
                                size: 14,
                                color: Color(0xFF3DD68C),
                              ),
                              const SizedBox(width: 4),
                              Text(
                                context.l10n.timelineImport,
                                style: const TextStyle(
                                  fontSize: 11,
                                  color: Color(0xFF3DD68C),
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
              const VerticalDivider(width: 1, color: Color(0xFF32363F)),
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
                        cursor: _hoverEdge == null
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
                            _panStartDx = d.localPosition.dx;
                            _pressHit = hitTestClipEdge(
                              widget.service.snapshot,
                              Offset(
                                _contentDx(d.localPosition.dx),
                                _contentDy(d.localPosition.dy),
                              ),
                              pixelsPerSecond: _pps,
                            );
                            _onPointerDown(d);
                          },
                          onPointerMove: _onPointerMove,
                          onPointerUp: _onPointerUp,
                          onPointerCancel: _onPointerCancel,
                          child: GestureDetector(
                            onTapUp: _onTapUp,
                            onSecondaryTapUp: _onSecondaryTapUp,
                            child: Container(
                              color: const Color(0xFF16171B),
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
                                                  selectedClipId: widget
                                                      .service
                                                      .selectedClipId,
                                                  dragClipId: _dragClipId,
                                                  hoverClipId: _hoverClipId,
                                                  hoverIsLeft:
                                                      _hoverEdge ==
                                                      ClipEdge.left,
                                                  previewStart: _previewStart,
                                                  previewLength: _previewLength,
                                                  previewFade: _previewFade,
                                                  previewFadeIsIn: _dragFadeIn,
                                                  pixelsPerSecond: _pps,
                                                  barSeconds:
                                                      snapshot.barSeconds,
                                                  vOffset: (_vScroll.hasClients
                                                      ? _vScroll.offset
                                                      : 0.0),
                                                  peaksFor: (clip) => widget
                                                      .service
                                                      .getClipPeaks(
                                                        clip,
                                                        pixelsPerSecond: _pps,
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
                                                    repaint: widget.frameTick,
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
        PopupMenuItem(value: 'canal', child: Text(context.l10n.timelineChannel)),
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
      builder: (context) => AlertDialog(
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
      builder: (context) => AlertDialog(
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
                        color: i == track.colorIndex
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
                          color: const Color(0xFF9BA1AE),
                        ),
                        const SizedBox(width: 4),
                        Expanded(
                          child: Text(
                            track.name,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(
                              color: Color(0xFFE9EBF0),
                              fontSize: 12,
                              fontWeight: FontWeight.w500,
                            ),
                          ),
                        ),
                        // Instrument slot (instrument tracks): tap picks a
                        // VST3/AU instrument, long-press returns to the
                        // internal synth.
                        if (track.type == 1) ...[
                          Tooltip(
                            message: context.l10n.aiGenerateAtPlayhead,
                            child: InkWell(
                              onTap: () => showAiGenerationDialog(
                                context,
                                service,
                                trackId: track.id,
                                startSeconds: service.positionSeconds,
                                initialMode: defaultAiModeForTrack(track),
                              ),
                              borderRadius: BorderRadius.circular(4),
                              child: const Padding(
                                padding: EdgeInsets.all(3),
                                child: Icon(Icons.auto_awesome,
                                    size: 13, color: Color(0xFF4D9DF7)),
                              ),
                            ),
                          ),
                          _InstrumentChip(track: track, service: service),
                        ],
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
                          Expanded(
                            child: _CompactSlider(
                              value: track.volume.clamp(0.0, 2.0).toDouble(),
                              min: 0,
                              max: 2,
                              onChangeEnd: (v) =>
                                  service.setTrackVolume(track.id, v),
                            ),
                          ),
                        ],
                      ),
                    ),
                    SizedBox(
                      height: 18,
                      child: Row(
                        children: [
                          const Text(
                            'L',
                            style: TextStyle(
                              fontSize: 8,
                              color: Color(0xFF5C6270),
                            ),
                          ),
                          Expanded(
                            child: _CompactSlider(
                              value: track.pan.clamp(-1.0, 1.0).toDouble(),
                              min: -1,
                              max: 1,
                              onChangeEnd: (v) =>
                                  service.setTrackPan(track.id, v),
                            ),
                          ),
                          const Text(
                            'R',
                            style: TextStyle(
                              fontSize: 8,
                              color: Color(0xFF5C6270),
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
    final label = hasPlugin && track.instrumentName.length > 10
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
      onDoubleTap: hasPlugin
          ? () => service.openTrackInstrumentEditor(track.id)
          : null,
      onLongPress: hasPlugin
          ? () => service.clearTrackInstrument(track.id)
          : null,
      child: Container(
        margin: const EdgeInsets.only(left: 4),
        padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 1),
        decoration: BoxDecoration(
          color: hasPlugin
              ? const Color(0xFF6E56CF).withValues(alpha: 0.25)
              : const Color(0xFF2C303A),
          borderRadius: BorderRadius.circular(3),
          border: Border.all(
            color: hasPlugin ? const Color(0xFF6E56CF) : Colors.transparent,
            width: 1,
          ),
        ),
        child: Text(
          label,
          style: TextStyle(
            fontSize: 8,
            fontWeight: FontWeight.w600,
            color: hasPlugin
                ? const Color(0xFFC9BFFF)
                : const Color(0xFF9BA1AE),
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
        child: Text(
          label,
          style: TextStyle(
            fontSize: 11,
            fontWeight: FontWeight.w600,
            color: active ? Colors.black : const Color(0xFF9BA1AE),
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
    required this.selectedClipId,
    required this.dragClipId,
    this.hoverClipId,
    this.hoverIsLeft = false,
    required this.previewStart,
    required this.previewLength,
    required this.previewFade,
    required this.previewFadeIsIn,
    required this.pixelsPerSecond,
    required this.barSeconds,
    required this.peaksFor,
    this.vOffset = 0.0,
    this.repaint,
  });

  final double vOffset; // vertical scroll of the track rows
  final Listenable? repaint;

  final ProjectSnapshot snapshot;
  final int? selectedClipId;
  final int? dragClipId;
  final int? hoverClipId; // clip edge under the pointer (trim affordance)
  final bool hoverIsLeft;
  final double previewStart;
  final double previewLength;
  final double previewFade; // >= 0 while a fade is being dragged
  final bool previewFadeIsIn;
  final double pixelsPerSecond;
  final double barSeconds;
  final PeakData? Function(ClipInfo clip) peaksFor;

  static const _accent = Color(0xFF4D9DF7);

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(
      Offset.zero & Size(size.width, kRulerHeight),
      Paint()..color = const Color(0xFF1E2026),
    );

    final tickPaint = Paint()
      ..color = const Color(0xFF3E4350)
      ..strokeWidth = 1;
    const textStyle = TextStyle(
      color: Color(0xFF9BA1AE),
      fontSize: 10,
      fontFamily: 'monospace',
    );
    final bars = (size.width / (barSeconds * pixelsPerSecond)).ceil();
    for (var bar = 0; bar <= bars; bar++) {
      final x = bar * barSeconds * pixelsPerSecond + 0.5;
      canvas.drawLine(
        Offset(x, kRulerHeight - 12),
        Offset(x, kRulerHeight),
        tickPaint,
      );
      TextPainter(
          text: TextSpan(text: '${bar + 1}', style: textStyle),
          textDirection: TextDirection.ltr,
        )
        ..layout()
        ..paint(canvas, Offset(x + 4, 4));
      for (var beat = 1; beat < 4; beat++) {
        final bx = x + beat * kSnapBeatSeconds * pixelsPerSecond;
        if (bx < size.width) {
          canvas.drawLine(
            Offset(bx, kRulerHeight - 6),
            Offset(bx, kRulerHeight),
            tickPaint,
          );
        }
      }
    }

    // Track rows scroll vertically UNDER the pinned ruler: clip to the
    // lane area and translate by the scroll offset.
    canvas.save();
    canvas.clipRect(
      Offset(0, kRulerHeight) & Size(size.width, size.height - kRulerHeight),
    );
    canvas.translate(0, -vOffset);
    for (var i = 0; i < snapshot.tracks.length; i++) {
      final track = snapshot.tracks[i];
      final top = kRulerHeight + i * kTrackRowHeight;
      final rowColor = i.isEven
          ? const Color(0xFF1B1D22)
          : const Color(0xFF1E2026);
      canvas.drawRect(
        Offset(0, top) & Size(size.width, kTrackRowHeight),
        Paint()..color = rowColor,
      );
      canvas.drawLine(
        Offset(0, top + kTrackRowHeight),
        Offset(size.width, top + kTrackRowHeight),
        Paint()..color = const Color(0xFF23262E),
      );

      final color = trackColor(track.colorIndex);
      for (final clip in track.clips) {
        final dragging = clip.id == dragClipId;
        final start = dragging ? previewStart : clip.start;
        final length = dragging ? previewLength : clip.length;
        final left = start * pixelsPerSecond;
        final clipWidth = (length * pixelsPerSecond)
            .clamp(4.0, size.width)
            .toDouble();
        final rect = Rect.fromLTWH(
          left,
          top + 22,
          clipWidth,
          kTrackRowHeight - 32,
        );
        final rounded = RRect.fromRectAndRadius(rect, const Radius.circular(4));
        canvas.drawRRect(
          rounded,
          Paint()..color = color.withValues(alpha: 0.82),
        );

        // Content: waveform for audio clips, mini piano-roll for MIDI.
        // Clipped to the clip rect: a TRIMMED clip must not paint its notes
        // beyond the (shortened) end — that read as "the trim did nothing,
        // the midi is still there".
        canvas.save();
        canvas.clipRect(rounded.outerRect);
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
          final notePaint = Paint()
            ..color = Colors.white.withValues(alpha: 0.8);
          for (final note in clip.notes) {
            final nx = left + note.start * pixelsPerSecond;
            final nw = (note.length * pixelsPerSecond).clamp(2.0, clipWidth);
            final ny = rect.bottom - (note.pitch - minP + 1) * slot;
            canvas.drawRect(
              Rect.fromLTWH(nx, ny, nw, math.max(2.0, slot)),
              notePaint,
            );
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
        canvas.restore();

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
            ..color = isSelected ? _accent : color,
        );

        if (isSelected) {
          // Trim grips.
          final grip = Paint()
            ..color = _accent.withValues(alpha: 0.9)
            ..strokeWidth = 2;
          canvas.drawLine(
            Offset(left + 3, top + 26),
            Offset(left + 3, top + kTrackRowHeight - 10),
            grip,
          );
          canvas.drawLine(
            Offset(left + clipWidth - 3, top + 26),
            Offset(left + clipWidth - 3, top + kTrackRowHeight - 10),
            grip,
          );
          // Fade handles: painted nothing — corners are the TRIM handle;
          // fades are Alt+drag on the corners (§7.3, Mixcraft-style trim).
        }
      }
    }

    // Trim affordance: bright accent on the hovered clip edge.
    if (hoverClipId != null) {
      for (var i = 0; i < snapshot.tracks.length; i++) {
        for (final clip in snapshot.tracks[i].clips) {
          if (clip.id != hoverClipId) continue;
          final top = kRulerHeight + i * kTrackRowHeight + 22;
          final x = (hoverIsLeft ? clip.start : clip.end) * pixelsPerSecond;
          canvas.drawLine(
            Offset(x, top),
            Offset(x, top + kTrackRowHeight - 32),
            Paint()
              ..color = _accent
              ..strokeWidth = 2,
          );
        }
      }
    }

    canvas.restore(); // end of the translated (scrolling) lane area
  }

  @override
  bool shouldRepaint(_TimelinePainter oldDelegate) =>
      oldDelegate.snapshot != snapshot ||
      oldDelegate.selectedClipId != selectedClipId ||
      oldDelegate.dragClipId != dragClipId ||
      oldDelegate.hoverClipId != hoverClipId ||
      oldDelegate.hoverIsLeft != hoverIsLeft ||
      oldDelegate.previewStart != previewStart ||
      oldDelegate.previewLength != previewLength ||
      oldDelegate.previewFade != previewFade ||
      oldDelegate.previewFadeIsIn != previewFadeIsIn ||
      oldDelegate.vOffset != vOffset ||
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

    // Recording ghost clips on armed tracks (§7.11: live draw). Instrument
    // tracks also show the take's notes as they arrive: waiting for the
    // finalize left the growing clip looking empty for the whole recording.
    if (service.isRecording) {
      final x1 = service.recordStartSeconds * pixelsPerSecond;
      final x2 = service.positionSeconds * pixelsPerSecond;
      final recPaint = Paint()..color = const Color(0x59E5484D);
      final tracks = service.snapshot.tracks;
      for (var i = 0; i < tracks.length; i++) {
        if (!tracks[i].armed) continue;
        final top = kRulerHeight + i * kTrackRowHeight + 22;
        final rect = Rect.fromLTWH(
          x1,
          top,
          (x2 - x1).clamp(2.0, size.width),
          kTrackRowHeight - 32,
        );
        canvas.drawRect(rect, recPaint);
        if (tracks[i].type != 1) continue; // audio take: no notes to draw
        _paintLiveNotes(canvas, rect, tracks[i].id, x1);
      }
    }

    final paint = Paint()
      ..color = const Color(0xFF4D9DF7)
      ..strokeWidth = 2;
    canvas.drawLine(Offset(x, 0), Offset(x, size.height), paint);
    canvas.drawCircle(
      Offset(x, 5),
      4,
      Paint()..color = const Color(0xFF4D9DF7),
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
    final slot = rect.height / range;
    final notePaint = Paint()..color = Colors.white.withValues(alpha: 0.9);
    for (final (pitch, start, length) in notes) {
      final nx = x1 + start * pixelsPerSecond;
      final nw = (length * pixelsPerSecond).clamp(2.0, rect.width);
      final ny = rect.bottom - (pitch - minP + 1) * slot;
      canvas.drawRect(
        Rect.fromLTWH(nx, ny, nw, math.max(2.0, slot)),
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
  });

  final ScrollController controller;
  final ProjectSnapshot snapshot;
  final double contentWidth;

  void _jumpToCenter(double stripX, double stripWidth) {
    // ignore: avoid_print
    print(
      'DBG jump stripX=$stripX stripW=$stripWidth clients=${controller.hasClients} contentW=$contentWidth',
    );
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
      builder: (context, _) => LayoutBuilder(
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
          final offset = controller.hasClients
              ? controller.offset.clamp(0.0, maxOffset)
              : 0.0;
          final scale = contentWidth > 0 ? stripWidth / contentWidth : 1.0;
          final winX = offset * scale;
          final winW = (viewport * scale).clamp(8.0, stripWidth);

          return GestureDetector(
            onTapUp: (d) => _jumpToCenter(d.localPosition.dx, stripWidth),
            onPanStart: (d) => _jumpToCenter(d.localPosition.dx, stripWidth),
            onPanUpdate: (d) => _jumpToCenter(d.localPosition.dx, stripWidth),
            child: CustomPaint(
              size: Size(stripWidth, stripHeight),
              painter: _NavStripPainter(
                snapshot: snapshot,
                colorFor: trackColor,
                scale: scale,
                winX: winX,
                winW: winW,
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
    required this.winX,
    required this.winW,
  });

  final ProjectSnapshot snapshot;
  final Color Function(int) colorFor;
  final double scale;
  final double winX;
  final double winW;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(
      Offset.zero & size,
      Paint()..color = const Color(0xFF1B1D22),
    );

    // Clip minimap marks (track color, one thin block per clip).
    final rows = math.max(1, snapshot.tracks.length);
    final rowH = math.max(1.0, (size.height - 4) / rows);
    for (var t = 0; t < snapshot.tracks.length; t++) {
      final track = snapshot.tracks[t];
      for (final clip in track.clips) {
        final x = clip.start * kPixelsPerSecond * scale;
        final w = (clip.length * kPixelsPerSecond * scale).clamp(
          2.0,
          size.width,
        );
        canvas.drawRect(
          Rect.fromLTWH(x, 2 + t * rowH, w, math.max(2.0, rowH - 1)),
          Paint()..color = colorFor(track.colorIndex).withValues(alpha: 0.8),
        );
      }
    }

    // Viewport window.
    canvas.drawRect(
      Rect.fromLTWH(winX, 1, winW, size.height - 2),
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2
        ..color = const Color(0xFF4D9DF7),
    );
    canvas.drawRect(
      Rect.fromLTWH(winX, 1, winW, size.height - 2),
      Paint()..color = const Color(0xFF4D9DF7).withValues(alpha: 0.08),
    );
  }

  @override
  bool shouldRepaint(_NavStripPainter oldDelegate) =>
      oldDelegate.winX != winX ||
      oldDelegate.winW != winW ||
      oldDelegate.snapshot != snapshot;
}
