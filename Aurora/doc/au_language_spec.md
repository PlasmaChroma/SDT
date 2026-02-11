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

score_block     = "score" "{" { section_block } "}" ;
section_block   = "section" ident_like "at" value "dur" value [ "|" directives ] "{" { section_event } "}" ;
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
- `out` (usually `stem("name")`; if omitted defaults to bus name)
- `graph` object (see Graph schema)

Validation rules:
- bus names must be unique
- graph must contain at least one node
- graph `io.out` is required

## 5.7 Graph schema (`patch.graph` / `bus.graph`)

```au
graph: {
  nodes: [
    { id: "node_id", type: node_type, params: { ... } }
  ],
  connect: [
    { from: "src", to: "dst", rate: control }
  ],
  io: { out: "node_or_port" }
}
```

Required:
- each node needs `id` and `type`
- each connection needs `from` and `to`

`rate` defaults to `audio`.

Renderer behavior only interprets a subset of node types/params (others parse but may have no audible effect).

Implemented oscillator parameter behavior (`osc_*` nodes):
- Note/event `pitch` drives oscillator frequency.
- `detune` offsets oscillator pitch:
  - `<number>` = cents (e.g. `-7`)
  - `<number>c` = cents (e.g. `-7c`)
  - `<number>st` = semitones (e.g. `+12st`)
- `transpose` offsets oscillator pitch:
  - `<number>` = semitones
  - `<number>st` = semitones
  - `<number>c` = cents
- `pw` is used by `osc_pulse_blep`.
- `freq` in oscillator params is currently parsed but not applied by the renderer.

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
- Filters are stateful per rendered voice (continuous state through each note event).

## 6. Score and Event Semantics

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

`params` parses but is currently not used by renderer.

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
- Automation targets currently used by renderer:
  - `<filter_node>.cutoff`
  - `<filter_node>.freq` (alias for cutoff when `.cutoff` lane is absent)
  - `<filter_node>.q`
  - `<filter_node>.res`
  - `<gain_node>.gain`

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
- empty graph nodes in patch/bus
- missing graph `io.out` in patch/bus
- patch send references unknown bus

Warnings:
- reused stem names across outputs
- no `tempo` and no `tempo_map` (engine falls back to 60 BPM)

## 8. Engine-Specific Practical Notes

- Unknown top-level keywords/events are parse errors.
- Unknown keys in known objects usually parse and are ignored unless renderer/validator uses them.
- `out: stem("name")` is just syntactic sugar for output name extraction; plain string also works.
- Times in `beats` are converted via tempo map.
- Automation values are interpreted numerically (`ValueToNumber`); non-numeric values become `0`.
- Current audio-rendering automation usage is parameter-specific:
  - filter: `cutoff|freq`, `q`, `res`
  - gain: `gain`
  - other automation lanes currently do not alter rendered audio.
- `play` velocities are clamped to `[0, 1.5]` in rendering.
- `seq` velocities are clamped to `[0, 1.0]`.

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
