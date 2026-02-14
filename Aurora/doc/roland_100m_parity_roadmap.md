# Aurora Roland 100M Parity Roadmap

## Goal
Reach practical feature parity with a classic Roland 100M-style modular workflow inside Aurora, while preserving Aurora's deterministic text-first/offline-render strengths.

## Scope Definition
"Parity" here means:
1. Equivalent patch-building power for core subtractive/modular synthesis tasks.
2. Equivalent control-patching flexibility (CV-style routing, scaling, inversion, modulation combinations).
3. Equivalent voice behavior for gate/trigger/envelope-driven articulation.
4. Musically comparable behavior for oscillator/filter/VCA response under modulation.

Out of scope for strict parity:
1. Physical hardware ergonomics and panel workflow.
2. Bit-identical analog circuit behavior.
3. External CV/gate hardware I/O (until a later integration phase).

## Current Baseline (Aurora)
Implemented/highly usable now:
1. Oscillators, filter, gain, ADSR envelope.
2. LFO as modulation source.
3. Explicit `vca` node.
4. Control modulation routing via `graph.connect` with `map` (`type|min|max|scale|offset|invert|bias`).
5. Event params + automation + static param precedence.
6. Sequencing (`play`, `trigger`, `gate`, `seq`), section directives, crossfade, binaural support.
7. Initial CV utility nodes (`cv_scale`, `cv_offset`, `cv_mix`, `cv_slew`).

Major gaps to close:
1. Full control utility module set (attenuverters, offset/scalers, combiners, S&H, logic).
2. Full audio/control rate semantics across all nodes and routes.
3. Robust graph execution model for arbitrary modular-style patching and feedback-safe topologies.
4. More complete modulation source behaviors (multiple envelope types, trigger tools, random).
5. Deeper analog-style behavior controls and characterization modes.
6. Real-time preview/patch-debug tooling.

## Architecture Changes Required
## A. Graph + Runtime Model
1. Add explicit per-port typing metadata: `audio`, `control`, `gate`, `trigger`.
2. Build graph compiler that:
- validates legal connections per port type,
- inserts coercions (`gate->control`, mono/stereo adapters),
- determines sample/block update strategy per route.
3. Add deterministic feedback handling strategy:
- one-sample delay insertion on feedback loops, or
- explicit cycle-break node with clear language semantics.

## B. Modulation System
1. Extend `connect.map` to include:
- `invert`, `clip`, `curve`, `slew`, `bias`.
2. Add explicit modulation math nodes:
- `cv_scale`, `cv_offset`, `cv_mix`, `cv_invert`, `cv_clip`, `cv_slew`.
3. Add modulation destination coverage for all significant node params.

## C. Voice/Gate Model
1. Introduce canonical voice lifecycle states (`idle`, `attack`, `decay`, `sustain`, `release`).
2. Support gate/trigger semantics independently of note pitch.
3. Implement mono priority + retrig modes + legato policy.

## D. DSP Coverage
1. Expand node catalog for practical 100M-style patch design:
- utility mixers/attenuators,
- sample-and-hold,
- ring modulation,
- comparator/logic utilities,
- additional envelope variants.
2. Add optional analog-character layer:
- drift/jitter controls,
- saturation points,
- calibrated filter self-oscillation behavior.

## E. Tooling
1. Add graph diagnostics:
- route validation errors with source location,
- unresolved/unconnected port warnings,
- cycle warnings with deterministic resolution notes.
2. Add render diagnostics:
- modulation activity traces,
- per-node level summaries.
3. Add preview path (later phase): low-latency audition mode with deterministic snapshot export.

## Milestones
## M1: CV/Gate Foundation (Parity Prerequisite)
Target: reliable modular control semantics.

Status: `complete`

Deliverables:
1. Port typing and connection validator.
2. `gate`/`trigger` event semantics in engine.
3. Core CV utility nodes (`cv_scale`, `cv_offset`, `cv_mix`, `cv_slew`, `cv_clip`).
4. Route-level mapping extensions (`invert`, `bias`, `curve`).

Implementation progress:
- Completed:
1. Internal port-kind classification and connection routing structures in renderer.
2. `gate`/`trigger` section events parsed and expanded to play-like events.
3. CV utility nodes implemented: `cv_scale`, `cv_offset`, `cv_mix`, `cv_slew`, `cv_clip`.
4. Route-level mapping extensions implemented: `invert`, `bias`, `curve`.
5. Graph validation now enforces basic port-type legality for known node classes.
6. M1 phase-gate harness added: `tests/run_m1_tests.sh` (positive renders, negative validation, determinism hash check).
- Remaining for M1:
1. None.

Done criteria:
1. Can patch envelope + LFO + scaled offset into one destination without hacks.
2. Can trigger envelopes without pitch events.
3. Deterministic behavior across repeated renders.

## M2: Voice + Envelope Parity
Target: articulation parity for classic modular patching.

Status: `complete`

Deliverables:
1. Mono/legato/retrig voice policies.
2. Additional envelope modes (`env_ad`, `env_ar`, retrigger options).
3. Clear gate release behavior for all envelope-driven nodes.

Implementation progress:
- Completed:
1. Patch-level voice policy fields added: `mono`, `legato`, `retrig`.
2. Mono overlap serialization implemented for monophonic patches.
3. `retrig: legato` overlap path implemented (attack suppression on transitions).
4. Envelope modes implemented: `env_ad`, `env_ar` (alongside `env_adsr`).
5. Release-tail rendering implemented and timeline length now accounts for envelope tails.
6. M2 phase-gate harness added: `tests/run_m2_tests.sh` (M2 feature tests + M1 regression + determinism).
7. Mono overlap priority modes implemented via `voice_steal` (`first|highest|lowest|last/default`).
8. `retrig: never` mode implemented for mono transitions.
- Remaining for M2:
1. None.

Done criteria:
1. Equivalent patch outcomes for common bass/lead/pluck/paraphonic patterns.
2. No hidden coupling between note scheduler and control envelopes.

## M3: Audio/Control Rate Unification
Target: credible deep modulation patching.

Deliverables:
1. Per-route execution rate support (`audio` vs `control`) with predictable CPU policy.
2. Audio-rate modulation for selected high-impact params (osc freq/PW, filter cutoff/Q, VCA CV).
3. Feedback-safe routing strategy fully documented and enforced.

Done criteria:
1. Cross-mod and FM-like patches are stable and reproducible.
2. Graph compiler reports illegal or unstable configurations clearly.

## M4: Module Catalog Expansion
Target: practical module parity set.

Deliverables:
1. Add missing utility and modulation processors.
2. Add ring modulation and sample-and-hold family.
3. Extend filter and VCA options where musically relevant.

Done criteria:
1. A reference bank of classic modular patch archetypes can be authored 1:1 in Aurora graph DSL.

## M5: Character + Workflow
Target: production-ready parity experience.

Deliverables:
1. Optional analog character profiles (`clean`, `vintage_light`, `vintage_heavy`).
2. Graph and modulation diagnostics in CLI output/metadata.
3. Preview mode for fast iteration.

Done criteria:
1. Patch design workflow speed is acceptable without external DAW workaround.
2. Render artifacts include enough metadata to debug complex modulation patches.

## Language/DOC Changes Needed
1. Update `doc/au_language_spec.md`:
- port typing grammar/semantics,
- gate/trigger event forms,
- expanded `connect.map` schema,
- new utility node specs.
2. Update `doc/aurora_requirements_v_1.md` (or v1.1 doc):
- deterministic rules for mixed-rate execution,
- feedback semantics,
- voice policy contracts.
3. Add a dedicated examples suite for parity patterns:
- envelope-controlled VCA pluck,
- S&H to filter,
- ring-mod percussion,
- mixed-source CV modulation chain.

## Testing Strategy
1. Golden audio tests for archetype patches (hash/tolerance based).
2. Structural graph tests for connection validation and cycle handling.
3. Determinism tests:
- same seed/version/config => same output.
4. Performance tests:
- CPU ceilings for dense modulation patches.

## Recommended Execution Order (Immediate)
1. Start M1 immediately (highest leverage).
2. Then M2 (stabilizes musical articulation semantics).
3. Then M3 (enables deeper modular patch parity).
4. Parallelize M4 module expansion once M1/M2 APIs are stable.
5. M5 after parity-critical engine behavior is locked.

## First Implementation Slice (Next Sprint)
1. Add port typing to internal graph compiler structures.
2. Implement `cv_scale`, `cv_offset`, `cv_mix`, `cv_slew` nodes.
3. Add trigger/gate event injection path in score expansion.
4. Extend `connect.map` with `invert` + `bias`.
5. Add 3 regression `.au` tests proving CV chain correctness.
