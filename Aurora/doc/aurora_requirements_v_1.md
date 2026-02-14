# AURORA: Specification 1.0 (Language v1.0)

**One-liner:** AURORA is a deterministic, code-native modular synthesis + sequencing runtime that renders **audio stems** (WAV/FLAC) and optional **MIDI**, optimized for long-form generative ritual music and general production.

---

## 1) Product goals

### 1.1 Primary goals

1. **First-principles synthesis**: oscillators (sine/saw/tri/pulse) + optional wavetables → filters → modulation/envelopes → effects → mixing/busing.
2. **Code-first workflow**: live-coded feel with an explicit graph and **offline render** as a first-class output.
3. **Stems as default artifact**: every logical voice or bus can render to a separate stem.
4. **Optional MIDI output**: especially for sequenced parts (basslines, drums, chord triggers).
5. **External sample inclusion (secondary)**: reference external samples (WAV/FLAC) for one-shots/loops/slices while keeping the system **principles-first** (samples are source material, not the core identity).
6. **Ritual engine readiness**: built-in abstractions for SDT-like constraints: pressure ramps, silence windows, dialect/register overlays, sparse event density, long-form evolution.

### 1.2 Non-goals (v0.x / v1.0)

- Not a full DAW.
- Not a GUI-first editor (CLI-first).
- Not multitrack audio recording/comping/piano-roll editing.
- Not >2-channel (surround/ambisonics) audio.

---

## 2) Core design principles

1. **Determinism by default**: same `.au` + seed + SR + engine version ⇒ matching output (within defined determinism scope).
2. **Time is a first-class type**: sample-accurate scheduling even when authored in musical time.
3. **Separation of concerns**:
   - **Language/Score** (what happens when)
   - **Synth Graph** (how it sounds)
   - **Renderer** (how it becomes stems/MIDI)
4. **Constraint-first ergonomics**: make “less / slower / sparser / silence / pressure rise” easy.

---

## 3) Runtime + platform requirements

### 3.1 Implementation language + performance

- Engine + renderer implemented in **C++20/23**.
- Offline rendering designed for **high throughput** (vectorization and multithreading where safe) with deterministic results.

### 3.2 Supported platforms

- Required targets: **native Linux** and **WSL (Ubuntu/Debian)**.
- macOS: best-effort.
- Windows native: future.

### 3.3 CLI (required)

- CLI-only tool invoked with an `.au` file to generate artifacts.
- Canonical usage:
  - `aurora render <file.au> --seed 123 --sr 48000 --out ./renders/`
- Optional helpers:
  - `aurora play <file.au> --preview 60s`
  - `aurora graph <file.au> --out graph.svg`
- Exit codes:
  - `0` success
  - non-zero on parse/validation/render/I/O failure (with human-readable diagnostics)

---

## 4) Rendering + artifact outputs

### 4.1 Offline renderer (first-class)

- Output formats:
  - WAV 24-bit / 32-float
  - Optional FLAC
- Sample rates: 44.1k / 48k / 96k
- **Channel outputs:** stems and buses may be **mono or stereo** only (1 or 2 channels).

### 4.2 Length invariants (critical)

- All rendered **audio stems** are **sample-aligned** and **exactly the same length**.
- All exported **MIDI files** match the render duration exactly (same start and end), including an explicit End-of-Track event at the tick corresponding to audio end.
- Silent stems/buses still render full-length files.

### 4.3 Render duration (source of truth)

The render duration is determined deterministically as:

1. Compute `timeline_end` = max end time of:
   - all `section` ranges (`at + dur`)
   - all scheduled events that extend beyond section bounds (e.g., long one-shots)
2. Apply `tail_policy` to produce `render_end`:
   - Default `tail_policy: fixed(0s)` (no automatic extension)
   - Optional `tail_policy: fixed(<time>)` extends by a constant time (e.g., `fixed(10s)`)
3. Round up `render_end` to the next multiple of `globals.block` samples.

**Rule:** sample playback that extends beyond a section contributes to `timeline_end` (so nothing is truncated unless explicitly authored).

### 4.4 Long-form safety (sleep tracks)

- Click-safe boundaries (fade discipline)
- DC offset protection
- Optional spectral hygiene checks (sub buildup / harshness warnings)

---

## 5) Implementation contracts (deterministic DSP)

### 5.1 DSP numeric + timing

- Internal sample type: `float` (32-bit) for DSP; optional `double` for coefficient calculations.
- Render block size: fixed **256 samples** per process block (compile-time constant in v1.0).
- Parameter updates:
  - Audio-rate modulators may update per-sample.
  - Control-rate modulators update per-block by default.
  - Any parameter that changes discontinuously must pass through a **smoother** unless explicitly `step`.
- Global timebase: sample index is authoritative; musical time resolves through tempo map.

### 5.2 Determinism rules

- RNG algorithm: `PCG32` (with explicitly specified constants).
- Seeding:
  - Global seed from CLI `--seed`.
  - Each module gets derived stream: `hash64(global_seed, module_path, stream_id)`.
- Floating point flags:
  - Disable `-ffast-math`.
  - Document compiler flags for consistent FMA behavior.
- Threading:
  - May parallelize across stems/voices, but any summation order must remain stable.

### 5.3 Pitch + polyphony

- Internal pitch representation at render time is `{ frequency_hz, midi_note }`.
- Authoring currently resolves pitch from:
  - Note text (`C2`, `F#3`, `Bb4`) with optional octave (default octave is 4 when omitted).
  - Numeric values (including non-`Hz` unit numbers) interpreted as MIDI note numbers and rounded.
  - `Hz` unit literals (for example `55Hz`), clamped to `>= 1Hz`.
- Default tuning: 12-TET A4=440Hz.
- Event default pitch is `C4` when `pitch` is omitted.
- Chords are supported via explicit pitch lists (`pitch: [C3,E3,G3]`) which expand to simultaneous note-ons.
- Current renderer limitations:
  - `pitch` call expressions (for example `chord(C3,"min7")`) are parsed but not expanded by pitch resolution; unhandled forms fall back to A4.
  - Note suffix offsets such as `A4+7c` or `C3+3st` are currently not applied during pitch resolution.
  - `poly` and full `voice_steal` strategies are not yet fully enforced as independent voice allocators.
  - `mono` is enforced as single-active-note serialization; `legato/retrig` provides basic overlap behavior.

### 5.4 Event scheduling

- Same-timestamp priority (high→low):
  1. Section boundary directives
  2. Parameter automation
  3. Note-on
  4. Note-off
- Automation interpolation: `linear` default; supports `step`, `exp`, `smooth`.

### 5.5 Smoothing + click avoidance

- Smoother: one-pole or critically-damped smoothing for automated params.
- Fade discipline:
  - Stem boundaries and any explicit stop should apply a short fade (default 5–20ms) unless disabled.

### 5.6 Oscillator + filter quality

- Anti-aliasing: PolyBLEP for saw/pulse; triangle via integrated bandlimited square or equivalent.
- Filters: stable SVF + biquad set; resonance bounded.

---

## 6) Canonical AURORA language (v1.0)

### 6.1 File overview and versioning

A `.au` file is UTF-8 text and **must** declare a language version header:

- `aurora { version: "1.0" }`

Rules:

- Unknown **major** versions are rejected.
- Newer **minor** versions may warn but should attempt to run if compatible.

After the header, the file may contain (in any order):

- `assets { ... }` (optional)
- `outputs { ... }` (optional)
- `globals { ... }` (optional)
- one or more `bus <Name> { ... }`
- one or more `patch <Name> { ... }`
- one `score { ... }`

### 6.2 Lexical rules

- Comments: `// ...` and `/* ... */`
- Identifiers: `[A-Za-z_][A-Za-z0-9_]*`
- Strings: `"..."` with escapes ` `, `	`, `\"`, `\`
- Numbers: ints/floats (`120`, `0.5`, `-3.0`)
- Lists: `[a, b, c]`
- Maps: `{ key: value, ... }`

### 6.3 Units and literals

- Time: `ms`, `s`, `min`, `h`, `beats`
- Frequency: `Hz`
- Gain: `dB` or linear float
- Notes: `C2`, `F#3`, `Bb4` (octave optional, defaults to 4), MIDI `0..127`, or `Hz`.
- Current implementation note: note suffix offsets like `+7c` / `+3st` are parsed as text but not applied to pitch.

### 6.4 Assets (external samples)

Optional `assets` block:

```au
assets {
  samples_dir: "./samples/",
  samples: { wind: "field/wind_loop.flac", tick: "foley/metal_tick.wav" }
}
```

**Path resolution:**

1. absolute path → use as-is
2. else if `samples_dir` set → relative to it
3. else → relative to `.au` location

Supported formats: WAV + FLAC (libsndfile).

#### 6.4.1 Sample semantics (no ambiguity)

- `start`/`end` are in **source sample time** when expressed in seconds; `end: -1s` = end of file.
- `end <= start` is an error.
- `speed` is pure playback-rate (no time-stretch).
- Looping:
  - `mode: loop` loops `[start,end)`.
  - Default loop crossfade `xfade: 10ms` (constant-power), clamped to ≤10% of loop length.
- Gate:
  - threshold >0.5.
  - oneshot ignores gate-off unless `stop_on_release: true` (10ms fade-out).
  - loop stops on gate-off with 10ms fade-out.
- Channel policy:
  - only mono/stereo supported; >2 channels is a validation error.
  - stereo→mono downmix is `(L+R)*0.5`.

### 6.5 Outputs (multiple filename targets)

Optional `outputs` block:

```au
outputs {
  stems_dir: "./renders/stems/",
  midi_dir:  "./renders/midi/",
  mix_dir:   "./renders/mix/",
  meta_dir:  "./renders/meta/",
  master:    "master.wav",
  render_json:"render.json"
}
```

Rules:

- Relative paths are resolved relative to `.au` location unless overridden by CLI `--out`.
- `stem("name")` resolves to `stems_dir + name + ".wav"` by default.

### 6.6 Globals and tempo

`globals` may specify render settings and tempo.

Global tempo (simple mode):

- `globals { tempo: 60 }`

Tempo map (advanced mode):

```au
globals {
  tempo_map: [ { at: 0s, bpm: 60 }, { at: 6min, bpm: 54 }, { at: 10min, bpm: 48 } ]
}
```

Rules:

- `tempo_map` points must be non-decreasing in `at`.
- Beat↔time conversion is piecewise linear.
- Authoring may mix seconds and beats; all resolves to sample time.

### 6.7 Patches, buses, events, and score

#### 6.7.1 Patch

Minimal keys:

- `poly`, `voice_steal`, `mono`
- optional `legato`, `retrig: always|legato|never`
- optional `binaural: { enabled, shift|shift_hz, mix }`
- `graph: { nodes: [...], connect: [...], io: { out: "nodeId" } }`
- `out: stem("name")`
- optional `send: { bus: "BusName", amount: -18dB }`

`binaural` semantics (renderer):
- `enabled: true` makes the patch stem stereo (L/R).
- `shift` (or `shift_hz`) defines base interaural frequency split in Hz.
- `mix` blends centered pitch vs split binaural pitch (`0..1`).

#### 6.7.2 Bus

A bus is a summing node with an FX graph.

Bus input contract:

- Each bus has an implicit summing input ``.
- All sends route into `bus_in` (stable summation order).
- A bus graph may reference `bus_in` as a node id (see Graph DSL).

#### 6.7.3 Events

- `play <PatchName> { at, dur, vel, pitch, params }`
- `trigger <PatchName> { at, [dur], vel, pitch, params }`
- `gate <PatchName> { at, [dur], vel, pitch, params }`
- `automate <target> <curve> { time: value, ... }`
- `seq <Name> { ... }` (deterministic expander)

Current implementation note (renderer):
- `trigger` and `gate` currently expand to play-like note events.
  - `trigger` default `dur` is `10ms` when omitted.
  - `gate` default `dur` is `250ms` when omitted.
- Envelope release tails are rendered after gate-off and included in timeline sizing.
- `play.params` and `seq.params` are active and routed as per-event overrides.
- Param keys support dotted form (`"node.param"`) or nested objects (`node: { param: ... }`).
- Resolution precedence per key is: event params > automation > static node/default value.

#### 6.7.4 Directives

`section` can include directives:

- `| key=value, key=value, ...`

Current implementation note (renderer):
- Section transition smoothing is supported with:
  - `xfade` (shorthand for both edges)
  - `xfade_in`
  - `xfade_out`
- These apply as section-level gain ramps and multiply event envelopes (`play` + expanded `seq`) near section boundaries.

#### 6.7.5 Score

- `score { section ... }`
- `section <Name> at <time> dur <time> | directives { events... }`
- Structural operators (deterministic compile-time expansion):
  - `repeat N { ... }` where `N` is a positive unitless integer.
  - `loop for <duration> { ... }` (whole-iteration fill, bounded by duration).
  - `pattern Name { ... }` and score-level `play Name x N [at <time>]`.

Current implementation notes (parser):
- `repeat` and `loop` expand sequentially by body span (no overlap-by-default).
- `loop for` iteration count is `floor(duration / body_span)`.
- Pattern play requires pattern declaration earlier in score order.
- Structural bodies must have positive span and unit-compatible timing.

---

## 7) Sequencing (`seq`) — patched v1.0

`seq` expands into `play` events deterministically.

### 7.1 Syntax

`seq <Name> { at, dur, rate, prob, pitch, jitter, pattern, pick, swing, max, burst, params }`

### 7.2 Timing

- Step grid from `at` to `at+dur` with interval `rate`.
- One trigger per step by default.
- Jitter is deterministic in `[-jitter,+jitter]` and clamped to step window.

### 7.3 Pattern

- String: `"x..x.."` wraps.
- Euclid: `euclid(k,n[,rot])`.

### 7.4 Probability + density cap

- Eligible step triggers if `rand() < prob_eff` where `prob_eff = clamp(prob * density.prob_multiplier, 0, 1)`.
- Hard cap `density.max_events_per_minute` via sliding 60s window; exceeding events are skipped deterministically.

### 7.5 Pitch selection

- `pick: "uniform"|"cycle"|"weighted"`.
- `weighted` uses `weights: [...]` aligned to pitch list.

### 7.6 Swing

- Applies only on beat grid.
- Odd steps delayed by `(swing-0.5)*step_duration`.

### 7.7 Burst

- `burst: { prob, count, spread }` emits evenly spaced hits within step window; still subject to caps.

### 7.8 RNG stream

- `hash64(global_seed, "seq", section_name, seq_name)`

---

## 8) Graph DSL v1.0 (patch and bus graphs)

### 8.1 Structure

`graph: { nodes: [ ... ], connect: [ ... ], io: { out: "nodeId" } }`

### 8.2 Node definition

`{ id: "osc1", type: osc_sine, params: { freq: 55Hz } }`

### 8.3 Connections

`{ from: "nodeId.port", to: "nodeId.port", rate: audio|control }`

Control modulation mapping (implemented):
- Optional `map` object on a control connection:
  - `{ type, curve, min, max, scale, offset, invert, bias }`
- `type` supports: `set|add|mul|range|db|hz|lin`
  - `db/range/hz/lin` are set-style mappings.
- `curve` supports: `linear|step|smooth|exp` on normalized control source.
- `min/max` map a normalized control value (`0..1`) into destination range.
- `scale/offset` apply after range mapping.
- `invert` flips the source as `1-source` before mapping.
- `bias` adds a source bias before mapping.
- Default op when `type` is omitted:
  - `set` for `*.cv`
  - `add` otherwise

Example:
- `{ from: "env.out", to: "amp.gain", rate: control, map: { type: db, min: -60, max: 0 } }`

### 8.4 Ports and channel rules (explicit 2-channel policy)

- Default output port: `out`.
- Audio inputs: `in` (mono) or `inL/inR` (stereo).
- Control inputs: named after parameter.
- Only mono/stereo exist in v1.0; >2 channels is a validation error.
- Coercions:
  - mono→stereo: duplicate
  - stereo→mono: (L+R)\*0.5

### 8.5 Automation target naming (canonical)

- `patch.<PatchName>.<nodeId>.<param>`
- `bus.<BusName>.<nodeId>.<param>`

---

## 9) Node catalog v1.0 (DSP building blocks)

### 9.1 Common conventions

- Only mono/stereo signals.
- Automated params smoothed unless `step`.
- dB params default range: `-120dB..+12dB` unless otherwise specified.

### 9.1.1 Param routing precedence (renderer)

General order for routed params:
1. event params (`play.params` / `seq.params`)
2. automation lane (`patch.<Patch>.<node>.<param>`)
3. static node/default value

Key table:
- Oscillator:
  - `<osc>.freq`: static `params.freq`; if unresolved, falls back to pitch path.
  - `<osc>.detune`, `<osc>.transpose`, `<osc>.pw`
  - `<osc>.binaural_shift`, `<osc>.binaural_mix`
- Envelope:
  - `<env>.a`, `<env>.d`, `<env>.s`, `<env>.r`
- Filter:
  - `<filter>.cutoff`, `<filter>.q`, `<filter>.res`
  - `<filter>.freq` is a cutoff alias when `.cutoff` event/automation is absent
- Gain:
  - `<gain>.gain`
- VCA:
  - `<vca>.cv`
  - `<vca>.gain`

### 9.2 Oscillators

- `osc_sine(freq, phase)`
- `osc_saw_blep(freq, phase)`
- `osc_pulse_blep(freq, pw, phase)`
- `osc_tri_blep(freq, phase)`
- `noise_white(amp)`
- `noise_pink(amp)`
- `osc_wavetable(table, freq, interp)`

Current implementation note (renderer):
- Oscillator frequency precedence is:
  1. event-level `params.<oscNode>.freq`
  2. automation lane `patch.<Patch>.<oscNode>.freq`
  3. static oscillator `params.freq`
  4. event `pitch` with detune/transpose
- Per-oscillator offsets are supported via `params.detune` and `params.transpose`.
- `detune` accepts cents by default (`-7`), or explicit `c`/`st` units (`-7c`, `+12st`).
- `transpose` accepts semitones by default (`+12`), or explicit `st`/`c`.
- `pw` is routable for pulse oscillators via automation/event params.
- Binaural controls are routable per oscillator when patch binaural is enabled:
  - `<osc>.binaural_shift` (Hz split between L/R frequencies)
  - `<osc>.binaural_mix` (`0..1` blend between centered and split)

### 9.3 External samples

- `sample_player(sample, mode, start, end, speed, gain, xfade, stop_on_release)`
- `sample_slice(sample, slices, index, gain)`

### 9.4 Envelopes + modulators

- `env_adsr(a,d,s,r,curve)`
- `env_ad(a,d,curve)`
- `env_ar(a,r,curve)`
- `lfo(shape, rate, depth, pw)`
- `cv_scale(scale, bias)`     // `out = in * scale + bias`
- `cv_offset(offset)`         // `out = in + offset`
- `cv_mix(a, b, bias)`        // `out = in1*a + in2*b + bias`
- `cv_clip(min, max, bias)`   // `out = clamp(in + bias, min, max)`
- `cv_slew(rise, fall)`       // smoothed control transition
- `slew(rise, fall)`

### 9.5 Filters

- `svf(mode, cutoff, res, drive)`
- `biquad(type, freq, q, gain)`

Current implementation note (renderer):
- Filter cutoff reads `cutoff` first, with `freq` accepted as an alias.
- Filter automation accepts both `<filterNode>.cutoff` and `<filterNode>.freq` (`.cutoff` takes precedence when both exist).
- Runtime cutoff clamp is `20Hz..0.99 * Nyquist`.
- `q` and `res` are routable through automation/event params.

### 9.6 Mix + dynamics

- `gain(gain)`
- `vca(gain, cv)`
- `pan(pan, law)`
- `mix()`
- `softclip(drive)`

Current implementation notes:
- `vca` is a real output multiply stage.
- Final per-voice gain includes `vca.cv * vca.gain` in addition to gain/env stages.
- `vca.cv` is intended for envelope/LFO control patching (`env.out -> vca.cv`).
- CV utility nodes are valid modulation sources for downstream control routing.
- Graph validation enforces port-type legality for known node classes.
- Envelope time params (`a/d/r`) support `ms|s|min|h` conversion to seconds.
- Mono voice behavior:
  - overlapping notes are serialized when `mono: true`
  - mono overlap priority uses `voice_steal`:
    - `first`: keep active note, ignore incoming overlaps
    - `highest`: keep higher-pitch note
    - `lowest`: keep lower-pitch note
    - `last`/`oldest`/other: newest note wins
  - retrigger modes:
    - `always`: normal attack on transitions
    - `legato`: with `legato: true`, suppress attack on overlap transitions
    - `never`: suppress attack on all mono transitions after first note

### 9.7 FX

- `delay(time, fb, mix, hicut, locut)`
- `reverb_algo(size, decay, predelay, mix, hicut)`
- `eq_shelf(low_gain, low_freq, high_gain, high_freq)`

---

## 10) Macro standard library v1.0 (ritual directives)

### 10.1 Conflicts

- Explicit `automate` and event-level params override macro outputs for the same target during overlap.

### 10.2 Standard macros

#### 10.2.1 `pressure`

Default mapping (if targets exist):

- `patch.*.<svf>.cutoff` opens with pressure
- patch output gain rises slightly
- reverb send rises early then stabilizes
- `seq.prob` increases (bounded)
- `noise_*.amp` increases (bounded)

Presets: `slow_rise`, `stable`, `fall`, `pulse`.

#### 10.2.2 `density`

Maps to `{ rate_multiplier, prob_multiplier, max_events_per_minute }`. Defaults:

- `very_low`: `{0.5, 0.6, 8}`
- `low`: `{0.75, 0.8, 16}`
- `medium`: `{1.0, 1.0, 32}`
- `high`: `{1.25, 1.15, 64}`

#### 10.2.3 `silence`

Suppresses *new* `seq` triggers (never gates ongoing voices):

- `long`: 60%
- `medium`: 35%
- `short`: 15%

### 10.3 Constraint packs

- `resist_resolution` → `{ density:"low", silence:"medium", pressure:"stable" }`
- `long_breath` → `{ silence:"long", density:"very_low" }`
- `sparse_events` → `{ density:"very_low" }`
- `monolithic_decl` → `{ pressure:"slow_rise", density:"low", silence:"long" }`

---

## 11) MIDI export contracts (v1.0)

- SMF **Format 1**.
- Default **PPQ**: `480`.
- Tempo:
  - If global tempo: single tempo meta event at time 0.
  - If tempo map: emit tempo meta events at each change point.
- End alignment:
  - Compute final audio sample index `N_end`.
  - Convert `N_end` to ticks using the same tempo rules.
  - Emit End-of-Track at tick **ceil** of that value (deterministic) and pad silence if needed so MIDI duration matches audio duration.
- CC automation export:
  - Default sampling: control-rate, once per render block (256 samples), unless explicitly requested otherwise.
  - Smoothed values are exported post-smoothing.

---

## 12) Minimal dependencies + build choices

### 12.1 Build

- CMake project with:
  - `aurora` (CLI)
  - `aurora_core` (DSP + graph)
  - `aurora_lang` (parser + AST/IR)
  - `aurora_io` (wav/flac/midi/json)

### 12.2 I/O libraries

- Audio file writing: **libsndfile** (WAV/FLAC)
- JSON metadata: **nlohmann/json**
- MIDI writing: deterministic in-house SMF writer

---

## 13) Acceptance criteria (definition of done for v1.0)

1. Define a patch with a real graph: osc → filter → amp env → send → stem.
2. Define a bus with a real graph: `bus_in` → reverb → stem.
3. Define a score with:
   - sections, tempo (global or map), sparse `seq`, pressure + silence directives.
4. Render:
   - ≥4 stems + master, all same length.
5. Export MIDI:
   - ≥1 track with notes + CC automation.
6. Reproducibility:
   - same inputs yield same outputs (per determinism scope).

---

## 14) Canonical example file (language v1.0)

```au
// AURORA canonical example (language v1.0)

aurora { version: "1.0" }

assets {
  samples_dir: "./samples/",
  samples: {
    wind: "field/wind_loop.flac",
    tick: "foley/metal_tick.wav"
  }
}

outputs {
  stems_dir: "./renders/stems/",
  midi_dir:  "./renders/midi/",
  mix_dir:   "./renders/mix/",
  meta_dir:  "./renders/meta/",
  master:    "master.wav",
  render_json:"render.json"
}

globals {
  sr: 48000,
  block: 256,
  tempo: 60,
  // tail_policy: fixed(10s)
  // tempo_map: [ { at: 0s, bpm: 60 }, { at: 6min, bpm: 54 } ]
}

bus ReverbBus {
  out: stem("fx_reverb"),
  graph: {
    nodes: [
      { id: "bus_in", type: mix },
      { id: "rev", type: reverb_algo, params: { size: 0.7, decay: 8s, predelay: 12ms, mix: 1.0 } }
    ],
    connect: [
      { from: "bus_in", to: "rev.in" }
    ],
    io: { out: "rev" }
  }
}

patch SubDrone {
  poly: 4,
  voice_steal: oldest,
  out: stem("sub_drone"),
  send: { bus: "ReverbBus", amount: -18dB },
  graph: {
    nodes: [
      { id: "osc1", type: osc_sine, params: { freq: 55Hz } },
      { id: "osc2", type: osc_pulse_blep, params: { freq: 55Hz, pw: 0.45 } },
      { id: "sum",  type: mix },
      { id: "filt", type: svf, params: { mode: lp, cutoff: 80Hz, res: 0.2 } },
      { id: "env",  type: env_adsr, params: { a: 2s, d: 6s, s: 0.7, r: 10s, curve: exp } },
      { id: "amp",  type: gain, params: { gain: -6dB } }
    ],
    connect: [
      { from: "osc1", to: "sum.in1" },
      { from: "osc2", to: "sum.in2" },
      { from: "sum",  to: "filt.in" },
      { from: "filt", to: "amp.in" },
      { from: "env",  to: "amp.gain", rate: control }
    ],
    io: { out: "amp" }
  }
}

patch MetalTicks {
  poly: 8,
  voice_steal: quietest,
  out: stem("metal_ticks"),
  send: { bus: "ReverbBus", amount: -10dB },
  graph: {
    nodes: [
      { id: "sp",   type: sample_player, params: { sample: "tick", mode: oneshot, gain: -8dB } },
      { id: "hp",   type: biquad, params: { type: hp, freq: 1800Hz, q: 0.9 } }
    ],
    connect: [
      { from: "sp", to: "hp.in" }
    ],
    io: { out: "hp" }
  }
}

score {
  section Subsurface at 0s dur 3min | pressure="slow_rise", density="low", silence="long" {
    play SubDrone { at: 0s, dur: 12min, vel: 0.6, pitch: C2 }
    automate patch.SubDrone.filt.cutoff linear { 0s: 70Hz, 3min: 140Hz }
  }

  section Hold at 3min dur 6min | pressure="stable", density="low", silence="medium" {
    seq MetalTicks { at: 3min, dur: 6min, rate: 1beats, pattern: euclid(3,8,1), prob: 0.12, pick: "cycle", pitch: [C5, D5, G5], swing: 0.58, jitter: 25ms, max: 120 }
  }

  section Seal at 9min dur 3min | pack="long_breath", pressure="fall" {
    automate patch.SubDrone.filt.cutoff smooth { 9min: 120Hz, 12min: 60Hz }
  }
}
```

---

## 15) Golden tests + regression strategy

- Maintain canonical projects:
  - `tests/canonical_root_gate.au`
  - `tests/tempo_map_changes.au`
  - `tests/chord_polyphony.au`
- Store expected outputs:
  - `render.json`
  - per-stem hashes (normalized header strategy)
  - peak/RMS summaries
- Sanity checks:
  - no NaNs/INFs
  - no clipping unless allowed
  - boundary click metrics
