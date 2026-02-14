#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
AURORA_BIN="$ROOT_DIR/build/src/aurora_cli/aurora"
OUT_ROOT="$ROOT_DIR/out/m2_test_runs"

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
  echo "[M2] render: $file"
  "$AURORA_BIN" render "$ROOT_DIR/$file" --out "$out" >/tmp/m2_${name}.log 2>&1
}

rm -rf "$OUT_ROOT"
mkdir -p "$OUT_ROOT"

# M2 feature coverage.
render_ok "env_modes" "tests/m2_env_modes.au"
render_ok "mono_legato" "tests/m2_mono_legato.au"
render_ok "mono_priority" "tests/m2_mono_priority.au"
render_ok "retrig_never" "tests/m2_retrig_never.au"

# Cross-phase regression guard (M1 still good).
render_ok "m1_regression" "tests/m1_cv_nodes_trigger.au"

# Determinism on mono/legato path.
DET_A="$OUT_ROOT/determinism_a"
DET_B="$OUT_ROOT/determinism_b"
SEED=99173
"$AURORA_BIN" render "$ROOT_DIR/tests/m2_mono_legato.au" --seed "$SEED" --out "$DET_A" >/tmp/m2_det_a.log 2>&1
"$AURORA_BIN" render "$ROOT_DIR/tests/m2_mono_legato.au" --seed "$SEED" --out "$DET_B" >/tmp/m2_det_b.log 2>&1

HASH_A="$(hash_file "$DET_A/mix/master.wav")"
HASH_B="$(hash_file "$DET_B/mix/master.wav")"
if [[ "$HASH_A" != "$HASH_B" ]]; then
  echo "error: M2 determinism hash mismatch"
  echo "a=$HASH_A"
  echo "b=$HASH_B"
  exit 1
fi

echo "[M2] all tests passed"
echo "[M2] determinism hash: $HASH_A"
