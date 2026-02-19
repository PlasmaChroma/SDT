# Aurora Spec: Control-Rate Modulation Smoothing (De-click)

## Goal

Eliminate note-on “sub-clicks” caused by **control-rate (block-held) modulation steps** (e.g., `env.out -> vca.cv` at `rate: control`) by applying a lightweight **audio-rate smoothing filter** at the moment control values are applied to audio-rate parameters.

This is an engine-level polish feature. No `.au` syntax changes required.

## Implementation Status (2026-02-19)

Completed (MVP):

* `vca.cv` smoothing implemented in `src/aurora_core/renderer.cpp`.
* Smoother is per-voice state (inside per-voice render path).
* Time constant constant set to `1.0 ms`.
* Applied only when `vca.cv` has control-rate modulation routes.
* First-sample behavior initializes to current value (no startup jump).

Current guardrail:

* Smoothing is enabled only when `vca.cv` is influenced by control-rate routes and **not** by audio-rate routes.

Deferred:

* Generic destination-param smoother map.
* Additional destination params (`filt.cutoff`, `pan.pos`, etc.).
* CLI/runtime override for smoothing time.
* Dedicated unit tests for smoother step response.

## Non-Goals

* Do **not** change envelope evaluation to audio-rate.
* Do **not** modify modulation routing semantics (`rate: control` remains block-held).
* Do **not** add new node types or require patch edits.
* Do **not** attempt to smooth audio-rate modulation routes.

---

## Definitions

### Control-rate modulation

A modulation connection with `rate: control` produces a value `x_block` updated once per render block and held constant for all samples in that block.

### Smoothed control value

We compute `y[n]` (per-sample) from block-held `x_block` using a 1-pole lowpass / exponential smoother:

**Difference equation**

```
y[n] = y[n-1] + a * (x_block - y[n-1])
```

**Coefficient from time constant τ**
Let `τ` be smoothing time in seconds.

```
a = 1 - exp(-1 / (τ * sample_rate))
```

Default τ: **1.0 ms** (tunable constant).

---

## Behavior Requirements

### R1. Apply smoothing only to control-rate modulation

* If a modulation connection is `rate: audio`, apply directly (no smoothing).
* If a connection is `rate: control`, apply smoothing **before** it affects the destination.

### R2. Smooth the *applied* modulation value, not the source

Smoothing is per **destination parameter**, after mapping/curve processing has produced the final numeric modulation contribution for that route.

### R3. Voice-local state

Smoothing must be **per voice per destination parameter** (because voices overlap and have independent modulation).

### R4. Reset policy

On voice start (note-on / voice allocation):

* initialize each smoother `y` to the current applied value (or `0` if unknown) to avoid jumps.
* minimal requirement: `y = x_block` on first sample of voice render.

On voice end:

* no special handling required beyond voice lifetime cleanup.

### R5. Default parameters

* `CONTROL_SMOOTH_MS = 1.0` ms
* Must behave consistently at any `--sr` (44.1k/48k/96k).

### R6. Negligible CPU

* Smoothing is one multiply-add per sample per smoothed destination param.
* Only instantiate smoothers for destinations that actually receive control-rate modulation.

---

## Implementation Design

### A. Add a generic 1-pole smoother utility

Create a small reusable struct/class:

**File**: `dsp/Smoother1p.h` (name flexible)

```cpp
struct Smoother1p {
  float a = 1.0f;      // coefficient
  float y = 0.0f;      // state
  bool inited = false;

  void setTimeSeconds(float tau, float sr) {
    // guard
    if (tau <= 0.0f) { a = 1.0f; return; }
    a = 1.0f - std::exp(-1.0f / (tau * sr));
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
  }

  void resetTo(float value) {
    y = value;
    inited = true;
  }

  inline float process(float x) {
    if (!inited) { y = x; inited = true; return y; }
    y += a * (x - y);
    return y;
  }
};
```

### B. Where smoothing lives

Smoothing should be applied at the point where the engine has:

1. resolved modulation routes for the current block (control routes),
2. computed the “applied modulation value” per destination parameter,
3. uses that value when rendering per-sample DSP.

**Recommended insertion point**

* Inside the per-voice render loop, right before using a resolved destination param that is influenced by control-rate modulation.

This ensures:

* control-rate remains block-held (x is constant during block),
* smoothing happens per sample (y evolves during the block).

### C. Data model: per-voice destination smoother map

For each voice, maintain a map keyed by “destination param id” (whatever internal representation you use for routed params; examples: enum, hashed string, pointer, etc.).

**Requirements**

* Only create smoothers for params that receive at least one `rate: control` modulation connection.
* If multiple control routes target the same destination param, combine them as usual (sum/add/mul/range logic) into one final `x_block`, then smooth once.

**Example keying strategy**

* If routed params already compile to an enum/ID: use that ID.
* If routed params are string paths (e.g., `vca.cv`, `filt.cutoff`): use a stable hash (e.g., FNV-1a 32-bit) at compile time and store as `uint32_t`.

### D. Control-route application workflow (per voice, per block)

At the start of each block (or once per block per voice):

1. Evaluate each control-rate route source (env/lfo/etc) at control rate (already done).
2. Apply map/curve processing → yield contribution value(s).
3. Resolve final destination param values for this block:

   * static base
   * automation (if block updated)
   * section set overrides (already resolved)
   * modulation contributions (control-rate ones included)
4. For each destination param that has control-rate modulation:

   * store `x_block` = resolved value (float)

Then in the per-sample loop:

* if destination has control-smoothing enabled:

  * `value = smoother.process(x_block)`
* else:

  * `value = x_block` (or direct per-sample computed value for audio-rate)

### E. Voice init (critical)

On voice allocation / note-on:

* For each smoothed destination param:

  * `smoother.resetTo(x_block)` before first sample is rendered
  * so there is no initial jump between uninitialized state and first computed value.

If `x_block` is not computed until block render begins:

* do a “precompute first block control resolves” before sample 0 of the voice, or
* initialize `y=0` and ensure the VCA multiply is applied from sample 0 (but this may still produce a tiny transient if x_block is non-zero). Prefer `resetTo(x_block)`.

---

## Parameters to Smooth (Recommended Defaults)

### Always smooth (if targeted by control-rate routes)

* `vca.cv`
* `vca.gain` (if modulated)
* `gain.gain` (if modulated)
* bus FX `delay.mix`, `reverb.mix` (optional but nice)

### Smooth if audible stepping is detected / optionally enabled

* `filt.cutoff` (good for sub-cleanliness; keeps plucks crisp if τ small)
* `pan.pos` (prevents zipper when modulated at control rate)
* `stereo_width.width`, `depth.distance`

**MVP scope**: `vca.cv` only.
This likely fixes the “sub-click” immediately with the lowest risk.

---

## Default Constant

Add a global/internal constant:

* `kControlModSmoothMs = 1.0f` (MVP)
* optionally allow override via CLI later (`--control-smooth-ms`), but not required for this change.

---

## Acceptance Criteria

### AC1: Twinkle sub-click eliminated

A patch that previously produced a click at note-on due to control-rate env→vca routing must render without audible click when smoothing is enabled.

### AC2: No audible “lag” on typical attacks

Strings/brass attacks should remain musically identical; only micro discontinuities removed.

### AC3: Determinism preserved

Given same seed and same `.au`, output must remain deterministic.

### AC4: Performance

No measurable performance regression beyond expected tiny overhead.

---

## Tests

### T1. Unit test: step response

Feed the smoother:

* x = 0 for 1 block worth of samples
* then x = 1 for next block
  Assert:
* y is monotonic increasing
* y[0] == 1 when resetTo(1) is used (voice-init case)
* or y approaches 1 with correct time constant (non-reset case)

### T2. Render test: single sine note repeated

Construct minimal `.au`:

* osc_sine into vca
* env_ad routed to vca.cv at control rate
* repeat short notes
  Measure:
* peak of high-frequency transient energy at note-on decreases significantly versus baseline
* (optional) ensure no DC offset introduced

### T3. Regression: audio-rate routes unchanged

A patch using `rate: audio` modulation must sound identical before/after.

---

## Rollout Plan (Minimal Risk)

1. Implement smoother utility.
2. Enable smoothing only for destinations `vca.cv` when control-modulated.
3. Validate twinkle sub stem: clicks gone.
4. Expand smoothing to a small allowlist if desired.

---

## Notes for Codex

* Look for the code path where control-rate modulation values are applied to per-sample DSP params.
* Ensure the **very first sample** of a voice uses a smoothed (or reset) value, not an uninitialized param.
* Prefer smoothing the **final resolved destination value** rather than smoothing each route separately.

---

If you want, I can also write a **tiny patch-level reproduction `.au`** that reliably demonstrates the click (pre-fix) and acts as a golden regression test (post-fix).
