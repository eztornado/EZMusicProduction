// A brand-new session has no project document yet. The AI context must still
// describe tempo/meter and the installed sounds instead of returning `{}`.
import 'dart:convert';

import 'package:cadence_ui/engine/cadence_bindings.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('blank session exposes a usable arranger context', () {
    final engine = CadenceEngine();
    addTearDown(engine.dispose);

    final raw = engine.aiContextJson(
      selectedTrackId: -1,
      anchorSeconds: 0,
      bars: 8,
    );
    final context = jsonDecode(raw) as Map<String, dynamic>;

    expect(context, isNot(isEmpty));
    expect(context['has_material'], isFalse);
    expect(context['key'], 'unspecified');
    expect(context['time_signature_numerator'], 4);
    expect(context['time_signature_denominator'], 4);
    expect(context['tracks'], isEmpty);

    final analysis = context['analysis'] as Map<String, dynamic>;
    expect(analysis['has_material'], isFalse);
    expect(analysis['key'], 'unspecified');
    expect(analysis['chord_line'], isEmpty);
    expect((analysis['heard'] as String), contains('blank canvas'));

    final available =
        context['available_instruments'] as Map<String, dynamic>;
    expect((available['ezinst_programs'] as List), hasLength(128));
    expect(available['ezsynth_presets'], isA<List>());
    expect(available['ezorq_programs'], isA<List>());
    expect(available['vst_instruments'], isA<List>());
  });
}
