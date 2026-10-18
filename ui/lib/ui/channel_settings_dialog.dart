// Channel settings (§7.8 v1): the per-channel "deep" view. Shows the
// instrument slot (instrument tracks) and the FULL insert chain — every
// slot with bypass / plugin editor / remove — so a channel manages any
// number of plugins comfortably, unlike the compact mixer chips.
// EQ curve and sends land in later versions per the doc.

import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import '../engine/project_snapshot.dart' show TrackInfo;
import '../l10n/l10n.dart';
import 'plugin_picker_dialog.dart' show showPluginPickerDialog;

Future<void> showChannelSettingsDialog(
    BuildContext context, EngineService service, int trackId) {
  return showDialog<void>(
    context: context,
    builder: (context) => Dialog(
      backgroundColor: const Color(0xFF252830),
      child: SizedBox(
        width: 380,
        child: ChannelSettingsBody(service: service, trackId: trackId),
      ),
    ),
  );
}

class ChannelSettingsBody extends StatefulWidget {
  const ChannelSettingsBody(
      {super.key, required this.service, required this.trackId});

  final EngineService service;
  final int trackId;

  @override
  State<ChannelSettingsBody> createState() => _ChannelSettingsBodyState();
}

class _ChannelSettingsBodyState extends State<ChannelSettingsBody> {
  void _rebuild() => widget.service.refresh();

  @override
  Widget build(BuildContext context) {
    TrackInfo? found;
    for (final t in widget.service.snapshot.tracks) {
      if (t.id == widget.trackId) {
        found = t;
        break;
      }
    }
    if (found == null) {
      return Padding(
        padding: const EdgeInsets.all(20),
        child: Text(context.l10n.channelUnavailable,
            style: const TextStyle(color: Color(0xFF9BA1AE))),
      );
    }
    final track = found;

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(children: [
            const Icon(Icons.tune, size: 15, color: Color(0xFF4D9DF7)),
            const SizedBox(width: 6),
            Expanded(
              child: Text(context.l10n.channelTitle(track.name),
                  style: const TextStyle(
                      fontSize: 14, fontWeight: FontWeight.w600)),
            ),
            IconButton(
              visualDensity: VisualDensity.compact,
              icon:
                  const Icon(Icons.close, size: 16, color: Color(0xFF9BA1AE)),
              onPressed: () => Navigator.pop(context),
            ),
          ]),
          const SizedBox(height: 10),

          if (track.type == 1) ...[
            Text(context.l10n.channelInstrumentSection,
                style: const TextStyle(
                    fontSize: 9,
                    fontWeight: FontWeight.w600,
                    color: Color(0xFF5C6270))),
            const SizedBox(height: 4),
            _SlotRow(
              indexLabel: 'I',
              name: track.instrumentName.isEmpty
                  ? context.l10n.channelInternalSynth
                  : track.instrumentName,
              bypassed: false,
              onToggleBypass: null,
              onEdit: track.instrumentName.isEmpty
                  ? null
                  : () => widget.service.openTrackInstrumentEditor(track.id),
              onRemove: track.instrumentName.isEmpty
                  ? null
                  : () {
                      widget.service.clearTrackInstrument(track.id);
                      _rebuild();
                    },
            ),
            const SizedBox(height: 10),
          ],

          Text(context.l10n.channelInsertsSection,
              style: const TextStyle(
                  fontSize: 9,
                  fontWeight: FontWeight.w600,
                  color: Color(0xFF5C6270))),
          const SizedBox(height: 4),
          for (var i = 0; i < track.fxNames.length; i++)
            Padding(
              padding: const EdgeInsets.only(bottom: 4),
              child: _SlotRow(
                indexLabel: '${i + 1}',
                name: track.fxNames[i],
                bypassed: i < track.fxBypass.length && track.fxBypass[i],
                onToggleBypass: () {
                  widget.service.toggleTrackFxBypass(track.id, i);
                  _rebuild();
                },
                onEdit: () =>
                    widget.service.openTrackPluginEditor(track.id, i),
                onRemove: () {
                  widget.service.removeTrackFx(track.id, i);
                  _rebuild();
                },
              ),
            ),
          const SizedBox(height: 4),
          GestureDetector(
            onTap: () async {
              final picked = await showPluginPickerDialog(context, widget.service,
                  onlyEffects: true, title: context.l10n.channelFxOf(track.name));
              if (picked != null) {
                // Waits for the engine to apply the add, then opens the NEW
                // slot (the dialog-captured count would be stale).
                await widget.service
                    .addTrackFxAndOpen(track.id, picked.index);
                _rebuild();
              }
            },
            child: Container(
              width: double.infinity,
              padding: const EdgeInsets.symmetric(vertical: 6),
              decoration: BoxDecoration(
                color: const Color(0xFF4D9DF7).withValues(alpha: 0.12),
                borderRadius: BorderRadius.circular(4),
                border: Border.all(color: const Color(0xFF4D9DF7), width: 1),
              ),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(Icons.add, size: 14, color: Color(0xFF4D9DF7)),
                  const SizedBox(width: 4),
                  Text(context.l10n.channelAddFx,
                      style: const TextStyle(
                          fontSize: 11,
                          fontWeight: FontWeight.w600,
                          color: Color(0xFF4D9DF7))),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _SlotRow extends StatelessWidget {
  const _SlotRow({
    required this.indexLabel,
    required this.name,
    required this.bypassed,
    this.onToggleBypass,
    this.onEdit,
    this.onRemove,
  });

  final String indexLabel;
  final String name;
  final bool bypassed;
  final VoidCallback? onToggleBypass;
  final VoidCallback? onEdit;
  final VoidCallback? onRemove;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: const Color(0xFF2C303A),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: const Color(0xFF3E4350)),
      ),
      child: Row(children: [
        SizedBox(
          width: 16,
          child: Text(indexLabel,
              style: const TextStyle(
                  fontSize: 10,
                  fontWeight: FontWeight.w600,
                  color: Color(0xFF5C6270))),
        ),
        Expanded(
          child: Text(name,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                  fontSize: 12,
                  color: bypassed ? Colors.white24 : const Color(0xFFE9EBF0),
                  decoration:
                      bypassed ? TextDecoration.lineThrough : null)),
        ),
        IconButton(
          visualDensity: VisualDensity.compact,
          tooltip: context.l10n.channelBypass,
          icon: Icon(Icons.power_settings_new,
              size: 15,
              color: bypassed ? const Color(0xFF5C6270) : const Color(0xFF3DD68C)),
          onPressed: onToggleBypass,
        ),
        IconButton(
          visualDensity: VisualDensity.compact,
          tooltip: context.l10n.channelEditPlugin,
          icon: const Icon(Icons.settings, size: 15, color: Color(0xFF9BA1AE)),
          onPressed: onEdit,
        ),
        IconButton(
          visualDensity: VisualDensity.compact,
          tooltip: context.l10n.channelRemove,
          icon: const Icon(Icons.close, size: 15, color: Color(0xFF9BA1AE)),
          onPressed: onRemove,
        ),
      ]),
    );
  }
}
