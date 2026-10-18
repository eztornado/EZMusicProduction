// Plugin picker (§7.7): searchable list of scanned VST3/AU plugins.
// `onlyInstruments` filters for instrument plugins (pistas instrumento);
// `onlyEffects` filters them out (inserts de pista y master).
//
// The engine scans CHUNKED on its own message thread, so the list fills in
// progressively: this dialog polls scanActive and refreshes while it runs.

import 'dart:async';

import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import '../l10n/l10n.dart';

Future<PluginInfo?> showPluginPickerDialog(
    BuildContext context, EngineService service,
    {bool onlyInstruments = false,
    bool onlyEffects = false,
    String? title}) async {
  return showDialog<PluginInfo>(
    context: context,
    builder: (context) => Dialog(
      backgroundColor: const Color(0xFF252830),
      child: SizedBox(
        width: 460,
        height: 480,
        child: _PickerBody(
            service: service,
            onlyInstruments: onlyInstruments,
            onlyEffects: onlyEffects,
            title: title),
      ),
    ),
  );
}

class _PickerBody extends StatefulWidget {
  const _PickerBody({
    required this.service,
    required this.onlyInstruments,
    required this.onlyEffects,
    required this.title,
  });

  final EngineService service;
  final bool onlyInstruments;
  final bool onlyEffects;
  final String? title;

  @override
  State<_PickerBody> createState() => _PickerBodyState();
}

class _PickerBodyState extends State<_PickerBody> {
  String _query = '';
  Timer? _poll;

  @override
  void initState() {
    super.initState();
    // Pull the cached list first; only kick a (minutes-long, background)
    // scan when there is nothing cached yet. While a scan runs, poll so
    // the list fills in progressively.
    if (!widget.service.pluginsScanned) {
      widget.service.refreshPluginList();
      if (widget.service.plugins.isEmpty) {
        Future.microtask(() => widget.service.scanPlugins());
      }
    }
    _poll = Timer.periodic(const Duration(milliseconds: 800), (_) {
      if (!mounted) return;
      if (widget.service.scanActive) {
        widget.service.refreshPluginList();
      } else if (widget.service.pluginsScanned) {
        _poll?.cancel();
      }
    });
  }

  @override
  void dispose() {
    _poll?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
        animation: widget.service,
        builder: (context, _) {
          final all = widget.service.plugins
              .where((p) => !widget.onlyInstruments || p.isInstrument)
              .where((p) => !widget.onlyEffects || !p.isInstrument)
              .where((p) =>
                  _query.isEmpty ||
                  p.name.toLowerCase().contains(_query.toLowerCase()))
              .toList()
            ..sort((a, b) =>
                a.name.toLowerCase().compareTo(b.name.toLowerCase()));

          return Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(widget.title ?? context.l10n.pluginAddTitle,
                    style: const TextStyle(
                        fontSize: 15, fontWeight: FontWeight.w600)),
                const SizedBox(height: 12),
                TextField(
                  autofocus: true,
                  decoration: InputDecoration(
                    hintText: context.l10n.pluginSearchHint,
                    prefixIcon: const Icon(Icons.search, size: 18),
                  ),
                  onChanged: (v) => setState(() => _query = v),
                ),
                const SizedBox(height: 8),
                Expanded(
                  child: all.isEmpty
                      ? Center(
                          child: Text(
                          widget.service.scanActive
                              ? context.l10n.pluginScanning
                              : context.l10n.pluginNoResults,
                          style: const TextStyle(color: Color(0xFF9BA1AE)),
                        ))
                      : ListView.builder(
                          itemCount: all.length,
                          itemBuilder: (context, i) {
                            final p = all[i];
                            return ListTile(
                              dense: true,
                              leading: Icon(
                                p.isInstrument
                                    ? Icons.piano
                                    : Icons.graphic_eq,
                                size: 18,
                                color: const Color(0xFF4D9DF7),
                              ),
                              title: Text(p.name,
                                  style: const TextStyle(fontSize: 12)),
                              subtitle: Text(
                                  '${p.format} · '
                                  '${p.isInstrument ? context.l10n.pluginKindInstrument : context.l10n.pluginKindEffect}',
                                  style: const TextStyle(
                                      fontSize: 10,
                                      color: Color(0xFF9BA1AE))),
                              onTap: () => Navigator.pop(context, p),
                            );
                          },
                        ),
                ),
              ],
            ),
          );
        });
  }
}
