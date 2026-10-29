// Mix assistant dialog.
//
// Analysis runs on an engine worker thread over an offline render. This dialog
// only starts it, polls the report and applies the proposed fixes; every apply
// is a single undo entry in the project.

import 'dart:async';

import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import '../l10n/l10n.dart';
import 'app_alert.dart';

const Color _kDialogBg = Color(0xFF252830);
const Color _kPanelBg = Color(0xFF1B1D22);
const Color _kTextDim = Color(0xFF9BA1AE);
const Color _kAccent = Color(0xFF4D9DF7);
const Color _kHigh = Color(0xFFE05C5C);
const Color _kMedium = Color(0xFFE0A84A);
const Color _kLow = Color(0xFF6FBF73);

Future<void> showMixAssistantDialog(
  BuildContext context,
  EngineService service,
) {
  return showDialog<void>(
    context: context,
    barrierDismissible: false,
    builder: (context) => _MixAssistantBody(service: service),
  );
}

class _MixAssistantBody extends StatefulWidget {
  const _MixAssistantBody({required this.service});

  final EngineService service;

  @override
  State<_MixAssistantBody> createState() => _MixAssistantBodyState();
}

class _MixAssistantBodyState extends State<_MixAssistantBody> {
  final _prompt = TextEditingController(
    text: '-14 LUFS, -1 dBTP, balanced mix',
  );
  final Set<String> _appliedIssues = {};
  Timer? _timer;
  bool _masterOnly = false;

  @override
  void initState() {
    super.initState();
    _appliedIssues.addAll(widget.service.appliedMixIssues);
    _timer = Timer.periodic(const Duration(milliseconds: 150), (_) {
      widget.service.pollMixAnalysis();
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    _prompt.dispose();
    super.dispose();
  }

  void _start() {
    if (widget.service.trackCount == 0) {
      showAppError(context, context.l10n.mixProjectEmpty);
      return;
    }
    final started = widget.service.startMixAnalysis(
      prompt: _prompt.text,
      masterOnly: _masterOnly,
    );
    if (started) setState(_appliedIssues.clear);
    final error = widget.service.statusMessage;
    if (!started && error != null && error.isNotEmpty) {
      showAppError(context, error);
    }
  }

  void _applyAll() {
    final applied = widget.service.applyMixPlan();
    final error = widget.service.statusMessage;
    if (!applied && error != null && error.isNotEmpty) {
      showAppError(context, error);
    } else if (applied) {
      Navigator.of(context).pop();
    }
  }

  void _applyIssue(MixIssue issue) {
    final applied = widget.service.applyMixIssue(issue.id);
    final error = widget.service.statusMessage;
    if (!applied && error != null && error.isNotEmpty) {
      showAppError(context, error);
    } else {
      setState(() => _appliedIssues.add(issue.id));
    }
  }

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    return Dialog(
      backgroundColor: _kDialogBg,
      insetPadding: const EdgeInsets.all(28),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 780, maxHeight: 760),
        child: AnimatedBuilder(
          animation: widget.service,
          builder: (context, _) {
            final snapshot = widget.service.mixAnalysis;
            return Padding(
              padding: const EdgeInsets.fromLTRB(22, 18, 22, 16),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  _header(l10n, snapshot),
                  const SizedBox(height: 14),
                  _promptRow(l10n, snapshot),
                  const SizedBox(height: 12),
                  Expanded(
                    child: SingleChildScrollView(
                      child: _body(l10n, snapshot),
                    ),
                  ),
                  const SizedBox(height: 12),
                  _footer(l10n, snapshot),
                ],
              ),
            );
          },
        ),
      ),
    );
  }

  Widget _header(AppLocalizations l10n, MixAnalysisSnapshot snapshot) {
    return Row(
      children: [
        const Icon(Icons.auto_awesome, color: _kAccent, size: 24),
        const SizedBox(width: 10),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                l10n.mixDialogTitle,
                style: const TextStyle(
                  color: Colors.white,
                  fontSize: 18,
                  fontWeight: FontWeight.w600,
                ),
              ),
              if (snapshot.isReady && snapshot.promptNote.isNotEmpty)
                Text(
                  snapshot.promptNote,
                  style: const TextStyle(color: _kTextDim, fontSize: 12),
                ),
            ],
          ),
        ),
        IconButton(
          tooltip: l10n.mixClose,
          onPressed: () => Navigator.of(context).pop(),
          icon: const Icon(Icons.close, color: _kTextDim),
        ),
      ],
    );
  }

  Widget _promptRow(AppLocalizations l10n, MixAnalysisSnapshot snapshot) {
    final busy = snapshot.isRunning;
    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Expanded(
          child: TextField(
            controller: _prompt,
            enabled: !busy,
            style: const TextStyle(color: Colors.white, fontSize: 13),
            decoration: InputDecoration(
              labelText: l10n.mixPromptLabel,
              hintText: l10n.mixPromptHint,
              labelStyle: const TextStyle(color: _kTextDim),
              hintStyle: const TextStyle(color: _kTextDim),
              filled: true,
              fillColor: _kPanelBg,
              isDense: true,
              border: OutlineInputBorder(
                borderRadius: BorderRadius.circular(8),
                borderSide: BorderSide.none,
              ),
            ),
            onSubmitted: (_) => _start(),
          ),
        ),
        const SizedBox(width: 10),
        Column(
          children: [
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                Checkbox(
                  value: _masterOnly,
                  activeColor: _kAccent,
                  onChanged: busy
                      ? null
                      : (value) =>
                          setState(() => _masterOnly = value ?? false),
                ),
                Text(
                  l10n.mixMasterOnly,
                  style: const TextStyle(color: _kTextDim, fontSize: 12),
                ),
              ],
            ),
            FilledButton.icon(
              style: FilledButton.styleFrom(backgroundColor: _kAccent),
              onPressed: busy ? null : _start,
              icon: const Icon(Icons.equalizer_rounded, size: 18),
              label: Text(busy ? l10n.mixAnalyzing : l10n.mixAnalyze),
            ),
          ],
        ),
      ],
    );
  }

  Widget _body(AppLocalizations l10n, MixAnalysisSnapshot snapshot) {
    if (snapshot.isRunning) {
      return const Padding(
        padding: EdgeInsets.symmetric(vertical: 48),
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              CircularProgressIndicator(color: _kAccent),
              SizedBox(height: 16),
              Text(
                'Analyzing levels, spectrum, stereo image and masking…',
                style: TextStyle(color: _kTextDim, fontSize: 12),
              ),
            ],
          ),
        ),
      );
    }
    if (snapshot.isFailed) {
      return Padding(
        padding: const EdgeInsets.symmetric(vertical: 36),
        child: Center(
          child: Text(
            snapshot.error.isEmpty ? l10n.mixFailed : snapshot.error,
            style: const TextStyle(color: _kHigh, fontSize: 13),
          ),
        ),
      );
    }
    if (!snapshot.isReady) {
      return Padding(
        padding: const EdgeInsets.symmetric(vertical: 40),
        child: Center(
          child: Text(
            l10n.mixPromptHint,
            style: const TextStyle(color: _kTextDim, fontSize: 12),
          ),
        ),
      );
    }

    final issues = snapshot.issues;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        _summary(l10n, snapshot),
        const SizedBox(height: 14),
        if (issues.isEmpty)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 20),
            child: Text(
              l10n.mixNoIssues,
              textAlign: TextAlign.center,
              style: const TextStyle(color: _kLow, fontSize: 13),
            ),
          )
        else ...[
          _sectionTitle(l10n.mixIssues),
          const SizedBox(height: 6),
          for (final issue in issues)
            _issueTile(l10n, snapshot, issue),
        ],
        if (snapshot.channels.isNotEmpty) ...[
          const SizedBox(height: 14),
          _sectionTitle(l10n.mixTargets),
          const SizedBox(height: 6),
          for (final channel in snapshot.channels) _channelTile(channel),
        ],
      ],
    );
  }

  Widget _summary(AppLocalizations l10n, MixAnalysisSnapshot snapshot) {
    final targets = snapshot.targets;
    final master = snapshot.masterStats;
    final lufs = (targets['lufs'] as num?)?.toStringAsFixed(1) ?? '—';
    final ceiling =
        (targets['true_peak_db'] as num?)?.toStringAsFixed(1) ?? '—';
    final profile = '${targets['profile'] ?? '—'}';
    final actualLufs =
        (master['lufs_integrated'] as num?)?.toStringAsFixed(1) ?? '—';
    final actualPeak =
        (master['true_peak_db'] as num?)?.toStringAsFixed(1) ?? '—';
    final crest =
        (master['crest_factor_db'] as num?)?.toStringAsFixed(1) ?? '—';
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: _kPanelBg,
        borderRadius: BorderRadius.circular(8),
      ),
      child: Wrap(
        spacing: 22,
        runSpacing: 8,
        children: [
          _metric(l10n.mixLoudness, '$actualLufs LUFS', 'target $lufs'),
          _metric(l10n.mixTruePeak, '$actualPeak dBTP', 'ceiling $ceiling'),
          _metric(l10n.mixProfile, profile, 'crest $crest dB'),
        ],
      ),
    );
  }

  Widget _metric(String label, String value, String detail) {
    return SizedBox(
      width: 190,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(label, style: const TextStyle(color: _kTextDim, fontSize: 11)),
          Text(
            value,
            style: const TextStyle(
              color: Colors.white,
              fontSize: 15,
              fontWeight: FontWeight.w600,
            ),
          ),
          Text(detail, style: const TextStyle(color: _kTextDim, fontSize: 10)),
        ],
      ),
    );
  }

  Widget _sectionTitle(String text) => Text(
        text.toUpperCase(),
        style: const TextStyle(
          color: _kTextDim,
          fontSize: 10,
          fontWeight: FontWeight.w700,
          letterSpacing: 1.1,
        ),
      );

  Widget _issueTile(
    AppLocalizations l10n,
    MixAnalysisSnapshot snapshot,
    MixIssue issue,
  ) {
    final color = issue.severity == 'high'
        ? _kHigh
        : issue.severity == 'medium'
            ? _kMedium
            : _kLow;
    final severityLabel = issue.severity == 'high'
        ? l10n.mixSeverityHigh
        : issue.severity == 'medium'
            ? l10n.mixSeverityMedium
            : l10n.mixSeverityLow;
    final alreadyApplied = _appliedIssues.contains(issue.id);
    final canApply = issue.canApply && !alreadyApplied;
    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      padding: const EdgeInsets.fromLTRB(12, 10, 10, 10),
      decoration: BoxDecoration(
        color: _kPanelBg,
        borderRadius: BorderRadius.circular(8),
        border: Border(left: BorderSide(color: color, width: 3)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text(
                severityLabel.toUpperCase(),
                style: TextStyle(
                  color: color,
                  fontSize: 10,
                  fontWeight: FontWeight.w700,
                ),
              ),
              const SizedBox(width: 8),
              Text(
                issue.kind.replaceAll('_', ' '),
                style: const TextStyle(color: _kTextDim, fontSize: 10),
              ),
              const Spacer(),
              if (issue.canApply)
                TextButton(
                  onPressed: canApply ? () => _applyIssue(issue) : null,
                  child: Text(alreadyApplied ? '✓' : l10n.mixApplyIssue),
                ),
            ],
          ),
          Text(
            issue.message,
            style: const TextStyle(color: Colors.white, fontSize: 12.5),
          ),
          if (issue.moves.isNotEmpty)
            Padding(
              padding: const EdgeInsets.only(top: 5),
              child: Text(
                issue.moves.map(_moveLabel).join(' · '),
                style: const TextStyle(color: _kTextDim, fontSize: 11),
              ),
            )
          else
            Padding(
              padding: const EdgeInsets.only(top: 5),
              child: Text(
                l10n.mixNoMoves,
                style: const TextStyle(color: _kTextDim, fontSize: 11),
              ),
            ),
        ],
      ),
    );
  }

  Widget _channelTile(Map<String, dynamic> channel) {
    final detail = channel['detail'];
    final stats = detail is Map<String, dynamic> ? detail['stats'] : null;
    final lufs = stats is Map<String, dynamic>
        ? (stats['lufs_integrated'] as num?)?.toStringAsFixed(1) ?? '—'
        : '—';
    final peak = stats is Map<String, dynamic>
        ? (stats['peak_db'] as num?)?.toStringAsFixed(1) ?? '—'
        : '—';
    final name = '${channel['name'] ?? ''}';
    final role = '${channel['role'] ?? ''}';
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: [
          Expanded(
            child: Text(
              name,
              style: const TextStyle(color: Colors.white, fontSize: 12),
            ),
          ),
          SizedBox(
            width: 110,
            child: Text(
              role,
              style: const TextStyle(color: _kTextDim, fontSize: 11),
            ),
          ),
          Text(
            '$lufs LUFS  ·  $peak dBFS',
            style: const TextStyle(color: _kTextDim, fontSize: 11),
          ),
        ],
      ),
    );
  }

  Widget _footer(AppLocalizations l10n, MixAnalysisSnapshot snapshot) {
    // Applying single issues leaves the report open but its plan still contains
    // those moves; disable "apply all" so nothing is applied twice.
    final canApplyAll = snapshot.isReady &&
        !snapshot.isRunning &&
        _appliedIssues.isEmpty &&
        snapshot.plan.isNotEmpty;
    return Row(
      mainAxisAlignment: MainAxisAlignment.end,
      children: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: Text(l10n.mixClose),
        ),
        const SizedBox(width: 8),
        if (snapshot.isReady)
          OutlinedButton.icon(
            onPressed: _start,
            icon: const Icon(Icons.refresh, size: 18),
            label: Text(l10n.mixRetry),
          ),
        const SizedBox(width: 8),
        FilledButton.icon(
          style: FilledButton.styleFrom(backgroundColor: _kAccent),
          onPressed: canApplyAll ? _applyAll : null,
          icon: const Icon(Icons.auto_fix_high, size: 18),
          label: Text(l10n.mixApplyAll),
        ),
      ],
    );
  }
}

String _moveLabel(Map<String, dynamic> move) {
  final kind = '${move['move'] ?? ''}';
  final target = move['track_id'] == null ? 'Master' : 'Track ${move['track_id']}';
  switch (kind) {
    case 'gain':
      if (move['delta_db'] is num) {
        return '$target fader ${_signed((move['delta_db'] as num).toDouble())} dB';
      }
      if (move['db'] is num) {
        return '$target fader to ${(move['db'] as num).toStringAsFixed(1)} dB';
      }
      return '$target fader';
    case 'pan':
      final pan = (move['pan'] as num?)?.toDouble() ?? 0;
      return '$target pan ${(pan * 100).round()}';
    case 'eq':
      final freq = (move['freq_hz'] as num?)?.toDouble() ?? 0;
      final gain = (move['gain_db'] as num?)?.toDouble() ?? 0;
      final eqKind = '${move['kind'] ?? 'bell'}'.replaceAll('_', ' ');
      return '$target $eqKind ${_hz(freq)} ${_signed(gain)} dB';
    case 'compressor':
      return '$target compressor';
    case 'limiter':
      final ceil = (move['ceiling_db'] as num?)?.toDouble();
      final drive = (move['input_gain_db'] as num?)?.toDouble();
      return '$target limiter'
          '${ceil != null ? ' ceiling ${_signed(ceil)} dB' : ''}'
          '${drive != null ? ' drive ${_signed(drive)} dB' : ''}';
  }
  return kind;
}

String _signed(double value) =>
    '${value >= 0 ? '+' : ''}${value.toStringAsFixed(1)}';

String _hz(double hz) =>
    hz >= 1000 ? '${(hz / 1000).toStringAsFixed(1)}k' : '${hz.round()}';
