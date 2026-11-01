# AI song arranger

The song arranger composes several roles at once (drums, bass, harmony, melody,
texture, FX) from the current project and a short creative brief. It is the
supported AI workflow: the engine never talks to the network, and all document
edits stay inside C++.

## Architecture

```
Flutter dialog ──► OpenAI-compatible HTTP endpoint
      │                       │
      │ project context JSON  │ arrangement plan JSON
      ▼                       ▼
stable C ABI ──► Engine::applyAiArrangement (C++20/JUCE)
                       │
                       ├── parse + validate the whole plan
                       ├── reuse or create instrument tracks
                       ├── create MIDI clips/notes
                       └── one editProject() → one undo entry
```

- The UI owns the connection settings, the brief, the HTTP client and the
  preview. It never edits the document directly.
- The engine owns the document. It exposes a read-only context JSON and an
  atomic apply call; no network code lives in C++.
- A failed or malformed plan leaves the project untouched.
- Applying a plan is one undo entry, including created tracks and clips.
- The old local clip generator is no longer exposed in the UI. Its engine/C ABI
  entry points remain for legacy compatibility, but the arranger is the
  supported AI workflow.

## Connection settings

The dialog reads the OpenAI-compatible endpoint, model, key, creativity, token
limit and request timeout from **AI settings**. The timeout is stored as
`ai.timeout_seconds` in `ui_settings.json`, defaults to 180 seconds and can be
set from 30 to 1800 seconds. It applies to both the response headers and the
streamed body, so slow local models can finish without being cut off.

**Test connection** sends a minimal probe with the values currently entered in
the form, without saving them and without project context. It verifies the URL,
model and credentials before the user spends time generating an arrangement.
The result is shown inline in the settings dialog.

## Time and pitch

All positions and lengths in a plan are **quarter-note beats** on the project
timeline, not seconds. The engine converts them with the project tempo:

```
seconds = beats × 60 / tempo_bpm
```

Pitches are MIDI note numbers 0–127 (middle C = 60). The plan parser also
accepts note names such as `C4`, `F#3` or `Bb2`, because models often choose
them. Velocities may be `0.01–1` or MIDI-style `0–127`.

The plan can start at the current playhead. The context includes the anchor in
seconds, beats and bars, and the brief asks for a number of bars from there.
Clips are still placed on the absolute project timeline.

## Project context

`cadence_engine_ai_context_json(engine, selected_track_id, anchor_seconds,
bars)` returns a compact JSON document:

- project name, tempo, meter, seconds/beats per bar;
- key, relative mode, confidence, inferred role, density and target track;
- one chord symbol per bar, plus a `chord_line` string;
- every track: id, name, type, inferred role, instrument, volume, pan,
  mute/solo and clips;
- for MIDI clips: start/length, note count, pitch range and up to 32 note
  excerpts (bounded globally to 512 notes);
- a short `guidance` string describing the output contract.

The context is read-only and bounded so it stays inside a reasonable prompt
budget even for large projects.

## Plan format

The model returns one JSON object. The parser accepts the object at the top
level or nested once under `plan`/`arrangement`, and tolerates Markdown fences
or prose around it (the Dart preview also strips them).

```json
{
  "title": "Night drive",
  "summary": "16 bars of organic house",
  "tracks": [
    {
      "track_id": 7,
      "name": "Drums",
      "role": "drums",
      "instrument": "drums",
      "color": 2,
      "volume_db": -3,
      "pan": 0,
      "clips": [
        {
          "start_beat": 0,
          "length_beats": 16,
          "notes": [
            {
              "pitch": 36,
              "start_beat": 0,
              "length_beats": 0.25,
              "velocity": 0.95
            }
          ]
        }
      ]
    }
  ]
}
```

### Tracks

- `track_id` (> 0) reuses an existing **instrument** track. If the id is not
  found, or is omitted/-1, a new instrument track is created.
- A new track's name is made unique. If the model omits the name, a default is
  used.
- `role` and `instrument` drive defaults. If `instrument` is empty, the role
  chooses a sound.
- `volume_db` and `pan` are optional. They are applied to reused tracks too.
- Audio tracks are never targeted; a plan that points at one is rejected.
- `color` is clamped to the project palette.

### Instrument tokens

| Token | Destination |
| --- | --- |
| `drums` | `EZDrums` |
| `piano`, `internal`, `none` | engine's internal piano voice |
| `bass` | `EZSynth` — Bajo Gordo |
| `sub` | `EZSynth` — Bajo Sub |
| `acid` | `EZSynth` — Bajo Ácido |
| `keys`, `rhodes`, `electric_piano` | `EZSynth` — Teclas Eléctricas |
| `pad` | `EZSynth` — Pad Etéreo |
| `lead` | `EZSynth` — Lead Sincero |
| `pluck`, `arp` | `EZSynth` — Pluck Cristal |
| `organ` | `EZSynth` — Órgano |
| `strings` | `EZSynth` — Cuerdas Analógicas |
| `orchestra` | `EZOrquestra` |
| `bell` | `EZSynth` — Campanas |
| `riser`, `fx` | `EZSynth` — FX Riser |
| `gm:<0-127>` | `EZInstruments` with that General MIDI program |

Unknown tokens fall back to `EZSynth` Init. New synth tracks get a complete
preset state so they sound right without opening an editor.

Drums use General MIDI notes (36 kick, 38 snare, 42 closed hat, 46 open hat,
49 crash, 51 ride, 39 clap, toms 41/43/45/47).

### Clips and notes

- A clip must have a `notes` array; `[]` creates an empty MIDI clip.
- A clip length shorter than its last note is expanded to contain it.
- Positions and lengths are clamped to sane ranges.
- The parser enforces limits: 16 tracks, 16 clips per track, 4,000 notes per
  clip, 12,000 notes total, 4,096 beats.

## Apply semantics

`cadence_engine_apply_ai_arrangement(engine, utf8_json)`:

1. Parses and validates the entire document.
2. Reuses tracks by id, or by case-insensitive name for a missing id, or
   creates a new instrument track.
3. Adds clips and notes, assigning fresh clip ids.
4. Applies volume/pan when present.
5. Commits once through `editProject()`, so undo restores the whole batch.

If any step fails, the edit is aborted and the project is unchanged. The last
error is available through `cadence_engine_last_error`.

On success, `cadence_engine_ai_arrangement_result_json(engine)` returns a
summary:

```json
{
  "ok": true,
  "title": "Night drive",
  "summary": "16 bars of organic house",
  "created_tracks": 2,
  "tracks": [
    {
      "track_id": 8,
      "name": "Drums",
      "created": true,
      "role": "drums",
      "instrument": "drums",
      "clip_ids": [12]
    }
  ],
  "clips": [12, 13, 14],
  "note_count": 384,
  "first_track_id": 8,
  "first_clip_id": 12
}
```

The UI uses the first track/clip to select the result after applying.

## Refinement

The dialog can send the previous plan back with a refinement request. The
second pass receives the same project context and asks for a complete
replacement plan, not a diff. This keeps the atomic apply path unchanged and
lets the model improve groove, voice leading, structure or density.

## Settings and security

- Endpoint URL, model, temperature and token limit are stored in
  `~/Music/Cadence/ui_settings.json` under an `ai` object.
- The API key is stored separately in
  `~/Music/Cadence/ai_credentials.json` with owner-only permissions (`0600`).
  It is never written to project files or logs.
- Environment variables can override the connection for local testing:
  `EZ_AI_BASE_URL`, `EZ_AI_MODEL`, `EZ_AI_API_KEY`.
- Local OpenAI-compatible servers (LM Studio, Ollama, etc.) can run without a
  key; the UI detects `localhost`/`127.0.0.1`/`::1`.
- Requests ask for `response_format: json_object`; if a compatible server
  rejects it, the client retries once without that field.

## Deliberate scope

- No send/bus routing changes: the engine has inserts, not sends/buses.
- No destructive rewrite of existing clips. The model adds material; the user
  can undo or discard before applying.
- No background agent loop or arbitrary tool calls. One plan per request, one
  atomic apply.
- No reference-project code or assets. The implementation is independent.

## Testing

- `cadence-cli arranger-test` checks context JSON, multi-track apply, reuse of
  an existing track, one-step undo, and rejection of an empty plan.
- `ui/test/song_plan_test.dart` covers wrappers, fences, note names, velocity
  normalization and invalid plans.
- `ui/test/ai_settings_test.dart` verifies key isolation and `0600`
  permissions.
- `ui/test/llm_client_test.dart` runs a local HTTP server and checks the
  request shape, bearer header, JSON-mode fallback and that an auth error does
  not echo the key.
- `ui/test/song_arranger_dialog_test.dart` renders the dialog and opens AI
  settings without making a network request.
