// Export dialog (§7.13 v1): offline render to WAV at the project sample
// rate — 16 or 24 bits, faster than realtime (not a live bounce). MP3
// needs an external LAME encoder; planned for a later version.

import 'dart:io';

import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import '../l10n/l10n.dart';
import 'app_alert.dart';

const String kExportsDir = '/Users/ivan/Music/EZMusicProduction/Exports';

Future<void> showExportDialog(BuildContext context, EngineService service) {
  final projectName = service.lastProjectPath != null
      ? File(service.lastProjectPath!)
          .uri
          .pathSegments
          .last
          .replaceAll('.ezmpp', '')
      : 'export';
  return showDialog<void>(
    context: context,
    builder: (context) => _ExportBody(service: service, defaultName: projectName),
  );
}

class _ExportBody extends StatefulWidget {
  const _ExportBody({required this.service, required this.defaultName});

  final EngineService service;
  final String defaultName;

  @override
  State<_ExportBody> createState() => _ExportBodyState();
}

class _ExportBodyState extends State<_ExportBody> {
  late final TextEditingController _name =
      TextEditingController(text: widget.defaultName);
  int _bits = 24;
  bool _exporting = false;
  String? _donePath;

  Future<void> _export() async {
    final safe = _name.text.trim().replaceAll(RegExp(r'[/:\\]'), '-');
    if (safe.isEmpty) return;
    final dir = Directory(kExportsDir);
    if (!dir.existsSync()) dir.createSync(recursive: true);
    final path = '${dir.path}/$safe.wav';
    setState(() => _exporting = true);
    // Let the dialog paint the "exporting" state before the blocking render.
    await Future<void>.delayed(Duration.zero);
    final error = widget.service.exportWav(path, bitsPerSample: _bits);
    if (!mounted) return;
    if (error != null) {
      setState(() => _exporting = false);
      showAppError(context, error);
      return;
    }
    setState(() {
      _exporting = false;
      _donePath = path;
    });
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      backgroundColor: const Color(0xFF252830),
      title: Text(context.l10n.exportTitle,
          style: const TextStyle(fontSize: 15)),
      content: SizedBox(
        width: 340,
        child: _donePath != null
            ? Column(mainAxisSize: MainAxisSize.min, children: [
                const Icon(Icons.check_circle,
                    size: 36, color: Color(0xFF3DD68C)),
                const SizedBox(height: 8),
                Text(context.l10n.exportExported,
                    style: const TextStyle(
                        fontSize: 13, fontWeight: FontWeight.w600)),
                const SizedBox(height: 4),
                Text(_donePath!,
                    style: const TextStyle(
                        fontSize: 10, color: Color(0xFF9BA1AE))),
                const SizedBox(height: 8),
                OutlinedButton(
                  onPressed: () =>
                      Process.run('open', [kExportsDir]),
                  child: Text(context.l10n.exportOpenFolder),
                ),
              ])
            : Column(mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                TextField(
                  controller: _name,
                  autofocus: true,
                  decoration:
                      InputDecoration(labelText: context.l10n.exportFileName),
                ),
                const SizedBox(height: 10),
                DropdownButton<int>(
                  value: _bits,
                  isExpanded: true,
                  dropdownColor: const Color(0xFF252830),
                  items: [
                    DropdownMenuItem(
                        value: 16, child: Text(context.l10n.exportWav16)),
                    DropdownMenuItem(
                        value: 24, child: Text(context.l10n.exportWav24)),
                  ],
                  onChanged: (v) => setState(() => _bits = v ?? 24),
                ),
                const SizedBox(height: 6),
                Text(
                  context.l10n.exportRateInfo(
                      widget.service.sampleRate.toStringAsFixed(0)),
                  style:
                      const TextStyle(fontSize: 10, color: Color(0xFF9BA1AE)),
                ),
                Text(
                  context.l10n.exportMp3Soon,
                  style:
                      const TextStyle(fontSize: 10, color: Color(0xFF5C6270)),
                ),
              ]),
      ),
      actions: _donePath != null
          ? [
              TextButton(
                  onPressed: () => Navigator.pop(context),
                  child: Text(context.l10n.exportClose)),
            ]
          : [
              TextButton(
                  onPressed: _exporting ? null : () => Navigator.pop(context),
                  child: Text(context.l10n.exportCancel)),
              TextButton(
                  onPressed: _exporting ? null : _export,
                  child: Text(_exporting
                      ? context.l10n.exportExporting
                      : context.l10n.exportTitle)),
            ],
    );
  }
}
