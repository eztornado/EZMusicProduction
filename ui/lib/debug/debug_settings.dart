// Ajustes del modo debug (desarrollo/diagnóstico), persistidos en
// ~/Music/Cadence/ui_settings.json.
//
// Es configuración SOLO de interfaz: el motor no lee este fichero. El modo
// debug añade el botón Debug a la barra superior y activa el registro de
// peticiones de IA y mensajes de la aplicación en debug.log.

import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

class DebugSettings {
  DebugSettings._();

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

  /// El modo debug está apagado por defecto.
  static bool enabled = false;

  /// La barra superior escucha este notifier para mostrar u ocultar el botón.
  static final ValueNotifier<bool> notifier = ValueNotifier<bool>(false);

  /// Lee la sección `debug` del fichero compartido.
  static void load() {
    enabled = false;
    try {
      final f = file;
      if (!f.existsSync()) {
        notifier.value = false;
        return;
      }
      final raw = f.readAsStringSync();
      if (raw.trim().isEmpty) {
        notifier.value = false;
        return;
      }
      final data = jsonDecode(raw);
      if (data is Map && data['debug'] is Map) {
        enabled = (data['debug'] as Map)['enabled'] == true;
      }
    } catch (e) {
      debugPrint('DebugSettings.load: $e');
    }
    notifier.value = enabled;
  }

  /// Guarda el modo debug conservando el resto de secciones del fichero.
  static void save({bool? enabled}) {
    if (enabled != null) DebugSettings.enabled = enabled;
    notifier.value = DebugSettings.enabled;
    try {
      final f = file;
      f.parent.createSync(recursive: true);
      Map<String, dynamic> data = <String, dynamic>{};
      if (f.existsSync()) {
        try {
          final decoded = jsonDecode(f.readAsStringSync());
          if (decoded is Map) data = Map<String, dynamic>.from(decoded);
        } catch (_) {
          // Un fichero roto se regenera; el resto de la app hace lo mismo.
        }
      }
      data['debug'] = <String, dynamic>{'enabled': DebugSettings.enabled};
      const encoder = JsonEncoder.withIndent('  ');
      f.writeAsStringSync('${encoder.convert(data)}\n');
    } catch (e) {
      debugPrint('DebugSettings.save: $e');
    }
  }
}
