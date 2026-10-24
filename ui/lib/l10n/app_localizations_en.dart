// ignore: unused_import
import 'package:intl/intl.dart' as intl;
import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for English (`en`).
class AppLocalizationsEn extends AppLocalizations {
  AppLocalizationsEn([String locale = 'en']) : super(locale);

  @override
  String get appTitle => 'Cadence';

  @override
  String get appNoClip => 'No clip';

  @override
  String get appEngineUnavailable => 'Engine unavailable';

  @override
  String appDylibError(String error) {
    return 'Could not load libcadence_engine.dylib: $error\nBuild the engine: cmake --build ~/daw/build';
  }

  @override
  String get appDylibOk => 'dylib ok';

  @override
  String appStatusLine(String tracks, String duration) {
    return '$tracks tracks · $duration s · dylib ok';
  }

  @override
  String get appWindowMixer => 'Mixer — EZMusicProduction';

  @override
  String get appWindowPianoRoll => 'Piano roll';

  @override
  String get appWindowAutomation => 'Automation';

  @override
  String get appMenuNew => 'New';

  @override
  String get appMenuOpen => 'Open…';

  @override
  String get appMenuSave => 'Save';

  @override
  String get appMenuExport => 'Export';

  @override
  String get appMenuSounds => 'Sounds';

  @override
  String get appMenuGrid => 'Grid';

  @override
  String get appMenuMixer => 'Mixer';

  @override
  String get appMenuPianoRoll => 'Piano roll';

  @override
  String get appMenuAutomation => 'Auto';

  @override
  String get appMenuPreferences => 'Preferences (audio, MIDI)';

  @override
  String get transportTempoTimeSig => 'Tempo and time signature';

  @override
  String get transportTempoHint => 'Tempo (BPM, 20–300)';

  @override
  String get transportTimeSig => 'Time signature';

  @override
  String get transportCancel => 'Cancel';

  @override
  String get transportApply => 'Apply';

  @override
  String get transportRecord => 'Record';

  @override
  String get transportStopRecording => 'Stop recording';

  @override
  String get transportMetronome => 'Metronome';

  @override
  String get transportFinishRecording => 'Finish recording (same as R)';

  @override
  String get transportPlayPause => 'Play / Pause';

  @override
  String get transportStop => 'Stop';

  @override
  String get transportIQ => 'IQ:';

  @override
  String get transportOff => 'Off';

  @override
  String get transportRestartAudio =>
      'Restart audio (recovers sound if it drops out)';

  @override
  String get transportUndo => 'Undo';

  @override
  String get transportRedo => 'Redo';

  @override
  String get mixerUndock => 'Undock the mixer into its own window';

  @override
  String mixerFxOf(String name) {
    return 'FX for $name';
  }

  @override
  String get mixerMaster => 'Master';

  @override
  String get mixerMasterFx => 'Master FX';

  @override
  String get mixerMinusInf => '-inf';

  @override
  String get timelineSplitAtPlayhead => 'Split at playhead';

  @override
  String get timelineDeleteClip => 'Delete clip';

  @override
  String get timelineTracks => 'Tracks';

  @override
  String get timelineAddTrack => 'Add track';

  @override
  String get timelineImport => 'Import';

  @override
  String get timelineImportPrompt => 'Import audio';

  @override
  String get timelineRename => 'Rename…';

  @override
  String get timelineColor => 'Color…';

  @override
  String get timelineChannel => 'Channel…';

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
  String get timelineInternalSynth => 'Synth';

  @override
  String get timelineTrackInstrument => 'Track instrument';

  @override
  String get pianoRollClipUnavailable => 'Clip unavailable';

  @override
  String pianoRollTitle(String name) {
    return 'Piano roll — $name';
  }

  @override
  String get pianoRollHint =>
      'drag empty: select · drag: move · edge: length · Del: delete';

  @override
  String get pianoRollUndock => 'Undock into its own window';

  @override
  String get pianoRollToolMove => 'Move/resize notes';

  @override
  String get pianoRollToolDraw => 'Draw notes';

  @override
  String get pianoRollToolErase => 'Erase notes';

  @override
  String get pianoRollQuantize => 'QUANTIZE';

  @override
  String get pianoRollSwing => 'Swing';

  @override
  String get pianoRollCatch => 'Catch';

  @override
  String get pianoRollMax => 'Max';

  @override
  String get pianoRollIQ => 'iQ';

  @override
  String get pianoRollReset => 'Reset';

  @override
  String get pianoRollFreeze => 'Freeze';

  @override
  String get automationAddPluginHint =>
      'Add a plugin to this channel to automate its parameters';

  @override
  String get automationChoosePlugin => 'Choose plugin';

  @override
  String automationParams(String name) {
    return 'Parameters — $name';
  }

  @override
  String get automationCancel => 'Cancel';

  @override
  String get automationTitleNoTrack => 'Automation — select a track';

  @override
  String automationTitle(String name) {
    return 'Automation — $name';
  }

  @override
  String get automationVolume => 'Volume';

  @override
  String get automationPan => 'Pan';

  @override
  String get automationSustain => 'Pedal (sustain CC64)';

  @override
  String get automationAddParamLane => 'Add plugin parameter lane';

  @override
  String get automationHint =>
      'click: add point · drag: move · right-click: delete';

  @override
  String get automationUndock => 'Undock into its own window';

  @override
  String get automationNoSelection =>
      'Select a track in the header to edit its automation';

  @override
  String get automationEmpty => 'No automation — click to add points';

  @override
  String get automationParamLaneDefault => 'Param';

  @override
  String get channelUnavailable => 'Channel unavailable';

  @override
  String channelTitle(String name) {
    return 'Channel — $name';
  }

  @override
  String get channelInstrumentSection => 'INSTRUMENT';

  @override
  String get channelInternalSynth => 'Internal synth';

  @override
  String get channelInsertsSection => 'INSERTS';

  @override
  String channelFxOf(String name) {
    return 'FX for $name';
  }

  @override
  String get channelAddFx => 'Add FX';

  @override
  String get channelBypass => 'Bypass';

  @override
  String get channelEditPlugin => 'Edit plugin';

  @override
  String get channelRemove => 'Remove';

  @override
  String get exportTitle => 'Export';

  @override
  String get exportExported => 'Exported';

  @override
  String get exportOpenFolder => 'Open folder';

  @override
  String get exportFileName => 'File name';

  @override
  String get exportWav16 => 'WAV 16-bit';

  @override
  String get exportWav24 => 'WAV 24-bit';

  @override
  String exportRateInfo(String rate) {
    return 'Rate: project rate ($rate Hz) · stereo · offline render (faster than real time)';
  }

  @override
  String get exportMp3Soon => 'MP3: coming soon (needs an external encoder)';

  @override
  String get exportClose => 'Close';

  @override
  String get exportCancel => 'Cancel';

  @override
  String get exportExporting => 'Exporting…';

  @override
  String get addTrackTitle => 'Add track';

  @override
  String get addTrackName => 'Name';

  @override
  String get addTrackColor => 'Color';

  @override
  String get addTrackType => 'Type';

  @override
  String get addTrackAudio => 'Audio';

  @override
  String get addTrackMidi => 'MIDI';

  @override
  String get addTrackCancel => 'Cancel';

  @override
  String get addTrackTrack => 'Track';

  @override
  String get addTrackCreate => 'Create track';

  @override
  String addTrackDefaultName(String n) {
    return 'Track $n';
  }

  @override
  String get pluginAddTitle => 'Add plugin';

  @override
  String get pluginSearchHint => 'Search plugin…';

  @override
  String get pluginScanning => 'Scanning…';

  @override
  String get pluginNoResults => 'No results';

  @override
  String get pluginKindInstrument => 'instrument';

  @override
  String get pluginKindEffect => 'effect';

  @override
  String get projectDefaultName => 'My project';

  @override
  String get projectSaveAs => 'Save project as…';

  @override
  String get projectNameLabel => 'Project name';

  @override
  String get projectCancel => 'Cancel';

  @override
  String get projectSave => 'Save';

  @override
  String get projectAppName => 'EZMusicProduction';

  @override
  String projectVersion(String version) {
    return 'Version $version';
  }

  @override
  String get projectAuthors => 'Created by EzTornado — Iván Pastor Simarro';

  @override
  String get projectWeb => 'www.ezmusicproduction.com';

  @override
  String get projectClose => 'Close';

  @override
  String get soundLibTitle => 'Sound library';

  @override
  String get soundLibSearchHint => 'Search…';

  @override
  String get soundLibAllTypes => 'All';

  @override
  String get soundLibLoops => 'Beds / Loops';

  @override
  String get soundLibOneShots => 'One-shots';

  @override
  String get soundLibCategories => 'Categories';

  @override
  String get soundLibCategoryAll => 'All';

  @override
  String get soundLibEmpty => 'Nothing here';

  @override
  String get soundLibFooter =>
      'Sounds from Wikimedia Commons and the Internet Archive under free licences (CC0 / public domain / CC-BY / BY-SA). Every download keeps its .license.txt attribution file — respect the licence in your work.';

  @override
  String soundLibSourceTooltip(String url, String license) {
    return 'Source: $url\nLicence: $license';
  }

  @override
  String soundLibAttributionAuthor(String name) {
    return 'Author: $name';
  }

  @override
  String soundLibAttributionLicense(String license) {
    return 'Licence: $license';
  }

  @override
  String soundLibAddedToTrack(String name) {
    return '$name added to the selected track';
  }

  @override
  String soundLibDownloadFailed(String name, String detail) {
    return 'Could not download $name: $detail';
  }

  @override
  String get soundLibLoop => 'Loop';

  @override
  String get soundLibOneShot => 'One-shot';

  @override
  String get soundLibInLibrary => 'In library';

  @override
  String get soundLibStop => 'Stop';

  @override
  String get soundLibListen => 'Audition';

  @override
  String get soundLibAdd => 'Add';

  @override
  String get soundLibDownload => 'Download';

  @override
  String soundLibLicenseFile(String url, String date) {
    return 'Source: $url\nDownloaded by EZMusicProduction on $date.\nCheck the terms at the linked source before commercial use.';
  }

  @override
  String get servicePluginsLoaded => 'Plugins loaded';

  @override
  String serviceAddFailed(String path, String detail) {
    return 'Could not add $path — generate the tones with `cadence-cli tone` or check CADENCE_ASSETS_DIR. ($detail)';
  }

  @override
  String serviceAudioDeviceError(String detail) {
    return 'Could not open the audio device: $detail';
  }

  @override
  String servicePluginsLoadedCount(String count) {
    return '$count plugins loaded';
  }

  @override
  String get serviceClipCopied => 'Clip copied';

  @override
  String get serviceSelectTrackToPaste => 'select the track to paste into';

  @override
  String get serviceClipPasted => 'Clip pasted';

  @override
  String get serviceFxAddFailed => 'Could not add the effect';

  @override
  String get serviceSelectTrackFirst => 'Select a track first';

  @override
  String get serviceRecordingInProgress => 'a recording is in progress';

  @override
  String serviceExportedTo(String path) {
    return 'Exported: $path';
  }

  @override
  String serviceSavedIn(String path) {
    return 'Saved to $path';
  }

  @override
  String get serviceNewProject => 'New project';

  @override
  String serviceFileNotFound(String path) {
    return 'file not found: $path';
  }

  @override
  String serviceNotCubaseProject(String detail) {
    return 'not a valid Cubase project: $detail';
  }

  @override
  String get serviceCprNoTracks => 'the .cpr contains no recognizable tracks';

  @override
  String serviceCubaseImported(String tracks, String notes) {
    return 'Cubase imported: $tracks tracks · $notes notes';
  }

  @override
  String serviceCubaseAudioClips(String placed) {
    return ' · $placed audio clips';
  }

  @override
  String serviceCubaseMissingAudio(String missing) {
    return ' · $missing audio without a file';
  }

  @override
  String serviceCubasePluginsRestored(String restored) {
    return ' · $restored with their state';
  }

  @override
  String serviceLoadingCubasePlugins(String index, String total) {
    return 'Loading Cubase plugins… $index/$total';
  }

  @override
  String serviceUnrecognizedFormat(String ext) {
    return 'Unrecognized format: $ext';
  }

  @override
  String serviceProjectLoaded(String path) {
    return 'Project loaded: $path';
  }

  @override
  String get serviceProjectNeedsName => 'give the project a name';

  @override
  String serviceSaved(String path) {
    return 'Saved: $path';
  }

  @override
  String get alertTitleError => 'Error';

  @override
  String get alertTitleWarning => 'Warning';

  @override
  String get alertTitleInfo => 'Information';

  @override
  String get alertClose => 'Close';

  @override
  String get alertTechnicalDetail => 'Technical details';

  @override
  String get prefTitle => 'Preferences — Audio and MIDI';

  @override
  String get prefSectionLanguage => 'Language';

  @override
  String get prefLanguageSystem => 'Follow system';

  @override
  String get prefOutputDevice => 'Output device';

  @override
  String get prefInputDevice => 'Input device';

  @override
  String get prefSampleRate => 'Sample rate';

  @override
  String get prefBuffer => 'Buffer';

  @override
  String prefRoundTrip(String ms) {
    return '~$ms ms round trip';
  }

  @override
  String get prefMidiLatency => 'MIDI input latency';

  @override
  String get prefMidiLatencyHelp =>
      'ms — shifts MIDI recordings earlier; raise it if your notes sound late';

  @override
  String prefActiveInputs(String count) {
    return 'Active device inputs: $count';
  }

  @override
  String get prefNoInputs =>
      '⚠ The active device has no inputs — to record, pick one with a microphone in \"Input device\"';

  @override
  String get prefRescan => 'Rescan devices';

  @override
  String get prefMidiInputTitle => 'MIDI input (pick a device)';

  @override
  String get prefNoMidi => 'No MIDI devices connected';

  @override
  String get aiTitle => 'Generate with AI';

  @override
  String aiTrackLabel(String name) {
    return 'Track: $name';
  }

  @override
  String get aiModeMelody => 'Melody';

  @override
  String get aiModeHarmony => 'Harmony';

  @override
  String get aiModeBass => 'Bass';

  @override
  String get aiModeDrums => 'Drums';

  @override
  String get aiBars => 'Bars';

  @override
  String get aiLength => 'Length';

  @override
  String aiHeardLabel(Object text) {
    return 'Heard: $text';
  }

  @override
  String get aiLongHint =>
      'Long clips use one candidate so generation stays quick.';

  @override
  String get aiVariations => 'Variations';

  @override
  String get aiTemperature => 'Creativity';

  @override
  String get aiPrompt => 'Prompt (optional)';

  @override
  String get aiPromptHint => 'e.g. darker, with arpeggios, in C minor';

  @override
  String get aiHarmonize => 'Add harmony';

  @override
  String get aiReplaceClip => 'Replace the clip';

  @override
  String get aiGenerate => 'Generate';

  @override
  String get aiRegenerate => 'Regenerate';

  @override
  String get aiApply => 'Apply to project';

  @override
  String get aiClose => 'Close';

  @override
  String get aiGenerating => 'Generating…';

  @override
  String get aiIdleHint => 'Pick a type and press Generate.';

  @override
  String get aiNoModel =>
      'Magenta models are not installed: the built-in generator will be used.';

  @override
  String get aiProviderLabel => 'Engine';

  @override
  String get aiKeyLabel => 'Key';

  @override
  String get aiChordsLabel => 'Chords';

  @override
  String get aiNotesLabel => 'Notes';

  @override
  String aiPromptUnderstood(String text) {
    return 'Interpreted as: $text';
  }

  @override
  String aiFailed(String error) {
    return 'Could not generate: $error';
  }

  @override
  String get aiBusy => 'A generation is already running';

  @override
  String get aiApplied => 'Generated clip applied';

  @override
  String get aiGenerateClip => 'Generate with AI…';

  @override
  String get aiRegenerateClip => 'Regenerate with AI…';

  @override
  String get aiGenerateAtPlayhead => 'Generate clip with AI…';

  @override
  String get aiToolbarButton => 'AI';

  @override
  String get aiToolbarTooltip =>
      'Generate a clip with AI on the selected instrument track';

  @override
  String get aiNoInstrumentTrack =>
      'Create or select an instrument track to generate with AI.';
}
