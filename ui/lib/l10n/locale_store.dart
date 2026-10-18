// Idioma de la interfaz, persistido en ~/Music/Cadence/ui_settings.json.
//
// Es un fichero SOLO de interfaz (el motor tiene el suyo, settings.json, con
// audio/MIDI): asi cambiar de idioma nunca puede romper la configuracion de
// audio. `null` significa "seguir al sistema".
import 'dart:convert';
import 'dart:io';

import 'package:flutter/widgets.dart';

/// Idioma activo. Cambiarlo reconstruye los MaterialApp (ValueListenableBuilder).
final ValueNotifier<Locale?> appLocale = ValueNotifier<Locale?>(null);

/// Idiomas que la interfaz sabe mostrar, ademas de "seguir al sistema".
const List<Locale> supportedUiLocales = <Locale>[
  Locale('es'),
  Locale('en'),
  Locale('ja'),
];

class LocaleStore {
  LocaleStore._();

  static File? _file;
  static File? _override;

  /// Solo para tests: usa otro fichero en vez de ~/Music/Cadence/ui_settings.json.
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

  /// Lee el idioma guardado. Si no hay nada (o el fichero esta roto) deja
  /// "seguir al sistema". Se llama al arrancar, en TODOS los procesos: la
  /// ventana principal y cada sub-ventana (mixer, piano roll...) son procesos
  /// distintos y necesitan el idioma por su cuenta.
  static void load() {
    try {
      final f = file;
      if (!f.existsSync()) return;
      final raw = f.readAsStringSync();
      if (raw.trim().isEmpty) return;
      final data = jsonDecode(raw);
      if (data is! Map) return;
      final code = data['locale'];
      if (code is String && code.isNotEmpty) {
        for (final l in supportedUiLocales) {
          if (l.languageCode == code) {
            appLocale.value = l;
            return;
          }
        }
      }
      appLocale.value = null;
    } catch (e) {
      // Un ui_settings.json corrupto no debe impedir arrancar.
      debugPrint('LocaleStore.load: $e');
      appLocale.value = null;
    }
  }

  /// Guarda el idioma elegido (null = seguir al sistema).
  static void save(Locale? locale) {
    appLocale.value = locale;
    try {
      final f = file;
      f.parent.createSync(recursive: true);
      Map<String, dynamic> data = <String, dynamic>{};
      if (f.existsSync()) {
        try {
          final decoded = jsonDecode(f.readAsStringSync());
          if (decoded is Map) data = Map<String, dynamic>.from(decoded);
        } catch (_) {
          // Si el fichero no es JSON valido lo regeneramos.
        }
      }
      if (locale == null) {
        data.remove('locale');
      } else {
        data['locale'] = locale.languageCode;
      }
      const encoder = JsonEncoder.withIndent('  ');
      f.writeAsStringSync('${encoder.convert(data)}\n');
    } catch (e) {
      debugPrint('LocaleStore.save: $e');
    }
  }
}
