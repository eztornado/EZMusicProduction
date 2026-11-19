// Carpetas que recorre el motor para indexar plugins.
//
// La lista inicial replica los valores por defecto de JUCE 8.0.9
// (`VST3PluginFormat::getDefaultLocationsToSearch`, que es lo que usa
// PluginHost::buildScanners) y añade una carpeta propia de la app, persistente
// y editable desde Preferencias. El usuario puede añadir o quitar carpetas;
// la selección se guarda en el directorio de configuración de la plataforma.
//
// En macOS los AudioUnit no se buscan por carpeta: el motor enumera los
// componentes registrados en el sistema. Por eso aquí solo aparecen las
// rutas VST3 (las que sí se recorren en Linux, Windows y macOS).

import 'dart:convert';
import 'dart:io';

import '../app_paths.dart';

class PluginSearchDirectory {
  const PluginSearchDirectory({
    required this.format,
    required this.path,
    required this.displayPath,
    required this.exists,
    this.isDefault = false,
  });

  /// Formato del plugin indexado en esta carpeta (hoy siempre VST3).
  final String format;

  /// Ruta absoluta real, con `~` ya expandido.
  final String path;

  /// Ruta compacta para mostrar (`~/...` en macOS/Linux/Windows).
  final String displayPath;

  /// Si la carpeta existe en este equipo.
  final bool exists;

  /// Si procede de la lista inicial del sistema/app (no la añadió el usuario).
  final bool isDefault;
}

/// Rutas de búsqueda por defecto del motor en el sistema actual.
List<PluginSearchDirectory> defaultPluginSearchDirectories() {
  return pluginSearchDirectoriesFor(
    os: currentPlatformKey(),
    environment: Platform.environment,
    exists: (path) => Directory(path).existsSync(),
  );
}

/// Variante testeable: permite simular cualquier sistema operativo.
///
/// [os] es `windows`, `macos` o `linux`; [environment] permite inyectar
/// `HOME`, `USERPROFILE`, `APPDATA`, `LOCALAPPDATA`, `ProgramFiles`,
/// `XDG_CONFIG_HOME` y `XDG_DATA_HOME`.
List<PluginSearchDirectory> pluginSearchDirectoriesFor({
  required String os,
  required Map<String, String> environment,
  required bool Function(String path) exists,
}) {
  final result = <PluginSearchDirectory>[];
  final seen = <String>{};

  void add(String rawPath) {
    final path = expandPluginPath(rawPath, os: os, environment: environment);
    final key = _pathKey(path, os);
    if (!seen.add(key)) return;
    result.add(
      PluginSearchDirectory(
        format: 'VST3',
        path: path,
        displayPath: prettyPluginPath(path, os: os, environment: environment),
        exists: exists(path),
        isDefault: true,
      ),
    );
  }

  if (os == 'macos') {
    add('~/Library/Audio/Plug-Ins/VST3');
    add('/Library/Audio/Plug-Ins/VST3');
  } else if (os == 'windows') {
    final localAppData = environment['LOCALAPPDATA'];
    final programFiles = environment['ProgramFiles'];
    if (localAppData != null && localAppData.isNotEmpty) {
      add(_joinWindows(localAppData, r'Programs\Common\VST3'));
    }
    if (programFiles != null && programFiles.isNotEmpty) {
      add(_joinWindows(programFiles, r'Common Files\VST3'));
    }
  } else {
    add('~/.vst3');
    add('/usr/lib/vst3');
    add('/usr/local/lib/vst3');
  }

  // Carpeta propia de la app: siempre existe tras el arranque y da al usuario
  // un sitio predecible donde dejar sus VST3 sin depender de rutas del sistema.
  add(cadencePluginPathFor(os: os, environment: environment));

  return result;
}

/// Construye entradas a partir de una lista persistida, recalculando `exists`
/// y la ruta compacta.
List<PluginSearchDirectory> pluginSearchDirectoriesFromPaths(
  Iterable<String> paths, {
  String format = 'VST3',
  String? os,
  Map<String, String>? environment,
  bool Function(String path)? exists,
}) {
  final platform = os ?? currentPlatformKey();
  final env = environment ?? Platform.environment;
  final defaults = <String>{
    for (final dir in pluginSearchDirectoriesFor(
      os: platform,
      environment: env,
      exists: (_) => false,
    ))
      _pathKey(dir.path, platform),
  };
  final check = exists ?? (path) => Directory(path).existsSync();
  final seen = <String>{};
  final result = <PluginSearchDirectory>[];

  for (final raw in paths) {
    final path = normalizePluginDirectoryPath(
      raw,
      os: platform,
      environment: env,
    );
    if (path.isEmpty) continue;
    final key = _pathKey(path, platform);
    if (!seen.add(key)) continue;
    result.add(
      PluginSearchDirectory(
        format: format,
        path: path,
        displayPath: prettyPluginPath(path, os: platform, environment: env),
        exists: check(path),
        isDefault: defaults.contains(key),
      ),
    );
  }
  return result;
}

/// Añade una carpeta sin duplicar (comparación insensible a mayúsculas en
/// Windows).
List<PluginSearchDirectory> addPluginSearchDirectory(
  List<PluginSearchDirectory> current,
  String rawPath,
) {
  final normalized = normalizePluginDirectoryPath(rawPath);
  if (normalized.isEmpty) return current;
  final os = currentPlatformKey();
  final key = _pathKey(normalized, os);
  if (current.any((dir) => _pathKey(dir.path, os) == key)) return current;
  return pluginSearchDirectoriesFromPaths([
    for (final dir in current) dir.path,
    normalized,
  ]);
}

List<PluginSearchDirectory> removePluginSearchDirectory(
  List<PluginSearchDirectory> current,
  String path,
) {
  final os = currentPlatformKey();
  final key = _pathKey(path, os);
  return pluginSearchDirectoriesFromPaths([
    for (final dir in current)
      if (_pathKey(dir.path, os) != key) dir.path,
  ]);
}

/// Expande `~` y devuelve una ruta absoluta normalizada.
String normalizePluginDirectoryPath(
  String rawPath, {
  String? os,
  Map<String, String>? environment,
}) {
  var value = rawPath.trim();
  if (value.length >= 2 &&
      ((value.startsWith('"') && value.endsWith('"')) ||
          (value.startsWith("'") && value.endsWith("'")))) {
    value = value.substring(1, value.length - 1).trim();
  }
  if (value.isEmpty) return '';
  final platform = os ?? currentPlatformKey();
  final expanded = expandPluginPath(
    value,
    os: platform,
    environment: environment,
  );
  return Directory(expanded).absolute.path;
}

String expandPluginPath(
  String rawPath, {
  String? os,
  Map<String, String>? environment,
}) {
  final platform = os ?? currentPlatformKey();
  final env = environment ?? Platform.environment;
  final home =
      platform == 'windows' ? (env['USERPROFILE'] ?? '') : (env['HOME'] ?? '');
  if (!rawPath.startsWith('~') || home.isEmpty) return rawPath;
  return '$home${rawPath.substring(1)}';
}

String prettyPluginPath(
  String path, {
  String? os,
  Map<String, String>? environment,
}) {
  final platform = os ?? currentPlatformKey();
  final env = environment ?? Platform.environment;
  final home =
      platform == 'windows' ? (env['USERPROFILE'] ?? '') : (env['HOME'] ?? '');
  if (home.isNotEmpty && path.startsWith(home)) {
    return '~${path.substring(home.length)}';
  }
  return path;
}

String _pathKey(String path, String os) {
  return os == 'windows' ? path.toLowerCase() : path;
}

String _joinWindows(String base, String child) {
  if (base.endsWith(r'\')) return '$base$child';
  return '$base\\$child';
}

/// Persistencia de la lista de carpetas.
///
/// Si el fichero no existe se usan los valores por defecto. Un fichero con
/// `"directories": []` significa que el usuario quitó todas las carpetas.
class PluginDirectoryStore {
  PluginDirectoryStore({File? file})
    : _file = file ?? cadenceConfigFile('plugin_directories.json');

  final File _file;

  File get file => _file;

  List<PluginSearchDirectory> load() {
    final defaults = defaultPluginSearchDirectories();
    if (!_file.existsSync()) return defaults;
    try {
      final raw = _file.readAsStringSync();
      if (raw.trim().isEmpty) return defaults;
      final decoded = jsonDecode(raw);
      if (decoded is! Map) return defaults;
      final entries = decoded['directories'];
      if (entries is! List) return defaults;

      final paths = <String>[];
      var format = 'VST3';
      for (final entry in entries) {
        if (entry is String) {
          if (entry.trim().isNotEmpty) paths.add(entry);
        } else if (entry is Map) {
          final path = entry['path'];
          final entryFormat = entry['format'];
          if (entryFormat is String && entryFormat.isNotEmpty) {
            format = entryFormat;
          }
          if (path is String && path.trim().isNotEmpty) paths.add(path);
        }
      }
      if (paths.isEmpty) return const <PluginSearchDirectory>[];
      return pluginSearchDirectoriesFromPaths(paths, format: format);
    } catch (_) {
      return defaults;
    }
  }

  void save(List<PluginSearchDirectory> directories) {
    final payload = <String, Object?>{
      'version': 1,
      'directories': [
        for (final dir in directories)
          <String, String>{'format': dir.format, 'path': dir.path},
      ],
    };
    _file.parent.createSync(recursive: true);
    _file.writeAsStringSync(
      const JsonEncoder.withIndent('  ').convert(payload),
    );
  }

  void reset() {
    if (_file.existsSync()) _file.deleteSync();
  }
}
