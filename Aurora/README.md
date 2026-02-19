# Aurora

Aurora is a C++20 command-line renderer for `.au` arrangement files.

## Prerequisites

- CMake 3.20+
- A C++20 compiler
  - GCC 12+ or Clang 14+ recommended
- Make or Ninja (examples below use default CMake generator)

## Build

From the repository root:

```bash
cmake -S . -B build-linux
cmake --build build-linux -j
```

This produces the CLI binary at:

```bash
./build-linux/src/aurora_cli/aurora
```

## Run

Render an example file:

```bash
./build-linux/src/aurora_cli/aurora render examples/canonical_v1.au
```

Render to a custom output root:

```bash
./build-linux/src/aurora_cli/aurora render examples/canonical_v1.au --out /tmp/aurora-out
```

With `--out`, artifacts are written directly under that directory (for example `stems/`, `midi/`, `mix/`, `meta/`).

## CLI Usage

```text
aurora render <file.au> [--seed N] [--sr 44100|48000|96000] [--out <dir>] [--analyze] [--analysis-out <path>] [--analyze-threads N] [--intent sleep|ritual|dub] [--nospectrogram] [--spectrogram-out <dir>] [--spectrogram-config <json>]
aurora analyze <input.wav|input.flac|input.mp3|input.aiff> [--out <analysis.json>] [--analyze-threads N] [--intent sleep|ritual|dub] [--nospectrogram] [--spectrogram-out <dir>] [--spectrogram-config <json>]
aurora analyze --stems <stem1.wav> <stem2.wav> ... [--mix <mix.wav>] [--out <analysis.json>] [--analyze-threads N] [--intent sleep|ritual|dub] [--nospectrogram] [--spectrogram-out <dir>] [--spectrogram-config <json>]
```

## Namespaced Imports (Phase 1)

Aurora render supports patch imports with aliases:

```au
imports {
  use "./lib_patch.au" as lib
}
```

Imported patches are referenced as `alias.PatchName` in events and patch targets (for example `play lib.ImportedLead { ... }` and `automate patch.lib.ImportedLead.env.a linear { ... }`).

Patch graph includes a `comb` node with core params:
- `time` (unit time literal)
- `fb` (feedback, clamped to [-0.99, 0.99])
- `mix` (dry/wet)
- `damp` (feedback damping, [0,1])

Analysis reports are written as deterministic JSON. In render mode with `--analyze`, Aurora writes `analysis.json` under `meta/` by default.
`--analyze-threads N` sets the maximum concurrent stem-analysis jobs (`N >= 1`).

Spectrogram artifacts (PNG) are enabled by default during analysis:
- Disable with `--nospectrogram`
- Default output directory:
  - `render --analyze`: `<meta>/spectrograms`
  - `analyze`: `<dirname(--out)>/spectrograms` (or `./spectrograms` when `--out` is omitted)
- Override output directory with `--spectrogram-out <dir>`
- Override rendering settings with inline JSON via `--spectrogram-config`, for example:

```bash
./build-linux/src/aurora_cli/aurora analyze input.wav \
  --spectrogram-config '{"width_px":1024,"height_px":256,"freq_scale":"log","colormap":"inferno"}'
```

Per-target analysis JSON now includes a `spectrogram` object (for `mix` and each entry in `stems`) with:
- `enabled` (bool)
- `path` (relative PNG path)
- `paths` (array; populated when channel mode emits multiple files)
- resolved config fields (`mode`, `window`, `hop`, `nfft`, `freq_scale`, `min_hz`, `max_hz`, `db_min`, `db_max`, `colormap`, `width_px`, `height_px`, `gamma`, `smoothing_bins`)
- `error` when generation fails non-fatally

Spectrogram generation runs in bounded parallel target jobs and honors `--analyze-threads N` as the maximum concurrent target count.

Current standalone `analyze` input support in this build:
- WAV (PCM 16/24/32-bit and 32-bit float)
- FLAC
- MP3
- AIFF

## Clean Rebuild

If CMake cache paths become stale (for example after moving the repo), remove the build directory and reconfigure:

```bash
rm -rf build-linux
cmake -S . -B build-linux
cmake --build build-linux -j
```
