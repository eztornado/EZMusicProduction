// Validates the bundled sound catalog (ui/assets/sound_catalog.json): unique
// ids/urls, required fields, a valid type and https sources. Keeps the
// generated catalog honest as it grows.
//
// Regenerate the catalog with: python3 ui/tool/build_sound_catalog.py

import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

void main() {
  test('sound catalog is well formed', () {
    final file = File('assets/sound_catalog.json');
    expect(file.existsSync(), isTrue, reason: 'catalog not found');

    final list = jsonDecode(file.readAsStringSync()) as List;
    expect(list, isNotEmpty);

    final ids = <String>{};
    final urls = <String>{};
    var loops = 0;
    var oneshots = 0;

    for (final raw in list) {
      final e = raw as Map<String, dynamic>;
      final id = e['id'] as String;
      expect(id, isNotEmpty);
      expect(ids.add(id), isTrue, reason: 'duplicate id $id');

      final url = e['url'] as String;
      expect(url, startsWith('https://'), reason: '$id url must be https');
      expect(urls.add(url), isTrue, reason: 'duplicate url $url');

      expect((e['name'] as String).trim(), isNotEmpty, reason: '$id name');
      expect((e['category'] as String).trim(), isNotEmpty, reason: '$id cat');
      expect((e['license'] as String).trim(), isNotEmpty, reason: '$id licence');

      final type = e['type'];
      expect(type, anyOf('loop', 'oneshot'), reason: '$id type');
      if (type == 'loop') {
        loops++;
      } else {
        oneshots++;
      }

      final duration = e['duration'];
      if (duration is num && duration > 0) {
        expect(duration, lessThan(2 * 60 * 60), reason: '$id duration');
      }
    }

    // The library must keep a healthy mix of bases (loops) and loose
    // one-shots, which is the whole point of this feature.
    expect(loops, greaterThanOrEqualTo(30), reason: 'not enough bases/loops');
    expect(oneshots, greaterThanOrEqualTo(100), reason: 'not enough one-shots');
  });
}
