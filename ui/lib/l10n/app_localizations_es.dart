// ignore: unused_import
import 'package:intl/intl.dart' as intl;
import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for Spanish Castilian (`es`).
class AppLocalizationsEs extends AppLocalizations {
  AppLocalizationsEs([String locale = 'es']) : super(locale);

  @override
  String get appTitle => 'Cadence';

  @override
  String get appNoClip => 'Sin clip';

  @override
  String get appEngineUnavailable => 'Engine no disponible';

  @override
  String appDylibError(String error) {
    return 'No se pudo cargar libcadence_engine.dylib: $error\nCompila el engine: cmake --build ~/daw/build';
  }

  @override
  String get appDylibOk => 'dylib ok';

  @override
  String appStatusLine(String tracks, String duration) {
    return '$tracks pistas · $duration s · dylib ok';
  }

  @override
  String get appWindowMixer => 'Mixer — EZMusicProduction';

  @override
  String get appWindowPianoRoll => 'Piano roll';

  @override
  String get appWindowAutomation => 'Automatización';

  @override
  String get appMenuNew => 'Nuevo';

  @override
  String get appMenuOpen => 'Abrir…';

  @override
  String get appMenuSave => 'Guardar';

  @override
  String get appMenuExport => 'Exportar';

  @override
  String get appMenuSounds => 'Sonidos';

  @override
  String get appMenuGrid => 'Rejilla';

  @override
  String get appMenuMixer => 'Mixer';

  @override
  String get appMenuPianoRoll => 'Piano roll';

  @override
  String get appMenuAutomation => 'Auto';

  @override
  String get appMenuPreferences => 'Preferencias (audio, MIDI, remoto)';

  @override
  String get appMenuRemote => 'Control remoto';

  @override
  String get transportTempoTimeSig => 'Tempo y compás';

  @override
  String get transportTempoHint => 'Tempo (BPM, 20–300)';

  @override
  String get transportTimeSig => 'Compás';

  @override
  String get transportCancel => 'Cancelar';

  @override
  String get transportApply => 'Aplicar';

  @override
  String get transportRecord => 'Grabar';

  @override
  String get transportStopRecording => 'Detener grabación';

  @override
  String get transportMetronome => 'Metrónomo';

  @override
  String get transportFinishRecording => 'Finalizar grabación (equivale a R)';

  @override
  String get transportPlayPause => 'Play / Pausa';

  @override
  String get transportStop => 'Stop';

  @override
  String get transportIQ => 'IQ:';

  @override
  String get transportOff => 'Off';

  @override
  String get transportRestartAudio =>
      'Reiniciar audio (recupera el sonido si se corta)';

  @override
  String get transportUndo => 'Undo';

  @override
  String get transportRedo => 'Redo';

  @override
  String get mixerUndock => 'Desacoplar el mixer en su propia ventana';

  @override
  String mixerFxOf(String name) {
    return 'FX de $name';
  }

  @override
  String get mixerMaster => 'Master';

  @override
  String get mixerMasterFx => 'FX de master';

  @override
  String get mixerMinusInf => '-inf';

  @override
  String get timelineSplitAtPlayhead => 'Dividir en el playhead';

  @override
  String get timelineDeleteClip => 'Borrar clip';

  @override
  String get timelineTracks => 'Tracks';

  @override
  String get timelineAddTrack => 'Añadir pista';

  @override
  String get timelineImport => 'Importar';

  @override
  String get timelineImportPrompt => 'Importar audio';

  @override
  String get timelineRename => 'Rename…';

  @override
  String get timelineColor => 'Color…';

  @override
  String get timelineChannel => 'Canal…';

  @override
  String get timelineInputLeft => 'Input: Left';

  @override
  String get timelineInputRight => 'Input: Right';

  @override
  String get timelineMute => 'Mute';

  @override
  String get timelineSolo => 'Solo';

  @override
  String get timelineDeleteTrack => 'Delete track';

  @override
  String get timelineRenameTrack => 'Rename track';

  @override
  String get timelineCancel => 'Cancel';

  @override
  String get timelineOk => 'OK';

  @override
  String get timelineTrackColor => 'Track color';

  @override
  String get timelineInternalSynth => 'Sinte';

  @override
  String get timelineTrackInstrument => 'Instrumento de pista';

  @override
  String get pianoRollClipUnavailable => 'Clip no disponible';

  @override
  String pianoRollTitle(String name) {
    return 'Piano roll — $name';
  }

  @override
  String get pianoRollHint =>
      'arrastrar en vacío: seleccionar · arrastrar: mover · borde: largo · Supr: borrar';

  @override
  String get pianoRollUndock => 'Desacoplar en su propia ventana';

  @override
  String get pianoRollToolMove => 'Mover/estirar notas';

  @override
  String get pianoRollToolDraw => 'Dibujar notas';

  @override
  String get pianoRollToolErase => 'Borrar notas';

  @override
  String get pianoRollQuantize => 'CUANTIZAR';

  @override
  String get pianoRollSwing => 'Swing';

  @override
  String get pianoRollCatch => 'Catch';

  @override
  String get pianoRollMax => 'Máx';

  @override
  String get pianoRollIQ => 'iQ';

  @override
  String get pianoRollReset => 'Reset';

  @override
  String get pianoRollFreeze => 'Freeze';

  @override
  String get automationAddPluginHint =>
      'Añade un plugin a este canal para automatizar sus parámetros';

  @override
  String get automationChoosePlugin => 'Elige plugin';

  @override
  String automationParams(String name) {
    return 'Parámetros — $name';
  }

  @override
  String get automationCancel => 'Cancelar';

  @override
  String get automationTitleNoTrack => 'Automatización — selecciona una pista';

  @override
  String automationTitle(String name) {
    return 'Automatización — $name';
  }

  @override
  String get automationVolume => 'Volumen';

  @override
  String get automationPan => 'Pan';

  @override
  String get automationSustain => 'Pedal (sustain CC64)';

  @override
  String get automationAddParamLane => 'Añadir lane de parámetro de plugin';

  @override
  String get automationHint =>
      'click: añadir punto · arrastrar: mover · click derecho: borrar';

  @override
  String get automationUndock => 'Desacoplar en su propia ventana';

  @override
  String get automationNoSelection =>
      'Selecciona una pista en el header para editar su automatización';

  @override
  String get automationEmpty => 'Sin automatización — click para añadir puntos';

  @override
  String get automationParamLaneDefault => 'Param';

  @override
  String get channelUnavailable => 'Canal no disponible';

  @override
  String channelTitle(String name) {
    return 'Canal — $name';
  }

  @override
  String get channelInstrumentSection => 'INSTRUMENTO';

  @override
  String get channelInternalSynth => 'Sinte interno';

  @override
  String get channelInsertsSection => 'INSERTS';

  @override
  String channelFxOf(String name) {
    return 'FX de $name';
  }

  @override
  String get channelAddFx => 'Añadir FX';

  @override
  String get channelBypass => 'Bypass';

  @override
  String get channelEditPlugin => 'Editar plugin';

  @override
  String get channelRemove => 'Quitar';

  @override
  String get exportTitle => 'Exportar';

  @override
  String get exportExported => 'Exportado';

  @override
  String get exportOpenFolder => 'Abrir carpeta';

  @override
  String get exportFileName => 'Nombre del fichero';

  @override
  String get exportWav16 => 'WAV 16-bit';

  @override
  String get exportWav24 => 'WAV 24-bit';

  @override
  String exportRateInfo(String rate) {
    return 'Frecuencia: la del proyecto ($rate Hz) · estéreo · render offline (más rápido que el tiempo real)';
  }

  @override
  String get exportMp3Soon =>
      'MP3: próximamente (requiere codificador externo)';

  @override
  String get exportClose => 'Cerrar';

  @override
  String get exportCancel => 'Cancelar';

  @override
  String get exportExporting => 'Exportando…';

  @override
  String get addTrackTitle => 'Añadir pista';

  @override
  String get addTrackName => 'Nombre';

  @override
  String get addTrackColor => 'Color';

  @override
  String get addTrackType => 'Tipo';

  @override
  String get addTrackAudio => 'Audio';

  @override
  String get addTrackMidi => 'MIDI';

  @override
  String get addTrackCancel => 'Cancelar';

  @override
  String get addTrackTrack => 'Pista';

  @override
  String get addTrackCreate => 'Crear pista';

  @override
  String addTrackDefaultName(String n) {
    return 'Pista $n';
  }

  @override
  String get pluginAddTitle => 'Añadir plugin';

  @override
  String get pluginSearchHint => 'Buscar plugin…';

  @override
  String get pluginScanning => 'Escaneando…';

  @override
  String get pluginNoResults => 'Sin resultados';

  @override
  String get pluginKindInstrument => 'instrumento';

  @override
  String get pluginKindEffect => 'efecto';

  @override
  String get projectDefaultName => 'Mi proyecto';

  @override
  String get projectSaveAs => 'Guardar proyecto como…';

  @override
  String get projectNameLabel => 'Nombre del proyecto';

  @override
  String get projectCancel => 'Cancelar';

  @override
  String get projectSave => 'Guardar';

  @override
  String get projectAppName => 'EZMusicProduction';

  @override
  String projectVersion(String version) {
    return 'Versión $version';
  }

  @override
  String get projectAuthors => 'Creado por EzTornado — Iván Pastor Simarro';

  @override
  String get projectWeb => 'www.ezmusicproduction.com';

  @override
  String get projectClose => 'Cerrar';

  @override
  String get soundLibTitle => 'Biblioteca de sonidos';

  @override
  String get soundLibSearchHint => 'Buscar…';

  @override
  String get soundLibAllTypes => 'Todo';

  @override
  String get soundLibLoops => 'Bases / Loops';

  @override
  String get soundLibOneShots => 'One-shots';

  @override
  String get soundLibCategories => 'Categorías';

  @override
  String get soundLibCategoryAll => 'Todas';

  @override
  String get soundLibEmpty => 'Nada por aquí';

  @override
  String get soundLibFooter =>
      'Sonidos de Wikimedia Commons e Internet Archive bajo licencias libres (CC0 / dominio público / CC-BY / BY-SA). Cada descarga guarda su fichero de atribución .license.txt — respeta la licencia en tus obras.';

  @override
  String soundLibSourceTooltip(String url, String license) {
    return 'Fuente: $url\nLicencia: $license';
  }

  @override
  String soundLibAttributionAuthor(String name) {
    return 'Autor: $name';
  }

  @override
  String soundLibAttributionLicense(String license) {
    return 'Licencia: $license';
  }

  @override
  String soundLibAddedToTrack(String name) {
    return '$name añadido a la pista seleccionada';
  }

  @override
  String soundLibDownloadFailed(String name, String detail) {
    return 'No se pudo descargar $name: $detail';
  }

  @override
  String get soundLibLoop => 'Loop';

  @override
  String get soundLibOneShot => 'One-shot';

  @override
  String get soundLibInLibrary => 'En biblioteca';

  @override
  String get soundLibStop => 'Detener';

  @override
  String get soundLibListen => 'Escuchar';

  @override
  String get soundLibAdd => 'Añadir';

  @override
  String get soundLibDownload => 'Descargar';

  @override
  String soundLibLicenseFile(String url, String date) {
    return 'Fuente: $url\nDescargado por EZMusicProduction el $date.\nComprueba los términos en la fuente enlazada antes de un uso comercial.';
  }

  @override
  String get servicePluginsLoaded => 'Plugins cargados';

  @override
  String serviceAddFailed(String path, String detail) {
    return 'No se pudo añadir $path — genera los tonos con `cadence-cli tone` o revisa CADENCE_ASSETS_DIR. ($detail)';
  }

  @override
  String serviceAudioDeviceError(String detail) {
    return 'No se pudo abrir el dispositivo de audio: $detail';
  }

  @override
  String servicePluginsLoadedCount(String count) {
    return '$count plugins cargados';
  }

  @override
  String get serviceClipCopied => 'Clip copiado';

  @override
  String get serviceSelectTrackToPaste => 'selecciona la pista donde pegar';

  @override
  String get serviceClipPasted => 'Clip pegado';

  @override
  String get serviceFxAddFailed => 'No se pudo añadir el efecto';

  @override
  String get serviceSelectTrackFirst => 'Selecciona una pista primero';

  @override
  String get serviceRecordingInProgress => 'hay una grabación en curso';

  @override
  String serviceExportedTo(String path) {
    return 'Exportado: $path';
  }

  @override
  String serviceSavedIn(String path) {
    return 'Guardado en $path';
  }

  @override
  String get serviceNewProject => 'Proyecto nuevo';

  @override
  String serviceFileNotFound(String path) {
    return 'no existe el fichero: $path';
  }

  @override
  String serviceNotCubaseProject(String detail) {
    return 'no es un proyecto de Cubase válido: $detail';
  }

  @override
  String get serviceCprNoTracks => 'el .cpr no contiene pistas reconocibles';

  @override
  String serviceCubaseImported(String tracks, String notes) {
    return 'Cubase importado: $tracks pistas · $notes notas';
  }

  @override
  String serviceCubaseAudioClips(String placed) {
    return ' · $placed clips de audio';
  }

  @override
  String serviceCubaseMissingAudio(String missing) {
    return ' · $missing audio sin fichero';
  }

  @override
  String serviceCubasePluginsRestored(String restored) {
    return ' · $restored con su estado';
  }

  @override
  String serviceLoadingCubasePlugins(String index, String total) {
    return 'Cargando plugins de Cubase… $index/$total';
  }

  @override
  String serviceUnrecognizedFormat(String ext) {
    return 'Formato no reconocido: $ext';
  }

  @override
  String serviceProjectLoaded(String path) {
    return 'Proyecto cargado: $path';
  }

  @override
  String get serviceProjectNeedsName => 'pon un nombre al proyecto';

  @override
  String serviceSaved(String path) {
    return 'Guardado: $path';
  }

  @override
  String get alertTitleError => 'Error';

  @override
  String get alertTitleWarning => 'Aviso';

  @override
  String get alertTitleInfo => 'Información';

  @override
  String get alertClose => 'Cerrar';

  @override
  String get alertTechnicalDetail => 'Detalles técnicos';

  @override
  String get prefTitle => 'Preferencias — Audio, MIDI y control remoto';

  @override
  String get prefSectionLanguage => 'Idioma';

  @override
  String get prefLanguageSystem => 'Seguir al sistema';

  @override
  String get prefOutputDevice => 'Dispositivo de salida';

  @override
  String get prefInputDevice => 'Dispositivo de entrada';

  @override
  String get prefSampleRate => 'Sample rate';

  @override
  String get prefBuffer => 'Buffer';

  @override
  String prefRoundTrip(String ms) {
    return '~$ms ms ida y vuelta';
  }

  @override
  String get prefMidiLatency => 'Latencia MIDI entrada';

  @override
  String get prefMidiLatencyHelp =>
      'ms — desplaza las grabaciones MIDI antes; súbela si tus notas suenan tarde';

  @override
  String prefActiveInputs(String count) {
    return 'Entradas activas del dispositivo: $count';
  }

  @override
  String get prefNoInputs =>
      '⚠ El dispositivo activo no tiene entradas — para grabar, elige en \"Dispositivo de entrada\" uno con micrófono';

  @override
  String get prefRescan => 'Re-escanear dispositivos';

  @override
  String get prefMidiInputTitle => 'Entrada MIDI (elige un dispositivo)';

  @override
  String get prefNoMidi => 'No hay dispositivos MIDI conectados';

  @override
  String get aiTitle => 'Generar con IA';

  @override
  String aiTrackLabel(String name) {
    return 'Pista: $name';
  }

  @override
  String get aiModeMelody => 'Melodía';

  @override
  String get aiModeHarmony => 'Armonía';

  @override
  String get aiModeBass => 'Bajo';

  @override
  String get aiModeDrums => 'Batería';

  @override
  String get aiBars => 'Compases';

  @override
  String get aiLength => 'Duración';

  @override
  String aiHeardLabel(Object text) {
    return 'Escuchado: $text';
  }

  @override
  String get aiLongHint =>
      'Los clips largos usan un candidato para que la generación sea rápida.';

  @override
  String get aiVariations => 'Variaciones';

  @override
  String get aiTemperature => 'Creatividad';

  @override
  String get aiPrompt => 'Indicación (opcional)';

  @override
  String get aiPromptHint => 'p. ej. más oscuro, con arpegios, en Do menor';

  @override
  String get aiHarmonize => 'Añadir armonía';

  @override
  String get aiReplaceClip => 'Reemplazar el clip';

  @override
  String get aiGenerate => 'Generar';

  @override
  String get aiRegenerate => 'Regenerar';

  @override
  String get aiApply => 'Aplicar al proyecto';

  @override
  String get aiClose => 'Cerrar';

  @override
  String get aiGenerating => 'Generando…';

  @override
  String get aiIdleHint => 'Elige el tipo y pulsa Generar.';

  @override
  String get aiNoModel =>
      'Los modelos Magenta no están instalados: se usará el generador interno.';

  @override
  String get aiProviderLabel => 'Motor';

  @override
  String get aiKeyLabel => 'Tonalidad';

  @override
  String get aiChordsLabel => 'Acordes';

  @override
  String get aiNotesLabel => 'Notas';

  @override
  String aiPromptUnderstood(String text) {
    return 'Interpretación: $text';
  }

  @override
  String aiFailed(String error) {
    return 'No se pudo generar: $error';
  }

  @override
  String get aiBusy => 'Ya hay una generación en curso';

  @override
  String get aiApplied => 'Clip generado aplicado';

  @override
  String get aiGenerateClip => 'Generar con IA…';

  @override
  String get aiRegenerateClip => 'Regenerar con IA…';

  @override
  String get aiGenerateAtPlayhead => 'Generar clip con IA…';

  @override
  String get aiToolbarButton => 'IA';

  @override
  String get aiToolbarTooltip =>
      'Generar un clip con IA en la pista de instrumento seleccionada';

  @override
  String get aiNoInstrumentTrack =>
      'Crea o selecciona una pista de instrumento para generar con IA.';

  @override
  String get prefSectionRemote => 'Control remoto';

  @override
  String get prefRemoteEnable => 'Activar control remoto por LAN';

  @override
  String get prefRemotePort => 'Puerto';

  @override
  String get prefRemoteCode => 'Código de emparejamiento';

  @override
  String get prefRemoteCopy => 'Copiar';

  @override
  String get prefRemoteCopied => 'Código copiado';

  @override
  String get prefRemoteNewCode => 'Nuevo código';

  @override
  String get prefRemoteAddress => 'Dirección';

  @override
  String prefRemoteClients(int count) {
    return '$count cliente(s) conectado(s)';
  }

  @override
  String prefRemoteRunning(int port) {
    return 'Servidor activo en el puerto $port';
  }

  @override
  String get prefRemoteStopped => 'Servidor detenido';

  @override
  String get prefRemoteWarning =>
      'Cualquiera en tu red local que tenga el código puede controlar esta DAW.';

  @override
  String prefRemoteError(String error) {
    return 'No se pudo iniciar: $error';
  }

  @override
  String get prefRemoteHelp =>
      'Abre la app remota y escribe esta dirección y el código.';
}
