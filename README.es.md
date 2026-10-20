<p align="center">
  <img src="assets/branding/logo.svg" alt="EZMusicProduction" width="380">
</p>

<h3 align="center">Toda tu música, en un solo programa.</h3>

<p align="center">
  Un DAW de código abierto para macOS · Apple Silicon e Intel
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
  <img alt="Plataforma" src="https://img.shields.io/badge/plataforma-macOS%2012%2B-0a0a0b?logo=apple&logoColor=white">
  <img alt="Interfaz" src="https://img.shields.io/badge/interfaz-Flutter-02569B?logo=flutter&logoColor=white">
  <img alt="Motor" src="https://img.shields.io/badge/motor-C%2B%2B20%20%2F%20JUCE-ff2d20">
  <img alt="Estado" src="https://img.shields.io/badge/estado-0.1%20pre--release-ff2d20">
</p>

---

**EZMusicProduction** es una estación de trabajo de audio digital para macOS que reúne todo el estudio en una sola ventana: grabación, edición, mezcla y automatización, con cuatro instrumentos, una librería de más de 21.000 sonidos gratis y un editor de notas completo desde el primer arranque.

Sin buscar plugins, sin packs adicionales que comprar y sin suscripciones. Instala, abre y empieza a hacer música.

## Qué incluye

| Área | Qué hace |
|---|---|
| **Timeline y grabación** | Arreglo multipista para audio y MIDI, snap al beat, cortar/copiar/pegar, metrónomo, tempo y compás, playhead con precisión de frame. |
| **Librería de sonidos** | Más de 21.000 loops y golpes sueltos gratis, con búsqueda por categoría y tipo, escucha previa y colocación directa en la pista seleccionada. |
| **Instrumentos incluidos** | EZInstruments (128 sonidos General MIDI + kit de batería), EZSynth, EZDrums y EZOrquestra. |
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

- macOS 12 o superior
- Mac con Apple Silicon o Intel

## Descarga

Descarga la última versión desde la [página de releases](https://github.com/eztornado/EZMusicProduction/releases/latest) o visita [ezmusicproduction.com](https://ezmusicproduction.com).

## Compilar desde el código

**Requisitos previos:** Xcode (con las command-line tools), CMake 3.22+, Ninja, Flutter 3.35.x y `libsndfile` de Homebrew. JUCE y FluidSynth se descargan automáticamente con CMake la primera vez.

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
ui/              app Flutter para macOS
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

## Contribuir

Los informes de errores, las ideas y los pull requests son bienvenidos. Si quieres ayudar:

- Reproduce un fallo y [abre una incidencia](https://github.com/eztornado/EZMusicProduction/issues) con tu versión de macOS, modelo de Mac y pasos para reproducirlo.
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
