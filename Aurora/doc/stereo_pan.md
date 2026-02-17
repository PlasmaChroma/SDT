# Aurora Spatialization and Pan Specification (Draft)

This document defines a proposed stereo/pan feature set for Aurora.
It is split into phases so we can implement incrementally without blocking on advanced spatial modeling.

## Status

- Current renderer does not implement a dedicated `pan` node.
- Current patch stereo behavior is tied to `patch.binaural.enabled`.
- Buses support `channels: 1|2`; patches do not currently parse `channels`.

## Phase 1 (MVP): Programmable Pan

### 1.1 Node Type: `pan`

Purpose:
- Pan mono sources in stereo.
- Reposition stereo sources in stereo.

Node params:
- `pos` (number, default `0.0`, clamp `[-1.0, 1.0]`)
- `law` (text, default `equal_power`, allowed `equal_power|linear`)
- `width` (number, default `1.0`, clamp `[0.0, 2.0]`)

Notes:
- `phase_offset` is deferred to Phase 3; it is not required for MVP.

Mono input equal-power law:

```text
L = in * cos((pos + 1) * pi / 4)
R = in * sin((pos + 1) * pi / 4)
```

Stereo input behavior:
- Convert to mid/side.
- Apply pan by shifting side energy across channels.
- Reconstruct stereo and apply optional width.

### 1.2 Routable Targets

Add routed params:
- `<pan>.pos`
- `<pan>.width`

`<pan>.law` is static (non-routable) in MVP.

Routing precedence follows existing engine rules:
1. event param (`play/seq params`)
2. section `set` override
3. automation lane
4. static node param

### 1.3 Automation/Modulation Examples

```au
automate patch.Strings.pan1.pos linear {
  0.0: -0.4,
  2.0:  0.3
}
```

```au
connect: [
  { from: "lfo1.out", to: "pan1.pos", rate: control, map: { type: add, scale: 0.2 } }
]
```

## Phase 2: Voice Spread

### 2.1 Patch-Level Object: `voice_spread`

Add optional patch object:

```au
voice_spread: {
  pan: 0.2,
  detune: 6c,
  delay: 8ms
}
```

Fields:
- `pan` (number, default `0.0`, clamp `[0.0, 1.0]`)
- `detune` (cents, default `0c`)
- `delay` (time, default `0ms`, clamp `>= 0ms`)

Behavior per triggered voice:
- Random pan offset in `[-pan, +pan]`
- Random detune offset in `[-detune, +detune]`
- Random start delay in `[0, delay]`

Determinism requirement:
- Randomization must be seeded from renderer seed + stable event/voice identity, so renders are repeatable.

## Phase 3: Spatial Extensions (Optional)

### 3.1 Node Type: `stereo_width`

Purpose:
- Explicit mid/side width processing when needed independently of panning.

Params:
- `width` (number, default `1.0`, clamp `[0.0, 2.0]`)
- `saturate` (bool, default `false`)

Transform:

```text
M = (L + R) / 2
S = (L - R) / 2
S = S * width
L' = M + S
R' = M - S
```

### 3.2 Node Type: `depth`

Purpose:
- Place source front/back before reverb tail.

Params:
- `distance` (`0..1`)
- `air_absorption` (`0..1`)
- `early_reflection_send` (`0..1`)

Proposed behavior:
- Distance-dependent gain attenuation
- High-frequency rolloff
- Early-reflection send increase with distance
- Optional small pre-delay increase

### 3.3 Stage Position Preset (Optional)

Optional helper object to place orchestral sections consistently:

```au
stage_position: { pan: -0.6, depth: 0.25 }
```

This is syntactic sugar over `pan` + `depth` defaults.

## Language and Parser Notes

- Existing `.au` object syntax requires `key: { ... }`, so `voice_spread` must use colon form.
- If we want `patch.channels: 2`, parser + validator + renderer need updates; today only buses parse `channels`.
- Existing mix utility node is `audio_mix`; examples should avoid undefined `mix` unless that node is added.

## MVP Example (Parser-Compatible Draft Shape)

```au
patch ViolinSection {
  poly: 16,
  binaural: { enabled: true, shift: 0Hz, mix: 1.0 },
  voice_spread: { pan: 0.25, detune: 6c, delay: 8ms },
  graph: {
    nodes: [
      { id: "osc1", type: osc_saw_blep },
      { id: "osc2", type: osc_saw_blep },
      { id: "sum",  type: audio_mix, params: { mix: 1.0 } },
      { id: "pan1", type: pan, params: { pos: -0.6, law: equal_power, width: 1.1 } }
    ],
    connect: [
      { from: "osc1", to: "sum.in1" },
      { from: "osc2", to: "sum.in2" },
      { from: "sum",  to: "pan1.in" }
    ],
    io: { out: "pan1" }
  }
}
```

## Recommended Implementation Order

1. `pan` node DSP + routing/automation (`<pan>.pos`, `<pan>.width`)
2. `voice_spread.pan` deterministic randomization
3. `voice_spread.detune` and `voice_spread.delay`
4. optional `stereo_width`, `depth`, and stage helpers
