# Aurora Spectrogram Composite (Option B) — Codex Spec v1.0

## Goal

During analysis (render `--analyze` or standalone `aurora analyze`), generate **one composite PNG** that vertically stacks spectrograms for:

1. `mix` (if present)
2. all `stems[]` (deterministic order)

Each row includes a **small header strip above the spectrogram** that contains a text label (stem name).

This reduces artifact sprawl and makes it easier to visually correlate events across stems.

---

## Non-Goals

* Changing the core STFT/spectrogram computation.
* Removing or renaming existing per-target spectrogram PNGs.
* Building a GUI viewer.

---

## CLI Surface

### New flags

Add these optional flags to both `aurora render ... --analyze` and `aurora analyze ...`:

* `--spectrogram-composite <mode>`

  * values: `none` (default), `stacked_headers`
* `--spectrogram-composite-out <dir>`

  * optional; defaults to the same directory as `--spectrogram-out` (or default spectrogram dir when omitted)

### Backward compatibility

* Default behavior remains unchanged: per-target spectrogram PNGs are generated as today unless `--nospectrogram` is set.
* Composite generation is **additive** and disabled by default.

---

## Output Artifacts

When `--spectrogram-composite stacked_headers` is enabled, write:

* `<spectrogram_dir>/composite.png`

Where:

* `spectrogram_dir` = `--spectrogram-composite-out` if set, else `--spectrogram-out` if set, else current default spectrogram directory.

---

## Analysis JSON Schema Changes

Add a new optional top-level object:

```json
"composite_spectrogram": {
  "enabled": true,
  "mode": "stacked_headers",
  "path": "spectrograms/composite.png",
  "targets": [
    {"kind":"mix","name":"Mix"},
    {"kind":"stem","name":"Underthump"},
    {"kind":"stem","name":"Vineglyphs"}
  ],
  "row_height_px": 256,
  "header_height_px": 24,
  "width_px": 1024,
  "freq_scale": "log",
  "colormap": "inferno",
  "error": null
}
```

### Rules

* `enabled`: true iff composite generation succeeded.
* `path`: relative to analysis output root (match existing `spectrogram.path` conventions).
* `targets`: ordered list describing the rows in the composite image.
* `error`: string populated on failure; composite failure must be **non-fatal** (analysis still succeeds).

---

## Deterministic Ordering

Composite row order is:

1. `Mix` row (only if mix is present in analysis)
2. Stems in the **same order** they appear in analysis JSON `stems[]` (which should already be deterministic)

---

## Composite Layout (Option B: Header per Row)

### Terminology

* `W` = composite spectrogram width in pixels (same as target spectrogram width)
* `S` = per-row spectrogram height in pixels (same as target spectrogram height)
* `H` = header bar height in pixels
* `N` = number of rows (mix + stems)
* `RowHeight = H + S`
* `TotalHeight = N * RowHeight`

### Canvas

Create a single RGBA image:

* width: `W`
* height: `TotalHeight`

### Per-row placement

For row index `i` in `[0..N-1]`:

* row top: `y0 = i * RowHeight`
* header rect: `y in [y0, y0 + H)`
* spectrogram rect: `y in [y0 + H, y0 + H + S)`

---

## Label Rendering

### Header background

Fill the header bar with a solid color (recommended):

* black (`#000000`) with alpha `255` (opaque)

Rationale: consistent readability regardless of colormap.

### Header text

Draw the label text in the header bar:

* label string examples:

  * `Mix`
  * `Stem: Underthump`
  * or simply `Underthump` (recommended minimal)

Text rendering rules:

* Left padding: `pad_x = 10px`
* Vertical alignment: center within header height
* Font: pick any bundled or system-available sans font (fallback to default raster font if needed)
* Font size: choose dynamically:

  * `font_px = clamp(H - 8, 10, 18)`
* Text color: white (`#FFFFFF`)

### Truncation

If the label exceeds available width:

* truncate and append ellipsis `…`
* Ensure deterministic truncation:

  * measure text width and shorten until it fits

---

## Spectrogram Image Source

Composite uses the already-generated per-target spectrogram images (or their in-memory buffers):

### Preferred approach (fastest, simplest)

1. Generate per-target spectrogram images exactly as today.
2. Load each image (or reuse buffer) for compositing.
3. Copy/paste into the correct row region under its header.

### Dimension requirement

All spectrograms included in a composite must have:

* identical width `W`
* identical height `S`

If not identical:

* attempt nearest-neighbor resize (or simple bilinear) to `W x S`
* record a warning-level log line
* do not fail analysis

---

## Config Interaction (`--spectrogram-config`)

Composite should inherit the resolved spectrogram config values used for per-target generation (width/height, log/linear, colormap, etc.).

### Composite-specific params (optional future extension)

Not required for v1.0, but keep code structured to allow later:

* `header_height_px`
* `header_bg_color`
* `header_text_color`
* `label_format`

---

## Failure Handling

Composite generation must be **non-fatal**.

On failure:

* set `analysis_json.composite_spectrogram.enabled = false`
* set `analysis_json.composite_spectrogram.error = "<message>"`
* continue writing standard analysis JSON and any per-target spectrograms

---

## Logging

Add one info log line on success:

* `Spectrogram composite written: <path> (rows=N, W x TotalHeight)`

Add warning logs on recoverable issues:

* missing per-target spectrogram for a stem
* resize mismatch
* label font fallback

---

## Implementation Checklist

1. **Parse CLI flags**

   * Add enums for `none` vs `stacked_headers`
2. **After per-target spectrogram generation**, build `targets[]`
3. **Allocate composite canvas**
4. For each target row:

   * draw header background
   * render label text
   * blit spectrogram image below header
5. Write `composite.png`
6. Update analysis JSON with `composite_spectrogram` object

---

## Test Plan

### Unit-ish tests

* Given N dummy images (solid colors), verify composite dimensions and placement.
* Verify truncation behavior deterministically.

### Integration tests

* Run `aurora analyze --stems ... --mix ... --spectrogram-composite stacked_headers`

  * Assert `composite.png` exists
  * Assert `analysis.json` contains `composite_spectrogram.path`
  * Assert `targets` order matches `mix` then stems order

### Visual smoke tests

* 1 stem + mix
* 12 stems + mix (scanability)
* Long stem names

---

## Notes on “Context Injection”

This format is intentionally optimized to be:

* human-readable
* “one-file” shareable
* row-labeled for unambiguous referencing in discussion

---

If you want, I can also draft the **exact C++ module boundaries** (e.g., `SpectrogramComposer.{h,cpp}` + minimal dependencies) so Codex lands in the right shape first try.
