# Aurora Integrated Audio Analyzer

**Specification Version:** 1.0
**Status:** Engineering Design Draft
**Target Integration:** Aurora CLI Synth Engine
**Scope:** Per-stem + master mix analysis with JSON output

---

# 1. Overview

Aurora shall include an integrated Audio Analysis subsystem capable of:

1. Analyzing rendered stems during synthesis.
2. Analyzing the final mix output.
3. Running in standalone analysis mode for external audio files.
4. Producing deterministic JSON reports suitable for:

   * Iterative synthesis refinement
   * CI-style verification
   * AI-assisted evaluation
   * Production QA

---

# 2. Operating Modes

Aurora shall support three analysis modes:

---

## 2.1 Post-Render Integrated Analysis Mode

```
aurora render track.au --analyze
```

Behavior:

* Render all stems
* Render final mix
* Automatically analyze:

  * Each stem independently
  * Final stereo mix
* Output:

  * `analysis.json`
  * Optional console summary

---

## 2.2 Standalone Audio Analysis Mode

```
aurora analyze input.wav
aurora analyze input.flac
aurora analyze input.mp3
```

Behavior:

* Skip synthesis
* Load audio file
* Perform full mix analysis
* Output JSON report

---

## 2.3 Hybrid Stem Analysis Mode

```
aurora analyze --stems stem1.wav stem2.wav mix.wav
```

Behavior:

* Analyze multiple files
* Treat last file as mix (optional flag)
* Produce combined structured report

---

# 3. Supported Input Formats

Required:

* WAV (PCM 16/24/32 bit)
* FLAC

Optional (v1.1+):

* MP3 (decode to float internally)
* AIFF

Internally:

* All audio converted to 32-bit float
* Preserve native sample rate
* Optional resample flag

---

# 4. Analysis Scope

## 4.1 Global Metrics (All Files)

Each analyzed file shall include:

### Loudness & Dynamics

* Integrated LUFS
* Short-term LUFS (windowed average)
* True Peak (dBTP)
* RMS
* Crest Factor
* Loudness Range (LRA)

### Spectral Distribution (Energy Ratios)

Compute FFT with windowed overlap (e.g., 2048 or configurable).

Report band energy percentages:

| Band     | Range       |
| -------- | ----------- |
| Sub      | <60 Hz      |
| Low      | 60–200 Hz   |
| Low-Mid  | 200–500 Hz  |
| Mid      | 500–2000 Hz |
| Presence | 2–5 kHz     |
| High     | 5–10 kHz    |
| Air      | 10–16 kHz   |
| Ultra    | 16 kHz+     |

Also compute:

* Spectral centroid (mean)
* Spectral centroid variance
* Spectral rolloff (85%)
* Spectral flatness

---

## 4.2 Transient & Density Metrics

Using onset detection:

* Transients per minute
* Average transient strength
* Transient variance
* Silence percentage (< -50 dB threshold)

---

## 4.3 Stereo Metrics (Stereo Files Only)

* Mid energy
* Side energy
* Mid/Side ratio
* Overall correlation coefficient
* Low-frequency correlation (<200 Hz)
* Side energy in high bands

---

## 4.4 Sub-Focused Metrics

Critical for Aurora’s sub-centric architecture.

* Sub RMS (<60 Hz)
* Sub crest factor
* Sub-to-total ratio
* Low-to-sub ratio
* Low-frequency phase coherence

---

# 5. Per-Stem Analysis Requirements

When running post-render:

Each stem must include:

* All global metrics
* Duration
* Peak level
* Relative loudness compared to mix

Additionally:

* Contribution ratio (stem RMS / mix RMS)
* Frequency dominance profile

Example:

* “Stem: Underthump contributes 14% of total energy, 62% of sub energy”

---

# 6. Output Format (JSON Schema)

Top-level structure:

```json
{
  "aurora_version": "1.0.0",
  "analysis_version": "1.0",
  "timestamp": "ISO8601",
  "sample_rate": 48000,
  "mode": "render_analysis",
  "mix": { ... },
  "stems": [
    { ... },
    { ... }
  ]
}
```

---

## 6.1 Mix Object

```json
"mix": {
  "duration_seconds": 345.21,
  "loudness": {
    "integrated_lufs": -18.3,
    "true_peak_db": -1.1,
    "lra": 6.4,
    "crest_factor": 12.1
  },
  "spectral_ratios": {
    "sub": 0.14,
    "low": 0.22,
    "low_mid": 0.18,
    "presence": 0.08,
    "air": 0.05
  },
  "stereo": {
    "correlation": 0.12,
    "mid_side_ratio": 0.61
  },
  "transients_per_minute": 42
}
```

---

## 6.2 Stem Object

```json
{
  "name": "Underthump",
  "duration_seconds": 345.21,
  "rms": -20.1,
  "peak_db": -3.2,
  "energy_contribution_ratio": 0.14,
  "sub_contribution_ratio": 0.62,
  "spectral_ratios": { ... },
  "transients_per_minute": 12
}
```

---

# 7. Intent Preset Support (Optional v1)

CLI flag:

```
--intent sleep
--intent ritual
--intent dub
```

Preset file:

```
intent_sleep.json
intent_ritual.json
```

Aurora compares metrics against target ranges and outputs:

```json
"intent_evaluation": {
  "status": "out_of_range",
  "notes": [
    "Transient density high for sleep",
    "Presence band elevated"
  ]
}
```

---

# 8. Performance Requirements

* Analyze 10-minute stereo file under 8 seconds (modern CPU)
* Memory < 500MB
* Multithread FFT permitted
* Deterministic output (no randomness)

---

# 9. Architectural Integration

Analyzer shall be implemented as:

```
Aurora::Analyzer
```

Submodules:

* Analyzer::Loudness
* Analyzer::Spectral
* Analyzer::Transient
* Analyzer::Stereo
* Analyzer::SubBand

It should operate on internal render buffers directly when post-render mode is enabled to avoid reloading from disk.

---

# 10. Error Handling

Must gracefully handle:

* Non-audio files
* Corrupt files
* Unsupported bit depths
* Mono files (disable stereo metrics)

---

# 11. Extensibility Roadmap (v1.1+)

* Harmonic tracking
* Key detection
* Reference track comparison mode
* Heatmap export
* Stem-to-stem interference detection
* GUI visualization
* Listener fatigue modeling

---

# 12. Determinism & Verification

* FFT window size must be fixed unless overridden.
* All floating-point operations must use consistent precision.
* JSON keys must remain stable across versions.

---

# 13. Design Philosophy

Aurora’s analyzer is:

* A verification instrument
* A structural feedback engine
* A synthesis refinement assistant

It does not interpret aesthetic meaning.
It measures acoustic structure.

