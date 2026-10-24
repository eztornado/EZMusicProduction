import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:intl/intl.dart' as intl;

import 'app_localizations_en.dart';
import 'app_localizations_es.dart';
import 'app_localizations_ja.dart';

// ignore_for_file: type=lint

/// Callers can lookup localized strings with an instance of AppLocalizations
/// returned by `AppLocalizations.of(context)`.
///
/// Applications need to include `AppLocalizations.delegate()` in their app's
/// `localizationDelegates` list, and the locales they support in the app's
/// `supportedLocales` list. For example:
///
/// ```dart
/// import 'l10n/app_localizations.dart';
///
/// return MaterialApp(
///   localizationsDelegates: AppLocalizations.localizationsDelegates,
///   supportedLocales: AppLocalizations.supportedLocales,
///   home: MyApplicationHome(),
/// );
/// ```
///
/// ## Update pubspec.yaml
///
/// Please make sure to update your pubspec.yaml to include the following
/// packages:
///
/// ```yaml
/// dependencies:
///   # Internationalization support.
///   flutter_localizations:
///     sdk: flutter
///   intl: any # Use the pinned version from flutter_localizations
///
///   # Rest of dependencies
/// ```
///
/// ## iOS Applications
///
/// iOS applications define key application metadata, including supported
/// locales, in an Info.plist file that is built into the application bundle.
/// To configure the locales supported by your app, you’ll need to edit this
/// file.
///
/// First, open your project’s ios/Runner.xcworkspace Xcode workspace file.
/// Then, in the Project Navigator, open the Info.plist file under the Runner
/// project’s Runner folder.
///
/// Next, select the Information Property List item, select Add Item from the
/// Editor menu, then select Localizations from the pop-up menu.
///
/// Select and expand the newly-created Localizations item then, for each
/// locale your application supports, add a new item and select the locale
/// you wish to add from the pop-up menu in the Value field. This list should
/// be consistent with the languages listed in the AppLocalizations.supportedLocales
/// property.
abstract class AppLocalizations {
  AppLocalizations(String locale)
    : localeName = intl.Intl.canonicalizedLocale(locale.toString());

  final String localeName;

  static AppLocalizations of(BuildContext context) {
    return Localizations.of<AppLocalizations>(context, AppLocalizations)!;
  }

  static const LocalizationsDelegate<AppLocalizations> delegate =
      _AppLocalizationsDelegate();

  /// A list of this localizations delegate along with the default localizations
  /// delegates.
  ///
  /// Returns a list of localizations delegates containing this delegate along with
  /// GlobalMaterialLocalizations.delegate, GlobalCupertinoLocalizations.delegate,
  /// and GlobalWidgetsLocalizations.delegate.
  ///
  /// Additional delegates can be added by appending to this list in
  /// MaterialApp. This list does not have to be used at all if a custom list
  /// of delegates is preferred or required.
  static const List<LocalizationsDelegate<dynamic>> localizationsDelegates =
      <LocalizationsDelegate<dynamic>>[
        delegate,
        GlobalMaterialLocalizations.delegate,
        GlobalCupertinoLocalizations.delegate,
        GlobalWidgetsLocalizations.delegate,
      ];

  /// A list of this localizations delegate's supported locales.
  static const List<Locale> supportedLocales = <Locale>[
    Locale('en'),
    Locale('es'),
    Locale('ja'),
  ];

  /// No description provided for @appTitle.
  ///
  /// In es, this message translates to:
  /// **'Cadence'**
  String get appTitle;

  /// No description provided for @appNoClip.
  ///
  /// In es, this message translates to:
  /// **'Sin clip'**
  String get appNoClip;

  /// No description provided for @appEngineUnavailable.
  ///
  /// In es, this message translates to:
  /// **'Engine no disponible'**
  String get appEngineUnavailable;

  /// No description provided for @appDylibError.
  ///
  /// In es, this message translates to:
  /// **'No se pudo cargar libcadence_engine.dylib: {error}\nCompila el engine: cmake --build ~/daw/build'**
  String appDylibError(String error);

  /// No description provided for @appDylibOk.
  ///
  /// In es, this message translates to:
  /// **'dylib ok'**
  String get appDylibOk;

  /// No description provided for @appStatusLine.
  ///
  /// In es, this message translates to:
  /// **'{tracks} pistas · {duration} s · dylib ok'**
  String appStatusLine(String tracks, String duration);

  /// No description provided for @appWindowMixer.
  ///
  /// In es, this message translates to:
  /// **'Mixer — EZMusicProduction'**
  String get appWindowMixer;

  /// No description provided for @appWindowPianoRoll.
  ///
  /// In es, this message translates to:
  /// **'Piano roll'**
  String get appWindowPianoRoll;

  /// No description provided for @appWindowAutomation.
  ///
  /// In es, this message translates to:
  /// **'Automatización'**
  String get appWindowAutomation;

  /// No description provided for @appMenuNew.
  ///
  /// In es, this message translates to:
  /// **'Nuevo'**
  String get appMenuNew;

  /// No description provided for @appMenuOpen.
  ///
  /// In es, this message translates to:
  /// **'Abrir…'**
  String get appMenuOpen;

  /// No description provided for @appMenuSave.
  ///
  /// In es, this message translates to:
  /// **'Guardar'**
  String get appMenuSave;

  /// No description provided for @appMenuExport.
  ///
  /// In es, this message translates to:
  /// **'Exportar'**
  String get appMenuExport;

  /// No description provided for @appMenuSounds.
  ///
  /// In es, this message translates to:
  /// **'Sonidos'**
  String get appMenuSounds;

  /// No description provided for @appMenuGrid.
  ///
  /// In es, this message translates to:
  /// **'Rejilla'**
  String get appMenuGrid;

  /// No description provided for @appMenuMixer.
  ///
  /// In es, this message translates to:
  /// **'Mixer'**
  String get appMenuMixer;

  /// No description provided for @appMenuPianoRoll.
  ///
  /// In es, this message translates to:
  /// **'Piano roll'**
  String get appMenuPianoRoll;

  /// No description provided for @appMenuAutomation.
  ///
  /// In es, this message translates to:
  /// **'Auto'**
  String get appMenuAutomation;

  /// No description provided for @appMenuPreferences.
  ///
  /// In es, this message translates to:
  /// **'Preferencias (audio, MIDI)'**
  String get appMenuPreferences;

  /// No description provided for @transportTempoTimeSig.
  ///
  /// In es, this message translates to:
  /// **'Tempo y compás'**
  String get transportTempoTimeSig;

  /// No description provided for @transportTempoHint.
  ///
  /// In es, this message translates to:
  /// **'Tempo (BPM, 20–300)'**
  String get transportTempoHint;

  /// No description provided for @transportTimeSig.
  ///
  /// In es, this message translates to:
  /// **'Compás'**
  String get transportTimeSig;

  /// No description provided for @transportCancel.
  ///
  /// In es, this message translates to:
  /// **'Cancelar'**
  String get transportCancel;

  /// No description provided for @transportApply.
  ///
  /// In es, this message translates to:
  /// **'Aplicar'**
  String get transportApply;

  /// No description provided for @transportRecord.
  ///
  /// In es, this message translates to:
  /// **'Grabar'**
  String get transportRecord;

  /// No description provided for @transportStopRecording.
  ///
  /// In es, this message translates to:
  /// **'Detener grabación'**
  String get transportStopRecording;

  /// No description provided for @transportMetronome.
  ///
  /// In es, this message translates to:
  /// **'Metrónomo'**
  String get transportMetronome;

  /// No description provided for @transportFinishRecording.
  ///
  /// In es, this message translates to:
  /// **'Finalizar grabación (equivale a R)'**
  String get transportFinishRecording;

  /// No description provided for @transportPlayPause.
  ///
  /// In es, this message translates to:
  /// **'Play / Pausa'**
  String get transportPlayPause;

  /// No description provided for @transportStop.
  ///
  /// In es, this message translates to:
  /// **'Stop'**
  String get transportStop;

  /// No description provided for @transportIQ.
  ///
  /// In es, this message translates to:
  /// **'IQ:'**
  String get transportIQ;

  /// No description provided for @transportOff.
  ///
  /// In es, this message translates to:
  /// **'Off'**
  String get transportOff;

  /// No description provided for @transportRestartAudio.
  ///
  /// In es, this message translates to:
  /// **'Reiniciar audio (recupera el sonido si se corta)'**
  String get transportRestartAudio;

  /// No description provided for @transportUndo.
  ///
  /// In es, this message translates to:
  /// **'Undo'**
  String get transportUndo;

  /// No description provided for @transportRedo.
  ///
  /// In es, this message translates to:
  /// **'Redo'**
  String get transportRedo;

  /// No description provided for @mixerUndock.
  ///
  /// In es, this message translates to:
  /// **'Desacoplar el mixer en su propia ventana'**
  String get mixerUndock;

  /// No description provided for @mixerFxOf.
  ///
  /// In es, this message translates to:
  /// **'FX de {name}'**
  String mixerFxOf(String name);

  /// No description provided for @mixerMaster.
  ///
  /// In es, this message translates to:
  /// **'Master'**
  String get mixerMaster;

  /// No description provided for @mixerMasterFx.
  ///
  /// In es, this message translates to:
  /// **'FX de master'**
  String get mixerMasterFx;

  /// No description provided for @mixerMinusInf.
  ///
  /// In es, this message translates to:
  /// **'-inf'**
  String get mixerMinusInf;

  /// No description provided for @timelineSplitAtPlayhead.
  ///
  /// In es, this message translates to:
  /// **'Dividir en el playhead'**
  String get timelineSplitAtPlayhead;

  /// No description provided for @timelineDeleteClip.
  ///
  /// In es, this message translates to:
  /// **'Borrar clip'**
  String get timelineDeleteClip;

  /// No description provided for @timelineTracks.
  ///
  /// In es, this message translates to:
  /// **'Tracks'**
  String get timelineTracks;

  /// No description provided for @timelineAddTrack.
  ///
  /// In es, this message translates to:
  /// **'Añadir pista'**
  String get timelineAddTrack;

  /// No description provided for @timelineImport.
  ///
  /// In es, this message translates to:
  /// **'Importar'**
  String get timelineImport;

  /// No description provided for @timelineImportPrompt.
  ///
  /// In es, this message translates to:
  /// **'Importar audio'**
  String get timelineImportPrompt;

  /// No description provided for @timelineRename.
  ///
  /// In es, this message translates to:
  /// **'Rename…'**
  String get timelineRename;

  /// No description provided for @timelineColor.
  ///
  /// In es, this message translates to:
  /// **'Color…'**
  String get timelineColor;

  /// No description provided for @timelineChannel.
  ///
  /// In es, this message translates to:
  /// **'Canal…'**
  String get timelineChannel;

  /// No description provided for @timelineInputLeft.
  ///
  /// In es, this message translates to:
  /// **'Input: Left'**
  String get timelineInputLeft;

  /// No description provided for @timelineInputRight.
  ///
  /// In es, this message translates to:
  /// **'Input: Right'**
  String get timelineInputRight;

  /// No description provided for @timelineMute.
  ///
  /// In es, this message translates to:
  /// **'Mute'**
  String get timelineMute;

  /// No description provided for @timelineSolo.
  ///
  /// In es, this message translates to:
  /// **'Solo'**
  String get timelineSolo;

  /// No description provided for @timelineDeleteTrack.
  ///
  /// In es, this message translates to:
  /// **'Delete track'**
  String get timelineDeleteTrack;

  /// No description provided for @timelineRenameTrack.
  ///
  /// In es, this message translates to:
  /// **'Rename track'**
  String get timelineRenameTrack;

  /// No description provided for @timelineCancel.
  ///
  /// In es, this message translates to:
  /// **'Cancel'**
  String get timelineCancel;

  /// No description provided for @timelineOk.
  ///
  /// In es, this message translates to:
  /// **'OK'**
  String get timelineOk;

  /// No description provided for @timelineTrackColor.
  ///
  /// In es, this message translates to:
  /// **'Track color'**
  String get timelineTrackColor;

  /// No description provided for @timelineInternalSynth.
  ///
  /// In es, this message translates to:
  /// **'Sinte'**
  String get timelineInternalSynth;

  /// No description provided for @timelineTrackInstrument.
  ///
  /// In es, this message translates to:
  /// **'Instrumento de pista'**
  String get timelineTrackInstrument;

  /// No description provided for @pianoRollClipUnavailable.
  ///
  /// In es, this message translates to:
  /// **'Clip no disponible'**
  String get pianoRollClipUnavailable;

  /// No description provided for @pianoRollTitle.
  ///
  /// In es, this message translates to:
  /// **'Piano roll — {name}'**
  String pianoRollTitle(String name);

  /// No description provided for @pianoRollHint.
  ///
  /// In es, this message translates to:
  /// **'click: añadir · arrastrar: mover · borde: largo · Supr: borrar'**
  String get pianoRollHint;

  /// No description provided for @pianoRollUndock.
  ///
  /// In es, this message translates to:
  /// **'Desacoplar en su propia ventana'**
  String get pianoRollUndock;

  /// No description provided for @pianoRollToolMove.
  ///
  /// In es, this message translates to:
  /// **'Mover/estirar notas'**
  String get pianoRollToolMove;

  /// No description provided for @pianoRollToolDraw.
  ///
  /// In es, this message translates to:
  /// **'Dibujar notas'**
  String get pianoRollToolDraw;

  /// No description provided for @pianoRollToolErase.
  ///
  /// In es, this message translates to:
  /// **'Borrar notas'**
  String get pianoRollToolErase;

  /// No description provided for @pianoRollQuantize.
  ///
  /// In es, this message translates to:
  /// **'CUANTIZAR'**
  String get pianoRollQuantize;

  /// No description provided for @pianoRollSwing.
  ///
  /// In es, this message translates to:
  /// **'Swing'**
  String get pianoRollSwing;

  /// No description provided for @pianoRollCatch.
  ///
  /// In es, this message translates to:
  /// **'Catch'**
  String get pianoRollCatch;

  /// No description provided for @pianoRollMax.
  ///
  /// In es, this message translates to:
  /// **'Máx'**
  String get pianoRollMax;

  /// No description provided for @pianoRollIQ.
  ///
  /// In es, this message translates to:
  /// **'iQ'**
  String get pianoRollIQ;

  /// No description provided for @pianoRollReset.
  ///
  /// In es, this message translates to:
  /// **'Reset'**
  String get pianoRollReset;

  /// No description provided for @pianoRollFreeze.
  ///
  /// In es, this message translates to:
  /// **'Freeze'**
  String get pianoRollFreeze;

  /// No description provided for @automationAddPluginHint.
  ///
  /// In es, this message translates to:
  /// **'Añade un plugin a este canal para automatizar sus parámetros'**
  String get automationAddPluginHint;

  /// No description provided for @automationChoosePlugin.
  ///
  /// In es, this message translates to:
  /// **'Elige plugin'**
  String get automationChoosePlugin;

  /// No description provided for @automationParams.
  ///
  /// In es, this message translates to:
  /// **'Parámetros — {name}'**
  String automationParams(String name);

  /// No description provided for @automationCancel.
  ///
  /// In es, this message translates to:
  /// **'Cancelar'**
  String get automationCancel;

  /// No description provided for @automationTitleNoTrack.
  ///
  /// In es, this message translates to:
  /// **'Automatización — selecciona una pista'**
  String get automationTitleNoTrack;

  /// No description provided for @automationTitle.
  ///
  /// In es, this message translates to:
  /// **'Automatización — {name}'**
  String automationTitle(String name);

  /// No description provided for @automationVolume.
  ///
  /// In es, this message translates to:
  /// **'Volumen'**
  String get automationVolume;

  /// No description provided for @automationPan.
  ///
  /// In es, this message translates to:
  /// **'Pan'**
  String get automationPan;

  /// No description provided for @automationSustain.
  ///
  /// In es, this message translates to:
  /// **'Pedal (sustain CC64)'**
  String get automationSustain;

  /// No description provided for @automationAddParamLane.
  ///
  /// In es, this message translates to:
  /// **'Añadir lane de parámetro de plugin'**
  String get automationAddParamLane;

  /// No description provided for @automationHint.
  ///
  /// In es, this message translates to:
  /// **'click: añadir punto · arrastrar: mover · click derecho: borrar'**
  String get automationHint;

  /// No description provided for @automationUndock.
  ///
  /// In es, this message translates to:
  /// **'Desacoplar en su propia ventana'**
  String get automationUndock;

  /// No description provided for @automationNoSelection.
  ///
  /// In es, this message translates to:
  /// **'Selecciona una pista en el header para editar su automatización'**
  String get automationNoSelection;

  /// No description provided for @automationEmpty.
  ///
  /// In es, this message translates to:
  /// **'Sin automatización — click para añadir puntos'**
  String get automationEmpty;

  /// No description provided for @automationParamLaneDefault.
  ///
  /// In es, this message translates to:
  /// **'Param'**
  String get automationParamLaneDefault;

  /// No description provided for @channelUnavailable.
  ///
  /// In es, this message translates to:
  /// **'Canal no disponible'**
  String get channelUnavailable;

  /// No description provided for @channelTitle.
  ///
  /// In es, this message translates to:
  /// **'Canal — {name}'**
  String channelTitle(String name);

  /// No description provided for @channelInstrumentSection.
  ///
  /// In es, this message translates to:
  /// **'INSTRUMENTO'**
  String get channelInstrumentSection;

  /// No description provided for @channelInternalSynth.
  ///
  /// In es, this message translates to:
  /// **'Sinte interno'**
  String get channelInternalSynth;

  /// No description provided for @channelInsertsSection.
  ///
  /// In es, this message translates to:
  /// **'INSERTS'**
  String get channelInsertsSection;

  /// No description provided for @channelFxOf.
  ///
  /// In es, this message translates to:
  /// **'FX de {name}'**
  String channelFxOf(String name);

  /// No description provided for @channelAddFx.
  ///
  /// In es, this message translates to:
  /// **'Añadir FX'**
  String get channelAddFx;

  /// No description provided for @channelBypass.
  ///
  /// In es, this message translates to:
  /// **'Bypass'**
  String get channelBypass;

  /// No description provided for @channelEditPlugin.
  ///
  /// In es, this message translates to:
  /// **'Editar plugin'**
  String get channelEditPlugin;

  /// No description provided for @channelRemove.
  ///
  /// In es, this message translates to:
  /// **'Quitar'**
  String get channelRemove;

  /// No description provided for @exportTitle.
  ///
  /// In es, this message translates to:
  /// **'Exportar'**
  String get exportTitle;

  /// No description provided for @exportExported.
  ///
  /// In es, this message translates to:
  /// **'Exportado'**
  String get exportExported;

  /// No description provided for @exportOpenFolder.
  ///
  /// In es, this message translates to:
  /// **'Abrir carpeta'**
  String get exportOpenFolder;

  /// No description provided for @exportFileName.
  ///
  /// In es, this message translates to:
  /// **'Nombre del fichero'**
  String get exportFileName;

  /// No description provided for @exportWav16.
  ///
  /// In es, this message translates to:
  /// **'WAV 16-bit'**
  String get exportWav16;

  /// No description provided for @exportWav24.
  ///
  /// In es, this message translates to:
  /// **'WAV 24-bit'**
  String get exportWav24;

  /// No description provided for @exportRateInfo.
  ///
  /// In es, this message translates to:
  /// **'Frecuencia: la del proyecto ({rate} Hz) · estéreo · render offline (más rápido que el tiempo real)'**
  String exportRateInfo(String rate);

  /// No description provided for @exportMp3Soon.
  ///
  /// In es, this message translates to:
  /// **'MP3: próximamente (requiere codificador externo)'**
  String get exportMp3Soon;

  /// No description provided for @exportClose.
  ///
  /// In es, this message translates to:
  /// **'Cerrar'**
  String get exportClose;

  /// No description provided for @exportCancel.
  ///
  /// In es, this message translates to:
  /// **'Cancelar'**
  String get exportCancel;

  /// No description provided for @exportExporting.
  ///
  /// In es, this message translates to:
  /// **'Exportando…'**
  String get exportExporting;

  /// No description provided for @addTrackTitle.
  ///
  /// In es, this message translates to:
  /// **'Añadir pista'**
  String get addTrackTitle;

  /// No description provided for @addTrackName.
  ///
  /// In es, this message translates to:
  /// **'Nombre'**
  String get addTrackName;

  /// No description provided for @addTrackColor.
  ///
  /// In es, this message translates to:
  /// **'Color'**
  String get addTrackColor;

  /// No description provided for @addTrackType.
  ///
  /// In es, this message translates to:
  /// **'Tipo'**
  String get addTrackType;

  /// No description provided for @addTrackAudio.
  ///
  /// In es, this message translates to:
  /// **'Audio'**
  String get addTrackAudio;

  /// No description provided for @addTrackMidi.
  ///
  /// In es, this message translates to:
  /// **'MIDI'**
  String get addTrackMidi;

  /// No description provided for @addTrackCancel.
  ///
  /// In es, this message translates to:
  /// **'Cancelar'**
  String get addTrackCancel;

  /// No description provided for @addTrackTrack.
  ///
  /// In es, this message translates to:
  /// **'Pista'**
  String get addTrackTrack;

  /// No description provided for @addTrackCreate.
  ///
  /// In es, this message translates to:
  /// **'Crear pista'**
  String get addTrackCreate;

  /// No description provided for @addTrackDefaultName.
  ///
  /// In es, this message translates to:
  /// **'Pista {n}'**
  String addTrackDefaultName(String n);

  /// No description provided for @pluginAddTitle.
  ///
  /// In es, this message translates to:
  /// **'Añadir plugin'**
  String get pluginAddTitle;

  /// No description provided for @pluginSearchHint.
  ///
  /// In es, this message translates to:
  /// **'Buscar plugin…'**
  String get pluginSearchHint;

  /// No description provided for @pluginScanning.
  ///
  /// In es, this message translates to:
  /// **'Escaneando…'**
  String get pluginScanning;

  /// No description provided for @pluginNoResults.
  ///
  /// In es, this message translates to:
  /// **'Sin resultados'**
  String get pluginNoResults;

  /// No description provided for @pluginKindInstrument.
  ///
  /// In es, this message translates to:
  /// **'instrumento'**
  String get pluginKindInstrument;

  /// No description provided for @pluginKindEffect.
  ///
  /// In es, this message translates to:
  /// **'efecto'**
  String get pluginKindEffect;

  /// No description provided for @projectDefaultName.
  ///
  /// In es, this message translates to:
  /// **'Mi proyecto'**
  String get projectDefaultName;

  /// No description provided for @projectSaveAs.
  ///
  /// In es, this message translates to:
  /// **'Guardar proyecto como…'**
  String get projectSaveAs;

  /// No description provided for @projectNameLabel.
  ///
  /// In es, this message translates to:
  /// **'Nombre del proyecto'**
  String get projectNameLabel;

  /// No description provided for @projectCancel.
  ///
  /// In es, this message translates to:
  /// **'Cancelar'**
  String get projectCancel;

  /// No description provided for @projectSave.
  ///
  /// In es, this message translates to:
  /// **'Guardar'**
  String get projectSave;

  /// No description provided for @projectAppName.
  ///
  /// In es, this message translates to:
  /// **'EZMusicProduction'**
  String get projectAppName;

  /// No description provided for @projectVersion.
  ///
  /// In es, this message translates to:
  /// **'Versión {version}'**
  String projectVersion(String version);

  /// No description provided for @projectAuthors.
  ///
  /// In es, this message translates to:
  /// **'Creado por EzTornado — Iván Pastor Simarro'**
  String get projectAuthors;

  /// No description provided for @projectWeb.
  ///
  /// In es, this message translates to:
  /// **'www.ezmusicproduction.com'**
  String get projectWeb;

  /// No description provided for @projectClose.
  ///
  /// In es, this message translates to:
  /// **'Cerrar'**
  String get projectClose;

  /// No description provided for @soundLibTitle.
  ///
  /// In es, this message translates to:
  /// **'Biblioteca de sonidos'**
  String get soundLibTitle;

  /// No description provided for @soundLibSearchHint.
  ///
  /// In es, this message translates to:
  /// **'Buscar…'**
  String get soundLibSearchHint;

  /// No description provided for @soundLibAllTypes.
  ///
  /// In es, this message translates to:
  /// **'Todo'**
  String get soundLibAllTypes;

  /// No description provided for @soundLibLoops.
  ///
  /// In es, this message translates to:
  /// **'Bases / Loops'**
  String get soundLibLoops;

  /// No description provided for @soundLibOneShots.
  ///
  /// In es, this message translates to:
  /// **'One-shots'**
  String get soundLibOneShots;

  /// No description provided for @soundLibCategories.
  ///
  /// In es, this message translates to:
  /// **'Categorías'**
  String get soundLibCategories;

  /// No description provided for @soundLibCategoryAll.
  ///
  /// In es, this message translates to:
  /// **'Todas'**
  String get soundLibCategoryAll;

  /// No description provided for @soundLibEmpty.
  ///
  /// In es, this message translates to:
  /// **'Nada por aquí'**
  String get soundLibEmpty;

  /// No description provided for @soundLibFooter.
  ///
  /// In es, this message translates to:
  /// **'Sonidos de Wikimedia Commons e Internet Archive bajo licencias libres (CC0 / dominio público / CC-BY / BY-SA). Cada descarga guarda su fichero de atribución .license.txt — respeta la licencia en tus obras.'**
  String get soundLibFooter;

  /// No description provided for @soundLibSourceTooltip.
  ///
  /// In es, this message translates to:
  /// **'Fuente: {url}\nLicencia: {license}'**
  String soundLibSourceTooltip(String url, String license);

  /// No description provided for @soundLibAttributionAuthor.
  ///
  /// In es, this message translates to:
  /// **'Autor: {name}'**
  String soundLibAttributionAuthor(String name);

  /// No description provided for @soundLibAttributionLicense.
  ///
  /// In es, this message translates to:
  /// **'Licencia: {license}'**
  String soundLibAttributionLicense(String license);

  /// No description provided for @soundLibAddedToTrack.
  ///
  /// In es, this message translates to:
  /// **'{name} añadido a la pista seleccionada'**
  String soundLibAddedToTrack(String name);

  /// No description provided for @soundLibDownloadFailed.
  ///
  /// In es, this message translates to:
  /// **'No se pudo descargar {name}: {detail}'**
  String soundLibDownloadFailed(String name, String detail);

  /// No description provided for @soundLibLoop.
  ///
  /// In es, this message translates to:
  /// **'Loop'**
  String get soundLibLoop;

  /// No description provided for @soundLibOneShot.
  ///
  /// In es, this message translates to:
  /// **'One-shot'**
  String get soundLibOneShot;

  /// No description provided for @soundLibInLibrary.
  ///
  /// In es, this message translates to:
  /// **'En biblioteca'**
  String get soundLibInLibrary;

  /// No description provided for @soundLibStop.
  ///
  /// In es, this message translates to:
  /// **'Detener'**
  String get soundLibStop;

  /// No description provided for @soundLibListen.
  ///
  /// In es, this message translates to:
  /// **'Escuchar'**
  String get soundLibListen;

  /// No description provided for @soundLibAdd.
  ///
  /// In es, this message translates to:
  /// **'Añadir'**
  String get soundLibAdd;

  /// No description provided for @soundLibDownload.
  ///
  /// In es, this message translates to:
  /// **'Descargar'**
  String get soundLibDownload;

  /// No description provided for @soundLibLicenseFile.
  ///
  /// In es, this message translates to:
  /// **'Fuente: {url}\nDescargado por EZMusicProduction el {date}.\nComprueba los términos en la fuente enlazada antes de un uso comercial.'**
  String soundLibLicenseFile(String url, String date);

  /// No description provided for @servicePluginsLoaded.
  ///
  /// In es, this message translates to:
  /// **'Plugins cargados'**
  String get servicePluginsLoaded;

  /// No description provided for @serviceAddFailed.
  ///
  /// In es, this message translates to:
  /// **'No se pudo añadir {path} — genera los tonos con `cadence-cli tone` o revisa CADENCE_ASSETS_DIR. ({detail})'**
  String serviceAddFailed(String path, String detail);

  /// No description provided for @serviceAudioDeviceError.
  ///
  /// In es, this message translates to:
  /// **'No se pudo abrir el dispositivo de audio: {detail}'**
  String serviceAudioDeviceError(String detail);

  /// No description provided for @servicePluginsLoadedCount.
  ///
  /// In es, this message translates to:
  /// **'{count} plugins cargados'**
  String servicePluginsLoadedCount(String count);

  /// No description provided for @serviceClipCopied.
  ///
  /// In es, this message translates to:
  /// **'Clip copiado'**
  String get serviceClipCopied;

  /// No description provided for @serviceSelectTrackToPaste.
  ///
  /// In es, this message translates to:
  /// **'selecciona la pista donde pegar'**
  String get serviceSelectTrackToPaste;

  /// No description provided for @serviceClipPasted.
  ///
  /// In es, this message translates to:
  /// **'Clip pegado'**
  String get serviceClipPasted;

  /// No description provided for @serviceFxAddFailed.
  ///
  /// In es, this message translates to:
  /// **'No se pudo añadir el efecto'**
  String get serviceFxAddFailed;

  /// No description provided for @serviceSelectTrackFirst.
  ///
  /// In es, this message translates to:
  /// **'Selecciona una pista primero'**
  String get serviceSelectTrackFirst;

  /// No description provided for @serviceRecordingInProgress.
  ///
  /// In es, this message translates to:
  /// **'hay una grabación en curso'**
  String get serviceRecordingInProgress;

  /// No description provided for @serviceExportedTo.
  ///
  /// In es, this message translates to:
  /// **'Exportado: {path}'**
  String serviceExportedTo(String path);

  /// No description provided for @serviceSavedIn.
  ///
  /// In es, this message translates to:
  /// **'Guardado en {path}'**
  String serviceSavedIn(String path);

  /// No description provided for @serviceNewProject.
  ///
  /// In es, this message translates to:
  /// **'Proyecto nuevo'**
  String get serviceNewProject;

  /// No description provided for @serviceFileNotFound.
  ///
  /// In es, this message translates to:
  /// **'no existe el fichero: {path}'**
  String serviceFileNotFound(String path);

  /// No description provided for @serviceNotCubaseProject.
  ///
  /// In es, this message translates to:
  /// **'no es un proyecto de Cubase válido: {detail}'**
  String serviceNotCubaseProject(String detail);

  /// No description provided for @serviceCprNoTracks.
  ///
  /// In es, this message translates to:
  /// **'el .cpr no contiene pistas reconocibles'**
  String get serviceCprNoTracks;

  /// No description provided for @serviceCubaseImported.
  ///
  /// In es, this message translates to:
  /// **'Cubase importado: {tracks} pistas · {notes} notas'**
  String serviceCubaseImported(String tracks, String notes);

  /// No description provided for @serviceCubaseAudioClips.
  ///
  /// In es, this message translates to:
  /// **' · {placed} clips de audio'**
  String serviceCubaseAudioClips(String placed);

  /// No description provided for @serviceCubaseMissingAudio.
  ///
  /// In es, this message translates to:
  /// **' · {missing} audio sin fichero'**
  String serviceCubaseMissingAudio(String missing);

  /// No description provided for @serviceCubasePluginsRestored.
  ///
  /// In es, this message translates to:
  /// **' · {restored} con su estado'**
  String serviceCubasePluginsRestored(String restored);

  /// No description provided for @serviceLoadingCubasePlugins.
  ///
  /// In es, this message translates to:
  /// **'Cargando plugins de Cubase… {index}/{total}'**
  String serviceLoadingCubasePlugins(String index, String total);

  /// No description provided for @serviceUnrecognizedFormat.
  ///
  /// In es, this message translates to:
  /// **'Formato no reconocido: {ext}'**
  String serviceUnrecognizedFormat(String ext);

  /// No description provided for @serviceProjectLoaded.
  ///
  /// In es, this message translates to:
  /// **'Proyecto cargado: {path}'**
  String serviceProjectLoaded(String path);

  /// No description provided for @serviceProjectNeedsName.
  ///
  /// In es, this message translates to:
  /// **'pon un nombre al proyecto'**
  String get serviceProjectNeedsName;

  /// No description provided for @serviceSaved.
  ///
  /// In es, this message translates to:
  /// **'Guardado: {path}'**
  String serviceSaved(String path);

  /// No description provided for @alertTitleError.
  ///
  /// In es, this message translates to:
  /// **'Error'**
  String get alertTitleError;

  /// No description provided for @alertTitleWarning.
  ///
  /// In es, this message translates to:
  /// **'Aviso'**
  String get alertTitleWarning;

  /// No description provided for @alertTitleInfo.
  ///
  /// In es, this message translates to:
  /// **'Información'**
  String get alertTitleInfo;

  /// No description provided for @alertClose.
  ///
  /// In es, this message translates to:
  /// **'Cerrar'**
  String get alertClose;

  /// No description provided for @alertTechnicalDetail.
  ///
  /// In es, this message translates to:
  /// **'Detalles técnicos'**
  String get alertTechnicalDetail;

  /// No description provided for @prefTitle.
  ///
  /// In es, this message translates to:
  /// **'Preferencias — Audio y MIDI'**
  String get prefTitle;

  /// No description provided for @prefSectionLanguage.
  ///
  /// In es, this message translates to:
  /// **'Idioma'**
  String get prefSectionLanguage;

  /// No description provided for @prefLanguageSystem.
  ///
  /// In es, this message translates to:
  /// **'Seguir al sistema'**
  String get prefLanguageSystem;

  /// No description provided for @prefOutputDevice.
  ///
  /// In es, this message translates to:
  /// **'Dispositivo de salida'**
  String get prefOutputDevice;

  /// No description provided for @prefInputDevice.
  ///
  /// In es, this message translates to:
  /// **'Dispositivo de entrada'**
  String get prefInputDevice;

  /// No description provided for @prefSampleRate.
  ///
  /// In es, this message translates to:
  /// **'Sample rate'**
  String get prefSampleRate;

  /// No description provided for @prefBuffer.
  ///
  /// In es, this message translates to:
  /// **'Buffer'**
  String get prefBuffer;

  /// No description provided for @prefRoundTrip.
  ///
  /// In es, this message translates to:
  /// **'~{ms} ms ida y vuelta'**
  String prefRoundTrip(String ms);

  /// No description provided for @prefMidiLatency.
  ///
  /// In es, this message translates to:
  /// **'Latencia MIDI entrada'**
  String get prefMidiLatency;

  /// No description provided for @prefMidiLatencyHelp.
  ///
  /// In es, this message translates to:
  /// **'ms — desplaza las grabaciones MIDI antes; súbela si tus notas suenan tarde'**
  String get prefMidiLatencyHelp;

  /// No description provided for @prefActiveInputs.
  ///
  /// In es, this message translates to:
  /// **'Entradas activas del dispositivo: {count}'**
  String prefActiveInputs(String count);

  /// No description provided for @prefNoInputs.
  ///
  /// In es, this message translates to:
  /// **'⚠ El dispositivo activo no tiene entradas — para grabar, elige en \"Dispositivo de entrada\" uno con micrófono'**
  String get prefNoInputs;

  /// No description provided for @prefRescan.
  ///
  /// In es, this message translates to:
  /// **'Re-escanear dispositivos'**
  String get prefRescan;

  /// No description provided for @prefMidiInputTitle.
  ///
  /// In es, this message translates to:
  /// **'Entrada MIDI (elige un dispositivo)'**
  String get prefMidiInputTitle;

  /// No description provided for @prefNoMidi.
  ///
  /// In es, this message translates to:
  /// **'No hay dispositivos MIDI conectados'**
  String get prefNoMidi;

  /// No description provided for @aiTitle.
  ///
  /// In es, this message translates to:
  /// **'Generar con IA'**
  String get aiTitle;

  /// No description provided for @aiTrackLabel.
  ///
  /// In es, this message translates to:
  /// **'Pista: {name}'**
  String aiTrackLabel(String name);

  /// No description provided for @aiModeMelody.
  ///
  /// In es, this message translates to:
  /// **'Melodía'**
  String get aiModeMelody;

  /// No description provided for @aiModeHarmony.
  ///
  /// In es, this message translates to:
  /// **'Armonía'**
  String get aiModeHarmony;

  /// No description provided for @aiModeBass.
  ///
  /// In es, this message translates to:
  /// **'Bajo'**
  String get aiModeBass;

  /// No description provided for @aiModeDrums.
  ///
  /// In es, this message translates to:
  /// **'Batería'**
  String get aiModeDrums;

  /// No description provided for @aiBars.
  ///
  /// In es, this message translates to:
  /// **'Compases'**
  String get aiBars;

  /// No description provided for @aiLength.
  ///
  /// In es, this message translates to:
  /// **'Duración'**
  String get aiLength;

  /// No description provided for @aiHeardLabel.
  ///
  /// In es, this message translates to:
  /// **'Escuchado: {text}'**
  String aiHeardLabel(Object text);

  /// No description provided for @aiLongHint.
  ///
  /// In es, this message translates to:
  /// **'Los clips largos usan un candidato para que la generación sea rápida.'**
  String get aiLongHint;

  /// No description provided for @aiVariations.
  ///
  /// In es, this message translates to:
  /// **'Variaciones'**
  String get aiVariations;

  /// No description provided for @aiTemperature.
  ///
  /// In es, this message translates to:
  /// **'Creatividad'**
  String get aiTemperature;

  /// No description provided for @aiPrompt.
  ///
  /// In es, this message translates to:
  /// **'Indicación (opcional)'**
  String get aiPrompt;

  /// No description provided for @aiPromptHint.
  ///
  /// In es, this message translates to:
  /// **'p. ej. más oscuro, con arpegios, en Do menor'**
  String get aiPromptHint;

  /// No description provided for @aiHarmonize.
  ///
  /// In es, this message translates to:
  /// **'Añadir armonía'**
  String get aiHarmonize;

  /// No description provided for @aiReplaceClip.
  ///
  /// In es, this message translates to:
  /// **'Reemplazar el clip'**
  String get aiReplaceClip;

  /// No description provided for @aiGenerate.
  ///
  /// In es, this message translates to:
  /// **'Generar'**
  String get aiGenerate;

  /// No description provided for @aiRegenerate.
  ///
  /// In es, this message translates to:
  /// **'Regenerar'**
  String get aiRegenerate;

  /// No description provided for @aiApply.
  ///
  /// In es, this message translates to:
  /// **'Aplicar al proyecto'**
  String get aiApply;

  /// No description provided for @aiClose.
  ///
  /// In es, this message translates to:
  /// **'Cerrar'**
  String get aiClose;

  /// No description provided for @aiGenerating.
  ///
  /// In es, this message translates to:
  /// **'Generando…'**
  String get aiGenerating;

  /// No description provided for @aiIdleHint.
  ///
  /// In es, this message translates to:
  /// **'Elige el tipo y pulsa Generar.'**
  String get aiIdleHint;

  /// No description provided for @aiNoModel.
  ///
  /// In es, this message translates to:
  /// **'Los modelos Magenta no están instalados: se usará el generador interno.'**
  String get aiNoModel;

  /// No description provided for @aiProviderLabel.
  ///
  /// In es, this message translates to:
  /// **'Motor'**
  String get aiProviderLabel;

  /// No description provided for @aiKeyLabel.
  ///
  /// In es, this message translates to:
  /// **'Tonalidad'**
  String get aiKeyLabel;

  /// No description provided for @aiChordsLabel.
  ///
  /// In es, this message translates to:
  /// **'Acordes'**
  String get aiChordsLabel;

  /// No description provided for @aiNotesLabel.
  ///
  /// In es, this message translates to:
  /// **'Notas'**
  String get aiNotesLabel;

  /// No description provided for @aiPromptUnderstood.
  ///
  /// In es, this message translates to:
  /// **'Interpretación: {text}'**
  String aiPromptUnderstood(String text);

  /// No description provided for @aiFailed.
  ///
  /// In es, this message translates to:
  /// **'No se pudo generar: {error}'**
  String aiFailed(String error);

  /// No description provided for @aiBusy.
  ///
  /// In es, this message translates to:
  /// **'Ya hay una generación en curso'**
  String get aiBusy;

  /// No description provided for @aiApplied.
  ///
  /// In es, this message translates to:
  /// **'Clip generado aplicado'**
  String get aiApplied;

  /// No description provided for @aiGenerateClip.
  ///
  /// In es, this message translates to:
  /// **'Generar con IA…'**
  String get aiGenerateClip;

  /// No description provided for @aiRegenerateClip.
  ///
  /// In es, this message translates to:
  /// **'Regenerar con IA…'**
  String get aiRegenerateClip;

  /// No description provided for @aiGenerateAtPlayhead.
  ///
  /// In es, this message translates to:
  /// **'Generar clip con IA…'**
  String get aiGenerateAtPlayhead;

  /// No description provided for @aiToolbarButton.
  ///
  /// In es, this message translates to:
  /// **'IA'**
  String get aiToolbarButton;

  /// No description provided for @aiToolbarTooltip.
  ///
  /// In es, this message translates to:
  /// **'Generar un clip con IA en la pista de instrumento seleccionada'**
  String get aiToolbarTooltip;

  /// No description provided for @aiNoInstrumentTrack.
  ///
  /// In es, this message translates to:
  /// **'Crea o selecciona una pista de instrumento para generar con IA.'**
  String get aiNoInstrumentTrack;
}

class _AppLocalizationsDelegate
    extends LocalizationsDelegate<AppLocalizations> {
  const _AppLocalizationsDelegate();

  @override
  Future<AppLocalizations> load(Locale locale) {
    return SynchronousFuture<AppLocalizations>(lookupAppLocalizations(locale));
  }

  @override
  bool isSupported(Locale locale) =>
      <String>['en', 'es', 'ja'].contains(locale.languageCode);

  @override
  bool shouldReload(_AppLocalizationsDelegate old) => false;
}

AppLocalizations lookupAppLocalizations(Locale locale) {
  // Lookup logic when only language code is specified.
  switch (locale.languageCode) {
    case 'en':
      return AppLocalizationsEn();
    case 'es':
      return AppLocalizationsEs();
    case 'ja':
      return AppLocalizationsJa();
  }

  throw FlutterError(
    'AppLocalizations.delegate failed to load unsupported locale "$locale". This is likely '
    'an issue with the localizations generation tool. Please file an issue '
    'on GitHub with a reproducible sample app and the gen-l10n configuration '
    'that was used.',
  );
}
