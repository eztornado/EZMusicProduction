// Multi-track AI song arranger.
//
// One brief goes to an OpenAI-compatible model with a read-only project
// context. The reply is a JSON plan; the engine validates the whole document
// and applies it as a single undo entry (creating tracks and clips as needed).

import 'dart:convert';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../ai/ai_settings.dart';
import '../ai/ai_settings_dialog.dart';
import '../ai/llm_client.dart';
import '../ai/song_plan.dart';
import '../debug/debug_log.dart';
import '../engine/engine_service.dart';
import '../l10n/l10n.dart';

const Color _kDialogBg = Color(0xFF252830);
const Color _kPanelBg = Color(0xFF1B1D22);
const Color _kTextDim = Color(0xFF9BA1AE);
const Color _kAccent = Color(0xFF4D9DF7);
const Color _kGreen = Color(0xFF4CC38A);

/// Keeps the analyst's document compact and rejects truncated/non-JSON
/// answers so the arranger can fall back to the original brief cleanly.
String? _musicDirectionOrNull(String raw) {
  try {
    final decoded = jsonDecode(extractJsonObject(raw));
    if (decoded is Map<String, dynamic> && decoded.isNotEmpty) {
      return jsonEncode(decoded);
    }
  } catch (_) {
    // Invalid or truncated analysis: ignore it.
  }
  return null;
}

double? _contextNumber(String contextJson, String key) {
  try {
    final decoded = jsonDecode(contextJson);
    if (decoded is Map<String, dynamic>) {
      final value = decoded[key];
      if (value is num) return value.toDouble();
    }
  } catch (_) {
    // Fall back to the values computed from the dialog.
  }
  return null;
}

/// Opens the song arranger. Returns true when a plan was applied.
Future<bool?> showSongArrangerDialog(
  BuildContext context,
  EngineService service, {
  int? trackId,
  double? anchorSeconds,
}) {
  return showDialog<bool>(
    context: context,
    barrierDismissible: false,
    builder:
        (context) => _SongArrangerBody(
          service: service,
          trackId: trackId,
          anchorSeconds: anchorSeconds,
        ),
  );
}

class _SongArrangerBody extends StatefulWidget {
  const _SongArrangerBody({
    required this.service,
    required this.trackId,
    required this.anchorSeconds,
  });

  final EngineService service;
  final int? trackId;
  final double? anchorSeconds;

  @override
  State<_SongArrangerBody> createState() => _SongArrangerBodyState();
}

class _SongArrangerBodyState extends State<_SongArrangerBody> {
  static const int _minBars = 1;
  static const int _maxBars = 64;

  late final TextEditingController _brief = TextEditingController();
  late final TextEditingController _refine = TextEditingController();
  final TextEditingController _customBars = TextEditingController();
  late double _anchorSeconds;
  int _bars = 16;
  bool _customBarsMode = false;
  bool _busy = false;
  bool _refining = false;
  bool _analyzing = false;
  String? _musicDirection;
  bool _cancelRequested = false;
  String? _error;
  SongPlan? _plan;
  LlmClient? _client;

  double get _barSeconds {
    final bpm = widget.service.tempoBpm;
    if (bpm <= 0) return 2.0;
    final num = widget.service.timeSigNumerator;
    final den = widget.service.timeSigDenominator;
    return (60.0 / bpm) * num * 4.0 / (den > 0 ? den : 1);
  }

  int get _startBar =>
      (_anchorSeconds / (_barSeconds <= 0 ? 2.0 : _barSeconds)).floor() + 1;

  bool get _needsKey => !AiSettings.isLocalEndpoint && !AiSettings.hasKey;

  int? get _customBarsValue {
    if (!_customBarsMode) return null;
    final value = int.tryParse(_customBars.text.trim());
    if (value == null || value < _minBars || value > _maxBars) return null;
    return value;
  }

  bool get _barsValid => !_customBarsMode || _customBarsValue != null;

  @override
  void initState() {
    super.initState();
    AiSettings.load();
    _anchorSeconds = widget.anchorSeconds ?? widget.service.positionSeconds;
  }

  @override
  void dispose() {
    _client?.cancel();
    _brief.dispose();
    _refine.dispose();
    _customBars.dispose();
    super.dispose();
  }

  Future<void> _openSettings() async {
    final saved = await showAiSettingsDialog(context);
    if (!mounted) return;
    AiSettings.load();
    if (saved == true && _error == context.l10n.arrangerNoKey) {
      _error = null;
    }
    setState(() {});
  }

  String _startDescription(AppLocalizations l) =>
      _anchorSeconds <= 0.001
          ? l.arrangerStartBeginning
          : l.arrangerStartAtBar(_startBar);

  Future<void> _generate() async {
    final l = context.l10n;
    if (!_barsValid) {
      setState(() => _error = l.arrangerBarsInvalid);
      return;
    }
    if (_needsKey) {
      setState(() => _error = l.arrangerNoKey);
      return;
    }

    final client = LlmClient();
    _client = client;
    _cancelRequested = false;
    setState(() {
      _busy = true;
      _refining = false;
      _analyzing = true;
      _musicDirection = null;
      _error = null;
    });

    try {
      final projectContext = widget.service.aiContextJson(
        trackId: widget.trackId,
        anchorSeconds: _anchorSeconds,
        bars: _bars,
      );

      String? musicDirection;
      try {
        final rawDirection = await client.analyzeBrief(
          projectContextJson: projectContext,
          brief: _brief.text,
          bars: _bars,
          startAt: _startDescription(l),
        );
        if (!mounted || client.isCancelled) return;
        musicDirection = _musicDirectionOrNull(rawDirection);
        if (musicDirection == null) {
          DebugLog.write(
            'ia',
            'Análisis del brief descartado: la respuesta no es un objeto JSON válido.',
          );
        }
      } on LlmException catch (e) {
        if (client.isCancelled) return;
        DebugLog.write('ia', 'Análisis del brief omitido: ${e.message}');
      } catch (e) {
        if (client.isCancelled) return;
        DebugLog.write('ia', 'Análisis del brief omitido: $e');
      } finally {
        if (mounted) setState(() => _analyzing = false);
      }

      final content = await client.createPlan(
        projectContextJson: projectContext,
        brief: _brief.text,
        bars: _bars,
        startAt: _startDescription(l),
        musicDirectionJson: musicDirection,
      );
      var plan = SongPlan.parse(content);
      if (!mounted || client.isCancelled) return;

      // Validate arithmetic, MIDI range and register before showing the
      // preview. A single focused repair is much cheaper than applying a plan
      // the engine will reject, and it fixes the common bar-vs-beat mistake.
      final windowStart =
          _contextNumber(projectContext, 'window_start_beat') ?? 0.0;
      final expectedBeats =
          _contextNumber(projectContext, 'window_beats') ??
          _bars * widget.service.beatsPerBar;
      final windowEnd =
          _contextNumber(projectContext, 'window_end_beat') ??
          windowStart + expectedBeats;
      final actualBeats = plan.noteEndBeat;
      final issues = plan.issuesForWindow(
        startBeat: windowStart,
        endBeat: windowEnd,
      );
      if (actualBeats + 0.01 < windowEnd) {
        issues.add(
          'The furthest note ends at beat ${actualBeats.toStringAsFixed(2)}, '
          'but the requested window ends at beat '
          '${windowEnd.toStringAsFixed(2)}. This is an arithmetic error, '
          'usually caused by using the bar count as a beat count. Extend the '
          'material so it reaches the end of the window.',
        );
      }

      if (issues.isNotEmpty) {
        DebugLog.write(
          'ia',
          'Plan con ${issues.length} problema(s); solicitando reparación:\n'
          '${issues.map((issue) => '- $issue').join('\n')}',
        );
        setState(() => _refining = true);
        try {
          final repairedContent = await client.refinePlan(
            projectContextJson: projectContext,
            brief: _brief.text,
            bars: _bars,
            startAt: _startDescription(l),
            previousPlanJson: plan.rawJson,
            feedback:
                'Fix every issue below and return the complete revised plan. '
                'The requested window is beat '
                '${windowStart.toStringAsFixed(2)} to '
                '${windowEnd.toStringAsFixed(2)} (inclusive start, exclusive '
                'end). Keep the same musical direction, instruments and form; '
                'extend and develop the material instead of copying it.\n'
                '${issues.map((issue) => '- $issue').join('\n')}\n'
                'Do not return a plan with a pitch outside MIDI 0-127, a '
                'chromatic/scale run instead of chord tones, a melody outside '
                'C4-C6, or a window that ends before beat '
                '${windowEnd.toStringAsFixed(2)}.',
            musicDirectionJson: musicDirection,
          );
          final repaired = SongPlan.parse(repairedContent);
          if (!mounted || client.isCancelled) return;
          final repairedIssues = repaired.issuesForWindow(
            startBeat: windowStart,
            endBeat: windowEnd,
          );
          final repairedShort = repaired.noteEndBeat + 0.01 < windowEnd;
          final oldScore = issues.length + (actualBeats + 0.01 < windowEnd ? 1 : 0);
          final newScore =
              repairedIssues.length + (repairedShort ? 1 : 0);
          if (newScore < oldScore ||
              (newScore == oldScore &&
                  repaired.noteEndBeat > plan.noteEndBeat)) {
            plan = repaired;
          }
        } catch (e) {
          if (client.isCancelled) return;
          DebugLog.write('ia', 'No se pudo reparar el plan: $e');
        } finally {
          if (mounted) setState(() => _refining = false);
        }
      }

      final remaining = plan.issuesForWindow(
        startBeat: windowStart,
        endBeat: windowEnd,
      );
      final needsPitchGuard = remaining.any(
        (issue) =>
            issue.contains('outside MIDI') ||
            issue.contains('median pitch') ||
            issue.contains('stepwise run'),
      );
      if (needsPitchGuard) {
        DebugLog.write(
          'ia',
          'Aplicando guardia de registro/MIDI al plan:\n'
          '${remaining.map((issue) => '- $issue').join('\n')}',
        );
        plan = plan.normalizedForPiano(
          startBeat: windowStart,
          beatsPerBar: widget.service.beatsPerBar,
        );
      }

      setState(() {
        _plan = plan;
        _musicDirection = musicDirection;
      });
    } on LlmException catch (e) {
      if (!mounted || client.isCancelled) return;
      setState(() => _error = l.arrangerGenerationFailed(e.message));
    } on FormatException catch (e) {
      if (!mounted || client.isCancelled) return;
      setState(() => _error = l.arrangerGenerationFailed(e.message));
    } catch (e) {
      if (!mounted || client.isCancelled) return;
      setState(() => _error = l.arrangerGenerationFailed('$e'));
    } finally {
      if (identical(_client, client)) _client = null;
      if (mounted) {
        setState(() {
          _busy = false;
          _analyzing = false;
          _cancelRequested = false;
        });
      }
    }
  }

  Future<void> _refinePlan() async {
    final l = context.l10n;
    final plan = _plan;
    if (plan == null || _busy) return;
    if (!_barsValid) {
      setState(() => _error = l.arrangerBarsInvalid);
      return;
    }
    if (_needsKey) {
      setState(() => _error = l.arrangerNoKey);
      return;
    }

    final client = LlmClient();
    _client = client;
    _cancelRequested = false;
    setState(() {
      _busy = true;
      _refining = true;
      _error = null;
    });

    try {
      final projectContext = widget.service.aiContextJson(
        trackId: widget.trackId,
        anchorSeconds: _anchorSeconds,
        bars: _bars,
      );
      final content = await client.refinePlan(
        projectContextJson: projectContext,
        brief: _brief.text,
        bars: _bars,
        startAt: _startDescription(l),
        previousPlanJson: plan.rawJson,
        feedback: _refine.text,
        musicDirectionJson: _musicDirection,
      );
      final refined = SongPlan.parse(content);
      if (!mounted || client.isCancelled) return;
      setState(() => _plan = refined);
    } on LlmException catch (e) {
      if (!mounted || client.isCancelled) return;
      setState(() => _error = l.arrangerGenerationFailed(e.message));
    } on FormatException catch (e) {
      if (!mounted || client.isCancelled) return;
      setState(() => _error = l.arrangerGenerationFailed(e.message));
    } catch (e) {
      if (!mounted || client.isCancelled) return;
      setState(() => _error = l.arrangerGenerationFailed('$e'));
    } finally {
      if (identical(_client, client)) _client = null;
      if (mounted) {
        setState(() {
          _busy = false;
          _refining = false;
          _cancelRequested = false;
        });
      }
    }
  }

  void _cancelRequest() {
    _cancelRequested = true;
    _client?.cancel();
    setState(() {});
  }

  void _applyPlan() {
    final l = context.l10n;
    final plan = _plan;
    if (plan == null || _busy) return;
    final summary = widget.service.applyAiArrangement(plan.rawJson);
    if (summary == null) {
      setState(() => _error = l.arrangerApplyFailed(widget.service.lastError));
      return;
    }
    Navigator.of(context).pop(true);
  }

  @override
  Widget build(BuildContext context) {
    final l = context.l10n;
    final plan = _plan;

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
              const Icon(Icons.queue_music_rounded, size: 19, color: _kAccent),
              const SizedBox(width: 8),
              Text(l.arrangerTitle, style: const TextStyle(fontSize: 15)),
            ],
          ),
          Padding(
            padding: const EdgeInsets.only(left: 27, top: 2),
            child: Text(
              l.arrangerSubtitle,
              style: const TextStyle(fontSize: 11, color: _kTextDim),
            ),
          ),
        ],
      ),
      content: SizedBox(
        width: 640,
        child: SingleChildScrollView(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            mainAxisSize: MainAxisSize.min,
            children: [
              TextField(
                controller: _brief,
                enabled: !_busy,
                minLines: 2,
                maxLines: 4,
                style: const TextStyle(fontSize: 12),
                decoration: InputDecoration(
                  labelText: l.arrangerBrief,
                  hintText: l.arrangerBriefHint,
                  hintStyle: const TextStyle(
                    fontSize: 11,
                    color: Color(0xFF6B7280),
                  ),
                  labelStyle: const TextStyle(fontSize: 11, color: _kTextDim),
                  isDense: true,
                  border: const OutlineInputBorder(),
                ),
              ),
              const SizedBox(height: 5),
              Text(
                l.arrangerPrivacyHint,
                style: const TextStyle(fontSize: 10, color: _kTextDim),
              ),
              const SizedBox(height: 12),
              Row(
                children: [
                  Text(
                    l.arrangerBars,
                    style: const TextStyle(fontSize: 11, color: _kTextDim),
                  ),
                  const SizedBox(width: 10),
                  SegmentedButton<int>(
                    segments: [
                      const ButtonSegment(value: 4, label: Text('4')),
                      const ButtonSegment(value: 8, label: Text('8')),
                      const ButtonSegment(value: 16, label: Text('16')),
                      const ButtonSegment(value: 32, label: Text('32')),
                      ButtonSegment(
                        value: 0,
                        label: Text(l.arrangerBarsCustom),
                      ),
                    ],
                    selected: {_customBarsMode ? 0 : _bars},
                    showSelectedIcon: false,
                    style: _segmentStyle(),
                    onSelectionChanged:
                        _busy
                            ? null
                            : (v) {
                              if (v.first == 0) {
                                setState(() {
                                  _customBarsMode = true;
                                  if (_customBarsValue == null) {
                                    _customBars.text = _bars.toString();
                                  }
                                });
                              } else {
                                setState(() {
                                  _customBarsMode = false;
                                  _bars = v.first;
                                });
                              }
                            },
                  ),
                  if (_customBarsMode) ...[
                    const SizedBox(width: 8),
                    SizedBox(
                      width: 118,
                      child: TextField(
                        key: const Key('arranger-custom-bars'),
                        controller: _customBars,
                        enabled: !_busy,
                        keyboardType: TextInputType.number,
                        inputFormatters: [
                          FilteringTextInputFormatter.digitsOnly,
                          LengthLimitingTextInputFormatter(2),
                        ],
                        style: const TextStyle(fontSize: 12),
                        decoration: InputDecoration(
                          isDense: true,
                          labelText: l.arrangerBars,
                          helperText: l.arrangerBarsRange,
                          errorText:
                              _customBarsValue == null
                                  ? l.arrangerBarsInvalid
                                  : null,
                          labelStyle: const TextStyle(
                            fontSize: 10,
                            color: _kTextDim,
                          ),
                          helperStyle: const TextStyle(
                            fontSize: 9,
                            color: _kTextDim,
                          ),
                          errorStyle: const TextStyle(fontSize: 9),
                          border: const OutlineInputBorder(),
                        ),
                        onChanged: (text) {
                          final value = int.tryParse(text.trim());
                          setState(() {
                            if (value != null &&
                                value >= _minBars &&
                                value <= _maxBars) {
                              _bars = value;
                            }
                          });
                        },
                      ),
                    ),
                  ],
                  const SizedBox(width: 12),
                  Expanded(
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.end,
                      children: [
                        const Icon(
                          Icons.place_outlined,
                          size: 14,
                          color: _kTextDim,
                        ),
                        const SizedBox(width: 5),
                        Flexible(
                          child: Text(
                            _startDescription(l),
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(
                              fontSize: 11,
                              color: _kTextDim,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
              if (_needsKey) ...[
                const SizedBox(height: 10),
                _notice(
                  icon: Icons.key_outlined,
                  text: l.arrangerNoKey,
                  action: TextButton(
                    onPressed: _busy ? null : _openSettings,
                    child: Text(l.arrangerSettings),
                  ),
                ),
              ],
              if (_busy) ...[
                const SizedBox(height: 14),
                LinearProgressIndicator(
                  minHeight: 2,
                  color: _kAccent,
                  backgroundColor: _kPanelBg,
                ),
                const SizedBox(height: 8),
                Row(
                  children: [
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            _analyzing
                                ? l.arrangerAnalyzing
                                : _refining
                                ? l.arrangerRefining
                                : l.arrangerThinking,
                            style: const TextStyle(
                              fontSize: 12,
                              color: _kAccent,
                            ),
                          ),
                          const SizedBox(height: 2),
                          Text(
                            l.arrangerThinkingHint,
                            style: const TextStyle(
                              fontSize: 10,
                              color: _kTextDim,
                            ),
                          ),
                        ],
                      ),
                    ),
                    TextButton(
                      onPressed: _cancelRequest,
                      child: Text(
                        _cancelRequested
                            ? l.arrangerCancelling
                            : l.arrangerCancelRequest,
                      ),
                    ),
                  ],
                ),
              ],
              if (_error != null) ...[
                const SizedBox(height: 12),
                Container(
                  padding: const EdgeInsets.all(9),
                  decoration: BoxDecoration(
                    color: const Color(0x33E5484D),
                    borderRadius: BorderRadius.circular(6),
                    border: Border.all(color: const Color(0x66E5484D)),
                  ),
                  child: Text(
                    _error!,
                    style: const TextStyle(
                      fontSize: 11,
                      color: Color(0xFFFF8A8D),
                    ),
                  ),
                ),
              ],
              if (plan != null && !_busy) ...[
                const SizedBox(height: 14),
                _planPreview(l, plan),
              ],
            ],
          ),
        ),
      ),
      actions: [
        if (_busy)
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: Text(l.arrangerClose),
          )
        else ...[
          TextButton.icon(
            onPressed: _openSettings,
            icon: const Icon(Icons.tune_rounded, size: 15),
            label: Text(l.arrangerSettings),
          ),
          if (plan == null) ...[
            TextButton(
              onPressed: () => Navigator.of(context).pop(false),
              child: Text(l.arrangerCancel),
            ),
            FilledButton.icon(
              onPressed: _barsValid ? _generate : null,
              style: FilledButton.styleFrom(backgroundColor: _kAccent),
              icon: const Icon(Icons.auto_awesome, size: 16),
              label: Text(l.arrangerGenerate),
            ),
          ] else ...[
            TextButton(
              onPressed: () => Navigator.of(context).pop(false),
              child: Text(l.arrangerDiscard),
            ),
            FilledButton.icon(
              onPressed: _applyPlan,
              style: FilledButton.styleFrom(backgroundColor: _kGreen),
              icon: const Icon(Icons.check_rounded, size: 16),
              label: Text(l.arrangerApply),
            ),
          ],
        ],
      ],
    );
  }

  Widget _planPreview(AppLocalizations l, SongPlan plan) {
    return Container(
      padding: const EdgeInsets.fromLTRB(12, 10, 12, 12),
      decoration: BoxDecoration(
        color: _kPanelBg,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: const Color(0xFF30343D)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            children: [
              const Icon(
                Icons.lightbulb_outline_rounded,
                size: 15,
                color: _kAccent,
              ),
              const SizedBox(width: 6),
              Expanded(
                child: Text(
                  plan.title.isEmpty ? l.arrangerProposal : plan.title,
                  style: const TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ),
            ],
          ),
          if (plan.summary.isNotEmpty) ...[
            const SizedBox(height: 4),
            Text(
              plan.summary,
              style: const TextStyle(fontSize: 11, color: _kTextDim),
            ),
          ],
          const SizedBox(height: 7),
          Text(
            l.arrangerSummary(
              plan.newTrackCount,
              plan.reusedTrackCount,
              plan.clipCount,
              plan.noteCount,
            ),
            style: const TextStyle(fontSize: 10, color: _kTextDim),
          ),
          const SizedBox(height: 8),
          ...plan.tracks.map(
            (track) => Padding(
              padding: const EdgeInsets.only(bottom: 5),
              child: _trackRow(l, track),
            ),
          ),
          const SizedBox(height: 2),
          Text(
            l.arrangerPreviewHint,
            style: const TextStyle(fontSize: 10, color: _kTextDim),
          ),
          const SizedBox(height: 10),
          TextField(
            controller: _refine,
            minLines: 1,
            maxLines: 2,
            style: const TextStyle(fontSize: 11),
            decoration: InputDecoration(
              labelText: l.arrangerRefineHint,
              hintText: l.arrangerRefineDefault,
              hintStyle: const TextStyle(
                fontSize: 10,
                color: Color(0xFF6B7280),
              ),
              labelStyle: const TextStyle(fontSize: 10, color: _kTextDim),
              isDense: true,
              border: const OutlineInputBorder(),
            ),
          ),
          const SizedBox(height: 6),
          Align(
            alignment: Alignment.centerRight,
            child: OutlinedButton.icon(
              onPressed: _busy || !_barsValid ? null : _refinePlan,
              icon: const Icon(Icons.refresh_rounded, size: 15),
              label: Text(l.arrangerRefine),
            ),
          ),
        ],
      ),
    );
  }

  Widget _trackRow(AppLocalizations l, SongPlanTrack track) {
    final role = track.role.isEmpty ? l.arrangerRoleTrack : track.role;
    final instrument =
        track.instrumentName.isNotEmpty
            ? track.instrumentName
            : track.instrument.isEmpty
            ? l.arrangerInstrumentDefault
            : track.instrument;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
      decoration: BoxDecoration(
        color: const Color(0xFF22252B),
        borderRadius: BorderRadius.circular(6),
      ),
      child: Row(
        children: [
          Icon(_roleIcon(track.role), size: 15, color: _kAccent),
          const SizedBox(width: 7),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  track.name,
                  style: const TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.w600,
                  ),
                ),
                Text(
                  '$role · $instrument',
                  style: const TextStyle(fontSize: 10, color: _kTextDim),
                ),
              ],
            ),
          ),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
            decoration: BoxDecoration(
              color:
                  track.reusesExisting
                      ? const Color(0x264D9DF7)
                      : const Color(0x264CC38A),
              borderRadius: BorderRadius.circular(4),
            ),
            child: Text(
              track.reusesExisting
                  ? l.arrangerExistingTrack
                  : l.arrangerNewTrack,
              style: TextStyle(
                fontSize: 9,
                color: track.reusesExisting ? _kAccent : _kGreen,
              ),
            ),
          ),
          const SizedBox(width: 8),
          Text(
            l.arrangerTrackStats(track.clips.length, track.noteCount),
            style: const TextStyle(fontSize: 10, color: _kTextDim),
          ),
        ],
      ),
    );
  }

  Widget _notice({
    required IconData icon,
    required String text,
    Widget? action,
  }) {
    return Container(
      padding: const EdgeInsets.fromLTRB(9, 6, 6, 6),
      decoration: BoxDecoration(
        color: const Color(0x26F5A623),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: const Color(0x55F5A623)),
      ),
      child: Row(
        children: [
          Icon(icon, size: 15, color: const Color(0xFFFFC46B)),
          const SizedBox(width: 7),
          Expanded(
            child: Text(
              text,
              style: const TextStyle(fontSize: 11, color: Color(0xFFFFD79A)),
            ),
          ),
          if (action != null) action,
        ],
      ),
    );
  }

  ButtonStyle _segmentStyle() {
    return ButtonStyle(
      visualDensity: VisualDensity.compact,
      textStyle: WidgetStateProperty.all(const TextStyle(fontSize: 11)),
      foregroundColor: WidgetStateProperty.resolveWith((states) {
        return states.contains(WidgetState.selected) ? _kDialogBg : _kTextDim;
      }),
      backgroundColor: WidgetStateProperty.resolveWith((states) {
        return states.contains(WidgetState.selected) ? _kAccent : _kPanelBg;
      }),
    );
  }

  IconData _roleIcon(String role) {
    final r = role.toLowerCase();
    if (r.contains('drum') || r.contains('bater')) return Icons.album_outlined;
    if (r.contains('bass') || r.contains('bajo')) {
      return Icons.graphic_eq_rounded;
    }
    if (r.contains('chord') || r.contains('armon') || r.contains('key')) {
      return Icons.piano_outlined;
    }
    if (r.contains('pad') || r.contains('texture')) {
      return Icons.blur_on_rounded;
    }
    if (r.contains('arp') || r.contains('pluck')) {
      return Icons.multiline_chart_rounded;
    }
    if (r.contains('fx') || r.contains('riser')) {
      return Icons.auto_fix_high_rounded;
    }
    return Icons.music_note_rounded;
  }
}
