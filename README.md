<p align="center">
  <img src="assets/branding/logo.svg" alt="EZMusicProduction" width="380">
</p>

<h3 align="center">Your whole music studio, in one app.</h3>

<p align="center">
  An open-source DAW for macOS, Linux and Windows
</p>

<p align="center">
  <a href="README.md">English</a> ·
  <a href="README.es.md">Español</a> ·
  <a href="README.ja.md">日本語</a>
</p>

<p align="center">
  <a href="https://ezmusicproduction.com">Website</a> ·
  <a href="https://github.com/eztornado/EZMusicProduction/releases/latest">Download</a> ·
  <a href="docs/design/ui-design.md">Design docs</a> ·
  <a href="https://github.com/eztornado/EZMusicProduction/issues">Issues</a>
</p>

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-macOS%20%7C%20Linux%20%7C%20Windows-0a0a0b?logo=apple&logoColor=white">
  <img alt="UI" src="https://img.shields.io/badge/UI-Flutter-02569B?logo=flutter&logoColor=white">
  <img alt="Engine" src="https://img.shields.io/badge/engine-C%2B%2B20%20%2F%20JUCE-ff2d20">
  <img alt="Status" src="https://img.shields.io/badge/status-0.1%20pre--release-ff2d20">
</p>

---

**EZMusicProduction** is a cross-platform digital audio workstation that brings the whole studio into a single window: recording, editing, mixing and automation, with four instruments, a library of more than 21,000 free sounds, a complete note editor and AI-powered tools included from the first launch.

No plugin hunting, no add-on packs to buy, no subscriptions. Install it, open it and start making music.

## What's inside

| Area | What it does |
|---|---|
| **Timeline and recording** | Multitrack arrangement for audio and MIDI, beat snapping, split/copy/paste, metronome, tempo and time signature, frame-accurate playhead. |
| **Sound library** | More than 21,000 free loops and one-shots, searchable by category and type, with in-place audition and direct placement on the selected track. |
| **Built-in instruments** | EZInstruments (128 General MIDI sounds + drum kit), EZSynth, EZDrums and EZOrquestra. |
| **Built-in effects** | EZQ4 (8-band parametric EQ), EZCompressor, EZLimiter and EZArpeggiator. |
| **AI tools** | AI Mix Assistant (auto-balance levels, pan, EQ, compression), AI Song Arranger (generate full song structures), AI Music Generation (MusicVAE/LSTM melody generation). |
| **Remote control** | LAN WebSocket server for external control and monitoring from companion apps. |
| **Piano Roll** | Note drawing, resizing and velocity editing, plus non-destructive quantize with grid, swing, catch range, iQ and freeze. |
| **Mixer and effects** | Channel strips with inserts, sends, pan, mute/solo and meters, a master strip and Channel Settings. Hosts AU and VST3 plugins. |
| **Automation** | Volume, pan, sustain and plugin-parameter lanes with editable points and linear segments. |
| **Cubase import** | Opens `.cpr` projects read-only and rebuilds their tracks, notes, audio, tempo and plugin states. Your originals are never touched. |
| **Export** | Offline stereo WAV at the project sample rate, in 16 or 24 bits, faster than real time. |
| **Languages** | English, Spanish and Japanese interface, following the system or set per user. |

## Features

### Record and arrange

The arrangement view is the centre of the app: audio and MIDI tracks, colour per track, mute/solo, volume and pan, and clips you can move, trim, split, copy and paste. Snapping follows the beat grid, and holding `⌘` bypasses it when you need free placement.

Every edit is a single undo step. Undo and redo live in the audio engine, not in the interface, so the history stays consistent even across undocked windows.

### A sound library that is ready to use

The library indexes **more than 21,000 free sounds** — loops, drums, percussion, strings, synths, vocals and effects — from Wikimedia Commons and the Internet Archive.

- Search and filter by category and by loop/one-shot.
- Audition a sound without modifying the project.
- Download it on demand and add it to the selected track at the playhead.
- Every download keeps a `.license.txt` file with its source, author and licence (CC0, public domain, CC-BY or CC-BY-SA).

### Four instruments included

- **EZInstruments** — the whole General MIDI catalogue (128 melodic programs plus a percussion kit) powered by FluidSynth. Pick a sound and play; perfect for turning an idea into music fast.
- **EZSynth** — a subtractive synth with three oscillators, unison, sub, noise, a multimode ladder filter, two LFOs, glide and a preset bank. Its **LINES** feature lets you draw polyphonic pitch-distortion areas over the piano roll: each note in a chord follows its own path.
- **EZDrums** — synthesized 808/909-style kits, patterns across styles, tempo-locked looping and configurable time signature. Record it to a real MIDI clip and edit it note by note like any other track.
- **EZOrquestra** — an orchestral sampler for Alpine Project-style libraries, with per-note samples, articulations, velocity layers and round-robin variation.

### Built-in effects

Four processors ship with the app, ready to drop on any channel or the master bus:

- **EZQ4** — an 8-band parametric EQ with Pro-Q-style workflow: per-band enable/type/frequency/gain/Q, plus 24 dB/oct cuts and natural-phase IIR filtering based on RBJ-cookbook biquads.
- **EZCompressor** — a stereo-linked dynamics processor with soft knee, attack/release and parallel mix.
- **EZLimiter** — a look-ahead brick-wall limiter for the master bus, with stereo-linked peak detection and a hard-clamp safety ceiling.
- **EZArpeggiator** — a MIDI-FX arpeggiator that sits in the instrument track's MIDI insert slot, consuming notes and re-emitting them arpeggiated, sample-locked to the host transport.

### AI-powered tools

- **AI Mix Assistant** — analyses the project and auto-balances levels, pan, EQ and compression across all tracks. One click to get a professional-sounding starting mix.
- **AI Song Arranger** — generates full song structures from a seed idea, creating sections (intro, verse, chorus, bridge, outro) with appropriate instrumentation and transitions.
- **AI Music Generation** — uses MusicVAE and LSTM models to generate melodic material that you can record directly into MIDI clips.

### Remote control

A built-in LAN server exposes a WebSocket + JSON protocol for external control and monitoring. Companion apps can connect over the network to read transport state, project metadata, meter levels and send commands — all protected by a pairing token.

### Piano Roll and non-destructive quantize

Write and correct music with the mouse: draw, move, resize and erase notes, and shape each note's velocity. The **Quantize** panel offers grid, swing, catch range, maximum move, **iQ** (iterative quantize) and **freeze**. Ghost outlines show where notes were before quantizing, so you can hear the difference and go back at any time.

The view follows the playhead while playing and recording. The editor can also be undocked into its own window.

### Mixer, effects and automation

The mixer gives every track a channel strip with inserts, sends, pan, mute/solo and a level meter, plus a master strip. **Channel Settings** exposes the instrument, inserts and routing for the selected channel.

EZMusicProduction hosts **AU and VST3** instruments and effects. Automation lanes are available for volume, pan, sustain (CC64) and any plugin parameter: click to add a point, drag to move it and right-click to delete it.

### Bring your Cubase projects

Open a Cubase `.cpr` project directly and find your work in place: tracks, tempo, time signature, MIDI notes, audio clips trimmed to their Cubase window and plug-in states.

The import is **read-only**. Your `.cpr` is never modified; when you save, the project is written in EZMusicProduction's own `.ezmpp` format.

### Export and project files

Projects are saved as `.ezmpp` files and can be opened, saved and undone like any other document. Export renders the full mix offline to a stereo WAV file — 16 or 24 bits at the project sample rate — faster than real time. MP3 export is planned.

## Architecture

```text
┌──────────────────────────────┐        ┌──────────────────────────────┐
│  Flutter UI (Dart)           │        │  Audio engine (C++20 + JUCE) │
│  timeline · mixer · editors  │◄──────►│  playback · mixing · render  │
└──────────────────────────────┘        └──────────────────────────────┘
        commands · snapshots · streams over a stable C ABI (FFI)
```

- **The UI is never in the way of the audio.** The Flutter interface and the C++ engine are separate layers connected through a stable C ABI; no audio callback crosses into Flutter.
- **The engine owns the truth.** Project state, undo/redo, mixing and offline rendering live in C++.
- **No disk I/O on the audio thread.** Audio sources are decoded into memory and resampled to the project rate when a clip is added, trading RAM for glitch-free playback.
- **One stable boundary.** The public C API is defined in [`engine/include/cadence/cadence_engine.h`](engine/include/cadence/cadence_engine.h).

## Requirements

- **macOS** 12 or later (Apple Silicon or Intel)
- **Linux** with GTK3 and ALSA/PulseAudio
- **Windows** 10 or later (x64)

## Download

Grab the latest build from the [releases page](https://github.com/eztornado/EZMusicProduction/releases/latest), or visit [ezmusicproduction.com](https://ezmusicproduction.com).

## Build from source

**Prerequisites:** CMake 3.22+, Ninja, Flutter 3.29.x and platform-specific dependencies. JUCE and FluidSynth are downloaded automatically by CMake the first time.

### macOS

**Prerequisites:** Xcode (with command-line tools), Homebrew's `libsndfile`.

```bash
brew install cmake ninja libsndfile

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(sysctl -n hw.ncpu)"

cd ui
flutter pub get
flutter build macos --release
```

The Flutter app loads `libcadence_engine.dylib` at runtime. After building the engine, copy the dylib into the app bundle and re-sign it — `./build_app.sh` automates the whole sequence (engine, Flutter build, dylib copy and code signing):

```bash
./build_app.sh
open ui/build/macos/Build/Products/Release/EZMusicProduction.app
```

The script expects the Flutter SDK in `~/development/flutter`; adjust its `PATH` line if yours lives elsewhere. The built app can also point at another engine library with the `CADENCE_ENGINE_DYLIB` environment variable.

For a universal build that runs natively on both Apple Silicon and Intel:

```bash
UNIVERSAL=1 ./build_app.sh
```

That variant links FluidSynth statically and disables SF3 support (SF2 still works), so the engine does not depend on Homebrew libraries.

### Linux

**Prerequisites:** GTK3, ALSA, openblas, libsndfile and development headers.

```bash
# Ubuntu/Debian
sudo apt install cmake ninja-build libgtk-3-dev libasound2-dev \
  libopenblas-dev libsndfile1-dev

# Arch
sudo pacman -S cmake ninja gtk3 alsa-lib openblas libsndfile

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"

cd ui
flutter pub get
flutter build linux --release
```

Copy the engine shared library into the Flutter bundle:

```bash
cp build/engine/libcadence_engine.so ui/build/linux/x64/release/bundle/lib/
```

### Windows

**Prerequisites:** Visual Studio 2022 (Desktop C++ workload), CMake and Ninja.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

cd ui
flutter pub get
flutter build windows --release
```

Copy the engine DLL next to the executable:

```powershell
copy build\engine\Release\cadence_engine.dll ui\build\windows\x64\runner\Release\
```

### Tests and CLI

```bash
cd ui
flutter analyze
flutter test

# Engine smoke test without a window
build/engine/cadence_cli_artefacts/Release/cadence_cli selftest assets/test/sine440.wav
```

The engine also ships a development CLI with commands for devices, tone generation, playback, project demos and document round-trips; run it with no arguments to see the list.

## Repository layout

```text
engine/          C++20 audio engine, C ABI and development CLI
  include/       public headers and the stable C API
  src/           engine, model, mixer and built-in instruments
  apps/cli/      headless test harness
ui/              Flutter cross-platform app (macOS, Linux, Windows)
  lib/engine/    Dart FFI bindings, snapshots and project service
  lib/ui/        timeline, mixer, piano roll, automation, dialogs
  lib/l10n/      English, Spanish and Japanese translations
bindings/        pointer to the canonical Dart bindings
assets/          demo audio and brand files
docs/design/     product, UI and format specifications (in Spanish)
build_app.sh     build, package and sign the macOS app
```

The engine still uses `cadence` internally as its namespace, C ABI prefix and dynamic library name.

## Documentation

- [`docs/design/ui-design.md`](docs/design/ui-design.md) — UI specification: information architecture, visual system, mixer, piano roll, keymap, FFI contract and roadmap.
- [`docs/design/cubase-import.md`](docs/design/cubase-import.md) — how the `.cpr` reader works, what it covers and its limitations.
- [`docs/design/i18n.md`](docs/design/i18n.md) — interface languages and error reporting.
- [`docs/design/remote-control.md`](docs/design/remote-control.md) — LAN remote-control server: WebSocket + JSON protocol, pairing, commands and events for the companion app.

## Contributing

Bug reports, feature ideas and pull requests are welcome. If you want to help:

- Reproduce a bug and [open an issue](https://github.com/eztornado/EZMusicProduction/issues) with your OS version, hardware and the steps that trigger it.
- Improve the translations in [`ui/lib/l10n/`](ui/lib/l10n/) (English, Spanish and Japanese must stay in sync).
- Add or fix metadata in the sound library catalogue.
- Improve the design documents or the test coverage.

Before opening a pull request, run `flutter analyze` and `flutter test` inside `ui/`, and keep the engine building with the release configuration.

## Status

**0.1 pre-release.** The app is under active development: features, internal APIs and the project format may change between releases. Back up important projects and expect rough edges.

## License and credits

EZMusicProduction is developed as open source. Third-party components keep their own licences:

- **JUCE 8.0.9** — AGPLv3 or commercial licence.
- **FluidSynth 2.6.1** — LGPL-2.1+.
- **Sound library** — sounds from Wikimedia Commons and the Internet Archive under free licences (CC0, public domain, CC-BY, CC-BY-SA), with attribution stored next to each download.

Created by **EzTornado — Iván Pastor Simarro**.

<p align="center">
  <a href="https://ezmusicproduction.com">ezmusicproduction.com</a>
</p>
