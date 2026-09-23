// Preferences dialog (§7.14 v0): audio device config (output/input, sample
// rate, buffer) + MIDI input list. Changes apply immediately and the
// settings file is saved automatically.

import 'package:flutter/material.dart';

import '../engine/engine_service.dart';

Future<void> showPreferencesDialog(
    BuildContext context, EngineService service) {
  return showDialog<void>(
    context: context,
    builder: (_) => Dialog(
      backgroundColor: const Color(0xFF252830),
      child: SizedBox(
        width: 520,
        child: _PreferencesBody(service: service),
      ),
    ),
  );
}

class _PreferencesBody extends StatefulWidget {
  const _PreferencesBody({required this.service});

  final EngineService service;

  @override
  State<_PreferencesBody> createState() => _PreferencesBodyState();
}

class _PreferencesBodyState extends State<_PreferencesBody> {
  late List<String> _outputs;
  late List<String> _inputs;
  late List<double> _rates;
  late List<int> _buffers;
  late List<String> _midi;

  String? _error;

  @override
  void initState() {
    super.initState();
    _reloadLists();
  }

  void _reloadLists() {
    _outputs = widget.service.outputDeviceNames;
    _inputs = widget.service.inputDeviceNames;
    _rates = widget.service.availableSampleRates;
    _buffers = widget.service.availableBufferSizes;
    _midi = widget.service.midiInputNames;
  }

  Future<void> _apply({String? out, String? input, double? rate, int? buffer}) async {
    final error = widget.service.setDeviceConfig(
      out ?? widget.service.currentOutputDevice,
      input ?? widget.service.currentInputDevice,
      rate ?? 0,
      buffer ?? 0,
    );
    setState(() {
      _error = error;
      _reloadLists();
    });
    if (error == null) widget.service.saveDeviceSettings();
  }

  @override
  Widget build(BuildContext context) {
    final service = widget.service;
    return Padding(
      padding: const EdgeInsets.all(20),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('Preferencias — Audio y MIDI',
              style: TextStyle(fontSize: 15, fontWeight: FontWeight.w600)),
          const SizedBox(height: 16),
          _dropdown('Dispositivo de salida', _outputs, service.currentOutputDevice,
              (v) => _apply(out: v)),
          const SizedBox(height: 10),
          _dropdown('Dispositivo de entrada', _inputs, service.currentInputDevice,
              (v) => _apply(input: v)),
          const SizedBox(height: 10),
          Row(children: [
            Expanded(
              child: _dropdownNum('Sample rate', _rates,
                  service.sampleRate, (v) => _apply(rate: v)),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: _dropdownInt('Buffer', _buffers, service.currentBuffer,
                  (v) => _apply(buffer: v)),
            ),
            const SizedBox(width: 12),
            Text(
                '~${(service.currentBuffer / service.sampleRate * 1000 * 2).toStringAsFixed(0)} ms ida y vuelta',
                style: const TextStyle(
                    fontSize: 10, color: Color(0xFF9BA1AE))),
          ]),
          const SizedBox(height: 10),
          Text(
            service.deviceInputChannelCount > 0
                ? 'Entradas activas del dispositivo: ${service.deviceInputChannelCount}'
                : '⚠ El dispositivo activo no tiene entradas — para grabar, elige '
                    'en "Dispositivo de entrada" uno con micrófono',
            style: TextStyle(
                fontSize: 11,
                color: service.deviceInputChannelCount > 0
                    ? const Color(0xFF3DD68C)
                    : const Color(0xFFF5A524)),
          ),
          const SizedBox(height: 16),
          const Text('Entrada MIDI (elige un dispositivo)',
              style: TextStyle(fontSize: 12, color: Color(0xFF9BA1AE))),
          if (_midi.isEmpty)
            const Padding(
              padding: EdgeInsets.symmetric(vertical: 8),
              child: Text('No hay dispositivos MIDI conectados',
                  style: TextStyle(fontSize: 11, color: Color(0xFF5C6270))),
            )
          else
            Column(
              children: [
                for (var i = 0; i < _midi.length; i++)
                  GestureDetector(
                    onTap: () {
                      widget.service.selectMidiInput(i);
                      setState(() {});
                    },
                    child: Container(
                      padding: const EdgeInsets.symmetric(
                          vertical: 6, horizontal: 8),
                      decoration: BoxDecoration(
                        color: widget.service.selectedMidiInputIndex == i
                            ? const Color(0xFF4D9DF7).withValues(alpha: 0.15)
                            : Colors.transparent,
                        borderRadius: BorderRadius.circular(4),
                      ),
                      child: Row(
                        children: [
                          Icon(
                            widget.service.selectedMidiInputIndex == i
                                ? Icons.radio_button_checked
                                : Icons.radio_button_off,
                            size: 14,
                            color: widget.service.selectedMidiInputIndex == i
                                ? const Color(0xFF4D9DF7)
                                : const Color(0xFF9BA1AE),
                          ),
                          const SizedBox(width: 8),
                          Text(_midi[i],
                              style: const TextStyle(
                                  fontSize: 11,
                                  color: Color(0xFFE9EBF0))),
                        ],
                      ),
                    ),
                  ),
              ],
            ),
          if (_error != null)
            Padding(
              padding: const EdgeInsets.only(top: 10),
              child: Text(_error!,
                  style: const TextStyle(color: Color(0xFFE5484D), fontSize: 11)),
            ),
          const SizedBox(height: 16),
          Align(
            alignment: Alignment.bottomRight,
            child: TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('Cerrar'),
            ),
          ),
        ],
      ),
    );
  }
}

  Widget _dropdown(
      String label, List<String> rawItems, String value, void Function(String) onChanged) {
    // The dropdown asserts value must appear exactly once in items: drop
    // empties and duplicates, and never hand it a value that is not listed.
    final items = rawItems.where((e) => e.isNotEmpty).toSet().toList()..sort();
    final current = value.isNotEmpty && items.contains(value) ? value : null;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label,
            style: const TextStyle(fontSize: 12, color: Color(0xFF9BA1AE))),
        DropdownButtonFormField<String>(
          // ignore: deprecated_member_use
          value: current,
          items: [
            for (final item in items)
              DropdownMenuItem(value: item, child: Text(item, style: const TextStyle(fontSize: 12))),
          ],
          onChanged: (v) => v == null ? null : onChanged(v),
        ),
      ],
    );
  }

  Widget _dropdownNum(String label, List<double> rawItems, double value,
      void Function(double) onChanged) {
    final items = rawItems.toSet().toList()..sort();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label,
            style: const TextStyle(fontSize: 12, color: Color(0xFF9BA1AE))),
        DropdownButtonFormField<double>(
          // ignore: deprecated_member_use
          value: items.contains(value) ? value : null,
          items: [
            for (final item in items)
              DropdownMenuItem(
                  value: item,
                  child: Text('${item.toStringAsFixed(0)} Hz',
                      style: const TextStyle(fontSize: 12))),
          ],
          onChanged: (v) => v == null ? null : onChanged(v),
        ),
      ],
    );
  }

  Widget _dropdownInt(String label, List<int> rawItems, int value,
      void Function(int) onChanged) {
    final items = rawItems.toSet().toList()..sort();
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label,
            style: const TextStyle(fontSize: 12, color: Color(0xFF9BA1AE))),
        DropdownButtonFormField<int>(
          // ignore: deprecated_member_use
          value: items.contains(value) ? value : null,
          items: [
            for (final item in items)
              DropdownMenuItem(
                  value: item,
                  child: Text('$item',
                      style: const TextStyle(fontSize: 12))),
          ],
          onChanged: (v) => v == null ? null : onChanged(v),
        ),
      ],
    );
  }
