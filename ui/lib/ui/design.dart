// Shared visual language for the desktop app.
//
// The companion remote app (EZMusicProductionRemote) already speaks a darker,
// calmer version of the DAW palette: near-black lanes, one accent, track
// colour only on content, rounded tiles and decibel-scaled controls. These
// tokens bring that language to the desktop window without changing its
// layout (toolbar / arrangement / lower zone / transport).

import 'dart:math' as math;

import 'package:flutter/material.dart';

abstract final class AppColors {
  // Surfaces, from deepest to highest.
  static const bgDeep = Color(0xFF0F1115); // arrangement holes, LCD wells
  static const bgPanel = Color(0xFF14161B); // toolbar, headers, lower zone
  static const surface = Color(0xFF1A1D24); // cards, mixer strips
  static const surfaceAlt = Color(0xFF20242C); // chips, inset controls
  static const surfaceHigh = Color(0xFF262B34); // popovers, hovered controls

  static const border = Color(0xFF2A2F38);
  static const borderStrong = Color(0xFF3A404C);

  static const text = Color(0xFFF0F2F6);
  static const textDim = Color(0xFFA1A8B5);
  static const textFaint = Color(0xFF6B7382);

  static const accent = Color(0xFF4D9DF7);
  static const accentSoft = Color(0xFF1E3655);
  static const record = Color(0xFFFF453A);
  static const success = Color(0xFF3DD68C);
  static const warning = Color(0xFFF5A524);
  static const danger = Color(0xFFE5484D);
}

/// Alpha-blends [tint] over [base]; handy for selected rows and cards.
Color blend(Color base, Color tint, double alpha) =>
    Color.alphaBlend(tint.withValues(alpha: alpha), base);

/// Linear gain → decibel readout used by faders and the mixer.
String formatDb(double linear, {String minusInf = '-inf'}) {
  if (linear <= 0.0008) return minusInf;
  final db = 20 * math.log(linear) / math.ln10;
  final rounded = db.abs() < 0.05 ? 0.0 : db;
  return '${rounded.toStringAsFixed(1)} dB';
}

/// Compact pan readout: C, L32, R64.
String formatPan(double pan) {
  final value = pan.clamp(-1.0, 1.0).toDouble();
  if (value.abs() < 0.01) return 'C';
  final amount = (value.abs() * 100).round();
  return value < 0 ? 'L$amount' : 'R$amount';
}

/// Gain → 0..1 fader position on a decibel scale (unity sits high, like a
/// real console). -60 dB is the bottom of the travel.
double gainToFader(double gain, {double minDb = -60, double maxDb = 6}) {
  if (gain <= 0 || !gain.isFinite) return 0;
  final db = 20 * math.log(gain) / math.ln10;
  return ((db - minDb) / (maxDb - minDb)).clamp(0.0, 1.0);
}

double faderToGain(double position, {double minDb = -60, double maxDb = 6}) {
  final clamped = position.clamp(0.0, 1.0);
  if (clamped <= 0) return 0;
  final db = minDb + (maxDb - minDb) * clamped;
  return math.pow(10, db / 20).toDouble();
}

/// Peak level → 0..1 meter height, also on a decibel scale.
double meterToFader(double peak) => gainToFader(peak, minDb: -60, maxDb: 0);

BoxDecoration panelDecoration({
  Color color = AppColors.surface,
  Color borderColor = AppColors.border,
  double radius = 10,
}) {
  return BoxDecoration(
    color: color,
    borderRadius: BorderRadius.circular(radius),
    border: Border.all(color: borderColor),
  );
}
