#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
AURORA_BIN="$ROOT_DIR/build/src/aurora_cli/aurora"
OUT_ROOT="$ROOT_DIR/out/m1_test_runs"

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
  echo "[M1] render: $file"
  "$AURORA_BIN" render "$ROOT_DIR/$file" --out "$out" >/tmp/m1_${name}.log 2>&1
}

expect_fail() {
  local name="$1"
  local file="$2"
  local pattern="$3"
  local out="$OUT_ROOT/$name"
  echo "[M1] expect fail: $file"
  set +e
  "$AURORA_BIN" render "$ROOT_DIR/$file" --out "$out" >/tmp/m1_${name}.log 2>&1
  local code=$?
  set -e
  if [[ $code -eq 0 ]]; then
    echo "error: expected failure but succeeded: $file"
    cat "/tmp/m1_${name}.log"
    exit 1
  fi
  if ! rg -q "$pattern" "/tmp/m1_${name}.log"; then
    echo "error: failure message mismatch for $file"
    cat "/tmp/m1_${name}.log"
    exit 1
  fi
}

rm -rf "$OUT_ROOT"
mkdir -p "$OUT_ROOT"

# Positive coverage: trigger/gate, cv nodes, clip+curve, map invert/bias.
render_ok "cv_nodes_trigger" "tests/m1_cv_nodes_trigger.au"
render_ok "trigger_defaults" "tests/m1_trigger_defaults.au"
render_ok "cv_chain" "tests/m1_cv_chain.au"
render_ok "cv_clip_curve" "tests/m1_cv_clip_curve.au"

# Negative validation coverage: port typing legality.
expect_fail "invalid_control_to_audio" "tests/m1_invalid_control_to_audio.au" "control source cannot drive audio input"
expect_fail "invalid_audio_to_control" "tests/m1_invalid_audio_to_control.au" "audio source cannot drive control input"

# Determinism check: same source/seed => identical audio hash.
DET_A="$OUT_ROOT/determinism_a"
DET_B="$OUT_ROOT/determinism_b"
SEED=424242
"$AURORA_BIN" render "$ROOT_DIR/tests/m1_cv_nodes_trigger.au" --seed "$SEED" --out "$DET_A" >/tmp/m1_det_a.log 2>&1
"$AURORA_BIN" render "$ROOT_DIR/tests/m1_cv_nodes_trigger.au" --seed "$SEED" --out "$DET_B" >/tmp/m1_det_b.log 2>&1

HASH_A="$(hash_file "$DET_A/mix/master.wav")"
HASH_B="$(hash_file "$DET_B/mix/master.wav")"
if [[ "$HASH_A" != "$HASH_B" ]]; then
  echo "error: determinism hash mismatch"
  echo "a=$HASH_A"
  echo "b=$HASH_B"
  exit 1
fi

echo "[M1] all tests passed"
echo "[M1] determinism hash: $HASH_A"
