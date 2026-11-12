// Preferences dialog (§7.14 v0): audio device config (output/input, sample
// rate, buffer), MIDI input, remote control and plugin folders. The settings
// apply immediately and the settings file is saved automatically.
//
// The dialog is organised as a desktop preferences window: a vertical section
// rail on the left and one scrollable panel on the right.

import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../debug/debug_log.dart';
import '../debug/debug_settings.dart';
import '../engine/engine_service.dart';
import '../engine/plugin_directories.dart';
import '../l10n/l10n.dart';
import '../l10n/locale_store.dart';
import '../shortcuts/shortcut_settings.dart';
import 'app_alert.dart';
import 'project_dialogs.dart';
import 'remote_control_panel.dart';

const Color _panel = Color(0xFF252830);
const Color _rail = Color(0xFF1E2027);
const Color _card = Color(0xFF2C303A);
const Color _border = Color(0xFF3A3F4B);
const Color _text = Color(0xFFE9EBF0);
const Color _muted = Color(0xFF9BA1AE);
const Color _faint = Color(0xFF5C6270);
const Color _accent = Color(0xFF4D9DF7);
const Color _ok = Color(0xFF3DD68C);
const Color _warn = Color(0xFFF5A524);

Future<void> showPreferencesDialog(
  BuildContext context,
  EngineService service,
) {
  final size = MediaQuery.sizeOf(context);
  // Deja sitio al insetPadding por defecto del Dialog (40 px por lado).
  final width = (size.width - 80).clamp(320.0, 780.0).toDouble();
  final height = (size.height - 80).clamp(360.0, 600.0).toDouble();
  return showDialog<void>(
    context: context,
    builder:
        (_) => Dialog(
          backgroundColor: _panel,
          clipBehavior: Clip.antiAlias,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(10),
          ),
          child: SizedBox(
            width: width,
            height: height,
            child: _PreferencesBody(service: service),
          ),
        ),
  );
}

enum _PrefSection {
  audio,
  plugins,
  midi,
  remote,
  interface,
  shortcuts,
  version,
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
  _PrefSection _section = _PrefSection.audio;

  @override
  void initState() {
    super.initState();
    _reloadLists();
    _latencyCtrl = TextEditingController(
      text: widget.service.midiInputLatencyMs.toStringAsFixed(0),
    );
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

  Future<void> _apply({
    String? out,
    String? input,
    double? rate,
    int? buffer,
  }) async {
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

  String _title(AppLocalizations l10n) => switch (_section) {
    _PrefSection.audio => l10n.prefNavAudio,
    _PrefSection.plugins => l10n.prefNavPlugins,
    _PrefSection.midi => l10n.prefNavMidi,
    _PrefSection.remote => l10n.prefNavRemote,
    _PrefSection.interface => l10n.prefNavInterface,
    _PrefSection.shortcuts => l10n.prefNavShortcuts,
    _PrefSection.version => l10n.prefNavVersion,
  };

  String _intro(AppLocalizations l10n) => switch (_section) {
    _PrefSection.audio => l10n.prefAudioIntro,
    _PrefSection.plugins => l10n.prefPluginsIntro,
    _PrefSection.midi => l10n.prefMidiIntro,
    _PrefSection.remote => l10n.prefRemoteIntro,
    _PrefSection.interface => l10n.prefInterfaceIntro,
    _PrefSection.shortcuts => l10n.prefShortcutsIntro,
    _PrefSection.version => l10n.prefVersionIntro,
  };

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    return Row(
      children: [
        _SectionRail(
          selected: _section,
          onSelected: (section) => setState(() => _section = section),
        ),
        const VerticalDivider(width: 1, color: _border),
        Expanded(
          child: Column(
            children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(24, 18, 12, 16),
                child: Row(
                  children: [
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            _title(l10n),
                            style: const TextStyle(
                              fontSize: 17,
                              fontWeight: FontWeight.w600,
                            ),
                          ),
                          const SizedBox(height: 3),
                          Text(
                            _intro(l10n),
                            style: const TextStyle(fontSize: 11, color: _muted),
                          ),
                        ],
                      ),
                    ),
                    IconButton(
                      tooltip: l10n.alertClose,
                      onPressed: () => Navigator.pop(context),
                      icon: const Icon(Icons.close, size: 18),
                    ),
                  ],
                ),
              ),
              const Divider(height: 1, color: _border),
              Expanded(
                child: SingleChildScrollView(
                  padding: const EdgeInsets.fromLTRB(24, 18, 24, 24),
                  child: _sectionContent(),
                ),
              ),
              const Divider(height: 1, color: _border),
              Padding(
                padding: const EdgeInsets.fromLTRB(24, 10, 16, 10),
                child: Row(
                  children: [
                    const Spacer(),
                    TextButton(
                      onPressed: () => Navigator.pop(context),
                      child: Text(l10n.projectClose),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _sectionContent() {
    switch (_section) {
      case _PrefSection.audio:
        return _AudioSection(
          service: widget.service,
          outputs: _outputs,
          inputs: _inputs,
          rates: _rates,
          buffers: _buffers,
          onApply: _apply,
          onRescan: _rescanDevices,
        );
      case _PrefSection.plugins:
        return _PluginsSection(service: widget.service);
      case _PrefSection.midi:
        return _MidiSection(
          service: widget.service,
          midi: _midi,
          latencyCtrl: _latencyCtrl,
          onMidiSelected: (index) {
            widget.service.selectMidiInput(index);
            setState(() {});
          },
          onLatencySubmitted: (value) {
            widget.service.setMidiInputLatencyMs(value);
            setState(() {});
          },
        );
      case _PrefSection.remote:
        return _SettingsCard(
          child: RemoteControlPanel(service: widget.service),
        );
      case _PrefSection.interface:
        return _InterfaceSection(onChanged: () => setState(() {}));
      case _PrefSection.shortcuts:
        return const _ShortcutsSection();
      case _PrefSection.version:
        return const _SettingsCard(child: AppAboutContent(showName: true));
    }
  }
}

/// Rail vertical de secciones. Versión queda separada al final.
class _SectionRail extends StatelessWidget {
  const _SectionRail({required this.selected, required this.onSelected});

  final _PrefSection selected;
  final ValueChanged<_PrefSection> onSelected;

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    return Container(
      width: 190,
      color: _rail,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(18, 20, 18, 18),
            child: Row(
              children: [
                const Icon(Icons.settings_outlined, size: 17, color: _accent),
                const SizedBox(width: 8),
                Expanded(
                  child: Text(
                    l10n.prefTitle,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                      fontSize: 13.5,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ),
              ],
            ),
          ),
          _RailItem(
            icon: Icons.speaker_rounded,
            label: l10n.prefNavAudio,
            selected: selected == _PrefSection.audio,
            onTap: () => onSelected(_PrefSection.audio),
          ),
          _RailItem(
            icon: Icons.extension_outlined,
            label: l10n.prefNavPlugins,
            selected: selected == _PrefSection.plugins,
            onTap: () => onSelected(_PrefSection.plugins),
          ),
          _RailItem(
            icon: Icons.piano_outlined,
            label: l10n.prefNavMidi,
            selected: selected == _PrefSection.midi,
            onTap: () => onSelected(_PrefSection.midi),
          ),
          _RailItem(
            icon: Icons.wifi_tethering,
            label: l10n.prefNavRemote,
            selected: selected == _PrefSection.remote,
            onTap: () => onSelected(_PrefSection.remote),
          ),
          _RailItem(
            icon: Icons.palette_outlined,
            label: l10n.prefNavInterface,
            selected: selected == _PrefSection.interface,
            onTap: () => onSelected(_PrefSection.interface),
          ),
          _RailItem(
            icon: Icons.keyboard_outlined,
            label: l10n.prefNavShortcuts,
            selected: selected == _PrefSection.shortcuts,
            onTap: () => onSelected(_PrefSection.shortcuts),
          ),
          const Spacer(),
          const Divider(height: 1, color: _border),
          const SizedBox(height: 6),
          _RailItem(
            icon: Icons.info_outline,
            label: l10n.prefNavVersion,
            selected: selected == _PrefSection.version,
            onTap: () => onSelected(_PrefSection.version),
          ),
          const SizedBox(height: 8),
        ],
      ),
    );
  }
}

class _RailItem extends StatelessWidget {
  const _RailItem({
    required this.icon,
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final IconData icon;
  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 1),
      child: Material(
        color: selected ? _accent.withValues(alpha: 0.15) : Colors.transparent,
        borderRadius: BorderRadius.circular(6),
        child: InkWell(
          onTap: onTap,
          borderRadius: BorderRadius.circular(6),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 9),
            child: Row(
              children: [
                Icon(icon, size: 16, color: selected ? _accent : _muted),
                const SizedBox(width: 10),
                Expanded(
                  child: Text(
                    label,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: selected ? FontWeight.w600 : FontWeight.w500,
                      color: selected ? _text : _muted,
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _SettingsCard extends StatelessWidget {
  const _SettingsCard({required this.child, this.title});

  final Widget child;
  final String? title;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: _card,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: _border),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (title != null) ...[
            Text(
              title!,
              style: const TextStyle(
                fontSize: 12,
                fontWeight: FontWeight.w600,
                color: _text,
              ),
            ),
            const SizedBox(height: 10),
          ],
          child,
        ],
      ),
    );
  }
}

class _AudioSection extends StatelessWidget {
  const _AudioSection({
    required this.service,
    required this.outputs,
    required this.inputs,
    required this.rates,
    required this.buffers,
    required this.onApply,
    required this.onRescan,
  });

  final EngineService service;
  final List<String> outputs;
  final List<String> inputs;
  final List<double> rates;
  final List<int> buffers;
  final Future<void> Function({
    String? out,
    String? input,
    double? rate,
    int? buffer,
  })
  onApply;
  final VoidCallback onRescan;

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SettingsCard(
          title: l10n.prefSectionAudio,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _dropdown(
                l10n.prefOutputDevice,
                outputs,
                service.currentOutputDevice,
                (v) => onApply(out: v),
              ),
              const SizedBox(height: 10),
              _dropdown(
                l10n.prefInputDevice,
                inputs,
                service.currentInputDevice,
                (v) => onApply(input: v),
              ),
              const SizedBox(height: 12),
              Row(
                children: [
                  Expanded(
                    child: _dropdownNum(
                      l10n.prefSampleRate,
                      rates,
                      service.sampleRate,
                      (v) => onApply(rate: v),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: _dropdownInt(
                      l10n.prefBuffer,
                      buffers,
                      service.currentBuffer,
                      (v) => onApply(buffer: v),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Text(
                    l10n.prefRoundTrip(
                      (service.currentBuffer / service.sampleRate * 1000 * 2)
                          .toStringAsFixed(0),
                    ),
                    style: const TextStyle(fontSize: 10, color: _muted),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              Text(
                service.deviceInputChannelCount > 0
                    ? l10n.prefActiveInputs(
                      '${service.deviceInputChannelCount}',
                    )
                    : l10n.prefNoInputs,
                style: TextStyle(
                  fontSize: 11,
                  color: service.deviceInputChannelCount > 0 ? _ok : _warn,
                ),
              ),
              const SizedBox(height: 4),
              TextButton.icon(
                onPressed: onRescan,
                icon: const Icon(Icons.refresh, size: 14),
                label: Text(
                  l10n.prefRescan,
                  style: const TextStyle(fontSize: 11),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _MidiSection extends StatelessWidget {
  const _MidiSection({
    required this.service,
    required this.midi,
    required this.latencyCtrl,
    required this.onMidiSelected,
    required this.onLatencySubmitted,
  });

  final EngineService service;
  final List<String> midi;
  final TextEditingController latencyCtrl;
  final ValueChanged<int> onMidiSelected;
  final ValueChanged<double> onLatencySubmitted;

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SettingsCard(
          title: l10n.prefMidiInputTitle,
          child:
              midi.isEmpty
                  ? Padding(
                    padding: const EdgeInsets.symmetric(vertical: 8),
                    child: Text(
                      l10n.prefNoMidi,
                      style: const TextStyle(fontSize: 11, color: _faint),
                    ),
                  )
                  : Column(
                    children: [
                      for (var i = 0; i < midi.length; i++)
                        GestureDetector(
                          onTap: () => onMidiSelected(i),
                          child: Container(
                            padding: const EdgeInsets.symmetric(
                              vertical: 6,
                              horizontal: 8,
                            ),
                            decoration: BoxDecoration(
                              color:
                                  service.selectedMidiInputIndex == i
                                      ? _accent.withValues(alpha: 0.15)
                                      : Colors.transparent,
                              borderRadius: BorderRadius.circular(4),
                            ),
                            child: Row(
                              children: [
                                Icon(
                                  service.selectedMidiInputIndex == i
                                      ? Icons.radio_button_checked
                                      : Icons.radio_button_off,
                                  size: 14,
                                  color:
                                      service.selectedMidiInputIndex == i
                                          ? _accent
                                          : _muted,
                                ),
                                const SizedBox(width: 8),
                                Expanded(
                                  child: Text(
                                    midi[i],
                                    overflow: TextOverflow.ellipsis,
                                    style: const TextStyle(
                                      fontSize: 11,
                                      color: _text,
                                    ),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ),
                    ],
                  ),
        ),
        const SizedBox(height: 10),
        Text(
          l10n.prefMidiScanHint,
          style: const TextStyle(fontSize: 10, color: _faint),
        ),
        const SizedBox(height: 14),
        _SettingsCard(
          title: l10n.prefMidiLatency,
          child: _LatencyEditor(
            controller: latencyCtrl,
            help: l10n.prefMidiLatencyHelp,
            onSubmit: onLatencySubmitted,
          ),
        ),
      ],
    );
  }
}

class _LatencyEditor extends StatelessWidget {
  const _LatencyEditor({
    required this.controller,
    required this.help,
    required this.onSubmit,
  });

  final TextEditingController controller;
  final String help;
  final ValueChanged<double> onSubmit;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        SizedBox(
          width: 64,
          child: TextField(
            controller: controller,
            keyboardType: TextInputType.number,
            inputFormatters: [
              FilteringTextInputFormatter.allow(RegExp(r'-?[0-9]')),
            ],
            style: const TextStyle(fontSize: 11),
            decoration: const InputDecoration(isDense: true, counterText: ''),
            maxLength: 4,
            onSubmitted: (v) => onSubmit(double.tryParse(v) ?? 0.0),
          ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: Text(
            help,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(fontSize: 9, color: _faint),
          ),
        ),
      ],
    );
  }
}

class _PluginsSection extends StatefulWidget {
  const _PluginsSection({required this.service});

  final EngineService service;

  @override
  State<_PluginsSection> createState() => _PluginsSectionState();
}

class _PluginsSectionState extends State<_PluginsSection> {
  Timer? _poll;
  late bool _scanning;
  late List<PluginSearchDirectory> _directories;

  EngineService get _service => widget.service;

  @override
  void initState() {
    super.initState();
    _directories = defaultPluginSearchDirectories();
    if (!_service.pluginsScanned) _service.refreshPluginList();
    _scanning = _service.scanActive;
    _poll = Timer.periodic(const Duration(milliseconds: 800), (_) {
      if (!mounted) return;
      final active = _service.scanActive;
      if (active) {
        _service.refreshPluginList();
      }
      if (active != _scanning) {
        setState(() => _scanning = active);
      }
    });
  }

  @override
  void dispose() {
    _poll?.cancel();
    super.dispose();
  }

  void _scan() {
    _service.scanPlugins();
    setState(() {
      _scanning = _service.scanActive;
      _directories = defaultPluginSearchDirectories();
    });
  }

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    return AnimatedBuilder(
      animation: _service,
      builder: (context, _) {
        final plugins = _service.plugins;
        final formats = <String, int>{};
        for (final plugin in plugins) {
          formats.update(
            plugin.format,
            (count) => count + 1,
            ifAbsent: () => 1,
          );
        }

        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _SettingsCard(
              child: Row(
                children: [
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          l10n.prefPluginsStatus(plugins.length),
                          style: const TextStyle(
                            fontSize: 13,
                            fontWeight: FontWeight.w600,
                          ),
                        ),
                        const SizedBox(height: 3),
                        Text(
                          _scanning
                              ? l10n.prefPluginsScanning
                              : l10n.prefPluginsScanHint,
                          style: TextStyle(
                            fontSize: 10,
                            color: _scanning ? _accent : _muted,
                          ),
                        ),
                        if (formats.isNotEmpty) ...[
                          const SizedBox(height: 8),
                          Wrap(
                            spacing: 6,
                            runSpacing: 6,
                            children: [
                              for (final entry in formats.entries)
                                _FormatChip(
                                  label: '${entry.key} · ${entry.value}',
                                ),
                            ],
                          ),
                        ],
                      ],
                    ),
                  ),
                  const SizedBox(width: 12),
                  OutlinedButton.icon(
                    onPressed: _scanning ? null : _scan,
                    icon: const Icon(Icons.refresh, size: 14),
                    label: Text(
                      l10n.prefPluginsRescan,
                      style: const TextStyle(fontSize: 11),
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 14),
            _SettingsCard(
              title: l10n.prefPluginsIndexedDirs,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  for (final dir in _directories) _DirectoryRow(directory: dir),
                  if (_directories.isEmpty)
                    Padding(
                      padding: const EdgeInsets.symmetric(vertical: 6),
                      child: Text(
                        l10n.prefPluginsNoDirectories,
                        style: const TextStyle(fontSize: 11, color: _faint),
                      ),
                    ),
                  const SizedBox(height: 8),
                  Text(
                    l10n.prefPluginsDirsHelp,
                    style: const TextStyle(fontSize: 10, color: _faint),
                  ),
                  if (_isMacOs) ...[
                    const SizedBox(height: 6),
                    Text(
                      l10n.prefPluginsAuNote,
                      style: const TextStyle(fontSize: 10, color: _faint),
                    ),
                  ],
                ],
              ),
            ),
            if (plugins.isEmpty && !_scanning) ...[
              const SizedBox(height: 10),
              Text(
                l10n.prefPluginsEmpty,
                style: const TextStyle(fontSize: 10, color: _faint),
              ),
            ],
          ],
        );
      },
    );
  }
}

bool get _isMacOs => Platform.isMacOS;

class _FormatChip extends StatelessWidget {
  const _FormatChip({required this.label});

  final String label;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 3),
      decoration: BoxDecoration(
        color: _accent.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: _accent.withValues(alpha: 0.35)),
      ),
      child: Text(
        label,
        style: const TextStyle(
          fontSize: 10,
          fontWeight: FontWeight.w600,
          color: _accent,
        ),
      ),
    );
  }
}

class _DirectoryRow extends StatelessWidget {
  const _DirectoryRow({required this.directory});

  final PluginSearchDirectory directory;

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 5),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            margin: const EdgeInsets.only(top: 1),
            padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
            decoration: BoxDecoration(
              color: _rail,
              borderRadius: BorderRadius.circular(4),
              border: Border.all(color: _border),
            ),
            child: Text(
              directory.format,
              style: const TextStyle(
                fontSize: 9,
                fontWeight: FontWeight.w600,
                color: _muted,
              ),
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: SelectableText(
              directory.displayPath,
              style: const TextStyle(
                fontSize: 11,
                color: _text,
                fontFamily: 'monospace',
                fontFamilyFallback: ['Menlo', 'Consolas', 'monospace'],
              ),
            ),
          ),
          const SizedBox(width: 8),
          Padding(
            padding: const EdgeInsets.only(top: 1),
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
              decoration: BoxDecoration(
                color: (directory.exists ? _ok : _warn).withValues(alpha: 0.12),
                borderRadius: BorderRadius.circular(4),
              ),
              child: Text(
                directory.exists
                    ? l10n.prefPluginsDirExists
                    : l10n.prefPluginsDirMissing,
                style: TextStyle(
                  fontSize: 9,
                  fontWeight: FontWeight.w600,
                  color: directory.exists ? _ok : _warn,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

/// Editable app-wide keyboard shortcuts. The list is generated from
/// [ShortcutAction] so a newly added action cannot be forgotten here.
class _ShortcutsSection extends StatefulWidget {
  const _ShortcutsSection();

  @override
  State<_ShortcutsSection> createState() => _ShortcutsSectionState();
}

class _ShortcutsSectionState extends State<_ShortcutsSection> {
  Future<void> _edit(ShortcutAction action) async {
    final binding = await showDialog<ShortcutBinding>(
      context: context,
      builder:
          (_) => _ShortcutCaptureDialog(
            action: action,
            current: ShortcutSettings.bindingFor(action),
          ),
    );
    if (binding == null || !mounted) return;
    if (!ShortcutSettings.setBinding(action, binding) && mounted) {
      final conflict = ShortcutSettings.conflictFor(
        binding,
        excluding: action,
      );
      if (conflict != null) {
        showAppError(
          context,
          context.l10n.prefShortcutConflict(
            _shortcutLabel(context.l10n, conflict),
          ),
        );
      }
      return;
    }
    setState(() {});
  }

  void _reset(ShortcutAction action) {
    final conflict = ShortcutSettings.reset(action);
    if (!mounted) return;
    if (conflict != null) {
      showAppError(
        context,
        context.l10n.prefShortcutConflict(
          _shortcutLabel(context.l10n, conflict),
        ),
      );
      return;
    }
    setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    final actions = ShortcutAction.values;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SettingsCard(
          title: l10n.prefShortcutsTitle,
          child: Column(
            children: [
              for (var i = 0; i < actions.length; i++) ...[
                _ShortcutRow(
                  label: _shortcutLabel(l10n, actions[i]),
                  binding: ShortcutSettings.bindingFor(actions[i]),
                  isDefault: ShortcutSettings.isDefault(actions[i]),
                  onEdit: () => _edit(actions[i]),
                  onReset: () => _reset(actions[i]),
                ),
                if (i != actions.length - 1)
                  const Divider(height: 1, color: _border),
              ],
            ],
          ),
        ),
        const SizedBox(height: 12),
        Row(
          children: [
            Expanded(
              child: Text(
                l10n.prefShortcutsHint,
                style: const TextStyle(fontSize: 10, color: _faint),
              ),
            ),
            const SizedBox(width: 12),
            TextButton.icon(
              onPressed: () {
                ShortcutSettings.resetAll();
                setState(() {});
              },
              icon: const Icon(Icons.restart_alt, size: 15),
              label: Text(l10n.prefShortcutResetAll),
            ),
          ],
        ),
      ],
    );
  }
}

class _ShortcutRow extends StatelessWidget {
  const _ShortcutRow({
    required this.label,
    required this.binding,
    required this.isDefault,
    required this.onEdit,
    required this.onReset,
  });

  final String label;
  final ShortcutBinding binding;
  final bool isDefault;
  final VoidCallback onEdit;
  final VoidCallback onReset;

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    return InkWell(
      onTap: onEdit,
      borderRadius: BorderRadius.circular(6),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 2, vertical: 5),
        child: Row(
          children: [
            Expanded(
              child: Text(
                label,
                style: const TextStyle(fontSize: 12, color: _text),
              ),
            ),
            const SizedBox(width: 12),
            Tooltip(
              message: l10n.prefShortcutEdit,
              child: Container(
                padding: const EdgeInsets.symmetric(
                  horizontal: 9,
                  vertical: 5,
                ),
                decoration: BoxDecoration(
                  color: _rail,
                  borderRadius: BorderRadius.circular(5),
                  border: Border.all(color: _border),
                ),
                child: Text(
                  _bindingLabel(l10n, binding),
                  style: const TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.w600,
                    color: _text,
                  ),
                ),
              ),
            ),
            const SizedBox(width: 4),
            IconButton(
              tooltip: l10n.prefShortcutReset,
              onPressed: isDefault ? null : onReset,
              icon: const Icon(Icons.restart_alt, size: 16),
              visualDensity: VisualDensity.compact,
              color: _muted,
              disabledColor: _faint.withValues(alpha: 0.45),
            ),
          ],
        ),
      ),
    );
  }
}

class _ShortcutCaptureDialog extends StatefulWidget {
  const _ShortcutCaptureDialog({
    required this.action,
    required this.current,
  });

  final ShortcutAction action;
  final ShortcutBinding current;

  @override
  State<_ShortcutCaptureDialog> createState() => _ShortcutCaptureDialogState();
}

class _ShortcutCaptureDialogState extends State<_ShortcutCaptureDialog> {
  String? _error;
  ShortcutBinding? _preview;

  KeyEventResult _onKeyEvent(FocusNode node, KeyEvent event) {
    if (event is KeyRepeatEvent) return KeyEventResult.handled;
    if (event is! KeyDownEvent) return KeyEventResult.handled;
    if (isShortcutModifierKey(event.logicalKey)) {
      return KeyEventResult.handled;
    }

    final binding = ShortcutBinding.fromKeyboard(event.logicalKey);
    final conflict = ShortcutSettings.conflictFor(
      binding,
      excluding: widget.action,
    );
    if (conflict != null) {
      final l10n = context.l10n;
      setState(() {
        _preview = binding;
        _error = l10n.prefShortcutConflict(
          _shortcutLabel(l10n, conflict),
        );
      });
      return KeyEventResult.handled;
    }

    Navigator.of(context).pop(binding);
    return KeyEventResult.handled;
  }

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    final shown = _preview ?? widget.current;
    return AlertDialog(
      backgroundColor: _panel,
      title: Text(l10n.prefShortcutDialogTitle),
      content: Focus(
        autofocus: true,
        onKeyEvent: _onKeyEvent,
        child: SizedBox(
          width: 360,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                _shortcutLabel(l10n, widget.action),
                style: const TextStyle(fontSize: 12, color: _muted),
              ),
              const SizedBox(height: 12),
              Container(
                width: double.infinity,
                padding: const EdgeInsets.symmetric(
                  horizontal: 14,
                  vertical: 18,
                ),
                decoration: BoxDecoration(
                  color: _card,
                  borderRadius: BorderRadius.circular(7),
                  border: Border.all(color: _border),
                ),
                child: Text(
                  _preview == null
                      ? l10n.prefShortcutPressKeys
                      : _bindingLabel(l10n, shown),
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    fontSize: _preview == null ? 12 : 16,
                    fontWeight:
                        _preview == null ? FontWeight.w400 : FontWeight.w600,
                    color: _preview == null ? _muted : _accent,
                  ),
                ),
              ),
              if (_error != null) ...[
                const SizedBox(height: 10),
                Text(
                  _error!,
                  style: const TextStyle(fontSize: 10, color: Color(0xFFE5484D)),
                ),
              ],
            ],
          ),
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: Text(l10n.projectCancel),
        ),
      ],
    );
  }
}

String _shortcutLabel(AppLocalizations l10n, ShortcutAction action) =>
    switch (action) {
      ShortcutAction.saveProject => l10n.prefShortcutSaveProject,
      ShortcutAction.openProject => l10n.prefShortcutOpenProject,
      ShortcutAction.newProject => l10n.prefShortcutNewProject,
      ShortcutAction.copyClip => l10n.prefShortcutCopyClip,
      ShortcutAction.pasteClip => l10n.prefShortcutPasteClip,
      ShortcutAction.playPause => l10n.prefShortcutPlayPause,
      ShortcutAction.record => l10n.prefShortcutRecord,
      ShortcutAction.selectAllClips => l10n.prefShortcutSelectAllClips,
      ShortcutAction.clearSelection => l10n.prefShortcutClearSelection,
      ShortcutAction.deleteSelection => l10n.prefShortcutDeleteSelection,
      ShortcutAction.splitClip => l10n.prefShortcutSplitClip,
    };

String _bindingLabel(AppLocalizations l10n, ShortcutBinding binding) {
  final isMac = Platform.isMacOS;
  final parts = <String>[];
  if (isMac) {
    if (binding.control) parts.add('⌃');
    if (binding.alt) parts.add('⌥');
    if (binding.shift) parts.add('⇧');
    if (binding.primary) parts.add('⌘');
  } else {
    if (binding.primary || binding.control) parts.add('Ctrl');
    if (binding.alt) parts.add('Alt');
    if (binding.shift) parts.add('Shift');
  }
  parts.add(_keyLabel(l10n, binding.key));
  return isMac ? parts.join() : parts.join('+');
}

String _keyLabel(AppLocalizations l10n, LogicalKeyboardKey key) {
  if (key == LogicalKeyboardKey.space) return l10n.prefShortcutKeySpace;
  if (key == LogicalKeyboardKey.delete ||
      key == LogicalKeyboardKey.backspace) {
    return l10n.prefShortcutKeyDelete;
  }
  if (key == LogicalKeyboardKey.escape) return 'Esc';
  if (key == LogicalKeyboardKey.enter ||
      key == LogicalKeyboardKey.numpadEnter) {
    return '⏎';
  }
  final label = key.keyLabel;
  if (label.isEmpty) return '?';
  return label.length == 1 ? label.toUpperCase() : label;
}

class _InterfaceSection extends StatelessWidget {
  const _InterfaceSection({required this.onChanged});

  final VoidCallback onChanged;

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        _SettingsCard(
          title: l10n.prefSectionLanguage,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
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
              const SizedBox(height: 8),
              Text(
                l10n.prefLanguageHelp,
                style: const TextStyle(fontSize: 10, color: _faint),
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        _SettingsCard(
          title: l10n.prefSectionDebug,
          child: SwitchListTile(
            contentPadding: EdgeInsets.zero,
            dense: true,
            value: DebugSettings.enabled,
            onChanged: (value) {
              DebugSettings.save(enabled: value);
              DebugLog.configure();
              onChanged();
            },
            title: Text(
              l10n.prefDebugMode,
              style: const TextStyle(fontSize: 12, color: _text),
            ),
            subtitle: Text(
              l10n.prefDebugModeHelp,
              style: const TextStyle(fontSize: 10, color: _muted),
            ),
          ),
        ),
      ],
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
        color: selected ? _accent : _card,
        borderRadius: BorderRadius.circular(5),
        border: Border.all(color: _border),
      ),
      child: Text(
        label,
        style: TextStyle(
          fontSize: 11,
          fontWeight: FontWeight.w600,
          color: selected ? Colors.black : _text,
        ),
      ),
    ),
  );
}

Widget _dropdown(
  String label,
  List<String> rawItems,
  String value,
  void Function(String) onChanged,
) {
  // The dropdown asserts value must appear exactly once in items: drop
  // empties and duplicates, and never hand it a value that is not listed.
  final items = rawItems.where((e) => e.isNotEmpty).toSet().toList()..sort();
  final current = value.isNotEmpty && items.contains(value) ? value : null;
  return Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text(label, style: const TextStyle(fontSize: 12, color: _muted)),
      DropdownButtonFormField<String>(
        // ignore: deprecated_member_use
        value: current,
        isDense: true,
        isExpanded: true,
        items: [
          for (final item in items)
            DropdownMenuItem(
              value: item,
              child: Text(item, style: const TextStyle(fontSize: 12)),
            ),
        ],
        onChanged: (v) => v == null ? null : onChanged(v),
      ),
    ],
  );
}

Widget _dropdownNum(
  String label,
  List<double> rawItems,
  double value,
  void Function(double) onChanged,
) {
  final items = rawItems.toSet().toList()..sort();
  return Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text(label, style: const TextStyle(fontSize: 12, color: _muted)),
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
              child: Text(
                '${item.toStringAsFixed(0)} Hz',
                style: const TextStyle(fontSize: 12),
              ),
            ),
        ],
        onChanged: (v) => v == null ? null : onChanged(v),
      ),
    ],
  );
}

Widget _dropdownInt(
  String label,
  List<int> rawItems,
  int value,
  void Function(int) onChanged,
) {
  final items = rawItems.toSet().toList()..sort();
  return Column(
    crossAxisAlignment: CrossAxisAlignment.start,
    children: [
      Text(label, style: const TextStyle(fontSize: 12, color: _muted)),
      DropdownButtonFormField<int>(
        // ignore: deprecated_member_use
        value: items.contains(value) ? value : null,
        items: [
          for (final item in items)
            DropdownMenuItem(
              value: item,
              child: Text('$item', style: const TextStyle(fontSize: 12)),
            ),
        ],
        onChanged: (v) => v == null ? null : onChanged(v),
      ),
    ],
  );
}
