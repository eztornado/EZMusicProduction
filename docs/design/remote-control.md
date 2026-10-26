# Control remoto por LAN

Este documento describe el servidor de control remoto del motor y el protocolo
que usará la futura app compañera (estilo Mixcraft Remote): transporte,
timeline, mezclador, efectos y automatización desde otro dispositivo de la red
local.

El servidor vive en el motor C++ (`cadence::RemoteServer`) y se expone a la UI
por la C ABI (`cadence_engine_remote_*`). La app remota será un proyecto aparte;
este documento es su contrato.

## 1. Objetivos y alcance

Incluido en v1:

- Transporte: play, pause, stop, seek, record, metrónomo, tempo y compás.
- Timeline: mover, borrar, cortar y ajustar clips; crear y borrar pistas.
- Mezclador: volumen, pan, mute, solo, armado, entrada, nombre y color.
- Efectos e instrumentos: listar plugins, leer parámetros y cambiarlos.
- Automatización: curvas de volumen/pan/sustain y curvas de parámetros.
- Estado completo del proyecto y eventos push de proyecto, transporte y
  medidores.

Fuera de alcance (por ahora):

- Audio en streaming hacia/desde el dispositivo remoto.
- Edición de notas MIDI una a una (el estado las incluye, pero no hay comandos
  de edición de notas todavía).
- Emparejamiento en la nube, TLS, cuentas de usuario o control fuera de la LAN.
- Varios proyectos o varias ventanas del motor a la vez.

## 2. Arquitectura

```
┌──────────────────────────────┐        ws://<ip>:47823        ┌─────────────────────┐
│ App compañera (futura)       │  ───────────────────────────▶ │ Motor (C++)          │
│ Flutter / Swift / web        │  ◀─────────────────────────── │ cadence::RemoteServer│
└──────────────────────────────┘   JSON + eventos push          └──────────┬──────────┘
                                                                          │ cola de comandos
                                                                          ▼
                                                               ┌─────────────────────┐
                                                               │ Hilo de mensajes     │
                                                               │ (JUCE / Flutter)     │
                                                               │ Engine + Project     │
                                                               └─────────────────────┘
```

- `RemoteServer` abre un `StreamingSocket` en `0.0.0.0` (IPv4) y acepta
  conexiones TCP. El handshake HTTP y el framing WebSocket están implementados
  a mano (RFC 6455); JUCE 8 no trae servidor WebSocket.
- Cada cliente tiene su propio hilo de lectura/escritura y una cola de salida
  acotada. Un hilo de tick (20 ms) genera los eventos push.
- Los comandos que **mutan** el documento no se ejecutan en el hilo del
  cliente: se encolan y se ejecutan en el hilo de mensajes del motor. Así el
  documento, los plugins y la UI no se tocan desde dos hilos a la vez.
- La cola se vacía por dos vías complementarias:
  1. `juce::MessageManager::callAsync` (cuando el host bombea la cola de JUCE).
  2. `cadence_engine_pump_remote()`, llamado desde el ticker de Flutter. En
     macOS el host no siempre bombea `callAsync`, así que esta vía es la que
     garantiza que los comandos se ejecuten.
- `hello`, `ping`, `state.get`, `transport.get` y `subscribe` son de solo
  lectura y se responden en el hilo del cliente.

## 3. Descubrimiento y conexión

### 3.1 HTTP de descubrimiento

Un `GET` normal al puerto del servidor devuelve un JSON pequeño, sin datos del
proyecto y sin exigir el código:

```http
GET / HTTP/1.1
Host: 192.168.1.20:47823
```

```json
{
  "app": "EZMusicProduction",
  "protocol": 1,
  "transport": "websocket+json",
  "port": 47823,
  "requiresToken": true
}
```

También responde `OPTIONS` con `204` y cabeceras CORS, para que una app web
pueda hacer el descubrimiento.

### 3.2 WebSocket

La conexión de control es un WebSocket estándar sobre `ws://` (sin TLS):

```
ws://192.168.1.20:47823/
```

- Solo se aceptan frames de texto (opcode `0x1`) con JSON UTF-8.
- Los frames de cliente deben ir enmascarados (RFC 6455 §5.1). Se soportan
  frames fragmentados, ping/pong y close.
- Tamaño máximo de mensaje: 2 MiB.
- Si un cliente no se autentica en 5 intentos, se cierra la conexión.

## 4. Autenticación y emparejamiento

El servidor genera un **código de 6 dígitos** al arrancar (o usa el que le pase
la UI). Se muestra en Preferencias → Control remoto y en el diálogo rápido de
la barra superior (icono de Wi-Fi, junto al engranaje).

El primer mensaje debe ser `hello` con el código:

```json
{"v":1,"id":1,"cmd":"hello","token":"123456","client":{"name":"iPhone de Ana"}}
```

Respuesta correcta:

```json
{
  "v": 1,
  "id": 1,
  "ok": true,
  "result": {
    "protocol": 1,
    "clientId": 3,
    "server": {
      "app": "EZMusicProduction",
      "port": 47823,
      "requiresToken": true,
      "protocol": 1,
      "projectName": "Mi canción",
      "capabilities": ["transport", "timeline", "mixer", "fx", "automation", "meters", "plugins"]
    },
    "state": { "...": "ver §7" },
    "transport": { "...": "ver §8.2" }
  }
}
```

Código incorrecto:

```json
{"v":1,"id":1,"ok":false,"error":{"code":"auth_failed","message":"Invalid pairing code"}}
```

Cualquier comando que no sea `hello` o `ping` antes de autenticarse devuelve
`auth_required`.

> **Seguridad:** el código es la única barrera. Cualquiera en la misma red
> local que lo conozca puede controlar la DAW. El servidor está apagado por
> defecto, escucha solo en la LAN y no se debe exponer a Internet. La UI avisa
> de esto y permite generar un código nuevo en cualquier momento.

## 5. Envoltorio de mensajes

### 5.1 Petición

```json
{"v":1,"id":42,"cmd":"mixer.setTrack","trackId":3,"volume":0.7}
```

- `v`: versión del protocolo. Actualmente `1`.
- `id`: entero elegido por el cliente. Se devuelve tal cual; permite
  peticiones concurrentes.
- `cmd`: nombre del comando.
- El resto de campos depende del comando.

### 5.2 Respuesta correcta

```json
{"v":1,"id":42,"ok":true,"result":{"revision":17,"trackId":3}}
```

### 5.3 Error

```json
{"v":1,"id":42,"ok":false,"error":{"code":"not_found","message":"track not found: 3"}}
```

Códigos de error estables:

| Código | Significado |
| --- | --- |
| `bad_json` | El frame no es JSON válido. |
| `bad_request` | Falta `cmd` o el cuerpo no tiene la forma esperada. |
| `auth_failed` | Código de emparejamiento incorrecto. |
| `auth_required` | Se intentó un comando antes de `hello`. |
| `invalid_argument` | Un campo tiene un valor fuera de rango. |
| `not_found` | Pista, clip, slot o parámetro inexistente. |
| `record_failed` | No se pudo iniciar la grabación. |
| `audio_device` | No se pudo abrir el dispositivo de audio. |
| `unknown_command` | Comando no implementado. |

### 5.4 Eventos push

El servidor envía mensajes sin `id`:

```json
{"v":1,"event":"transport","data":{"playing":true,"position":12.34}}
```

| Evento | Cuándo | Contenido |
| --- | --- | --- |
| `project` | Cambia la revisión del documento (edición, undo, redo, carga). Máximo ~8 Hz. | Estado completo (§7). |
| `transport` | 10 Hz mientras reproduce o graba; 0,5 Hz en reposo; también al cambiar play/record/posición. | Estado de transporte (§8.2). |
| `meters` | 20 Hz (configurable con `subscribe`). | Picos por pista y master (§8.3). |

Los medidores remotos usan acumuladores independientes de los locales: leerlos
no roba los picos que muestra el mezclador de la DAW.

## 6. Catálogo de comandos

Todos los comandos que mutan el documento devuelven `revision` (la nueva
revisión del proyecto). Los comandos de solo lectura se ejecutan en el hilo del
cliente; el resto, en el hilo de mensajes.

### 6.1 Conexión y estado

| Comando | Campos | Resultado |
| --- | --- | --- |
| `hello` | `token`, `client.name` | Bienvenida + estado + transporte. |
| `ping` | — | `{pong:true, time:<ms>}`. |
| `state.get` | — | `{state, transport}`. |
| `transport.get` | — | Estado de transporte. |
| `subscribe` | `meters` (bool), `metersHz` (1..30) | Preferencias efectivas del stream. |

### 6.2 Transporte

| Comando | Campos | Resultado |
| --- | --- | --- |
| `transport.play` | — | `{playing:true}`. Inicializa el audio si hace falta. |
| `transport.pause` | — | `{playing:false}`. |
| `transport.stop` | — | `{playing:false, position:0}`. |
| `transport.seek` | `seconds` (o `position`) | `{position}`. |
| `transport.record.start` | — | `{recording:true, recordStart}`. |
| `transport.record.stop` | — | `{recording:false, position}`. |
| `transport.metronome` | `enabled` (bool) | `{metronome}`. |
| `transport.tempo` | `bpm` | `{tempoBpm, revision}`. |
| `transport.timeSig` | `numerator`, `denominator` | `{numerator, denominator, revision}`. |

### 6.3 Proyecto

| Comando | Campos | Resultado |
| --- | --- | --- |
| `project.undo` | — | `{changed, revision}`. |
| `project.redo` | — | `{changed, revision}`. |

### 6.4 Mezclador

| Comando | Campos | Resultado |
| --- | --- | --- |
| `mixer.setTrack` | `trackId` + cualquiera de `volume`, `pan`, `mute`, `solo`, `armed`, `inputChannel`, `name`, `colorIndex`; opcional `undo` | `{trackId, revision}`. |
| `mixer.setMaster` | `gain` (0..4), opcional `undo` | `{gain, revision}`. |

`volume` es ganancia lineal (0..2), `pan` va de -1 a +1.
`undo:false` publica el cambio pero no crea una entrada de undo: úsalo en
arrastres continuos de fader/pan para no inundar el historial. Al soltar, envía
un último comando con `undo:true` si quieres que el cambio sea deshacible.

### 6.5 Timeline

| Comando | Campos | Resultado |
| --- | --- | --- |
| `track.add` | `name`, `type` (`"audio"`/`"instrument"` o `0`/`1`), `colorIndex` | `{trackId, revision}`. |
| `track.remove` | `trackId` | `{revision}`. |
| `clip.move` | `clipId`, `trackId`, `start` | `{revision}`. |
| `clip.remove` | `clipId` | `{revision}`. |
| `clip.split` | `clipId`, `seconds` | `{revision}`. |
| `clip.bounds` | `clipId`, `start`, `offset`, `length` | `{revision}`. |
| `clip.fades` | `clipId`, `fadeIn`, `fadeOut` | `{revision}`. |
| `clip.gain` | `clipId`, `gain` | `{revision}`. |

Los tiempos y duraciones están en **segundos** de timeline. `offset` es el
desplazamiento dentro del material fuente del clip.

### 6.6 Plugins y efectos

| Comando | Campos | Resultado |
| --- | --- | --- |
| `plugins.list` | — | Lista `[{index, name, isInstrument, format}]`. |
| `fx.params` | `trackId`, `slotKind`, `slotIndex` | `{slotKind, slotIndex, params:[{index, name, value, text}]}`. |
| `fx.setParam` | `trackId`, `slotKind`, `slotIndex`, `paramIndex`, `value` (0..1) | `{value, text}`. |
| `fx.setBypass` | `trackId`, `slotIndex`, `bypass` | `{revision}`. |
| `fx.remove` | `trackId`, `slotIndex` | `{revision}`. |
| `fx.add` | `trackId`, `pluginIndex` | `{revision}`. |
| `fx.master.add` | `pluginIndex` | `{revision}`. |
| `fx.master.remove` | `slotIndex` | `{revision}`. |
| `fx.master.setBypass` | `slotIndex`, `bypass` | `{revision}`. |

`slotKind` distingue la cadena de inserts del instrumento:

- `0` = FX insert de la pista.
- `1` = instrumento de la pista.

Los valores de parámetro son **normalizados** (0..1), no unidades del plugin.
`text` es la representación legible que muestra el propio plugin (por ejemplo
`3.5 dB`). `plugins.list` usa el mismo índice que `fx.add`/`fx.master.add`.

`fx.setParam` no crea entrada de undo: es un ajuste continuo pensado para
perillas y sliders remotos.

### 6.7 Automatización

| Comando | Campos | Resultado |
| --- | --- | --- |
| `automation.set` | `trackId`, `lane`, `points:[{t,v}]` | `{revision}`. |
| `automation.param.set` | `trackId`, `slotKind`, `slotIndex`, `paramIndex`, `name`, `points:[{t,v}]` | `{revision}`. |
| `automation.param.remove` | `trackId`, `laneIndex` | `{revision}`. |
| `automation.writeVolume` | `trackId`, `time`, `value` | `{revision}`. |

- `lane`: `0` = volumen, `1` = pan, `2` = sustain (CC64).
- `points` reemplaza la curva completa. `t` en segundos, `v` normalizado
  0..1. También se aceptan los alias `time`/`value`.
- `automation.param.set` crea o reemplaza la curva de un parámetro concreto de
  un plugin. `laneIndex` para borrarla es el índice que aparece en
  `state.tracks[].automation.params[].index`.
- `automation.writeVolume` inserta o reemplaza **un** punto en la curva de
  volumen; es el modo escritura (botón W).

## 7. Estado del proyecto (`state`)

`state.get`, la bienvenida y el evento `project` devuelven la misma estructura:

```json
{
  "revision": 17,
  "canUndo": true,
  "canRedo": false,
  "name": "Mi canción",
  "sampleRate": 48000.0,
  "tempoBpm": 120.0,
  "timeSigNum": 4,
  "timeSigDen": 4,
  "duration": 184.5,
  "masterGain": 1.0,
  "inputQuantize": 0.0,
  "masterFx": [{"index": 0, "name": "Pro-Q 3", "bypass": false}],
  "tracks": []
}
```

Cada pista:

```json
{
  "id": 3,
  "type": "instrument",
  "name": "Bajo",
  "colorIndex": 2,
  "volume": 0.8,
  "pan": -0.1,
  "mute": false,
  "solo": false,
  "armed": true,
  "inputChannel": 0,
  "instrument": "Analog Lab V",
  "midiFx": "",
  "fx": [{"index": 0, "name": "Compressor", "bypass": false}],
  "automation": {
    "volume": [{"t": 0.0, "v": 0.8}],
    "pan": [],
    "sustain": [],
    "params": [
      {
        "index": 0,
        "slotKind": 0,
        "slotIndex": 0,
        "paramIndex": 12,
        "name": "Cutoff",
        "points": [{"t": 4.0, "v": 0.35}]
      }
    ]
  },
  "clips": [
    {
      "id": 9,
      "kind": "midi",
      "start": 8.0,
      "length": 16.0,
      "offset": 0.0,
      "gain": 1.0,
      "fadeIn": 0.0,
      "fadeOut": 0.0,
      "muted": false,
      "notes": [
        {"pitch": 36, "velocity": 0.8, "start": 0.0, "length": 0.25}
      ]
    }
  ]
}
```

- `type`: `"audio"` o `"instrument"`.
- `instrument`/`midiFx`: nombre del plugin cargado, o cadena vacía.
- `kind`: `"audio"` o `"midi"`. Los clips MIDI incluyen `notes`; los de audio,
  no.
- `notes[].start` es relativo al inicio del clip. `origStart` solo aparece si
  la nota se movió de su posición original (para restaurar quantize).
- `revision` es monótona: cualquier cambio del documento (edición, undo, redo,
  carga) la incrementa. Es la forma barata de detectar desincronización.

## 8. Transporte y medidores

### 8.1 `transport.get`

```json
{
  "playing": true,
  "recording": false,
  "position": 12.34,
  "duration": 184.5,
  "recordStart": 0.0,
  "metronome": true,
  "tempoBpm": 120.0,
  "timeSigNum": 4,
  "timeSigDen": 4,
  "revision": 17
}
```

### 8.2 Evento `transport`

Mismo objeto que `transport.get`, dentro de `data`.

### 8.3 Evento `meters`

```json
{
  "tracks": [{"id": 3, "peak": 0.42}],
  "master": [0.51, 0.49]
}
```

`peak` es lineal 0..1. El master tiene dos canales (izquierdo/derecho). Los
picos se leen y se reinician de forma atómica; el servidor remoto usa
acumuladores separados de los del mezclador local.

## 9. Integración en la app de escritorio

- **C ABI** (`engine/include/cadence/cadence_engine.h`):
  `cadence_engine_remote_start/stop/running/port/client_count/token/last_error`,
  `cadence_engine_pump_remote` y `cadence_engine_project_revision`.
- **Dart FFI** (`ui/lib/engine/cadence_bindings.dart`): mismos métodos en
  `CadenceEngine` (`startRemoteServer`, `remoteServerPort`, …).
- **Servicio** (`ui/lib/engine/engine_service.dart`): expone el estado y llama
  a `pumpRemoteCommands()` desde el ticker de la app. Si la revisión cambió,
  refresca el snapshot de la UI.
- **Ajustes** (`ui/lib/remote/remote_settings.dart`): `enabled`, `port` y
  `token` se guardan en `~/Music/Cadence/ui_settings.json` (sección `remote`).
  El servidor está apagado por defecto.
- **Panel de control remoto** (`ui/lib/ui/remote_control_panel.dart`):
  interruptor, puerto, código con botón de copiar, dirección `ws://…`, clientes
  conectados, botón de código nuevo y aviso de seguridad. Se reutiliza en
  Preferencias y en el diálogo rápido de la barra superior
  (`ui/lib/ui/remote_dialog.dart`), accesible con el icono de Wi-Fi.
- **Arranque** (`ui/lib/main.dart`): solo la ventana principal arranca el
  servidor si estaba activado; las sub-ventanas comparten el mismo motor.

## 10. Añadir un comando nuevo

1. Implementa la operación en `Engine` (preferiblemente como una edición
   undoable y publicada con `editProject`).
2. Añade la rama en `RemoteServer::executeCommand` con el mismo estilo:
   leer campos, validar, llamar al motor, responder `revisionResult()` o
   `fail(...)`.
3. Si el comando es de solo lectura y no toca el motor, puede ir en
   `Client::handleText` (inline).
4. Documenta el comando en la tabla de §6 y añade una prueba en
   `ui/test/remote_control_test.dart` o en el arnés C++.

## 11. Pruebas

- `ui/test/remote_control_test.dart`: abre un WebSocket real contra el motor,
  autentica, consulta estado, mueve un fader, cambia tempo/metrónomo y comprueba
  errores estructurados. Bombea la cola con `pumpRemoteCommands()` igual que la
  app.
- `ui/test/remote_settings_test.dart`: comprueba la persistencia de
  `enabled`/`port`/`token` sin pisar la sección `locale` del mismo fichero.
- Para una prueba manual sin Flutter, cualquier cliente WebSocket sirve: hay
  que enviar el `hello` con el código que muestra Preferencias y mantener el
  motor bombeando comandos (la app lo hace en su ticker).

## 12. Mapa de ficheros

| Fichero | Papel |
| --- | --- |
| `engine/src/remote_server.h` | Declaración de `RemoteServer`. |
| `engine/src/remote_server.cpp` | HTTP, WebSocket, autenticación, comandos, eventos. |
| `engine/src/engine.cpp` | Ciclo de vida, revisión, mezclador por lotes, parámetros de plugin, medidores remotos. |
| `engine/include/cadence/engine.hpp` | API pública del motor. |
| `engine/include/cadence/cadence_engine.h` | C ABI para Flutter. |
| `engine/src/c_api.cpp` | Implementación de la C ABI. |
| `ui/lib/engine/cadence_bindings.dart` | FFI de Dart. |
| `ui/lib/engine/engine_service.dart` | Estado de UI y bombeo de comandos. |
| `ui/lib/remote/remote_settings.dart` | Persistencia de ajustes. |
| `ui/lib/ui/preferences_dialog.dart` | UI de emparejamiento. |
| `ui/lib/main.dart` | Arranque del servidor. |
