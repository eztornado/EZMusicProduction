// Transport bar v0: play/pause, stop, position readout, undo/redo.
// Rebuilds on engine edits and on the shared frame tick while playing.

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import '../l10n/l10n.dart';

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
        text: service.tempoBpm.toStringAsFixed(0));
    int numerator = service.timeSigNumerator;
    int denominator = service.timeSigDenominator;
    showDialog<void>(
      context: context,
      builder: (context) => StatefulBuilder(builder: (context, setDialogState) {
        return AlertDialog(
          backgroundColor: const Color(0xFF252830),
          title: Text(l10n.transportTempoTimeSig,
              style: const TextStyle(fontSize: 15)),
          content: SizedBox(
            width: 300,
            child: Column(mainAxisSize: MainAxisSize.min, children: [
              TextField(
                controller: bpmController,
                autofocus: true,
                keyboardType: TextInputType.number,
                decoration: InputDecoration(
                    labelText: l10n.transportTempoHint),
              ),
              const SizedBox(height: 12),
              Row(children: [
                Text(l10n.transportTimeSig,
                    style: const TextStyle(fontSize: 12,
                        color: Color(0xFF9BA1AE))),
                const Spacer(),
                DropdownButton<int>(
                  value: numerator,
                  dropdownColor: const Color(0xFF252830),
                  items: [
                    for (final n in [1, 2, 3, 4, 5, 6, 7, 9, 11, 12])
                      DropdownMenuItem(value: n, child: Text('$n')),
                  ],
                  onChanged: (v) =>
                      setDialogState(() => numerator = v ?? 4),
                ),
                const Text('/', style: TextStyle(fontSize: 14)),
                DropdownButton<int>(
                  value: denominator,
                  dropdownColor: const Color(0xFF252830),
                  items: [
                    for (final n in const [1, 2, 4, 8, 16, 32])
                      DropdownMenuItem(value: n, child: Text('$n')),
                  ],
                  onChanged: (v) =>
                      setDialogState(() => denominator = v ?? 4),
                ),
              ]),
            ]),
          ),
          actions: [
            TextButton(
                onPressed: () => Navigator.pop(context),
                child: Text(l10n.transportCancel)),
            TextButton(
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
      }),
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
          height: 52,
          color: const Color(0xFF1E2026),
          padding: const EdgeInsets.symmetric(horizontal: 12),
          child: Row(
            children: [
              IconButton(
                tooltip: service.isRecording
                    ? l10n.transportStopRecording
                    : l10n.transportRecord,
                icon: Icon(Icons.fiber_manual_record,
                    color: service.isRecording
                        ? const Color(0xFFE5484D)
                        : Colors.white38),
                onPressed: () {
                  if (service.isRecording) {
                    service.stopRecord();
                  } else {
                    final error = service.startRecord();
                    if (error != null) onError?.call(error);
                  }
                },
              ),
              IconButton(
                tooltip: l10n.transportMetronome,
                icon: Icon(Icons.music_note,
                    color: service.metronomeOn
                        ? const Color(0xFF4D9DF7)
                        : Colors.white38),
                onPressed: () => service.toggleMetronome(),
              ),
              IconButton(
                tooltip: service.isRecording
                    ? l10n.transportFinishRecording
                    : l10n.transportPlayPause,
                icon: Icon(service.isPlaying ? Icons.pause : Icons.play_arrow,
                    color: Colors.white),
                onPressed: () {
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
              IconButton(
                tooltip: l10n.transportStop,
                icon: const Icon(Icons.stop, color: Colors.white),
                onPressed: service.stop,
              ),
              const SizedBox(width: 12),
              Text(
                '${_format(service.positionSeconds)}  /  '
                '${_format(service.durationSeconds)}',
                style: const TextStyle(
                    fontFamily: 'monospace',
                    fontSize: 14,
                    color: Color(0xFFE9EBF0)),
              ),
              const SizedBox(width: 12),
              // Input quantize (§7.5.2 v1.5): recorded MIDI snaps to the
              // chosen grid at take finalization.
              Row(children: [
                Text(l10n.transportIQ,
                    style: const TextStyle(fontSize: 10,
                        color: Color(0xFF9BA1AE))),
                const SizedBox(width: 4),
                DropdownButton<double>(
                  value: _iqValue(service),
                  dropdownColor: const Color(0xFF252830),
                  underline: const SizedBox(),
                  style: const TextStyle(
                      fontSize: 11, color: Color(0xFFE9EBF0)),
                  items: [
                    DropdownMenuItem(
                        value: 0.0, child: Text(l10n.transportOff)),
                    for (final (label, beats) in const [
                      ('1/4', 1.0),
                      ('1/8', 0.5),
                      ('1/16', 0.25),
                    ])
                      DropdownMenuItem(
                          value: beats * service.snapshot.beatSeconds,
                          child: Text(label)),
                  ],
                  onChanged: (grid) {
                    if (grid == null) return;
                    final error = service.setInputQuantize(grid);
                    if (error != null) onError?.call(error);
                  },
                ),
              ]),
              const SizedBox(width: 16),
              GestureDetector(
                onTap: () => _showTempoDialog(context),
                child: Container(
                  padding: const EdgeInsets.symmetric(
                      horizontal: 8, vertical: 3),
                  decoration: BoxDecoration(
                    color: const Color(0xFF2C303A),
                    borderRadius: BorderRadius.circular(4),
                    border: Border.all(color: const Color(0xFF3E4350)),
                  ),
                  child: Text(
                    '${service.tempoBpm.toStringAsFixed(0)} BPM · '
                    '${service.timeSigNumerator}/'
                    '${service.timeSigDenominator}',
                    style: const TextStyle(
                        fontSize: 12,
                        fontWeight: FontWeight.w600,
                        color: Color(0xFFE9EBF0)),
                  ),
                ),
              ),
              const SizedBox(width: 12),
              Text('${service.sampleRate.toStringAsFixed(0)} Hz',
                  style: const TextStyle(
                      fontSize: 11, color: Color(0xFF9BA1AE))),
              const SizedBox(width: 6),
              IconButton(
                tooltip: l10n.transportRestartAudio,
                icon: const Icon(Icons.refresh, size: 16,
                    color: Color(0xFF9BA1AE)),
                onPressed: () {
                  final error = service.restartAudio();
                  if (error != null) onError?.call(error);
                },
              ),
              const Spacer(),
              IconButton(
                tooltip: l10n.transportUndo,
                icon: Icon(Icons.undo,
                    color: service.canUndo ? Colors.white : Colors.white24),
                onPressed: service.canUndo ? service.undo : null,
              ),
              IconButton(
                tooltip: l10n.transportRedo,
                icon: Icon(Icons.redo,
                    color: service.canRedo ? Colors.white : Colors.white24),
                onPressed: service.canRedo ? service.redo : null,
              ),
            ],
          ),
        );
      },
    );
  }
}
