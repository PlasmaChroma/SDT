# Aurora Code Review Report (2026-02-14)

## Scope
- Reviewed core runtime and language pipeline: `src/aurora_core/renderer.cpp`, `src/aurora_lang/parser.cpp`, `src/aurora_lang/validation.cpp`, `src/aurora_cli/main.cpp`, `src/aurora_io/*.cpp`.
- Reviewed current shell test harnesses: `tests/run_m1_tests.sh`, `tests/run_m2_tests.sh`, `tests/run_m3_tests.sh`, `tests/run_m4_tests.sh`.
- Focused on correctness, stability, and regression risk over style.

## Findings (Ordered by Severity)

### 1) `std::async` captures a loop reference with invalid lifetime in CLI output writers
- Severity: Critical
- Location: `src/aurora_cli/main.cpp:191`, `src/aurora_cli/main.cpp:193`, `src/aurora_cli/main.cpp:202`, `src/aurora_cli/main.cpp:203`
- Issue:
  - Lambdas capture `&stem` inside range-based loops:
    - `for (const auto& stem : rendered.patch_stems) { ... [path, &stem, ...] ... }`
    - `for (const auto& stem : rendered.bus_stems) { ... [path, &stem, ...] ... }`
  - `stem` is the loop variable; capturing it by reference in async work introduces dangling-reference UB once iteration advances/ends.
- Impact:
  - Nondeterministic behavior during output writes (wrong stem written, corrupt data, sporadic crashes).
- Recommendation:
  - Capture stable pointers/references to container elements (`const aurora::core::AudioStem* stem_ptr = &stem;`) and use `stem_ptr` by value in lambda capture.
  - Add a regression test that writes multiple stems and verifies their hashes differ/stay stable across runs.

### 2) `std::async` captures loop reference `patch` in render worker dispatch
- Severity: Critical
- Location: `src/aurora_core/renderer.cpp:2489`, `src/aurora_core/renderer.cpp:2499`
- Issue:
  - Render workers are launched with lambda capture `&patch` inside `for (const auto& patch : file.patches)`.
  - This is the same lifetime hazard pattern as above.
- Impact:
  - Undefined behavior in concurrent patch rendering path.
  - Can manifest as missing/misrouted patch audio or intermittent crashes under load.
- Recommendation:
  - Capture a stable value (`const std::string patch_name = patch.name;`) and use it in worker lookup.
  - Keep map references (`patch_programs`, `patch_buffers`, `expanded`) but do not capture loop variables by reference.

### 3) `globals.sr` is not validated; negative/zero sample rate can cause pathological behavior
- Severity: High
- Location: `src/aurora_lang/parser.cpp:1046`, `src/aurora_core/renderer.cpp:2397`, `src/aurora_core/renderer.cpp:2447`, `include/aurora/core/timebase.hpp:106`
- Issue:
  - Parser accepts any numeric `globals.sr`.
  - Validator does not bound-check sample rate.
  - Renderer uses `globals.sr` when no positive CLI override is given.
  - Negative/zero rates flow into time/sample conversions and buffer sizing.
- Impact:
  - Potential huge allocation requests, invalid timing math, or render instability from malformed `.au` input.
- Recommendation:
  - Add validation: require `sr` > 0 and within allowed set/range (for example `44100|48000|96000`, or bounded positive range).
  - Fail validation before rendering.
  - Add negative test fixture (`globals { sr: -1 }`) expecting validation failure.

### 4) CLI numeric argument parsing can terminate process on invalid input
- Severity: High
- Location: `src/aurora_cli/main.cpp:44`, `src/aurora_cli/main.cpp:52`
- Issue:
  - `std::stoull` / `std::stoi` are called without exception handling.
  - Non-numeric values (`--seed abc`, `--sr nope`) throw and can crash the process.
- Impact:
  - Poor CLI robustness and user-facing crash behavior instead of controlled argument error.
- Recommendation:
  - Wrap numeric parsing in `try/catch` and return `"Argument error: ..."` consistently.
  - Add CLI tests for invalid `--seed` / `--sr` values.

### 5) WAV writer does not verify interleaved frame alignment
- Severity: Medium
- Location: `src/aurora_io/wav_writer.cpp:49`, `src/aurora_io/wav_writer.cpp:72`
- Issue:
  - `num_frames` is computed via integer division of `samples.size() / channels` with no remainder check.
  - If malformed data reaches writer, trailing samples are silently dropped.
- Impact:
  - Silent data truncation and difficult debugging when upstream buffers are inconsistent.
- Recommendation:
  - Reject write when `samples.size() % channels != 0` and return explicit error.
  - Add unit test that intentionally passes malformed interleaved sample count.

### 6) MIDI writer uses unchecked `uint32_t` tick conversion for long timelines
- Severity: Medium
- Location: `src/aurora_io/midi_writer.cpp:61`, `src/aurora_io/midi_writer.cpp:115`, `src/aurora_io/midi_writer.cpp:122`
- Issue:
  - Tick calculations cast to `uint32_t` without overflow guard.
  - Very long renders/tempo maps can exceed representable tick range.
- Impact:
  - Tick wraparound leads to invalid ordering or corrupted MIDI timing.
- Recommendation:
  - Clamp and error when computed ticks exceed `uint32_t` max.
  - Add a long-duration stress test for MIDI export path.

### 7) Parser exceptions frequently lose source location fidelity
- Severity: Medium
- Location: `src/aurora_lang/parser.cpp:544`, `src/aurora_lang/parser.cpp:564`, `src/aurora_lang/parser.cpp:1098`, `src/aurora_lang/parser.cpp:1103`
- Issue:
  - Several parse-time failures use `std::runtime_error` (no token location).
  - Catch-all path reports `1:1`, hiding true error location.
- Impact:
  - Slower debugging for invalid `.au` sources and lower DX quality.
- Recommendation:
  - Replace runtime errors in parse-path with `ParseException` using active token line/column.
  - Keep `1:1` only for truly locationless fatal errors.

### 8) Empty-but-readable `.au` is reported as read failure instead of parse failure
- Severity: Low
- Location: `src/aurora_cli/main.cpp:125`, `src/aurora_cli/main.cpp:126`
- Issue:
  - `ReadFile` returns empty string for both "cannot open" and "empty file".
  - CLI treats both as read failure.
- Impact:
  - Misleading error reporting; empty file should ideally surface parse/validation diagnostics.
- Recommendation:
  - Differentiate file open/read failure from empty contents.
  - Allow parser to handle empty source and report proper syntax/header error.

## Testing Gaps
- No automated tests currently cover:
  - Invalid CLI numeric arguments (`--seed`, `--sr`) error handling.
  - Sample-rate validation failures (`globals.sr <= 0`).
  - Async output correctness across multiple stems under concurrent writes.
  - MIDI overflow boundaries for very long renders.
  - Malformed interleaved audio buffer rejection in WAV writer.

## Improvement Opportunities (Non-blocking)
- `WriteRenderJson` metadata is minimal (`src/aurora_io/json_writer.cpp:54` onward).
  - Add per-stem channels, frame counts, and peak/RMS for diagnostics and regression tooling.
- Validation could be stricter for graph endpoint port names.
  - Current port classification is heuristic (`src/aurora_lang/validation.cpp:41`) and may miss certain schema errors.

## Recommended Fix Order
1. Fix both async capture lifetime bugs in CLI and renderer.
2. Add `globals.sr` validation and invalid CLI parse handling.
3. Harden WAV/MIDI writers with structural bounds checks.
4. Improve parser diagnostic precision and add targeted tests for each fix.
