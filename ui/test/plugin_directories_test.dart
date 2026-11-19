// Las rutas indexadas deben coincidir con los valores por defecto de JUCE en
// Linux, Windows y macOS, más la carpeta propia de la app (persistente y
// editable desde Preferencias). Ver ui/lib/engine/plugin_directories.dart.

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/app_paths.dart';
import 'package:cadence_ui/engine/plugin_directories.dart';

List<String> _paths(List<PluginSearchDirectory> dirs) =>
    dirs.map((d) => d.path).toList();

void main() {
  test('Linux: ~/.vst3, sistema y carpeta propia', () {
    final dirs = pluginSearchDirectoriesFor(
      os: 'linux',
      environment: {'HOME': '/home/ana'},
      exists: (_) => true,
    );

    expect(_paths(dirs), [
      '/home/ana/.vst3',
      '/usr/lib/vst3',
      '/usr/local/lib/vst3',
      '/home/ana/.local/share/Cadence/Plugins/VST3',
    ]);
    expect(dirs.map((d) => d.displayPath), [
      '~/.vst3',
      '/usr/lib/vst3',
      '/usr/local/lib/vst3',
      '~/.local/share/Cadence/Plugins/VST3',
    ]);
    expect(dirs.every((d) => d.format == 'VST3'), isTrue);
  });

  test('Windows: LOCALAPPDATA, Program Files y carpeta propia', () {
    final dirs = pluginSearchDirectoriesFor(
      os: 'windows',
      environment: {
        'USERPROFILE': r'C:\Users\ana',
        'LOCALAPPDATA': r'C:\Users\ana\AppData\Local',
        'ProgramFiles': r'C:\Program Files',
      },
      exists: (_) => false,
    );

    expect(_paths(dirs), [
      r'C:\Users\ana\AppData\Local\Programs\Common\VST3',
      r'C:\Program Files\Common Files\VST3',
      r'C:\Users\ana\AppData\Local\Cadence\Plugins\VST3',
    ]);
    expect(dirs.every((d) => !d.exists), isTrue);
  });

  test('macOS: VST3 de usuario, de sistema y carpeta propia', () {
    final dirs = pluginSearchDirectoriesFor(
      os: 'macos',
      environment: {'HOME': '/Users/ana'},
      exists: (_) => true,
    );

    expect(_paths(dirs), [
      '/Users/ana/Library/Audio/Plug-Ins/VST3',
      '/Library/Audio/Plug-Ins/VST3',
      '/Users/ana/Library/Cadence/Plugins/VST3',
    ]);
    expect(dirs.first.displayPath, '~/Library/Audio/Plug-Ins/VST3');
  });

  test('marca qué carpetas existen realmente', () {
    final dirs = pluginSearchDirectoriesFor(
      os: 'linux',
      environment: {'HOME': '/home/ana'},
      exists: (path) => path == '/usr/lib/vst3',
    );

    expect(dirs.map((d) => d.exists), [false, true, false, false]);
  });

  test('el sistema actual siempre devuelve alguna ruta', () {
    expect(Platform.operatingSystem, isNotEmpty);
    expect(defaultPluginSearchDirectories(), isNotEmpty);
  });

  test('platform config/data paths are persistent and native', () {
    expect(
      cadenceConfigPathFor(
        os: 'windows',
        environment: {
          'USERPROFILE': r'C:\Users\Ivan',
          'APPDATA': r'C:\Users\Ivan\AppData\Roaming',
          'LOCALAPPDATA': r'C:\Users\Ivan\AppData\Local',
        },
      ),
      r'C:\Users\Ivan\AppData\Roaming\Cadence',
    );
    expect(
      cadenceDataPathFor(
        os: 'windows',
        environment: {
          'USERPROFILE': r'C:\Users\Ivan',
          'APPDATA': r'C:\Users\Ivan\AppData\Roaming',
          'LOCALAPPDATA': r'C:\Users\Ivan\AppData\Local',
        },
      ),
      r'C:\Users\Ivan\AppData\Local\Cadence',
    );
    expect(
      cadencePluginPathFor(
        os: 'windows',
        environment: {
          'USERPROFILE': r'C:\Users\Ivan',
          'LOCALAPPDATA': r'C:\Users\Ivan\AppData\Local',
        },
      ),
      r'C:\Users\Ivan\AppData\Local\Cadence\Plugins\VST3',
    );

    expect(
      cadenceConfigPathFor(
        os: 'linux',
        environment: {
          'HOME': '/home/ivan',
          'XDG_CONFIG_HOME': '/home/ivan/.config',
          'XDG_DATA_HOME': '/home/ivan/.local/share',
        },
      ),
      '/home/ivan/.config/Cadence',
    );
    expect(
      cadencePluginPathFor(
        os: 'linux',
        environment: {
          'HOME': '/home/ivan',
          'XDG_CONFIG_HOME': '/home/ivan/.config',
          'XDG_DATA_HOME': '/home/ivan/.local/share',
        },
      ),
      '/home/ivan/.local/share/Cadence/Plugins/VST3',
    );

    expect(
      cadenceConfigPathFor(
        os: 'macos',
        environment: {'HOME': '/Users/ivan'},
      ),
      '/Users/ivan/Library/Cadence',
    );
    expect(
      legacyCadenceConfigPath(
        os: 'macos',
        environment: {'HOME': '/Users/ivan'},
        name: 'ui_settings.json',
      ),
      '/Users/ivan/Music/Cadence/ui_settings.json',
    );
  });

  test('custom folders are added once and can be removed', () {
    final temp = Directory.systemTemp.createTempSync('cadence_plugins_');
    addTearDown(() => temp.deleteSync(recursive: true));

    final base = pluginSearchDirectoriesFromPaths([temp.path]);
    final again = addPluginSearchDirectory(base, temp.path);
    expect(again.length, base.length);

    final custom = Directory('${temp.path}${Platform.pathSeparator}Custom')
      ..createSync();
    final added = addPluginSearchDirectory(again, custom.path);
    expect(added.length, base.length + 1);
    expect(added.any((d) => d.path == custom.path), isTrue);

    final removed = removePluginSearchDirectory(added, custom.path);
    expect(removed.length, base.length);
    expect(removed.any((d) => d.path == custom.path), isFalse);
  });

  test('folder store round-trips and reset restores defaults', () {
    final temp = Directory.systemTemp.createTempSync('cadence_dirs_');
    addTearDown(() => temp.deleteSync(recursive: true));
    final file = File('${temp.path}${Platform.pathSeparator}directories.json');
    final store = PluginDirectoryStore(file: file);

    // No file yet: deterministic defaults for this machine.
    final defaults = store.load();
    expect(defaults, isNotEmpty);

    final custom = Directory('${temp.path}${Platform.pathSeparator}VST3')
      ..createSync();
    final paths = pluginSearchDirectoriesFromPaths([custom.path]);
    store.save(paths);

    final loaded = store.load();
    expect(loaded.length, 1);
    expect(loaded.single.path, custom.path);
    expect(loaded.single.exists, isTrue);

    store.reset();
    final restored = store.load();
    expect(
      restored.map((d) => d.path).toList(),
      defaults.map((d) => d.path).toList(),
    );
  });

  test('an explicitly empty folder list is preserved', () {
    final temp = Directory.systemTemp.createTempSync('cadence_empty_');
    addTearDown(() => temp.deleteSync(recursive: true));
    final file = File('${temp.path}${Platform.pathSeparator}directories.json');
    final store = PluginDirectoryStore(file: file);
    store.save(const []);

    expect(store.load(), isEmpty);
  });
}
