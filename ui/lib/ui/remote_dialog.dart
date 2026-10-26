// Diálogo rápido de control remoto (barra superior).
import 'package:flutter/material.dart';

import '../engine/engine_service.dart';
import '../l10n/l10n.dart';
import 'remote_control_panel.dart';

Future<void> showRemoteControlDialog(
  BuildContext context,
  EngineService service,
) {
  return showDialog<void>(
    context: context,
    builder: (dialogContext) => Dialog(
      backgroundColor: const Color(0xFF252830),
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxHeight: 560),
        child: SizedBox(
          width: 460,
          child: SingleChildScrollView(
            child: Padding(
              padding: const EdgeInsets.all(20),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const Icon(
                        Icons.wifi_tethering,
                        size: 18,
                        color: Color(0xFF4D9DF7),
                      ),
                      const SizedBox(width: 8),
                      Text(
                        dialogContext.l10n.prefSectionRemote,
                        style: const TextStyle(
                          fontSize: 15,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 14),
                  RemoteControlPanel(service: service),
                  const SizedBox(height: 16),
                  Align(
                    alignment: Alignment.bottomRight,
                    child: TextButton(
                      onPressed: () => Navigator.pop(dialogContext),
                      child: Text(dialogContext.l10n.alertClose),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    ),
  );
}
