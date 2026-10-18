# Carga de proyectos de Cubase (`.cpr`)

Estado: **incremental verificado** (2026-10-17). Esta entrega importa la
estructura del proyecto (pistas, tempo, compás), **las notas MIDI** (partes,
pitch, velocidad, posición y duración) y **los eventos de audio** (posición,
recorte, ganancia y fichero fuente). Las notas MIDI están verificadas 1:1 contra
ficheros SMF exportados por el propio Cubase y los eventos de audio contra la
duración real de los WAV del proyecto. La automatización y los plugins siguen
documentados como pendientes, con las pistas de ingeniería inversa ya obtenidas.

## Objetivo y disparador

- Cargar un proyecto de Cubase **directamente desde el buscador de ficheros**:
  el botón *Importar* de la timeline (`ui/lib/ui/timeline_view.dart`,
  `_importFile`) abre el selector nativo y, si el fichero termina en `.cpr`,
  llama a `EngineService.importCubaseProject(path)`; en caso contrario añade un
  clip de audio como antes.
- **Solo lectura**: nunca se escribe de vuelta al `.cpr`. Guardar sigue siendo
  `.ezmpp` (`saveProjectAs`).

## Dónde vive el código

| Fichero | Papel |
| --- | --- |
| `ui/lib/engine/cubase/cpr_reader.dart` | Lector **Dart puro** (solo `dart:convert`/`typed_data`/`math`). Analizable y testeable sin el motor nativo. |
| `ui/lib/engine/engine_service.dart` → `importCubaseProject` | Recrea el proyecto en el motor (tempo, compás, pistas, clips MIDI y clips de audio, e instrumentos/efectos con su estado). |
| `engine/src/engine.cpp`, `engine/src/c_api.cpp`, `engine/include/cadence/cadence_engine.h` | API nativa para cargar plugins en pistas y restaurar su estado (`track_set_fx_state` / `track_set_instrument_state`). |
| `ui/lib/ui/timeline_view.dart` | Selector de ficheros + despacho por extensión. |
| `ui/tool/cpr_inspect.dart` | Volcado de depuración: `dart run tool/cpr_inspect.dart <f.cpr>`. |
| `ui/test/cpr_reader_test.dart` | Fixture sintético + test guardado sobre un `.cpr` real. |

## El contenedor

Un `.cpr` es un RIFF con form type `NUND` (Steinberg). El cuerpo es una
**secuencia de pares `(ROOT, ARCH)`**:

```
ROOT descriptor = u32 len | nombre      (repetido: nombre lógico + clase ARCH)
ARCH payload    = datos reales de esa clase
```

Clases observadas en Cubase 13.0.40: `PAppVersion`, `PArrangement`,
`CmString`, `FAttributes` (x2), `UWindowLayout`, `StMedia::PAttributes`.
El lector se queda con el `ARCH` de clase `PArrangement` (5.7 MB en el proyecto
de referencia), que contiene el mixer y la timeline.

## Gramática de registros con nombre

Dentro del arrangement conviven dos codificaciones. La primera es un árbol
recursivo, **big-endian**:

```
registro := u32 nameLen(=strlen+1) | nombre | \0 | u16 tipo | payload
tipo 1  := i64
tipo 4  := f64
tipo 8  := string: u32 len(=strlen+4) | texto | \0 | EF BB BF   (UTF-8 BOM)
tipo 2  := contenedor; un u16 elige el subtipo:
   0x0006 objeto:      u32 nºHijos | hijos
   0x0005 array:       u32 nºItems | (u32 nºRegistros | registros)*
   0x0002 array i64
   0x0004 array string
   0x0007 blob:        u32 tamaño | bytes      (el panner vive aquí, f32 LE)
   0x0014 instancia de clase: nombre, 2×u32, u32 nºHijos | hijos
   0x00c9 array de instancias
```

### Canal de mixer → pista

Ancla del canal (`Name` es un objeto con un `String`):

```
00 00 00 05 "Name" 00  00 02 00 06  00 00 00 01  00 00 00 07 "String" 00 00 08
```

Campos verificados de cada canal:

- `Name` — el **nombre de pista** que ve el usuario (coincide con el nombre del
  `MMidiPart` que Cubase crea para esa pista). Puede traer espacios sobrantes,
  repetirse y venir **mal codificado** en UTF-8 re-codificado como Latin-1
  (`c3 8c c2 81` = "í" en `Sin título`); `_decodeText` lo repara.
- `Type` — `4` = pista de instrumento, `1` = pista de audio. Los tipos `7`
  (bus de entrada) y `8` (bus de salida) **no** son pistas; viven en el archivo
  del VST Mixer, no en el *arrangement* (ver «Master» abajo).
- `Volume/AnchorValue` — **la posición del fader en dB** (autoritativa).
  `Volume/Value` es una escala cruda donde `25856.0` = 0 dB
  (`dB = 20·log10(Value/25856)`).
- `Panner/audioComponent` — blob cuyo primer `f32` **little-endian** es la
  posición (0.0 = todo a la izquierda, 0.5 = centro, 1.0 = derecha);
  `pan = pos·2−1`.
- `IDString` — identificador interno del canal (`Synth`, `Audio 1`, …). No es
  el nombre mostrado, pero sirve para deduplicar.

**Cada canal aparece serializado dos veces** (una con `IDString`+`hasVSTi`, otra
sin). El lector deduplica por `IDString` (o por nombre normalizado si falta),
quedando una pista por canal real (29 en el proyecto de referencia).

### Master (salida principal) y nivel

El canal de salida principal (`Stereo Out`, `Type` = 8, `IDString` =
`OutputChannel`) **no está en el archivo del *arrangement*** sino en el del VST
Mixer (`FAttributes/StoredDevices/Mixer`), así que `detectMasterVolumeDb()` busca
en **todos** los archivos la misma ancla de canal y se queda con el `Volume/
AnchorValue` del bus de salida (0 dB si no hay ninguno). El importador lo lleva
al `masterGain` del motor (`setGain`); en RIKOS COOKING es −2.17 dB.

El nivel de cada clip importado depende de dos cosas, ambas ya alineadas con
Cubase:

- **El fader del canal** (`AnchorValue`) → `setTrackVolume`.
- **La ley de paneo al centro.** Cadence aplica la ley de potencia constante
  (cos/sin) → **−3 dB al centro**. Cubase usa esa misma ley por defecto
  (proyecto → *Stereo Pan Law* = −3 dB) **pero solo para canales mono**: para
  material estéreo el panner de Cubase es un *balance* y el *pan law* no se
  aplica (0 dB al centro). Por eso el motor ahora distingue: fuentes de 1 canal
  → potencia constante; fuentes de ≥2 canales → balance (`panGains()` en
  `engine.cpp`). Antes de este ajuste los audios estéreo importados sonaban
  ~3 dB bajos, compensados en parte por el fader de master que no se aplicaba.

### Plugins

Cabecera de plugin `\0\0\0\x07Plugin\0\0\x02\0\x06` → objeto con
`Plugin UID` (que contiene `GUID`, hexadecimal ASCII) y `Plugin Name`. Los VST3
que empiezan por `VST…` se decodifican a texto legible
(`5653544E694B376B6F6E74616B742037` → `VSTNiK7kontakt 7`).

### Instrumentos (VSTi por pista)

El VSTi que hospeda una pista de instrumento está en **su propio canal**, dentro
del objeto `Synth Slot`:

```
Synth Slot (obj)
  RuntimeID : i64
  Plugin isA: "VstCtrlInternalEffect"
  Plugin (obj)
    Plugin UID (obj) { GUID: "324755DF8FDF4788B4CE5B70A8037EC4" }
    Plugin Name: "ZENOLOGY"
```

El objeto `Plugin` usa una serialización interna que este lector **no** parsea
(tiene subtipos de registro desconocidos), así que el nombre se localiza por
patrón (`Synth Slot` → `Plugin Name`, acotado al rango del propio canal:
desde el ancla del canal hasta el ancla del siguiente). `CubaseTrack.
instrumentName` lo expone (`ZENOLOGY`, `Musio`, `Kontakt 7` en RIKOS; vacío
cuando el *slot* está libre, p. ej. `Studio Grand`).

Al importar, si en Cadence hay instalado un **instrumento** cuyo nombre coincide
con el del VSTi de Cubase (`_matchingPlugin`, comparación sin distinguir
mayúsculas y por contención en ambos sentidos), se carga en esa pista
(`trackSetInstrumentPlugin`) **con su estado** (ver abajo). Si no hay
coincidencia —o si el usuario nunca ha escaneado sus plugins— la pista se queda
con el **sintetizador interno** (`PianoSound`), de modo que las notas MIDI suenan
igualmente. Cubase guarda el estado del plug-in, no un preset legible, pero el
plug-in sí sabe reinterpretarlo: se le devuelve tal cual.

### Estado de los plugins (`audioComponent` / `editController`)

Cada objeto `Plugin` guarda **el estado serializado del plug-in en dos blobs**
consecutivos:

```
Plugin (obj)
  …
  audioComponent: blob   ← IComponent::getStateInformation  (estado "de
                            proceso": el parche, los ajustes de un efecto)
  editController: blob   ← IEditController::getStateInformation (suele ir vacío;
                            algunos VSTi lo usan)
```

Son **byte a byte lo que el plug-in devolvió** por la API VST3, en su propio
formato —este lector no lo interpreta—: XML propio (Musio: `VC2!`+`<PluginState>`,
que es `copyXmlToBinary` de JUCE), un *preset chunk* (`VstW…CcnK` en SINE/BBC),
msgpack (Raum: `Q$iN…`), zlib (iZotope: `78 9c…`), binario privado (Kontakt:
`hsin`/`DSIN`, ZENOLOGY) o una tabla de parámetros (los de Steinberg). El lector
los expone como `CubasePluginState{component, controller}`
(`CubaseTrack.instrumentState`, `CubaseTrack.insertStates[i]`,
`CubaseProject.masterInsertStates[i]`).

Al importar, cada plug-in cargado recibe su estado. Como JUCE espera **los dos
sub-estados combinados** en un sobre XML con base64 (`<VST3PluginState>
<IComponent>…</IComponent><IEditController>…</IEditController></VST3PluginState>`,
serializado con `AudioProcessor::copyXmlToBinary`), el motor lo recompone
(`wrapVst3PluginState` en `engine.cpp`, base64 propio de JUCE) y llama a
`setStateInformation`. El estado es **opaco**: no hay que entender el formato,
solo devolverlo.

Dos consecuencias:

- **Se prefiere VST3 sobre AudioUnit**: Cubase guarda estado de *proceso* VST3;
  un AU homónimo (mismo nombre en el catálogo) usa otro formato, así que
  `_matchingPlugin` ordena los VST3 primero y **solo se aplica estado a
  instancias VST3**. Un AU se carga igual, pero con su preset por defecto.
- Un *insert* cuyo estado **no** se pudo aplicar se deja **bypasseado**
  (`trackSetFxBypass(slot, true)`): sin el ajuste original, un *de-noiser* o un
  compresor sonarían mal con su defecto. Si el estado se aplica, el insert queda
  **activo**. El encadenado de master se registra pero aún no se instancia (ver
  limitaciones).

### Tempo y compás

- **Tempo**: `f32` big-endian de *segundos por pulso* justo después de
  `MTempoTrackEvent` (0.5 → 120 BPM). Se busca el primer valor plausible
  (0.2–1.5 s/pulso → 40–300 BPM).
- **Compás**: `MTimeSignatureEvent`; se localizan los campos `Numerator` /
  `Denominator` (i64) por su firma y se leen los 8 bytes siguientes.

### Notas MIDI (verificado)

Cada nota es un **registro de 113 bytes** dentro del *arrangement*. Se localiza
por la etiqueta ASCII `GLFX` que acompaña a cada evento; el inicio del registro
es `GLFX − 18` (o, equivalentemente, `\x00\x00\x00\x05GLFX − 14`):

```
S+0        tipo de evento  0x90 = nota (se conserva); 0xb0, 0x70… = otro (se descarta)
S+1..8     f64 BE  posición en ticks, **relativa al origen de contenido de la parte**
S+9        0
S+10       pitch (0..127)
S+11       velocity (0..127)
S+12..13   0
S+14..17   u32 nº de tags (5 en una nota)
S+18..     tags: cada uno `nombre(4 ASCII) | tipo(u16 BE) | payload(8)`
           GLFX i64=0 · Pler f64=0 · TDRH f64≈vel · VffO f64=0.5 · adcn i64=0x2000000000
S+88       f64 BE  duración en ticks (480 PPQ)      [= S+18 + nºTags·14]
S+96..112  17 bytes a cero
```

#### Posición de una parte (el modelo clave)

Cada parte MIDI es un objeto con esta cabecera (big-endian):

```
ref(4: primer byte con el bit alto) | u32 longitudDePayload | u16 0 |
f64 pos (ticks) | f64 len (ticks) | f64 third (origen de contenido)
```

cuyo *payload* `[p+8, p+8+longitud)` contiene los registros de nota de esa parte
—y también los que quedan ocultos, porque Cubase guarda el contenedor de
contenido **entero**. Una nota se **muestra** en la parte si
`third ≤ rel < third + len`, y su tick absoluto es:

```
abs = pos + (rel − third)
```

La **primera** parte de un fichero no lleva `ref` + longitud: va *inline* detrás
de su definición de clase
(`\xff\xff\xff\xff | u32 largoDelNombre | "MMidiPartEvent" | \0 | \x00\x02 …`), y
sus campos empiezan 4 bytes antes del final del patrón (`c = finDelPatrón − 4`).
El `ref` de las partes es **distinto en cada fichero** (`80 12 89 7d` en `9.cpr`,
`80 00 08 2a` en `7.cpr`, `80 00 08 02` en `HOLO`), así que se detecta por su
forma, no por su valor.

Los **nodos de pista/arrangement** comparten la cabecera pero abarcan toda la
canción (`len` = 576000 ticks) y **repiten las notas de sus partes**. Se detectan
y descartan: un candidato que contiene a otro y reproduce su conjunto de notas es
un contenedor, no una parte.

- **Verificación** (contra los SMF que Cubase exporta con los proyectos):
  `7.cpr` → **1674/1674** notas y **5/5** partes en las posiciones 0, 0, 0, 0 y
  23040 ticks, con 344/532/532/148/118 notas. `9.cpr` → **839/842** notas (las 3
  restantes son empates de redondeo a 1 tick) y 16 partes en las posiciones
  verificadas (`#3`–`#6` = 23040, `#8` = 67200, `#10` = 0, `#12` = 23040,
  `#13` = 38400…). `HOLO THEME.cpr` → 28 partes, 1491 notas, todas alineadas a
  compás y sin solapes dentro de una pista.
- **Pista destino**: el nombre de la pista es el **primer registro de nombre**
  (`u32 len | texto | \0 | EF BB BF`) dentro del payload del nodo de pista que
  contiene la parte (va justo antes del registro de clases). Se descartan los
  nombres de clase (`MMidiPartEvent`, `MPartNode`…). Si la parte no tiene nodo, se
  busca hacia atrás la aparición más cercana de un nombre del mixer, probando sus
  variantes Latin-1 (Cubase a veces guarda el nombre doblemente codificado). Esto
  reproduce **exactamente** los nombres de pista del MIDI de Cubase: `7.cpr` →
  Kontakt 7 01, Xpand!2_x64 01, Xpand!2_x64 01 (D), SINE Player 01, Xpand!2_x64
  02; `9.cpr` → Musio 01, Musio 01 (D), Musio 03,…
- **Asignación a pistas**: las partes que comparten nodo pertenecen a la **misma
  pista** (una pista puede tener varias partes), así que se asignan juntas
  (`CubaseMidiPart.ownerOffset`). Las pistas que comparten nombre (varias pistas
  MIDI al mismo VSTi, o una pista duplicada que Cubase marca con `" (D)"`) se
  emparejan **por orden** por nombre base (`assignMidiPartsToTracks`).
- **Importación**: cada parte se crea como un clip MIDI (posición/duración en
  segundos vía `ticks · 60 / (480 · BPM)`) y cada nota se añade en coordenadas
  relativas al clip (`n.startTicks − part.startTicks`) con `velocity/127`.

### Eventos de audio (Cubase 13, verificado)

Un evento de audio (`MAudioEvent`) reutiliza **la misma cabecera que una parte
MIDI**:

```
ref(4) | u32 longitudDePayload | u16 0 |
f64 pos (ticks, 480 PPQ) | f64 len (muestras) | f64 sourceOffset (muestras)
```

pero cambia la **unidad**: `pos` va en ticks y `len` / `sourceOffset` en
**frames de la fuente** al sample rate del proyecto. La comprobación externa
que lo confirma: en `RIKOS COOKING.cpr` el primer evento de
`8-Voltage-Modular(1).wav` tiene `sourceOffset + len = 2 304 000 + 1 332 244 =
3 636 244` frames ÷ 48000 = **75.755 s**, exactamente la duración del fichero en
disco (y ninguno de los dos números es múltiplo de una rejilla musical, así que
no son ticks).

El **primer** evento/pista de audio va *inline* detrás de su definición de clase
(`\xff\xff\xff\xff | u32 0x11 | "MAudioTrackEvent" | \0 | \x00\x01 …`), con los
campos también en `c = finDelPatrón − 4` — el mismo truco que las partes MIDI,
solo que el marcador de cierre es `\x00\x01` (pista) en vez de `\x00\x02`
(parte).

**Separar audio de MIDI.** Cubase da a todas las instancias de una clase la
**misma referencia de 4 bytes**, así que se agrupan los candidatos por `ref`:
un grupo es de audio si ningún miembro tiene registros de nota (`GLFX`) y algún
miembro lleva un descriptor de media (un `string8` que acaba en `.wav`/`.flac`/
`.mp3`…). Los nodos de pista de audio (`pos = 0`, `len = 576000`, igual que los
de MIDI) se reconocen porque **contienen** a otros objetos de audio y se
descartan como contenedores; el más pequeño que contiene a un evento es su
pista.

**Fichero fuente.** El payload del evento contiene o bien un descriptor de media
(`FNPath`, p. ej. `…instrumental.wav`), o bien solo el **nombre de clip** sin
extensión (`RIKO_ISA3`, `8-Voltage-Modular(1)`). El nombre se resuelve contra el
**media pool** (todos los `string8` del fichero con extensión de audio) por
nombre base. La ruta local se busca junto al `.cpr` (`Audio/`, `audio/`, la
propia carpeta), sin distinguir mayúsculas. El **gain** del evento es el `f32`
que sigue a `u32 0` justo después del nombre (siempre `1.0` en el proyecto de
referencia); solo se lee cuando ese `u32` es realmente 0, porque en un evento
largo el nombre va seguido de otros registros (un `f64` de tiempo cuyo primer
`f32` daría un gain falso de `0.48`).

**Sample rate.** `AudioSampleRate` vive en el archivo de *metadata*
(`StMedia::PAttributes`), no en el arrangement: el lector busca el nombre en todo
el fichero y lee el `f64` del registro `Float` que le sigue (48000 en los cuatro
proyectos de referencia; `44100` en el fixture).

- **Verificación** (`RIKOS COOKING.cpr`, 120 BPM, 48 kHz): **12 eventos** en
  **9 pistas** de audio de las 10 del mixer (la décima, "Audio 02", no tiene
  clips). Los 4 eventos de las pistas de render ocupan el fichero entero desde
  el compás 1 (`len = 6 338 975` frames = 132.06 s = duración exacta del WAV);
  los dos de `8-Voltage-Modular(1)` empiezan en 46080 y 88200 ticks (= 48.0 s y
  91.875 s) y cubren `48.0 → 75.755 s` y `53.75 → 75.755 s`; los seis de
  `RIKO_ISA3.flac` caen todos dentro de la duración del fichero (hasta 114.5 s).
- **Se recrea como clip de audio** con `startTicks·60/(480·BPM)`,
  `sourceOffset/48k` y `len/48k` (`setClipBounds`): el motor coloca, recorta y
  desplaza el clip exactamente como en Cubase.

### Automatización y mute/solo

Pendientes. Los carriles de automatización (`Mute-1`, `Solo-1`…) y el flag de
mute/solo por canal siguen sin descifrar (ver limitaciones).

## Matriz de cobertura

| Alcance pedido | Estado | Notas |
| --- | --- | --- |
| Contenedor RIFF/NUND | ✅ | |
| Versión de Cubase | ✅ | `PAppVersion` |
| Pistas (nombre, tipo) | ✅ | canales de mixer |
| Volumen / pan | ✅ | `AnchorValue` / `audioComponent` |
| Nivel de master (Stereo Out) | ✅ | fader del bus de salida → `masterGain` |
| Ley de paneo estéreo = balance | ✅ | 0 dB al centro para fuentes de ≥2 canales |
| Tempo / compás | ✅ | |
| Plugins (nombre + UID) | ✅ (metadatos) | no se recrean en la cadena de FX |
| Instrumento por pista (VSTi) | ✅ | `Synth Slot → Plugin`; se carga si está instalado, con su estado |
| Estado de los plugins | ✅ | `audioComponent`/`editController` → `setStateInformation` (VST3) |
| Inserts de pista (cadena de FX) | ✅ | recreados con su estado; *bypass* si el estado no aplica |
| Master (Stereo Out) inserts | ⚠️ solo registrados | el motor aún no instancia la cadena de master |
| Notas MIDI (pitch/vel/pos/duración) | ✅ | verificado 1:1 contra SMF de Cubase |
| Marcadores | ⚠️ solo nombres | falta la posición |
| Mute / solo | ❌ | no existe un flag por canal en el fichero (ver abajo) |
| Eventos de audio | ✅ | posición/recorte/gain/fuente, verificado contra duración de los WAV |
| Ficheros de audio locales | ✅ | se resuelven junto al `.cpr` (`Audio/`) |
| Automatización | ❌ | |

## Limitaciones y cabos sueltos (para la siguiente pasada)

- **Mute/solo**: no hay campo `Mute`/`Solo` por canal. Las únicas apariciones son
  identificadores de carriles de automatización (`Mute-1`, `Solo-1`) y estados
  de plugin en XML. Por ahora mute/solo se importan a `false`.
- **Instrumentos — preset**: el VSTi de cada pista se identifica (`Synth Slot →
  Plugin → Plugin Name`) y se carga con el estado que Cubase guardó
  (`audioComponent`) siempre que sea una instancia **VST3** (los estados son de
  proceso VST3; ver "Estado de los plugins"). Si el plug-in no está instalado
  (o no se ha escaneado el catálogo), la pista usa el sintetizador interno. Si el
  único candidato es un **AudioUnit** homónimo, se carga pero sin estado.
- **Efectos — estado no restaurable**: si el plug-in no acepta el estado (o el
  candidato no es VST3), el insert se deja **bypasseado** en vez de sonar con su
  preset por defecto.
- **Master inserts**: `masterAddFx` solo registra el slot en el modelo; el motor
  todavía **no instancia ni procesa** la cadena de master (no hay
  `masterFxChain_` construida), así que su estado no se aplica.
- **Lista de eventos por pista**: el nodo de pista de Cubase 12 usaba
  `00 00 02 0b 00 00 02 f0`; en Cubase 13 es **`00 00 02 0f 00 00 02 f4`**
  (precedido de 4 ceros y `EF BB BF`, y de `[u32 len][nombre]\0`). El `u32`
  que sigue es el número declarado de eventos de la pista
  (`CprReader.extractTrackNodes()`): en el proyecto de referencia, 29
  contenedores y 1549 eventos declarados. En Cubase 15 el nodo cambia
  (`MMidiPart`/`MMidiPartEvent`/`MPartNode`) y `extractTrackNodes()` devuelve 0.
- **No hay `MThd`**: los bloques `MTrk` no son un SMF estándar.
- **Audio**: `MAudioEvent` sí está en Cubase 13 (la nota anterior "ausente en 13"
  valía para otros ficheros); lo que cambia respecto al `CLIP_DEF_MAGIC`
  (`80 00 06 36`) de cpr2rpp es que en este formato el audio se reconoce por la
  forma del objeto + el descriptor de media, no por un magic del pool. Si el
  fichero fuente no está junto al `.cpr` el evento se **omite** (se avisa en el
  mensaje de estado); el motor decodifica el fichero entero en memoria, así que
  un proyecto con muchos audios largos tarda y consume RAM al importar.
- **Audio sin gain fiable**: el gain solo se lee cuando el nombre del evento va
  seguido de `u32 0`; en eventos largos se deja en `1.0` (evita el falso `0.48`).
- **Grupos / buses intermedios**: solo se importa el fader del bus de salida
  principal. Si una pista de audio va a un grupo con su propio fader, el import
  la aplana al master y ese fader se pierde (RIKOS COOKING no tiene grupos: sus
  pistas de audio van todas al `Stereo Out`).
- **Entradas de canal (trim)**: `InputGain` existe por canal, pero en los
  proyectos de referencia vale siempre `16383.5` (el centro de la escala, 0 dB),
  así que no se aplica.

## Pruebas

```bash
export PATH="$HOME/development/flutter/bin:$PATH"
cd ui
dart analyze lib test
flutter test test/cpr_reader_test.dart        # fixture sintético + .cpr real (si existe)
dart run tool/cpr_inspect.dart "/ruta/proyecto.cpr"   # volcado manual
```

El fixture sintético construye un `.cpr` mínimo en memoria (contenedor +
gramática + un canal + dos notas y un evento de control) y verifica versión,
tempo, compás, nombre, tipo, volumen, pan y la decodificación MIDI (pitch,
velocidad, posición y duración, descartando el evento de control). Cubre además
la asignación parte→pista (nombre base y reparto por orden). Un segundo fixture
construye **objetos de parte reales** (`ref | u32 | u16 0 | pos | len | third`)
y comprueba la fórmula `abs = pos + rel − third`, la ventana de contenido
(`third ≤ rel < third + len`) y el descarte del nodo de pista que repite las
notas de sus partes. Un tercer fixture monta una **pista de audio inline**
(`MAudioTrackEvent`) con dos eventos: comprueba las unidades (ticks vs frames),
la resolución del fichero por descriptor inline y por nombre del media pool, el
gain, la pista propietaria, la asignación a pista y que el sample rate se lee del
archivo de metadata (`44100`). Un cuarto escenario añade un canal de salida
(`Type` 8) en un archivo aparte (VST Mixer) y comprueba que el fader de master se
lee de ahí y que ese canal **no** se convierte en pista. El canal del fixture
puede llevar además su `Synth Slot` (con `Plugin Name`), que debe aparecer en
`CubaseTrack.instrumentName` —y quedar a `null` cuando el *slot* está vacío. El
mismo fixture puede llevar el **estado** de cada plugin (`audioComponent` /
`editController` como blobs), que debe salir en `instrumentState` /
`insertStates[i]` / `masterInsertStates[i]`, alineado con los nombres (un *slot*
sin estado queda `null`, para no romper los índices).

Los tests reales, guardados por existencia del fichero, comprueban:
`HOLO THEME.cpr` (29 pistas, 120 BPM, 4/4, 8 plugins, 28 partes, 1491 notas
alineadas a compás y sin solapes dentro de una pista) y `7.cpr` (5 partes en
0,0,0,0,23040 con 344/532/532/148/118 notas y los nombres de pista del SMF).
`test/cubase_import_real_test.dart` recorre el camino completo (reader → motor
nativo) y verifica que **cada clip llega al motor en la posición de su parte**;
con `RIKOS COOKING.cpr` (si existe, con su carpeta `Audio/`) verifica además que
los **12 clips de audio** llegan al motor con su posición, su recorte
(`offset`) y su duración exactos, que el `masterGain` del motor coincide con el
fader del *Stereo Out* del proyecto (−2.17 dB) y que los instrumentos de las
pistas de instrumento salen del fichero (`ZENOLOGY`, `Musio`, `Kontakt 7`).

La restauración de estado se comprueba leyendo de vuelta lo que el plugin
instanciado reporta (`EngineService.pluginStateBase64`, que es un *hook* de
diagnóstico): se decodifica el sobre `<VST3PluginState>` de JUCE —base64 propio
de JUCE, con `.` en el índice 0 y prefijo `"<tamaño>."`— y se compara su bloque
`<IComponent>` con el `audioComponent` de Cubase.

Además, `7.cpr` + `7/Sin título.midi` y `9.cpr` + `9/9.midi` son los pares de
referencia para validar la decodificación de notas y la asignación parte→pista
contra exportaciones reales de Cubase (nombres de pista del SMF).

## Referencias de ingeniería inversa

- `daniel-lynch/cpr2rpp` (`docs/FORMAT.md`,
  `src/cpr2rpp/{records,arrangement,mixer}.py`) — Cubase 12; el panner, la
  escala del fader y el formato de evento de audio salen de ahí.
- `schwifty00/CubaseTools`.
