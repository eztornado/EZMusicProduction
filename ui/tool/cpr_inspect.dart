// Debug tool: dump what the Cadence .cpr reader extracts from a Cubase project.
//
//   dart run tool/cpr_inspect.dart "/path/to/Project.cpr"
//
// Pure Dart (no Flutter, no engine): useful for iterating on the reader.
// ignore_for_file: avoid_print

import 'dart:io';

import 'package:cadence_ui/engine/cubase/cpr_reader.dart';

void main(List<String> args) {
  if (args.isEmpty) {
    stderr.writeln('usage: dart run tool/cpr_inspect.dart <file.cpr>');
    exit(2);
  }
  final path = args.first;
  final bytes = File(path).readAsBytesSync();
  final reader = CprReader(bytes, projectName: _baseName(path));

  final project = reader.read();

  print('file:        $path (${bytes.length} bytes)');
  print('app:         ${project.appVersion}');
  print('tempo:       ${project.tempoBpm} bpm');
  print('time sig:    ${project.timeSigNumerator}/${project.timeSigDenominator}');
  print('sample rate: ${project.sampleRate} Hz');
  print('master:      ${project.masterVolumeDb.toStringAsFixed(2)} dB'
      '${project.masterInserts.isEmpty ? "" : "  inserts: ${_chain(project.masterInserts, project.masterInsertStates)}"}');
  print('chunks:      ${reader.chunks.where((c) => c.id == 'ARCH').length} archives');
  print('tracks:      ${project.tracks.length}');
  var i = 0;
  for (final t in project.tracks) {
    print('  [${i++}] ${t.channelId.padRight(10)} '
        'type=${t.typeCode} ${t.volumeDb.toStringAsFixed(2)}dB '
        'pan=${t.pan.toStringAsFixed(3)} '
        '${t.isInstrument ? "instrument" : "audio"} '
        '${t.instrumentName ?? ""} '
        '${t.inserts.isEmpty ? "" : "ins: ${_chain(t.inserts, t.insertStates)} "}'
        '${t.instrumentState == null ? "" : "state: ${t.instrumentState!.size}B "}'
        '"${t.name}"');
  }
  final nodes = reader.extractTrackNodes();
  final dataNodes = nodes.where((n) => n.eventCount > 1).toList();
  print('track nodes: ${nodes.length} '
      '(${dataNodes.length} event containers, '
      '${dataNodes.fold<int>(0, (s, n) => s + n.eventCount)} declared events)');
  print('plugins:     ${project.plugins.length}');
  for (final p in project.plugins) {
    print('    ${p.name}  uid=${p.uid} decoded=${p.decodedVstName}');
  }
  print('markers:     ${project.markers.length}');
  for (final m in project.markers) {
    print('    "${m.name}"');
  }
  final notes = project.midiParts.fold<int>(0, (s, p) => s + p.notes.length);
  print('midi parts:  ${project.midiParts.length} ($notes notes)');
  for (final p in project.midiParts) {
    final pitches = p.notes.map((n) => n.pitch);
    final lo = p.notes.isEmpty ? 0 : pitches.reduce((a, b) => a < b ? a : b);
    final hi = p.notes.isEmpty ? 0 : pitches.reduce((a, b) => a > b ? a : b);
    print('    "${p.name}" @${p.startTicks.round()}t '
        'len=${p.lengthTicks.round()}t notes=${p.notes.length} '
        'pitch=$lo..$hi');
  }
  print('audio events: ${project.audioEvents.length} '
      '(@${project.sampleRate.toStringAsFixed(0)} Hz)');
  for (final e in project.audioEvents) {
    print('    "${e.name}" clip="${e.clipName}" '
        'src=${e.sourceFile} @${e.startTicks.round()}t '
        'len=${e.lengthFrames.round()}f off=${e.sourceOffsetFrames.round()}f '
        'gain=${e.gain.toStringAsFixed(3)} off=${e.offset} owner=${e.ownerOffset}');
  }
}

/// Renders an insert chain as `Name [stateB] > …` so the reader's state
/// extraction is visible at a glance.
String _chain(List<String> names, List<CubasePluginState?> states) {
  final out = <String>[];
  for (var i = 0; i < names.length; i++) {
    final state = i < states.length ? states[i] : null;
    out.add(state == null || state.isEmpty
        ? names[i]
        : '${names[i]} [${state.size}B]');
  }
  return out.join(' > ');
}

String _baseName(String path) {
  final name = path.split('/').last;
  return name.endsWith('.cpr') ? name.substring(0, name.length - 4) : name;
}
