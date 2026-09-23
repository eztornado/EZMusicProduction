import 'package:flutter_test/flutter_test.dart';
import 'package:cadence_ui/engine/engine_service.dart';

void main() {
  test('device lists are populated', () {
    final service = EngineService();
    addTearDown(service.dispose);
    // ignore: avoid_print
    print('OUTPUTS (${service.outputDeviceNames.length}): ${service.outputDeviceNames}');
    // ignore: avoid_print
    print('INPUTS (${service.inputDeviceNames.length}): ${service.inputDeviceNames}');
    // ignore: avoid_print
    print('RATES: ${service.availableSampleRates}');
    // ignore: avoid_print
    print('BUFFERS: ${service.availableBufferSizes}');
    expect(service.outputDeviceNames, isNotEmpty);
  });
}
