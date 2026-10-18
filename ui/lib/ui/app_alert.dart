// Avisos y errores de la interfaz como CUADROS DE ALERTA modales (estilo DAW),
// en vez de texto suelto en la barra superior o SnackBars mal colocados.
//
// Uso:
//     final error = service.saveProject(path);
//     if (error != null) showAppError(context, error);
//
// Los SnackBar se reservan para confirmaciones breves ("Clip copiado") que no
// interrumpen el trabajo.
import 'dart:collection';

import 'package:flutter/material.dart';

import '../l10n/l10n.dart';

/// Tono del alerta: cambia icono, color y titulo por defecto.
enum AppAlertKind { info, warning, error }

/// Muestra un cuadro de alerta. Si ya hay uno abierto, se encola y aparece al
/// cerrar el anterior (asi dos fallos seguidos no apilan dialogos).
Future<void> showAppAlert(
  BuildContext context, {
  required String message,
  String? title,
  AppAlertKind kind = AppAlertKind.error,
  String? detail,
}) {
  return _AlertQueue.enqueue(
    context,
    _AlertRequest(message: message, title: title, kind: kind, detail: detail),
  );
}

/// Atajo para errores: `showAppError(context, errorText)`.
Future<void> showAppError(BuildContext context, String message,
    {String? detail}) {
  return showAppAlert(context,
      message: message, kind: AppAlertKind.error, detail: detail);
}

class _AlertRequest {
  const _AlertRequest({
    required this.message,
    required this.kind,
    this.title,
    this.detail,
  });

  final String message;
  final AppAlertKind kind;
  final String? title;
  final String? detail;
}

class _AlertQueue {
  static final Queue<_AlertRequest> _pending = Queue<_AlertRequest>();
  static bool _showing = false;

  static Future<void> enqueue(BuildContext context, _AlertRequest req) async {
    _pending.add(req);
    if (_showing) return;
    _showing = true;
    try {
      while (_pending.isNotEmpty) {
        final next = _pending.removeFirst();
        if (!context.mounted) continue;
        await showDialog<void>(
          context: context,
          barrierDismissible: true,
          barrierColor: const Color(0x99000000),
          builder: (_) => _AppAlertDialog(request: next),
        );
      }
    } finally {
      _showing = false;
    }
  }
}

/// El cuadro en si: icono + titulo + mensaje (+ detalles tecnicos plegables).
class _AppAlertDialog extends StatefulWidget {
  const _AppAlertDialog({required this.request});

  final _AlertRequest request;

  @override
  State<_AppAlertDialog> createState() => _AppAlertDialogState();
}

class _AppAlertDialogState extends State<_AppAlertDialog> {
  bool _showDetail = false;

  ({Color color, IconData icon, String Function() title}) get _style {
    switch (widget.request.kind) {
      case AppAlertKind.error:
        return (
          color: const Color(0xFFE5484D),
          icon: Icons.error_outline,
          title: () => context.l10n.alertTitleError,
        );
      case AppAlertKind.warning:
        return (
          color: const Color(0xFFF5A524),
          icon: Icons.warning_amber_rounded,
          title: () => context.l10n.alertTitleWarning,
        );
      case AppAlertKind.info:
        return (
          color: const Color(0xFF4D9DF7),
          icon: Icons.info_outline,
          title: () => context.l10n.alertTitleInfo,
        );
    }
  }

  @override
  Widget build(BuildContext context) {
    final style = _style;
    final title = widget.request.title ?? style.title();
    final detail = widget.request.detail;

    return Dialog(
      backgroundColor: const Color(0xFF1E2026),
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(8),
        side: const BorderSide(color: Color(0xFF32363F)),
      ),
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 520),
        child: Padding(
          padding: const EdgeInsets.fromLTRB(20, 18, 20, 14),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Icon(style.icon, color: style.color, size: 22),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Padding(
                      padding: const EdgeInsets.only(top: 1),
                      child: Text(
                        title,
                        style: TextStyle(
                          fontSize: 15,
                          fontWeight: FontWeight.w700,
                          color: style.color,
                        ),
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              SelectableText(
                widget.request.message,
                style: const TextStyle(
                    fontSize: 13, height: 1.35, color: Color(0xFFE9EBF0)),
              ),
              if (detail != null && detail.trim().isNotEmpty) ...[
                const SizedBox(height: 10),
                InkWell(
                  onTap: () => setState(() => _showDetail = !_showDetail),
                  child: Padding(
                    padding: const EdgeInsets.symmetric(vertical: 4),
                    child: Row(
                      children: [
                        Icon(
                          _showDetail
                              ? Icons.keyboard_arrow_down
                              : Icons.keyboard_arrow_right,
                          size: 16,
                          color: const Color(0xFF9BA1AE),
                        ),
                        const SizedBox(width: 4),
                        Text(
                          context.l10n.alertTechnicalDetail,
                          style: const TextStyle(
                              fontSize: 11, color: Color(0xFF9BA1AE)),
                        ),
                      ],
                    ),
                  ),
                ),
                if (_showDetail)
                  Container(
                    width: double.infinity,
                    margin: const EdgeInsets.only(top: 4),
                    padding: const EdgeInsets.all(8),
                    decoration: BoxDecoration(
                      color: const Color(0xFF16171B),
                      borderRadius: BorderRadius.circular(4),
                      border: Border.all(color: const Color(0xFF32363F)),
                    ),
                    child: SelectableText(
                      detail,
                      style: const TextStyle(
                        fontSize: 11,
                        height: 1.3,
                        fontFamily: 'Menlo',
                        color: Color(0xFF9BA1AE),
                      ),
                    ),
                  ),
              ],
              const SizedBox(height: 16),
              Align(
                alignment: Alignment.centerRight,
                child: OutlinedButton(
                  autofocus: true,
                  onPressed: () => Navigator.of(context).pop(),
                  child: Text(context.l10n.alertClose),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
