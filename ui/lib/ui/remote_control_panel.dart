// Panel reutilizable de control remoto por LAN.
//
// Se usa en Preferencias y en el diálogo rápido de la barra superior. Mantiene
// su propio temporizador para refrescar el número de clientes conectados.

import 'dart:async';
import 'dart:io';
import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../engine/engine_service.dart';
import '../l10n/l10n.dart';
import '../remote/remote_settings.dart';

class RemoteControlPanel extends StatefulWidget {
  const RemoteControlPanel({super.key, required this.service});

  final EngineService service;

  @override
  State<RemoteControlPanel> createState() => _RemoteControlPanelState();
}

class _RemoteControlPanelState extends State<RemoteControlPanel> {
  late final TextEditingController _portCtrl;
  Timer? _timer;
  String _code = '';
  int _clients = 0;
  String? _error;
  String _host = '';

  EngineService get _service => widget.service;

  @override
  void initState() {
    super.initState();
    _portCtrl = TextEditingController(text: RemoteSettings.port.toString());
    _code = _service.remoteServerToken;
    _clients = _service.remoteServerClientCount;
    _loadHost();
    _timer = Timer.periodic(const Duration(seconds: 1), (_) {
      if (!mounted) return;
      final clients = _service.remoteServerClientCount;
      final code = _service.remoteServerToken;
      if (clients != _clients || code != _code) {
        setState(() {
          _clients = clients;
          _code = code;
        });
      }
    });
  }

  @override
  void dispose() {
    _timer?.cancel();
    _portCtrl.dispose();
    super.dispose();
  }

  Future<void> _loadHost() async {
    try {
      final interfaces = await NetworkInterface.list(
        type: InternetAddressType.IPv4,
        includeLoopback: false,
      );
      for (final interface in interfaces) {
        for (final address in interface.addresses) {
          if (!address.isLoopback && address.type == InternetAddressType.IPv4) {
            if (!mounted) return;
            setState(() => _host = address.address);
            return;
          }
        }
      }
    } catch (_) {
      // Sin red: la UI muestra un marcador en vez de la IP.
    }
  }

  void _setEnabled(bool enabled) {
    if (!enabled) {
      _service.stopRemoteServer();
      RemoteSettings.save(enabled: false);
      setState(() => _error = null);
      return;
    }
    final port = int.tryParse(_portCtrl.text.trim());
    if (port == null || port < 1 || port > 65535) {
      setState(() => _error = context.l10n.prefRemoteError('1-65535'));
      return;
    }
    final error = _service.startRemoteServer(
      port: port,
      token: RemoteSettings.token,
    );
    if (error != null) {
      setState(() => _error = error);
      return;
    }
    RemoteSettings.save(
      enabled: true,
      port: port,
      token: _service.remoteServerToken,
    );
    setState(() {
      _error = null;
      _code = _service.remoteServerToken;
    });
  }

  void _applyPort() {
    final port = int.tryParse(_portCtrl.text.trim());
    if (port == null || port < 1 || port > 65535) {
      setState(() => _error = context.l10n.prefRemoteError('1-65535'));
      return;
    }
    RemoteSettings.save(port: port);
    if (!_service.remoteServerRunning) {
      setState(() => _error = null);
      return;
    }
    final error = _service.startRemoteServer(
      port: port,
      token: RemoteSettings.token,
    );
    setState(() => _error = error);
  }

  void _newCode() {
    final code = (Random.secure().nextInt(900000) + 100000).toString();
    RemoteSettings.save(token: code);
    String? error;
    if (_service.remoteServerRunning) {
      error = _service.startRemoteServer(
        port: RemoteSettings.port,
        token: code,
      );
    }
    setState(() {
      _code = code;
      _error = error;
    });
  }

  Future<void> _copyCode() async {
    if (_code.isEmpty) return;
    await Clipboard.setData(ClipboardData(text: _code));
    if (!mounted) return;
    ScaffoldMessenger.maybeOf(context)?.showSnackBar(
      SnackBar(
        content: Text(context.l10n.prefRemoteCopied),
        duration: const Duration(seconds: 2),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final l10n = context.l10n;
    final running = _service.remoteServerRunning;
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Expanded(
              child: Text(
                l10n.prefRemoteEnable,
                style: const TextStyle(fontSize: 11, color: Color(0xFFE9EBF0)),
              ),
            ),
            Switch(value: running, onChanged: _setEnabled),
          ],
        ),
        Row(
          children: [
            SizedBox(
              width: 74,
              child: TextField(
                controller: _portCtrl,
                keyboardType: TextInputType.number,
                inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                style: const TextStyle(fontSize: 11),
                decoration: InputDecoration(
                  isDense: true,
                  labelText: l10n.prefRemotePort,
                  labelStyle: const TextStyle(fontSize: 10),
                  counterText: '',
                ),
                maxLength: 5,
                onSubmitted: (_) => _applyPort(),
              ),
            ),
            const SizedBox(width: 12),
            Expanded(
              child: Text(
                running
                    ? l10n.prefRemoteRunning(_service.remoteServerPort)
                    : l10n.prefRemoteStopped,
                style: TextStyle(
                  fontSize: 11,
                  color: running
                      ? const Color(0xFF3DD68C)
                      : const Color(0xFF9BA1AE),
                ),
              ),
            ),
          ],
        ),
        if (running) ...[
          const SizedBox(height: 8),
          Text(
            '${l10n.prefRemoteAddress}: '
            'ws://${_host.isEmpty ? '<IP>' : _host}:'
            '${_service.remoteServerPort}',
            style: const TextStyle(fontSize: 11, color: Color(0xFF9BA1AE)),
          ),
          const SizedBox(height: 8),
          Text(
            l10n.prefRemoteCode,
            style: const TextStyle(fontSize: 10, color: Color(0xFF9BA1AE)),
          ),
          const SizedBox(height: 4),
          Row(
            children: [
              Container(
                padding: const EdgeInsets.symmetric(
                  horizontal: 10,
                  vertical: 6,
                ),
                decoration: BoxDecoration(
                  color: const Color(0xFF16171B),
                  borderRadius: BorderRadius.circular(4),
                  border: Border.all(color: const Color(0xFF3E4350)),
                ),
                child: SelectableText(
                  _code.isEmpty ? '······' : _code,
                  style: const TextStyle(
                    fontSize: 15,
                    letterSpacing: 3,
                    fontWeight: FontWeight.w600,
                    color: Color(0xFFE9EBF0),
                  ),
                ),
              ),
              const SizedBox(width: 4),
              IconButton(
                tooltip: l10n.prefRemoteCopy,
                icon: const Icon(Icons.copy, size: 14),
                onPressed: _copyCode,
              ),
              TextButton(
                onPressed: _newCode,
                child: Text(
                  l10n.prefRemoteNewCode,
                  style: const TextStyle(fontSize: 11),
                ),
              ),
              if (_clients > 0)
                Text(
                  l10n.prefRemoteClients(_clients),
                  style: const TextStyle(
                    fontSize: 10,
                    color: Color(0xFF3DD68C),
                  ),
                ),
            ],
          ),
        ],
        const SizedBox(height: 4),
        Text(
          l10n.prefRemoteHelp,
          style: const TextStyle(fontSize: 9, color: Color(0xFF5C6270)),
        ),
        const SizedBox(height: 2),
        Text(
          l10n.prefRemoteWarning,
          style: const TextStyle(fontSize: 9, color: Color(0xFFF5A524)),
        ),
        if (_error != null)
          Padding(
            padding: const EdgeInsets.only(top: 4),
            child: Text(
              l10n.prefRemoteError(_error!),
              style: const TextStyle(fontSize: 10, color: Color(0xFFF5A524)),
            ),
          ),
      ],
    );
  }
}
