// Pure-Dart tests for the Cubase .cpr reader. No engine/FFI dependency.
//
// The synthetic fixture exercises the RIFF/NUND container, the named-record
// grammar, mixer-channel extraction (name/type/volume/pan), MIDI note decoding
// and the tempo / time-signature anchors. A guarded test additionally checks a
// real project when one is present on this machine.

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/cubase/cpr_reader.dart';

void main() {
  group('container', () {
    test('rejects a buffer that is not RIFF/NUND', () {
      final reader = CprReader(Uint8List.fromList(List.filled(32, 0x41)));
      expect(reader.parseContainer, throwsFormatException);
    });

    test('parses ROOT/ARCH pairs and finds the arrangement', () {
      final bytes = _syntheticProject();
      final reader = CprReader(bytes)..parseContainer();
      expect(reader.chunks.where((c) => c.id == 'ARCH'), hasLength(2));
      expect(reader.arrangement, isNotEmpty);
    });
  });

  group('synthetic project', () {
    test('extracts version, tempo and time signature', () {
      final project = CprReader(_syntheticProject()).read();
      expect(project.appVersion, 'Version 13.0.40');
      expect(project.tempoBpm, closeTo(120.0, 0.01));
      expect(project.timeSigNumerator, 4);
      expect(project.timeSigDenominator, 4);
    });

    test('extracts the mixer channel as a track', () {
      final project = CprReader(_syntheticProject()).read();
      expect(project.tracks, hasLength(1));
      final track = project.tracks.single;
      expect(track.name, 'Test Track');
      expect(track.typeCode, 4);
      expect(track.isInstrument, isTrue);
      expect(track.channelId, 'Ch1');
      expect(track.volumeDb, closeTo(-6.0, 1e-9));
      expect(track.volumeLinear, closeTo(0.5011872, 1e-6));
      expect(track.pan, closeTo(-0.5, 1e-6)); // blob centre 0.25 → pan -0.5
    });

    test('reads the master fader from the mixer archive', () {
      // The output channel lives outside the arrangement archive; a project
      // without one stays at unity.
      expect(CprReader(_syntheticProject()).read().masterVolumeDb, 0.0);
      final project = CprReader(_syntheticProject(master: true)).read();
      expect(project.masterVolumeDb, closeTo(-2.17, 1e-9));
      expect(project.masterVolumeLinear, closeTo(0.7789328, 1e-6));
      // The output channel is not a track.
      expect(project.tracks, hasLength(1));
    });

    test('reads a channel\'s insert chain, not its channel strip', () {
      final project = CprReader(
              _syntheticProject(inserts: ['Ozone 11 Equalizer', 'Raum']))
          .read();
      // `InsertFolder` slots come back in order, and the strip's EQ — which the
      // fixture writes in `StripFolder`, after the folder — is not an insert.
      expect(project.tracks.single.inserts, ['Ozone 11 Equalizer', 'Raum']);
      expect(CprReader(_syntheticProject()).read().tracks.single.inserts,
          isEmpty);
    });

    test('reads the master insert chain from the mixer archive', () {
      final project = CprReader(_syntheticProject(
              master: true,
              masterInserts: ['MultibandCompressor', 'Ozone 11 Elements']))
          .read();
      expect(project.masterInserts, ['MultibandCompressor', 'Ozone 11 Elements']);
      expect(CprReader(_syntheticProject()).read().masterInserts, isEmpty);
    });

    test('reads each plug-in\'s serialized state', () {
      final ozone = _bytes([0xde, 0x8a, 0x68, 0x00, 1, 2, 3]);
      final controller = _bytes([9, 9, 9]);
      final project = CprReader(_syntheticProject(
              instrument: 'Musio',
              instrumentComponent: _bytes([0x56, 0x43, 0x32, 0x21]),
              inserts: ['Ozone 11 Equalizer', 'Raum'],
              insertComponents: [ozone, null],
              insertControllers: [controller, null],
              master: true,
              masterInserts: ['Ozone 11 Elements'],
              masterInsertComponents: [ozone]))
          .read();
      final track = project.tracks.single;
      // The VSTi's component state, from the Synth Slot's Plugin object.
      expect(track.instrumentState, isNotNull);
      expect(track.instrumentState!.component, [0x56, 0x43, 0x32, 0x21]);
      expect(track.instrumentState!.controller, isEmpty);
      // One state per insert, aligned with the names; a slot without state is
      // null so the lists stay index-compatible.
      expect(track.insertStates, hasLength(2));
      expect(track.insertStates[0]!.component, ozone);
      expect(track.insertStates[0]!.controller, controller);
      expect(track.insertStates[1], isNull);
      // The output channel's inserts carry their state too.
      expect(project.masterInsertStates, hasLength(1));
      expect(project.masterInsertStates[0]!.component, ozone);
      // No state anywhere when the file stores none.
      final plain = CprReader(_syntheticProject(inserts: ['Raum'])).read();
      expect(plain.tracks.single.insertStates.single, isNull);
      expect(plain.tracks.single.instrumentState, isNull);
    });

    test('reads the VSTi hosted by an instrument channel', () {
      // `Synth Slot → Plugin → Plugin Name`, inside the channel's own range.
      final project =
          CprReader(_syntheticProject(instrument: 'ZENOLOGY')).read();
      expect(project.tracks.single.instrumentName, 'ZENOLOGY');
      // A channel without a Synth Slot keeps a null instrument (the engine
      // then plays the internal synth).
      expect(CprReader(_syntheticProject()).read().tracks.single.instrumentName,
          isNull);
    });

    test('decodes MIDI notes into a part bound to its track', () {
      final project = CprReader(_syntheticProject()).read();
      expect(project.midiParts, hasLength(1));
      final part = project.midiParts.single;
      expect(part.name, 'Test Track');
      expect(part.notes, hasLength(2));

      final a = part.notes[0];
      expect(a.pitch, 60);
      expect(a.velocity, 100);
      expect(a.startTicks, closeTo(480.0, 1e-9));
      expect(a.lengthTicks, closeTo(240.0, 1e-9));

      final b = part.notes[1];
      expect(b.pitch, 64);
      expect(b.velocity, 80);
      expect(b.startTicks, closeTo(960.0, 1e-9));
      expect(b.lengthTicks, closeTo(480.0, 1e-9));

      expect(part.startTicks, closeTo(480.0, 1e-9));
      expect(part.endTicks, closeTo(1440.0, 1e-9));
      expect(part.lengthTicks, closeTo(960.0, 1e-9));
    });

    test('skips controller events (non-0x90 records)', () {
      final project = CprReader(_syntheticProject(controller: true)).read();
      expect(project.midiParts.single.notes, hasLength(2));
    });
  });

  group('part objects', () {
    test('places notes at pos + rel - third and hides notes outside the window',
        () {
      final project = CprReader(_syntheticObjectProject()).read();
      expect(project.midiParts, hasLength(2));

      // Part A: pos 1920, len 1920, content origin 480. Notes stored at 480 and
      // 960 land on 1920 and 2400; the one at 4800 is outside the part window.
      final a = project.midiParts[0];
      expect(a.startTicks, closeTo(1920.0, 1e-9));
      expect(a.lengthTicks, closeTo(1920.0, 1e-9));
      expect(a.notes.map((n) => n.startTicks).toList(), [1920.0, 2400.0]);
      expect(a.notes.map((n) => n.pitch).toList(), [60, 64]);

      // Part B: origin 0, so the stored positions are already part-relative.
      final b = project.midiParts[1];
      expect(b.startTicks, closeTo(9600.0, 1e-9));
      expect(b.notes.map((n) => n.startTicks).toList(), [9600.0, 10080.0]);
    });

    test('drops a track node that merely repeats its parts', () {
      final project = CprReader(_syntheticObjectProject(withNode: true)).read();
      // The track node spans the whole song and reproduces both parts' notes;
      // only the two real parts must survive (4 notes, not 8).
      expect(project.midiParts, hasLength(2));
      expect(project.midiParts.expand((p) => p.notes), hasLength(4));
    });
  });

  group('part → track assignment', () {
    test('strips the duplicated-track marker', () {
      expect(cubaseBaseTrackName('Xpand!2_x64 01 (D)'), 'xpand!2_x64 01');
      expect(cubaseBaseTrackName('  Musio 01  '), 'musio 01');
      expect(cubaseBaseTrackName('SINE Player 01'), 'sine player 01');
    });

    test('spreads same-named parts over the matching tracks in order', () {
      final parts = [
        _midiPart('Musio 01', 3),
        _midiPart('Musio 01', 2),
        _midiPart('Musio 01', 2),
      ];
      final names = ['Musio 01', 'Musio 01 (D)', 'Musio 01 (D)'];
      expect(assignMidiPartsToTracks(parts, names), [0, 1, 2]);
    });

    test('groups a duplicated track with its original (7.cpr)', () {
      final parts = [
        _midiPart('Kontakt 7 01', 344),
        _midiPart('Xpand!2_x64 01', 532),
        _midiPart('Xpand!2_x64 01', 532),
        _midiPart('SINE Player 01', 148),
        _midiPart('Xpand!2_x64 02', 118),
      ];
      final names = [
        'Kontakt 7 01',
        'Musio 01',
        'Xpand!2_x64 01',
        'Xpand!2_x64 01 (D)',
        'SINE Player 01',
        'Xpand!2_x64 02',
      ];
      expect(assignMidiPartsToTracks(parts, names), [0, 2, 3, 4, 5]);
    });

    test('falls back to the first track, or -1 with no tracks', () {
      expect(assignMidiPartsToTracks([_midiPart('Nope', 1)], ['A', 'B']), [0]);
      expect(assignMidiPartsToTracks([_midiPart('A', 1)], []), [-1]);
    });
  });

  group('audio events', () {
    test('decodes events and resolves their media pool files', () {
      final project = CprReader(_syntheticAudioProject()).read();
      expect(project.sampleRate, 44100.0); // from the metadata archive
      expect(project.audioEvents, hasLength(2));

      final kick = project.audioEvents.firstWhere((e) => e.clipName == 'Kick');
      expect(kick.name, 'Audio Test'); // owning track node
      expect(kick.sourceFile, 'Kick.wav'); // matched through the pool
      expect(kick.startTicks, 1920); // ticks, not frames
      expect(kick.lengthFrames, 48000); // source frames at 44.1 kHz
      expect(kick.sourceOffsetFrames, 96000);
      expect(kick.gain, closeTo(0.5, 1e-6));
      expect(kick.ownerOffset, isNonNegative);

      final snare = project.audioEvents.firstWhere((e) => e.clipName == 'Snare');
      expect(snare.sourceFile, 'Snare.wav'); // inlined media descriptor
      expect(snare.gain, 1.0);
      expect(snare.ownerOffset, kick.ownerOffset); // same owning node
    });

    test('assigns the events of one owning node to the same track', () {
      final project = CprReader(_syntheticAudioProject()).read();
      final targets = assignAudioEventsToTracks(
        project.audioEvents,
        [for (final t in project.tracks) t.name],
      );
      expect(targets, hasLength(project.audioEvents.length));
      expect(targets.toSet(), hasLength(1));
      expect(targets.first, isNonNegative);
    });

    test('reports no audio events for a MIDI-only arrangement', () {
      expect(CprReader(_syntheticObjectProject()).read().audioEvents, isEmpty);
    });
  });

  // Real files are machine-specific; skip quietly when absent.
  group('real project (guarded)', () {
    const paths = [
      '/Users/ivan/Documents/CUBASE_PROYECTS/HOLO THEME/HOLO THEME.cpr',
      '/Users/ivan/Documents/HOLO THEME.cpr',
    ];

    test('parses the first available HOLO project', () {
      File? file;
      for (final p in paths) {
        final f = File(p);
        if (f.existsSync()) {
          file = f;
          break;
        }
      }
      if (file == null) {
        markTestSkipped('no real .cpr available on this machine');
        return;
      }
      final project = CprReader(file.readAsBytesSync()).read();
      expect(project.appVersion, startsWith('Version 13.'));
      expect(project.tempoBpm, closeTo(120.0, 0.5));
      expect(project.timeSigNumerator, 4);
      expect(project.timeSigDenominator, 4);
      expect(project.tracks, isNotEmpty);
      // IDs are unique after de-duplicating the double serialization.
      final ids = project.tracks.map((t) => t.channelId).toList();
      expect(ids.toSet().length, ids.length);
      expect(project.plugins, isNotEmpty);
      // Track names mis-encoded by Cubase are repaired (no raw mojibake).
      expect(project.tracks.map((t) => t.name), isNot(contains('Sin tiÌtulo')));
      // MIDI notes are decoded and bound to a resolved track name.
      expect(project.midiParts, isNotEmpty);
      final notes = project.midiParts.expand((p) => p.notes).toList();
      expect(notes, isNotEmpty);
      expect(
        notes.every((n) => n.pitch >= 0 && n.pitch <= 127),
        isTrue,
      );
      expect(
        notes.every((n) => n.velocity > 0 && n.velocity <= 127),
        isTrue,
      );
      expect(
        notes.every((n) => n.lengthTicks >= 0 && n.startTicks >= 0),
        isTrue,
      );
      expect(project.midiParts.map((p) => p.name), isNot(contains('')));
      // Every part resolves to a valid track index.
      final targets = assignMidiPartsToTracks(
        project.midiParts,
        project.tracks.map((t) => t.name).toList(),
      );
      expect(targets, hasLength(project.midiParts.length));
      expect(targets.every((i) => i >= 0 && i < project.tracks.length), isTrue);
      // Multi-part tracks (the bug this reader used to have): every part keeps
      // its own position, all 1491 notes survive, and no two parts that belong
      // to the same track node overlap.
      expect(notes, hasLength(1491));
      expect(project.midiParts, hasLength(28));
      for (final p in project.midiParts) {
        expect(p.startTicks % 480, closeTo(0.0, 1e-9));
        expect(p.lengthTicks, greaterThan(0));
        expect(p.name, isNotEmpty);
      }
      final byOwner = <int, List<CubaseMidiPart>>{};
      for (final p in project.midiParts) {
        if (p.ownerOffset >= 0) {
          byOwner.putIfAbsent(p.ownerOffset, () => []).add(p);
        }
      }
      for (final group in byOwner.values) {
        group.sort((a, b) => a.startTicks.compareTo(b.startTicks));
        for (var i = 1; i < group.length; i++) {
          expect(group[i].startTicks,
              greaterThanOrEqualTo(group[i - 1].endTicks));
        }
      }
    });

    test('keeps multi-part track timings (7.cpr)', () {
      final file = File('/Users/ivan/Documents/CUBASE_PROYECTS/7/7.cpr');
      if (!file.existsSync()) {
        markTestSkipped('no 7.cpr available on this machine');
        return;
      }
      final parts = CprReader(file.readAsBytesSync()).read().midiParts;
      // Cubase's own MIDI export of this project has five parts on five tracks,
      // four at tick 0 and one at bar 2 (23040 ticks), with these note counts.
      expect(parts.map((p) => p.startTicks.round()).toList(),
          [0, 0, 0, 0, 23040]);
      expect(parts.map((p) => p.notes.length).toList(), [344, 532, 532, 148, 118]);
      expect(parts.map((p) => p.name).toList(), [
        'Kontakt 7 01',
        'Xpand!2_x64 01',
        'Xpand!2_x64 01 (D)',
        'SINE Player 01',
        'Xpand!2_x64 02',
      ]);
    });
  });
}

// ---- fixture builders -------------------------------------------------------

Uint8List _syntheticProject(
    {bool controller = false,
    bool master = false,
    String? instrument,
    Uint8List? instrumentComponent,
    List<String> inserts = const [],
    List<Uint8List?> insertComponents = const [],
    List<Uint8List?> insertControllers = const [],
    List<String> masterInserts = const [],
    List<Uint8List?> masterInsertComponents = const []}) {
  final app = _bytes([
    ..._ascii('Version 13.0.40'),
  ]);
  final arrangement = _bytes([
    ..._ascii('MTempoTrackEvent'),
    ...List.filled(16, 0),
    ..._f32be(0.5), // seconds per beat → 120 BPM
    ..._ascii('\x00\x00\x00\x09Numerator\x00\x00\x01'),
    ..._i64bytes(4),
    ..._ascii('\x00\x00\x00\x0cDenominator\x00\x00\x01'),
    ..._i64bytes(4),
    ..._channel(
        instrument: instrument,
        instrumentComponent: instrumentComponent,
        inserts: inserts,
        insertComponents: insertComponents,
        insertControllers: insertControllers),
    if (controller) ..._controller(),
    ..._note(startTicks: 480, lengthTicks: 240, pitch: 60, velocity: 100),
    ..._note(startTicks: 960, lengthTicks: 480, pitch: 64, velocity: 80),
  ]);
  return _riffNund([
    ('App', 'PAppVersion', app),
    ('Arr', 'PArrangement', arrangement),
    // Cubase keeps the output channel in the VST Mixer archive, not in the
    // arrangement one.
    if (master)
      (
        'Mix',
        'CVST Mixer',
        _channel(
            name: 'Stereo Out',
            type: 8,
            volumeDb: -2.17,
            idString: 'OutputChannel',
            inserts: masterInserts,
            insertComponents: masterInsertComponents)
      ),
  ]);
}

/// Synthetic arrangement whose MIDI parts are real Cubase part objects
/// (`ref | u32 payloadLen | u16 0 | f64 pos | f64 len | f64 third` + notes),
/// which is how Cubase stores multi-part tracks. With [withNode] a track node
/// object wraps both parts and repeats their notes.
Uint8List _syntheticObjectProject({bool withNode = false}) {
  final parts = _bytes([
    ..._partObject(pos: 1920, len: 1920, third: 480, notes: [
      _note(startTicks: 480, lengthTicks: 240, pitch: 60, velocity: 100),
      _note(startTicks: 960, lengthTicks: 240, pitch: 64, velocity: 90),
      _note(startTicks: 4800, lengthTicks: 240, pitch: 67, velocity: 80),
    ]),
    ..._partObject(pos: 9600, len: 1920, third: 0, notes: [
      _note(startTicks: 0, lengthTicks: 240, pitch: 62, velocity: 100),
      _note(startTicks: 480, lengthTicks: 240, pitch: 65, velocity: 90),
    ]),
  ]);
  final body = withNode
      ? _bytes([
          ..._partContainer(pos: 0, len: 576000, third: 0, child: parts),
        ])
      : parts;
  final arrangement = _bytes([
    ..._ascii('MTempoTrackEvent'),
    ...List.filled(16, 0),
    ..._f32be(0.5), // seconds per beat → 120 BPM
    ..._ascii('\x00\x00\x00\x09Numerator\x00\x00\x01'),
    ..._i64bytes(4),
    ..._ascii('\x00\x00\x00\x0cDenominator\x00\x00\x01'),
    ..._i64bytes(4),
    ..._channel(),
    ...body,
    ...List.filled(32, 0),
  ]);
  return _riffNund([
    ('App', 'PAppVersion', _bytes([..._ascii('Version 13.0.40')])),
    ('Arr', 'PArrangement', arrangement),
  ]);
}

/// One part object. [padding] keeps the trailing note fully addressable.
Uint8List _partObject({
  required double pos,
  required double len,
  required double third,
  required List<Uint8List> notes,
  int padding = 32,
}) =>
    _objectWithHeader(const [0x80, 0x12, 0x89, 0x7d], pos, len, third, [
      ...notes,
      List.filled(padding, 0),
    ]);

/// A track node object wrapping [child] (its payload repeats the child bytes).
Uint8List _partContainer({
  required double pos,
  required double len,
  required double third,
  required Uint8List child,
}) =>
    _objectWithHeader(const [0x80, 0x00, 0x07, 0xc0], pos, len, third, [child]);

/// Synthetic arrangement with a Cubase audio track: the first audio track is
/// written inline behind its `MAudioTrackEvent` class definition (exactly how
/// Cubase serializes it), and its payload holds two audio events. One resolves
/// its source through an inlined media descriptor and the other through the
/// media pool file name.
Uint8List _syntheticAudioProject() {
  final events = _bytes([
    ..._audioEvent(
      pos: 1920,
      len: 48000,
      sourceOffset: 96000,
      clip: 'Kick',
      gain: 0.5,
    ),
    ..._audioEvent(
      pos: 9600,
      len: 24000,
      sourceOffset: 0,
      clip: 'Snare',
      descriptor: 'Snare.wav',
    ),
  ]);
  final arrangement = _bytes([
    ..._ascii('MTempoTrackEvent'),
    ...List.filled(16, 0),
    ..._f32be(0.5), // seconds per beat → 120 BPM
    ..._ascii('\x00\x00\x00\x09Numerator\x00\x00\x01'),
    ..._i64bytes(4),
    ..._ascii('\x00\x00\x00\x0cDenominator\x00\x00\x01'),
    ..._i64bytes(4),
    ..._channel(name: 'Audio Test'),
    ..._str8Value('Kick.wav'), // media pool entry
    ..._inlineAudioTrack(pos: 0, len: 576000, third: 0, child: _bytes([
      ..._str8Value('Audio Test'), // track name at the head of the node
      ...events,
    ])),
    ...List.filled(32, 0),
  ]);
  // `AudioSampleRate` + a `Float` record lives in the metadata archive, not in
  // the arrangement, so the reader must find it across the whole file.
  final meta = BytesBuilder(copy: false);
  meta.add(_ascii('AudioSampleRate'));
  _name(meta, 'Float');
  meta.add([0x00, 0x04]);
  meta.add(_f64be(44100));
  return _riffNund([
    ('App', 'PAppVersion', _bytes([..._ascii('Version 13.0.40')])),
    ('Arr', 'PArrangement', arrangement),
    ('Meta', 'StMedia::PAttributes', meta.toBytes()),
  ]);
}

/// One audio event object: the MIDI-part header plus a payload holding the clip
/// display name, a `u32 0 | f32 gain` pair and an optional media descriptor.
Uint8List _audioEvent({
  required double pos,
  required double len,
  required double sourceOffset,
  required String clip,
  double gain = 1.0,
  String? descriptor,
}) =>
    _objectWithHeader(
      const [0x80, 0x32, 0xc0, 0x5d],
      pos,
      len,
      sourceOffset,
      [
        _str8Value(clip),
        _u32bytes(0),
        _f32be(gain),
        if (descriptor != null) _str8Value(descriptor),
        List.filled(8, 0),
      ],
    );

/// The inline first audio track node: `ff ff ff ff | u32 nameLen |
/// "MAudioTrackEvent" | \0 | \x00\x01` followed by the usual object header (the
/// `\x00\x01` track marker doubles as the object's trailing reference bytes).
Uint8List _inlineAudioTrack({
  required double pos,
  required double len,
  required double third,
  required Uint8List child,
}) {
  final b = BytesBuilder(copy: false);
  b.add(_ascii('\xff\xff\xff\xff'));
  b.add(_u32bytes(0x11)); // "MAudioTrackEvent" + NUL
  b.add(_ascii('MAudioTrackEven')); // the closing "t" is the first ref byte
  b.add(_objectWithHeader(const [0x74, 0x00, 0x00, 0x01], pos, len, third, [child]));
  return b.toBytes();
}

/// A bare `string8` value: `u32 len | text | \0 | EF BB BF`, length included.
Uint8List _str8Value(String s) {
  final b = BytesBuilder(copy: false);
  final text = _ascii(s);
  _u32(b, text.length + 1 + 3); // text + NUL + BOM
  b.add(text);
  b.addByte(0);
  b.add([0xef, 0xbb, 0xbf]);
  return b.toBytes();
}

Uint8List _objectWithHeader(
  List<int> ref,
  double pos,
  double len,
  double third,
  List<List<int>> body,
) {
  final bodyBytes = BytesBuilder(copy: false);
  for (final chunk in body) {
    bodyBytes.add(chunk);
  }
  final inner = bodyBytes.toBytes();
  final b = BytesBuilder(copy: false);
  b.add(ref);
  b.add(_u32bytes(26 + inner.length)); // payload length: fields + body
  b.add([0, 0]); // z0
  b.add(_f64be(pos));
  b.add(_f64be(len));
  b.add(_f64be(third));
  b.add(inner);
  return b.toBytes();
}

/// A Cubase note event: `0x90` type byte, `f64` start (480 PPQ ticks), pitch /
/// velocity bytes, five property tags and an `f64` length after them (113 bytes).
Uint8List _note({
  required double startTicks,
  required double lengthTicks,
  required int pitch,
  required int velocity,
}) {
  final b = BytesBuilder(copy: false);
  b.addByte(0x90);
  b.add(_f64be(startTicks));
  b.addByte(0);
  b.addByte(pitch);
  b.addByte(velocity);
  b.add([0, 0]);
  _u32(b, 5); // five property tags
  _midiTag(b, 'GLFX', 1, 0);
  _midiTag(b, 'Pler', 4, 0);
  _midiTag(b, 'TDRH', 4, velocity / 128.0);
  _midiTag(b, 'VffO', 4, 0.5);
  _midiTag(b, 'adcn', 1, 0x2000000000);
  b.add(_f64be(lengthTicks));
  b.add(List.filled(17, 0));
  return b.toBytes();
}

/// A non-note event (controller): same layout shape but type `0xb0` and three
/// tags, so it must be skipped by the note decoder.
Uint8List _controller() {
  final b = BytesBuilder(copy: false);
  b.addByte(0xb0);
  b.add(_f64be(0));
  b.addByte(0);
  b.addByte(7);
  b.addByte(0);
  b.add([0, 0]);
  _u32(b, 3);
  _midiTag(b, 'GLFX', 1, 0);
  _midiTag(b, 'Pler', 4, 0);
  _midiTag(b, 'TDRH', 4, 0);
  b.add(_f64be(0));
  b.add(List.filled(17, 0));
  return b.toBytes();
}

void _midiTag(BytesBuilder b, String name, int type, num value) {
  b.add(_ascii(name));
  b.add([(type >> 8) & 0xff, type & 0xff]);
  if (type == 1) {
    b.add(_i64bytes(value.toInt()));
  } else {
    b.add(_f64be(value.toDouble()));
  }
}

/// A MIDI part with [noteCount] identical placeholder notes (for assignment
/// tests where only the name and the note count matter).
CubaseMidiPart _midiPart(String name, int noteCount) {
  final part = CubaseMidiPart(name: name, offset: 0);
  for (var i = 0; i < noteCount; i++) {
    part.notes.add(const CubaseMidiNote(
      startTicks: 0,
      lengthTicks: 0,
      pitch: 60,
      velocity: 100,
    ));
  }
  return part;
}

Uint8List _channel({
  String name = 'Test Track',
  int type = 4,
  double volumeDb = -6.0,
  String idString = 'Ch1',
  String? instrument,
  Uint8List? instrumentComponent,
  Uint8List? instrumentController,
  List<String> inserts = const [],
  List<Uint8List?> insertComponents = const [],
  List<Uint8List?> insertControllers = const [],
}) {
  final b = BytesBuilder(copy: false);
  // Name object: { String: <name> } — byte-identical to the anchor.
  _name(b, 'Name');
  b.add([0x00, 0x02, 0x00, 0x06]); // type 2, subtype 6 (object)
  _u32(b, 1);
  _str8Record(b, 'String', name);
  _i64Record(b, 'Type', type);
  // Volume object: Value (raw scale) + AnchorValue (dB, authoritative).
  _name(b, 'Volume');
  b.add([0x00, 0x02, 0x00, 0x06]);
  _u32(b, 2);
  _f64Record(b, 'Value', 25856.0);
  _f64Record(b, 'AnchorValue', volumeDb);
  // Panner object: audioComponent blob, first LE f32 = 0.25 → pan -0.5.
  _name(b, 'Panner');
  b.add([0x00, 0x02, 0x00, 0x06]);
  _u32(b, 1);
  _name(b, 'audioComponent');
  b.add([0x00, 0x02, 0x00, 0x07]); // type 2, subtype 7 (blob)
  final blob = _bytes([..._f32le(0.25), ..._f32le(0.25)]);
  _u32(b, blob.length);
  b.add(blob);
  _str8Record(b, 'IDString', idString);
  if (inserts.isNotEmpty) {
    // `hasAudioInserts, InsertFolder { RuntimeID, Bypass, SeparationPosition,
    // Slot[ … ] }` — an occupied slot per insert, then the channel fields that
    // close the folder.
    _i64Record(b, 'hasAudioInserts', 1);
    _name(b, 'InsertFolder');
    b.add([0x00, 0x02, 0x00, 0x06]);
    _u32(b, 4);
    _i64Record(b, 'RuntimeID', 100);
    _i64Record(b, 'Bypass', 0);
    _i64Record(b, 'SeparationPosition', 0);
    _name(b, 'Slot');
    b.add([0x00, 0x02, 0x00, 0x05]); // array: n items, each `u32` record count
    _u32(b, inserts.length);
    for (var s = 0; s < inserts.length; s++) {
      final component = s < insertComponents.length ? insertComponents[s] : null;
      final controller = s < insertControllers.length ? insertControllers[s] : null;
      _u32(b, 3);
      _i64Record(b, 'RuntimeID', 200);
      _str8Record(b, 'Plugin isA', 'VstCtrlInternalEffect');
      _name(b, 'Plugin');
      b.add([0x00, 0x02, 0x00, 0x06]);
      _u32(b, 1 + (component != null ? 1 : 0) + (controller != null ? 1 : 0));
      _str8Record(b, 'Plugin Name', inserts[s]);
      _stateRecords(b, component, controller);
    }
    _i64Record(b, 'SummingMode', 0);
    // The channel strip's own modules are *not* inserts: an EQ here must not
    // show up in the track's insert chain.
    _i64Record(b, 'hasAudioStrips', 1);
    _name(b, 'StripFolder');
    b.add([0x00, 0x02, 0x00, 0x06]);
    _u32(b, 1);
    _str8Record(b, 'Plugin Name', 'EQ');
  }
  if (instrument != null) {
    // `Synth Slot { RuntimeID, Plugin { Plugin Name } }` — the hosted VSTi.
    _name(b, 'Synth Slot');
    b.add([0x00, 0x02, 0x00, 0x06]);
    _u32(b, 2);
    _i64Record(b, 'RuntimeID', 736);
    _name(b, 'Plugin');
    b.add([0x00, 0x02, 0x00, 0x06]);
    _u32(b, 1 + (instrumentComponent != null ? 1 : 0) +
        (instrumentController != null ? 1 : 0));
    _str8Record(b, 'Plugin Name', instrument);
    _stateRecords(b, instrumentComponent, instrumentController);
  }
  return b.toBytes();
}

Uint8List _riffNund(List<(String, String, Uint8List)> archives) {
  final body = BytesBuilder(copy: false);
  for (final (logical, className, payload) in archives) {
    final descriptor = _bytes([
      ..._u32bytes(logical.length),
      ..._ascii(logical),
      ..._u32bytes(className.length),
      ..._ascii(className),
    ]);
    _chunk(body, 'ROOT', descriptor);
    _chunk(body, 'ARCH', payload);
  }
  final inner = body.toBytes();
  final out = BytesBuilder(copy: false);
  out.add(_ascii('RIFF'));
  out.add(_u32bytes(4 + inner.length)); // payload size after this field
  out.add(_ascii('NUND'));
  out.add(inner);
  return out.toBytes();
}

void _chunk(BytesBuilder b, String id, Uint8List data) {
  b.add(_ascii(id));
  b.add(_u32bytes(data.length));
  b.add(data);
}

// record helpers --------------------------------------------------------------

void _name(BytesBuilder b, String s) {
  _u32(b, s.length + 1); // length includes the NUL
  b.add(_ascii(s));
  b.addByte(0);
}

void _i64Record(BytesBuilder b, String n, int v) {
  _name(b, n);
  b.add([0x00, 0x01]);
  _i64(b, v);
}

void _f64Record(BytesBuilder b, String n, double v) {
  _name(b, n);
  b.add([0x00, 0x04]);
  final bd = ByteData(8)..setFloat64(0, v, Endian.big);
  b.add(bd.buffer.asUint8List());
}

void _str8Record(BytesBuilder b, String n, String s) {
  _name(b, n);
  b.add([0x00, 0x08]);
  final text = _ascii(s);
  _u32(b, text.length + 1 + 3); // text + NUL + BOM
  b.add(text);
  b.addByte(0);
  b.add([0xef, 0xbb, 0xbf]);
}

/// A blob record (grammar type 2 / subtype 7): `u32 length | bytes`. Cubase
/// stores a plug-in's state as two of these (`audioComponent`, `editController`).
void _blobRecord(BytesBuilder b, String n, Uint8List data) {
  _name(b, n);
  b.add([0x00, 0x02, 0x00, 0x07]);
  _u32(b, data.length);
  b.add(data);
}

/// Writes the `audioComponent` / `editController` state records of a plug-in
/// object (whichever of [component]/[controller] is non-null).
void _stateRecords(BytesBuilder b, Uint8List? component, Uint8List? controller) {
  if (component != null) _blobRecord(b, 'audioComponent', component);
  if (controller != null) _blobRecord(b, 'editController', controller);
}

// number helpers --------------------------------------------------------------

void _u32(BytesBuilder b, int v) => b.add(_u32bytes(v));

Uint8List _u32bytes(int v) =>
    Uint8List.fromList([(v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff]);

void _i64(BytesBuilder b, int v) => b.add(_i64bytes(v));

Uint8List _i64bytes(int v) {
  final bd = ByteData(8)..setInt64(0, v, Endian.big);
  return bd.buffer.asUint8List();
}

Uint8List _f32be(double v) {
  final bd = ByteData(4)..setFloat32(0, v, Endian.big);
  return bd.buffer.asUint8List();
}

Uint8List _f64be(double v) {
  final bd = ByteData(8)..setFloat64(0, v, Endian.big);
  return bd.buffer.asUint8List();
}

Uint8List _f32le(double v) {
  final bd = ByteData(4)..setFloat32(0, v, Endian.little);
  return bd.buffer.asUint8List();
}

Uint8List _ascii(String s) => Uint8List.fromList(latin1.encode(s));

Uint8List _bytes(List<int> b) => Uint8List.fromList(b);
