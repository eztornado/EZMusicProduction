<p align="center">
  <img src="assets/branding/logo.svg" alt="EZMusicProduction" width="380">
</p>

<h3 align="center">Toda tu música, en un solo programa.</h3>

<p align="center">
  Un DAW de código abierto para macOS, Linux y Windows
</p>

<p align="center">
  <a href="README.md">English</a> ·
  <a href="README.es.md">Español</a> ·
  <a href="README.ja.md">日本語</a>
</p>

<p align="center">
  <a href="https://ezmusicproduction.com">Web</a> ·
  <a href="https://github.com/eztornado/EZMusicProduction/releases/latest">Descargar</a> ·
  <a href="docs/design/ui-design.md">Documentación de diseño</a> ·
  <a href="https://github.com/eztornado/EZMusicProduction/issues">Incidencias</a>
</p>

<p align="center">
  <img alt="Plataforma" src="https://img.shields.io/badge/plataforma-macOS%20%7C%20Linux%20%7C%20Windows-0a0a0b?logo=apple&logoColor=white">
  <img alt="Interfaz" src="https://img.shields.io/badge/interfaz-Flutter-02569B?logo=flutter&logoColor=white">
  <img alt="Motor" src="https://img.shields.io/badge/motor-C%2B%2B20%20%2F%20JUCE-ff2d20">
  <img alt="Estado" src="https://img.shields.io/badge/estado-0.1%20pre--release-ff2d20">
</p>

---

**EZMusicProduction** es una estación de trabajo de audio digital multiplataforma que reúne todo el estudio en una sola ventana: grabación, edición, mezcla y automatización, con cuatro instrumentos, una librería de más de 21.000 sonidos gratis, un editor de notas completo y herramientas impulsadas por IA desde el primer arranque.

Sin buscar plugins, sin packs adicionales que comprar y sin suscripciones. Instala, abre y empieza a hacer música.

## Qué incluye

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

## Funciones

### Graba y organiza

La vista de arreglo es el centro del programa: pistas de audio y MIDI, color por pista, mute/solo, volumen y pan, y clips que puedes mover, recortar, cortar, copiar y pegar. El snap sigue la rejilla de beats; mantén pulsada `⌘` para desactivarlo cuando necesites colocar algo libremente.

Cada edición es un único paso de undo. Deshacer y rehacer viven en el motor de audio, no en la interfaz, así que el historial se mantiene coherente incluso entre ventanas desacopladas.

### Una librería de sonidos lista para usar

La librería indexa **más de 21.000 sonidos gratis** —loops, baterías, percusión, cuerdas, sinte, voces y efectos— procedentes de Wikimedia Commons y el Internet Archive.

- Busca y filtra por categoría y por loop/golpe suelto.
- Escucha un sonido sin modificar el proyecto.
- Descárgalo a demanda y añádelo a la pista seleccionada en el playhead.
- Cada descarga guarda un fichero `.license.txt` con su origen, autor y licencia (CC0, dominio público, CC-BY o CC-BY-SA).

### Cuatro instrumentos incluidos

- **EZInstruments** — todo el catálogo General MIDI (128 programas melódicos y un kit de percusión) con FluidSynth. Elige un sonido y toca; perfecto para convertir una idea en música rápido.
- **EZSynth** — un sintetizador sustractivo con tres osciladores, unísono, sub, ruido, filtro ladder multimodo, dos LFOs, glide y banco de presets. Su función **LINES** permite dibujar zonas de distorsión de altura polifónica sobre el piano roll: cada nota de un acorde sigue su propio camino.
- **EZDrums** — kits sintetizados de estilo 808/909, patrones de varios estilos, bucle sincronizado al tempo y compás configurable. Grábalo en un clip MIDI real y edítalo nota a nota como cualquier otra pista.
- **EZOrquestra** — un sampler de orquesta para librerías estilo Alpine Project, con samples por nota, articulaciones, capas de intensidad y variación round-robin.

### Efectos incluidos

Cuatro procesadores vienen con la app, listos para añadir a cualquier canal o al bus master:

- **EZQ4** — un ecualizador paramétrico de 8 bandas con flujo de trabajo estilo Pro-Q: activación/tipo/frecuencia/ganancia/Q por banda, cortes de 24 dB/oct y filtrado IIR de fase natural basado en biquads RBJ.
- **EZCompresor** — un procesador de dinámica estéreo con rodilla suave, ataque/liberación y mezcla paralela.
- **EZLimitador** — un limitador brick-wall con look-ahead para el bus master, con detección de pico estéreo y techo de seguridad.
- **EZArpegiador** — un MIDI-FX arpegiador que se coloca en el slot de inserto MIDI del instrumento, consumiendo notas y reemitiéndolas arpegiadas, sincronizado al transporte.

### Herramientas impulsadas por IA

- **Asistente de mezcla IA** — analiza el proyecto y auto-balancea niveles, pan, EQ y compresión en todas las pistas. Un clic para obtener una mezcla inicial profesional.
- **Arreglista de canciones IA** — genera estructuras completas de canción a partir de una idea semilla, creando secciones (intro, verso, estribillo, puente, final) con instrumentación y transiciones apropiadas.
- **Generación musical IA** — usa los modelos MusicVAE y LSTM para generar material melódico que puedes grabar directamente en clips MIDI.

### Control remoto

Un servidor LAN integrado expone un protocolo WebSocket + JSON para control y monitorización externa. Las apps compañeras pueden conectarse por la red para leer el estado del transporte, metadatos del proyecto, niveles de medidores y enviar comandos — todo protegido por un token de emparejamiento.

### Piano Roll y cuantización no destructiva

Escribe y corrige música con el ratón: dibuja, mueve, redimensiona y borra notas, y ajusta la velocidad de cada una. El panel **Quantize** ofrece rejilla, swing, catch range, movimiento máximo, **iQ** (cuantización iterativa) y **freeze**. Los contornos fantasma muestran dónde estaban las notas antes de cuantizar, para que puedas escuchar la diferencia y volver atrás cuando quieras.

La vista sigue al playhead mientras reproduces y grabas. El editor también se puede desacoplar en su propia ventana.

### Mezclador, efectos y automatización

El mezclador da a cada pista un canal con insertos, sends, pan, mute/solo y medidor de nivel, además de un canal master. **Channel Settings** muestra el instrumento, los insertos y el routing del canal seleccionado.

EZMusicProduction aloja instrumentos y efectos **AU y VST3**. Hay lanes de automatización para volumen, pan, sustain (CC64) y cualquier parámetro de plugin: haz clic para añadir un punto, arrástralo para moverlo y haz clic derecho para borrarlo.

### Trae tus proyectos de Cubase

Abre un proyecto `.cpr` de Cubase directamente y encuentra tu trabajo en su sitio: pistas, tempo, compás, notas MIDI, clips de audio recortados a su ventana de Cubase y estados de los plugins.

La importación es de **solo lectura**. Tu `.cpr` no se modifica nunca; al guardar, el proyecto se escribe en el formato propio de EZMusicProduction, `.ezmpp`.

### Exportación y ficheros de proyecto

Los proyectos se guardan como `.ezmpp` y se pueden abrir, guardar y deshacer como cualquier documento. La exportación renderiza la mezcla completa offline a un WAV estéreo —16 o 24 bits a la frecuencia del proyecto— más rápido que en tiempo real. La exportación a MP3 está prevista.

## Arquitectura

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

## Requisitos

- **macOS** 12 o superior (Apple Silicon o Intel)
- **Linux** con GTK3 y ALSA/PulseAudio
- **Windows** 10 o superior (x64)

## Descarga

Descarga la última versión desde la [página de releases](https://github.com/eztornado/EZMusicProduction/releases/latest) o visita [ezmusicproduction.com](https://ezmusicproduction.com).

## Compilar desde el código

**Requisitos previos:** CMake 3.22+, Ninja, Flutter 3.29.x y dependencias específicas de cada plataforma. JUCE y FluidSynth se descargan automáticamente con CMake la primera vez.

### macOS

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

### Linux

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

### Windows

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

### Tests y CLI

```bash
cd ui
flutter analyze
flutter test

# Prueba de humo del motor sin ventana
build/engine/cadence_cli_artefacts/Release/cadence_cli selftest assets/test/sine440.wav
```

El motor también incluye una CLI de desarrollo con comandos para dispositivos, generación de tonos, reproducción, demos de proyecto y round-trips de documento; ejecútala sin argumentos para ver la lista.

## Estructura del repositorio

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

## Documentación

- [`docs/design/ui-design.md`](docs/design/ui-design.md) — especificación de UI: arquitectura de información, sistema visual, mezclador, piano roll, keymap, contrato FFI y hoja de ruta.
- [`docs/design/cubase-import.md`](docs/design/cubase-import.md) — cómo funciona el lector de `.cpr`, qué cubre y sus limitaciones.
- [`docs/design/i18n.md`](docs/design/i18n.md) — idiomas de la interfaz y presentación de errores.
- [`docs/design/remote-control.md`](docs/design/remote-control.md) — servidor de control remoto por LAN: protocolo WebSocket + JSON, emparejamiento, comandos y eventos para la app compañera.

## Contribuir

Los informes de errores, las ideas y los pull requests son bienvenidos. Si quieres ayudar:

- Reproduce un fallo y [abre una incidencia](https://github.com/eztornado/EZMusicProduction/issues) con tu versión de SO, hardware y pasos para reproducirlo.
- Mejora las traducciones de [`ui/lib/l10n/`](ui/lib/l10n/) (inglés, español y japonés deben mantenerse sincronizados).
- Añade o corrige metadatos del catálogo de la librería de sonidos.
- Mejora la documentación de diseño o la cobertura de tests.

Antes de abrir un pull request, ejecuta `flutter analyze` y `flutter test` dentro de `ui/`, y mantén el motor compilando en configuración release.

## Estado

**0.1 pre-release.** La app está en desarrollo activo: las funciones, las APIs internas y el formato de proyecto pueden cambiar entre versiones. Haz copias de seguridad de tus proyectos importantes y cuenta con algún borde sin pulir.

## Licencia y créditos

EZMusicProduction se desarrolla como código abierto. Los componentes de terceros conservan sus propias licencias:

- **JUCE 8.0.9** — AGPLv3 o licencia comercial.
- **FluidSynth 2.6.1** — LGPL-2.1+.
- **Librería de sonidos** — sonidos de Wikimedia Commons y el Internet Archive bajo licencias libres (CC0, dominio público, CC-BY, CC-BY-SA), con la atribución guardada junto a cada descarga.

Creado por **EzTornado — Iván Pastor Simarro**.

<p align="center">
  <a href="https://ezmusicproduction.com">ezmusicproduction.com</a>
</p>
