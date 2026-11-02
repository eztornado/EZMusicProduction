// Registro de depuración en tiempo real.
//
// El modo debug escribe un fichero compartido por todas las ventanas/procesos
// de la app. La consola abre ese fichero y muestra lo que va llegando; así la
// ventana Debug ve también lo que registra la ventana principal (incluidas las
// peticiones y respuestas de Arreglo IA).
//
// Nunca se registra la cabecera Authorization ni la API key: solo el cuerpo
// JSON de la petición y la respuesta del servidor.

import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

import 'debug_settings.dart';

class DebugLog {
  DebugLog._();

  static const int _maxBytes = 4 * 1024 * 1024;
  static File? _override;
  static File? _file;
  static DebugPrintCallback? _originalDebugPrint;
  static bool _installed = false;

  /// Solo para tests: usa otro fichero en vez de debug.log.
  @visibleForTesting
  static void overrideFile(File? f) {
    _override = f;
    _file = null;
  }

  static File get file {
    if (_override != null) return _override!;
    _file ??= File('${_home()}/Music/Cadence/debug.log');
    return _file!;
  }

  static String _home() => Platform.environment['HOME'] ?? '.';

  /// Activa o desactiva la captura global de debugPrint según el ajuste.
  static void configure() {
    if (DebugSettings.enabled) {
      _install();
    } else {
      _uninstall();
    }
  }

  static void _install() {
    if (_installed) return;
    _originalDebugPrint ??= debugPrint;
    debugPrint = (String? message, {int? wrapWidth}) {
      _originalDebugPrint?.call(message, wrapWidth: wrapWidth);
      if (message != null) write('app', message);
    };
    _installed = true;
    write('debug', 'Modo debug activado. Log: ${file.path}');
  }

  static void _uninstall() {
    if (!_installed) return;
    write('debug', 'Modo debug desactivado.');
    if (_originalDebugPrint != null) debugPrint = _originalDebugPrint!;
    _installed = false;
  }

  /// Escribe una entrada con marca de tiempo. No hace nada si el modo debug
  /// está apagado.
  static void write(String channel, String message) {
    if (!DebugSettings.enabled) return;
    final text = message.replaceAll('\r\n', '\n').trimRight();
    final lines = text.split('\n');
    final buffer = StringBuffer(
      '[${_timestamp()}] [${channel.padRight(4)}] ',
    );
    buffer.write(lines.isEmpty ? '' : lines.first);
    for (var i = 1; i < lines.length; i++) {
      buffer
        ..write('\n  ')
        ..write(lines[i]);
    }
    buffer.writeln();
    try {
      final f = file;
      f.parent.createSync(recursive: true);
      _rotateIfNeeded(f);
      f.writeAsStringSync(
        buffer.toString(),
        mode: FileMode.append,
        flush: true,
      );
    } catch (_) {
      // La consola nunca debe tumbar la app.
    }
  }

  /// Borra el contenido del log.
  static void clear() {
    try {
      final f = file;
      f.parent.createSync(recursive: true);
      f.writeAsStringSync('', flush: true);
    } catch (_) {
      // Ignorar errores de disco.
    }
  }

  /// Devuelve el log completo, limitado a [maxChars] por el final para que un
  /// Text enorme no congele la consola.
  static String readText({int maxChars = 500000}) {
    try {
      final f = file;
      if (!f.existsSync()) return '';
      final text = utf8.decode(f.readAsBytesSync(), allowMalformed: true);
      if (text.length <= maxChars) return text;
      return '… (mostrando el final del registro)\n'
          '${text.substring(text.length - maxChars)}';
    } catch (_) {
      return '';
    }
  }

  static int get size {
    try {
      final f = file;
      return f.existsSync() ? f.lengthSync() : 0;
    } catch (_) {
      return 0;
    }
  }

  static void _rotateIfNeeded(File f) {
    try {
      if (!f.existsSync() || f.lengthSync() <= _maxBytes) return;
      final previous = File('${f.path}.1');
      if (previous.existsSync()) previous.deleteSync();
      f.renameSync(previous.path);
    } catch (_) {
      // Si no se puede rotar, se sigue escribiendo en el mismo fichero.
    }
  }

  static String _timestamp() {
    final now = DateTime.now();
    String two(int n) => n.toString().padLeft(2, '0');
    String three(int n) => n.toString().padLeft(3, '0');
    return '${two(now.hour)}:${two(now.minute)}:${two(now.second)}.'
        '${three(now.millisecond)}';
  }

  /// Solo para tests: restaura debugPrint y los overrides.
  @visibleForTesting
  static void resetForTest() {
    _uninstall();
    _override = null;
    _file = null;
  }
}
