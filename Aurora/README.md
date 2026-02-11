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
aurora render <file.au> [--seed N] [--sr 44100|48000|96000] [--out <dir>]
```

## Clean Rebuild

If CMake cache paths become stale (for example after moving the repo), remove the build directory and reconfigure:

```bash
rm -rf build-linux
cmake -S . -B build-linux
cmake --build build-linux -j
```
