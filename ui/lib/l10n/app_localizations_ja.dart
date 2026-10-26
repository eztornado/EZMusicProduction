// ignore: unused_import
import 'package:intl/intl.dart' as intl;
import 'app_localizations.dart';

// ignore_for_file: type=lint

/// The translations for Japanese (`ja`).
class AppLocalizationsJa extends AppLocalizations {
  AppLocalizationsJa([String locale = 'ja']) : super(locale);

  @override
  String get appTitle => 'Cadence';

  @override
  String get appNoClip => 'クリップなし';

  @override
  String get appEngineUnavailable => 'エンジンを利用できません';

  @override
  String appDylibError(String error) {
    return 'libcadence_engine.dylib を読み込めませんでした: $error\nエンジンをビルドしてください: cmake --build ~/daw/build';
  }

  @override
  String get appDylibOk => 'dylib ok';

  @override
  String appStatusLine(String tracks, String duration) {
    return '$tracks トラック · $duration 秒 · dylib ok';
  }

  @override
  String get appWindowMixer => 'ミキサー — EZMusicProduction';

  @override
  String get appWindowPianoRoll => 'ピアノロール';

  @override
  String get appWindowAutomation => 'オートメーション';

  @override
  String get appMenuNew => '新規';

  @override
  String get appMenuOpen => '開く…';

  @override
  String get appMenuSave => '保存';

  @override
  String get appMenuExport => '書き出し';

  @override
  String get appMenuSounds => 'サウンド';

  @override
  String get appMenuGrid => 'グリッド';

  @override
  String get appMenuMixer => 'ミキサー';

  @override
  String get appMenuPianoRoll => 'ピアノロール';

  @override
  String get appMenuAutomation => 'オートメーション';

  @override
  String get appMenuPreferences => '環境設定（オーディオ・MIDI・リモート）';

  @override
  String get appMenuRemote => 'リモートコントロール';

  @override
  String get transportTempoTimeSig => 'テンポと拍子';

  @override
  String get transportTempoHint => 'テンポ（BPM、20〜300）';

  @override
  String get transportTimeSig => '拍子';

  @override
  String get transportCancel => 'キャンセル';

  @override
  String get transportApply => '適用';

  @override
  String get transportRecord => '録音';

  @override
  String get transportStopRecording => '録音を停止';

  @override
  String get transportMetronome => 'メトロノーム';

  @override
  String get transportFinishRecording => '録音を終了（R と同じ）';

  @override
  String get transportPlayPause => '再生 / 一時停止';

  @override
  String get transportStop => '停止';

  @override
  String get transportIQ => 'IQ:';

  @override
  String get transportOff => 'オフ';

  @override
  String get transportRestartAudio => 'オーディオを再起動（音が途切れたときに復帰します）';

  @override
  String get transportUndo => '元に戻す';

  @override
  String get transportRedo => 'やり直す';

  @override
  String get mixerUndock => 'ミキサーを別ウィンドウに分離';

  @override
  String mixerFxOf(String name) {
    return '$name の FX';
  }

  @override
  String get mixerMaster => 'マスター';

  @override
  String get mixerMasterFx => 'マスター FX';

  @override
  String get mixerMinusInf => '-inf';

  @override
  String get timelineSplitAtPlayhead => '再生ヘッドで分割';

  @override
  String get timelineDeleteClip => 'クリップを削除';

  @override
  String get timelineTracks => 'トラック';

  @override
  String get timelineAddTrack => 'トラックを追加';

  @override
  String get timelineImport => '読み込み';

  @override
  String get timelineImportPrompt => 'オーディオを読み込む';

  @override
  String get timelineRename => '名前を変更…';

  @override
  String get timelineColor => '色…';

  @override
  String get timelineChannel => 'チャンネル…';

  @override
  String get timelineInputLeft => '入力: 左';

  @override
  String get timelineInputRight => '入力: 右';

  @override
  String get timelineMute => 'ミュート';

  @override
  String get timelineSolo => 'ソロ';

  @override
  String get timelineDeleteTrack => 'トラックを削除';

  @override
  String get timelineRenameTrack => 'トラック名を変更';

  @override
  String get timelineCancel => 'キャンセル';

  @override
  String get timelineOk => 'OK';

  @override
  String get timelineTrackColor => 'トラックの色';

  @override
  String get timelineInternalSynth => 'シンセ';

  @override
  String get timelineTrackInstrument => 'トラック音源';

  @override
  String get pianoRollClipUnavailable => 'クリップを利用できません';

  @override
  String pianoRollTitle(String name) {
    return 'ピアノロール — $name';
  }

  @override
  String get pianoRollHint => '空白ドラッグ: 選択 · ドラッグ: 移動 · 端: 長さ · Del: 削除';

  @override
  String get pianoRollUndock => '別ウィンドウに分離';

  @override
  String get pianoRollToolMove => 'ノートを移動/伸縮';

  @override
  String get pianoRollToolDraw => 'ノートを描画';

  @override
  String get pianoRollToolErase => 'ノートを消去';

  @override
  String get pianoRollQuantize => 'クオンタイズ';

  @override
  String get pianoRollSwing => 'スウィング';

  @override
  String get pianoRollCatch => 'キャッチ';

  @override
  String get pianoRollMax => '最大';

  @override
  String get pianoRollIQ => 'iQ';

  @override
  String get pianoRollReset => 'リセット';

  @override
  String get pianoRollFreeze => 'フリーズ';

  @override
  String get automationAddPluginHint => 'このチャンネルにプラグインを追加すると、パラメーターを自動化できます';

  @override
  String get automationChoosePlugin => 'プラグインを選択';

  @override
  String automationParams(String name) {
    return 'パラメーター — $name';
  }

  @override
  String get automationCancel => 'キャンセル';

  @override
  String get automationTitleNoTrack => 'オートメーション — トラックを選択してください';

  @override
  String automationTitle(String name) {
    return 'オートメーション — $name';
  }

  @override
  String get automationVolume => '音量';

  @override
  String get automationPan => 'パン';

  @override
  String get automationSustain => 'ペダル（サステイン CC64）';

  @override
  String get automationAddParamLane => 'プラグインのパラメーター レーンを追加';

  @override
  String get automationHint => 'クリック: ポイント追加 · ドラッグ: 移動 · 右クリック: 削除';

  @override
  String get automationUndock => '別ウィンドウに分離';

  @override
  String get automationNoSelection => 'ヘッダーでトラックを選択するとオートメーションを編集できます';

  @override
  String get automationEmpty => 'オートメーションなし — クリックでポイントを追加';

  @override
  String get automationParamLaneDefault => 'パラメーター';

  @override
  String get channelUnavailable => 'チャンネルを利用できません';

  @override
  String channelTitle(String name) {
    return 'チャンネル — $name';
  }

  @override
  String get channelInstrumentSection => 'インストゥルメント';

  @override
  String get channelInternalSynth => '内蔵シンセ';

  @override
  String get channelInsertsSection => 'インサート';

  @override
  String channelFxOf(String name) {
    return '$name の FX';
  }

  @override
  String get channelAddFx => 'FX を追加';

  @override
  String get channelBypass => 'バイパス';

  @override
  String get channelEditPlugin => 'プラグインを編集';

  @override
  String get channelRemove => '削除';

  @override
  String get exportTitle => '書き出し';

  @override
  String get exportExported => '書き出し完了';

  @override
  String get exportOpenFolder => 'フォルダを開く';

  @override
  String get exportFileName => 'ファイル名';

  @override
  String get exportWav16 => 'WAV 16-bit';

  @override
  String get exportWav24 => 'WAV 24-bit';

  @override
  String exportRateInfo(String rate) {
    return 'サンプルレート: プロジェクト設定（$rate Hz）· ステレオ · オフライン レンダリング（リアルタイムより高速）';
  }

  @override
  String get exportMp3Soon => 'MP3: 近日対応（外部エンコーダーが必要です）';

  @override
  String get exportClose => '閉じる';

  @override
  String get exportCancel => 'キャンセル';

  @override
  String get exportExporting => '書き出し中…';

  @override
  String get addTrackTitle => 'トラックを追加';

  @override
  String get addTrackName => '名前';

  @override
  String get addTrackColor => '色';

  @override
  String get addTrackType => '種類';

  @override
  String get addTrackAudio => 'オーディオ';

  @override
  String get addTrackMidi => 'MIDI';

  @override
  String get addTrackCancel => 'キャンセル';

  @override
  String get addTrackTrack => 'トラック';

  @override
  String get addTrackCreate => 'トラックを作成';

  @override
  String addTrackDefaultName(String n) {
    return 'トラック $n';
  }

  @override
  String get pluginAddTitle => 'プラグインを追加';

  @override
  String get pluginSearchHint => 'プラグインを検索…';

  @override
  String get pluginScanning => 'スキャン中…';

  @override
  String get pluginNoResults => '結果なし';

  @override
  String get pluginKindInstrument => 'インストゥルメント';

  @override
  String get pluginKindEffect => 'エフェクト';

  @override
  String get projectDefaultName => 'マイプロジェクト';

  @override
  String get projectSaveAs => 'プロジェクトを別名で保存…';

  @override
  String get projectNameLabel => 'プロジェクト名';

  @override
  String get projectCancel => 'キャンセル';

  @override
  String get projectSave => '保存';

  @override
  String get projectAppName => 'EZMusicProduction';

  @override
  String projectVersion(String version) {
    return 'バージョン $version';
  }

  @override
  String get projectAuthors => '制作: EzTornado — Iván Pastor Simarro';

  @override
  String get projectWeb => 'www.ezmusicproduction.com';

  @override
  String get projectClose => '閉じる';

  @override
  String get soundLibTitle => 'サウンド ライブラリ';

  @override
  String get soundLibSearchHint => '検索…';

  @override
  String get soundLibAllTypes => 'すべて';

  @override
  String get soundLibLoops => 'ベース / ループ';

  @override
  String get soundLibOneShots => 'ワンショット';

  @override
  String get soundLibCategories => 'カテゴリ';

  @override
  String get soundLibCategoryAll => 'すべて';

  @override
  String get soundLibEmpty => '該当なし';

  @override
  String get soundLibFooter =>
      'Wikimedia Commons と Internet Archive の自由ライセンス（CC0 / パブリックドメイン / CC-BY / BY-SA）のサウンドです。各ダウンロードには帰属表示の .license.txt が保存されます — 作品でのライセンス遵守をお願いします。';

  @override
  String soundLibSourceTooltip(String url, String license) {
    return '出典: $url\nライセンス: $license';
  }

  @override
  String soundLibAttributionAuthor(String name) {
    return '作者: $name';
  }

  @override
  String soundLibAttributionLicense(String license) {
    return 'ライセンス: $license';
  }

  @override
  String soundLibAddedToTrack(String name) {
    return '$name を選択中のトラックに追加しました';
  }

  @override
  String soundLibDownloadFailed(String name, String detail) {
    return '$name をダウンロードできませんでした: $detail';
  }

  @override
  String get soundLibLoop => 'ループ';

  @override
  String get soundLibOneShot => 'ワンショット';

  @override
  String get soundLibInLibrary => 'ライブラリ内';

  @override
  String get soundLibStop => '停止';

  @override
  String get soundLibListen => '試聴';

  @override
  String get soundLibAdd => '追加';

  @override
  String get soundLibDownload => 'ダウンロード';

  @override
  String soundLibLicenseFile(String url, String date) {
    return '出典: $url\nEZMusicProduction が $date にダウンロード。\n商用利用の前に、リンク先の利用条件をご確認ください。';
  }

  @override
  String get servicePluginsLoaded => 'プラグインを読み込みました';

  @override
  String serviceAddFailed(String path, String detail) {
    return '$path を追加できませんでした — `cadence-cli tone` でトーンを生成するか、CADENCE_ASSETS_DIR を確認してください。（$detail）';
  }

  @override
  String serviceAudioDeviceError(String detail) {
    return 'オーディオデバイスを開けませんでした: $detail';
  }

  @override
  String servicePluginsLoadedCount(String count) {
    return '$count プラグインを読み込みました';
  }

  @override
  String get serviceClipCopied => 'クリップをコピーしました';

  @override
  String get serviceSelectTrackToPaste => '貼り付け先のトラックを選択してください';

  @override
  String get serviceClipPasted => 'クリップを貼り付けました';

  @override
  String get serviceFxAddFailed => 'エフェクトを追加できませんでした';

  @override
  String get serviceSelectTrackFirst => '先にトラックを選択してください';

  @override
  String get serviceRecordingInProgress => '録音中です';

  @override
  String serviceExportedTo(String path) {
    return '書き出し完了: $path';
  }

  @override
  String serviceSavedIn(String path) {
    return '$path に保存しました';
  }

  @override
  String get serviceNewProject => '新規プロジェクト';

  @override
  String serviceFileNotFound(String path) {
    return 'ファイルが見つかりません: $path';
  }

  @override
  String serviceNotCubaseProject(String detail) {
    return '有効な Cubase プロジェクトではありません: $detail';
  }

  @override
  String get serviceCprNoTracks => '.cpr に認識できるトラックがありません';

  @override
  String serviceCubaseImported(String tracks, String notes) {
    return 'Cubase を読み込みました: $tracks トラック · $notes ノート';
  }

  @override
  String serviceCubaseAudioClips(String placed) {
    return ' · オーディオクリップ $placed';
  }

  @override
  String serviceCubaseMissingAudio(String missing) {
    return ' · ファイルのないオーディオ $missing';
  }

  @override
  String serviceCubasePluginsRestored(String restored) {
    return ' · 状態付きで復元 $restored';
  }

  @override
  String serviceLoadingCubasePlugins(String index, String total) {
    return 'Cubase のプラグインを読み込み中… $index/$total';
  }

  @override
  String serviceUnrecognizedFormat(String ext) {
    return '未対応の形式: $ext';
  }

  @override
  String serviceProjectLoaded(String path) {
    return 'プロジェクトを読み込みました: $path';
  }

  @override
  String get serviceProjectNeedsName => 'プロジェクト名を入力してください';

  @override
  String serviceSaved(String path) {
    return '保存しました: $path';
  }

  @override
  String get alertTitleError => 'エラー';

  @override
  String get alertTitleWarning => '警告';

  @override
  String get alertTitleInfo => '情報';

  @override
  String get alertClose => '閉じる';

  @override
  String get alertTechnicalDetail => '技術的な詳細';

  @override
  String get prefTitle => '環境設定 — オーディオ、MIDI、リモートコントロール';

  @override
  String get prefSectionLanguage => '言語';

  @override
  String get prefLanguageSystem => 'システムに従う';

  @override
  String get prefOutputDevice => '出力デバイス';

  @override
  String get prefInputDevice => '入力デバイス';

  @override
  String get prefSampleRate => 'サンプルレート';

  @override
  String get prefBuffer => 'バッファ';

  @override
  String prefRoundTrip(String ms) {
    return '~$ms ms（往復）';
  }

  @override
  String get prefMidiLatency => 'MIDI 入力レイテンシー';

  @override
  String get prefMidiLatencyHelp =>
      'ms — MIDI 録音を前にずらします。ノートが遅れて聴こえる場合は値を上げてください';

  @override
  String prefActiveInputs(String count) {
    return 'デバイスの有効な入力数: $count';
  }

  @override
  String get prefNoInputs =>
      '⚠ 使用中のデバイスに入力がありません — 録音するには「入力デバイス」でマイクのあるデバイスを選んでください';

  @override
  String get prefRescan => 'デバイスを再スキャン';

  @override
  String get prefMidiInputTitle => 'MIDI 入力（デバイスを選択）';

  @override
  String get prefNoMidi => 'MIDI デバイスが接続されていません';

  @override
  String get aiTitle => 'AI で生成';

  @override
  String aiTrackLabel(String name) {
    return 'トラック: $name';
  }

  @override
  String get aiModeMelody => 'メロディ';

  @override
  String get aiModeHarmony => 'ハーモニー';

  @override
  String get aiModeBass => 'ベース';

  @override
  String get aiModeDrums => 'ドラム';

  @override
  String get aiBars => '小節';

  @override
  String get aiLength => '長さ';

  @override
  String aiHeardLabel(Object text) {
    return '聴取: $text';
  }

  @override
  String get aiLongHint => '長いクリップは生成を速くするため候補を1つ使います。';

  @override
  String get aiVariations => 'バリエーション';

  @override
  String get aiTemperature => '創造性';

  @override
  String get aiPrompt => 'プロンプト（任意）';

  @override
  String get aiPromptHint => '例: 暗めに、アルペジオ付き、ハ短調で';

  @override
  String get aiHarmonize => 'ハーモニーを追加';

  @override
  String get aiReplaceClip => 'クリップを置き換える';

  @override
  String get aiGenerate => '生成';

  @override
  String get aiRegenerate => '再生成';

  @override
  String get aiApply => 'プロジェクトに適用';

  @override
  String get aiClose => '閉じる';

  @override
  String get aiGenerating => '生成中…';

  @override
  String get aiIdleHint => '種類を選んで「生成」を押してください。';

  @override
  String get aiNoModel => 'Magenta モデルが未インストールです: 内蔵ジェネレーターを使用します。';

  @override
  String get aiProviderLabel => 'エンジン';

  @override
  String get aiKeyLabel => 'キー';

  @override
  String get aiChordsLabel => 'コード';

  @override
  String get aiNotesLabel => 'ノート';

  @override
  String aiPromptUnderstood(String text) {
    return '解釈: $text';
  }

  @override
  String aiFailed(String error) {
    return '生成できませんでした: $error';
  }

  @override
  String get aiBusy => '生成がすでに実行中です';

  @override
  String get aiApplied => '生成したクリップを適用しました';

  @override
  String get aiGenerateClip => 'AI で生成…';

  @override
  String get aiRegenerateClip => 'AI で再生成…';

  @override
  String get aiGenerateAtPlayhead => 'AI でクリップを生成…';

  @override
  String get aiToolbarButton => 'AI';

  @override
  String get aiToolbarTooltip => '選択したインストゥルメントトラックに AI でクリップを生成';

  @override
  String get aiNoInstrumentTrack => 'AI 生成を使うにはインストゥルメントトラックを作成または選択してください。';

  @override
  String get prefSectionRemote => 'リモートコントロール';

  @override
  String get prefRemoteEnable => 'LAN リモートコントロールを有効にする';

  @override
  String get prefRemotePort => 'ポート';

  @override
  String get prefRemoteCode => 'ペアリングコード';

  @override
  String get prefRemoteCopy => 'コピー';

  @override
  String get prefRemoteCopied => 'コードをコピーしました';

  @override
  String get prefRemoteNewCode => '新しいコード';

  @override
  String get prefRemoteAddress => 'アドレス';

  @override
  String prefRemoteClients(int count) {
    return '$count クライアント接続中';
  }

  @override
  String prefRemoteRunning(int port) {
    return 'ポート $port でサーバー稼働中';
  }

  @override
  String get prefRemoteStopped => 'サーバー停止中';

  @override
  String get prefRemoteWarning => 'コードを知っているローカルネットワーク上の誰でもこの DAW を操作できます。';

  @override
  String prefRemoteError(String error) {
    return '起動できませんでした: $error';
  }

  @override
  String get prefRemoteHelp => 'リモートアプリでこのアドレスとコードを入力してください。';
}
