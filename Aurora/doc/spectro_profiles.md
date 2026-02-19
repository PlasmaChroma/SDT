# Aurora Analyzer Spec

## Feature: Spectrogram Profiles (Preview Default)

---

## Phase 1 Status (Implemented on 2026-02-19)

Implemented:

* `--spectrogram-profile <preview|analysis|publication>` with default `preview`
* Explicit dimension overrides:
  * `--spectrogram-width <int>`
  * `--spectrogram-row-height <int>`
  * `--spectrogram-header-height <int>`
* Precedence implemented as:
  1. Explicit dimension flags
  2. `--spectrogram-config` values (for shared spectrogram fields)
  3. Profile defaults
  4. Default profile (`preview`)
* Profile metadata emitted in JSON under `composite_spectrogram.profile`
* Composite dimensions honor resolved profile/override values

Not yet implemented (deferred beyond Phase 1):

* `--spectrogram-format <png|jpg>` output switching
* JPEG encoding path (`--spectrogram-jpeg-quality`)
* Palette-indexed file-size acceptance assertions and benchmark gating

Current behavior notes:

* Output format remains PNG-only in this phase.
* Composite spectrogram remains default output in analysis mode; separate per-target files are opt-in via `--spectrogram-separate`.
* Indexed PNG encode path is implemented via `--spectrogram-indexed <true|false>`.
* Profile defaults for indexed output:
  * `preview`: indexed PNG enabled
  * `analysis`/`publication`: RGB PNG (indexed disabled)
* `composite_spectrogram` JSON includes `profile`, `format`, and `indexed_palette`.

---

## 1. Objective

Introduce profile-based spectrogram rendering to:

* Reduce PNG size and generation time during iteration
* Provide scalable output modes (preview / analysis / publication)
* Preserve deterministic rendering
* Maintain backward compatibility

Default profile: **`preview`**

---

## 2. Scope

Applies only to:

* Spectrogram PNG/JPG generation
* Composite spectrogram output

Does NOT affect:

* Spectral metrics computation
* JSON analysis fields
* DSP rendering
* Stem generation

---

## 3. CLI Interface

### New Flag

```
--spectrogram-profile <preview|analysis|publication>
```

Optional. Default = `preview`.

---

### Dimension Overrides (Optional)

```
--spectrogram-width <int>
--spectrogram-row-height <int>
--spectrogram-header-height <int>
--spectrogram-format <png|jpg>
--spectrogram-jpeg-quality <0-100>
--spectrogram-indexed <true|false>
```

---

## 4. Precedence Rules

When determining render configuration:

1. Explicit dimension/format flags override everything.
2. `--spectrogram-profile` sets base configuration.
3. If no profile specified → use `preview`.

Pseudo-code:

```cpp
config = profile_defaults("preview");

if (cli.profile_specified)
    config = profile_defaults(cli.profile);

if (cli.width_specified)
    config.width = cli.width;

if (cli.row_height_specified)
    config.row_height = cli.row_height;

...
```

---

## 5. Profile Definitions

### 5.1 Preview (DEFAULT)

Optimized for iteration speed and manageable file size.

| Parameter        | Value         |
| ---------------- | ------------- |
| width_px         | 1200          |
| row_height_px    | 280           |
| header_height_px | 60            |
| format           | png           |
| indexed_palette  | true          |
| png_compression  | 6             |
| freq_scale       | log           |
| bit_depth        | 8-bit indexed |

Expected composite size:

* ~2–5 MB typical

Purpose:

* Fast iteration
* Visual debugging
* Routine spectral inspection

---

### 5.2 Analysis

High-fidelity engineering inspection.

| Parameter        | Value      |
| ---------------- | ---------- |
| width_px         | 1600       |
| row_height_px    | 536        |
| header_height_px | 80         |
| format           | png        |
| indexed_palette  | false      |
| png_compression  | 6          |
| freq_scale       | log        |
| bit_depth        | 24-bit RGB |

Expected composite size:

* ~8–15 MB

Purpose:

* Detailed harmonic analysis
* Noise floor inspection
* Fine alias/transient detection

---

### 5.3 Publication

High-resolution export for documentation or presentation.

| Parameter        | Value      |
| ---------------- | ---------- |
| width_px         | 2400       |
| row_height_px    | 720        |
| header_height_px | 120        |
| format           | png        |
| indexed_palette  | false      |
| png_compression  | 9          |
| freq_scale       | log        |
| bit_depth        | 24-bit RGB |

Expected composite size:

* 15–30 MB+

Purpose:

* Whitepapers
* Marketing visuals
* High-res zooms

---

## 6. Indexed PNG Implementation (Preview Mode)

If `indexed_palette == true`:

1. Render full float spectrogram.
2. Map to fixed 256-color magma LUT.
3. Output 8-bit indexed PNG.
4. Disable alpha channel.

This reduces file size significantly.

---

## 7. JPEG Option

If:

```
--spectrogram-format jpg
```

Then:

* Encode as 4:2:0 subsampled JPEG
* Default quality = 85 (unless overridden)
* Ignore indexed_palette setting

Use case:

* Lightweight sharing
* Non-archival viewing

---

## 8. JSON Metadata Update

Add to analysis.json:

```json
"spectrogram": {
  "profile": "preview",
  "width_px": 1200,
  "row_height_px": 280,
  "format": "png",
  "indexed_palette": true
}
```

This preserves reproducibility.

---

## 9. Determinism Requirements

* Same input + same profile must produce identical pixel output.
* LUT mapping must be fixed.
* No adaptive compression heuristics allowed.

---

## 10. Acceptance Criteria

### AC1

No CLI flags → preview profile used.

### AC2

`--spectrogram-profile analysis` produces current high-res behavior.

### AC3

Explicit dimension flags override profile defaults.

### AC4

Indexed PNG in preview reduces file size by at least 40% relative to analysis profile.

---

## 11. Backward Compatibility

If user previously relied on high-res default behavior:

They must now explicitly specify:

```
--spectrogram-profile analysis
```

This is considered acceptable because:

* Behavior is deterministic
* No breaking changes to JSON schema
* Only default resolution changed

---

## 12. Recommended Implementation Order

1. Add profile config struct.
2. Add CLI parsing.
3. Implement profile default table.
4. Implement indexed PNG path.
5. Update JSON metadata.
6. Regression test composite generation.

---

# Strategic Note

This is not about constraints.

It’s about iteration velocity.

You are now generating large-scale multi-stem spectral diagnostics routinely. Making preview the default keeps Aurora agile.

---

If you’d like next, we can:

* Add an automatic file size estimator before write
* Or design a `--spectrogram-diff` mode for A/B comparison overlays
* Or pivot back into orchestration refinement

Where shall we steer the engine next?
