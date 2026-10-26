// Ajustes del control remoto (app compañera por LAN), persistidos en
// ~/Music/Cadence/ui_settings.json.
//
// Igual que el idioma, es configuración SOLO de interfaz: el motor no lee
// este fichero. El token se guarda para que el código de emparejamiento sea
// estable entre arranques; si está vacío el motor genera uno nuevo.
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

/// Valores por defecto del servidor de control remoto.
const int kDefaultRemotePort = 47823;

class RemoteSettings {
  RemoteSettings._();

  static File? _file;
  static File? _override;

  /// Solo para tests: usa otro fichero en vez de ui_settings.json.
  @visibleForTesting
  static void overrideFile(File? f) {
    _override = f;
    _file = null;
  }

  static File get file {
    if (_override != null) return _override!;
    _file ??= File('${_home()}/Music/Cadence/ui_settings.json');
    return _file!;
  }

  static String _home() => Platform.environment['HOME'] ?? '.';

  /// El control remoto está apagado por defecto: es un servidor de red y
  /// nadie debe poder controlar la DAW sin activarlo expresamente.
  static bool enabled = false;
  static int port = kDefaultRemotePort;

  /// Código de emparejamiento de 6 dígitos. Vacío = que el motor lo genere.
  static String token = '';

  /// Lee la sección `remote` del fichero compartido. Un fichero roto o
  /// ausente deja los valores por defecto.
  static void load() {
    enabled = false;
    port = kDefaultRemotePort;
    token = '';
    try {
      final f = file;
      if (!f.existsSync()) return;
      final raw = f.readAsStringSync();
      if (raw.trim().isEmpty) return;
      final data = jsonDecode(raw);
      if (data is! Map) return;
      final remote = data['remote'];
      if (remote is! Map) return;
      enabled = remote['enabled'] == true;
      final storedPort = remote['port'];
      if (storedPort is int && storedPort > 0 && storedPort <= 65535) {
        port = storedPort;
      }
      final storedToken = remote['token'];
      if (storedToken is String && storedToken.trim().isNotEmpty) {
        token = storedToken.trim();
      }
    } catch (e) {
      debugPrint('RemoteSettings.load: $e');
    }
  }

  /// Guarda los valores indicados (los no indicados conservan su valor).
  static void save({bool? enabled, int? port, String? token}) {
    if (enabled != null) RemoteSettings.enabled = enabled;
    if (port != null) {
      RemoteSettings.port = port > 0 && port <= 65535 ? port : kDefaultRemotePort;
    }
    if (token != null) RemoteSettings.token = token.trim();
    try {
      final f = file;
      f.parent.createSync(recursive: true);
      Map<String, dynamic> data = <String, dynamic>{};
      if (f.existsSync()) {
        try {
          final decoded = jsonDecode(f.readAsStringSync());
          if (decoded is Map) data = Map<String, dynamic>.from(decoded);
        } catch (_) {
          // Si el fichero no es JSON válido lo regeneramos.
        }
      }
      data['remote'] = <String, dynamic>{
        'enabled': RemoteSettings.enabled,
        'port': RemoteSettings.port,
        'token': RemoteSettings.token,
      };
      const encoder = JsonEncoder.withIndent('  ');
      f.writeAsStringSync('${encoder.convert(data)}\n');
    } catch (e) {
      debugPrint('RemoteSettings.save: $e');
    }
  }
}
