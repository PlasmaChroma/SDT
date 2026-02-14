#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
AURORA_BIN="$ROOT_DIR/build/src/aurora_cli/aurora"
OUT_ROOT="$ROOT_DIR/out/m3_test_runs"

if [[ ! -x "$AURORA_BIN" ]]; then
  echo "error: missing binary at $AURORA_BIN"
  echo "build first: cmake --build build -j4"
  exit 1
fi

hash_file() {
  local f="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$f" | awk '{print $1}'
  else
    shasum -a 256 "$f" | awk '{print $1}'
  fi
}

render_ok() {
  local name="$1"
  local file="$2"
  local out="$OUT_ROOT/$name"
  echo "[M3] render: $file"
  "$AURORA_BIN" render "$ROOT_DIR/$file" --out "$out" >/tmp/m3_${name}.log 2>&1
}

rm -rf "$OUT_ROOT"
mkdir -p "$OUT_ROOT"

# M3 feature coverage.
render_ok "rate_modes" "tests/m3_rate_modes.au"
render_ok "control_feedback" "tests/m3_control_feedback.au"

if ! rg -q "control feedback cycle" /tmp/m3_control_feedback.log; then
  echo "error: expected control feedback cycle warning was not emitted"
  cat /tmp/m3_control_feedback.log
  exit 1
fi

# Cross-phase guard.
render_ok "m2_regression" "tests/m2_mono_legato.au"

# Determinism check on rate-mode graph.
DET_A="$OUT_ROOT/determinism_a"
DET_B="$OUT_ROOT/determinism_b"
SEED=77031
"$AURORA_BIN" render "$ROOT_DIR/tests/m3_rate_modes.au" --seed "$SEED" --out "$DET_A" >/tmp/m3_det_a.log 2>&1
"$AURORA_BIN" render "$ROOT_DIR/tests/m3_rate_modes.au" --seed "$SEED" --out "$DET_B" >/tmp/m3_det_b.log 2>&1

HASH_A="$(hash_file "$DET_A/mix/master.wav")"
HASH_B="$(hash_file "$DET_B/mix/master.wav")"
if [[ "$HASH_A" != "$HASH_B" ]]; then
  echo "error: M3 determinism hash mismatch"
  echo "a=$HASH_A"
  echo "b=$HASH_B"
  exit 1
fi

echo "[M3] all tests passed"
echo "[M3] determinism hash: $HASH_A"
