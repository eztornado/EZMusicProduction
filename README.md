# Cadence (working title)

DAW híbrido **Cubase × Mixcraft**: timeline multipista clásico con la profundidad funcional
de Cubase (cuantización no destructiva, mixer por canales, gestión de efectos por insertos/sends)
y el lenguaje visual y el flujo de Mixcraft (library con preview tempo-matched, color por pista,
drag & drop primero).

## Stack

- **UI:** Flutter (la piel, nunca el corazón del audio)
- **Core de audio:** C++20/23 + JUCE (+ Tracktion Engine como referencia; licencia AGPLv3/comercial a evaluar)
- **Puente:** FFI por comandos/snapshots/streams — sin llamadas por bloque de audio

## Documentos

- [`docs/design/ui-design.md`](docs/design/ui-design.md) — Especificación de diseño UI v0.1
  (arquitectura de información, sistema visual, subsistemas, keymap, contrato FFI, alcance v1/v1.5/v2)
- [`docs/design/cubase-import.md`](docs/design/cubase-import.md) — Lector de proyectos
  de Cubase (`.cpr`): formato, cobertura (pistas, mixer, tempo, notas MIDI) y
  limitaciones (solo lectura, Dart puro)

## Estado

**Fase 4 en marcha** (2026-09-22) — app Flutter (`ui/`) con binding FFI vivo.
- **v0**: timeline (pistas + clips + playhead a frame rate), transporte, demo loader.
- **v1**: interacción — tap para seleccionar clips, drag para moverlos con snap a
  beat (⌘ anula el snap), Delete borra el clip seleccionado; cada gesto = un
  comando = una entrada de undo. 11 tests en verde (lógica, FFI, widget).

- **Fase 1** — device CoreAudio + playback de fichero + C API + CLI.
- **Fase 2** — documento de proyecto + mezcla + render offline.
- **Fase 3** — persistencia JSON + undo/redo en el engine.
- **Fase 4 (incrementos)** — v0 hecho; siguientes: interacción en timeline
  (seleccionar/mover clips, snap), track list real (§7.2), lower zone mixer.

## Entorno de desarrollo (este Mac)

- **macOS 13.6 (Ventura), Intel** — el Flutter actual (3.44+) exige macOS 14;
  usamos **Flutter 3.35.3 (Dart 3.9.2) en `~/development/flutter`** (zip oficial x64).
  PATH: `export PATH="$HOME/development/flutter/bin:$PATH"`.
- **Xcode 15.2 instalado y con licencia aceptada** — `flutter build/run macos` funcionan.
- **CocoaPods: NO necesario** con Flutter 3.35 y cero plugins (el build de macOS
  funciona sin él). No intentar `brew install cocoapods` en este Intel: brew tira de
  compilar LLVM desde fuente (horas). Si algún día hace falta, valorar
  `brew install ruby && gem install cocoapods`.
- Primera app: `flutter build macos --debug` ✓ y la app corre
  (`open ui/build/macos/Build/Products/Debug/cadence_ui.app`).
- El brew cask `flutter` instalado está roto en Ventura (3.47) — no usar.
- La dylib que carga la app: `build/engine/libcadence_engine.dylib` (target
  `cadence_engine_shared`), configurable con `CADENCE_ENGINE_DYLIB`; assets demo con
  `CADENCE_ASSETS_DIR` (default `~/daw/assets/test`).

## App Flutter (ui/)

```bash
export PATH="$HOME/development/flutter/bin:$PATH"
cd ui
flutter analyze          # lint
flutter test             # smoke tests FFI contra la dylib real (sin ventana)
flutter build macos --debug   # requiere Xcode (pendiente)
open build/macos/Build/Products/Debug/cadence_ui.app
```

Estructura: `lib/engine/` (bindings FFI + snapshot + servicio), `lib/ui/`
(timeline + transporte), `lib/l10n/` (traducciones), `test/engine_smoke_test.dart`.

### Idioma de la interfaz (es / en / ja)

Los textos viven en `ui/lib/l10n/app_es.arb` (plantilla, fuente de verdad),
`app_en.arb` y `app_ja.arb`; `gen-l10n` genera las clases al hacer
`flutter pub get` (`generate: true` en `pubspec.yaml`). En el código:
`context.l10n.<clave>` y, sin contexto, `L10n.current.<clave>`.

```bash
cd ui
flutter gen-l10n               # regenera lib/l10n/app_localizations*.dart
cat build/l10n_untranslated.txt   # debe ser {} (nada sin traducir)
```

El idioma se elige en **Preferencias → Idioma** (por defecto, seguir al sistema) y
se guarda en `~/Music/Cadence/ui_settings.json`. Cada sub-ventana es un proceso
aparte y lee el idioma por su cuenta. Los **errores salen en cuadros de alerta**
modales (`ui/lib/ui/app_alert.dart`), no como texto suelto ni SnackBar.
Detalles: `docs/design/i18n.md`.

### Build y prueba

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # descarga JUCE la 1ª vez
cmake --build build -j 8

CLI=build/engine/cadence_cli_artefacts/Release/cadence_cli
$CLI devices
$CLI tone assets/test/sine440.wav 3 440      # genera WAV de prueba
$CLI play assets/test/sine440.wav 2          # reproduce 2 s
$CLI selftest assets/test/sine440.wav        # play/pause/seek/stop/gain checks
$CLI proj-demo assets/test/proj_demo.wav --play   # 3 pistas: verifica mezcla/solo/mute
$CLI doc-test assets/test/doc                # undo/redo + save/load round-trip
```

### Estructura

- `engine/include/cadence/cadence_engine.h` — C ABI (superficie estable para FFI)
- `engine/include/cadence/model.hpp` — Project/Track/Clip + fuentes (snapshots inmutables)
- `engine/include/cadence/engine.hpp` — API C++ (cadence::Engine, ProjectAudioSource)
- `engine/src/` — implementación (JUCE 8.0.9 vía FetchContent, pineado)
- `engine/apps/cli/` — CLI de prueba (devices/tone/play/selftest/proj-demo/doc-test)
- `ui/` — app Flutter (bindings FFI, timeline, transporte)
- `bindings/dart/` — puntero a los bindings canónicos (ahora viven en `ui/lib/engine/`)
- `docs/design/ui-design.md` — especificación UI v0.1
- `docs/design/i18n.md` — idioma de la interfaz (es/en/ja) y avisos de error

Nota de arquitectura (Fase 2): las fuentes de audio se decodifican a memoria
(resampleadas a la tasa del proyecto) al añadir el clip — cero I/O de disco en el
hilo de audio a cambio de RAM. El streaming con read-ahead por pista es el
siguiente escalado natural.
