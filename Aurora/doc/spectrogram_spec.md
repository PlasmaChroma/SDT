# Aurora Analyzer — Spectrogram Artifact (v1)

## Goal

Add an optional analysis artifact that produces an Audacity-like **colored spectrogram expanding over time** for:

* each rendered stem
* the final mix

The artifact must be deterministic and written to disk alongside the existing analysis JSON workflow.

---

## CLI Additions

### Render mode

Existing:

* `aurora render <file.au> ... --analyze --analysis-out <path> --analyze-threads N`

Add flags:

* `--nospectrogram` (bool flag)
  If set, do not generate spectrogram images for analyzed targets. Otherwise always do in `--analyze`.
* `--spectrogram-out <dir>` (string, optional)
  Directory where images will be written.
* `--spectrogram-config <json>` (optional inline JSON string)
  Overrides default spectrogram parameters.

### Standalone analyze modes

Existing:

* `aurora analyze <input.wav|...> ... [--out <analysis.json>] ...`
* `aurora analyze --stems <stem1.wav> ... [--mix <mix.wav>] [--out <analysis.json>] ...`

Add the same `--nospectrogram`, `--spectrogram-out`, `--spectrogram-config`.

Default resolution rules for `--spectrogram-out`:

* In `render --analyze`: default to `<resolved meta dir>/spectrograms`.
* In `analyze`: default to `<dirname(resolved --out)>/spectrograms`.
* If `--spectrogram-out` is provided, use it exactly.

Path base rule:

* `analysis root` means the parent directory of the final analysis JSON file path.

---

## Outputs

For each analyzed target (stem or mix), write PNG files:

* `<spectrogram-out>/<target_name>.spectrogram.png`

Where `target_name` is:

* stem name if analyzing stems
* `"mix"` for the mix file

Target naming normalization:

* Use a filesystem-safe name for `target_name`.
* Replace path separators and control characters with `_`.
* If empty after normalization, use `unnamed`.

If a target is stereo and `mode="mixdown"`, produce one image.
If stereo and `mode="channels"`, produce:

* `<target_name>.L.spectrogram.png`
* `<target_name>.R.spectrogram.png`

Filename and path rules:

* v1 output format is PNG only.
* File extension is always `.png`.
* If a file already exists at the output path, overwrite atomically if possible.

---

## JSON Report Additions

Add `spectrogram` inside each per-target analysis object:

* `mix.spectrogram`
* `stems[i].spectrogram`

Example:

```json
"spectrogram": {
  "enabled": true,
  "path": "meta/spectrograms/<target>.spectrogram.png",
  "mode": "mixdown",
  "sr": 48000,
  "window": 2048,
  "hop": 512,
  "nfft": 2048,
  "freq_scale": "log",
  "min_hz": 20.0,
  "max_hz": 20000.0,
  "db_min": -90.0,
  "db_max": 0.0,
  "colormap": "magma",
  "width_px": 1600,
  "height_px": 512
}
```

* If disabled, omit `spectrogram` or set `"enabled": false`.
* `path` is relative to analysis root (prefer relative paths for portability).
* `path` must match the actual emitted filename including `.png`.
* If generation fails after analysis succeeded, set `"enabled": false` and include `"error": "<message>"`.

---

## Default Spectrogram Parameters

Use these defaults unless overridden by `--spectrogram-config`:

* `window`: 2048 samples
* `hop`: 512 samples
* `nfft`: 2048 (must be power-of-two and >= window)
* `window_fn`: Hann
* `mode`: `"mixdown"` (stereo -> mono by `(L+R)/2`)
* `freq_scale`: `"log"` (Audacity-like readability)
* `min_hz`: 20.0
* `max_hz`: `min(20000.0, 0.49 * sr)`
* `db_min`: -90.0
* `db_max`: 0.0
* `colormap`: `"magma"` (acceptable alternatives: `"inferno"`, `"viridis"`, `"plasma"`)
* `width_px`: 1600 (time axis)
* `height_px`: 512 (frequency axis)
* `gamma`: 1.0 (post-normalization power curve; optional)
* `smoothing_bins`: 0 (optional integer; if >0, apply small vertical blur in frequency)

`--spectrogram-config` accepted JSON keys (all optional, object at top-level):

* `window` (int)
* `hop` (int)
* `nfft` (int)
* `mode` (`"mixdown"` | `"channels"`)
* `freq_scale` (`"log"` | `"linear"`)
* `min_hz` (number)
* `max_hz` (number)
* `db_min` (number)
* `db_max` (number)
* `colormap` (`"magma"` | `"inferno"` | `"viridis"` | `"plasma"`)
* `width_px` (int)
* `height_px` (int)
* `gamma` (number)
* `smoothing_bins` (int)

Parsing and override rules:

* Unknown keys: validation error (fail fast before analysis starts).
* Wrong JSON type for a known key: validation error.
* Missing keys use defaults.
* `null` values are invalid (must provide concrete typed values).
* Precedence is: hardcoded defaults < `--spectrogram-config` values.

Rationale:

* `window/hop` gives decent time resolution without exploding compute.
* Log frequency is more musically useful for stems.

---

## Spectrogram Algorithm

### 1) Load / Normalize Input

Input: floating samples in `[-1, +1]` (whatever the analyzer currently uses).

If stereo:

* if `mode == "mixdown"`: `x[n] = 0.5*(L[n] + R[n])`
* if `mode == "channels"`: process each channel independently

No peak normalization is required (this is analysis, not presentation), but do clamp to finite.

### 2) Frame + Window

Let:

* `W = window`
* `H = hop`
* `F = nfft`
* `num_frames = 1 + floor((N - W)/H)` if `N >= W` else 1 (pad)

For each frame `t`:

* copy `W` samples starting at `t*H`
* zero-pad to length `F`
* apply Hann window to first `W` samples:

  * `w[i] = 0.5 - 0.5*cos(2*pi*i/(W-1))`

### 3) FFT + Magnitude

Compute FFT for each frame.
Use only bins `[0..F/2]` (inclusive).
Magnitude:

* `mag[k] = sqrt(re^2 + im^2)`

Convert to dB:

* `db = 20 * log10(mag + eps)`
* `eps = 1e-12`

### 4) Frequency Axis Mapping (linear or log)

We map the spectrum into `height_px` rows.

Fixed orientation requirement:

* image top is high frequency
* image bottom is low frequency

#### Linear

For logical low->high row `y` in `[0, height_px-1]`:

* `k = round(y * (F/2) / (height_px-1))`
* write to image row `yy = (height_px - 1) - y`

#### Log

Let `f(k) = k * sr / F`.

For logical low->high row `y`:

* `alpha = y / (height_px-1)`
* `fy = min_hz * (max_hz/min_hz) ^ alpha`
* `kf = fy * F / sr`
* `k0 = floor(kf)`, `k1 = min(k0 + 1, F/2)`
* `v = lerp(mag[k0], mag[k1], frac)` (interpolate in linear magnitude domain)
* convert `v` to dB after interpolation
* write to image row `yy = (height_px - 1) - y`

### 5) Time Axis Resampling to width_px

We have `num_frames` time slices; output image width is `width_px`.

For each x pixel:

* `tf = x * (num_frames-1) / (width_px-1)`
* sample frame index via linear interpolation between nearest frames.

This ensures stable image size even for very short/long audio.

### 6) dB Clamp + Normalize + Gamma

For each pixel value `db`:

* clamp: `db = clamp(db, db_min, db_max)`
* normalize: `u = (db - db_min) / (db_max - db_min)` in `[0,1]`
* gamma (optional): `u = pow(u, 1/gamma)` if gamma != 1

### 7) Colormap

Map `u in [0,1]` to RGB using a fixed colormap LUT (256 entries).

* Provide LUTs for at least `magma`, `inferno`, `viridis`, `plasma`.
* Determinism requirement: LUT values must be hardcoded (no platform-dependent color ops).

### 8) Write PNG

Write 8-bit RGB PNG:

* width = `width_px`
* height = `height_px`
* origin: top-left
* fixed orientation: time left->right, frequency bottom->top

---

## Performance Requirements

* Must not increase analyzer wall-clock time by more than:

  * ~1x baseline analysis for short stems (<30s)
  * ~2x baseline analysis for long stems (~5 min)
    (These are targets; enforce via sane defaults.)

* Honor `--analyze-threads N` as the maximum number of concurrent target jobs. Do **not** spawn additional uncontrolled thread pools inside each job.

---

## Error Handling

* If audio is shorter than one window, pad to one frame.
* Validation errors (hard fail for spectrogram generation request):

  * `window < 2`
  * `hop < 1`
  * `nfft < window` or `nfft` not power-of-two
  * `min_hz <= 0` or `max_hz <= min_hz`
  * `max_hz > 0.49 * sr` (reject, do not silently clamp for explicit config)
  * `db_max <= db_min`
  * `width_px < 2` or `height_px < 2`
  * `gamma <= 0`
  * `smoothing_bins < 0`
  * invalid enum values for `mode`, `freq_scale`, `colormap`
* If PNG writing fails: emit a clear error and continue analysis JSON generation (spectrogram entry omitted or `"enabled": false` with `"error": "..."`).

Failure behavior contract:

* Invalid spectrogram configuration is a command error and returns non-zero.
* Runtime image-generation failures are non-fatal to core analysis metrics and JSON output.

---

## Test Plan

1. **Determinism Test**

   * Same input, same config -> identical PNG bytes (or identical pixel hash).
2. **Sanity Visual**

   * 440 Hz sine should produce a horizontal line near 440 Hz.
3. **Stereo Mode**

   * L=440 Hz, R=880 Hz:

     * mixdown shows both lines
     * channels mode produces separate images.
4. **Scaling**

   * Compare `freq_scale=linear` vs `log` visually.
5. **Performance**

   * 60s noise file: ensure runtime remains within expected bounds.

---

## Notes / Integration Context

* The analyzer already writes deterministic JSON and supports threaded stem analysis via `--analyze-threads`. This feature extends that pipeline with an image artifact per target.
* `.au` language spec is unaffected; this is purely an analyzer artifact.

---

If you want the “Audacity feel” even harder, the two biggest knobs are:

* **log frequency scale** + good `db_min/db_max` defaults
* a pleasing colormap (magma/inferno are close to what many audio tools gravitate toward)

And if you want it to read *musically* for your stems: consider a future add-on “note grid overlay” (C notes as faint horizontal lines). But the above spec keeps v1 implementable in one clean pass.
