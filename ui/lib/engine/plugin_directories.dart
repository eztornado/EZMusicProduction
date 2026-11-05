// Carpetas que recorre el motor para indexar plugins.
//
// La lista replica los valores por defecto de JUCE 8.0.9
// (`VST3PluginFormat::getDefaultLocationsToSearch`, que es lo que usa
// PluginHost::buildScanners). Si se actualiza JUCE hay que revisar esto.
//
// En macOS los AudioUnit no se buscan por carpeta: el motor enumera los
// componentes registrados en el sistema. Por eso aquí solo aparecen las
// rutas VST3 (las que sí se recorren en Linux, Windows y macOS).

import 'dart:io';

class PluginSearchDirectory {
  const PluginSearchDirectory({
    required this.format,
    required this.path,
    required this.displayPath,
    required this.exists,
  });

  /// Formato del plugin indexado en esta carpeta (hoy siempre VST3).
  final String format;

  /// Ruta absoluta real, con `~` ya expandido.
  final String path;

  /// Ruta compacta para mostrar (`~/...` en macOS/Linux).
  final String displayPath;

  /// Si la carpeta existe en este equipo.
  final bool exists;
}

/// Rutas de búsqueda por defecto del motor en el sistema actual.
List<PluginSearchDirectory> defaultPluginSearchDirectories() {
  final os =
      Platform.isWindows
          ? 'windows'
          : Platform.isMacOS
          ? 'macos'
          : 'linux';
  return pluginSearchDirectoriesFor(
    os: os,
    environment: Platform.environment,
    exists: (path) => Directory(path).existsSync(),
  );
}

/// Variante testeable: permite simular cualquier sistema operativo.
///
/// [os] es `windows`, `macos` o `linux`; [environment] permite inyectar
/// `HOME`, `USERPROFILE`, `LOCALAPPDATA` y `ProgramFiles`.
List<PluginSearchDirectory> pluginSearchDirectoriesFor({
  required String os,
  required Map<String, String> environment,
  required bool Function(String path) exists,
}) {
  final result = <PluginSearchDirectory>[];

  String home() {
    if (os == 'windows') return environment['USERPROFILE'] ?? '';
    return environment['HOME'] ?? '';
  }

  String expandHome(String path) {
    if (!path.startsWith('~')) return path;
    final h = home();
    if (h.isEmpty) return path;
    return '$h${path.substring(1)}';
  }

  String prettyPath(String path) {
    final h = home();
    if (h.isNotEmpty && path.startsWith(h)) {
      return '~${path.substring(h.length)}';
    }
    return path;
  }

  void add(String rawPath) {
    final path = expandHome(rawPath);
    result.add(
      PluginSearchDirectory(
        format: 'VST3',
        path: path,
        displayPath: prettyPath(path),
        exists: exists(path),
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

  return result;
}

String _joinWindows(String base, String child) {
  if (base.endsWith(r'\')) return '$base$child';
  return '$base\\$child';
}
