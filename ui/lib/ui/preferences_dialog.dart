// Preferences dialog (§7.14 v0): audio device config (output/input, sample
// rate, buffer) + MIDI input list. Changes apply immediately and the
// settings file is saved automatically.

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../engine/engine_service.dart';
import '../l10n/l10n.dart';
import '../l10n/locale_store.dart';
import 'app_alert.dart';
import 'remote_control_panel.dart';

Future<void> showPreferencesDialog(
    BuildContext context, EngineService service) {
  return showDialog<void>(
    context: context,
    builder: (_) => Dialog(
      backgroundColor: const Color(0xFF252830),
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxHeight: 560),
        child: SizedBox(
          width: 520,
          child: SingleChildScrollView(
            child: _PreferencesBody(service: service),
          ),
        ),
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
  late final TextEditingController _latencyCtrl;

  @override
  void initState() {
    super.initState();
    _reloadLists();
    _latencyCtrl = TextEditingController(
        text: widget.service.midiInputLatencyMs.toStringAsFixed(0));
  }

  @override
  void dispose() {
    _latencyCtrl.dispose();
    super.dispose();
  }

  void _reloadLists() {
    _outputs = widget.service.outputDeviceNames;
    _inputs = widget.service.inputDeviceNames;
    _rates = widget.service.availableSampleRates;
    _buffers = widget.service.availableBufferSizes;
    _midi = widget.service.midiInputNames;
  }

  /// Re-enumerates devices from CoreAudio (explicit user action).
  void _rescanDevices() {
    widget.service.refreshDeviceLists();
    _reloadLists();
    setState(() {});
  }

  Future<void> _apply({String? out, String? input, double? rate, int? buffer}) async {
    final error = widget.service.setDeviceConfig(
      out ?? widget.service.currentOutputDevice,
      input ?? widget.service.currentInputDevice,
      rate ?? 0,
      buffer ?? 0,
    );

    // Re-read the ACTUAL values from the device — the device may have
    // adjusted the requested buffer/rate to the nearest supported value.
    widget.service.refreshDeviceLists();
    setState(() {
      _reloadLists();
    });
    if (error == null) {
      widget.service.saveDeviceSettings();
    } else {
      // Un fallo al abrir el dispositivo (p. ej. "en uso") merece un aviso
      // claro, no un texto suelto bajo los desplegables.
      showAppError(context, error);
    }
  }

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    final service = widget.service;
    return Padding(
      padding: const EdgeInsets.all(20),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(l10n.prefTitle,
              style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600)),
          const SizedBox(height: 14),
          // ---- idioma de la interfaz ----
          Text(l10n.prefSectionLanguage,
              style: const TextStyle(fontSize: 12, color: Color(0xFF9BA1AE))),
          const SizedBox(height: 6),
          Wrap(
            spacing: 6,
            runSpacing: 6,
            children: [
              _languageChip(context, l10n.prefLanguageSystem, null),
              // El nombre de cada idioma va SIEMPRE en su propio idioma.
              _languageChip(context, 'Español', const Locale('es')),
              _languageChip(context, 'English', const Locale('en')),
              _languageChip(context, '日本語', const Locale('ja')),
            ],
          ),
          const Padding(
            padding: EdgeInsets.symmetric(vertical: 12),
            child: Divider(height: 1, color: Color(0xFF32363F)),
          ),
          // ---- control remoto por LAN ----
          Text(l10n.prefSectionRemote,
              style: const TextStyle(fontSize: 12, color: Color(0xFF9BA1AE))),
          const SizedBox(height: 6),
          RemoteControlPanel(service: service),
          const Padding(
            padding: EdgeInsets.symmetric(vertical: 12),
            child: Divider(height: 1, color: Color(0xFF32363F)),
          ),
          _dropdown(l10n.prefOutputDevice, _outputs, service.currentOutputDevice,
              (v) => _apply(out: v)),
          const SizedBox(height: 10),
          _dropdown(l10n.prefInputDevice, _inputs, service.currentInputDevice,
              (v) => _apply(input: v)),
          const SizedBox(height: 10),
          Row(children: [
            Expanded(
              child: _dropdownNum(l10n.prefSampleRate, _rates,
                  service.sampleRate, (v) => _apply(rate: v)),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: _dropdownInt(l10n.prefBuffer, _buffers, service.currentBuffer,
                  (v) => _apply(buffer: v)),
            ),
            const SizedBox(width: 12),
            Text(
                l10n.prefRoundTrip(
                    (service.currentBuffer / service.sampleRate * 1000 * 2)
                        .toStringAsFixed(0)),
                style: const TextStyle(
                    fontSize: 10, color: Color(0xFF9BA1AE))),
          ]),
          const SizedBox(height: 10),
          Row(children: [
            Text(l10n.prefMidiLatency,
                style: const TextStyle(fontSize: 11, color: Color(0xFF9BA1AE))),
            const SizedBox(width: 8),
            SizedBox(
              width: 56,
              child: TextField(
                controller: _latencyCtrl,
                keyboardType: TextInputType.number,
                inputFormatters: [
                  FilteringTextInputFormatter.allow(RegExp(r'-?[0-9]'))
                ],
                style: const TextStyle(fontSize: 11),
                decoration: const InputDecoration(
                    isDense: true, counterText: ''),
                maxLength: 4,
                onSubmitted: (v) {
                  final ms = double.tryParse(v) ?? 0.0;
                  widget.service.setMidiInputLatencyMs(ms);
                  setState(() {});
                },
              ),
            ),
            const SizedBox(width: 6),
            Expanded(
              child: Text(l10n.prefMidiLatencyHelp,
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(fontSize: 9, color: Color(0xFF5C6270))),
            ),
          ]),
          const SizedBox(height: 10),
          Text(
            service.deviceInputChannelCount > 0
                ? l10n.prefActiveInputs('${service.deviceInputChannelCount}')
                : l10n.prefNoInputs,
            style: TextStyle(
                fontSize: 11,
                color: service.deviceInputChannelCount > 0
                    ? const Color(0xFF3DD68C)
                    : const Color(0xFFF5A524)),
          ),
          const SizedBox(height: 8),
          TextButton.icon(
            onPressed: _rescanDevices,
            icon: const Icon(Icons.refresh, size: 14),
            label: Text(l10n.prefRescan,
                style: const TextStyle(fontSize: 11)),
          ),
          const SizedBox(height: 8),
          Text(l10n.prefMidiInputTitle,
              style: const TextStyle(fontSize: 12, color: Color(0xFF9BA1AE))),
          if (_midi.isEmpty)
            Padding(
              padding: const EdgeInsets.symmetric(vertical: 8),
              child: Text(l10n.prefNoMidi,
                  style: const TextStyle(fontSize: 11, color: Color(0xFF5C6270))),
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
          const SizedBox(height: 16),
          Align(
            alignment: Alignment.bottomRight,
            child: TextButton(
              onPressed: () => Navigator.pop(context),
              child: Text(context.l10n.alertClose),
            ),
          ),
        ],
      ),
    );
  }
}

/// Boton de idioma. `value == null` = seguir al sistema.
Widget _languageChip(BuildContext context, String label, Locale? value) {
  final selected = appLocale.value?.languageCode == value?.languageCode;
  return InkWell(
    onTap: () => LocaleStore.save(value),
    borderRadius: BorderRadius.circular(5),
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: selected ? const Color(0xFF4D9DF7) : const Color(0xFF2C303A),
        borderRadius: BorderRadius.circular(5),
        border: Border.all(color: const Color(0xFF3E4350)),
      ),
      child: Text(
        label,
        style: TextStyle(
          fontSize: 11,
          fontWeight: FontWeight.w600,
          color: selected ? Colors.black : const Color(0xFFE9EBF0),
        ),
      ),
    ),
  );
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
        isDense: true,
        isExpanded: true,
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
