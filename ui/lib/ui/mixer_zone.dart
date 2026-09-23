// Mixer lower zone (§7.6 v0): channel strips with fader, pan, M/S and
// read-and-reset peak meters polled once per frame; master strip apart.
// Inserts/sends/EQ arrive when the engine grows them.

import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import '../engine/project_snapshot.dart';

const double _meterMax = 1.5; // linear full scale for the meters

class MixerZone extends StatelessWidget {
  const MixerZone({super.key, required this.service, required this.frameTick});

  final EngineService service;
  final ValueListenable<int> frameTick;

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: service,
      builder: (context, _) {
        final snapshot = service.snapshot;
        final tracks = snapshot.tracks;
        return Container(
          height: 210,
          color: const Color(0xFF1B1D22),
          child: SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                for (var i = 0; i < tracks.length; i++)
                  _Strip(
                    name: tracks[i].name,
                    color: trackColor(tracks[i].colorIndex),
                    volume: tracks[i].volume.clamp(0.0, 2.0).toDouble(),
                    pan: tracks[i].pan,
                    mute: tracks[i].mute,
                    solo: tracks[i].solo,
                    isMaster: false,
                    frameTick: frameTick,
                    readPeak: () => service.meterTrackPeak(i),
                    onVolume: (v) => service.setTrackVolume(tracks[i].id, v),
                    onPan: (v) => service.setTrackPan(tracks[i].id, v),
                    onMute: () => service.setTrackMute(tracks[i].id, !tracks[i].mute),
                    onSolo: () => service.setTrackSolo(tracks[i].id, !tracks[i].solo),
                  ),
                const VerticalDivider(width: 8, color: Color(0xFF32363F)),
                _Strip(
                  name: 'Master',
                  color: const Color(0xFF9BA1AE),
                  volume: snapshot.masterGain.clamp(0.0, 2.0).toDouble(),
                  pan: 0,
                  mute: false,
                  solo: false,
                  isMaster: true,
                  frameTick: frameTick,
                  readPeakL: () => service.meterMasterPeak(0),
                  readPeakR: () => service.meterMasterPeak(1),
                  onVolume: (v) => service.setMasterGain(v),
                ),
              ],
            ),
          ),
        );
      },
    );
  }
}

class _Strip extends StatelessWidget {
  const _Strip({
    required this.name,
    required this.color,
    required this.volume,
    required this.pan,
    required this.mute,
    required this.solo,
    required this.isMaster,
    required this.frameTick,
    required this.onVolume,
    this.readPeak,
    this.readPeakL,
    this.readPeakR,
    this.onPan,
    this.onMute,
    this.onSolo,
  });

  final String name;
  final Color color;
  final double volume;
  final double pan;
  final bool mute;
  final bool solo;
  final bool isMaster;
  final ValueListenable<int> frameTick;
  final void Function(double) onVolume;
  final void Function(double)? onPan;
  final VoidCallback? onMute;
  final VoidCallback? onSolo;
  final double Function()? readPeak;
  final double Function()? readPeakL;
  final double Function()? readPeakR;

  String _db(double linear) =>
      linear <= 0.001 ? '-inf' : (20 * math.log(linear)).toStringAsFixed(1);

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: frameTick,
      builder: (context, _) {
        return Container(
          width: 96,
          margin: const EdgeInsets.symmetric(horizontal: 3, vertical: 6),
          decoration: BoxDecoration(
            color: const Color(0xFF252830),
            borderRadius: BorderRadius.circular(6),
            border: Border.all(color: const Color(0xFF32363F)),
          ),
          child: Column(
            children: [
              Container(
                height: 18,
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: color.withValues(alpha: 0.25),
                  borderRadius:
                      const BorderRadius.vertical(top: Radius.circular(5)),
                ),
                child: Text(name,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                        fontSize: 10,
                        fontWeight: FontWeight.w600,
                        color: Color(0xFFE9EBF0))),
              ),
              const SizedBox(height: 4),
              Expanded(
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    // Vertical fader (rotated slider).
                    SizedBox(
                      width: 26,
                      child: RotatedBox(
                        quarterTurns: 3,
                        child: SliderTheme(
                          data: SliderTheme.of(context).copyWith(
                            trackHeight: 3,
                            thumbShape:
                                const RoundSliderThumbShape(enabledThumbRadius: 7),
                            overlayShape:
                                const RoundSliderOverlayShape(overlayRadius: 10),
                            padding: EdgeInsets.zero,
                          ),
                          child: Slider(
                            min: 0,
                            max: 2,
                            value: volume,
                            onChanged: onVolume,
                          ),
                        ),
                      ),
                    ),
                    const SizedBox(width: 4),
                    if (isMaster)
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 6),
                        child: Row(
                          children: [
                            _MeterBar(readPeak: readPeakL ?? () => 0.0),
                            const SizedBox(width: 2),
                            _MeterBar(readPeak: readPeakR ?? () => 0.0),
                          ],
                        ),
                      )
                    else
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 6),
                        child: _MeterBar(readPeak: readPeak ?? () => 0.0),
                      ),
                  ],
                ),
              ),
              Text(_db(volume),
                  style: TextStyle(
                      fontSize: 9,
                      fontFamily: 'monospace',
                      color: const Color(0xFF9BA1AE))),
              if (!isMaster)
                Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 6),
                  child: Row(
                    children: [
                      _TinyButton(
                          label: 'M',
                          color: const Color(0xFFF5A524),
                          active: mute,
                          onTap: onMute),
                      const SizedBox(width: 3),
                      _TinyButton(
                          label: 'S',
                          color: const Color(0xFF4D9DF7),
                          active: solo,
                          onTap: onSolo),
                    ],
                  ),
                ),
              SizedBox(
                height: 16,
                child: onPan != null
                    ? SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          trackHeight: 2,
                          thumbShape:
                              const RoundSliderThumbShape(enabledThumbRadius: 4),
                          overlayShape:
                              const RoundSliderOverlayShape(overlayRadius: 6),
                          padding: EdgeInsets.zero,
                        ),
                        child: Slider(min: -1, max: 1, value: pan, onChanged: onPan),
                      )
                    : const SizedBox(),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _MeterBar extends StatelessWidget {
  const _MeterBar({required this.readPeak});

  final double Function() readPeak;

  Color _color(double v) {
    if (v > 1.0) return const Color(0xFFE5484D);
    if (v > 0.7) return const Color(0xFFF5A524);
    return const Color(0xFF3DD68C);
  }

  @override
  Widget build(BuildContext context) {
    // Read-and-reset: this build IS the poll (driven by frameTick upstream).
    final peak = readPeak().clamp(0.0, _meterMax);
    return Container(
      width: 7,
      decoration: BoxDecoration(
        color: const Color(0xFF16171B),
        borderRadius: BorderRadius.circular(2),
      ),
      child: Align(
        alignment: Alignment.bottomCenter,
        child: FractionallySizedBox(
          heightFactor: (peak / _meterMax).clamp(0.0, 1.0),
          child: Container(
            decoration: BoxDecoration(
              color: _color(peak),
              borderRadius: BorderRadius.circular(2),
            ),
          ),
        ),
      ),
    );
  }
}

class _TinyButton extends StatelessWidget {
  const _TinyButton({
    required this.label,
    required this.color,
    required this.active,
    required this.onTap,
  });

  final String label;
  final Color color;
  final bool active;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    return Expanded(
      child: GestureDetector(
        onTap: onTap,
        child: Container(
          height: 18,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: active ? color : const Color(0xFF2C303A),
            borderRadius: BorderRadius.circular(3),
          ),
          child: Text(label,
              style: TextStyle(
                  fontSize: 10,
                  fontWeight: FontWeight.w600,
                  color: active ? Colors.black : const Color(0xFF9BA1AE))),
        ),
      ),
    );
  }
}
