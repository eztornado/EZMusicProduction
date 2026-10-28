// Transport bar: play/pause, stop, position readout, tempo, IQ and undo/redo.
// Rebuilds on engine edits and on the shared frame tick while playing.

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import '../l10n/l10n.dart';
import 'design.dart';

class TransportBar extends StatelessWidget {
  const TransportBar({
    super.key,
    required this.service,
    required this.frameTick,
    this.onError,
  });

  final EngineService service;
  final ValueListenable<int> frameTick;

  /// Se llama SOLO cuando una acción de transporte falla (nunca con null):
  /// el que lo monta decide cómo avisar (en la app, un cuadro de alerta).
  final void Function(String error)? onError;

  void _showTempoDialog(BuildContext context) {
    final l10n = context.l10n;
    final bpmController = TextEditingController(
      text: service.tempoBpm.toStringAsFixed(0),
    );
    int numerator = service.timeSigNumerator;
    int denominator = service.timeSigDenominator;
    showDialog<void>(
      context: context,
      builder:
          (context) => StatefulBuilder(
            builder: (context, setDialogState) {
              return AlertDialog(
                title: Text(
                  l10n.transportTempoTimeSig,
                  style: const TextStyle(fontSize: 15),
                ),
                content: SizedBox(
                  width: 300,
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      TextField(
                        controller: bpmController,
                        autofocus: true,
                        keyboardType: TextInputType.number,
                        decoration: InputDecoration(
                          labelText: l10n.transportTempoHint,
                        ),
                      ),
                      const SizedBox(height: 12),
                      Row(
                        children: [
                          Text(
                            l10n.transportTimeSig,
                            style: const TextStyle(
                              fontSize: 12,
                              color: AppColors.textDim,
                            ),
                          ),
                          const Spacer(),
                          DropdownButton<int>(
                            value: numerator,
                            dropdownColor: AppColors.surfaceHigh,
                            items: [
                              for (final n in [1, 2, 3, 4, 5, 6, 7, 9, 11, 12])
                                DropdownMenuItem(value: n, child: Text('$n')),
                            ],
                            onChanged:
                                (v) => setDialogState(() => numerator = v ?? 4),
                          ),
                          const Text('/', style: TextStyle(fontSize: 14)),
                          DropdownButton<int>(
                            value: denominator,
                            dropdownColor: AppColors.surfaceHigh,
                            items: [
                              for (final n in const [1, 2, 4, 8, 16, 32])
                                DropdownMenuItem(value: n, child: Text('$n')),
                            ],
                            onChanged:
                                (v) =>
                                    setDialogState(() => denominator = v ?? 4),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
                actions: [
                  TextButton(
                    onPressed: () => Navigator.pop(context),
                    child: Text(l10n.transportCancel),
                  ),
                  FilledButton(
                    onPressed: () {
                      final bpm = double.tryParse(bpmController.text);
                      if (bpm != null) service.setTempo(bpm);
                      service.setTimeSignature(numerator, denominator);
                      Navigator.pop(context);
                    },
                    child: Text(l10n.transportApply),
                  ),
                ],
              );
            },
          ),
    );
  }

  double _iqValue(EngineService service) {
    final iq = service.inputQuantizeSeconds;
    for (final beats in const [1.0, 0.5, 0.25]) {
      final grid = beats * service.snapshot.beatSeconds;
      if ((iq - grid).abs() < 0.0005) return grid;
    }
    return 0.0;
  }

  String _format(double seconds) {
    final m = (seconds / 60).floor();
    final s = (seconds - m * 60).toStringAsFixed(1).padLeft(4, '0');
    return '$m:$s';
  }

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    return AnimatedBuilder(
      animation: Listenable.merge([frameTick, service]),
      builder: (context, _) {
        return Container(
          height: 54,
          padding: const EdgeInsets.symmetric(horizontal: 10),
          decoration: const BoxDecoration(
            color: AppColors.bgPanel,
            border: Border(top: BorderSide(color: AppColors.border)),
          ),
          child: Row(
            children: [
              _TransportCluster(
                children: [
                  _TransportButton(
                    tooltip:
                        service.isRecording
                            ? l10n.transportStopRecording
                            : l10n.transportRecord,
                    icon: Icons.fiber_manual_record,
                    active: service.isRecording,
                    activeColor: AppColors.record,
                    onTap: () {
                      if (service.isRecording) {
                        service.stopRecord();
                      } else {
                        final error = service.startRecord();
                        if (error != null) onError?.call(error);
                      }
                    },
                  ),
                  _TransportButton(
                    tooltip: l10n.transportMetronome,
                    icon: Icons.music_note_rounded,
                    active: service.metronomeOn,
                    onTap: () => service.toggleMetronome(),
                  ),
                  const _TransportDivider(),
                  _TransportButton(
                    tooltip:
                        service.isRecording
                            ? l10n.transportFinishRecording
                            : l10n.transportPlayPause,
                    icon:
                        service.isPlaying
                            ? Icons.pause_rounded
                            : Icons.play_arrow_rounded,
                    emphasized: true,
                    activeColor: AppColors.accent,
                    onTap: () {
                      // Play/pausa también finaliza la toma en curso.
                      if (service.isRecording) {
                        service.stopRecord();
                      } else if (service.isPlaying) {
                        service.pause();
                      } else {
                        final error = service.play();
                        if (error != null) onError?.call(error);
                      }
                    },
                  ),
                  _TransportButton(
                    tooltip: l10n.transportStop,
                    icon: Icons.stop_rounded,
                    onTap: service.stop,
                  ),
                ],
              ),
              const SizedBox(width: 8),
              _TimeDisplay(
                current: _format(service.positionSeconds),
                total: _format(service.durationSeconds),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: SingleChildScrollView(
                  scrollDirection: Axis.horizontal,
                  child: Row(
                    children: [
                      // Input quantize (§7.5.2 v1.5): recorded MIDI snaps to
                      // the chosen grid at take finalization.
                      _Chip(
                        label: l10n.transportIQ,
                        child: DropdownButtonHideUnderline(
                          child: DropdownButton<double>(
                            value: _iqValue(service),
                            dropdownColor: AppColors.surfaceHigh,
                            borderRadius: BorderRadius.circular(8),
                            style: const TextStyle(
                              fontSize: 10.5,
                              fontWeight: FontWeight.w600,
                              color: AppColors.text,
                            ),
                            items: [
                              DropdownMenuItem(
                                value: 0.0,
                                child: Text(l10n.transportOff),
                              ),
                              for (final (label, beats) in const [
                                ('1/4', 1.0),
                                ('1/8', 0.5),
                                ('1/16', 0.25),
                              ])
                                DropdownMenuItem(
                                  value: beats * service.snapshot.beatSeconds,
                                  child: Text(label),
                                ),
                            ],
                            onChanged: (grid) {
                              if (grid == null) return;
                              final error = service.setInputQuantize(grid);
                              if (error != null) onError?.call(error);
                            },
                          ),
                        ),
                      ),
                      const SizedBox(width: 6),
                      MouseRegion(
                        cursor: SystemMouseCursors.click,
                        child: GestureDetector(
                          onTap: () => _showTempoDialog(context),
                          child: Container(
                            height: 34,
                            padding: const EdgeInsets.symmetric(horizontal: 11),
                            alignment: Alignment.center,
                            decoration: panelDecoration(
                              color: AppColors.surface,
                              radius: 7,
                            ),
                            child: Row(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                Text(
                                  service.tempoBpm.toStringAsFixed(0),
                                  style: const TextStyle(
                                    fontSize: 12,
                                    fontWeight: FontWeight.w700,
                                    color: AppColors.text,
                                    fontFeatures: [
                                      FontFeature.tabularFigures(),
                                    ],
                                  ),
                                ),
                                const SizedBox(width: 3),
                                const Text(
                                  'BPM',
                                  style: TextStyle(
                                    fontSize: 7.5,
                                    fontWeight: FontWeight.w700,
                                    letterSpacing: 0.6,
                                    color: AppColors.textFaint,
                                  ),
                                ),
                                const SizedBox(width: 8),
                                Container(
                                  width: 1,
                                  height: 14,
                                  color: AppColors.border,
                                ),
                                const SizedBox(width: 8),
                                Text(
                                  '${service.timeSigNumerator}/'
                                  '${service.timeSigDenominator}',
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
                      const SizedBox(width: 6),
                      Container(
                        height: 34,
                        padding: const EdgeInsets.symmetric(horizontal: 9),
                        alignment: Alignment.center,
                        child: Text(
                          '${service.sampleRate.toStringAsFixed(0)} Hz',
                          style: const TextStyle(
                            fontSize: 9.5,
                            fontWeight: FontWeight.w500,
                            color: AppColors.textFaint,
                            fontFeatures: [FontFeature.tabularFigures()],
                          ),
                        ),
                      ),
                      _TransportButton(
                        tooltip: l10n.transportRestartAudio,
                        icon: Icons.refresh_rounded,
                        onTap: () {
                          final error = service.restartAudio();
                          if (error != null) onError?.call(error);
                        },
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(width: 8),
              _TransportCluster(
                children: [
                  _TransportButton(
                    tooltip: l10n.transportUndo,
                    icon: Icons.undo_rounded,
                    enabled: service.canUndo,
                    onTap: service.undo,
                  ),
                  _TransportButton(
                    tooltip: l10n.transportRedo,
                    icon: Icons.redo_rounded,
                    enabled: service.canRedo,
                    onTap: service.redo,
                  ),
                ],
              ),
            ],
          ),
        );
      },
    );
  }
}

class _TransportCluster extends StatelessWidget {
  const _TransportCluster({required this.children});

  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 36,
      padding: const EdgeInsets.all(3),
      decoration: panelDecoration(color: AppColors.surface, radius: 8),
      child: Row(mainAxisSize: MainAxisSize.min, children: children),
    );
  }
}

class _TransportDivider extends StatelessWidget {
  const _TransportDivider();

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 1,
      height: 16,
      margin: const EdgeInsets.symmetric(horizontal: 3),
      color: AppColors.border,
    );
  }
}

class _TransportButton extends StatefulWidget {
  const _TransportButton({
    required this.tooltip,
    required this.icon,
    required this.onTap,
    this.active = false,
    this.emphasized = false,
    this.enabled = true,
    this.activeColor = AppColors.accent,
  });

  final String tooltip;
  final IconData icon;
  final VoidCallback? onTap;
  final bool active;
  final bool emphasized;
  final bool enabled;
  final Color activeColor;

  @override
  State<_TransportButton> createState() => _TransportButtonState();
}

class _TransportButtonState extends State<_TransportButton> {
  bool _hovered = false;

  @override
  Widget build(BuildContext context) {
    final enabled = widget.enabled && widget.onTap != null;
    final Color background;
    final Color foreground;
    if (!enabled) {
      background = Colors.transparent;
      foreground = AppColors.textFaint.withValues(alpha: 0.45);
    } else if (widget.emphasized) {
      background = widget.activeColor;
      foreground = AppColors.bgDeep;
    } else if (widget.active) {
      background = widget.activeColor.withValues(alpha: 0.18);
      foreground = widget.activeColor;
    } else {
      background = _hovered ? AppColors.surfaceHigh : Colors.transparent;
      foreground = _hovered ? AppColors.text : AppColors.textDim;
    }
    return Tooltip(
      message: widget.tooltip,
      waitDuration: const Duration(milliseconds: 500),
      child: MouseRegion(
        cursor: enabled ? SystemMouseCursors.click : MouseCursor.defer,
        onEnter: (_) => setState(() => _hovered = true),
        onExit: (_) => setState(() => _hovered = false),
        child: GestureDetector(
          onTap: enabled ? widget.onTap : null,
          child: AnimatedContainer(
            duration: const Duration(milliseconds: 90),
            width: 30,
            height: 30,
            decoration: BoxDecoration(
              color: background,
              borderRadius: BorderRadius.circular(6),
              border:
                  widget.active && !widget.emphasized
                      ? Border.all(
                        color: widget.activeColor.withValues(alpha: 0.6),
                      )
                      : null,
            ),
            child: Icon(widget.icon, size: 17, color: foreground),
          ),
        ),
      ),
    );
  }
}

class _TimeDisplay extends StatelessWidget {
  const _TimeDisplay({required this.current, required this.total});

  final String current;
  final String total;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 36,
      padding: const EdgeInsets.symmetric(horizontal: 11),
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: const Color(0xFF0A0C0F),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: AppColors.border),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            current,
            style: const TextStyle(
              fontFamily: 'monospace',
              fontSize: 13.5,
              fontWeight: FontWeight.w600,
              color: AppColors.text,
              fontFeatures: [FontFeature.tabularFigures()],
            ),
          ),
          const Padding(
            padding: EdgeInsets.symmetric(horizontal: 4),
            child: Text(
              '/',
              style: TextStyle(fontSize: 10, color: AppColors.textFaint),
            ),
          ),
          Text(
            total,
            style: const TextStyle(
              fontFamily: 'monospace',
              fontSize: 10.5,
              fontWeight: FontWeight.w500,
              color: AppColors.textFaint,
              fontFeatures: [FontFeature.tabularFigures()],
            ),
          ),
        ],
      ),
    );
  }
}

class _Chip extends StatelessWidget {
  const _Chip({required this.label, required this.child});

  final String label;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 34,
      padding: const EdgeInsets.only(left: 9, right: 4),
      decoration: panelDecoration(color: AppColors.surface, radius: 7),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text(
            label,
            style: const TextStyle(
              fontSize: 7.5,
              fontWeight: FontWeight.w700,
              letterSpacing: 0.7,
              color: AppColors.textFaint,
            ),
          ),
          const SizedBox(width: 5),
          child,
        ],
      ),
    );
  }
}
