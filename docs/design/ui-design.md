# Cadence — Especificación de diseño de interfaz

> **Working title:** *Cadence* (nombre provisional, pendiente de decisión).
> **Versión:** 0.1 — 2026-09-22 — primer borrador de diseño.
> **Producto:** DAW híbrido **funcionalmente Cubase × Mixcraft**, **visualmente cercano a Mixcraft**.
> **Stack asociado:** UI en Flutter · Core de audio en C++20/23 + JUCE · puente FFI por snapshots/eventos.
> Documento hermano (pendiente): `docs/design/architecture.md` (core C++, modelo de datos, protocolo FFI).

---

## Índice

1. [Visión de producto](#1-visión-de-producto)
2. [Principios de diseño](#2-principios-de-diseño)
3. [Decisiones por defecto](#3-decisiones-por-defecto)
4. [Arquitectura de información](#4-arquitectura-de-información)
5. [Ventana principal](#5-ventana-principal)
6. [Sistema visual](#6-sistema-visual)
7. [Especificación por subsistemas](#7-especificación-por-subsistemas)
   - 7.1 Timeline & arrangement
   - 7.2 Tracks y track headers
   - 7.3 Clips
   - 7.4 Inspector contextual
   - 7.5 Piano Roll y cuantización (estilo Cubase)
   - 7.6 Mixer (estilo MixConsole)
   - 7.7 Gestión de efectos
   - 7.8 Channel Settings
   - 7.9 Browser / Library (estilo Mixcraft)
   - 7.10 Performance Panel (clip launcher)
   - 7.11 Transporte y grabación
   - 7.12 Automatización
   - 7.13 Export / Render
   - 7.14 Preferencias y configuración de audio
8. [Interacciones transversales](#8-interacciones-transversales)
9. [Mapeo UI ↔ Engine (contrato FFI)](#9-mapeo-ui--engine-contrato-ffi)
10. [Alcance por versiones](#10-alcance-por-versiones)
11. [Preguntas abiertas](#11-preguntas-abiertas)
12. [Referencias](#12-referencias)

---

## 1. Visión de producto

**Fórmula:** *la profundidad de Cubase con la inmediatez de Mixcraft.*

| Dimensión | Herencia | Significa |
|---|---|---|
| **Modelo de trabajo** | Cubase | Timeline lineal multipista como estructura principal: grabar, editar, mezclar, automatizar. |
| **Profundidad funcional** | Cubase | Cuantización no destructiva con iQ/swing/catch-range; mixer por canales con inserts/sends/EQ + Channel Settings; automatización por parámetros. |
| **Accesibilidad y flujo rápido** | Mixcraft | Library de loops/samples con preview sincronizada al proyecto; crear pista y empezar a grabar en < 30 s desde proyecto vacío; drag & drop como primera acción, menús como segunda. |
| **Lenguaje visual** | Mixcraft | Tema oscuro cálido, color por pista, bordes redondeados, controles grandes y legibles, densidad moderada con "modo denso" opcional. |
| **Compatibilidad de hábitos** | Cubase | Keymap por defecto compatible con Cubase donde sea razonable (el usuario de referencia viene de Cubase Pro 14). |

**Lo que NO somos (v1):** no es un clon de Cubase ni de Bitwig. Sin Logical Editor, sin ARA, sin control surfaces, sin video. Es un DAW lineal de grabación/producción con un launcher de clips (v2) y una library con preview tempo-matched como rasgo distintivo.

---

## 2. Principios de diseño

1. **Una ventana es el producto.** Todo el trabajo ocurre en la ventana principal (zonas y pestañas, no multiventana). Las únicas ventanas flotantes permitidas en v1: GUIs de plugins, Channel Settings y la lista de atajos. El Mixer y el Piano Roll viven como pestañas de la lower zone y *pueden* flotar (v1.5), pero nunca *necesitan* hacerlo.
2. **La UI nunca está en el camino del audio.** Todo elemento que muestre datos en vivo (playhead, medidores, waveform en reproducción) consume streams del engine; ningún gesto de UI bloquea o ralentiza el hilo de audio. Ver §9.
3. **Un gesto = una acción = un undo.** Cada operación de edición es atómica para el undo/redo, que vive en el engine (no en Flutter).
4. **Contextual pero predecible.** El Inspector cambia según la selección; su posición y tamaño nunca cambian. Quien aprende dónde están las cosas, siempre las encuentra ahí.
5. **Ratón primero, teclado siempre.** Toda acción de edición tiene gesto directo; toda acción frecuente tiene atajo; ningún flujo requiere menú contextual obligatorio.
6. **Densidad progresiva.** Por defecto, controles grandes y aireados (ADN Mixcraft). Un ajuste de densidad ("Compact mode", v1.5) reduce alturas de strip/tamaños de control para sesiones grandes.
7. **El color es información, no decoración.** El color de pista tiñe clips, strip de mixer, lane de automatización e Inspector. Un color = una pista, en todas las vistas.

---

## 3. Decisiones por defecto

Decisiones tomadas para poder diseñar sin bloqueos. Cualquiera es reversible; las marcadas ⚠ se revisan en la primera revisión de diseño.

| # | Decisión | Valor por defecto | Nota |
|---|---|---|---|
| D1 | Plataforma v1 | **macOS 12+** (Apple Silicon + Intel) | Desarrollo en este Mac; Windows en v1.5. |
| D2 | Ventana mínima | 1280 × 800; cómoda a partir de 1440 × 900 | Soporte HiDPI y escalado 100–200 %. |
| D3 | Tema | **Oscuro único** en v1 | Tema claro en v1.5 (los tokens de §6 ya lo contemplan). |
| D4 | Idioma de la UI | **Inglés** (convención del sector) | Textos externalizados en ARB desde el día 1; español en v1.5. |
| D5 ⚠ | Transporte | **Integrado en la barra superior** (estilo Mixcraft/Logic), no flotante | Cubase usa panel flotante; descartamos flotante para v1. |
| D6 ⚠ | GUIs de plugins | **Ventanas OS separadas** en v1, no embebidas en la ventana Flutter | Embedding por platform views se evalúa en v2 (jank en Windows es el riesgo). |
| D7 | Formatos de plugin v1 | **AU + VST3** en macOS | CLAP en v2 (JUCE no lo aloja de fábrica). |
| D8 | Keymap | Compatible con Cubase donde exista conflicto con convenciones macOS | Tabla en §8.5. |
| D9 | Transporte por defecto | Bars.Beats | Conmutador en el ruler (Time ↔ Beats, como el botón Time/Beats de Mixcraft). |
| D10 | Motor de tempo | Tempo y compás globales en v1; tempo track en v1.5 | El modelo de datos ya lo modela como curva (1 punto = tempo fijo). |

---

## 4. Arquitectura de información

Mapa completo de vistas y superficies de la v1:

```text
CADENCE
│
├── VENTANA PRINCIPAL
│   ├── Barra de menús (nativa macOS)
│   ├── Toolbar superior
│   │   ├── Transporte (rew / stop / play / rec / loop / metrónomo / punch)
│   │   ├── Display de posición (bars.beats + time), tempo, compás
│   │   ├── Snap, quantize rápido (solo con clip MIDI seleccionado)
│   │   ├── Herramientas (pointer, scissors, glue, eraser, mute, draw)  [v1.5]
│   │   └── Master volume + medidor master + CPU/Disco
│   ├── Zona izquierda A — Library (colapsable) ──────► §7.9
│   ├── Zona izquierda B — Track list + Inspector ────► §7.2, §7.4
│   ├── Centro — Timeline (arrangement) ──────────────► §7.1–7.3
│   └── Lower zone (pestañas, colapsable)
│       ├── Editor (Piano Roll) ──────────────────────► §7.5
│       ├── Mixer ────────────────────────────────────► §7.6
│       └── Performance Panel  [v2] ──────────────────► §7.10
│
├── VENTANAS FLOTANTES
│   ├── Channel Settings (EQ, inserts, sends de un canal) ──► §7.8
│   ├── GUI de cada plugin (ventana OS nativa) ─────────────► D6
│   └── Render / Export ────────────────────────────────────► §7.13
│
└── DIÁLOGOS MODALES
    ├── Preferencias (Audio / MIDI / UI / Plugins)
    ├── Nuevo proyecto / Plantillas
    └── Propiedades del proyecto (tempo, compás, sample rate)
```

Reglas de navegación:

- **Tab** mueve el foco entre zonas grandes; `F5` Library, `F3` lower zone open/close (ver keymap §8.5).
- Toda zona colapsable recuerda su estado por proyecto y globalmente.
- Los paneles nunca reflowan el timeline: abrir la Library recorta el área del arrangement, nunca lo desplaza verticalmente.

---

## 5. Ventana principal

### 5.1 Mockup general

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│ Cadence  File  Edit  Project  Track  MIDI  Audio  Mix  Help        ⌘⌥ esc │
├─────────────────────────────────────────────────────────────────────────────┤
│  ⏮  ■  ▶  ●   ⟳   🔂  🥁 ⏱ │ 5.3.1.0  00:00:12.4 │ 120.00  4/4  │ Snap:Beat │
│                             │     posición         │ tempo  compás│  🔍 50 %  │
├──────────┬──────────────┬───────────────────────────────────────────────────┤
│ LIBRARY  │ TRACK LIST   │  TIMELINE                                         │
│ (F5)     │              │                                                   │
│ [Loops]  │ ▸ 🎤 Vocals  │ 1     2     3     4     5     6     7     8      │
│ [Samples]│   S M ● vol  │ ┌────────────────────────┐                        │
│ [Instr]  │ ▸ 🎸 Guitars │ │██████████░░░░░░░░░░░░░░│ vocal_take_01          │
│ [Files]  │   S M ● vol  │ └────────────────────────┘                        │
│          │ ▸ 🎹 Keys    │      ┌──────────────────────┐ ┌───────────┐       │
│ 🔍 search│   S M ● vol  │      │♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪♪│ │░░░░░░░░░░░│ chords │
│ tags...  │ ▸ 🥁 Drums   │      └──────────────────────┘ └───────────┘       │
│          │   S M ● vol  │  ┌──────────────────────┐                         │
│ ▶ preview│ ▸ 🔊 FX Bus  │  │████░░░░░░░░░░░░░░░░░░│ guitar_ovd_02          │
│          │   M          │  └──────────────────────┘                         │
│          │              │                                                   │
│          │ [+ Add Track]│                                                   │
├──────────┴──────────────┴───────────────────────────────────────────────────┤
│ [Editor] [Mixer] [Performance*]                                  ▲ collapse │
│  Piano Roll / channel strips (según pestaña activa)                        │
└─────────────────────────────────────────────────────────────────────────────┘
   280 px         300 px        resto                                   28 px
```

\* Performance Panel es v2; en v1 la pestaña no se muestra.

### 5.2 Zonas y comportamiento

| Zona | Ancho/alto por defecto | Comportamiento |
|---|---|---|
| Toolbar superior | 64 px fijo | No se puede cerrar. Agrupa transporte (izquierda), displays (centro), herramientas contextuales (derecha). |
| Library | 280 px | Colapsable con `F5` o botón. Su estado es global (no por proyecto). |
| Track list | 300 px (300–500 arrastrable) | Siempre visible. Contiene headers de pista + Inspector debajo (§7.4). |
| Timeline | flex | Scroll horizontal con rueda/trackpad; scroll vertical con rueda; zoom §7.1. |
| Lower zone | 240 px (120–50 % de la ventana) | Colapsable (`F3`). Pestañas: Editor, Mixer. Auto-acciones: doble clic en clip MIDI la abre y muestra el piano roll; doble clic en strip del mixer cambia a Mixer. |

### 5.3 Redimensionado y estados de ventana

- Cada zona tiene handle de 4 px; double-click en el handle restaura el ancho por defecto.
- Layout recordado por proyecto (anchos, pestañas activas, alturas) + layout global por defecto para proyectos nuevos.
- Fullscreen macOS soportado; el timeline es la única zona que crece.

---

## 6. Sistema visual

### 6.1 Principio

Superficies oscuras neutras y planas; **el color vive en el contenido** (clips, medidores, acentos de pista), no en los contenedores. Controles con esquinas redondeadas (radio 6 px en controles, 4 px en clips), 1 px de borde por elevación, sin sombras decorativas — solo una sombra sutil en ventanas flotantes y popovers.

### 6.2 Tokens de color (tema oscuro)

| Token | Valor | Uso |
|---|---|---|
| `bg.deep` | `#16171B` | Fondo de la ventana, huecos del timeline |
| `bg.panel` | `#1E2026` | Track list, Library, toolbar, lower zone |
| `bg.surface` | `#252830` | Cards, strips del mixer, cabeceras de sección |
| `bg.elevated` | `#2C303A` | Popovers, menús, ventanas flotantes |
| `border.subtle` | `#32363F` | Separadores de 1 px |
| `border.strong` | `#3E4350` | Bordes de control activable |
| `text.primary` | `#E9EBF0` | Texto principal |
| `text.secondary` | `#9BA1AE` | Labels, unidades |
| `text.disabled` | `#5C6270` | Deshabilitado |
| `accent` | `#4D9DF7` | Selección, foco, botones primarios, playhead |
| `accent.record` | `#E5484D` | Record, arm, clip en grabación |
| `accent.play` | `#3DD68C` | Play, medidor "sano" |
| `accent.warn` | `#F5A524` | Medidor medio, avisos |
| `accent.clip` | `#E5484D` | Saturación de medidor, clímax |
| `selection.overlay` | `accent @ 20 %` | Selección por rectángulo |

Tema claro (v1.5): mismos tokens con inversión de superficies (`bg.deep → #F2F3F5`, etc.) y *el mismo* rol semántico. La paleta de pista no cambia entre temas.

### 6.3 Paleta de pista (16 colores)

Saturación alta pero no fluorescente; pensados para tintar clips rellenos y strip headers:

`#E5484D` red · `#E5622E` orange · `#F0A020` amber · `#B8C22A` lime · `#46A758` green · `#26A69A` teal · `#22B3D1` cyan · `#3E8FDB` blue · `#5B6EE1` indigo · `#6E56CF` violet · `#9B59B6` purple · `#D6409F` magenta · `#E93D82` pink · `#8E6A4E` brown · `#7C8B2D` olive · `#6E7191` slate

Asignación: automática secuencial al crear pista (saltando colores usados por pistas vecinas); cambio manual con click derecho en el dot de color → popover de swatches. Clip toma el color de pista al 100 % de tinte en fondo y una versión más oscura del mismo para waveform/MIDI-notes por defecto (el usuario puede recolorear un clip individualmente, v1.5).

### 6.4 Tipografía

| Uso | Fuente | Tamaño |
|---|---|---|
| UI general | SF Pro Text (macOS) / Inter fallback | 13 px, labels 11 px |
| Displays numéricos (posición, tempo, dB) | SF Mono / monospace tabular | 13–15 px |
| Nombres de pista/clip | SF Pro Text Medium | 12 px, elipsis a 1 línea |
| Atajos en menús/tooltips | SF Mono | 11 px |

Escala de spacing: 4 px base (4/8/12/16/24). Alturas mínimas de control táctil no aplican (desktop): botones ≥ 22 px, filas de lista ≥ 24 px.

### 6.5 Controles

- **Fader:** 160 px de recorrido vertical en strip de mixer; escala dB marcada a +6/0/−6/−12/−24/−60; unity marcado con muesca; readout numérico bajo el fader; drag fino con `Shift` (×0.1). Rango +6 → −∞ (−∞ a partir de −60, caída exponencial).
- **Knob:** solo en EQ y módulos de strip; arco de 270°, valor numérico en tooltip y doble-click para reset al default. Drag vertical (convención moderna), `Shift` = fino.
- **Botones M/S/R:** 22 × 22 px, cuadrados redondeados; estado activo: M amarillo `#F5A524`, S azul `#4D9DF7`, R rojo `#E5484D` (convenciones Cubase/Mixcraft).
- **Medidor:** vertical junto al fader, gradiente `accent.play → accent.warn (−12 dB) → accent.clip (−3 dB)`; peak hold 1.5 s; readout numérico opcional al hover; doble-click limpia peaks.
- **Iconografía:** set lineal de 16 px, stroke 1.5 px, consistente (una única familia; candidata: Lucide + iconos propios de audio: scissors, glue, magnet, metronome).

---

## 7. Especificación por subsistemas

### 7.1 Timeline & arrangement

**Ruler.** Doble escala siempre disponible: la superior en Bars.Beats, conmutador `Time/Beats` (dos botones pequeños estilo Mixcraft) alterna la escala activa. Marcas de compás con número; subrayas adaptativas al zoom. Click en el ruler mueve el playhead; drag en el ruler define el **cycle/loop range** (rango de loop amarillo como Cubase, mostrado como banda de 8 px bajo el ruler).

**Grid y snapping.**

| Modo | Comportamiento |
|---|---|
| Adaptive (default) | Resolución de grid según zoom: compás → 1/4 → 1/8 → 1/16 |
| Bar / Beat / 1/8 / 1/16 / 1/32 | Fijo |
| Event | Snap a bordes de otros clips (Cubase "Events") |
| Off | Libre |

Toggle magnético en toolbar; **`⌘` mientras se arrastra desactiva el snap temporalmente**. Snap afecta a: mover clips, trim, playhead, notas, puntos de automatización, marcadores.

**Zoom.**

| Gesto | Acción |
|---|---|
| `⌘` + rueda / pinch trackpad | Zoom horizontal centrado en el cursor |
| `⌥` + rueda | Zoom vertical (altura de pistas) |
| Rueda | Scroll horizontal |
| `Z` | Zoom a selección |
| `Shift+Z` | Zoom out completo (Cubase) |
| Follow playhead (toggle) | Auto-scroll durante reproducción, salto por página (no línea continua) |

**Marcadores:** track de marcadores global bajo el ruler (v1: añadir en playhead con `Shift+M`... ver keymap; renombrar con doble click; v1.5: loop markers con comportamiento de cycle).

**Pista de tempo/compás:** v1 fija en Propiedades de proyecto (tempo 90–250 BPM, compases de la lista común + custom). v1.5: tempo track editable con puntos como automatización.

### 7.2 Tracks y track headers

Tipos v1: **Audio**, **Instrument (MIDI)**, **FX Bus** (para sends), **Group** (v1.5). Header de pista (ancho de la track list):

```text
┌──────────────────────────────────┐
│ ● 🎤 Vocals               [▸][⌄] │  color, icono, nombre, altura, menú
│ [I] [◉] [M][S]  ⏺ monitor      │  input/monitor, mute, solo, arm
│ In: Input 1+2 ▾   Out: FX Bus ▾ │  routing (colapsable)
│        ────●────────  -3.2 dB   │  mini fader + pan (audio/instrument)
└──────────────────────────────────┘
```

- **Alturas:** Small 44 px (solo color+nombre+M/S), Medium 72 px (default, mini fader), Large 120 px (añade medidor + mini waveform overview v1.5). Drag del borde inferior ajusta altura individual.
- **Orden:** drag & drop del header; `⌘`+drag inserta copia de la pista (v1.5). Selección: click; `Shift+click` rango; `⌘+click` individual.
- **Iconos:** set por tipo (mic, guitar, piano, drum, synth, bus…), editable, con sentido Mixcraft (identificar la pista por icono y color antes que por nombre).
- **Add Track:** botón `+` al pie; menú con Audio, Instrument (elige instrumento), FX Bus, Group, *y desde Library: arrastrar un loop a la zona vacía crea la pista con el nombre del loop (comportamiento Mixcraft)*.

### 7.3 Clips

**Audio clip:**

```text
┌────────────────────────────────┐
│ ╱╲    nombre_del_clip.wav      │  ← fade-in handle (esquina sup. izq.)
│╱  ╲╱╲    ╱╲╱╲    ╱╲            │
│    ╱  ╲╱╲╱    ╲╱╲   ╱╲╱╲       │  waveform (render del engine, §9)
│ ╱╲╱    ╲╱╲    ╱  ╲╱╱    ╲      │
│╱                        ╲______│  ← fade-out handle
└──────────◀──────────────────▶──┘
            loop pegs (borde inferior) [v1.5]
```

- **Cuerpo:** fondo = color de pista (alpha ~0.85), waveform en tinte más claro; nombre arriba-izquierda; borde 1 px, redondeo 4 px.
- **Fade in/out:** handles triangulares en esquinas superiores (formato Cubase/Mixcraft), curva lineal por defecto, `⌥+drag` curva; auto-crossfade al solapar clips si "Auto crossfade" está activo (default ON, curva equal-gain de 10 ms ajustable).
- **Trim:** drag del borde izquierdo/derecho (con snap); `⌘` durante trim desactiva snap; trim no destructivo (media file intacta, sándwich de offsets).
- **Mover/copiar:** drag mueve (con snap); `⌥+drag` copia; `⌘+D` duplica adyacente; drag vertical entre pistas del mismo tipo permitido (audio↔audio, midi↔midi).
- **Split:** `S` parte todos los clips bajo selección en el playhead; scissors tool (v1.5) parte en la posición del cursor.
- **Mute de clip:** `⌥+M` o eraser… (v1.5); clip muted con hash diagonal y alpha 40 %.
- **Loop pegs** (v1.5): borde inferior con asas ◀▶; arrastrar extiende repitiendo el clip con las repeticiones mostradas.
- **Time-stretch:** por defecto el audio **no** se estira al cambiar tempo (v1). Modo del clip en Inspector: `Free` (default), `Stretch` (musical, quality: Normal/Pro), `Wrap` (loop library se estira siempre — heredado de la Library §7.9).

**MIDI clip:** fondo de color con preview de notas (notas como rectángulos pequeños pitch→altura dentro del clip, tinte más oscuro); doble click abre Piano Roll en lower zone; bordes de clip con "peg" de extensión continua de notas.

### 7.4 Inspector contextual

Vive bajo los headers de pista en la track list (como el Inspector de Cubase, pero sin pestañas múltiples — secciones colapsables, más cercano a Mixcraft en simplicidad). Muestra el objeto seleccionado con esta prioridad: **clip seleccionado → pista seleccionada → vacío (ayuda contextual)**.

| Selección | Secciones |
|---|---|
| Pista Audio | Name/Color · Routing (in/out) · **FX list** (§7.7) · Sends (4) · Volume/Pan (fader mini + valor) |
| Pista Instrument | Name/Color · Instrument (slot + preset + botón GUI) · **FX list** · Sends · Volume/Pan |
| Clip audio | Name · Gain · Fades (in/out ms + curva) · Time-stretch mode · Reverse (v1.5) · Normalizar (v1.5) |
| Clip MIDI | Name · Transpose (st) · Finetune (cents) · Velocity offset · **Quantize (preset + botón aplicar)** · Loop |

Regla de interacción: cualquier cambio en el Inspector es directo (sin modo "editar"), con undo por gesto de drag y por campo (un dblclick+type = un undo).

### 7.5 Piano Roll y cuantización (estilo Cubase) — núcleo de la petición

**Ubicación:** lower zone, pestaña Editor. Se abre con doble click en clip MIDI, `Return` con clip seleccionado, o `Ctrl+E`.

```text
┌─────────────┬──────────────────────────────────────────────┬──────────────┐
│ INSPECTOR   │ TOOLBAR: Q/iQ · Snap ▾ ·.velocity · solo/sub │  INFO LINE   │
│ ┌───────────┤ ┌──────────────────────────────────────────┐ │  pos: 3.2.1  │
│ │QUANTIZE ▾ │ │ C#5 ─┤    │  │▄▄│   │    │  │    │  │   │ │  len: 1/8    │
│ │  (§7.5.2) │ │ C4  ─┤    │ ▄│  │▄▄▄│   │    │  │    │  │ │  vel: 98     │
│ ├───────────┤ │ A3  ─┤ ▄▄▄│  │  │   │ ▄▄│   │    │  │   │ │              │
│ │TRANSPOSE  │ │ F3  ─┤    │  │  │   │   │ ▄▄▄▄▄│   │  │   │ │              │
│ │  [0 st]   │ │    ─┼────┼──┼──┼───┼───┼────┼───┼──┼───┤ │              │
│ │VELOCITY   │ │     │ 1  │  │2 │   │ 3 │    │ 4 │  │   │ │              │
│ │  offset 0 │ └──────────────────────────────────────────┘ │              │
│ ├───────────┤ ┌──────────────────────────────────────────┐ │              │
│ │CLIP       │ │ VELOCITY LANE  (vertical bars, editable) │ │              │
│ └───────────┘ └──────────────────────────────────────────┘ │              │
└─────────────┴──────────────────────────────────────────────┴──────────────┘
```

#### 7.5.1 Editor

- **Note display:** teclado vertical (C1–C7 visible, scrolleable, `Ctrl+⌅`…); teclas negras con fondo diferenciado; preview de sonido al click en tecla (audition). Notas: rectángulos redondeados color de pista; seleccionadas: borde `accent`; drag mueve (pitch+time, con snap); borde derecho trim length; `⌥+drag` copia; dibujar con lápiz (tool por defecto al `⌘+click` en vacío); borrar con `Del` o click derecho.
- **Auto-scroll vertical inteligente:** si las notas nuevas grabadas salen de la vista, la vista sigue la nota más grave/aguda recientemente tocada (comportamiento Cubase).
- **Lanes (parte inferior, pestañas):** Velocity (barras verticales, drag individual o selección múltiple; `⌘+drag` horizontal = rampa), CC lanes añadir/persistir por clip (v1: CC1 Modwheel, CC7 Volume, CC11 Expression, CC64 Sustain; v1.5: cualquier CC).
- **Info line:** posición/longitud/pitch/velocity de la nota bajo el cursor o selección (editable numéricamente).

#### 7.5.2 Quantize Panel — especificación funcional (referencia directa: Quantize Panel del Key Editor de Cubase)

Ubicación: sección superior del Inspector del editor, sección colapsable con preview en vivo.

| Control | Rango / valores | Comportamiento |
|---|---|---|
| **Preset** | Fábrica + usuario | Fábrica: `1/4, 1/8, 1/8T, 1/8., 1/16, 1/16T, 1/16., 1/32, 1/32T` cada uno "Straight"; además presets combinados ("1/16 Shuffle 55 %", etc.). Guardar preset actual con icono 💾. |
| **Grid** | 1/1 … 1/128 + T (triplet) + . (dotted) | Define la rejilla destino. |
| **Grid Type** | Straight / Triplet / Dotted | Atajo visible del calificador de Grid (ambos controles se sincronizan). |
| **Swing** | 0–100 % | Desplaza cada segunda posición del grid hacia la siguiente (shuffle). **Solo activo en grids straight** (regla Cubase). Se muestra en el preview y también afecta al grid visual del editor (toggle "Grid follows quantize"). |
| **Catch Range** | 0–100 % | Notas dentro de ese % de cercanía a su posición de grid destino **no se mueven** (protege la humanización). |
| **Max Move** | 0–100 % | Distancia máxima que una nota puede desplazarse; notas más lejos quedan donde están. |
| **Non-Grid Positions** | checkbox | Si ON, notas que caen *entre* dos posiciones de grid (en el punto medio) no se cuantizan. |
| **Iterative Strength** | 1–100 % | Cuánto se acerca la nota al grid por cada aplicación del iQ. |
| **Botones** | `Quantize` `iQ` `Reset` | Quantize aplica; iQ aplica un paso iterativo; Reset devuelve a posiciones originales. |

**Modo Q / iQ en toolbar:** el botón principal de quantize del editor puede operar en modo *Quantize* (aplica completo) o *iterative* (cada pulsación acerca un % adicional), toggle con el propio botón (como el Q/iQ de Cubase) y su atajo `Q`.

**Modelo de datos (no destructivo):** la cuantización se guarda como **propiedad del conjunto de notas** (`quantizePresetId` + parámetros) sin tocar las posiciones originales hasta *Freeze*. Consecuencias visibles:

1. Las notas muestran su posición cuantizada; `Reset` vuelve a la original sin undo de por medio.
2. **Ghost display** (toggle): contorno de la posición original de cada nota cuantizada.
3. **Freeze Quantize** (botón + comando): "hornea" las posiciones actuales como originales y limpia la propiedad; se usa antes de editar posiciones a mano para no perder la referencia.
4. Aplicar un preset distinto re-cuantiza desde las posiciones originales (no acumulativo), salvo iQ que es acumulativo por definición.

**Targets:** notas seleccionadas; si no hay selección, todo el clip activo. **Cuantizar desde fuera del editor:** el Inspector del clip MIDI (§7.4) expone preset + botón *Apply* que aplica el último preset usado a todo el clip (flujo rápido Mixcraft).

**Fuera de alcance v1** (documentado para no perderlo): quantize de *length* (longitud de nota al grid siguiente), quantize de velocity (procesado de curvas), Logical Editor, quantize en tiempo real de entrada (input quantize) → v1.5/v2.

### 7.6 Mixer (estilo MixConsole, visual Mixcraft)

**Ubicación:** lower zone, pestaña Mixer; florable en v1.5. Orden de strips: canales de pista (orden de proyecto) → buses FX → grupos (v1.5) → **Master** siempre a la derecha, separado.

```text
┌─ 1 ──────────────┐ ┌─ 2 ──────────────┐ ┌─ FX1 ────────────┐ ╔═ MASTER ═══════╗
│ ● Voice          │ │ ● Guitars        │ │ ● Reverb Bus     │ ║ ● Master       │
│ ┌──────────────┐ │ │ ┌──────────────┐ │ │ ┌──────────────┐ │ ║ ┌────────────┐ │
│ │ IN  Int 1 ▾  │ │ │ │ IN  In 1+2 ▾ │ │ │ │ IN  —        │ │ ║ │ IN  —      │ │
│ └──────────────┘ │ │ └──────────────┘ │ │ └──────────────┘ │ ║ └────────────┘ │
│ [FX1][FX2][ ][ ] │ │ [FX1][ ][ ][ ]   │ │ [FX1][FX2][ ][ ] │ │ [ ][ ][ ][ ]   │
│ S1 ─●──  S2 ─●─  │ │ S1 ─●─  S2 ─     │ │ S1 ─    S2 ─     │ │ S1 ─    S2 ─   │
│ S3 ─    S4 ─     │ │ S3 ─    S4 ─     │ │ S3 ─    S4 ─     │ │ S3 ─    S4 ─   │
│      PAN         │ │      PAN         │ │      PAN         │ │      PAN       │
│   ◄────●───►     │ │   ◄─●──────►     │ │   ◄────●───►     │ │   ◄────●───►   │
│    ║███║         │ │    ║██║          │ │    ║██║          │ │    ║████║      │
│    ║███║  ▲peak   │ │    ║██║          │ │    ║██║          │ │    ║████║      │
│    ║██▌║         │ │    ║█▌║          │ │    ║█▌║          │ │    ║███▌║      │
│  ──●──  -3.2 dB  │ │  ─●──  -6.8 dB   │ │  ──●──  -12.0 dB │ │  ──●──  0.0 dB │
│ [M][S][ ]        │ │ [M][S][◉]        │ │ [M][S][ ]        │ │ [M]            │
│ OUT: Master ▾    │ │ OUT: Master ▾    │ │ OUT: Master ▾    │ │                │
└──────────────────┘ └──────────────────┘ └──────────────────┘ ╚════════════════╝
```

**Anatomía del strip (arriba→abajo):** número+color+nombre (doble click renombra) · routing IN (audio: entrada de interfaz; instrument: instrumento; bus: —) · **4 slots de inserts** (vacíos con `+`) · **4 sends** (knob/fader horizontal, toggle pre/post con click derecho, activación al mover) · pan · fader con medidor · M/S/(R en audio) · routing OUT.

**Interacciones:**

- Selección múltiple de canales: `⌘+click`, `Shift+click` rango; acciones (mute/solo/fader con `⌥+drag` simultáneo... v1.5: link de canales).
- Fader: drag; `Shift` fino; doble-click reset a unity; escribir valor con `⌥+click` (campo editable); rueda = ±0.5 dB.
- Pan: knob horizontal estilo Cubase (un solo valor en v1, balance estéreo); doble-click = centro.
- Meters: siempre activos; peak hold; click-derecho → limpiar peaks, cambiar a pre-fader metering (v1.5).
- Doble click en la zona EQ/strip (o click en nombre de sección) abre **Channel Settings** (§7.8).
- Menú contextual de strip: Rename, Color, Copy/Paste channel settings (v1.5), Move to group (v1.5), Delete track.

**EQ:** en v1, cada canal audio/instrument trae un **EQ paramétrico de 4 bandas interno** (estilo channel strip de Cubase: LowShelf — 2× paramétricas — HighShelf, con toggle HP), editable **solo en Channel Settings**; en el strip, solo el toggle ON/OFF y un LED de actividad. Mixcraft-style: presets ("Vocal Air", "Guitar Cut", "Bass Tight"…) en dropdown.

**Canal Master:** strip con las mismas secciones + su propia cadena de inserts (master effects, flujo Mixcraft) y medidor estéreo con readout dB; *no* tiene sends.

**Racks colapsables (v1.5):** botón de vista "extended" que muestra/oculta filas de sección (Gate/Comp/Lim módulos de strip estilo MixConsole llegarán en v1.5 como módulos fijos Gate→Comp→EQ→Sat→Lim con LED on/off, orden fijo como Cubase).

### 7.7 Gestión de efectos

**Modelo:** cada canal tiene: 1 cadena de **inserts** (máx. 8, 4 visibles en strip) · 4 **sends** a buses FX (los buses FX son pistas de tipo FX Bus con su propia cadena de inserts y salida a Master) · el **Master** con su propia cadena. Los *instruments* son un tipo de plugin que solo vive en el slot de instrumento de una pista Instrument.

**Superficies de gestión (las tres coexisten, mismo modelo):**

1. **Slots del strip del Mixer** (estilo Cubase): click en slot vacío → **plugin picker**; click en slot lleno → toggle bypass (⌥+click abre GUI).
2. **FX list del Inspector** (estilo Mixcraft — lista amable por pista):

```text
FX — Voice                    [+ Add FX]
┌──────────────────────────────────────┐
│ 1  🔵 ChromVerb        [Preset ▾][⏻][⚙] │  ⏻=bypass  ⚙=abrir GUI
│ 2  🔵 Vintage EQ       [Bright ▾][⏻][⚙] │
│ 3  ➕                                    │
└──────────────────────────────────────┘
```

3. **Channel Settings** (§7.8): la vista completa con reordenado por drag & drop.

**Plugin picker:** popover con búsqueda; categorías (EQ · Dynamics · Reverb · Delay · Modulation · Filter · Utility · Instrument · Other); subsecciones "Favorites" (pin manual) y "Recently used" (automático, 10); escaneado de AU+VST3 en Preferencias; plugins rotos van a lista negra silenciosamente con aviso.

**GUI de plugins:** ventana OS nativa (decisión D6), tamaño libre hasta el límite declarado por el plugin; cierre con `⌘W`; sólo una instancia de GUI por plugin abierta. Estado bypass/bypass-GUI reflejado en todas las superficies.

**Presets:** barra de preset del plugin (estándar VST3: presets del propio plugin); preset manager de usuario unificado en v1.5 (browsing por categoría, guardado con metadata, favoritos).

**Sends:** knob de nivel + on/off al girar; pre/post por send en menú contextual; los buses FX viven como pistas visibles en el proyecto (visibilidad de su contenido en timeline: plegado por defecto).

### 7.8 Channel Settings

Ventana flotante por canal (una a la vez por canal; varias canales = varias ventanas permitido). Es la vista "profunda" estilo Cubase:

```text
┌ Channel Settings — Voice ────────────────────────────────┐
│ ┌ EQ ─────────────────────────────────────────────────┐ │
│ │   curva de respuesta (draggable: freq/gain/Q)       │ │
│ │  4 bandas + HP: puntos sobre la curva, ON/OFF       │ │
│ │  [Low Shelf] [Low Mid] [Mid] [High Mid] [High Shelf]│ │
│ │  presets ▾   [Flat]                                 │ │
│ └─────────────────────────────────────────────────────┘ │
│ ┌ INSERTS ────────────────┐ ┌ SENDS ──────────────────┐ │
│ │ 1 ChromVerb      [⏻][⚙] │ │ FX1 Reverb   ──●──  pre │ │
│ │ 2 Vintage EQ     [⏻][⚙] │ │ FX2 Delay    ─●─── post │ │
│ │ 3 +                      │ │ FX3 —                   │ │
│ │ (drag para reordenar)    │ │ FX4 —                   │ │
│ └──────────────────────────┘ └─────────────────────────┘ │
│ In ▾   Pan ──●──   Fader ───●── -3.2 dB  [M][S]          │
│                      [Copy settings]  (v1.5)             │
└──────────────────────────────────────────────────────────┘
```

- **EQ interactivo:** curva de respuesta en tiempo real (4096-point FFT del engine vía stream, §9); 5 puntos (4 bandas + HP) arrastrables; rueda = gain, `⌥+rueda` = Q; analizador ON/OFF.
- Inserts reordenables drag & drop; sends con toggle pre/post inline; fader/pan espejo del strip.
- (v1.5) Copiar/pegar ajustes de canal completo entre canales.

### 7.9 Browser / Library (estilo Mixcraft)

Zona izquierda colapsable (`F5`). Pestañas: **Loops** · **Samples** · **Instruments** · **FX Presets** (v1.5) · **Files** (navegador de carpetas del proyecto/home).

```text
┌ LIBRARY ────────────────────────┐
│ [Loops][Samples][Instr][Files]  │
│ 🔍 search…            [⚙ filtros]│
│ ┌────────────────────────────┐  │
│ │Tempo: any ▾ Key: any ▾     │  │
│ │Instrument: Drums ▾ Genre ▾ │  │
│ └────────────────────────────┘  │
│ ─────────────────────────────── │
│ ▶ 💿 funky_drum_92       92 ♭m  │
│ ▶ 💿 rock_beats_100     100 C   │
│ ▶ 💿 jazzy_brushes_80    80 F   │
│ ▶ 🎹 pad_warm_c               │  │
│ …                               │
│  ★ favorites          [import]  │
└─────────────────────────────────┘
```

- **Filas:** icono tipo (loop/sample/instrumento/preset), nombre, badge de tempo y tonalidad cuando se conocen (los loops de la library llevan metadata).
- **Preview (rasgo distintivo, ADN Mixcraft):** click en ▶ reproduce; si el loop tiene tempo/tonalidad y "Match project" está ON (default), se reproduce **estirado al tempo del proyecto y transportado a su tonalidad**, sincronizado al playhead si está reproduciendo (start en el próximo beat). Preview por click-through: un solo loop suena a la vez.
- **Drag & drop:** loop → timeline vacío crea pista Audio con nombre del archivo y clip en modo `Wrap` (auto-stretch); → pista existente inserta clip en posición soltada. Instrumento → zona vacía crea pista Instrument con ese instrumento. Sample → pista Audio (sin stretch).
- **Import:** drag de ficheros del Finder a la Library los copia a la carpeta del proyecto (`Audio/`, con dedupe por hash).
- **Fuentes v1:** carpetas locales indexadas (`~/Music/Cadence Library` + las que añada el usuario); **no** store online en v1 (ver §11).

### 7.10 Performance Panel (clip launcher) — v2

Grid filas=pistas, columnas=escenas; celdas acogen clips/loops (audio o MIDI) que se lanzan cuantizados al compás; columna "scene" lanza la fila completa; grabación de loops en celda en vivo (Mixcraft 10.5+). El mismo proyecto contiene timeline y launcher: los clips del launcher no aparecen en el timeline hasta "bounce to arrangement". *Se especificará en un documento aparte cuando toque (v2).*

### 7.11 Transporte y grabación

**Controles (toolbar):** `⏮` volver a inicio (o a la última posición de inicio de reproducción con segundo click), `■` stop, `▶` play/pause toggle, `●` record, `⟳` cycle on/off, metrónomo on/off (config: popover con volumen, precount 0–4 compases), punch in/out on/off (v1.5: auto-punch con cycle).

**Displays:** posición principal en Bars.Beats.Ticks + secundaria en h:m:s.ms; tempo editable (drag o doble-click, 90–250... no: 30–300 BPM); compás editable; master volume + medidor estéreo; medidores de CPU y de carga de disco (formato Cubase, mini).

**Grabación de audio:**

1. Arm en header de pista (`●`); seleccionar entrada mono/estéreo en routing del header.
2. Monitor: botón `◉` (con aviso de latencia si buffer > 256 samples).
3. `●` del transporte (o `*` numpad) arranca: precount configurable → graba en pista(s) armadas.
4. Durante la grabación el clip se dibuja en vivo (stream de waveform §9) en rojo.
5. Stop: el clip queda; con cycle activo, cada pasada crea un **take** (v1: se guardan todos, se reproduce el último; comping visual en v1.5).

**Grabación MIDI:** igual, con input device por pista (en routing), MIDI thru ON/OFF global, y las notas apareciendo en vivo en el clip. Input quantize OFF por defecto (v2).

**Audición segura:** si el buffer/latencia no permite monitorizar cómodamente, banner sugerido en el header (no modal).

### 7.12 Automatización

- Cada pista puede mostrar **automation lanes** (toggle en header `A` o menú de pista): primera lane = volume por defecto; añadir lanes para pan, sends, y cualquier parámetro de plugin marcado como automatizable (popup con árbol plugin→parámetro).
- Edición v1: puntos + segmentos lineales; herramientas: dibujar (lápiz libre que colapsa a puntos con tolerancia), seleccionar/mover, borrar; `⌘+click` añade punto; valores con curva Bezier en v1.5.
- Modos v1: **Read** siempre activo (lo dibujado se aplica); *Touch/Latch/Write* con faders en v1.5.
- La lane se superpone al clip en tinte del color de pista; la línea de volumen/pan de la pista puede mostrarse "inline" sobre los clips (toggle).
- Undo por gesto; snap de puntos al grid; `⌥+click` sobre punto borra.

### 7.13 Export / Render

Diálogo (ventana flotante, no modal para el preview... v1: modal con progreso):

| Sección | Opciones v1 |
|---|---|
| Rango | Whole project · Cycle range · Selección |
| Canales | Mixdown master · (v1.5: stems por pista/bus) |
| Formato | WAV 16/24/32f · FLAC 16/24 · MP3 192/256/320 (v1.5: AAC) |
| Sample rate | Del proyecto (default) o conversión 44.1/48/88.2/96 |
| Nombre/ruta | Campo + botón reveal en Finder; nombre por defecto `<proyecto>_<fecha>` |
| Post | (v1.5) Normalize −0.3 dBFS toggle |

Render en background con barra de progreso y tiempo estimado; se puede seguir trabajando (el engine pausa reproducción durante el render offline por conflicto de device — documentado como limitación v1).

### 7.14 Preferencias y audio setup

Pestañas: **Audio** (device CoreAudio, sample rate, buffer 32–2048 con indicador de latencia redondeo→ms, test de configuración con tono, medidor de xruns) · **MIDI** (inputs on/off, thru,MMC... v1: inputs + thru) · **UI** (idioma **v1: hecho** — ver `docs/design/i18n.md`; tema v1.5, densidad compacta v1.5) · **Plugins** (carpetas de scan AU/VST3, rescan, lista con estado y blacklist, abrir carpeta de VST3) · **Key Commands** (editor v1.5, hoy: tabla fija §8.5).

Idioma: el selector vive **dentro** del diálogo de Preferencias (sección "Idioma"), con
"Seguir al sistema" por defecto y Español/English/日本語. El nombre de cada idioma se
muestra siempre en su propio idioma. La elección se guarda en
`~/Music/Cadence/ui_settings.json` (fichero solo de interfaz: no toca el `settings.json`
de audio/MIDI).

Cambiar buffer/sample rate en caliente: reapertura del device con fade-out de 50 ms; si el device falla, banner persistente con botón "Open Audio Preferences" (nunca modal bloqueante).

---

## 8. Interacciones transversales

### 8.1 Selección

- Timeline: click clip; `Shift+click` rango; `⌘+click` toggle; rubber band en área vacía selecciona clips intersectados (y notas en el editor).
- `⌘A` selecciona todo (clips del timeline, notas del editor, canales en mixer según foco).
- La selección es por contexto de zona (timeline vs editor vs mixer), un único "foco activo" (la última zona clicada), visible con el acento de sus elementos.

### 8.2 Drag & drop (fuentes → destinos)

| Origen → Destino | Resultado |
|---|---|
| Library → área vacía | Nueva pista + clip (auto-stretch si loop) |
| Library → pista | Clip en la posición soltada |
| Finder → Library | Import (copia) |
| Finder → timeline | Import + pista si hace falta |
| Strip/routing out → otra pista | Cambia el routing (drag del chip "OUT:") v1.5 |
| Clip → otra pista (mismo tipo) | Mover (con `⌥` copiar) |
| Plugin del picker → slot | Insertar efecto |

Feedback de drop: el destino se resalta con overlay `accent @ 15 %` y borde `accent`; cursor de "no permitido" si el tipo no encaja (audio loop sobre pista Instrument → ofrece crear pista nueva al soltar, no error).

### 8.3 Menús contextuales

- **Clip:** Rename, Color ▸, Gain…, Split at playhead, Duplicate, Loop…, Quantize ▸ (MIDI), Reverse (v1.5), Convert to… (v2), Delete.
- **Track header:** Rename, Color ▸, Icon ▸, Height ▸, Add track ▸, Duplicate track, Move ▸, Delete.
- **Strip mixer:** Rename, Color, Copy channel settings (v1.5), Send section ▸ pre/post, Remove track.
- **Nota:** Velocity ▸ (v1.5: procesado), Quantize ▸, Duplicate, Delete.
- Todos los ítems muestran su atajo a la derecha (fuente única de verdad: keymap).

### 8.4 Undo / Redo

- `⌘Z` / `⌘⇧Z`, historial lineal por proyecto (sin ramas), gestionado **por el engine** (§9). Agrupación: un gesto continuo (drag, teclear un campo) = una entrada; grabación completa = una entrada (el clip resultante); aplicar quantize = una entrada.
- Límite: 200 pasos; el historial sobrevive a guardar/cerrar (v1.5).

### 8.5 Keymap propuesto (base Cubase-compatible)

Propuesta — pendiente de validación de conflicto con convenciones macOS (`⌘` reservado a comandos de documento estándar):

| Acción | Atajo | Nota |
|---|---|---|
| Play / Stop (toggle) | `Space` | |
| Record | `*` (numpad) | Cubase |
| Cycle on/off | `/` | Cubase |
| Return to start | `.` (numpad) o `Home` | |
| Split en playhead | `S` | |
| Duplicate | `⌘D` | Cubase |
| Copiar con drag | `⌥+drag` | Cubase/ macOS |
| Undo / Redo | `⌘Z` / `⌘⇧Z` | |
| Select all | `⌘A` | |
| Delete | `Del` | |
| Mute / Solo pista seleccionada | `M` / `S` (sin foco de texto) | |
| Arm pista seleccionada | `R` | |
| Nuevo track audio / instrument | `⌘T` / `⌘⇧T` | |
| Quantize (modo actual Q/iQ) | `Q` | En editor MIDI o clip MIDI seleccionado |
| Zoom a selección / out total | `Z` / `⇧Z` | |
| Bypass snap (momentáneo) | mantener `⌘` mientras se arrastra | |
| Abrir/cerrar Library | `F5` | |
| Abrir/cerrar lower zone | `F3` | |
| Abrir Channel Settings de la pista | `E` (en mixer con strip seleccionada) | |
| Buscar en Library | `⌘F` | |
| Guardar / Guardar como | `⌘S` / `⌘⇧S` | |
| Export audio… | `⌘E` | |
| Preferencias | `⌘,` | macOS |

### 8.6 Estados de error y límites

- **Errores y avisos:** los fallos de una acción del usuario (abrir/guardar/exportar, grabar, añadir sonido, plugin, dispositivo de audio…) salen en un **cuadro de alerta modal** (icono + título traducido + mensaje), nunca como texto suelto en la barra ni como SnackBar. Los SnackBar quedan solo para confirmaciones breves ("Clip copiado"). Un error técnico del engine se muestra como "Detalles técnicos" plegables dentro del propio cuadro (v1: texto del engine sin traducir; el título y el envoltorio sí van traducidos). Implementación: `ui/lib/ui/app_alert.dart` (`showAppError` / `showAppAlert`).
- **Device de audio perdido:** banner superior persistente + reproducción se detiene con fade 20 ms.
- **Disco lleno durante grabación:** stop de grabación, el clip se conserva hasta el último bloque escrito, banner con explicación.
- **Plugin crashea:** el aislamiento de proceso de plugin es v2 (en v1, crash del plugin = crash del proceso: documentado como riesgo conocido; mitigación: blacklist al reiniciar).
- **Proyecto corrupto:** auto-backup cada 5 min a `~/Music/Cadence/Backups/<proyecto>/` (10 generaciones); al abrir, ofrecer recuperar.

---

## 9. Mapeo UI ↔ Engine (contrato FFI)

Principio (de la discusión de arquitectura): **el engine C++ es dueño del documento y del undo; Flutter manda intents y pinta snapshots/streams.** Nunca una llamada FFI por bloque de audio ni por objeto.

Canal **C** = Command (intent, UI→engine, cola lock-free) · **S** = Snapshot event (engine→UI, con diffing) · **Stream** = datos de alta frecuencia vía shared memory ring buffer, leídos a 60 fps.

| Elemento UI | Dueño del estado | Canal |
|---|---|---|
| Documento (pistas, clips, notas, routing) | Engine | S (doc snapshot + diffs) |
| Undo/Redo | Engine | C (`undo`/`redo`) + S (doc) |
| Selección, foco, tool activa, zoom, scroll | UI (Flutter) | — (local) |
| Playhead | Engine (reloj de audio) | Stream (valor atómico, 60 fps) |
| Transporte (play/stop/rec/loop) | Engine | C + S (estado de transporte) |
| Meters (strips, master) | Engine | Stream (ring buffer, 30–60 fps) |
| Waveform de clips | Engine (peaks cacheados por niveles) | S (metadata) + Stream/texture al editar |
| Drag de clip/nota (preview durante gesto) | UI muestra fantasma | C al soltar (commit único) + `C(movePreview)` throttled opcional |
| Quantize (panel y resultados) | Engine (modelo no destructivo) | C (`setQuantizeParams`, `applyQuantize`, `iq`, `resetQuantize`, `freezeQuantize`) + S |
| EQ curva / analizador | Engine | S (params) + Stream (FFT 20 fps) |
| Snap y grid visual | UI (deriva de doc snapshot) | — |
| Autosave / backups | Engine | — |
| Library index y previews | Engine (preview player con tempo-match) | C + S (metadata) |
| Medidores CPU/disco | Engine | Stream |

Reglas:

1. Todo comando es asíncrono con confirmación por snapshot; la UI puede hacer *optimistic rendering* solo en gestos (fantasmas), nunca en estructura.
2. Los gestos en curso envían previews throttled (≤ 30 Hz) solo para feedback audible (p. ej. audition de posición), el commit real va al soltar.
3. Los streams usan formatos planos (floats/interleaved) sin alocación por frame en el lado Dart.

---

## 10. Alcance por versiones

| Área | V1 (MVP) | V1.5 | V2 |
|---|---|---|---|
| Timeline | Tracks, clips audio/MIDI, trim, fades, crossfade, snap, zoom, marcadores simples | Loop pegs, track icons/rangos, folders, tempo track, clip mute/color propio, scissors/mute tools | Comping de takes, track regions |
| Editor MIDI | Piano roll, velocity lane, **quantize panel completo (Q/iQ/swing/catch/maxmove/non-grid)**, CC1/7/11/64 | Length quantize, procesado de velocity, cualquier CC, drum editor | Input quantize, logical tools |
| Mixer | Strips completas, 4 inserts, 4 sends, EQ 4 bandas interno, master | Groups, link de canales, módulos Gate/Comp/Lim, racks extended, pre/post metering, copy channel | VCA, sidechain UI de envío a inserts |
| Efectos | AU+VST3, picker, 3 superficies, presets de fábrica | Preset manager usuario, plugin chain presets (Mixcraft FX chains) | Aislamiento de plugins por proceso, CLAP |
| Library | Folders locales, search, preview tempo/key-matched, drag&drop | Favoritos + rating, FX presets tab, import dialogs | Store/online packs |
| Automation | Lanes volumen/pan/sends/params, puntos + líneas, Read | Touch/Latch/Write, Bezier, inline edit | Automation follow clips |
| Launcher | — | — | Performance Panel completo |
| Otros | Render WAV/FLAC/MP3, autosave, backups, keymap Cubase | Tema claro, español, densidad compacta, stems export, key commands editor | Control surfaces, video, ARA |

---

## 11. Preguntas abiertas

1. **Nombre del producto** (working title *Cadence*).
2. ⚠ ¿Transporte integrado en toolbar (D5) o barra inferior dedicada como algunos DAW? — *recomendación: integrado, revisar tras primer prototipo.*
3. **¿Library store/contenido?** v1 solo carpetas locales; decidir si algún día habrá packs propios (afecta a modelo de licencias y negocio).
4. **¿Idioma de la v1 para uso propio?** UI en inglés (D4) aunque el usuario principal sea hispanohablante — confirmar comodidad.
5. **Windows:** ¿v1.5 o esperar a v2? (Afecta a decisiones D6/platform views.)
6. **EQ interno de canal:** ¿4 bandas fijo estilo Cubase o empezar por 3 bandas estilo Mixcraft y ampliar? — *recomendación: 4 bandas con HP, ocultando bandas si el usuario quiere simpleza.*

---

## 12. Referencias

**Mixcraft (referencia visual/flujo):**
- Acoustica — A Survey of Mixcraft (Performance Panel, Library): https://acoustica.com
- Mixcraft 10.6 — UI renovada, Quick Access, Stem Separation: https://acoustica.com/mixcraft/
- Paneles separables (mixer/library/piano roll flotantes), toggle Time/Beats en ruler: documentación Mixcraft 10.

**Cubase (referencia funcional):**
- Quantize Panel del Key Editor (Grid/Swing/Catch Range/Non-Grid/Max Move, swing solo en grids straight): https://www.steinberg.help — "Options for Quantizing to a Musical Grid", Cubase Pro.
- Q vs iQ como modos del botón Quantize desde Cubase 6; Iterative Strength: foros Steinberg + Quantize Setup.
- Channel Settings / Channel Strip modules (orden fijo Gate→Comp→EQ→Tools/Sat→Lim), MixConsole: https://www.steinberg.help — "Channel Strip / Equalizer", Cubase Pro.

**Arquitectura (contexto del proyecto):**
- JUCE + Tracktion Engine (doble licencia AGPLv3/comercial, aplicar a ambos): https://juce.com · https://github.com/Tracktion/tracktion_engine
- Zrythm v2 (rewrite C++23/Qt/QML, alpha 2026): https://forum.zrythm.org
- Decisión de stack y fases (Flutter piel / C++ corazón): conversación 2026-09-22; doc hermano pendiente `architecture.md`.
