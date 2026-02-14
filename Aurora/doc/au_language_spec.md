# Aurora `.au` Language Specification (v1.x)

This document specifies the `.au` text format implemented in this repository (`aurora_lang` parser + validator + renderer). It is written so a code model can reliably read, generate, and edit valid files.

## 1. File Model

An `.au` file is UTF-8 text.

A valid file must include:
- `aurora { version: "..." }`
- at least one `patch ... { ... }`
- a `score { section ... }` with at least one `section`

Top-level blocks:
- `aurora { ... }` (required)
- `assets { ... }` (optional)
- `outputs { ... }` (optional)
- `globals { ... }` (optional)
- `bus <name> { ... }` (0+)
- `patch <name> { ... }` (1+)
- `score { section ... }` (1+, sections are appended)

Top-level order is free-form.

## 2. Lexical Rules

### 2.1 Comments and Whitespace
- Whitespace is ignored.
- Line comments: `// ...`
- Block comments: `/* ... */`

### 2.2 Identifiers
- Start: letter or `_`
- Continue with letters, digits, `_`, `#`, `+`, `-`, `$`

Examples:
- `osc_sine`
- `patch`
- `C#4`
- `very_low`

### 2.3 Strings
- Either `'...'` or `"..."`
- Escapes supported: `\n`, `\t`, `\r`, `\\`, `\"`, `\'`

### 2.4 Numbers and Unit Numbers
A numeric token is:
- signed/unsigned integer or float
- optional exponent (`e`/`E`)
- optional trailing alphabetic unit

Examples:
- `12`
- `-0.25`
- `1e-3`
- `120Hz`
- `-18dB`
- `3min`
- `600ms`
- `2beats`

Units are parsed as raw suffix strings in syntax; semantic support depends on context.

### 2.5 Symbols
Single-character symbols:
- `{ } [ ] ( ) : , . | =`

## 3. Value Grammar

Values allowed in object fields and event bodies:
- `null`
- booleans: `true` / `false`
- number or unit number
- string
- identifier
- list: `[v1, v2, ...]`
- object: `{ key: value, ... }`
- call: `name(arg1, arg2, ...)`

Object keys can be identifier, string, or number token.

## 4. EBNF (Practical)

```ebnf
file            = { top_level } ;

top_level       = aurora_header
                | assets_block
                | outputs_block
                | globals_block
                | bus_block
                | patch_block
                | score_block ;

aurora_header   = "aurora" object ;
assets_block    = "assets" object ;
outputs_block   = "outputs" object ;
globals_block   = "globals" object ;

bus_block       = "bus" ident_like object ;
patch_block     = "patch" ident_like object ;

score_block     = "score" "{" { score_item } "}" ;
score_item      = section_block
                | repeat_block
                | loop_block
                | pattern_decl
                | pattern_play ;
section_block   = "section" ident_like "at" value "dur" value [ "|" directives ] "{" { section_event } "}" ;
repeat_block    = "repeat" positive_int "{" { score_item } "}" ;
loop_block      = "loop" "for" value "{" { score_item } "}" ;
pattern_decl    = "pattern" ident_like "{" { score_item } "}" ;
pattern_play    = "play" ident_like "x" positive_int [ "at" value ] ;
directives      = ident_like "=" value { "," ident_like "=" value } ;

section_event   = play_event | automate_event | seq_event ;

play_event      = "play" ident_like object ;
automate_event  = "automate" dotted_ident ident_like "{" automation_point { [","] automation_point } "}" ;
automation_point= number_token ":" value ;
seq_event       = "seq" ident_like object ;

dotted_ident    = ident_like { "." ident_like } ;
ident_like      = identifier | string ;

object          = "{" [ pair { [","] pair } ] "}" ;
pair            = object_key ":" value ;
object_key      = identifier | string | number_token ;

list            = "[" [ value { "," value } ] "]" ;
call            = identifier "(" [ value { "," value } ] ")" ;
positive_int    = unitless_number_token ;
```

Notes:
- Object commas are optional between pairs.
- List commas are required between elements.
- `automate` point separators are optional commas.

## 5. Top-Level Block Schemas

## 5.1 `aurora`
Required key:
- `version` (string/identifier-like text)

Validation rule:
- major version must be `1` (e.g., `1.0`, `1.2`)

## 5.2 `assets`
Optional keys:
- `samples_dir`: string
- `samples`: object map of sample name -> path string

## 5.3 `outputs`
All optional; defaults:
- `stems_dir`: `renders/stems`
- `midi_dir`: `renders/midi`
- `mix_dir`: `renders/mix`
- `meta_dir`: `renders/meta`
- `master`: `master.wav`
- `render_json`: `render.json`

## 5.4 `globals`
Optional keys and defaults:
- `sr` (number): default `48000`
- `block` (number): default `256`
- `tempo` (number): optional, default behavior is 60 BPM when not set
- `tempo_map` (list of `{ at: <time>, bpm: <number> }`)
- `tail_policy`: currently `fixed(<time>)` only, default fixed 0 seconds

Validation rule:
- `block` must be exactly `256` in v1

Time units accepted by time conversion:
- `s`, `ms`, `min`, `h`, `beats`, or no unit (treated as seconds in time contexts)

## 5.5 `patch <name>`
Body keys:
- `poly` (number, default `8`)
- `voice_steal` (text, default `oldest`)
- `mono` (bool, default `false`)
- `legato` (bool, default `false`)
- `retrig` (`always|legato|never`, default `always`)
- `binaural` object (optional):
  - `enabled` (bool, default `false`)
  - `shift` or `shift_hz` (number or `Hz`, default `0`)
  - `mix` (number, default `1.0`)
- `out` (usually `stem("name")`; if omitted defaults to patch name)
- `send` object:
  - `bus` (text)
  - `amount` (number or `dB` unit, default `0 dB`)
- `graph` object (see Graph schema)

Validation rules:
- patch names must be unique
- graph must contain at least one node
- graph `io.out` is required

## 5.6 `bus <name>`
Body keys:
- `channels` (number, default `1`, allowed `1|2`)
- `out` (usually `stem("name")`; if omitted defaults to bus name)
- `graph` object (see Graph schema)

Validation rules:
- bus names must be unique
- `channels` must be `1` or `2`
- graph must contain at least one node
- graph `io.out` is required

## 5.7 Graph schema (`patch.graph` / `bus.graph`)

```au
graph: {
  nodes: [
    { id: "node_id", type: node_type, params: { ... } }
  ],
  connect: [
    { from: "src", to: "dst", rate: control, map: { ... } }
  ],
  io: { out: "node_or_port" }
}
```

Required:
- each node needs `id` and `type`
- each connection needs `from` and `to`

`rate` defaults to `audio`.
`map` is optional and used for control modulation mapping.

Control modulation (`connect`) behavior implemented by renderer:
- Connections that target non-audio inputs (`to: "<node>.<param>"`) and are `rate: control` are treated as modulation routes.
- Per-route execution rate:
  - `rate: audio` evaluates modulation per-sample.
  - `rate: control` evaluates at render-block boundaries and holds between updates (sample-and-hold behavior).
- Source nodes currently supported as control sources:
  - `env_adsr` / `env_ad` / `env_ar` (`<env>.out`)
  - `lfo` (`<lfo>.out`)
  - `cv_scale`, `cv_offset`, `cv_mix`, `cv_slew`, `cv_clip`, `cv_invert`, `cv_sample_hold`, `cv_cmp`, `cv_logic`
- Map fields:
  - `type`: `set|add|mul|range|db|hz|lin` (`db/range/hz/lin` behave as `set`)
  - `curve`: `linear|step|smooth|exp` (applied on normalized source before range/map math)
  - `min`, `max`: optional range mapping (source clamped to `[0,1]` before scaling to range)
  - `scale`, `offset`: optional final affine transform
  - `invert` (bool): transforms source as `1 - source` before mapping
  - `bias` (number): additive source bias before mapping
- If `map.type` is omitted:
  - default op is `set` when destination port is `cv`
  - otherwise default op is `add`

Example:
```au
{ from: "env.out", to: "amp.gain", rate: control, map: { type: db, min: -60, max: 0 } }
```

Renderer behavior only interprets a subset of node types/params (others parse but may have no audible effect).

Implemented oscillator parameter behavior (`osc_*` nodes):
- Oscillator frequency precedence:
  1. event-level `params.<oscNode>.freq`
  2. automation lane `patch.<Patch>.<oscNode>.freq`
  3. static node `params.freq`
  4. event `pitch` with detune/transpose
- `detune` offsets oscillator pitch:
  - `<number>` = cents (e.g. `-7`)
  - `<number>c` = cents (e.g. `-7c`)
  - `<number>st` = semitones (e.g. `+12st`)
- `transpose` offsets oscillator pitch:
  - `<number>` = semitones
  - `<number>st` = semitones
  - `<number>c` = cents
- `pw` is used by `osc_pulse_blep`.
- Binaural stereo controls (when patch `binaural.enabled` is `true`):
  - `<osc>.binaural_shift` controls L/R frequency split in Hz.
  - `<osc>.binaural_mix` blends center vs split (`0..1`).

Implemented filter behavior (`svf` / `biquad` nodes):
- Filter node type may be `svf` or `biquad`; both use the same renderer filter core.
- Mode is read from:
  - `mode` param first (e.g. `mode: lp`)
  - else `type` param (e.g. `type: hp`)
  - else default `lp`
- Supported modes (with aliases):
  - low-pass: `lp` (default/fallback)
  - high-pass: `hp`, `highpass`
  - band-pass: `bp`, `bandpass`
  - notch/band-stop: `notch`, `bandstop`
- Cutoff parameter:
  - `cutoff` preferred
  - `freq` accepted as alias
  - numeric or `Hz` unit values are accepted
  - runtime clamp: `[20 Hz, 0.99 * Nyquist]`
- Resonance shaping:
  - `q` is used (default `0.707`, minimum `0.05`)
  - `res` is used (default `0.0`, clamped `[0,1]`) as an additional resonance boost factor on top of `q`
- Filter slope:
  - `slope: 12|24` (`24` runs a cascaded second filter pass for steeper response)
- Filter key-tracking:
  - `keytrack` scales cutoff by note pitch relative to MIDI 60 (`C4`)
  - `0.0` disables key tracking; `1.0` applies one-octave cutoff scaling per played octave
- Filter envelope amount:
  - `env_amt` (`env_amount` alias) adds envelope-scaled Hz offset to cutoff each sample
- Filters are stateful per rendered voice (continuous state through each note event).

Implemented VCA behavior (`vca` node):
- Node params:
  - `gain`: base linear multiplier (or `dB`, converted to linear)
  - `cv`: base control value (`0..1`)
- Runtime multiply stage:
  - final output gain includes `vca_cv * vca_gain`
  - `vca.cv` and `vca.gain` are routable via event params, automation, and modulation connections
- Typical subtractive patching:
```au
{ from: "env.out", to: "vca.cv", rate: control }
```

Implemented envelope mode behavior:
- `env_adsr`: attack/decay/sustain during gate, release after gate-off.
- `env_ad`: attack then decay to zero (one-shot contour).
- `env_ar`: attack to full level while gate is high, release after gate-off.
- Time units on envelope params (`a/d/r`) support `ms|s|min|h` and are converted to seconds.

## 6. Score and Event Semantics

## 6.0 Score Structural Operators (Compile-Time Expansion)

Score blocks support deterministic structural composition operators:

- `repeat N { ... }`
  - `N` must be a positive unitless integer literal.
  - Expansion is sequential (each iteration offset by body span).
- `loop for <duration> { ... }`
  - Body repeats sequentially until duration is filled by whole iterations.
  - Iteration count = `floor(loop_duration / body_span)`.
- `pattern Name { ... }`
  - Declares a reusable score structure.
- `play Name x N [at <time>]`
  - Places a declared pattern `N` times sequentially.
  - Optional `at` adds a start offset before the first pattern iteration.

Determinism constraints and limitations:
- These operators are expanded at parse time.
- Body span must be strictly positive (`> 0`), otherwise parse error.
- Time-unit arithmetic inside these operators requires compatible units.
- Pattern placement requires prior declaration in score order.
- This system intentionally excludes unbounded `while`/stateful loops.

## 6.1 Section Header
Format:
```au
section Name at <time> dur <time> | key=value, ... { ... }
```

- `at` and `dur` are required.
- Directives are optional free-form key/value pairs.

Known section directives affecting `seq` expansion:
- `density`: `very_low|low|medium|high`
- `silence`: `short|medium|long`
- `pack`: presets (`resist_resolution`, `long_breath`, `sparse_events`, `monolithic_decl`) that set/modify density/silence
- `xfade`: shorthand to set both `xfade_in` and `xfade_out`
- `xfade_in`: fade-in duration applied from section start
- `xfade_out`: fade-out duration applied toward section end

Section crossfade behavior:
- Crossfade directives are optional and parse-time free-form directives.
- Values are time literals (`ms|s|min|h|beats` where applicable).
- During rendering, a section-level gain ramp is multiplied into event envelopes:
  - `xfade_in`: ramps section gain `0 -> 1` over `[section.at, section.at + xfade_in]`
  - `xfade_out`: ramps section gain `1 -> 0` over `[section.end - xfade_out, section.end]`
- These ramps apply to both `play` and expanded `seq` events in the section.
- If `xfade` is present and specific `xfade_in`/`xfade_out` are absent, both sides use `xfade`.

## 6.2 `play`
Format:
```au
play PatchName { at: <time>, dur: <time>, vel: <num>, pitch: <pitch-or-list>, params: { ... } }
```

Defaults:
- `vel`: `1.0`
- `pitch`: `C4` if omitted

Pitch forms accepted:
- note text (`C2`, `F#3`, `Bb1`)
- MIDI-like number
- `Hz` unit number
- list of any of the above

`params` is used by renderer as per-event node parameter overrides.
- Supported shapes:
  - flat: `params: { "filt.cutoff": 1200Hz, "amp.gain": -9dB }`
  - nested: `params: { filt: { cutoff: 1200Hz }, amp: { gain: -9dB } }`
- Nested objects are flattened to dotted keys (`filt.cutoff`).
- Precedence for a routed key is:
  - event param (`play/seq params`) > automation lane > static node/default value.

## 6.2.1 `trigger` and `gate`
Format:
```au
trigger PatchName { at: <time>, [dur: <time>], vel: <num>, pitch: <pitch-or-list>, params: { ... } }
gate PatchName    { at: <time>, [dur: <time>], vel: <num>, pitch: <pitch-or-list>, params: { ... } }
```

Renderer behavior:
- Both forms are expanded to play-like note events.
- Default durations when `dur` is omitted:
  - `trigger`: `10ms`
  - `gate`: `250ms`

## 6.3 `automate`
Format:
```au
automate patch.<PatchName>.<node>.<param> <curve> { <time>: <value>, ... }
```

Curves recognized by renderer:
- `linear` (default behavior for unknown curve names)
- `step`
- `smooth`
- `exp`

Target handling:
- Must have at least 4 dot-separated parts and begin with `patch.` to take effect.
- Effective automation key is `<node>.<param>` inside the named patch.
- Routed automation targets currently used by renderer:
  - oscillator: `<osc_node>.freq`, `<osc_node>.detune`, `<osc_node>.transpose`, `<osc_node>.pw`, `<osc_node>.binaural_shift`, `<osc_node>.binaural_mix`
  - envelope: `<env_node>.a`, `<env_node>.d`, `<env_node>.s`, `<env_node>.r`
  - filter: `<filter_node>.cutoff`, `<filter_node>.freq` (cutoff alias when `.cutoff` is absent), `<filter_node>.q`, `<filter_node>.res`
  - gain: `<gain_node>.gain`

## 6.4 `seq`
Format:
```au
seq PatchName {
  at: <time>,
  dur: <time>,
  rate: <time>,
  pattern: "x..x" | euclid(k,n,rot),
  prob: <0..1>,
  vel: <0..1>,
  pitch: <pitch-or-list>,
  pick: uniform|cycle|weighted,
  weights: [w1, w2, ...],
  swing: <0..1>,
  jitter: <time>,
  max: <events_per_minute>,
  burst: { prob: <0..1>, count: <int>, spread: <time> }
  params: { ... }
}
```

Defaults (before section density/silence shaping):
- `at`: section start
- `dur`: section duration
- `rate`: `1s`
- `prob`: `1.0`
- `vel`: `0.8`
- `pitch`: `C4`
- `pick`: `uniform`
- `swing`: `0.5`
- `jitter`: `0s`
- `max`: density-dependent cap

Pattern behavior:
- String/identifier pattern: active chars are `x`, `X`, `*`, `1`; others are rests.
- `euclid(k,n,rot)` generates a Euclidean rhythm.

Density and silence modifiers:
- density alters `rate`, `prob`, and cap-per-minute
- silence injects additional random event skipping

## 7. Validation Rules Summary

Errors:
- unsupported major version
- no patches
- no sections in score
- `globals.block != 256`
- duplicate patch names
- duplicate bus names
- bus channels outside `1..2`
- empty graph nodes in patch/bus
- missing graph `io.out` in patch/bus
- patch send references unknown bus

Warnings:
- reused stem names across outputs
- no `tempo` and no `tempo_map` (engine falls back to 60 BPM)

## 8. Engine-Specific Practical Notes

- Unknown top-level keywords/events are parse errors.
- Unknown keys in known objects usually parse and are ignored unless renderer/validator uses them.
- Score structural operators (`repeat`, `loop for`, `pattern`, score-level `play ... x ...`) are parse-time expansions.
- `out: stem("name")` is just syntactic sugar for output name extraction; plain string also works.
- Times in `beats` are converted via tempo map.
- Automation values are interpreted numerically (`ValueToNumber`); non-numeric values become `0`.
- Routed render params currently include oscillator/filter/envelope/gain keys listed above.
- Routed render params also include:
  - `vca`: `<vca>.cv`, `<vca>.gain`
  - `vca`: `<vca>.curve_amt` (alias `<vca>.curve_amount`)
  - `filter`: `<filter>.drive`
  - `ring_mod`: `<ring>.freq`, `<ring>.mix`, `<ring>.depth`, `<ring>.bias`, `<ring>.pw`
  - `softclip`: `<clip>.drive`, `<clip>.mix`, `<clip>.bias`
  - `audio_mix`: `<mx>.gain`, `<mx>.mix`, `<mx>.bias`
- Modulation routes from graph `connect` are applied after event/automation/static resolution for each sample.
- CV utility nodes are evaluated as control sources:
  - `cv_scale`: `out = in * scale + bias`
  - `cv_offset`: `out = in + offset`
  - `cv_mix`: `out = in1 * a + in2 * b + bias`
  - `cv_clip`: `out = clamp(in + bias, min, max)`
  - `cv_slew`: one-pole slew with independent `rise`/`fall` times
  - `cv_invert`: `out = (bias - in) * scale + offset`
  - `cv_sample_hold`: samples `in` on rising edge of `in2` trigger (threshold + optional hysteresis), holds between triggers
  - `cv_cmp`: comparator with `threshold` and optional `hysteresis`; outputs `high`/`low`
  - `cv_logic`: logic gate on thresholded `in`/`in2`; `op` supports `and|or|xor|nand|nor|xnor`, outputs `high`/`low`
- `ring_mod` node is an active audio stage:
  - stage equation per channel: `out = dry*(1-mix) + (dry * (carrier*depth + bias))*mix`
  - carrier is generated from `shape` + `freq` (Hz) + optional `pw` (for pulse/square)
  - `mode` variants:
    - `balanced` (default): classic bipolar ring multiply
    - `unbalanced`: includes carrier pass-through style AM component
    - `diode`: rectified carrier response
  - `ring_mod_diode` is accepted as an alias node type for `ring_mod` with `mode: diode`
  - currently inserted as a per-voice stage before filter/gain/VCA output gain stages
- `softclip` node is an active audio stage:
  - stage equation per channel: `out = dry*(1-mix) + tanh((dry + bias)*drive)*mix`
  - currently inserted as a per-voice stage before filter/gain/VCA output gain stages
- `audio_mix` node is an active audio utility stage:
  - stage equation per channel: `out = dry*(1-mix) + (dry*gain + bias)*mix`
  - currently inserted as a per-voice stage before filter/gain/VCA output gain stages
- filter `drive` is an active pre-filter input stage:
  - input shaping uses normalized `tanh` waveshaping
  - `drive_pos` / `drive_stage` selects placement: `pre` (default) or `post`
  - `drive` is routable via event params, automation, and modulation
- VCA `curve` currently supports:
  - `linear` (default)
  - `exp` / `exponential` (power-law CV shaping before gain multiply)
  - `log` / `logarithmic` (inverse power-style CV shaping before gain multiply)
  - `curve_amt` / `curve_amount` controls curve intensity (clamped runtime to `0.2..8.0`)
- Graph validation enforces simple port-type rules for known node classes:
  - audio sources cannot connect to control destinations
  - control sources cannot connect to audio inputs
- Control feedback cycles are permitted with deterministic fallback:
  - validator emits a warning when a control cycle is detected
  - renderer resolves cycles with one-sample delayed control fallback
- Mono voice policy:
  - when `patch.mono` is `true`, overlapping notes for that patch are serialized (single active voice)
  - overlap priority in mono mode is based on `voice_steal`:
    - `first`: keep currently active note during overlap, ignore new overlaps
    - `highest`: keep higher-pitch note on overlap
    - `lowest`: keep lower-pitch note on overlap
    - `last`/`oldest`/other: newest note wins
  - retrigger behavior:
    - `always`: normal attack behavior on note transitions
    - `legato`: with `legato: true`, suppress attack on overlap transitions
    - `never`: suppress attack on all mono note transitions after first note
- Envelope release tails are rendered beyond note duration (`gate`/`play`), and render length accounts for those tails.
- `play.params` and `seq.params` override automation and static node values for the same key.
- `osc.freq` is now an active/static parameter and participates in precedence.
- Patch stems are stereo (`channels=2`) when `patch.binaural.enabled` is `true`; otherwise mono.
- Bus stems follow `bus.channels` (`1` mono, `2` stereo).
- Master becomes stereo automatically when any contributing stem is stereo.
- `play` velocities are clamped to `[0, 1.5]` in rendering.
- `seq` velocities are clamped to `[0, 1.0]`.
- Active bus FX params:
  - `delay`: `time`, `fb`, `mix`, `hicut`, `locut`, `pingpong`, `mod_rate`, `mod_depth`
  - `reverb_algo`: `mix`, `decay`, `predelay`, `size`, `width`, `hicut`, `locut`

## 8.1 Param Routing Precedence Tables

All routed params follow:
1. event param (`play/seq params`)
2. automation lane
3. static node/default value

| Domain | Key form | Static source | Notes |
| --- | --- | --- | --- |
| Oscillator | `<osc>.freq` | oscillator node `params.freq` | If none of the above exist, renderer uses pitch path (`event pitch` + detune/transpose). |
| Oscillator | `<osc>.detune` | oscillator node `params.detune` | Bare numeric automation/event values are treated as cents. |
| Oscillator | `<osc>.transpose` | oscillator node `params.transpose` | Bare numeric automation/event values are semitones. |
| Oscillator | `<osc>.pw` | oscillator node `params.pw` | Runtime clamped to `[0.01, 0.99]`. |
| Oscillator | `<osc>.binaural_shift` | patch `binaural.shift` | Active when patch binaural is enabled; interpreted as Hz split between L/R channels. |
| Oscillator | `<osc>.binaural_mix` | patch `binaural.mix` | Runtime clamped to `[0, 1]`; 0 = centered, 1 = full split. |
| Envelope | `<env>.a`, `<env>.d`, `<env>.r` | env node params `a/d/r` | Event values may be unit times (for example `10ms`, `0.2s`). |
| Envelope | `<env>.s` | env node param `s` | Runtime clamped to `[0, 1]`. |
| Filter | `<filter>.cutoff` | filter `cutoff` (or static `freq` alias) | Runtime clamped to `[20Hz, 0.99*Nyquist]`. |
| Filter | `<filter>.freq` | filter `cutoff` fallback path | Alias path for cutoff, used only when `<filter>.cutoff` event/automation is absent. |
| Filter | `<filter>.q` | filter `q` | Runtime min `0.05`. |
| Filter | `<filter>.res` | filter `res` | Runtime clamped to `[0, 1]`. |
| Filter | `slope` (static) | filter `slope` | `12` default, `24` enables cascaded filter pass. |
| Filter | `<filter>.drive` | filter `drive` | Runtime minimum `0`; drive placement is controlled by static `drive_pos`/`drive_stage` (`pre|post`). |
| Filter | `<filter>.keytrack` | filter `keytrack` | Pitch-relative cutoff scaling around MIDI 60; `0` off, `1` full octave-follow. |
| Filter | `<filter>.env_amt` (`<filter>.env_amount`) | filter `env_amt`/`env_amount` | Adds envelope-scaled Hz offset to cutoff each sample. |
| Gain | `<gain>.gain` | gain node `params.gain` | Treated as dB in render path and converted to linear gain. |
| VCA | `<vca>.cv` | vca node `params.cv` | Runtime clamped to `[0, 1]`; multiplied into final gain. |
| VCA | `<vca>.gain` | vca node `params.gain` | Linear by default (`dB` accepted statically); multiplied into final gain. |
| VCA | `<vca>.curve_amt` (`<vca>.curve_amount`) | vca node `params.curve_amt`/`curve_amount` | Runtime clamped to `[0.2, 8.0]`; used by `curve: exp|log`. |

## 9. Authoring Checklist for LLMs

When generating `.au` files:
1. Always emit `aurora { version: "1.0" }`.
2. Set `globals.block: 256`.
3. Provide at least one `patch` with non-empty `graph.nodes` and `graph.io.out`.
4. Provide at least one `section` in `score`.
5. Ensure referenced patch names and send bus names exist.
6. Keep time units to `s|ms|min|h|beats`.
7. For automation, use `patch.<Patch>.<node>.<param>`.
8. Prefer explicit `out: stem("...")` for stable stem naming.

## 10. Minimal Valid Example

```au
aurora { version: "1.0" }

globals { sr: 48000, block: 256, tempo: 60 }

patch Tone {
  out: stem("tone"),
  graph: {
    nodes: [
      { id: "osc", type: osc_sine, params: { detune: -7c } },
      { id: "amp", type: gain, params: { gain: -12dB } }
    ],
    connect: [ { from: "osc", to: "amp.in" } ],
    io: { out: "amp" }
  }
}

score {
  section A at 0s dur 8s {
    play Tone { at: 0s, dur: 8s, vel: 0.8, pitch: C3 }
  }
}
```
