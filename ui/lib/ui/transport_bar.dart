// Transport bar v0: play/pause, stop, position readout, undo/redo.
// Rebuilds on engine edits and on the shared frame tick while playing.

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../engine/engine_service.dart';

class TransportBar extends StatelessWidget {
  const TransportBar({
    super.key,
    required this.service,
    required this.frameTick,
    this.onRecordError,
  });

  final EngineService service;
  final ValueListenable<int> frameTick;
  final void Function(String? error)? onRecordError;

  String _format(double seconds) {
    final m = (seconds / 60).floor();
    final s = (seconds - m * 60).toStringAsFixed(1).padLeft(4, '0');
    return '$m:$s';
  }

  @override
  Widget build(BuildContext context) {
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
                tooltip: service.isRecording ? 'Detener grabación' : 'Grabar',
                icon: Icon(Icons.fiber_manual_record,
                    color: service.isRecording
                        ? const Color(0xFFE5484D)
                        : Colors.white38),
                onPressed: () {
                  if (service.isRecording) {
                    service.stopRecord();
                  } else {
                    onRecordError?.call(service.startRecord());
                  }
                },
              ),
              IconButton(
                tooltip: 'Metrónomo',
                icon: Icon(Icons.music_note,
                    color: service.metronomeOn
                        ? const Color(0xFF4D9DF7)
                        : Colors.white38),
                onPressed: () => service.toggleMetronome(),
              ),
              IconButton(
                tooltip: 'Play / Pausa',
                icon: Icon(service.isPlaying ? Icons.pause : Icons.play_arrow,
                    color: Colors.white),
                onPressed: () =>
                    service.isPlaying ? service.pause() : service.play(),
              ),
              IconButton(
                tooltip: 'Stop',
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
              const SizedBox(width: 16),
              Text('${service.sampleRate.toStringAsFixed(0)} Hz',
                  style: const TextStyle(
                      fontSize: 11, color: Color(0xFF9BA1AE))),
              const Spacer(),
              IconButton(
                tooltip: 'Undo',
                icon: Icon(Icons.undo,
                    color: service.canUndo ? Colors.white : Colors.white24),
                onPressed: service.canUndo ? service.undo : null,
              ),
              IconButton(
                tooltip: 'Redo',
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
