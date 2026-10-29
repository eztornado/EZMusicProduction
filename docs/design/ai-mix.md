# AI mix and mastering

Cadence can analyse the whole project offline and propose concrete mix moves:
faders, pan, corrective EQ, compression and master limiting. The goal is a
second engineer that hears the render, explains what is wrong and offers fixes
the user can apply one by one or all at once.

Analysis and decisions run entirely inside the engine. There is no network
client, no language model and no external service: the prompt is a small,
deterministic parser and every number comes from the project's own audio.

## User-facing flow

- **Toolbar → Mix assistant** opens the assistant.
- **Mixer header → ✨** opens the same assistant from the console.
- The prompt is optional. It can name targets (`-14 LUFS`, `-1 dBTP`), a genre
  or profile (`pop`, `rock`, `electronic`, `acoustic`, `orchestral`, `hip hop`)
  and constraints (`master only`, `gentle`, `no EQ`, `no dynamics`).
- `Analyze mix` starts an offline render on a worker thread. The project keeps
  playing and is never modified during analysis.
- The report shows the measured loudness, true peak, crest factor, per-channel
  role and level, masking pairs and a severity-ordered issue list.
- Each issue can be applied on its own, or `Apply all fixes` applies the whole
  plan. Either way the change is a single undo entry. After applying a single
  issue the report stays open for the next one, but `Apply all` is disabled so
  no move is applied twice.

## Architecture

```
UI dialog ──JSON── C ABI ── Engine
                              ├─ ProjectAudioSource (offline render)
                              │    ├─ per-track capture after fader/pan
                              │    └─ master capture after master FX/gain
                              ├─ mixai::DetailAnalyzer (worker thread)
                              ├─ role inference, targets, masking, issues
                              └─ moves → fader/pan/EQ/EZ Compressor/EZ Limiter
```

The offline render reuses the normal playback graph rather than a second mixer.
`ProjectAudioSource` exposes an optional `AnalysisSink`; while an analysis job
is active it receives one block per audible track (post fader and pan) and one
master block (post master inserts and master gain). Tracks that are muted or
outside their clip range contribute silence.

The worker owns the report. The UI only polls a small state value:

| State | Meaning |
| --- | --- |
| `0` | idle |
| `1` | running |
| `2` | ready |
| `3` | failed |

The full report and apply result are JSON strings returned through the C ABI,
which keeps the binding surface small and lets the report grow without new
getters. The report timeline is decimated to at most 240 frames before it
crosses the ABI.

## Measurements

`mixai::DetailAnalyzer` is incremental and processes the render block by block:

- **Level**: sample peak, RMS, integrated loudness and short-term maximum using
  the BS.1770 K-weighting filter and gating, plus true peak with 4×
  oversampling.
- **Crest factor**: difference between peak and integrated loudness, used to
  distinguish a dynamic mix from a squashed one.
- **Spectrum**: 5 broad bands (low, low-mid, mid, high-mid, high), 31 third-octave
  bands, 10 octave bands and a normalised octave shape for tone comparison.
- **Stereo**: width, side energy and low-band correlation, which exposes mono
  compatibility problems and an over-wide low end.
- **Timeline**: short frames of band energy, loudness and stereo values for the
  report and for future automation-oriented moves.
- **Clipping**: count of samples at or above full scale.

## Roles and targets

Track names, instrument names and hints are matched against a small role
vocabulary: kick, drums, percussion, bass, keys, guitar, pad, lead, vocal,
strings, brass and FX. Each role has a balance offset, an optional high-pass
frequency, a preferred stereo spread and a spectral ownership curve.

The default target is `-14 LUFS` and `-1 dBTP` with a pop profile. Profiles
shape the expected tonal balance (for example electronic has more low and high
end, acoustic has less high-mid). The prompt parser resolves targets and
constraints, and the resolved values are echoed in `prompt_note` and the
`targets` object of the report.

`master only` restricts the plan to the master bus. `gentle` scales the
proposed gains and dynamics. `no EQ` and `no dynamics` remove those move types
from the plan instead of merely hiding them.

## Issues

`findIssues` compares the master and each track against the targets and returns
severity-ordered issues with ready-to-apply moves:

- clipping and true-peak overs,
- loudness too hot or too quiet,
- phase correlation problems and an excessively narrow image,
- an over-wide or mono-incompatible low end,
- tonal balance (for example too much low-mid or a dull top),
- over-compression (crest factor below the profile's expectation),
- individual tracks that are much hotter than their role balance,
- rumble on non-bass sources,
- masking between channel pairs, with the quieter/less important side chosen
  for a cut.

Every issue carries an `id`, `severity` (`high`, `medium`, `low`), `kind`,
optional `track_id`, a human-readable message and a `moves` array. Issues
without an automatic fix are still reported with an empty move list.

Masking detection compares third-octave band energy between tracks, weighted by
role ownership and level difference. The pair report (`a`, `b`, `band_hz`,
`overlap`, `level_diff_db`, `score`, `cut`) is included so the UI or a future
automation lane can explain the decision.

## Moves and devices

The engine understands a deliberately small move vocabulary:

| Move | Parameters | Applied to |
| --- | --- | --- |
| `gain` | `delta_db` or `db` | track fader / master gain |
| `pan` | `pan` (−1…1) | track |
| `eq` | `kind`, `freq_hz`, `gain_db`, `q` | EZ EQ Pro band |
| `compressor` | `threshold_db`, `ratio`, `attack_ms`, `release_ms`, `makeup_db`, `knee_db` | EZ Compressor |
| `limiter` | `ceiling_db`, `input_gain_db` | EZ Limiter |

Moves are inserted into the track's normal insert chain. Built-ins are found by
their stable identifier, not by the user-visible name, so renamed instances are
still reused. A missing built-in is created; an existing one is reused.

The apply path is transactional from the user's point of view:

1. fader, pan and insert-chain changes are made in one project edit;
2. plugin parameters and EQ band placement run on the message thread;
3. the resulting device states are captured back into the project snapshot so a
   saved song restores the same sound;
4. the whole operation produces one undo entry.

Undo restores the project snapshot, including the insert chain and fader/pan
values. Because parameter state is captured into the snapshot after the edit,
undo/redo of plugin parameters is intentionally partial: redo re-creates the
structure but does not replay every parameter automation step.

## Prompt interpretation

The prompt is matched case-insensitively, with English and Spanish terms:

- loudness: a number followed by `lufs` / `lu` / `lkfs`;
- ceiling: a number followed by `dbtp` / `true peak`;
- profile: genre names and synonyms (`pop`, `rock`, `electronic`, `edm`,
  `hip hop`, `acoustic`, `orchestral`, `clásico`, …);
- scope: `master only`, `solo máster`, `master bus`;
- strength: `gentle`, `suave`, `light`;
- exclusions: `no eq`, `sin eq`, `no dynamics`, `sin dinámica`.

Anything the parser does not recognise is ignored. When the prompt names a
loudness target or ceiling it overrides the values passed to the analysis call;
otherwise the start parameters (or the engine defaults) are used. This keeps the
prompt useful without making the result unpredictable.

## C ABI

The mix API mirrors the rest of the engine:

```c
int  cadence_engine_start_mix_analysis(CadenceEngine*, const char* prompt,
                                       double target_lufs, double true_peak_db,
                                       int profile, int master_only,
                                       int max_issues);
int  cadence_engine_mix_analysis_state(CadenceEngine*);
void cadence_engine_cancel_mix_analysis(CadenceEngine*);
void cadence_engine_clear_mix_analysis(CadenceEngine*);
const char* cadence_engine_mix_analysis_json(CadenceEngine*);
int  cadence_engine_apply_mix_plan(CadenceEngine*);
int  cadence_engine_apply_mix_issue(CadenceEngine*, const char* issue_id);
int  cadence_engine_apply_mix_moves(CadenceEngine*, const char* moves_json);
const char* cadence_engine_mix_apply_json(CadenceEngine*);
```

`start` returns `1` when the job was queued and `0` when one is already running
or the project is empty. `apply_*` return `0` on success. The apply result JSON
contains `ok`, an optional `error`, and the number of applied moves/devices.

## Testing

`cadence_cli mix-test` builds a small hot project, runs the analysis
synchronously, checks the report shape and applies the plan. It verifies that:

- the report has master stats, channels and at least one issue,
- the plan creates the expected master inserts,
- the applied plan is exactly one undo entry,
- undo removes the inserted devices.

The Dart binding test exercises the same flow through FFI without touching the
UI, so the state polling and JSON parsing can be tested on CI.

## Limitations

- The engine has insert chains but no send/bus routing; the assistant therefore
  works on tracks and the master only. Reference sends are out of scope.
- Analysis is offline and single-shot. It does not follow live changes while
  the dialog is open; press `Analyze again` after editing.
- The role vocabulary is name-driven; unusual track names fall back to a
  neutral `other` role.
- Plugin parameter undo is partial, as described above.
- The assistant proposes conservative, explainable moves. It is not a
  generative mastering chain and does not attempt to clone a reference track.
