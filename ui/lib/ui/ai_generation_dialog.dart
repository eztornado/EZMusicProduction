// AI clip generation dialog (Magenta Studio / procedural fallback).
//
// The engine generates asynchronously on a worker thread. This dialog only
// polls state and previews the result; nothing touches the project until the
// user presses "Apply" (one undo entry).

import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import '../engine/project_snapshot.dart';
import '../l10n/l10n.dart';
import 'app_alert.dart';

const Color _kDialogBg = Color(0xFF252830);
const Color _kPanelBg = Color(0xFF1B1D22);
const Color _kTextDim = Color(0xFF9BA1AE);
const Color _kAccent = Color(0xFF4D9DF7);

/// Picks the most plausible generation mode for a track, mirroring the
/// engine's role inference.
int defaultAiModeForTrack(TrackInfo track) {
  final s = '${track.name} ${track.instrumentName}'.toLowerCase();
  if (s.contains('bass') || s.contains('bajo') || s.contains('sub')) {
    return AiMode.bass;
  }
  if (s.contains('drum') ||
      s.contains('bater') ||
      s.contains('perc') ||
      s.contains('kit')) {
    return AiMode.drums;
  }
  if (s.contains('pad') ||
      s.contains('chord') ||
      s.contains('armon') ||
      s.contains('keys') ||
      s.contains('piano') ||
      s.contains('organ')) {
    return AiMode.harmony;
  }
  return AiMode.melody;
}

/// Opens the AI dialog. Returns the created/rewritten clip id when applied.
///
/// [clipId] + [replaceClip] regenerate an existing MIDI clip in place.
Future<int?> showAiGenerationDialog(
  BuildContext context,
  EngineService service, {
  required int trackId,
  int? clipId,
  required double startSeconds,
  int? initialMode,
  bool replaceClip = false,
}) {
  return showDialog<int>(
    context: context,
    barrierDismissible: false,
    builder: (context) => _AiGenerationBody(
      service: service,
      trackId: trackId,
      clipId: clipId,
      startSeconds: startSeconds,
      initialMode: initialMode,
      replaceClip: replaceClip,
    ),
  );
}

class _AiGenerationBody extends StatefulWidget {
  const _AiGenerationBody({
    required this.service,
    required this.trackId,
    required this.clipId,
    required this.startSeconds,
    required this.initialMode,
    required this.replaceClip,
  });

  final EngineService service;
  final int trackId;
  final int? clipId;
  final double startSeconds;
  final int? initialMode;
  final bool replaceClip;

  @override
  State<_AiGenerationBody> createState() => _AiGenerationBodyState();
}

class _AiGenerationBodyState extends State<_AiGenerationBody> {
  late final TextEditingController _prompt = TextEditingController();
  late int _mode;
  late bool _replace;
  double _lengthSeconds = 30;
  double _temperature = 0.9;
  int _variations = 3;
  bool _harmonize = true;
  Timer? _timer;

  double get _barSeconds {
    final bpm = widget.service.tempoBpm;
    if (bpm <= 0) return 2.0;
    final num = widget.service.timeSigNumerator;
    final den = widget.service.timeSigDenominator;
    return (60.0 / bpm) * num * 4.0 / (den > 0 ? den : 1);
  }

  TrackInfo? get _track {
    for (final t in widget.service.snapshot.tracks) {
      if (t.id == widget.trackId) return t;
    }
    return null;
  }

  bool get _canReplace => widget.clipId != null && widget.clipId! > 0;

  @override
  void initState() {
    super.initState();
    _mode = widget.initialMode ??
        (_track == null ? AiMode.melody : defaultAiModeForTrack(_track!));
    _replace = widget.replaceClip && _canReplace;
    _timer = Timer.periodic(const Duration(milliseconds: 100), (_) {
      if (!mounted) return;
      widget.service.pollAiGeneration();
      setState(() {});
    });
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) _generate();
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    // Closing without applying must not leave a job running or a stale preview.
    if (widget.service.aiGeneration.isRunning) {
      widget.service.cancelAiGeneration();
    } else if (widget.service.aiGeneration.isReady ||
        widget.service.aiGeneration.isFailed) {
      widget.service.clearAiGeneration();
    }
    _prompt.dispose();
    super.dispose();
  }

  void _generate() {
    // A fresh seed per run makes "Regenerate" always produce a new take.
    final seed = DateTime.now().microsecondsSinceEpoch % 0x7ffffff;
    final longClip = _lengthSeconds > 8 * _barSeconds;
    widget.service.startAiGeneration(
      trackId: widget.trackId,
      clipId: _replace ? widget.clipId : null,
      anchorSeconds: widget.startSeconds,
      mode: _mode,
      bars: 4,
      lengthSeconds: _lengthSeconds,
      temperature: _temperature,
      variations: longClip ? 1 : _variations,
      seed: seed == 0 ? 1 : seed,
      prompt: _prompt.text.trim().isEmpty ? null : _prompt.text.trim(),
      replaceClip: _replace && _canReplace,
      harmonize: _harmonize,
    );
    setState(() {});
  }

  void _apply() {
    final clipId = widget.service.applyAiGeneration();
    if (clipId == null) {
      showAppError(context, widget.service.lastError);
      return;
    }
    Navigator.pop(context, clipId);
  }

  @override
  Widget build(BuildContext context) {
    final l = context.l10n;
    final ai = widget.service.aiGeneration;
    final trackName = _track?.name ?? '';
    final longClip = _lengthSeconds > 8 * _barSeconds;

    return AlertDialog(
      backgroundColor: _kDialogBg,
      titlePadding: const EdgeInsets.fromLTRB(20, 18, 20, 0),
      contentPadding: const EdgeInsets.fromLTRB(20, 12, 20, 0),
      actionsPadding: const EdgeInsets.fromLTRB(12, 8, 20, 14),
      title: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            children: [
              const Icon(Icons.auto_awesome, size: 18, color: _kAccent),
              const SizedBox(width: 8),
              Text(l.aiTitle, style: const TextStyle(fontSize: 15)),
            ],
          ),
          if (trackName.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(left: 26, top: 2),
              child: Text(
                l.aiTrackLabel(trackName),
                style: const TextStyle(fontSize: 11, color: _kTextDim),
              ),
            ),
        ],
      ),
      content: SizedBox(
        width: 480,
        child: SingleChildScrollView(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            mainAxisSize: MainAxisSize.min,
            children: [
              _modeChips(l),
              const SizedBox(height: 12),
              _field(
                label: l.aiLength,
                child: SegmentedButton<double>(
                  segments: const [
                    ButtonSegment(value: 8, label: Text('8s')),
                    ButtonSegment(value: 16, label: Text('16s')),
                    ButtonSegment(value: 30, label: Text('30s')),
                    ButtonSegment(value: 45, label: Text('45s')),
                    ButtonSegment(value: 60, label: Text('60s')),
                  ],
                  selected: {_lengthSeconds},
                  showSelectedIcon: false,
                  style: _segmentStyle(),
                  onSelectionChanged: ai.isRunning
                      ? null
                      : (v) => setState(() => _lengthSeconds = v.first),
                ),
              ),
              const SizedBox(height: 10),
              _field(
                label: l.aiVariations,
                child: SegmentedButton<int>(
                  segments: const [
                    ButtonSegment(value: 1, label: Text('1')),
                    ButtonSegment(value: 3, label: Text('3')),
                    ButtonSegment(value: 5, label: Text('5')),
                    ButtonSegment(value: 8, label: Text('8')),
                  ],
                  selected: {longClip ? 1 : _variations},
                  showSelectedIcon: false,
                  style: _segmentStyle(),
                  onSelectionChanged: ai.isRunning || longClip
                      ? null
                      : (v) => setState(() => _variations = v.first),
                ),
              ),
              if (longClip)
                Padding(
                  padding: const EdgeInsets.only(top: 4),
                  child: Text(
                    l.aiLongHint,
                    style: const TextStyle(fontSize: 10, color: _kTextDim),
                  ),
                ),
              const SizedBox(height: 10),
              _field(
                label: '${l.aiTemperature}  ${_temperature.toStringAsFixed(2)}',
                child: Slider(
                  value: _temperature,
                  min: 0.4,
                  max: 1.4,
                  divisions: 20,
                  activeColor: _kAccent,
                  onChanged: ai.isRunning
                      ? null
                      : (v) => setState(() => _temperature = v),
                ),
              ),
              TextField(
                controller: _prompt,
                enabled: !ai.isRunning,
                style: const TextStyle(fontSize: 12),
                decoration: InputDecoration(
                  labelText: l.aiPrompt,
                  hintText: l.aiPromptHint,
                  hintStyle:
                      const TextStyle(fontSize: 11, color: Color(0xFF6B7280)),
                  labelStyle: const TextStyle(fontSize: 11, color: _kTextDim),
                  isDense: true,
                  border: const OutlineInputBorder(),
                ),
                onSubmitted: (_) {
                  if (!ai.isRunning) _generate();
                },
              ),
              const SizedBox(height: 6),
              Wrap(
                spacing: 4,
                children: [
                  if (_mode != AiMode.drums && _mode != AiMode.bass)
                    _toggle(
                      label: l.aiHarmonize,
                      value: _harmonize,
                      enabled: !ai.isRunning,
                      onChanged: (v) => setState(() => _harmonize = v),
                    ),
                  if (_canReplace)
                    _toggle(
                      label: l.aiReplaceClip,
                      value: _replace,
                      enabled: !ai.isRunning,
                      onChanged: (v) => setState(() => _replace = v),
                    ),
                ],
              ),
              const SizedBox(height: 10),
              _statusPanel(l, ai),
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: Text(l.aiClose),
        ),
        TextButton(
          onPressed: ai.isRunning ? null : _generate,
          child: Text(ai.isReady || ai.isFailed ? l.aiRegenerate : l.aiGenerate),
        ),
        FilledButton(
          onPressed: ai.isReady ? _apply : null,
          style: FilledButton.styleFrom(backgroundColor: _kAccent),
          child: Text(l.aiApply),
        ),
      ],
    );
  }

  Widget _modeChips(AppLocalizations l) {
    final modes = <(int, String)>[
      (AiMode.melody, l.aiModeMelody),
      (AiMode.harmony, l.aiModeHarmony),
      (AiMode.bass, l.aiModeBass),
      (AiMode.drums, l.aiModeDrums),
    ];
    return Wrap(
      spacing: 6,
      runSpacing: 6,
      children: [
        for (final (value, label) in modes)
          ChoiceChip(
            label: Text(label, style: const TextStyle(fontSize: 11)),
            selected: _mode == value,
            showCheckmark: false,
            visualDensity: VisualDensity.compact,
            onSelected: widget.service.aiGeneration.isRunning
                ? null
                : (_) => setState(() => _mode = value),
          ),
      ],
    );
  }

  Widget _field({required String label, required Widget child}) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(label, style: const TextStyle(fontSize: 11, color: _kTextDim)),
        const SizedBox(height: 3),
        child,
      ],
    );
  }

  Widget _toggle({
    required String label,
    required bool value,
    required bool enabled,
    required ValueChanged<bool> onChanged,
  }) {
    return InkWell(
      onTap: enabled ? () => onChanged(!value) : null,
      borderRadius: BorderRadius.circular(6),
      child: Padding(
        padding: const EdgeInsets.only(right: 8),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Checkbox(
              value: value,
              visualDensity: VisualDensity.compact,
              materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
              onChanged: enabled ? (v) => onChanged(v ?? false) : null,
            ),
            Text(label, style: const TextStyle(fontSize: 11)),
          ],
        ),
      ),
    );
  }

  ButtonStyle _segmentStyle() => ButtonStyle(
        visualDensity: VisualDensity.compact,
        textStyle: WidgetStateProperty.all(const TextStyle(fontSize: 11)),
        padding: WidgetStateProperty.all(
          const EdgeInsets.symmetric(horizontal: 8),
        ),
      );

  Widget _statusPanel(AppLocalizations l, AiGenerationSnapshot ai) {
    if (ai.isRunning) {
      return _panel(
        Row(
          children: [
            const SizedBox(
              width: 16,
              height: 16,
              child: CircularProgressIndicator(strokeWidth: 2),
            ),
            const SizedBox(width: 10),
            Text(l.aiGenerating,
                style: const TextStyle(fontSize: 12, color: _kTextDim)),
          ],
        ),
      );
    }

    if (ai.isFailed) {
      return _panel(
        Text(
          l.aiFailed(ai.error.isEmpty ? '?' : ai.error),
          style: const TextStyle(fontSize: 12, color: Color(0xFFE5484D)),
        ),
      );
    }

    if (!ai.isReady) {
      return _panel(
        Text(l.aiIdleHint,
            style: const TextStyle(fontSize: 11, color: _kTextDim)),
      );
    }

    final modelOk = widget.service.aiModelAvailable(_mode);
    return _panel(
      Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          if (!modelOk)
            Padding(
              padding: const EdgeInsets.only(bottom: 6),
              child: Text(l.aiNoModel,
                  style: const TextStyle(
                      fontSize: 10, color: Color(0xFFF0A020))),
            ),
          _preview(ai),
          const SizedBox(height: 8),
          Wrap(
            spacing: 16,
            runSpacing: 4,
            children: [
              _fact(l.aiProviderLabel, ai.provider),
              if (ai.key.isNotEmpty) _fact(l.aiKeyLabel, ai.key),
              if (ai.chords.isNotEmpty) _fact(l.aiChordsLabel, ai.chords),
              _fact(l.aiNotesLabel, '${ai.notes.length}'),
            ],
          ),
          if (ai.contextNote.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(top: 6),
              child: Text(
                l.aiHeardLabel(ai.contextNote),
                style: const TextStyle(fontSize: 10, color: _kTextDim),
              ),
            ),
          if (ai.promptNote.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(top: 6),
              child: Text(
                l.aiPromptUnderstood(ai.promptNote),
                style: const TextStyle(fontSize: 10, color: _kTextDim),
              ),
            ),
        ],
      ),
    );
  }

  Widget _panel(Widget child) => Container(
        width: double.infinity,
        padding: const EdgeInsets.all(10),
        decoration: BoxDecoration(
          color: _kPanelBg,
          borderRadius: BorderRadius.circular(6),
          border: Border.all(color: const Color(0xFF32363F)),
        ),
        child: child,
      );

  Widget _fact(String label, String value) {
    return RichText(
      text: TextSpan(
        style: const TextStyle(fontSize: 10, color: _kTextDim),
        children: [
          TextSpan(text: '$label: '),
          TextSpan(
            text: value,
            style: const TextStyle(color: Color(0xFFE6E8EC)),
          ),
        ],
      ),
    );
  }

  Widget _preview(AiGenerationSnapshot ai) {
    final length = math.max(0.01, ai.clipLengthSeconds);
    return SizedBox(
      height: 92,
      width: double.infinity,
      child: CustomPaint(
        painter: _AiPreviewPainter(
          ai.notes,
          length,
          math.max(1, (length / _barSeconds).ceil()),
        ),
      ),
    );
  }
}

class _AiPreviewPainter extends CustomPainter {
  _AiPreviewPainter(this.notes, this.lengthSeconds, this.bars);

  final List<AiNote> notes;
  final double lengthSeconds;
  final int bars;

  @override
  void paint(Canvas canvas, Size size) {
    final bg = Paint()..color = const Color(0xFF14161A);
    final r = RRect.fromRectAndRadius(Offset.zero & size, const Radius.circular(5));
    canvas.drawRRect(r, bg);

    final grid = Paint()
      ..color = const Color(0xFF2C3038)
      ..strokeWidth = 1;

    // Bar lines + beat lines.
    final beatCount = bars * 4;
    for (var i = 0; i <= beatCount; i++) {
      final x = size.width * i / beatCount;
      final isBar = i % 4 == 0;
      canvas.drawLine(
        Offset(x, 0),
        Offset(x, size.height),
        Paint()
          ..color = isBar ? const Color(0xFF3E444E) : const Color(0xFF23262C)
          ..strokeWidth = 1,
      );
    }
    canvas.drawLine(
      Offset(0, size.height / 2),
      Offset(size.width, size.height / 2),
      grid,
    );

    if (notes.isEmpty) return;

    var lo = 127;
    var hi = 0;
    for (final n in notes) {
      if (n.pitch < lo) lo = n.pitch;
      if (n.pitch > hi) hi = n.pitch;
    }
    if (hi - lo < 12) {
      final mid = (hi + lo) ~/ 2;
      lo = mid - 6;
      hi = mid + 6;
    }

    final notePaint = Paint();
    for (final n in notes) {
      final x0 = size.width * (n.startSeconds / lengthSeconds);
      final x1 = size.width * ((n.startSeconds + n.lengthSeconds) / lengthSeconds);
      final y = size.height * (1 - (n.pitch - lo) / (hi - lo + 1));
      final h = math.max(2.5, size.height / (hi - lo + 1));
      final rect = RRect.fromRectAndRadius(
        Rect.fromLTWH(x0, y - h / 2, math.max(1.5, x1 - x0), h),
        const Radius.circular(1.5),
      );
      notePaint.color = _kAccent.withValues(alpha: 0.35 + 0.6 * n.velocity);
      canvas.drawRRect(rect, notePaint);
    }
    canvas.drawRRect(
      r,
      Paint()
        ..color = const Color(0xFF3E444E)
        ..style = PaintingStyle.stroke,
    );
  }

  @override
  bool shouldRepaint(covariant _AiPreviewPainter old) =>
      old.notes != notes ||
      old.lengthSeconds != lengthSeconds ||
      old.bars != bars;
}
