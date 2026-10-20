<p align="center">
  <img src="assets/branding/logo.svg" alt="EZMusicProduction" width="380">
</p>

<h3 align="center">音楽制作のすべてを、ひとつのアプリで。</h3>

<p align="center">
  macOS向けオープンソースDAW · Apple Silicon / Intel
</p>

<p align="center">
  <a href="README.md">English</a> ·
  <a href="README.es.md">Español</a> ·
  <a href="README.ja.md">日本語</a>
</p>

<p align="center">
  <a href="https://ezmusicproduction.com">ウェブサイト</a> ·
  <a href="https://github.com/eztornado/EZMusicProduction/releases/latest">ダウンロード</a> ·
  <a href="docs/design/ui-design.md">設計ドキュメント</a> ·
  <a href="https://github.com/eztornado/EZMusicProduction/issues">Issues</a>
</p>

<p align="center">
  <img alt="対応OS" src="https://img.shields.io/badge/OS-macOS%2012%2B-0a0a0b?logo=apple&logoColor=white">
  <img alt="UI" src="https://img.shields.io/badge/UI-Flutter-02569B?logo=flutter&logoColor=white">
  <img alt="エンジン" src="https://img.shields.io/badge/engine-C%2B%2B20%20%2F%20JUCE-ff2d20">
  <img alt="状態" src="https://img.shields.io/badge/status-0.1%20pre--release-ff2d20">
</p>

---

**EZMusicProduction**は、スタジオのすべてをひとつのウィンドウにまとめたmacOS用のDAWです。録音・編集・ミックス・オートメーションに加え、4つの内蔵楽器、21,000以上のフリー音源、本格的なノートエディターを最初から使えます。

プラグインを探し回る必要も、追加の音源パックを買う必要も、サブスクリプションもありません。インストールして開けば、すぐに音楽づくりを始められます。

## 主な内容

| 分野 | 内容 |
|---|---|
| **タイムラインと録音** | オーディオ／MIDIのマルチトラック編集、ビートスナップ、分割・コピー・貼り付け、メトロノーム、テンポと拍子、フレーム単位のプレイヘッド。 |
| **サウンドライブラリ** | 21,000以上のフリーなループとワンショット。カテゴリーと種類で検索でき、その場で試聴して選択中のトラックへ直接配置できます。 |
| **内蔵楽器** | EZInstruments（General MIDI 128音色＋ドラムキット）、EZSynth、EZDrums、EZOrquestra。 |
| **ピアノロール** | ノートの描画・長さ変更・ベロシティ編集に加え、グリッド、スウィング、キャッチレンジ、iQ、フリーズを備えた非破壊クオンタイズ。 |
| **ミキサーとエフェクト** | インサート、センド、パン、ミュート／ソロ、メーター付きのチャンネルストリップ、マスターストリップ、チャンネル設定。AU／VST3プラグインに対応。 |
| **オートメーション** | 音量、パン、サステイン、プラグインパラメーターのレーン。ポイントを編集でき、線形補間でつながります。 |
| **Cubaseの読み込み** | `.cpr`プロジェクトを読み取り専用で開き、トラック、ノート、オーディオ、テンポ、プラグイン状態を再構築。元ファイルは変更しません。 |
| **書き出し** | プロジェクトのサンプルレートで、16／24ビットのステレオWAVをオフライン書き出し。リアルタイムより高速です。 |
| **言語** | 英語・スペイン語・日本語のUI。システム設定に従うか、ユーザーごとに選択できます。 |

## 機能

### 録音して、曲を組み立てる

アレンジビューがアプリの中心です。オーディオとMIDIのトラック、トラックごとの色、ミュート／ソロ、音量とパン、そして移動・トリム・分割・コピー・貼り付けができるクリップがあります。スナップはビートグリッドに従い、`⌘`を押している間は無効になって自由に配置できます。

編集はすべて1回のUndoで戻せます。Undo／Redoはインターフェースではなくオーディオエンジン側にあり、ウィンドウを切り離しても履歴が一貫します。

### すぐに使えるサウンドライブラリ

ライブラリには**21,000以上のフリー音源**が収録されています。ループ、ドラム、パーカッション、ストリングス、シンセ、ボーカル、エフェクトまで、Wikimedia CommonsとInternet Archiveから集めた音源です。

- カテゴリーとループ／ワンショットで検索・絞り込み。
- プロジェクトを変更せずに試聴。
- 必要なときにダウンロードし、選択中のトラックのプレイヘッド位置へ追加。
- ダウンロードごとに、出典・作者・ライセンス（CC0、パブリックドメイン、CC-BY、CC-BY-SA）を記した`.license.txt`を保存。

### 4つの内蔵楽器

- **EZInstruments** — FluidSynthによるGeneral MIDIカタログ全体（メロディック128音色＋パーカッションキット）。音色を選んですぐ弾けるので、アイデアを素早く形にできます。
- **EZSynth** — 3オシレーター、ユニゾン、サブ、ノイズ、マルチモードラダーフィルター、2基のLFO、グライド、プリセットバンクを備えたサブトラクティブシンセ。**LINES**機能では、ピアノロール上にポリフォニックなピッチディストーション領域を描けます。和音の各音がそれぞれの経路をたどります。
- **EZDrums** — 808／909系のシンセドラムキット、多彩なスタイルのパターン、テンポに同期するループ、設定可能な拍子。実際のMIDIクリップとして録音し、ほかのトラックと同じようにノート単位で編集できます。
- **EZOrquestra** — Alpine Project形式のライブラリに対応したオーケストラサンプラー。音ごとのサンプル、奏法、ベロシティレイヤー、ラウンドロビンに対応します。

### ピアノロールと非破壊クオンタイズ

マウスで音符を書き、直せます。描画、移動、長さ変更、削除、そしてノートごとのベロシティ調整。**Quantize**パネルにはグリッド、スウィング、キャッチレンジ、最大移動量、**iQ**（反復クオンタイズ）、**フリーズ**があります。クオンタイズ前の位置はゴースト表示されるので、違いを聴き比べていつでも元に戻せます。

再生・録音中はプレイヘッドを追いかけます。エディターは独立したウィンドウに切り離すこともできます。

### ミキサー、エフェクト、オートメーション

ミキサーでは各トラックにインサート、センド、パン、ミュート／ソロ、レベルメーター付きのチャンネルストリップがあり、マスターストリップも用意されています。**チャンネル設定**では、選択中チャンネルの楽器、インサート、ルーティングを確認できます。

EZMusicProductionは**AU／VST3**の楽器とエフェクトをホストします。オートメーションは音量、パン、サステイン（CC64）、および任意のプラグインパラメーターに対応。クリックでポイント追加、ドラッグで移動、右クリックで削除できます。

### Cubaseのプロジェクトをそのまま

Cubaseの`.cpr`プロジェクトを直接開くと、トラック、テンポ、拍子、MIDIノート、Cubase上の範囲でトリムされたオーディオクリップ、プラグインの状態がそのまま復元されます。

読み込みは**読み取り専用**です。`.cpr`が変更されることはなく、保存時にはEZMusicProduction独自の`.ezmpp`形式で書き出されます。

### 書き出しとプロジェクトファイル

プロジェクトは`.ezmpp`ファイルとして保存され、通常のドキュメントと同じように開く・保存する・元に戻すができます。書き出しはミックス全体をオフラインでステレオWAVにレンダリングします。16または24ビット、プロジェクトのサンプルレートで、リアルタイムより高速です。MP3書き出しは今後対応予定です。

## アーキテクチャ

```text
┌──────────────────────────────┐        ┌────────────────────────────────┐
│  Flutter UI（Dart）           │        │  オーディオエンジン（C++20+JUCE） │
│  タイムライン · ミキサー · 編集 │◄──────►│  再生 · ミックス · レンダリング   │
└──────────────────────────────┘        └────────────────────────────────┘
        安定したC ABI（FFI）上のコマンド · スナップショット · ストリーム
```

- **UIはオーディオの邪魔をしない。** FlutterのインターフェースとC++エンジンは安定したC ABIでつながれた別レイヤーで、オーディオコールバックがFlutterへ渡ることはありません。
- **エンジンが唯一の正となる。** プロジェクト状態、Undo／Redo、ミックス、オフラインレンダリングはC++側にあります。
- **オーディオスレッドでディスクI/Oをしない。** 音源はクリップ追加時にメモリへデコードし、プロジェクトのレートへリサンプリングします。RAMと引き換えに、途切れない再生を実現します。
- **安定した境界。** 公開C APIは[`engine/include/cadence/cadence_engine.h`](engine/include/cadence/cadence_engine.h)に定義されています。

## 動作環境

- macOS 12以降
- Apple Silicon または Intel Mac

## ダウンロード

[リリースページ](https://github.com/eztornado/EZMusicProduction/releases/latest)から最新版を入手するか、[ezmusicproduction.com](https://ezmusicproduction.com)をご覧ください。

## ソースからビルドする

**必要なもの：** Xcode（コマンドラインツールを含む）、CMake 3.22以降、Ninja、Flutter 3.35.x、Homebrewの`libsndfile`。JUCEとFluidSynthは初回のCMake実行時に自動で取得されます。

```bash
brew install cmake ninja libsndfile

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(sysctl -n hw.ncpu)"

cd ui
flutter pub get
flutter build macos --release
```

Flutterアプリは実行時に`libcadence_engine.dylib`を読み込みます。エンジンのビルド後、dylibをアプリバンドルへコピーして再署名してください。`./build_app.sh`が一連の処理（エンジン、Flutterビルド、dylibコピー、コード署名）を自動化します。

```bash
./build_app.sh
open ui/build/macos/Build/Products/Release/EZMusicProduction.app
```

このスクリプトはFlutter SDKが`~/development/flutter`にある前提です。別の場所にある場合は`PATH`の行を調整してください。環境変数`CADENCE_ENGINE_DYLIB`で別のエンジンライブラリを指定することもできます。

Apple SiliconとIntelの両方でネイティブに動作するユニバーサルビルドを作るには：

```bash
UNIVERSAL=1 ./build_app.sh
```

このモードではFluidSynthを静的リンクし、SF3を無効にします（SF2は利用可能）。エンジンはHomebrewのライブラリに依存しません。

### テストとCLI

```bash
cd ui
flutter analyze
flutter test

# ウィンドウなしでエンジンをスモークテスト
build/engine/cadence_cli_artefacts/Release/cadence_cli selftest assets/test/sine440.wav
```

エンジンには開発用CLIも付属しています。デバイス、トーン生成、再生、プロジェクトのデモ、ドキュメントのラウンドトリップなどのコマンドがあり、引数なしで実行すると一覧が表示されます。

## リポジトリ構成

```text
engine/          C++20オーディオエンジン、C ABI、開発用CLI
  include/       公開ヘッダーと安定したC API
  src/           エンジン、モデル、ミキサー、内蔵楽器
  apps/cli/      ヘッドレステストハーネス
ui/              Flutter製macOSアプリ
  lib/engine/    Dart FFIバインディング、スナップショット、プロジェクトサービス
  lib/ui/        タイムライン、ミキサー、ピアノロール、オートメーション、ダイアログ
  lib/l10n/      英語・スペイン語・日本語の翻訳
bindings/        正規のDartバインディングへのポインター
assets/          デモ音源とブランド素材
docs/design/     製品・UI・フォーマットの設計仕様（スペイン語）
build_app.sh     macOSアプリのビルド、パッケージング、署名
```

エンジン内部では、名前空間、C ABIのプレフィックス、ダイナミックライブラリ名に引き続き`cadence`を使用しています。

## ドキュメント

- [`docs/design/ui-design.md`](docs/design/ui-design.md) — UI仕様：情報アーキテクチャ、ビジュアルシステム、ミキサー、ピアノロール、キーマップ、FFI契約、ロードマップ。
- [`docs/design/cubase-import.md`](docs/design/cubase-import.md) — `.cpr`リーダーの仕組み、対応範囲、制限事項。
- [`docs/design/i18n.md`](docs/design/i18n.md) — UIの言語とエラー表示。

## コントリビュート

バグ報告、機能のアイデア、プルリクエストを歓迎します。参加するには：

- 不具合を再現し、macOSのバージョン、Macのモデル、再現手順を添えて[Issueを開く](https://github.com/eztornado/EZMusicProduction/issues)。
- [`ui/lib/l10n/`](ui/lib/l10n/)の翻訳を改善する（英語・スペイン語・日本語のキーを揃えてください）。
- サウンドライブラリのカタログのメタデータを追加・修正する。
- 設計ドキュメントやテストカバレッジを改善する。

プルリクエストを開く前に、`ui/`内で`flutter analyze`と`flutter test`を実行し、エンジンがrelease構成でビルドできる状態を保ってください。

## 状態

**0.1 pre-release。** アプリは活発に開発中です。機能、内部API、プロジェクト形式はリリース間で変更される可能性があります。大切なプロジェクトはバックアップし、未完成な部分があることを前提にしてください。

## ライセンスとクレジット

EZMusicProductionはオープンソースとして開発されています。サードパーティ製コンポーネントはそれぞれのライセンスに従います：

- **JUCE 8.0.9** — AGPLv3 または商用ライセンス。
- **FluidSynth 2.6.1** — LGPL-2.1+。
- **サウンドライブラリ** — Wikimedia CommonsとInternet Archiveのフリーライセンス音源（CC0、パブリックドメイン、CC-BY、CC-BY-SA）。各ダウンロードのそばに出典情報を保存します。

制作：**EzTornado — Iván Pastor Simarro**

<p align="center">
  <a href="https://ezmusicproduction.com">ezmusicproduction.com</a>
</p>
