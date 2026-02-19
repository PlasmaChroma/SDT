# Click Investigation Notes

## Context

Issue: audible click at the start of brass notes in `au/twinkle_epic.au`.

Primary suspect patch:
- `BrassLead` in `au/twinkle_epic.au` (`mono: true`, `legato: true`, ADSR to VCA)

## High-Priority Finding

### 1) Envelope discontinuity around `note_dur` (likely root cause)

Evidence:
- Forced end fade in render loop: `src/aurora_core/renderer.cpp:2155`
- ADSR release starts after note duration from sustain level: `src/aurora_core/renderer.cpp:1541`

Why this can click:
- In the final ~5ms before `note_dur`, audio is multiplied down toward zero by the forced fade.
- On the first sample after `note_dur`, ADSR release computes from `env.s` (for BrassLead this is `0.7`).
- That can create an amplitude step (near-zero -> ~0.7-scaled signal), which is a classic click source.

Suggested fix direction:
- Do not apply the forced pre-release end fade when envelope mode already has release (ADSR/AR).
- Keep forced end fade only for no-envelope or AD mode.

## Secondary Findings

### 2) Voice state reset at each play event

Evidence:
- Oscillator phase reset per note/event: `src/aurora_core/renderer.cpp:1990`
- Filter integrator state reset per note/event: `src/aurora_core/renderer.cpp:1992`

Impact:
- Can produce transient discontinuities at note starts, especially in mono melodic lines with driven filter.

Potential follow-up:
- For true mono/legato policies, consider preserving selected state across tied/legato note transitions.

### 3) Legato expectation mismatch (`legato: true` but retrig default is `always`)

Evidence:
- Patch default `retrig = "always"`: `include/aurora/lang/ast.hpp:99`
- Legato/no-attack path only enabled under overlap with `retrig == "legato"` or `"never"`: `src/aurora_core/renderer.cpp:819`

Impact:
- `legato: true` alone may not produce expected non-retriggered envelope behavior.

Potential follow-up:
- Clarify semantics in docs and/or adjust defaults when `legato: true` is set.

### 4) Envelope appears in both CV path and final output multiply

Evidence:
- `env` routed to `vca.cv` in brass patch: `au/twinkle_epic.au:71`
- Renderer also multiplies by `env` at output stage: `src/aurora_core/renderer.cpp:2556`

Impact:
- Effectively squares/compounds envelope contribution in common VCA setups.
- Can exaggerate transition behavior and make click tuning less intuitive.

Potential follow-up:
- Revisit whether final `* env` should always apply, or only when no VCA/env route exists.

## Proposed Remediation Order

1. Fix envelope discontinuity (#1) first.
2. Re-render `au/twinkle_epic.au` and verify click reduction by ear/waveform.
3. If needed, investigate state continuity for mono/legato transitions (#2).
4. Review legato/retrig semantics and documentation (#3).
5. Reassess envelope application architecture (#4).

## Validation Plan (for when fixes are applied)

- A/B render `BrassLead` phrase before/after fix.
- Inspect sample continuity around each note boundary.
- Confirm no regression in non-brass patches (short percussive and sustained).
- Confirm deterministic render output for same seed/config.
