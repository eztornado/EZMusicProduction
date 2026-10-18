// Acceso a las traducciones de la interfaz.
//
// Uso normal, dentro de un build con BuildContext:
//
//     Text(context.l10n.timelineAddTrack)
//
// Y desde codigo que NO tiene contexto (p. ej. EngineService devolviendo un
// mensaje de error): `L10n.current.serviceRecordingInProgress`. La instancia
// global se actualiza en el `builder` de cada MaterialApp, asi que siempre
// refleja el idioma activo.
import 'package:flutter/widgets.dart';

import 'app_localizations.dart';

export 'app_localizations.dart';

/// Atajo: `context.l10n.<clave>`.
extension L10nContext on BuildContext {
  AppLocalizations get l10n => AppLocalizations.of(this);
}

/// Ultima instancia de traducciones montada en la app.
///
/// Necesario porque los mensajes de error se construyen en el servicio (FFI),
/// lejos de cualquier BuildContext.
class L10n {
  L10n._();

  static AppLocalizations? _current;

  /// Si aun no se ha montado ninguna app (tests del servicio, arranque),
  /// se cae al idioma de referencia (espanol) en vez de reventar.
  static AppLocalizations get current =>
      _current ?? lookupAppLocalizations(const Locale('es'));

  /// Se llama desde el `builder` de los MaterialApp, en cada rebuild.
  static void bind(AppLocalizations value) => _current = value;
}
