// Rutas de datos persistentes de la aplicación.
//
// La app es portable (zip/tar en Windows/Linux), pero la configuración NO debe
// quedar junto al ejecutable: se perdería al actualizar o mover la carpeta.
// Usamos el directorio estándar por plataforma, el mismo que JUCE resuelve
// como `userApplicationDataDirectory` para la caché del motor:
//
//   macOS   ~/Library/Cadence
//   Windows %APPDATA%\Cadence
//   Linux   ${XDG_CONFIG_HOME:-~/.config}/Cadence
//
// Los plugins propios de la app viven en el directorio de datos (no en el de
// configuración) para no viajar en perfiles móviles de Windows:
//
//   macOS   ~/Library/Cadence/Plugins/VST3
//   Windows %LOCALAPPDATA%\Cadence\Plugins\VST3
//   Linux   ${XDG_DATA_HOME:-~/.local/share}/Cadence/Plugins/VST3
//
// Las versiones anteriores guardaban la interfaz en ~/Music/Cadence. Al leer
// un fichero nuevo que aún no existe se copia una vez desde esa ruta legacy.

import 'dart:io';

const String _cadenceFolder = 'Cadence';

/// Clave de plataforma usada por las funciones puras (testeables).
String currentPlatformKey() {
  if (Platform.isWindows) return 'windows';
  if (Platform.isLinux) return 'linux';
  return 'macos';
}

String _separatorFor(String os) => os == 'windows' ? r'\' : '/';

String _joinFor(String os, String base, String child) {
  final root = base.isEmpty ? '.' : base;
  if (root.endsWith('/') || root.endsWith(r'\')) return '$root$child';
  return '$root${_separatorFor(os)}$child';
}

String _joinAll(String os, Iterable<String> parts) {
  var result = '';
  for (final part in parts) {
    if (part.isEmpty) continue;
    result = result.isEmpty ? part : _joinFor(os, result, part);
  }
  return result;
}

String _homeFor(String os, Map<String, String> environment) {
  final home =
      os == 'windows' ? environment['USERPROFILE'] : environment['HOME'];
  return (home == null || home.isEmpty) ? '.' : home;
}

/// Configuración de la app (ajustes de UI, controlador, caché de plugins).
String cadenceConfigPathFor({
  required String os,
  required Map<String, String> environment,
}) {
  final home = _homeFor(os, environment);
  switch (os) {
    case 'windows':
      final appData = environment['APPDATA'];
      if (appData != null && appData.isNotEmpty) {
        return _joinFor(os, appData, _cadenceFolder);
      }
      final localAppData = environment['LOCALAPPDATA'];
      if (localAppData != null && localAppData.isNotEmpty) {
        return _joinFor(os, localAppData, _cadenceFolder);
      }
      return _joinFor(
        os,
        _joinFor(os, home, r'AppData\Roaming'),
        _cadenceFolder,
      );
    case 'linux':
      final xdg = environment['XDG_CONFIG_HOME'];
      if (xdg != null && xdg.isNotEmpty) {
        return _joinFor(os, xdg, _cadenceFolder);
      }
      return _joinFor(os, _joinFor(os, home, '.config'), _cadenceFolder);
    default:
      return _joinFor(os, home, 'Library/Cadence');
  }
}

/// Datos de usuario de la app (plugins propios, bibliotecas descargadas).
String cadenceDataPathFor({
  required String os,
  required Map<String, String> environment,
}) {
  final home = _homeFor(os, environment);
  switch (os) {
    case 'windows':
      final localAppData = environment['LOCALAPPDATA'];
      if (localAppData != null && localAppData.isNotEmpty) {
        return _joinFor(os, localAppData, _cadenceFolder);
      }
      final appData = environment['APPDATA'];
      if (appData != null && appData.isNotEmpty) {
        return _joinFor(os, appData, _cadenceFolder);
      }
      return _joinFor(os, _joinFor(os, home, r'AppData\Local'), _cadenceFolder);
    case 'linux':
      final xdg = environment['XDG_DATA_HOME'];
      if (xdg != null && xdg.isNotEmpty) {
        return _joinFor(os, xdg, _cadenceFolder);
      }
      return _joinFor(os, _joinFor(os, home, '.local/share'), _cadenceFolder);
    default:
      return _joinFor(os, home, 'Library/Cadence');
  }
}

/// Carpeta propia para plugins VST3 que el usuario quiera instalar sin tocar
/// las carpetas del sistema. Se crea al arrancar y aparece en Preferencias.
String cadencePluginPathFor({
  required String os,
  required Map<String, String> environment,
}) {
  return _joinFor(
    os,
    _joinFor(
      os,
      cadenceDataPathFor(os: os, environment: environment),
      'Plugins',
    ),
    'VST3',
  );
}

/// Proyectos y exportaciones: carpetas visibles dentro del directorio
/// personal del usuario. Mantiene la ruta histórica de macOS y funciona igual
/// en Windows/Linux, donde antes se usaba una ruta absoluta de macOS.
String cadenceProjectsPathFor({
  required String os,
  required Map<String, String> environment,
}) {
  return _joinAll(os, <String>[
    _homeFor(os, environment),
    'Music',
    'EZMusicProduction',
    'Projects',
  ]);
}

String cadenceExportsPathFor({
  required String os,
  required Map<String, String> environment,
}) {
  return _joinAll(os, <String>[
    _homeFor(os, environment),
    'Music',
    'EZMusicProduction',
    'Exports',
  ]);
}

Directory cadenceConfigDirectory() => Directory(
  cadenceConfigPathFor(
    os: currentPlatformKey(),
    environment: Platform.environment,
  ),
);

Directory cadenceDataDirectory() => Directory(
  cadenceDataPathFor(
    os: currentPlatformKey(),
    environment: Platform.environment,
  ),
);

Directory cadencePluginDirectory() => Directory(
  cadencePluginPathFor(
    os: currentPlatformKey(),
    environment: Platform.environment,
  ),
);

Directory cadenceProjectsDirectory() => Directory(
  cadenceProjectsPathFor(
    os: currentPlatformKey(),
    environment: Platform.environment,
  ),
);

Directory cadenceExportsDirectory() => Directory(
  cadenceExportsPathFor(
    os: currentPlatformKey(),
    environment: Platform.environment,
  ),
);

/// Crea los directorios persistentes de la app. Los fallos no son fatales:
/// el usuario puede arrancar aunque una carpeta esté protegida.
void ensureCadenceDirectories() {
  for (final dir in <Directory>[
    cadenceConfigDirectory(),
    cadenceDataDirectory(),
    cadencePluginDirectory(),
    cadenceProjectsDirectory(),
    cadenceExportsDirectory(),
  ]) {
    try {
      dir.createSync(recursive: true);
    } catch (_) {
      // Sin permisos: se mostrará como "No existe" en Preferencias.
    }
  }
}

/// Fichero dentro del directorio de configuración. Migra una vez desde
/// `~/Music/Cadence/<name>` para no perder ajustes existentes.
File cadenceConfigFile(String name) {
  final os = currentPlatformKey();
  final environment = Platform.environment;
  final dir = cadenceConfigDirectory();
  final file = File(_joinFor(os, dir.path, name));
  if (file.existsSync()) return file;

  final legacy = File(
    _joinAll(os, <String>[_homeFor(os, environment), 'Music', 'Cadence', name]),
  );
  if (legacy.existsSync()) {
    try {
      dir.createSync(recursive: true);
      legacy.copySync(file.path);
    } catch (_) {
      // Si falla la migración se usará el fichero nuevo cuando se guarde.
    }
  }
  return file;
}

/// Solo visible para tests/documentación: ruta legacy de un ajuste.
String legacyCadenceConfigPath({
  required String os,
  required Map<String, String> environment,
  required String name,
}) {
  return _joinAll(os, <String>[
    _homeFor(os, environment),
    'Music',
    'Cadence',
    name,
  ]);
}
