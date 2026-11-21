<a name="top"></a>

<p align="center">
  <img src="assets/branding/logo.svg" alt="EZMusicProduction" width="380">
</p>

<h3 align="center">Your whole music studio, in one app.</h3>

<p align="center">
  An open-source DAW for macOS, Linux and Windows
</p>

<p align="center">
  <a href="#top">English</a> ·
  <a href="#spanish">Español</a> ·
  <a href="#japanese">日本語</a>
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

<a name="spanish"></a>

---

## 🇪🇸 Español

<h3 align="center">Toda tu música, en un solo programa.</h3>

<p align="center">
  Un DAW de código abierto para macOS, Linux y Windows
</p>

---

**EZMusicProduction** es una estación de trabajo de audio digital multiplataforma que reúne todo el estudio en una sola ventana: grabación, edición, mezcla y automatización, con cuatro instrumentos, una librería de más de 21.000 sonidos gratis, un editor de notas completo y herramientas impulsadas por IA desde el primer arranque.

Sin buscar plugins, sin packs adicionales que comprar y sin suscripciones. Instala, abre y empieza a hacer música.

### Qué incluye

| Área | Qué hace |
|---|---|
| **Timeline y grabación** | Arreglo multipista para audio y MIDI, snap al beat, cortar/copiar/pegar, metrónomo, tempo y compás, playhead con precisión de frame. |
| **Librería de sonidos** | Más de 21.000 loops y golpes sueltos gratis, con búsqueda por categoría y tipo, escucha previa y colocación directa en la pista seleccionada. |
| **Instrumentos incluidos** | EZInstruments (128 sonidos General MIDI + kit de batería), EZSynth, EZDrums y EZOrquestra. |
| **Efectos incluidos** | EZQ4 (ecualizador paramétrico de 8 bandas), EZCompresor, EZLimitador y EZArpegiador. |
| **Herramientas IA** | Asistente de mezcla IA (auto-balanceo de niveles, pan, EQ, compresión), arreglista de canciones IA, generación musical IA (MusicVAE/LSTM). |
| **Control remoto** | Servidor WebSocket por LAN para control y monitorización externa desde apps compañeras. |
| **Piano Roll** | Dibujo, redimensionado y edición de velocidad de notas, más cuantización no destructiva con rejilla, swing, catch range, iQ y freeze. |
| **Mezclador y efectos** | Canales con insertos, sends, pan, mute/solo y medidores, canal master y Channel Settings. Aloja plugins AU y VST3. |
| **Automatización** | Lanes de volumen, pan, sustain y parámetros de plugins, con puntos editables y segmentos lineales. |
| **Importación de Cubase** | Abre proyectos `.cpr` en solo lectura y reconstruye pistas, notas, audio, tempo y estados de plugin. Tus originales no se tocan. |
| **Exportación** | WAV estéreo offline a la frecuencia del proyecto, en 16 o 24 bits, más rápido que en tiempo real. |
| **Idiomas** | Interfaz en inglés, español y japonés, siguiendo al sistema o a la preferencia del usuario. |

### Funciones

#### Graba y organiza

La vista de arreglo es el centro del programa: pistas de audio y MIDI, color por pista, mute/solo, volumen y pan, y clips que puedes mover, recortar, cortar, copiar y pegar. El snap sigue la rejilla de beats; mantén pulsada `⌘` para desactivarlo cuando necesites colocar algo libremente.

Cada edición es un único paso de undo. Deshacer y rehacer viven en el motor de audio, no en la interfaz, así que el historial se mantiene coherente incluso entre ventanas desacopladas.

#### Una librería de sonidos lista para usar

La librería indexa **más de 21.000 sonidos gratis** —loops, baterías, percusión, cuerdas, sinte, voces y efectos— procedentes de Wikimedia Commons y el Internet Archive.

- Busca y filtra por categoría y por loop/golpe suelto.
- Escucha un sonido sin modificar el proyecto.
- Descárgalo a demanda y añádelo a la pista seleccionada en el playhead.
- Cada descarga guarda un fichero `.license.txt` con su origen, autor y licencia (CC0, dominio público, CC-BY o CC-BY-SA).

#### Cuatro instrumentos incluidos

- **EZInstruments** — todo el catálogo General MIDI (128 programas melódicos y un kit de percusión) con FluidSynth. Elige un sonido y toca; perfecto para convertir una idea en música rápido.
- **EZSynth** — un sintetizador sustractivo con tres osciladores, unísono, sub, ruido, filtro ladder multimodo, dos LFOs, glide y banco de presets. Su función **LINES** permite dibujar zonas de distorsión de altura polifónica sobre el piano roll: cada nota de un acorde sigue su propio camino.
- **EZDrums** — kits sintetizados de estilo 808/909, patrones de varios estilos, bucle sincronizado al tempo y compás configurable. Grábalo en un clip MIDI real y edítalo nota a nota como cualquier otra pista.
- **EZOrquestra** — un sampler de orquesta para librerías estilo Alpine Project, con samples por nota, articulaciones, capas de intensidad y variación round-robin.

#### Efectos incluidos

Cuatro procesadores vienen con la app, listos para añadir a cualquier canal o al bus master:

- **EZQ4** — un ecualizador paramétrico de 8 bandas con flujo de trabajo estilo Pro-Q: activación/tipo/frecuencia/ganancia/Q por banda, cortes de 24 dB/oct y filtrado IIR de fase natural basado en biquads RBJ.
- **EZCompresor** — un procesador de dinámica estéreo con rodilla suave, ataque/liberación y mezcla paralela.
- **EZLimitador** — un limitador brick-wall con look-ahead para el bus master, con detección de pico estéreo y techo de seguridad.
- **EZArpegiador** — un MIDI-FX arpegiador que se coloca en el slot de inserto MIDI del instrumento, consumiendo notas y reemitiéndolas arpegiadas, sincronizado al transporte.

#### Herramientas impulsadas por IA

- **Asistente de mezcla IA** — analiza el proyecto y auto-balancea niveles, pan, EQ y compresión en todas las pistas. Un clic para obtener una mezcla inicial profesional.
- **Arreglista de canciones IA** — genera estructuras completas de canción a partir de una idea semilla, creando secciones (intro, verso, estribillo, puente, final) con instrumentación y transiciones apropiadas.
- **Generación musical IA** — usa los modelos MusicVAE y LSTM para generar material melódico que puedes grabar directamente en clips MIDI.

#### Control remoto

Un servidor LAN integrado expone un protocolo WebSocket + JSON para control y monitorización externa. Las apps compañeras pueden conectarse por la red para leer el estado del transporte, metadatos del proyecto, niveles de medidores y enviar comandos — todo protegido por un token de emparejamiento.

#### Piano Roll y cuantización no destructiva

Escribe y corrige música con el ratón: dibuja, mueve, redimensiona y borra notas, y ajusta la velocidad de cada una. El panel **Quantize** ofrece rejilla, swing, catch range, movimiento máximo, **iQ** (cuantización iterativa) y **freeze**. Los contornos fantasma muestran dónde estaban las notas antes de cuantizar, para que puedas escuchar la diferencia y volver atrás cuando quieras.

La vista sigue al playhead mientras reproduces y grabas. El editor también se puede desacoplar en su propia ventana.

#### Mezclador, efectos y automatización

El mezclador da a cada pista un canal con insertos, sends, pan, mute/solo y medidor de nivel, además de un canal master. **Channel Settings** muestra el instrumento, los insertos y el routing del canal seleccionado.

EZMusicProduction aloja instrumentos y efectos **AU y VST3**. Hay lanes de automatización para volumen, pan, sustain (CC64) y cualquier parámetro de plugin: haz clic para añadir un punto, arrástralo para moverlo y haz clic derecho para borrarlo.

#### Trae tus proyectos de Cubase

Abre un proyecto `.cpr` de Cubase directamente y encuentra tu trabajo en su sitio: pistas, tempo, compás, notas MIDI, clips de audio recortados a su ventana de Cubase y estados de los plugins.

La importación es de **solo lectura**. Tu `.cpr` no se modifica nunca; al guardar, el proyecto se escribe en el formato propio de EZMusicProduction, `.ezmpp`.

#### Exportación y ficheros de proyecto

Los proyectos se guardan como `.ezmpp` y se pueden abrir, guardar y deshacer como cualquier documento. La exportación renderiza la mezcla completa offline a un WAV estéreo —16 o 24 bits a la frecuencia del proyecto— más rápido que en tiempo real. La exportación a MP3 está prevista.

### Arquitectura

```text
┌──────────────────────────────┐        ┌──────────────────────────────┐
│  Interfaz Flutter (Dart)     │        │  Motor de audio (C++20+JUCE) │
│  timeline · mixer · editores │◄──────►│  reproducción · mezcla · render │
└──────────────────────────────┘        └──────────────────────────────┘
        comandos · snapshots · streams sobre una C ABI estable (FFI)
```

- **La interfaz nunca se interpone en el audio.** La interfaz Flutter y el motor C++ son capas separadas conectadas por una C ABI estable; ninguna callback de audio cruza a Flutter.
- **El motor es la fuente de verdad.** El estado del proyecto, el undo/redo, la mezcla y el render offline viven en C++.
- **Cero I/O de disco en el hilo de audio.** Las fuentes de audio se decodifican a memoria y se remuestrean a la frecuencia del proyecto al añadir un clip, cambiando RAM por reproducción sin cortes.
- **Una frontera estable.** La API C pública está definida en [`engine/include/cadence/cadence_engine.h`](engine/include/cadence/cadence_engine.h).

### Requisitos

- **macOS** 12 o superior (Apple Silicon o Intel)
- **Linux** con GTK3 y ALSA/PulseAudio
- **Windows** 10 o superior (x64)

### Descarga

Descarga la última versión desde la [página de releases](https://github.com/eztornado/EZMusicProduction/releases/latest) o visita [ezmusicproduction.com](https://ezmusicproduction.com).

### Compilar desde el código

**Requisitos previos:** CMake 3.22+, Ninja, Flutter 3.29.x y dependencias específicas de cada plataforma. JUCE y FluidSynth se descargan automáticamente con CMake la primera vez.

#### macOS

**Requisitos previos:** Xcode (con las command-line tools) y `libsndfile` de Homebrew.

```bash
brew install cmake ninja libsndfile

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(sysctl -n hw.ncpu)"

cd ui
flutter pub get
flutter build macos --release
```

La app Flutter carga `libcadence_engine.dylib` en tiempo de ejecución. Después de compilar el motor, copia la dylib al bundle de la app y vuelve a firmarlo: `./build_app.sh` automatiza toda la secuencia (motor, build de Flutter, copia de la dylib y firma de código):

```bash
./build_app.sh
open ui/build/macos/Build/Products/Release/EZMusicProduction.app
```

El script espera el SDK de Flutter en `~/development/flutter`; ajusta su línea `PATH` si el tuyo está en otro sitio. La app también puede apuntar a otra biblioteca del motor con la variable de entorno `CADENCE_ENGINE_DYLIB`.

Para generar una build universal que funcione de forma nativa en Apple Silicon e Intel:

```bash
UNIVERSAL=1 ./build_app.sh
```

Esa variante enlaza FluidSynth de forma estática y desactiva SF3 (SF2 sigue funcionando), así que el motor no depende de bibliotecas de Homebrew.

#### Linux

**Requisitos previos:** GTK3, ALSA, openblas, libsndfile y cabeceras de desarrollo.

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

Copia la biblioteca compartida del motor en el bundle de Flutter:

```bash
cp build/engine/libcadence_engine.so ui/build/linux/x64/release/bundle/lib/
```

#### Windows

**Requisitos previos:** Visual Studio 2022 (carga de trabajo Desktop C++), CMake y Ninja.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

cd ui
flutter pub get
flutter build windows --release
```

Copia la DLL del motor junto al ejecutable:

```powershell
copy build\engine\Release\cadence_engine.dll ui\build\windows\x64\runner\Release\
```

#### Tests y CLI

```bash
cd ui
flutter analyze
flutter test

# Prueba de humo del motor sin ventana
build/engine/cadence_cli_artefacts/Release/cadence_cli selftest assets/test/sine440.wav
```

El motor también incluye una CLI de desarrollo con comandos para dispositivos, generación de tonos, reproducción, demos de proyecto y round-trips de documento; ejecútala sin argumentos para ver la lista.

### Estructura del repositorio

```text
engine/          motor de audio C++20, C ABI y CLI de desarrollo
  include/       cabeceras públicas y API C estable
  src/           motor, modelo, mezclador e instrumentos incluidos
  apps/cli/      harness de pruebas sin ventana
ui/              app Flutter multiplataforma (macOS, Linux, Windows)
  lib/engine/    bindings FFI de Dart, snapshots y servicio de proyecto
  lib/ui/        timeline, mixer, piano roll, automatización, diálogos
  lib/l10n/      traducciones en inglés, español y japonés
bindings/        puntero a los bindings canónicos de Dart
assets/          audio de demo y ficheros de marca
docs/design/     especificaciones de producto, UI y formatos
build_app.sh     compila, empaqueta y firma la app de macOS
```

El motor sigue usando `cadence` internamente como namespace, prefijo de la C ABI y nombre de la biblioteca dinámica.

### Documentación

- [`docs/design/ui-design.md`](docs/design/ui-design.md) — especificación de UI: arquitectura de información, sistema visual, mezclador, piano roll, keymap, contrato FFI y hoja de ruta.
- [`docs/design/cubase-import.md`](docs/design/cubase-import.md) — cómo funciona el lector de `.cpr`, qué cubre y sus limitaciones.
- [`docs/design/i18n.md`](docs/design/i18n.md) — idiomas de la interfaz y presentación de errores.
- [`docs/design/remote-control.md`](docs/design/remote-control.md) — servidor de control remoto por LAN: protocolo WebSocket + JSON, emparejamiento, comandos y eventos para la app compañera.

### Contribuir

Los informes de errores, las ideas y los pull requests son bienvenidos. Si quieres ayudar:

- Reproduce un fallo y [abre una incidencia](https://github.com/eztornado/EZMusicProduction/issues) con tu versión de SO, hardware y pasos para reproducirlo.
- Mejora las traducciones de [`ui/lib/l10n/`](ui/lib/l10n/) (inglés, español y japonés deben mantenerse sincronizados).
- Añade o corrige metadatos del catálogo de la librería de sonidos.
- Mejora la documentación de diseño o la cobertura de tests.

Antes de abrir un pull request, ejecuta `flutter analyze` y `flutter test` dentro de `ui/`, y mantén el motor compilando en configuración release.

### Estado

**0.1 pre-release.** La app está en desarrollo activo: las funciones, las APIs internas y el formato de proyecto pueden cambiar entre versiones. Haz copias de seguridad de tus proyectos importantes y cuenta con algún borde sin pulir.

### Licencia y créditos

EZMusicProduction se desarrolla como código abierto. Los componentes de terceros conservan sus propias licencias:

- **JUCE 8.0.9** — AGPLv3 o licencia comercial.
- **FluidSynth 2.6.1** — LGPL-2.1+.
- **Librería de sonidos** — sonidos de Wikimedia Commons y el Internet Archive bajo licencias libres (CC0, dominio público, CC-BY, CC-BY-SA), con la atribución guardada junto a cada descarga.

Creado por **EzTornado — Iván Pastor Simarro**.

<p align="center">
  <a href="https://ezmusicproduction.com">ezmusicproduction.com</a>
</p>

<a name="japanese"></a>

---

## 🇯🇵 日本語

<h3 align="center">音楽制作のすべてを、ひとつのアプリで。</h3>

<p align="center">
  macOS、Linux、Windows向けのオープンソースDAW
</p>

---

**EZMusicProduction**は、スタジオのすべてをひとつのウィンドウにまとめたクロスプラットフォームのDAWです。録音・編集・ミックス・オートメーションに加え、4つの内蔵楽器、21,000以上のフリー音源、本格的なノートエディター、そしてAIパワードのツールを最初から使えます。

プラグインを探し回る必要も、追加の音源パックを買う必要も、サブスクリプションもありません。インストールして開けば、すぐに音楽づくりを始められます。

### 主な内容

| 分野 | 内容 |
|---|---|
| **タイムラインと録音** | オーディオ／MIDIのマルチトラック編集、ビートスナップ、分割・コピー・貼り付け、メトロノーム、テンポと拍子、フレーム単位のプレイヘッド。 |
| **サウンドライブラリ** | 21,000以上のフリーなループとワンショット。カテゴリーと種類で検索でき、その場で試聴して選択中のトラックへ直接配置できます。 |
| **内蔵楽器** | EZInstruments（General MIDI 128音色＋ドラムキット）、EZSynth、EZDrums、EZOrquestra。 |
| **内蔵エフェクト** | EZQ4（8バンドパラメトリックEQ）、EZコンプレッサー、EZリミッター、EZアルペジエーター。 |
| **AIツール** | AIミックスアシスタント（レベル、パン、EQ、コンプレッションの自動バランス）、AIソングアレンジ（楽曲構造の生成）、AI音楽生成（MusicVAE/LSTM）。 |
| **リモートコントロール** | LAN WebSocketサーバーによる外部コントロールとモニタリング。 |
| **ピアノロール** | ノートの描画・長さ変更・ベロシティ編集に加え、グリッド、スウィング、キャッチレンジ、iQ、フリーズを備えた非破壊クオンタイズ。 |
| **ミキサーとエフェクト** | インサート、センド、パン、ミュート／ソロ、メーター付きのチャンネルストリップ、マスターストリップ、チャンネル設定。AU／VST3プラグインに対応。 |
| **オートメーション** | 音量、パン、サステイン、プラグインパラメーターのレーン。ポイントを編集でき、線形補間でつながります。 |
| **Cubaseの読み込み** | `.cpr`プロジェクトを読み取り専用で開き、トラック、ノート、オーディオ、テンポ、プラグイン状態を再構築。元ファイルは変更しません。 |
| **書き出し** | プロジェクトのサンプルレートで、16／24ビットのステレオWAVをオフライン書き出し。リアルタイムより高速です。 |
| **言語** | 英語・スペイン語・日本語のUI。システム設定に従うか、ユーザーごとに選択できます。 |

### 機能

#### 録音して、曲を組み立てる

アレンジビューがアプリの中心です。オーディオとMIDIのトラック、トラックごとの色、ミュート／ソロ、音量とパン、そして移動・トリム・分割・コピー・貼り付けができるクリップがあります。スナップはビートグリッドに従い、`⌘`を押している間は無効になって自由に配置できます。

編集はすべて1回のUndoで戻せます。Undo／Redoはインターフェースではなくオーディオエンジン側にあり、ウィンドウを切り離しても履歴が一貫します。

#### すぐに使えるサウンドライブラリ

ライブラリには**21,000以上のフリー音源**が収録されています。ループ、ドラム、パーカッション、ストリングス、シンセ、ボーカル、エフェクトまで、Wikimedia CommonsとInternet Archiveから集めた音源です。

- カテゴリーとループ／ワンショットで検索・絞り込み。
- プロジェクトを変更せずに試聴。
- 必要なときにダウンロードし、選択中のトラックのプレイヘッド位置へ追加。
- ダウンロードごとに、出典・作者・ライセンス（CC0、パブリックドメイン、CC-BY、CC-BY-SA）を記した`.license.txt`を保存。

#### 4つの内蔵楽器

- **EZInstruments** — FluidSynthによるGeneral MIDIカタログ全体（メロディック128音色＋パーカッションキット）。音色を選んですぐ弾けるので、アイデアを素早く形にできます。
- **EZSynth** — 3オシレーター、ユニゾン、サブ、ノイズ、マルチモードラダーフィルター、2基のLFO、グライド、プリセットバンクを備えたサブトラクティブシンセ。**LINES**機能では、ピアノロール上にポリフォニックなピッチディストーション領域を描けます。和音の各音がそれぞれの経路をたどります。
- **EZDrums** — 808／909系のシンセドラムキット、多彩なスタイルのパターン、テンポに同期するループ、設定可能な拍子。実際のMIDIクリップとして録音し、ほかのトラックと同じようにノート単位で編集できます。
- **EZOrquestra** — Alpine Project形式のライブラリに対応したオーケストラサンプラー。音ごとのサンプル、奏法、ベロシティレイヤー、ラウンドロビンに対応します。

#### 内蔵エフェクト

4つのプロセッサーがアプリに同梱されており、どのチャンネルやマスターバスにもすぐに追加できます：

- **EZQ4** — Pro-Qスタイルのワークフローを持つ8バンドパラメトリックEQ：バンドごとの有効化/タイプ/周波数/ゲイン/Q、24 dB/octのカット、RBJクックブックバイクアッドに基づく自然位相IIRフィルタリング。
- **EZコンプレッサー** — ソフトニー、アタック/リリース、パラレルミックス機能を備えたステレオリンクダイナミクスプロセッサー。
- **EZリミッター** — マスターバス用のルックアヘッドブリックウォールリミッター、ステレオリンクピーク検出とハードクランプセーフティシーリング。
- **EZアルペジエーター** — 楽器トラックのMIDIインサートスロットに配置されるMIDI-FXアルペジエーター。ノートを消費してアルペジオ化して再出力し、ホストトランスポートにサンプルロック。

#### AIパワードのツール

- **AIミックスアシスタント** — プロジェクトを分析し、すべてのトラックのレベル、パン、EQ、コンプレッションを自動バランス。ワンクリックでプロフェッショナルな初期ミックスを取得。
- **AIソングアレンジ** — シードアイデアから完全な楽曲構造を生成し、セクション（イントロ、バーズ、コーラス、ブリッジ、アウトロ）を適切な楽器編成とトランジションで作成。
- **AI音楽生成** — MusicVAEとLSTMモデルを使用してメロディックな素材を生成し、MIDIクリップに直接録音可能。

#### リモートコントロール

内蔵LANサーバーは外部コントロールとモニタリング用のWebSocket + JSONプロトコルを提供します。コンパニオンアプリはネットワーク経由で接続し、トランスポート状態、プロジェクトメタデータ、メーターレベルの読み取り、コマンド送信が可能です — ペアリングトークンで保護されています。

#### ピアノロールと非破壊クオンタイズ

マウスで音符を書き、直せます。描画、移動、長さ変更、削除、そしてノートごとのベロシティ調整。**Quantize**パネルにはグリッド、スウィング、キャッチレンジ、最大移動量、**iQ**（反復クオンタイズ）、**フリーズ**があります。クオンタイズ前の位置はゴースト表示されるので、違いを聴き比べていつでも元に戻せます。

再生・録音中はプレイヘッドを追いかけます。エディターは独立したウィンドウに切り離すこともできます。

#### ミキサー、エフェクト、オートメーション

ミキサーでは各トラックにインサート、センド、パン、ミュート／ソロ、レベルメーター付きのチャンネルストリップがあり、マスターストリップも用意されています。**チャンネル設定**では、選択中チャンネルの楽器、インサート、ルーティングを確認できます。

EZMusicProductionは**AU／VST3**の楽器とエフェクトをホストします。オートメーションは音量、パン、サステイン（CC64）、および任意のプラグインパラメーターに対応。クリックでポイント追加、ドラッグで移動、右クリックで削除できます。

#### Cubaseのプロジェクトをそのまま

Cubaseの`.cpr`プロジェクトを直接開くと、トラック、テンポ、拍子、MIDIノート、Cubase上の範囲でトリムされたオーディオクリップ、プラグインの状態がそのまま復元されます。

読み込みは**読み取り専用**です。`.cpr`が変更されることはなく、保存時にはEZMusicProduction独自の`.ezmpp`形式で書き出されます。

#### 書き出しとプロジェクトファイル

プロジェクトは`.ezmpp`ファイルとして保存され、通常のドキュメントと同じように開く・保存する・元に戻すができます。書き出しはミックス全体をオフラインでステレオWAVにレンダリングします。16または24ビット、プロジェクトのサンプルレートで、リアルタイムより高速です。MP3書き出しは今後対応予定です。

### アーキテクチャ

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

### 動作環境

- **macOS** 12以降（Apple Silicon または Intel）
- **Linux**（GTK3、ALSA/PulseAudio対応）
- **Windows** 10以降（x64）

### ダウンロード

[リリースページ](https://github.com/eztornado/EZMusicProduction/releases/latest)から最新版を入手するか、[ezmusicproduction.com](https://ezmusicproduction.com)をご覧ください。

### ソースからビルドする

**必要なもの：** CMake 3.22以降、Ninja、Flutter 3.29.x、プラットフォーム固有の依存関係。JUCEとFluidSynthは初回のCMake実行時に自動で取得されます。

#### macOS

**必要なもの：** Xcode（コマンドラインツールを含む）、Homebrewの`libsndfile`。

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

#### Linux

**必要なもの：** GTK3、ALSA、openblas、libsndfile、開発用ヘッダー。

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

エンジンの共有ライブラリをFlutterバンドルへコピー：

```bash
cp build/engine/libcadence_engine.so ui/build/linux/x64/release/bundle/lib/
```

#### Windows

**必要なもの：** Visual Studio 2022（Desktop C++ワークロード）、CMake、Ninja。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

cd ui
flutter pub get
flutter build windows --release
```

エンジンのDLLを実行ファイルの隣へコピー：

```powershell
copy build\engine\Release\cadence_engine.dll ui\build\windows\x64\runner\Release\
```

#### テストとCLI

```bash
cd ui
flutter analyze
flutter test

# ウィンドウなしでエンジンをスモークテスト
build/engine/cadence_cli_artefacts/Release/cadence_cli selftest assets/test/sine440.wav
```

エンジンには開発用CLIも付属しています。デバイス、トーン生成、再生、プロジェクトのデモ、ドキュメントのラウンドトリップなどのコマンドがあり、引数なしで実行すると一覧が表示されます。

### リポジトリ構成

```text
engine/          C++20オーディオエンジン、C ABI、開発用CLI
  include/       公開ヘッダーと安定したC API
  src/           エンジン、モデル、ミキサー、内蔵楽器
  apps/cli/      ヘッドレステストハーネス
ui/              Flutter製クロスプラットフォームアプリ（macOS、Linux、Windows）
  lib/engine/    Dart FFIバインディング、スナップショット、プロジェクトサービス
  lib/ui/        タイムライン、ミキサー、ピアノロール、オートメーション、ダイアログ
  lib/l10n/      英語・スペイン語・日本語の翻訳
bindings/        正規のDartバインディングへのポインター
assets/          デモ音源とブランド素材
docs/design/     製品・UI・フォーマットの設計仕様（スペイン語）
build_app.sh     macOSアプリのビルド、パッケージング、署名
```

エンジン内部では、名前空間、C ABIのプレフィックス、ダイナミックライブラリ名に引き続き`cadence`を使用しています。

### ドキュメント

- [`docs/design/ui-design.md`](docs/design/ui-design.md) — UI仕様：情報アーキテクチャ、ビジュアルシステム、ミキサー、ピアノロール、キーマップ、FFI契約、ロードマップ。
- [`docs/design/cubase-import.md`](docs/design/cubase-import.md) — `.cpr`リーダーの仕組み、対応範囲、制限事項。
- [`docs/design/i18n.md`](docs/design/i18n.md) — UIの言語とエラー表示。
- [`docs/design/remote-control.md`](docs/design/remote-control.md) — LANリモートコントロールサーバー：WebSocket + JSONプロトコル、ペアリング、コマンド、イベント。

### コントリビュート

バグ報告、機能のアイデア、プルリクエストを歓迎します。参加するには：

- 不具合を再現し、OSのバージョン、ハードウェア、再現手順を添えて[Issueを開く](https://github.com/eztornado/EZMusicProduction/issues)。
- [`ui/lib/l10n/`](ui/lib/l10n/)の翻訳を改善する（英語・スペイン語・日本語のキーを揃えてください）。
- サウンドライブラリのカタログのメタデータを追加・修正する。
- 設計ドキュメントやテストカバレッジを改善する。

プルリクエストを開く前に、`ui/`内で`flutter analyze`と`flutter test`を実行し、エンジンがrelease構成でビルドできる状態を保ってください。

### 状態

**0.1 pre-release。** アプリは活発に開発中です。機能、内部API、プロジェクト形式はリリース間で変更される可能性があります。大切なプロジェクトはバックアップし、未完成な部分があることを前提にしてください。

### ライセンスとクレジット

EZMusicProductionはオープンソースとして開発されています。サードパーティ製コンポーネントはそれぞれのライセンスに従います：

- **JUCE 8.0.9** — AGPLv3 または商用ライセンス。
- **FluidSynth 2.6.1** — LGPL-2.1+。
- **サウンドライブラリ** — Wikimedia CommonsとInternet Archiveのフリーライセンス音源（CC0、パブリックドメイン、CC-BY、CC-BY-SA）。各ダウンロードのそばに出典情報を保存します。

制作：**EzTornado — Iván Pastor Simarro**

<p align="center">
  <a href="https://ezmusicproduction.com">ezmusicproduction.com</a>
</p>
