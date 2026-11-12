// Editable keyboard shortcuts, persisted in ~/Music/Cadence/ui_settings.json.
//
// The file is interface-only configuration: the engine never reads it. Every
// window process loads it because keyboard handling happens in the main
// window as well as in undocked piano-roll windows.

import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

/// Every app-level keyboard action. The enum name is the stable key used in
/// ui_settings.json, so do not rename existing values.
enum ShortcutAction {
  saveProject,
  openProject,
  newProject,
  copyClip,
  pasteClip,
  playPause,
  record,
  selectAllClips,
  clearSelection,
  deleteSelection,
  splitClip,
}

/// A key plus its modifiers. [primary] is ⌘ on macOS and Ctrl elsewhere;
/// [control] is an explicit Control modifier (useful on macOS for Ctrl+key).
@immutable
class ShortcutBinding {
  const ShortcutBinding(
    this.key, {
    this.primary = false,
    this.control = false,
    this.shift = false,
    this.alt = false,
  });

  final LogicalKeyboardKey key;
  final bool primary;
  final bool control;
  final bool shift;
  final bool alt;

  bool get hasModifier => primary || control || shift || alt;

  /// Activator used by the root CallbackShortcuts.
  SingleActivator get activator {
    final isMac = Platform.isMacOS;
    return SingleActivator(
      key,
      meta: primary && isMac,
      control: control || (primary && !isMac),
      shift: shift,
      alt: alt,
    );
  }

  /// Matches a key event against this binding, using the current hardware
  /// modifier state. Delete and Backspace are treated as the same key so the
  /// macOS "borrar" gesture keeps working after remapping.
  bool matches(KeyEvent event) {
    if (event is! KeyDownEvent) return false;
    if (!_sameKey(event.logicalKey, key)) return false;
    final keyboard = HardwareKeyboard.instance;
    final isMac = Platform.isMacOS;
    return keyboard.isMetaPressed == (primary && isMac) &&
        keyboard.isControlPressed == (control || (primary && !isMac)) &&
        keyboard.isShiftPressed == shift &&
        keyboard.isAltPressed == alt;
  }

  /// Builds a binding from the modifiers currently held down. Backspace is
  /// normalised to Delete because both erase the current selection.
  factory ShortcutBinding.fromKeyboard(LogicalKeyboardKey pressed) {
    final keyboard = HardwareKeyboard.instance;
    final isMac = Platform.isMacOS;
    final key =
        pressed == LogicalKeyboardKey.backspace
            ? LogicalKeyboardKey.delete
            : pressed;
    return ShortcutBinding(
      key,
      primary: isMac ? keyboard.isMetaPressed : keyboard.isControlPressed,
      control: isMac && keyboard.isControlPressed,
      shift: keyboard.isShiftPressed,
      alt: keyboard.isAltPressed,
    );
  }

  Map<String, dynamic> toJson() => <String, dynamic>{
    'keyId': key.keyId,
    if (primary) 'primary': true,
    if (control) 'control': true,
    if (shift) 'shift': true,
    if (alt) 'alt': true,
  };

  static ShortcutBinding? fromJson(Object? raw) {
    if (raw is! Map) return null;
    final keyId = raw['keyId'];
    if (keyId is! int || keyId <= 0) return null;
    return ShortcutBinding(
      LogicalKeyboardKey(keyId),
      primary: raw['primary'] == true,
      control: raw['control'] == true,
      shift: raw['shift'] == true,
      alt: raw['alt'] == true,
    );
  }

  @override
  bool operator ==(Object other) =>
      other is ShortcutBinding &&
      other.key.keyId == key.keyId &&
      other.primary == primary &&
      other.control == control &&
      other.shift == shift &&
      other.alt == alt;

  @override
  int get hashCode => Object.hash(key.keyId, primary, control, shift, alt);
}

final Set<LogicalKeyboardKey> _deleteKeys = <LogicalKeyboardKey>{
  LogicalKeyboardKey.delete,
  LogicalKeyboardKey.backspace,
};

bool _sameKey(LogicalKeyboardKey a, LogicalKeyboardKey b) {
  if (a == b) return true;
  return _deleteKeys.contains(a) && _deleteKeys.contains(b);
}

/// True for keys that only change the modifier state. The capture dialog
/// ignores them until a "real" key arrives.
bool isShortcutModifierKey(LogicalKeyboardKey key) {
  final modifiers = <LogicalKeyboardKey>{
    LogicalKeyboardKey.metaLeft,
    LogicalKeyboardKey.metaRight,
    LogicalKeyboardKey.controlLeft,
    LogicalKeyboardKey.controlRight,
    LogicalKeyboardKey.shiftLeft,
    LogicalKeyboardKey.shiftRight,
    LogicalKeyboardKey.altLeft,
    LogicalKeyboardKey.altRight,
    LogicalKeyboardKey.capsLock,
    LogicalKeyboardKey.fn,
  };
  return modifiers.contains(key);
}

class ShortcutSettings {
  ShortcutSettings._();

  static File? _file;
  static File? _override;

  /// Bumped after every edit so the main window rebuilds its shortcut map.
  static final ValueNotifier<int> revision = ValueNotifier<int>(0);

  static final Map<ShortcutAction, ShortcutBinding>
  _defaults = <ShortcutAction, ShortcutBinding>{
    ShortcutAction.saveProject: const ShortcutBinding(
      LogicalKeyboardKey.keyS,
      primary: true,
    ),
    ShortcutAction.openProject: const ShortcutBinding(
      LogicalKeyboardKey.keyO,
      primary: true,
    ),
    ShortcutAction.newProject: const ShortcutBinding(
      LogicalKeyboardKey.keyN,
      primary: true,
    ),
    ShortcutAction.copyClip: const ShortcutBinding(
      LogicalKeyboardKey.keyC,
      primary: true,
    ),
    ShortcutAction.pasteClip: const ShortcutBinding(
      LogicalKeyboardKey.keyV,
      primary: true,
    ),
    ShortcutAction.playPause: const ShortcutBinding(LogicalKeyboardKey.space),
    ShortcutAction.record: const ShortcutBinding(LogicalKeyboardKey.keyR),
    ShortcutAction.selectAllClips: const ShortcutBinding(
      LogicalKeyboardKey.keyA,
      primary: true,
    ),
    ShortcutAction.clearSelection: const ShortcutBinding(
      LogicalKeyboardKey.escape,
    ),
    ShortcutAction.deleteSelection: const ShortcutBinding(
      LogicalKeyboardKey.delete,
    ),
    ShortcutAction.splitClip: const ShortcutBinding(LogicalKeyboardKey.keyS),
  };

  static final Map<ShortcutAction, ShortcutBinding> _bindings =
      Map<ShortcutAction, ShortcutBinding>.of(_defaults);

  static File get file {
    if (_override != null) return _override!;
    _file ??= File('${_home()}/Music/Cadence/ui_settings.json');
    return _file!;
  }

  static String _home() => Platform.environment['HOME'] ?? '.';

  /// Only for tests: use a different ui_settings.json.
  @visibleForTesting
  static void overrideFile(File? f) {
    _override = f;
    _file = null;
  }

  static ShortcutBinding bindingFor(ShortcutAction action) =>
      _bindings[action] ?? _defaults[action]!;

  static ShortcutBinding defaultFor(ShortcutAction action) =>
      _defaults[action]!;

  static bool isDefault(ShortcutAction action) =>
      bindingFor(action) == defaultFor(action);

  static SingleActivator activatorFor(ShortcutAction action) =>
      bindingFor(action).activator;

  static bool matches(ShortcutAction action, KeyEvent event) =>
      bindingFor(action).matches(event);

  /// Returns the action already using [binding], if any. [excluding] lets an
  /// action keep its current binding while being edited.
  static ShortcutAction? conflictFor(
    ShortcutBinding binding, {
    ShortcutAction? excluding,
  }) {
    for (final entry in _bindings.entries) {
      if (entry.key == excluding) continue;
      if (entry.value == binding) return entry.key;
    }
    return null;
  }

  /// Applies a binding unless another action already uses it.
  static bool setBinding(ShortcutAction action, ShortcutBinding binding) {
    if (conflictFor(binding, excluding: action) != null) return false;
    _bindings[action] = binding;
    _save();
    revision.value++;
    return true;
  }

  /// Restores one action's default. Returns the conflicting action when the
  /// default is currently claimed by another custom binding.
  static ShortcutAction? reset(ShortcutAction action) {
    final defaultBinding = defaultFor(action);
    final conflict = conflictFor(defaultBinding, excluding: action);
    if (conflict != null) return conflict;
    _bindings[action] = defaultBinding;
    _save();
    revision.value++;
    return null;
  }

  static void resetAll() {
    _bindings
      ..clear()
      ..addAll(_defaults);
    _save();
    revision.value++;
  }

  /// Reads the `shortcuts` section of the shared interface settings file.
  /// Missing or corrupt entries keep their defaults.
  static void load() {
    _bindings
      ..clear()
      ..addAll(_defaults);
    try {
      final f = file;
      if (!f.existsSync()) return;
      final raw = f.readAsStringSync();
      if (raw.trim().isEmpty) return;
      final data = jsonDecode(raw);
      if (data is! Map || data['shortcuts'] is! Map) return;
      final stored = data['shortcuts'] as Map;
      for (final action in ShortcutAction.values) {
        final binding = ShortcutBinding.fromJson(stored[action.name]);
        if (binding == null) continue;
        // Skip entries that clash with a current/default binding; a corrupt
        // file must never leave the app with two actions on the same key.
        if (conflictFor(binding, excluding: action) != null) continue;
        _bindings[action] = binding;
      }
    } catch (e) {
      debugPrint('ShortcutSettings.load: $e');
    }
    revision.value++;
  }

  static void _save() {
    try {
      final f = file;
      f.parent.createSync(recursive: true);
      Map<String, dynamic> data = <String, dynamic>{};
      if (f.existsSync()) {
        try {
          final decoded = jsonDecode(f.readAsStringSync());
          if (decoded is Map) data = Map<String, dynamic>.from(decoded);
        } catch (_) {
          // A corrupt file is regenerated, like the other UI stores do.
        }
      }
      data['shortcuts'] = <String, dynamic>{
        for (final action in ShortcutAction.values)
          action.name: bindingFor(action).toJson(),
      };
      const encoder = JsonEncoder.withIndent('  ');
      f.writeAsStringSync('${encoder.convert(data)}\n');
    } catch (e) {
      debugPrint('ShortcutSettings.save: $e');
    }
  }
}
