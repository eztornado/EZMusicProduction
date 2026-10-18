// Cubase project (.cpr) reader — pure Dart, no Flutter/FFI dependency.
//
// .cpr files are RIFF containers with form type "NUND" (Steinberg). The file is
// a sequence of (ROOT, ARCH) chunk pairs: ROOT is a small descriptor holding
// two length-prefixed names (logical name + archive class name) and ARCH holds
// the actual payload. The payload mixes two encodings, both big-endian:
//
//  1. A recursive named-record grammar (mixer channels, devices, tempo track,
//     track objects): u32 nameLen | name | \0 | u16 type | payload.
//       type 1 = i64, 4 = f64, 8 = string, 2 = container (subtype 6 object,
//       5 array, 7 blob, 2 int64 array, 4 string array, 0x14 class instance,
//       0xc9 instance array).
//  2. Compact fixed-layout blobs for timeline events and MIDI parts.
//
// This reader decodes the container and the record grammar. Extractors built on
// top pull out project metadata, mixer channels (→ tracks), device plugins and
// markers. See docs/design/cubase-import.md for the reverse-engineering notes
// and the coverage matrix.

import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';

/// Logical kinds carried by a [CprValue].
enum CprKind {
  int64,
  double64,
  string,
  object,
  array,
  blob,
  int64Array,
  stringArray,
  instance,
  instanceArray,
}

/// A decoded record value. [data] type depends on [kind]:
///  - int64        → `int`
///  - double64     → `double`
///  - string       → `String`
///  - object       → `List<CprRecord>`
///  - array        → `List<List<CprRecord>>`
///  - blob         → `Uint8List`
///  - int64Array   → `List<int>`
///  - stringArray  → `List<String>`
///  - instance     → `CprInstance`
///  - instanceArray→ `List<CprInstance?>`
class CprValue {
  const CprValue(this.kind, this.data);
  final CprKind kind;
  final Object? data;

  List<CprRecord>? get children =>
      kind == CprKind.object ? data as List<CprRecord> : null;
}

/// A `name = value` record from the grammar.
class CprRecord {
  const CprRecord(this.name, this.value);
  final String name;
  final CprValue value;

  @override
  String toString() => '$name = ${value.kind}';
}

/// A class instance (grammar subtype 0x14).
class CprInstance {
  const CprInstance(this.className, this.uid, this.children);
  final String className;
  final int uid;
  final List<CprRecord> children;
}

/// A ROOT/ARCH pair. [id] is "ROOT"/"ARCH"; [logicalName] and [className] come
/// from the ROOT descriptor; [data] is the ARCH payload (the ROOT descriptor
/// bytes for ROOT entries).
class CprChunk {
  const CprChunk({
    required this.id,
    required this.offset,
    required this.logicalName,
    required this.className,
    required this.data,
  });

  final String id;
  final int offset;
  final String logicalName;
  final String className;
  final Uint8List data;
}

/// Immutable view over a byte buffer with a moveable cursor (big-endian).
class CprBytes {
  CprBytes(this.d, this.p);
  final Uint8List d;
  int p;

  int u16() {
    final v = (d[p] << 8) | d[p + 1];
    p += 2;
    return v;
  }

  int u32() {
    final v = (d[p] << 24) | (d[p + 1] << 16) | (d[p + 2] << 8) | d[p + 3];
    p += 4;
    return v;
  }

  int i64() {
    final hi = u32();
    final lo = u32();
    return (hi << 32) | lo;
  }

  double f64() {
    final bd = ByteData.sublistView(d, p, p + 8);
    p += 8;
    return bd.getFloat64(0, Endian.big);
  }

  Uint8List take(int n) {
    final out = Uint8List.sublistView(d, p, p + n);
    p += n;
    return out;
  }

  /// u32 length-prefixed ASCII name with a trailing NUL (length includes it).
  String name() {
    final len = u32();
    if (len == 0 || len > 64) {
      throw FormatException('bad name length $len @${p - 4}');
    }
    final raw = take(len);
    if (raw[len - 1] != 0) {
      throw FormatException('name not NUL-terminated @${p - len}');
    }
    for (var i = 0; i < len - 1; i++) {
      final c = raw[i];
      if (c < 0x20 || c > 0x7e) {
        throw FormatException('non-printable name byte @${p - len + i}');
      }
    }
    return latin1.decode(Uint8List.sublistView(raw, 0, len - 1));
  }

  /// Grammar string (type 8): u32 len | text | \0 | EF BB BF.
  String string8() => _decodeText(take(u32()));
}

String _decodeText(Uint8List raw) {
  var end = raw.length;
  if (end >= 3 &&
      raw[end - 3] == 0xef &&
      raw[end - 2] == 0xbb &&
      raw[end - 1] == 0xbf) {
    end -= 3;
  }
  while (end > 0 && raw[end - 1] == 0) {
    end--;
  }
  return _repairMojibake(utf8.decode(raw.sublist(0, end), allowMalformed: true));
}

/// Cubase sometimes stores a UTF-8 string re-encoded as Latin-1 (e.g. the
/// Spanish "Sin título" appears as `c3 8c c2 81`). When a decoded string is
/// entirely in the Latin-1 range and re-encoding it as bytes yields different
/// *valid* UTF-8, prefer the repaired spelling.
String _repairMojibake(String s) {
  if (s.isEmpty) return s;
  final bytes = Uint8List(s.length);
  for (var i = 0; i < s.length; i++) {
    final c = s.codeUnitAt(i);
    if (c > 0xff) return s; // already real Unicode; leave it alone
    bytes[i] = c;
  }
  try {
    final repaired = utf8.decode(bytes);
    if (repaired != s && !repaired.contains('\uFFFD')) return repaired;
  } on FormatException {
    // not valid UTF-8 → the Latin-1 reading was correct
  }
  return s;
}

/// Parses the named-record grammar starting at a byte offset.
class CprRecordParser {
  CprRecordParser(this.b);

  final CprBytes b;

  /// Parses one record; throws [FormatException] on a malformed stream.
  CprRecord record() {
    final name = b.name();
    final type = b.u16();
    switch (type) {
      case 1:
        return CprRecord(name, CprValue(CprKind.int64, b.i64()));
      case 4:
        return CprRecord(name, CprValue(CprKind.double64, b.f64()));
      case 8:
        return CprRecord(name, CprValue(CprKind.string, b.string8()));
      case 2:
        return CprRecord(name, _container());
      default:
        throw FormatException('unknown record type $type for $name @${b.p - 2}');
    }
  }

  CprValue _container() {
    final sub = b.u16();
    switch (sub) {
      case 6: // object
        final n = b.u32();
        return CprValue(CprKind.object, [for (var i = 0; i < n; i++) record()]);
      case 5: // array: n items, each preceded by its record count
        final n = b.u32();
        final items = <List<CprRecord>>[];
        for (var i = 0; i < n; i++) {
          final per = b.u32();
          items.add([for (var j = 0; j < per; j++) record()]);
        }
        return CprValue(CprKind.array, items);
      case 7: // blob
        final n = b.u32();
        return CprValue(CprKind.blob, b.take(n));
      case 2: // int64 array
        final n = b.u32();
        return CprValue(CprKind.int64Array, [for (var i = 0; i < n; i++) b.i64()]);
      case 4: // string array
        final n = b.u32();
        return CprValue(CprKind.stringArray,
            [for (var i = 0; i < n; i++) _decodeText(b.take(b.u32()))]);
      case 0x14: // class instance
        return CprValue(CprKind.instance, _instanceBody());
      case 0xc9: // instance array
        final n = b.u32();
        final items = <CprInstance?>[];
        for (var i = 0; i < n; i++) {
          final tag = b.u16();
          if (tag == 0x14) {
            items.add(_instanceBody());
          } else if (tag == 0) {
            items.add(null);
          } else {
            throw FormatException('bad instance-array tag $tag @${b.p - 2}');
          }
        }
        return CprValue(CprKind.instanceArray, items);
      default:
        throw FormatException('unknown container subtype $sub @${b.p - 2}');
    }
  }

  CprInstance _instanceBody() {
    final len = b.u32();
    if (len == 0 || len > 64) throw FormatException('bad class length $len');
    final raw = b.take(len);
    if (raw[len - 1] != 0) throw FormatException('class not NUL-terminated');
    final cls = latin1.decode(Uint8List.sublistView(raw, 0, len - 1));
    b.u32(); // unknown flag
    final uid = b.u32();
    final n = b.u32();
    return CprInstance(cls, uid, [for (var i = 0; i < n; i++) record()]);
  }
}

/// An arrangement track-list node (see [CprReader.extractTrackNodes]).
class CprTrackNode {
  const CprTrackNode({
    required this.name,
    required this.eventCount,
    required this.offset,
  });
  final String name;
  final int eventCount;
  final int offset;
}

/// A Cubase mixer channel mapped to a Cadence track.
class CubaseTrack {
  CubaseTrack({
    required this.name,
    required this.typeCode,
    required this.channelId,
    required this.volumeDb,
  });

  String name;
  int typeCode;
  String channelId;
  double volumeDb;
  double pan = 0.0;
  bool mute = false;
  bool solo = false;
  bool isInstrument = false;
  String? instrumentName;

  /// Serialized state of the hosted VSTi (null when the track hosts none, or
  /// the file carries no state for it).
  CubasePluginState? instrumentState;

  /// Insert-slot plugins of the channel, in slot order (Cubase stores them in
  /// the channel's `InsertFolder`). The channel strip's own modules — EQ,
  /// compressor — live in `StripFolder` and are *not* inserts, so they are not
  /// listed here.
  final List<String> inserts = [];

  /// Serialized state of each insert, aligned with [inserts]; a slot is null
  /// when Cubase stored no state for it.
  final List<CubasePluginState?> insertStates = [];

  /// Linear gain from the channel fader (0 dB → 1.0).
  double get volumeLinear => volumeDb <= -95.0 ? 0.0 : math.pow(10, volumeDb / 20).toDouble();
}

/// A plugin referenced by the project (device or insert chain).
class CubasePlugin {
  CubasePlugin({required this.name, required this.uid});
  final String name;
  final String uid;
  String? decodedUid;

  /// VST3 class ids are hex-encoded ASCII (e.g. "5653544E694B37" → "VSTNiK7").
  String? get decodedVstName {
    if (uid.length.isEven && RegExp(r'^[0-9A-Fa-f]+$').hasMatch(uid)) {
      try {
        final b = <int>[];
        for (var i = 0; i + 1 < uid.length; i += 2) {
          b.add(int.parse(uid.substring(i, i + 2), radix: 16));
        }
        final s = latin1.decode(b);
        return RegExp(r'^[\x20-\x7e]+$').hasMatch(s) ? s : null;
      } catch (_) {
        return null;
      }
    }
    return null;
  }

  @override
  String toString() => '$name ($uid)';
}

/// A plug-in's serialized state, exactly as Cubase stored it.
///
/// Cubase keeps a VST3 plug-in's two sub-states apart: [component] holds the
/// bytes the plug-in's `IComponent::getStateInformation` returned and
/// [controller] those of `IEditController::getStateInformation` (empty for most
/// plug-ins). Cubase does not reinterpret them — they are the plug-in's own
/// format (XML, zlib, a preset chunk, …) — so importing hands them straight
/// back to the plug-in (see docs/design/cubase-import.md).
class CubasePluginState {
  const CubasePluginState({required this.component, required this.controller});

  final Uint8List component;
  final Uint8List controller;

  bool get isEmpty => component.isEmpty && controller.isEmpty;
  int get size => component.length + controller.length;
}

class CubaseMarker {
  CubaseMarker({required this.name, required this.startSeconds});
  final String name;
  final double startSeconds;
}

/// A single MIDI note decoded from a Cubase part.
class CubaseMidiNote {
  const CubaseMidiNote({
    required this.startTicks,
    required this.lengthTicks,
    required this.pitch,
    required this.velocity,
  });

  /// Absolute start in ticks at the Cubase timebase (480 ticks per beat).
  final double startTicks;

  /// Length in ticks at the same timebase.
  final double lengthTicks;

  /// MIDI note number (0..127).
  final int pitch;

  /// MIDI velocity (0..127).
  final int velocity;
}

/// A contiguous block of MIDI notes (a Cubase part) on one track.
class CubaseMidiPart {
  CubaseMidiPart({required this.name, required this.offset});

  /// Owning track name, spelled as in the mixer (empty when unresolved).
  String name;

  /// Byte offset of the first note record inside the arrangement payload.
  final int offset;
  final List<CubaseMidiNote> notes = [];
  double startTicks = 0;
  double endTicks = 0;

  /// Byte offset of the owning track node (or -1 when unknown). Parts that
  /// share an owner belong to the same track.
  int ownerOffset = -1;

  double get lengthTicks => endTicks > startTicks ? endTicks - startTicks : 0;
}

/// A single audio event placed on the Cubase timeline.
///
/// Cubase stores an audio event with the same object layout as a MIDI part
/// (`ref | u32 payloadLen | u16 0 | f64 pos | f64 len | f64 sourceOffset`) but
/// without note records: [startTicks] is in 480 PPQ ticks while [lengthFrames]
/// and [sourceOffsetFrames] count source sample frames at [CubaseProject.sampleRate].
class CubaseAudioEvent {
  CubaseAudioEvent({required this.name, required this.offset});

  /// Owning track name, spelled as in the mixer (empty when unresolved).
  String name;

  /// Clip display name as serialized with the event.
  String clipName = '';

  /// Source file name resolved from the media pool (e.g. `Kick.wav`), if any.
  /// Only the file name is stored: the local path is resolved next to the .cpr.
  String? sourceFile;

  /// Byte offset of the event object inside the arrangement payload.
  final int offset;

  /// Absolute position on the timeline, in ticks (480 PPQ).
  double startTicks = 0;

  /// Length in source sample frames.
  double lengthFrames = 0;

  /// Offset into the source file, in sample frames.
  double sourceOffsetFrames = 0;

  /// Linear event gain.
  double gain = 1.0;

  /// Byte offset of the owning track node (or -1 when unknown). Events that
  /// share an owner belong to the same track.
  int ownerOffset = -1;
}

/// The parsed project document (extraction result).
class CubaseProject {
  CubaseProject({required this.projectName, this.appVersion = ''});

  final String projectName;
  final String appVersion;
  double tempoBpm = 120.0;
  int timeSigNumerator = 4;
  int timeSigDenominator = 4;
  double sampleRate = 48000.0;

  /// Fader of the project's main output channel (Cubase's "Stereo Out"), in
  /// dB. 0 (= unity) when the file carries no output channel.
  double masterVolumeDb = 0.0;
  /// [masterVolumeDb] as a linear gain, matching [CubaseTrack.volumeLinear]
  /// (faders at or below -95 dB are silent).
  double get masterVolumeLinear => masterVolumeDb <= -95.0
      ? 0.0
      : math.pow(10, masterVolumeDb / 20).toDouble();

  /// Insert-slot plugins of the output channel ("Stereo Out"), in slot order
  /// (empty when the file carries no output channel). Cubase calls the summing
  /// chain "Stereo Out inserts".
  final List<String> masterInserts = [];

  /// Serialized state of each master insert, aligned with [masterInserts].
  final List<CubasePluginState?> masterInsertStates = [];
  final List<CubaseTrack> tracks = [];
  final List<CubaseMidiPart> midiParts = [];
  final List<CubaseAudioEvent> audioEvents = [];
  final List<CubasePlugin> plugins = [];
  final List<CubaseMarker> markers = [];
}

/// Reads a `.cpr` byte buffer and extracts everything the reader understands.
class CprReader {
  CprReader(this.bytes, {this.projectName = 'Cubase Project'});

  final Uint8List bytes;
  final String projectName;
  final List<CprChunk> chunks = [];
  Uint8List _arrangement = Uint8List(0);
  String? _arrangementText;
  Set<String>? _mediaPoolCache;

  /// The arrangement archive payload (holds tracks, mixer, tempo, events).
  Uint8List get arrangement => _arrangement;

  String get _text => _arrangementText ??= latin1.decode(_arrangement);

  /// Parses the RIFF/NUND container into [chunks]. Idempotent.
  void parseContainer() {
    if (chunks.isNotEmpty) return;
    if (bytes.length < 12 ||
        latin1.decode(Uint8List.sublistView(bytes, 0, 4)) != 'RIFF' ||
        latin1.decode(Uint8List.sublistView(bytes, 8, 12)) != 'NUND') {
      throw const FormatException('Not a Cubase project (expected RIFF/NUND)');
    }
    var pos = 12;
    CprChunk? pendingRoot;
    while (pos + 8 <= bytes.length) {
      final id = latin1.decode(Uint8List.sublistView(bytes, pos, pos + 4));
      final len = (bytes[pos + 4] << 24) |
          (bytes[pos + 5] << 16) |
          (bytes[pos + 6] << 8) |
          bytes[pos + 7];
      final dataStart = pos + 8;
      final dataEnd = dataStart + len;
      if (dataEnd > bytes.length) break;
      final data = Uint8List.sublistView(bytes, dataStart, dataEnd);
      if (id == 'ROOT') {
        final names = _descriptorNames(data);
        pendingRoot = CprChunk(
          id: id,
          offset: pos,
          logicalName: names.$1,
          className: names.$2,
          data: data,
        );
        chunks.add(pendingRoot);
      } else if (id == 'ARCH' && pendingRoot != null) {
        chunks.add(CprChunk(
          id: id,
          offset: pos,
          logicalName: pendingRoot.logicalName,
          className: pendingRoot.className,
          data: data,
        ));
        pendingRoot = null;
      }
      pos = dataEnd;
    }
    for (final c in chunks) {
      if (c.className.contains('PArrangement')) _arrangement = c.data;
    }
  }

  // ROOT descriptor: u32 len | name[len] (repeated; no NUL, len = byte count).
  (String, String) _descriptorNames(Uint8List data) {
    var p = 0;
    String readName() {
      if (p + 4 > data.length) return '';
      final len =
          (data[p] << 24) | (data[p + 1] << 16) | (data[p + 2] << 8) | data[p + 3];
      p += 4;
      if (len == 0 || p + len > data.length) return '';
      final s = latin1.decode(Uint8List.sublistView(data, p, p + len));
      p += len;
      return s;
    }

    final a = readName();
    final b = readName();
    return (a, b);
  }

  /// Parses the app-version archive (PAppVersion) for the Cubase version string.
  String appVersion() {
    for (final c in chunks) {
      if (c.className.contains('PAppVersion')) {
        final m = RegExp(r'Version [0-9][0-9.]*').firstMatch(latin1.decode(c.data));
        if (m != null) return m.group(0)!;
      }
    }
    return '';
  }

  /// Extracts tracks from the mixer channels in the arrangement archive.
  List<CubaseTrack> extractTracks() {
    final data = arrangement;
    final byId = <String, CubaseTrack>{};
    final order = <String>[];
    final offsets = _findAll(_channelAnchor).toList();
    String? lastName;
    for (var i = 0; i < offsets.length; i++) {
      final off = offsets[i];
      final next = i + 1 < offsets.length ? offsets[i + 1] : data.length;
      final parser = CprRecordParser(CprBytes(data, off));
      final fields = <String, CprValue>{};
      while (parser.b.p < data.length) {
        final save = parser.b.p;
        CprRecord rec;
        try {
          rec = parser.record();
        } catch (_) {
          parser.b.p = save;
          break;
        }
        fields.putIfAbsent(rec.name, () => rec.value);
      }
      final rawName = (_stringOf(fields['Name']) ?? '').trim();
      if (rawName.isEmpty) continue;
      final norm = _normalizeName(rawName);
      final channelId = _stringOf(fields['IDString']) ?? '';
      final typeCode = _intOf(fields['Type']) ?? 0;
      final track = CubaseTrack(
        name: rawName,
        typeCode: typeCode,
        channelId: channelId,
        volumeDb: _faderDb(fields['Volume']),
      );
      track.isInstrument = typeCode == 4;
      track.pan = _pannerPan(fields['Panner']);
      // Instrument tracks name the VSTi they host in a `Synth Slot` object
      // that follows the channel's own fields.
      if (track.isInstrument) {
        track.instrumentName = _channelInstrument(off, next);
        track.instrumentState = _channelInstrumentState(off, next);
      }
      for (final insert in _insertPlugins(data, _text, off, next)) {
        track.inserts.add(insert.name);
        track.insertStates.add(insert.state);
      }
      // Cubase 12+ stores the panner as a plugin object; not decoded yet.
      // A channel is serialized twice (with and without IDString): drop the
      // redundant copy, which always follows an identically named channel.
      if (channelId.isEmpty && lastName == norm) continue;
      lastName = norm;
      final key = channelId.isNotEmpty ? channelId : 'name:$norm:${order.length}';
      if (byId.containsKey(key)) {
        final existing = byId[key]!;
        if (!existing.isInstrument && track.isInstrument) byId[key] = track;
        continue;
      }
      byId[key] = track;
      order.add(key);
    }
    return [for (final k in order) byId[k]!];
  }

  /// Name of the VSTi an instrument channel hosts, or null when it has none.
  ///
  /// Cubase serialises it as `Synth Slot → Plugin → Plugin Name`. The plugin
  /// object around it uses an encoding this reader does not parse, so the name
  /// record is located by its byte pattern inside the channel's own range
  /// ([from], [until] = the next channel's offset). The channel name often
  /// already is the instrument's name ("ZENOLOGY (VST3)"); it is not when the
  /// user renamed the track ("Bassoon" hosts Kontakt 7).
  String? _channelInstrument(int from, int until) {
    final slot = _text.indexOf(_synthSlotAnchor, from);
    if (slot < 0 || slot >= until) return null;
    final i = _text.indexOf(_pluginNameRecord, slot + _synthSlotAnchor.length);
    if (i < 0 || i >= until) return null;
    return _pluginNameString(arrangement, i);
  }

  /// Serialized state of the VSTi an instrument channel hosts (the same
  /// `Synth Slot → Plugin` object [_channelInstrument] names), or null.
  CubasePluginState? _channelInstrumentState(int from, int until) {
    final slot = _text.indexOf(_synthSlotAnchor, from);
    if (slot < 0 || slot >= until) return null;
    final i = _text.indexOf(_pluginNameRecord, slot + _synthSlotAnchor.length);
    if (i < 0 || i >= until) return null;
    return _pluginState(arrangement, _text, i, until);
  }

  /// Insert-slot plugins between [from] and [until], in byte order (Cubase
  /// serialises the slots in order), each with its serialized state. The
  /// channel's `InsertFolder` holds an occupied `Slot` per insert, each with
  /// its `Plugin` object; the channel strip's own modules (EQ, compressor)
  /// belong to `StripFolder` and must not be reported as inserts, hence the
  /// range's closing bound.
  static List<({String name, CubasePluginState? state})> _insertPlugins(
      Uint8List d, String text, int from, int until) {
    final start = text.indexOf(_insertFolderAnchor, from);
    if (start < 0 || start >= until) return const [];
    var end = text.indexOf(_hasAudioStripsAnchor, start);
    final summing = text.indexOf(_summingModeAnchor, start);
    if (summing >= 0 && (end < 0 || summing < end)) end = summing;
    if (end < 0 || end > until) end = until;
    final out = <({String name, CubasePluginState? state})>[];
    for (var at = text.indexOf(_pluginNameRecord, start);
        at >= 0 && at < end;
        at = text.indexOf(_pluginNameRecord, at + _pluginNameRecord.length)) {
      final name = _pluginNameString(d, at);
      if (name == null) continue;
      // One `Plugin` object per slot: its state lies before the next slot's
      // name (or the folder's closing field).
      final nextAt =
          text.indexOf(_pluginNameRecord, at + _pluginNameRecord.length);
      final bound = (nextAt < 0 || nextAt > end) ? end : nextAt;
      out.add((name: name, state: _pluginState(d, text, at, bound)));
    }
    return out;
  }

  /// Serialized state of the `Plugin` whose `Plugin Name` record sits at [i],
  /// read from the `audioComponent` (component) and `editController`
  /// (controller) blob records that follow it, within [until].
  static CubasePluginState? _pluginState(
      Uint8List d, String text, int i, int until) {
    var component = Uint8List(0);
    var controller = Uint8List(0);
    final c = text.indexOf(_audioComponentAnchor, i);
    if (c >= 0 && c < until) {
      component = _blobAfter(d, c + _audioComponentAnchor.length) ?? component;
    }
    final e = text.indexOf(_editControllerAnchor, i);
    if (e >= 0 && e < until) {
      controller = _blobAfter(d, e + _editControllerAnchor.length) ?? controller;
    }
    if (component.isEmpty && controller.isEmpty) return null;
    return CubasePluginState(component: component, controller: controller);
  }

  /// Bytes of a blob record whose `u32 length` prefix starts at [at], or null
  /// when the length runs past the buffer.
  static Uint8List? _blobAfter(Uint8List d, int at) {
    if (at + 4 > d.length) return null;
    final len =
        (d[at] << 24) | (d[at + 1] << 16) | (d[at + 2] << 8) | d[at + 3];
    if (len < 0 || at + 4 + len > d.length) return null;
    return Uint8List.sublistView(d, at + 4, at + 4 + len);
  }

  /// Text of the string that follows the `Plugin Name` record header at [i], or
  /// null when the record is malformed (see [_pluginNameRecord]).
  static String? _pluginNameString(Uint8List d, int i) {
    final q = i + _pluginNameRecord.length; // the string8 length prefix
    if (q + 4 > d.length || d[q] != 0) return null;
    final len = (d[q + 1] << 16) | (d[q + 2] << 8) | d[q + 3];
    if (len < 4 || q + 4 + len > d.length) return null;
    var end = q + 4 + len;
    if (d[end - 4] == 0 && d[end - 3] == 0xef && d[end - 2] == 0xbb) {
      end -= 4; // trailing NUL + UTF-8 BOM
    } else if (d[end - 1] == 0) {
      end -= 1;
    } else {
      return null;
    }
    if (end <= q + 4) return null;
    final name = _decodeText(Uint8List.sublistView(d, q + 4, end)).trim();
    return name.isEmpty ? null : name;
  }

  /// Fader of the main output channel, in dB, or null when the file has none.
  ///
  /// The output/input channels live in the VST Mixer archive, not in the
  /// arrangement one, so every archive is searched. Only the master fader is
  /// read; channel/track faders come from [extractTracks].
  double? detectMasterVolumeDb() {
    final out = _outputChannel();
    if (out == null) return null;
    final volume = out.fields['Volume'];
    if (volume == null || volume.kind != CprKind.object) return null;
    return _doubleOf(_find(volume.data as List<CprRecord>, 'AnchorValue'));
  }

  /// Insert-slot plugins of the output channel ("Stereo Out"), in slot order,
  /// each with its serialized state (empty when the file carries no output
  /// channel, or it has no inserts).
  List<({String name, CubasePluginState? state})> detectMasterPlugins() {
    final out = _outputChannel();
    if (out == null) return const [];
    return _insertPlugins(out.data, out.text, out.off, out.until);
  }

  /// Insert-slot plugin names of the output channel, in slot order.
  List<String> detectMasterInserts() =>
      [for (final p in detectMasterPlugins()) p.name];

  /// Finds the mixer's output channel (`Stereo Out`): Cubase keeps it out of the
  /// arrangement archive, in the VST Mixer one. Returns the archive it was found
  /// in, the channel's range within it and its parsed top-level fields.
  ({Uint8List data, String text, int off, int until,
      Map<String, CprValue> fields})? _outputChannel() {
    for (final chunk in chunks) {
      final text = latin1.decode(chunk.data);
      var from = 0;
      while (true) {
        final off = text.indexOf(_channelAnchor, from);
        if (off < 0) break;
        from = off + 1;
        final parser = CprRecordParser(CprBytes(chunk.data, off));
        final fields = <String, CprValue>{};
        while (parser.b.p < chunk.data.length) {
          final save = parser.b.p;
          CprRecord rec;
          try {
            rec = parser.record();
          } catch (_) {
            parser.b.p = save;
            break;
          }
          fields.putIfAbsent(rec.name, () => rec.value);
        }
        final typeCode = _intOf(fields['Type']) ?? 0;
        final channelId = _stringOf(fields['IDString']) ?? '';
        if (typeCode != _outputChannelType && channelId != 'OutputChannel') {
          continue;
        }
        final next = text.indexOf(_channelAnchor, off + 1);
        return (
          data: chunk.data,
          text: text,
          off: off,
          until: next < 0 ? chunk.data.length : next,
          fields: fields,
        );
      }
    }
    return null;
  }

  /// Arrangement track-list nodes: `[len][name][\0][EF BB BF][u32 0][MAGIC]
  /// [u32 declaredEventCount]`. Each track contributes a pair of nodes (the
  /// instrument/track node with count 1 and the event container). Used as a
  /// cross-check on the mixer-derived track list and to size future MIDI work.
  List<CprTrackNode> extractTrackNodes() {
    final data = arrangement;
    final out = <CprTrackNode>[];
    for (final off in _findAll(_trackNodeMagic)) {
      final s = off - 4;
      if (s < 4) continue;
      if (latin1.decode(Uint8List.sublistView(data, s, s + 4)) !=
          '\x00\x00\x00\x00') {
        continue;
      }
      if (latin1.decode(Uint8List.sublistView(data, s - 3, s)) !=
          '\xef\xbb\xbf') {
        continue;
      }
      final name = (_bomStringBefore(data, s - 4) ?? '').trim();
      final cntOff = off + _trackNodeMagic.length;
      if (cntOff + 4 > data.length) continue;
      final count = ByteData.sublistView(data, cntOff, cntOff + 4)
          .getUint32(0, Endian.big);
      out.add(CprTrackNode(
          name: name, eventCount: count, offset: off));
    }
    return out;
  }

  /// Reads a BOM-terminated string ending at [end] (length stored 4 bytes
  /// before its start; the stored length includes the `\0` and the BOM).
  String? _bomStringBefore(Uint8List data, int end) {
    for (var length = 1; length < 90; length++) {
      final start = end - length;
      if (start - 4 < 0) break;
      final stored =
          ByteData.sublistView(data, start - 4, start).getUint32(0, Endian.big);
      if (stored == length + 4) {
        return utf8.decode(
            Uint8List.sublistView(data, start, end),
            allowMalformed: true);
      }
    }
    return null;
  }

  /// Pan from the Panner plugin's `audioComponent` blob: the first two
  /// little-endian float32 are the position (0.0 = hard L, 0.5 = centre,
  /// 1.0 = hard R). Cubase 12+ stores this in place of a named Pan record.
  double _pannerPan(CprValue? panner) {
    final kids = panner?.children;
    if (kids == null) return 0.0;
    final blob = _find(kids, 'audioComponent');
    if (blob == null || blob.kind != CprKind.blob) return 0.0;
    final u = blob.data as Uint8List;
    if (u.length < 8) return 0.0;
    final pos = ByteData.sublistView(u, 0, 4).getFloat32(0, Endian.little);
    return (pos * 2.0 - 1.0).clamp(-1.0, 1.0);
  }

  double _faderDb(CprValue? volume) {
    final obj = volume?.children;
    if (obj == null) return 0.0;
    final anchor = _doubleOf(_find(obj, 'AnchorValue'));
    if (anchor != null) return anchor;
    final raw = _doubleOf(_find(obj, 'Value'));
    if (raw != null && raw > 0) return 20.0 * (math.log(raw / 25856.0) / math.ln10);
    return 0.0;
  }

  /// Extracts device plugins (Plugin UID + Plugin Name pairs).
  List<CubasePlugin> extractPlugins() {
    final data = arrangement;
    final seen = <String>{};
    final out = <CubasePlugin>[];
    for (final off in _findAll(_pluginHeader)) {
      CprRecord rec;
      try {
        rec = CprRecordParser(CprBytes(data, off)).record();
      } catch (_) {
        continue;
      }
      final kids = rec.value.children;
      if (kids == null) continue;
      final name = _stringOf(_find(kids, 'Plugin Name'));
      final uidObj = _find(kids, 'Plugin UID')?.children;
      final guid = uidObj == null ? null : _stringOf(_find(uidObj, 'GUID'));
      if (name == null || name.isEmpty) continue;
      if (!seen.add('$name|${guid ?? ''}')) continue;
      out.add(CubasePlugin(name: name, uid: guid ?? ''));
    }
    return out;
  }

  /// Extracts markers (MMarkerTrackEvent objects) — best effort.
  List<CubaseMarker> extractMarkers() {
    final out = <CubaseMarker>[];
    for (final off in _findAll(_markerMagic)) {
      final end = _min(off + 400, _arrangement.length);
      final window = latin1.decode(
          Uint8List.sublistView(_arrangement, off + _markerMagic.length, end));
      for (final m in RegExp(r'[A-Za-z0-9][A-Za-z0-9 _\-\.]{1,40}').allMatches(window)) {
        final s = m.group(0)!;
        if (_looksLikeMarker(s)) {
          out.add(CubaseMarker(name: s, startSeconds: 0.0));
          break;
        }
      }
    }
    return out;
  }

  bool _looksLikeMarker(String s) =>
      !s.startsWith('M') &&
      !s.contains('Event') &&
      s.length >= 2 &&
      RegExp(r'[a-z]').hasMatch(s);

  /// Best-effort tempo detection: seconds-per-beat f32 near the tempo track.
  double? detectTempo() {
    final data = arrangement;
    for (final magic in [_tempoMagic, 'MTempoEvent', 'MTempoTrack']) {
      for (final off in _findAll(magic)) {
        final start = off + magic.length;
        final end = _min(start + 256, data.length);
        for (var i = start; i + 4 <= end; i++) {
          final v = ByteData.sublistView(data, i, i + 4).getFloat32(0, Endian.big);
          if (v.isFinite && v > 0.2 && v < 1.5) {
            final bpm = 60.0 / v;
            if (bpm >= 30 && bpm <= 300) return double.parse(bpm.toStringAsFixed(3));
          }
        }
      }
    }
    return null;
  }

  /// Time signature from the MTimeSignatureEvent record (Numerator/Denominator).
  (int, int)? detectTimeSignature() {
    final num = _firstI64After(_numeratorMagic);
    final den = _firstI64After(_denominatorMagic);
    if (num != null && den != null && num > 0 && den > 0) return (num, den);
    return null;
  }

  int? _firstI64After(String magic) {
    final data = arrangement;
    for (final off in _findAll(magic)) {
      final n = CprBytes(data, off + magic.length);
      if (n.p + 8 > data.length) continue;
      return n.i64();
    }
    return null;
  }

  /// Decodes MIDI notes grouped into parts.
  ///
  /// A note record is a `0x90` type byte followed by an `f64` position (480 PPQ
  /// ticks), the pitch/velocity bytes at +10/+11 and an `f64` length after the
  /// five property tags (+88). Cubase stores that position **relative to the
  /// part's content origin** (`third`), not to the song; the note's absolute
  /// tick is `part.pos + (rel - third)`, and only notes with
  /// `third <= rel < third + part.len` are shown by the part.
  ///
  /// Every part instance is serialized as an object
  /// `ref(4, high bit set) | u32 payloadLen | u16 0 | f64 pos | f64 len |
  /// f64 third` whose payload holds that part's note records (Cubase writes the
  /// whole content container, so hidden notes may also be present). The very
  /// first part of a file is instead written inline behind its class
  /// definition (`\xff\xff\xff\xff u32 nameLen name \0 \x00\x02 ...`).
  ///
  /// Track/arrangement nodes share the layout but span the whole song; they
  /// duplicate their children's notes, so they are detected (a candidate that
  /// contains another candidate and reproduces its note set) and dropped.
  /// Verified 1:1 against Cubase-exported Standard MIDI Files (7.cpr: 1674/1674
  /// notes; 9.cpr: 839/842 — the remainder being tick-rounding ties).
  List<CubaseMidiPart> extractMidiParts(List<CubaseTrack> tracks) {
    final data = arrangement;
    if (data.length < 128) return const [];
    final names = <String>[];
    for (final t in tracks) {
      final n = t.name.trim();
      if (n.isNotEmpty && !names.contains(n)) names.add(n);
    }

    final notes = _noteRecords();
    if (notes.isEmpty) return const [];

    final objects = _partObjects(data);
    if (objects.isEmpty) return _legacyMidiParts(notes, names);
    _attachNotes(objects, notes);
    _markContainers(objects);

    final parts = <CubaseMidiPart>[];
    for (final o in objects) {
      if (o.container || o.notes.isEmpty) continue;
      _PartObject? owner;
      for (final c in objects) {
        if (!c.container) continue;
        if (c.start <= o.start && o.end <= c.end) {
          if (owner == null || (c.end - c.start) < (owner.end - owner.start)) {
            owner = c;
          }
        }
      }
      final part = CubaseMidiPart(
        name: _resolvePartName(data, o, owner, names),
        offset: o.start,
      );
      part.ownerOffset = owner?.start ?? -1;
      for (final r in o.notes) {
        part.notes.add(CubaseMidiNote(
          startTicks: o.pos + r.rel - o.third,
          lengthTicks: r.length,
          pitch: r.pitch,
          velocity: r.velocity,
        ));
      }
      part.notes.sort((a, b) => a.startTicks.compareTo(b.startTicks));
      part.startTicks = o.pos;
      part.endTicks = o.pos + o.len;
      parts.add(part);
    }
    if (parts.isNotEmpty) return parts;
    return _legacyMidiParts(notes, names);
  }

  /// All note records in the arrangement, with the position exactly as stored
  /// (relative to the owning part's content container).
  List<_NoteRecord> _noteRecords() {
    final data = arrangement;
    final out = <_NoteRecord>[];
    for (final i in _findAll(_glfxAnchor)) {
      final s = i - 14;
      if (s < 0 || s + 96 > data.length || data[s] != 0x90) continue;
      final pitch = data[s + 10];
      final velocity = data[s + 11];
      if (pitch > 127 || velocity > 127) continue;
      final rel = _f64At(s + 1);
      final length = _f64At(s + 88);
      if (!rel.isFinite || rel < 0 || rel > _maxTicks) continue;
      if (!length.isFinite || length < 0 || length > _maxTicks) continue;
      out.add(_NoteRecord(s, rel, length, pitch, velocity));
    }
    return out;
  }

  /// Part objects (and the inline first part) in file order.
  List<_PartObject> _partObjects(Uint8List data) {
    final n = data.length;
    final out = <_PartObject>[];
    for (var p = 0; p + 34 <= n; p++) {
      if (data[p] != 0x80) continue;
      if (data[p + 8] != 0 || data[p + 9] != 0) continue;
      final payload = _u32At(p + 4);
      if (payload == 0) continue;
      final end = p + 8 + payload;
      if (end > n) continue;
      final pos = _f64At(p + 10);
      final len = _f64At(p + 18);
      final third = _f64At(p + 26);
      if (!_plausible(pos, len, third)) continue;
      out.add(_PartObject(
          start: p, end: end, pos: pos, len: len, third: third, inline: false));
    }
    for (final q in _findAll(_inlineMidiPartMagic)) {
      final c = q + _inlineMidiPartMagic.length - 4;
      if (c < 0 || c + 34 > n) continue;
      final pos = _f64At(c + 10);
      final len = _f64At(c + 18);
      final third = _f64At(c + 26);
      if (!_plausible(pos, len, third)) continue;
      out.add(_PartObject(
          start: c, end: n, pos: pos, len: len, third: third, inline: true));
    }
    out.sort((a, b) => a.start.compareTo(b.start));
    // An inline part has no payload length; it runs up to the next object.
    for (var i = 0; i < out.length; i++) {
      if (!out[i].inline) continue;
      for (var j = i + 1; j < out.length; j++) {
        if (out[j].start > out[i].start) {
          out[i].end = out[j].start;
          break;
        }
      }
    }
    return out;
  }

  /// Resolves the owning track name for a part. Cubase writes the track name as
  /// a name record (`u32 len | text | \0 | EF BB BF`) at the head of the track
  /// node object, ahead of the part class registry; parts without a containing
  /// node carry their own name just before their notes.
  String _resolvePartName(
    Uint8List data,
    _PartObject part,
    _PartObject? owner,
    List<String> names,
  ) {
    if (owner != null) {
      final hit = _firstNameIn(data, owner.start + 34);
      if (hit != null) return hit;
      final viaText = _partNameFor(owner.start, names);
      if (viaText.isNotEmpty) return viaText;
    }
    final viaText = _partNameFor(part.start, names);
    if (viaText.isNotEmpty) return viaText;
    return _firstNameIn(data, part.start + 34) ?? '';
  }

  /// First name record (`u32 len | text | \0 | [EF BB BF]`) after [from],
  /// skipping serialization class names such as "MMidiPartEvent".
  String? _firstNameIn(Uint8List data, int from, [int? to]) =>
      _nameRecordIn(data, from, to)?.$1;

  /// Like [_firstNameIn] but also reports the offset just past the record, so
  /// callers can read the `u32 0 | f32 gain` that follows an audio event name.
  (String, int)? _nameRecordIn(Uint8List data, int from, [int? to]) {
    final stop = math.min(to ?? from + 1024, data.length);
    for (var q = from; q + 8 <= stop; q++) {
      final l = _u32At(q);
      if (l < 2 || l > 200 || q + 4 + l > stop) continue;
      final end = q + 4 + l;
      final nulTail = data[end - 1] == 0;
      final bomTail = l >= 4 &&
          data[end - 4] == 0 &&
          data[end - 3] == 0xef &&
          data[end - 2] == 0xbb &&
          data[end - 1] == 0xbf;
      if (!nulTail && !bomTail) continue;
      final textEnd = bomTail ? end - 4 : end - 1;
      if (textEnd <= q + 4) continue;
      var printable = true;
      for (var k = q + 4; k < textEnd; k++) {
        final c = data[k];
        if (c < 0x20 || c == 0x7f) {
          printable = false;
          break;
        }
      }
      if (!printable) continue;
      final name = _decodeText(Uint8List.sublistView(data, q + 4, end)).trim();
      if (name.isEmpty || _looksLikeClassName(name)) continue;
      return (name, end);
    }
    return null;
  }

  /// Serialization class names ("MMidiPartEvent", "MPartNode", …) are not track
  /// names: a capital "M" prefix with an inner capital and no space.
  static bool _looksLikeClassName(String s) =>
      s.length > 2 &&
      s.startsWith('M') &&
      !s.contains(' ') &&
      s.codeUnitAt(1) >= 0x41 &&
      s.codeUnitAt(1) <= 0x5a;

  static bool _plausible(double pos, double len, double third) {
    if (!pos.isFinite || pos < 0 || pos >= _maxPartTicks) return false;
    if (pos != pos.roundToDouble()) return false;
    if (!len.isFinite || len <= 0 || len >= 5e8) return false;
    if (!third.isFinite || third < 0 || third >= 5e8) return false;
    return true;
  }

  /// Gives each object the notes it actually displays (the content window).
  void _attachNotes(List<_PartObject> objects, List<_NoteRecord> notes) {
    for (final o in objects) {
      for (final r in notes) {
        if (r.off < o.start) continue;
        if (r.off >= o.end) break;
        if (r.rel >= o.third && r.rel < o.third + o.len) o.notes.add(r);
      }
    }
  }

  /// Flags arrangement/track nodes: objects that contain another object and
  /// reproduce every note it displays (Cubase serializes the whole content
  /// container on the track node too, which would otherwise double the notes).
  void _markContainers(List<_PartObject> objects) {
    for (final c in objects) {
      if (c.notes.isEmpty) continue;
      for (final o in objects) {
        if (identical(o, c) || o.notes.isEmpty) continue;
        if (!(c.start <= o.start && o.end <= c.end)) continue;
        if (c.start == o.start && c.end == o.end) continue;
        if (o.len >= c.len) continue;
        var superset = true;
        for (final r in o.notes) {
          if (!c.notes.contains(r)) {
            superset = false;
            break;
          }
        }
        if (superset) {
          c.container = true;
          break;
        }
      }
    }
  }

  /// Fallback when the arrangement carries no recognizable part objects: group
  /// note records by the gap between them and treat the stored position as
  /// absolute. Keeps the synthetic/legacy fixtures working.
  List<CubaseMidiPart> _legacyMidiParts(
      List<_NoteRecord> notes, List<String> names) {
    final parts = <CubaseMidiPart>[];
    var group = <_NoteRecord>[notes.first];
    for (var n = 1; n < notes.length; n++) {
      if (notes[n].off - notes[n - 1].off <= _maxNoteGap) {
        group.add(notes[n]);
      } else {
        parts.add(_buildLegacyPart(group, names));
        group = <_NoteRecord>[notes[n]];
      }
    }
    parts.add(_buildLegacyPart(group, names));
    return parts;
  }

  CubaseMidiPart _buildLegacyPart(List<_NoteRecord> recs, List<String> names) {
    final part = CubaseMidiPart(
        name: _partNameFor(recs.first.off, names), offset: recs.first.off);
    var lo = double.infinity;
    var hi = 0.0;
    for (final r in recs) {
      part.notes.add(CubaseMidiNote(
        startTicks: r.rel,
        lengthTicks: r.length,
        pitch: r.pitch,
        velocity: r.velocity,
      ));
      if (r.rel < lo) lo = r.rel;
      final end = r.rel + r.length;
      if (end > hi) hi = end;
    }
    part.startTicks = lo;
    part.endTicks = hi;
    return part;
  }

  // ---- audio events ----

  /// Decodes the audio clips placed on the arrangement timeline.
  ///
  /// An audio event is a class instance with the same object layout as a MIDI
  /// part (`ref | u32 payloadLen | u16 0 | f64 pos | f64 len | f64 srcOffset`)
  /// but without note records. [pos] is in ticks (480 PPQ); [len] and
  /// [srcOffset] are **source sample frames** at the project sample rate (for
  /// the first event of `8-Voltage-Modular(1).wav`, `srcOffset + len` equals the
  /// file's exact frame count).
  ///
  /// Cubase groups a track's events inside a track node exactly like MIDI parts
  /// (same header, `pos = 0`, `len = 576000`); the first audio track is written
  /// inline behind its `MAudioTrackEvent` class definition. The owner node gives
  /// the track name, and the payload carries either a media descriptor
  /// (`FNPath`) or the pool clip name that resolves the source file.
  List<CubaseAudioEvent> extractAudioEvents(List<CubaseTrack> tracks) {
    final data = arrangement;
    if (data.length < 128) return const [];
    final pool = _mediaPool();
    if (pool.isEmpty) return const [];

    final objects = _audioCandidates(data);
    if (objects.isEmpty) return const [];
    _attachNotes(objects, _noteRecords());

    // Cubase gives every instance of one class the same 4-byte reference, so a
    // media descriptor on any member identifies the whole family as audio; the
    // events themselves are no-note objects.
    final audio = <_PartObject>[];
    final groups = <String, List<_PartObject>>{};
    for (final o in objects) {
      if (o.notes.isNotEmpty) continue; // MIDI parts / MIDI track nodes
      groups.putIfAbsent(o.ref, () => []).add(o);
    }
    for (final group in groups.values) {
      if (group.any((o) => o.inline || _hasAudioDescriptor(data, o, pool))) {
        audio.addAll(group);
      }
    }
    // A track node wrapping audio events duplicates their bytes; it carries no
    // media descriptor of its own, so pull it in and let it be dropped.
    for (var changed = true; changed;) {
      changed = false;
      for (final c in objects) {
        if (c.notes.isNotEmpty || audio.contains(c)) continue;
        for (final o in audio) {
          if (c.start < o.start && o.end <= c.end) {
            audio.add(c);
            changed = true;
            break;
          }
        }
      }
    }

    // Track nodes span other objects; the tightest one is the owner.
    for (final c in audio) {
      for (final o in audio) {
        if (identical(c, o)) continue;
        if (c.start <= o.start && o.end <= c.end &&
            (c.start < o.start || o.end < c.end)) {
          c.container = true;
          break;
        }
      }
    }

    final names = <String>[];
    for (final t in tracks) {
      final n = t.name.trim();
      if (n.isNotEmpty && !names.contains(n)) names.add(n);
    }

    final events = <CubaseAudioEvent>[];
    for (final o in audio) {
      if (o.container || o.notes.isNotEmpty) continue;
      _PartObject? owner;
      for (final c in audio) {
        if (!c.container) continue;
        if (c.start <= o.start && o.end <= c.end) {
          if (owner == null || (c.end - c.start) < (owner.end - owner.start)) {
            owner = c;
          }
        }
      }
      final label = _nameRecordIn(data, o.start + 34);
      final trackName = _resolvePartName(data, o, owner, names);
      final source = _audioSourceIn(data, o, pool, label?.$1);
      if (source == null) continue; // nothing to place: no resolvable file
      final event = CubaseAudioEvent(name: trackName, offset: o.start);
      event.clipName = label?.$1 ?? '';
      event.sourceFile = source;
      event.startTicks = o.pos;
      event.lengthFrames = o.len;
      event.sourceOffsetFrames = o.third;
      event.ownerOffset = owner?.start ?? -1;
      // An event written on its own ends with `u32 0 | f32 gain` after its
      // display name; a track node's payload continues with other records.
      if (label != null && _u32At(label.$2) == 0) {
        final g = _f32At(label.$2 + 4);
        if (g.isFinite && g > 0.0 && g <= 4.0) event.gain = g;
      }
      events.add(event);
    }
    events.sort((a, b) => a.offset.compareTo(b.offset));
    return events;
  }

  /// Every event/part-shaped class instance, including the inline first audio
  /// track node (which has no `0x80`-tagged reference).
  List<_PartObject> _audioCandidates(Uint8List data) {
    final n = data.length;
    final out = <_PartObject>[];
    for (var p = 0; p + 34 <= n; p++) {
      if (data[p] != 0x80) continue;
      if (data[p + 8] != 0 || data[p + 9] != 0) continue;
      final payload = _u32At(p + 4);
      if (payload == 0) continue;
      final end = p + 8 + payload;
      if (end > n) continue;
      final pos = _f64At(p + 10);
      final len = _f64At(p + 18);
      final third = _f64At(p + 26);
      if (!_plausible(pos, len, third)) continue;
      out.add(_PartObject(
        start: p,
        end: end,
        pos: pos,
        len: len,
        third: third,
        inline: false,
        ref: latin1.decode(Uint8List.sublistView(data, p, p + 4)),
      ));
    }
    for (final q in _findAll(_inlineAudioTrackMagic)) {
      final c = q + _inlineAudioTrackMagic.length - 4;
      if (c < 0 || c + 34 > n) continue;
      final pos = _f64At(c + 10);
      final len = _f64At(c + 18);
      final third = _f64At(c + 26);
      if (!_plausible(pos, len, third)) continue;
      final payload = _u32At(c + 4);
      final end = payload > 0 && c + 8 + payload <= n ? c + 8 + payload : n;
      out.add(_PartObject(
        start: c,
        end: end,
        pos: pos,
        len: len,
        third: third,
        inline: true,
        ref: latin1.decode(Uint8List.sublistView(data, c, c + 4)),
      ));
    }
    out.sort((a, b) => a.start.compareTo(b.start));
    return out;
  }

  /// Every audio file name listed in the media pool (a `string8` value that
  /// ends in a known audio extension). Cached for the reader's lifetime.
  Set<String> _mediaPool() {
    final cached = _mediaPoolCache;
    if (cached != null) return cached;
    final d = bytes;
    final out = <String>{};
    for (var q = 0; q + 4 <= d.length; q++) {
      if (d[q] != 0) continue; // a string8 length is far below 16 MiB
      final s = _string8At(d, q);
      if (s != null && _audioExtension.hasMatch(s)) out.add(s);
    }
    return _mediaPoolCache = out;
  }

  /// Whether [o]'s payload holds a media descriptor naming an audio file.
  bool _hasAudioDescriptor(Uint8List data, _PartObject o, Set<String> pool) {
    for (var q = o.start + 34; q + 4 <= o.end; q++) {
      if (data[q] != 0) continue;
      final s = _string8At(data, q);
      if (s != null && _audioExtension.hasMatch(s)) return true;
    }
    return false;
  }

  /// Resolves the source file of an event: a capture file name inlined in the
  /// payload wins, otherwise the clip display name is matched against the pool
  /// by base name (Cubase stores the clip name without its extension).
  String? _audioSourceIn(
      Uint8List data, _PartObject o, Set<String> pool, String? clipName) {
    for (var q = o.start + 34; q + 4 <= o.end; q++) {
      if (data[q] != 0) continue;
      final s = _string8At(data, q);
      if (s != null && _audioExtension.hasMatch(s)) return s;
    }
    if (clipName == null || clipName.isEmpty) return null;
    final want = _baseNameOf(clipName);
    for (final f in pool) {
      if (_baseNameOf(f) == want) return f;
    }
    return null;
  }

  /// A `string8` (`u32 len | text | \0 [EF BB BF]`, len includes the trailer)
  /// starting at [q], or null when the bytes there are not a clean string.
  static String? _string8At(Uint8List d, int q) {
    if (q + 4 > d.length) return null;
    if (d[q] != 0) return null; // names are short; reject large lengths early
    final len = (d[q + 1] << 16) | (d[q + 2] << 8) | d[q + 3];
    if (len < 4 || len > 400 || q + 4 + len > d.length) return null;
    final stop = q + 4 + len;
    var textEnd = stop;
    if (len >= 4 &&
        d[stop - 4] == 0 &&
        d[stop - 3] == 0xef &&
        d[stop - 2] == 0xbb &&
        d[stop - 1] == 0xbf) {
      textEnd = stop - 4;
    } else if (d[stop - 1] == 0) {
      textEnd = stop - 1;
    } else {
      return null;
    }
    if (textEnd <= q + 4) return null;
    for (var k = q + 4; k < textEnd; k++) {
      final c = d[k];
      if (c < 0x20 || c == 0x7f) return null;
    }
    return latin1.decode(Uint8List.sublistView(d, q + 4, textEnd));
  }

  /// Lower-cased file name without its extension, for pool matching.
  static String _baseNameOf(String s) {
    final t = s.trim().toLowerCase();
    final dot = t.lastIndexOf('.');
    return dot > 0 ? t.substring(0, dot) : t;
  }

  /// Project sample rate from the metadata archive: the `Float` record that
  /// follows `AudioSampleRate` (44.1 kHz projects store 44100 there).
  double? detectSampleRate() {
    final d = bytes;
    var from = 0;
    while (true) {
      final i = _indexOfAscii(d, 'AudioSampleRate', from);
      if (i < 0) return null;
      from = i + 1;
      final stop = math.min(i + 512, d.length);
      final j = _indexOfAscii(d, _sampleRateFloatRecord, i, stop);
      if (j < 0) continue;
      final v = ByteData.sublistView(d, j + 12, j + 20).getFloat64(0, Endian.big);
      if (v.isFinite && v >= 8000.0 && v <= 384000.0) return v;
    }
  }

  /// Naive byte search (the container is decoded as Latin-1 text elsewhere, but
  /// the sample-rate record lives in the metadata archive, outside it).
  static int _indexOfAscii(Uint8List d, String needle, int from, [int? end]) {
    final n = needle.length;
    final limit = (end ?? d.length) - n;
    for (var i = from < 0 ? 0 : from; i <= limit; i++) {
      var ok = true;
      for (var k = 0; k < n; k++) {
        if (d[i + k] != needle.codeUnitAt(k)) {
          ok = false;
          break;
        }
      }
      if (ok) return i;
    }
    return -1;
  }

  double _f32At(int o) =>
      ByteData.sublistView(arrangement, o, o + 4).getFloat32(0, Endian.big);

  /// Resolves the owning track name by finding, in the bytes just before the
  /// notes, the closest preceding occurrence of any known track name.
  String _partNameFor(int recordStart, List<String> names) {
    if (names.isEmpty) return '';
    final text = _text;
    final winStart = (recordStart - _partNameWindow).clamp(0, text.length);
    final win = text.substring(winStart, recordStart);
    var bestEnd = -1;
    var best = '';
    for (final name in names) {
      for (final variant in _nameVariants(name)) {
        final i = win.lastIndexOf(variant);
        if (i < 0) continue;
        final end = i + variant.length;
        if (end > bestEnd || (end == bestEnd && name.length > best.length)) {
          bestEnd = end;
          best = name;
        }
      }
    }
    return best;
  }

  /// Cubase may store a name as double-encoded UTF-8; yield the plausible
  /// spellings so a search in the raw (Latin-1) text can still succeed.
  static Iterable<String> _nameVariants(String name) sync* {
    yield name;
    final once = latin1.decode(utf8.encode(name));
    if (once != name) yield once;
    final twice = latin1.decode(utf8.encode(once));
    if (twice != once && twice != name) yield twice;
  }

  double _f64At(int o) =>
      ByteData.sublistView(arrangement, o, o + 8).getFloat64(0, Endian.big);

  int _u32At(int o) =>
      ByteData.sublistView(arrangement, o, o + 4).getUint32(0, Endian.big);

  /// Reads everything the reader currently supports.
  CubaseProject read() {
    parseContainer();
    final project =
        CubaseProject(projectName: projectName, appVersion: appVersion());
    final tempo = detectTempo();
    if (tempo != null) project.tempoBpm = tempo;
    final sig = detectTimeSignature();
    if (sig != null) {
      project.timeSigNumerator = sig.$1;
      project.timeSigDenominator = sig.$2;
    }
    final tracks = extractTracks();
    project.tracks.addAll(tracks);
    final rate = detectSampleRate();
    if (rate != null) project.sampleRate = rate;
    final masterDb = detectMasterVolumeDb();
    if (masterDb != null) project.masterVolumeDb = masterDb;
    for (final master in detectMasterPlugins()) {
      project.masterInserts.add(master.name);
      project.masterInsertStates.add(master.state);
    }
    project.midiParts.addAll(extractMidiParts(tracks));
    project.audioEvents.addAll(extractAudioEvents(tracks));
    project.plugins.addAll(extractPlugins());
    project.markers.addAll(extractMarkers());
    return project;
  }

  // ---- helpers ----

  Iterable<int> _findAll(String pattern) sync* {
    final hay = _text;
    var from = 0;
    while (true) {
      final i = hay.indexOf(pattern, from);
      if (i < 0) break;
      yield i;
      from = i + 1;
    }
  }

  static CprValue? _find(List<CprRecord> recs, String name) {
    for (final r in recs) {
      if (r.name == name) return r.value;
    }
    return null;
  }

  static String? _stringOf(CprValue? v) {
    if (v == null) return null;
    if (v.kind == CprKind.string) return v.data as String;
    if (v.kind == CprKind.object) {
      final s = _find(v.data as List<CprRecord>, 'String');
      if (s != null && s.kind == CprKind.string) return s.data as String;
    }
    return null;
  }

  static int? _intOf(CprValue? v) =>
      (v != null && v.kind == CprKind.int64) ? v.data as int : null;

  static double? _doubleOf(CprValue? v) =>
      (v != null && v.kind == CprKind.double64) ? v.data as double : null;

  static int _min(int a, int b) => a < b ? a : b;

  /// Normalizes a channel name for dedup: trims outer spaces and folds common
  /// Latin accents onto their base letters so NFC and NFD spellings converge.
  static String _normalizeName(String s) {
    final t = s.trim();
    if (t.isEmpty) return t;
    final out = StringBuffer();
    for (final r in t.runes) {
      if (r >= 0x0300 && r <= 0x036f) continue; // drop combining marks
      out.writeCharCode(_foldLatinAccent(r));
    }
    return out.toString();
  }

  static int _foldLatinAccent(int r) {
    // Latin-1 Supplement precomposed letters (U+00C0..U+00FF) → ASCII base.
    const table = 'AAAAAAÆCEEEEIIIIDNOOOOO×OUUUUYÞßaaaaaaæceeeeiiiidnooooo÷ouuuuyþy';
    if (r >= 0x00c0 && r <= 0x00ff) {
      final c = table[r - 0x00c0];
      if (c != '\u0000') return c.codeUnitAt(0);
    }
    return r;
  }

  // Anchors (latin1).
  static final String _channelAnchor = '\x00\x00\x00\x05Name\x00\x00\x02\x00\x06'
      '\x00\x00\x00\x01\x00\x00\x00\x07String\x00\x00\x08';
  static const String _pluginHeader = '\x00\x00\x00\x07Plugin\x00\x00\x02\x00\x06';
  static const String _tempoMagic = 'MTempoTrackEvent';
  static const String _markerMagic = 'MMarkerTrackEvent';
  // Cubase channel type codes: 4 instrument, 1 audio, 7 input bus, 8 output bus.
  static const int _outputChannelType = 8;
  // An instrument channel's hosted VSTi: `Synth Slot` object holding a
  // `Plugin` whose `Plugin Name` is the instrument (see _channelInstrument).
  static const String _synthSlotAnchor =
      '\x00\x00\x00\x0bSynth Slot\x00\x00\x02\x00\x06';
  static const String _pluginNameRecord =
      '\x00\x00\x00\x0cPlugin Name\x00\x00\x08';
  // A plug-in's serialized state, as two blob records inside its `Plugin`
  // object: the VST3 component state and the (usually empty) controller state.
  static const String _audioComponentAnchor =
      '\x00\x00\x00\x0faudioComponent\x00\x00\x02\x00\x07';
  static const String _editControllerAnchor =
      '\x00\x00\x00\x0feditController\x00\x00\x02\x00\x07';
  // The channel's insert chain: every occupied `InsertFolder` slot carries a
  // `Plugin`; the fields that follow the folder delimit it.
  static const String _insertFolderAnchor =
      '\x00\x00\x00\x0dInsertFolder\x00\x00\x02\x00\x06';
  static const String _summingModeAnchor =
      '\x00\x00\x00\x0cSummingMode\x00\x00\x01';
  static const String _hasAudioStripsAnchor =
      '\x00\x00\x00\x0fhasAudioStrips\x00\x00\x01';
  // MIDI note records: `\x00\x00\x00\x05GLFX` sits 14 bytes into a 0x90 note
  // record (the four tag-count bytes are followed by the first property tag).
  static const String _glfxAnchor = '\x00\x00\x00\x05GLFX';
  // Note records within one part are contiguous; parts are separated by much
  // larger gaps (part objects). 4 KiB cleanly splits the two.
  static const int _maxNoteGap = 4096;
  static const double _maxTicks = 1e8;
  static const double _maxPartTicks = 1e6;
  static const int _partNameWindow = 4096;
  // The first MIDI part of a file is serialized inline behind its class
  // definition; the object fields follow 4 bytes before this pattern's end.
  static const String _inlineMidiPartMagic =
      '\xff\xff\xff\xff\x00\x00\x00\x0fMMidiPartEvent\x00\x00\x02';
  // The first audio track is serialized inline the same way, behind its
  // `MAudioTrackEvent` class definition (the trailing `\x00\x01` is the track
  // marker where MIDI parts carry `\x00\x02`).
  static const String _inlineAudioTrackMagic =
      '\xff\xff\xff\xff\x00\x00\x00\x11MAudioTrackEvent\x00\x00\x01';
  // `Float` record inside the metadata archive's `AudioSampleRate` attribute.
  static const String _sampleRateFloatRecord =
      '\x00\x00\x00\x06Float\x00\x00\x04';
  static final RegExp _audioExtension =
      RegExp(r'\.(wav|wave|bwf|aif|aiff|aifc|mp3|flac|ogg|m4a|caf)$',
          caseSensitive: false);
  // Cubase 12 used \x00\x00\x02\x0b\x00\x00\x02\xf0; 13 bumped it by 4.
  static const String _trackNodeMagic = '\x00\x00\x02\x0f\x00\x00\x02\xf4';
  static final String _numeratorMagic = '\x00\x00\x00\x09Numerator\x00\x00\x01';
  static final String _denominatorMagic = '\x00\x00\x00\x0cDenominator\x00\x00\x01';
}

/// Quick-access key for matching parts to tracks: lower-cased name with the
/// duplicated-track marker Cubase appends (" (D)") stripped. A duplicated
/// track therefore groups with its original.
String cubaseBaseTrackName(String name) {
  var t = name.trim();
  if (t.endsWith(' (D)')) t = t.substring(0, t.length - 4).trimRight();
  return t.toLowerCase();
}

/// A note record as stored: [off] is its byte offset in the arrangement and
/// [rel] the position relative to the owning part's content container.
class _NoteRecord {
  const _NoteRecord(this.off, this.rel, this.length, this.pitch, this.velocity);
  final int off;
  final double rel;
  final double length;
  final int pitch;
  final int velocity;
}

/// A MIDI-part object (or the inline first part) located in the arrangement.
class _PartObject {
  _PartObject({
    required this.start,
    required this.end,
    required this.pos,
    required this.len,
    required this.third,
    required this.inline,
    this.ref = '',
  });
  final int start;
  int end;
  final double pos;
  final double len;
  final double third;
  final bool inline;

  /// The 4 raw reference bytes Cubase stamps on every instance of a class.
  final String ref;
  final List<_NoteRecord> notes = [];
  bool container = false;
}

/// Resolves which track each MIDI part belongs to, as indices into [trackNames]
/// (or -1 when there are no tracks). Parts decoded from the arrangement carry
/// the byte offset of their owning track node ([CubaseMidiPart.ownerOffset]);
/// everything under one node is a single track, so those parts are assigned
/// together. Tracks that share a name (several MIDI tracks routed to the same
/// VSTi, or a duplicated track tagged " (D)") are matched in order, which
/// reproduces the track names of Cubase's own MIDI export (7.cpr and 9.cpr).
List<int> assignMidiPartsToTracks(
  List<CubaseMidiPart> parts,
  List<String> trackNames,
) =>
    _assignToTracks(
      [for (final p in parts) p.name],
      [for (final p in parts) p.ownerOffset],
      trackNames,
    );

/// Audio counterpart of [assignMidiPartsToTracks]: audio events carry the byte
/// offset of their owning audio track node, so each track's events are assigned
/// together; the rest are matched by name (Cubase's audio tracks often share the
/// default "Audio 01 (D)" name, and the rank spreads them in order).
List<int> assignAudioEventsToTracks(
  List<CubaseAudioEvent> events,
  List<String> trackNames,
) =>
    _assignToTracks(
      [for (final e in events) e.name],
      [for (final e in events) e.ownerOffset],
      trackNames,
    );

List<int> _assignToTracks(
  List<String> partNames,
  List<int> partOwners,
  List<String> trackNames,
) {
  final byBase = <String, List<int>>{};
  for (var i = 0; i < trackNames.length; i++) {
    byBase.putIfAbsent(cubaseBaseTrackName(trackNames[i]), () => []).add(i);
  }
  final used = <String, int>{};
  final owners = <int, int>{};
  final out = <int>[];
  for (var i = 0; i < partNames.length; i++) {
    final owner = partOwners[i];
    if (owner >= 0) {
      final known = owners[owner];
      if (known != null) {
        out.add(known);
        continue;
      }
    }
    final key = cubaseBaseTrackName(partNames[i]);
    final candidates = byBase[key];
    if (candidates == null || candidates.isEmpty) {
      final fallback = trackNames.isEmpty ? -1 : 0;
      if (owner >= 0) owners[owner] = fallback;
      out.add(fallback);
      continue;
    }
    final rank = used[key] ?? 0;
    used[key] = rank + 1;
    final target = candidates[rank % candidates.length];
    if (owner >= 0) owners[owner] = target;
    out.add(target);
  }
  return out;
}
