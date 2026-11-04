// The arranger prompt should ask for compositional depth without imposing
// arbitrary small limits that the engine does not have.
import 'package:cadence_ui/ai/llm_client.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('system prompt requests professional musical development', () {
    for (final phrase in const [
      'harmonic plan',
      'primary motif',
      'voice leading',
      'parallel fifths',
      'countermelody',
      'idiomatically',
      'two-hand texture',
      'silently shorten',
      '16 tracks',
      '12,000 notes',
    ]) {
      expect(systemPrompt, contains(phrase), reason: phrase);
    }

    expect(systemPrompt, isNot(contains('at most 8 tracks')));
    expect(systemPrompt, isNot(contains('1600 notes total')));
    expect(systemPrompt, contains('safety limits, not targets'));
  });

  test('brief analyst respects scope and translates references', () {
    for (final phrase in const [
      'music director',
      'copyrighted melody',
      'piano intro stays',
      'arrangement_scope',
      'register_plan',
      'arranger_brief',
      'project tempo is authoritative',
    ]) {
      expect(briefAnalysisPrompt, contains(phrase), reason: phrase);
    }
  });
}
