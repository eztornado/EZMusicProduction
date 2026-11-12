// Las rutas indexadas deben coincidir con los valores por defecto de JUCE en
// Linux, Windows y macOS (ver engine/plugin_directories.dart).

import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:cadence_ui/engine/plugin_directories.dart';

List<String> _paths(List<PluginSearchDirectory> dirs) =>
    dirs.map((d) => d.path).toList();

void main() {
  test('Linux: ~/.vst3 y las carpetas de sistema', () {
    final dirs = pluginSearchDirectoriesFor(
      os: 'linux',
      environment: {'HOME': '/home/ana'},
      exists: (_) => true,
    );

    expect(_paths(dirs), [
      '/home/ana/.vst3',
      '/usr/lib/vst3',
      '/usr/local/lib/vst3',
    ]);
    expect(dirs.map((d) => d.displayPath), [
      '~/.vst3',
      '/usr/lib/vst3',
      '/usr/local/lib/vst3',
    ]);
    expect(dirs.every((d) => d.format == 'VST3'), isTrue);
  });

  test('Windows: LOCALAPPDATA y Program Files', () {
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
    ]);
    expect(dirs.every((d) => !d.exists), isTrue);
  });

  test('macOS: VST3 de usuario y de sistema', () {
    final dirs = pluginSearchDirectoriesFor(
      os: 'macos',
      environment: {'HOME': '/Users/ana'},
      exists: (_) => true,
    );

    expect(_paths(dirs), [
      '/Users/ana/Library/Audio/Plug-Ins/VST3',
      '/Library/Audio/Plug-Ins/VST3',
    ]);
    expect(dirs.first.displayPath, '~/Library/Audio/Plug-Ins/VST3');
  });

  test('marca qué carpetas existen realmente', () {
    final dirs = pluginSearchDirectoriesFor(
      os: 'linux',
      environment: {'HOME': '/home/ana'},
      exists: (path) => path == '/usr/lib/vst3',
    );

    expect(dirs.map((d) => d.exists), [false, true, false]);
  });

  test('el sistema actual siempre devuelve alguna ruta', () {
    expect(Platform.operatingSystem, isNotEmpty);
    expect(defaultPluginSearchDirectories(), isNotEmpty);
  });
}
